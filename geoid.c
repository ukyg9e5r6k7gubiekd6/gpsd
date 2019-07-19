/*
 * geoid.c -- ECEF to WGS84 conversions, including ellipsoid-to-MSL height
 *
 * This code does not specify, but given the creation date, I assume it
 * is using the EGM96 geoid revised 2004.
 *
 * Geoid separation code by Oleg Gusev, from data by Peter Dana.
 * ECEF conversion by Rob Janssen.
 *
 * This file is Copyright (c) 2010-2018 by the GPSD project
 * SPDX-License-Identifier: BSD-2-clause
 */

#include "gpsd_config.h"  /* must be before all includes */

#include <math.h>
#include "gpsd.h"

static double fix_minuz(double d);
static double atan2z(double y, double x);

static double bilinear(double x1, double y1, double x2, double y2, double x,
		       double y, double z11, double z12, double z21,
		       double z22)
{
    double delta;

#define EQ(a, b) (fabs((a) - (b)) < 0.001)
    if (EQ(y1, y2) && EQ(x1, x2))
	return (z11);
    if (EQ(y1, y2) && !EQ(x1, x2))
	return (z22 * (x - x1) + z11 * (x2 - x)) / (x2 - x1);
    if (EQ(x1, x2) && !EQ(y1, y2))
	return (z22 * (y - y1) + z11 * (y2 - y)) / (y2 - y1);
#undef EQ

    delta = (y2 - y1) * (x2 - x1);

    return (z22 * (y - y1) * (x - x1) + z12 * (y2 - y) * (x - x1) +
	    z21 * (y - y1) * (x2 - x) + z11 * (y2 - y) * (x2 - x)) / delta;
}


/* return geoid separation (MSL-WGS84) in meters, given a lat/lon in degrees
 * FIXME: Which MSL is this?  EGM2008, EGM96 or EGM84?
 * It seems closest to, but not exactly EGM2008.
 *
 * Online calculator here:
 * https://geographiclib.sourceforge.io/cgi-bin/GeoidEval
 *
 * geoid_delta[][] has the geoid separation, in cm, on a 10 degree by 10
 *  degree grid for the entire planet.
 */
