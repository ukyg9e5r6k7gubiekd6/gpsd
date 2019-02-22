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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>            /* for getopt() */
#include "../gpsdclient.h"
#include "../revision.h"

struct test {
    double deg;
    char dd[20];
    char ddmm[20];
    char ddmmss[20];
};

struct test tests[] = {
    /* 1.999999995 sec */
    {(1.999999995),
     "  2.00000000",            /* rounded up */
     "  2 00.000000'",          /* rounded up */
     "  1 59' 59.99998\""},
    /* 3.999999999 sec */
    {(3.999999994),
     "  3.99999999",            /* not rounded up */
     "  4 00.000000'",          /* rounded up */
     "  3 59' 59.99998\""},
    /* 5 degree, 1.99999960 arcmin */
    {(5.0 + 1.999999600/60.0),
     "  5.03333333",
     "  5 02.000000'",          /* rounded up */
     "  5 01' 59.99998\""},
    /* 6 degree, 1.99999940 arcmin */
    {(6.0 + 1.999999400/60.0),
     "  6.03333332",
     "  6 01.999999'",          /* not rounded up */
     "  6 01' 59.99996\""},
    /* 7 degree, 59.99999960 arcmin */
    {(7.0 + 59.999999600/60.0),
     "  7.99999999",
     "  8 00.000000'",          /* rounded up */
     "  7 59' 59.99998\""},
    /* 9 degree, 59.99999940 arcmin */
    {(9.0 + 59.999999400/60.0),
     "  9.99999999",
     "  9 59.999999'",          /* not rounded up */
     "  9 59' 59.99996\""},
    /* 11 degree, 1 arcminute, 1.99999600 arcsec */
    {(11.0 + 1.0/60.0 + 1.99999600/3600.0),
     " 11.01722222",
     " 11 01.033333'",
     " 11 01' 02.00000\""},     /* rounded up */
    /* 12 deg, 2 min, 2.99999400 sec */
    {(12.0 + 2.0/60.0 + 2.99999400/3600.0),
     " 12.03416667",
     " 12 02.050000'",
     " 12 02' 02.99999\""},     /* not rounded up */
    /* -44.99999999999 */
    /* nan because not positive degrees */
    {-44.99999999999,
     "nan",
     "nan",
     "nan"},
    /* 359.99999999999 */
    {359.99999999999,
     "  0.00000000",         /* rounded up, and rolled over */
     "  0 00.000000'",
     "  0 00' 00.00000\""},
};


int main(int argc, char **argv)
{
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
	 s = deg_to_str (deg_dd, tests[i].deg);
	 if (0 != strcmp(s, tests[i].dd)) {
	     printf("ERROR: %s s/b %s\n", s, tests[i].dd);
	     fail_count++;
         }
         if (0 < verbose) {
	     printf("%s s/b %s\n", s, tests[i].dd);
         }
	 s = deg_to_str (deg_ddmm, tests[i].deg);
	 if (0 != strcmp(s, tests[i].ddmm)) {
	     printf("ERROR: %s s/b %s\n", s, tests[i].ddmm);
	     fail_count++;
         }
         if (0 < verbose) {
	     printf("%s s/b %s\n", s, tests[i].ddmm);
         }
	 s = deg_to_str (deg_ddmmss, tests[i].deg);
	 if (0 != strcmp(s, tests[i].ddmmss)) {
	     printf("ERROR: %s s/b %s\n", s, tests[i].ddmmss);
	     fail_count++;
         }
         if (0 < verbose) {
	     printf("%s s/b %s\n", s, tests[i].ddmmss);
         }
     }
     exit(fail_count);

}

