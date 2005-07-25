/*****************************************************************************

This is a decoder for RTCM-104, an obscure and complicated serial
protocol used for broadcasting pseudorange corrections from
differential-GPS reference stations.  The applicable
standard is

RTCM RECOMMENDED STANDARDS FOR DIFFERENTIAL NAVSTAR GPS SERVICE,
RTCM PAPER 194-93/SC 104-STD

Ordering instructions are accessible from <http://www.rtcm.org/>
under "Publications".

Also applicable is ITU-R M.823: "Technical characteristics of
differential transmissions for global navigation satellite systems
from maritime radio beacons in the frequency band 283.5 - 315 kHz in
region 1 and 285 - 325 kHz in regions 2 & 3."

The code was originally by Wolfgang Rupprecht.  ESR severely hacked
it, with Wolfgang's help, in order to separate message analysis from
message dumping (he also added support for big-endian machines and
repacking).  You are not expected to understand any of it. 

However, in case you think you need to, this decoder is divided into
two layers.  The lower one just handles synchronizing with the
incoming bitstream and parity checking, Here are Wolfgang's original
rather cryptic notes on the lower level:

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

Wolfgang's decoder was loosely based on one written by John Sanger in
1999 (in particular the dump function emits Sanger's dump format).
Here are John Sanger's original notes:

--------------------------------------------------------------------------
The RTCM decoder prints a legible representation of the input data.
The RTCM SC-104 specification is copyrighted, so I cannot
quote it - in fact, I have never read it! Most of the information
used to develop the decoder came from publication ITU-R M.823.
This is a specification of the data transmitted from LF DGPS
beacons in the 300kHz band. M.823 contains most of those parts of
RTCM SC-104 directly relevant to the air interface (there
are one or two annoying and vital omissions!). Information
about the serial interface format was gleaned from studying
the output of a beacon receiver test program made available on
Starlink's website.
--------------------------------------------------------------------------

*****************************************************************************/

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "gpsd.h"

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

static unsigned int isgpsparity(isgps30bits_t th)
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

    gpsd_report(RTCM_ERRLEVEL_BASE+2, "parity %u\n", p);
    return (p);
}


#define isgpsparityok(w)	(isgpsparity(w) == ((w) & 0x3f))

#if 0
/* 
 * ESR found a doozy of a bug...
 *
 * Defining the above as a function triggers an optimizer bug in gcc 3.4.2.
 * The symptom is that parity computation is screwed up and the decoder
 * never achieves sync lock.  Something steps on the argument to 
 * isgpsparity(); the lossage appears to be related to the compiler's 
 * attempt to fold the isgpsparity() call into isgpsparityok() in some
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
static bool isgpsparityok(isgps30bits_t w)
{
    return (isgpsparity(w) == (w & 0x3f));
}
#endif

void isgps_init(/*@out@*/struct gps_device_t *session)
{
    session->isgps.curr_word = 0;
    session->isgps.curr_offset = 24;	/* first word */
    session->isgps.locked = false;
    session->isgps.bufindex = 0;
}

