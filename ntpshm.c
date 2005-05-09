/* 
 * ntpshm.c - put time information in SHM segment for xntpd
 * struct shmTime and getShmTime from file in the xntp distribution:
 *	sht.c - Testprogram for shared memory refclock
 */

#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include "gpsd.h"
#ifdef NTPSHM_ENABLE

#if defined(HAVE_SYS_TIME_H)
#include <sys/time.h>
#endif

#include <sys/ipc.h>
#include <sys/shm.h>

#define PPS_MAX_OFFSET	100000		/* microseconds the PPS can 'pull' */
#define PUT_MAX_OFFSET	400000		/* microseconds for lost lock */

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

static struct shmTime *getShmTime(int unit)
{
    int shmid=shmget (NTPD_BASE+unit, sizeof (struct shmTime), IPC_CREAT|0700);
    if (shmid == -1) {
	gpsd_report(1, "shmget failed\n");
	return NULL;
    } else {
	struct shmTime *p=(struct shmTime *)shmat (shmid, 0, 0);
	if ((int)(long)p == -1) {
	    gpsd_report(1, "shmat failed\n");
	    p=0;
	}
	return p;
    }
}

/* initialize NTP SHM segment.  called once at startup, while still root */

int ntpshm_init(struct gps_context_t *context)
{
    if ((context->shmTime = getShmTime(SHM_UNIT)) == NULL)
	return 0;

    memset((void *)context->shmTime,0,sizeof(struct shmTime));
    context->shmTime->mode = 1;
    context->shmTime->precision = -1;	/* initially 0.5 sec */
    context->shmTime->nsamples = 3;	/* stages of median filter */

    return 1;
}

/* put a received fix time into shared memory for NTP */

int ntpshm_put(struct gps_context_t *context, double fixtime)
{
    struct shmTime *shmTime;
    struct timeval tv;
    double seconds,microseconds;

    if ((shmTime = context->shmTime) == NULL)
	return 0;

    gettimeofday(&tv,NULL);
    microseconds = 1000000.0 * modf(fixtime,&seconds);

    if (shmTime->precision < -1) {	/* PPS lock? */
	/* check if recent update received and within locking range */

	if ((tv.tv_sec - shmTime->receiveTimeStampSec) < 3 &&
	    abs((seconds-tv.tv_sec)*1000000 + microseconds-tv.tv_usec)
		< PUT_MAX_OFFSET)
	    return 1;			/* don't disturb */

	shmTime->precision = -1;	/* lost lock */
	gpsd_report(2, "ntpshm_put: lost PPS lock\n");
    }

    shmTime->count++;
    shmTime->clockTimeStampSec = seconds;
    shmTime->clockTimeStampUSec = microseconds;
    shmTime->receiveTimeStampSec = tv.tv_sec;
    shmTime->receiveTimeStampUSec = tv.tv_usec;
    shmTime->count++;
    shmTime->valid = 1;

    return 1;
}

/* put NTP shared memory info based on received PPS pulse */

int ntpshm_pps(struct gps_context_t *context,struct timeval *tv)
{
    struct shmTime *shmTime;
    time_t seconds;
    double offset;

    if ((shmTime = context->shmTime) == NULL)
	return 0;

    if (tv->tv_usec < PPS_MAX_OFFSET) {
	seconds = tv->tv_sec;
	offset = tv->tv_usec / 1000000.0;
    } else {
	if (tv->tv_usec > (1000000 - PPS_MAX_OFFSET)) {
	    seconds = tv->tv_sec + 1;
	    offset = 1 - (tv->tv_usec / 1000000.0);
	} else {
	    shmTime->precision = -1;	/* lost lock */
	    gpsd_report(2, "ntpshm_pps: lost PPS lock\n");
	    return -1;
	}
    }

    shmTime->count++;
    shmTime->clockTimeStampSec = seconds;
    shmTime->clockTimeStampUSec = 0;
    shmTime->receiveTimeStampSec = tv->tv_sec;
    shmTime->receiveTimeStampUSec = tv->tv_usec;
    shmTime->precision = offset? ceil(log(offset) / M_LN2) : -20;
    shmTime->count++;
    shmTime->valid = 1;

    gpsd_report(5, "ntpshm_pps: precision %d\n",shmTime->precision);
    return 1;
}

#endif /* NTPSHM_ENABLE */
