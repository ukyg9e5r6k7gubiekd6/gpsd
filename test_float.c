/*
 * This file is Copyright (c) 2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 */
#include <stdio.h>

/*
 * Copyright (c) 2006 Chris Kuethe <chris.kuethe@gmail.com>
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
 * this simple program tests to see whether your system can do proper
 * single and double precision floating point. This is apparently Very
 * Hard To Do(tm) on embedded systems, judging by the number of broken
 * ARM toolchains I've seen... :(
 *
 * compile with: gcc -O -o test_float test_float.c
 *     (use whatever -O level you like)
 */

int main(void);
int test_single(void);
int test_double(void);

int main(void) {
	int i, j;

	if ((i = test_single()))
		printf("WARNING: Single-precision "
			"floating point math might be broken\n");

	if ((j = test_double()))
		printf("WARNING: Double-precision "
			"floating point math might be broken\n");

	i += j;
	if (i == 0)
		printf("floating point math appears to work\n");
	return i;
}

int test_single(void) {
	static float f;
	static int i;
	static int e = 0;

	/* addition test */
	f = 1.0;
	for(i = 0; i < 10; i++)
		f += (1<<i);
	if (f != 1024.0) {
		printf("s1 ");
		e++;
	}

	/* subtraction test */
	f = 1024.0;
	for(i = 0; i < 10; i++)
		f -= (1<<i);
	if (f != 1.0) {
		printf("s2 ");
		e++;
	}

	/* multiplication test */
	f = 1.0;
	for(i = 1; i < 10; i++)
		f *= i;
	if (f != 362880.0) {
		printf("s3 ");
		e++;
	}

	/* division test */
	f = 362880.0;
	for(i = 1; i < 10; i++)
		f /= i;
	if (f != 1.0) {
		printf("s4 ");
		e++;
	}

	/* multiply-accumulate test */
	f = 0.5;
	for(i = 1; i < 1000000; i++) {
		f += 2.0;
		f *= 0.5;
	}
	if (f != 2.0) {
		printf("s5 ");
		e++;
	}

	/* divide-subtract test */
	f = 2.0;
	for(i = 1; i < 1000000; i++) {
		f /= 0.5;
		f -= 2.0;
	}
	if (f != 2.0) {
		printf("s6 ");
		e++;
	}

	/* add-multiply-subtract-divide test */
	f = 1000000.0;
	for(i = 1; i < 1000000; i++)
		f = ((((f + 1.5) * 0.5) - 1.25) / 0.5);
	if (f != 1.0) {
		printf("s7 ");
		e++;
	}

	/* multiply-add-divide-subtract test */
	f = 1.0;
	for(i = 1; i < 1000000; i++)
		f = ((((f * 5.0) + 3.0) / 2.0) - 3.0);
	if (f != 1.0)
		printf("s8 ");

	/* subtract-divide-add-multiply test */
	f = 8.0;
	for(i = 1; i < 1000000; i++)
		f = ((((f - 5.0) / 2.0) + 2.5) * 2.0);
	if (f != 8.0) {
		printf("s9 ");
		e++;
	}

	/* divide-subtract-multiply-add test */
	f = 42.0;
	for(i = 1; i < 1000000; i++)
		f = ((((f / 6.0) - 5.0) * 19.75 ) + 2.5);
	if (f != 42.0) {
		printf("s10 ");
		e++;
	}
	if (e) {
		printf("\n");
		return 1;
	}
	return 0;
}


int test_double(void) {
	static double f;
	static int i;
	static int e = 0;

	/* addition test */
	f = 1.0;
	for(i = 0; i < 10; i++)
		f += (1<<i);
	if (f != 1024.0) {
		printf("d1 ");
		e++;
	}

	/* subtraction test */
	f = 1024.0;
	for(i = 0; i < 10; i++)
		f -= (1<<i);
	if (f != 1.0) {
		printf("d2 ");
		e++;
	}

	/* multiplication test */
	f = 1.0;
	for(i = 1; i < 10; i++)
		f *= i;
	if (f != 362880.0) {
		printf("d3 ");
		e++;
	}

	/* division test */
	f = 362880.0;
	for(i = 1; i < 10; i++)
		f /= i;
	if (f != 1.0) {
		printf("d4 ");
		e++;
	}

	/* multiply-accumulate test */
	f = 0.5;
	for(i = 1; i < 1000000; i++) {
		f += 2.0;
		f *= 0.5;
	}
	if (f != 2.0) {
		printf("d5 ");
		e++;
	}

	/* divide-subtract test */
	f = 2.0;
	for(i = 1; i < 1000000; i++) {
		f /= 0.5;
		f -= 2.0;
	}
	if (f != 2.0) {
		printf("d6 ");
		e++;
	}

	/* add-multiply-subtract-divide test */
	f = 1000000.0;
	for(i = 1; i < 1000000; i++)
		f = ((((f + 1.5) * 0.5) - 1.25) / 0.5);
	if (f != 1.0) {
		printf("d7 ");
		e++;
	}

	/* multiply-add-divide-subtract test */
	f = 1.0;
	for(i = 1; i < 1000000; i++)
		f = ((((f * 5.0) + 3.0) / 2.0) - 3.0);
	if (f != 1.0)
		printf("d8 ");

	/* subtract-divide-add-multiply test */
	f = 8.0;
	for(i = 1; i < 1000000; i++)
		f = ((((f - 5.0) / 2.0) + 2.5) * 2.0);
	if (f != 8.0) {
		printf("d9 ");
		e++;
	}

	/* divide-subtract-multiply-add test */
	f = 42.0;
	for(i = 1; i < 1000000; i++)
		f = ((((f / 6.0) - 5.0) * 19.75 ) + 2.5);
	if (f != 42.0) {
		printf("d10 ");
		e++;
	}
	if (e) {
		printf("\n");
		return 1;
	}
	return 0;
}
