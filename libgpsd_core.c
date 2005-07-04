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

#include "config.h"
#if defined(PPS_ENABLE) && defined(TIOCMIWAIT)
#include <pthread.h>
#endif

#include "gpsd.h"

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
}

void gpsd_deactivate(struct gps_device_t *session)
/* temporarily release the GPS device */
{
    gpsd_report(1, "closing GPS=%s (%d)\n", 
		session->gpsdata.gps_device, session->gpsdata.gps_fd);
    (void)gpsd_close(session);
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
	session->sirf.satcounter = 0;
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
    if (session->packet_type == ZODIAC_PACKET && session->zodiac.Zs[0] != 0) {
	bufp += strlen(bufp);
	bufp2 = bufp;
	strcpy(bufp, "$PRWIZCH");
	for (i = 0; i < ZODIAC_CHANNELS; i++) {
	    len -= snprintf(bufp+strlen(bufp), len,
			  ",%02u,%X", 
			    session->zodiac.Zs[i], 
			    session->zodiac.Zv[i] & 0x0f);
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
    for (i = 0; i < NMEA_CHANNELS; i++) {
	if (session->gpsdata.used[i]) {
	    bufp += strlen(bufp);
	    (void)snprintf(bufp, len-strlen(bufp),
			   "%02d,", session->gpsdata.PRN[i]);
	    j++;
	}
    }
    for (i = j; i < NMEA_CHANNELS; i++) {
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
	session->gpsdata.seen_sentences |= PGRME;
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
    /* only used if the GPS doesn't report estimated position error itself */
#define UERE_NO_DGPS	8.0	/* meters */
#define UERE_WITH_DGPS	2.0	/* meters */
    double uere;
    int i;
    bool waas_active = false;
    bool dgps_active = (session->context->dsock<0);

    /*
     * If we have a satellite picture that includes a WAAS/Egnos PRN,
     * it will ship DGPS corrections.  Note: this assumes only WAAS-capable
     * GPSes can see these PRNs at all.
     */
    if (session->gpsdata.set & SATELLITE_SET)
	for (i = 0; i < session->gpsdata.satellites; i++)
	    if (session->gpsdata.PRN[i] > 32)
		waas_active = true;

    uere = ((dgps_active || waas_active) ? UERE_NO_DGPS : UERE_WITH_DGPS);

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
    ssize_t packet_length;

    if (session->inbuflen==0)
	session->gpsdata.d_xmit_time = timestamp();

    /* can we get a full packet from the device? */
    if (session->device_type)
	packet_length = session->device_type->get_packet(session);
    else {
	packet_length = packet_get(session);
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

    /* update the scoreboard structure from the GPS */
    gpsd_report(7, "GPS sent %d characters\n", packet_length);
    if (packet_length == BAD_PACKET)
	return 0;
    else if (packet_length == 0) {
	if (session->device_type != NULL && timestamp()>session->gpsdata.online+session->device_type->cycle+1){
	    session->gpsdata.online = 0;
	    return 0;
	} else
	    return ONLINE_SET;
    } else {
	gps_mask_t received, dopmask = 0;
	session->gpsdata.online = timestamp();

	/*@ -nullstate @*/
	if (session->gpsdata.raw_hook)
	    session->gpsdata.raw_hook(&session->gpsdata, 
				      (char *)session->outbuffer,
				      (size_t)packet_length, 2);
	/*@ -nullstate @*/
	session->gpsdata.sentence_time = NAN;
	session->gpsdata.sentence_length = session->outbuflen;
	session->gpsdata.d_recv_time = timestamp();

	/* Get data from current packet into the newdata structure */
	received = session->device_type->parse_packet(session);

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

	/* count all packets and good fixes */
	session->counter++;
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

	dgpsip_report(session);

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

