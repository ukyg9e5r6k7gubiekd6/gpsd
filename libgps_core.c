/* libgps.c -- client interface library for the gpsd daemon
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
#include <stdarg.h>
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

#include "gpsd.h"
#include "gps_json.h"

#ifdef S_SPLINT_S
extern char *strtok_r(char *, const char *, char **);
#endif /* S_SPLINT_S */

#if defined(TESTMAIN) || defined(CLIENTDEBUG_ENABLE)
#define LIBGPS_DEBUG
#endif /* defined(TESTMAIN) || defined(CLIENTDEBUG_ENABLE) */

struct privdata_t
{
    bool newstyle;
    /* data buffered from the last read */
    ssize_t waiting;
    char buffer[GPS_JSON_RESPONSE_MAX * 2];

};
#define PRIVATE(gpsdata) ((struct privdata_t *)gpsdata->privdata)

#ifdef LIBGPS_DEBUG
#define DEBUG_CALLS	1	/* shallowest debug level */
#define DEBUG_JSON	5	/* minimum level for verbose JSON debugging */
static int debuglevel = 0;
static FILE *debugfp;

void gps_enable_debug(int level, FILE * fp)
/* control the level and destination of debug trace messages */
{
    debuglevel = level;
    debugfp = fp;
#ifdef CLIENTDEBUG_ENABLE
    json_enable_debug(level - DEBUG_JSON, fp);
#endif
}

static void gps_trace(int errlevel, const char *fmt, ...)
/* assemble command in printf(3) style */
{
    if (errlevel <= debuglevel) {
	char buf[BUFSIZ];
	va_list ap;

	(void)strlcpy(buf, "libgps: ", BUFSIZ);
	va_start(ap, fmt);
	(void)vsnprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), fmt,
			ap);
	va_end(ap);

	(void)fputs(buf, debugfp);
    }
}

# define libgps_debug_trace(args) (void) gps_trace args
#else
# define libgps_debug_trace(args) /*@i1@*/do { } while (0)
#endif /* LIBGPS_DEBUG */

/*@-nullderef@*/
int gps_open(/*@null@*/const char *host, /*@null@*/const char *port,
	       /*@out@*/ struct gps_data_t *gpsdata)
{
    /*@ -branchstate @*/
    if (!gpsdata)
	return -1;
    if (!host)
	host = "localhost";
    if (!port)
	port = DEFAULT_GPSD_PORT;

    libgps_debug_trace((DEBUG_CALLS, "gps_open(%s, %s)\n", host, port));

#ifndef USE_QT
    if ((gpsdata->gps_fd =
	 netlib_connectsock(AF_UNSPEC, host, port, "tcp")) < 0) {
	errno = gpsdata->gps_fd;
	libgps_debug_trace((DEBUG_CALLS, "netlib_connectsock() returns error %d\n", errno));
	return -1;
    }
    else
	libgps_debug_trace((DEBUG_CALLS, "netlib_connectsock() returns socket on fd %d\n",
			    gpsdata->gps_fd));
#else
    QTcpSocket *sock = new QTcpSocket();
    gpsdata->gps_fd = sock;
    sock->connectToHost(host, QString(port).toInt());
    if (!sock->waitForConnected())
	qDebug() << "libgps::connect error: " << sock->errorString();
    else
	qDebug() << "libgps::connected!";
#endif

    gpsdata->set = 0;
    gpsdata->status = STATUS_NO_FIX;
    gps_clear_fix(&gpsdata->fix);

    /* set up for line-buffered I/O over the daemon socket */
    gpsdata->privdata = (void *)malloc(sizeof(struct privdata_t));
    if (gpsdata->privdata == NULL)
	return -1;
    PRIVATE(gpsdata)->newstyle = false;
    PRIVATE(gpsdata)->waiting = 0;
    return 0;
    /*@ +branchstate @*/
}

/*@-compdef -usereleased@*/
int gps_close(struct gps_data_t *gpsdata)
/* close a gpsd connection */
{
    libgps_debug_trace((DEBUG_CALLS, "gps_close()\n"));
#ifndef USE_QT
    free(PRIVATE(gpsdata));
    (void)close(gpsdata->gps_fd);
    gpsdata->gps_fd = -1;
#else
    QTcpSocket *sock = (QTcpSocket *) gpsdata->gps_fd;
    sock->disconnectFromHost();
    delete sock;
    gpsdata->gps_fd = NULL;
#endif

    return 0;
}
/*@+compdef +usereleased@*/

