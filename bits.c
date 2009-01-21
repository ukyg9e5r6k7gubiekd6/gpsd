/*
 * Bitfield extraction functions.  In each, start is a bit index (not a byte
 * index) and width is a bit width (bounded above by the bit width of long 
 * long).
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
/* extract a bitfield from the buffer as an unsigned big-endian long */
{
    unsigned long long fld = 0;
    unsigned int i;
    unsigned end;

    /*@i1@*/assert(width <= sizeof(long long) * BITS_PER_BYTE);
    for (i = start / BITS_PER_BYTE; i < (start + width + BITS_PER_BYTE - 1) / BITS_PER_BYTE; i++) {
	fld <<= BITS_PER_BYTE;
	fld |= (unsigned char)buf[i];
    }
#ifdef DEBUG
    printf("Extracting %d:%d from %s: segment 0x%llx = %lld\n", start, width,
	   gpsd_hexdump(buf, 12), fld, fld);
#endif /* DEBUG */

    end = (start + width) % BITS_PER_BYTE;
    if (end != 0) {
	fld >>= (BITS_PER_BYTE - end);
#ifdef DEBUG
	printf("After downshifting by %d bits: 0x%llx = %lld\n", 
	       BITS_PER_BYTE - end, fld, fld);
#endif /* DEBUG */
    }

    fld &= ~(0xffffffff << width);
#ifdef DEBUG
    printf("After selecting out the bottom %u bits: 0x%llx = %lld\n", 
	   width, fld, fld);
#endif /* DEBUG */

    return fld;
}

signed long long sbits(char buf[], unsigned int start, unsigned int width)
/* extract a bitfield from the buffer as a signed big-endian long */
{
    unsigned long long un = ubits(buf, start, width);
    signed long long fld;

    /*@ +relaxtypes */
    if (un & (1 << width))
	fld = -(un & ~(1 << width));
    else
	fld = (signed long long)un;
    
    return fld;
    /*@ -relaxtypes */
}