double wgs84_separation(double lat, double lon)
{
#define GEOID_ROW	19
#define GEOID_COL	37
    /* *INDENT-OFF* */
    /* geoid separation in cm */
    const int geoid_delta[GEOID_ROW][GEOID_COL]={
	/* 90S */
        {-3000, -3000, -3000, -3000, -3000, -3000, -3000, -3000, -3000, -3000,
         -3000, -3000, -3000, -3000, -3000, -3000, -3000, -3000, -3000, -3000,
         -3000, -3000, -3000, -3000, -3000, -3000, -3000, -3000, -3000, -3000,
         -3000, -3000, -3000, -3000, -3000, -3000, -3000},
	/* 80S */
        {-5300, -5400, -5500, -5200, -4800, -4200, -3800, -3800, -2900, -2600,
         -2600, -2400, -2300, -2100, -1900, -1600, -1200,  -800,  -400,  -100,
           100,   400,   400,   600,   500,   400,   200,  -600, -1500, -2400,
         -3300, -4000, -4800, -5000, -5300, -5200, -5300},
	/* 70S */
        {-6100, -6000, -6100, -5500, -4900, -4400, -3800, -3100, -2500, -1600,
          -600,   100,   400,   500,   400,   200,   600,  1200,  1600,  1600,
          1700,  2100,  2000,  2600,  2600,  2200,  1600,  1000,  -100, -1600,
         -2900, -3600, -4600, -5500, -5400, -5900, -6100},
	/* 60S */
        {-4500, -4300, -3700, -3200, -3000, -2600, -2300, -2200, -1600, -1000,
          -200,  1000,  2000,  2000,  2100,  2400,  2200,  1700,  1600,  1900,
          2500,  3000,  3500,  3500,  3300,  3000,  2700,  1000,  -200, -1400,
         -2300, -3000, -3300, -2900, -3500, -4300, -4500},
	/* 50S */
        {-1500, -1800, -1800, -1600, -1700, -1500, -1000, -1000,  -800,  -200,
           600,  1400,  1300,   300,   300,  1000,  2000,  2700,  2500,  2600,
          3400,  3900,  4500,  4500,  3800,  3900,  2800,  1300,  -100, -1500,
         -2200, -2200, -1800, -1500, -1400, -1000, -1500},
	/* 40S */
        { 2100,   600,   100,  -700, -1200, -1200, -1200, -1000,  -700,  -100,
           800,  2300,  1500,  -200,  -600,   600,  2100,  2400,  1800,  2600,
          3100,  3300,  3900,  4100,  3000,  2400,  1300,  -200, -2000, -3200,
         -3300, -2700, -1400,  -200,   500,  2000,  2100},
	/* 30S */
        { 4600,  2200,   500,  -200,  -800, -1300, -1000,  -700,  -400,   100,
           900,  3200,  1600,   400,  -800,   400,  1200,  1500,  2200,  2700,
          3400,  2900,  1400,  1500,  1500,   700,  -900, -2500, -3700, -3900,
         -2300, -1400,  1500,  3300,  3400,  4500,  4600},
	/* 20S */
         {5100,  2700,  1000,   000,  -900, -1100,  -500,  -200,  -300,  -100,
           900,  3500,  2000,  -500,  -600,  -500,   000,  1300,  1700,  2300,
          2100,   800,  -900, -1000, -1100, -2000, -4000, -4700, -4500, -2500,
           500,  2300,  4500,  5800,  5700,  6300,  5100},
	/* 10S */
        { 3600,  2200,  1100,   600,  -100,  -800, -1000,  -800, -1100,  -900,
           100,  3200,   400, -1800, -1300,  -900,   400,  1400,  1200,  1300,
          -200, -1400, -2500, -3200, -3800, -6000, -7500, -6300, -2600,   000,
          3500,  5200,  6800,  7600,  6400,  5200,  3600},
	/* 00N */
        { 2200,  1600,  1700,  1300,   100, -1200, -2300, -2000, -1400,  -300,
          1400,  1000, -1500, -2700, -1800,   300,  1200,  2000,  1800,  1200,
         -1300,  -900, -2800, -4900, -6200, -8900, -10200,-6300,  -900,  3300,
          5800,  7300,  7400,  6300,  5000,  3200,  2200},
	/* 10N */
        { 1300,  1200,  1100,   200, -1100, -2800, -3800, -2900, -1000,   300,
           100, -1100, -4100, -4200, -1600,   300,  1700,  3300,  2200,  2300,
           200,  -300,  -700, -3600, -5900, -9000, -9500, -6300, -2400,  1200,
          5300,  6000,  5800,  4600,  3600,  2600,  1300},
	/* 20N */
        {  500,  1000,   700,  -700, -2300, -3900, -4700, -3400,  -900, -1000,
         -2000, -4500, -4800, -3200,  -900,  1700,  2500,  3100,  3100,  2600,
          1500,   600,   100, -2900, -4400, -6100, -6700, -5900, -3600, -1100,
          2100,  3900,  4900,  3900,  2200,  1000,   500},
	/* 30N */
        { -700,  -500,  -800, -1500, -2800, -4000, -4200, -2900, -2200, -2600,
         -3200, -5100, -4000, -1700,  1700,  3100,  3400,  4400,  3600,  2800,
          2900,  1700,  1200, -2000, -1500, -4000, -3300, -3400, -3400, -2800,
           700,  2900,  4300,  2000,   400,  -600,  -700},
	/* 40N */
        { 1200, -1000, -1300, -2000, -3100, -3400, -2100, -1600, -2600, -3400,
         -3300, -3500, -2600,   200,  3300,  5900,  5200,  5100,  5200,  4800,
          3500,  4000,  3300,  -900, -2800, -3900, -4800, -5900, -5000, -2800,
           300,  2300,  3700,  1800,  -100, -1100, -1200},
	/* 50N */
        { -800,   800,   800,   100, -1100, -1900, -1600, -1800, -2200, -3500,
         -4000, -2600, -1200,  2400,  4500,  6300,  6200,  5900,  4700,  4800,
          4200,  2800,  1200, -1000, -1900, -3300, -4300, -4200, -4300, -2900,
          -200,  1700,  2300,  2200,   600,   200,  -800},
	/* 60N */
        {  200,   900,  1700,  1000,  1300,   100, -1400, -3000, -3900, -4600,
         -4200, -2100,   600,  2900,  4900,  6500,  6000,  5700,  4700,  4100,
          2100,  1800,  1400,   700,  -300, -2200, -2900, -3200, -3200, -2600,
         -1500,  -200,  1300,  1700,  1900,   600,   200},
	/* 70N */
        {  200,   200,   100,  -100,  -300,  -700, -1400, -2400, -2700, -2500,
         -1900,   300,  2400,  3700,  4700,  6000,  6100,  5800,  5100,  4300,
          2900,  2000,  1200,   500,  -200, -1000, -1400, -1200, -1000, -1400,
         -1200,  -600,  -200,   300,   600,   400,   200},
	/* 80N */
        {  300,   100,  -200,  -300,  -300,  -300,  -100,   300,   100,   500,
           900,  1100,  1900,  2700,  3100,  3400,  3300,  3400,  3300,  3400,
          2800,  2300,  1700,  1300,   900,   400,   400,   100,  -200,  -200,
           000,   200,   300,   200,   100,   100,   300},
	/* 90N */
        { 1300,  1300,  1300,  1300,  1300,  1300,  1300,  1300,  1300,  1300,
          1300,  1300,  1300,  1300,  1300,  1300,  1300,  1300,  1300,  1300,
          1300,  1300,  1300,  1300,  1300,  1300,  1300,  1300,  1300,  1300,
          1300,  1300,  1300,  1300,  1300,  1300,  1300}
    };
    /* *INDENT-ON* */
    int ilat, ilon;
    int ilat1, ilat2, ilon1, ilon2;

    /* ilat is 0 to 18
     * lat -90 (90S) is ilat 0
     * lat 0 is ilat 9
     * lat 90 (90N) is ilat 18 */
    ilat = (int)floor((90. + lat) / 10);
    /* ilon is 0 to 36
     * lon -180 is ilon 0
     * long 0 (Prime Median) is ilon 18
     * long 180 is ilon 36 */
    ilon = (int)floor((180. + lon) / 10);

    /* sanity checks to prevent segfault on bad data */
    if ((GEOID_ROW <= ilat) || (0 > ilat) ||
        (GEOID_COL <= ilon) || (0 > ilon))
        return 0.0;

    ilat1 = ilat;
    ilon1 = ilon;
    ilat2 = (ilat < GEOID_ROW - 1) ? ilat + 1 : ilat;
    ilon2 = (ilon < GEOID_COL - 1) ? ilon + 1 : ilon;

    return bilinear(ilon1 * 10.0 - 180.0, ilat1 * 10.0 - 90.0,
		    ilon2 * 10.0 - 180.0, ilat2 * 10.0 - 90.0,
		    lon, lat,
		    (double)geoid_delta[ilat1][ilon1],
		    (double)geoid_delta[ilat1][ilon2],
		    (double)geoid_delta[ilat2][ilon1],
		    (double)geoid_delta[ilat2][ilon2]
	) / 100;
}


