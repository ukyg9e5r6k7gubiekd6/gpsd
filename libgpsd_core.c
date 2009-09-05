/* $Id$ */
/* libgpsd_core.c -- direct access to GPSes on serial or USB devices. */
#include <sys/time.h>
#include <sys/ioctl.h>
#ifndef S_SPLINT_S
#include <sys/socket.h>
#endif /* S_SPLINT_S */
#include <sys/time.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <netdb.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

#include "gpsd_config.h"
#include "gpsd.h"

#if defined(PPS_ENABLE) && defined(TIOCMIWAIT)
#ifndef S_SPLINT_S
#include <pthread.h>	/* pacifies OpenBSD's compiler */
#endif
#endif


int gpsd_switch_driver(struct gps_device_t *session, char* type_name)
{
    const struct gps_type_t **dp;

    gpsd_report(LOG_PROG, "switch_driver(%s) called...\n", type_name);
    if (session->device_type != NULL &&
	strcmp(session->device_type->type_name, type_name) == 0) {
#ifdef ALLOW_RECONFIGURE
	gpsd_report(LOG_PROG, "Reconfiguring for %s...\n", session->device_type->type_name);
	if (session->enable_reconfigure
		&& session->device_type->configurator != NULL)
	    session->device_type->configurator(session, 0);
#endif /* ALLOW_RECONFIGURE */
	return 0;
    }

    /*@ -compmempass @*/
    for (dp = gpsd_drivers; *dp; dp++)
	if (strcmp((*dp)->type_name, type_name) == 0) {
	    gpsd_report(LOG_PROG, "selecting %s driver...\n", (*dp)->type_name);
	    gpsd_assert_sync(session);
	    /*@i@*/session->device_type = *dp;
	    session->gpsdata.dev.mincycle = session->device_type->min_cycle;
	    if (!session->context->readonly && session->device_type->probe_subtype != NULL)
		session->device_type->probe_subtype(session, session->packet.counter = 0);
#ifdef ALLOW_RECONFIGURE
	    if (session->enable_reconfigure
			&& session->device_type->configurator != NULL) {
		gpsd_report(LOG_PROG, "configuring for %s...\n", session->device_type->type_name);
		session->device_type->configurator(session, 0);
	    }
#endif /* ALLOW_RECONFIGURE */
	    return 1;
	}
    gpsd_report(LOG_ERROR, "invalid GPS type \"%s\".\n", type_name);
    return 0;
    /*@ +compmempass @*/
}


void gpsd_init(struct gps_device_t *session, struct gps_context_t *context, char *device)
/* initialize GPS polling */
{
    /*@ -mayaliasunique @*/
    if (device != NULL)
	(void)strlcpy(session->gpsdata.dev.path, device, sizeof(session->gpsdata.dev.path));
    /*@ -mustfreeonly @*/
    session->device_type = NULL;	/* start by hunting packets */
    session->observed = 0;
    session->rtcmtime = 0;
    /*@ -temptrans @*/
    session->context = context;
    /*@ +temptrans @*/
    /*@ +mayaliasunique @*/
    /*@ +mustfreeonly @*/
    gps_clear_fix(&session->gpsdata.fix);
    session->gpsdata.set &=~ (FIX_SET | DOP_SET);
    session->gpsdata.hdop = NAN;
    session->gpsdata.vdop = NAN;
    session->gpsdata.pdop = NAN;
    session->gpsdata.tdop = NAN;
    session->gpsdata.gdop = NAN;
    session->gpsdata.epe = NAN;
    session->mag_var = NAN;
    session->gpsdata.dev.cycle = session->gpsdata.dev.mincycle = 1; 

    /* tty-level initialization */
    gpsd_tty_init(session);

    /* necessary in case we start reading in the middle of a GPGSV sequence */
    gpsd_zero_satellites(&session->gpsdata);

    /* initialize things for the packet parser */
    packet_reset(&session->packet);
}

