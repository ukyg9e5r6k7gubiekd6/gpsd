/*
 * Checksum for the GNSS Receiver External Interface Specification (GREIS).
 *
 * This file is Copyright (c) 2017 Virgin Orbit
 * SPDX-License-Identifier: BSD-2-clause
 */

#include <limits.h>

#include "driver_greis.h"

static inline unsigned char greis_rotate_left(unsigned char val)
{
    /* left circular rotation by two bits */
    return (val << 2) | (val >> (CHAR_BIT - 2));
}

unsigned char greis_checksum(const unsigned char *src, int count)
{
    unsigned char res = 0;
    while (count--)
	res = greis_rotate_left(res) ^ *src++;
    return greis_rotate_left(res);
}
