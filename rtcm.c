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

void rtcm_init(/*@out@*/struct gps_device_t *session)
{
    session->rtcm.curr_word = 0;
    session->rtcm.curr_offset = 24;	/* first word */
    session->rtcm.locked = false;
    session->rtcm.bufindex = 0;
}

/*
 * Structures for interpreting words in an RTCM-104 message (after
 * parity checking and removing inversion).
 *
 * The RTCM words are 30-bit words.  We will lay them into memory into
 * 30-bit (low-end justified) chunks.  To write them out we will write
 * 5 Magnavox-format bytes where the low 6-bits of the byte are 6-bits
 * of the 30-word msg.
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

/* msg header - all msgs */

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

struct rtcm_msghdr {
    struct rtcm_msghw1   w1;
    struct rtcm_msghw2   w2;
};

/* msg 1 - differential gps corrections */

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

/* msg 3 - reference station parameters */

struct rtcm_msg3w3 {
    uint            parity:6;
    uint	    x_h:24;
    uint            _pad:2;
};

struct rtcm_msg3w4 {
    uint            parity:6;
    uint	    y_h:16;
    uint	    x_l:8;
    uint            _pad:2;
};

struct rtcm_msg3w5 {
    uint            parity:6;
    uint	    z_h:8;
    uint	    y_l:16;
    uint            _pad:2;
};

struct rtcm_msg3w6 {
    uint            parity:6;
    uint	    z_l:24;
    uint            _pad:2;
};

struct rtcm_msg3 {
    struct rtcm_msghw1   w1;
    struct rtcm_msghw2   w2;

    struct rtcm_msg3w3   w3;
    struct rtcm_msg3w4   w4;
    struct rtcm_msg3w5   w5;
    struct rtcm_msg3w6   w6;
};

/* msg 4 - reference station datum */

struct rtcm_msg4w3 {
    uint            parity:6;
    uint	    datum_alpha_char2:8;
    uint	    datum_alpha_char1:8;
    uint	    spare:4;
    uint	    dat:1;
    uint	    dgnss:3;
    uint            _pad:2;
};

struct rtcm_msg4w4 {
    uint            parity:6;
    uint	    datum_sub_div_char2:8;
    uint	    datum_sub_div_char1:8;
    uint	    datum_sub_div_char3:8;
    uint            _pad:2;
};

struct rtcm_msg4w5 {
    uint            parity:6;
    uint	    dy_h:8;
    uint	    dx:16;
    uint            _pad:2;
};

struct rtcm_msg4w6 {
    uint            parity:6;
    uint	    dz:24;
    uint	    dy_l:8;
    uint            _pad:2;
};

struct rtcm_msg4 {
    struct rtcm_msghw1   w1;
    struct rtcm_msghw2   w2;

    struct rtcm_msg4w3   w3;
    struct rtcm_msg4w4   w4;
    struct rtcm_msg4w5   w5;		/* optional */
    struct rtcm_msg4w6   w6;		/* optional */
};

/* msg 5 - constellation health */

struct rtcm_msg5w3 {
    uint            parity:6;
    uint	    unassigned:2;
    uint	    time_unhealthy:4;
    uint	    loss_warn:1;
    uint	    new_nav_data:1;
    uint	    health_enable:1;
    uint	    cn0:5;
    uint	    data_health:3;
    uint	    issue_of_data_link:1;
    uint	    sat_id:5;
    uint	    reserved:1;
    uint            _pad:2;
};

struct rtcm_msg5 {
    struct rtcm_msghw1   w1;
    struct rtcm_msghw2   w2;

    struct rtcm_msg5w3   w3;
};

/* msg 6 */

struct rtcm_msg6 {
    struct rtcm_msghw1   w1;
    struct rtcm_msghw2   w2;
};

/* msg 7 - beacon almanac */

struct rtcm_msg7w3 {
    uint            parity:6;
    int	    	    lon_h:8;
    int	            lat:16;
    uint            _pad:2;
};

