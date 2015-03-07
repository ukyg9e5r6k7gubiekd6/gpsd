/*
 * This file is Copyright (c) 2015 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 */

#ifndef PPSTHREAD_H
#define PPSTHREAD_H

#include <stdbool.h>
#include <time.h>
#include <sys/time.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#ifndef HAVE_TIMEDELTA

struct timedelta_t {
    struct timespec	real;
    struct timespec	clock;
};

#define HAVE_TIMEDELTA
#endif /* HAVE_TIMEDELTA */

struct pps_thread_t {
    timestamp_t fixin_real;
    struct timespec fixin_clock; /* system clock time when last fix received */
    struct timedelta_t ppsout_last;
    int ppsout_count;
};

#define PPS_THREAD_OK	0
#define PPS_LOCK_ERR	-1
#define PPS_UNLOCK_ERR	-2

extern int pps_thread_stash_fixtime(volatile struct pps_thread_t *, 
			      timestamp_t, struct timespec);
extern int pps_thread_lastpps(volatile struct pps_thread_t *, 
			      struct timedelta_t *);

#endif /* PPSTHREAD_H */

/* end */
