/* subframe.c -- interpret satellite subframe data.
 *
 * This file is Copyright (c) 2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 */
#include <math.h>

#include "gpsd.h"
#include "timebase.h"

#define BIT6  0x00020
#define BIT8  0x00080
#define BIT11 0x00400
#define BIT16 0x08000
#define BIT22 0x0200000
#define BIT24 0x0800000
/* convert unsigned to signed */
#define uint2int( u, m) ( u & m ? u - m : u)

/*@ -usedef @*/
int gpsd_interpret_subframe_raw(struct gps_device_t *session,
				unsigned int svid, uint32_t words[])
{
    unsigned int i;
    uint32_t preamble, parity;

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
	parity = (uint32_t)isgps_parity((isgps30bits_t)words[i]);
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

struct almanac
{
    uint16_t e;
    int16_t Omegad;
    uint32_t toa, svh, sqrtA;
    int32_t deltai, Omega0, omega, M0, af0, af1;
    /* eccentricity, 16 bits, unsigned, dimensionless */
    double d_eccentricity;
    /* sqrt A, Square Root of the Semi-Major Axis
     * 24 bits unsigned, square_root(meters) */
    double d_sqrtA;
    /* Omega dot, Rate of Right Ascension, 16 bits signed, semi-circles/sec */
    double d_Omegad;
};

/* you can find up to date almanac data for comparision here:
 * https://gps.afspc.af.mil/gps/Current/current.alm
 */
static void subframe_almanac(unsigned int svid, uint32_t words[], 
			     unsigned int subframe, unsigned int sv,
			     unsigned int data_id, 
			     /*@out@*/struct almanac *almp)
{
    

    almp->e      = ( words[2] & 0x00FFFF);
    almp->d_eccentricity  = pow(2.0,-21) * almp->e;
    /* carefull, each SV can have more than 2 toa's active at the same time 
     * you can not just store one or two almanacs for each sat */
    almp->toa    = ((words[3] >> 16) & 0x0000FF);
    almp->deltai = ( words[3] & 0x00FFFF);
    almp->deltai = uint2int(almp->deltai, BIT16);
    almp->Omegad = ((words[4] >>  8) & 0x00FFFF);
    almp->d_Omegad = pow(2.0, -38) * almp->Omegad;
    almp->svh    = ( words[4] & 0x0000FF);
    almp->sqrtA  = ( words[5] & 0xFFFFFF);
    almp->d_sqrtA  = pow(2.0,-11) * almp->sqrtA;
    almp->Omega0 = ( words[6] & 0xFFFFFF);
    almp->Omega0 = uint2int(almp->Omega0, BIT24);
    almp->omega  = ( words[7] & 0xFFFFFF);
    almp->omega  = uint2int(almp->omega, BIT24);
    almp->M0     = ( words[8] & 0xFFFFFF);
    almp->M0     = uint2int(almp->M0, BIT24);
    almp->af1    = ((words[9] >>  5) & 0x0007FF);
    almp->af1    = uint2int(almp->af1, BIT11);
    almp->af0    = ((words[9] >> 16) & 0x0000FF);
    almp->af0 <<= 3;
    almp->af0   |= ((words[9] >>  2) & 0x000007);
    almp->af0    = uint2int(almp->af0, BIT11);
    gpsd_report(LOG_PROG,
		"50B: SF:%d SV:%2u TSV:%2u data_id %d e:%g toa:%u deltai:%d"
		" Omegad:%.5g svh:%u sqrtA:%.8g Omega0:%d omega:%d M0:%d"
		" af0:%d af1:%d\n",
		subframe, sv, svid, data_id, 
		almp->d_eccentricity, 
		almp->toa, 
		almp->deltai,
		almp->d_Omegad,
		almp->svh,
		almp->d_sqrtA,
		almp->Omega0,
		almp->omega,
		almp->M0,
		almp->af0,
		almp->af1);
}

struct subframe {
    int subtype;
    union {
	struct {
	    unsigned int l2, ura, hlth, iodc, toc, l2p;
	    int32_t tgd, af0, af1, af2;
	} sub1;
	struct {
	    /* Issue of Data (Ephemeris), 
	     * equal to the 8 LSBs of the 10 bit IODC of the same data set */
	    uint8_t IODE;
	    /* Age of Data Offset for the NMCT, 6 bits, ignore if all ones */
	    uint8_t AODO;
	    /* Age of Data Offset for the NMCT, seconds */
	    uint16_t u_AODO;
	    uint32_t e, sqrtA, toe, fit;
	    int32_t Crs, Cus, Cuc, deltan, M0;
	    /* eccentricity, 32 bits, unsigned, dimensionless */
	    double d_eccentricity;
	    /* sqrt A, Square Root of the Semi-Major Axis
	     * 32 bits unsigned, square_root(meters) */
	    double d_sqrtA;
	} sub2;
	struct {
	    struct almanac almanac;
	} sub4;
	struct {
	    struct almanac almanac;
	} sub5;
    };
};

struct subframe subframe;

void gpsd_interpret_subframe(struct gps_device_t *session,
			     unsigned int svid, uint32_t words[])
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
    /* FIXME!! I really doubt this is Big Endian compatible */
    unsigned int pageid, data_id, preamble;
    uint32_t tow17;
    bool alert, antispoof;
    struct subframe *subp = &subframe;
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
    subp->subtype = ((words[1] >> 2) & 0x07);
    alert = (bool)((words[1] >> 6) & 0x01);
    antispoof = (bool)((words[1] >> 6) & 0x01);
    gpsd_report(LOG_PROG,
		"50B: SF:%d SV:%2u TOW17:%6u Alert:%u AS:%u\n", 
		subp->subtype, svid, tow17, (unsigned)alert, (unsigned)antispoof);
    /*
     * Consult the latest revision of IS-GPS-200 for the mapping
     * between magic SVIDs and pages.
     */
    pageid  = (words[2] >> 16) & 0x00003F; /* only in frames 4 & 5 */
    data_id = (words[2] >> 22) & 0x3;      /* only in frames 4 & 5 */

    switch (subp->subtype) {
    case 1:
	/* subframe 1: clock parameters for transmitting SV */
	/* get Week Number (WN) from subframe 1 */
	{
	    /*
	     * FIXME: This only extracts 10 bits of GPS week.
	     * This counter is moving to 13 bits, but we don't
	     * know how or when to look for the other 3 yet.
	     */
	    session->context->gps_week =
		(unsigned short)((words[2] >> 14) & 0x03ff);
	    subp->sub1.l2   = ((words[2] >> 10) & 0x000003); /* L2 Code */
	    subp->sub1.ura  = ((words[2] >>  8) & 0x00000F); /* URA Index */
	    subp->sub1.hlth = ((words[2] >>  2) & 0x00003F); /* SV health */
	    subp->sub1.iodc = (words[2] & 0x000003); /* IODC 2 MSB */
	    subp->sub1.l2p  = ((words[3] >> 23) & 0x000001); /* L2 P flag */
	    subp->sub1.tgd  = ( words[6] & 0x0000FF);
	    subp->sub1.tgd  = uint2int(subp->sub1.tgd, BIT8);
	    subp->sub1.toc  = ( words[7] & 0x00FFFF);
	    subp->sub1.af2  = ((words[8] >> 16) & 0x0FF);
	    subp->sub1.af2  = uint2int(subp->sub1.af2, BIT8);
	    subp->sub1.af1  = ( words[8] & 0x00FFFF);
	    subp->sub1.af1  = uint2int(subp->sub1.af1, BIT16);
	    subp->sub1.af0  = ((words[9] >>  1) & 0x03FFFFF);
	    subp->sub1.af0  = uint2int(subp->sub1.af0, BIT22);
	    subp->sub1.iodc <<= 8;
	    subp->sub1.iodc |= ((words[7] >> 16) & 0x00FF);
	    gpsd_report(LOG_PROG, "50B: SF:1 SV:%2u WN:%4u IODC:%4u"
			" L2:%u ura:%u hlth:%u L2P:%u Tgd:%d toc:%u af2:%3d"
			" af1:%5d af0:%7d\n", svid,
			session->context->gps_week,
			subp->sub1.iodc,
			subp->sub1.l2,
			subp->sub1.ura,
			subp->sub1.hlth,
			subp->sub1.l2p,
			subp->sub1.tgd,
			subp->sub1.toc,
			subp->sub1.af2,
			subp->sub1.af1,
			subp->sub1.af0);
	}
	break;
    case 2:
	/* subframe 2: ephemeris for transmitting SV */
	{
	    subp->sub2.IODE   = ((words[2] >> 16) & 0x00FF);
	    subp->sub2.Crs    = ( words[2] & 0x00FFFF);
	    subp->sub2.Crs    = uint2int(subp->sub2.Crs, BIT16);
	    subp->sub2.deltan = ((words[3] >>  8) & 0x00FFFF);
	    subp->sub2.deltan = uint2int(subp->sub2.deltan, BIT16);
	    subp->sub2.M0     = ( words[3] & 0x0000FF);
	    subp->sub2.M0   <<= 24;
	    subp->sub2.M0    |= ( words[4] & 0x00FFFFFF);
	    subp->sub2.M0     = uint2int(subp->sub2.M0, BIT24);
	    subp->sub2.Cuc    = ((words[5] >>  8) & 0x00FFFF);
	    subp->sub2.Cuc    = uint2int(subp->sub2.Cuc, BIT16);
	    subp->sub2.e      = ( words[5] & 0x0000FF);
	    subp->sub2.e    <<= 24;
	    subp->sub2.e     |= ( words[6] & 0x00FFFFFF);
	    subp->sub2.d_eccentricity  = pow(2.0,-33) * subp->sub2.e;
	    subp->sub2.Cus    = ((words[7] >>  8) & 0x00FFFF);
	    subp->sub2.Cus    = uint2int(subp->sub2.Cus, BIT16);
	    subp->sub2.sqrtA  = ( words[7] & 0x0000FF);
	    subp->sub2.sqrtA <<= 24;
	    subp->sub2.sqrtA |= ( words[8] & 0x00FFFFFF);
	    subp->sub2.d_sqrtA = pow(2.0, -19) * subp->sub2.sqrtA;
	    subp->sub2.toe    = ((words[9] >>  8) & 0x00FFFF);
	    subp->sub2.fit    = ((words[9] >>  7) & 0x000001);
	    subp->sub2.AODO   = ((words[9] >>  2) & 0x00001F);
	    subp->sub2.u_AODO   = subp->sub2.AODO * 900;
	    gpsd_report(LOG_PROG,
			"50B: SF:2 SV:%2u IODE:%3u Crs:%d deltan:%d M0:%d "
			"Cuc:%d e:%f Cus:%d sqrtA:%.11g toe:%u FIT:%u AODO:%5u\n", 
			svid, 
			subp->sub2.IODE,
			subp->sub2.Crs,
			subp->sub2.deltan,
			subp->sub2.M0,
			subp->sub2.Cuc,
			subp->sub2.d_eccentricity,
			subp->sub2.Cus,
			subp->sub2.d_sqrtA,
			subp->sub2.toe,
			subp->sub2.fit,
			subp->sub2.u_AODO);
	}
	break;
    case 3:
	/* subframe 3: ephemeris for transmitting SV */
	{
	    /* Issue of Data (Ephemeris), 8 bits, unsigned 
	     * equal to the 8 LSBs of the 10 bit IODC of the same data set */
	    uint8_t IODE;
	    uint32_t IDOT;
	    int32_t Cic, Cis, Crc, om0, i0, om, Omegad;
	    /* Omega dot, Rate of Right Ascension, 24 bits signed, 
	     * semi-circles/sec */
	    double d_Omegad;

	    Cic    = ((words[2] >>  8) & 0x00FFFF);
	    Cic    = uint2int(Cic, BIT16);
	    om0    = ( words[2] & 0x0000FF);
	    om0  <<= 24;
	    om0   |= ( words[3] & 0x00FFFFFF);
	    Cis    = ((words[4] >>  8) & 0x00FFFF);
	    Cis    = uint2int(Cis, BIT16);
	    i0     = ( words[4] & 0x0000FF);
	    i0   <<= 24;
	    i0    |= ( words[5] & 0x00FFFFFF);
	    Crc    = ((words[6] >>  8) & 0x00FFFF);
	    Crc    = uint2int(Crc, BIT16);
	    om     = ( words[6] & 0x0000FF);
	    om   <<= 24;
	    om    |= ( words[7] & 0x00FFFFFF);
	    Omegad = ( words[8] & 0x00FFFFFF);
	    Omegad = uint2int(Omegad, BIT24);
	    d_Omegad = pow(2.0, -43) * Omegad;
	    IODE   = ((words[9] >> 16) & 0x0000FF);
	    IDOT   = ((words[9] >>  2) & 0x003FFF);
	    gpsd_report(LOG_PROG,
		"50B: SF:3 SV:%2u IODE:%3u I IDOT:%u Cic:%d om0:%d Cis:%d "
		"i0:%d crc:%d om:%d Omegad:%.6g\n", 
			svid, IODE, IDOT, Cic, om0, Cis, i0, Crc, om, 
			d_Omegad );
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
		    /* mapping ord ERD# to SV # is non trivial
		     * leave it alone */
		    char ERD[33];
		    unsigned char ai;
		    int i;

		    sv = -1;
		    ai      = ((words[2] >> 22) & 0x000003);
		    ERD[1]  = (char)((words[2] >>  8) & 0x00003F);
		    ERD[2]  = (char)((words[2] >>  2) & 0x00003F);
		    ERD[3]  = (char)((words[2] >>  0) & 0x000003);
		    ERD[3] <<= 2;
		    ERD[3] |= (char)((words[3] >> 20) & 0x00000F);

		    ERD[4]  = (char)((words[3] >> 14) & 0x00003F);
		    ERD[5]  = (char)((words[3] >>  8) & 0x00003F);
		    ERD[6]  = (char)((words[3] >>  2) & 0x00003F);
		    ERD[7]  = (char)((words[3] >>  0) & 0x000003);

		    ERD[7] <<= 2;
		    ERD[7] |= (char)((words[4] >> 20) & 0x00000F);
		    ERD[8]  = (char)((words[4] >> 14) & 0x00003F);
		    ERD[9]  = (char)((words[4] >>  8) & 0x00003F);
		    ERD[10] = (char)((words[4] >>  2) & 0x00003F);
		    ERD[11] = (char)((words[4] >>  0) & 0x00000F);

		    ERD[11] <<= 2;
		    ERD[11] |= (char)((words[5] >> 20) & 0x00000F);
		    ERD[12]  = (char)((words[5] >> 14) & 0x00003F);
		    ERD[13]  = (char)((words[5] >>  8) & 0x00003F);
		    ERD[14]  = (char)((words[5] >>  2) & 0x00003F);
		    ERD[15]  = (char)((words[5] >>  0) & 0x000003);

		    ERD[15] <<= 2;
		    ERD[15] |= (char)((words[6] >> 20) & 0x00000F);
		    ERD[16]  = (char)((words[6] >> 14) & 0x00003F);
		    ERD[17]  = (char)((words[6] >>  8) & 0x00003F);
		    ERD[18]  = (char)((words[6] >>  2) & 0x00003F);
		    ERD[19]  = (char)((words[6] >>  0) & 0x000003);

		    ERD[19] <<= 2;
		    ERD[19] |= (char)((words[7] >> 20) & 0x00000F);
		    ERD[20]  = (char)((words[7] >> 14) & 0x00003F);
		    ERD[21]  = (char)((words[7] >>  8) & 0x00003F);
		    ERD[22]  = (char)((words[7] >>  2) & 0x00003F);
		    ERD[23]  = (char)((words[7] >>  0) & 0x000003);

		    ERD[23] <<= 2;
		    ERD[23] |= (char)((words[8] >> 20) & 0x00000F);
		    ERD[24]  = (char)((words[8] >> 14) & 0x00003F);
		    ERD[25]  = (char)((words[8] >>  8) & 0x00003F);
		    ERD[26]  = (char)((words[8] >>  2) & 0x00003F);
		    ERD[27]  = (char)((words[8] >>  0) & 0x000003);

		    ERD[27] <<= 2;
		    ERD[27] |= (char)((words[9] >> 20) & 0x00000F);
		    ERD[28]  = (char)((words[9] >> 14) & 0x00003F);
		    ERD[29]  = (char)((words[9] >>  8) & 0x00003F);
		    ERD[30]  = (char)((words[9] >>  2) & 0x00003F);

		    for ( i = 1; i < 31; i++ ) {
			ERD[i]  = uint2int(ERD[i], BIT6);
		    }

		    gpsd_report(LOG_PROG, "50B: SF:4-13 data_id %d ai:%u "
			"ERD1:%d ERD2:%d ERD3:%d ERD4:%d "
			"ERD5:%d ERD6:%d ERD7:%d ERD8:%d "
			"ERD9:%d ERD10:%d ERD11:%d ERD12:%d "
			"ERD13:%d ERD14:%d ERD15:%d ERD16:%d "
			"ERD17:%d ERD18:%d ERD19:%d ERD20:%d "
			"ERD21:%d ERD22:%d ERD23:%d ERD24:%d "
			"ERD25:%d ERD26:%d ERD27:%d ERD28:%d "
			"ERD29:%d ERD30:%d\n",
				data_id, ai,
				ERD[1], ERD[2], ERD[3], ERD[4],
				ERD[5], ERD[5], ERD[6], ERD[4],
				ERD[9], ERD[10], ERD[11], ERD[12],
				ERD[13], ERD[14], ERD[15], ERD[16],
				ERD[17], ERD[18], ERD[19], ERD[20],
				ERD[21], ERD[22], ERD[23], ERD[24],
				ERD[25], ERD[26], ERD[27], ERD[28],
				ERD[29], ERD[30]);
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
		    unsigned char svf[33];
		    unsigned char svh25 = ((words[7] >>  0) & 0x00003F);
		    unsigned char svh26 = ((words[8] >> 18) & 0x00003F);
		    unsigned char svh27 = ((words[8] >> 12) & 0x00003F);
		    unsigned char svh28 = ((words[8] >>  6) & 0x00003F);
		    unsigned char svh29 = ((words[8] >>  0) & 0x00003F);
		    unsigned char svh30 = ((words[9] >> 18) & 0x00003F);
		    unsigned char svh31 = ((words[9] >> 12) & 0x00003F);
		    unsigned char svh32 = ((words[9] >>  6) & 0x00003F);
		    sv = -1;
		    svf[1]  = (unsigned char)((words[2] >> 12) & 0x00000F);
		    svf[2]  = (unsigned char)((words[2] >>  8) & 0x00000F);
		    svf[3]  = (unsigned char)((words[2] >>  4) & 0x00000F);
		    svf[4]  = (unsigned char)((words[2] >>  0) & 0x00000F);
		    svf[5]  = (unsigned char)((words[3] >> 20) & 0x00000F);
		    svf[6]  = (unsigned char)((words[3] >> 16) & 0x00000F);
		    svf[7]  = (unsigned char)((words[3] >> 12) & 0x00000F);
		    svf[8]  = (unsigned char)((words[3] >>  8) & 0x00000F);
		    svf[9]  = (unsigned char)((words[3] >>  4) & 0x00000F);
		    svf[10] = (unsigned char)((words[3] >>  0) & 0x00000F);
		    svf[11] = (unsigned char)((words[4] >> 20) & 0x00000F);
		    svf[12] = (unsigned char)((words[4] >> 16) & 0x00000F);
		    svf[13] = (unsigned char)((words[4] >> 12) & 0x00000F);
		    svf[14] = (unsigned char)((words[4] >>  8) & 0x00000F);
		    svf[15] = (unsigned char)((words[4] >>  4) & 0x00000F);
		    svf[16] = (unsigned char)((words[4] >>  0) & 0x00000F);
		    svf[17] = (unsigned char)((words[5] >> 20) & 0x00000F);
		    svf[18] = (unsigned char)((words[5] >> 16) & 0x00000F);
		    svf[19] = (unsigned char)((words[5] >> 12) & 0x00000F);
		    svf[20] = (unsigned char)((words[5] >>  8) & 0x00000F);
		    svf[21] = (unsigned char)((words[5] >>  4) & 0x00000F);
		    svf[22] = (unsigned char)((words[5] >>  0) & 0x00000F);
		    svf[23] = (unsigned char)((words[6] >> 20) & 0x00000F);
		    svf[24] = (unsigned char)((words[6] >> 16) & 0x00000F);
		    svf[25] = (unsigned char)((words[6] >> 12) & 0x00000F);
		    svf[26] = (unsigned char)((words[6] >>  8) & 0x00000F);
		    svf[27] = (unsigned char)((words[6] >>  4) & 0x00000F);
		    svf[28] = (unsigned char)((words[6] >>  0) & 0x00000F);
		    svf[29] = (unsigned char)((words[7] >> 20) & 0x00000F);
		    svf[30] = (unsigned char)((words[7] >> 16) & 0x00000F);
		    svf[31] = (unsigned char)((words[7] >> 12) & 0x00000F);
		    svf[32] = (unsigned char)((words[7] >>  8) & 0x00000F);

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
		    int32_t A0, A1;
		    int8_t alpha0, alpha1, alpha2, alpha3;
		    int8_t beta0, beta1, beta2, beta3;
		    int8_t leap, lsf;
		    uint8_t tot, WNt, WNlsf, DN;

		    sv = -1;
		    /* current leap seconds */
		    alpha0 = ((words[2] >> 16) & 0x0000FF);
		    alpha1 = ((words[2] >>  8) & 0x0000FF);
		    alpha2 = ((words[3] >> 16) & 0x0000FF);
		    alpha3 = ((words[3] >>  8) & 0x0000FF);
		    beta0  = ((words[3] >>  0) & 0x0000FF);
		    beta1  = ((words[4] >> 16) & 0x0000FF);
		    beta2  = ((words[4] >>  8) & 0x0000FF);
		    beta3  = ((words[4] >>  0) & 0x0000FF);
		    A1     = ((words[5] >>  0) & 0xFFFFFF);
		    A1     = uint2int(A1, BIT24);
		    A0     = ((words[6] >>  0) & 0xFFFFFF);
		    A0   <<= 8;
		    A0    |= ((words[7] >> 16) & 0x00FFFF);
		    /* careful WN is 10 bits, but WNt is 8 bits! */
		    /* WNt (Week Number of LSF) */
		    tot    = ((words[7] >> 8) & 0x0000FF);
		    WNt    = ((words[7] >> 0) & 0x0000FF);
		    leap  = ((words[8] >> 16) & 0x0000FF);
		    WNlsf  = ((words[8] >>  8) & 0x0000FF);

		    /* DN (Day Number of LSF) */
		    DN = (words[8] & 0x0000FF);	   
		    /* leap second future */
		    lsf = ((words[9] >> 16) & 0x0000FF);
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
			    "50B: SF:4-18 leap-seconds:%d lsf:%d WNlsf:%u "
			    "DN:%d\n",
				    leap, lsf, WNlsf, DN);
			gpsd_report(LOG_PROG,
			    "50B: SF:4-18 a0:%d a1:%d a2:%d a3:%d "
			    "b0:%d b1:%d b2:%d b3:%d "
			    "A1:%d A0:%d tot:%u WNt:%u "
			    "ls: %d WNlsf:%u DN:%u, lsf:%d\n",
				alpha0, alpha1, alpha2, alpha3,
				beta0, beta1, beta2, beta3,
				A1, A0, tot, WNt,
				leap, WNlsf, DN, lsf);
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
		subframe_almanac(svid, words, subp->subtype, sv, data_id, 
				 &subp->sub4.almanac);
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
            subframe_almanac(svid, words, subp->subtype, pageid, data_id, 
			     &subp->sub5.almanac);
	} else if ( 51 == pageid ) {
	    /* for some inscrutable reason page 25 is sent as page 51 
	     * IS-GPS-200E Table 20-V */
	    uint8_t toa, WNa;
	    uint8_t sv[25];
	    toa   = ((words[2] >> 8) & 0x0000FF);
	    WNa   = ( words[2] & 0x0000FF);
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
			svid, data_id, toa, WNa,
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
