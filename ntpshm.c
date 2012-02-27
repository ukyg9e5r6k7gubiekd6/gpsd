/* 
 * ntpshm.c - put time information in SHM segment for xntpd
 * struct shmTime and getShmTime from file in the xntp distribution:
 *	sht.c - Testprogram for shared memory refclock
 *
 * This file is Copyright (c) 2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
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

#ifdef NTPSHM_ENABLE
#include <sys/time.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#define PPS_MAX_OFFSET	100000	/* microseconds the PPS can 'pull' */
#define PUT_MAX_OFFSET	1000000	/* microseconds for lost lock */

#define NTPD_BASE	0x4e545030	/* "NTP0" */
#define SHM_UNIT	0	/* SHM driver unit number (0..3) */

struct shmTime
{
    int mode;			/* 0 - if valid set
				 *       use values, 
				 *       clear valid
				 * 1 - if valid set 
				 *       if count before and after read of values is equal,
				 *         use values 
				 *       clear valid
				 */
    int count;
    time_t clockTimeStampSec;
    int clockTimeStampUSec;
    time_t receiveTimeStampSec;
    int receiveTimeStampUSec;
    int leap;
    int precision;
    int nsamples;
    int valid;
    int pad[10];
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
 *
 * As gpsd can be started as both root and non-root, this behaviour is
 * mimiced by:
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
 *  ipcs -m
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
static /*@null@*/ volatile struct shmTime *getShmTime(int unit)
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
     * well-secured Linux systems.  This is why ntpshm_init() has to be
     * called before privilege-dropping.
     */
    shmid = shmget((key_t) (NTPD_BASE + unit),
		   sizeof(struct shmTime), (int)(IPC_CREAT | perms));
    if (shmid == -1) {
	gpsd_report(LOG_ERROR, "NTPD shmget(%ld, %zd, %o) fail: %s\n",
		    (long int)(NTPD_BASE + unit), sizeof(struct shmTime),
		    (int)perms, strerror(errno));
	return NULL;
    } 
    p = (struct shmTime *)shmat(shmid, 0, 0);
    /*@ -mustfreefresh */
    if ((int)(long)p == -1) {
	gpsd_report(LOG_ERROR, "NTPD shmat failed: %s\n",
		    strerror(errno));
	return NULL;
    }
    gpsd_report(LOG_PROG, "NTPD shmat(%d,0,0) succeeded, segment %d\n",
		shmid, unit);
    return p;
    /*@ +mustfreefresh */
}

void ntpshm_init(struct gps_context_t *context, bool enablepps)
/* Attach all NTP SHM segments. Called once at startup, while still root. */
{
    int i;

    for (i = 0; i < NTPSHMSEGS; i++) {
	// Only grab the first two when running as root.
	if (2 <= i || 0 == getuid()) {
	    context->shmTime[i] = getShmTime(i);
	}
    }
    memset(context->shmTimeInuse, 0, sizeof(context->shmTimeInuse));
# ifdef PPS_ENABLE
    context->shmTimePPS = enablepps;
# endif	/* PPS_ENABLE */
    context->enable_ntpshm = true;
}

