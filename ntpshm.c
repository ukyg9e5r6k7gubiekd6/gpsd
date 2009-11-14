/* $Id$ */
/* 
 * ntpshm.c - put time information in SHM segment for xntpd
 * struct shmTime and getShmTime from file in the xntp distribution:
 *	sht.c - Testprogram for shared memory refclock
 */

#include <sys/types.h>
#ifndef S_SPLINT_S
#include <unistd.h>
#endif /* S_SPLINT_S */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <errno.h>

#include "gpsd_config.h"
#include "gpsd.h"
#ifdef NTPSHM_ENABLE

#if defined(HAVE_SYS_TIME_H)
#include <sys/time.h>
#endif

#include <sys/ipc.h>
#include <sys/shm.h>

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

static /*@null@*/ struct shmTime *getShmTime(int unit)
{
    int shmid=shmget ((key_t)(NTPD_BASE+unit),
		      sizeof (struct shmTime), IPC_CREAT|0644);
    if (shmid == -1) {
	gpsd_report(LOG_ERROR, "NTPD shmget fail: %s\n", strerror(errno));
	return NULL;
    } else {
	struct shmTime *p=(struct shmTime *)shmat (shmid, 0, 0);
	/*@ -mustfreefresh */
	if ((int)(long)p == -1) {
	    gpsd_report(LOG_ERROR, "NTPD shmat failed: %s\n", strerror(errno));
	    return NULL;
	}
	gpsd_report(LOG_PROG, "NTPD shmat(%d,0,0) succeeded\n", shmid);
	return p;
	/*@ +mustfreefresh */
    }
}

void ntpshm_init(struct gps_context_t *context, bool enablepps)
/* attach all NTP SHM segments.  called once at startup, while still root */
{
    int i;

    for (i = 0; i < NTPSHMSEGS; i++)
	context->shmTime[i] = (i >= 2 || getuid() == 0) ? getShmTime(i) : NULL;
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


int ntpshm_put(struct gps_device_t *session, double fixtime)
/* put a received fix time into shared memory for NTP */
{
    struct shmTime *shmTime = NULL;
    struct timeval tv;
    double seconds,microseconds;

    if (session->shmindex < 0 ||
	(shmTime = session->context->shmTime[session->shmindex]) == NULL)
	return 0;

    (void)gettimeofday(&tv,NULL);
    microseconds = 1000000.0 * modf(fixtime,&seconds);

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

    gpsd_report(LOG_RAW, "NTPD ntpshm_put: Clock: %lu.%06lu @ %lu.%06lu\n"
	, (unsigned long)seconds , (unsigned long)microseconds
	, (unsigned long)tv.tv_sec, (unsigned long)tv.tv_usec);

    return 1;
}

#ifdef PPS_ENABLE
/* put NTP shared memory info based on received PPS pulse */

int ntpshm_pps(struct gps_device_t *session, struct timeval *tv)
{
    struct shmTime *shmTime = NULL, *shmTimeP = NULL;
    time_t seconds;
    /* FIXME, microseconds needs to be set for 5Hz PPS */
    unsigned long microseconds = 0;
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

    seconds = shmTime->clockTimeStampSec + 1;
    offset = fabs(((tv->tv_sec - seconds) + (tv->tv_usec - 0) / 1000000.0));


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
    shmTimeP->clockTimeStampUSec = microseconds;
    shmTimeP->receiveTimeStampSec = (time_t)tv->tv_sec;
    shmTimeP->receiveTimeStampUSec = (int)tv->tv_usec;
    /* this is more an offset jitter/dispersion than precision, 
     * but still useful */
    shmTimeP->precision = offset != 0 ? (int)(ceil(log(offset) / M_LN2)) : -20;
    shmTimeP->count++;
    shmTimeP->valid = 1;

    gpsd_report(LOG_RAW
        , "PPS ntpshm_pps %lu.%03lu @ %lu.%06lu, precision %d\n"
	, (unsigned long)seconds, (unsigned long)microseconds
	, (unsigned long)tv->tv_sec, (unsigned long)tv->tv_usec
	, shmTimeP->precision);
    return 1;
}
#endif /* PPS_ENABLE */
#endif /* NTPSHM_ENABLE */
