/* gpsutils.c -- code shared between low-level and high-level interfaces
 *
 * This file is Copyright (c) 2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 */
#include <sys/types.h>
#include <stdio.h>
#ifndef S_SPLINT_S
#include <unistd.h>
#endif /* S_SPLINT_S */
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

#include "gpsd.h"

#ifdef USE_QT
#include <QDateTime>
#include <QStringList>
#endif

#define MONTHSPERYEAR	12	/* months per calendar year */

void gps_clear_fix( /*@out@*/ struct gps_fix_t *fixp)
/* stuff a fix structure with recognizable out-of-band values */
{
    fixp->time = NAN;
    fixp->mode = MODE_NOT_SEEN;
    fixp->latitude = fixp->longitude = NAN;
    fixp->track = NAN;
    fixp->speed = NAN;
    fixp->climb = NAN;
    fixp->altitude = NAN;
    fixp->ept = NAN;
    fixp->epx = NAN;
    fixp->epy = NAN;
    fixp->epv = NAN;
    fixp->epd = NAN;
    fixp->eps = NAN;
    fixp->epc = NAN;
}

void gps_merge_fix( /*@ out @*/ struct gps_fix_t *to,
		   gps_mask_t transfer,
		   /*@ in @*/ struct gps_fix_t *from)
/* merge new data into an old fix */
{
    if ((NULL == to) || (NULL == from))
	return;
    if ((transfer & TIME_IS) != 0)
	to->time = from->time;
    if ((transfer & LATLON_IS) != 0) {
	to->latitude = from->latitude;
	to->longitude = from->longitude;
    }
    if ((transfer & MODE_IS) != 0)
	to->mode = from->mode;
    if ((transfer & ALTITUDE_IS) != 0)
	to->altitude = from->altitude;
    if ((transfer & TRACK_IS) != 0)
	to->track = from->track;
    if ((transfer & SPEED_IS) != 0)
	to->speed = from->speed;
    if ((transfer & CLIMB_IS) != 0)
	to->climb = from->climb;
    if ((transfer & TIMERR_IS) != 0)
	to->ept = from->ept;
    if ((transfer & HERR_IS) != 0) {
	to->epx = from->epx;
	to->epy = from->epy;
    }
    if ((transfer & VERR_IS) != 0)
	to->epv = from->epv;
    if ((transfer & SPEEDERR_IS) != 0)
	to->eps = from->eps;
}

double timestamp(void)
{
    struct timeval tv;
    (void)gettimeofday(&tv, NULL);
    /*@i1@*/ return (tv.tv_sec + tv.tv_usec * 1e-6);
}

time_t mkgmtime(register struct tm * t)
/* struct tm to seconds since Unix epoch */
{
    register int year;
    register time_t result;
    static const int cumdays[MONTHSPERYEAR] =
	{ 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };

    /*@ +matchanyintegral @*/
    year = 1900 + t->tm_year + t->tm_mon / MONTHSPERYEAR;
    result = (year - 1970) * 365 + cumdays[t->tm_mon % MONTHSPERYEAR];
    result += (year - 1968) / 4;
    result -= (year - 1900) / 100;
    result += (year - 1600) / 400;
    if ((year % 4) == 0 && ((year % 100) != 0 || (year % 400) == 0) &&
	(t->tm_mon % MONTHSPERYEAR) < 2)
	result--;
    result += t->tm_mday - 1;
    result *= 24;
    result += t->tm_hour;
    result *= 60;
    result += t->tm_min;
    result *= 60;
    result += t->tm_sec;
    /*@ -matchanyintegral @*/
    return (result);
}

double iso8601_to_unix( /*@in@*/ char *isotime)
/* ISO8601 UTC to Unix UTC */
{
#ifndef USE_QT
    char *dp = NULL;
    double usec;
    struct tm tm;

    /*@i1@*/ dp = strptime(isotime, "%Y-%m-%dT%H:%M:%S", &tm);
    if (dp != NULL && *dp == '.')
	usec = strtod(dp, NULL);
    else
	usec = 0;
    return (double)mkgmtime(&tm) + usec;
#else
    double usec = 0;

    QString t(isotime);
    QDateTime d = QDateTime::fromString(isotime, Qt::ISODate);
    QStringList sl = t.split(".");
    if (sl.size() > 1)
	usec = sl[1].toInt() / pow(10., (double)sl[1].size());
    return d.toTime_t() + usec;
#endif
}

