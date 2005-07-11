/******************************************************************************
*									      *
*	File:     rtcm.h						      *
*	Author:   Wolfgang Rupprecht <wolfgang@capsicum.wsrcc.com>	      *
*	Created:  Sun Jan 24 16:47:57 PST 1999				      *
*	Contents: rtcm sc104 message format				      *
*									      *
*	Copyright (c) 1999 Wolfgang Rupprecht.				      *
*	All rights reserved.						      *
*									      *
*	$Id: rtcmmsg.h,v 1.1 2000/02/28 08:08:32 wolfgang Exp $
******************************************************************************/

/*
 * The rtcm words are 30-bit words.  We will lay them into memory into
 * 30-bit (low-end justified) chunks.  To write them out we will write
 * 5 magnavox-format bytes where the low 6-bits of the byte are 6-bits
 * of the 30-word msg.
 */


typedef unsigned int RTCMWORD;

#define MAG_SHIFT 6
#define MAG_TAG_DATA (1 << MAG_SHIFT)
#define MAG_TAG_MASK (3 << MAG_SHIFT)

#define PREAMBLE_PATTERN 0x66
#define PREAMBLE_SHIFT 22
#define PREAMBLE_MASK (0xFF << PREAMBLE_SHIFT)

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

struct msghw1 {			/* header word 1 */
    uint            parity:6;
    uint            refstaid:10;
    uint            msgtype:6;
    uint            preamble:8;	/* 01100110 */
    uint            _pad:2;
};

struct msghw2 {			/* header word 2 */
    uint            parity:6;
    uint            stathlth:3;
    uint            frmlen:5;
    uint            sqnum:3;
    uint            zcnt:13;
    uint            _pad:2;
};

struct msg1w3 {			/* msg 1 word 3 */
    uint            parity:6;
    int             pc1:16;
    uint            satident1:5;
    uint            udre1:2;
    uint            scale1:1;
    uint            _pad:2;
};

struct msg1w4 {			/* msg 1 word 4 */
    uint            parity:6;
    uint            satident2:5;
    uint            udre2:2;
    uint            scale2:1;
    uint            issuedata1:8;
    int             rangerate1:8;
    uint            _pad:2;
};

struct msg1w5 {			/* msg 1 word 5 */
    uint            parity:6;
    int             rangerate2:8;
    int             pc2:16;
    uint            _pad:2;
};


struct msg1w6 {			/* msg 1 word 6 */
    uint            parity:6;
    int             pc3_h:8;
    uint            satident3:5;
    uint            udre3:2;
    uint            scale3:1;
    uint            issuedata2:8;
    uint            _pad:2;
};

struct msg1w7 {			/* msg 1 word 7 */
    uint            parity:6;
    uint            issuedata3:8;
    int             rangerate3:8;
    uint            pc3_l:8;	/* NOTE: uint for low byte */
    uint            _pad:2;
};

struct msghdr {
    struct msghw1   w1;
    struct msghw2   w2;
};

struct msg1 {
    struct msghw1   w1;
    struct msghw2   w2;

    struct msg1w3   w3;		/* clump #1 of 5-corrections each */
    struct msg1w4   w4;
    struct msg1w5   w5;
    struct msg1w6   w6;
    struct msg1w7   w7;

    struct msg1w3   w8;		/* clump #2 of 5-corrections each */
    struct msg1w4   w9;
    struct msg1w5   w10;
    struct msg1w6   w11;
    struct msg1w7   w12;

    struct msg1w3   w13;	/* clump #2 of 5-corrections each */
    struct msg1w4   w14;
    struct msg1w5   w15;
    struct msg1w6   w16;
    struct msg1w7   w17;
};

/* end */