void gpsd_deactivate(struct gps_device_t *session)
/* temporarily release the GPS device */
{
#ifdef NTPSHM_ENABLE
    (void)ntpshm_free(session->context, session->shmindex);
    session->shmindex = -1;
# ifdef PPS_ENABLE
    (void)ntpshm_free(session->context, session->shmTimeP);
    session->shmTimeP = -1;
# endif /* PPS_ENABLE */
#endif /* NTPSHM_ENABLE */
#ifdef ALLOW_RECONFIGURE
    if (session->enable_reconfigure
	&& session->device_type != NULL
	&& session->device_type->revert != NULL) {
	session->device_type->revert(session);
	session->enable_reconfigure = false;
    }
    if (session->device_type!=NULL) {
	if (session->back_to_nmea && session->device_type->mode_switcher!=NULL)
	    session->device_type->mode_switcher(session, 0);
	if (session->device_type->wrapup!=NULL)
	    session->device_type->wrapup(session);
    }
#endif /* ALLOW_RECONFIGURE */
    gpsd_report(LOG_INF, "closing GPS=%s (%d)\n",
		session->gpsdata.dev.path, session->gpsdata.gps_fd);
    (void)gpsd_close(session);
}

#if defined(PPS_ENABLE) && defined(TIOCMIWAIT)
static /*@null@*/void *gpsd_ppsmonitor(void *arg)
{
    struct gps_device_t *session = (struct gps_device_t *)arg;
    int cycle,duration, state = 0, laststate = -1, unchanged = 0;
    struct timeval tv;
    struct timeval pulse[2] = {{0,0},{0,0}};
    int pps_device = TIOCM_CAR;

#if defined(PPS_ON_CTS)
    pps_device = TIOCM_CTS;
#endif

    gpsd_report(LOG_PROG, "Create Thread gpsd_ppsmonitor\n");

    /* wait for status change on the device's carrier-detect line */
    while (ioctl(session->gpsdata.gps_fd, TIOCMIWAIT, pps_device) == 0) {
	(void)gettimeofday(&tv,NULL);
	/*@ +ignoresigns */
	if (ioctl(session->gpsdata.gps_fd, TIOCMGET, &state) != 0)
	    break;
	/*@ -ignoresigns */

	state = (int)((state & pps_device) != 0);

	if (state == laststate) {
	    if (++unchanged == 10) {
		gpsd_report(LOG_WARN, "TIOCMIWAIT returns unchanged state, ppsmonitor terminates\n");
		break;
	    }
	} else {
	    gpsd_report(LOG_RAW, "pps-detect (%s) on %s changed to %d\n",
			((pps_device==TIOCM_CAR) ? "DCD" : "CTS"),
			  session->gpsdata.dev.path, state);
	    laststate = state;
	    unchanged = 0;
	}

	/*@ +boolint @*/
	if ( session->context->fixcnt > 3 ) {
	    /* Garmin doc says PPS is valid after four good fixes. */
	    /*
	     * The PPS pulse is normally a short pulse with a frequency of
	     * 1 Hz, and the UTC second is defined by the front edge. But we
	     * don't know the polarity of the pulse (different receivers
	     * emit different polarities). The duration variable is used to
	     * determine which way the pulse is going. The code assumes
	     * that the UTC second is changing when the signal has not
	     * been changing for at least 800ms, i.e. it assumes the duty
	     * cycle is at most 20%.
	     *
	     * Some GPS instead output a square wave that is 0.5 Hz and each
	     * edge denotes the start of a second.
	     *
	     * A few stupid GPS, like the Furuno GPSClock, output a 1.0 Hz 
	     * square wave where the leading edge is the start of a second
	     *
	     * 5Hz GPS (Garmin 18-5Hz) pulses at 5Hz. Set the pulse length to 
	     * 40ms which gives a 160ms pulse before going high.
	     *
	     */
#define timediff(x, y)	(int)((x.tv_sec-y.tv_sec)*1000000+x.tv_usec-y.tv_usec)
	    cycle = timediff(tv, pulse[state]);
	    duration = timediff(tv, pulse[state == 0]);
#undef timediff

#define min_interval(center) ((center) * 9 / 10)
#define max_interval(center) ((center) * 11 / 10)
#define in_interval(x, center) ((x) > min_interval(center) && (x) < max_interval(center))

	    if (in_interval(cycle, 200000)) {
		/* 5Hz cycle */
		/* looks like 5hz PPS pulse */
		if (duration > min_interval(160000))
		    (void)ntpshm_pps(session, &tv, 5);
		gpsd_report(LOG_RAW, "5Hz PPS pulse. cycle: %d, duration: %d\n",
			cycle, duration);
	    } else if (in_interval(cycle, 1000000)) {
		/* looks like PPS pulse or square wave */
		if (in_interval(duration, 500000)
#if defined(NMEA_ENABLE) && defined(GPSCLOCK_ENABLE)
		  && session->driver.nmea.ignore_trailing_edge
#endif /* GPSCLOCK_ENABLE */
		  ) {
		    /* looks like 1.0 Hz square wave, ignore trailing edge */
		    if (state == 1) {
			 (void)ntpshm_pps(session, &tv, 1);
		    }
		} else if (duration > min_interval(800000)) {
		    /* looks like PPS pulse */
		    (void)ntpshm_pps(session, &tv, 1);
		}
		gpsd_report(LOG_RAW, "PPS pulse. cycle: %d, duration: %d\n",
			cycle, duration);

	    } else if (in_interval(cycle, 2000000)) {
		/* looks like 0.5 Hz square wave */
		(void)ntpshm_pps(session, &tv, 1);
		gpsd_report(LOG_RAW, "PPS square wave. cycle: %d, duration: %d\n",
			cycle, duration);
	    } else {
		gpsd_report(LOG_INF, "PPS pulse rejected.  cycle: %d, duration: %d\n",
			cycle, duration);
	    }
	} else {
		gpsd_report(LOG_INF, "PPS pulse rejected. No fix.\n");
	}
	/*@ -boolint @*/

#undef min_interval
#undef max_interval
#undef in_interval

	pulse[state] = tv;
    }

    return NULL;
}
#endif /* PPS_ENABLE */

