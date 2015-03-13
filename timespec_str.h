/*
 * This file is Copyright (c) 2015 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 */

#ifndef GPSD_TIMESPEC_H
#define GPSD_TIMESPEC_H

#define TIMESPEC_LEN	22	/* required length of a timespec buffer */

extern void timespec_str(const struct timespec *, /*@out@*/char *, size_t);

#endif /* GPSD_TIMESPEC_H */

/* end */
