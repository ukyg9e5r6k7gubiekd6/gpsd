/* test for gpsdclient.c: function deg_to_str
 *
 *  Consider rounding off also:
 *  dsec = (int)(fdsec * 10000.0 + 0.5);
 *
 * This file is Copyright (c) 2010 by the GPSD project
 * SPDX-License-Identifier: BSD-2-clause
*/

/* first so the #defs work */
#include "../gpsd_config.h"
#include <math.h>              /* for nan() */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>            /* for getopt() */
#include "../gpsdclient.h"
#include "../revision.h"

struct test {
    double deg;
    char dd[20];
    char dd2[20];
    char ddmm[20];
    char ddmm2[20];
    char ddmmss[20];
    char ddmmss2[20];
};

#define NANFLAG 9999

struct test tests[] = {
    /* 1.999999995 sec */
    {(1.999999995),
     "  2.00000000",               /* rounded up */
     "  2.00000000 E",             /* rounded up */
     "  2 00.000000'",             /* rounded up */
     "  2 00.000000' E",           /* rounded up */
     "  1 59' 59.99998\"",
     "  1 59' 59.99998\" N"},
    /* 3.999999999 sec */
    {(3.999999994),
     "  3.99999999",               /* not rounded up */
     "  3.99999999 E",             /* not rounded up */
     "  4 00.000000'",             /* rounded up */
     "  4 00.000000' E",           /* rounded up */
     "  3 59' 59.99998\"",
     "  3 59' 59.99998\" N"},
    /* 5 degree, 1.99999960 arcmin */
    {(5.0 + 1.999999600/60.0),
     "  5.03333333",
     "  5.03333333 E",
     "  5 02.000000'",             /* rounded up */
     "  5 02.000000' E",           /* rounded up */
     "  5 01' 59.99998\"",
     "  5 01' 59.99998\" N"},
    /* 6 degree, 1.99999940 arcmin */
    {(6.0 + 1.999999400/60.0),
     "  6.03333332",
     "  6.03333332 E",
     "  6 01.999999'",             /* not rounded up */
     "  6 01.999999' E",           /* not rounded up */
     "  6 01' 59.99996\"",
     "  6 01' 59.99996\" N"},
    /* 7 degree, 59.99999960 arcmin */
    {(7.0 + 59.999999600/60.0),
     "  7.99999999",
     "  7.99999999 E",
     "  8 00.000000'",             /* rounded up */
     "  8 00.000000' E",           /* rounded up */
     "  7 59' 59.99998\"",
     "  7 59' 59.99998\" N"},
    /* 9 degree, 59.99999940 arcmin */
    {(9.0 + 59.999999400/60.0),
     "  9.99999999",
     "  9.99999999 E",
     "  9 59.999999'",             /* not rounded up */
     "  9 59.999999' E",             /* not rounded up */
     "  9 59' 59.99996\"",
     "  9 59' 59.99996\" N"},
    /* 11 degree, 1 arcminute, 1.99999600 arcsec */
    {(11.0 + 1.0/60.0 + 1.99999600/3600.0),
     " 11.01722222",
     " 11.01722222 E",
     " 11 01.033333'",
     " 11 01.033333' E",
     " 11 01' 02.00000\"",        /* rounded up */
     " 11 01' 02.00000\" N"},     /* rounded up */
    /* 12 deg, 2 min, 2.99999400 sec */
    {(12.0 + 2.0/60.0 + 2.99999400/3600.0),
     " 12.03416667",
     " 12.03416667 E",
     " 12 02.050000'",
     " 12 02.050000' E",
     " 12 02' 02.99999\"",        /* not rounded up */
     " 12 02' 02.99999\" N"},     /* not rounded up */
    /* 13.00000001 sec, LSB of dd */
    {-13.00000001,
     " 13.00000001",
     " 13.00000001 W",
     " 13 00.000001'",
     " 13 00.000001' W",
     " 13 00' 00.00004\"",
     " 13 00' 00.00004\" S"},
    /* 14 deg, 0.000001 min, LSB of ddmm */
    {(14.0 + 0.000001/60.0),
     " 14.00000002",
     " 14.00000002 E",
     " 14 00.000001'",
     " 14 00.000001' E",
     " 14 00' 00.00006\"",
     " 14 00' 00.00006\" N"},
    /* 15 deg, 2 min, 2.00001 sec, LSB of ddmmss */
    {(15.0 + 2.0/60.0 + 2.00001/3600.0),
     " 15.03388889",
     " 15.03388889 E",
     " 15 02.033334'",
     " 15 02.033334' E",
     " 15 02' 02.00001\"",
     " 15 02' 02.00001\" N"},
    /* -44.99999999999 */
    /* fabs() */
    {-44.0,
     " 44.00000000",
     " 44.00000000 W",
     " 44 00.000000'",
     " 44 00.000000' W",
     " 44 00' 00.00000\"",
     " 44 00' 00.00000\" S"},
    /* 359.99999999999 */
    {359.99999999999,
     "  0.00000000",            /* rounded up, and rolled over */
     "  0.00000000 E",            /* rounded up, and rolled over */
     "  0 00.000000'",
     "  0 00.000000' E",
     "  0 00' 00.00000\"",
     "  0 00' 00.00000\" N"},
    /* 361 */
    /* nan because out of range */
    {361,
     "n/a",
     "n/a",
     "n/a",
     "n/a",
     "n/a",
     "n/a"},
    /* -361 */
    /* nan, just because */
    {NANFLAG,
     "n/a",
     "n/a",
     "n/a",
     "n/a",
     "n/a",
     "n/a"},
    /* FP_INFINITE */
    /* gcc too 'smart' to let us put a Nan here */
    {9999,
     "n/a",
     "n/a",
     "n/a",
     "n/a",
     "n/a",
     "n/a"},
};