/*@ -branchstate @*/
int gpsd_activate(struct gps_device_t *session, bool reconfigurable)
/* acquire a connection to the GPS device */
{
    if (gpsd_open(session) < 0)
	return -1;
    else {
#ifdef NON_NMEA_ENABLE
	const struct gps_type_t **dp;

	/*@ -mustfreeonly @*/
	for (dp = gpsd_drivers; *dp; dp++) {
	    (void)tcflush(session->gpsdata.gps_fd, TCIOFLUSH);  /* toss stale data */
	    if ((*dp)->probe_detect!=NULL && (*dp)->probe_detect(session)!=0) {
		gpsd_report(LOG_PROG, "probe found %s driver...\n", (*dp)->type_name);
		session->device_type = *dp;
		gpsd_assert_sync(session);
		goto foundit;
	    }
 	}
	/*@ +mustfreeonly @*/
	gpsd_report(LOG_PROG, "no probe matched...\n");
    foundit:
#ifdef ALLOW_RECONFIGURE
	session->enable_reconfigure = reconfigurable;
#endif /* ALLOW_RECONFIGURE */
#endif /* NON_NMEA_ENABLE */
	session->gpsdata.online = timestamp();
#ifdef SIRF_ENABLE
	session->driver.sirf.satcounter = 0;
#endif /* SIRF_ENABLE */
	session->packet.char_counter = 0;
	session->packet.retry_counter = 0;
	gpsd_report(LOG_INF, "gpsd_activate(%d): opened GPS (fd %d)\n", reconfigurable, session->gpsdata.gps_fd);
	// session->gpsdata.online = 0;
	session->gpsdata.fix.mode = MODE_NOT_SEEN;
	session->gpsdata.status = STATUS_NO_FIX;
	session->gpsdata.fix.track = NAN;
	session->gpsdata.separation = NAN;
	session->mag_var = NAN;
	session->releasetime = 0;

	/* clear driver subtype field and private data union */
	session->subtype[0] = '\0';
	memset(&session->driver, '\0', sizeof(session->driver));
	/* if we know the device type, probe for subtype and configure it */
	if (session->device_type != NULL) {
	    if (!session->context->readonly && session->device_type->probe_subtype !=NULL)
		session->device_type->probe_subtype(session, session->packet.counter = 0);
#ifdef ALLOW_RECONFIGURE
	    if (reconfigurable) {
		if (session->device_type->configurator != NULL)
		    session->device_type->configurator(session, session->packet.counter);
	    }
#endif /* ALLOW_RECONFIGURE */
	}
    }

    return session->gpsdata.gps_fd;
}
/*@ +branchstate @*/

