/* libgpsd_core.c -- direct access to GPSes on serial or USB devices. */
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <netdb.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include "config.h"
#ifdef HAVE_SYS_FILIO_H
#include <sys/filio.h>	/* for FIONREAD on BSD systems */
#endif
#include <string.h>
#if defined(PPS_ENABLE) && defined(TIOCMIWAIT)
#include <pthread.h>
#endif

#include "gpsd.h"

/*@ -branchstate */
int gpsd_open_dgps(char *dgpsserver)
{
    char hn[256], buf[BUFSIZ];
    char *colon, *dgpsport = "rtcm-sc104";
    int dsock;

    if ((colon = strchr(dgpsserver, ':'))) {
	dgpsport = colon+1;
	*colon = '\0';
    }
    if (!getservbyname(dgpsport, "tcp"))
	dgpsport = "2101";

    dsock = netlib_connectsock(dgpsserver, dgpsport, "tcp");
    if (dsock >= 0) {
	(void)gethostname(hn, sizeof(hn));
	(void)snprintf(buf,sizeof(buf), "HELO %s gpsd %s\r\nR\r\n",hn,VERSION);
	(void)write(dsock, buf, strlen(buf));
    }
    return dsock;
}
/*@ +branchstate */

int gpsd_switch_driver(struct gps_device_t *session, char* typename)
{
    struct gps_type_t **dp;
    /*@ -compmempass @*/
    for (dp = gpsd_drivers; *dp; dp++)
	if (strcmp((*dp)->typename, typename) == 0) {
	    gpsd_report(3, "Selecting %s driver...\n", (*dp)->typename);
	    /*@i@*/session->device_type = *dp;
	    if (session->device_type->initializer)
		session->device_type->initializer(session);
	    return 1;
	}
    gpsd_report(1, "invalid GPS type \"%s\".\n", typename);
    return 0;
    /*@ +compmempass @*/
}

struct gps_device_t *gpsd_init(struct gps_context_t *context, char *device)
/* initialize GPS polling */
{
    struct gps_device_t *session = (struct gps_device_t *)calloc(sizeof(struct gps_device_t), 1);
    if (!session)
	return NULL;

    /*@ -mustfreeonly @*//* splint 3.1.1 can't deduce storage is zero */
    session->gpsdata.gps_device = strdup(device);
    session->device_type = NULL;	/* start by hunting packets */
    session->dsock = -1;
    /*@ -temptrans @*/
    session->context = context;
    /*@ +temptrans @*/
    /*@ +mustfreeonly @*/
    session->gpsdata.hdop = NAN;
    session->gpsdata.vdop = NAN;
    session->gpsdata.pdop = NAN;
    session->gpsdata.tdop = NAN;
    session->gpsdata.gdop = NAN;

    /* mark GPS fd closed */
    session->gpsdata.gps_fd = -1;

    /* necessary in case we start reading in the middle of a GPGSV sequence */
    gpsd_zero_satellites(&session->gpsdata);

    return session;
}

void gpsd_deactivate(struct gps_device_t *session)
/* temporarily release the GPS device */
{
    gpsd_report(1, "closing GPS=%s\n", session->gpsdata.gps_device);
    (void)gpsd_close(session);
    session->gpsdata.gps_fd = -1;
#ifdef NTPSHM_ENABLE
    (void)ntpshm_free(session->context, session->shmTime);
    session->shmTime = -1;
# ifdef PPS_ENABLE
    (void)ntpshm_free(session->context, session->shmTimeP);
    session->shmTimeP = -1;
# endif /* PPS_ENABLE */
#endif /* NTPSHM_ENABLE */
    if (session->device_type != NULL && session->device_type->wrapup != NULL)
	session->device_type->wrapup(session);
}

