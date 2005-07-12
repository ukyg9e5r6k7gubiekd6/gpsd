#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "gpsd.h"
#include "rtcm.h"

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

    gpsd_report(6, "parity %u\n", p);
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
 * both i386 and amd64. gcc 4.0 does not manifest the bug.
 */
static bool rtcmparityok(RTCMWORD w)
{
    return (rtcmparity(w) == (w & 0x3f));
}
#endif

void rtcm_init(/*@out@*/struct rtcm_ctx * ctx)
{
    ctx->curr_word = 0;
    ctx->curr_offset = 24;	/* first word */
    ctx->locked = false;
    ctx->bufindex = 0;
}

/*@ -usereleased -compdef @*/
/*@null@*/ struct rtcm_msghdr *rtcm_decode(struct rtcm_ctx * ctx, unsigned int c)
{
    struct rtcm_msghdr *res;

    if ((c & MAG_TAG_MASK) != MAG_TAG_DATA) {
	return RTCM_NO_SYNC;
    }
    c = reverse_bits[c & 0x3f];

    /*@ -shiftnegative @*/
    if (!ctx->locked) {
	ctx->curr_offset = -5;
	ctx->bufindex = 0;

	while (ctx->curr_offset <= 0) {
	    gpsd_report(7, "syncing");
	    ctx->curr_word <<= 1;
	    if (ctx->curr_offset > 0) {
		ctx->curr_word |= c << ctx->curr_offset;
	    } else {
		ctx->curr_word |= c >> -(ctx->curr_offset);
	    }
	    if (((struct rtcm_msghw1 *) & ctx->curr_word)->preamble ==
		PREAMBLE_PATTERN) {
		if (rtcmparityok(ctx->curr_word)) {
		    gpsd_report(5, "preamble ok, parity ok -- locked\n");
		    ctx->locked = true;
		    /* ctx->curr_offset;  XXX - testing */
		    break;
		}
		gpsd_report(5, "preamble ok, parity fail\n");
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
		    gpsd_report(6, 
				"Preamble spotted (index: %u)",
				ctx->bufindex);
		    ctx->bufindex = 0;
		}
		gpsd_report(6,
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
			gpsd_report(6, "word 0 not a preamble- punting\n");
			return RTCM_NO_SYNC;
		    }
		    ctx->bufindex++;
		    /* rtcm_print_msg(msghdr); */

		    if (ctx->bufindex > 2) {	/* do we have the length yet? */
			if (ctx->bufindex >= msghdr->w2.frmlen + 2) {
			    res = msghdr;
			    ctx->bufindex = 0;
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
		gpsd_report(5, "Parity failure, lost lock\n");
		ctx->locked = false;
	    }
	}
	ctx->curr_offset -= 6;
	gpsd_report(7, "residual %d", ctx->curr_offset);
	return res;
    }
    /*@ +shiftnegative @*/

    /* never achieved lock */
    return RTCM_NO_SYNC;
}
/*@ +usereleased +compdef @*/

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
