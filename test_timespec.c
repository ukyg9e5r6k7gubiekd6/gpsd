/*
 * Unit test for timespec's
 *
 */
#include <stdlib.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>     /* for time_t */
#include <math.h>
#include <unistd.h>

#include "compiler.h"
#include "revision.h"
#include "timespec.h"

#define TS_ZERO         {0,0}
#define TS_ZERO_ONE     {0,1}
#define TS_ZERO_TWO     {0,2}
#define TS_ZERO_NINES   {0,999999999}
#define TS_ONE          {1,0}
#define TS_ONE_ONE      {1,1}
#define TS_TWO          {2,0}
#define TS_N_ZERO_ONE   {0,-1}
#define TS_N_ZERO_TWO   {0,-2}
#define TS_N_ZERO_NINES {0,-999999999}
#define TS_N_ONE        {-1,0}
/* Dec 31, 23:59 2037 GMT */
#define TS_2037         {2145916799, 0}
#define TS_2037_ONE     {2145916799, 1}
#define TS_2037_TWO     {2145916799, 2}
#define TS_2037_X       {2145916799, 123456789}
#define TS_2037_NINES   {2145916799, 999999999}

struct subtract_test {
	struct timespec a;
	struct timespec b;
	struct timespec c;
	bool last;
};

struct subtract_test subtract_tests[] = {
	{ TS_ZERO,        TS_ZERO,        TS_ZERO,         0},
	{ TS_ONE,         TS_ONE,         TS_ZERO,         0},
	{ TS_ZERO_ONE,    TS_ZERO_ONE,    TS_ZERO,         0},
	{ TS_ONE_ONE,     TS_ONE_ONE,     TS_ZERO,         0},
	{ TS_N_ONE,       TS_N_ONE,       TS_ZERO,         0},
	{ TS_N_ZERO_ONE,  TS_N_ZERO_ONE,  TS_ZERO,         0},
	{ TS_ZERO_NINES,  TS_ZERO_NINES,  TS_ZERO,         0},
	{ TS_ZERO,        TS_N_ONE,       TS_ONE,          0},
	{ TS_ONE,         TS_ZERO,        TS_ONE,          0},
	{ TS_TWO,         TS_ONE,         TS_ONE,          0},
	{ TS_ONE_ONE,     TS_ONE,         TS_ZERO_ONE,     0},
	{ TS_ONE,         TS_ZERO_NINES,  TS_ZERO_ONE,     0},
	{ TS_ZERO_TWO,    TS_ZERO_ONE,    TS_ZERO_ONE,     0},
	{ TS_2037_ONE,    TS_2037,        TS_ZERO_ONE,     0},
	{ TS_ONE_ONE,     TS_ZERO_NINES,  TS_ZERO_TWO,     0},
	{ TS_2037_NINES,  TS_2037,        TS_ZERO_NINES,   0},
	{ TS_ZERO,        TS_ONE,         TS_N_ONE,        0},
	{ TS_ONE,         TS_TWO,         TS_N_ONE,        0},
	{ TS_ZERO,        TS_ZERO_ONE,    TS_N_ZERO_ONE,   0},
	{ TS_ONE,         TS_ONE_ONE,     TS_N_ZERO_ONE,   0},
	{ TS_ZERO_ONE,    TS_ZERO_TWO,    TS_N_ZERO_ONE,   0},
	{ TS_2037,        TS_2037_ONE,    TS_N_ZERO_ONE,   0},
	{ TS_ZERO_NINES,  TS_ONE_ONE,     TS_N_ZERO_TWO,   0},
	{ TS_2037,        TS_2037_NINES,  TS_N_ZERO_NINES, 1},
};

struct format_test {
	struct timespec input;
	char *expected;
	bool last;
};

struct format_test format_tests[] = {
	{ TS_ZERO,         " 0.000000000", 0},
	{ TS_ZERO_ONE,     " 0.000000001", 0},
	{ TS_ZERO_TWO,     " 0.000000002", 0},
	{ TS_ZERO_NINES,   " 0.999999999", 0},
	{ TS_ONE,          " 1.000000000", 0},
	{ TS_ONE_ONE,      " 1.000000001", 0},
	{ TS_TWO,          " 2.000000000", 0},
	{ TS_N_ZERO_ONE,   "-0.000000001", 0},
	{ TS_N_ZERO_TWO,   "-0.000000002", 0},
	{ TS_N_ZERO_NINES, "-0.999999999", 0},
	{ TS_N_ONE,        "-1.000000000", 0},
	{ { -1, 1},        "-1.000000001", 0},
	{ { -1, -1},       "-1.000000001", 0},
	{ TS_2037,         " 2145916799.000000000", 0},
	{ TS_2037_ONE,     " 2145916799.000000001", 0},
	{ TS_2037_NINES,   " 2145916799.999999999", 1},
};

