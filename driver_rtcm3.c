/*****************************************************************************

This is a decoder for RTCM-104 3.x, a serial protocol used for
broadcasting pseudorange corrections from differential-GPS reference
stations.  The applicable specification is RTCM 10403.1: RTCM Paper
177-2006-SC104-STD.  This obsolesces the earlier RTCM-104 2.x
specifications. The specification document is proprietary; ordering 
instructions are accessible from <http://www.rtcm.org/>
under "Publications".  

Unike the RTCM 2.x protocol, RTCM3.x does not use the strange
sliding-bit-window IS-GPS-200 protocol as a transport layer, but is a
self-contained byte-oriented packet protocol.  Packet recognition is
handled in the GPSD packet-getter state machine; this code is
concerned with unpacking the packets into well-behaved C structures,
coping with odd field lengths and fields that may overlap byte
boudaries.  These report structures live in gps.h.

Note that the unpacking this module does is probably useful only for
RTCM reporting and diagnostic tools.  It is not necessary when
passing RTCM corrections to a GPS, which normally should just be
passed an entire correction packet for processing by their internal
firmware.

Decodes of the following types have been verified: 1005

This file is Copyright (c) 2010 by the GPSD project
BSD terms apply: see the file COPYING in the distribution root for details.

*****************************************************************************/

#include <stdio.h>
#include <string.h>
#include "gpsd_config.h"
#ifndef S_SPLINT_S
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>		/* for ntohl(3) and friends */
#endif /* S_SPLINT_S */

#include "gpsd.h"
#include "bits.h"

#ifdef RTCM104V3_ENABLE

/* scaling constants for RTCM3 real number types */
#define GPS_PSEUDORANGE_RESOLUTION	0.2	/* DF011 */
#define PSEUDORANGE_DIFF_RESOLUTION	0.0005	/* DF012,DF042 */
#define CARRIER_NOISE_RATIO_UNITS	0.25	/* DF015 */
#define ANTENNA_POSITION_RESOLUTION	0.0001	/* DF025-027 */
#define GLONASS_PSEUDORANGE_RESOLUTION	0.02	/* DF041 */
#define ANTENNA_DEGREE_RESOLUTION	25e-6	/* DF062 */
#define GPS_EPOCH_TIME_RESOLUTION	0.1	/* DF065 */
#define PHASE_CORRECTION_RESOLUTION	0.5	/* DF069-070 */

/* Other magic values */
#define GPS_INVALID_PSEUDORANGE		0x80000	/* DF012 */
#define GLONASS_INVALID_RANGEINCR	0x2000	/* DF047 */

/* Large case statements make GNU indent very confused */
/* *INDENT-OFF* */
/*@ -type @*//* re-enable when we're ready to take this live */

