/*
 * ppsthread.c - manage PPS watcher threads
 *
 * If you are not good at threads do not touch this file!
 * For example: errno is thread safe; strerror() is not.
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
 * To use the thread manager, you need to first fill in the
 * devicefd, devicename, and the four hook function members in the thread
 * context structure. The void *context member is available for your hook
 * functions to use; the thread-monitor code doesn't touch it.
 *
 * After this setup, you can call pps_thread_activate() and the
 * thread will launch.  It is OK to do this before the device is open,
 * the thread will wait on that.
 * 
 * WARNING!  Loss of precision
 * UNIX time to nanoSec precision is 62 significant bits
 * UNIX time to nanoSec precision after 2038 is 63 bits
 * a double is only 53 significant bits.
 * 
 * You cannot do PPS math with doubles
 *
 * This file is Copyright (c) 2013 by the GPSD project. BSD terms
 * apply: see the file COPYING in the distribution root for details.
 */

#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <limits.h>
#include <errno.h>
#include <pthread.h>
#include <math.h>
#ifndef S_SPLINT_S
#include <sys/socket.h>
#include <unistd.h>
#endif /* S_SPLINT_S */

#include "gpsd_config.h"
#include "timespec_str.h"
#include "ppsthread.h"
#include "compiler.h"	/* required for erno thread-safety */

#ifdef PPS_ENABLE
#if defined(HAVE_SYS_TIMEPPS_H)
#include <fcntl.h>	/* needed for open() and friends */
#endif /* defined(HAVE_SYS_TIMEPPS_H) */

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

/* normalize a timespec
 *
 * three cases to note
 * if tv_sec is positve, then tv_nsec must be positive
 * if tv_sec is negative, then tv_nsec must be negative
 * if tv_sec is zero, then tv_nsec may be positive or negative.
 *
 * this only handles the case where two normalized timespecs
 * are added or subracted.  (e.g. only a one needs to be borrowed/carried
 */
/*@-type -noeffect@*/ /* splint is confused about struct timespec */
/*@unused@*/static inline void TS_NORM( struct timespec *ts)
{
    if ( (  1 <= ts->tv_sec ) ||
         ( (0 == ts->tv_sec ) && (0 <= ts->tv_nsec ) ) ) {
        /* result is positive */
	if ( 1000000000 <= ts->tv_nsec ) {
            /* borrow from tv_sec */
	    ts->tv_nsec -= 1000000000;
	    ts->tv_sec++;
	} else if ( 0 > (ts)->tv_nsec ) {
            /* carry to tv_sec */
	    ts->tv_nsec += 1000000000;
	    ts->tv_sec--;
	}
    }  else {
        /* result is negative */
	if ( -1000000000 >= ts->tv_nsec ) {
            /* carry to tv_sec */
	    ts->tv_nsec += 1000000000;
	    ts->tv_sec--;
	} else if ( 0 < ts->tv_nsec ) {
            /* borrow from tv_sec */
	    ts->tv_nsec -= 1000000000;
	    ts->tv_sec++;
	}
    }
}
/*@+type +noeffect@*/

/* subtract two timespec */
#define TS_SUB(r, ts1, ts2) \
    do { \
	(r)->tv_sec = (ts1)->tv_sec - (ts2)->tv_sec; \
	(r)->tv_nsec = (ts1)->tv_nsec - (ts2)->tv_nsec; \
        TS_NORM( r ); \
    } while (0)


/*@i1@*/static pthread_mutex_t ppslast_mutex = PTHREAD_MUTEX_INITIALIZER;