static int ntpshm_alloc(struct gps_context_t *context)
/* allocate NTP SHM segment.  return its segment number, or -1 */
{
    int i;

    for (i = 0; i < NTPSHMSEGS; i++)
	if (context->shmTime[i] != NULL && !context->shmTimeInuse[i]) {
	    context->shmTimeInuse[i] = true;

	    memset((void *)context->shmTime[i], 0, sizeof(struct shmTime));
	    context->shmTime[i]->mode = 1;
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

int ntpshm_put(struct gps_device_t *session, double fixtime, double fudge)
/* put a received fix time into shared memory for NTP */
{
    /* shmTime is volatile to try to prevent C compiler from reordering
     * writes, or optimizing some 'dead code'.  but CPU cache may still 
     *write out of order since we do not use memory barriers, yet */
    volatile struct shmTime *shmTime = NULL;
    struct timeval tv;
    double seconds, microseconds;

    // gpsd_report(LOG_PROG, "NTP: doing ntpshm_put(,%g, %g)\n", fixtime, fudge);
    if (session->shmindex < 0 ||
	(shmTime = session->context->shmTime[session->shmindex]) == NULL) {
	gpsd_report(LOG_RAW, "NTPD missing shm\n");
	return 0;
    }

    (void)gettimeofday(&tv, NULL);
    fixtime += fudge;
    microseconds = 1000000.0 * modf(fixtime, &seconds);
    if (shmTime->clockTimeStampSec == (time_t) seconds) {
	gpsd_report(LOG_RAW, "NTPD ntpshm_put: skipping duplicate second\n");
	return 0;
    }

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
    /* FIXME need a memory barrier here to prevent write reordering by
     * the compiler or CPU cache */
    shmTime->clockTimeStampSec = (time_t) seconds;
    shmTime->clockTimeStampUSec = (int)microseconds;
    shmTime->receiveTimeStampSec = (time_t) tv.tv_sec;
    shmTime->receiveTimeStampUSec = (int)tv.tv_usec;
    /* setting the precision here does not seem to help anything, too
     * hard to calculate properly anyway.  Let ntpd figure it out.
     * Any NMEA will be about -1 or -2. 
     * Garmin GPS-18/USB is around -6 or -7.
     */
    /* FIXME need a memory barrier here to prevent write reordering by
     * the compiler or CPU cache */
    shmTime->count++;
    shmTime->valid = 1;

    gpsd_report(LOG_RAW,
		"NTPD ntpshm_put: Clock: %lu.%06lu @ %lu.%06lu, fudge: %0.3f\n",
		(unsigned long)seconds, (unsigned long)microseconds,
		(unsigned long)tv.tv_sec, (unsigned long)tv.tv_usec, fudge);

    return 1;
}

#ifdef PPS_ENABLE
/*
 * Possible pins for PPS: DCD, CTS, RTS, RI. Pinouts:
 *
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
 *  5     7    SG       Signal ground 
 */
#include "pps_pin.h"

/*@unused@*//* splint is confused here */
/* put NTP shared memory info based on received PPS pulse
 *
 * good news is that kernel PPS gives us nSec resolution
 * bad news is that ntpshm only has uSec resolution
 */
static int ntpshm_pps(struct gps_device_t *session, struct timeval *tv)
{
    volatile struct shmTime *shmTime = NULL, *shmTimeP = NULL;
    time_t seconds;
    /* FIX-ME, microseconds needs to be set for 5Hz PPS */
    int microseconds = 0;
    int precision;
    double offset;
    long l_offset;

    if (0 > session->shmindex || 0 > session->shmTimeP ||
	(shmTime = session->context->shmTime[session->shmindex]) == NULL ||
	(shmTimeP = session->context->shmTime[session->shmTimeP]) == NULL)
	return 0;

    /* PPS has no seconds attached to it.
     * check to see if we have a fresh timestamp from the
     * GPS serial input then use that */

    /* FIX-ME, does not handle 5Hz yet */

#ifdef S_SPLINT_S		/* avoids an internal error in splint 3.1.1 */
    l_offset = 0;
#else
    l_offset = tv->tv_sec - shmTime->receiveTimeStampSec;
#endif
    /*@ -ignorequals @*/
    l_offset *= 1000000;
    l_offset += tv->tv_usec - shmTime->receiveTimeStampUSec;
    if (0 > l_offset || 1000000 < l_offset) {
	gpsd_report(LOG_RAW, "PPS ntpshm_pps: no current GPS seconds: %ld\n",
		    (long)l_offset);
	return -1;
    }

    /*@+relaxtypes@*/
    seconds = shmTime->clockTimeStampSec + 1;
    offset = fabs((tv->tv_sec - seconds)
		  + ((double)(tv->tv_usec - 0) / 1000000.0));
    /*@-relaxtypes@*/


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
    shmTimeP->valid = 0;
    shmTimeP->count++;
    shmTimeP->clockTimeStampSec = seconds;
    shmTimeP->clockTimeStampUSec = (int)microseconds;
    shmTimeP->receiveTimeStampSec = (time_t) tv->tv_sec;
    shmTimeP->receiveTimeStampUSec = (int)tv->tv_usec;
    /* precision is a placebo, ntpd does not really use it
     * real world accuracy is around 16uS, thus -16 precision */
    shmTimeP->precision = -16;
    shmTimeP->count++;
    shmTimeP->valid = 1;

    /* this is more an offset jitter/dispersion than precision, 
     * but still useful for debug */
    precision = offset != 0 ? (int)(ceil(log(offset) / M_LN2)) : -20;
    gpsd_report(LOG_RAW, "PPS ntpshm_pps %lu.%03lu @ %lu.%06lu, preci %d\n",
		(unsigned long)seconds, (unsigned long)microseconds / 1000,
		(unsigned long)tv->tv_sec, (unsigned long)tv->tv_usec,
		precision);
    return 1;
}

/*
 * Warning: This is a potential portability problem. 
 * It's needed so that TIOCMIWAIT will be defined and the serial-PPS 
 * code will work, but it's not a SuS/POSIX standard header.  We're
 * going to include it unconditionally here because we expect both
 * Linux and BSD to have it and we want compilation to break with
 * an audible snapping sound.
 */
#include <sys/ioctl.h>

#ifndef S_SPLINT_S
#include <pthread.h>		/* pacifies OpenBSD's compiler */
#endif
#if defined(HAVE_SYS_TIMEPPS_H)
    /* use RFC 2783 PPS API */
    /* this needs linux >= 2.6.34 and
     * CONFIG_PPS=y
     * CONFIG_PPS_DEBUG=y  [optional to kernel log pulses]
     * CONFIG_PPS_CLIENT_LDISC=y
     */
    /* get timepps.h from the pps-tools package, or from here:
     * http://www.mail-archive.com/debian-glibc@lists.debian.org/msg43125.html
     * RFC2783 says timepps.h is in sys
     */
    #include <sys/timepps.h>
    #include <glob.h>
#endif
/* and for chrony */
#include <sys/un.h>

/* normalize a timespec */
#define TS_NORM(ts)  \
    do { \
	if ( 1000000000 <= (ts)->tv_nsec ) { \
	    (ts)->tv_nsec -= 1000000000; \
	    (ts)->tv_sec++; \
	} else if ( 0 > (ts)->tv_nsec ) { \
	    (ts)->tv_nsec += 1000000000; \
	    (ts)->tv_sec--; \
	} \
    } while (0)

/* normalize a timeval */
#define TV_NORM(tv)  \
    do { \
	if ( 1000000 <= (tv)->tv_usec ) { \
	    (tv)->tv_usec -= 1000000; \
	    (tv)->tv_sec++; \
	} else if ( 0 > (tv)->tv_usec ) { \
	    (tv)->tv_usec += 1000000; \
	    (tv)->tv_sec--; \
	} \
    } while (0)

/* convert timespec to timeval, with rounding */
#define TSTOTV(tv, ts) \
    do { \
	(tv)->tv_sec = (ts)->tv_sec; \
	(tv)->tv_usec = ((ts)->tv_nsec + 500)/1000; \
        TV_NORM( tv ); \
    } while (0)

/* convert timeval to timespec */
#define TVTOTS(ts, tv) \
    do { \
	(ts)->tv_sec = (tv)->tv_sec; \
	(ts)->tv_nsec = (tv)->tv_usec*1000; \
        TS_NORM( ts ); \
    } while (0)

#endif

#if defined(PPS_ENABLE) && defined(TIOCMIWAIT)
#if defined(HAVE_SYS_TIMEPPS_H)
/* return handle for kernel pps, or -1 */
static int init_kernel_pps(struct gps_device_t *session) {
    int ldisc = 18;   /* the PPS line discipline */
    pps_params_t pp;
    glob_t globbuf;
    int i;
    char pps_num = 0;     /* /dev/pps[pps_num] is our device */
    char path[GPS_PATH_MAX] = "";

    session->kernelpps_handle = -1;
    if ( !isatty(session->gpsdata.gps_fd) ) {
	gpsd_report(LOG_INF, "KPPS gps_fd not a tty\n");
    	return -1;
    }
    /* Attach the line PPS discipline, so no need to ldattach */
    /* This activates the magic /dev/pps0 device */
    /* Note: this ioctl() requires root */
    if ( 0 > ioctl(session->gpsdata.gps_fd, TIOCSETD, &ldisc)) {
	gpsd_report(LOG_INF, "KPPS cannot set PPS line discipline: %s\n"
	    , strerror(errno));
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
	gpsd_report(LOG_INF, "KPPS checking %s, %s\n"
	    , globbuf.gl_pathv[i], path);
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
	gpsd_report(LOG_INF, "KPPS device not found.\n");
    	return -1;
    }
    /* contruct the magic device path */
    (void)snprintf(path, sizeof(path), "/dev/pps%c", pps_num);

    /* root privs are required for this device open */
    int ret = open(path, O_RDWR);
    if ( 0 > ret ) {
	gpsd_report(LOG_INF, "KPPS cannot open %s: %s\n"
	    , path, strerror(errno));
    	return -1;
    }
    /* root privs are not required past this point */ 

    if ( 0 > time_pps_create(ret, &session->kernelpps_handle )) {
	gpsd_report(LOG_INF, "KPPS time_pps_create(%d) failed: %s\n"
	    , ret, strerror(errno));
    	return -1;
    } else {
    	/* have kernel PPS handle */
        int caps;
	/* get features  supported */
        if ( 0 > time_pps_getcap(session->kernelpps_handle, &caps)) {
	    gpsd_report(LOG_ERROR, "KPPS time_pps_getcap() failed\n");
        } else {
	    gpsd_report(LOG_INF, "KPPS caps %0x\n", caps);
        }

        /* linux 2.6.34 can not PPS_ECHOASSERT | PPS_ECHOCLEAR */
        memset( (void *)&pp, 0, sizeof(pps_params_t));
        pp.mode = PPS_CAPTUREBOTH;

        if ( 0 > time_pps_setparams(session->kernelpps_handle, &pp)) {
	    gpsd_report(LOG_ERROR, 
		"KPPS time_pps_setparams() failed: %s\n", strerror(errno));
	    time_pps_destroy(session->kernelpps_handle);
	    return -1;
        }
    }
    return 0;
}
#endif /* defined(HAVE_SYS_TIMEPPS_H) */

/*@-mustfreefresh -type@ -unrecog*/
static /*@null@*/ void *gpsd_ppsmonitor(void *arg)
{
    struct gps_device_t *session = (struct gps_device_t *)arg;
    int cycle, duration, state = 0, laststate = -1, unchanged = 0;
    struct timeval  tv;
    struct timespec ts;
    struct timeval pulse[2] = { {0, 0}, {0, 0} };
#if defined(HAVE_SYS_TIMEPPS_H)
    int kpps_edge = 0;       /* 0 = clear edge, 1 = assert edge */
    int cycle_kpps, duration_kpps;
    struct timespec pulse_kpps[2] = { {0, 0}, {0, 0} };
    struct timespec tv_kpps;
    pps_info_t pi;
#endif
/* for chrony SOCK interface, which allows nSec timekeeping */
#define SOCK_MAGIC 0x534f434b
    struct sock_sample {
	struct timeval tv;
	double offset;
	int pulse;
	int leap;
	int _pad;	/* unused */
	int magic;      /* must be SOCK_MAGIC */
    } sample;
    /* chrony must be started first as chrony insists on creating the socket */
    /* open the chrony socket */
    int chronyfd = -1;
    char chrony_path[PATH_MAX];

    gpsd_report(LOG_PROG, "PPS Create Thread gpsd_ppsmonitor\n");

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
	gpsd_report(LOG_PROG, "PPS chrony socket %s doesn't exist\n", chrony_path);
    } else {
	chronyfd = netlib_localsocket(chrony_path, SOCK_DGRAM);
	if (chronyfd < 0)
	    gpsd_report(LOG_PROG, "PPS can not connect chrony socket: %s\n",
		chrony_path);
	else
	    gpsd_report(LOG_RAW, "PPS using chrony socket: %s\n", chrony_path);
    }

    /* end chrony */

    /* wait for the device to go active - makes this safe to call early */
    while (session->gpsdata.gps_fd == -1) {
	/* should probably remove this once code is verified */
	gpsd_report(LOG_PROG, "PPS thread awaiting device activation\n");
	(void)sleep(1);
    }

#if defined(HAVE_SYS_TIMEPPS_H)
    /* some operations in init_kernel_pps() require root privs */
    (void)init_kernel_pps( session );
    if ( 0 <= session->kernelpps_handle ) {
	gpsd_report(LOG_WARN, "KPPS kernel PPS will be used\n");
    }
    memset( (void *)&pi, 0, sizeof(pps_info_t));
#endif

    /* root privileges are not required after this point */

    /* wait for status change on the device's carrier-detect line */
    while (ioctl(session->gpsdata.gps_fd, TIOCMIWAIT, PPS_LINE_TIOC) == 0) {
	int ok = 0;
	char *log = NULL;

/*@-noeffect@*/
#ifdef HAVE_CLOCK_GETTIME
	/* using  clock_gettime() here, that is nSec, 
	 * not uSec like gettimeofday */
	if ( 0 > clock_gettime(CLOCK_REALTIME, &ts) ) {
	    /* uh, oh, can not get time! */
	    gpsd_report(LOG_ERROR, "PPS clock_gettime() failed\n");
	    break;
	}
	TSTOTV( &tv, &ts);
#else
	if ( 0 > gettimeofday(&tv, NULL) ) {
	    /* uh, oh, can not get time! */
	    gpsd_report(LOG_ERROR, "PPS gettimeofday() failed\n");
	    break;
	}
	TVTOTS( &ts, &tv);
#endif
/*@+noeffect@*/

#if defined(HAVE_SYS_TIMEPPS_H)
        if ( 0 <= session->kernelpps_handle ) {
	    struct timespec kernelpps_tv;
	    /* on a quad core 2.4GHz Xeon this removes about 20uS of 
	     * latency, and about +/-5uS of jitter over the other method */
            memset( (void *)&kernelpps_tv, 0, sizeof(kernelpps_tv));
	    if ( 0 > time_pps_fetch(session->kernelpps_handle, PPS_TSFMT_TSPEC
	        , &pi, &kernelpps_tv)) {
		gpsd_report(LOG_ERROR, "KPPS kernel PPS failed\n");
	    } else {
		// find the last edge
	    	if ( pi.assert_timestamp.tv_sec > pi.clear_timestamp.tv_sec ) {
		    kpps_edge = 1;
		    tv_kpps = pi.assert_timestamp;
	    	} else if ( pi.assert_timestamp.tv_sec < pi.clear_timestamp.tv_sec ) {
		    kpps_edge = 0;
		    tv_kpps = pi.clear_timestamp;
		} else if ( pi.assert_timestamp.tv_nsec > pi.clear_timestamp.tv_nsec ) {
		    kpps_edge = 1;
		    tv_kpps = pi.assert_timestamp;
		} else {
		    kpps_edge = 0;
		    tv_kpps = pi.clear_timestamp;
		}
		gpsd_report(LOG_PROG, "KPPS assert %ld.%09ld, sequence: %ld - "
		       "clear  %ld.%09ld, sequence: %ld\n",
		       pi.assert_timestamp.tv_sec,
		       pi.assert_timestamp.tv_nsec,
		       pi.assert_sequence,
		       pi.clear_timestamp.tv_sec,
		       pi.clear_timestamp.tv_nsec, 
		       pi.clear_sequence);
		gpsd_report(LOG_PROG, "KPPS data: using %s\n",
		       kpps_edge ? "assert" : "clear");

#define timediff_kpps(x, y)	(int)((x.tv_sec-y.tv_sec)*1000000+((x.tv_nsec-y.tv_nsec)/1000))
	        cycle_kpps = timediff_kpps(tv_kpps, pulse_kpps[kpps_edge]);
	        duration_kpps = timediff_kpps(tv_kpps, pulse_kpps[(int)(kpps_edge == 0)]);
		if ( 3000000 < duration_kpps ) {
		    // invisible pulse
		    duration_kpps = 0;
		}
#undef timediff_kpps
	        gpsd_report(LOG_INF, 
		    "KPPS cycle: %7d, duration: %7d @ %lu.%09lu\n",
		    cycle_kpps, duration_kpps,
		    (unsigned long)tv_kpps.tv_sec, 
		    (unsigned long)tv_kpps.tv_nsec);
		pulse_kpps[kpps_edge] = tv_kpps;
	    }
	}
#endif

	ok = 0;
	log = NULL;

	/*@ +ignoresigns */
	if (ioctl(session->gpsdata.gps_fd, TIOCMGET, &state) != 0)
	    break;
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
		gpsd_report(LOG_RAW,
			    "PPS pps-detect (%s) on %s invisible pulse\n",
			    PPS_LINE_NAME, session->gpsdata.dev.path);
	    } else if (++unchanged == 10) {
		unchanged = 1;
		gpsd_report(LOG_WARN,
			    "PPS TIOCMIWAIT returns unchanged state, ppsmonitor sleeps 10\n");
		(void)sleep(10);
	    }
	} else {
	    gpsd_report(LOG_RAW, "PPS pps-detect (%s) on %s changed to %d\n",
			PPS_LINE_NAME, session->gpsdata.dev.path, state);
	    laststate = state;
	    unchanged = 0;
	}
	pulse[state] = tv;
	if (unchanged) {
	    // strange, try again
	    continue;
	}
	gpsd_report(LOG_INF, "PPS cycle: %7d, duration: %7d @ %lu.%06lu\n",
		    cycle, duration,
		    (unsigned long)tv.tv_sec, (unsigned long)tv.tv_usec);

	/*@ +boolint @*/
	if (session->ship_to_ntpd || session->context->pps_hook != NULL) {
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
		    ok = 1;
		    log = "5Hz PPS pulse\n";
		}
	    } else if (999000 > cycle) {
		log = "Too long for 5Hz, too short for 1Hz\n";
	    } else if (1001000 > cycle) {
		/* looks like PPS pulse or square wave */
		if (0 == duration) {
		    ok = 1;
		    log = "invisible pulse\n";
		} else if (499000 > duration) {
		    /* end of the short "half" of the cycle */
		    /* aka the trailing edge */
		    log = "1Hz trailing edge\n";
		} else if (501000 > duration) {
		    /* looks like 1.0 Hz square wave, ignore trailing edge */
		    if (state == 1) {
			ok = 1;
			log = "square\n";
		    }
		} else {
		    /* end of the long "half" of the cycle */
		    /* aka the leading edge */
		    ok = 1;
		    log = "1Hz leading edge\n";
		}
	    } else if (1999000 > cycle) {
		log = "Too long for 1Hz, too short for 2Hz\n";
	    } else if (2001000 > cycle) {
		/* looks like 0.5 Hz square wave */
		if (999000 > duration) {
		    log = "0.5 Hz square too short duration\n";
		} else if (1001000 > duration) {
		    ok = 1;
		    log = "0.5 Hz square wave\n";
		} else {
		    log = "0.5 Hz square too long duration\n";
		}
	    } else {
		log = "Too long for 0.5Hz\n";
	    }
	} else {
	    /* not a good fix, but a test for an otherwise good PPS
	     * would go here */
	    log = "no fix.\n";
	}
	/*@ -boolint @*/
	if (0 != ok) {
	    gpsd_report(LOG_RAW, "PPS edge accepted %.100s", log);
	    /* chrony expects tv-sec since Jan 1970 */
	    /* FIXME!! offset is double of the error from local time */
	    sample.pulse = 0;
	    sample.leap = 0;
	    sample.magic = SOCK_MAGIC;
#if defined(HAVE_SYS_TIMEPPS_H)
            if ( 0 <= session->kernelpps_handle) {
		/* pick the right edge */
		if ( kpps_edge ) {
		    ts = pi.assert_timestamp; /* structure copy */
		} else {
		    ts = pi.clear_timestamp;  /* structure copy */
		}
		TSTOTV( &sample.tv, &ts);
	    } else
#endif
	    {
		sample.tv = tv; 	/* structure copy */
	    } 
	    /* FIXME!! this is wrong if signal is 5Hz or 10Hz instead of PPS */
	    /* careful, Unix time to nSec is more precision than a double */
	    sample.offset = 1 + session->last_fixtime - ts.tv_sec;
	    sample.offset -= ts.tv_nsec / 1e9;
/* was: defined(ONCORE_ENABLE) && defined(BINARY_ENABLE) */
#ifdef __UNUSED__
	    /*@-noeffect@*/
	    if (session->device_type == &oncore_binary) {
		int pulse_delay_ns = session->driver.oncore.pps_offset_ns;
	        sample.offset += (double)pulse_delay_ns / 1000000000;
	        ts.tv_nsec    -= pulse_delay_ns;
	        TS_NORM( &ts );
	    }
	    /*@+noeffect@*/
#endif

	    if ( 0 <= chronyfd ) {
		(void)send(chronyfd, &sample, sizeof (sample), 0);
		gpsd_report(LOG_RAW, "PPS edge accepted chrony sock %lu.%06lu offset %.9f\n",
			    (unsigned long)sample.tv.tv_sec,
			    (unsigned long)sample.tv.tv_usec,
			    sample.offset);
	    }
	    TSTOTV( &tv, &ts );
	    if (session->ship_to_ntpd)
		(void)ntpshm_pps(session, &tv);
	    if (session->context->pps_hook != NULL)
		session->context->pps_hook(session, &tv);
	} else {
	    gpsd_report(LOG_RAW, "PPS edge rejected %.100s", log);
	}

    }
    gpsd_report(LOG_PROG, "PPS gpsd_ppsmonitor exited???\n");
    return NULL;
}
/*@+mustfreefresh +type +unrecog@*/
#endif /* PPS_ENABLE */

