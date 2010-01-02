/* $Id$ */
/* libgpsd_core.c -- direct access to GPSes on serial or USB devices. */
#include <stdlib.h>
#include "gpsd_config.h"
#include <sys/time.h>
#ifdef HAVE_SYS_IOCTL_H
 #include <sys/ioctl.h>
#endif /* HAVE_SYS_IOCTL_H */
#ifndef S_SPLINT_S
 #ifdef HAVE_SYS_SOCKET_H
  #include <sys/socket.h>
 #endif /* HAVE_SYS_SOCKET_H */
 #include <unistd.h>
#endif /* S_SPLINT_S */
#include <sys/time.h>
#include <stdio.h>
#include <math.h>
#ifndef S_SPLINT_S
 #ifdef HAVE_NETDB_H
  #include <netdb.h>
 #endif /* HAVE_NETDB_H */
#endif /* S_SPLINT_S */
#include <string.h>
#include <errno.h>
#include <fcntl.h>

#include "gpsd.h"

#if defined(PPS_ENABLE) && defined(TIOCMIWAIT)
#ifndef S_SPLINT_S
#include <pthread.h>	/* pacifies OpenBSD's compiler */
#endif
#endif


int gpsd_switch_driver(struct gps_device_t *session, char* type_name)
{
    const struct gps_type_t **dp;
    bool identified = (session->device_type != NULL);

    gpsd_report(LOG_PROG, "switch_driver(%s) called...\n", type_name);
    if (identified && strcmp(session->device_type->type_name, type_name) == 0)
	return 0;

    /*@ -compmempass @*/
    for (dp = gpsd_drivers; *dp; dp++)
	if (strcmp((*dp)->type_name, type_name) == 0) {
	    gpsd_report(LOG_PROG, "selecting %s driver...\n", (*dp)->type_name);
	    gpsd_assert_sync(session);
	    /*@i@*/session->device_type = *dp;
#ifdef ALLOW_RECONFIGURE
	    session->gpsdata.dev.mincycle = session->device_type->min_cycle;
#endif /* ALLOW_RECONFIGURE */
	    /* reconfiguration might be required */
	    if (identified && session->device_type->event_hook != NULL)
		session->device_type->event_hook(session, event_driver_switch);
	    /* clients should be notified */
	    session->notify_clients = true;
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
    session->is_serial = false;		/* gpsd_open() setss this */
    /*@ -temptrans @*/
    session->context = context;
    /*@ +temptrans @*/
    /*@ +mayaliasunique @*/
    /*@ +mustfreeonly @*/
    gps_clear_fix(&session->gpsdata.fix);
    session->gpsdata.set = 0;
    session->gpsdata.dop.hdop = NAN;
    session->gpsdata.dop.vdop = NAN;
    session->gpsdata.dop.pdop = NAN;
    session->gpsdata.dop.tdop = NAN;
    session->gpsdata.dop.gdop = NAN;
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
    if (!session->context->readonly
	&& session->device_type != NULL
	&& session->device_type->event_hook != NULL) {
	session->device_type->event_hook(session, event_deactivate);
    }
    if (session->device_type!=NULL) {
	if (session->back_to_nmea && session->device_type->mode_switcher!=NULL)
	    session->device_type->mode_switcher(session, 0);
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

#if defined(PPS_ON_CTS)
    int pps_device = TIOCM_CTS;
    #define pps_device_str "CTS"
#else
    int pps_device = TIOCM_CAR;
    #define pps_device_str "DCD"
#endif

    gpsd_report(LOG_PROG, "PPS Create Thread gpsd_ppsmonitor\n");

    /* wait for status change on the device's carrier-detect line */
    while (ioctl(session->gpsdata.gps_fd, TIOCMIWAIT, pps_device) == 0) {
	int ok = 0;
	char *log = NULL;

	(void)gettimeofday(&tv,NULL);

	ok = 0;
	log = NULL;

	/*@ +ignoresigns */
	if (ioctl(session->gpsdata.gps_fd, TIOCMGET, &state) != 0)
	    break;
	/*@ -ignoresigns */

	state = (int)((state & pps_device) != 0);
	/*@ +boolint @*/
#define timediff(x, y)	(int)((x.tv_sec-y.tv_sec)*1000000+x.tv_usec-y.tv_usec)
	cycle = timediff(tv, pulse[state]);
	duration = timediff(tv, pulse[(int)(state == 0)]);
#undef timediff
	/*@ -boolint @*/

	if (state == laststate) {
	    /* some pulses may be so short that state never changes */
	    if ( 999000 < cycle && 1001000 > cycle ) {
		duration = 0;
		unchanged = 0;
		gpsd_report(LOG_RAW,
			"PPS pps-detect (%s) on %s invisible pulse\n",
			pps_device_str, session->gpsdata.dev.path);
	    } else if (++unchanged == 10) {
		unchanged = 1;
		gpsd_report(LOG_WARN,
	"PPS TIOCMIWAIT returns unchanged state, ppsmonitor sleeps 10\n");
		(void)sleep(10);
	    }
	} else {
	    gpsd_report(LOG_RAW, "PPS pps-detect (%s) on %s changed to %d\n",
			  pps_device_str,
			  session->gpsdata.dev.path, state);
	    laststate = state;
	    unchanged = 0;
	}
	pulse[state] = tv;
	if ( unchanged ) {
	    // strange, try again
	    continue;
	}
	gpsd_report(LOG_INF, "PPS cycle: %d, duration: %d @ %lu.%06lu\n",
	    cycle, duration,
	    (unsigned long)tv.tv_sec, (unsigned long)tv.tv_usec);

	/*@ +boolint @*/
	if ( 3 < session->context->fixcnt ) {
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
	     * Some GPS, like the Globalsat MR-350P, output a 1uS pulse.
	     * The pulse is so short that TIOCMIWAIT sees a state change
	     * but by the time TIOCMGET is called the pulse is gone.
	     *
	     * A few stupid GPS, like the Furuno GPSClock, output a 1.0 Hz
	     * square wave where the leading edge is the start of a second
	     *
	     * 5Hz GPS (Garmin 18-5Hz) pulses at 5Hz. Set the pulse length to
	     * 40ms which gives a 160ms pulse before going high.
	     *
	     */

	    if (199000 > cycle) {
		// too short to even be a 5Hz pulse
		log = "Too short for 5Hz\n";
	    } else if (201000 > cycle) {
		/* 5Hz cycle */
		/* looks like 5hz PPS pulse */
		if (100000 > duration) {
		    /* BUG: how does the code know to tell ntpd
		     * which 1/5 of a second to use?? */
		    ok = 1;
		    log = "5Hz PPS pulse\n";
		}
	    } else if (999000 > cycle) {
		    log = "Too long for 5Hz, too short for 1Hz\n";
	    } else if (1001000 > cycle) {
		/* looks like PPS pulse or square wave */

#if 0
/* huh? */
#if defined(NMEA_ENABLE) && defined(GPSCLOCK_ENABLE)
		 && session->driver.nmea.ignore_trailing_edge
#endif /* GPSCLOCK_ENABLE */
#endif

		if (0 == duration) {
		    ok = 1;
		    log = "PPS invisible pulse\n";
		} else if (499000 > duration) {
		    /* end of the short "half" of the cycle */
		    /* aka the trailing edge */
		    log = "PPS 1Hz trailing edge\n";
		} else if (501000 > duration) {
		    /* looks like 1.0 Hz square wave, ignore trailing edge */
		    if (state == 1) {
			ok = 1;
			log = "PPS square\n";
		    }
		} else {
		    /* end of the long "half" of the cycle */
		    /* aka the leading edge */
		    ok = 1;
		    log = "PPS 1Hz leading edge\n";
		}
	    } else if (1999000 > cycle) {
		log = "Too long for 1Hz, too short for 2Hz\n";
	    } else if (2001000 > cycle) {
		/* looks like 0.5 Hz square wave */
                if (999000 > duration) {
                    log = "PPS 0.5 Hz square too short duration\n";
                } else if (1001000 > duration) {
                    ok = 1;
                    log = "PPS 0.5 Hz square wave\n";
                } else {
                    log = "PPS 0.5 Hz square too long duration\n";
                }
	    } else {
		log = "Too long for 0.5Hz\n";
	    }
	} else {
	    /* not a good fix, but a test for an otherwise good PPS
	     * would go here */
	    log = "PPS no fix.\n";
	}
	/*@ -boolint @*/
	if ( NULL != log ) {
	    gpsd_report(LOG_RAW, "%s", log);
	}
	if ( 0 != ok ) {
	    (void)ntpshm_pps(session, &tv);
	} else {
	    gpsd_report(LOG_INF, "PPS pulse rejected\n");
	}

    }

    return NULL;
}
#endif /* PPS_ENABLE */

/*@ -branchstate @*/
int gpsd_activate(struct gps_device_t *session)
/* acquire a connection to the GPS device */
{
    /* special case: source may be a URI to a remote GNSS or DGPS service */
    if (netgnss_uri_check(session->gpsdata.dev.path))
	session->gpsdata.gps_fd = netgnss_uri_open(session->context, 
						   session->gpsdata.dev.path);
    /* otherwise, could be an AIS data feed */
    else if (strncmp(session->gpsdata.dev.path, "ais://", 6) == 0) {
	char server[GPS_PATH_MAX], *port;
	socket_t dsock;
	(void)strlcpy(server, session->gpsdata.dev.path+6, sizeof(server));
	session->gpsdata.gps_fd = -1;
	port = strchr(server, ':');
	if (port == NULL) {
	    gpsd_report(LOG_ERROR, "Missing colon in AIS feed spec.\n");
	    return -1;
	}
	*port++ = '\0';
	gpsd_report(LOG_INF, "opening AIS feed at %s, port %s.\n", server,port);
	if ((dsock = netlib_connectsock(server, port, "tcp")) < 0) {
	    gpsd_report(LOG_ERROR, "AIS device open error %s.\n", 
			netlib_errstr(dsock));
	    return -1;
	}
	session->gpsdata.gps_fd = dsock;
    }
    /* otherwise, ordinary serial device */
    else 
	session->gpsdata.gps_fd = gpsd_open(session);

    if (session->gpsdata.gps_fd < 0)
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
#endif /* NON_NMEA_ENABLE */
	session->gpsdata.online = timestamp();
#ifdef SIRF_ENABLE
	session->driver.sirf.satcounter = 0;
#endif /* SIRF_ENABLE */
	session->packet.char_counter = 0;
	session->packet.retry_counter = 0;
	gpsd_report(LOG_INF,
		    "gpsd_activate(): opened GPS (fd %d)\n",
		    session->gpsdata.gps_fd);
	// session->gpsdata.online = 0;
	session->gpsdata.fix.mode = MODE_NOT_SEEN;
	session->gpsdata.status = STATUS_NO_FIX;
	session->gpsdata.fix.track = NAN;
	session->gpsdata.separation = NAN;
	session->mag_var = NAN;
	session->releasetime = 0;

	/* clear the private data union */
	memset(&session->driver, '\0', sizeof(session->driver));
	/*
	 * We might know the device's type, but we shoudn't assume it has
	 * retained its settings.  A revert hook might well have undone
	 * them on the previous close.  Fire a reactivate event so drivers
	 * can do something about this if they choose.
	 */
	if (session->device_type != NULL
	    && session->device_type->event_hook != NULL)
	    session->device_type->event_hook(session, event_reactivate);
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

    if ( 0 > session->shmindex ) {
	gpsd_report(LOG_INF, "NTPD ntpshm_alloc() failed\n");
#if defined(PPS_ENABLE) && defined(TIOCMIWAIT)
    } else if ( session->context->shmTimePPS) {
	/* We also have the 1pps capability, allocate a shared-memory segment
	 * for the 1pps time data and launch a thread to capture the 1pps
	 * transitions
	 */
	if ((session->shmTimeP = ntpshm_alloc(session->context)) >= 0) {
	    /*@i1@*/(void)pthread_create(&pt,NULL,gpsd_ppsmonitor,(void *)session);
	} else {
	    gpsd_report(LOG_INF, "NTPD ntpshm_alloc(1) failed\n");
	}

#endif /* defined(PPS_ENABLE) && defined(TIOCMIWAIT) */
    }
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
     * OK, this is not an error computation, but
     * we're at the right place in the architrcture for it.
     * Compute climb/sink in the simplest possible way.
     * FIXME: Someday we should compute speed here too.
     */
    if (fix->mode>=MODE_3D && oldfix->mode>=MODE_3D && isnan(fix->climb)!=0) {
	if (fix->time == oldfix->time)
	    fix->climb = 0;
	else if (isnan(fix->altitude)==0 && isnan(oldfix->altitude)==0) {
	    fix->climb = (fix->altitude-oldfix->altitude)/(fix->time-oldfix->time);
	}
    }

    /*
     * Field reports match the theoretical prediction that
     * expected time error should be half the resolution of
     * the GPS clock, so we put the bound of the error
     * in as a constant pending getting it from each driver.
     */
    if (isnan(fix->time)==0 && isnan(fix->ept)!=0)
	fix->ept = 0.005;
    /* Other error computations depend on having a valid fix */
    if (fix->mode >= MODE_2D) {
	if (isnan(fix->epx)!=0 && finite(session->gpsdata.dop.hdop)!=0)
		fix->epx = session->gpsdata.dop.xdop * h_uere;

	if (isnan(fix->epy)!=0 && finite(session->gpsdata.dop.hdop)!=0)
		fix->epy = session->gpsdata.dop.ydop * h_uere;

	if ((fix->mode >= MODE_3D)
		&& isnan(fix->epv)!=0 && finite(session->gpsdata.dop.vdop)!=0)
	    fix->epv = session->gpsdata.dop.vdop * v_uere;

	if (isnan(session->gpsdata.epe)!=0 && finite(session->gpsdata.dop.pdop)!=0)
	    session->gpsdata.epe = session->gpsdata.dop.pdop * p_uere;
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

#ifdef TIMING_ENABLE
    if (session->packet.outbuflen == 0)
	session->d_xmit_time = timestamp();
#endif /* TIMING_ENABLE */

    if (session->packet.type >= COMMENT_PACKET)
	/*@i2@*/session->observed |= PACKET_TYPEMASK(session->packet.type);

    /* can we get a full packet from the device? */
    if (session->device_type) {
	newlen = session->device_type->get_packet(session);
	gpsd_report(LOG_RAW,
		    "%s is known to be %s\n",
		    session->gpsdata.dev.path,
		    session->device_type->type_name);
    } else {
	const struct gps_type_t **dp;

	newlen = generic_get(session);
	gpsd_report(LOG_RAW,
		    "packet sniff on %s finds type %d\n",
		    session->gpsdata.dev.path,
		    session->packet.type);
	if (session->packet.type > COMMENT_PACKET) {
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
    gpsd_report(LOG_RAW+2, "%s sent %zd new characters\n",
		session->gpsdata.dev.path, newlen);
   if (newlen == -1)	{		/* read error */
	gpsd_report(LOG_INF, "GPS on %s is offline (%lf sec since data)\n",
		    session->gpsdata.dev.path,
		    timestamp() - session->gpsdata.online);
	session->gpsdata.online = 0;
	return 0;
    } else if (newlen == 0) {		/* no new data */
	if (session->device_type != NULL && timestamp()>session->gpsdata.online+session->gpsdata.dev.cycle+1) {
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
    } else {				/* we have recognized a packet */
	gps_mask_t received = PACKET_SET, dopmask = 0;
	session->gpsdata.online = timestamp();

	gpsd_report(LOG_RAW+3, "Accepted packet on %s.\n",
			    session->gpsdata.dev.path);

#ifdef TIMING_ENABLE
	session->d_recv_time = timestamp();
#endif /* TIMING_ENABLE */

	/* track the packet count since achieving sync on the device */
	if (first_sync) {
	    /* fire the identified hook */
	    if (session->device_type != NULL && session->device_type->event_hook != NULL)
		session->device_type->event_hook(session, event_identified);
	    session->packet.counter = 0;
	} else
	    session->packet.counter++;

	/* fire the configure hook */
	if (session->device_type != NULL && session->device_type->event_hook != NULL)
	    session->device_type->event_hook(session, event_configure);

	/*
	 * If this is the first time we've achieved sync on this
	 * device, or the the driver type has changed for any other
	 * reason, that's a significant event that the caller needs to
	 * know about.  Using DEVICE_SET this way is a bit shaky but
	 * we're short of bits in the flag mask (client library uses
	 * it differently).
	 */
	if (first_sync || session->notify_clients) {
	    session->notify_clients = false;
	    received |= DEVICE_SET;
	}

	/* Get data from current packet into the fix structure */
	if (session->packet.type != COMMENT_PACKET)
	    if (session->device_type != NULL && session->device_type->parse_packet!=NULL)
		received |= session->device_type->parse_packet(session);

	/*
	 * Compute fix-quality data from the satellite positions.
	 * These will not overwrite DOPs reported from the packet we just got.
	 */
	if (session->gpsdata.fix.mode > MODE_NO_FIX
		    && (session->gpsdata.set & SATELLITE_SET) != 0
		    && session->gpsdata.satellites_visible > 0) {
	    dopmask = fill_dop(&session->gpsdata, &session->gpsdata.dop);
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

#ifdef TIMING_ENABLE
	session->d_decode_time = timestamp();
#endif /* TIMING_ENABLE */

	/*
	 * Sanity check.  This catches a surprising number of port and
	 * driver errors, including 32-vs.-64-bit problems.
	 */
	/*@+relaxtypes +longunsignedintegral@*/
	if ((session->gpsdata.set & TIME_SET)!=0) {
	    if (session->gpsdata.fix.time > time(NULL) + (60 * 60 * 24 * 365))
		gpsd_report(LOG_ERROR,"date more than a year in the future!\n");
	    else if (session->gpsdata.fix.time < 0)
		gpsd_report(LOG_ERROR,"date is negative!\n");
	}
	/*@-relaxtypes -longunsignedintegral@*/

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
    out->satellites_visible = 0;
}

