/* libgpsd_core.c -- irect access to GPSes on serial or USB devices. */
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <netdb.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include "config.h"
#ifdef HAVE_SYS_FILIO_H
#include <sys/filio.h>	/* for FIONREAD on BSD systems */
#endif

#include "gpsd.h"

struct gps_session_t *gpsd_init(char devicetype, char *dgpsserver)
/* initialize GPS polling */
{
    time_t now = time(NULL);
    struct gps_session_t *session = (struct gps_session_t *)calloc(sizeof(struct gps_session_t), 1);
    if (!session)
	return NULL;

    session->gpsd_device = DEFAULT_DEVICE_NAME;
    session->device_type = gpsd_drivers[0];
#ifdef NON_NMEA_ENABLE
    {
    struct gps_type_t **dp;
    for (dp = gpsd_drivers; *dp; dp++)
	if ((*dp)->typekey == devicetype) {
	    gpsd_report(3, "Selecting %s driver...\n", (*dp)->typename);
	    session->device_type = *dp;
	    goto foundit;
	}
    gpsd_report(1, "invalid GPS type \"%s\", using NMEA instead\n");
    foundit:;
    }
#endif /* NON_NMEA_ENABLE */
    session->gNMEAdata.baudrate = session->device_type->baudrate;
    session->dsock = -1;
    if (dgpsserver) {
	char hn[256], buf[BUFSIZE];
	char *colon, *dgpsport = "rtcm-sc104";

	if ((colon = strchr(dgpsserver, ':'))) {
	    dgpsport = colon+1;
	    *colon = '\0';
	}
	if (!getservbyname(dgpsport, "tcp"))
	    dgpsport = "2101";

	session->dsock = netlib_connectsock(dgpsserver, dgpsport, "tcp");
	if (session->dsock < 0)
	    gpsd_report(1, "Can't connect to dgps server, netlib error %d\n", session->dsock);
	else {
	    gethostname(hn, sizeof(hn));
	    sprintf(buf, "HELO %s gpsd %s\r\nR\r\n", hn, VERSION);
	    write(session->dsock, buf, strlen(buf));
	}
    }

    /* mark GPS fd closed */
    session->gNMEAdata.gps_fd = -1;

    INIT(session->gNMEAdata.online_stamp, now);
    INIT(session->gNMEAdata.latlon_stamp, now);
    INIT(session->gNMEAdata.altitude_stamp, now);
    INIT(session->gNMEAdata.track_stamp, now);
    INIT(session->gNMEAdata.speed_stamp, now);
    INIT(session->gNMEAdata.status_stamp, now);
    INIT(session->gNMEAdata.mode_stamp, now);
    INIT(session->gNMEAdata.fix_quality_stamp, now);
    INIT(session->gNMEAdata.satellite_stamp, now);
    session->gNMEAdata.mode = MODE_NO_FIX;

    return session;
}

void gpsd_deactivate(struct gps_session_t *session)
/* temporarily release the GPS device */
{
    session->gNMEAdata.online = 0;
    REFRESH(session->gNMEAdata.online_stamp);
    session->gNMEAdata.mode = MODE_NO_FIX;
    session->gNMEAdata.status = STATUS_NO_FIX;
    gpsd_close(session);
    session->gNMEAdata.gps_fd = -1;
    if (session->device_type->wrapup)
	session->device_type->wrapup(session);
    gpsd_report(1, "closed GPS\n");
}

int gpsd_activate(struct gps_session_t *session)
/* acquire a connection to the GPS device */
{
    if ((session->gNMEAdata.gps_fd = gpsd_open(session->gNMEAdata.baudrate, session->device_type->stopbits, session)) < 0)
	return -1;
    else {
	session->gNMEAdata.online = 1;
	REFRESH(session->gNMEAdata.online_stamp);
	gpsd_report(1, "gpsd_activate: opened GPS (%d)\n", session->gNMEAdata.gps_fd);

	/* if there is an initializer and no trigger string, invoke it */
	if (session->device_type->initializer && !session->device_type->trigger)
	    session->device_type->initializer(session);
	return session->gNMEAdata.gps_fd;
    }
}

static int is_input_waiting(int fd)
{
    int	count;
    if (fd < 0 || ioctl(fd, FIONREAD, &count) < 0)
	return -1;
    return count;
}

int gpsd_poll(struct gps_session_t *session)
/* update the stuff in the scoreboard structure */
{
    int waiting;

    /* accept a DGPS correction if one is pending */
    if (is_input_waiting(session->dsock) > 0) {
	char buf[BUFSIZE];
	int rtcmbytes;

	if ((rtcmbytes=read(session->dsock,buf,BUFSIZE))>0 && (session->gNMEAdata.gps_fd !=-1)) {
	    if (session->device_type->rtcm_writer(session, buf, rtcmbytes) <= 0)
		gpsd_report(1, "Write to rtcm sink failed\n");
	    else
		gpsd_report(2, "<= DGPS: %d bytes of RTCM relayed.\n", rtcmbytes);
	} else
	    gpsd_report(1, "Read from rtcm source failed\n");
    }

    /* update the scoreboard structure from the GPS */
    waiting = is_input_waiting(session->gNMEAdata.gps_fd);
    gpsd_report(5, "GPS has %d chars waiting\n", waiting);
    if (waiting < 0)
	return waiting;
    else if (!waiting) {
	if (time(NULL) <= session->gNMEAdata.online_stamp.last_refresh + session->device_type->interval+1) {
	    return 0;
	} else {
	    session->gNMEAdata.online = 0;
	    REFRESH(session->gNMEAdata.online_stamp);
	    return -1;
	}
    } else {
	session->gNMEAdata.online = 1;
	REFRESH(session->gNMEAdata.online_stamp);

	/* call the input routine from the device-specific driver */
	session->device_type->handle_input(session);

	/* count the good fixes */
	if (session->gNMEAdata.status > STATUS_NO_FIX) 
	    session->fixcnt++;

	/* may be time to ship a DGPS correction to the GPS */
	if (session->fixcnt > 10 && !session->sentdgps) {
	    session->sentdgps++;
	    if (session->dsock > -1) {
		char buf[BUFSIZE];
		sprintf(buf, "R %0.8f %0.8f %0.2f\r\n", 
			session->gNMEAdata.latitude,
			session->gNMEAdata.longitude, 
			session->gNMEAdata.altitude);
		write(session->dsock, buf, strlen(buf));
		gpsd_report(2, "=> dgps %s", buf);
	    }
	}
    }
    return waiting;
}

