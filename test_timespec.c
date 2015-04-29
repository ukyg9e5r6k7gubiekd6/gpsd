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

#include "compiler.h"
#include "timespec.h"

#define TS_ZERO         {0,0}
#define TS_ZERO_ONE     {0,1}
#define TS_ZERO_TWO     {0,2}
#define TS_ZERO_NINES   {0,999999999}
#define TS_ONE          {1,0}
#define TS_ONE_ONE      {1,1}
#define TS_TWO          {2,0}
#define TS_N_ONE        {-1,0}
#define TS_N_ZERO_ONE   {0,-1}

struct subtract_test {
	struct timespec a;
	struct timespec b;
	struct timespec c;
	bool last;
};

struct subtract_test subtract_tests[] = {
	{ TS_ZERO,        TS_ZERO,        TS_ZERO,       0},
	{ TS_ONE,         TS_ONE,         TS_ZERO,       0},
	{ TS_ZERO_ONE,    TS_ZERO_ONE,    TS_ZERO,       0},
	{ TS_ONE_ONE,     TS_ONE_ONE,     TS_ZERO,       0},
	{ TS_N_ONE,       TS_N_ONE,       TS_ZERO,       0},
	{ TS_N_ZERO_ONE,  TS_N_ZERO_ONE,  TS_ZERO,       0},
	{ TS_ZERO_NINES,  TS_ZERO_NINES,  TS_ZERO,       0},
	{ TS_ZERO,        TS_N_ONE,       TS_ONE,        0},
	{ TS_ONE,         TS_ZERO,        TS_ONE,        0},
	{ TS_TWO,         TS_ONE,         TS_ONE,        0},
	{ TS_ONE_ONE,     TS_ONE,         TS_ZERO_ONE,   0},
	{ TS_ONE,         TS_ZERO_NINES,  TS_ZERO_ONE,   0},
	{ TS_ZERO_TWO,    TS_ZERO_ONE,    TS_ZERO_ONE,   0},
	{ TS_ZERO,        TS_ONE,         TS_N_ONE,      0},
	{ TS_ONE,         TS_TWO,         TS_N_ONE,      0},
	{ TS_ZERO,        TS_ZERO_ONE,    TS_N_ZERO_ONE, 0},
	{ TS_ONE,         TS_ONE_ONE,     TS_N_ZERO_ONE, 0},
	{ TS_ZERO_ONE,    TS_ZERO_TWO,    TS_N_ZERO_ONE, 0},
	{ TS_ZERO_NINES,  TS_ONE,         TS_N_ZERO_ONE, 1},
};

struct format_test {
	struct timespec input;
	char *expected;
	bool last;
};

struct format_test format_tests[] = {
	{ TS_ZERO,   " 0.000000000", 0},
	{ { 0, 1},   " 0.000000001", 0},
	{ TS_ONE,    " 1.000000000", 0},
	{ { 1, 1},   " 1.000000001", 0},
	{ { 0, -1},  "-0.000000001", 0},
	{ TS_N_ONE,  "-1.000000000", 0},
	{ { -1, 1},  "-1.000000001", 0},
	{ { -1, -1}, "-1.000000001", 1},
};

static int test_subtract( void) 
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
	printf("%s - %s = %s", buf_a, buf_b, buf_r);
	if ( (p->c.tv_sec != r.tv_sec) || (p->c.tv_nsec != r.tv_nsec) ) {
		printf(", FAIL s/b: %s\n", buf_c);
		fail_count++;
	} else {
		printf("\n");
	}
		
	
	if ( p->last ) {
		break;
	}
	p++;
    };
    
    if ( fail_count ) {
	printf("subtract test failed %d tests\n", fail_count );
    } else {
	printf("subtract test succeeded\n");
    }
    return fail_count;
}

static int test_format(void) 
{
    struct format_test *p = format_tests;
    int fail_count = 0;

    while ( 1 ) {
	char buf[TIMESPEC_LEN];
	int fail;

        timespec_str( &p->input, buf, sizeof(buf) );
	printf("%s", buf);
	fail = strncmp( buf, p->expected, TIMESPEC_LEN);
	if ( fail ) {
		printf(", FAIL s/b: %s\n", p->expected);
		fail_count++;
	} else {
		printf("\n");
	}
	
	if ( p->last ) {
		break;
	}
	p++;
    };

    if ( fail_count ) {
	printf("timespec_str test failed %d tests\n", fail_count );
    } else {
	printf("timespec_str test succeeded\n");
    }
    return fail_count;
}

int main(int argc UNUSED, char *argv[] UNUSED)
{
    int fail_count = 0;

    fail_count = test_format();
    fail_count = test_subtract();

    if ( fail_count ) {
	printf("timespec tests failed %d tests\n", fail_count );
	exit(1);
    }
    printf("timespec tests succeeded\n");
    exit(0);
}
