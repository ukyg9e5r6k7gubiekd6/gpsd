/* $Id$ */
/* 
 * ntpshm.c - put time information in SHM segment for xntpd
 * struct shmTime and getShmTime from file in the xntp distribution:
 *	sht.c - Testprogram for shared memory refclock
 */

#include <stdlib.h>
#include "gpsd_config.h"
#include <sys/types.h>
#ifndef S_SPLINT_S
#include <unistd.h>
#endif /* S_SPLINT_S */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <errno.h>

#include "gpsd.h"
#ifdef NTPSHM_ENABLE

#if defined(HAVE_SYS_TIME_H)
#include <sys/time.h>
#endif

#ifdef HAVE_SYS_IPC_H
 #include <sys/ipc.h>
#endif /* HAVE_SYS_IPC_H */
#ifdef HAVE_SYS_SHM_H
 #include <sys/shm.h>
#endif /* HAVE_SYS_SHM_H */

#define PPS_MAX_OFFSET	100000		/* microseconds the PPS can 'pull' */
#define PUT_MAX_OFFSET	1000000		/* microseconds for lost lock */

#define NTPD_BASE	0x4e545030	/* "NTP0" */
#define SHM_UNIT	0		/* SHM driver unit number (0..3) */

struct shmTime {
    int    mode; /* 0 - if valid set
		  *       use values, 
		  *       clear valid
		  * 1 - if valid set 
		  *       if count before and after read of values is equal,
		  *         use values 
		  *       clear valid
		  */
    int    count;
    time_t clockTimeStampSec;
    int    clockTimeStampUSec;
    time_t receiveTimeStampSec;
    int    receiveTimeStampUSec;
    int    leap;
    int    precision;
    int    nsamples;
    int    valid;
    int    pad[10];
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
 * to debug, try this:
 *  cat /proc/sysvipc/shm
 *
 * if you see the shared segments (keys 1314148400 -- 1314148403), and
 * no gpsd or ntpd is running then try removing them like this:
 *
 * ipcrm  -M 0x4e545030
 * ipcrm  -M 0x4e545031
 * ipcrm  -M 0x4e545032
 * ipcrm  -M 0x4e545033
 */
static /*@null@*/ struct shmTime *getShmTime(int unit)
{
    int shmid;
    unsigned int perms;
    // set the SHM perms the way ntpd does
    if ( unit < 2 ) {
    	// we are root, be careful
	perms = 0600;
    } else {
        // we are not root, try to work anyway
	perms = 0666;
    }

    shmid=shmget ((key_t)(NTPD_BASE+unit),
		  sizeof (struct shmTime), (int)(IPC_CREAT|perms));
    if (shmid == -1) {
	gpsd_report(LOG_ERROR, "NTPD shmget(%ld, %ld, %o) fail: %s\n",
	   (long int)(NTPD_BASE+unit),sizeof (struct shmTime), (int)perms, strerror(errno));
	return NULL;
    } else {
	struct shmTime *p=(struct shmTime *)shmat (shmid, 0, 0);
	/*@ -mustfreefresh */
	if ((int)(long)p == -1) {
	    gpsd_report(LOG_ERROR, "NTPD shmat failed: %s\n", strerror(errno));
	    return NULL;
	}
	gpsd_report(LOG_PROG, "NTPD shmat(%d,0,0) succeeded, segment %d\n", 
	   shmid, unit);
	return p;
	/*@ +mustfreefresh */
    }
}

void ntpshm_init(struct gps_context_t *context, bool enablepps)
/* attach all NTP SHM segments.  
 * called once at startup, while still root,  although root not required */
{
    int i;

    for (i = 0; i < NTPSHMSEGS; i++) {
	// Only grab the first two when running as root.
	if ( 2 <= i || 0 == getuid()) {
	    context->shmTime[i] = getShmTime(i);
	}
    }
    memset(context->shmTimeInuse,0,sizeof(context->shmTimeInuse));
# ifdef PPS_ENABLE
    context->shmTimePPS = enablepps;
# endif /* PPS_ENABLE */
    context->enable_ntpshm = true;
}

int ntpshm_alloc(struct gps_context_t *context)
/* allocate NTP SHM segment.  return its segment number, or -1 */
{
    int i;

    for (i = 0; i < NTPSHMSEGS; i++)
	if (context->shmTime[i] != NULL && !context->shmTimeInuse[i]) {
	    context->shmTimeInuse[i] = true;

	    memset((void *)context->shmTime[i],0,sizeof(struct shmTime));
	    context->shmTime[i]->mode = 1;
	    context->shmTime[i]->precision = -1; /* initially 0.5 sec */
	    context->shmTime[i]->nsamples = 3;	/* stages of median filter */

	    return i;
	}

    return -1;
}

bool ntpshm_free(struct gps_context_t *context, int segment)
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
    struct shmTime *shmTime = NULL;
    struct timeval tv;
    double seconds,microseconds;