/*@-compdef -usedef -uniondef@*/
int gps_read(/*@out@*/struct gps_data_t *gpsdata)
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

extern const char /*@observer@*/ *gps_errstr(const int err)
{
    /* 
     * We might add our own error codes in the future, e.g for
     * protocol compatibility checks
     */
#ifndef USE_QT
    return netlib_errstr(err);
#else
    static char buf[32];
    (void)snprintf(buf, sizeof(buf), "Qt error %d", err);
    return buf;
#endif
}

#ifdef TESTMAIN
/*
 * A simple command-line exerciser for the library.
 * Not really useful for anything but debugging.
 */

#ifndef S_SPLINT_S
#include <unistd.h>
#endif /* S_SPLINT_S */
#include <getopt.h>
#include <signal.h>

static void libgps_dump_state(struct gps_data_t *collect)
{
    const char *status_values[] = { "NO_FIX", "FIX", "DGPS_FIX" };
    const char *mode_values[] = { "", "NO_FIX", "MODE_2D", "MODE_3D" };

    /* no need to dump the entire state, this is a sanity check */
#ifndef USE_QT
    /* will fail on a 32-bit macine */
    (void)fprintf(debugfp, "flags: (0x%04x) %s\n",
		  (unsigned int)collect->set, gps_maskdump(collect->set));
#endif
    if (collect->set & ONLINE_SET)
	(void)fprintf(debugfp, "ONLINE: %lf\n", collect->online);
    if (collect->set & TIME_SET)
	(void)fprintf(debugfp, "TIME: %lf\n", collect->fix.time);
    if (collect->set & LATLON_SET)
	(void)fprintf(debugfp, "LATLON: lat/lon: %lf %lf\n",
		      collect->fix.latitude, collect->fix.longitude);
    if (collect->set & ALTITUDE_SET)
	(void)fprintf(debugfp, "ALTITUDE: altitude: %lf  U: climb: %lf\n",
		      collect->fix.altitude, collect->fix.climb);
    if (collect->set & SPEED_SET)
	(void)fprintf(debugfp, "SPEED: %lf\n", collect->fix.speed);
    if (collect->set & TRACK_SET)
	(void)fprintf(debugfp, "TRACK: track: %lf\n", collect->fix.track);
    if (collect->set & CLIMB_SET)
	(void)fprintf(debugfp, "CLIMB: climb: %lf\n", collect->fix.climb);
    if (collect->set & STATUS_SET)
	(void)fprintf(debugfp, "STATUS: status: %d (%s)\n",
		      collect->status, status_values[collect->status]);
    if (collect->set & MODE_SET)
	(void)fprintf(debugfp, "MODE: mode: %d (%s)\n",
		      collect->fix.mode, mode_values[collect->fix.mode]);
    if (collect->set & DOP_SET)
	(void)fprintf(debugfp,
		      "DOP: satellites %d, pdop=%lf, hdop=%lf, vdop=%lf\n",
		      collect->satellites_used, collect->dop.pdop,
		      collect->dop.hdop, collect->dop.vdop);
    if (collect->set & VERSION_SET)
	(void)fprintf(debugfp, "VERSION: release=%s rev=%s proto=%d.%d\n",
		      collect->version.release,
		      collect->version.rev,
		      collect->version.proto_major,
		      collect->version.proto_minor);
    if (collect->set & POLICY_SET)
	(void)fprintf(debugfp,
		      "POLICY: watcher=%s nmea=%s raw=%d scaled=%s timing=%s, devpath=%s\n",
		      collect->policy.watcher ? "true" : "false",
		      collect->policy.nmea ? "true" : "false",
		      collect->policy.raw,
		      collect->policy.scaled ? "true" : "false",
		      collect->policy.timing ? "true" : "false",
		      collect->policy.devpath);
    if (collect->set & SATELLITE_SET) {
	int i;

	(void)fprintf(debugfp, "SKY: satellites in view: %d\n",
		      collect->satellites_visible);
	for (i = 0; i < collect->satellites_visible; i++) {
	    (void)fprintf(debugfp, "    %2.2d: %2.2d %3.3d %3.0f %c\n",
			  collect->PRN[i], collect->elevation[i],
			  collect->azimuth[i], collect->ss[i],
			  collect->used[i] ? 'Y' : 'N');
	}
    }
    if (collect->set & DEVICE_SET)
	(void)fprintf(debugfp, "DEVICE: Device is '%s', driver is '%s'\n",
		      collect->dev.path, collect->dev.driver);
#ifdef OLDSTYLE_ENABLE
    if (collect->set & DEVICEID_SET)
	(void)fprintf(debugfp, "GPSD ID is %s\n", collect->dev.subtype);
#endif /* OLDSTYLE_ENABLE */
    if (collect->set & DEVICELIST_SET) {
	int i;
	(void)fprintf(debugfp, "DEVICELIST:%d devices:\n",
		      collect->devices.ndevices);
	for (i = 0; i < collect->devices.ndevices; i++) {
	    (void)fprintf(debugfp, "%d: path='%s' driver='%s'\n",
			  collect->devices.ndevices,
			  collect->devices.list[i].path,
			  collect->devices.list[i].driver);
	}
    }

}

