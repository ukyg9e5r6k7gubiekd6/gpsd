/* test driver for the ECEF to WGS84 conversions, and
 * magnetic variance, in geoid.c
 *
 * Keep in sync with test_clienthelpers.py
 *
 * This file is Copyright (c) 2010-2019 by the GPSD project
 * SPDX-License-Identifier: BSD-2-clause
 */

/* first so the #defs work */
#include "../gpsd_config.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>            /* for getopt() */

#include "../gpsd.h"

struct test3 {
    double lat;
    double lon;
    double separation;
    double variation;
    char *desc;
};

struct test3 tests3[] = {
    /*  wgs84 separation, cm precision
     *  online calculator:
     *  https://geographiclib.sourceforge.io/cgi-bin/GeoidEval
     *
     * magnetic variation, hundredths of a degree precision.
     *
     * same tests as test_clienthelpers.py.  Keep them in sync.
     */

    // Easter Island: EGM2008 -3.8178, EGM96 -4.9979, EGM84 -5.1408
    // wmm2015 2.90
    {27.1127, -109.3497, -31.29, 8.45, "Easter Island"},
    // Kalahari: EGM2008, 20.6560, EGM96, 20.8419, EGM84 23.4496
    // wmm2015 6.73
    {25.5920, 21.0937, 21.80, 3.35,  "Kalahari Desert"},
    // Greenland: EGM2008 40.3981, EGM96 40.5912, EGM84 41.7056
    // wmm2015 28.26  AWEFUL!
    {71.7069, -42.6043, 40.11, -28.25, "Greenland"},
    // Kuk Swamp: EGM2008 62.8837, EGM96, 62.8002, EGM84, 64.5655
    // wmm2015 9.11
    // seems to be over a gravitational anomaly
    {5.7837, 144.3317, 62.34, 2.42, "Kuk Swamp PG"},
    // KBDN: EGM2008 -20.3509, EGM96 -19.8008, EGM84 -18.5562
    // wmm2015 14.61
    {44.094556, -121.200222, -21.95, 14.40, "Bend Airport, OR (KBDN)"},
    // PANC: EGM2008 7.6508, EGM96 7.8563, EGM84 8.0838
    // wmm2015 15.78
    {61.171333, -149.991164, 13.54, 15.52, "Anchorage Airport, AK (PANC)"},
    // KDEN: EGM2008 -18.1941, EGM96 -18.4209, EGM84 -15.9555
    // wmm2015 7.95
    {39.861666, -104.673166, -18.15, 7.84, "Denver Airport, CO (KDEN)"},
    // LHR: EGM2008 46.4499, EGM96 46.3061, EGM84 47.7620
    // wmm2015 0.17
    {51.46970, -0.45943, 46.26, -0.28, "London Heathrow Airport, UK (LHR)"},
    // SCPE: EGM2008 37.9592, EGM96 39.3400, EGM84 46.6604
    // wmm2015 -6.17
    {-22.92170, -68.158401, 35.46, -6.11,
     "San Pedro de Atacama Airport, CL (SCPE)"},
    // SIN: EGM2008 8.6453, EGM96 8.3503, EGM84 8.2509
    // wmm2015 0.22
    {1.350190, 103.994003, 7.51, 0.17, "Singapore Changi Airport, SG (SIN)"},
    // UURB: EGM2008 13.6322, EGM96 13.6448, EGM84 13.1280
    // wmm2015 11.41
    {55.617199, 38.06000, 13.22, 11.42, "Moscow Bykovo Airport, RU (UUBB)"},
    // SYD: EGM2008 13.0311, EGM96 13.3736, EGM84 13.3147
    // wmm2015 -4.28
    {33.946098, 151.177002, 13.59, -4.26, "Sydney Airport, AU (SYD)"},
    // Doyle: EGM2008 -23.3366, EGM96 -23.3278, EGM84 -21.1672
    // wmm2015 13.35
    {40, -120, -23.34, 13.35, "Near Doyle, CA"},

    // test calc at delta lat == 0
    // North Poll: EGM2008 14.8980, EGM96 13.6050, EGM84 13.0980
    // wmm2015 1.75
    {90, 0, 14.90, 1.75, "North Poll 0"},
    // wmm2015 3.75
    {90, 2, 14.90, 3.75, "North Poll 2"},
    // wmm2015 4.25
    {90, 2.5, 14.90, 4.25, "North Poll 2.5"},
    // wmm2015 1.75
    {90, 3, 14.90, 4.75, "North Poll 3"},
    // wmm2015 1.75
    {90, 5, 14.90, 6.75, "North Poll 5"},
    // wmm2015 -178.25
    {90, 180, 14.90, -178.25, "North Poll"},

