/*
 * ppsthread.c - manage PPS watcher threads
 *
 * If you are not good at threads do not touch this file!
 *
 * It helps to know that there are two PPS measurement methods in
 * play.  One is defined by RFC2783 and typically implemented in the
 * kernel.  It is available on FreeBSD, Linux, and NetBSD.  In gpsd it
 * is referred to as KPPS.  KPPS is accessed on Linux via /dev/ppsN
 * devices.  On BSD it is accessed via the same device as the serial
 * port.  This mechanism is preferred as it should provide the smallest
 * latency and jitter from control line transition to timestamp.
 *
 * The other mechanism is user-space PPS, which uses the (not
 * standardized) TIOCMIWAIT ioctl to wait for PPS transitions on
 * serial port control lines.  It is implemented on Linux and OpenBSD.
 *
 * On Linux, RFC2783 PPS requires root permissions for initialization;
 * user-space PPS does not.  User-space PPS loses some functionality
 * when not initialized as root.  In gpsd, user-space PPS is referred
 * to as "plain PPS".
 *
 * On {Free,Net}BSD, RFC2783 PPS should only require access to the
 * serial port, but details have not yet been tested and documented
 * here.
 *
 * Note that for easy debugging all logging from this file is prefixed
 * with PPS or KPPS.
 *
 * To use the thread manager, you need to first fill in the two
 * thread_* methods in the session structure and/or the pps_hook in
 * the context structure.  Then you can call pps_thread_activate() and
 * the thread will launch.  It is OK to do this before the device is
 * open, the thread will wait on that.
 *
 * This file is Copyright (c) 2013 by the GPSD project. BSD terms
 * apply: see the file COPYING in the distribution root for details.
 */

#include <string.h>
#include <errno.h>
#include <pthread.h>
#ifndef S_SPLINT_S
#include <sys/socket.h>
#include <unistd.h>
#endif /* S_SPLINT_S */

#include "gpsd.h"

#ifdef PPS_ENABLE
#if defined(HAVE_SYS_TIMEPPS_H)
#include <fcntl.h>	/* needed for open() and friends */
#endif /* defined(HAVE_SYS_TIMEPPS_H) */

#define PPS_MAX_OFFSET	100000	/* microseconds the PPS can 'pull' */
#define PUT_MAX_OFFSET	1000000	/* microseconds for lost lock */

/*
 * Warning: This is a potential portability problem.
 * It's needed so that TIOCMIWAIT will be defined and the plain PPS
 * code will work, but it's not a SuS/POSIX standard header.  We're
 * going to include it unconditionally here because we expect both
 * Linux and BSD to have it and we want compilation to break with
 * an audible snapping sound if it's not present.
 */
#include <sys/ioctl.h>

#ifndef S_SPLINT_S
#include <pthread.h>		/* pacifies OpenBSD's compiler */
#endif
#if defined(HAVE_SYS_TIMEPPS_H)
#include <glob.h>
#endif

static pthread_mutex_t ppslast_mutex;

