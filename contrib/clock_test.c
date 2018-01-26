/*
 * clock_test.  A simple program to test the latency of the clock_gettime() call
 *
 * Compile: gcc clock_test.c -lm -o clock_test
 *
 */

#include <getopt.h>             /* for getopt() */
#include <limits.h>             /* for LONG_MAX */
#include <math.h>               /* for pow(), sqrt() */
#include <stdio.h>              /* for printf() */
#include <stdlib.h>             /* for qsort() */
#include <time.h>               /* for time_t */

#define NUM_TESTS 101           /* default samples, make it odd for a clean median */
#define DELAY 10000000          /* default delay between samples in ns, 10 ms is good */

int compare_long( const void *ap, const void *bp)
{
        long a = *((long *)ap);
        long b = *((long *)bp);
	if ( a < b ) return -1;
	if ( a > b ) return 1;
        return 0;
}

int main(int argc, char **argv)
{
    int i;
    int opt;                   /* for getopts() */
    int verbose = 0;
    int samples = NUM_TESTS;
    long delay = DELAY;
    long *diffs = NULL;
    long min = LONG_MAX, max = 0, sum = 0, mean = 0, median = 0;
    double stddev = 0.0;
    
    while ((opt = getopt(argc, argv, "d:hvn:")) != -1) {
        switch (opt) {
        case 'd':
	    delay = atol(optarg);
	    break;
        case 'n':
	    samples = atoi(optarg);
            /* make odd, for a good median */
            if ( (samples & 1) == 0) {
                samples += 1;
            }
	    break;
        case 'v':
	    verbose = 1;
	    break;
        case 'h':
            /* fall through */
        default: /* '?' */
	    fprintf(stderr, "Usage: %s [-h] [-d nsec] [-n samples] [-v]\n\n", argv[0]);
	    fprintf(stderr, "-d nsec     : nano seconde paus between samples\n");
	    fprintf(stderr, "-h          : help\n");
	    fprintf(stderr, "-n samples  : Number of samples, default %d\n", NUM_TESTS);
	    fprintf(stderr, "-v          : verbose\n");
	    exit(EXIT_FAILURE);
        }
    }

    diffs = alloca( sizeof(long) * (samples + 2));  /* add 2 for off by one errors */

    /* collect test data */
    for ( i = 0 ; i < samples; i++ ) {
	struct timespec now, now1, sleep, sleep1;

	(void)clock_gettime(CLOCK_REALTIME, &now);
	(void)clock_gettime(CLOCK_REALTIME, &now1);
	diffs[i] = now1.tv_nsec - now.tv_nsec;
        if ( now1.tv_sec != now.tv_sec ) {
            /* clock roll over, fix it */
            diffs[i] += 1000000000;  /* add one second */
        }
        /* instead of hammering, sleep between tests, let the cache get cold */
        sleep.tv_sec = 0;
        sleep.tv_nsec = delay;    /* sleep delay */
        /* sleep1 unused, should not be returning early */
        nanosleep(&sleep, &sleep1);
    }

    /* analyze test data */

    /* print diffs, calculate min and max */
    for ( i = 0 ; i < samples; i++ ) {
        if ( verbose > 0 ) {
	    printf("diff %ld\n", diffs[i]);
        }
        sum += diffs[i];
        if ( diffs[i] < min ) min = diffs[i];
        if ( diffs[i] > max ) max = diffs[i];
    }
    mean = sum / (samples - 1);

    qsort( diffs, samples, sizeof(long), compare_long);
    median = diffs[(samples / 2) + 1];


    for ( i = 0 ; i < samples; i++ ) {
        stddev += pow(diffs[i] - mean, 2);
    }
    stddev = sqrt(stddev/samples);

    printf("samples %d, delay %ld ns\n", samples, delay);
    printf("min %ld ns, max %ld ns, mean %ld ns, median %ld ns, StdDev %ld ns\n",
           min, max, mean, median, (long)stddev);
}
