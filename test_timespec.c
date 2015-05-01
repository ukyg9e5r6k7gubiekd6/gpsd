/*
 * Unit test for timespec's
 *
 */
#include <stdlib.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>   /* required by C99, for int32_t */
#include <time.h>     /* for time_t */
#include <math.h>
#include <unistd.h>

#include "compiler.h"
#include "revision.h"
#include "ppsthread.h"
#include "timespec.h"

#define TS_ZERO         {0,0}
#define TS_ZERO_ONE     {0,1}
#define TS_ZERO_TWO     {0,2}
#define TS_ZERO_TREES   {0,333333333}
#define TS_ZERO_SIXS7   {0,666666667}
#define TS_ZERO_NINES   {0,999999999}
#define TS_ONE          {1,0}
#define TS_ONE_ONE      {1,1}
#define TS_TWO          {2,0}
#define TS_N_ZERO_ONE   {0,-1}
#define TS_N_ZERO_TWO   {0,-2}
#define TS_N_ZERO_TREES {0,-333333333}
#define TS_N_ZERO_NINES {0,-999999999}
#define TS_N_ONE        {-1,0}
/* Dec 31, 23:59 2037 GMT */
#define TS_2037         {2145916799, 0}
#define TS_2037_ONE     {2145916799, 1}
#define TS_2037_TWO     {2145916799, 2}
#define TS_2037_X       {2145916799, 123456789}
#define TS_2037_TREES   {2145916799, 333333333}
#define TS_2037_SIXS7   {2145916799, 666666667}
#define TS_2037_NINES   {2145916799, 999999999}
#define TS_N_2037_TREES {-2145916799, -333333333}
#define TS_N_2037_NINES {-2145916799, -999999999}

/* a 32 bit copy to force a 32 bit long */
#define timespec_diff_ns32(x, y)	(int32_t)((int32_t)(((x).tv_sec-(y).tv_sec)*1000000000)+(x).tv_nsec-(y).tv_nsec)

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
	{ TS_ZERO_TREES,  TS_ZERO_TREES,  TS_ZERO,         0},
	{ TS_ZERO_NINES,  TS_ZERO_NINES,  TS_ZERO,         0},
	{ TS_ZERO,        TS_N_ONE,       TS_ONE,          0},
	{ TS_ONE,         TS_ZERO,        TS_ONE,          0},
	{ TS_TWO,         TS_ONE,         TS_ONE,          0},
	{ TS_ONE_ONE,     TS_ONE,         TS_ZERO_ONE,     0},
	{ TS_ONE,         TS_ZERO_TREES,  TS_ZERO_SIXS7,   0},
	{ TS_ONE,         TS_ZERO_NINES,  TS_ZERO_ONE,     0},
	{ TS_ZERO_TWO,    TS_ZERO_ONE,    TS_ZERO_ONE,     0},
	{ TS_2037_ONE,    TS_2037,        TS_ZERO_ONE,     0},
	{ TS_ONE_ONE,     TS_ZERO_NINES,  TS_ZERO_TWO,     0},
	{ TS_2037_NINES,  TS_2037,        TS_ZERO_NINES,   0},
	{ TS_2037_TREES,  TS_ZERO,        TS_2037_TREES,   0},
	{ TS_2037_SIXS7,  TS_2037,        TS_ZERO_SIXS7,   0},
	{ TS_2037_TREES,  TS_2037,        TS_ZERO_TREES,   0},
	{ TS_2037_NINES,  TS_ZERO,        TS_2037_NINES,   0},
	{ TS_ZERO,        TS_ONE,         TS_N_ONE,        0},
	{ TS_ONE,         TS_TWO,         TS_N_ONE,        0},
	{ TS_ZERO,        TS_ZERO_ONE,    TS_N_ZERO_ONE,   0},
	{ TS_ONE,         TS_ONE_ONE,     TS_N_ZERO_ONE,   0},
	{ TS_ZERO_ONE,    TS_ZERO_TWO,    TS_N_ZERO_ONE,   0},
	{ TS_2037,        TS_2037_ONE,    TS_N_ZERO_ONE,   0},
	{ TS_ZERO_NINES,  TS_ONE_ONE,     TS_N_ZERO_TWO,   0},
	{ TS_2037,        TS_2037_NINES,  TS_N_ZERO_NINES, 0},
	{ TS_ZERO,        TS_2037_NINES,  TS_N_2037_NINES, 1},
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