void ntpd_link_activate(struct gps_device_t *session)
{
#if defined(PPS_ENABLE) && defined(TIOCMIWAIT)
    pthread_t pt;
#endif /* defined(PPS_ENABLE) && defined(TIOCMIWAIT) */

#ifdef NTPSHM_ENABLE
    /* If we are talking to ntpd, allocate a shared-memory segment for "NMEA" time data */
    if (session->context->enable_ntpshm)
	session->shmindex = ntpshm_alloc(session->context);

#if defined(PPS_ENABLE) && defined(TIOCMIWAIT)
    /* If we also have the 1pps capability, allocate a shared-memory segment for
     * the 1pps time data and launch a thread to capture the 1pps transitions
     */
    if (session->shmindex >= 0 && session->context->shmTimePPS)
	if ((session->shmTimeP = ntpshm_alloc(session->context)) >= 0)
	    /*@i1@*/(void)pthread_create(&pt,NULL,gpsd_ppsmonitor,(void *)session);

#endif /* defined(PPS_ENABLE) && defined(TIOCMIWAIT) */
#endif /* NTPSHM_ENABLE */
}

char /*@observer@*/ *gpsd_id(/*@in@*/struct gps_device_t *session)
/* full ID of the device for reports, including subtype */
{
    static char buf[128];
    if ((session == NULL) || (session->device_type == NULL) ||
	(session->device_type->type_name == NULL))
	return "unknown,";
    (void)strlcpy(buf, session->device_type->type_name, sizeof(buf));
    if (session->subtype[0] != '\0') {
	(void)strlcat(buf, " ", sizeof(buf));
	(void)strlcat(buf, session->subtype, sizeof(buf));
    }
    return(buf);
}

#if defined(BINARY_ENABLE) || defined(RTCM104V2_ENABLE) || defined(NTRIP_ENABLE)
/*
 * Support for generic binary drivers.  These functions dump NMEA for passing
 * to the client in raw mode.  They assume that (a) the public gps.h structure 
 * members are in a valid state, (b) that the private members hours, minutes, 
 * and seconds have also been filled in, (c) that if the private member
 * mag_var is not NAN it is a magnetic variation in degrees that should be
 * passed on, and (d) if the private member separation does not have the
 * value NAN, it is a valid WGS84 geoidal separation in meters for the fix.
 */

static double degtodm(double angle)
/* decimal degrees to GPS-style, degrees first followed by minutes */
{
    double fraction, integer;
    fraction = modf(angle, &integer);
    return floor(angle) * 100 + fraction * 60;
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
		session->gpsdata.status,
		session->gpsdata.satellites_used);
	if (isnan(session->gpsdata.hdop))
	    (void)strlcat(bufp, ",", len);
	else
	    (void)snprintf(bufp+strlen(bufp), len-strlen(bufp),
			   "%.2f,",session->gpsdata.hdop);
	if (isnan(session->gpsdata.fix.altitude))
	    (void)strlcat(bufp, ",", len);
	else
	    (void)snprintf(bufp+strlen(bufp), len-strlen(bufp),
			   "%.2f,M,", session->gpsdata.fix.altitude);
	if (isnan(session->gpsdata.separation))
	    (void)strlcat(bufp, ",", len);
	else
	    (void)snprintf(bufp+strlen(bufp), len-strlen(bufp),
			   "%.3f,M,", session->gpsdata.separation);
	if (isnan(session->mag_var))
	    (void)strlcat(bufp, ",", len);
	else {
	    (void)snprintf(bufp+strlen(bufp),
			   len-strlen(bufp),
			   "%3.2f,", fabs(session->mag_var));
	    (void)strlcat(bufp, (session->mag_var > 0) ? "E": "W", len);
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

    tm.tm_mday = tm.tm_mon = tm.tm_year = tm.tm_hour = tm.tm_min = tm.tm_sec = 0;
    if (isnan(session->gpsdata.fix.time)==0) {
	intfixtime = (time_t)session->gpsdata.fix.time;
	(void)gmtime_r(&intfixtime, &tm);
	tm.tm_mon++;
	tm.tm_year %= 100;
    }
#define ZEROIZE(x)	(isnan(x)!=0 ? 0.0 : x)
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
	    ZEROIZE(session->gpsdata.fix.speed * MPS_TO_KNOTS),
	    ZEROIZE(session->gpsdata.fix.track),
	    tm.tm_mday,
	    tm.tm_mon,
	    tm.tm_year);
    /*@ +usedef @*/
