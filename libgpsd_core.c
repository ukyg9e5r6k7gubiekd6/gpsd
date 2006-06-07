/* libgpsd_core.c -- direct access to GPSes on serial or USB devices. */
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <netdb.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

#include "gpsd.h"

#if defined(PPS_ENABLE) && defined(TIOCMIWAIT)
#ifndef S_SPLINT_S
#include <pthread.h>	/* pacifies OpenBSD's compiler */
#endif
#endif


int gpsd_switch_driver(struct gps_device_t *session, char* typename)
{
    struct gps_type_t **dp;

    /* make it idempotent */
    if (session->device_type != NULL && 
		strcmp(session->device_type->typename, typename) == 0)
	return 0;

    /*@ -compmempass @*/
    for (dp = gpsd_drivers; *dp; dp++)
	if (strcmp((*dp)->typename, typename) == 0) {
	    gpsd_report(3, "Selecting %s driver...\n", (*dp)->typename);
            if (session->saved_baud == -1)
                session->saved_baud = (int)cfgetispeed(&session->ttyset);
	    if (session->device_type != NULL && session->device_type->wrapup != NULL)
		session->device_type->wrapup(session);
	    /*@i@*/session->device_type = *dp;
	    if (session->device_type->initializer)
		session->device_type->initializer(session);
	    return 1;
	}
    gpsd_report(1, "invalid GPS type \"%s\".\n", typename);
    return 0;
    /*@ +compmempass @*/
}

void gpsd_init(struct gps_device_t *session, struct gps_context_t *context, char *device)
/* initialize GPS polling */
{
    /*@ -mayaliasunique @*/
    strncpy(session->gpsdata.gps_device, device, PATH_MAX);
    /*@ -mustfreeonly @*/
    session->device_type = NULL;	/* start by hunting packets */
    session->rtcmtime = 0;
    /*@ -temptrans @*/
    session->context = context;
    /*@ +temptrans @*/
    /*@ +mayaliasunique @*/
    /*@ +mustfreeonly @*/
    gps_clear_fix(&session->gpsdata.fix);
    session->gpsdata.hdop = NAN;
    session->gpsdata.vdop = NAN;
    session->gpsdata.pdop = NAN;
    session->gpsdata.tdop = NAN;
    session->gpsdata.gdop = NAN;

    /* mark GPS fd closed */
    session->gpsdata.gps_fd = -1;

    /* necessary in case we start reading in the middle of a GPGSV sequence */
    gpsd_zero_satellites(&session->gpsdata);

    /* initialize things for the packet parser */
    packet_reset(session);
}

void gpsd_deactivate(struct gps_device_t *session)
/* temporarily release the GPS device */
{
    gpsd_report(1, "closing GPS=%s (%d)\n", 
		session->gpsdata.gps_device, session->gpsdata.gps_fd);
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
    (void)gpsd_close(session);
}

#if defined(PPS_ENABLE) && defined(TIOCMIWAIT)
static void *gpsd_ppsmonitor(void *arg)
{
    struct gps_device_t *session = (struct gps_device_t *)arg;
    int cycle,duration, state = 0, laststate = -1, unchanged = 0;
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

	if (state == laststate) {
	    if (++unchanged == 10) {
		gpsd_report(1, "TIOCMIWAIT returns unchanged state, ppsmonitor terminates\n");
		break;
	    }
	} else {
	    gpsd_report(5, "carrier-detect on %s changed to %d\n", 
			session->gpsdata.gps_device, state);
	    laststate = state;
	    unchanged = 0;
	}

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
	session->driver.sirf.satcounter = 0;
#endif /* SIRFII_ENABLE */
	session->char_counter = 0;
	session->retry_counter = 0;
	gpsd_report(1, "gpsd_activate: opened GPS (%d)\n", session->gpsdata.gps_fd);
	// session->gpsdata.online = 0;
	session->gpsdata.fix.mode = MODE_NOT_SEEN;
	session->gpsdata.status = STATUS_NO_FIX;
	session->gpsdata.fix.track = NAN;
	session->gpsdata.separation = NAN;
#ifdef BINARY_ENABLE
	session->mag_var = NAN;
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

/*@ -mustdefine @*/
void gpsd_position_fix_dump(struct gps_device_t *session,
			    /*@out@*/char bufp[], size_t len)
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
    }
}
/*@ +mustdefine @*/