struct rtcm_msg7w4 {
    uint            parity:6;
    uint	    freq_h:6;
    uint	    range:10;
    uint	    lon_l:8;
    uint            _pad:2;
};

struct rtcm_msg7w5 {
    uint            parity:6;
    uint	    encoding:1;
    uint	    sync_type:1;
    uint	    mod_mode:1;
    uint	    bit_rate:3;

    /*
     * ITU-R M.823-2 page 9 and RTCM-SC104 v2.1 pages 4-21 and 4-22
     * are in conflict over the next two field sizes.  ITU says 9+3,
     * RTCM says 10+2.  The latter correctly decodes the USCG station
     * id's so I'll use that one here. -wsr
     */

    uint	    station_id:10;
    uint	    health:2;
    uint	    freq_l:6;
    uint            _pad:2;
};

struct rtcm_msg7 {
    struct rtcm_msghw1   w1;
    struct rtcm_msghw2   w2;

    struct rtcm_msg7w3   w3;
    struct rtcm_msg7w4   w4;
    struct rtcm_msg7w5   w5;

    struct rtcm_msg7w3   w6;		/* optional 1 */
    struct rtcm_msg7w4   w7;
    struct rtcm_msg7w5   w8;

    struct rtcm_msg7w3   w9;		/* optional 2 ... */
    struct rtcm_msg7w4   w10;
    struct rtcm_msg7w5   w11;
};

/* msg 16 - text msg */

struct rtcm_msg16w3 {
    uint            parity:6;
    uint	    byte3:8;
    uint	    byte2:8;
    uint	    byte1:8;
    uint            _pad:2;
};

struct rtcm_msg16 {
    struct rtcm_msghw1   w1;
    struct rtcm_msghw2   w2;

    struct rtcm_msg16w3   w3;
    struct rtcm_msg16w3   w4;	/* optional */
    struct rtcm_msg16w3   w5;	/* optional */

    struct rtcm_msg16w3   w6;	/* optional */
    struct rtcm_msg16w3   w7;	/* optional */
    struct rtcm_msg16w3   w8;	/* optional */

    struct rtcm_msg16w3   w9;	/* optional */
    struct rtcm_msg16w3   w10;	/* optional */
    struct rtcm_msg16w3   w11;	/* optional ... */
};

/* msg unknown */

struct rtcm_msgunk {
    struct rtcm_msghw1   w1;
    struct rtcm_msghw2   w2;

    uint            w3;
};

