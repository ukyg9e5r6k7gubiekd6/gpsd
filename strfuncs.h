/*
 * strfuncs.h - string functions
 *
 * This software is distributed under a BSD-style license. See the
 * file "COPYING" in the toop-level directory of the distribution for details.
 */
#ifndef _GPSD_STRFUNCS_H_
#define _GPSD_STRFUNCS_H_

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define str_starts_with(str, prefix) \
    (strncmp((str), (prefix), strlen(prefix)) == 0)

#define str_appendf(str, alloc_size, format, ...) \
    ((void) snprintf((str) + strlen(str), (alloc_size) - strlen(str), (format), ##__VA_ARGS__))
#define str_vappendf(str, alloc_size, format, ap) \
    ((void) vsnprintf((str) + strlen(str), (alloc_size) - strlen(str), (format), (ap)))

#define str_rstrip_char(str, ch) \
    do { \
        if ((str)[strlen(str) - 1] == ch) \
            (str)[strlen(str) - 1] = '\0'; \
    } while (0)

#endif /* _GPSD_STRFUNCS_H_ */
