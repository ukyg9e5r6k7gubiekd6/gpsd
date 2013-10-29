/*
 * ppsthread.c - manage PPS watcher threads
 *
 * If you are not good at threads do not touch this file!
 *
 * It helps to know that there are two PPS measurement methods in
 * play. One, kernel PPS (KPPS), is RFCRFC2783 and available on many
 * OS and supplied through special /dev/pps devices. The other, plain
 * PPS, uses the TIOCMIWAIT ioctl to explicitly watch for PPS on
 * serial lines. KPPS requires root permissions for intialization;
 * plain PPS does not.  Plain PPS loses some functionality when not
 * initialized as root.
 *
 * To use the thread manager, you need to first fill in the four
 * thread_* methods in the session structure.  Then you can call
 * pps_thread_activate() and the thread will launch.  It is OK to do
 * this before the device is open, the thread will wait on that.
 *
 * This file is Copyright (c) 2013 by the GPSD project BSD terms apply:
 * see the file COPYING in the distribution root for details.
 */

#include <string.h>
#include <libgen.h>
#include <stdbool.h>
#include <math.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>
#ifndef S_SPLINT_S
#include <sys/wait.h>
#include <sys/socket.h>
#include <unistd.h>
#endif /* S_SPLINT_S */

#include "gpsd.h"
#if defined(HAVE_SYS_TIMEPPS_H)
#include <fcntl.h>	/* needed for open() and friends */
#endif

#if defined(HAVE_SYS_TIMEPPS_H)
static pthread_mutex_t initialization_mutex;
static volatile int uninitialized_pps_thread_count;
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

