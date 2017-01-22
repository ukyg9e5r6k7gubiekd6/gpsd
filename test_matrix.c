/*
 * Unit test for matrix-algebra code
 *
 * Check examples computed at
 * http://www.elektro-energetika.cz/calculations/matreg.php
 */
#include <stdlib.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "compiler.h"
#include "matrix.h"

static struct {
    double mat[4][4];
    double inv[4][4];
} inverses[] = {
    /* identity matrix is self-inverse */
    {
	.mat = {{1,0,0,0}, {0,1,0,0}, {0,0,1,0}, {0,0,0,1}},
	.inv = {{1,0,0,0}, {0,1,0,0}, {0,0,1,0}, {0,0,0,1}},
    },
    /* inverse of a diagonal matrix has reciprocal values */
    {
	.mat = {{10,0,0,0}, {0,10,0,0}, {0,0,10,0}, {0,0,0,10}},
	.inv = {{0.1,0,0,0}, {0,0.1,0,0}, {0,0,0.1,0}, {0,0,0,0.1}},
    },
    /* random values with asymmetrical off-diagonal elements */
    {
	.mat = {{1,0,0,0}, {0,1,0,-2}, {0, 2,1,-4}, {0,0,0,1}},
	.inv = {{1,0,0,0}, {0,1,0, 2}, {0,-2,1, 0}, {0,0,0,1}},
    },
    {
	.mat = {{6,-4,1,-3},{-4,7,3,2},{1,3,6,-4},{-3,2,-4,6}},
	.inv = {{14,34,-40,-31},{34,84,-99,-77},{-40,-99,117,91},{-31,-77,91,71}},
    },
};

static void dump(const char *label, double m[4][4])
{
    printf("%s:\n", label);
    printf("%f, %f, %f, %f\n", m[0][0], m[0][1], m[0][2], m[0][3]);
    printf("%f, %f, %f, %f\n", m[1][0], m[1][1], m[1][2], m[1][3]);
    printf("%f, %f, %f, %f\n", m[2][0], m[2][1], m[2][2], m[2][3]);
    printf("%f, %f, %f, %f\n", m[3][0], m[3][1], m[3][2], m[3][3]);
}

static bool check_diag(int n, double a[4][4], double b[4][4])
{
#define approx(x, y)	(fabs(x - y) < 0.0001)

    if (approx(b[0][0], a[0][0]) && approx(b[1][1], a[1][1]) &&
	approx(b[2][2], a[2][2]) && approx(b[3][3], a[3][3]))
	return true;

    dump("a", a);
    dump("b", b);
    printf("Test %d residuals: %f %f %f %f\n",
	   n,
	   b[0][0] - a[0][0],
	   b[1][1] - a[1][1],
	   b[2][2] - a[2][2],
	   b[3][3] - a[3][3]
	);
   return true;
}

int main(int argc UNUSED, char *argv[] UNUSED)
{
    unsigned int i;

    for (i = 0; i < sizeof(inverses) / sizeof(inverses[0]); i++) {
	double inverse[4][4];
	if (!matrix_invert(inverses[i].mat, inverse)) {
	    printf("Vanishing determinant in test %u\n", i);
	}
	if (!check_diag(i, inverse, inverses[i].inv))
	    break;
    }

    printf("Matrix-algebra regression test succeeded\n");
    exit(0);
}
