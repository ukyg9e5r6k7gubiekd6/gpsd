/* libgps_sock.c -- client interface library for the gpsd daemon
 *
 * This file is Copyright (c) 2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <fcntl.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <locale.h>
#include <assert.h>
#include <sys/time.h>	 /* expected to have a select(2) prototype a la SuS */
#include <sys/types.h>
#include <sys/stat.h>
#ifndef S_SPLINT_S
#include <sys/socket.h>
#include <unistd.h>
#endif /* S_SPLINT_S */

#ifndef USE_QT
#ifndef S_SPLINT_S
#include <sys/socket.h>
#endif /* S_SPLINT_S */
#else
#include <QTcpSocket>
#endif /* USE_QT */

#include "gps.h"
#include "gpsd.h"
#include "libgps.h"
#ifdef SOCKET_EXPORT_ENABLE
#include "gps_json.h"

#ifdef S_SPLINT_S
extern char *strtok_r(char *, const char *, char **);
#endif /* S_SPLINT_S */

/*@-matchfields@*/
struct privdata_t
{
    bool newstyle;
    /* data buffered from the last read */
    ssize_t waiting;
    char buffer[GPS_JSON_RESPONSE_MAX * 2];
#ifdef LIBGPS_DEBUG
    int waitcount;
#endif /* LIBGPS_DEBUG */
};
/*@+matchfields@*/

/*@-branchstate@*/
int gps_sock_open(/*@null@*/const char *host, /*@null@*/const char *port,
		  /*@out@*/ struct gps_data_t *gpsdata)
{
    if (!host)
	host = "localhost";
    if (!port)
	port = DEFAULT_GPSD_PORT;

    libgps_debug_trace((DEBUG_CALLS, "gps_sock_open(%s, %s)\n", host, port));

#ifndef USE_QT
	if ((gpsdata->gps_fd =
	    netlib_connectsock(AF_UNSPEC, host, port, "tcp")) < 0) {
	    errno = gpsdata->gps_fd;
	    libgps_debug_trace((DEBUG_CALLS, "netlib_connectsock() returns error %d\n", errno));
	    return -1;
        }
	else
	    libgps_debug_trace((DEBUG_CALLS, "netlib_connectsock() returns socket on fd %d\n", gpsdata->gps_fd));
#else
	QTcpSocket *sock = new QTcpSocket();
	gpsdata->gps_fd = sock;
	sock->connectToHost(host, QString(port).toInt());
	if (!sock->waitForConnected())
	    qDebug() << "libgps::connect error: " << sock->errorString();
	else
	    qDebug() << "libgps::connected!";
#endif /* USE_QT */

    /* set up for line-buffered I/O over the daemon socket */
    gpsdata->privdata = (void *)malloc(sizeof(struct privdata_t));
    if (gpsdata->privdata == NULL)
	return -1;
    PRIVATE(gpsdata)->newstyle = false;
    PRIVATE(gpsdata)->waiting = 0;

#ifdef LIBGPS_DEBUG
    PRIVATE(gpsdata)->waitcount = 0;
#endif /* LIBGPS_DEBUG */
    return 0;
}
/*@+branchstate@*/

bool gps_sock_waiting(const struct gps_data_t *gpsdata, int timeout)
/* is there input waiting from the GPS? */
{
#ifndef USE_QT
    fd_set rfds;
    struct timeval tv;

    libgps_debug_trace((DEBUG_CALLS, "gps_waiting(%d): %d\n", timeout, PRIVATE(gpsdata)->waitcount++));
    if (PRIVATE(gpsdata)->waiting > 0)
	return true;

    /* we might want to check for EINTR if this returns false */
    errno = 0;

    FD_ZERO(&rfds);
    FD_SET(gpsdata->gps_fd, &rfds);
    tv.tv_sec = timeout / 1000000;
    tv.tv_usec = timeout % 1000000;
    /* all error conditions return "not waiting" -- crude but effective */
    return (select(gpsdata->gps_fd + 1, &rfds, NULL, NULL, &tv) == 1);
#else
    return ((QTcpSocket *) (gpsdata->gps_fd))->waitForReadyRead(timeout / 1000);
#endif
}

/*@-usereleased -compdef@*/
int gps_sock_close(struct gps_data_t *gpsdata)
/* close a gpsd connection */
{
#ifndef USE_QT
    int status;

    free(PRIVATE(gpsdata));
    status = close(gpsdata->gps_fd);
    gpsdata->gps_fd = -1;
    return status;
#else
    QTcpSocket *sock = (QTcpSocket *) gpsdata->gps_fd;
    sock->disconnectFromHost();
    delete sock;
    gpsdata->gps_fd = NULL;
    return 0;
#endif
}
/*@+usereleased +compdef@*/