#if defined(HAVE_SYS_TIMEPPS_H)
/*@-compdestroy -nullpass -unrecog@*/
static int init_kernel_pps(struct gps_device_t *session)
/* return handle for kernel pps, or -1; requires root privileges */
{
#ifndef S_SPLINT_S
    pps_params_t pp;
#endif /* S_SPLINT_S */
    int ret;
#ifdef linux
    /* These variables are only needed by Linux to find /dev/ppsN. */
    int ldisc = 18;   /* the PPS line discipline */
    glob_t globbuf;
    size_t i;             /* to match type of globbuf.gl_pathc */
    char pps_num = '\0';  /* /dev/pps[pps_num] is our device */
    char path[GPS_PATH_MAX] = "";
#endif

    session->kernelpps_handle = -1;
    if ( isatty(session->gpsdata.gps_fd) == 0 ) {
	gpsd_report(session->context->debug, LOG_INF, "KPPS gps_fd not a tty\n");
    	return -1;
    }

    /*
     * This next code block abuses "ret" by storing the filedescriptor
     * to use for RFC2783 calls.
     */
    ret = -1;
#ifdef linux
    /*
     * On Linux, one must make calls to associate a serial port with a
     * /dev/ppsN device and then grovel in system data to determine
     * the association.
     */
    /*@+ignoresigns@*/
    /* Attach the line PPS discipline, so no need to ldattach */
    /* This activates the magic /dev/pps0 device */
    /* Note: this ioctl() requires root */
    if ( 0 > ioctl(session->gpsdata.gps_fd, TIOCSETD, &ldisc)) {
	gpsd_report(session->context->debug, LOG_INF,
		    "KPPS cannot set PPS line discipline: %s\n",
		    strerror(errno));
    	return -1;
    }
    /*@-ignoresigns@*/

    /* uh, oh, magic file names!, RFC2783 neglects to specify how
     * to associate the serial device and pps device names */
    /* need to look in /sys/devices/virtual/pps/pps?/path
     * (/sys/class/pps/pps?/path is just a link to that)
     * to find the /dev/pps? that matches our serial port.
     * this code fails if there are more then 10 pps devices.
     *
     * yes, this could be done with libsysfs, but trying to keep the
     * number of required libs small, and libsysfs would still be linux only */
    memset( (void *)&globbuf, 0, sizeof(globbuf));
    (void)glob("/sys/devices/virtual/pps/pps?/path", 0, NULL, &globbuf);

    memset( (void *)&path, 0, sizeof(path));
    for ( i = 0; i < globbuf.gl_pathc; i++ ) {
        int fd = open(globbuf.gl_pathv[i], O_RDONLY);
	if ( 0 <= fd ) {
	    ssize_t r = read( fd, path, sizeof(path) -1);
	    if ( 0 < r ) {
		path[r - 1] = '\0'; /* remove trailing \x0a */
	    }
	    (void)close(fd);
	}
	gpsd_report(session->context->debug, LOG_INF,
		    "KPPS checking %s, %s\n",
		    globbuf.gl_pathv[i], path);
	if ( 0 == strncmp( path, session->gpsdata.dev.path, sizeof(path))) {
	    /* this is the pps we are looking for */
	    /* FIXME, now build the proper pps device path */
	    pps_num = globbuf.gl_pathv[i][28];
	    break;
	}
	memset( (void *)&path, 0, sizeof(path));
    }
    /* done with blob, clear it */
    globfree(&globbuf);

    if ( 0 == (int)pps_num ) {
	gpsd_report(session->context->debug, LOG_INF,
		    "KPPS device not found.\n");
    	return -1;
    }
    /* contruct the magic device path */
    (void)snprintf(path, sizeof(path), "/dev/pps%c", pps_num);

    /* root privs are required for this device open */
    if ( 0 != getuid() ) {
	gpsd_report(session->context->debug, LOG_INF,
		    "KPPS only works as root \n");
    	return -1;
    }
    ret = open(path, O_RDWR);
    if ( 0 > ret ) {
	gpsd_report(session->context->debug, LOG_INF,
		    "KPPS cannot open %s: %s\n", path, strerror(errno));
    	return -1;
    }
#else /* not linux */
    /*
     * On BSDs that support RFC2783, one uses the API calls on serial
     * port file descriptor.
     */
    // cppcheck-suppress redundantAssignment
    ret  = session->gpsdata.gps_fd;
#endif
    /* assert(ret >= 0); */
    gpsd_report(session->context->debug, LOG_INF,
		"KPPS RFC2783 fd is %d\n",
		ret);

    /* RFC 2783 implies the time_pps_setcap() needs priviledges *
     * keep root a tad longer just in case */
    if ( 0 > time_pps_create(ret, &session->kernelpps_handle )) {
	gpsd_report(session->context->debug, LOG_INF,
		    "KPPS time_pps_create(%d) failed: %s\n",
		    ret, strerror(errno));
    	return -1;
    } else {
#ifndef S_SPLINT_S
    	/* have kernel PPS handle */
        int caps;
	/* get features  supported */
        if ( 0 > time_pps_getcap(session->kernelpps_handle, &caps)) {
	    gpsd_report(session->context->debug, LOG_ERROR,
			"KPPS time_pps_getcap() failed\n");
        } else {
	    gpsd_report(session->context->debug,
			LOG_INF, "KPPS caps %0x\n", caps);
        }

#ifdef linux
        /* linux 2.6.34 can not PPS_ECHOASSERT | PPS_ECHOCLEAR */
        memset( (void *)&pp, 0, sizeof(pps_params_t));
        pp.mode = PPS_CAPTUREBOTH;
#else /* not linux */
	/*
	 * Attempt to follow RFC2783 as straightforwardly as possible.
	 */
	pp.mode = PPS_TSFMT_TSPEC | PPS_CAPTUREBOTH;
#endif
#endif /* S_SPLINT_S */

        if ( 0 > time_pps_setparams(session->kernelpps_handle, &pp)) {
	    gpsd_report(session->context->debug, LOG_ERROR,
		"KPPS time_pps_setparams() failed: %s\n", strerror(errno));
	    time_pps_destroy(session->kernelpps_handle);
	    return -1;
        }
    }
    return 0;
}
/*@+compdestroy +nullpass +unrecog@*/
#endif /* defined(HAVE_SYS_TIMEPPS_H) */