void gpsd_wrap(struct gps_session_t *session)
/* end-of-session wrapup */
{
    gpsd_deactivate(session);
    if (session->dsock >= 0)
	close(session->dsock);
}

#ifdef BINARY_ENABLE
static double degtodm(double a)
{
    double m, t;
    m = modf(a, &t);
    t = floor(a) * 100 + m * 60;
    return t;
}

void gpsd_binary_fix_dump(struct gps_session_t *session, char *bufp)
{
    if (session->gNMEAdata.mode > 1) {
	sprintf(bufp,
		"$GPGGA,%02d%02d%02d,%f,%c,%f,%c,%d,%02d,%.2f,%.1f,%c,%f,%c,%s,%s*",
		session->hours,
		session->minutes,
		session->seconds,
		degtodm(fabs(session->gNMEAdata.latitude)),
		((session->gNMEAdata.latitude > 0) ? 'N' : 'S'),
		degtodm(fabs(session->gNMEAdata.longitude)),
		((session->gNMEAdata.longitude > 0) ? 'E' : 'W'),
		session->gNMEAdata.mode,
		session->gNMEAdata.satellites_used,
		session->gNMEAdata.hdop,
		session->gNMEAdata.altitude, 'M',
		session->separation, 'M', "", "");
	nmea_add_checksum(bufp + 1);
	bufp = bufp + strlen(bufp);
    }
    sprintf(bufp,
	    "$GPRMC,%02d%02d%02d,%c,%f,%c,%f,%c,%f,%f,%02d%02d%02d,,*",
	    session->hours, session->minutes, session->seconds,
	    session->gNMEAdata.status ? 'A' : 'V',
	    degtodm(fabs(session->gNMEAdata.latitude)),
	    ((session->gNMEAdata.latitude > 0) ? 'N' : 'S'),
	    degtodm(fabs(session->gNMEAdata.longitude)),
	    ((session->gNMEAdata.longitude > 0) ? 'E' : 'W'),
	    session->gNMEAdata.speed,
	    session->gNMEAdata.track,
	    session->day,
	    session->month,
	    (session->year % 100));
	nmea_add_checksum(bufp + 1);
}

void gpsd_binary_satellite_dump(struct gps_session_t *session, char *bufp)
{
    int i, j;

    j = (session->gNMEAdata.satellites / 4) + (((session->gNMEAdata.satellites % 4) > 0) ? 1 : 0);

    for( i = 0 ; i < MAXCHANNELS; i++ ) {
	if (i % 4 == 0)
	    sprintf(bufp, "$GPGSV,%d,%d,%02d", j, (i / 4) + 1, session->gNMEAdata.satellites);
	bufp += strlen(bufp);
	if (i <= session->gNMEAdata.satellites && session->gNMEAdata.elevation[i])
	    sprintf(bufp, ",%02d,%02d,%03d,%02d", session->gNMEAdata.PRN[i],
		    session->gNMEAdata.elevation[i], session->gNMEAdata.azimuth[i], session->gNMEAdata.ss[i]);
	else
	    sprintf(bufp, ",%02d,00,000,%02d,", session->gNMEAdata.PRN[i],
		    session->gNMEAdata.ss[i]);
	bufp += strlen(bufp);
	if (i % 4 == 3) {
	    sprintf(bufp, "*");
	    nmea_add_checksum(bufp + 1);
	    bufp += strlen(bufp);
	}
    }
}

void gpsd_binary_quality_dump(struct gps_session_t *session, char *bufp)
{
    int	i, j;

    sprintf(bufp, "$GPGSA,%c,%d,", 'A', session->gNMEAdata.mode);
    j = 0;
    for (i = 0; i < MAXCHANNELS; i++) {
	if (session->gNMEAdata.used[i]) {
	    bufp = bufp + strlen(bufp);
	    sprintf(bufp, "%02d,", session->gNMEAdata.PRN[i]);
	    j++;
	}
    }
    for (i = j; i < MAXCHANNELS; i++) {
	bufp = bufp + strlen(bufp);
	sprintf(bufp, ",");
    }
    bufp = bufp + strlen(bufp);
    sprintf(bufp, "%.2f,%.2f,%.2f*", session->gNMEAdata.pdop, session->gNMEAdata.hdop,
	    session->gNMEAdata.vdop);
    nmea_add_checksum(bufp + 1);
}

#endif /* BINARY_ENABLE */