    if (session->shmindex < 0 ||
	(shmTime = session->context->shmTime[session->shmindex]) == NULL)
	return 0;

    (void)gettimeofday(&tv,NULL);
    fixtime += fudge;
    microseconds = 1000000.0 * modf(fixtime,&seconds);
    if ( shmTime->clockTimeStampSec == (time_t)seconds) {
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
    shmTime->clockTimeStampSec = (time_t)seconds;
    shmTime->clockTimeStampUSec = (int)microseconds;
    shmTime->receiveTimeStampSec = (time_t)tv.tv_sec;
    shmTime->receiveTimeStampUSec = (int)tv.tv_usec;
    /* setting the precision here does not seem to help anything, too
       hard to calculate properly anyway.  Let ntpd figure it out.
       Any NMEA will be about -1 or -2. 
       Garmin GPS-18/USB is around -6 or -7.
    */
    shmTime->count++;
    shmTime->valid = 1;

    gpsd_report(LOG_RAW, 
        "NTPD ntpshm_put: Clock: %lu.%06lu @ %lu.%06lu, fudge: %0.3f\n"
	, (unsigned long)seconds , (unsigned long)microseconds
	, (unsigned long)tv.tv_sec, (unsigned long)tv.tv_usec, fudge);

    return 1;
}

#ifdef PPS_ENABLE
/* put NTP shared memory info based on received PPS pulse */

int ntpshm_pps(struct gps_device_t *session, struct timeval *tv)
{
    struct shmTime *shmTime = NULL, *shmTimeP = NULL;
    time_t seconds;
    /* FIXME, microseconds needs to be set for 5Hz PPS */
    int microseconds = 0;
    int precision;
    double offset;
    long l_offset;

    if ( 0 > session->shmindex ||  0 > session->shmTimeP ||
	(shmTime = session->context->shmTime[session->shmindex]) == NULL ||
	(shmTimeP = session->context->shmTime[session->shmTimeP]) == NULL)
	return 0;

    /* PPS has no seconds attached to it.
     * check to see if we have a fresh timestamp from the
     * GPS serial input then use that */

    /* FIXME, does not handle 5Hz yet */

#ifdef S_SPLINT_S      /* avoids an internal error in splint 3.1.1 */
    l_offset = 0;
#else
    l_offset = tv->tv_sec - shmTime->receiveTimeStampSec;
#endif
    /*@ -ignorequals @*/
    l_offset *= 1000000;
    l_offset += tv->tv_usec - shmTime->receiveTimeStampUSec;
    if ( 0 > l_offset || 1000000 < l_offset ) {
	gpsd_report(LOG_RAW, "PPS ntpshm_pps: no current GPS seconds: %ld\n"
	    , (long)l_offset);
	return -1;
    }

    /*@+relaxtypes@*/
    seconds = shmTime->clockTimeStampSec + 1;
    offset = fabs((tv->tv_sec - seconds)
    	+((double)(tv->tv_usec - 0)/1000000.0));
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
    shmTimeP->receiveTimeStampSec = (time_t)tv->tv_sec;
    shmTimeP->receiveTimeStampUSec = (int)tv->tv_usec;
    /* precision is a placebo, ntpd does not really use it
     * real world accuracty is around 16uS, thus -16 precision */
    shmTimeP->precision = -16;
    shmTimeP->count++;
    shmTimeP->valid = 1;

    /* this is more an offset jitter/dispersion than precision, 
     * but still useful for debug */
    precision = offset != 0 ? (int)(ceil(log(offset) / M_LN2)) : -20;
    gpsd_report(LOG_RAW
        , "PPS ntpshm_pps %lu.%03lu @ %lu.%06lu, preci %d\n"
	, (unsigned long)seconds, (unsigned long)microseconds/1000
	, (unsigned long)tv->tv_sec, (unsigned long)tv->tv_usec
	, precision);
    return 1;
}
#endif /* PPS_ENABLE */
#endif /* NTPSHM_ENABLE */
