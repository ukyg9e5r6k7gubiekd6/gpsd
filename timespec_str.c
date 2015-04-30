/*
 * This file is Copyright (c) 2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 */

/*
 * We also need to set the value high enough to signal inclusion of
 * newer features (like clock_gettime).  See the POSIX spec for more info:
 * http://pubs.opengroup.org/onlinepubs/9699919799/functions/V2_chap02.html#tag_15_02_01_02
*/
#define _XOPEN_SOURCE 600

#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <ctype.h>

#include "timespec.h"

/* Convert a normalized timespec to a nice string
 * put in it *buf, buf should be at least 22 bytes
 *
 * the returned buffer will look like, shortest case:
 *    sign character ' ' or '-'
 *    one digit of seconds
 *    decmal point '.'
 *    9 digits of nanoSec
 *
 * So 12 chars, like this: "-0.123456789"
 *
 * Absolute worst case is 10 digits of seconds.
 * So 21 digits like this: "-2147483647.123456789"
 *
*/
void timespec_str(const struct timespec *ts, char *buf, size_t buf_size)
{
    char sign = ' ';

    if ( (0 > ts->tv_nsec ) || ( 0 > ts->tv_sec ) ) {
	sign = '-';
    }
    (void) snprintf( buf, buf_size, "%c%ld.%09ld",
			  sign,
			  (long)labs(ts->tv_sec),
			  (long)labs(ts->tv_nsec));
}

/* end */
