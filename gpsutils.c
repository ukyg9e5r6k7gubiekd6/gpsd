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

double timestamp(void) 
{
    struct timeval tv; 
    gettimeofday(&tv, NULL); 
    return(tv.tv_sec + tv.tv_usec/1e6);
}

int tzoffset(void)
{
    time_t now = time(NULL);
    struct tm tm;
    int res = 0;

    tzset();
#ifdef HAVE_TIMEZONE
    res = timezone;
#else
    res = localtime_r(now, &tm)->tm.tm_gmtoff;
#endif
#ifdef HAVE_DAYLIGHT
    if (daylight && localtime_r(&now, &tm)->tm_isdst)
	res += 3600;
#else
    if (localtime_r(&now, &tm)->tm_isdst)
	res += 3600;
#endif
    return res;
}

double iso8601_to_unix(char *isotime)
/* ISO8601 UTC to Unix UTC */
{
    char *dp = NULL;
    double usec;
    struct tm tm;

    dp = strptime(isotime, "%Y-%m-%dT%H:%M:%S", &tm);
    if (*dp == '.')
	usec = strtod(dp, NULL);
    else
	usec = 0;
    return mktime(&tm) + usec;
}

char *unix_to_iso8601(double fixtime, char *isotime)
/* Unix UTC time to ISO8601, no timezone adjustment */
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
    strcat(isotime, "Z");
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

#define Deg2Rad(n)	((n) * DEG_2_RAD)

static double CalcRad(double lat)
/* earth's radius of curvature in meters at specified latitude.*/
{
    const double a = 6378.137;
    const double e2 = 0.081082 * 0.081082;
    // the radius of curvature of an ellipsoidal Earth in the plane of a
    // meridian of latitude is given by
    //
    // R' = a * (1 - e^2) / (1 - e^2 * (sin(lat))^2)^(3/2)
    //
    // where a is the equatorial radius,
    // b is the polar radius, and
    // e is the eccentricity of the ellipsoid = sqrt(1 - b^2/a^2)
    //
    // a = 6378 km (3963 mi) Equatorial radius (surface to center distance)
    // b = 6356.752 km (3950 mi) Polar radius (surface to center distance)
    // e = 0.081082 Eccentricity
    double sc = sin(Deg2Rad(lat));
    double x = a * (1.0 - e2);
    double z = 1.0 - e2 * sc * sc;
    double y = pow(z, 1.5);
    double r = x / y;

    return r * 1000.0;	// Convert to meters
}

double earth_distance(double lat1, double lon1, double lat2, double lon2)
/* distance in meters between two points specified in degrees. */
{
    double x1 = CalcRad(lat1) * cos(Deg2Rad(lon1)) * sin(Deg2Rad(90-lat1));
    double x2 = CalcRad(lat2) * cos(Deg2Rad(lon2)) * sin(Deg2Rad(90-lat2));
    double y1 = CalcRad(lat1) * sin(Deg2Rad(lon1)) * sin(Deg2Rad(90-lat1));
    double y2 = CalcRad(lat2) * sin(Deg2Rad(lon2)) * sin(Deg2Rad(90-lat2));
    double z1 = CalcRad(lat1) * cos(Deg2Rad(90-lat1));
    double z2 = CalcRad(lat2) * cos(Deg2Rad(90-lat2));
    double a = (x1*x2 + y1*y2 + z1*z2)/pow(CalcRad((lat1+lat2)/2),2);
    // a should be in [1, -1] but can sometimes fall outside it by
    // a very small amount due to rounding errors in the preceding
    // calculations (this is prone to happen when the argument points
    // are very close together).  Thus we constrain it here.
    if (abs(a) > 1) 
	a = 1;
    else if (a < -1) 
	a = -1;
    return CalcRad((lat1+lat2) / 2) * acos(a);
}