/* fill in WGS84 position/velocity fields from ECEF coordinates
 * x, y, z are all in meters
 * vx, vy, vz are all in meters/second
 */
gps_mask_t ecef_to_wgs84fix(struct gps_fix_t *fix, double *separation,
		            double x, double y, double z,
		            double vx, double vy, double vz)
{
    double lambda, phi, p, theta, n,vnorth, veast, vup;
    const double a = WGS84A;	/* equatorial radius */
    const double b = WGS84B;	/* polar radius */
    const double e2 = (a * a - b * b) / (a * a);
    const double e_2 = (a * a - b * b) / (b * b);
    gps_mask_t mask = 0;

    if (0 == isfinite(x) ||
        0 == isfinite(y) ||
        0 == isfinite(z)) {
	/* invalid inputs */
	return mask;
    }

    /* geodetic location */
    lambda = atan2z(y, x);
    p = sqrt(pow(x, 2) + pow(y, 2));
    theta = atan2z(z * a, p * b);
    phi = atan2z(z + e_2 * b * pow(sin(theta), 3),
	        p - e2 * a * pow(cos(theta), 3));
    n = a / sqrt(1.0 - e2 * pow(sin(phi), 2));

    /* altitude is WGS84 */
    fix->altitude = p / cos(phi) - n;

    fix->latitude = phi * RAD_2_DEG;
    fix->longitude = lambda * RAD_2_DEG;
    mask |= LATLON_SET | ALTITUDE_SET;
    *separation = wgs84_separation(fix->latitude, fix->longitude);
    /* velocity computation */
    vnorth = -vx * sin(phi) * cos(lambda) - vy * sin(phi) * sin(lambda) +
	     vz * cos(phi);
    veast = -vx * sin(lambda) + vy * cos(lambda);

    vup = vx * cos(phi) * cos(lambda) + vy * cos(phi) * sin(lambda) +
	  vz * sin(phi);

    /* save velNED */
    fix->NED.velN = vnorth;
    fix->NED.velE = veast;
    fix->NED.velD = -vup;
    mask |= VNED_SET;

    /* velNED is saved, let gpsd_error_model() do the
     * sanity checks and calculate climb/speed/track */

    return mask;
}

/*
 * Some systems propagate the sign along with zero. This messes up
 * certain trig functions, like atan2():
 *    atan2(+0, +0) = 0
 *    atan2(+0, -0) = PI
 * Obviously that will break things. Luckily the "==" operator thinks
 * that -0 == +0; we will use this to return an unambiguous value.
 *
 * I hereby decree that zero is not allowed to have a negative sign!
 */
static double fix_minuz(double d)
{
    return ((d == 0.0) ? 0.0 : d);
}

/* atan2() protected by fix_minuz() */
static double atan2z(double y, double x)
{
    return atan2(fix_minuz(y), fix_minuz(x));
}
