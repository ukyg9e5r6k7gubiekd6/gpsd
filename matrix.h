/*
 * matrix.h - matrix-algebra prototypes
 *
 * This file is Copyright (c)2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 */

extern bool matrix_invert(double mat[4][4], double inverse[4][4]);
extern void matrix_symmetrize(double mat[4][4], double inverse[4][4]);

/* end */