/*@-compdef -usedef -uniondef@*/
int gps_sock_read(/*@out@*/struct gps_data_t *gpsdata)
/* wait for and read data being streamed from the daemon */
{
    char *eol;
    ssize_t response_length;
    int status = -1;

    gpsdata->set &= ~PACKET_SET;
    for (eol = PRIVATE(gpsdata)->buffer;
	 *eol != '\n' && eol < PRIVATE(gpsdata)->buffer + PRIVATE(gpsdata)->waiting; eol++)
	continue;
    if (*eol != '\n')
	eol = NULL;

    errno = 0;

    if (eol == NULL) {
#ifndef USE_QT
	/* read data: return -1 if no data waiting or buffered, 0 otherwise */
	status = (int)recv(gpsdata->gps_fd,
			   PRIVATE(gpsdata)->buffer + PRIVATE(gpsdata)->waiting,
			   sizeof(PRIVATE(gpsdata)->buffer) - PRIVATE(gpsdata)->waiting, 0);
#else
	status =
	    ((QTcpSocket *) (gpsdata->gps_fd))->read(PRIVATE(gpsdata)->buffer +
						     PRIVATE(gpsdata)->waiting,
						     sizeof(PRIVATE(gpsdata)->buffer) -
						     PRIVATE(gpsdata)->waiting);
#endif

	/* if we just received data from the socket, it's in the buffer */
	if (status > -1)
	    PRIVATE(gpsdata)->waiting += status;
	/* buffer is empty - implies no data was read */
	if (PRIVATE(gpsdata)->waiting == 0) {
	    /* 
	     * If we received 0 bytes, other side of socket is closing.
	     * Return -1 as end-of-data indication.
	     */
	    if (status == 0)
		return -1;
#ifndef USE_QT
	    /* count transient errors as success, we'll retry later */
	    else if (errno == EINTR || errno == EAGAIN
		     || errno == EWOULDBLOCK)
		return 0;
#endif
	    /* hard error return of -1, pass it along */
	    else
		return -1;
	}
	/* there's buffered data waiting to be returned */
	for (eol = PRIVATE(gpsdata)->buffer;
	     *eol != '\n' && eol < PRIVATE(gpsdata)->buffer + PRIVATE(gpsdata)->waiting; eol++)
	    continue;
	if (*eol != '\n')
	    eol = NULL;
	if (eol == NULL)
	    return 0;
    }

    assert(eol != NULL);
    *eol = '\0';
    response_length = eol - PRIVATE(gpsdata)->buffer + 1;
    gpsdata->online = timestamp();
    status = gps_unpack(PRIVATE(gpsdata)->buffer, gpsdata);
    /*@+matchanyintegral@*/
    memmove(PRIVATE(gpsdata)->buffer,
	    PRIVATE(gpsdata)->buffer + response_length, PRIVATE(gpsdata)->waiting - response_length);
    /*@-matchanyintegral@*/
    PRIVATE(gpsdata)->waiting -= response_length;
    gpsdata->set |= PACKET_SET;

    return (status == 0) ? (int)response_length : status;
}
/*@+compdef -usedef +uniondef@*/