#if defined(PPS_ENABLE) && defined(TIOCMIWAIT)
static void *gpsd_ppsmonitor(void *arg)
{
    struct gps_device_t *session = (struct gps_device_t *)arg;
    int cycle,duration, state = 0;
    struct timeval tv;
    struct timeval pulse[2] = {{0,0},{0,0}};

    /* wait for status change on the device's carrier-detect line */
    while (ioctl(session->gpsdata.gps_fd, TIOCMIWAIT, TIOCM_CAR) == 0) {
	(void)gettimeofday(&tv,NULL);
	/*@ +ignoresigns */
	if (ioctl(session->gpsdata.gps_fd, TIOCMGET, &state) != 0)
	    break;
	/*@ -ignoresigns */

        state = (int)((state & TIOCM_CAR) != 0);
	gpsd_report(5, "carrier-detect on %s changed to %d\n", 
		    session->gpsdata.gps_device, state);

	/*@ +boolint @*/
	if (session->gpsdata.fix.mode > MODE_NO_FIX) {
	    /*
	     * The PPS pulse is normally a short pulse with a frequency of
	     * 1 Hz, and the UTC second is defined by the front edge.  But we
	     * don't know the polarity of the pulse (different receivers
	     * emit different polarities).  The duration variable is used to
	     * determine which way the pulse is going.  The code assumes
	     * that the UTC second is changing when the signal has not
	     * been changing for at least 800ms, i.e. it assumes the duty
	     * cycle is at most 20%.
	     */
#define timediff(x, y)	(int)((x.tv_sec-y.tv_sec)*1000000+x.tv_usec-y.tv_usec)
	    cycle = timediff(tv, pulse[state]);
	    duration = timediff(tv, pulse[state == 0]);
#undef timediff
	    if (cycle > 999000 && cycle < 1001000 && duration > 800000)
		(void)ntpshm_pps(session, &tv);
	}
	/*@ -boolint @*/

	pulse[state] = tv;
    }

    return NULL;
}
#endif /* PPS_ENABLE */