/* *INDENT-OFF* */
/*@observer@*/char *unix_to_iso8601(double fixtime, /*@ out @*/
				     char isotime[], size_t len)
/* Unix UTC time to ISO8601, no timezone adjustment */
/* example: 2007-12-11T23:38:51.0Z */
{
    struct tm when;
    double integral, fractional;
    time_t intfixtime;
    char timestr[30];
    char fractstr[10];

    fractional = modf(fixtime, &integral);
    intfixtime = (time_t) integral;
    (void)gmtime_r(&intfixtime, &when);

    (void)strftime(timestr, sizeof(timestr), "%Y-%m-%dT%H:%M:%S", &when);
    (void)snprintf(fractstr, sizeof(fractstr), "%.1f", fractional);
    /* add fractional part, ignore leading 0; "0.2" -> ".2" */
    (void)snprintf(isotime, len, "%s%sZ", timestr, fractstr + 1);
    return isotime;
}
/* *INDENT-ON* */

/*
 * The 'week' part of GPS dates are specified in weeks since 0000 on 06
 * January 1980, with a rollover at 1024.  At time of writing the last
 * rollover happened at 0000 22 August 1999.  Time-of-week is in seconds.
 *
 * This code copes with both conventional GPS weeks and the "extended"
 * 15-or-16-bit version with no wraparound that appears in Zodiac
 * chips and is supposed to appear in the Geodetic Navigation
 * Information (0x29) packet of SiRF chips.  Some SiRF firmware versions
 * (notably 231) actually ship the wrapped 10-bit week, despite what
 * the protocol reference claims.
 *
 * Note: This time will need to be corrected for leap seconds.
 */
#define GPS_EPOCH	315964800	/* GPS epoch in Unix time */
#define SECS_PER_WEEK	(60*60*24*7)	/* seconds per week */
#define GPS_ROLLOVER	(1024*SECS_PER_WEEK)	/* rollover period */

double gpstime_to_unix(int week, double tow)
{
    double fixtime;

    if (week >= 1024)
	fixtime = GPS_EPOCH + (week * SECS_PER_WEEK) + tow;
    else {
	time_t now, last_rollover;
	(void)time(&now);
	last_rollover =
	    GPS_EPOCH + ((now - GPS_EPOCH) / GPS_ROLLOVER) * GPS_ROLLOVER;
	/*@i@*/ fixtime = last_rollover + (week * SECS_PER_WEEK) + tow;
    }
    return fixtime;
}

void unix_to_gpstime(double unixtime, /*@out@*/ int *week, /*@out@ */
		     double *tow)
{
    unixtime -= GPS_EPOCH;
    *week = (int)(unixtime / SECS_PER_WEEK);
    *tow = fmod(unixtime, SECS_PER_WEEK);
}

#define Deg2Rad(n)	((n) * DEG_2_RAD)

