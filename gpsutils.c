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

/*
 * The 'week' part of GPS dates are specified in weeks since 0000 on 06 
 * January 1980, with a rollover at 1024.  At time of writing the last 
 * rollover happened at 0000 22 August 1999.  Time-of-week is in seconds.
 *
 * This code copes with both conventional GPS weeks and the "extended"
 * 15-or-16-bit version with no wraparound that apperas in Zodiac
 * chips and is supposed to appear in the Geodetic Navigation
 * Information (0x29) packet of SiRF chips.  Some SiRF firmware versions
 * (notably 231) actually ship the wrapped 10-bit week, despite what
 * the protocol reference claims.
 */
#define GPS_EPOCH	315982800		/* GPS epoch in Unix time */
#define SECS_PER_WEEK	(60*60*24*7)		/* seconds per week */
#define GPS_ROLLOVER	(1024*SECS_PER_WEEK)	/* rollover period */

double gpstime_to_unix(int week, double tow)
{
    double fixtime;

    if (week >= GPS_ROLLOVER)
	fixtime = GPS_EPOCH + (week * SECS_PER_WEEK) + tow;
    else {
	time_t now, last_rollover;
	time(&now);
	last_rollover = GPS_EPOCH+((now-GPS_EPOCH)/GPS_ROLLOVER)*GPS_ROLLOVER;
	fixtime = last_rollover + (week * SECS_PER_WEEK) + tow;
    }
    return fixtime;
}

