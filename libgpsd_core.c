/* libgpsd_core.c -- direct access to GPSes on serial or USB devices. */
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <netdb.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include "config.h"
#ifdef HAVE_SYS_FILIO_H
#include <sys/filio.h>	/* for FIONREAD on BSD systems */
#endif

#include "gpsd.h"

#define NO_MAG_VAR	-999	/* must be out of band for degrees */

int gpsd_switch_driver(struct gps_session_t *session, char* typename)
{
    struct gps_type_t **dp;
    for (dp = gpsd_drivers; *dp; dp++)
	if (!strcmp((*dp)->typename, typename)) {
	    gpsd_report(3, "Selecting %s driver...\n", (*dp)->typename);
	    session->device_type = *dp;
	    if (session->device_type->initializer)
		session->device_type->initializer(session);
	    return 1;
	}
    gpsd_report(1, "invalid GPS type \"%s\", using NMEA instead\n", typename);
    return 0;
}

struct gps_session_t *gpsd_init(char *dgpsserver)
/* initialize GPS polling */
{
    struct gps_session_t *session = (struct gps_session_t *)calloc(sizeof(struct gps_session_t), 1);
    if (!session)
	return NULL;

    session->gpsd_device = DEFAULT_DEVICE_NAME;
    session->device_type = gpsd_drivers[0];
    session->dsock = -1;
    if (dgpsserver) {
	char hn[256], buf[BUFSIZ];
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
    session->gpsdata.gps_fd = -1;

#ifdef NTPSHM_ENABLE
    ntpshm_init(session);
#endif /* defined(SHM_H) && defined(IPC_H) */

    return session;
}

void gpsd_deactivate(struct gps_session_t *session)
/* temporarily release the GPS device */
{
    session->gpsdata.online = 0;
    session->gpsdata.fix.mode = MODE_NOT_SEEN;
    session->gpsdata.status = STATUS_NO_FIX;
    session->gpsdata.fix.track = TRACK_NOT_VALID;
    session->gpsdata.fix.altitude = ALTITUDE_NOT_VALID;
    gpsd_close(session);
    session->gpsdata.gps_fd = -1;
    if (session->device_type->wrapup)
	session->device_type->wrapup(session);
    gpsd_report(1, "closed GPS\n");
}

int gpsd_activate(struct gps_session_t *session)
/* acquire a connection to the GPS device */
{
    if (gpsd_open(session) < 0)
	return -1;
    else {
	session->gpsdata.online = timestamp();
	session->counter = 0;
	gpsd_report(1, "gpsd_activate: opened GPS (%d)\n", session->gpsdata.gps_fd);
	if (session->packet_type == SIRF_PACKET)
	    gpsd_switch_driver(session, "SiRF-II binary");
	else if (session->packet_type == NMEA_PACKET)
	    gpsd_switch_driver(session, "Generic NMEA");
	else if (session->device_type->initializer)
	    session->device_type->initializer(session);

	session->gpsdata.fix.mode = MODE_NOT_SEEN;
	session->gpsdata.status = STATUS_NO_FIX;
	session->gpsdata.fix.track = TRACK_NOT_VALID;
	session->mag_var = NO_MAG_VAR;
	session->separation = NO_SEPARATION;

	return session->gpsdata.gps_fd;
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
	char buf[BUFSIZ];
	int rtcmbytes;

	if ((rtcmbytes=read(session->dsock,buf,sizeof(buf)))>0 && (session->gpsdata.gps_fd !=-1)) {
	    if (session->device_type->rtcm_writer(session, buf, rtcmbytes) <= 0)
		gpsd_report(1, "Write to rtcm sink failed\n");
	    else
		gpsd_report(2, "<= DGPS: %d bytes of RTCM relayed.\n", rtcmbytes);
	} else
	    gpsd_report(1, "Read from rtcm source failed\n");
    }

    /* update the scoreboard structure from the GPS */
    waiting = is_input_waiting(session->gpsdata.gps_fd);
    gpsd_report(7, "GPS has %d chars waiting\n", waiting);
    if (waiting < 0)
	return 0;
    else if (!waiting) {
	if (timestamp()>session->gpsdata.online+session->device_type->cycle+1){
	    session->gpsdata.online = 0;
	    return 0;
	} else
	    return ONLINE_SET;
    } else {
	struct gps_data_t old;
	int mask = 0;

	memcpy(&old, &session->gpsdata, sizeof(struct gps_data_t));

	session->gpsdata.online = timestamp();

	/* can we get a full packet from the device? */
	if (!session->device_type->get_packet(session, waiting))
	    return ONLINE_SET;

	session->gpsdata.d_xmit_time = timestamp();

	mask = ONLINE_SET | session->device_type->parse_packet(session);

	session->counter++;
	session->gpsdata.d_decode_time = timestamp();

	/* count the good fixes */
	if (session->gpsdata.status > STATUS_NO_FIX) 
	    session->fixcnt++;

	/* may be time to ship a DGPS correction to the GPS */
	if (session->fixcnt > 10 && !session->sentdgps) {
	    session->sentdgps++;
	    if (session->dsock > -1) {
		char buf[BUFSIZ];
		sprintf(buf, "R %0.8f %0.8f %0.2f\r\n", 
			session->gpsdata.fix.latitude, 
			session->gpsdata.fix.longitude, 
			session->gpsdata.fix.altitude);
		write(session->dsock, buf, strlen(buf));
		gpsd_report(2, "=> dgps %s", buf);
	    }
	}
	return mask;
    }
}

void gpsd_wrap(struct gps_session_t *session)
/* end-of-session wrapup */
{
    gpsd_deactivate(session);
    if (session->dsock >= 0)
	close(session->dsock);
}

void gpsd_zero_satellites(struct gps_data_t *out)
{
    memset(out->PRN,       '\0', sizeof(out->PRN));
    memset(out->elevation, '\0', sizeof(out->elevation));
    memset(out->azimuth,   '\0', sizeof(out->azimuth));
    memset(out->ss,        '\0', sizeof(out->ss));
    out->satellites = 0;
}

void gpsd_raw_hook(struct gps_session_t *session, char *sentence)
{
    char *sp, *tp;
    if (sentence[0] != '$')
	session->gpsdata.tag[0] = '\0';
    else {
	for (tp = session->gpsdata.tag, sp = sentence+1; *sp && *sp != ','; sp++, tp++)
	    *tp = *sp;
	*tp = '\0';
    }
    session->gpsdata.sentence_length = strlen(sentence);

    if (session->gpsdata.raw_hook) {
	session->gpsdata.raw_hook(&session->gpsdata, sentence);
    }
}

#ifdef BINARY_ENABLE
/*
 * Support for generic binary drivers.  These functions dump NMEA for passing
 * to the client in raw mode.  They assume that (a) the public gps.h structure 
 * members are in a valid state, (b) that the private members hours, minutes, 
 * and seconds have also been filled in, (c) that if the private member
 * mag_var is nonzero it is a magnetic variation in degrees that should be
 * passed on., and (d) if the private member separation does not have the
 * value NO_SEPARATION, it is a valid WGS84 geoidal separation in 
 * meters for the fix.
 */

static double degtodm(double a)
{
    double m, t;
    m = modf(a, &t);
    t = floor(a) * 100 + m * 60;
    return t;
}

void gpsd_binary_fix_dump(struct gps_session_t *session, char *bufp)
{
    char hdop_str[NMEA_MAX] = "";

    if (session->gpsdata.hdop)
	sprintf(hdop_str, "%.2f", session->gpsdata.hdop);

    if (session->gpsdata.fix.mode > 1) {
	sprintf(bufp,
		"$GPGGA,%02d%02d%02.3f,%.4f,%c,%.4f,%c,%d,%02d,%s,%.1f,%c,",
		session->hours,
		session->minutes,
		session->seconds,
		degtodm(fabs(session->gpsdata.fix.latitude)),
		((session->gpsdata.fix.latitude > 0) ? 'N' : 'S'),
		degtodm(fabs(session->gpsdata.fix.longitude)),
		((session->gpsdata.fix.longitude > 0) ? 'E' : 'W'),
		session->gpsdata.fix.mode,
		session->gpsdata.satellites_used,
		hdop_str,
		session->gpsdata.fix.altitude, 'M');
	if (session->separation == NO_SEPARATION)
	    strcat(bufp, ",,");
	else
	    sprintf(bufp+strlen(bufp), "%.3f,M", session->separation);
	if (session->mag_var == NO_MAG_VAR) 
	    strcat(bufp, ",,");
	else {
	    sprintf(bufp+strlen(bufp), "%3.2f,", fabs(session->mag_var));
	    strcat(bufp, (session->mag_var > 0) ? "E": "W");
	}
	nmea_add_checksum(bufp);
	gpsd_raw_hook(session, bufp);
	bufp += strlen(bufp);
    }
    sprintf(bufp,
	    "$GPRMC,%02d%02d%02.2f,%c,%.4f,%c,%.4f,%c,%.4f,%.3f,%02d%02d%02d,,",
	    session->hours, session->minutes, session->seconds,
	    session->gpsdata.status ? 'A' : 'V',
	    degtodm(fabs(session->gpsdata.fix.latitude)),
	    ((session->gpsdata.fix.latitude > 0) ? 'N' : 'S'),
	    degtodm(fabs(session->gpsdata.fix.longitude)),
	    ((session->gpsdata.fix.longitude > 0) ? 'E' : 'W'),
	    session->gpsdata.fix.speed,
	    session->gpsdata.fix.track,
	    session->day,
	    session->month,
	    (session->year % 100));
	nmea_add_checksum(bufp);
	gpsd_raw_hook(session, bufp);
}

void gpsd_binary_satellite_dump(struct gps_session_t *session, char *bufp)
{
    int i;
    char *bufp2 = bufp;
    bufp[0] = '\0';

    for( i = 0 ; i < session->gpsdata.satellites; i++ ) {
	if (i % 4 == 0) {
	    bufp += strlen(bufp);
            bufp2 = bufp;
	    sprintf(bufp, "$GPGSV,%d,%d,%02d", 
		    ((session->gpsdata.satellites-1) / 4) + 1, 
		    (i / 4) + 1,
		    session->gpsdata.satellites);
	}
	bufp += strlen(bufp);
	if (i < session->gpsdata.satellites)
	    sprintf(bufp, ",%02d,%02d,%03d,%02d", 
		    session->gpsdata.PRN[i],
		    session->gpsdata.elevation[i], 
		    session->gpsdata.azimuth[i], 
		    session->gpsdata.ss[i]);
	if (i % 4 == 3 || i == session->gpsdata.satellites-1) {
	    nmea_add_checksum(bufp2);
	    gpsd_raw_hook(session, bufp2);
	}
    }
}

void gpsd_binary_quality_dump(struct gps_session_t *session, char *bufp)
{
    int	i, j;
    char *bufp2 = bufp;

    sprintf(bufp, "$GPGSA,%c,%d,", 'A', session->gpsdata.fix.mode);
    j = 0;
    for (i = 0; i < MAXCHANNELS; i++) {
	if (session->gpsdata.used[i]) {
	    bufp += strlen(bufp);
	    sprintf(bufp, "%02d,", session->gpsdata.PRN[i]);
	    j++;
	}
    }
    for (i = j; i < MAXCHANNELS; i++) {
	bufp += strlen(bufp);
	sprintf(bufp, ",");
    }
    bufp += strlen(bufp);
    sprintf(bufp, "%.1f,%.1f,%.1f*", 
	    session->gpsdata.pdop, 
	    session->gpsdata.hdop,
	    session->gpsdata.vdop);
    nmea_add_checksum(bufp2);
    gpsd_raw_hook(session, bufp2);
    bufp += strlen(bufp);
    if ((session->gpsdata.fix.eph || session->gpsdata.fix.epv)
	&& finite(session->gpsdata.fix.eph)
	&& finite(session->gpsdata.fix.epv)
	&& finite(session->gpsdata.epe)
    ) {
        // output PGRME
        // only if realistic
        sprintf(bufp, "$PGRME,%.2f,%.2f,%.2f",
	    session->gpsdata.fix.eph, 
	    session->gpsdata.fix.epv, 
	    session->gpsdata.epe);
        nmea_add_checksum(bufp);
	gpsd_raw_hook(session, bufp);
	session->gpsdata.seen_sentences |= PGRME;
     }
}

#endif /* BINARY_ENABLE */