#if defined(HAVE_SYS_TIMEPPS_H)
static int init_kernel_pps(struct gps_device_t *session)
/* return handle for kernel pps, or -1 */
{
    int ldisc = 18;   /* the PPS line discipline */
    pps_params_t pp;
    glob_t globbuf;
    size_t i;             /* to match type of globbuf.gl_pathc */
    char pps_num = 0;     /* /dev/pps[pps_num] is our device */
    char path[GPS_PATH_MAX] = "";

    session->kernelpps_handle = -1;
    if ( !isatty(session->gpsdata.gps_fd) ) {
	gpsd_report(session->context->debug, LOG_INF, "KPPS gps_fd not a tty\n");
    	return -1;
    }
    /* Attach the line PPS discipline, so no need to ldattach */
    /* This activates the magic /dev/pps0 device */
    /* Note: this ioctl() requires root */
    if ( 0 > ioctl(session->gpsdata.gps_fd, TIOCSETD, &ldisc)) {
	gpsd_report(session->context->debug, LOG_INF,
		    "KPPS cannot set PPS line discipline: %s\n",
		    strerror(errno));
    	return -1;
    }

    /* uh, oh, magic file names!, this is not how RFC2783 was designed */
    /* need to look in /sys/devices/virtual/pps/pps?/path
     * (/sys/class/pps/pps?/path is just a link to that)
     * to find the /dev/pps? that matches our serial port.
     * this code fails if there are more then 10 pps devices.
     *
     * yes, this could be done with libsysfs, but trying to keep the
     * number of required libs small */
    memset( (void *)&globbuf, 0, sizeof(globbuf));
    glob("/sys/devices/virtual/pps/pps?/path", 0, NULL, &globbuf);

    memset( (void *)&path, 0, sizeof(path));
    for ( i = 0; i < globbuf.gl_pathc; i++ ) {
        int fd = open(globbuf.gl_pathv[i], O_RDONLY);
	if ( 0 <= fd ) {
	    ssize_t r = read( fd, path, sizeof(path) -1);
	    if ( 0 < r ) {
		path[r - 1] = '\0'; /* remove trailing \x0a */
	    }
	    close(fd);
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

    if ( 0 == pps_num ) {
	gpsd_report(session->context->debug, LOG_INF, "KPPS device not found.\n");
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
    int ret = open(path, O_RDWR);
    if ( 0 > ret ) {
	gpsd_report(session->context->debug, LOG_INF,
		    "KPPS cannot open %s: %s\n", path, strerror(errno));
    	return -1;
    }
    /* RFC 2783 implies the time_pps_setcap() needs priviledges *
     * keep root a tad longer just in case */
    if ( 0 > time_pps_create(ret, &session->kernelpps_handle )) {
	gpsd_report(session->context->debug, LOG_INF,
		    "KPPS time_pps_create(%d) failed: %s\n",
		    ret, strerror(errno));
    	return -1;
    } else {
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

        /* linux 2.6.34 can not PPS_ECHOASSERT | PPS_ECHOCLEAR */
        memset( (void *)&pp, 0, sizeof(pps_params_t));
        pp.mode = PPS_CAPTUREBOTH;

        if ( 0 > time_pps_setparams(session->kernelpps_handle, &pp)) {
	    gpsd_report(session->context->debug, LOG_ERROR,
		"KPPS time_pps_setparams() failed: %s\n", strerror(errno));
	    time_pps_destroy(session->kernelpps_handle);
	    return -1;
        }
    }

    /* root privs are not required past this point */

    /*@ -unrecog  (splint has no pthread declarations as yet) @*/
    (void)pthread_mutex_lock(&initialization_mutex);
    /* +unrecog */
    --uninitialized_pps_thread_count;
    /*@ -unrecog (splint has no pthread declarations as yet) @*/
    (void)pthread_mutex_unlock(&initialization_mutex);
    /* +unrecog */

    return 0;
}
#endif /* defined(HAVE_SYS_TIMEPPS_H) */

/*@-mustfreefresh -type@ -unrecog*/
static /*@null@*/ void *gpsd_ppsmonitor(void *arg)
{
    struct gps_device_t *session = (struct gps_device_t *)arg;
    struct timeval  tv;
    struct timespec ts;
#if defined(TIOCMIWAIT)
    int cycle, duration, state = 0, laststate = -1, unchanged = 0;
    struct timeval pulse[2] = { {0, 0}, {0, 0} };
#endif /* TIOCMIWAIT */
#if defined(HAVE_SYS_TIMEPPS_H)
    int edge_kpps = 0;       /* 0 = clear edge, 1 = assert edge */
    int cycle_kpps, duration_kpps;
    struct timespec pulse_kpps[2] = { {0, 0}, {0, 0} };
    struct timespec tv_kpps;
    pps_info_t pi;
#endif

    gpsd_report(session->context->debug, LOG_PROG,
		"PPS Create Thread gpsd_ppsmonitor\n");

    /* wait for the device to go active - makes this safe to call early */
    /*@-infloops@*/
    while (BAD_SOCKET(session->gpsdata.gps_fd)) {
	/* gpsd_report(session->context->debug, LOG_PROG, "PPS thread awaiting device activation\n"); */
	(void)sleep(1);
    }
    /*@+infloops@*/

    /*  Activates PPS support for RS-232 or USB devices only. */
    if (!(session->sourcetype == source_rs232 || session->sourcetype == source_usb)) {
	gpsd_report(session->context->debug, LOG_PROG,
		    "PPS thread deactivation. Not RS-232 or USB device.\n");
	if (session->thread_error_hook != NULL)
	    session->thread_error_hook(session);
	return NULL;
    }

    if (session->thread_init_hook != NULL)
	session->thread_init_hook(session);

#if defined(HAVE_SYS_TIMEPPS_H)
    /* some operations in init_kernel_pps() require root privs */
    (void)init_kernel_pps( session );
    if ( 0 <= session->kernelpps_handle ) {
	gpsd_report(session->context->debug, LOG_WARN,
		    "KPPS kernel PPS will be used\n");
    }
    memset( (void *)&pi, 0, sizeof(pps_info_t));
#endif

    /* root privileges are not required after this point */

    /*
     * Wait for status change on any handshake line. The only assumption here
     * is that no GPS lights up more than one of these pins.  By waiting on
     * all of them we remove a configuration switch.
     */
    while (session->thread_report_hook != NULL) {
	bool ok = false;
#if defined(HAVE_SYS_TIMEPPS_H)
	bool ok_kpps = false;
#endif /* HAVE_SYS_TIMEPPS_H */
	char *log = NULL;

#if defined(TIOCMIWAIT)
#define PPS_LINE_TIOC (TIOCM_CD|TIOCM_CAR|TIOCM_RI|TIOCM_CTS)
        if (ioctl(session->gpsdata.gps_fd, TIOCMIWAIT, PPS_LINE_TIOC) != 0) {
	    gpsd_report(session->context->debug, LOG_ERROR,
			"PPS ioctl(TIOCMIWAIT) failed: %d %.40s\n",
			errno, strerror(errno));
	    break;
	}
#endif /* TIOCMIWAIT */

/*@-noeffect@*/
#ifdef HAVE_CLOCK_GETTIME
	/* using  clock_gettime() here, that is nSec,
	 * not uSec like gettimeofday */
	if ( 0 > clock_gettime(CLOCK_REALTIME, &ts) ) {
	    /* uh, oh, can not get time! */
	    gpsd_report(session->context->debug, LOG_ERROR,
			"PPS clock_gettime() failed\n");
	    break;
	}
	TSTOTV( &tv, &ts);
#else
	if ( 0 > gettimeofday(&tv, NULL) ) {
	    /* uh, oh, can not get time! */
	    gpsd_report(session->context->debug, LOG_ERROR,
			"PPS gettimeofday() failed\n");
	    break;
	}
	TVTOTS( &ts, &tv);
#endif
/*@+noeffect@*/

	/* ok and log used by KPPS and TIOMCWAIT */
	ok = false;  
	log = NULL;  
#if defined(HAVE_SYS_TIMEPPS_H)
        if ( 0 <= session->kernelpps_handle ) {
	    struct timespec kernelpps_tv;
	    /* on a quad core 2.4GHz Xeon this removes about 20uS of
	     * latency, and about +/-5uS of jitter over the other method */
            memset( (void *)&kernelpps_tv, 0, sizeof(kernelpps_tv));
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
		    tv_kpps = pi.assert_timestamp;
	    	} else if ( pi.assert_timestamp.tv_sec < pi.clear_timestamp.tv_sec ) {
		    edge_kpps = 0;
		    tv_kpps = pi.clear_timestamp;
		} else if ( pi.assert_timestamp.tv_nsec > pi.clear_timestamp.tv_nsec ) {
		    edge_kpps = 1;
		    tv_kpps = pi.assert_timestamp;
		} else {
		    edge_kpps = 0;
		    tv_kpps = pi.clear_timestamp;
		}
		gpsd_report(session->context->debug, LOG_PROG,
			    "KPPS assert %ld.%09ld, sequence: %ld - "
			    "clear  %ld.%09ld, sequence: %ld\n",
			    pi.assert_timestamp.tv_sec,
			    pi.assert_timestamp.tv_nsec,
			    pi.assert_sequence,
			    pi.clear_timestamp.tv_sec,
			    pi.clear_timestamp.tv_nsec,
			    pi.clear_sequence);
		gpsd_report(session->context->debug, LOG_PROG,
			    "KPPS data: using %s\n",
			    edge_kpps ? "assert" : "clear");

#define timediff_kpps(x, y)	(int)((x.tv_sec-y.tv_sec)*1000000+((x.tv_nsec-y.tv_nsec)/1000))
	        cycle_kpps = timediff_kpps(tv_kpps, pulse_kpps[edge_kpps]);
	        duration_kpps = timediff_kpps(tv_kpps, pulse_kpps[(int)(edge_kpps == 0)]);
		if ( 3000000 < duration_kpps ) {
		    // invisible pulse
		    duration_kpps = 0;
		}
#undef timediff_kpps
	        gpsd_report(session->context->debug, LOG_INF,
		    "KPPS cycle: %7d, duration: %7d @ %lu.%09lu\n",
		    cycle_kpps, duration_kpps,
		    (unsigned long)tv_kpps.tv_sec,
		    (unsigned long)tv_kpps.tv_nsec);
		pulse_kpps[edge_kpps] = tv_kpps;
		if (990000 < cycle_kpps && 1010000 > cycle_kpps) {
		    /* KPPS passes a basic sanity check */
		    ok_kpps = true;
		    log = "KPPS";
		}
	    }
	}
#endif /* HAVE_SYS_TIMEPPS_H */

#if defined(TIOCMIWAIT)

	/*@ +ignoresigns */
	if (ioctl(session->gpsdata.gps_fd, TIOCMGET, &state) != 0) {
	    gpsd_report(session->context->debug, LOG_ERROR,
			"PPS ioctl(TIOCMGET) failed\n");
	    break;
	}
	/*@ -ignoresigns */

	state = (int)((state & PPS_LINE_TIOC) != 0);
	/*@ +boolint @*/
#define timediff(x, y)	(int)((x.tv_sec-y.tv_sec)*1000000+x.tv_usec-y.tv_usec)
	cycle = timediff(tv, pulse[state]);
	duration = timediff(tv, pulse[(int)(state == 0)]);
#undef timediff
	/*@ -boolint @*/

	if (state == laststate) {
	    /* some pulses may be so short that state never changes */
	    if (999000 < cycle && 1001000 > cycle) {
		duration = 0;
		unchanged = 0;
		gpsd_report(session->context->debug, LOG_RAW,
			    "PPS pps-detect on %s invisible pulse\n",
			    session->gpsdata.dev.path);
	    } else if (++unchanged == 10) {
		unchanged = 1;
		gpsd_report(session->context->debug, LOG_WARN,
			    "PPS TIOCMIWAIT returns unchanged state, ppsmonitor sleeps 10\n");
		(void)sleep(10);
	    }
	} else {
	    gpsd_report(session->context->debug, LOG_RAW,
			"PPS pps-detect on %s changed to %d\n",
			session->gpsdata.dev.path, state);
	    laststate = state;
	    unchanged = 0;
	}
	pulse[state] = tv;
	if (unchanged) {
	    // strange, try again
	    continue;
	}
	gpsd_report(session->context->debug, LOG_INF,
		    "PPS cycle: %7d, duration: %7d @ %lu.%06lu\n",
		    cycle, duration,
		    (unsigned long)tv.tv_sec, (unsigned long)tv.tv_usec);

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
	if (199000 > cycle) {
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
		if (state == 1) {
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

	if (ok) {
	    long l_offset;
	    char *log1 = NULL;
	    /* actual_tv is the time we think the pulse represents  */
	    struct timeval  actual_tv;
	    /* edge_offset is the skew from expected to observed pulse time */
	    double edge_offset;
	    gpsd_report(session->context->debug, LOG_RAW,
			"PPS edge accepted %.100s", log);
#if defined(HAVE_SYS_TIMEPPS_H)
            if ( 0 <= session->kernelpps_handle && ok_kpps) {
		/* use KPPS time */
		/* pick the right edge */
		if ( edge_kpps ) {
		    ts = pi.assert_timestamp; /* structure copy */
		} else {
		    ts = pi.clear_timestamp;  /* structure copy */
		}
	    } else
#endif
	    {
	        // use plain PPS
		TVTOTS( &ts, &tv);
	    }

            /* This innocuous-looking "+ 1" embodies a significant
             * assumption: that GPSes report time to the second over the
             * serial stream *after* * emitting PPS for the top of second.
             * Thus, when we see PPS our available report is from the
             * previous cycle and we must increment. 
             */

	    /*@+relaxtypes@*/
	    actual_tv.tv_sec = session->last_fixtime + 1;
	    actual_tv.tv_usec = 0;  /* need to be fixed for 5Hz */
	    /*@-relaxtypes@*/

	    edge_offset = actual_tv.tv_sec - ts.tv_sec;
	    edge_offset -= ts.tv_nsec / 1e9;

	    /* check to see if we have a fresh timestamp from the
	     * GPS serial input then use that */
	    l_offset = (long)edge_offset;
	    if (0 > l_offset || 1000000 < l_offset) {
		gpsd_report(session->context->debug, LOG_RAW,
			    "PPS: no current GPS seconds: %ld\n",
			    (long)l_offset);
		log1 = "timestamp out of range";
	    }
	    else if (session->thread_report_hook != NULL)
		log1 = session->thread_report_hook(session,
						   &actual_tv, &ts, edge_offset);
	    else
		log1 = "no report hook";
	    gpsd_report(session->context->debug, LOG_RAW,
		    "PPS edge %.20s %lu.%06lu offset %.9f\n",
		    log1,
		    (unsigned long)ts.tv_sec,
		    (unsigned long)ts.tv_nsec,
		    edge_offset);
	    if (session->context->pps_hook != NULL)
		session->context->pps_hook(session, actual_tv.tv_sec, &ts);
	} else {
	    gpsd_report(session->context->debug, LOG_RAW,
			"PPS edge rejected %.100s", log);
	}

    }
#if defined(HAVE_SYS_TIMEPPS_H)
    if (session->kernelpps_handle > 0) {
	gpsd_report(session->context->debug, LOG_PROG, "PPS descriptor cleaned up\n");
	time_pps_destroy(session->kernelpps_handle);
    }
#endif
    if (session->thread_init_hook != NULL)
	session->thread_wrap_hook(session);
    gpsd_report(session->context->debug, LOG_PROG, "PPS gpsd_ppsmonitor exited.\n");
    return NULL;
}
/*@+mustfreefresh +type +unrecog@*/

/*
 * Entry points begin here.
 */

void pps_thread_activate(struct gps_device_t *session)
/* activate a thread to watch the device's PPS transitions */
{
    pthread_t pt;
    if (session->shmTimeP >= 0) {
	gpsd_report(session->context->debug, LOG_PROG, "PPS thread launched\n");
#if defined(HAVE_SYS_TIMEPPS_H)
	/*@ -unrecog  (splint has no pthread declarations as yet) @*/
	(void)pthread_mutex_lock(&initialization_mutex);
	/* +unrecog */
	++uninitialized_pps_thread_count;
	/*@ -unrecog (splint has no pthread declarations as yet) @*/
	(void)pthread_mutex_unlock(&initialization_mutex);
	/* +unrecog */
#endif /* defined(HAVE_SYS_TIMEPPS_H) */
	/*@-compdef -nullpass@*/
	(void)pthread_create(&pt, NULL, gpsd_ppsmonitor, (void *)session);
        /*@+compdef +nullpass@*/
    }
}

void pps_thread_deactivate(struct gps_device_t *session)
/* cleanly terminate PPS thread */
{
    session->thread_report_hook = NULL;
}

#if defined(HAVE_SYS_TIMEPPS_H)
void gpsd_await_pps_initialization(struct gps_context_t *context)
/* wait for all threads seeking kernel PPS to open /dev/pps devices */
{
    /* wait only this many intervals if some thread croaked too early */
    static int dropdead = 100;

    gpsd_report(context->debug, LOG_WARN,
		"waiting on KPPS initalization...\n");

    while (dropdead-- > 0)
    {
	/*@ -unrecog  (splint has no pthread declarations as yet) @*/
	(void)pthread_mutex_lock(&initialization_mutex);
	/* +unrecog */
	if (uninitialized_pps_thread_count == 0)
	    return;
	/*@ -unrecog (splint has no pthread declarations as yet) @*/
	(void)pthread_mutex_unlock(&initialization_mutex);
	/* +unrecog */
	usleep(1000);
    }

    gpsd_report(context->debug, LOG_WARN,
		"wait for KPPS initalization timed out\n");

}
#endif /* defined(HAVE_SYS_TIMEPPS_H) */

/* end */

