/*
 * This file is Copyright (c) 2015 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 */

#ifndef GPSD_NTPSHM_H
#define GPSD_NTPSHM_H

#include <stdbool.h>
#include <sys/time.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#define NTPD_BASE	0x4e545030	/* "NTP0" */

/* 
 * How to read and write fields in an NTP shared segment.
 * This definition of shmTime is from ntpd source ntpd/refclock_shm.c
 */

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

#endif

/*
 * These types are internal to GPSD
 */
enum segstat_t {
    OK, NO_SEGMENT, NOT_READY, BAD_MODE, CLASH};

struct shm_stat_t {
    struct timespec tvr, tvt;
    time_t now;
    int leap;
};

struct shmTime *shm_get(int, bool);
extern char *shm_name(const int);
enum segstat_t shm_query(struct shmTime *, struct shm_stat_t *);

/* end */