/*@ -usereleased -compdef @*/
enum isgpsstat_t isgps_decode(struct gps_device_t *session, 
				     bool (*preamble_match)(isgps30bits_t *),
				     bool (*length_check)(struct gps_device_t *),
				     unsigned int c)
{
    enum isgpsstat_t res;

    if ((c & MAG_TAG_MASK) != MAG_TAG_DATA) {
	gpsd_report(RTCM_ERRLEVEL_BASE+1, 
		    "word tag not correct, skipping\n");
	return ISGPS_SKIP;
    }
    c = reverse_bits[c & 0x3f];

    /*@ -shiftnegative @*/
    if (!session->isgps.locked) {
	session->isgps.curr_offset = -5;
	session->isgps.bufindex = 0;

	while (session->isgps.curr_offset <= 0) {
	    gpsd_report(RTCM_ERRLEVEL_BASE+2, "syncing\n");
	    session->isgps.curr_word <<= 1;
	    if (session->isgps.curr_offset > 0) {
		session->isgps.curr_word |= c << session->isgps.curr_offset;
	    } else {
		session->isgps.curr_word |= c >> -(session->isgps.curr_offset);
	    }

	    if (preamble_match(&session->isgps.curr_word)) {
		if (isgpsparityok(session->isgps.curr_word)) {
		    gpsd_report(RTCM_ERRLEVEL_BASE+1, 
				"preamble ok, parity ok -- locked\n");
		    session->isgps.locked = true;
		    /* session->isgps.curr_offset;  XXX - testing */
		    break;
		}
		gpsd_report(RTCM_ERRLEVEL_BASE+1, 
			    "preamble ok, parity fail\n");
	    }
	    session->isgps.curr_offset++;
	}			/* end while */
    }
    if (session->isgps.locked) {
	res = ISGPS_SYNC;

	if (session->isgps.curr_offset > 0) {
	    session->isgps.curr_word |= c << session->isgps.curr_offset;
	} else {
	    session->isgps.curr_word |= c >> -(session->isgps.curr_offset);
	}

	if (session->isgps.curr_offset <= 0) {
	    /* weird-assed inversion */
	    if (session->isgps.curr_word & P_30_MASK)
		session->isgps.curr_word ^= W_DATA_MASK;

	    if (isgpsparityok(session->isgps.curr_word)) {
#if 0
		/*
		 * Don't clobber the buffer just because we spot
		 * another preamble pattern in the data stream. -wsr
		 */
		if (preamble_match(&session->isgps.curr_word)) {
		    gpsd_report(RTCM_ERRLEVEL_BASE+2, 
				"Preamble spotted (index: %u)\n",
				session->isgps.bufindex);
		    session->isgps.bufindex = 0;
		}
#endif
		gpsd_report(RTCM_ERRLEVEL_BASE+2,
			    "processing word %u (offset %d)\n",
			    session->isgps.bufindex, session->isgps.curr_offset);
		{
		    /*
		     * Guard against a buffer overflow attack.  Just wait for
		     * the next PREAMBLE_PATTERN and go on from there. 
		     */
		    if (session->isgps.bufindex >= RTCM_WORDS_MAX){
			session->isgps.bufindex = 0;
			gpsd_report(RTCM_ERRLEVEL_BASE+1, 
				    "RTCM buffer overflowing -- resetting\n");
			return ISGPS_NO_SYNC;
		    }

		    session->isgps.buf[session->isgps.bufindex] = session->isgps.curr_word;

		    if ((session->isgps.bufindex == 0) &&
			!preamble_match((isgps30bits_t *)session->isgps.buf)) {
			gpsd_report(RTCM_ERRLEVEL_BASE+1, 
				    "word 0 not a preamble- punting\n");
			return ISGPS_NO_SYNC;
		    }
		    session->isgps.bufindex++;

		    if (length_check(session)) {
			/* jackpot, we have a complete packet*/
			session->isgps.bufindex = 0;
			res = ISGPS_MESSAGE;
		    }
		}
		session->isgps.curr_word <<= 30;	/* preserve the 2 low bits */
		session->isgps.curr_offset += 30;
		if (session->isgps.curr_offset > 0) {
		    session->isgps.curr_word |= c << session->isgps.curr_offset;
		} else {
		    session->isgps.curr_word |= c >> -(session->isgps.curr_offset);
		}
	    } else {
		gpsd_report(RTCM_ERRLEVEL_BASE+0, 
			    "parity failure, lost lock\n");
		session->isgps.locked = false;
	    }
	}
	session->isgps.curr_offset -= 6;
	gpsd_report(RTCM_ERRLEVEL_BASE+2, "residual %d\n", session->isgps.curr_offset);
	return res;
    }
    /*@ +shiftnegative @*/

    /* never achieved lock */
    gpsd_report(RTCM_ERRLEVEL_BASE+1, 
		"lock never achieved\n");
    return ISGPS_NO_SYNC;
}
/*@ +usereleased +compdef @*/