int gpsd_activate(struct gps_device_t *session)
/* acquire a connection to the GPS device */
{
#if defined(PPS_ENABLE) && defined(TIOCMIWAIT)
    pthread_t pt;
#endif /* PPS_ENABLE */

    if (gpsd_open(session) < 0)
	return -1;
    else {
	session->gpsdata.online = timestamp();
#ifdef SIRFII_ENABLE
	session->satcounter = 0;
#endif /* SIRFII_ENABLE */
	session->counter = 0;
	gpsd_report(1, "gpsd_activate: opened GPS (%d)\n", session->gpsdata.gps_fd);
	// session->gpsdata.online = 0;
	session->gpsdata.fix.mode = MODE_NOT_SEEN;
	session->gpsdata.status = STATUS_NO_FIX;
	session->gpsdata.fix.track = NAN;
#ifdef BINARY_ENABLE
	session->mag_var = NAN;
	session->gpsdata.separation = NAN;
#endif /* BINARY_ENABLE */

#ifdef NTPSHM_ENABLE
	session->shmTime = ntpshm_alloc(session->context);
#if defined(PPS_ENABLE) && defined(TIOCMIWAIT)
	if (session->shmTime >= 0 && session->context->shmTimePPS) {
	    if ((session->shmTimeP = ntpshm_alloc(session->context)) >= 0)
		/*@i1@*/(void)pthread_create(&pt,NULL,gpsd_ppsmonitor,(void *)session);
	}
#endif /* defined(PPS_ENABLE) && defined(TIOCMIWAIT) */
#endif /* NTPSHM_ENABLE */

	return session->gpsdata.gps_fd;
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
 * value NAN, it is a valid WGS84 geoidal separation in 
 * meters for the fix.
 */

static double degtodm(double a)
{
    double m, t;
    m = modf(a, &t);
    t = floor(a) * 100 + m * 60;
    return t;
}

static void gpsd_binary_fix_dump(struct gps_device_t *session, 
				 char bufp[],size_t len)
{
    struct tm tm;
    time_t intfixtime;

    intfixtime = (time_t)session->gpsdata.fix.time;
    (void)gmtime_r(&intfixtime, &tm);
    if (session->gpsdata.fix.mode > 1) {
	(void)snprintf(bufp, len,
		"$GPGGA,%02d%02d%02d,%09.4f,%c,%010.4f,%c,%d,%02d,",
		tm.tm_hour,
		tm.tm_min,
		tm.tm_sec,
		degtodm(fabs(session->gpsdata.fix.latitude)),
		((session->gpsdata.fix.latitude > 0) ? 'N' : 'S'),
		degtodm(fabs(session->gpsdata.fix.longitude)),
		((session->gpsdata.fix.longitude > 0) ? 'E' : 'W'),
		session->gpsdata.fix.mode,
		session->gpsdata.satellites_used);
	if (isnan(session->gpsdata.hdop))
	    (void)strcat(bufp, ",");
	else
	    (void)snprintf(bufp+strlen(bufp), len-strlen(bufp),
			   "%.2f,",session->gpsdata.hdop);
	if (isnan(session->gpsdata.fix.altitude))
	    (void)strcat(bufp, ",");
	else
	    (void)snprintf(bufp+strlen(bufp), len-strlen(bufp), 
			   "%.1f,M,", session->gpsdata.fix.altitude);
	if (isnan(session->gpsdata.separation))
	    (void)strcat(bufp, ",");
	else
	    (void)snprintf(bufp+strlen(bufp), len-strlen(bufp), 
			   "%.3f,M,", session->gpsdata.separation);
	if (isnan(session->mag_var)) 
	    (void)strcat(bufp, ",");
	else {
	    (void)snprintf(bufp+strlen(bufp),
			   len-strlen(bufp),
			   "%3.2f,", fabs(session->mag_var));
	    (void)strcat(bufp, (session->mag_var > 0) ? "E": "W");
	}
	nmea_add_checksum(bufp);
	bufp += strlen(bufp);
    }
    /*@ -usedef @*/
    (void)snprintf(bufp, len-strlen(bufp),
	    "$GPRMC,%02d%02d%02d,%c,%09.4f,%c,%010.4f,%c,%.4f,%.3f,%02d%02d%02d,,",
	    tm.tm_hour, 
	    tm.tm_min, 
	    tm.tm_sec,
	    session->gpsdata.status ? 'A' : 'V',
	    degtodm(fabs(session->gpsdata.fix.latitude)),
	    ((session->gpsdata.fix.latitude > 0) ? 'N' : 'S'),
	    degtodm(fabs(session->gpsdata.fix.longitude)),
	    ((session->gpsdata.fix.longitude > 0) ? 'E' : 'W'),
	    session->gpsdata.fix.speed,
	    session->gpsdata.fix.track,
	    tm.tm_mday,
	    tm.tm_mon + 1,
	    tm.tm_year % 100);
    /*@ +usedef @*/
    nmea_add_checksum(bufp);
}

static void gpsd_binary_satellite_dump(struct gps_device_t *session, 
				char bufp[], size_t len)
{
    int i;
    char *bufp2 = bufp;
    bufp[0] = '\0';

    for( i = 0 ; i < session->gpsdata.satellites; i++ ) {
	if (i % 4 == 0) {
	    bufp += strlen(bufp);
            bufp2 = bufp;
	    len -= snprintf(bufp, len,
		    "$GPGSV,%d,%d,%02d", 
		    ((session->gpsdata.satellites-1) / 4) + 1, 
		    (i / 4) + 1,
		    session->gpsdata.satellites);
	}
	bufp += strlen(bufp);
	if (i < session->gpsdata.satellites)
	    len -= snprintf(bufp, len, 
		    ",%02d,%02d,%03d,%02d", 
		    session->gpsdata.PRN[i],
		    session->gpsdata.elevation[i], 
		    session->gpsdata.azimuth[i], 
		    session->gpsdata.ss[i]);
	if (i % 4 == 3 || i == session->gpsdata.satellites-1) {
	    nmea_add_checksum(bufp2);
	    len -= 5;
	}
    }

#ifdef ZODIAC_ENABLE
    if (session->Zs[0] != 0) {
	bufp += strlen(bufp);
	bufp2 = bufp;
	strcpy(bufp, "$PRWIZCH");
	for (i = 0; i < MAXCHANNELS; i++) {
	    len -= snprintf(bufp+strlen(bufp), len,
			  ",%02u,%X", session->Zs[i], session->Zv[i] & 0x0f);
	}
	nmea_add_checksum(bufp2);
    }
#endif /* ZODIAC_ENABLE */
}

static void gpsd_binary_quality_dump(struct gps_device_t *session, 
			      char bufp[], size_t len)
{
    int	i, j;
    char *bufp2 = bufp;

    (void)snprintf(bufp, len-strlen(bufp),
		   "$GPGSA,%c,%d,", 'A', session->gpsdata.fix.mode);
    j = 0;
    for (i = 0; i < MAXCHANNELS; i++) {
	if (session->gpsdata.used[i]) {
	    bufp += strlen(bufp);
	    (void)snprintf(bufp, len-strlen(bufp),
			   "%02d,", session->gpsdata.PRN[i]);
	    j++;
	}
    }
    for (i = j; i < MAXCHANNELS; i++) {
	bufp += strlen(bufp);
	(void)strcpy(bufp, ",");
    }
    bufp += strlen(bufp);
    if (session->gpsdata.fix.mode == MODE_NO_FIX)
	(void)strcat(bufp, ",,,");
    else
	(void)snprintf(bufp, len-strlen(bufp),
		       "%.1f,%.1f,%.1f*", 
		       session->gpsdata.pdop, 
		       session->gpsdata.hdop,
		       session->gpsdata.vdop);
    nmea_add_checksum(bufp2);
    bufp += strlen(bufp);
    if (finite(session->gpsdata.fix.eph)
	&& finite(session->gpsdata.fix.epv)
	&& finite(session->gpsdata.epe)) {
        // output PGRME
        // only if realistic
        (void)snprintf(bufp, len-strlen(bufp),
	    "$PGRME,%.2f,%.2f,%.2f",
	    session->gpsdata.fix.eph, 
	    session->gpsdata.fix.epv, 
	    session->gpsdata.epe);
        nmea_add_checksum(bufp);
	session->gpsdata.seen_sentences |= PGRME;
     }
}

#endif /* BINARY_ENABLE */

static int is_input_waiting(int fd)
{
    int	count = 0;
    if (fd < 0 || ioctl(fd, FIONREAD, &count) < 0)
	return -1;
    return count;
}

static gps_mask_t handle_packet(struct gps_device_t *session)
{
    session->packet_full = 0;
    session->gpsdata.sentence_time = 0;
    session->gpsdata.sentence_length = session->outbuflen;
    session->gpsdata.d_recv_time = timestamp();

    session->gpsdata.set = ONLINE_SET | session->device_type->parse_packet(session);

    /* count all packets and good fixes */
    session->counter++;
    if (session->gpsdata.status > STATUS_NO_FIX) 
	session->fixcnt++;

    if ((session->gpsdata.set & TIME_SET)==0)
    	session->gpsdata.sentence_time = NAN;
    /* 
     * When there is valid fix data, we have an accumulating policy
     * about position data.  This does the right thing in cases 
     * like SiRF and Zodiac where we get all the fix info in one
     * packet per turn, and papers over the commonest crack -- the
     * fact that NMEA devices ship altitude separately in GGA from most of
     * the rest of the fix information we want in RMC.  
     *
     * Thus, we never clear 2D or 3D position data here unless the
     * mode has gone from 3D to 2D (or lower).  But we do want to clear
     * velocity data, because that's always shipped atomically if it's
     * valid (in RMC or a binary packet)
     */
    if ((session->gpsdata.set & MODE_SET)!=0 && session->gpsdata.fix.mode < MODE_3D)
	session->gpsdata.fix.altitude = NAN;
    if ((session->gpsdata.set & SPEED_SET)==0)
    	session->gpsdata.fix.speed = NAN;
    if ((session->gpsdata.set & TRACK_SET)==0)
	session->gpsdata.fix.track = NAN;
    if ((session->gpsdata.set & CLIMB_SET)==0)
    	session->gpsdata.fix.climb = NAN;
    /*
     * Now we compute derived quantities.  This is where the tricky error-
     * modeling stuff goes. Presently we don't know how to derive 
     * time or track error.
     *
     * Field reports match the theoretical prediction that
     * expected time error should be half the resolution of
     * the GPS clock, so we put the bound of the error
     * in as a constant pending getting it from each driver.
     *
     * Some drivers set the position-error fields.  Only the Zodiacs 
     * report speed error.  Nobody reports track error or climb error.
     */
    session->gpsdata.fix.ept = 0.005;
    if ((session->gpsdata.set & HERR_SET)==0 
	&& (session->gpsdata.set & HDOP_SET)!=0) {
	session->gpsdata.fix.eph = session->gpsdata.hdop*UERE(session);
	session->gpsdata.set |= HERR_SET;
    }
    if ((session->gpsdata.set & VERR_SET)==0 
	&& (session->gpsdata.set & VDOP_SET)!=0) {
	session->gpsdata.fix.epv = session->gpsdata.vdop*UERE(session);
	session->gpsdata.set |= VERR_SET;
    }
    if ((session->gpsdata.set & PERR_SET)==0
	&& (session->gpsdata.set & PDOP_SET)!=0) {
	session->gpsdata.epe = session->gpsdata.pdop*UERE(session);
	session->gpsdata.set |= PERR_SET;
    }
    /*
     * If we have a current fix and an old fix, and the packet handler 
     * didn't set the speed error and climb error members itself, 
     * try to compute them now.
     * FIXME:  We need to compute track error here.
     */
    session->gpsdata.fix.epd = NAN;
    if (session->gpsdata.fix.mode >= MODE_2D) {
	if ((session->gpsdata.set & SPEEDERR_SET)==0 && session->gpsdata.fix.time > session->lastfix.time) {
	    session->gpsdata.fix.eps = NAN;
	    if (session->lastfix.mode > MODE_NO_FIX 
		&& session->gpsdata.fix.mode > MODE_NO_FIX) {
		double t = session->gpsdata.fix.time-session->lastfix.time;
		double e = session->lastfix.eph + session->gpsdata.fix.eph;
		session->gpsdata.fix.eps = e/t;
	    }
	    if (session->gpsdata.fix.eps != NAN)
		session->gpsdata.set |= SPEEDERR_SET;
	}
	if ((session->gpsdata.set & CLIMBERR_SET)==0 && session->gpsdata.fix.time > session->lastfix.time) {
	    session->gpsdata.fix.epc = NAN;
	    if (session->lastfix.mode > MODE_3D 
		&& session->gpsdata.fix.mode > MODE_3D) {
		double t = session->gpsdata.fix.time-session->lastfix.time;
		double e = session->lastfix.epv + session->gpsdata.fix.epv;
		/* if vertical uncertainties are zero this will be too */
		session->gpsdata.fix.epc = e/t;
	    }
	    if (isnan(session->gpsdata.fix.epc)==0)
		session->gpsdata.set |= CLIMBERR_SET;
	}

	/* save the old fix for later uncertainty computations */
	(void)memcpy(&session->lastfix, 
		     &session->gpsdata.fix, 
		     sizeof(struct gps_fix_t));
    }

    session->gpsdata.d_decode_time = timestamp();

    /* also copy the sentence up to clients in raw mode */
    if (session->packet_type == NMEA_PACKET)
	session->gpsdata.raw_hook(&session->gpsdata,
				  (char *)session->outbuffer,
				  strlen((char *)session->outbuffer), 1);
    else {
	char buf2[MAX_PACKET_LENGTH*3+2];

	buf2[0] = '\0';
	if ((session->gpsdata.set & LATLON_SET) != 0)
	    gpsd_binary_fix_dump(session, 
				 buf2+strlen(buf2), 
				 (sizeof(buf2)-strlen(buf2)));
	if ((session->gpsdata.set & HDOP_SET) != 0)
	    gpsd_binary_quality_dump(session,
				     buf2 + strlen(buf2),
				     (sizeof(buf2)-strlen(buf2)));
	if ((session->gpsdata.set & SATELLITE_SET) != 0)
	    gpsd_binary_satellite_dump(session,
				     buf2 + strlen(buf2),
				     (sizeof(buf2)-strlen(buf2)));
	if (buf2[0] != '\0') {
	    gpsd_report(3, "<= GPS: %s", buf2);
	    if (session->gpsdata.raw_hook)
		session->gpsdata.raw_hook(&session->gpsdata, 
					  buf2, strlen(buf2), 1);
	}
    }

    /* may be time to ship a DGPS correction to the GPS */
    if (session->fixcnt > 10 && session->sentdgps==0) {
	session->sentdgps++;
	if (session->dsock > -1) {
	    char buf[BUFSIZ];
	    (void)snprintf(buf, sizeof(buf), "R %0.8f %0.8f %0.2f\r\n", 
			   session->gpsdata.fix.latitude, 
			   session->gpsdata.fix.longitude, 
			   session->gpsdata.fix.altitude);
	    (void)write(session->dsock, buf, strlen(buf));
	    gpsd_report(2, "=> dgps %s", buf);
	}
    }

    return session->gpsdata.set;
}

gps_mask_t gpsd_poll(struct gps_device_t *session)
/* update the stuff in the scoreboard structure */
{
    int waiting;

    /* accept a DGPS correction if one is pending */
    if (is_input_waiting(session->dsock) > 0) {
	char buf[BUFSIZ];
	int rtcmbytes;

	if ((rtcmbytes=(int)read(session->dsock,buf,sizeof(buf)))>0 && (session->gpsdata.gps_fd !=-1)) {
	    if (session->device_type->rtcm_writer(session, buf, (size_t)rtcmbytes) == 0)
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
    else if (waiting == 0) {
	if (session->device_type != NULL && timestamp()>session->gpsdata.online+session->device_type->cycle+1){
	    session->gpsdata.online = 0;
	    return 0;
	} else
	    return ONLINE_SET;
    } else {
	size_t packet_length;
	session->gpsdata.online = timestamp();

	if (session->inbuflen==0 || session->packet_full!=0) {
	    session->gpsdata.d_xmit_time = timestamp();
	    session->packet_full = 0;
	}

	/* can we get a full packet from the device? */
	if (session->device_type)
	    packet_length = (size_t)session->device_type->get_packet(session, (size_t)waiting);
	else {
	    packet_length = (size_t)packet_get(session, (size_t)waiting);
	    if (session->packet_type != BAD_PACKET) {
		gpsd_report(3, 
			    "packet sniff finds type %d\n", 
			    session->packet_type);
		if (session->packet_type == SIRF_PACKET)
		    (void)gpsd_switch_driver(session, "SiRF-II binary");
		else if (session->packet_type == TSIP_PACKET)
		    (void)gpsd_switch_driver(session, "Trimble TSIP");
		else if (session->packet_type == NMEA_PACKET)
		    (void)gpsd_switch_driver(session, "Generic NMEA");
		else if (session->packet_type == ZODIAC_PACKET)
		    (void)gpsd_switch_driver(session, "Zodiac binary");
		session->gpsdata.d_xmit_time = timestamp();
	    } else if (!gpsd_next_hunt_setting(session))
		return ERROR_SET;
	}

	if (packet_length) {
	    /*@ -nullstate @*/
	    if (session->gpsdata.raw_hook)
		session->gpsdata.raw_hook(&session->gpsdata, 
					  (char *)session->outbuffer,
					  packet_length, 2);
	    /*@ -nullstate @*/
	    return handle_packet(session);
	} else
	    return ONLINE_SET;
    }
}

void gpsd_wrap(struct gps_device_t *session)
/* end-of-session wrapup */
{
    gpsd_deactivate(session);
    if (session->gpsdata.gps_device)
	(void)free(session->gpsdata.gps_device);
    /*@i@*/(void)free(session);
}

void gpsd_zero_satellites(/*@out@*/struct gps_data_t *out)
{
    (void)memset(out->PRN,       0, sizeof(out->PRN));
    (void)memset(out->elevation, 0, sizeof(out->elevation));
    (void)memset(out->azimuth,   0, sizeof(out->azimuth));
    (void)memset(out->ss,        0, sizeof(out->ss));
    out->satellites = 0;
}

