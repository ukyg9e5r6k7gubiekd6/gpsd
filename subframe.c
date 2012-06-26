/* subframe.c -- interpret satellite subframe data.
 *
 * This file is Copyright (c) 2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 */
#include <math.h>

#include "gpsd.h"

/* convert unsigned to signed */
#define uint2int( u, bit) ( u & (1<<(bit-1)) ? u - (1<<bit) : u)

/*@ -usedef @*/
gps_mask_t gpsd_interpret_subframe_raw(struct gps_device_t *session,
				unsigned int tSVID, uint32_t words[])
{
    unsigned int i;
    uint8_t preamble;
    uint32_t parity;

    if (session->subframe_count++ == 0) { 
	speed_t speed = gpsd_get_speed(&session->ttyset);

	if (speed < 38400)
	    gpsd_report(LOG_WARN, "speed less than 38,400 may cause data lag and loss of functionality\n");
    }

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

    preamble = (uint8_t)((words[0] >> 22) & 0xFF);
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

    return gpsd_interpret_subframe(session, tSVID, words);
}

/* you can find up to date almanac data for comparision here:
 * https://gps.afspc.af.mil/gps/Current/current.alm
 */
static void subframe_almanac(uint8_t tSVID, uint32_t words[],
			     uint8_t subframe, uint8_t sv,
			     uint8_t data_id,
			     /*@out@*/struct almanac_t *almp)
{
    /*@+matchanyintegral -shiftimplementation@*/
    almp->sv     = sv; /* ignore the 0 sv problem for now */
    almp->e      = ( words[2] & 0x00FFFF);
    almp->d_eccentricity  = pow(2.0,-21) * almp->e;
    /* carefull, each SV can have more than 2 toa's active at the same time
     * you can not just store one or two almanacs for each sat */
    almp->toa      = ((words[3] >> 16) & 0x0000FF);
    almp->l_toa    = almp->toa << 12;
    almp->deltai   = ( words[3] & 0x00FFFF);
    almp->d_deltai = pow(2.0, -19) * almp->deltai;
    almp->Omegad   = ((words[4] >>  8) & 0x00FFFF);
    almp->d_Omegad = pow(2.0, -38) * almp->Omegad;
    almp->svh      = ( words[4] & 0x0000FF);
    almp->sqrtA    = ( words[5] & 0xFFFFFF);
    almp->d_sqrtA  = pow(2.0,-11) * almp->sqrtA;
    almp->Omega0   = ( words[6] & 0xFFFFFF);
    almp->Omega0   = uint2int(almp->Omega0, 24);
    almp->d_Omega0 = pow(2.0, -23) * almp->Omega0;
    almp->omega    = ( words[7] & 0xFFFFFF);
    almp->omega    = uint2int(almp->omega, 24);
    almp->d_omega  = pow(2.0, -23) * almp->omega;
    almp->M0       = ( words[8] & 0x00FFFFFF);
    almp->M0       = uint2int(almp->M0, 24);
    /* if you want radians, multiply by GPS_PI, but we do semi-circles
     * to match IS-GPS-200E */
    almp->d_M0     = pow(2.0,-23) * almp->M0;
    almp->af1      = ((words[9] >>  5) & 0x0007FF);
    almp->af1      = (short)uint2int(almp->af1, 11);
    almp->d_af1    = pow(2.0,-38) * almp->af1;
    almp->af0      = ((words[9] >> 16) & 0x0000FF);
    almp->af0    <<= 3;
    almp->af0     |= ((words[9] >>  2) & 0x000007);
    almp->af0      = (short)uint2int(almp->af0, 11);
    almp->d_af0    = pow(2.0,-20) * almp->af0;
    gpsd_report(LOG_PROG,
		"50B: SF:%d SV:%2u TSV:%2u data_id %d e:%g toa:%lu "
		"deltai:%.10e Omegad:%.5e svh:%u sqrtA:%.10g Omega0:%.10e "
		"omega:%.10e M0:%.11e af0:%.5e af1:%.5e\n",
		subframe, almp->sv, tSVID, data_id,
		almp->d_eccentricity,
		almp->l_toa,
		almp->d_deltai,
		almp->d_Omegad,
		almp->svh,
		almp->d_sqrtA,
		almp->d_Omega0,
		almp->d_omega,
		almp->d_M0,
		almp->d_af0,
		almp->d_af1);
    /*@-matchanyintegral -shiftimplementation@*/
}

