#define DEBUG
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
#include <stdlib.h>
#include "gpsd_config.h"
#include "gpsd.h"
#endif /* DEBUG */


#define BITS_PER_BYTE	8

unsigned long long ubits(char buf[], unsigned int start, unsigned int width)
/* extract a bitfield from the buffer as an unsigned big-endian long */
{
    unsigned long long fld = 0;
    unsigned int i;;

    assert(width <= sizeof(long long) * BITS_PER_BYTE);
    for (i = 0; i < (width + BITS_PER_BYTE - 1) / BITS_PER_BYTE; i++) {
	fld <<= BITS_PER_BYTE;
	fld |= (unsigned char)buf[start / BITS_PER_BYTE + i];
    }
#ifdef DEBUG
    printf("Extracting %d:%d from %s: segment 0x%llx = %lld\n", start, width, gpsd_hexdump(buf, 12), fld, fld);
#endif /* DEBUG */

    fld &= (0xffffffff >> (start % BITS_PER_BYTE));
#ifdef DEBUG
    printf("After masking: 0x%llx = %lld\n", fld, fld);
#endif /* DEBUG */
    fld >>= (BITS_PER_BYTE - 1) - ((start + width) % BITS_PER_BYTE);
#ifdef DEBUG
    printf("After downshifting: 0x%llx = %lld\n", fld, fld);
#endif /* DEBUG */

    return fld;
}

signed long long sbits(char buf[], unsigned int start, unsigned int width)
/* extract a bitfield from the buffer as a signed big-endian long */
{
    unsigned long long un = ubits(buf, start, width);
    signed long long fld;

    if (un & (1 << width))
	fld = -(un & ~(1 << width));
    else
	fld = (signed long long)un;
    
    return fld;
}