    // Equator 0, EGM2008 17.2260, EGM96 17.1630, EGM84 18.3296
    // wmm2015 -4.84
    {0, 0, 17.23, -4.84, "Equator 0W"},

    // South Poll: EGM2008 -30.1500, EGM96 -29.5350, EGM84 -29.7120
    // wmm2015 -30.80
    {-90, 0, -30.15, -30.80, "South Poll"},

    // test calc at delta lon == 0
    // 2 0: EGM2008 17.1724, EGM96 16.8962, EGM84 17.3676
    // wmm2015 -4.17
    {2, 0, 18.42, -4.23, "2N 0W"},
    // 2.5 0: EGM2008 16.5384, EGM96 16.5991, EGM84 17.0643
    // wmm2015 -4.02
    {2.5, 0, 18.71, -4.08, "2.5N 0W"},
    // 3 0: EGM2008 16.7998, EGM96 16.6161, EGM84 16.7857
    // wmm2015 -3.87
    {3, 0, 19.01, -3.92, "3N 0W"},
    // 3.5 0: EGM2008 17.0646, EGM96 17.0821, EGM84 16.7220
    // wmm2015 -3.72
    {3.5, 0, 19.31, -3.77, "3.5N 0W"},
    // 5 0: EGM2008 20.1991, EGM96 20.4536, EGM84 20.3181
    // wmm2015 -3.31
    {5, 0, 20.20, -3.31, "5N 0W"},

    // test calc on diagonal
    // Equator 0, EGM2008 17.2260, EGM96 17.1630, EGM84 18.3296
    // wmm2015 -4.84
    // 2 2: EGM2008 16.2839, EGM96 16.1579, EGM84 17.5354
    // wmm2015 -3.53
    {2, 2, 18.39, -3.60, "2N 2E"},
    // 2.5 2.5: EGM2008 15.7918, EGM96 15.5314, EGM84 16.3230
    // wmm2015 -3.24
    {2.5, 2.5, 18.78, -3.30, "2.5N 2.5E"},
    // 3 3: EGM2008 15.2097, EGM96 15.0751, EGM84 14.6542
    // wmm2015 -2.95
    {3, 3, 19.20, -3.01, "3N 3E"},
    // 3.5 3.5: EGM2008 14.8706, EGM96 14.6668, EGM84 13.9592
    // wmm2015 -3.72
    {3.5, 3.5, 19.66, -2.73, "3.5N 3.5E"},

    // some 5x5 points, s/b exact EGM2008, +/- rounding
    // 5, 5:  EGM2008 21.2609, EGM96 20.8917, EGM84 20.3509
    // wmm2015 -1.91
    {5, 5, 21.26, -1.91, "5, 5"},
    // -5, -5: EGM2008 17.1068, EGM96 16.8362, EGM84 17.5916
    // wmm2015 -9.03
    {-5, -5, 17.11, -9.03, "-5, -5"},
    // -5, 5: EGM2008 9.3988, EGM96 9.2399, EGM84 9.7948
    // wmm2015 -4.80
    {-5, 5, 9.40, -4.80, "-5, 5"},
    // 5, -5: EGM2008 25.7668, EGM96 25.6144, EGM84 25.1224
    // wmm2015 -4.90
    {5, -5, 25.77, -4.90, "5, 5"},

    // test data for some former corners in the code
    // 0, -78.452222: EGM2008 26.8978, EGM96 25.3457, EGM84 26.1507
    // wmm2015 -3.87
    {0, -78.452222, 15.98, -3.89, "Equatorial Sign Bolivia"},
    // 51.4778067, 0: EGM2008 45.8961, EGM96 45.7976, EGM84 47.2468
    // wmm2015 -0.10
    {51.4778067, 0, 45.46, -0.11, "Lawn Greenwich Observatory UK"},
    // 0, 180: EGM2008 21.2813, EGM96 21.1534, EGM84 21.7089
    // wmm2015 9.75
    {0, 180, 21.28, 9.75, "Far away from Google default"},
    {0, -180, 21.28, 9.75, "Away far from Google default"},
};

struct test4 {
    double lat1;
    double lon1;
    double lat2;
    double lon2;
    double distance;    /* distance in meters */
    double ib;          /* initial bearina, radiansg */
    double fb;          /* final bearina, radiansg */
};

