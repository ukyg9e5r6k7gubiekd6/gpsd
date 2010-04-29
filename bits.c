/* bits.c - bitfield extraction code
 *
 * This file is Copyright (c)2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 *
 * Bitfield extraction functions.  In each, start is a bit index (not
 * a byte index) and width is a bit width.  The width bounded above by
 * the bit width of a long long, which is 64 bits in all standard data
 * models for 32- and 64-bit processors.
 *
 * The sbits() function assumes twos-complement arithmetic.
 */
#include <assert.h>

#include "bits.h"
#ifdef DEBUG
#include <stdio.h>
#include "gpsd.h"
#endif /* DEBUG */

#define BITS_PER_BYTE	8

unsigned long long ubits(char buf[], unsigned int start, unsigned int width)
/* extract a (zero-origin) bitfield from the buffer as an unsigned big-endian long long */
{
    unsigned long long fld = 0;
    unsigned int i;
    unsigned end;

    /*@i1@*/ assert(width <= sizeof(long long) * BITS_PER_BYTE);
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

signed long long sbits(char buf[], unsigned int start, unsigned int width)
/* extract a bitfield from the buffer as a signed big-endian long */
{
    unsigned long long fld = ubits(buf, start, width);

#ifdef SDEBUG
    (void)fprintf(stderr, "sbits(%d, %d) extracts %llx\n", start, width, fld);
#endif /* SDEBUG */
    /*@ +relaxtypes */
    if (fld & (1 << (width - 1))) {
#ifdef SDEBUG
	(void)fprintf(stderr, "%llx is signed\n", fld);
#endif /* SDEBUG */
	/*@ -shiftimplementation @*/
	fld |= (-1LL << (width - 1));
	/*@ +shiftimplementation @*/
    }
#ifdef SDEBUG
    (void)fprintf(stderr, "sbits(%d, %d) returns %lld\n", start, width,
		  (signed long long)fld);
#endif /* SDEBUG */
    return (signed long long)fld;
    /*@ -relaxtypes */
}
