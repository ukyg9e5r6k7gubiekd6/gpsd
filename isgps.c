/*****************************************************************************

This is a decoder for the unnamed protocol described in IS-GPS-200,
the Navstar GPS Interface Specification, and used as a transport layer
for both GPS satellite downlink transmissions and the RTCM104 format
for broadcasting differential-GPS corrections.

The code was originally by Wolfgang Rupprecht.  ESR severely hacked
it, with Wolfgang's help, in order to separate message analysis from
message dumping and separate this lower layer from the upper layer 
handing RTCM decoding.  You are not expected to understand any of it. 

This lower layer just handles synchronizing with the incoming
bitstream and parity checking; all it does is assemble message
packets.  It needs an upper layer to analyze the packets into
bitfields and the assemble the bitfields into usable data.

Here are Wolfgang's original rather cryptic notes on this code:

--------------------------------------------------------------------------
1) trim and bitflip the input.

While syncing the msb of the input gets shifted into lsb of the
assembled word.  
    word <<= 1, or in input >> 5 
    word <<= 1, or in input >> 4
    word <<= 1, or in input >> 3
    word <<= 1, or in input >> 2 
    word <<= 1, or in input >> 1 
    word <<= 1, or in input

At one point it should sync-lock.

----

Shift 6 bytes of RTCM data in as such:

---> (trim-bits-to-5-bits) ---> (end-for-end-bit-flip) ---> 

---> shift-into-30-bit-shift-register
              |||||||||||||||||||||||
	      detector-for-preamble
              |||||||||||||||||||||||
              detector-for-parity
              |||||||||||||||||||||||
--------------------------------------------------------------------------

The lower layer's job is done when it has assembled a message of up to
33 words of clean parity-checked data.  At this point the upper layer
takes over.  struct rtcm_msg_t is overlaid on the buffer and the bitfields
are used to extract pieces of it (which, if you're on a big-endian machine
may need to be swapped end-for-end).  Those pieces are copied and (where
necessary) reassembled into a struct rtcm_t.

*****************************************************************************/

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "gpsd.h"

#ifdef BINARY_ENABLE

#define MAG_SHIFT 6u
#define MAG_TAG_DATA (1 << MAG_SHIFT)
#define MAG_TAG_MASK (3 << MAG_SHIFT)

#define PREAMBLE_SHIFT 22
#define PREAMBLE_MASK (0xFF << PREAMBLE_SHIFT)

#define W_DATA_MASK	0x3fffffc0u

/*@ +charint @*/
static unsigned char   parity_array[] = {
    0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
    1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
    1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
    0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
    1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
    0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
    0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
    1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
    1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
    0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
    0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
    1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
    0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
    1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
    1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
    0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0
};

static unsigned int reverse_bits[] = {
    0, 32, 16, 48, 8, 40, 24, 56, 4, 36, 20, 52, 12, 44, 28, 60,
    2, 34, 18, 50, 10, 42, 26, 58, 6, 38, 22, 54, 14, 46, 30, 62,
    1, 33, 17, 49, 9, 41, 25, 57, 5, 37, 21, 53, 13, 45, 29, 61,
    3, 35, 19, 51, 11, 43, 27, 59, 7, 39, 23, 55, 15, 47, 31, 63
};
/*@ -charint @*/