static int test_subtract( int verbose )
{
    struct subtract_test *p = subtract_tests;
    int fail_count = 0;

    while ( 1 ) {
	char buf_a[TIMESPEC_LEN];
	char buf_b[TIMESPEC_LEN];
	char buf_c[TIMESPEC_LEN];
	char buf_r[TIMESPEC_LEN];
	struct timespec r;

        TS_SUB(&r, &p->a, &p->b);
        timespec_str( &p->a, buf_a, sizeof(buf_a) );
        timespec_str( &p->b, buf_b, sizeof(buf_b) );
        timespec_str( &p->c, buf_c, sizeof(buf_c) );
        timespec_str( &r,    buf_r, sizeof(buf_r) );
	if ( (p->c.tv_sec != r.tv_sec) || (p->c.tv_nsec != r.tv_nsec) ) {
		printf("%21s - %21s = %21s, FAIL s/b %21s\n", 
		buf_a, buf_b, buf_r, buf_c);
		fail_count++;
	} else if ( verbose ) {
		printf("%21s - %21s = %21s\n", buf_a, buf_b, buf_r);
	}
		
	
	if ( p->last ) {
		break;
	}
	p++;
    };

    if ( fail_count ) {
	printf("subtract test failed %d tests\n", fail_count );
    } else {
	puts("subtract test succeeded\n");
    }
    return fail_count;
}

static int test_format(int verbose )
{
    struct format_test *p = format_tests;
    int fail_count = 0;

    while ( 1 ) {
	char buf[TIMESPEC_LEN];
	int fail;

        timespec_str( &p->input, buf, sizeof(buf) );
	fail = strncmp( buf, p->expected, TIMESPEC_LEN);
	if ( fail ) {
		printf("%21s, FAIL s/b: %21s\n", buf,  p->expected);
		fail_count++;
	} else if ( verbose )  {
		printf("%21s\n", buf);
	}
	
	if ( p->last ) {
		break;
	}
	p++;
    };

    if ( fail_count ) {
	printf("timespec_str test failed %d tests\n", fail_count );
    } else {
	puts("timespec_str test succeeded\n");
    }
    return fail_count;
}

struct timespec exs[] = {
	TS_ZERO_ONE,
	TS_ZERO_TWO,
	TS_ZERO_NINES,
	TS_ONE,
	TS_ONE_ONE,
	TS_TWO,
	TS_2037,
	TS_2037_ONE,
	TS_2037_TWO,
	TS_2037_X,
	TS_2037_NINES,
	TS_ZERO,
};

static void ex_precision(void)
{
	float f;
	double d;
	char buf[TIMESPEC_LEN];
	struct timespec *v = exs;

	puts( "\nPrecision examples:\n\n");
	printf( "\n%10stimespec%14sdouble%16sfloat\n\n", "", "", "");

	while ( 1 ) {
	    d = TSTONS( v );
	    f = (float) d;
	    timespec_str( v, buf, sizeof(buf) );
	    printf( "%21s %21.9f %21.9f \n", buf, d, f);

	    if ( ( 0 == v->tv_sec ) && ( 0 == v->tv_nsec) ) {
		/* done */
		break;
	    }
	    v++;
	}

}

int main(int argc, char *argv[])
{
    int fail_count = 0;
    int verbose = 0;
    int option;

    while ((option = getopt(argc, argv, "h?vV")) != -1) {
	switch (option) {
	default:
		fail_count = 1;
		/* FALL THROUGH! */
	case '?':
	case 'h':
	    (void)fputs("usage: test_timespec [-v] [-V]\n", stderr);
	    exit(fail_count);
	case 'V':
	    (void)fprintf( stderr, "test_timespec %s\n", 
		VERSION);
	    exit(EXIT_SUCCESS);
	case 'v':
	    verbose = 1;
	    break;
	}
    }


    fail_count = test_format( verbose );
    fail_count += test_subtract( verbose );

    if ( fail_count ) {
	printf("timespec tests failed %d tests\n", fail_count );
	exit(1);
    }
    printf("timespec tests succeeded\n");

    if ( verbose ) {
	ex_precision();
    }
    exit(0);
}
