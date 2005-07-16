/*****************************************************************************

This is a decoder for RTCM-104, an obscure and complicated serial
protocol used for broadcasting pseudorange corrections from
differential-GPS reference stations.  The applicable
standard is

RTCM RECOMMENDED STANDARDS FOR DIFFERENTIAL NAVSTAR GPS SERVICE,
RTCM PAPER 194-93/SC 104-STD

Ordering instructions are accessible from <http://www.rtcm.org/>
under "Publications".

This decoder is incomplete. It handles only messages of type 1 and 9.

The code was originally by Wolfgang Rupprecht.  You are not expected
to understand it. Here are his rather cryptic notes:

--------------------------------------------------------------------------
1) trim and  bitflip the input.

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

Shift 6 bytes of rtcm data in as such:

---> (trim-bits-to-5-bits) ---> (end-for-end-bit-flip) ---> 

---> shift-into-30-bit-shift-register
              |||||||||||||||||||||||
	      detector-for-preamble
              |||||||||||||||||||||||
              detector-for-parity
              |||||||||||||||||||||||
--------------------------------------------------------------------------

Wolfgang's decoder was loosely based on one written by John Sanger
in 1999 (in particular it uses Sanger's dump format).  Here are John 
Sanger's original notes:

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
#include <stdio.h>

#include "gpsd.h"

#define MAG_SHIFT 6u
#define MAG_TAG_DATA (1 << MAG_SHIFT)
#define MAG_TAG_MASK (3 << MAG_SHIFT)

#define PREAMBLE_PATTERN 0x66
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

static unsigned int rtcmparity(RTCMWORD th)
{
#define P_30_MASK	0x40000000u

#define	PARITY_25	0xbb1f3480u
#define	PARITY_26	0x5d8f9a40u
#define	PARITY_27	0xaec7cd00u
#define	PARITY_28	0x5763e680u
#define	PARITY_29	0x6bb1f340u
#define	PARITY_30	0x8b7a89c0u
    RTCMWORD        t;
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


#define rtcmparityok(w)	(rtcmparity(w) == ((w) & 0x3f))

#if 0
/* 
 * Defining the above as a function triggers an optimizer bug in gcc 3.4.2.
 * The symptom is that parity computation is screwed up and the decoder
 * never achieves sync lock.  Something steps on the argument to 
 * rtcmparity(); the lossage appears to be related to the compiler's 
 * attempt to fold the rtcmparity() call into rtcmparityok() in some
 * tail-recursion-like manner.  This happens under -O2, but not -O1, on
 * both i386 and amd64.  Disabling all of the individual -O2 suboptions
 * does *not* fix it.
 *
 * And the fun doesn't stop there! It turns out that even with this fix, bare
 * -O2 generates bad code.  It takes "-O2 -fschedule-insns" to generate good
 * code under 3.4.[23]...which is weird because -O2 is supposed to *imply*
 * -fschedule-insns.
 *
 *  gcc 4.0 does not manifest the bug.
 */
static bool rtcmparityok(RTCMWORD w)
{
    return (rtcmparity(w) == (w & 0x3f));
}
#endif

void rtcm_init(/*@out@*/struct rtcm_t *ctx)
{
    ctx->curr_word = 0;
    ctx->curr_offset = 24;	/* first word */
    ctx->locked = false;
    ctx->bufindex = 0;
}

/*
 *  Here are the constants and structures used for decoding
 */

#define	ZCOUNT_SCALE	0.6	/* sec */
#define	PCSMALL		0.02	/* metres */
#define	PCLARGE		0.32	/* metres */
#define	RRSMALL		0.002	/* metres/sec */
#define	RRLARGE		0.032	/* metres/sec */

#define XYZ_SCALE	0.01	/* metres */
#define DXYZ_SCALE	0.1	/* metres */
#define	LA_SCALE	90.0/32767.0	/* degrees */
#define	LO_SCALE	180.0/32767.0	/* degrees */
#define	FREQ_SCALE	0.1	/* kHz */
#define	FREQ_OFFSET	190.0	/* kHz */
#define CNR_OFFSET	24	/* dB */
#define TU_SCALE	5	/* minutes */

