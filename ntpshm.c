/*
 * ntpshm.c - put time information in SHM segment for xntpd, or to chrony
 *
 * struct shmTime and getShmTime from file in the xntp distribution:
 *	sht.c - Testprogram for shared memory refclock
 *
 * Note that for easy debugging all logging from this file is prefixed
 * with PPS or NTP.
 *
 * This file is Copyright (c) 2010 by the GPSD project BSD terms apply:
 * see the file COPYING in the distribution root for details.
 */

#include <string.h>
#include <libgen.h>
#include <stdbool.h>
#include <math.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifndef S_SPLINT_S
#include <sys/wait.h>
#include <sys/socket.h>
#include <unistd.h>
#endif /* S_SPLINT_S */

#include "gpsd.h"

#ifdef NTPSHM_ENABLE
#include <sys/time.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#define NTPD_BASE	0x4e545030	/* "NTP0" */
#define SHM_UNIT	0	/* SHM driver unit number (0..3) */

#define PPS_MIN_FIXES	3	/* # fixes to wait for before shipping PPS */

/* the definition of shmTime is from ntpd source ntpd/refclock_shm.c */
struct shmTime
{
    int mode;	/* 0 - if valid set
		 *       use values,
		 *       clear valid
		 * 1 - if valid set
		 *       if count before and after read of values is equal,
		 *         use values
		 *       clear valid
		 */
    volatile int count;
    time_t clockTimeStampSec;
    int clockTimeStampUSec;
    time_t receiveTimeStampSec;
    int receiveTimeStampUSec;
    int leap;
    int precision;
    int nsamples;
    volatile int valid;
    unsigned        clockTimeStampNSec;     /* Unsigned ns timestamps */
    unsigned        receiveTimeStampNSec;   /* Unsigned ns timestamps */
    int             dummy[8];
};

/* Note: you can start gpsd as non-root, and have it work with ntpd.
 * However, it will then only use the ntpshm segments 2 and 3.
 *
 * Ntpd always runs as root (to be able to control the system clock).
 * Its logics for the creation of ntpshm segments are:
 *
 * Segments 0 and 1: permissions 0600, i.e. other programs can only
 *                   read and write as root.
 *
 * Segments 2 and 3: permissions 0666, i.e. other programs can read
 *                   and write as any user.  I.e.: if ntpd has been
 *                   configured to use these segments, any
 *                   unpriviliged user is allowed to provide data
 *                   for synchronisation.
 * * As gpsd can be started as both root and non-root, this behaviour is
 * mimicked by:
 *
 * Started as root: do as ntpd when attaching (creating) the segments.
 * (In contrast to ntpd, which only attaches (creates) configured
 * segments, gpsd creates all segments.)
 *
 * Started as non-root: only attach (create) segments 2 and 3 with
 * permissions 0666.  As the permissions are for any user, the creator
 * does not matter.
 *
 * For each GPS module gpsd controls, it will use the attached ntpshm
 * segments in pairs (for coarse clock and pps source, respectively)
 * starting from the first found segments.  I.e. started as root, one
 * GPS will deliver data on segments 0 and 1, and as non-root data
 * will be delivered on segments 2 and 3.
 *
 * to debug, try looking at the live segments this way
 *
 *  ipcs -m
 * 
 * results  should look like this:
 * ------ Shared Memory Segments --------
 *  key        shmid      owner      perms      bytes      nattch     status
 *  0x4e545030 0          root       700        96         2
 *  0x4e545031 32769      root       700        96         2
 *  0x4e545032 163842     root       666        96         1
 *  0x4e545033 196611     root       666        96         1
 *
 * For a bit more data try this:
 *  cat /proc/sysvipc/shm
 *
 * If gpsd can not open the segments be sure you are not running SELinux
 * or apparmor.
 *
 * if you see the shared segments (keys 1314148400 -- 1314148403), and
 * no gpsd or ntpd is running then try removing them like this:
 *
 * ipcrm  -M 0x4e545030
 * ipcrm  -M 0x4e545031
 * ipcrm  -M 0x4e545032
 * ipcrm  -M 0x4e545033
 */