static void unpack(struct gps_device_t *session)
/* break out the raw bits into the content fields */
{
    int len;
    unsigned int n;
    struct rtcm_msghdr  *msghdr;

    /* someday we'll do big-endian correction here */
    msghdr = (struct rtcm_msghdr *)session->rtcm.buf;
    session->rtcm.type = msghdr->w1.msgtype;
    session->rtcm.length = msghdr->w2.frmlen;
    session->rtcm.zcount = msghdr->w2.zcnt * ZCOUNT_SCALE;
    session->rtcm.refstaid = msghdr->w1.refstaid;
    session->rtcm.seqnum = msghdr->w2.sqnum;
    session->rtcm.stathlth = msghdr->w2.stathlth;

    len = (int)session->rtcm.length;
    n = 0;
    switch (session->rtcm.type) {
    case 1:
    case 9:
	{
	    struct rtcm_msg1    *m = (struct rtcm_msg1 *) msghdr;

	    while (len >= 0) {
		if (len >= 2) {
		    session->rtcm.ranges.sat[n].ident      = m->w3.satident1;
		    session->rtcm.ranges.sat[n].udre       = m->w3.udre1;
		    session->rtcm.ranges.sat[n].issuedata  = m->w4.issuedata1;
		    session->rtcm.ranges.sat[n].rangerr    = m->w3.pc1 * 
			(m->w3.scale1 ? PCLARGE : PCSMALL);
		    session->rtcm.ranges.sat[n].rangerate  = m->w4.rangerate1 * 
					(m->w3.scale1 ? RRLARGE : RRSMALL);
		    n++;
		}
		if (len >= 4) {
		    session->rtcm.ranges.sat[n].ident      = m->w4.satident2;
		    session->rtcm.ranges.sat[n].udre       = m->w4.udre2;
		    session->rtcm.ranges.sat[n].issuedata  = m->w6.issuedata2;
		    session->rtcm.ranges.sat[n].rangerr    = m->w5.pc2 * 
			(m->w4.scale2 ? PCLARGE : PCSMALL);
		    session->rtcm.ranges.sat[n].rangerate  = m->w5.rangerate2 * 
			(m->w4.scale2 ? RRLARGE : RRSMALL);
		    n++;
		}
		if (len >= 5) {
		    session->rtcm.ranges.sat[n].ident       = m->w6.satident3;
		    session->rtcm.ranges.sat[n].udre        = m->w6.udre3;
		    session->rtcm.ranges.sat[n].issuedata   = m->w7.issuedata3;
		    /*@ -shiftimplementation @*/
		    session->rtcm.ranges.sat[n].rangerr     = ((m->w6.pc3_h<<8)|(m->w7.pc3_l)) *
					(m->w6.scale3 ? PCLARGE : PCSMALL);
		    session->rtcm.ranges.sat[n].rangerate   = m->w7.rangerate3 * 
					(m->w6.scale3 ? RRLARGE : RRSMALL);
		    /*@ +shiftimplementation @*/
		    n++;
		}
		len -= 5;
		m = (struct rtcm_msg1 *) (((RTCMWORD *) m) + 5);
	    }
	    session->rtcm.ranges.nentries = n;
	}
	break;
    case 3:
        {
	    struct rtcm_msg3    *m = (struct rtcm_msg3 *)msghdr;

	    if ((session->rtcm.ecef.valid = len >= 4)) {
		session->rtcm.ecef.x = ((m->w3.x_h<<8)|(m->w4.x_l))*XYZ_SCALE;
		session->rtcm.ecef.y = ((m->w4.y_h<<16)|(m->w5.y_l))*XYZ_SCALE;
		session->rtcm.ecef.z = ((m->w5.z_h<<24)|(m->w6.z_l))*XYZ_SCALE;
	    }
	}
	break;
    case 4:
	{
	    struct rtcm_msg4    *m = (struct rtcm_msg4 *) msghdr;
 
	    if ((session->rtcm.reference.valid = len >= 2)){
		session->rtcm.reference.system =
			(m->w3.dgnss==0) ? gps :
		    		((m->w3.dgnss==1) ? glonass : unknown);
		session->rtcm.reference.sense = (m->w3.dat != 0) ? global : local;
		if (m->w3.datum_alpha_char1){
		    session->rtcm.reference.datum[n++] = (char)(m->w3.datum_alpha_char1);
		}
		if (m->w3.datum_alpha_char2){
		    session->rtcm.reference.datum[n++] = (char)(m->w3.datum_alpha_char2);
		}
		if (m->w4.datum_sub_div_char1){
		    session->rtcm.reference.datum[n++] = (char)(m->w4.datum_sub_div_char1);
		}
		if (m->w4.datum_sub_div_char2){
		    session->rtcm.reference.datum[n++] = (char)(m->w4.datum_sub_div_char2);
		}
		if (m->w4.datum_sub_div_char3){
		    session->rtcm.reference.datum[n++] = (char)(m->w4.datum_sub_div_char3);
		}
		session->rtcm.reference.datum[n++] = '\0';
		if (len >= 4) {
		    session->rtcm.reference.dx = m->w5.dx * DXYZ_SCALE;
		    session->rtcm.reference.dy = ((m->w5.dy_h << 8) | m->w6.dy_l) * DXYZ_SCALE;
		    session->rtcm.reference.dz = m->w6.dz * DXYZ_SCALE;
		} else 
		    session->rtcm.reference.sense = invalid;
	    }
	}
	break;
    case 5:
        {
	    struct rtcm_msg5    *m = (struct rtcm_msg5 *)msghdr;
	    while (len >= 1) {
		session->rtcm.conhealth.sat[n].ident = m->w3.sat_id;
		session->rtcm.conhealth.sat[n].iodl = m->w3.issue_of_data_link!=0;
		session->rtcm.conhealth.sat[n].health = m->w3.data_health!=0;
		/*@i@*/session->rtcm.conhealth.sat[n].snr = (m->w3.cn0?(m->w3.cn0+CNR_OFFSET):-1);
		session->rtcm.conhealth.sat[n].health_en = m->w3.health_enable!=0;
		session->rtcm.conhealth.sat[n].new_data = m->w3.new_nav_data!=0;
		session->rtcm.conhealth.sat[n].los_warning = m->w3.loss_warn!=0;
		session->rtcm.conhealth.sat[n].tou = m->w3.time_unhealthy*TU_SCALE;
		len--;
		n++;
		m = (struct rtcm_msg5 *) (((RTCMWORD *) m) + 1);
	    }
	    session->rtcm.conhealth.nentries = n;
	}
	break;
    case 7:
	{
	    struct rtcm_msg7    *m = (struct rtcm_msg7 *) msghdr;
	    unsigned int tx_speed[] = { 25, 50, 100, 110, 150, 200, 250, 300 };

	    while (len >= 3) {
		session->rtcm.almanac.station[n].latitude = m->w3.lat * LA_SCALE;
		session->rtcm.almanac.station[n].longitude = ((m->w3.lon_h << 8) | m->w4.lon_l) * LO_SCALE;
		session->rtcm.almanac.station[n].range = m->w4.range;
		session->rtcm.almanac.station[n].frequency = (((m->w4.freq_h << 6) | m->w5.freq_l) * FREQ_SCALE) + FREQ_OFFSET;
		session->rtcm.almanac.station[n].health = m->w5.health;
		session->rtcm.almanac.station[n].station_id = m->w5.station_id,
		session->rtcm.almanac.station[n].bitrate = tx_speed[m->w5.bit_rate];
		len -= 3;
		n++;
		m = (struct rtcm_msg7 *) (((RTCMWORD *) m) + 3);
	    }
	    session->rtcm.almanac.nentries = n;
	}
	break;
    default:
	break;
    }
}