struct rtcm_msghw1 {			/* header word 1 */
    uint            parity:6;
    uint            refstaid:10;	/* reference station ID */
    uint            msgtype:6;		/* RTCM message type */
    uint            preamble:8;		/* fixed at 01100110 */
    uint            _pad:2;
};

struct rtcm_msghw2 {			/* header word 2 */
    uint            parity:6;
    uint            stathlth:3;		/* station health */
    uint            frmlen:5;
    uint            sqnum:3;
    uint            zcnt:13;
    uint            _pad:2;
};

struct rtcm_msg1w3 {			/* msg 1 word 3 */
    uint            parity:6;
    int             pc1:16;
    uint            satident1:5;	/* satellite ID */
    uint            udre1:2;
    uint            scale1:1;
    uint            _pad:2;
};

struct rtcm_msg1w4 {			/* msg 1 word 4 */
    uint            parity:6;
    uint            satident2:5;	/* satellite ID */
    uint            udre2:2;
    uint            scale2:1;
    uint            issuedata1:8;
    int             rangerate1:8;
    uint            _pad:2;
};

struct rtcm_msg1w5 {			/* msg 1 word 5 */
    uint            parity:6;
    int             rangerate2:8;
    int             pc2:16;
    uint            _pad:2;
};


struct rtcm_msg1w6 {			/* msg 1 word 6 */
    uint            parity:6;
    int             pc3_h:8;
    uint            satident3:5;	/* satellite ID */
    uint            udre3:2;
    uint            scale3:1;
    uint            issuedata2:8;
    uint            _pad:2;
};

struct rtcm_msg1w7 {			/* msg 1 word 7 */
    uint            parity:6;
    uint            issuedata3:8;
    int             rangerate3:8;
    uint            pc3_l:8;		/* NOTE: uint for low byte */
    uint            _pad:2;
};

struct rtcm_msghdr {
    struct rtcm_msghw1   w1;
    struct rtcm_msghw2   w2;
};

struct rtcm_msg1 {
    struct rtcm_msghw1   w1;
    struct rtcm_msghw2   w2;

    struct rtcm_msg1w3   w3;		/* clump #1 of 5-corrections each */
    struct rtcm_msg1w4   w4;
    struct rtcm_msg1w5   w5;
    struct rtcm_msg1w6   w6;
    struct rtcm_msg1w7   w7;

    struct rtcm_msg1w3   w8;		/* clump #2 of 5-corrections each */
    struct rtcm_msg1w4   w9;
    struct rtcm_msg1w5   w10;
    struct rtcm_msg1w6   w11;
    struct rtcm_msg1w7   w12;

    struct rtcm_msg1w3   w13;		/* clump #3 of 5-corrections each */
    struct rtcm_msg1w4   w14;
    struct rtcm_msg1w5   w15;
    struct rtcm_msg1w6   w16;
    struct rtcm_msg1w7   w17;
};