#undef ZEROIZE
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
		    ",%02d,%02d,%03d,%02.0f",
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
    if (session->packet.type == ZODIAC_PACKET && session->driver.zodiac.Zs[0] != 0) {
	bufp += strlen(bufp);
	bufp2 = bufp;
	(void)strlcpy(bufp, "$PRWIZCH", len);
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
			   "%02d,", session->gpsdata.used[i]);
	    j++;
	}
    }
    for (i = j; i < session->device_type->channels; i++) {
	bufp += strlen(bufp);
	(void)strlcpy(bufp, ",", len);
    }
    bufp += strlen(bufp);
#define ZEROIZE(x)	(isnan(x)!=0 ? 0.0 : x)
    if (session->gpsdata.fix.mode == MODE_NO_FIX)
	(void)strlcat(bufp, ",,,", len);
    else
	(void)snprintf(bufp, len-strlen(bufp),
		       "%.1f,%.1f,%.1f*",
		       ZEROIZE(session->gpsdata.pdop),
		       ZEROIZE(session->gpsdata.hdop),
		       ZEROIZE(session->gpsdata.vdop));
    nmea_add_checksum(bufp2);
    bufp += strlen(bufp);
    if (finite(session->gpsdata.fix.epx)
	&& finite(session->gpsdata.fix.epy)
	&& finite(session->gpsdata.fix.epv)
	&& finite(session->gpsdata.epe)) {
	struct tm tm;
	time_t intfixtime;

	tm.tm_hour = tm.tm_min = tm.tm_sec = 0;
	if (isnan(session->gpsdata.fix.time)==0) {
	    intfixtime = (time_t)session->gpsdata.fix.time;
	    (void)gmtime_r(&intfixtime, &tm);
	}
	(void)snprintf(bufp, len-strlen(bufp),
		       "$GPGBS,%02d%02d%02d,%.2f,M,%.2f,M,%.2f,M",
		       tm.tm_hour, tm.tm_min, tm.tm_sec,
		       ZEROIZE(session->gpsdata.fix.epx),
		       ZEROIZE(session->gpsdata.fix.epy),
		       ZEROIZE(session->gpsdata.fix.epv));
	nmea_add_checksum(bufp);
    }
#undef ZEROIZE
}

static void gpsd_binary_dump(struct gps_device_t *session,
			      char bufp[], size_t len)
{
    if ((session->gpsdata.set & LATLON_SET) != 0)
	gpsd_binary_fix_dump(session, bufp+strlen(bufp), len-strlen(bufp));
    if ((session->gpsdata.set & (DOP_SET | ERR_SET)) != 0)
	gpsd_binary_quality_dump(session, bufp+strlen(bufp), len-strlen(bufp));
    if ((session->gpsdata.set & SATELLITE_SET) != 0)
	gpsd_binary_satellite_dump(session,bufp+strlen(bufp),len-strlen(bufp));
}

#endif /* BINARY_ENABLE */


void gpsd_error_model(struct gps_device_t *session,
		      struct gps_fix_t *fix, struct gps_fix_t *oldfix)
