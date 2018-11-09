/*
 * Copyright (c) 2006 Chris Kuethe <chris.kuethe@gmail.com>
 * Copyright (c) 2009 BBN Technologies (Greg Troxel)
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
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
