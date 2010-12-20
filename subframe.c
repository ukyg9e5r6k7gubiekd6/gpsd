/* subframe.c -- interpret satellite subframe data.
 *
 * This file is Copyright (c) 2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 */

#include "gpsd.h"
#include "timebase.h"

#ifdef __NOT_YET__
static char sf4map[] =
    { -1, 57, 25, 26, 27, 28, 57, 29, 30, 31, 32, 57, 62, 52, 53, 54, 57, 55,
    56, 58, 59, 57, 60, 61, 62, 63
};

static char sf5map[] =
    { -1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
    20, 21, 22, 23, 24, 51
};
#endif

/*@ -usedef @*/
int gpsd_interpret_subframe_raw(struct gps_device_t *session,
				unsigned int svid, unsigned int words[])
{
    unsigned int i;
    unsigned int preamble, parity;

    /*
     * This function assumes an array of 10 ints, each of which carries
     * a raw 30-bit GPS word use your favorite search engine to find the
     * latest version of the specification: IS-GPS-200.
     *
     * Each raw 30-bit word is made of 24 data bits and 6 parity bits. The
     * raw word and transport word are emitted from the GPS MSB-first and
     * right justified. In other words, masking the raw word against 0x3f
     * will return just the parity bits. Masking with 0x3fffffff and shifting
     * 6 bits to the right returns just the 24 data bits. The top two bits
     * (b31 and b30) are undefined; chipset designers may store copies of
     * the bits D29* and D30* here to aid parity checking.
     *
     * Since bits D29* and D30* are not available in word 0, it is tested for
     * a known preamble to help check its validity and determine whether the
     * word is inverted.
     *
     */
    gpsd_report(LOG_IO, "50B: gpsd_interpret_subframe_raw: "
		"%08x %08x %08x %08x %08x %08x %08x %08x %08x %08x\n",
		words[0], words[1], words[2], words[3], words[4],
		words[5], words[6], words[7], words[8], words[9]);

    preamble = (words[0] >> 22) & 0xff;
    if (preamble == 0x8b) {	/* preamble is inverted */
	words[0] ^= 0x3fffffc0;	/* invert */
    } else if (preamble != 0x74) {
	/* strangely this is very common, so don't log it */
	gpsd_report(LOG_IO,
		    "50B: gpsd_interpret_subframe_raw: bad preamble 0x%x\n",
		    preamble);
	return 0;
    }
    words[0] = (words[0] >> 6) & 0xffffff;

    for (i = 1; i < 10; i++) {
	int invert;
	/* D30* says invert */
	invert = (words[i] & 0x40000000) ? 1 : 0;
	/* inverted data, invert it back */
	if (invert) {
	    words[i] ^= 0x3fffffc0;
	}
	parity = isgps_parity(words[i]);
	if (parity != (words[i] & 0x3f)) {
	    gpsd_report(LOG_IO,
			"50B: gpsd_interpret_subframe_raw parity fail words[%d] 0x%x != 0x%x\n",
			i, parity, (words[i] & 0x1));
	    return 0;
	}
	words[i] = (words[i] >> 6) & 0xffffff;
    }

    gpsd_interpret_subframe(session, svid, words);
    return 0;
}

static void subframe_almanac( unsigned int svid, unsigned int words[], 
			     unsigned int subframe, unsigned int sv,
			     unsigned int data_id)
{
    unsigned int e      = ( words[2] & 0x00FFFF);
    /* carefull, each SV can have more than 2 toa's active at the same time 
     * you can not just store one or two almanacs for each sat */
    unsigned int toa    = ((words[3] >> 16) & 0x0000FF);
    unsigned int deltai = ( words[3] & 0x00FFFF);
    unsigned int omegad = ((words[4] >>  8) & 0x00FFFF);
    unsigned int svh    = ( words[4] & 0x0000FF);
    unsigned int sqrtA  = ( words[5] & 0xFFFFFF);
    unsigned int Omega0 = ( words[6] & 0xFFFFFF);
    unsigned int omega  = ( words[7] & 0xFFFFFF);
    unsigned int M0     = ( words[8] & 0xFFFFFF);
    unsigned int af1    = ((words[9] >>  5) & 0x0003FF);
    unsigned int af0    = ((words[9] >> 16) & 0x0000FF);
    af0 <<= 3;
    af0                += ((words[9] >>  2) & 0x000003);
    gpsd_report(LOG_PROG,
	"50B: SF:%d SV:%2u TSV:%2u data_id %d e:%u toa:%u deltai:%u omegad:%u"
	" svh:%u sqrtA:%u Omega0:%u omega:%u M0:%u af0:%u af1:%u\n",
        subframe, sv, svid, data_id, e, toa, deltai, omegad, svh, sqrtA, 
	Omega0, omega, M0, af0, af1);
}