/*@ -branchstate -usereleased -mustfreefresh -nullstate -usedef @*/
int gps_unpack(char *buf, struct gps_data_t *gpsdata)
/* unpack a gpsd response into a status structure, buf must be writeable.
 * gps_unpack() currently returns 0 in all cases, but should it ever need to
 * return an error status, it must be < 0.
 */
{
    libgps_debug_trace((DEBUG_CALLS, "gps_unpack(%s)\n", buf));

    /* detect and process a JSON response */
    if (buf[0] == '{') {
	const char *jp = buf, **next = &jp;
	while (next != NULL && *next != NULL && next[0][0] != '\0') {
	    libgps_debug_trace((DEBUG_CALLS,"gps_unpack() segment parse '%s'\n", *next));
	    if (libgps_json_unpack(*next, gpsdata, next) == -1)
		break;
#ifdef LIBGPS_DEBUG
	    if (libgps_debuglevel >= 1)
		libgps_dump_state(gpsdata);
#endif /* LIBGPS_DEBUG */

	}
#ifdef OLDSTYLE_ENABLE
	if (PRIVATE(gpsdata) != NULL)
	    PRIVATE(gpsdata)->newstyle = true;
#endif /* OLDSTYLE_ENABLE */
    }
#ifdef OLDSTYLE_ENABLE
    else {
	/*
	 * Get the decimal separator for the current application locale.
	 * This looks thread-unsafe, but it's not.  The key is that
	 * character assignment is atomic.
	 */
	char *ns, *sp, *tp;

#ifdef __UNUSED__
	static char decimal_point = '\0';
	if (decimal_point == '\0') {
	    struct lconv *locale_data = localeconv();
	    if (locale_data != NULL && locale_data->decimal_point[0] != '.')
		decimal_point = locale_data->decimal_point[0];
	}
#endif /* __UNUSED__ */

	for (ns = buf; ns; ns = strstr(ns + 1, "GPSD")) {
	    if ( /*@i1@*/ strncmp(ns, "GPSD", 4) == 0) {
		bool eol = false;
		/* the following should execute each time we have a good next sp */
		for (sp = ns + 5; *sp != '\0'; sp = tp + 1) {
		    tp = sp + strcspn(sp, ",\r\n");
		    eol = *tp == '\r' || *tp == '\n';
		    if (*tp == '\0')
			tp--;
		    else
			*tp = '\0';

#ifdef __UNUSED__
		    /*
		     * The daemon always emits the Anglo-American and SI
		     * decimal point.  Hack these into whatever the
		     * application locale requires if it's not the same.
		     * This has to happen *after* we grab the next
		     * comma-delimited response, or we'll lose horribly
		     * in locales where the decimal separator is comma.
		     */
		    if (decimal_point != '\0') {
			char *cp;
			for (cp = sp; cp < tp; cp++)
			    if (*cp == '.')
				*cp = decimal_point;
		    }
#endif /* __UNUSED__ */

		    /* note, there's a bit of skip logic after the switch */

		    switch (*sp) {
		    case 'F':	/*@ -mustfreeonly */
			if (sp[2] == '?')
			    gpsdata->dev.path[0] = '\0';
			else {
			    /*@ -mayaliasunique @*/
			    (void)strlcpy(gpsdata->dev.path, sp + 2,
				    sizeof(gpsdata->dev.path));
			    /*@ +mayaliasunique @*/
			    gpsdata->set |= DEVICE_SET;
			}
			/*@ +mustfreeonly */
			break;
		    case 'I':
			/*@ -mustfreeonly */
			if (sp[2] == '?')
			    gpsdata->dev.subtype[0] = '\0';
			else {
			    (void)strlcpy(gpsdata->dev.subtype, sp + 2,
					  sizeof(gpsdata->dev.subtype));
			    gpsdata->set |= DEVICEID_SET;
			}
			/*@ +mustfreeonly */
			break;
		    case 'O':
			if (sp[2] == '?') {
			    gpsdata->set = MODE_SET | STATUS_SET;
			    gpsdata->status = STATUS_NO_FIX;
			    gps_clear_fix(&gpsdata->fix);
			} else {
			    struct gps_fix_t nf;
			    char tag[MAXTAGLEN + 1], alt[20];
			    char eph[20], epv[20], track[20], speed[20],
				climb[20];
			    char epd[20], eps[20], epc[20], mode[2];
			    char timestr[20], ept[20], lat[20], lon[20];
			    int st = sscanf(sp + 2,
					    "%8s %19s %19s %19s %19s %19s %19s %19s %19s %19s %19s %19s %19s %19s %1s",
					    tag, timestr, ept, lat, lon,
					    alt, eph, epv, track, speed,
					    climb,
					    epd, eps, epc, mode);
			    if (st >= 14) {
#define DEFAULT(val) (val[0] == '?') ? NAN : safe_atof(val)
				/*@ +floatdouble @*/
				nf.time = DEFAULT(timestr);
				nf.latitude = DEFAULT(lat);
				nf.longitude = DEFAULT(lon);
				nf.ept = DEFAULT(ept);
				nf.altitude = DEFAULT(alt);
				/* designed before we split eph into epx+epy */
				nf.epx = nf.epy = DEFAULT(eph) / sqrt(2);
				nf.epv = DEFAULT(epv);
				nf.track = DEFAULT(track);
				nf.speed = DEFAULT(speed);
				nf.climb = DEFAULT(climb);
				nf.epd = DEFAULT(epd);
				nf.eps = DEFAULT(eps);
				nf.epc = DEFAULT(epc);
				/*@ -floatdouble @*/
#undef DEFAULT
				if (st >= 15)
				    nf.mode =
					(mode[0] ==
					 '?') ? MODE_NOT_SEEN : atoi(mode);
				else
				    nf.mode =
					(alt[0] == '?') ? MODE_2D : MODE_3D;
				if (alt[0] != '?')
				    gpsdata->set |= ALTITUDE_SET | CLIMB_SET;
				if (isnan(nf.epx) == 0 && isnan(nf.epy) == 0)
				    gpsdata->set |= HERR_SET;
				if (isnan(nf.epv) == 0)
				    gpsdata->set |= VERR_SET;
				if (isnan(nf.track) == 0)
				    gpsdata->set |= TRACK_SET | SPEED_SET;
				if (isnan(nf.eps) == 0)
				    gpsdata->set |= SPEEDERR_SET;
				if (isnan(nf.epc) == 0)
				    gpsdata->set |= CLIMBERR_SET;
				gpsdata->fix = nf;
				(void)strlcpy(gpsdata->tag, tag,
					      MAXTAGLEN + 1);
				gpsdata->set |=
				    TIME_SET | TIMERR_SET | LATLON_SET |
				    MODE_SET;
				gpsdata->status = STATUS_FIX;
				gpsdata->set |= STATUS_SET;
			    }
			}
			break;
		    case 'X':
			if (sp[2] == '?')
			    gpsdata->online = (timestamp_t)-1;
			else {
			    (void)sscanf(sp, "X=%lf", &gpsdata->online);
			    gpsdata->set |= ONLINE_SET;
			}
			break;
		    case 'Y':
			if (sp[2] == '?') {
			    gpsdata->satellites_visible = 0;
			} else {
			    int j, i1, i2, i3, i5;
			    int PRN[MAXCHANNELS];
			    int elevation[MAXCHANNELS], azimuth[MAXCHANNELS];
			    int used[MAXCHANNELS];
			    double ss[MAXCHANNELS], f4;
			    char tag[MAXTAGLEN + 1], timestamp[21];

			    (void)sscanf(sp, "Y=%8s %20s %d ",
					 tag, timestamp,
					 &gpsdata->satellites_visible);
			    (void)strlcpy(gpsdata->tag, tag, MAXTAGLEN);
			    if (timestamp[0] != '?') {
				gpsdata->set |= TIME_SET;
			    }
			    for (j = 0; j < gpsdata->satellites_visible; j++) {
				PRN[j] = elevation[j] = azimuth[j] = used[j] =
				    0;
				ss[j] = 0.0;
			    }
			    for (j = 0, gpsdata->satellites_used = 0;
				 j < gpsdata->satellites_visible; j++) {
				if ((sp != NULL)
				    && ((sp = strchr(sp, ':')) != NULL)) {
				    sp++;
				    (void)sscanf(sp, "%d %d %d %lf %d", &i1,
						 &i2, &i3, &f4, &i5);
				    PRN[j] = i1;
				    elevation[j] = i2;
				    azimuth[j] = i3;
				    ss[j] = f4;
				    used[j] = i5;
				    if (i5 == 1)
					gpsdata->satellites_used++;
				}
			    }
			    /*@ -compdef @*/
			    memcpy(gpsdata->PRN, PRN, sizeof(PRN));
			    memcpy(gpsdata->elevation, elevation,
				   sizeof(elevation));
			    memcpy(gpsdata->azimuth, azimuth,
				   sizeof(azimuth));
			    memcpy(gpsdata->ss, ss, sizeof(ss));
			    memcpy(gpsdata->used, used, sizeof(used));
			    /*@ +compdef @*/
			}
			gpsdata->set |= SATELLITE_SET;
			break;
		    }

#ifdef LIBGPS_DEBUG
		    if (libgps_debuglevel >= 1)
			libgps_dump_state(gpsdata);
#endif /* LIBGPS_DEBUG */

		    /*
		     * Skip to next GPSD when we see \r or \n;
		     * we don't want to try interpreting stuff
		     * in between that might be raw mode data.
		     */
		    if (eol)
			break;
		}
	    }
	}
    }
#endif /* OLDSTYLE_ENABLE */

#ifndef USE_QT
    libgps_debug_trace((DEBUG_CALLS, "final flags: (0x%04x) %s\n", gpsdata->set,gps_maskdump(gpsdata->set)));
#endif
    return 0;
}
/*@ +compdef @*/