static /*@null@*/ volatile struct shmTime *getShmTime(struct gps_context_t *context, int unit)
{
    int shmid;
    unsigned int perms;
    volatile struct shmTime *p;
    // set the SHM perms the way ntpd does
    if (unit < 2) {
	// we are root, be careful
	perms = 0600;
    } else {
	// we are not root, try to work anyway
	perms = 0666;
    }

    /*
     * Note: this call requires root under BSD, and possibly on
     * well-secured Linux systems.  This is why ntpshm_context_init() has to be
     * called before privilege-dropping.
     */
    shmid = shmget((key_t) (NTPD_BASE + unit),
		   sizeof(struct shmTime), (int)(IPC_CREAT | perms));
    if (shmid == -1) {
	gpsd_report(context->debug, LOG_ERROR,
		    "NTPD shmget(%ld, %zd, %o) fail: %s\n",
		    (long int)(NTPD_BASE + unit), sizeof(struct shmTime),
		    (int)perms, strerror(errno));
	return NULL;
    }
    p = (struct shmTime *)shmat(shmid, 0, 0);
    /*@ -mustfreefresh */
    if ((int)(long)p == -1) {
	gpsd_report(context->debug, LOG_ERROR,
		    "NTPD shmat failed: %s\n",
		    strerror(errno));
	return NULL;
    }
    gpsd_report(context->debug, LOG_PROG,
		"NTPD shmat(%d,0,0) succeeded, segment %d\n",
		shmid, unit);
    return p;
    /*@ +mustfreefresh */
}

void ntpshm_context_init(struct gps_context_t *context)
/* Attach all NTP SHM segments. Called once at startup, while still root. */
{
    int i;

    for (i = 0; i < NTPSHMSEGS; i++) {
	// Only grab the first two when running as root.
	if (2 <= i || 0 == getuid()) {
	    context->shmTime[i] = getShmTime(context, i);
	}
    }
    memset(context->shmTimeInuse, 0, sizeof(context->shmTimeInuse));
}

static int ntpshm_alloc(struct gps_context_t *context)
/* allocate NTP SHM segment.  return its segment number, or -1 */
{
    int i;

    for (i = 0; i < NTPSHMSEGS; i++)
	if (context->shmTime[i] != NULL && !context->shmTimeInuse[i]) {
	    context->shmTimeInuse[i] = true;

	    /*
	     * In case this segment gets sent to ntpd before an
	     * ephemeris is available, the LEAP_NOTINSYNC value will
	     * tell ntpd that this source is in a "clock alarm" state
	     * and should be ignored.  The goal is to prevent ntpd
	     * from declaring the GPS a falseticker before it gets
	     * all its marbles together.
	     */
	    memset((void *)context->shmTime[i], 0, sizeof(struct shmTime));
	    context->shmTime[i]->mode = 1;
	    context->shmTime[i]->leap = LEAP_NOTINSYNC;
	    context->shmTime[i]->precision = -1;	/* initially 0.5 sec */
	    context->shmTime[i]->nsamples = 3;	/* stages of median filter */

	    return i;
	}

    return -1;
}

static bool ntpshm_free(struct gps_context_t * context, int segment)
/* free NTP SHM segment */
{
    if (segment < 0 || segment >= NTPSHMSEGS)
	return false;

    context->shmTimeInuse[segment] = false;
    return true;
}

void ntpshm_session_init(struct gps_device_t *session)
{
#ifdef NTPSHM_ENABLE
    /* mark NTPD shared memory segments as unused */
    session->shmIndex = -1;
#endif /* NTPSHM_ENABLE */
#ifdef PPS_ENABLE
    session->shmIndexPPS = -1;
#endif	/* PPS_ENABLE */
}