unsigned int isgps_parity(isgps30bits_t th)
{
#define P_30_MASK	0x40000000u

#define	PARITY_25	0xbb1f3480u
#define	PARITY_26	0x5d8f9a40u
#define	PARITY_27	0xaec7cd00u
#define	PARITY_28	0x5763e680u
#define	PARITY_29	0x6bb1f340u
#define	PARITY_30	0x8b7a89c0u
    isgps30bits_t        t;
    unsigned int    p;

    /*
    if (th & P_30_MASK)
	th ^= W_DATA_MASK;
    */

    /*@ +charint @*/
    t = th & PARITY_25;
    p = parity_array[t & 0xff] ^ parity_array[(t >> 8) & 0xff] ^
	parity_array[(t >> 16) & 0xff] ^ parity_array[(t >> 24) & 0xff];
    t = th & PARITY_26;
    p = (p << 1) | (parity_array[t & 0xff] ^ parity_array[(t >> 8) & 0xff] ^
		  parity_array[(t >> 16) & 0xff] ^ parity_array[(t >> 24) & 0xff]);
    t = th & PARITY_27;
    p = (p << 1) | (parity_array[t & 0xff] ^ parity_array[(t >> 8) & 0xff] ^
		  parity_array[(t >> 16) & 0xff] ^ parity_array[(t >> 24) & 0xff]);
    t = th & PARITY_28;
    p = (p << 1) | (parity_array[t & 0xff] ^ parity_array[(t >> 8) & 0xff] ^
		  parity_array[(t >> 16) & 0xff] ^ parity_array[(t >> 24) & 0xff]);
    t = th & PARITY_29;
    p = (p << 1) | (parity_array[t & 0xff] ^ parity_array[(t >> 8) & 0xff] ^
		  parity_array[(t >> 16) & 0xff] ^ parity_array[(t >> 24) & 0xff]);
    t = th & PARITY_30;
    p = (p << 1) | (parity_array[t & 0xff] ^ parity_array[(t >> 8) & 0xff] ^
		  parity_array[(t >> 16) & 0xff] ^ parity_array[(t >> 24) & 0xff]);
    /*@ -charint @*/

    gpsd_report(ISGPS_ERRLEVEL_BASE+2, "ISGPS parity %u\n", p);
    return (p);
}


#define isgps_parityok(w)	(isgps_parity(w) == ((w) & 0x3f))

#if 0
/* 
 * ESR found a doozy of a bug...
 *
 * Defining the above as a function triggers an optimizer bug in gcc 3.4.2.
 * The symptom is that parity computation is screwed up and the decoder
 * never achieves sync lock.  Something steps on the argument to 
 * isgpsparity(); the lossage appears to be related to the compiler's 
 * attempt to fold the isgps_parity() call into isgps_parityok() in some
 * tail-recursion-like manner.  This happens under -O2, but not -O1, on
 * both i386 and amd64.  Disabling all of the individual -O2 suboptions
 * does *not* fix it.
 *
 * And the fun doesn't stop there! It turns out that even with this fix, bare
 * -O2 generates bad code.  It takes "-O2 -fschedule-insns" to generate good
 * code under 3.4.[23]...which is weird because -O2 is supposed to *imply*
 * -fschedule-insns.
 *
 *  gcc 4.0 does not manifest these bugs.
 */
static bool isgps_parityok(isgps30bits_t w)
{
    return (isgpsparity(w) == (w & 0x3f));
}
#endif

void isgps_init(/*@out@*/struct gps_device_t *session)
{
    session->driver.isgps.curr_word = 0;
    session->driver.isgps.curr_offset = 24;	/* first word */
    session->driver.isgps.locked = false;
    session->driver.isgps.bufindex = 0;
}

