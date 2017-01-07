/*
 * Simulate ANSI/POSIX conformance on platforms that don't have it
 *
 * This file is Copyright (c) 2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 */

#include <time.h>
#include <sys/time.h>

#include "compiler.h"

#ifndef HAVE_CLOCK_GETTIME

/*
 * Note that previous versions of this code made use of clock_get_time()
 * on OSX, as a way to get time of day with nanosecond resolution.  But
 * it turns out that clock_get_time() only has microsecond resolution,
 * in spite of the data format, and it's also substantially slower than
 * gettimeofday().  Thus, it makes no sense to do anything special for OSX.
 */

int clock_gettime(clockid_t clk_id UNUSED, struct timespec *ts)
{
    struct timeval tv;
    if (gettimeofday(&tv, NULL) < 0)
	return -1;
    ts->tv_sec = tv.tv_sec;
    ts->tv_nsec = tv.tv_usec * 1000;
    return 0;
}
#endif /* HAVE_CLOCK_GETTIME */

/* end */
