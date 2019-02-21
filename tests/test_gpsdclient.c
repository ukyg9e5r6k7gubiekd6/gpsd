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
    /* 1 degree, 1 arcminute, 1.999 arcsec */
    {(1.0 + 1.0/60.0 + 1.999/3600.0),
     "  1.01722194",
     "  1 01.033316'",
     "  1 01' 01.99899\""},
    /* 1 deg, 2 min, 2.0999 sec */
    {(1.0 + 2.0/60.0 + 2.999/3600.0),
     "  1.03416638",
     "  1 02.049983'",
     "  1 02' 02.99900\""},
    /* 44.99999994, should not be rounded up */
    {44.99999994,
     " 44.99999994",
     " 44 59.999996'",
     " 44 59' 59.99978\""},
    /* 44.99999999999 */
    /* FIXME: s/b rounded */
    {44.99999999999,
     " 44.99999999",
     " 44 59.999999'",
     " 44 59' 59.99999\""},
    /* -44.99999999999 */
    /* FIXME: should not be nan? */
    {-44.99999999999,
     "nan",
     "nan",
     "nan"},
    /* 359.99999999999 */
    /* FIXME: s/b rounded */
    {359.99999999999,
     "359.99999999",
     "359 59.999999'",
     "359 59' 59.99999\""},
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

