/*
 * This file is Copyright (c) 2015 by the GPSD project
 * SPDX-License-Identifier: BSD-2-clause
 */

#ifndef GPSD_TIMESPEC_H
#define GPSD_TIMESPEC_H

#include <stdbool.h>       /* for bool */

/* normalize a timespec
 *
 * three cases to note
 * if tv_sec is positve, then tv_nsec must be positive
 * if tv_sec is negative, then tv_nsec must be negative
 * if tv_sec is zero, then tv_nsec may be positive or negative.
 *
 * this only handles the case where two normalized timespecs
 * are added or subracted.  (e.g. only a one needs to be borrowed/carried
 *
 * NOTE: this normalization is not the same as ntpd uses
 */
#define NS_IN_SEC	1000000000LL     /* nanoseconds in a second */
#define US_IN_SEC	1000000LL        /* microseconds in a second */
#define MS_IN_SEC	1000LL           /* milliseconds in a second */

/* return the difference between timespecs in nanoseconds
 * int may be too small, 32 bit long is too small, floats are too imprecise,
 * doubles are not quite precise enough 
 * MUST be long long to maintain precision on 32 bit code */
#define timespec_diff_ns(x, y) \
    (long long)((((x).tv_sec-(y).tv_sec)*NS_IN_SEC)+(x).tv_nsec-(y).tv_nsec)

static inline void TS_NORM( struct timespec *ts)
{
    if ( (  1 <= ts->tv_sec ) ||
         ( (0 == ts->tv_sec ) && (0 <= ts->tv_nsec ) ) ) {
        /* result is positive */
	if ( NS_IN_SEC <= ts->tv_nsec ) {
            /* borrow from tv_sec */
	    ts->tv_nsec -= NS_IN_SEC;
	    ts->tv_sec++;
	} else if ( 0 > (ts)->tv_nsec ) {
            /* carry to tv_sec */
	    ts->tv_nsec += NS_IN_SEC;
	    ts->tv_sec--;
	}
    }  else {
        /* result is negative */
	if ( -NS_IN_SEC >= ts->tv_nsec ) {
            /* carry to tv_sec */
	    ts->tv_nsec += NS_IN_SEC;
	    ts->tv_sec--;
	} else if ( 0 < ts->tv_nsec ) {
            /* borrow from tv_sec */
	    ts->tv_nsec -= NS_IN_SEC;
	    ts->tv_sec++;
	}
    }
}

/* normalize a timeval */
#define TV_NORM(tv)  \
    do { \
	if ( US_IN_SEC <= (tv)->tv_usec ) { \
	    (tv)->tv_usec -= US_IN_SEC; \
	    (tv)->tv_sec++; \
	} else if ( 0 > (tv)->tv_usec ) { \
	    (tv)->tv_usec += US_IN_SEC; \
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

/* subtract two timespec */
#define TS_SUB(r, ts1, ts2) \
    do { \
	(r)->tv_sec = (ts1)->tv_sec - (ts2)->tv_sec; \
	(r)->tv_nsec = (ts1)->tv_nsec - (ts2)->tv_nsec; \
        TS_NORM( r ); \
    } while (0)

/* convert a timespec to a double.
 * if tv_sec > 2, then inevitable loss of precision in tv_nsec
 * so best to NEVER use TSTONS() 
 * WARNING replacing 1e9 with NS_IN_SEC causes loss of precision */
#define TSTONS(ts) ((double)((ts)->tv_sec + ((ts)->tv_nsec / 1e9)))

#define TIMESPEC_LEN	22	/* required length of a timespec buffer */

extern void timespec_str(const struct timespec *, char *, size_t);

bool nanowait(int, int);

#endif /* GPSD_TIMESPEC_H */

/* end */
