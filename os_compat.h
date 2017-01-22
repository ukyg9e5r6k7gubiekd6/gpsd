/*
 * This file is Copyright (c) 2017 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 *
 * This is the header for os_compat.c, which contains functions dealing with
 * compatibility issues across OSes.
 */
#ifndef _GPSD_OS_COMPAT_H_
#define _GPSD_OS_COMPAT_H_

/* Determine which of these functions we need */
#include "gpsd_config.h"

# ifdef __cplusplus
extern "C" {
# endif

#ifndef HAVE_CLOCK_GETTIME

#include <time.h>

#ifndef CLOCKID_T_DEFINED
typedef int clockid_t;
#define CLOCKID_T_DEFINED
#endif /* !CLOCKID_T_DEFINED */

/*
 * OS X 10.5 and later use _STRUCT_TIMESPEC (like other OSes)
 * 10.4 uses _TIMESPEC
 * 10.3 and earlier use _TIMESPEC_DECLARED
 */
#if !defined(_STRUCT_TIMESPEC) && \
    !defined(_TIMESPEC) && \
    !defined(_TIMESPEC_DECLARED) && \
    !defined(__timespec_defined)
#define _STRUCT_TIMESPEC
struct timespec {
    time_t  tv_sec;
    long    tv_nsec;
};
#endif /* !_STRUCT_TIMESPEC ... */

/* OS X does not have clock_gettime */
#define CLOCK_REALTIME 0
int clock_gettime(clockid_t, struct timespec *);

#endif /* !HAVE_CLOCK_GETTIME */

#ifndef HAVE_STRLCAT

#include <string.h>
size_t strlcat(char *dst, const char *src, size_t size);

#endif /* !HAVE_STRLCAT */

#ifndef HAVE_STRLCPY

#include <string.h>
size_t strlcpy(char *dst, const char *src, size_t size);

#endif /* !HAVE_STRLCPY */

# ifdef __cplusplus
}
# endif

#endif /* _GPSD_OS_COMPAT_H_ */