#if defined(HAVE_SYS_TIMEPPS_H)
/*@-compdestroy -nullpass -unrecog@*/
static int init_kernel_pps(volatile struct pps_thread_t *pps_thread)
/* return handle for kernel pps, or -1; requires root privileges */
{
#ifndef S_SPLINT_S
    pps_params_t pp;
#endif /* S_SPLINT_S */
    int ret;
#ifdef __linux__
    /* These variables are only needed by Linux to find /dev/ppsN. */
    int ldisc = 18;   /* the PPS line discipline */
    glob_t globbuf;
    char path[PATH_MAX] = "";
#endif

    pps_thread->kernelpps_handle = -1;

    /*
     * This next code block abuses "ret" by storing the filedescriptor
     * to use for RFC2783 calls.
     */
    ret = -1;
#ifdef __linux__
    /*
     * Some Linuxes, like the RasbPi's, have PPS devices preexisting.
     * Other OS have no way to automatically determine the proper /dev/ppsX.
     * Allow user to pass in an explicit PPS device path.
     */
    if (strncmp(pps_thread->devicename, "/dev/pps", 8) == 0)
	strlcpy(path, pps_thread->devicename, sizeof(path));
    else {
	char pps_num = '\0';  /* /dev/pps[pps_num] is our device */
	size_t i;             /* to match type of globbuf.gl_pathc */
	/*
	 * Otherwise one must make calls to associate a serial port with a
	 * /dev/ppsN device and then grovel in system data to determine
	 * the association.
	 */
	/*@+ignoresigns@*/
	/* Attach the line PPS discipline, so no need to ldattach */
	/* This activates the magic /dev/pps0 device */
	/* Note: this ioctl() requires root, and device is a tty */
	if ( 0 > ioctl(pps_thread->devicefd, TIOCSETD, &ldisc)) {
	    char errbuf[BUFSIZ] = "unknown error";
	    strerror_r(errno, errbuf, sizeof(errbuf));
	    pps_thread->log_hook(pps_thread, THREAD_INF,
				 "KPPS:%s cannot set PPS line discipline %s\n",
				 pps_thread->devicename, errbuf);
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
	    pps_thread->log_hook(pps_thread, THREAD_INF,
				 "KPPS:%s checking %s, %s\n",
				 pps_thread->devicename,
				 globbuf.gl_pathv[i], path);
	    if ( 0 == strncmp( path, pps_thread->devicename, sizeof(path))) {
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
	    pps_thread->log_hook(pps_thread, THREAD_INF,
				 "KPPS:%s device not found.\n",
				 pps_thread->devicename);
	    return -1;
	}
	/* construct the magic device path */
	(void)snprintf(path, sizeof(path), "/dev/pps%c", pps_num);
    }

    /* root privs are probably required for this device open
     * do not bother to check uid, just go for the open() */
    ret = open(path, O_RDWR);
    if ( 0 > ret ) {
	char errbuf[BUFSIZ] = "unknown error";
	(void)strerror_r(errno, errbuf, sizeof(errbuf));
	pps_thread->log_hook(pps_thread, THREAD_INF,
		    "KPPS:%s cannot open %s: %s\n", 
		    pps_thread->devicename,
                    path, errbuf);
    	return -1;
    }
#else /* not __linux__ */
    /*
     * On BSDs that support RFC2783, one uses the API calls on serial
     * port file descriptor.
     *
     * FIXME! need more specific than 'not linux'
     */
    strlcpy(path, pps_thread->devicename, sizeof(path));
    // cppcheck-suppress redundantAssignment
    ret  = pps_thread->devicefd;
#endif
    /* assert(ret >= 0); */
    pps_thread->log_hook(pps_thread, THREAD_INF,
		"KPPS:%s RFC2783 path:%s, fd is %d\n",
	        pps_thread->devicename, path,
		ret);

    /* RFC 2783 implies the time_pps_setcap() needs priviledges *
     * keep root a tad longer just in case */
    if ( 0 > time_pps_create(ret, (pps_handle_t *)&pps_thread->kernelpps_handle )) {
	char errbuf[BUFSIZ] = "unknown error";
	(void)strerror_r(errno, errbuf, (int)sizeof(errbuf));
	pps_thread->log_hook(pps_thread, THREAD_INF,
		    "KPPS:%s time_pps_create(%d) failed: %s\n",
	            pps_thread->devicename,
		    ret, errbuf);
    	return -1;
    }
#ifndef S_SPLINT_S
    /* have kernel PPS handle */
    int pps_caps;
    /* get RFC2783 features supported */
    if ( 0 > time_pps_getcap(pps_thread->kernelpps_handle, &pps_caps)) {
	pps_caps = 0;
	pps_thread->log_hook(pps_thread, THREAD_ERROR,
		    "KPPS:%s time_pps_getcap() failed\n",
		    pps_thread->devicename);
    } else {
	pps_thread->log_hook(pps_thread, THREAD_INF, 
		    "KPPS:%s pps_caps 0x%02X\n", 
		    pps_thread->devicename,
		    pps_caps);
    }
#endif /* S_SPLINT_S */

    /* construct the setparms structure */
    memset( (void *)&pp, 0, sizeof(pps_params_t));
    pp.api_version = PPS_API_VERS_1;  /* version 1 protocol */
    if ( 0 == (PPS_TSFMT_TSPEC & pps_caps ) ) {
       /* PPS_TSFMT_TSPEC means return a timespec
        * mandatory for driver to implement, require it */
	pps_thread->log_hook(pps_thread, THREAD_ERROR,
		    "KPPS:%s fail, missing PPS_TSFMT_TSPEC\n",
		    pps_thread->devicename);
        return -1;
    }
    pp.mode = PPS_TSFMT_TSPEC;
    switch ( (PPS_CAPTUREASSERT | PPS_CAPTURECLEAR) & pps_caps ) {
    case PPS_CAPTUREASSERT:
	pps_thread->log_hook(pps_thread, THREAD_WARN,
		    "KPPS:%s missing PPS_CAPTURECLEAR, pulse may be offset\n",
		    pps_thread->devicename);
	pp.mode |= PPS_CAPTUREASSERT;
        break;
    case PPS_CAPTURECLEAR:
	pps_thread->log_hook(pps_thread, THREAD_WARN,
		    "KPPS:%s missing PPS_CAPTUREASSERT, pulse may be offset\n",
		    pps_thread->devicename);
	pp.mode |= PPS_CAPTURECLEAR;
        break;
    case PPS_CAPTUREASSERT | PPS_CAPTURECLEAR:
	pp.mode |= PPS_CAPTUREASSERT | PPS_CAPTURECLEAR;
        break;
    default:
	pps_thread->log_hook(pps_thread, THREAD_WARN,
		    "KPPS:%s missing PPS_CAPTUREASSERT and CLEAR\n",
		    pps_thread->devicename);
        return -1;
    }

    if ( 0 > time_pps_setparams(pps_thread->kernelpps_handle, &pp)) {
	char errbuf[BUFSIZ] = "unknown error";
	(void)strerror_r(errno, errbuf, (int)sizeof(errbuf));
	pps_thread->log_hook(pps_thread, THREAD_ERROR,
	    "KPPS:%s time_pps_setparams(mode=0x%02X) failed: %s\n", 
	    pps_thread->devicename, pp.mode,
	    errbuf);
	time_pps_destroy(pps_thread->kernelpps_handle);
	return -1;
    }
    return 0;
}
/*@+compdestroy +nullpass +unrecog@*/
#endif /* defined(HAVE_SYS_TIMEPPS_H) */

/*@-mustfreefresh -type -unrecog -branchstate@*/
static /*@null@*/ void *gpsd_ppsmonitor(void *arg)
{
    char ts_str1[TIMESPEC_LEN], ts_str2[TIMESPEC_LEN];
    volatile struct pps_thread_t *thread_context = (struct pps_thread_t *)arg;
    /* the system clock time, to the nSec, when the last fix received */
    /* using a double would cause loss of precision */
    volatile struct timedelta_t last_fixtime = {{0, 0}, {0, 0}};
    struct timespec clock_ts = {0, 0};
    time_t last_second_used = 0;
#if defined(TIOCMIWAIT)
    int cycle, duration; 
    /* state is the last state of the tty control signals */
    int state = 0, unchanged = 0;
    /* state_last is previous state */
    int state_last = 0;
    /* pulse stores the time of the last two edges */
    struct timespec pulse[2] = { {0, 0}, {0, 0} };
    /* edge, used as index into pulse to find previous edges */
    int edge = 0;       /* 0 = clear edge, 1 = assert edge */
#endif /* TIOCMIWAIT */
#if defined(HAVE_SYS_TIMEPPS_H)
#ifndef S_SPLINT_S
    int edge_kpps = 0;       /* 0 = clear edge, 1 = assert edge */
    int cycle_kpps, duration_kpps;
    /* kpps_pulse stores the time of the last two edges */
    struct timespec pulse_kpps[2] = { {0, 0}, {0, 0} };
    struct timespec ts_kpps;
    pps_info_t pi;

    memset( (void *)&pi, 0, sizeof(pps_info_t));
#endif /* S_SPLINT_S */
#endif /* defined(HAVE_SYS_TIMEPPS_H) */
    /* pthread error return */
    int pthread_err; 
    bool not_a_tty = false;
    bool pps_canwait = false;

    if ( isatty(thread_context->devicefd) == 0 ) {
	thread_context->log_hook(thread_context, THREAD_INF, 
            "KPPS:%s gps_fd:%d not a tty\n", 
            thread_context->devicename,
            thread_context->devicefd);
        /* why do we care the device is a tty? so as not to ioctl(TIO..)
        * /dev/pps0 is not a tty and we need to use it */
        not_a_tty = true;
    }
    /* if no TIOCMIWAIT, we hope to have PPS_CANWAIT */

#if defined(HAVE_SYS_TIMEPPS_H)
    int pps_caps;
    /* get RFC2783 features supported */
    if ( 0 > time_pps_getcap(thread_context->kernelpps_handle, &pps_caps)) {
	pps_caps = 0;
	thread_context->log_hook(thread_context, THREAD_ERROR,
		    "KPPS:%s time_pps_getcap() failed\n",
		    thread_context->devicename);
    } else {
	thread_context->log_hook(thread_context, THREAD_INF, 
		    "KPPS:%s pps_caps 0x%02X\n", 
		    thread_context->devicename,
		    pps_caps);
    }

    if ( 0 != (PPS_CANWAIT & pps_caps ) ) {
       /* we can wait! */
	thread_context->log_hook(thread_context, THREAD_PROG,
		    "KPPS:%s have PPS_CANWAIT\n",
	 	    thread_context->devicename);
        // pps_canwait = true; // broken now, 
    }
#endif  /* HAVE_SYS_TIMEPPS_H */

    if ( not_a_tty && !pps_canwait ) {
	/* for now, no way to wait for an edge, in the future maybe figure out
         * a sleep */
    }

    /*
     * Wait for status change on any handshake line.  Just one edge,
     * we do not want to be spinning waiting for the trailing edge of
     * a pulse. The only assumption here is that no GPS lights up more
     * than one of these pins.  By waiting on all of them we remove a
     * configuration switch. 
     *
     * Once we have the latest edge we compare it to the last edge which we
     * stored.  If the edge passes sanity checks we pass is out through
     * the call context.
     */
    while (thread_context->report_hook != NULL
           || thread_context->pps_hook != NULL) {
	bool ok = false;
#ifndef S_SPLINT_S
#if defined(HAVE_SYS_TIMEPPS_H)
	// cppcheck-suppress variableScope
	bool ok_kpps = false;
#endif /* HAVE_SYS_TIMEPPS_H */
#endif /* S_SPLINT_S */
	char *log = NULL;

#if defined(TIOCMIWAIT)
        /* we are lucky to have TIOCMIWAIT, so wait for next edge */
#define PPS_LINE_TIOC (TIOCM_CD|TIOCM_RI|TIOCM_CTS|TIOCM_DSR)
	/*
	 * DB9  DB25  Name      Full name
	 * ---  ----  ----      --------------------
	 *  3     2    TXD  --> Transmit Data
	 *  2     3    RXD  <-- Receive Data
	 *  7     4    RTS  --> Request To Send
	 *  8     5    CTS  <-- Clear To Send
	 *  6     6    DSR  <-- Data Set Ready
	 *  4    20    DTR  --> Data Terminal Ready
	 *  1     8    DCD  <-- Data Carrier Detect
	 *  9    22    RI   <-- Ring Indicator
	 *  5     7    GND      Signal ground
	 *
	 * Note that it only makes sense to wait on handshake lines
	 * activated from the receive side (DCE->DTE) here; in this
	 * context "DCE" is the GPS. {CD,RI,CTS,DSR} is the
	 * entire set of these.
	 */
        if ( !not_a_tty && !pps_canwait ) {
            /* we are a tty, so can TIOCMIWAIT */
            /* we have no PPS_CANWAIT, so must TIOCMIWAIT */
	    if (ioctl(thread_context->devicefd, TIOCMIWAIT, PPS_LINE_TIOC) != 0) {
		char errbuf[BUFSIZ] = "unknown error";
		(void)strerror_r(errno, errbuf, sizeof(errbuf));
		thread_context->log_hook(thread_context, THREAD_WARN,
			"PPS:%s ioctl(TIOCMIWAIT) failed: %d %.40s\n",
			thread_context->devicename, errno, errbuf);
		break;
	    }
        
	    /*
	     * Start of time critical section 
	     * Only error reporting, not success reporting in critical section
	     */

	    /* quick, grab a copy of last_fixtime before it changes */
	    /*@ -unrecog  (splint has no pthread declarations as yet) @*/
	    pthread_err = pthread_mutex_lock(&ppslast_mutex);
	    if ( 0 != pthread_err ) {
		char errbuf[BUFSIZ] = "unknown error";
		(void)strerror_r(errno, errbuf, sizeof(errbuf));
		thread_context->log_hook(thread_context, THREAD_ERROR,
			"PPS:%s pthread_mutex_lock() : %s\n", 
			    thread_context->devicename, errno, errbuf);
	    }
	    /*@ +unrecog @*/
	    last_fixtime = thread_context->fixin;
	    /*@ -unrecog (splint has no pthread declarations as yet) @*/
	    pthread_err = pthread_mutex_unlock(&ppslast_mutex);
	    if ( 0 != pthread_err ) {
		char errbuf[BUFSIZ] = "unknown error";
		(void)strerror_r(errno, errbuf, sizeof(errbuf));
		thread_context->log_hook(thread_context, THREAD_ERROR,
			    "PPS:%s pthread_mutex_unlock() : %s\n", 
			    thread_context->devicename, errno, errbuf);
	    }
	    /*@ +unrecog @*/

/*@-noeffect@*/
	    /* get the time after we just woke up */
	    if ( 0 > clock_gettime(CLOCK_REALTIME, &clock_ts) ) {
		/* uh, oh, can not get time! */
		thread_context->log_hook(thread_context, THREAD_ERROR,
			    "PPS:%s clock_gettime() failed\n",
			    thread_context->devicename);
		break;
	    }
/*@+noeffect@*/
	 
	    /* got the edge, got the time just after the edge, now quickly
	     * get the edge state */
	    /*@ +ignoresigns */
	    if (ioctl(thread_context->devicefd, TIOCMGET, &state) != 0) {
		thread_context->log_hook(thread_context, THREAD_ERROR,
			    "PPS:%s ioctl(TIOCMGET) failed\n",
			    thread_context->devicename);
		break;
	    }
	    /*@ -ignoresigns */
	    /* end of time critical section */
	    thread_context->log_hook(thread_context, THREAD_PROG,
			"PPS:%s ioctl(TIOCMIWAIT) succeeded\n",
			thread_context->devicename);

	    /* mask for monitored lines */
	    state &= PPS_LINE_TIOC;
	    edge = (state > state_last) ? 1 : 0;
        }
#endif /* TIOCMIWAIT */


	/* ok and log used by KPPS and TIOCMIWAIT */
	// cppcheck-suppress redundantAssignment
	ok = false;  
	log = NULL;  
#if defined(HAVE_SYS_TIMEPPS_H) && !defined(S_SPLINT_S)
        if ( 0 <= thread_context->kernelpps_handle ) {
	    struct timespec kernelpps_tv;
	    /* on a quad core 2.4GHz Xeon using KPPS timestamp instead of plain 
             * PPS timestamp removes about 20uS of latency, and about +/-5uS 
             * of jitter 
             */
	    if ( pps_canwait ) {
		/*
		 * RFC2783 specifies that a NULL timeval means to wait, if
                 * PPS_CANWAIT is available.
                 *
                 * since we pps_canwait, we skipped the TIOMCIWAIT
                 *
                 * 3 second time out, some GPS output 0.5Hz and some RFC2783
                 * can only trigger on one edge
                 * a better and more complex solution would be to wait 
                 * for 1/20 second and suffer the cycles
		 */
		kernelpps_tv.tv_sec = 3;
		kernelpps_tv.tv_nsec = 0;
	    } else {
		/*
		 * We use of a non-NULL zero timespec here,
		 * which means to return immediately with -1 (section
		 * 3.4.3).  This is because we know we just got a pulse because 
		 * TIOCMIWAIT just woke up.
		 * The timestamp has already been captured in the kernel, and we 
		 * are merely fetching it here.
		 */
		memset( (void *)&kernelpps_tv, 0, sizeof(kernelpps_tv));
	    }
	    if ( 0 > time_pps_fetch(thread_context->kernelpps_handle, PPS_TSFMT_TSPEC
	        , &pi, &kernelpps_tv)) {

		char errbuf[BUFSIZ] = "unknown error";
		(void)strerror_r(errno, errbuf, sizeof(errbuf));
		thread_context->log_hook(thread_context, THREAD_ERROR,
			    "KPPS:%s kernel PPS failed %s\n",
			    thread_context->devicename, errbuf);
                if ( ETIMEDOUT == errno || EINTR == errno ) {
			/* just a timeout */
                        continue;
                }
                break;
	    } else {
thread_context->log_hook(thread_context, THREAD_ERROR,
"KPPS:%s kernel PPS!\n",
thread_context->devicename);
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
		timespec_str( &pi.assert_timestamp, ts_str1, sizeof(ts_str1) );
		timespec_str( &pi.clear_timestamp, ts_str2, sizeof(ts_str2) );
		thread_context->log_hook(thread_context, THREAD_PROG,
			    "KPP:%s assert %s, sequence: %ld - "
			    "clear  %s, sequence: %ld\n",
			    thread_context->devicename,
			    ts_str1,
			    (unsigned long) pi.assert_sequence,
			    ts_str2,
			    (unsigned long) pi.clear_sequence);
		thread_context->log_hook(thread_context, THREAD_PROG,
			    "KPPS:%s data: using %s\n",
			    thread_context->devicename,
			    edge_kpps ? "assert" : "clear");

		/* WARNING! this will fail if delta more than a few seconds,
                  that should not be the case here */
	        cycle_kpps = timespec_diff_ns(ts_kpps, pulse_kpps[edge_kpps])/1000;
	        duration_kpps = timespec_diff_ns(ts_kpps, pulse_kpps[(int)(edge_kpps == 0)])/1000;
		timespec_str( &ts_kpps, ts_str1, sizeof(ts_str1) );
	        thread_context->log_hook(thread_context, THREAD_PROG,
		    "KPPS:%s cycle: %7d uSec, duration: %7d uSec @ %s\n",
		    thread_context->devicename,
		    cycle_kpps, duration_kpps, ts_str1);
		pulse_kpps[edge_kpps] = ts_kpps;
		if (990000 < cycle_kpps && 1010000 > cycle_kpps) {
		    /* KPPS passes a basic sanity check */
		    ok_kpps = true;
		    log = "KPPS";
		}
	    }
	}
#endif /* defined(HAVE_SYS_TIMEPPS_H) && !defined(S_SPLINT_S) */
        if ( not_a_tty && !pps_canwait ) {
	    /* uh, oh, no TIOMCIWAIT, nor RFC2783, die */
	    break;
	}

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
		thread_context->log_hook(thread_context, THREAD_RAW,
			    "PPS:%s pps-detect invisible pulse\n",
			    thread_context->devicename);
	    } else if (++unchanged == 10) {
                /* not really unchanged, just out of bounds */
		unchanged = 1;
		thread_context->log_hook(thread_context, THREAD_WARN,
			    "PPS:%s TIOCMIWAIT returns unchanged state, ppsmonitor sleeps 10\n",
			    thread_context->devicename);
		(void)sleep(10);
	    }
	} else {
	    thread_context->log_hook(thread_context, THREAD_RAW,
			"PPS:%s pps-detect changed to %d\n",
			thread_context->devicename, state);
	    unchanged = 0;
	}
	state_last = state;
        /* save this edge so we know next cycle time */
	pulse[edge] = clock_ts;
	timespec_str( &clock_ts, ts_str1, sizeof(ts_str1) );
	thread_context->log_hook(thread_context, THREAD_PROG,
	    "PPS:%s edge: %d, cycle: %7d uSec, duration: %7d uSec @ %s\n",
	    thread_context->devicename,
	    edge, cycle, duration, ts_str1);
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
	} else if (900000 > cycle) {
            /* Yes, 10% window.  The Rasberry Pi clock is very coarse
             * when it starts and chronyd may be doing a fast slew. 
             * chronyd by default will slew up to 8.334% !
             * Don't worry, ntpd and chronyd will do further sanitizing.*/
	    log = "Too long for 5Hz, too short for 1Hz\n";
	} else if (1100000 > cycle) {
            /* Yes, 10% window.  */
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
	if ( ok && last_second_used >= last_fixtime.real.tv_sec ) {
		/* uh, oh, this second already handled */
		ok = 0;
		log = "this second already handled\n";
	}

	/*
	 * If there has not yet been any valid in-band time stashed
	 * from the GPS when the PPS event was asserted, we can do
	 * nothing further.  Some GPSes like Garmin always send a PPS,
	 * valid or not.  Other GPSes like some uBlox may only send
	 * PPS when time is valid.  It is common to get PPS, and no
	 * fixtime, while autobauding.
	 */
        if (last_fixtime.real.tv_sec == 0) {
	    /* probably should log computed offset jjust for grins here */
	    continue;
        }


	if (ok) {
	    /* offset is the skew from expected to observed pulse time */
            struct timespec offset;
	    /* offset as a printable string */
	    char offset_str[TIMESPEC_LEN];
	    /* delay after last fix */
	    struct timespec  delay;
	    /* delay as a printable string */
	    char delay_str[TIMESPEC_LEN];
	    char *log1 = NULL;
	    /* ppstimes.real is the time we think the pulse represents  */
	    struct timedelta_t ppstimes;
	    thread_context->log_hook(thread_context, THREAD_RAW,
			"PPS:%s edge accepted %.100s", 
			thread_context->devicename, log);
#ifndef S_SPLINT_S
#if defined(HAVE_SYS_TIMEPPS_H)
            if ( 0 <= thread_context->kernelpps_handle && ok_kpps) {
		/* use KPPS time */
		thread_context->log_hook(thread_context, THREAD_RAW,
			    "KPPS:%s using edge %d", 
			    thread_context->devicename,
			    edge_kpps );
		/* pick the right edge */
		if ( edge_kpps ) {
		    clock_ts = pi.assert_timestamp; /* structure copy */
		} else {
		    clock_ts = pi.clear_timestamp;  /* structure copy */
		}
	    } 
#endif /* defined(HAVE_SYS_TIMEPPS_H) */
#endif /* S_SPLINT_S */
	    /* else, use plain PPS */

            /* This innocuous-looking "+ 1" embodies a significant
             * assumption: that GPSes report time to the second over the
             * serial stream *after* emitting PPS for the top of second.
             * Thus, when we see PPS our available report is from the
             * previous cycle and we must increment. 
             *
             * FIXME! The GR-601W at 38,400 or faster can send the
             * serial fix before the interrupt event carrying the PPS 
	     * line assertion by about 10 mSec!
             */

	    /*@+relaxtypes@*/
	    ppstimes.real.tv_sec = (time_t)last_fixtime.real.tv_sec + 1;
	    ppstimes.real.tv_nsec = 0;  /* need to be fixed for 5Hz */
	    ppstimes.clock = clock_ts;
	    /*@-relaxtypes@*/

	    /* check to see if we have a fresh timestamp from the
	     * GPS serial input then use that */
	    /*@-compdef@*/
	    TS_SUB( &offset, &ppstimes.real, &ppstimes.clock);
	    TS_SUB( &delay, &ppstimes.clock, &last_fixtime.clock);
	    timespec_str( &delay, delay_str, sizeof(delay_str) );
	    /*@+compdef@*/

	    if ( 0> delay.tv_sec || 0 > delay.tv_nsec ) {
		thread_context->log_hook(thread_context, THREAD_RAW,
			    "PPS:%s system clock went backwards: %.20s\n",
			    thread_context->devicename,
			    delay_str);
		log1 = "system clock went backwards";
	    } else if ( ( 2 < delay.tv_sec) 
	      || ( 1 == delay.tv_sec && 100000000 > delay.tv_nsec ) ) {
                /* system clock could be slewing so allow 1.1 sec delay */
		thread_context->log_hook(thread_context, THREAD_RAW,
			    "PPS:%s no current GPS seconds: %.20s\n",
			    thread_context->devicename,
			    delay_str);
		log1 = "timestamp out of range";
	    } else {
		/*@-compdef@*/
		last_second_used = last_fixtime.real.tv_sec;
		if (thread_context->report_hook != NULL) 
		    log1 = thread_context->report_hook(thread_context, &ppstimes);
		else
		    log1 = "no report hook";
		if (thread_context->pps_hook != NULL)
		    thread_context->pps_hook(thread_context, &ppstimes);
		/*@ -unrecog  (splint has no pthread declarations as yet) @*/
		pthread_err = pthread_mutex_lock(&ppslast_mutex);
                if ( 0 != pthread_err ) {
		    char errbuf[BUFSIZ] = "unknown error";
		    (void)strerror_r(errno, errbuf, sizeof(errbuf));
		    thread_context->log_hook(thread_context, THREAD_ERROR,
			    "PPS:%s pthread_mutex_lock() : %s\n", 
			    thread_context->devicename, errbuf);
		}
		/*@ +unrecog @*/
		/*@-type@*/ /* splint is confused about struct timespec */
		thread_context->ppsout_last = ppstimes;
		/*@+type@*/
		thread_context->ppsout_count++;
		/*@ -unrecog (splint has no pthread declarations as yet) @*/
		pthread_err = pthread_mutex_unlock(&ppslast_mutex);
                if ( 0 != pthread_err ) {
		    char errbuf[BUFSIZ] = "unknown error";
		    (void)strerror_r(errno, errbuf, (int)sizeof(errbuf));
		    thread_context->log_hook(thread_context, THREAD_ERROR,
			    "PPS:%s pthread_mutex_unlock() : %s\n",
			    thread_context->devicename, errbuf);
		}
		/*@ +unrecog @*/
		/*@-type@*/ /* splint is confused about struct timespec */
		timespec_str( &ppstimes.clock, ts_str1, sizeof(ts_str1) );
		timespec_str( &ppstimes.real, ts_str2, sizeof(ts_str2) );
		thread_context->log_hook(thread_context, THREAD_INF,
		    "PPS:%s hooks called with %.20s clock: %s real: %s\n",
		    thread_context->devicename,
		    log1, ts_str1, ts_str2);
		/*@+type@*/
		/*@+compdef@*/
            }
	    /*@-compdef@*/
	    /*@-type@*/ /* splint is confused about struct timespec */
	    timespec_str( &clock_ts, ts_str1, sizeof(ts_str1) );
	    timespec_str( &offset, offset_str, sizeof(offset_str) );
	    thread_context->log_hook(thread_context, THREAD_PROG,
		    "PPS:%s edge %.20s @ %s offset %.20s\n",
		    thread_context->devicename,
		    log1, ts_str1, offset_str);
	    /*@+type@*/
	    /*@+compdef@*/
	} else {
	    thread_context->log_hook(thread_context, THREAD_RAW,
			"PPS:%s edge rejected %.100s", 
			thread_context->devicename, log);
	}
    }