/*@-mustfreefresh -type -unrecog -branchstate@*/
static /*@null@*/ void *gpsd_ppsmonitor(void *arg)
{
    struct gps_device_t *session = (struct gps_device_t *)arg;
    double last_fixtime_real = 0, last_fixtime_clock = 0;
#ifndef HAVE_CLOCK_GETTIME
    struct timeval  clock_tv = {0, 0};
#endif /* HAVE_CLOCK_GETTIME */
    struct timespec clock_ts = {0, 0};
    time_t last_second_used = 0;
#if defined(TIOCMIWAIT)
    int cycle, duration; 
    /* state is the last state of the tty control ssignals */
    int state = 0, unchanged = 0;
    /* state_last is previous state */
    int state_last = 0;
    /* pulse stores the time of the last two edges */
    struct timespec pulse[2] = { {0, 0}, {0, 0} };
    /* edge, used as index into pulse to find previous edges */
    int edge = 0;       /* 0 = clear edge, 1 = assert edge */
#endif /* TIOCMIWAIT */
#if defined(HAVE_SYS_TIMEPPS_H)
    int edge_kpps = 0;       /* 0 = clear edge, 1 = assert edge */
#ifndef S_SPLINT_S
    int cycle_kpps, duration_kpps;
    /* kpps_pulse stores the time of the last two edges */
    struct timespec pulse_kpps[2] = { {0, 0}, {0, 0} };
    struct timespec ts_kpps;
    pps_info_t pi;

    memset( (void *)&pi, 0, sizeof(pps_info_t));
#endif /* S_SPLINT_S */
#endif /* defined(HAVE_SYS_TIMEPPS_H) */

    /*
     * Wait for status change on any handshake line.  Just one edge,
     * we do not want to be spinning waiting for the trailing edge of
     * a pulse. The only assumption here is that no GPS lights up more
     * than one of these pins.  By waiting on all of them we remove a
     * configuration switch. 
     *
     * Once we have the latest edge we compare it to the last edge which we
     * stored.  If the edge passes sanity checks we use it to send to
     * ntpshm and chrony_send
     */

    while (session->thread_report_hook != NULL 
           || session->context->pps_hook != NULL) {
	bool ok = false;
#if defined(HAVE_SYS_TIMEPPS_H)
	// cppcheck-suppress variableScope
	bool ok_kpps = false;
#endif /* HAVE_SYS_TIMEPPS_H */
	char *log = NULL;

#if defined(TIOCMIWAIT)
        /* we are lucky to have TIOCMIWAIT, so wait for next edge */
#define PPS_LINE_TIOC (TIOCM_CD|TIOCM_CAR|TIOCM_RI|TIOCM_CTS)
        if (ioctl(session->gpsdata.gps_fd, TIOCMIWAIT, PPS_LINE_TIOC) != 0) {
	    gpsd_report(session->context->debug, LOG_ERROR,
			"PPS ioctl(TIOCMIWAIT) failed: %d %.40s\n",
			errno, strerror(errno));
	    break;
	}
        /* quick, grab a copy of last_fixtime before it changes */
	last_fixtime_real = session->last_fixtime.real;
	last_fixtime_clock = session->last_fixtime.clock;

/*@-noeffect@*/
        /* get the time after we just woke up */
#ifdef HAVE_CLOCK_GETTIME
	/* using  clock_gettime() here, that is nSec,
	 * not uSec like gettimeofday */
	if ( 0 > clock_gettime(CLOCK_REALTIME, &clock_ts) ) {
	    /* uh, oh, can not get time! */
	    gpsd_report(session->context->debug, LOG_ERROR,
			"PPS clock_gettime() failed\n");
	    break;
	}
#else
	if ( 0 > gettimeofday(&clock_tv, NULL) ) {
	    /* uh, oh, can not get time! */
	    gpsd_report(session->context->debug, LOG_ERROR,
			"PPS gettimeofday() failed\n");
	    break;
	}
	TVTOTS( &clock_ts, &clock_tv);
#endif /* HAVE_CLOCK_GETTIME */
/*@+noeffect@*/

        /* got the edge, got the time just after the edge, now quickly
         * get the edge state */
	/*@ +ignoresigns */
	if (ioctl(session->gpsdata.gps_fd, TIOCMGET, &state) != 0) {
	    gpsd_report(session->context->debug, LOG_ERROR,
			"PPS ioctl(TIOCMGET) failed\n");
	    break;
	}
	/*@ -ignoresigns */

	/* mask for monitored lines */
	state &= PPS_LINE_TIOC;
	edge = (state > state_last) ? 1 : 0;
#endif /* TIOCMIWAIT */

	/* ok and log used by KPPS and TIOCMIWAIT */
	// cppcheck-suppress redundantAssignment
	ok = false;  
	log = NULL;  
#if defined(HAVE_SYS_TIMEPPS_H) && !defined(S_SPLINT_S)
        if ( 0 <= session->kernelpps_handle ) {
	    struct timespec kernelpps_tv;
	    /* on a quad core 2.4GHz Xeon using KPPS timestamp instead of plain 
             * PPS timestamp removes about 20uS of latency, and about +/-5uS 
             * of jitter 
             */
#ifdef TIOCMIWAIT
	    /*
	     * We use of a non-NULL zero timespec here,
	     * which means to return immediately with -1 (section
	     * 3.4.3).  This is because we know we just got a pulse because 
             * TIOCMIWAIT just woke up.
	     * The timestamp has already been captured in the kernel, and we 
             * are merely fetching it here.
	     */
            memset( (void *)&kernelpps_tv, 0, sizeof(kernelpps_tv));
#else /* not TIOCMIWAIT */
	    /*
	     * RFC2783 specifies that a NULL timeval means to wait.
             *
             * FIXME, this will fail on 2Hz 'PPS', maybe should wait 3 Sec.
	     */
	    kernelpps_tv.tv_sec = 1;
	    kernelpps_tv.tv_nsec = 0;
#endif
	    if ( 0 > time_pps_fetch(session->kernelpps_handle, PPS_TSFMT_TSPEC
	        , &pi, &kernelpps_tv)) {
		gpsd_report(session->context->debug, LOG_ERROR,
			    "KPPS kernel PPS failed\n");
	    } else {
		// find the last edge
		// FIXME a bit simplistic, should hook into the
                // cycle/duration check below.
	    	if ( pi.assert_timestamp.tv_sec > pi.clear_timestamp.tv_sec ) {
		    edge_kpps = 1;
		    ts_kpps = pi.assert_timestamp;
	    	} else if ( pi.assert_timestamp.tv_sec < pi.clear_timestamp.tv_sec ) {
		    edge_kpps = 0;
		    ts_kpps = pi.clear_timestamp;
		} else if ( pi.assert_timestamp.tv_nsec > pi.clear_timestamp.tv_nsec ) {
		    edge_kpps = 1;
		    ts_kpps = pi.assert_timestamp;
		} else {
		    edge_kpps = 0;
		    ts_kpps = pi.clear_timestamp;
		}
		/*
		 * pps_seq_t is uint32_t on NetBSD, so cast to
		 * unsigned long as a wider-or-equal type to
		 * accomodate Linux's type.
		 */
		gpsd_report(session->context->debug, LOG_PROG,
			    "KPPS assert %ld.%09ld, sequence: %ld - "
			    "clear  %ld.%09ld, sequence: %ld\n",
			    pi.assert_timestamp.tv_sec,
			    pi.assert_timestamp.tv_nsec,
			    (unsigned long) pi.assert_sequence,
			    pi.clear_timestamp.tv_sec,
			    pi.clear_timestamp.tv_nsec,
			    (unsigned long) pi.clear_sequence);
		gpsd_report(session->context->debug, LOG_PROG,
			    "KPPS data: using %s\n",
			    edge_kpps ? "assert" : "clear");

	        cycle_kpps = timespec_diff_ns(ts_kpps, pulse_kpps[edge_kpps])/1000;
	        duration_kpps = timespec_diff_ns(ts_kpps, pulse_kpps[(int)(edge_kpps == 0)])/1000;
	        gpsd_report(session->context->debug, LOG_PROG,
		    "KPPS cycle: %7d uSec, duration: %7d uSec @ %lu.%09lu\n",
		    cycle_kpps, duration_kpps,
		    (unsigned long)ts_kpps.tv_sec,
		    (unsigned long)ts_kpps.tv_nsec);
		pulse_kpps[edge_kpps] = ts_kpps;
		if (990000 < cycle_kpps && 1010000 > cycle_kpps) {
		    /* KPPS passes a basic sanity check */
		    ok_kpps = true;
		    log = "KPPS";
		}
	    }
	}
#endif /* defined(HAVE_SYS_TIMEPPS_H) && !defined(S_SPLINT_S) */

#if defined(TIOCMIWAIT)
	/*@ +boolint @*/
	cycle = timespec_diff_ns(clock_ts, pulse[edge]) / 1000;
	duration = timespec_diff_ns(clock_ts, pulse[(int)(edge == 0)])/1000;
	/*@ -boolint @*/
	if (state == state_last) {
	    /* some pulses may be so short that state never changes */
	    if (999000 < cycle && 1001000 > cycle) {
		duration = 0;
		unchanged = 0;
		gpsd_report(session->context->debug, LOG_RAW,
			    "PPS pps-detect on %s invisible pulse\n",
			    session->gpsdata.dev.path);
	    } else if (++unchanged == 10) {
                /* not really unchanged, just out of bounds */
		unchanged = 1;
		gpsd_report(session->context->debug, LOG_WARN,
			    "PPS TIOCMIWAIT returns unchanged state, ppsmonitor sleeps 10\n");
		(void)sleep(10);
	    }
	} else {
	    gpsd_report(session->context->debug, LOG_RAW,
			"PPS pps-detect on %s changed to %d\n",
			session->gpsdata.dev.path, state);
	    unchanged = 0;
	}
	state_last = state;
        /* save this edge so we know next cycle time */
	pulse[edge] = clock_ts;
	gpsd_report(session->context->debug, LOG_PROG,
		    "PPS edge: %d, cycle: %7d uSec, duration: %7d uSec @ %lu.%09lu\n",
		    edge, cycle, duration,
		    (unsigned long)clock_ts.tv_sec,
		    (unsigned long)clock_ts.tv_nsec);
	if (unchanged) {
	    // strange, try again
	    continue;
	}

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
	 * Some GPSes instead output a square wave that is 0.5 Hz and each
	 * edge denotes the start of a second.
	 *
	 * Some GPSes, like the Globalsat MR-350P, output a 1uS pulse.
	 * The pulse is so short that TIOCMIWAIT sees a state change
	 * but by the time TIOCMGET is called the pulse is gone.
	 *
	 * A few stupid GPSes, like the Furuno GPSClock, output a 1.0 Hz
	 * square wave where the leading edge is the start of a second
	 *
	 * 5Hz GPS (Garmin 18-5Hz) pulses at 5Hz. Set the pulse length to
	 * 40ms which gives a 160ms pulse before going high.
	 *
	 */

	log = "Unknown error";
        if ( 0 > cycle ) {
	    log = "Rejecting negative cycle\n";
	} else if (199000 > cycle) {
	    // too short to even be a 5Hz pulse
	    log = "Too short for 5Hz\n";
	} else if (201000 > cycle) {
	    /* 5Hz cycle */
	    /* looks like 5hz PPS pulse */
	    if (100000 > duration) {
		/* BUG: how does the code know to tell ntpd
		 * which 1/5 of a second to use?? */
		ok = true;
		log = "5Hz PPS pulse\n";
	    }
	} else if (999000 > cycle) {
	    log = "Too long for 5Hz, too short for 1Hz\n";
	} else if (1001000 > cycle) {
	    /* looks like PPS pulse or square wave */
	    if (0 == duration) {
		ok = true;
		log = "invisible pulse\n";
	    } else if (499000 > duration) {
		/* end of the short "half" of the cycle */
		/* aka the trailing edge */
		log = "1Hz trailing edge\n";
	    } else if (501000 > duration) {
		/* looks like 1.0 Hz square wave, ignore trailing edge */
		if (edge == 1) {
		    ok = true;
		    log = "square\n";
		}
	    } else {
		/* end of the long "half" of the cycle */
		/* aka the leading edge */
		ok = true;
		log = "1Hz leading edge\n";
	    }
	} else if (1999000 > cycle) {
	    log = "Too long for 1Hz, too short for 2Hz\n";
	} else if (2001000 > cycle) {
	    /* looks like 0.5 Hz square wave */
	    if (999000 > duration) {
		log = "0.5 Hz square too short duration\n";
	    } else if (1001000 > duration) {
		ok = true;
		log = "0.5 Hz square wave\n";
	    } else {
		log = "0.5 Hz square too long duration\n";
	    }
	} else {
	    log = "Too long for 0.5Hz\n";
	}
#endif /* TIOCMIWAIT */
	if ( ok && last_second_used >= last_fixtime_real ) {
		/* uh, oh, this second already handled */
		ok = 0;
		log = "this second already handled\n";
	}

	if (ok) {
	    /* offset is the skew from expected to observed pulse time */
	    double offset;
	    /* delay after last fix */
	    double delay;
	    char *log1 = NULL;
	    /* drift.real is the time we think the pulse represents  */
	    struct timedrift_t drift;
	    gpsd_report(session->context->debug, LOG_RAW,
			"PPS edge accepted %.100s", log);
#if defined(HAVE_SYS_TIMEPPS_H)
            if ( 0 <= session->kernelpps_handle && ok_kpps) {
		/* use KPPS time */
		/* pick the right edge */
		if ( edge_kpps ) {
		    clock_ts = pi.assert_timestamp; /* structure copy */
		} else {
		    clock_ts = pi.clear_timestamp;  /* structure copy */
		}
	    } 
#endif /* defined(HAVE_SYS_TIMEPPS_H) */
	    /* else, use plain PPS */

            /* This innocuous-looking "+ 1" embodies a significant
             * assumption: that GPSes report time to the second over the
             * serial stream *after* emitting PPS for the top of second.
             * Thus, when we see PPS our available report is from the
             * previous cycle and we must increment. 
             *
             * FIXME! The GR-601W at 38,400 or faster can send the
             * serial fix before PPS by about 10 mSec!
             */

	    /*@+relaxtypes@*/
	    drift.real.tv_sec = last_fixtime_real + 1;
	    drift.real.tv_nsec = 0;  /* need to be fixed for 5Hz */
	    drift.clock = clock_ts;
	    /*@-relaxtypes@*/

	    /* check to see if we have a fresh timestamp from the
	     * GPS serial input then use that */
	    offset = (drift.real.tv_sec - drift.clock.tv_sec);
	    offset += ((drift.real.tv_nsec - drift.clock.tv_nsec) / 1e9);
	    delay = (drift.clock.tv_sec + drift.clock.tv_nsec / 1e9) - last_fixtime_clock;
	    if (0.0 > delay || 1.0 < delay) {
		gpsd_report(session->context->debug, LOG_RAW,
			    "PPS: no current GPS seconds: %f\n",
			    delay);
		log1 = "timestamp out of range";
	    } else {
		/*@-compdef@*/
		last_second_used = last_fixtime_real;
		if (session->thread_report_hook != NULL) 
		    log1 = session->thread_report_hook(session, &drift);
		else
		    log1 = "no report hook";
		if (session->context->pps_hook != NULL)
		    session->context->pps_hook(session, &drift);
		/*@ -unrecog  (splint has no pthread declarations as yet) @*/
		(void)pthread_mutex_lock(&ppslast_mutex);
		/*@ +unrecog @*/
		/*@-type@*/ /* splint is confused about struct timespec */
		session->ppslast = drift;
		/*@+type@*/
		session->ppscount++;
		/*@ -unrecog (splint has no pthread declarations as yet) @*/
		(void)pthread_mutex_unlock(&ppslast_mutex);
		/*@ +unrecog @*/
		/*@+compdef@*/
		/*@-type@*/ /* splint is confused about struct timespec */
		gpsd_report(session->context->debug, LOG_INF,
			    "PPS hooks called with %.20s %lu.%09lu offset %.9f\n",
			    log1,
			    (unsigned long)clock_ts.tv_sec,
			    (unsigned long)clock_ts.tv_nsec,
			    offset);
		/*@+type@*/
            }
	    /*@-type@*/ /* splint is confused about struct timespec */
	    gpsd_report(session->context->debug, LOG_PROG,
		    "PPS edge %.20s %lu.%09lu offset %.9f\n",
		    log1,
		    (unsigned long)clock_ts.tv_sec,
		    (unsigned long)clock_ts.tv_nsec,
		    offset);
	    /*@+type@*/
	} else {
	    gpsd_report(session->context->debug, LOG_RAW,
			"PPS edge rejected %.100s", log);
	}
    }
#if defined(HAVE_SYS_TIMEPPS_H)
    if (session->kernelpps_handle > 0) {
	gpsd_report(session->context->debug, LOG_PROG, 
            "PPS descriptor cleaned up\n");
	(void)time_pps_destroy(session->kernelpps_handle);
    }
#endif
    if (session->thread_wrap_hook != NULL)
	session->thread_wrap_hook(session);
    gpsd_report(session->context->debug, LOG_PROG, 
         "PPS gpsd_ppsmonitor exited.\n");
    return NULL;
}
/*@+mustfreefresh +type +unrecog +branchstate@*/