/* compute errors and derived quantities */
{
    /*
     * Now we compute derived quantities.  This is where the tricky error-
     * modeling stuff goes. Presently we don't know how to derive
     * time error.
     *
     * Some drivers set the position-error fields.  Only the Zodiacs
     * report speed error.  Nobody reports track error or climb error.
     *
     * The UERE constants are our assumption about the base error of
     * GPS fixes in different directions.
     */
#define H_UERE_NO_DGPS		15.0	/* meters, 95% confidence */
#define H_UERE_WITH_DGPS	3.75	/* meters, 95% confidence */
#define V_UERE_NO_DGPS		23.0	/* meters, 95% confidence */
#define V_UERE_WITH_DGPS	5.75	/* meters, 95% confidence */
#define P_UERE_NO_DGPS		19.0	/* meters, 95% confidence */
#define P_UERE_WITH_DGPS	4.75	/* meters, 95% confidence */
    double h_uere, v_uere, p_uere;

    if (NULL == session)
	return;

    h_uere = (session->gpsdata.status == STATUS_DGPS_FIX ? H_UERE_WITH_DGPS : H_UERE_NO_DGPS);
    v_uere = (session->gpsdata.status == STATUS_DGPS_FIX ? V_UERE_WITH_DGPS : V_UERE_NO_DGPS);
    p_uere = (session->gpsdata.status == STATUS_DGPS_FIX ? P_UERE_WITH_DGPS : P_UERE_NO_DGPS);

    /*
     * Field reports match the theoretical prediction that
     * expected time error should be half the resolution of
     * the GPS clock, so we put the bound of the error
     * in as a constant pending getting it from each driver.
     */
    if (isnan(fix->ept)!=0)
	fix->ept = 0.005;
    /* Other error computations depend on having a valid fix */
    if (fix->mode >= MODE_2D) {
	if (isnan(fix->epx)!=0 && finite(session->gpsdata.hdop)!=0)
		fix->epx = session->gpsdata.hdop * h_uere;

	if (isnan(fix->epy)!=0 && finite(session->gpsdata.hdop)!=0)
		fix->epy = session->gpsdata.hdop * h_uere;

	if ((fix->mode >= MODE_3D)
		&& isnan(fix->epv)!=0 && finite(session->gpsdata.vdop)!=0)
	    fix->epv = session->gpsdata.vdop * v_uere;

	if (isnan(session->gpsdata.epe)!=0 && finite(session->gpsdata.pdop)!=0)
	    session->gpsdata.epe = session->gpsdata.pdop * p_uere;
	else
	    session->gpsdata.epe = NAN;

	/*
	 * If we have a current fix and an old fix, and the packet handler 
	 * didn't set the speed error and climb error members itself, 
	 * try to compute them now.
	 */
	if (isnan(fix->eps)!=0)
	{
	    if (oldfix->mode > MODE_NO_FIX && fix->mode > MODE_NO_FIX
			&& isnan(oldfix->epx)==0 && isnan(oldfix->epy)==0
			&& isnan(oldfix->time)==0 && isnan(oldfix->time)==0
			&& fix->time > oldfix->time) {
		double t = fix->time-oldfix->time;
		double e = EMIX(oldfix->epx,oldfix->epy) + EMIX(fix->epx,fix->epy);
		fix->eps = e/t;
	    } else
		fix->eps = NAN;
	}
	if ((fix->mode >= MODE_3D)
		&& isnan(fix->epc)!=0 && fix->time > oldfix->time) {
	    if (oldfix->mode > MODE_3D && fix->mode > MODE_3D) {
		double t = fix->time-oldfix->time;
		double e = oldfix->epv + fix->epv;
		/* if vertical uncertainties are zero this will be too */
		fix->epc = e/t;
	    }
	    /*
	     * We compute a track error estimate solely from the
	     * position of this fix and the last one.  The maximum
	     * track error, as seen from the position of last fix, is
	     * the angle subtended by the two most extreme possible
	     * error positions of the current fix; the expected track
	     * error is half that.  Let the position of the old fix be
	     * A and of the new fix B.  We model the view from A as
	     * two right triangles ABC and ABD with BC and BD both
	     * having the length of the new fix's estimated error.
	     * adj = len(AB), opp = len(BC) = len(BD), hyp = len(AC) =
	     * len(AD). This leads to spurious uncertainties
	     * near 180 when we're moving slowly; to avoid reporting
	     * garbage, throw back NaN if the distance from the previous
	     * fix is less than the error estimate.
	     */
	    fix->epd = NAN;
	    if (oldfix->mode >= MODE_2D) {
		double adj = earth_distance(
		    oldfix->latitude, oldfix->longitude,
		    fix->latitude, fix->longitude);
		if (isnan(adj)==0 && adj > EMIX(fix->epx, fix->epy)) {
		    double opp = EMIX(fix->epx, fix->epy);
		    double hyp = sqrt(adj*adj + opp*opp);
		    fix->epd = RAD_2_DEG * 2 * asin(opp / hyp);
		}
	    }
	}
    }

    /* save old fix for later error computations */
    /*@ -mayaliasunique @*/
    if (fix->mode >= MODE_2D)
	(void)memcpy(oldfix, fix, sizeof(struct gps_fix_t));
    /*@ +mayaliasunique @*/
}