#if defined(HAVE_SYS_TIMEPPS_H)
    if (thread_context->kernelpps_handle > 0) {
	thread_context->log_hook(thread_context, THREAD_PROG, 
            "PPS:%s descriptor cleaned up\n",
	    thread_context->devicename);
	(void)time_pps_destroy(thread_context->kernelpps_handle);
    }
#endif
    if (thread_context->wrap_hook != NULL)
	thread_context->wrap_hook(thread_context);
    thread_context->log_hook(thread_context, THREAD_PROG, 
		"PPS:%s gpsd_ppsmonitor exited.\n",
		thread_context->devicename);
    return NULL;
}
/*@+mustfreefresh +type +unrecog +branchstate@*/

/*
 * Entry points begin here.
 */

void pps_thread_activate(volatile struct pps_thread_t *pps_thread)
/* activate a thread to watch the device's PPS transitions */
{
    int retval;
    pthread_t pt;
#if defined(HAVE_SYS_TIMEPPS_H)
    /* some operations in init_kernel_pps() require root privs */
    (void)init_kernel_pps(pps_thread);
    if ( 0 <= pps_thread->kernelpps_handle ) {
	pps_thread->log_hook(pps_thread, THREAD_WARN,
		    "KPPS:%s kernel PPS will be used\n",
		    pps_thread->devicename);
    }
#endif
    /*@-compdef -nullpass@*/
    retval = pthread_create(&pt, NULL, gpsd_ppsmonitor, (void *)pps_thread);
    /*@+compdef +nullpass@*/
    pps_thread->log_hook(pps_thread, THREAD_PROG, "PPS:%s thread %s\n",
	        pps_thread->devicename,
		(retval==0) ? "launched" : "FAILED");
}