double earth_distance(double lat1, double lon1, double lat2, double lon2)
/* distance in meters between two points specified in degrees. */
{
    /*
     * this is a translation of the javascript implementation of the
     * Vincenty distance formula by Chris Veness. See
     * http://www.movable-type.co.uk/scripts/latlong-vincenty.html
     */
    double a, b, f;		// WGS-84 ellipsoid params
    double L, L_P, U1, U2, s_U1, c_U1, s_U2, c_U2;
    double uSq, A, B, d_S, lambda;
    double s_L, c_L, s_S, C;
    double c_S, S, s_A, c_SqA, c_2SM;
    int i = 100;

    a = WGS84A;
    b = WGS84B;
    f = 1 / WGS84F;
    L = Deg2Rad(lon2 - lon1);
    U1 = atan((1 - f) * tan(Deg2Rad(lat1)));
    U2 = atan((1 - f) * tan(Deg2Rad(lat2)));
    s_U1 = sin(U1);
    c_U1 = cos(U1);
    s_U2 = sin(U2);
    c_U2 = cos(U2);
    lambda = L;

    do {
	s_L = sin(lambda);
	c_L = cos(lambda);
	s_S = sqrt((c_U2 * s_L) * (c_U2 * s_L) +
		   (c_U1 * s_U2 - s_U1 * c_U2 * c_L) *
		   (c_U1 * s_U2 - s_U1 * c_U2 * c_L));

	if (s_S == 0)
	    return 0;

	c_S = s_U1 * s_U2 + c_U1 * c_U2 * c_L;
	S = atan2(s_S, c_S);
	s_A = c_U1 * c_U2 * s_L / s_S;
	c_SqA = 1 - s_A * s_A;
	c_2SM = c_S - 2 * s_U1 * s_U2 / c_SqA;

	if (isnan(c_2SM))
	    c_2SM = 0;

	C = f / 16 * c_SqA * (4 + f * (4 - 3 * c_SqA));
	L_P = lambda;
	lambda = L + (1 - C) * f * s_A *
	    (S + C * s_S * (c_2SM + C * c_S * (2 * c_2SM * c_2SM - 1)));
    } while ((fabs(lambda - L_P) > 1.0e-12) && (--i > 0));

    if (i == 0)
	return NAN;		// formula failed to converge

    uSq = c_SqA * ((a * a) - (b * b)) / (b * b);
    A = 1 + uSq / 16384 * (4096 + uSq * (-768 + uSq * (320 - 175 * uSq)));
    B = uSq / 1024 * (256 + uSq * (-128 + uSq * (74 - 47 * uSq)));
    d_S = B * s_S * (c_2SM + B / 4 *
		     (c_S * (-1 + 2 * c_2SM * c_2SM) - B / 6 * c_2SM *
		      (-3 + 4 * s_S * s_S) * (-3 + 4 * c_2SM * c_2SM)));

    return (WGS84B * A * (S - d_S));
}

/*****************************************************************************

Carl Carter of SiRF supplied this algorithm for computing DOPs from
a list of visible satellites (some typos corrected)...

For satellite n, let az(n) = azimuth angle from North and el(n) be elevation.
Let:

    a(k, 1) = sin az(k) * cos el(k)
    a(k, 2) = cos az(k) * cos el(k)
    a(k, 3) = sin el(k)

Then form the line-of-sight matrix A for satellites used in the solution:

    | a(1,1) a(1,2) a(1,3) 1 |
    | a(2,1) a(2,2) a(2,3) 1 |
    |   :       :      :   : |
    | a(n,1) a(n,2) a(n,3) 1 |

And its transpose A~:

    |a(1, 1) a(2, 1) .  .  .  a(n, 1) |
    |a(1, 2) a(2, 2) .  .  .  a(n, 2) |
    |a(1, 3) a(2, 3) .  .  .  a(n, 3) |
    |    1       1   .  .  .     1    |

Compute the covariance matrix (A~*A)^-1, which is guaranteed symmetric:

    | s(x)^2    s(x)*s(y)  s(x)*s(z)  s(x)*s(t) |
    | s(y)*s(x) s(y)^2     s(y)*s(z)  s(y)*s(t) |
    | s(z)*s(x) s(z)*s(y)  s(z)^2     s(z)*s(t) |
    | s(t)*s(x) s(t)*s(y)  s(t)*s(z)  s(t)^2    |

Then:

GDOP = sqrt(s(x)^2 + s(y)^2 + s(z)^2 + s(t)^2)
TDOP = sqrt(s(t)^2)
PDOP = sqrt(s(x)^2 + s(y)^2 + s(z)^2)
HDOP = sqrt(s(x)^2 + s(y)^2)
VDOP = sqrt(s(z)^2)

Here's how we implement it...

First, each compute element P(i,j) of the 4x4 product A~*A.
If S(k=1,k=n): f(...) is the sum of f(...) as k varies from 1 to n, then
applying the definition of matrix product tells us:

P(i,j) = S(k=1,k=n): B(i, k) * A(k, j)

But because B is the transpose of A, this reduces to

P(i,j) = S(k=1,k=n): A(k, i) * A(k, j)

This is not, however, the entire algorithm that SiRF uses.  Carl writes:

> As you note, with rounding accounted for, most values agree exactly, and
> those that don't agree our number is higher.  That is because we
> deweight some satellites and account for that in the DOP calculation.
> If a satellite is not used in a solution at the same weight as others,
> it should not contribute to DOP calculation at the same weight.  So our
> internal algorithm does a compensation for that which you would have no
> way to duplicate on the outside since we don't output the weighting
> factors.  In fact those are not even available to API users.

Queried about the deweighting, Carl says:

> In the SiRF tracking engine, each satellite track is assigned a quality
> value based on the tracker's estimate of that signal.  It includes C/No
> estimate, ability to hold onto the phase, stability of the I vs. Q phase
> angle, etc.  The navigation algorithm then ranks all the tracks into
> quality order and selects which ones to use in the solution and what
> weight to give those used in the solution.  The process is actually a
> bit of a "trial and error" method -- we initially use all available
> tracks in the solution, then we sequentially remove the lowest quality
> ones until the solution stabilizes.  The weighting is inherent in the
> Kalman filter algorithm.  Once the solution is stable, the DOP is
> computed from those SVs used, and there is an algorithm that looks at
> the quality ratings and determines if we need to deweight any.
> Likewise, if we use altitude hold mode for a 3-SV solution, we deweight
> the phantom satellite at the center of the Earth.

So we cannot exactly duplicate what SiRF does internally.  We'll leave
HDOP alone and use our computed values for VDOP and PDOP.  Note, this
may have to change in the future if this code is used by a non-SiRF
driver.

******************************************************************************/

