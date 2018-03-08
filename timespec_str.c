/*
 * This file is Copyright (c) 2010 by the GPSD project
 * SPDX-License-Identifier: BSD-2-clause
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
 * Probable worst case is 10 digits of seconds,
 * but standards do not provide hard limits to time_t
 * So 21 characters like this: "-2147483647.123456789"
 *
 * date --date='@2147483647' is: Mon Jan 18 19:14:07 PST 2038
 * date --date='@9999999999' is: Sat Nov 20 09:46:39 PST 2286
 *
 */
void timespec_str(const struct timespec *ts, char *buf, size_t buf_size)
{
    char sign = ' ';

    if ( (0 > ts->tv_nsec ) || ( 0 > ts->tv_sec ) ) {
	sign = '-';
    }
    (void) snprintf( buf, buf_size, "%c%lld.%09ld",
			  sign,
			  (long long)llabs(ts->tv_sec),
			  (long)labs(ts->tv_nsec));
}

/* end */
