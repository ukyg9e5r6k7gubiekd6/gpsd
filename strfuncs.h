/*
 * strfuncs.h - string functions
 *
 * This software is distributed under a BSD-style license. See the
 * file "COPYING" in the toop-level directory of the distribution for details.
 */
#ifndef _GPSD_STRFUNCS_H_
#define _GPSD_STRFUNCS_H_

#include <string.h>

#define str_starts_with(str, prefix) \
    (strncmp((str), (prefix), strlen(prefix)) == 0)

#endif /* _GPSD_STRFUNCS_H_ */