void gpsd_interpret_subframe(struct gps_device_t *session,
			     unsigned int svid, unsigned int words[])
{
    /*
     * Heavy black magic begins here!
     *
     * A description of how to decode these bits is at
     * <http://home-2.worldonline.nl/~samsvl/nav2eu.htm>
     *
     * We're mostly looking for subframe 4 page 18 word 9, the leap second
     * correction. This functions assumes an array of words without parity
     * or inversion (inverted word 0 is OK). It may be called directly by a
     * driver if the chipset emits acceptable data.
     *
     * To date this code has been tested on iTrax, SiRF and ublox.
     */
    /* FIXME!! I really doubt this is Big Endian copmatible */
    unsigned int pageid, subframe, data_id, preamble;
    unsigned int tow17;
    unsigned char alert, antispoof;
    gpsd_report(LOG_IO,
		"50B: gpsd_interpret_subframe: (%d) "
		"%06x %06x %06x %06x %06x %06x %06x %06x %06x %06x\n",
		svid, words[0], words[1], words[2], words[3], words[4],
		words[5], words[6], words[7], words[8], words[9]);

    preamble = (unsigned int)((words[0] >> 16) & 0x0ffL);
    if (preamble == 0x8b) {
	preamble ^= 0xff;
	words[0] ^= 0xffffff;
    }
    if (preamble != 0x74) {
	gpsd_report(LOG_WARN,
		    "50B: gpsd_interpret_subframe bad preamble: 0x%x header 0x%x\n",
		    preamble, words[0]);
	return;
    }
    /* The subframe ID is in the Hand Over Word (page 80) */
    tow17 = ((words[1] >> 7) & 0x01FFFF);
    subframe = ((words[1] >> 2) & 0x07);
    alert = ((words[1] >> 6) & 0x01);
    antispoof = ((words[1] >> 6) & 0x01);
    gpsd_report(LOG_PROG,
		"50B: SF:%d SV:%2u TOW17:%6u Alert:%u AS:%u"
		"\n", 
			subframe, svid, tow17, alert, antispoof);
    /*
     * Consult the latest revision of IS-GPS-200 for the mapping
     * between magic SVIDs and pages.
     */
    pageid  = (words[2] >> 16) & 0x00003F; /* only in frames 4 & 5 */
    data_id = (words[2] >> 22) & 0x3;      /* only in frames 4 & 5 */

    switch (subframe) {
    case 1:
	/* subframe 1: clock parameters for transmitting SV */
	/* get Week Number (WN) from subframe 1 */
	{
	    unsigned int l2   = ((words[2] >> 10) & 0x000003); /* L2 Code */
	    unsigned int ura  = ((words[2] >>  8) & 0x00000F); /* URA Index */
	    unsigned int hlth = ((words[2] >>  2) & 0x00003F); /* SV health */
	    unsigned int iodc = ( words[2] & 0x000003); /* IODC 2 MSB */
	    unsigned int l2p  = ((words[3] >> 23) & 0x000001); /* L2 P flag */
	    unsigned int tgd  = ( words[6] & 0x0000FF);
	    unsigned int toc  = ( words[7] & 0x00FFFF);
	    unsigned int af2  = ((words[8] >> 16) & 0x0FF);
	    unsigned int af1  = ( words[8] & 0x00FFFF);
	    unsigned int af0  = ((words[9] >>  1) & 0x03FFFFF);
	    iodc <<= 8;
	    iodc += ((words[7] >> 16) & 0x00FF);
	    session->context->gps_week =
		(unsigned short)((words[2] >> 14) & 0x03ff);
	    gpsd_report(LOG_PROG, "50B: SF:1 SV:%2u WN:%4u IODC:%4u"
		" L2:%u ura:%u hlth:%u L2P:%u"
		" Tgd:%u toc:%u af2:%3u"
	        " af1:%5u af0:%7u\n", svid,
	    	session->context->gps_week, iodc,
		l2, ura, hlth, l2p,
		tgd, toc, af2, af1, af0);
	}
	break;
    case 2:
	/* subframe 2: ephemeris for transmitting SV */
	{
	    unsigned int iode   = ((words[2] >> 16) & 0x00FF);
	    unsigned int crs    = ( words[2] & 0x00FFFF);
	    unsigned int deltan = ((words[3] >>  8) & 0x00FFFF);
	    unsigned int m0     = ( words[3] & 0x0000FF);
	    m0 <<= 24;
	    m0                 += ( words[4] & 0x00FFFFFF);
	    unsigned int cuc    = ((words[5] >>  8) & 0x00FFFF);
	    unsigned int e      = ( words[5] & 0x0000FF);
	    e <<= 24;
	    e                  += ( words[6] & 0x00FFFFFF);
	    unsigned int cus    = ((words[7] >>  8) & 0x00FFFF);
	    unsigned int sqrta  = ( words[7] & 0x0000FF);
	    sqrta <<= 24;
	    sqrta              += ( words[8] & 0x00FFFFFF);
	    unsigned int toe    = ((words[9] >>  8) & 0x00FFFF);
	    unsigned int fit    = ((words[9] >>  7) & 0x000001);
	    unsigned int aodo   = ((words[9] >>  2) & 0x00001F);
	    gpsd_report(LOG_PROG,
		"50B: SF:2 SV:%2u IODE:%u Crs:%u deltan:%u m0:%u "
		"Cuc:%u e:%u Cus:%u sqrtA:%u toe:%u FIT:%u AODO:%u\n", 
		    svid, iode, crs, deltan, m0,
		    cuc, e, cus, sqrta, toe, fit, aodo);
	}
	break;
    case 3:
	/* subframe 3: ephemeris for transmitting SV */
	{
	    unsigned int cic = ((words[2] >>  8) & 0x00FFFF);
	    unsigned int om0 = ( words[2] & 0x0000FF);
	    om0 <<= 24;
	    om0             += ( words[3] & 0x00FFFFFF);
	    unsigned int cis = ((words[4] >>  8) & 0x00FFFF);
	    unsigned int i0  = ( words[4] & 0x0000FF);
	    i0  <<= 24;
	    i0              += ( words[5] & 0x00FFFFFF);
	    unsigned int crc = ((words[6] >>  8) & 0x00FFFF);
	    unsigned int om  = ( words[6] & 0x0000FF);
	    om  <<= 24;
	    om              += ( words[7] & 0x00FFFFFF);
	    unsigned int omd = ( words[8] & 0x00FFFFFF);
	    unsigned int iode = (words[9] & 0xFF0000) >> 16; 
	    unsigned int iote = (words[9] & 0x003FFF) >>  2; 
	    gpsd_report(LOG_PROG,
		"50B: SF:3 SV:%2u IODE:%3u IOTE:%u Cic:%u om0:%u Cis:%u i0:%u "
		" crc:%u om:%u omd:%u\n", 
			svid, iode, iote, cic, om0, cis, i0, crc, om, omd );
	}
	break;
    case 4:
	{
	    int sv = -2;
	    switch (pageid) {
	    case 0:
		/* almanac for dummy sat 0, which is same as transmitting sat */
		sv = 0;
		break;
	    case 1:
	    case 6:
	    case 11:
	    case 16:
	    case 21:
	    case 57:
		/* for some inscutable reason these pages are all sent
		 * as page 57, IS-GPS-200E Table 20-V */
		break;
	    case 12:
	    case 24:
	    case 62:
		/* for some inscrutable reason these pages are all sent
		 * as page 62, IS-GPS-200E Table 20-V */
		break;
	    case 14:
	    case 53:
		/* for some inscrutable reason page 14 is sent
		 * as page 53, IS-GPS-200E Table 20-V */
		break;
	    case 15:
	    case 54:
		/* for some inscrutable reason page 15 is sent
		 * as page 54, IS-GPS-200E Table 20-V */
		break;
	    case 19:
		/* for some inscrutable reason page 20 is sent
		 * as page 58, IS-GPS-200E Table 20-V */
		/* reserved page */
		break;
	    case 20:
		/* for some inscrutable reason page 20 is sent
		 * as page 59, IS-GPS-200E Table 20-V */
		/* reserved page */
		break;
	    case 22:
	    case 60:
		/* for some inscrutable reason page 22 is sent
		 * as page 60, IS-GPS-200E Table 20-V */
		/* reserved page */
		break;
	    case 23:
	    case 61:
		/* for some inscrutable reason page 23 is sent
		 * as page 61, IS-GPS-200E Table 20-V */
		/* reserved page */
		break;

	    /* almanac data for SV 25 through 32 respectively; */
	    case 2:
		sv = 25;
		break;
	    case 3:
		sv = 26;
		break;
	    case 4:
		sv = 27;
		break;
	    case 5:
		sv = 28;
		break;
	    case 7:
		sv = 29;
		break;
	    case 8:
		sv = 30;
		break;
	    case 9:
		sv = 31;
		break;
	    case 10:
		sv = 32;
		break;

	    case 13:
	    case 52:
		/* NMCT */
		{
		    sv = -1;
		    unsigned char erd[33];
		    unsigned char ai  = ((words[2] >> 22) & 0x000003);
		    erd[1]  = ((words[2] >>  8) & 0x00003F);
		    erd[2]  = ((words[2] >>  2) & 0x00003F);
		    erd[3]  = ((words[2] >>  0) & 0x000003);
		    erd[3] <<= 2;
		    erd[3] += ((words[3] >> 20) & 0x00000F);

		    erd[4]  = ((words[3] >> 14) & 0x00003F);
		    erd[5]  = ((words[3] >>  8) & 0x00003F);
		    erd[6]  = ((words[3] >>  2) & 0x00003F);
		    erd[7]  = ((words[3] >>  0) & 0x000003);

		    erd[7] <<= 2;
		    erd[7] += ((words[4] >> 20) & 0x00000F);
		    erd[8]  = ((words[4] >> 14) & 0x00003F);
		    erd[9]  = ((words[4] >>  8) & 0x00003F);
		    erd[10] = ((words[4] >>  2) & 0x00003F);
		    erd[11] = ((words[4] >>  0) & 0x00000F);

		    erd[11] <<= 2;
		    erd[11] += ((words[5] >> 20) & 0x00000F);
		    erd[12]  = ((words[5] >> 14) & 0x00003F);
		    erd[13]  = ((words[5] >>  8) & 0x00003F);
		    erd[14]  = ((words[5] >>  2) & 0x00003F);
		    erd[15]  = ((words[5] >>  0) & 0x000003);

		    erd[15] <<= 2;
		    erd[15] += ((words[6] >> 20) & 0x00000F);
		    erd[16]  = ((words[6] >> 14) & 0x00003F);
		    erd[17]  = ((words[6] >>  8) & 0x00003F);
		    erd[18]  = ((words[6] >>  2) & 0x00003F);
		    erd[19]  = ((words[6] >>  0) & 0x000003);

		    erd[19] <<= 2;
		    erd[19] += ((words[7] >> 20) & 0x00000F);
		    erd[20]  = ((words[7] >> 14) & 0x00003F);
		    erd[21]  = ((words[7] >>  8) & 0x00003F);
		    erd[22]  = ((words[7] >>  2) & 0x00003F);
		    erd[23]  = ((words[7] >>  0) & 0x000003);

		    erd[23] <<= 2;
		    erd[23] += ((words[8] >> 20) & 0x00000F);
		    erd[24]  = ((words[8] >> 14) & 0x00003F);
		    erd[25]  = ((words[8] >>  8) & 0x00003F);
		    erd[26]  = ((words[8] >>  2) & 0x00003F);
		    erd[27]  = ((words[8] >>  0) & 0x000003);

		    erd[27] <<= 2;
		    erd[27] += ((words[9] >> 20) & 0x00000F);
		    erd[28]  = ((words[9] >> 14) & 0x00003F);
		    erd[29]  = ((words[9] >>  8) & 0x00003F);
		    erd[30]  = ((words[9] >>  2) & 0x00003F);

		    gpsd_report(LOG_PROG, "50B: SF:4-13 data_id %d ai:%u "
			"ERD1:%u ERD2:%u ERD3:%u ERD4:%u "
			"ERD5:%u ERD6:%u ERD7:%u ERD8:%u "
			"ERD9:%u ERD10:%u ERD11:%u ERD12:%u "
			"ERD13:%u ERD14:%u ERD15:%u ERD16:%u "
			"ERD17:%u ERD18:%u ERD19:%u ERD20:%u "
			"ERD21:%u ERD22:%u ERD23:%u ERD24:%u "
			"ERD25:%u ERD26:%u ERD27:%u ERD28:%u "
			"ERD29:%u ERD30:%u\n",
				data_id, ai,
				erd[1], erd[2], erd[3], erd[4],
				erd[5], erd[5], erd[6], erd[4],
				erd[9], erd[10], erd[11], erd[12],
				erd[13], erd[14], erd[15], erd[16],
				erd[17], erd[18], erd[19], erd[20],
				erd[21], erd[22], erd[23], erd[24],
				erd[25], erd[26], erd[27], erd[28],
				erd[29], erd[30]);
		}
		break;

	    case 25:
	    case 63:
		/* for some inscrutable reason page 25 is sent
		 * as page 63, IS-GPS-200E Table 20-V */
		/* A-S flags/SV configurations for 32 SVs, 
		 * plus SV health for SV 25 through 32
		 */
		{
		    sv = -1;
		    unsigned char svf[33];
		    svf[1]  = ((words[2] >> 12) & 0x00000F);
		    svf[2]  = ((words[2] >>  8) & 0x00000F);
		    svf[3]  = ((words[2] >>  4) & 0x00000F);
		    svf[4]  = ((words[2] >>  0) & 0x00000F);
		    svf[5]  = ((words[3] >> 20) & 0x00000F);
		    svf[6]  = ((words[3] >> 16) & 0x00000F);
		    svf[7]  = ((words[3] >> 12) & 0x00000F);
		    svf[8]  = ((words[3] >>  8) & 0x00000F);
		    svf[9]  = ((words[3] >>  4) & 0x00000F);
		    svf[10] = ((words[3] >>  0) & 0x00000F);
		    svf[11] = ((words[4] >> 20) & 0x00000F);
		    svf[12] = ((words[4] >> 16) & 0x00000F);
		    svf[13] = ((words[4] >> 12) & 0x00000F);
		    svf[14] = ((words[4] >>  8) & 0x00000F);
		    svf[15] = ((words[4] >>  4) & 0x00000F);
		    svf[16] = ((words[4] >>  0) & 0x00000F);
		    svf[17] = ((words[5] >> 20) & 0x00000F);
		    svf[18] = ((words[5] >> 16) & 0x00000F);
		    svf[19] = ((words[5] >> 12) & 0x00000F);
		    svf[20] = ((words[5] >>  8) & 0x00000F);
		    svf[21] = ((words[5] >>  4) & 0x00000F);
		    svf[22] = ((words[5] >>  0) & 0x00000F);
		    svf[23] = ((words[6] >> 20) & 0x00000F);
		    svf[24] = ((words[6] >> 16) & 0x00000F);
		    svf[25] = ((words[6] >> 12) & 0x00000F);
		    svf[26] = ((words[6] >>  8) & 0x00000F);
		    svf[27] = ((words[6] >>  4) & 0x00000F);
		    svf[28] = ((words[6] >>  0) & 0x00000F);
		    svf[29] = ((words[7] >> 20) & 0x00000F);
		    svf[30] = ((words[7] >> 16) & 0x00000F);
		    svf[31] = ((words[7] >> 12) & 0x00000F);
		    svf[32] = ((words[7] >>  8) & 0x00000F);

		    unsigned char svh25 = ((words[7] >>  0) & 0x00003F);
		    unsigned char svh26 = ((words[8] >> 18) & 0x00003F);
		    unsigned char svh27 = ((words[8] >> 12) & 0x00003F);
		    unsigned char svh28 = ((words[8] >>  6) & 0x00003F);
		    unsigned char svh29 = ((words[8] >>  0) & 0x00003F);
		    unsigned char svh30 = ((words[9] >> 18) & 0x00003F);
		    unsigned char svh31 = ((words[9] >> 12) & 0x00003F);
		    unsigned char svh32 = ((words[9] >>  6) & 0x00003F);

		    gpsd_report(LOG_PROG, "50B: SF:4-25 data_id %d "
			"SV1:%u SV2:%u SV3:%u SV4:%u "
			"SV5:%u SV6:%u SV7:%u SV8:%u "
			"SV9:%u SV10:%u SV11:%u SV12:%u "
			"SV13:%u SV14:%u SV15:%u SV16:%u "
			"SV17:%u SV18:%u SV19:%u SV20:%u "
			"SV21:%u SV22:%u SV23:%u SV24:%u "
			"SV25:%u SV26:%u SV27:%u SV28:%u "
			"SV29:%u SV30:%u SV31:%u SV32:%u "
			"SVH25:%u SVH26:%u SVH27:%u SVH28:%u "
			"SVH29:%u SVH30:%u SVH31:%u SVH32:%u\n",
				data_id, 
				svf[1], svf[2], svf[3], svf[4],
				svf[5], svf[5], svf[6], svf[4],
				svf[9], svf[10], svf[11], svf[12],
				svf[13], svf[14], svf[15], svf[16],
				svf[17], svf[18], svf[19], svf[20],
				svf[21], svf[22], svf[23], svf[24],
				svf[25], svf[26], svf[27], svf[28],
				svf[29], svf[30], svf[31], svf[32],
				svh25, svh26, svh27, svh28,
				svh29, svh30, svh31, svh32);
		}
		break;

	    case 33:
	    case 34:
	    case 35:
	    case 36:
	    case 37:
	    case 38:
	    case 39:
	    case 40:
	    case 41:
	    case 42:
	    case 43:
	    case 44:
	    case 45:
	    case 46:
	    case 47:
	    case 48:
	    case 49:
	    case 50:
		/* unassigned */
		break;

	    case 51:
		/* unknown */
		break;
	
	    case 17:
	    case 55:
		/* for some inscrutable reason page 17 is sent
		 * as page 55, IS-GPS-200E Table 20-V */
		sv = -1;
		/*
		 * "The requisite 176 bits shall occupy bits 9 through 24 
		 * of word TWO, the 24 MSBs of words THREE through EIGHT, 
		 * plus the 16 MSBs of word NINE." (word numbers changed 
 		 * to account for zero-indexing)
		 * Since we've already stripped the low six parity bits, 
		 * and shifted the data to a byte boundary, we can just 
		 * copy it out. */

		{
		    char str[24];
		    int j = 0;
		    /*@ -type @*/
		    str[j++] = (words[2] >> 8) & 0xff;
		    str[j++] = (words[2]) & 0xff;

		    str[j++] = (words[3] >> 16) & 0xff;
		    str[j++] = (words[3] >> 8) & 0xff;
		    str[j++] = (words[3]) & 0xff;

		    str[j++] = (words[4] >> 16) & 0xff;
		    str[j++] = (words[4] >> 8) & 0xff;
		    str[j++] = (words[4]) & 0xff;

		    str[j++] = (words[5] >> 16) & 0xff;
		    str[j++] = (words[5] >> 8) & 0xff;
		    str[j++] = (words[5]) & 0xff;

		    str[j++] = (words[6] >> 16) & 0xff;
		    str[j++] = (words[6] >> 8) & 0xff;
		    str[j++] = (words[6]) & 0xff;

		    str[j++] = (words[7] >> 16) & 0xff;
		    str[j++] = (words[7] >> 8) & 0xff;
		    str[j++] = (words[7]) & 0xff;

		    str[j++] = (words[8] >> 16) & 0xff;
		    str[j++] = (words[8] >> 8) & 0xff;
		    str[j++] = (words[8]) & 0xff;

		    str[j++] = (words[9] >> 16) & 0xff;
		    str[j++] = (words[9] >> 8) & 0xff;
		    str[j++] = '\0';
		    /*@ +type @*/
		    gpsd_report(LOG_INF, "50B: SF:4-17 system message: %s\n",
			    str);
		}
		break;
	    case 18:
	    case 56:
		/* for some inscrutable reason page 18 is sent
		 * as page 56, IS-GPS-200E Table 20-V */
		/* ionospheric and UTC data */
		{
		    sv = -1;
		    /* current leap seconds */
		    unsigned int a0 = ((words[2] >> 16) & 0x0000FF);
		    unsigned int a1 = ((words[2] >>  8) & 0x0000FF);
		    unsigned int a2 = ((words[3] >> 16) & 0x0000FF);
		    unsigned int a3 = ((words[3] >>  8) & 0x0000FF);
		    unsigned int b0 = ((words[3] >>  0) & 0x0000FF);
		    unsigned int b1 = ((words[4] >> 16) & 0x0000FF);
		    unsigned int b2 = ((words[4] >>  8) & 0x0000FF);
		    unsigned int b3 = ((words[4] >>  0) & 0x0000FF);
		    unsigned int A1 = ((words[5] >>  0) & 0xFFFFFF);
		    unsigned int A0 = ((words[6] >>  0) & 0xFFFFFF);
		    A0 <<= 8;
		    A0             += ((words[7] >> 16) & 0x00FFFF);
		    /* careful WN is 10 bits, but WNt is 8 bits! */
		    /* WNt (Week Number of LSF) */
		    unsigned int tot   = ((words[7] >> 8) & 0x0000FF);
		    unsigned int wnt   = ((words[7] >> 0) & 0x0000FF);
		    unsigned int leap  = ((words[8] >> 16) & 0x0000FF);
		    unsigned int wnlsf = ((words[8] >>  8) & 0x0000FF);

		    /* DN (Day Number of LSF) */
		    unsigned int dn = (words[8] & 0x0000FF);	   
		    /* leap second future */
		    unsigned int lsf = ((words[9] >> 16) & 0x0000FF);
		    /*
		     * On SiRFs, the 50BPS data is passed on even when the
		     * parity fails.  This happens frequently.  So the driver 
		     * must be extra careful that bad data does not reach here.
		     */
		    if (LEAP_SECONDS > leap) {
			/* something wrong */
			gpsd_report(LOG_ERROR, 
			    "50B: SF:4-18 Invalid leap_seconds: %d\n",
				    leap);
			leap = LEAP_SECONDS;
			session->context->valid &= ~LEAP_SECOND_VALID;
		    } else {
			gpsd_report(LOG_INF,
			    "50B: SF:4-18 leap-seconds:%u lsf:%u WNlsf:%u "
			    "DN:%d\n",
				    leap, lsf, wnlsf, dn);
			gpsd_report(LOG_PROG,
			    "50B: SF:4-18 a0:%u a1:%u a2:%u a3:%u "
			    "b0:%u b1:%u b2:%u b3:%u "
			    "A1:%u A0:%u tot:%u WNt:%u "
			    "ls: %u wnlsf:%u DN:%u, lsf:%u\n",
				a0, a1, a2, a3,
				b0, b1, b2, b3,
				A1, A0, tot, wnt,
				leap, wnlsf, dn, lsf);
			session->context->valid |= LEAP_SECOND_VALID;
			if (leap != lsf) {
			    gpsd_report(LOG_PROG, 
				"50B: SF:4-18 leap-second change coming\n");
			}
		    }
		    session->context->leap_seconds = (int)leap;
		}
		break;
	    default:
		;			/* no op */
	    }
	    if ( -1 < sv ) {
		subframe_almanac(svid, words, subframe, sv, data_id);
	    } else if ( -2 == sv ) {
		gpsd_report(LOG_PROG,
			"50B: SF:4-%d data_id %d\n",
			pageid, data_id);
	    }
	    /* else, already handled */
	}
	break;
    case 5:
	/* Pages 0, dummy almanc for dummy SV 0
	 * Pages 1 through 24: almanac data for SV 1 through 24
	 * Page 25: SV health data for SV 1 through 24, the almanac 
	 * reference time, the almanac reference week number.
	 */
	if ( 25 > pageid ) {
            subframe_almanac(svid, words, subframe, pageid, data_id);
	} else if ( 51 == pageid ) {
	    /* for some inscrutable reason page 25 is sent as page 51 
	     * IS-GPS-200E Table 20-V */
	    unsigned int toa   = ((words[2] >> 8) & 0x0000FF);
	    unsigned int wna   = ( words[2] & 0x0000FF);
	    unsigned int sv[25];
	    sv[1] = ((words[2] >> 18) & 0x00003F);
	    sv[2] = ((words[2] >> 12) & 0x00003F);
	    sv[3] = ((words[2] >>  6) & 0x00003F);
	    sv[4] = ((words[2] >>  0) & 0x00003F);
	    sv[5] = ((words[3] >> 18) & 0x00003F);
	    sv[6] = ((words[3] >> 12) & 0x00003F);
	    sv[7] = ((words[3] >>  6) & 0x00003F);
	    sv[8] = ((words[3] >>  0) & 0x00003F);
	    sv[9] = ((words[4] >> 18) & 0x00003F);
	    sv[10] = ((words[4] >> 12) & 0x00003F);
	    sv[11] = ((words[4] >>  6) & 0x00003F);
	    sv[12] = ((words[4] >>  0) & 0x00003F);
	    sv[13] = ((words[5] >> 18) & 0x00003F);
	    sv[14] = ((words[5] >> 12) & 0x00003F);
	    sv[15] = ((words[5] >>  6) & 0x00003F);
	    sv[16] = ((words[5] >>  0) & 0x00003F);
	    sv[17] = ((words[6] >> 18) & 0x00003F);
	    sv[18] = ((words[6] >> 12) & 0x00003F);
	    sv[19] = ((words[6] >>  6) & 0x00003F);
	    sv[20] = ((words[6] >>  0) & 0x00003F);
	    sv[21] = ((words[7] >> 18) & 0x00003F);
	    sv[22] = ((words[7] >> 12) & 0x00003F);
	    sv[23] = ((words[7] >>  6) & 0x00003F);
	    sv[24] = ((words[7] >>  0) & 0x00003F);
	    gpsd_report(LOG_PROG,
		"50B: SF:5-25 SV:%2u DI:%u toa:%u WNa:%u "
		"SV1:%u SV2:%u SV3:%uSV4:%u "
		"SV5:%u SV6:%u SV7:%uSV8:%u "
		"SV9:%u SV10:%u SV11:%uSV12:%u "
		"SV13:%u SV14:%u SV15:%uSV16:%u "
		"SV17:%u SV18:%u SV19:%uSV20:%u "
		"SV21:%u SV22:%u SV23:%uSV24:%u\n",
			svid, data_id, toa, wna,
			sv[1], sv[2], sv[3], sv[4],
			sv[5], sv[5], sv[6], sv[4],
			sv[9], sv[10], sv[11], sv[12],
			sv[13], sv[14], sv[15], sv[16],
			sv[17], sv[18], sv[19], sv[20],
			sv[21], sv[22], sv[23], sv[24]);
	} else {
	    /* unknown page */
	    gpsd_report(LOG_PROG, "50B: SF:5-%d data_id %d uknown page\n",
		pageid, data_id);
	}
	break;
    default:
	/* unknown/illegal subframe */
	break;
    }
    return;
}

/*@ +usedef @*/
