/* gpsutils.c -- code shared between low-level and high-level interfaces */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdarg.h>
#define __USE_XOPEN
#include <time.h>

#include "gpsd.h"

double iso8661_to_unix(char *isotime)
/* ISO8661 UTC to Unix local time */
{
    char *dp = NULL;
    struct tm tm;
    double usec, res;
    time_t now;

    dp = strptime(isotime, "%Y-%m-%dT%H:%M:%S", &tm);
    if (*dp == '.')
	usec = strtod(dp, NULL);
    else
	usec = 0;
#ifdef HAVE_TIMEZONE
    res = mktime(&tm) - timezone + usec;
#else
    res = mktime(&tm) - tm.tm_gmtoff + usec;
#endif
    now = time(NULL);
#ifdef HAVE_DAYLIGHT
    if (daylight && localtime_r(&now, &tm)->tm_isdst)
	res -= 3600;
#else
    if (localtime_r(&now, &tm)->tm_isdst)
	res -= 3600;
#endif
    return res;
}

char *unix_to_iso8661(double fixtime, char *isotime)
/* Unix local time to ISO8661, no timezone adjustment */
{
    struct tm when;
    double integral, fractional;
    time_t intfixtime;
    int slen;

    fractional = modf(fixtime, &integral);
    intfixtime = (time_t)integral;
    localtime_r(&intfixtime, &when);

    strftime(isotime, 28, "%Y-%m-%dT%H:%M:%S", &when);
    slen = strlen(isotime);
    sprintf(isotime + slen, "%.1f", fractional);
    memcpy(isotime+slen, isotime+slen+1, strlen(isotime+slen+1));
    return isotime;
}