/*@ -usereleased -compdef @*/
enum isgpsstat_t isgps_decode(struct gps_device_t *session, 
				     bool (*preamble_match)(isgps30bits_t *),
				     bool (*length_check)(struct gps_device_t *),
			      size_t maxlen,
				     unsigned int c)
{
    enum isgpsstat_t res;

    /* ASCII characters 64-127, @ through DEL */
    if ((c & MAG_TAG_MASK) != MAG_TAG_DATA) {
	gpsd_report(ISGPS_ERRLEVEL_BASE+1, 
		    "ISGPS word tag not correct, skipping\n");
	return ISGPS_SKIP;
    }

    c = reverse_bits[c & 0x3f];

    /*@ -shiftnegative @*/
    if (!session->driver.isgps.locked) {
	session->driver.isgps.curr_offset = -5;
	session->driver.isgps.bufindex = 0;

	while (session->driver.isgps.curr_offset <= 0) {
	    session->driver.isgps.curr_word <<= 1;
	    if (session->driver.isgps.curr_offset > 0) {
		session->driver.isgps.curr_word |= c << session->driver.isgps.curr_offset;
	    } else {
		session->driver.isgps.curr_word |= c >> -(session->driver.isgps.curr_offset);
	    }
	    gpsd_report(ISGPS_ERRLEVEL_BASE+2, "ISGPS syncing at byte %d: %0x%08x\n", session->char_counter, session->driver.isgps.curr_word);

	    if (preamble_match(&session->driver.isgps.curr_word)) {
		if (isgps_parityok(session->driver.isgps.curr_word)) {
		    gpsd_report(ISGPS_ERRLEVEL_BASE+1, 
				"ISGPS preamble ok, parity ok -- locked\n");
		    session->driver.isgps.locked = true;
		    /* session->driver.isgps.curr_offset;  XXX - testing */
		    break;
		}
		gpsd_report(ISGPS_ERRLEVEL_BASE+1, 
			    "ISGPS preamble ok, parity fail\n");
	    }
	    session->driver.isgps.curr_offset++;
	}			/* end while */
    }
    if (session->driver.isgps.locked) {
	res = ISGPS_SYNC;

	if (session->driver.isgps.curr_offset > 0) {
	    session->driver.isgps.curr_word |= c << session->driver.isgps.curr_offset;
	} else {
	    session->driver.isgps.curr_word |= c >> -(session->driver.isgps.curr_offset);
	}

	if (session->driver.isgps.curr_offset <= 0) {
	    /* weird-assed inversion */
	    if (session->driver.isgps.curr_word & P_30_MASK)
		session->driver.isgps.curr_word ^= W_DATA_MASK;

	    if (isgps_parityok(session->driver.isgps.curr_word)) {
#if 0
		/*
		 * Don't clobber the buffer just because we spot
		 * another preamble pattern in the data stream. -wsr
		 */
		if (preamble_match(&session->driver.isgps.curr_word)) {
		    gpsd_report(ISGPS_ERRLEVEL_BASE+2, 
				"ISGPS preamble spotted (index: %u)\n",
				session->driver.isgps.bufindex);
		    session->driver.isgps.bufindex = 0;
		}
#endif
		gpsd_report(ISGPS_ERRLEVEL_BASE+2,
			    "ISGPS processing word %u (offset %d)\n",
			    session->driver.isgps.bufindex, session->driver.isgps.curr_offset);
		{
		    /*
		     * Guard against a buffer overflow attack.  Just wait for
		     * the next PREAMBLE_PATTERN and go on from there. 
		     */
		    if (session->driver.isgps.bufindex >= (unsigned)maxlen){
			session->driver.isgps.bufindex = 0;
			gpsd_report(ISGPS_ERRLEVEL_BASE+1, 
				    "ISGPS buffer overflowing -- resetting\n");
			return ISGPS_NO_SYNC;
		    }

		    session->driver.isgps.buf[session->driver.isgps.bufindex] = session->driver.isgps.curr_word;

		    if ((session->driver.isgps.bufindex == 0) &&
			!preamble_match((isgps30bits_t *)session->driver.isgps.buf)) {
			gpsd_report(ISGPS_ERRLEVEL_BASE+1, 
				    "ISGPS word 0 not a preamble- punting\n");
			return ISGPS_NO_SYNC;
		    }
		    session->driver.isgps.bufindex++;

		    if (length_check(session)) {
			/* jackpot, we have a complete packet*/
			session->driver.isgps.bufindex = 0;
			res = ISGPS_MESSAGE;
		    }
		}
		session->driver.isgps.curr_word <<= 30;	/* preserve the 2 low bits */
		session->driver.isgps.curr_offset += 30;
		if (session->driver.isgps.curr_offset > 0) {
		    session->driver.isgps.curr_word |= c << session->driver.isgps.curr_offset;
		} else {
		    session->driver.isgps.curr_word |= c >> -(session->driver.isgps.curr_offset);
		}
	    } else {
		gpsd_report(ISGPS_ERRLEVEL_BASE+0, 
			    "ISGPS parity failure, lost lock\n");
		session->driver.isgps.locked = false;
	    }
	}
	session->driver.isgps.curr_offset -= 6;
	gpsd_report(ISGPS_ERRLEVEL_BASE+2, "residual %d\n", session->driver.isgps.curr_offset);
	return res;
    }
    /*@ +shiftnegative @*/

    /* never achieved lock */
    gpsd_report(ISGPS_ERRLEVEL_BASE+1, 
		"lock never achieved\n");
    return ISGPS_NO_SYNC;
}
/*@ +usereleased +compdef @*/

#endif /* BINARY_ENABLE */
