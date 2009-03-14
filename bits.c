/* $Id$ */
/*
 * Bitfield extraction functions.  In each, start is a bit index (not
 * a byte index) and width is a bit width.  The width bounded above by
 * the bit width of a long long, which s 64 bits in all standard data
 * models for 64-bit processors.
 *
 * The sbits() function assumes twos-complement arithmetic.
 */
#include <assert.h>

#include "bits.h"
#ifdef DEBUG
#include <stdio.h>
#include "gpsd_config.h"
#include "gpsd.h"
#endif /* DEBUG */

#define BITS_PER_BYTE	8

unsigned long long ubits(char buf[], unsigned int start, unsigned int width)
/* extract a bitfield from the buffer as an unsigned big-endian long long */
{
    unsigned long long fld = 0;
    unsigned int i;
    unsigned end;

    /*@i1@*/assert(width <= sizeof(long long) * BITS_PER_BYTE);
    for (i = start / BITS_PER_BYTE; i < (start + width + BITS_PER_BYTE - 1) / BITS_PER_BYTE; i++) {
	fld <<= BITS_PER_BYTE;
	fld |= (unsigned char)buf[i];
    }
#ifdef UDEBUG
    printf("Extracting %d:%d from %s: segment 0x%llx = %lld\n", start, width,
	   gpsd_hexdump(buf, 12), fld, fld);
#endif /* UDEBUG */

    end = (start + width) % BITS_PER_BYTE;
    if (end != 0) {
	fld >>= (BITS_PER_BYTE - end);
#ifdef UDEBUG
	printf("After downshifting by %d bits: 0x%llx = %lld\n", 
	       BITS_PER_BYTE - end, fld, fld);
#endif /* UDEBUG */
    }

    fld &= ~(0xffffffff << width);
#ifdef UDEBUG
    printf("After selecting out the bottom %u bits: 0x%llx = %lld\n", 
	   width, fld, fld);
#endif /* UDEBUG */

    return fld;
}

signed long long sbits(char buf[], unsigned int start, unsigned int width)
/* extract a bitfield from the buffer as a signed big-endian long */
{
    unsigned long long fld = ubits(buf, start, width);

#ifdef DEBUG
    (void)fprintf(stderr, "sbits(%d, %d) extracts %llx\n", start, width, fld);
#endif /* DEBUG */
    /*@ +relaxtypes */
    if (fld & (1 << (width-1))) {
#ifdef DEBUG
	(void)fprintf(stderr, "%llx is signed\n", fld);
#endif /* DEBUG */
	fld |= (-1LL << (width-1));
    }
#ifdef DEBUG
    (void)fprintf(stderr, "sbits(%d, %d) returns %lld\n", start, width, (signed long long)fld);
#endif /* DEBUG */
    return (signed long long)fld;
    /*@ -relaxtypes */
}

