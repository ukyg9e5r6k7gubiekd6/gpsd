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