void pps_thread_deactivate(volatile struct pps_thread_t *pps_thread)
/* cleanly terminate PPS thread */
{
    /*@-nullstate -mustfreeonly@*/
    pps_thread->report_hook = NULL;
    pps_thread->pps_hook = NULL;
    /*@+nullstate +mustfreeonly@*/
}

void pps_thread_fixin(volatile struct pps_thread_t *pps_thread, 
			      volatile struct timedelta_t *fixin)
/* thread-safe update of last fix time - only way we pass data in */
{
    /*@ -unrecog  (splint has no pthread declarations as yet) @*/
    int pthread_err = pthread_mutex_lock(&ppslast_mutex);
    if ( 0 != pthread_err ) {
	char errbuf[BUFSIZ] = "unknown error";
	(void)strerror_r(errno, errbuf, (int)sizeof(errbuf));
	pps_thread->log_hook(pps_thread, THREAD_ERROR,
		"PPS:%s pthread_mutex_lock() : %s\n", 
	        pps_thread->devicename, errbuf);
    }
    /*@ +unrecog @*/
    pps_thread->fixin = *fixin;
    /*@ -unrecog (splint has no pthread declarations as yet) @*/
    pthread_err = pthread_mutex_unlock(&ppslast_mutex);
    if ( 0 != pthread_err ) {
	char errbuf[BUFSIZ] = "unknown error";
	(void)strerror_r(errno, errbuf, (int)sizeof(errbuf));
	pps_thread->log_hook(pps_thread, THREAD_ERROR,
		"PPS:%s pthread_mutex_unlock() : %s\n",
	        pps_thread->devicename, errbuf);
    }
    /*@ +unrecog @*/
}

