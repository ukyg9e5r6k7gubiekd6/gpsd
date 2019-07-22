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

#define GEOID_ROW	19
#define GEOID_COL	37
/* This table is EGM2008.  Values obtained from GeoidEval, part of
 * geographiclib.
 *
 * geoid_delta[][] has the geoid separation, in cm, on a 10 degree by 10
 * degree grid for the entire planet.
 */
/* *INDENT-OFF* */
const short geoid_delta[GEOID_ROW][GEOID_COL]={
    /* 180,  170W,  160W,  150W,  140W,  130W,  120W,  110W,  100W,   90W,
     * 80W,   70W,   60W,   50W,   40W,   30W,   20W,   10W,     0,   10E,
     * 20E,   30E,   40E,   50E,   60E,   70E,   80E    90E,  100E,  110E,
     *120E,  130E,  140E,  150E,  160E,  170E,  180 */
    /* 90S */
    {-3015, -3015, -3015, -3015, -3015, -3015, -3015, -3015, -3015, -3015,
     -3015, -3015, -3015, -3015, -3015, -3015, -3015, -3015, -3015, -3015,
     -3015, -3015, -3015, -3015, -3015, -3015, -3015, -3015, -3015, -3015,
     -3015, -3015, -3015, -3015, -3015, -3015, -3015},
    /* 80S */
    {-5232, -5275, -5286, -5218, -4775, -4319, -3854, -3523, -2910, -2337,
     -2417, -2471, -2230, -1869, -1732, -1540, -1048,  -569,  -501,  -166,
       146,   444,   534,   458,   473,   607,   562,  -271, -1300, -2475,
     -3434, -4196, -4989, -5094, -4987, -5055, -5232},
    /* 70S */
    {-6218, -6333, -6021, -5705, -5213, -4365, -3757, -3250, -2672, -2138,
      -844,   249,   458,    26,   166,   118,   303,  1010,  1423,  1682,
      1766,  2027,  2164,  2882,  3010,  1749,  1799,  1099,  -813, -2149,
     -2705, -3907, -4287, -5466, -5576, -5800, -6218},
    /* 60S */
    {-4598, -4278, -3732, -3205, -3008, -2669, -2350, -2218, -1660, -1123,
      -350,   924,  2016,  2032,  2123,  2384,  2121,  1720,  1517,  2008,
      2546,  2914,  3395,  3315,  3279,  2930,  2502,   893,  -328, -1364,
     -2467, -3143, -3311, -2956, -3485, -4454, -4598},
    /* 50S */
    {-1615, -1875, -1839, -1605, -1737, -1580, -1010, -1051,  -838,  -316,
       502,  1186,  1264,   231,   353,  1085,  2065,  2744,  2518,  2584,
      3323,  3792,  4520,  4459,  3818,  3975,  2748,  1272,  -207, -1498,
     -2242, -2212, -1843, -1705, -1400, -1013, -1615},
    /* 40S */
    { 2048,   637,   109,  -679, -1186, -1275, -1282, -1022,  -690,   -84,
       694,  2220,  1341,  -266,  -670,   643,  2101,  2307,  1765,  2496,
      2941,  3298,  3877,  4069,  2844,  2333,  1318,  -238, -2098, -3250,
     -3351, -2741, -1482,  -201,   491,  1877,  2048},
    /* 30S */
    { 4772,  2257,   566,  -136,  -868, -1309, -1033,  -780,  -443,   100,
       815,  3633,  1810,   297,  -847,   347,  1025,  1504,  2165,  2599,
      3332,  3094,  1369,  1468,  1505,   613,  -904, -2512, -3751, -3939,
     -2222, -1442,  1262,  3189,  3371,  4379,  4772},
    /* 20S */
    { 4994,  2649,   935,  -115,  -838, -1134,  -525,  -218,  -396,  -137,
       811,  3496,  1958,  -784,  -740,  -579,   -49,  1221,  1657,  2280,
      2157,   728, -1021, -1098, -1032, -2065, -4028, -4802, -4599, -2561,
       446,  2424,  4588,  5724,  5698,  6400,  4994},
    /* 10S */
    { 3525,  2135,  1078,   544,  -268,  -917, -1087,  -771, -1177,  -919,
      -108,  2818,   386, -1911, -1226,  -952,   399,  1280,  1117,  1306,
       538, -1208, -2662, -3203, -3733, -6083, -7578, -6418, -2647,   -87,
      3840,  5240,  6898,  7727,  6209,  5068,  3525},
    /* 00N */
    { 2128,  1532,  1613,  1291,    79, -1296, -2298, -2026, -1412,  -406,
      1463,  1352, -1336, -2593, -1863,   245,  1355,  1913,  1723,   940,
     -1668, -1078, -2762, -4885, -6260, -8947,-10259, -6329,  -692,  3354,
      5983,  7602,  7231,  6240,  4944,  3205,  2128},
    /* 10N */
    { 1285,  1212,  1055,   140, -1115, -2904, -3930, -2924, -1145,   164, 
	91, -1043, -4146, -4259, -1691,   306,  1735,  3275,  2373,  2151,
       207,  -507,  -956, -3167, -5820, -9107, -9732, -6308, -2512,  1108,
      5183,  5985,  6049,  4527,  3531,  2456,  1285},
    /* 20N */
    {  442,   986,   757,  -696, -2285, -3936, -4739, -3350,  -747, -1083,
     -2037, -4813, -4698, -3279, -1001,  1714,  2535,  3112,  3086,  2679,
      1422,   923,   183, -3070, -4245, -6064, -6806, -5947, -3814, -1319,
      2150,  3799,  4898,  4149,  2148,   899,   442},
    /* 30N */
    { -720,  -528,  -867, -1588, -2796, -4036, -4246, -2996, -2323, -2744,
     -3238, -5164, -4064, -1751,  1657,  3023,  3404,  4354,  3440,  2813,
      2611,  1606,  1086, -1793, -1527, -3766, -3687, -3375, -3365, -2385,
       785,  3020,  4178,  1940,   331,  -683,  -720},
    /* 40N */
    {-1223, -1076, -1298, -2085, -3120, -3679, -2334, -1787, -2482, -3323,
     -3395, -3372, -2654,   105,  3250,  5889,  5198,  4903,  5086,  4519,
      3395,  3890,  3113, -1769, -2856, -4092, -5468, -6597, -5476, -2802,
       232,  2305,  3768,  1694,  -154, -1068, -1223},
    /* 50N */
    { -578,   559,   749,     0, -1140, -1818, -1555, -1769, -2372, -3485, 
     -3941, -2614, -1253,  2406,  4434,  6265,  6180,  5855,  4506,  4799,
      3972,  2745,  1085, -1170, -1916, -3305, -4435, -4163, -4190, -2951,
      -414,  1494,  2268,  2031,   638,   325,  -578},
    /* 60N */
    {   93,   969,  1498,  1439,  1128,     5, -1610, -3095, -4193, -4678,
     -4184, -2252,   503,  2882,  4892,  6390,  6069,  5582,  4788,  4080,
      1893,  1586,  1136,   622,  -389, -2123, -3028, -3366, -3396, -2770,
     -1663,    25,  1168,  1641,  1426,   921,    93},
    /* 70N */
    {  242,    98,   -54,  -127,  -253,  -805, -1506, -2464, -2740, -2559,
     -1849,   294,  2357,  2907,  4523,  5594,  5966,  5592,  4878,  4190,
      2963,  1981,  1064,   253,  -318,  -979, -1274, -1175, -1449,  -850,
      -828,  -540,   -35,   279,   345,   334,   242},
    /* 80N */
    {  342,   244,    38,    15,  -109,  -240,    16,   550,   350,   865,
      1188,   703,  1930,  2776,  3351,  3653,  3177,  3354,  3615,  3522,
      2914,  2426,  1845,  1297,   809,   248,   230,   149,   274,   153,
       164,   104,    12,   243,   184,    99,   342},
    /* 90N */
    { 1490,  1490,  1490,  1490,  1490,  1490,  1490,  1490,  1490,  1490,
      1490,  1490,  1490,  1490,  1490,  1490,  1490,  1490,  1490,  1490,
      1490,  1490,  1490,  1490,  1490,  1490,  1490,  1490,  1490,  1490,
      1490,  1490,  1490,  1490,  1490,  1490,  1490}
};
/* *INDENT-ON* */

static double bilinear(double x1, double y1, double x2, double y2, double x,
		       double y, double z11, double z12, double z21,
		       double z22)
{
    double delta = (y2 - y1) * (x2 - x1);
    double xx1 = x - x1;
    double x2x = x2 - x;
    double yy1 = y - y1;
    double y2y = y2 - y;

    return (z22 * yy1 * xx1 + z12 * y2y * xx1 +
	    z21 * yy1 * x2x + z11 * y2y * x2x) / delta;
}


/* return geoid separation (MSL-WGS84) in meters, given a lat/lon in degrees.
 * Online calculator here:
 * https://geographiclib.sourceforge.io/cgi-bin/GeoidEval
 *
 * The value for any lat/lon is computed from the 10x10 usingin bilinear
 * interpolation.  Should probably use cubic interpolation as GeoidEval
 * does by default.
 *
 * Calculated separation can differ from geoidEval by up to 12m!
 */
double wgs84_separation(double lat, double lon)
{
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

#if __UNUSED__
    fprintf(stderr, "ilat1 %3d lat %9.4f ilat2 %3d\n"
                    "ilon1 %3d lon %9.4f ilon2 %3d\n",
                    ilat1, lat, ilat2,
                    ilon1, lon, ilon2);
#endif

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
