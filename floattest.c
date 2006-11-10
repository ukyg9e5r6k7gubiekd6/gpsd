/* $Id$ */
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
 * compile with: gcc -O -o floattest floattest.c
 *     (use whatever -O level you like)
 */

int main(void) {
	float  a, b, c, d, e, f, g;
	double A, B, C, D, E, F, G;
	const char *reference = "3.00 5.00 7.00 11.00 12.00 132.00 38.00";
	char try1[64], try2[64]; 

	a = 3.0; b = 5.0 ; c = 7.0; d = 11.0;
	g = a + b * c; /* multiply and add */
	e = b + c; /* add */
	f = d * e; /* multiply */
	sprintf(try1, 
		"%.2f %.2f %.2f %.2f %.2f %.2f %.2f", a, b, c, d, e, f, g);

	A = 3.0; B = 5.0 ; C = 7.0; D = 11.0;
	G = A + B * C; /* multiply and add */
	E = B + C; /* add */
	F = D * E; /* multiply */
	sprintf(try2, 
		"%.2f %.2f %.2f %.2f %.2f %.2f %.2f", A, B, C, D, E, F, G);

	puts("Floating Point test - the next 3 lines should be the same");
	puts(reference);
	puts(try1);
	puts(try2);

	return strcmp(try1, reference) || strcmp(try2, reference);
}