static void gpsd_transit_fix_dump(struct gps_device_t *session,
				  char bufp[], size_t len)
{
    struct tm tm;
    time_t intfixtime;

    intfixtime = (time_t)session->gpsdata.fix.time;
    (void)gmtime_r(&intfixtime, &tm);
    /*@ -usedef @*/
    (void)snprintf(bufp, len,
	    "$GPRMC,%02d%02d%02d,%c,%09.4f,%c,%010.4f,%c,%.4f,%.3f,%02d%02d%02d,,",
	    tm.tm_hour, 
	    tm.tm_min, 
	    tm.tm_sec,
	    session->gpsdata.status ? 'A' : 'V',
	    degtodm(fabs(session->gpsdata.fix.latitude)),
	    ((session->gpsdata.fix.latitude > 0) ? 'N' : 'S'),
	    degtodm(fabs(session->gpsdata.fix.longitude)),
	    ((session->gpsdata.fix.longitude > 0) ? 'E' : 'W'),
	    session->gpsdata.fix.speed * MPS_TO_KNOTS,
	    session->gpsdata.fix.track,
	    tm.tm_mday,
	    tm.tm_mon + 1,
	    tm.tm_year % 100);
    /*@ +usedef @*/
    nmea_add_checksum(bufp);
}

static void gpsd_binary_fix_dump(struct gps_device_t *session,
				 char bufp[], size_t len)
{
    gpsd_position_fix_dump(session, bufp, len);
    gpsd_transit_fix_dump(session, bufp + strlen(bufp), len - strlen(bufp));
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
    if (session->packet_type == ZODIAC_PACKET && session->driver.zodiac.Zs[0] != 0) {
	bufp += strlen(bufp);
	bufp2 = bufp;
	strcpy(bufp, "$PRWIZCH");
	for (i = 0; i < ZODIAC_CHANNELS; i++) {
	    len -= snprintf(bufp+strlen(bufp), len,
			  ",%02u,%X", 
			    session->driver.zodiac.Zs[i], 
			    session->driver.zodiac.Zv[i] & 0x0f);
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
    for (i = 0; i < session->device_type->channels; i++) {
	if (session->gpsdata.used[i]) {
	    bufp += strlen(bufp);
	    (void)snprintf(bufp, len-strlen(bufp),
			   "%02d,", session->gpsdata.PRN[i]);
	    j++;
	}
    }
    for (i = j; i < session->device_type->channels; i++) {
	bufp += strlen(bufp);
	(void)strcpy(bufp, ",");
    }
    bufp += strlen(bufp);
#define ZEROIZE(x)	(isnan(x)!=0 ? 0.0 : x)  
    if (session->gpsdata.fix.mode == MODE_NO_FIX)
	(void)strcat(bufp, ",,,");
    else
	(void)snprintf(bufp, len-strlen(bufp),
		       "%.1f,%.1f,%.1f*", 
		       ZEROIZE(session->gpsdata.pdop), 
		       ZEROIZE(session->gpsdata.hdop),
		       ZEROIZE(session->gpsdata.vdop));
    nmea_add_checksum(bufp2);
    bufp += strlen(bufp);
    if (finite(session->gpsdata.fix.eph)
	|| finite(session->gpsdata.fix.epv)
	|| finite(session->gpsdata.epe)) {
        // output PGRME
        // only if realistic
        (void)snprintf(bufp, len-strlen(bufp),
	    "$PGRME,%.2f,%.2f,%.2f",
	    ZEROIZE(session->gpsdata.fix.eph), 
	    ZEROIZE(session->gpsdata.fix.epv), 
	    ZEROIZE(session->gpsdata.epe));
        nmea_add_checksum(bufp);
     }
#undef ZEROIZE
}

#endif /* BINARY_ENABLE */


static void apply_error_model(struct gps_device_t *session)
/* compute errors and derived quantities */
{
    /*
     * Now we compute derived quantities.  This is where the tricky error-
     * modeling stuff goes. Presently we don't know how to derive 
     * time error.
     *
     * Field reports match the theoretical prediction that
     * expected time error should be half the resolution of
     * the GPS clock, so we put the bound of the error
     * in as a constant pending getting it from each driver.
     *
     * Some drivers set the position-error fields.  Only the Zodiacs 
     * report speed error.  Nobody reports track error or climb error.
     */
#define UERE_NO_DGPS	8.0	/* meters, 95% confidence */
#define UERE_WITH_DGPS	2.0	/* meters, 95% confidence */
    double uere = (session->gpsdata.status == STATUS_DGPS_FIX ? UERE_WITH_DGPS : UERE_NO_DGPS);

    session->gpsdata.fix.ept = 0.005;
    if ((session->gpsdata.set & HERR_SET)==0 
	&& (session->gpsdata.set & HDOP_SET)!=0) {
	session->gpsdata.fix.eph = session->gpsdata.hdop * uere;
	session->gpsdata.set |= HERR_SET;
    }
    if ((session->gpsdata.set & VERR_SET)==0 
	&& (session->gpsdata.set & VDOP_SET)!=0) {
	session->gpsdata.fix.epv = session->gpsdata.vdop * uere;
	session->gpsdata.set |= VERR_SET;
    }
    if ((session->gpsdata.set & PERR_SET)==0
	&& (session->gpsdata.set & PDOP_SET)!=0) {
	session->gpsdata.epe = session->gpsdata.pdop * uere;
	session->gpsdata.set |= PERR_SET;
    }
    /*
     * If we have a current fix and an old fix, and the packet handler 
     * didn't set the speed error and climb error members itself, 
     * try to compute them now.
     */
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
	    /*
	     * We compute track error solely from the position of this 
	     * fix and the last one.  The maximum track error, as seen from the
	     * position of last fix, is the angle subtended by the two
	     * most extreme possible error positions of the current fix.
	     * Let the position of the old fix be A and of the new fix B.
	     * We model the view from A as two right triangles ABC and ABD
	     * with BC and BD both having the length of the new fix's 
	     * estimated error.  adj = len(AB), opp = len(BC) = len(BD), 
	     * hyp = len(AC) = len(AD). Yes, this normally leads to 
	     * uncertainties near 180 when we're moving slowly.
	     */
	    session->gpsdata.fix.epd = NAN;
	    if (session->lastfix.mode >= MODE_2D) {
		double adj = earth_distance(
		    session->lastfix.latitude,
		    session->lastfix.longitude,
		    session->gpsdata.fix.latitude,      
		    session->gpsdata.fix.longitude);
		if (adj != 0) {
		    double opp = session->gpsdata.fix.eph;
		    double hyp = sqrt(adj*adj + opp*opp);
		    session->gpsdata.fix.epd = RAD_2_DEG * 2 * asin(opp / hyp);
		}
	    }
	}
    }
}