void clear_dop( /*@out@*/ struct dop_t *dop)
{
    dop->xdop = dop->ydop = dop->vdop = dop->tdop = dop->hdop = dop->pdop =
	dop->gdop = NAN;
}

/*@ -fixedformalarray -mustdefine @*/
static bool invert(double mat[4][4], /*@out@*/ double inverse[4][4])
{
    // Find all NECESSARY 2x2 subdeterminants
    double Det2_12_01 = mat[1][0] * mat[2][1] - mat[1][1] * mat[2][0];
    double Det2_12_02 = mat[1][0] * mat[2][2] - mat[1][2] * mat[2][0];
    //double Det2_12_03 = mat[1][0]*mat[2][3] - mat[1][3]*mat[2][0];
    double Det2_12_12 = mat[1][1] * mat[2][2] - mat[1][2] * mat[2][1];
    //double Det2_12_13 = mat[1][1]*mat[2][3] - mat[1][3]*mat[2][1];
    //double Det2_12_23 = mat[1][2]*mat[2][3] - mat[1][3]*mat[2][2];
    double Det2_13_01 = mat[1][0] * mat[3][1] - mat[1][1] * mat[3][0];
    //double Det2_13_02 = mat[1][0]*mat[3][2] - mat[1][2]*mat[3][0];
    double Det2_13_03 = mat[1][0] * mat[3][3] - mat[1][3] * mat[3][0];
    //double Det2_13_12 = mat[1][1]*mat[3][2] - mat[1][2]*mat[3][1]; 
    double Det2_13_13 = mat[1][1] * mat[3][3] - mat[1][3] * mat[3][1];
    //double Det2_13_23 = mat[1][2]*mat[3][3] - mat[1][3]*mat[3][2]; 
    double Det2_23_01 = mat[2][0] * mat[3][1] - mat[2][1] * mat[3][0];
    double Det2_23_02 = mat[2][0] * mat[3][2] - mat[2][2] * mat[3][0];
    double Det2_23_03 = mat[2][0] * mat[3][3] - mat[2][3] * mat[3][0];
    double Det2_23_12 = mat[2][1] * mat[3][2] - mat[2][2] * mat[3][1];
    double Det2_23_13 = mat[2][1] * mat[3][3] - mat[2][3] * mat[3][1];
    double Det2_23_23 = mat[2][2] * mat[3][3] - mat[2][3] * mat[3][2];

    // Find all NECESSARY 3x3 subdeterminants
    double Det3_012_012 = mat[0][0] * Det2_12_12 - mat[0][1] * Det2_12_02
	+ mat[0][2] * Det2_12_01;
    //double Det3_012_013 = mat[0][0]*Det2_12_13 - mat[0][1]*Det2_12_03
    //                            + mat[0][3]*Det2_12_01;
    //double Det3_012_023 = mat[0][0]*Det2_12_23 - mat[0][2]*Det2_12_03
    //                            + mat[0][3]*Det2_12_02;
    //double Det3_012_123 = mat[0][1]*Det2_12_23 - mat[0][2]*Det2_12_13
    //                            + mat[0][3]*Det2_12_12;
    //double Det3_013_012 = mat[0][0]*Det2_13_12 - mat[0][1]*Det2_13_02
    //                            + mat[0][2]*Det2_13_01;
    double Det3_013_013 = mat[0][0] * Det2_13_13 - mat[0][1] * Det2_13_03
	+ mat[0][3] * Det2_13_01;
    //double Det3_013_023 = mat[0][0]*Det2_13_23 - mat[0][2]*Det2_13_03
    //                            + mat[0][3]*Det2_13_02;
    //double Det3_013_123 = mat[0][1]*Det2_13_23 - mat[0][2]*Det2_13_13
    //                            + mat[0][3]*Det2_13_12;
    //double Det3_023_012 = mat[0][0]*Det2_23_12 - mat[0][1]*Det2_23_02
    //                            + mat[0][2]*Det2_23_01;
    //double Det3_023_013 = mat[0][0]*Det2_23_13 - mat[0][1]*Det2_23_03
    //                            + mat[0][3]*Det2_23_01;
    double Det3_023_023 = mat[0][0] * Det2_23_23 - mat[0][2] * Det2_23_03
	+ mat[0][3] * Det2_23_02;
    //double Det3_023_123 = mat[0][1]*Det2_23_23 - mat[0][2]*Det2_23_13
    //                            + mat[0][3]*Det2_23_12;
    double Det3_123_012 = mat[1][0] * Det2_23_12 - mat[1][1] * Det2_23_02
	+ mat[1][2] * Det2_23_01;
    double Det3_123_013 = mat[1][0] * Det2_23_13 - mat[1][1] * Det2_23_03
	+ mat[1][3] * Det2_23_01;
    double Det3_123_023 = mat[1][0] * Det2_23_23 - mat[1][2] * Det2_23_03
	+ mat[1][3] * Det2_23_02;
    double Det3_123_123 = mat[1][1] * Det2_23_23 - mat[1][2] * Det2_23_13
	+ mat[1][3] * Det2_23_12;

    // Find the 4x4 determinant
    static double det;
    det = mat[0][0] * Det3_123_123
	- mat[0][1] * Det3_123_023
	+ mat[0][2] * Det3_123_013 - mat[0][3] * Det3_123_012;

    // Very small determinants probably reflect floating-point fuzz near zero
    if (fabs(det) < 0.0001)
	return false;

    inverse[0][0] = Det3_123_123 / det;
    //inverse[0][1] = -Det3_023_123 / det;
    //inverse[0][2] =  Det3_013_123 / det;
    //inverse[0][3] = -Det3_012_123 / det;

    //inverse[1][0] = -Det3_123_023 / det;
    inverse[1][1] = Det3_023_023 / det;
    //inverse[1][2] = -Det3_013_023 / det;
    //inverse[1][3] =  Det3_012_023 / det;

    //inverse[2][0] =  Det3_123_013 / det;
    //inverse[2][1] = -Det3_023_013 / det;
    inverse[2][2] = Det3_013_013 / det;
    //inverse[2][3] = -Det3_012_013 / det;

    //inverse[3][0] = -Det3_123_012 / det;
    //inverse[3][1] =  Det3_023_012 / det;
    //inverse[3][2] = -Det3_013_012 / det;
    inverse[3][3] = Det3_012_012 / det;

    return true;
}

