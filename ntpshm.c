/* 
 * ntpshm.c - put time information in SHM segment for xntpd
 * struct shmTime and getShmTime from file in the xntp distribution:
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

int ntpshm_init(struct gps_context_t *context)
{
    if ((context->shmTime = getShmTime(SHM_UNIT)) == NULL)
	return 0;

    memset((void *)context->shmTime,0,sizeof(struct shmTime));
    context->shmTime->mode = 1;

    return 1;
}

int ntpshm_put(struct gps_context_t *context, double fixtime)
{
    struct timeval tv;
    double seconds,microseconds;

    if (context->shmTime == NULL)
	return 0;

    gettimeofday(&tv,NULL);
    microseconds = 1000000.0 * modf(fixtime,&seconds);

    context->shmTime->count++;
    context->shmTime->clockTimeStampSec = seconds;
    context->shmTime->clockTimeStampUSec = microseconds;
    context->shmTime->receiveTimeStampSec = tv.tv_sec;
    context->shmTime->receiveTimeStampUSec = tv.tv_usec;
    context->shmTime->precision = -1;	/* this needs work */
    context->shmTime->count++;
    context->shmTime->valid = 1;

    return 1;
}

#endif /* defined(SHM_H) && defined(IPC_H) */