static int ex_subtract_float( void )
{
    struct subtract_test *p = subtract_tests;
    int fail_count = 0;

    printf( "\n\nsubtract test examples using doubles/floats:\n"
            " TS:  TS_SUB()\n"
            " l:   timespec_to_ns() math\n"
            " l32: timespec_to_ns() math with 32 bit long\n"
            " d:   double float math\n"
            " f:   float math\n"
	    "\n");

    while ( 1 ) {
	char buf_a[TIMESPEC_LEN];
	char buf_b[TIMESPEC_LEN];
	char buf_c[TIMESPEC_LEN];
	char buf_r[TIMESPEC_LEN];
	struct timespec ts_r;
	float f_a, f_b, f_r;
	double d_a, d_b, d_r;
	long l;
	int32_t l32;  /* simulate a 32 bit long */

	/* timespec math */
        TS_SUB(&ts_r, &p->a, &p->b);

	/* float math */
	f_a = TSTONS( &p->a );
	f_b = TSTONS( &p->b );
	f_r = f_a - f_b;

	/* double float math */
	d_a = TSTONS( &p->a );
	d_b = TSTONS( &p->b );
	d_r = d_a - d_b;

	/* long math */
	l = timespec_diff_ns( p->a, p->b);
	l32 = timespec_diff_ns32( p->a, p->b);

        timespec_str( &p->a, buf_a, sizeof(buf_a) );
        timespec_str( &p->b, buf_b, sizeof(buf_b) );
        timespec_str( &p->c, buf_c, sizeof(buf_c) );
        timespec_str( &ts_r, buf_r, sizeof(buf_r) );

	printf(" TS;  %21s - %21s = %21s\n", buf_a, buf_b, buf_r);
	printf(" l;   %21s - %21s = %21ld\n", buf_a, buf_b, l);
	printf(" l32; %21s - %21s = %21ld\n", buf_a, buf_b, (long)l32);
	printf(" d;   %21.9f - %21.9f = %21.9f\n", d_a, d_b, d_r);
	printf(" f;   %21.9f - %21.9f = %21.9f\n", f_a, f_b, f_r);
	puts("\n");
		
	
	if ( p->last ) {
		break;
	}
	p++;
    };

    if ( fail_count ) {
	// printf("subtract test failed %d tests\n", fail_count );
    } else {
	// puts("subtract test succeeded\n");
    }
    return fail_count;
}


static void ex_precision(void)
{
	char buf[TIMESPEC_LEN];
	struct timespec *v = exs;

	puts( "\nPrecision examples:\n\n  Simple conversions\n");
	printf( "\n%10stimespec%14s32 bit long%11sdouble%16sfloat\n\n", "", "", "", "");

	while ( 1 ) {
	    float f;
	    double d;
	    int32_t l32;

	    d = TSTONS( v );
	    l32 = (int32_t)(v->tv_sec * 1000000000)+(int32_t)v->tv_nsec;
	    f = (float) d;
	    timespec_str( v, buf, sizeof(buf) );
	    printf( "%21s %21ld %21.9f %21.9f \n", buf, (long)l32, d, f);

	    if ( ( 0 == v->tv_sec ) && ( 0 == v->tv_nsec) ) {
		/* done */
		break;
	    }
	    v++;
	}

	printf( "\n\nSubtraction examples:\n");

        ex_subtract_float();
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