gps_mask_t gpsd_poll(struct gps_device_t *session)
/* update the stuff in the scoreboard structure */
{
    ssize_t newdata;

    if (session->inbuflen==0)
	session->gpsdata.d_xmit_time = timestamp();

    /* can we get a full packet from the device? */
    if (session->device_type) {
	newdata = session->device_type->get_packet(session);
	session->gpsdata.d_xmit_time = timestamp();
    } else {
	newdata = packet_get(session);
	session->gpsdata.d_xmit_time = timestamp();
	gpsd_report(3, 
		    "packet sniff finds type %d\n", 
		    session->packet_type);
	if (session->packet_type != BAD_PACKET) {
	    switch (session->packet_type) {
#ifdef SIRFII_ENABLE
	    case SIRF_PACKET:
		(void)gpsd_switch_driver(session, "SiRF-II binary");
		break;
#endif /* SIRFII_ENABLE */
#ifdef TSIP_ENABLE
	    case TSIP_PACKET:
		(void)gpsd_switch_driver(session, "Trimble TSIP");
		break;
#endif /* TSIP_ENABLE */
#ifdef NMEA_ENABLE
	    case NMEA_PACKET:
		(void)gpsd_switch_driver(session, "Generic NMEA");
		break;
#endif /* NMEA_ENABLE */
#ifdef ZODIAC_ENABLE
	    case ZODIAC_PACKET:
		(void)gpsd_switch_driver(session, "Zodiac binary");
		break;
#endif /* ZODIAC_ENABLE */
#ifdef EVERMORE_ENABLE
	    case EVERMORE_PACKET:
		(void)gpsd_switch_driver(session, "EverMore binary");
		break;
#endif /* EVERMORE_ENABLE */
#ifdef ITALK_ENABLE
	    case ITALK_PACKET:
		(void)gpsd_switch_driver(session, "iTalk binary");
		break;
#endif /* ITALK_ENABLE */
#ifdef RTCM104_ENABLE
	    case RTCM_PACKET:
		(void)gpsd_switch_driver(session, "RTCM104");
		break;
#endif /* RTCM104_ENABLE */
	    }
	} else if (!gpsd_next_hunt_setting(session))
	    return ERROR_SET;
    }

    /* update the scoreboard structure from the GPS */
    gpsd_report(7, "GPS sent %d new characters\n", newdata);
    if (newdata == -1)	{		/* read error */
	session->gpsdata.online = 0;
	return 0;
    } else if (newdata == 0) {		/* no new data */
	if (session->device_type != NULL && timestamp()>session->gpsdata.online+session->device_type->cycle+1){
		gpsd_report(3, "GPS is offline (%lf sec since data)\n", 
			timestamp() - session->gpsdata.online);
	    session->gpsdata.online = 0;
	    return 0;
	} else
	    return ONLINE_SET;
    } else if (session->outbuflen == 0) {   /* got new data, but no packet */
	    gpsd_report(8, "New data, not yet a packet\n");
	    return ONLINE_SET;
    } else {
	gps_mask_t received, dopmask = 0;
	session->gpsdata.online = timestamp();

	/*@ -nullstate @*/
	if (session->gpsdata.raw_hook)
	    session->gpsdata.raw_hook(&session->gpsdata, 
				      (char *)session->outbuffer,
				      (size_t)session->outbuflen, 2);
	/*@ -nullstate @*/
	session->gpsdata.sentence_time = NAN;
	session->gpsdata.sentence_length = session->outbuflen;
	session->gpsdata.d_recv_time = timestamp();

	/* Get data from current packet into the newdata structure */
	if (session->device_type != NULL && session->device_type->parse_packet!=NULL)
	    received = session->device_type->parse_packet(session);
	else
	    received = 0;	/* it was all done in the packet getter */

	/* Clear fix data at start of cycle */
	if ((received & CYCLE_START_SET)!=0) {
	    gps_clear_fix(&session->gpsdata.fix);
	    session->gpsdata.set &=~ FIX_SET;
	}
	/*
	 * Compute fix-quality data from the satellite positions.
	 * This may be overridden by DOPs reported from the packet we just got.
	 */
	if (session->gpsdata.fix.mode > MODE_NO_FIX 
		    && (session->gpsdata.set & SATELLITE_SET) != 0
		    && session->gpsdata.satellites > 0)
	    dopmask = dop(&session->gpsdata);
	/* Merge in the data from the current packet. */
	gps_merge_fix(&session->gpsdata.fix, received, &session->gpsdata.newdata);
	session->gpsdata.set = ONLINE_SET | dopmask | received;

	/* count good fixes */
	if (session->gpsdata.status > STATUS_NO_FIX) 
	    session->context->fixcnt++;

	/* compute errors and derived quantities */
	apply_error_model(session);

	/* save the old fix for later uncertainty computations */
	if (session->gpsdata.fix.mode >= MODE_2D)
	    (void)memcpy(&session->lastfix, 
		     &session->gpsdata.fix, 
		     sizeof(struct gps_fix_t));

	session->gpsdata.d_decode_time = timestamp();

	/* also copy the sentence up to clients in raw mode */
	if (session->packet_type == NMEA_PACKET)
	    session->gpsdata.raw_hook(&session->gpsdata,
				      (char *)session->outbuffer,
				      strlen((char *)session->outbuffer), 1);
	else {
	    char buf2[MAX_PACKET_LENGTH*3+2];

	    buf2[0] = '\0';
#ifdef RTCM104_ENABLE
	    if ((session->gpsdata.set & RTCM_SET) != 0)
		rtcm_dump(session, 
			  buf2+strlen(buf2), 
			  (sizeof(buf2)-strlen(buf2)));
	    else
#endif /* RTCM104_ENABLE */
#ifdef BINARY_ENABLE
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
#endif /* BINARY_ENABLE */
	    if (buf2[0] != '\0') {
		gpsd_report(3, "<= GPS: %s", buf2);
		if (session->gpsdata.raw_hook)
		    session->gpsdata.raw_hook(&session->gpsdata, 
					      buf2, strlen(buf2), 1);
	    }
	}

	dgnss_report(session);

	return session->gpsdata.set;
    }
}