/*@ -usereleased -compdef @*/
enum rtcmstat_t rtcm_decode(struct gps_device_t *session, unsigned int c)
{
    enum rtcmstat_t res;

    if ((c & MAG_TAG_MASK) != MAG_TAG_DATA) {
	return RTCM_NO_SYNC;
    }
    c = reverse_bits[c & 0x3f];

    /*@ -shiftnegative @*/
    if (!session->rtcm.locked) {
	session->rtcm.curr_offset = -5;
	session->rtcm.bufindex = 0;

	while (session->rtcm.curr_offset <= 0) {
	    gpsd_report(RTCM_ERRLEVEL_BASE+2, "syncing");
	    session->rtcm.curr_word <<= 1;
	    if (session->rtcm.curr_offset > 0) {
		session->rtcm.curr_word |= c << session->rtcm.curr_offset;
	    } else {
		session->rtcm.curr_word |= c >> -(session->rtcm.curr_offset);
	    }
	    if (((struct rtcm_msghw1 *) & session->rtcm.curr_word)->preamble ==
		PREAMBLE_PATTERN) {
		if (rtcmparityok(session->rtcm.curr_word)) {
		    gpsd_report(RTCM_ERRLEVEL_BASE+1, 
				"preamble ok, parity ok -- locked\n");
		    session->rtcm.locked = true;
		    /* session->rtcm.curr_offset;  XXX - testing */
		    break;
		}
		gpsd_report(RTCM_ERRLEVEL_BASE+1, 
			    "preamble ok, parity fail\n");
	    }
	    session->rtcm.curr_offset++;
	}			/* end while */
    }
    if (session->rtcm.locked) {
	res = RTCM_SYNC;

	if (session->rtcm.curr_offset > 0) {
	    session->rtcm.curr_word |= c << session->rtcm.curr_offset;
	} else {
	    session->rtcm.curr_word |= c >> -(session->rtcm.curr_offset);
	}

	if (session->rtcm.curr_offset <= 0) {
	    /* weird-assed inversion */
	    if (session->rtcm.curr_word & P_30_MASK)
		session->rtcm.curr_word ^= W_DATA_MASK;

	    if (rtcmparityok(session->rtcm.curr_word)) {
#if 0
		/*
		 * Don't clobber the buffer just because we spot
		 * another preamble pattern in the data stream. -wsr
		 */
		if (((struct rtcm_msghw1 *) & session->rtcm.curr_word)->preamble ==
		    PREAMBLE_PATTERN) {
		    gpsd_report(RTCM_ERRLEVEL_BASE+2, 
				"Preamble spotted (index: %u)\n",
				session->rtcm.bufindex);
		    session->rtcm.bufindex = 0;
		}
#endif
		gpsd_report(RTCM_ERRLEVEL_BASE+2,
			    "processing word %u (offset %d)\n",
			    session->rtcm.bufindex, session->rtcm.curr_offset);
		{
		    struct rtcm_msghdr  *msghdr = (struct rtcm_msghdr *) session->rtcm.buf;

		    /*
		     * Guard against a buffer overflow attack.  Just wait for
		     * the next PREAMBLE_PATTERN and go on from there. 
		     */
		    if (session->rtcm.bufindex >= RTCM_WORDS_MAX){
			session->rtcm.bufindex = 0;
			gpsd_report(RTCM_ERRLEVEL_BASE+1, 
				    "RTCM buffer overflowing -- resetting\n");
			return RTCM_NO_SYNC;
		    }

		    session->rtcm.buf[session->rtcm.bufindex] = session->rtcm.curr_word;

		    if ((session->rtcm.bufindex == 0) &&
			(msghdr->w1.preamble != PREAMBLE_PATTERN)) {
			gpsd_report(RTCM_ERRLEVEL_BASE+1, 
				    "word 0 not a preamble- punting\n");
			return RTCM_NO_SYNC;
		    }
		    session->rtcm.bufindex++;
		    /* rtcm_print_msg(msghdr); */

		    if (session->rtcm.bufindex >= 2) {	/* do we have the length yet? */
			if (session->rtcm.bufindex >= msghdr->w2.frmlen + 2) {
			    /* jackpot, we have an RTCM packet*/
			    res = RTCM_STRUCTURE;
			    session->rtcm.bufindex = 0;
			    unpack(session);
			}
		    }
		}
		session->rtcm.curr_word <<= 30;	/* preserve the 2 low bits */
		session->rtcm.curr_offset += 30;
		if (session->rtcm.curr_offset > 0) {
		    session->rtcm.curr_word |= c << session->rtcm.curr_offset;
		} else {
		    session->rtcm.curr_word |= c >> -(session->rtcm.curr_offset);
		}
	    } else {
		gpsd_report(RTCM_ERRLEVEL_BASE+0, 
			    "parity failure, lost lock\n");
		session->rtcm.locked = false;
	    }
	}
	session->rtcm.curr_offset -= 6;
	gpsd_report(RTCM_ERRLEVEL_BASE+2, "residual %d", session->rtcm.curr_offset);
	return res;
    }
    /*@ +shiftnegative @*/