/*
 * Entry points begin here.
 */

void pps_thread_activate(struct gps_device_t *session)
/* activate a thread to watch the device's PPS transitions */
{
    int retval;
    pthread_t pt;
#if defined(HAVE_SYS_TIMEPPS_H)
    /* some operations in init_kernel_pps() require root privs */
    (void)init_kernel_pps( session );
    if ( 0 <= session->kernelpps_handle ) {
	gpsd_report(session->context->debug, LOG_WARN,
		    "KPPS kernel PPS will be used\n");
    }
#endif
    /*@-compdef -nullpass@*/
    retval = pthread_create(&pt, NULL, gpsd_ppsmonitor, (void *)session);
    /*@+compdef +nullpass@*/
    gpsd_report(session->context->debug, LOG_PROG, "PPS thread %s\n",
		(retval==0) ? "launched" : "FAILED");
}

void pps_thread_deactivate(struct gps_device_t *session)
/* cleanly terminate PPS thread */
{
    /*@-nullstate -mustfreeonly@*/
    session->thread_report_hook = NULL;
    session->context->pps_hook = NULL;
    /*@+nullstate +mustfreeonly@*/
}

int pps_thread_lastpps(struct gps_device_t *session, struct timedrift_t *td)
/* return a copy of the drift at the time of the last PPS */
{
    volatile int ret;

    /*@ -unrecog  (splint has no pthread declarations as yet) @*/
    (void)pthread_mutex_lock(&ppslast_mutex);
    /*@ +unrecog @*/
    *td = session->ppslast;
    ret = session->ppscount;
    /*@ -unrecog (splint has no pthread declarations as yet) @*/
    (void)pthread_mutex_unlock(&ppslast_mutex);
    /*@ +unrecog @*/

    return ret;
}

#endif /* PPS_ENABLE */

/* end */