void gpsd_wrap(struct gps_device_t *session)
/* end-of-session wrapup */
{
    gpsd_deactivate(session);
}

void gpsd_zero_satellites(/*@out@*/struct gps_data_t *out)
{
    (void)memset(out->PRN,       0, sizeof(out->PRN));
    (void)memset(out->elevation, 0, sizeof(out->elevation));
    (void)memset(out->azimuth,   0, sizeof(out->azimuth));
    (void)memset(out->ss,        0, sizeof(out->ss));
    out->satellites = 0;
}

char /*@ observer @*/ *gpsd_hexdump(const void *binbuf, size_t binbuflen)
{
    static char hexbuf[MAX_PACKET_LENGTH*2+1];
    size_t i;
    size_t len = (size_t)((binbuflen > MAX_PACKET_LENGTH) ? MAX_PACKET_LENGTH : binbuflen);
    const char *ibuf = (const char *)binbuf;
    memset(hexbuf, 0, sizeof(hexbuf));

    for (i = 0; i < len; i++) {
	(void)snprintf(hexbuf + (2 * i), 3, "%02x", (unsigned int)(ibuf[i]&0xff));
    }
    return hexbuf;
}

#ifdef BINARY_ENABLE
/*@ -usedef @*/
void gpsd_interpret_subframe(struct gps_device_t *session,unsigned int words[])
/* extract leap-second from RTCM-104 subframe data */
{
    /*
     * Heavy black magic begins here!
     *
     * A description of how to decode these bits is at
     * <http://home-2.worldonline.nl/~samsvl/nav2eu.htm>
     *
     * We're after subframe 4 page 18 word 9, the leap year correction.
     * We assume that the chip is presenting clean data that has been
     * parity-checked.
     *
     * To date this code has been tested only on SiRFs.  It's in the
     * core because other chipsets reporting only GPS time but with 
     * the capability to read subframe data may want it.
     */
    int i;
    unsigned int pageid, subframe, leap;
    gpsd_report(4, 
		"50B (raw): %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x\n", 
		words[0], words[1], words[2], words[3], words[4], 
		words[5], words[6], words[7], words[8], words[9]);
    /*
     * Mask off the high 2 bits and shift out the 6 parity bits.
     * Once we've filtered, we can ignore the TEL and HOW words.
     * We don't need to check parity here, the SiRF chipset does
     * that and throws a subframe error if the parity is wrong.
     */
    for (i = 0; i < 10; i++)
	words[i] = (words[i]  & 0x3fffffff) >> 6;
    /*
     * "First, throw away everything that doesn't start with 8b or
     * 74. more correctly the first byte should be 10001011. If
     * it's 01110100, then you have a subframe with inverted
     * polarity and each byte needs to be xored against 0xff to
     * remove the inversion."
     */
    words[0] &= 0xff0000;
    if (words[0] != 0x8b0000 && words[0] != 0x740000)
	return;
    if (words[0] == 0x740000)
	for (i = 1; i < 10; i++)
	    words[i] ^= 0xffffff;
    /*
     * The subframe ID is in the Hand Over Word (page 80) 
     */
    subframe = ((words[1] >> 2) & 0x07);
    /* we're not interested in anything but subframe 4 */
    if (subframe != 4)
	return;
    /*
     * Pages 66-76a,80 of ICD-GPS-200 are the subframe structures.
     * Subframe 4 page 18 is on page 74.
     * See page 105 for the mapping between magic SVIDs and pages.
     */
    pageid = (words[2] & 0x3F0000) >> 16;
    gpsd_report(2, "Subframe 4 SVID is %d\n", pageid);
    if (pageid == 56) {	/* magic SVID for page 18 */
	/* once we've filtered, we can ignore the TEL and HOW words */
	gpsd_report(2, "50B: SF=%d %06x %06x %06x %06x %06x %06x %06x %06x\n", 
		    subframe,
		    words[2], words[3], words[4], words[5], 
		    words[6], words[7], words[8], words[9]);
	leap = (words[8] & 0xff0000) >> 16;
	/*
	 * On SiRFs, there appears to be some bizarre bug that
	 * randomly causes this field to come out two's-complemented.
	 * This could very well be a general problem; work around it.
	 * At the current expected rate of issuing leap-seconds this
	 * kluge won't bite until about 2070, by which time the
	 * vendors had better have fixed their damn firmware...
	 *
	 * Carl: ...I am unsure, and suggest you
	 * experiment.  The D30 bit is in bit 30 of the 32-bit
	 * word (next to MSB), and should signal an inverted
	 * value when it is one coming over the air.  But if
	 * the bit is set and the word decodes right without
	 * inversion, then we properly caught it.  Cases where
	 * you see subframe 6 rather than 1 means we should
	 * have done the inversion but we did not.  Some other
	 * things you can watch for: in any subframe, the
	 * second word (HOW word) should have last 2 parity
	 * bits 00 -- there are bits within the rest of the
	 * word that are set as required to ensure that.  The
	 * same goes for word 10.  That means that both words
	 * 1 and 3 (the words that immediately follow words 10
	 * and 2, respectively) should always be uninverted.
	 * In these cases, the D29 and D30 from the previous
	 * words, found in the two MSBs of the word, should
	 * show 00 -- if they don't then you may find an
	 * unintended inversion due to noise on the data link.
	 */
	if (leap > 128)
	    leap ^= 0xff;
	gpsd_report(2, "leap-seconds is %d\n", leap);
	session->context->leap_seconds = (int)leap;
	session->context->valid |= LEAP_SECOND_VALID;
    }
}
/*@ +usedef @*/
#endif /* BINARY_ENABLE */