int pps_thread_ppsout(volatile struct pps_thread_t *pps_thread,
		       volatile struct timedelta_t *td)
/* return the delta at the time of the last PPS - only way we pass data out */
{
    volatile int ret;
    /* pthread error return */
    int pthread_err; 

    /*@ -unrecog  (splint has no pthread declarations as yet) @*/
    pthread_err = pthread_mutex_lock(&ppslast_mutex);
    if ( 0 != pthread_err ) {
	char errbuf[BUFSIZ] = "unknown error";
	(void)strerror_r(errno, errbuf,(int) sizeof(errbuf));
	pps_thread->log_hook(pps_thread, THREAD_ERROR,
		"PPS:%s pthread_mutex_lock() : %s\n", 
	        pps_thread->devicename, errbuf);
    }
    /*@ +unrecog @*/
    *td = pps_thread->ppsout_last;
    ret = pps_thread->ppsout_count;
    /*@ -unrecog (splint has no pthread declarations as yet) @*/
    pthread_err = pthread_mutex_unlock(&ppslast_mutex);
    if ( 0 != pthread_err ) {
	char errbuf[BUFSIZ] = "unknown error";
	(void)strerror_r(errno, errbuf, (int)sizeof(errbuf));
	pps_thread->log_hook(pps_thread, THREAD_ERROR,
		"PPS:%s pthread_mutex_unlock() : %s\n",
	        pps_thread->devicename, errbuf);
    }
    /*@ +unrecog @*/

    return ret;
}

#endif /* PPS_ENABLE */

/* end */