static void onsig(int sig)
{
    (void)fprintf(stderr, "libgps: died with signal %d\n", sig);
    exit(1);
}

/* must start zeroed, otherwise the unit test will try to chase garbage pointer fields. */
static struct gps_data_t gpsdata;

int main(int argc, char *argv[])
{
    struct gps_data_t collect;
    char buf[BUFSIZ];
    int option;
    bool batchmode = false;
    int debug = 0;

    (void)signal(SIGSEGV, onsig);
    (void)signal(SIGBUS, onsig);

    while ((option = getopt(argc, argv, "bhsD:?")) != -1) {
	switch (option) {
	case 'b':
	    batchmode = true;
	    break;
	case 's':
	    (void)
		printf
		("Sizes: fix=%zd gpsdata=%zd rtcm2=%zd rtcm3=%zd ais=%zd compass=%zd raw=%zd devices=%zd policy=%zd version=%zd, noise=%zd\n",
		 sizeof(struct gps_fix_t),
		 sizeof(struct gps_data_t), sizeof(struct rtcm2_t),
		 sizeof(struct rtcm3_t), sizeof(struct ais_t),
		 sizeof(struct attitude_t), sizeof(struct rawdata_t),
		 sizeof(collect.devices), sizeof(struct policy_t),
		 sizeof(struct version_t), sizeof(struct gst_t));
	    exit(0);
	case 'D':
	    debug = atoi(optarg);
	    break;
	case '?':
	case 'h':
	default:
	    (void)fputs("usage: libgps [-b] [-d lvl] [-s]\n", stderr);
	    exit(1);
	}
    }

    gps_enable_debug(debug, stdout);
    if (batchmode) {
	while (fgets(buf, sizeof(buf), stdin) != NULL) {
	    if (buf[0] == '{' || isalpha(buf[0])) {
		gps_unpack(buf, &gpsdata);
		libgps_dump_state(&gpsdata);
	    }
	}
    } else if (gps_open(NULL, 0, &collect) <= 0) {
	(void)fputs("Daemon is not running.\n", stdout);
	exit(1);
    } else if (optind < argc) {
	(void)strlcpy(buf, argv[optind], BUFSIZ);
	(void)strlcat(buf, "\n", BUFSIZ);
	(void)gps_send(&collect, buf);
	(void)gps_read(&collect);
	libgps_dump_state(&collect);
	(void)gps_close(&collect);
    } else {
	int tty = isatty(0);

	if (tty)
	    (void)fputs("This is the gpsd exerciser.\n", stdout);
	for (;;) {
	    if (tty)
		(void)fputs("> ", stdout);
	    if (fgets(buf, sizeof(buf), stdin) == NULL) {
		if (tty)
		    putchar('\n');
		break;
	    }
	    collect.set = 0;
	    (void)gps_send(&collect, buf);
	    (void)gps_read(&collect);
	    libgps_dump_state(&collect);
	}
	(void)gps_close(&collect);
    }

    return 0;
}

/*@-nullderef@*/

#endif /* TESTMAIN */
