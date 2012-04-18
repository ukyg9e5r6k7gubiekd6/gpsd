/* bits.c - bitfield extraction code
 *
 * This file is Copyright (c)2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 *
 * Bitfield extraction functions.  In each, start is a bit index  - not
 * a byte index - and width is a bit width.  The width is bounded above by
 * 64 bits.
 *
 * The sbits() function assumes twos-complement arithmetic.
 */
#include <assert.h>
#include <stdint.h>

#include "bits.h"
#ifdef DEBUG
#include <stdio.h>
#include "gpsd.h"
#endif /* DEBUG */

#define BITS_PER_BYTE	8

uint64_t ubits(char buf[], unsigned int start, unsigned int width)
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
#ifdef DEBUG
    (void)printf("%d:%d from %s:\n", start, width, gpsd_hexdump(buf, 32));
#endif

#ifdef DEBUG
    (void)printf("    segment=0x%llx,", fld);
#endif /* DEBUG */
    end = (start + width) % BITS_PER_BYTE;
    if (end != 0) {
	fld >>= (BITS_PER_BYTE - end);
#ifdef DEBUG
	(void)printf(" after downshifting by %d bits: 0x%llx",
		     BITS_PER_BYTE - end, fld);
#endif /* UDEBUG */
    }
#ifdef DEBUG
    (void)printf(" = %lld\n", fld);
#endif /* UDEBUG */

    /*@ -shiftimplementation @*/
    fld &= ~(-1LL << width);
    /*@ +shiftimplementation @*/
#ifdef DEBUG
    (void)
	printf("    after selecting out the bottom %u bits: 0x%llx = %lld\n",
	       width, fld, fld);
#endif /* DEBUG */

    return fld;
}

int64_t sbits(char buf[], unsigned int start, unsigned int width)
/* extract a bitfield from the buffer as a signed big-endian long */
{
    uint64_t fld = ubits(buf, start, width);

#ifdef __UNUSED_DEBUG__
    (void)fprintf(stderr, "sbits(%d, %d) extracts %llx\n", start, width, fld);
#endif /* __UNUSED_DEBUG__ */
    /*@ +relaxtypes */
    if (fld & (1LL << (width - 1))) {
#ifdef __UNUSED_DEBUG__
	(void)fprintf(stderr, "%llx is signed\n", fld);
#endif /* __UNUSED_DEBUG__ */
	/*@ -shiftimplementation @*/
	fld |= (-1LL << (width - 1));
	/*@ +shiftimplementation @*/
    }
#ifdef __UNUSED_DEBUG__
    (void)fprintf(stderr, "sbits(%d, %d) returns %lld\n", start, width,
		  (int64_t)fld);
#endif /* __UNUSED_DEBUG__ */
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