int ntpshm_put(struct gps_device_t *session, int shmIndex, struct timedrift_t *td)
/* put a received fix time into shared memory for NTP */
{
    /*
     * shmTime is volatile to try to prevent C compiler from reordering
     * writes, or optimizing some 'dead code'.  but CPU cache may still
     * write out of order if memory_barrier() is a no-op (our implementation 
     * isn't portable).
     */
    volatile struct shmTime *shmTime = NULL;
    /* Any NMEA will be about -1 or -2. Garmin GPS-18/USB is around -6 or -7. */
    int precision = -1; /* default precision */

    if (shmIndex < 0 || (shmTime = session->context->shmTime[shmIndex]) == NULL) {
	gpsd_report(session->context->debug, LOG_RAW, "NTPD missing shm\n");
	return 0;
    }

#ifdef PPS_ENABLE
    /* ntpd sets -20 for PPS refclocks, thus -20 precision */
    if (shmIndex == session->shmIndexPPS)
	precision = -20;
#endif	/* PPS_ENABLE */

    /* we use the shmTime mode 1 protocol
     *
     * ntpd does this:
     *
     * reads valid.
     * IFF valid is 1
     *    reads count
     *    reads values
     *    reads count
     *    IFF count unchanged
     *        use values
     *    clear valid
     *
     */

    shmTime->valid = 0;
    shmTime->count++;
    /* We need a memory barrier here to prevent write reordering by
     * the compiler or CPU cache */
    memory_barrier();
    /*@-type@*/ /* splint is confused about struct timespec */
    shmTime->clockTimeStampSec = (time_t)td->real.tv_sec;
    shmTime->clockTimeStampUSec = (int)(td->real.tv_nsec/1000);
    shmTime->clockTimeStampNSec = (unsigned)td->real.tv_nsec;
    shmTime->receiveTimeStampSec = (time_t)td->clock.tv_sec;
    shmTime->receiveTimeStampUSec = (int)(td->clock.tv_nsec/1000);
    shmTime->receiveTimeStampNSec = (unsigned)td->clock.tv_nsec;
    /*@+type@*/
    shmTime->leap = session->context->leap_notify;
    shmTime->precision = precision;
    memory_barrier();
    shmTime->count++;
    shmTime->valid = 1;

    /*@-type@*/ /* splint is confused about struct timespec */
    gpsd_report(session->context->debug, LOG_RAW,
		"NTP ntpshm_put(%d) %lu.%09lu @ %lu.%09lu\n",
                shmIndex,
		(unsigned long)td->real.tv_sec,
		(unsigned long)td->real.tv_nsec,
		(unsigned long)td->clock.tv_sec,
		(unsigned long)td->clock.tv_nsec);
    /*@+type@*/

    return 1;
}

#ifdef PPS_ENABLE
#define SOCK_MAGIC 0x534f434b
struct sock_sample {
    struct timeval tv;
    double offset;
    int pulse;
    int leap;
    // cppcheck-suppress unusedStructMember
    int _pad;
    int magic;      /* must be SOCK_MAGIC */
};

/*@-mustfreefresh@*/
static void init_hook(struct gps_device_t *session)
/* for chrony SOCK interface, which allows nSec timekeeping */
{
    /* open the chrony socket */
    char chrony_path[GPS_PATH_MAX];

    session->chronyfd = -1;
    if ( 0 == getuid() ) {
	/* this case will fire on command-line devices;
	 * they're opened before priv-dropping.  Matters because
         * only root can use /var/run.
	 */
	(void)snprintf(chrony_path, sizeof (chrony_path),
		"/var/run/chrony.%s.sock", basename(session->gpsdata.dev.path));
    } else {
	(void)snprintf(chrony_path, sizeof (chrony_path),
		"/tmp/chrony.%s.sock", 	basename(session->gpsdata.dev.path));
    }

    if (access(chrony_path, F_OK) != 0) {
	gpsd_report(session->context->debug, LOG_PROG,
		    "PPS chrony socket %s doesn't exist\n", chrony_path);
    } else {
	session->chronyfd = netlib_localsocket(chrony_path, SOCK_DGRAM);
	if (session->chronyfd < 0)
	    gpsd_report(session->context->debug, LOG_PROG,
		"PPS connect chrony socket failed: %s, error: %d, errno: %d/%s\n",
		chrony_path, session->chronyfd, errno, strerror(errno));
	else
	    gpsd_report(session->context->debug, LOG_RAW,
			"PPS using chrony socket: %s\n", chrony_path);
    }
}
/*@+mustfreefresh@*/