void ntpd_link_deactivate(struct gps_device_t *session)
/* release ntpshm storage for a session */
{
    (void)ntpshm_free(session->context, session->shmindex);
    session->shmindex = -1;
# ifdef PPS_ENABLE
    (void)ntpshm_free(session->context, session->shmTimeP);
    session->shmTimeP = -1;
# endif	/* PPS_ENABLE */
}

void ntpd_link_activate(struct gps_device_t *session)
/* set up ntpshm storage for a session - may fail if not called as root */
{
    /* If we are talking to ntpd, allocate a shared-memory segment for "NMEA" time data */
    if (session->context->enable_ntpshm)
	session->shmindex = ntpshm_alloc(session->context);

    if (0 > session->shmindex) {
	gpsd_report(LOG_INF, "NTPD ntpshm_alloc() failed\n");
#if defined(PPS_ENABLE) && defined(TIOCMIWAIT)
    } else if (session->context->shmTimePPS) {
	/* We also have the 1pps capability, allocate a shared-memory segment
	 * for the 1pps time data and launch a thread to capture the 1pps
	 * transitions
	 *
	 * Ideally we'd like to launch the device's PPS thread right after.
	 * this call succeeds.  But there's a problem - a 2.6 kernel bug.
	 * The thread watching a TIOCMIWAITed serial will hang if the baud
	 * rate on the line is changed.  Thus we can't start the thread until
	 * the hunt loop has done its thing.
	 */
	if ((session->shmTimeP = ntpshm_alloc(session->context)) < 0) {
	    gpsd_report(LOG_INF, "NTPD ntpshm_alloc(1) failed\n");
	}
#endif /* defined(PPS_ENABLE) && defined(TIOCMIWAIT) */
    }
}

#if defined(PPS_ENABLE) && defined(TIOCMIWAIT)
void pps_thread_activate(struct gps_device_t *session)
/* activate a thread to watch the device's PPS transitions */
{
    pthread_t pt;
    if (session->shmTimeP >= 0) {
	gpsd_report(LOG_PROG, "PPS thread launched\n");
	/*@-compdef -nullpass@*/
	(void)pthread_create(&pt, NULL, gpsd_ppsmonitor, (void *)session);
        /*@+compdef +nullpass@*/
    }
}
#else
void pps_thread_activate(struct gps_device_t *session UNUSED)
{
    /* nothing to call */
}
#endif /* defined(PPS_ENABLE) && defined(TIOCMIWAIT) */

#if defined(HAVE_SYS_TIMEPPS_H)
void pps_thread_deactivate(struct gps_device_t *session)
/* cleanly terminate device's PPS thread */
{
    if (session->kernelpps_handle > 0) {
	gpsd_report(LOG_PROG, "PPS descriptor cleaned up\n");
	time_pps_destroy(session->kernelpps_handle);
    }
}
#else
void pps_thread_deactivate(struct gps_device_t *session UNUSED)
{
    /* nothing to call */
}
#endif

#endif /* NTPSHM_ENABLE */