/*@ +fixedformalarray +mustdefine @*/

gps_mask_t fill_dop(const struct gps_data_t * gpsdata, struct dop_t * dop)
{
    double prod[4][4];
    double inv[4][4];
    double satpos[MAXCHANNELS][4];
    double xdop, ydop, hdop, vdop, pdop, tdop, gdop;
    int i, j, k, n;

    gpsd_report(LOG_INF, "Satellite picture:\n");
    for (k = 0; k < MAXCHANNELS; k++) {
	if (gpsdata->used[k])
	    gpsd_report(LOG_INF, "az: %d el: %d  SV: %d\n",
			gpsdata->azimuth[k], gpsdata->elevation[k],
			gpsdata->used[k]);
    }
#ifdef __UNUSED__
#endif /* __UNUSED__ */

    for (n = k = 0; k < gpsdata->satellites_used; k++) {
	if (gpsdata->used[k] == 0)
	    continue;
	satpos[n][0] = sin(gpsdata->azimuth[k] * DEG_2_RAD)
	    * cos(gpsdata->elevation[k] * DEG_2_RAD);
	satpos[n][1] = cos(gpsdata->azimuth[k] * DEG_2_RAD)
	    * cos(gpsdata->elevation[k] * DEG_2_RAD);
	satpos[n][2] = sin(gpsdata->elevation[k] * DEG_2_RAD);
	satpos[n][3] = 1;
	n++;
    }

#ifdef __UNUSED__
    gpsd_report(LOG_INF, "Line-of-sight matrix:\n");
    for (k = 0; k < n; k++) {
	gpsd_report(LOG_INF, "%f %f %f %f\n",
		    satpos[k][0], satpos[k][1], satpos[k][2], satpos[k][3]);
    }
#endif /* __UNUSED__ */

    for (i = 0; i < 4; ++i) {	//< rows
	for (j = 0; j < 4; ++j) {	//< cols
	    prod[i][j] = 0.0;
	    for (k = 0; k < n; ++k) {
		prod[i][j] += satpos[k][i] * satpos[k][j];
	    }
	}
    }

#ifdef __UNUSED__
    gpsd_report(LOG_INF, "product:\n");
    for (k = 0; k < 4; k++) {
	gpsd_report(LOG_INF, "%f %f %f %f\n",
		    prod[k][0], prod[k][1], prod[k][2], prod[k][3]);
    }
#endif /* __UNUSED__ */

    if (invert(prod, inv)) {
#ifdef __UNUSED__
	/*
	 * Note: this will print garbage unless all the subdeterminants
	 * are computed in the invert() function.
	 */
	gpsd_report(LOG_RAW, "inverse:\n");
	for (k = 0; k < 4; k++) {
	    gpsd_report(LOG_RAW, "%f %f %f %f\n",
			inv[k][0], inv[k][1], inv[k][2], inv[k][3]);
	}
#endif /* __UNUSED__ */
    } else {
#ifndef USE_QT
	gpsd_report(LOG_DATA,
		    "LOS matrix is singular, can't calculate DOPs - source '%s'\n",
		    gpsdata->dev.path);
#endif
	return 0;
    }

    xdop = sqrt(inv[0][0]);
    ydop = sqrt(inv[1][1]);
    hdop = sqrt(inv[0][0] + inv[1][1]);
    vdop = sqrt(inv[2][2]);
    pdop = sqrt(inv[0][0] + inv[1][1] + inv[2][2]);
    tdop = sqrt(inv[3][3]);
    gdop = sqrt(inv[0][0] + inv[1][1] + inv[2][2] + inv[3][3]);

#ifndef USE_QT
    gpsd_report(LOG_DATA,
		"DOPS computed/reported: X=%f/%f, Y=%f/%f, H=%f/%f, V=%f/%f, P=%f/%f, T=%f/%f, G=%f/%f\n",
		xdop, dop->xdop, ydop, dop->ydop, hdop, dop->hdop, vdop,
		dop->vdop, pdop, dop->pdop, tdop, dop->tdop, gdop, dop->gdop);
#endif

    /*@ -usedef @*/
    if (isnan(dop->xdop) != 0) {
	dop->xdop = xdop;
    }
    if (isnan(dop->ydop) != 0) {
	dop->ydop = ydop;
    }
    if (isnan(dop->hdop) != 0) {
	dop->hdop = hdop;
    }
    if (isnan(dop->vdop) != 0) {
	dop->vdop = vdop;
    }
    if (isnan(dop->pdop) != 0) {
	dop->pdop = pdop;
    }
    if (isnan(dop->tdop) != 0) {
	dop->tdop = tdop;
    }
    if (isnan(dop->gdop) != 0) {
	dop->gdop = gdop;
    }
    /*@ +usedef @*/

    return DOP_IS;
}