/* td is the real time and clock time of the edge */
/* offset is actual_ts - clock_ts */
static void chrony_send(struct gps_device_t *session, struct timedrift_t *td)
{
    struct sock_sample sample;

    /* chrony expects tv-sec since Jan 1970 */
    sample.pulse = 0;
    sample.leap = session->context->leap_notify;
    sample.magic = SOCK_MAGIC;
    /*@-type@*//* splint is confused about struct timespec */
    TSTOTV(&sample.tv, &td->clock);
    /*@-compdef@*/
    sample.offset = timespec_diff_ns(td->real, td->clock) / 1e9;
    /*@+compdef@*/
#ifdef __COVERITY__
    sample._pad = 0;
#endif /* __COVERITY__ */
    /*@+type@*/

    /*@-type@*/ /* splint is confused about struct timespec */
    gpsd_report(session->context->debug, LOG_RAW,
		"PPS chrony_send %lu.%09lu @ %lu.%09lu Offset: %0.9f\n",
		(unsigned long)td->real.tv_sec,
		(unsigned long)td->real.tv_nsec,
		(unsigned long)td->clock.tv_sec,
		(unsigned long)td->clock.tv_nsec,
                sample.offset);
    /*@+type@*/
    (void)send(session->chronyfd, &sample, sizeof (sample), 0);
}

static void wrap_hook(struct gps_device_t *session)
{
    if (session->chronyfd != -1)
	(void)close(session->chronyfd);
}

static /*@observer@*/ char *report_hook(struct gps_device_t *session,
					struct timedrift_t *td)
/* ship the time of a PPS event to ntpd and/or chrony */
{
    char *log1;

    if (!session->ship_to_ntpd)
	return "skipped ship_to_ntp=0";

    /*
     * Only listen to PPS after several consecutive fixes,
     * otherwise time may be inaccurate.  (We know this is
     * required on some Garmins in binary mode; safest to do it
     * for all case we're talking to a Garmin in text mode, and
     * out of general safety-first conservatism.)
     *
     * Not sure yet how to handle u-blox UBX_MODE_TMONLY
     */
    if (session->fixcnt <= PPS_MIN_FIXES)
	return "no fix";

    log1 = "accepted";
    if ( 0 <= session->chronyfd ) {
	log1 = "accepted chrony sock";
	chrony_send(session, td);
    }
    (void)ntpshm_put(session, session->shmIndexPPS, td);

    return log1;
}
#endif	/* PPS_ENABLE */

void ntpshm_link_deactivate(struct gps_device_t *session)
/* release ntpshm storage for a session */
{
    (void)ntpshm_free(session->context, session->shmIndex);
#if defined(PPS_ENABLE)
    if (session->shmIndexPPS != -1) {
	pps_thread_deactivate(session);
	(void)ntpshm_free(session->context, session->shmIndexPPS);
    }
#endif	/* PPS_ENABLE */
}

void ntpshm_link_activate(struct gps_device_t *session)
/* set up ntpshm storage for a session */
{
    /* allocate a shared-memory segment for "NMEA" time data */
    session->shmIndex = ntpshm_alloc(session->context);

    if (0 > session->shmIndex) {
	gpsd_report(session->context->debug, LOG_INF, 
                    "NTPD ntpshm_alloc() failed\n");
#if defined(PPS_ENABLE)
    } else if (session->sourcetype == source_usb || session->sourcetype == source_rs232) {
	/* We also have the 1pps capability, allocate a shared-memory segment
	 * for the 1pps time data and launch a thread to capture the 1pps
	 * transitions
	 */
	if ((session->shmIndexPPS = ntpshm_alloc(session->context)) < 0) {
	    gpsd_report(session->context->debug, LOG_INF, 
                        "NTPD ntpshm_alloc(1) failed\n");
	} else {
	    init_hook(session);
	    session->thread_report_hook = report_hook;
	    session->thread_wrap_hook = wrap_hook;
	    pps_thread_activate(session);
	}
#endif /* PPS_ENABLE */
    }
}

#endif /* NTPSHM_ENABLE */
/* end */