    /* never achieved lock */
    return RTCM_NO_SYNC;
}
/*@ +usereleased +compdef @*/

void rtcm_dump(struct gps_device_t *session, /*@out@*/char buf[], size_t buflen)
/* dump the contents of a parsed RTCM104 message */
{
    unsigned int n;

    (void)snprintf(buf, buflen, "H\t%u\t%u\t%0.1f\t%u\t%u\t%u\n",
	   session->rtcm.type,
	   session->rtcm.refstaid,
	   session->rtcm.zcount,
	   session->rtcm.seqnum,
	   session->rtcm.length,
	   session->rtcm.stathlth);

    switch (session->rtcm.type) {
    case 1:
    case 9:
	for (n = 0; n < session->rtcm.ranges.nentries; n++)
	    (void)snprintf(buf + strlen(buf), buflen - strlen(buf),
			   "S\t%u\t%u\t%u\t%0.1f\t%0.3f\t%0.3f\n",
			   session->rtcm.ranges.sat[n].ident,
			   session->rtcm.ranges.sat[n].udre,
			   session->rtcm.ranges.sat[n].issuedata,
			   session->rtcm.zcount,
			   session->rtcm.ranges.sat[n].rangerr,
			   session->rtcm.ranges.sat[n].rangerate);
	break;

#if 0				/* I was too slow in getting these in. -wsr */

    case 3:
	if (session->rtcm.ecef.valid)
	    (void)snprintf(buf + strlen(buf), buflen - strlen(buf),
			   "R\t%.2f\t%.2f\t%.2f\n",
			   session->rtcm.ecef.x, 
			   session->rctm.ecef.y,
			   session->rtcm.ecef.z);
	break;

    case 4:
	if (session->rtcm.reference.valid)
	    (void)snprintf(buf + strlen(buf), buflen - strlen(buf),
			   "D\t%s\t%1d\t%s\t%.1f\t%.1f\t%.1f\n",
			   (session->rtcm.reference.system==gps) ? "GPS"
			   : ((session->rtcm.reference.system==glonass) ? "GLONASS"
			      : "UNKNOWN"),
			   session->rtcm.reference.sense,
			   session->rtcm.reference.datum,
			   session->rtcm.reference.dx,
			   session->rtcm.reference.dy,
			   session->rtcm.reference.dz);
	break;

    case 5:
	for (n = 0; n < session->rtcm.conhealth.nentries; n++)
	    (void)snprintf(buf + strlen(buf), buflen - strlen(buf),
			   "C\t%2d\t%1d  %1d\t%2d\t%1d  %1d  %1d\t%2d\n",
			   session->rtcm.conhealth.sat[n].satident,
			   session->rtcm.conhealth.sat[n].iodl,
			   session->rtcm.conhealth.sat[n].snr,
			   session->rtcm.conhealth.sat[n].health_en,
			   session->rtcm.conhealth.sat[n].new_data,
			   session->rtcm.conhealth.sat[n].los_warn,
			   session->rtcm.conhealth.sat[n].tou);
	}
	break;

    case 6: 			/* NOP msg */
	strcat(buf, "N\n");
	break;

    case 7:
	for (n = 0; n < session->rtcm.almanac.nentries; n++)
	    (void)snprintf(buf + strlen(buf), buflen - strlen(buf),
		printf("A\t%.4f\t%.4f\t%d\t%.1f\t%d\t%d\t%d\n",
		       session->rtcm.almanac.station[n].latitude.
		       session->rtcm.almanac.station[n].longitude.
		       session->rtcm.almanac.station[n].range.
		       session->rtcm.almanac.station[n].frequency.
		       session->rtcm.almanac.station[n].health.
		       session->rtcm.almanac.station[n].station_id.
		       session->rtcm.almanac.station[n].bitrate);
	break;

    case 16:
	{
	    struct msg16    *m = (struct msg16 *) msghdr;

	    printf("T\t\"");	/* Note: text is now quoted in the output */
	    while (len >= 1){
		if (!m->w3.byte1) {
		    break;
		}
		putchar (m->w3.byte1);
		if (!m->w3.byte2) {
		    break;
		}
		putchar (m->w3.byte2);
		if (!m->w3.byte3) {
		    break;
		}
		putchar (m->w3.byte3);
		len--;
		m = (struct msg16 *) (((RTCMWORD *) m) + 1);
	    }
	    printf("\"\n");
	}
	break;

    default:
	{
	    struct msgunk    *m = (struct msgunk *) msghdr;

	    printf ("U\t*** Unknown msg type! (len %d) ***\n", len);
	    while (len >= 1){
		printf ("U\t0x%08x\n", m->w3);
		len--;
		m = (struct msgunk *) (((RTCMWORD *) m) + 1);
	    }
	}
	break;
#endif

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
