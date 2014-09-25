/*
 * Code swiped from a manual page
 */

#include <features.h>

#ifndef __GLIBC__

#include <time.h>
#include <stdlib.h>

time_t timegm(struct tm *tm)
{
    time_t ret;
    char *tz;

    tz = getenv("TZ");
    if (tz)
	tz = strdup(tz);
    setenv("TZ", "", 1);
    tzset();
    ret = mktime(tm);
    if (tz) {
	setenv("TZ", tz, 1);
	free(tz);
    } else
	unsetenv("TZ");
    tzset();
    return ret;
}
#endif

/* end */
