/*
 * Copyright (c) 2006 Chris Kuethe <chris.kuethe@gmail.com>
 * Copyright (c) 2009 BBN Technologies (Greg Troxel)
 *
 * This file is Copyright (c)2005-2019 by the GPSD project
 * SPDX-License-Identifier: BSD-2-clause
 */

/*
 * This program provides a way to check sin/cos.
 */

#include <stdio.h>
#include <math.h>

int test_trig(void);

int main(void) {
	test_trig();

	/* For now, no evaluation. */
	return 0;
}

#define Deg2Rad(x) ((x) * (2 * M_PI / 360.0))

int test_trig(void) {
	int i;
	double arg;
	double res;

	for (i = 0; i <= 360; i++) {
		arg = Deg2Rad(i);
		res = sin(arg);
		printf("sin(%.30f) = %.30f\n", arg, res);
	}

	for (i = 0; i <= 360; i++) {
		arg = Deg2Rad(i);
		res = cos(arg);
		printf("cos(%.30f) = %.30f\n", arg, res);
	}

	/* Always claim success. */
	return 0;
}