gps_mask_t gpsd_poll(struct gps_device_t *session)
/* update the stuff in the scoreboard structure */
{
    ssize_t newlen;
    bool first_sync = false;

    gps_clear_fix(&session->gpsdata.fix);

    if (session->packet.inbuflen==0)
	session->gpsdata.d_xmit_time = timestamp();

    /* can we get a full packet from the device? */
    if (session->device_type) {
	newlen = session->device_type->get_packet(session);
	session->gpsdata.d_xmit_time = timestamp();
	if (session->packet.outbuflen>0 && !session->context->readonly && session->device_type->probe_subtype!=NULL)
	    session->device_type->probe_subtype(session, ++session->packet.counter);
    } else {
	const struct gps_type_t **dp;

	newlen = generic_get(session);
	session->gpsdata.d_xmit_time = timestamp();
	gpsd_report(LOG_RAW,
		    "packet sniff on %s finds type %d\n",
		    session->gpsdata.dev.path,
		    session->packet.type);
	if (session->packet.type > COMMENT_PACKET) {
	    session->observed |= PACKET_TYPEMASK(session->packet.type);
	    first_sync = (session->device_type == NULL);
	    for (dp = gpsd_drivers; *dp; dp++)
		if (session->packet.type == (*dp)->packet_type) {
		    (void)gpsd_switch_driver(session, (*dp)->type_name);
		    break;
		}
	} else if (!gpsd_next_hunt_setting(session))
	    return ERROR_SET;
    }

    /* update the scoreboard structure from the GPS */
    gpsd_report(LOG_RAW+2, "GPS sent %zd new characters\n", newlen);
    if (newlen == -1)	{		/* read error */
	gpsd_report(LOG_INF, "GPS on %s is offline (%lf sec since data)\n",
		    session->gpsdata.dev.path,
		    timestamp() - session->gpsdata.online);
	session->gpsdata.online = 0;
	return 0;
    } else if (newlen == 0) {		/* no new data */
	if (session->device_type != NULL && timestamp()>session->gpsdata.online+session->gpsdata.dev.cycle+1){
	gpsd_report(LOG_INF, "GPS on %s is offline (%lf sec since data)\n",
		    session->gpsdata.dev.path,
		    timestamp() - session->gpsdata.online);
	session->gpsdata.online = 0;
	    return 0;
	} else
	    return ONLINE_SET;
    } else if (session->packet.outbuflen == 0) {   /* got new data, but no packet */
	gpsd_report(LOG_RAW+3, "New data on %s, not yet a packet\n",
			    session->gpsdata.dev.path);
	return ONLINE_SET;
    } else {
	gps_mask_t received, dopmask = 0;
	session->gpsdata.online = timestamp();
	session->cycle_state = 0;

	gpsd_report(LOG_RAW+3, "Accepted packet on %s.\n",
			    session->gpsdata.dev.path);
	/*@ -nullstate @*/
	if (session->gpsdata.raw_hook)
	    session->gpsdata.raw_hook(&session->gpsdata,
				      (char *)session->packet.outbuffer,
				      (size_t)session->packet.outbuflen, 2);
	/*@ -nullstate @*/
	session->gpsdata.sentence_length = session->packet.outbuflen;
	session->gpsdata.d_recv_time = timestamp();

	/* 
	 * If this is the first time we've achieved sync on this device, that's
	 * a significant event that the caller needs to know about.  Using
	 * DEVICE_SET this way is a bit shaky but we're short of bits in
	 * the flag mask (client library uses it differently).
	 */
	if (first_sync)
	    received |= DEVICE_SET;

	/* Get data from current packet into the fix structure */
	if (session->packet.type != COMMENT_PACKET)
	    if (session->device_type != NULL && session->device_type->parse_packet!=NULL)
		received = session->device_type->parse_packet(session);

	/*
	 * Compute fix-quality data from the satellite positions.
	 * These will not overwrite DOPs reported from the packet we just got.
	 */
	if (session->gpsdata.fix.mode > MODE_NO_FIX
		    && (session->gpsdata.set & SATELLITE_SET) != 0
		    && session->gpsdata.satellites > 0) {
	    dopmask = dop(&session->gpsdata);
	    session->gpsdata.epe = NAN;
	}
	session->gpsdata.set = ONLINE_SET | dopmask | received;

	/*
	 * Count good fixes. We used to check 
	 *	session->gpsdata.status > STATUS_NO_FIX
	 * here, but that wasn't quite right.  That tells us whether
	 * we think we have a valid fix for the current cycle, but remains
	 * true while following non-fix packets are received.  What we
	 * really want to know is whether the last packet received was a
	 * fix packet AND held a valid fix. We must ignore non-fix packets
	 * AND packets which have fix data but are flagged as invalid. Some
	 * devices output fix packets on a regular basis, even when unable
	 * to derive a good fix. Such packets should set STATUS_NO_FIX.
	 */
	if ((session->gpsdata.set & LATLON_SET )!=0 && session->gpsdata.status > STATUS_NO_FIX)
	    session->context->fixcnt++;

	session->gpsdata.d_decode_time = timestamp();

	/* also copy the sentence up to clients in raw mode */
	if (session->packet.type == NMEA_PACKET) {
	    if (session->gpsdata.raw_hook)
		session->gpsdata.raw_hook(&session->gpsdata,
					  (char *)session->packet.outbuffer,
					  strlen((char *)session->packet.outbuffer),
					  1);
	} else {
	    char buf2[MAX_PACKET_LENGTH*3+2];

	    buf2[0] = '\0';

	    /* Some kinds of data is automatically passed through */
#ifdef BINARY_ENABLE
#if defined(RTCM104V2_ENABLE) || defined(RTCM104V3_ENABLE)
	    if ((session->gpsdata.set & (RTCM2_SET | RTCM3_SET)) == 0)
#endif /* defined(RTCM104V2_ENABLE) || defined(RTCM104V3_ENABLE) */
		gpsd_binary_dump(session, buf2, sizeof(buf2));
#endif /* BINARY_ENABLE */
	    if (buf2[0] != '\0') {
		gpsd_report(LOG_IO, "<= GPS (binary) %s: %s",
			    session->gpsdata.dev.path, buf2);
		if (session->gpsdata.raw_hook)
		    session->gpsdata.raw_hook(&session->gpsdata,
					      buf2, strlen(buf2), 1);
	    }
	}

	if (session->gpsdata.fix.mode == MODE_3D)
	    netgnss_report(session);

	return session->gpsdata.set;
    }
}

void gpsd_wrap(struct gps_device_t *session)
/* end-of-session wrapup */
{
    if (session->gpsdata.gps_fd != -1)
	gpsd_deactivate(session);
}

void gpsd_zero_satellites(/*@out@*/struct gps_data_t *out)
{
    (void)memset(out->PRN,	 0, sizeof(out->PRN));
    (void)memset(out->elevation, 0, sizeof(out->elevation));
    (void)memset(out->azimuth,	 0, sizeof(out->azimuth));
    (void)memset(out->ss,	 0, sizeof(out->ss));
    out->satellites = 0;
}