gps_mask_t gpsd_interpret_subframe(struct gps_device_t *session,
			     unsigned int tSVID, uint32_t words[])
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
    uint8_t preamble;
    int i = 0;   /* handy loop counter */
    struct subframe_t *subp = &session->gpsdata.subframe;
    gpsd_report(LOG_IO,
		"50B: gpsd_interpret_subframe: (%d) "
		"%06x %06x %06x %06x %06x %06x %06x %06x %06x %06x\n",
		tSVID, words[0], words[1], words[2], words[3], words[4],
		words[5], words[6], words[7], words[8], words[9]);

    preamble = (uint8_t)((words[0] >> 16) & 0x0FF);
    if (preamble == 0x8b) {
	/* somehow missed an inversion */
	preamble ^= 0xff;
	words[0] ^= 0xffffff;
    }
    if (preamble != 0x74) {
	gpsd_report(LOG_WARN,
	    "50B: gpsd_interpret_subframe bad preamble: 0x%x header 0x%x\n",
	    preamble, words[0]);
	return 0;
    }
    subp->integrity = (bool)((words[0] >> 1) & 0x01);
    /* The subframe ID is in the Hand Over Word (page 80) */
    subp->TOW17 = ((words[1] >> 7) & 0x01FFFF);
    subp->l_TOW17 = (long)(subp->TOW17 * 6);
    subp->tSVID = (uint8_t)tSVID;
    subp->subframe_num = ((words[1] >> 2) & 0x07);
    subp->alert = (bool)((words[1] >> 6) & 0x01);
    subp->antispoof = (bool)((words[1] >> 6) & 0x01);
    gpsd_report(LOG_PROG,
		"50B: SF:%d SV:%2u TOW17:%7lu Alert:%u AS:%u IF:%d\n",
		subp->subframe_num, subp->tSVID, subp->l_TOW17,
		(unsigned)subp->alert, (unsigned)subp->antispoof,
		(unsigned)subp->integrity);
    /*
     * Consult the latest revision of IS-GPS-200 for the mapping
     * between magic SVIDs and pages.
     */
    subp->pageid  = (words[2] >> 16) & 0x00003F; /* only in frames 4 & 5 */
    subp->data_id = (words[2] >> 22) & 0x3;      /* only in frames 4 & 5 */
    subp->is_almanac = 0;

    switch (subp->subframe_num) {
    case 1:
	/* subframe 1: clock parameters for transmitting SV */
	/* get Week Number (WN) from subframe 1 */
	/*
	 * This only extracts 10 bits of GPS week.
	 * 13 bits are available in the extension CNAV message,
	 * which we don't decode yet because we don't know
	 * of any receiver that reports it.
	 */
	session->context->gps_week =
	    (unsigned short)((words[2] >> 14) & 0x03ff);
	subp->sub1.WN   = (uint16_t)session->context->gps_week;
	subp->sub1.l2   = (uint8_t)((words[2] >> 12) & 0x000003); /* L2 Code */
	subp->sub1.ura  = (unsigned int)((words[2] >>  8) & 0x00000F); /* URA Index */
	subp->sub1.hlth = (unsigned int)((words[2] >>  2) & 0x00003F); /* SV health */
	subp->sub1.IODC = (words[2] & 0x000003); /* IODC 2 MSB */
	subp->sub1.l2p  = ((words[3] >> 23) & 0x000001); /* L2 P flag */
	subp->sub1.Tgd  = (int8_t)( words[6] & 0x0000FF);
	subp->sub1.d_Tgd  = pow(2.0, -31) * (int)subp->sub1.Tgd;
	subp->sub1.toc  = ( words[7] & 0x00FFFF);
	subp->sub1.l_toc = (long)subp->sub1.toc  << 4;
	subp->sub1.af2  = (int8_t)((words[8] >> 16) & 0x0FF);
	subp->sub1.d_af2  = pow(2.0, -55) * (int)subp->sub1.af2;
	subp->sub1.af1  = (int16_t)( words[8] & 0x00FFFF);
	subp->sub1.d_af1  = pow(2.0, -43) * subp->sub1.af1;
	subp->sub1.af0  = (int32_t)((words[9] >>  2) & 0x03FFFFF);
	subp->sub1.af0  = uint2int(subp->sub1.af0, 22);
	subp->sub1.d_af0  = pow(2.0, -31) * subp->sub1.af0;
	subp->sub1.IODC <<= 8;
	subp->sub1.IODC |= ((words[7] >> 16) & 0x00FF);
	gpsd_report(LOG_PROG, "50B: SF:1 SV:%2u WN:%4u IODC:%4u"
		    " L2:%u ura:%u hlth:%u L2P:%u Tgd:%g toc:%lu af2:%.4g"
		    " af1:%.6e af0:%.7e\n",
		    subp->tSVID,
		    subp->sub1.WN,
		    subp->sub1.IODC,
		    subp->sub1.l2,
		    subp->sub1.ura,
		    subp->sub1.hlth,
		    subp->sub1.l2p,
		    subp->sub1.d_Tgd,
		    subp->sub1.l_toc,
		    subp->sub1.d_af2,
		    subp->sub1.d_af1,
		    subp->sub1.d_af0);
	break;
    case 2:
	/* subframe 2: ephemeris for transmitting SV */
	subp->sub2.IODE   = ((words[2] >> 16) & 0x00FF);
	subp->sub2.Crs    = (int16_t)( words[2] & 0x00FFFF);
	subp->sub2.d_Crs  = pow(2.0,-5) * subp->sub2.Crs;
	subp->sub2.deltan = (int16_t)((words[3] >>  8) & 0x00FFFF);
	subp->sub2.d_deltan  = pow(2.0,-43) * subp->sub2.deltan;
	subp->sub2.M0     = (int32_t)( words[3] & 0x0000FF);
	subp->sub2.M0   <<= 24;
	subp->sub2.M0    |= ( words[4] & 0x00FFFFFF);
	subp->sub2.d_M0   = pow(2.0,-31) * subp->sub2.M0 * GPS_PI;
	subp->sub2.Cuc    = (int16_t)((words[5] >>  8) & 0x00FFFF);
	subp->sub2.d_Cuc  = pow(2.0,-29) * subp->sub2.Cuc;
	subp->sub2.e      = ( words[5] & 0x0000FF);
	subp->sub2.e    <<= 24;
	subp->sub2.e     |= ( words[6] & 0x00FFFFFF);
	subp->sub2.d_eccentricity  = pow(2.0,-33) * subp->sub2.e;
	subp->sub2.Cus    = (int16_t)((words[7] >>  8) & 0x00FFFF);
	subp->sub2.d_Cus  = pow(2.0,-29) * subp->sub2.Cus;
	subp->sub2.sqrtA  = ( words[7] & 0x0000FF);
	subp->sub2.sqrtA <<= 24;
	subp->sub2.sqrtA |= ( words[8] & 0x00FFFFFF);
	subp->sub2.d_sqrtA = pow(2.0, -19) * subp->sub2.sqrtA;
	subp->sub2.toe    = ((words[9] >>  8) & 0x00FFFF);
	subp->sub2.l_toe  = (long)(subp->sub2.toe << 4);
	subp->sub2.fit    = ((words[9] >>  7) & 0x000001);
	subp->sub2.AODO   = ((words[9] >>  2) & 0x00001F);
	subp->sub2.u_AODO   = subp->sub2.AODO * 900;
	gpsd_report(LOG_PROG,
		    "50B: SF:2 SV:%2u IODE:%3u Crs:%.6e deltan:%.6e "
		    "M0:%.11e Cuc:%.6e e:%f Cus:%.6e sqrtA:%.11g "
		    "toe:%lu FIT:%u AODO:%5u\n",
		    subp->tSVID,
		    subp->sub2.IODE,
		    subp->sub2.d_Crs,
		    subp->sub2.d_deltan,
		    subp->sub2.d_M0,
		    subp->sub2.d_Cuc,
		    subp->sub2.d_eccentricity,
		    subp->sub2.d_Cus,
		    subp->sub2.d_sqrtA,
		    subp->sub2.l_toe,
		    subp->sub2.fit,
		    subp->sub2.u_AODO);
	break;
    case 3:
	/* subframe 3: ephemeris for transmitting SV */
	subp->sub3.Cic      = (int16_t)((words[2] >>  8) & 0x00FFFF);
	subp->sub3.d_Cic    = pow(2.0, -29) * subp->sub3.Cic;
	subp->sub3.Omega0   = (int32_t)(words[2] & 0x0000FF);
	subp->sub3.Omega0 <<= 24;
	subp->sub3.Omega0  |= ( words[3] & 0x00FFFFFF);
	subp->sub3.d_Omega0 = pow(2.0, -31) * subp->sub3.Omega0;
	subp->sub3.Cis      = (int16_t)((words[4] >>  8) & 0x00FFFF);
	subp->sub3.d_Cis    = pow(2.0, -29) * subp->sub3.Cis;
	subp->sub3.i0       = (int32_t)(words[4] & 0x0000FF);
	subp->sub3.i0     <<= 24;
	subp->sub3.i0      |= ( words[5] & 0x00FFFFFF);
	subp->sub3.d_i0     = pow(2.0, -31) * subp->sub3.i0;
	subp->sub3.Crc      = (int16_t)((words[6] >>  8) & 0x00FFFF);
	subp->sub3.d_Crc    = pow(2.0, -5) * subp->sub3.Crc;
	subp->sub3.omega    = (int32_t)(words[6] & 0x0000FF);
	subp->sub3.omega  <<= 24;
	subp->sub3.omega   |= ( words[7] & 0x00FFFFFF);
	subp->sub3.d_omega  = pow(2.0, -31) * subp->sub3.omega;
	subp->sub3.Omegad   = (int32_t)(words[8] & 0x00FFFFFF);
	subp->sub3.Omegad   = uint2int(subp->sub3.Omegad, 24);
	subp->sub3.d_Omegad = pow(2.0, -43) * subp->sub3.Omegad;
	subp->sub3.IODE     = ((words[9] >> 16) & 0x0000FF);
	subp->sub3.IDOT     = (int16_t)((words[9] >>  2) & 0x003FFF);
	subp->sub3.IDOT     = uint2int(subp->sub3.IDOT, 14);
	subp->sub3.d_IDOT   = pow(2.0, -43) * subp->sub3.IDOT;
	gpsd_report(LOG_PROG,
	    "50B: SF:3 SV:%2u IODE:%3u I IDOT:%.6g Cic:%.6e Omega0:%.11e "
	    " Cis:%.7g i0:%.11e Crc:%.7g omega:%.11e Omegad:%.6e\n",
		    subp->tSVID, subp->sub3.IODE, subp->sub3.d_IDOT,
		    subp->sub3.d_Cic, subp->sub3.d_Omega0, subp->sub3.d_Cis,
		    subp->sub3.d_i0, subp->sub3.d_Crc, subp->sub3.d_omega,
		    subp->sub3.d_Omegad );
	break;
    case 4:
	{
	    int sv = -2;
	    switch (subp->pageid) {
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
		sv = -1;
		subp->sub4_13.ai      = (unsigned char)((words[2] >> 22) & 0x000003);
		/*@+charint@*/
		subp->sub4_13.ERD[1]  = (char)((words[2] >>  8) & 0x00003F);
		subp->sub4_13.ERD[2]  = (char)((words[2] >>  2) & 0x00003F);
		subp->sub4_13.ERD[3]  = (char)((words[2] >>  0) & 0x000003);
		subp->sub4_13.ERD[3] <<= 2;
		subp->sub4_13.ERD[3] |= (char)((words[3] >> 20) & 0x00000F);

		subp->sub4_13.ERD[4]  = (char)((words[3] >> 14) & 0x00003F);
		subp->sub4_13.ERD[5]  = (char)((words[3] >>  8) & 0x00003F);
		subp->sub4_13.ERD[6]  = (char)((words[3] >>  2) & 0x00003F);
		subp->sub4_13.ERD[7]  = (char)((words[3] >>  0) & 0x000003);

		subp->sub4_13.ERD[7] <<= 2;
		subp->sub4_13.ERD[7] |= (char)((words[4] >> 20) & 0x00000F);
		subp->sub4_13.ERD[8]  = (char)((words[4] >> 14) & 0x00003F);
		subp->sub4_13.ERD[9]  = (char)((words[4] >>  8) & 0x00003F);
		subp->sub4_13.ERD[10] = (char)((words[4] >>  2) & 0x00003F);
		subp->sub4_13.ERD[11] = (char)((words[4] >>  0) & 0x00000F);

		subp->sub4_13.ERD[11] <<= 2;
		subp->sub4_13.ERD[11] |= (char)((words[5] >> 20) & 0x00000F);
		subp->sub4_13.ERD[12]  = (char)((words[5] >> 14) & 0x00003F);
		subp->sub4_13.ERD[13]  = (char)((words[5] >>  8) & 0x00003F);
		subp->sub4_13.ERD[14]  = (char)((words[5] >>  2) & 0x00003F);
		subp->sub4_13.ERD[15]  = (char)((words[5] >>  0) & 0x000003);

		subp->sub4_13.ERD[15] <<= 2;
		subp->sub4_13.ERD[15] |= (char)((words[6] >> 20) & 0x00000F);
		subp->sub4_13.ERD[16]  = (char)((words[6] >> 14) & 0x00003F);
		subp->sub4_13.ERD[17]  = (char)((words[6] >>  8) & 0x00003F);
		subp->sub4_13.ERD[18]  = (char)((words[6] >>  2) & 0x00003F);
		subp->sub4_13.ERD[19]  = (char)((words[6] >>  0) & 0x000003);

		subp->sub4_13.ERD[19] <<= 2;
		subp->sub4_13.ERD[19] |= (char)((words[7] >> 20) & 0x00000F);
		subp->sub4_13.ERD[20]  = (char)((words[7] >> 14) & 0x00003F);
		subp->sub4_13.ERD[21]  = (char)((words[7] >>  8) & 0x00003F);
		subp->sub4_13.ERD[22]  = (char)((words[7] >>  2) & 0x00003F);
		subp->sub4_13.ERD[23]  = (char)((words[7] >>  0) & 0x000003);

		subp->sub4_13.ERD[23] <<= 2;
		subp->sub4_13.ERD[23] |= (char)((words[8] >> 20) & 0x00000F);
		subp->sub4_13.ERD[24]  = (char)((words[8] >> 14) & 0x00003F);
		subp->sub4_13.ERD[25]  = (char)((words[8] >>  8) & 0x00003F);
		subp->sub4_13.ERD[26]  = (char)((words[8] >>  2) & 0x00003F);
		subp->sub4_13.ERD[27]  = (char)((words[8] >>  0) & 0x000003);

		subp->sub4_13.ERD[27] <<= 2;
		subp->sub4_13.ERD[27] |= (char)((words[9] >> 20) & 0x00000F);
		subp->sub4_13.ERD[28]  = (char)((words[9] >> 14) & 0x00003F);
		subp->sub4_13.ERD[29]  = (char)((words[9] >>  8) & 0x00003F);
		subp->sub4_13.ERD[30]  = (char)((words[9] >>  2) & 0x00003F);

		for ( i = 1; i < 31; i++ ) {
		    subp->sub4_13.ERD[i]  = uint2int(subp->sub4_13.ERD[i], 6);
		}
		/*@-charint@*/

		gpsd_report(LOG_PROG, "50B: SF:4-13 data_id %d ai:%u "
		    "ERD1:%d ERD2:%d ERD3:%d ERD4:%d "
		    "ERD5:%d ERD6:%d ERD7:%d ERD8:%d "
		    "ERD9:%d ERD10:%d ERD11:%d ERD12:%d "
		    "ERD13:%d ERD14:%d ERD15:%d ERD16:%d "
		    "ERD17:%d ERD18:%d ERD19:%d ERD20:%d "
		    "ERD21:%d ERD22:%d ERD23:%d ERD24:%d "
		    "ERD25:%d ERD26:%d ERD27:%d ERD28:%d "
		    "ERD29:%d ERD30:%d\n",
			    subp->data_id, subp->sub4_13.ai,
			    subp->sub4_13.ERD[1], subp->sub4_13.ERD[2],
			    subp->sub4_13.ERD[3], subp->sub4_13.ERD[4],
			    subp->sub4_13.ERD[5], subp->sub4_13.ERD[6],
			    subp->sub4_13.ERD[7], subp->sub4_13.ERD[8],
			    subp->sub4_13.ERD[9], subp->sub4_13.ERD[10],
			    subp->sub4_13.ERD[11], subp->sub4_13.ERD[12],
			    subp->sub4_13.ERD[13], subp->sub4_13.ERD[14],
			    subp->sub4_13.ERD[15], subp->sub4_13.ERD[16],
			    subp->sub4_13.ERD[17], subp->sub4_13.ERD[18],
			    subp->sub4_13.ERD[19], subp->sub4_13.ERD[20],
			    subp->sub4_13.ERD[21], subp->sub4_13.ERD[22],
			    subp->sub4_13.ERD[23], subp->sub4_13.ERD[24],
			    subp->sub4_13.ERD[25], subp->sub4_13.ERD[26],
			    subp->sub4_13.ERD[27], subp->sub4_13.ERD[28],
			    subp->sub4_13.ERD[29], subp->sub4_13.ERD[30]);
		break;

	    case 25:
	    case 63:
		/* for some inscrutable reason page 25 is sent
		 * as page 63, IS-GPS-200E Table 20-V */
		/* A-S flags/SV configurations for 32 SVs,
		 * plus SV health for SV 25 through 32
		 */

		sv = -1;
		subp->sub4_25.svf[1]  = (unsigned char)((words[2] >> 12) & 0x0F);
		subp->sub4_25.svf[2]  = (unsigned char)((words[2] >>  8) & 0x0F);
		subp->sub4_25.svf[3]  = (unsigned char)((words[2] >>  4) & 0x0F);
		subp->sub4_25.svf[4]  = (unsigned char)((words[2] >>  0) & 0x0F);
		subp->sub4_25.svf[5]  = (unsigned char)((words[3] >> 20) & 0x0F);
		subp->sub4_25.svf[6]  = (unsigned char)((words[3] >> 16) & 0x0F);
		subp->sub4_25.svf[7]  = (unsigned char)((words[3] >> 12) & 0x0F);
		subp->sub4_25.svf[8]  = (unsigned char)((words[3] >>  8) & 0x0F);
		subp->sub4_25.svf[9]  = (unsigned char)((words[3] >>  4) & 0x0F);
		subp->sub4_25.svf[10] = (unsigned char)((words[3] >>  0) & 0x0F);
		subp->sub4_25.svf[11] = (unsigned char)((words[4] >> 20) & 0x0F);
		subp->sub4_25.svf[12] = (unsigned char)((words[4] >> 16) & 0x0F);
		subp->sub4_25.svf[13] = (unsigned char)((words[4] >> 12) & 0x0F);
		subp->sub4_25.svf[14] = (unsigned char)((words[4] >>  8) & 0x0F);
		subp->sub4_25.svf[15] = (unsigned char)((words[4] >>  4) & 0x0F);
		subp->sub4_25.svf[16] = (unsigned char)((words[4] >>  0) & 0x0F);
		subp->sub4_25.svf[17] = (unsigned char)((words[5] >> 20) & 0x0F);
		subp->sub4_25.svf[18] = (unsigned char)((words[5] >> 16) & 0x0F);
		subp->sub4_25.svf[19] = (unsigned char)((words[5] >> 12) & 0x0F);
		subp->sub4_25.svf[20] = (unsigned char)((words[5] >>  8) & 0x0F);
		subp->sub4_25.svf[21] = (unsigned char)((words[5] >>  4) & 0x0F);
		subp->sub4_25.svf[22] = (unsigned char)((words[5] >>  0) & 0x0F);
		subp->sub4_25.svf[23] = (unsigned char)((words[6] >> 20) & 0x0F);
		subp->sub4_25.svf[24] = (unsigned char)((words[6] >> 16) & 0x0F);
		subp->sub4_25.svf[25] = (unsigned char)((words[6] >> 12) & 0x0F);
		subp->sub4_25.svf[26] = (unsigned char)((words[6] >>  8) & 0x0F);
		subp->sub4_25.svf[27] = (unsigned char)((words[6] >>  4) & 0x0F);
		subp->sub4_25.svf[28] = (unsigned char)((words[6] >>  0) & 0x0F);
		subp->sub4_25.svf[29] = (unsigned char)((words[7] >> 20) & 0x0F);
		subp->sub4_25.svf[30] = (unsigned char)((words[7] >> 16) & 0x0F);
		subp->sub4_25.svf[31] = (unsigned char)((words[7] >> 12) & 0x0F);
		subp->sub4_25.svf[32] = (unsigned char)((words[7] >>  8) & 0x0F);

		subp->sub4_25.svhx[0] = ((words[7] >>  0) & 0x00003F);
		subp->sub4_25.svhx[1] = ((words[8] >> 18) & 0x00003F);
		subp->sub4_25.svhx[2] = ((words[8] >> 12) & 0x00003F);
		subp->sub4_25.svhx[3] = ((words[8] >>  6) & 0x00003F);
		subp->sub4_25.svhx[4] = ((words[8] >>  0) & 0x00003F);
		subp->sub4_25.svhx[5] = ((words[9] >> 18) & 0x00003F);
		subp->sub4_25.svhx[6] = ((words[9] >> 12) & 0x00003F);
		subp->sub4_25.svhx[7] = ((words[9] >>  6) & 0x00003F);

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
			    subp->data_id,
			    subp->sub4_25.svf[1],  subp->sub4_25.svf[2],
			    subp->sub4_25.svf[3],  subp->sub4_25.svf[4],
			    subp->sub4_25.svf[5],  subp->sub4_25.svf[6],
			    subp->sub4_25.svf[7],  subp->sub4_25.svf[8],
			    subp->sub4_25.svf[9],  subp->sub4_25.svf[10],
			    subp->sub4_25.svf[11], subp->sub4_25.svf[12],
			    subp->sub4_25.svf[13], subp->sub4_25.svf[14],
			    subp->sub4_25.svf[15], subp->sub4_25.svf[16],
			    subp->sub4_25.svf[17], subp->sub4_25.svf[18],
			    subp->sub4_25.svf[19], subp->sub4_25.svf[20],
			    subp->sub4_25.svf[21], subp->sub4_25.svf[22],
			    subp->sub4_25.svf[23], subp->sub4_25.svf[24],
			    subp->sub4_25.svf[25], subp->sub4_25.svf[26],
			    subp->sub4_25.svf[27], subp->sub4_25.svf[28],
			    subp->sub4_25.svf[29], subp->sub4_25.svf[30],
			    subp->sub4_25.svf[31], subp->sub4_25.svf[32],
			    subp->sub4_25.svhx[0],   subp->sub4_25.svhx[1],
			    subp->sub4_25.svhx[2],   subp->sub4_25.svhx[3],
			    subp->sub4_25.svhx[4],   subp->sub4_25.svhx[5],
			    subp->sub4_25.svhx[6],   subp->sub4_25.svhx[7]);
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

		/*@ -type @*/
		i = 0;
		subp->sub4_17.str[i++] = (words[2] >> 8) & 0xff;
		subp->sub4_17.str[i++] = (words[2]) & 0xff;

		subp->sub4_17.str[i++] = (words[3] >> 16) & 0xff;
		subp->sub4_17.str[i++] = (words[3] >> 8) & 0xff;
		subp->sub4_17.str[i++] = (words[3]) & 0xff;

		subp->sub4_17.str[i++] = (words[4] >> 16) & 0xff;
		subp->sub4_17.str[i++] = (words[4] >> 8) & 0xff;
		subp->sub4_17.str[i++] = (words[4]) & 0xff;

		subp->sub4_17.str[i++] = (words[5] >> 16) & 0xff;
		subp->sub4_17.str[i++] = (words[5] >> 8) & 0xff;
		subp->sub4_17.str[i++] = (words[5]) & 0xff;

		subp->sub4_17.str[i++] = (words[6] >> 16) & 0xff;
		subp->sub4_17.str[i++] = (words[6] >> 8) & 0xff;
		subp->sub4_17.str[i++] = (words[6]) & 0xff;

		subp->sub4_17.str[i++] = (words[7] >> 16) & 0xff;
		subp->sub4_17.str[i++] = (words[7] >> 8) & 0xff;
		subp->sub4_17.str[i++] = (words[7]) & 0xff;

		subp->sub4_17.str[i++] = (words[8] >> 16) & 0xff;
		subp->sub4_17.str[i++] = (words[8] >> 8) & 0xff;
		subp->sub4_17.str[i++] = (words[8]) & 0xff;

		subp->sub4_17.str[i++] = (words[9] >> 16) & 0xff;
		subp->sub4_17.str[i++] = (words[9] >> 8) & 0xff;
		subp->sub4_17.str[i] = '\0';
		/*@ +type @*/
		gpsd_report(LOG_PROG, "50B: SF:4-17 system message: %.24s\n",
			subp->sub4_17.str);
		break;
	    case 18:
	    case 56:
		/* for some inscrutable reason page 18 is sent
		 * as page 56, IS-GPS-200E Table 20-V */
		/* ionospheric and UTC data */

		sv = -1;
		/* current leap seconds */
		subp->sub4_18.alpha0 = (int8_t)((words[2] >> 8) & 0x0000FF);
		subp->sub4_18.d_alpha0 = pow(2.0, -30) * (int)subp->sub4_18.alpha0;
		subp->sub4_18.alpha1 = (int8_t)((words[2] >> 0) & 0x0000FF);
		subp->sub4_18.d_alpha1 = pow(2.0, -27) * (int)subp->sub4_18.alpha1;
		subp->sub4_18.alpha2 = (int8_t)((words[3] >> 16) & 0x0000FF);
		subp->sub4_18.d_alpha2 = pow(2.0, -24) * (int)subp->sub4_18.alpha2;
		subp->sub4_18.alpha3 = (int8_t)((words[3] >>  8) & 0x0000FF);
		subp->sub4_18.d_alpha3 = pow(2.0, -24) * (int)subp->sub4_18.alpha3;

		subp->sub4_18.beta0  = (int8_t)((words[3] >>  0) & 0x0000FF);
		subp->sub4_18.d_beta0 = pow(2.0, 11) * (int)subp->sub4_18.beta0;
		subp->sub4_18.beta1  = (int8_t)((words[4] >> 16) & 0x0000FF);
		subp->sub4_18.d_beta1 = pow(2.0, 14) * (int)subp->sub4_18.beta1;
		subp->sub4_18.beta2  = (int8_t)((words[4] >>  8) & 0x0000FF);
		subp->sub4_18.d_beta2 = pow(2.0, 16) * (int)subp->sub4_18.beta2;
		subp->sub4_18.beta3  = (int8_t)((words[4] >>  0) & 0x0000FF);
		subp->sub4_18.d_beta3 = pow(2.0, 16) * (int)subp->sub4_18.beta3;

		subp->sub4_18.A1     = (int32_t)((words[5] >>  0) & 0xFFFFFF);
		subp->sub4_18.A1     = uint2int(subp->sub4_18.A1, 24);
		subp->sub4_18.d_A1   = pow(2.0,-50) * subp->sub4_18.A1;
		subp->sub4_18.A0     = (int32_t)((words[6] >>  0) & 0xFFFFFF);
		subp->sub4_18.A0   <<= 8;
		subp->sub4_18.A0    |= ((words[7] >> 16) & 0x0000FF);
		subp->sub4_18.d_A0   = pow(2.0,-30) * subp->sub4_18.A0;

		/* careful WN is 10 bits, but WNt is 8 bits! */
		/* WNt (Week Number of LSF) */
		subp->sub4_18.tot    = ((words[7] >> 8) & 0x0000FF);
		subp->sub4_18.d_tot  = pow(2.0,12) * subp->sub4_18.tot;
		subp->sub4_18.WNt    = ((words[7] >> 0) & 0x0000FF);
		subp->sub4_18.leap  = (int8_t)((words[8] >> 16) & 0x0000FF);
		subp->sub4_18.WNlsf  = ((words[8] >>  8) & 0x0000FF);

		/* DN (Day Number of LSF) */
		subp->sub4_18.DN = (words[8] & 0x0000FF);
		/* leap second future */
		subp->sub4_18.lsf = (int8_t)((words[9] >> 16) & 0x0000FF);

		gpsd_report(LOG_PROG,
		    "50B: SF:4-18 a0:%.5g a1:%.5g a2:%.5g a3:%.5g "
		    "b0:%.5g b1:%.5g b2:%.5g b3:%.5g "
		    "A1:%.11e A0:%.11e tot:%.5g WNt:%u "
		    "ls: %d WNlsf:%u DN:%u, lsf:%d\n",
			subp->sub4_18.d_alpha0, subp->sub4_18.d_alpha1,
			subp->sub4_18.d_alpha2, subp->sub4_18.d_alpha3,
			subp->sub4_18.d_beta0, subp->sub4_18.d_beta1,
			subp->sub4_18.d_beta2, subp->sub4_18.d_beta3,
			subp->sub4_18.d_A1, subp->sub4_18.d_A0,
			subp->sub4_18.d_tot, subp->sub4_18.WNt,
			subp->sub4_18.leap, subp->sub4_18.WNlsf,
			subp->sub4_18.DN, subp->sub4_18.lsf);

#ifdef NTPSHM_ENABLE
		/* IS-GPS-200 Revision E, paragraph 20.3.3.5.2.4 */
		if ((subp->sub4_18.WNt == subp->sub4_18.WNlsf) &&
		    ((session->context->gps_week % 256) == (unsigned short)subp->sub4_18.WNlsf) &&
		    /* notify the leap seconds correction in the end of current day */
		    ((double)((subp->sub4_18.DN - 1) * SECS_PER_DAY) < session->context->gps_tow) &&
		    ((double)(subp->sub4_18.DN * SECS_PER_DAY) > session->context->gps_tow)) {
		   if ( subp->sub4_18.leap < subp->sub4_18.lsf )
			session->context->leap_notify = LEAP_ADDSECOND;
		   else if ( subp->sub4_18.leap > subp->sub4_18.lsf )
			session->context->leap_notify = LEAP_DELSECOND;
		   else
			session->context->leap_notify = LEAP_NOWARNING;
		} else
		   session->context->leap_notify = LEAP_NOWARNING;
#endif /* NTPSHM_ENABLE */

		session->context->leap_seconds = (int)subp->sub4_18.leap;
		session->context->valid |= LEAP_SECOND_VALID;
		break;
	    default:
		;			/* no op */
	    }
	    if ( -1 < sv ) {
		subp->is_almanac = 1;
		subframe_almanac(subp->tSVID, words, subp->subframe_num,
				 (uint8_t)sv, subp->data_id, &subp->sub4.almanac);
	    } else if ( -2 == sv ) {
		/* unknown or secret page */
		gpsd_report(LOG_PROG,
			"50B: SF:4-%d data_id %d\n",
			subp->pageid, subp->data_id);
		return 0;
	    }
	    /* else, already handled */
	}
	break;
    case 5:
	/* Pages 0, dummy almanac for dummy SV 0
	 * Pages 1 through 24: almanac data for SV 1 through 24
	 * Page 25: SV health data for SV 1 through 24, the almanac
	 * reference time, the almanac reference week number.
	 */
	if ( 25 > subp->pageid ) {
	    subp->is_almanac = 1;
	    subframe_almanac(subp->tSVID, words, subp->subframe_num,
		subp->pageid, subp->data_id, &subp->sub5.almanac);
	} else if ( 51 == subp->pageid ) {
	    /* for some inscrutable reason page 25 is sent as page 51
	     * IS-GPS-200E Table 20-V */

	    subp->sub5_25.toa   = ((words[2] >> 8) & 0x0000FF);
	    subp->sub5_25.l_toa <<= 12;
	    subp->sub5_25.WNa   = ( words[2] & 0x0000FF);
	    subp->sub5_25.sv[1] = ((words[2] >> 18) & 0x00003F);
	    subp->sub5_25.sv[2] = ((words[2] >> 12) & 0x00003F);
	    subp->sub5_25.sv[3] = ((words[2] >>  6) & 0x00003F);
	    subp->sub5_25.sv[4] = ((words[2] >>  0) & 0x00003F);
	    subp->sub5_25.sv[5] = ((words[3] >> 18) & 0x00003F);
	    subp->sub5_25.sv[6] = ((words[3] >> 12) & 0x00003F);
	    subp->sub5_25.sv[7] = ((words[3] >>  6) & 0x00003F);
	    subp->sub5_25.sv[8] = ((words[3] >>  0) & 0x00003F);
	    subp->sub5_25.sv[9] = ((words[4] >> 18) & 0x00003F);
	    subp->sub5_25.sv[10] = ((words[4] >> 12) & 0x00003F);
	    subp->sub5_25.sv[11] = ((words[4] >>  6) & 0x00003F);
	    subp->sub5_25.sv[12] = ((words[4] >>  0) & 0x00003F);
	    subp->sub5_25.sv[13] = ((words[5] >> 18) & 0x00003F);
	    subp->sub5_25.sv[14] = ((words[5] >> 12) & 0x00003F);
	    subp->sub5_25.sv[15] = ((words[5] >>  6) & 0x00003F);
	    subp->sub5_25.sv[16] = ((words[5] >>  0) & 0x00003F);
	    subp->sub5_25.sv[17] = ((words[6] >> 18) & 0x00003F);
	    subp->sub5_25.sv[18] = ((words[6] >> 12) & 0x00003F);
	    subp->sub5_25.sv[19] = ((words[6] >>  6) & 0x00003F);
	    subp->sub5_25.sv[20] = ((words[6] >>  0) & 0x00003F);
	    subp->sub5_25.sv[21] = ((words[7] >> 18) & 0x00003F);
	    subp->sub5_25.sv[22] = ((words[7] >> 12) & 0x00003F);
	    subp->sub5_25.sv[23] = ((words[7] >>  6) & 0x00003F);
	    subp->sub5_25.sv[24] = ((words[7] >>  0) & 0x00003F);
	    gpsd_report(LOG_PROG,
		"50B: SF:5-25 SV:%2u ID:%u toa:%lu WNa:%u "
		"SV1:%u SV2:%u SV3:%u SV4:%u "
		"SV5:%u SV6:%u SV7:%u SV8:%u "
		"SV9:%u SV10:%u SV11:%u SV12:%u "
		"SV13:%u SV14:%u SV15:%u SV16:%u "
		"SV17:%u SV18:%u SV19:%u SV20:%u "
		"SV21:%u SV22:%u SV23:%u SV24:%u\n",
			subp->tSVID, subp->data_id,
			subp->sub5_25.l_toa, subp->sub5_25.WNa,
			subp->sub5_25.sv[1], subp->sub5_25.sv[2],
			subp->sub5_25.sv[3], subp->sub5_25.sv[4],
			subp->sub5_25.sv[5], subp->sub5_25.sv[6],
			subp->sub5_25.sv[7], subp->sub5_25.sv[8],
			subp->sub5_25.sv[9], subp->sub5_25.sv[10],
			subp->sub5_25.sv[11], subp->sub5_25.sv[12],
			subp->sub5_25.sv[13], subp->sub5_25.sv[14],
			subp->sub5_25.sv[15], subp->sub5_25.sv[16],
			subp->sub5_25.sv[17], subp->sub5_25.sv[18],
			subp->sub5_25.sv[19], subp->sub5_25.sv[20],
			subp->sub5_25.sv[21], subp->sub5_25.sv[22],
			subp->sub5_25.sv[23], subp->sub5_25.sv[24]);
	} else {
	    /* unknown page */
	    gpsd_report(LOG_PROG, "50B: SF:5-%d data_id %d uknown page\n",
		subp->pageid, subp->data_id);
	    return 0;
	}
	break;
    default:
	/* unknown/illegal subframe */
	return 0;
    }
    return SUBFRAME_SET;
}

/*@ +usedef @*/