static void unpack(struct rtcm_t *ctx)
/* break out the raw bits into the content fields */
{
    int n, len;
    struct rtcm_msghdr  *msghdr;

    /* someday we'll do big-endian correction here */
    msghdr = (struct rtcm_msghdr *)ctx->buf;
    ctx->type = msghdr->w1.msgtype;
    ctx->length = msghdr->w2.frmlen;
    ctx->zcount = msghdr->w2.zcnt * ZCOUNT_SCALE;
    ctx->refstaid = msghdr->w1.refstaid;
    ctx->seqnum = msghdr->w2.sqnum;
    ctx->stathlth = msghdr->w2.stathlth;

    memset(ctx->ranges, 0, sizeof(ctx->ranges));
    len = (int)ctx->length;
    n = 0;
    switch (ctx->type) {
    case 1:
    case 9:
	{
	    struct rtcm_msg1    *m = (struct rtcm_msg1 *) msghdr;

	    while (len >= 0) {
		if (len >= 2) {
		    ctx->ranges[n].satident   = m->w3.satident1;
		    ctx->ranges[n].udre       = m->w3.udre1;
		    ctx->ranges[n].issuedata  = m->w4.issuedata1;
		    ctx->ranges[n].rangerr    = m->w3.pc1 * 
			(m->w3.scale1 ? PCLARGE : PCSMALL);
		    ctx->ranges[n].rangerate  = m->w4.rangerate1 * 
					(m->w3.scale1 ? RRLARGE : RRSMALL);
		    n++;
		}
		if (len >= 4) {
		    ctx->ranges[n].satident   = m->w4.satident2;
		    ctx->ranges[n].udre       = m->w4.udre2;
		    ctx->ranges[n].issuedata  = m->w6.issuedata2;
		    ctx->ranges[n].rangerr    = m->w5.pc2 * 
			(m->w4.scale2 ? PCLARGE : PCSMALL);
		    ctx->ranges[n].rangerate  = m->w5.rangerate2 * 
			(m->w4.scale2 ? RRLARGE : RRSMALL);
		    n++;
		}
		if (len >= 5) {
		    ctx->ranges[n].satident    = m->w6.satident3;
		    ctx->ranges[n].udre        = m->w6.udre3;
		    ctx->ranges[n].issuedata   = m->w7.issuedata3;
		    /*@ -shiftimplementation @*/
		    ctx->ranges[n].rangerr     = ((m->w6.pc3_h<<8)|(m->w7.pc3_l)) *
					(m->w6.scale3 ? PCLARGE : PCSMALL);
		    ctx->ranges[n].rangerate   = m->w7.rangerate3 * 
					(m->w6.scale3 ? RRLARGE : RRSMALL);
		    /*@ +shiftimplementation @*/
		    n++;
		}
		len -= 5;
		m = (struct rtcm_msg1 *) (((RTCMWORD *) m) + 5);
	    }
	}
	break;
    default:
	break;
    }
}

/*@ -usereleased -compdef @*/
enum rtcmstat_t rtcm_decode(struct rtcm_t *ctx, unsigned int c)
{
    enum rtcmstat_t res;

    if ((c & MAG_TAG_MASK) != MAG_TAG_DATA) {
	return RTCM_NO_SYNC;
    }
    c = reverse_bits[c & 0x3f];

    /*@ -shiftnegative @*/
    if (!ctx->locked) {
	ctx->curr_offset = -5;
	ctx->bufindex = 0;

	while (ctx->curr_offset <= 0) {
	    gpsd_report(RTCM_ERRLEVEL_BASE+2, "syncing");
	    ctx->curr_word <<= 1;
	    if (ctx->curr_offset > 0) {
		ctx->curr_word |= c << ctx->curr_offset;
	    } else {
		ctx->curr_word |= c >> -(ctx->curr_offset);
	    }
	    if (((struct rtcm_msghw1 *) & ctx->curr_word)->preamble ==
		PREAMBLE_PATTERN) {
		if (rtcmparityok(ctx->curr_word)) {
		    gpsd_report(RTCM_ERRLEVEL_BASE+1, 
				"preamble ok, parity ok -- locked\n");
		    ctx->locked = true;
		    /* ctx->curr_offset;  XXX - testing */
		    break;
		}
		gpsd_report(RTCM_ERRLEVEL_BASE+1, 
			    "preamble ok, parity fail\n");
	    }
	    ctx->curr_offset++;
	}			/* end while */
    }
    if (ctx->locked) {
	res = RTCM_SYNC;

	if (ctx->curr_offset > 0) {
	    ctx->curr_word |= c << ctx->curr_offset;
	} else {
	    ctx->curr_word |= c >> -(ctx->curr_offset);
	}

	if (ctx->curr_offset <= 0) {
	    /* weird-assed inversion */
	    if (ctx->curr_word & P_30_MASK)
		ctx->curr_word ^= W_DATA_MASK;

	    if (rtcmparityok(ctx->curr_word)) {
		if (((struct rtcm_msghw1 *) & ctx->curr_word)->preamble ==
		    PREAMBLE_PATTERN) {
		    gpsd_report(RTCM_ERRLEVEL_BASE+2, 
				"Preamble spotted (index: %u)\n",
				ctx->bufindex);
		    ctx->bufindex = 0;
		}
		gpsd_report(RTCM_ERRLEVEL_BASE+2,
			    "processing word %u (offset %d)\n",
			    ctx->bufindex, ctx->curr_offset);
		{
		    struct rtcm_msghdr  *msghdr = (struct rtcm_msghdr *) ctx->buf;

		    /*
		     * Guard against a buffer overflow attack.  Just wait for
		     * the next PREAMBLE_PATTERN and go on from there. 
		     */
		    if (ctx->bufindex >= RTCM_WORDS_MAX){
			ctx->bufindex = 0;
			return RTCM_NO_SYNC;
		    }

		    ctx->buf[ctx->bufindex] = ctx->curr_word;

		    if ((ctx->bufindex == 0) &&
			(msghdr->w1.preamble != PREAMBLE_PATTERN)) {
			gpsd_report(RTCM_ERRLEVEL_BASE+1, 
				    "word 0 not a preamble- punting\n");
			return RTCM_NO_SYNC;
		    }
		    ctx->bufindex++;
		    /* rtcm_print_msg(msghdr); */

		    if (ctx->bufindex > 2) {	/* do we have the length yet? */
			if (ctx->bufindex >= msghdr->w2.frmlen + 2) {
			    /* jackpot, we have an RTCM packet*/
			    res = RTCM_STRUCTURE;
			    ctx->bufindex = 0;
			    unpack(ctx);
			}
		    }
		}
		ctx->curr_word <<= 30;	/* preserve the 2 low bits */
		ctx->curr_offset += 30;
		if (ctx->curr_offset > 0) {
		    ctx->curr_word |= c << ctx->curr_offset;
		} else {
		    ctx->curr_word |= c >> -(ctx->curr_offset);
		}
	    } else {
		gpsd_report(RTCM_ERRLEVEL_BASE+0, 
			    "parity failure, lost lock\n");
		ctx->locked = false;
	    }
	}
	ctx->curr_offset -= 6;
	gpsd_report(RTCM_ERRLEVEL_BASE+2, "residual %d", ctx->curr_offset);
	return res;
    }
    /*@ +shiftnegative @*/

