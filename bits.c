/* bits.c - bitfield extraction code
 *
 * This file is Copyright (c)2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 *
 * Bitfield extraction functions.  In each, start is a bit index  - not
 * a byte index - and width is a bit width.  The width is bounded above by
 * 64 bits.
 *
 * The sbits() function assumes twos-complement arithmetic. ubits()
 * and sbits() assume no padding in integers.
 */
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>

#include "bits.h"

#define BITS_PER_BYTE	8

uint64_t ubits(char buf[], unsigned int start, unsigned int width, bool le)
/* extract a (zero-origin) bitfield from the buffer as an unsigned big-endian uint64_t */
{
    uint64_t fld = 0;
    unsigned int i;
    unsigned end;

    /*@i1@*/ assert(width <= sizeof(uint64_t) * BITS_PER_BYTE);
    for (i = start / BITS_PER_BYTE;
	 i < (start + width + BITS_PER_BYTE - 1) / BITS_PER_BYTE; i++) {
	fld <<= BITS_PER_BYTE;
	fld |= (unsigned char)buf[i];
    }

    end = (start + width) % BITS_PER_BYTE;
    if (end != 0) {
	fld >>= (BITS_PER_BYTE - end);
    }

    /*@ -shiftimplementation @*/
    fld &= ~(-1LL << width);
    /*@ +shiftimplementation @*/

    /* was extraction as a little-endian requested? */
    if (le)
    {
	unsigned int i;
	uint64_t reversed = 0;

	for (i = width; i; --i)
	{
	    reversed <<= 1;
	    if (fld & 1)
		reversed |= 1;
	    fld >>= 1;
	}
	fld = reversed;
    }

    return fld;
}

int64_t sbits(char buf[], unsigned int start, unsigned int width, bool le)
/* extract a bitfield from the buffer as a signed big-endian long */
{
    uint64_t fld = ubits(buf, start, width, le);

    /*@ +relaxtypes */
    if (fld & (1LL << (width - 1))) {
	/*@ -shiftimplementation @*/
	fld |= (-1LL << (width - 1));
	/*@ +shiftimplementation @*/
    }
    return (int64_t)fld;
    /*@ -relaxtypes */
}

#ifdef __UNUSED__
u_int16_t swap_u16(u_int16_t i)
/* byte-swap a 16-bit unsigned int */
{
    u_int8_t c1, c2;
 
    c1 = i & 255;
    c2 = (i >> 8) & 255;
 
    return (c1 << 8) + c2;
}
 
u_int32_t swap_u32(u_int32_t i) 
/* byte-swap a 32-bit unsigned int */
{
    u_int8_t c1, c2, c3, c4;    
 
    c1 = i & 255;
    c2 = (i >> 8) & 255;
    c3 = (i >> 16) & 255;
    c4 = (i >> 24) & 255;
 
    return ((u_int32_t)c1 << 24) + ((u_int32_t)c2 << 16) + ((u_int32_t)c3 << 8) + c4;
}
 
u_int64_t swap_u64(u_int64_t i) 
/* byte-swap a 64-bit unsigned int */
{
    u_int8_t c1, c2, c3, c4, c5, c6, c7, c8; 
 
    c1 = i & 255;
    c2 = (i >> 8) & 255;
    c3 = (i >> 16) & 255;
    c4 = (i >> 24) & 255;
    c5 = (i >> 32) & 255;
    c6 = (i >> 40) & 255;
    c7 = (i >> 48) & 255;
    c8 = (i >> 56) & 255;
 
    return ((u_int64_t)c1 << 56) + 
            ((u_int64_t)c2 << 48) + 
            ((u_int64_t)c3 << 40) + 
            ((u_int64_t)c4 << 32) + 
            ((u_int64_t)c5 << 24) + 
            ((u_int64_t)c6 << 16) + 
            ((u_int64_t)c7 << 8) + 
            c8;
}
#endif /* __UNUSED__ */