// tests for earth_distance_and_bearings()
// Online distance calculators give different answers.
// This seems to match the "Vincenty Formula"
struct test4 tests4[] = {
    // zero
    {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0},
    // about 1 cm
    {0.00000009, 0.0, 0.0, 0.0, 0.0100, GPS_PI, GPS_PI},
    {0.0, 0.00000009, 0.0, 0.0, 0.0100, -(GPS_PI / 2), -(GPS_PI / 2)},
    {0.0, 0.0, 0.00000009, 0.0, 0.0100, -GPS_PI, -GPS_PI},
    {0.0, 0.0, 0.0, 0.00000009, 0.0100, GPS_PI / 2, GPS_PI / 2},
    // one degree
    {1.0, 0.0, 0.0, 0.0, 110574.3886, GPS_PI, GPS_PI},
    {0.0, 1.0, 0.0, 0.0, 111319.4908, -(GPS_PI / 2), -(GPS_PI / 2)},
    {0.0, 0.0, 1.0, 0.0, 110574.3886, -GPS_PI, -GPS_PI},
    {0.0, 0.0, 0.0, 1.0, 111319.4908, GPS_PI / 2, GPS_PI / 2},
    // 90 degrees
    {90.0, 0.0, 0.0, 0.0, 10001965.7293, GPS_PI, GPS_PI},
    {0.0, 90.0, 0.0, 0.0, 10018754.1714, -(GPS_PI / 2), -(GPS_PI / 2)},
    {0.0, 0.0, 90.0, 0.0, 10001965.7293, -GPS_PI, -GPS_PI},
    {0.0, 0.0, 0.0, 90.0, 10018754.1714, GPS_PI / 2, GPS_PI / 2},
};

int main(int argc, char **argv)
{
    int verbose = 0;
    size_t i;
    int fail_count = 0;
    int option;

    while ((option = getopt(argc, argv, "h?vV")) != -1) {
	switch (option) {
	default:
		fail_count = 1;
		/* FALLTHROUGH */
	case '?':
		/* FALLTHROUGH */
	case 'h':
	    (void)fputs("usage: test_gpsdclient [-v] [-V]\n", stderr);
	    exit(fail_count);
	case 'V':
	    (void)fprintf( stderr, "test_gpsdclient %s\n",
		VERSION);
	    exit(EXIT_SUCCESS);
	case 'v':
	    verbose = 1;
	    break;
	}
    }

    if (0 < verbose)
	printf("wgs84_separation() tests\n");

    for (i = 0; i < (sizeof(tests3)/sizeof(struct test3)); i++) {
	double result;

	result = wgs84_separation(tests3[i].lat, tests3[i].lon);

	if (0 == isfinite(result) ||
	    0.01 < fabs(tests3[i].separation - result)) {
	    printf("ERROR: %.2f %.2f separation was %.2f s/b %.2f, %s\n",
	    tests3[i].lat, tests3[i].lon,
	    result, tests3[i].separation, tests3[i].desc);
	    fail_count++;
	} else if (0 < verbose) {
	    printf("%.2f %.2f separation was %.2f s/b %.2f, %s\n",
	    tests3[i].lat, tests3[i].lon,
	    result, tests3[i].separation, tests3[i].desc);
	}
    }

    if (0 < verbose)
	printf("mag_var() tests\n");

    for (i = 0; i < (sizeof(tests3)/sizeof(struct test3)); i++) {
	double result;

	result = mag_var(tests3[i].lat, tests3[i].lon);

	if (0 == isfinite(result) ||
	    0.01 < fabs(tests3[i].variation - result)) {
	    printf("ERROR: %.2f %.2f mag_var was %.2f s/b %.2f, %s\n",
	    tests3[i].lat, tests3[i].lon,
	    result, tests3[i].variation, tests3[i].desc);
	    fail_count++;
	} else if (0 < verbose) {
	    printf("%.2f %.2f mag_var was %.2f s/b %.2f, %s\n",
	           tests3[i].lat, tests3[i].lon,
	           result, tests3[i].variation, tests3[i].desc);
	}
    }

    for (i = 0; i < (sizeof(tests4)/sizeof(struct test4)); i++) {
	double result, ib, fb;

        result = earth_distance_and_bearings(tests4[i].lat1, tests4[i].lon1,
                                            tests4[i].lat2, tests4[i].lon2,
                                            &ib, &fb);
	if (0 == isfinite(result) ||
	    0.001 < fabs(tests4[i].distance - result)) {
	    printf("ERROR earth_distance_and bearings(%.8f, %.8f, %.8f, "
                   "%.8f,,) = %.4f, %.2f, %.2f s/b %.4f, %.2f, %.2f\n",
		   tests4[i].lat1, tests4[i].lon1,
		   tests4[i].lat2, tests4[i].lon2,
                   result, ib, fb,
		   tests4[i].distance, tests4[i].ib, tests4[i].fb);
	} else if (0 < verbose) {
	    printf("earth_distance_and_bearings(%.8f, %.8f, %.8f, %.8f) = "
                   "%.4f, %.2f, %.2f\n",
		   tests4[i].lat1, tests4[i].lon1,
		   tests4[i].lat2, tests4[i].lon2,
		   tests4[i].distance, tests4[i].ib, tests4[i].fb);
        }
    }

    exit(fail_count);
}