const char /*@observer@*/ *gps_sock_data(const struct gps_data_t *gpsdata)
/* return the contents of the client data buffer */
{
    return PRIVATE(gpsdata)->buffer;
}

int gps_sock_send(struct gps_data_t *gpsdata, const char *buf)
/* send a command to the gpsd instance */
{
#ifndef USE_QT
    if (write(gpsdata->gps_fd, buf, strlen(buf)) == (ssize_t) strlen(buf))
	return 0;
    else
	return -1;
#else
    QTcpSocket *sock = (QTcpSocket *) gpsdata->gps_fd;
    sock->write(buf, strlen(buf));
    if (sock->waitForBytesWritten())
	return 0;
    else {
	qDebug() << "libgps::send error: " << sock->errorString();
	return -1;
    }
#endif
}

int gps_sock_stream(struct gps_data_t *gpsdata, unsigned int flags,
	       /*@null@*/ void *d)
/* ask gpsd to stream reports at you, hiding the command details */
{
    char buf[GPS_JSON_COMMAND_MAX];

    if ((flags & (WATCH_JSON | WATCH_OLDSTYLE | WATCH_NMEA | WATCH_RAW)) == 0) {
	flags |= WATCH_JSON;
    }
    if ((flags & WATCH_DISABLE) != 0) {
	if ((flags & WATCH_OLDSTYLE) != 0) {
	    (void)strlcpy(buf, "w-", sizeof(buf));
	    if ((flags & WATCH_NMEA) != 0)
		(void)strlcat(buf, "r-", sizeof(buf));
	} else {
	    (void)strlcpy(buf, "?WATCH={\"enable\":false,", sizeof(buf));
	    if (flags & WATCH_JSON)
		(void)strlcat(buf, "\"json\":false,", sizeof(buf));
	    if (flags & WATCH_NMEA)
		(void)strlcat(buf, "\"nmea\":false,", sizeof(buf));
	    if (flags & WATCH_RAW)
		(void)strlcat(buf, "\"raw\":1,", sizeof(buf));
	    if (flags & WATCH_RARE)
		(void)strlcat(buf, "\"raw\":0,", sizeof(buf));
	    if (flags & WATCH_SCALED)
		(void)strlcat(buf, "\"scaled\":false,", sizeof(buf));
	    if (flags & WATCH_TIMING)
		(void)strlcat(buf, "\"timing\":false,", sizeof(buf));
	    if (buf[strlen(buf) - 1] == ',')
		buf[strlen(buf) - 1] = '\0';
	    (void)strlcat(buf, "};", sizeof(buf));
	}
	libgps_debug_trace((DEBUG_CALLS, "gps_stream() disable command: %s\n", buf));
	return gps_send(gpsdata, buf);
    } else {			/* if ((flags & WATCH_ENABLE) != 0) */

	if ((flags & WATCH_OLDSTYLE) != 0) {
	    (void)strlcpy(buf, "w+x", sizeof(buf));
	    if ((flags & WATCH_NMEA) != 0)
		(void)strlcat(buf, "r+", sizeof(buf));
	} else {
	    (void)strlcpy(buf, "?WATCH={\"enable\":true,", sizeof(buf));
	    if (flags & WATCH_JSON)
		(void)strlcat(buf, "\"json\":true,", sizeof(buf));
	    if (flags & WATCH_NMEA)
		(void)strlcat(buf, "\"nmea\":true,", sizeof(buf));
	    if (flags & WATCH_RARE)
		(void)strlcat(buf, "\"raw\":1,", sizeof(buf));
	    if (flags & WATCH_RAW)
		(void)strlcat(buf, "\"raw\":2,", sizeof(buf));
	    if (flags & WATCH_SCALED)
		(void)strlcat(buf, "\"scaled\":true,", sizeof(buf));
	    if (flags & WATCH_TIMING)
		(void)strlcat(buf, "\"timing\":true,", sizeof(buf));
	    /*@-nullpass@*//* shouldn't be needed, splint has a bug */
	    if (flags & WATCH_DEVICE)
		(void)snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
			       "\"device\":\"%s\",", (char *)d);
	    /*@+nullpass@*/
	    if (buf[strlen(buf) - 1] == ',')
		buf[strlen(buf) - 1] = '\0';
	    (void)strlcat(buf, "};", sizeof(buf));
	}
	libgps_debug_trace((DEBUG_CALLS, "gps_stream() enable command: %s\n", buf));
	return gps_send(gpsdata, buf);
    }
}

int gps_sock_mainloop(struct gps_data_t *gpsdata, int timeout, 
			 void (*hook)(struct gps_data_t *gpsdata))
/* run a socket main loop with a specified handler */
{
    for (;;) {
	if (!gps_waiting(gpsdata, timeout)) {
	    return -1;
	} else {
	    (void)gps_read(gpsdata);
	    (*hook)(gpsdata);
	}
    }
    //return 0;
}

#endif /* SOCKET_EXPORT_ENABLE */

/* end */
