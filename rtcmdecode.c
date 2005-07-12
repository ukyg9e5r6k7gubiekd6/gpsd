#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "rtcm.h"

static int             verbose = 0;

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

    printf("parity %u\n", p);
    return (p);
}


static bool rtcmparityok(RTCMWORD w)
{
    return (rtcmparity(w) == (w & 0x3f));
}

void rtcm_init(/*@out@*/struct rtcm_ctx * ctx)
{
    ctx->curr_word = 0;
    ctx->curr_offset = 24;	/* first word */
    ctx->locked = false;
    ctx->bufindex = 0;
}

static void process_word(struct rtcm_ctx * ctx, RTCMWORD r)
{
    struct rtcm_msghdr  *msghdr = (struct rtcm_msghdr *) ctx->buf;

    /*
     * Guard against a buffer overflow attack.  Just wait for
     * the next PREAMBLE_PATTERN and go on from there. 
     */
    if (ctx->bufindex >= RTCM_CTX_MAX_MSGSZ){
	ctx->bufindex = 0;
	return;
    }

    ctx->buf[ctx->bufindex] = r;

    if ((ctx->bufindex == 0) &&
	(msghdr->w1.preamble != PREAMBLE_PATTERN)) {
	if (verbose)
	    fprintf(stderr, "word 0 not a preamble- punting\n");
	return;
    }
    ctx->bufindex++;
    /* rtcm_print_msg(msghdr); */

    if (ctx->bufindex > 2) {	/* do we have the length yet? */
	if (ctx->bufindex >= msghdr->w2.frmlen + 2) {
	    rtcm_print_msg(msghdr);
	    /* do other processing here */
	    ctx->bufindex = 0;
	    bzero((char *)ctx->buf, (int)sizeof(ctx->buf));	/* XXX debug */
	}
    }
}

void rtcm_decode(struct rtcm_ctx * ctx, unsigned int c)
{
    if ((c & MAG_TAG_MASK) != MAG_TAG_DATA) {
	return;
    }
    c = reverse_bits[c & 0x3f];

    if (verbose)
	(void)putc('.', stderr);

    /*@ -shiftnegative @*/
    if (!ctx->locked) {
	ctx->curr_offset = -5;
	ctx->bufindex = 0;

	while (ctx->curr_offset <= 0) {
	    /* complain("syncing"); */
	    ctx->curr_word <<= 1;
	    if (ctx->curr_offset > 0) {
		ctx->curr_word |= c << ctx->curr_offset;
	    } else {
		ctx->curr_word |= c >> -(ctx->curr_offset);
	    }
	    if (((struct rtcm_msghw1 *) & ctx->curr_word)->preamble ==
		PREAMBLE_PATTERN) {
		if (rtcmparityok(ctx->curr_word)) {
		    (void)putc('\n', stderr);
		    fprintf(stderr, "preamble ok, parity ok -- locked\n");
		    ctx->locked = true;
		    /* ctx->curr_offset;  XXX - testing */
		    break;
		}
		(void)putc('\n', stderr);
		fprintf(stderr, "preamble ok, parity fail\n");
	    }
	    ctx->curr_offset++;
	}			/* end while */
    }
    if (ctx->locked) {
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
		    if (verbose)
			fprintf(stderr, 
				"Preamble spotted (index: %u)",
				ctx->bufindex);
		    ctx->bufindex = 0;
		}
		if (verbose)
		    fprintf(stderr,
			    "processing word %u (offset %d)\n",
			    ctx->bufindex, ctx->curr_offset);
		process_word(ctx, ctx->curr_word);
		ctx->curr_word <<= 30;	/* preserve the 2 low bits */
		ctx->curr_offset += 30;
		if (ctx->curr_offset > 0) {
		    ctx->curr_word |= c << ctx->curr_offset;
		} else {
		    ctx->curr_word |= c >> -(ctx->curr_offset);
		}
	    } else {
		(void)putc('\n', stderr);
		fprintf(stderr, "Parity failure, lost lock\n");
		ctx->locked = false;
	    }
	}
	ctx->curr_offset -= 6;
	/* complain("residual %d", ctx->curr_offset); */
    }
    /*@ +shiftnegative @*/
}

void rtcm_print_msg(struct rtcm_msghdr *msghdr)
{
    int             len = (int)msghdr->w2.frmlen;
    double          zcount = msghdr->w2.zcnt * ZCOUNT_SCALE;

    printf("H\t%u\t%u\t%0.1f\t%u\t%u\t%u\n",
	   msghdr->w1.msgtype,
	   msghdr->w1.refstaid,
	   zcount,
	   msghdr->w2.sqnum,
	   msghdr->w2.frmlen,
	   msghdr->w2.stathlth);
    switch (msghdr->w1.msgtype) {
    case 1:
    case 9:
	{
	    struct rtcm_msg1    *m = (struct rtcm_msg1 *) msghdr;

	    while (len >= 0) {
		if (len >= 2)
		    printf("S\t%u\t%u\t%u\t%0.1f\t%0.3f\t%0.3f\n",
			   m->w3.satident1,
			   m->w3.udre1,
			   m->w4.issuedata1,
			   zcount,
			   m->w3.pc1 * (m->w3.scale1 ? PCLARGE : PCSMALL),
			   m->w4.rangerate1 * (m->w3.scale1 ?
					       RRLARGE : RRSMALL));
		if (len >= 4)
		    printf("S\t%u\t%u\t%u\t%0.1f\t%0.3f\t%0.3f\n",
			   m->w4.satident2,
			   m->w4.udre2,
			   m->w6.issuedata2,
			   zcount,
			   m->w5.pc2 * (m->w4.scale2 ? PCLARGE : PCSMALL),
			   m->w5.rangerate2 * (m->w4.scale2 ?
					       RRLARGE : RRSMALL));
		
		/*@ -shiftimplementation @*/
		if (len >= 5)
		    printf("S\t%u\t%u\t%u\t%0.1f\t%0.3f\t%0.3f\n",
			   m->w6.satident3,
			   m->w6.udre3,
			   m->w7.issuedata3,
			   zcount,
			   ((m->w6.pc3_h << 8) | (m->w7.pc3_l)) *
			   (m->w6.scale3 ? PCLARGE : PCSMALL),
			   m->w7.rangerate3 * (m->w6.scale3 ?
					       RRLARGE : RRSMALL));
		/*@ +shiftimplementation @*/
		len -= 5;
		m = (struct rtcm_msg1 *) (((RTCMWORD *) m) + 5);
	    }
	}
	break;
    default:
	break;
    }
    /* complain(""); */
}

int main(int argc, char **argv)
{
    int             c;
    struct rtcm_ctx ctxbuf,
                   *ctx = &ctxbuf;

    while ((c = getopt(argc, argv, "v:")) != EOF) {
	switch (c) {
	case 'v':		/* verbose */
	    verbose = atoi(optarg);
	    break;

	case '?':
	default:
	    /* usage(); */
	    break;
	}
    }
    argc -= optind;
    argv += optind;

    rtcm_init(ctx);

    while ((c = getchar()) != EOF) {
	rtcm_decode(ctx, (unsigned int)c);
    }
    exit(0);
}

/* end */