void rtcm3_unpack( /*@out@*/ struct rtcm3_t *rtcm, char *buf)
/* break out the raw bits into the scaled report-structure fields */
{
    unsigned int n, n2;
    int bitcount = 0;
    unsigned int i;
    signed long temp;

    /*@ -evalorder -sefparams -mayaliasunique @*/
#define ugrab(width)	(bitcount += width, ubits(buf, bitcount-width, width))
#define sgrab(width)	(bitcount += width, sbits(buf, bitcount-width, width))
#define GPS_PSEUDORANGE(fld, len) \
    {temp = (unsigned long)ugrab(len);		\
    if (temp == GPS_INVALID_PSEUDORANGE)	\
	fld.pseudorange = 0;			\
    else					\
	fld.pseudorange = temp * GPS_PSEUDORANGE_RESOLUTION;}
#define RANGEDIFF(fld, len) \
    temp = (long)sgrab(len);			\
    if (temp == GPS_INVALID_PSEUDORANGE)	\
	fld.rangediff = 0;			\
    else					\
	fld.rangediff = temp * PSEUDORANGE_DIFF_RESOLUTION;

    //assert(ugrab(8) == 0xD3);
    //assert(ugrab(6) == 0x00);
    ugrab(14);

    rtcm->length = (uint)ugrab(10);
    rtcm->type = (uint)ugrab(12);

    gpsd_report(LOG_RAW, "RTCM3: type %d payload length %d: %s\n",
		rtcm->type, rtcm->length, 
		gpsd_hexdump_wrapper(buf+3, rtcm->length, LOG_RAW));

    switch (rtcm->type) {
    case 1001:			/* GPS Basic RTK, L1 Only */
	rtcm->rtcmtypes.rtcm3_1001.header.station_id = (uint)ugrab(12);
	rtcm->rtcmtypes.rtcm3_1001.header.tow = (time_t)ugrab(30);
	rtcm->rtcmtypes.rtcm3_1001.header.sync = (bool)ugrab(1);
	rtcm->rtcmtypes.rtcm3_1001.header.satcount = (ushort)ugrab(5);
	rtcm->rtcmtypes.rtcm3_1001.header.smoothing = (bool)ugrab(1);
	rtcm->rtcmtypes.rtcm3_1001.header.interval = (ushort)ugrab(3);
#define R1001 rtcm->rtcmtypes.rtcm3_1001.rtk_data[i]
	for (i = 0; i < rtcm->rtcmtypes.rtcm3_1001.header.satcount; i++) {
	    R1001.ident = (ushort)ugrab(6);
	    R1001.L1.indicator = (unsigned char)ugrab(1);
	    GPS_PSEUDORANGE(R1001.L1, 24);
	    RANGEDIFF(R1001.L1, 20);
	    R1001.L1.locktime =	(unsigned char)sgrab(7);
	}
#undef R1001
	break;

    case 1002:			/* GPS Extended RTK, L1 Only */
	rtcm->rtcmtypes.rtcm3_1002.header.station_id = (uint)ugrab(12);
	rtcm->rtcmtypes.rtcm3_1002.header.tow = (time_t)ugrab(30);
	rtcm->rtcmtypes.rtcm3_1002.header.sync = (bool)ugrab(1);
	rtcm->rtcmtypes.rtcm3_1002.header.satcount = (ushort)ugrab(5);
	rtcm->rtcmtypes.rtcm3_1002.header.smoothing = (bool)ugrab(1);
	rtcm->rtcmtypes.rtcm3_1002.header.interval = (ushort)ugrab(3);
#define R1002 rtcm->rtcmtypes.rtcm3_1002.rtk_data[i]
	for (i = 0; i < rtcm->rtcmtypes.rtcm3_1002.header.satcount; i++) {
	    R1002.ident = (ushort)ugrab(6);
	    R1002.L1.indicator = (unsigned char)ugrab(1);
	    GPS_PSEUDORANGE(R1002.L1, 24);
	    RANGEDIFF(R1002.L1, 20);
	    R1002.L1.locktime =	(unsigned char)sgrab(7);
	    R1002.L1.ambiguity = (unsigned char)ugrab(8);
	    R1002.L1.CNR = (ugrab(8)) * CARRIER_NOISE_RATIO_UNITS;
	}
#undef R1002
	break;

    case 1003:			/* GPS Basic RTK, L1 & L2 */
	rtcm->rtcmtypes.rtcm3_1003.header.station_id = (uint)ugrab(12);
	rtcm->rtcmtypes.rtcm3_1003.header.tow = (time_t)ugrab(30);
	rtcm->rtcmtypes.rtcm3_1003.header.sync = (bool)ugrab(1);
	rtcm->rtcmtypes.rtcm3_1003.header.satcount = (ushort)ugrab(5);
	rtcm->rtcmtypes.rtcm3_1003.header.smoothing = (bool)ugrab(1);
	rtcm->rtcmtypes.rtcm3_1003.header.interval = (ushort)ugrab(3);
#define R1003 rtcm->rtcmtypes.rtcm3_1003.rtk_data[i]
	for (i = 0; i < rtcm->rtcmtypes.rtcm3_1003.header.satcount; i++) {
	    R1003.ident = (ushort)ugrab(6);
	    R1003.L1.indicator =
		(unsigned char)ugrab(1);
	    GPS_PSEUDORANGE(R1003.L1, 24);
	    RANGEDIFF(R1003.L1, 20);
	    R1003.L1.locktime =	(unsigned char)sgrab(7);
	    R1003.L2.indicator = (unsigned char)ugrab(2);
	    GPS_PSEUDORANGE(R1003.L2, 24);
	    temp = (long)sgrab(20);
	    if (temp == GPS_INVALID_PSEUDORANGE)
		R1003.L2.rangediff = 0;
	    else
		R1003.L2.rangediff = temp * PSEUDORANGE_DIFF_RESOLUTION;
	    R1003.L2.locktime =	(unsigned char)sgrab(7);
	}
#undef R1003
	break;

    case 1004:			/* GPS Extended RTK, L1 & L2 */
	rtcm->rtcmtypes.rtcm3_1004.header.station_id = (uint)ugrab(12);
	rtcm->rtcmtypes.rtcm3_1004.header.tow = (time_t)ugrab(30);
	rtcm->rtcmtypes.rtcm3_1004.header.sync = (bool)ugrab(1);
	rtcm->rtcmtypes.rtcm3_1004.header.satcount = (ushort)ugrab(5);
	rtcm->rtcmtypes.rtcm3_1004.header.smoothing = (bool)ugrab(1);
	rtcm->rtcmtypes.rtcm3_1004.header.interval = (ushort)ugrab(3);
#define R1004 rtcm->rtcmtypes.rtcm3_1004.rtk_data[i]
	for (i = 0; i < rtcm->rtcmtypes.rtcm3_1004.header.satcount; i++) {
	    R1004.ident = (ushort)ugrab(6);
	    R1004.L1.indicator = (bool)ugrab(1);
	    GPS_PSEUDORANGE(R1004.L1, 24);
	    RANGEDIFF(R1004.L1, 20);
	    R1004.L1.locktime =	(unsigned char)sgrab(7);
	    R1004.L1.ambiguity = (unsigned char)ugrab(8);
	    R1004.L1.CNR = ugrab(8) * CARRIER_NOISE_RATIO_UNITS;
	    R1004.L2.indicator = (unsigned char)ugrab(2);
	    GPS_PSEUDORANGE(R1004.L2, 24);
	    RANGEDIFF(R1004.L2, 20);
	    R1004.L2.locktime =	(unsigned char)sgrab(7);
	    R1004.L2.ambiguity = (unsigned char)ugrab(8);
	    R1004.L2.CNR = ugrab(8) * CARRIER_NOISE_RATIO_UNITS;
	}
#undef R1004
	break;

    case 1005:			/* Stationary Antenna Reference Point, No Height Information */
#define R1005 rtcm->rtcmtypes.rtcm3_1005
	R1005.station_id = (unsigned short)ugrab(12);
	ugrab(6);		/* reserved */
	temp = ugrab(3);
	if ((temp & 0x04)!=0)
	    R1005.system = NAVSYSTEM_GPS;
	if ((temp & 0x02)!=0)
	    R1005.system = NAVSYSTEM_GLONASS;
	if ((temp & 0x01)!=0)
	    R1005.system = NAVSYSTEM_GALILEO;
	R1005.reference_station = (bool)ugrab(1);
	R1005.ecef_x = sgrab(38) * ANTENNA_POSITION_RESOLUTION;
	R1005.single_receiver = ugrab(1);
	ugrab(1);
	R1005.ecef_y = sgrab(38) * ANTENNA_POSITION_RESOLUTION;
	ugrab(2);
	R1005.ecef_z = sgrab(38) * ANTENNA_POSITION_RESOLUTION;
#undef R1005
	break;

    case 1006:			/* Stationary Antenna Reference Point, with Height Information */
#define R1006 rtcm->rtcmtypes.rtcm3_1006
	R1006.station_id = (unsigned short)ugrab(12);
	(void)ugrab(6);		/* reserved */
	R1006.system = ugrab(3);
	R1006.reference_station = (bool)ugrab(1);
	R1006.ecef_x = sgrab(38) * ANTENNA_POSITION_RESOLUTION;
	R1006.single_receiver = ugrab(1);
	ugrab(1);
	R1006.ecef_y = sgrab(38) * ANTENNA_POSITION_RESOLUTION;
	ugrab(2);
	R1006.ecef_z = sgrab(38) * ANTENNA_POSITION_RESOLUTION;
	R1006.height = ugrab(16) * ANTENNA_POSITION_RESOLUTION;
#undef R1006
	break;

    case 1007:			/* Antenna Descriptor */
	rtcm->rtcmtypes.rtcm3_1007.station_id = (unsigned short)ugrab(12);
	n = (unsigned long)ugrab(8);
	(void)memcpy(rtcm->rtcmtypes.rtcm3_1007.descriptor, buf + 4, n);
	rtcm->rtcmtypes.rtcm3_1007.descriptor[n] = '\0';
	bitcount += 8 * n;
	rtcm->rtcmtypes.rtcm3_1007.setup_id = ugrab(8);
	break;

    case 1008:			/* Antenna Descriptor & Serial Number */
	rtcm->rtcmtypes.rtcm3_1008.station_id = (unsigned short)ugrab(12);
	n = (unsigned long)ugrab(8);
	(void)memcpy(rtcm->rtcmtypes.rtcm3_1008.descriptor, buf + 4, n);
	rtcm->rtcmtypes.rtcm3_1008.descriptor[n] = '\0';
	bitcount += 8 * n;
	rtcm->rtcmtypes.rtcm3_1008.setup_id = ugrab(8);
	n2 = (unsigned long)ugrab(8);
	(void)memcpy(rtcm->rtcmtypes.rtcm3_1008.serial, buf + 6 + n, n2);
	rtcm->rtcmtypes.rtcm3_1008.serial[n2] = '\0';
	bitcount += 8 * n2;
	break;

    case 1009:			/* GLONASS Basic RTK, L1 Only */
	rtcm->rtcmtypes.rtcm3_1009.header.station_id =
	    (unsigned short)ugrab(12);
	rtcm->rtcmtypes.rtcm3_1009.header.tow = (time_t)ugrab(27);
	rtcm->rtcmtypes.rtcm3_1009.header.sync = (bool)ugrab(1);
	rtcm->rtcmtypes.rtcm3_1009.header.satcount = (ushort)ugrab(5);
	rtcm->rtcmtypes.rtcm3_1009.header.smoothing = (bool)ugrab(1);
	rtcm->rtcmtypes.rtcm3_1009.header.interval = (ushort)ugrab(3);
#define R1009 rtcm->rtcmtypes.rtcm3_1009.rtk_data[i]
	for (i = 0; i < rtcm->rtcmtypes.rtcm3_1009.header.satcount; i++) {
	    R1009.ident = (ushort)ugrab(6);
	    R1009.L1.indicator = (bool)ugrab(1);
	    R1009.L1.channel = (ushort)ugrab(5);
	    R1009.L1.pseudorange = ugrab(25) * GLONASS_PSEUDORANGE_RESOLUTION;
	    RANGEDIFF(R1009.L1, 20);
	    R1009.L1.locktime =	(unsigned char)sgrab(7);
	}
#undef R1009
	break;

    case 1010:			/* GLONASS Extended RTK, L1 Only */
	rtcm->rtcmtypes.rtcm3_1010.header.station_id =
	    (unsigned short)ugrab(12);
	rtcm->rtcmtypes.rtcm3_1010.header.tow = (time_t)ugrab(27);
	rtcm->rtcmtypes.rtcm3_1010.header.sync = (bool)ugrab(1);
	rtcm->rtcmtypes.rtcm3_1010.header.satcount = (ushort)ugrab(5);
	rtcm->rtcmtypes.rtcm3_1010.header.smoothing = (bool)ugrab(1);
	rtcm->rtcmtypes.rtcm3_1010.header.interval = (ushort)ugrab(3);
#define R1010 rtcm->rtcmtypes.rtcm3_1010.rtk_data[i]
	for (i = 0; i < rtcm->rtcmtypes.rtcm3_1010.header.satcount; i++) {
	    R1010.ident = (ushort)ugrab(6);
	    R1010.L1.indicator = (bool)ugrab(1);
	    R1010.L1.channel = (ushort)ugrab(5);
	    R1010.L1.pseudorange = ugrab(25) * GLONASS_PSEUDORANGE_RESOLUTION;
	    RANGEDIFF(R1010.L1, 20);
	    R1010.L1.locktime =	(unsigned char)sgrab(7);
	    R1010.L1.ambiguity = (unsigned char)ugrab(7);
	    R1010.L1.CNR = ugrab(8) * CARRIER_NOISE_RATIO_UNITS;
	}
#undef R1010
	break;

    case 1011:			/* GLONASS Basic RTK, L1 & L2 */
	rtcm->rtcmtypes.rtcm3_1011.header.station_id =
	    (unsigned short)ugrab(12);
	rtcm->rtcmtypes.rtcm3_1011.header.tow = (time_t)ugrab(27);
	rtcm->rtcmtypes.rtcm3_1011.header.sync = (bool)ugrab(1);
	rtcm->rtcmtypes.rtcm3_1011.header.satcount = (ushort)ugrab(5);
	rtcm->rtcmtypes.rtcm3_1011.header.smoothing = (bool)ugrab(1);
	rtcm->rtcmtypes.rtcm3_1011.header.interval = (ushort)ugrab(3);
#define R1011 rtcm->rtcmtypes.rtcm3_1011.rtk_data[i]
	for (i = 0; i < rtcm->rtcmtypes.rtcm3_1011.header.satcount; i++) {
	    R1011.ident = (ushort)ugrab(6);
	    R1011.L1.indicator = (bool)ugrab(1);
	    R1011.L1.channel = (ushort)ugrab(5);
	    R1011.L1.pseudorange = ugrab(25) * GLONASS_PSEUDORANGE_RESOLUTION;
	    RANGEDIFF(R1011.L1, 20);
	    R1011.L1.locktime =	(unsigned char)sgrab(7);
	    R1011.L1.ambiguity = (unsigned char)ugrab(7);
	    R1011.L1.CNR = ugrab(8) * CARRIER_NOISE_RATIO_UNITS;
	    R1011.L2.indicator = (bool)ugrab(1);
	    R1011.L2.channel = (ushort)ugrab(5);
	    R1011.L2.pseudorange = ugrab(25) * GLONASS_PSEUDORANGE_RESOLUTION;
	    RANGEDIFF(R1011.L2, 20);
	    R1011.L2.locktime =	(unsigned char)sgrab(7);
	    R1011.L2.ambiguity = (unsigned char)ugrab(7);
	    R1011.L2.CNR = ugrab(8) * CARRIER_NOISE_RATIO_UNITS;
	}
#undef R1011
	break;

    case 1012:			/* GLONASS Extended RTK, L1 & L2 */
	rtcm->rtcmtypes.rtcm3_1012.header.station_id =
	    (unsigned short)ugrab(12);
	rtcm->rtcmtypes.rtcm3_1012.header.tow = (time_t)ugrab(27);
	rtcm->rtcmtypes.rtcm3_1012.header.sync = (bool)ugrab(1);
	rtcm->rtcmtypes.rtcm3_1012.header.satcount = (ushort)ugrab(5);
	rtcm->rtcmtypes.rtcm3_1012.header.smoothing = (bool)ugrab(1);
	rtcm->rtcmtypes.rtcm3_1012.header.interval = (ushort)ugrab(3);
#define R1012 rtcm->rtcmtypes.rtcm3_1012.rtk_data[i]
	for (i = 0; i < rtcm->rtcmtypes.rtcm3_1012.header.satcount; i++) {
	    unsigned int rangeincr;
	    R1012.ident = (ushort)ugrab(6);
	    R1012.L1.indicator = (bool)ugrab(1);
	    R1012.L1.channel = (ushort)ugrab(5);
	    R1012.L1.pseudorange = ugrab(25) * GLONASS_PSEUDORANGE_RESOLUTION;
	    RANGEDIFF(R1012.L1, 20);
	    R1012.L1.locktime =	(unsigned char)ugrab(7);
	    R1012.L1.ambiguity = (unsigned char)ugrab(7);
	    R1012.L1.CNR = (unsigned char)ugrab(8);
	    R1012.L2.indicator = (bool)ugrab(2);
	    rangeincr = ugrab(14);
	    if (rangeincr == GLONASS_INVALID_RANGEINCR)
		R1012.L2.pseudorange = 0;
	    else
		R1012.L2.pseudorange = R1012.L1.pseudorange + (rangeincr * GLONASS_PSEUDORANGE_RESOLUTION);
	    RANGEDIFF(R1012.L2, 20);
	    R1012.L2.locktime =	(unsigned char)sgrab(7);
	    R1012.L2.CNR = (unsigned char)ugrab(8);
	}
#undef R1012
	break;

    case 1013:			/* System Parameters */
	rtcm->rtcmtypes.rtcm3_1013.station_id = (unsigned short)ugrab(12);
	rtcm->rtcmtypes.rtcm3_1013.mjd = (unsigned short)ugrab(16);
	rtcm->rtcmtypes.rtcm3_1013.sod = (unsigned short)ugrab(17);
	rtcm->rtcmtypes.rtcm3_1013.ncount = (unsigned long)ugrab(5);
	rtcm->rtcmtypes.rtcm3_1013.leapsecs = (unsigned char)ugrab(8);
#define R1013 rtcm->rtcmtypes.rtcm3_1013.announcements[i]
	for (i = 0; i < rtcm->rtcmtypes.rtcm3_1013.ncount; i++) {
	    R1013.id = (unsigned short)ugrab(12);
	    R1013.sync = (bool)ugrab(1);
	    R1013.interval = (unsigned short)ugrab(16);
	}
#undef R1013
	break;

    case 1014:
	rtcm->rtcmtypes.rtcm3_1014.network_id = (int)ugrab(8);
	rtcm->rtcmtypes.rtcm3_1014.subnetwork_id = (int)ugrab(4);
	rtcm->rtcmtypes.rtcm3_1014.stationcount = (char)ugrab(5);
	rtcm->rtcmtypes.rtcm3_1014.master_id = (int)ugrab(12);
	rtcm->rtcmtypes.rtcm3_1014.aux_id = (int)ugrab(12);
	rtcm->rtcmtypes.rtcm3_1014.d_lat =
	    (unsigned short)ugrab(20) * ANTENNA_DEGREE_RESOLUTION;
	rtcm->rtcmtypes.rtcm3_1014.d_lon =
	    (unsigned short)ugrab(21) * ANTENNA_DEGREE_RESOLUTION;
	rtcm->rtcmtypes.rtcm3_1014.d_alt = (unsigned short)ugrab(23) / 1000;
	break;

    case 1015:
	break;

    case 1016:
	break;

    case 1017:
	break;

    case 1018:
	break;

    case 1019:
	break;

    case 1020:
	break;

    case 1029:
	rtcm->rtcmtypes.rtcm3_1029.station_id = (unsigned short)ugrab(12);
	rtcm->rtcmtypes.rtcm3_1029.mjd = (unsigned short)ugrab(16);
	rtcm->rtcmtypes.rtcm3_1029.sod = (unsigned short)ugrab(17);
	rtcm->rtcmtypes.rtcm3_1029.len = (unsigned long)ugrab(7);
	rtcm->rtcmtypes.rtcm3_1029.unicode_units = (size_t)ugrab(8);
	(void)memcpy(rtcm->rtcmtypes.rtcm3_1029.text, 
		     buf + 12, rtcm->rtcmtypes.rtcm3_1029.unicode_units);
	break;

    default:
	/* 
	 * Leader bytes, message length, and checksum won't be copied.
	 * The first 12 bits of the copied payload will be the type field.
	 */
	memcpy(rtcm->rtcmtypes.data, buf+3, rtcm->length);
	break;
    }
#undef RANGEDIFF
#undef GPS_PSEUDORANGE
#undef sgrab
#undef ugrab
    /*@ +evalorder +sefparams +mayaliasunique @*/
}

/* *INDENT-ON* */
/*@ +type @*/

#endif /* RTCM104V3_ENABLE */