    /* never achieved lock */
    return RTCM_NO_SYNC;
}
/*@ +usereleased +compdef @*/

void rtcm_dump(struct rtcm_t *rtcmp, /*@out@*/char buf[], size_t buflen)
/* dump the contents of a parsed RTCM104 message */
{
    int             i;

    (void)snprintf(buf, buflen, "H\t%u\t%u\t%0.1f\t%u\t%u\t%u\n",
	   rtcmp->type,
	   rtcmp->refstaid,
	   rtcmp->zcount,
	   rtcmp->seqnum,
	   rtcmp->length,
	   rtcmp->stathlth);

    switch (rtcmp->type) {
    case 1:
    case 9:
	for (i = 0; i < MAXCORRECTIONS; i++)
	    if (rtcmp->ranges[i].satident != 0)
		(void)snprintf(buf + strlen(buf), buflen - strlen(buf),
			       "S\t%u\t%u\t%u\t%0.1f\t%0.3f\t%0.3f\n",
			       rtcmp->ranges[i].satident,
			       rtcmp->ranges[i].udre,
			       rtcmp->ranges[i].issuedata,
			       rtcmp->zcount,
			       rtcmp->ranges[i].rangerr,
			       rtcmp->ranges[i].rangerate);
	break;
    default:
	break;
    }

}

#ifdef __UNUSED__
void rtcm_output_mag(RTCMWORD * ip)
/* ship an RTCM message to standard output in Magnavox format */
{
    static RTCMWORD w = 0;
    int             len;
    static uint     sqnum = 0;

    len = ((struct rtcm_msghdr *) ip)->w2.frmlen + 2;
    ((struct rtcm_msghdr *) ip)->w2.sqnum = sqnum++;
    sqnum &= 0x7;

    while (len-- > 0) {
	w <<= 30;
	w |= *ip++ & W_DATA_MASK;

	w |= rtcmparity(w);

	/* weird-assed inversion */
	if (w & P_30_MASK)
	    w ^= W_DATA_MASK;

	/* msb first */
	putchar(MAG_TAG_DATA | reverse_bits[(w >> 24) & 0x3f]);
	putchar(MAG_TAG_DATA | reverse_bits[(w >> 18) & 0x3f]);
	putchar(MAG_TAG_DATA | reverse_bits[(w >> 12) & 0x3f]);
	putchar(MAG_TAG_DATA | reverse_bits[(w >> 6) & 0x3f]);
	putchar(MAG_TAG_DATA | reverse_bits[(w) & 0x3f]);
    }
}
#endif /* UNUSED */