struct test2 {
    double lat;
    double lon;
    char *maidenhead;
    char *name;
};

struct test2 tests2[] = {
    /* maidenhead
     * keep in sync with test_clienthelpers.py */
    {48.86471, 2.37305, "JN18eu", "Paris"},
    {41.93498, 12.43652, "JN61fw", "Rome"},
    {39.9771, -75.1685, "FM29jx", "Philadelphia"},
    {-23.4028, -50.9766, "GG46mo", "Sao Paulo"},
    {90, 180, "RR99xx", "North Pole"},
    {-90, -180, "AA00aa", "South Pole"},
};


int main(int argc, char **argv)
{
    char buf[20];
    char *s;
    unsigned int i;
    int verbose = 0;
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


    for (i = 0; i < (sizeof(tests)/sizeof(struct test)); i++) {
        if (NANFLAG == tests[i].deg) {
           /* make it a NaN */
           tests[i].deg = nan("a");
        }
        s = deg_to_str(deg_dd, tests[i].deg);
        if (0 != strcmp(s, tests[i].dd)) {
            printf("ERROR: %s s/b %s\n", s, tests[i].dd);
            fail_count++;
        } else if (0 < verbose) {
            printf("%s s/b %s\n", s, tests[i].dd);
        }
        s = deg_to_str2(deg_dd, tests[i].deg, buf,
                        sizeof(buf), " E", " W");
        if (0 != strcmp(s, tests[i].dd2)) {
            printf("ERROR: %s s/b %s\n", s, tests[i].dd2);
            fail_count++;
        } else if (0 < verbose) {
            printf("%s s/b %s\n", s, tests[i].dd2);
        }
        s = deg_to_str(deg_ddmm, tests[i].deg);
        if (0 != strcmp(s, tests[i].ddmm)) {
            printf("ERROR: %s s/b %s\n", s, tests[i].ddmm);
            fail_count++;
        } else if (0 < verbose) {
            printf("%s s/b %s\n", s, tests[i].ddmm);
        }
        s = deg_to_str2(deg_ddmm, tests[i].deg, buf,
                        sizeof(buf), " E", " W");
        if (0 != strcmp(s, tests[i].ddmm2)) {
            printf("ERROR: %s s/b %s\n", s, tests[i].ddmm2);
            fail_count++;
        } else if (0 < verbose) {
            printf("%s s/b %s\n", s, tests[i].ddmm2);
        }
        s = deg_to_str(deg_ddmmss, tests[i].deg);
        if (0 != strcmp(s, tests[i].ddmmss)) {
            printf("ERROR: %s s/b %s\n", s, tests[i].ddmmss);
            fail_count++;
        } else if (0 < verbose) {
            printf("%s s/b %s\n", s, tests[i].ddmmss);
        }
        s = deg_to_str2(deg_ddmmss, tests[i].deg, buf,
                        sizeof(buf), " N", " S");
        if (0 != strcmp(s, tests[i].ddmmss2)) {
            printf("ERROR: %s s/b %s\n", s, tests[i].ddmmss2);
            fail_count++;
        } else if (0 < verbose) {
            printf("%s s/b %s\n", s, tests[i].ddmmss2);
        }
    }

    for (i = 0; i < (sizeof(tests2)/sizeof(struct test2)); i++) {
        s = maidenhead(tests2[i].lat, tests2[i].lon);
        if (0 != strcmp(s, tests2[i].maidenhead)) {
            printf("ERROR: %s s/b %s\n", s, tests2[i].maidenhead);
            fail_count++;
        } else if (0 < verbose) {
            printf("%s s/b %s\n", s, tests2[i].maidenhead);
        }
    }

    exit(fail_count);
}

