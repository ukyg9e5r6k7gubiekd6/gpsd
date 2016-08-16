/*
 * ntpshmwrite.c - put time information in SHM segment for ntpd
 *
 * This file is Copyright (c) 2010 by the GPSD project BSD terms apply:
 * see the file COPYING in the distribution root for details.
 */

/* sys/ipc.h needs _XOPEN_SOURCE, 500 means X/Open 1995 */
#define _XOPEN_SOURCE 500

#include <string.h>
#include <libgen.h>
#include <stdbool.h>
#include <math.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <unistd.h>

#include "ntpshm.h"
#include "compiler.h"

#define LEAP_NOWARNING  0x0     /* normal, no leap second warning */

void ntp_write(volatile struct shmTime *shmseg,
	       struct timedelta_t *td, int precision, int leap_notify)
/* put a received fix time into shared memory for NTP */
{
    struct tm tm;

    /*
     * insist that leap seconds only happen in june and december
     * GPS emits leap pending for 3 months prior to insertion
     * NTP expects leap pending for only 1 month prior to insertion
     * Per http://bugs.ntp.org/1090
     *
     * ITU-R TF.460-6, Section 2.1, says laep seconds can be primarily
     * in Jun/Dec but may be in March or September
     */
    (void)gmtime_r( &(td->real.tv_sec), &tm);
    if ( 5 != tm.tm_mon && 11 != tm.tm_mon ) {
        /* Not june, not December, no way */
        leap_notify = LEAP_NOWARNING;
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

    shmseg->valid = 0;
    shmseg->count++;
    /* We need a memory barrier here to prevent write reordering by
     * the compiler or CPU cache */
    memory_barrier();
    shmseg->clockTimeStampSec = (time_t)td->real.tv_sec;
    shmseg->clockTimeStampUSec = (int)(td->real.tv_nsec/1000);
    shmseg->clockTimeStampNSec = (unsigned)td->real.tv_nsec;
    shmseg->receiveTimeStampSec = (time_t)td->clock.tv_sec;
    shmseg->receiveTimeStampUSec = (int)(td->clock.tv_nsec/1000);
    shmseg->receiveTimeStampNSec = (unsigned)td->clock.tv_nsec;
    shmseg->leap = leap_notify;
    shmseg->precision = precision;
    memory_barrier();
    shmseg->count++;
    shmseg->valid = 1;
}

/* end */
