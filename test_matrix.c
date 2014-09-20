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

#include "matrix.h"

/* Macro for declaring function arguments unused. */
#if defined(__GNUC__)
#  define UNUSED __attribute__((unused)) /* Flag variable as unused */
#else /* not __GNUC__ */
#  define UNUSED
#endif

static double a[4][4] = {
	{1, 2, 3, 4}, 
	{5, 6, 7, 8},
	{9, 10, 11, 12},
	{13, 14, 15, 16}};
static double ainv[4][4] = {
    {-0.0028097262031538, 0.0073561782338441, 0.0040563991615249, -0.0086028511922152},
    {-0.0018928965221056, 0.00016424848911584, 0.0070451831437969, -0.0053165351108071},
    {0.031623219351165,	0.00010097243183351, -0.020259108723076, -0.011465083059922},
    {0.031734962175727, -0.10943930681752, -0.52209748013199,1.5998018247738},
};

int main(int argc UNUSED, char *argv[] UNUSED)
{
    double inv[4][4];

    matrix_invert(a, inv);
    printf("%f %f %f %f\n",
	   inv[0][0] - ainv[0][0],
	   inv[1][1] - ainv[1][1],
	   inv[2][2] - ainv[2][2],
	   inv[3][3] - ainv[3][3]
	);

    exit(0);
}
