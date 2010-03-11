/* $Id$ */
/*****************************************************************************

This file is Copyright (c) 2010 by the GPSD project
BSD terms apply: see the file COPYING in the distribution root for details.

This is a decoder for RTCM-104 3.x, a serial protocol used for
broadcasting pseudorange corrections from differential-GPS reference
stations.  The applicable specification is RTCM 10403.1: RTCM Paper
177-2006-SC104-STD.  This obsolesces the esrlier RTCM-104 2.x
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

*****************************************************************************/

#include <sys/types.h>
#ifndef S_SPLINT_S
#include <unistd.h>
#endif /* S_SPLINT_S */
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
#include "gpsd_config.h"
#ifndef S_SPLINT_S
 #ifdef HAVE_ARPA_INET
  #include <arpa/inet.h>	/* for ntohl(3) and friends */
 #endif /* HAVE_ARPA_INET */
#endif /* S_SPLINT_S */

#include "gpsd.h"
#include "bits.h"

#ifdef RTCM104V3_ENABLE

/* scaling constants for RTCM3 real number types */
#define PSEUDORANGE_RESOLUTION		0.2	/* DF011 */
#define PSEUDORANGE_DIFF_RESOLUTION	0.0005	/* DF012 */
#define CARRIER_NOISE_RATIO_UNITS	0.25	/* DF015 */
#define ANTENNA_POSITION_RESOLUTION	0.0001	/* DF025-027 */
#define ANTENNA_DEGREE_RESOLUTION	25e-6	/* DF062 */
#define GPS_EPOCH_TIME_RESOLUTION	0.1	/* DF065 */
#define PHASE_CORRECTION_RESOLUTION	0.5	/* DF069-070 */

/* Other magic values */
#define INVALID_PSEUDORANGE		0x80000	/* DF012 */

/*@ -type @*/	/* re-enable when we're ready to take this live */

void rtcm3_unpack(/*@out@*/struct rtcm3_t *rtcm, char *buf)
/* break out the raw bits into the scaled report-structure fields */
{
    unsigned int n, n2;
    int bitcount = 0;
    unsigned int i;
    signed long temp;

    /*@ -evalorder -sefparams -mayaliasunique @*/    
#define ugrab(width)	(bitcount += width, ubits(buf, bitcount-width, width))
#define sgrab(width)	(bitcount += width, sbits(buf, bitcount-width, width))
    assert(ugrab(8) == 0xD3);
    assert(ugrab(6) == 0x00);

    rtcm->length = (uint)ugrab(10);
    rtcm->type = (uint)ugrab(12);

    switch(rtcm->type) {
    case 1001:	/* GPS Basic RTK, L1 Only */
	rtcm->rtcmtypes.rtcm3_1001.header.station_id = (uint)ugrab(12);
	rtcm->rtcmtypes.rtcm3_1001.header.tow        = (time_t)ugrab(30);
	rtcm->rtcmtypes.rtcm3_1001.header.sync       = (bool)ugrab(1);
	rtcm->rtcmtypes.rtcm3_1001.header.satcount   = (ushort)ugrab(5);
	rtcm->rtcmtypes.rtcm3_1001.header.smoothing  = (bool)ugrab(1);
	rtcm->rtcmtypes.rtcm3_1001.header.interval   = (ushort)ugrab(3);
	for (i = 0; i < rtcm->rtcmtypes.rtcm3_1001.header.satcount; i++) {
	    rtcm->rtcmtypes.rtcm3_1001.rtk_data[i].ident = (ushort)ugrab(6);
	    rtcm->rtcmtypes.rtcm3_1001.rtk_data[i].L1.indicator = (unsigned char)ugrab(1);
	    temp = (unsigned long)ugrab(24);
	    if (temp == INVALID_PSEUDORANGE)
		rtcm->rtcmtypes.rtcm3_1001.rtk_data[i].L1.pseudorange  = 0;
	    else
		rtcm->rtcmtypes.rtcm3_1001.rtk_data[i].L1.pseudorange  = temp * PSEUDORANGE_RESOLUTION;
	    temp = (long)sgrab(20);
	    if (temp == INVALID_PSEUDORANGE)
		rtcm->rtcmtypes.rtcm3_1001.rtk_data[i].L1.rangediff  = 0;
	    else
		rtcm->rtcmtypes.rtcm3_1001.rtk_data[i].L1.rangediff  = temp * PSEUDORANGE_DIFF_RESOLUTION;
	    rtcm->rtcmtypes.rtcm3_1001.rtk_data[i].L1.locktime = (unsigned char)sgrab(7);
	}
	assert(bitcount == 64 + 58*rtcm->rtcmtypes.rtcm3_1001.header.satcount);
	break;

    case 1002:	/* GPS Extended RTK, L1 Only */
	rtcm->rtcmtypes.rtcm3_1002.header.station_id = (uint)ugrab(12);
	rtcm->rtcmtypes.rtcm3_1002.header.tow        = (time_t)ugrab(30);
	rtcm->rtcmtypes.rtcm3_1002.header.sync       = (bool)ugrab(1);
	rtcm->rtcmtypes.rtcm3_1002.header.satcount   = (ushort)ugrab(5);
	rtcm->rtcmtypes.rtcm3_1002.header.smoothing  = (bool)ugrab(1);
	rtcm->rtcmtypes.rtcm3_1002.header.interval   = (ushort)ugrab(3);
	for (i = 0; i < rtcm->rtcmtypes.rtcm3_1002.header.satcount; i++) {
	    rtcm->rtcmtypes.rtcm3_1002.rtk_data[i].ident = (ushort)ugrab(6);
	    rtcm->rtcmtypes.rtcm3_1002.rtk_data[i].L1.indicator = (unsigned char)ugrab(1);
	    temp = (unsigned long)ugrab(24);
	    if (temp == INVALID_PSEUDORANGE)
		rtcm->rtcmtypes.rtcm3_1002.rtk_data[i].L1.pseudorange  = 0;
	    else
		rtcm->rtcmtypes.rtcm3_1002.rtk_data[i].L1.pseudorange  = temp * PSEUDORANGE_RESOLUTION;
	    temp = (long)sgrab(20);
	    if (temp == INVALID_PSEUDORANGE)
		rtcm->rtcmtypes.rtcm3_1002.rtk_data[i].L1.rangediff  = 0;
	    else
		rtcm->rtcmtypes.rtcm3_1002.rtk_data[i].L1.rangediff  = temp * PSEUDORANGE_DIFF_RESOLUTION;
	    rtcm->rtcmtypes.rtcm3_1002.rtk_data[i].L1.locktime = (unsigned char)sgrab(7);
	    rtcm->rtcmtypes.rtcm3_1002.rtk_data[i].L1.ambiguity = (bool)ugrab(8);
	    rtcm->rtcmtypes.rtcm3_1002.rtk_data[i].L1.CNR = (bool)ugrab(8) * CARRIER_NOISE_RATIO_UNITS;
	}
	assert(bitcount == 64 + 74*rtcm->rtcmtypes.rtcm3_1002.header.satcount);
	break;

    case 1003:	/* GPS Basic RTK, L1 & L2 */
	rtcm->rtcmtypes.rtcm3_1003.header.station_id = (uint)ugrab(12);
	rtcm->rtcmtypes.rtcm3_1003.header.tow        = (time_t)ugrab(30);
	rtcm->rtcmtypes.rtcm3_1003.header.sync       = (bool)ugrab(1);
	rtcm->rtcmtypes.rtcm3_1003.header.satcount   = (ushort)ugrab(5);
	rtcm->rtcmtypes.rtcm3_1003.header.smoothing  = (bool)ugrab(1);
	rtcm->rtcmtypes.rtcm3_1003.header.interval   = (ushort)ugrab(3);
	for (i = 0; i < rtcm->rtcmtypes.rtcm3_1003.header.satcount; i++) {
	    rtcm->rtcmtypes.rtcm3_1003.rtk_data[i].ident = (ushort)ugrab(6);
	    rtcm->rtcmtypes.rtcm3_1003.rtk_data[i].L1.indicator = (unsigned char)ugrab(1);
	    temp = (unsigned long)ugrab(24);
	    if (temp == INVALID_PSEUDORANGE)
		rtcm->rtcmtypes.rtcm3_1003.rtk_data[i].L1.pseudorange  = 0;
	    else
		rtcm->rtcmtypes.rtcm3_1003.rtk_data[i].L1.pseudorange  = temp * PSEUDORANGE_RESOLUTION;
	    temp = (long)sgrab(20);
	    if (temp == INVALID_PSEUDORANGE)
		rtcm->rtcmtypes.rtcm3_1003.rtk_data[i].L1.rangediff  = 0;
	    else
		rtcm->rtcmtypes.rtcm3_1003.rtk_data[i].L1.rangediff  = temp * PSEUDORANGE_DIFF_RESOLUTION;
	    rtcm->rtcmtypes.rtcm3_1003.rtk_data[i].L1.locktime = (unsigned char)sgrab(7);
	    rtcm->rtcmtypes.rtcm3_1003.rtk_data[i].L2.indicator = (unsigned char)ugrab(2);
	    temp = (unsigned long)ugrab(24);
	    if (temp == INVALID_PSEUDORANGE)
		rtcm->rtcmtypes.rtcm3_1003.rtk_data[i].L2.pseudorange  = 0;
	    else
		rtcm->rtcmtypes.rtcm3_1003.rtk_data[i].L2.pseudorange  = temp * PSEUDORANGE_RESOLUTION;
	    temp = (long)sgrab(20);
	    if (temp == INVALID_PSEUDORANGE)
		rtcm->rtcmtypes.rtcm3_1003.rtk_data[i].L2.rangediff  = 0;
	    else
		rtcm->rtcmtypes.rtcm3_1003.rtk_data[i].L2.rangediff  = temp * PSEUDORANGE_DIFF_RESOLUTION;
	    rtcm->rtcmtypes.rtcm3_1003.rtk_data[i].L2.locktime = (unsigned char)sgrab(7);
	}
	assert(bitcount == 64 + 101*rtcm->rtcmtypes.rtcm3_1003.header.satcount);
	break;

    case 1004:	/* GPS Extended RTK, L1 & L2 */
	rtcm->rtcmtypes.rtcm3_1004.header.station_id = (uint)ugrab(12);
	rtcm->rtcmtypes.rtcm3_1004.header.tow        = (time_t)ugrab(30);
	rtcm->rtcmtypes.rtcm3_1004.header.sync       = (bool)ugrab(1);
	rtcm->rtcmtypes.rtcm3_1004.header.satcount   = (ushort)ugrab(5);
	rtcm->rtcmtypes.rtcm3_1004.header.smoothing  = (bool)ugrab(1);
	rtcm->rtcmtypes.rtcm3_1004.header.interval   = (ushort)ugrab(3);
	for (i = 0; i < rtcm->rtcmtypes.rtcm3_1004.header.satcount; i++) {
	    rtcm->rtcmtypes.rtcm3_1004.rtk_data[i].ident = (ushort)ugrab(6);
	    rtcm->rtcmtypes.rtcm3_1004.rtk_data[i].L1.indicator = (bool)ugrab(1);
	    temp = (unsigned long)ugrab(24);
	    if (temp == INVALID_PSEUDORANGE)
		rtcm->rtcmtypes.rtcm3_1004.rtk_data[i].L1.pseudorange  = 0;
	    else
		rtcm->rtcmtypes.rtcm3_1004.rtk_data[i].L1.pseudorange  = temp * PSEUDORANGE_RESOLUTION;
	    temp = (long)sgrab(20);
	    if (temp == INVALID_PSEUDORANGE)
		rtcm->rtcmtypes.rtcm3_1004.rtk_data[i].L1.rangediff  = 0;
	    else
		rtcm->rtcmtypes.rtcm3_1004.rtk_data[i].L1.rangediff  = temp * PSEUDORANGE_DIFF_RESOLUTION;
	    rtcm->rtcmtypes.rtcm3_1004.rtk_data[i].L1.locktime = (unsigned char)sgrab(7);
	    rtcm->rtcmtypes.rtcm3_1004.rtk_data[i].L1.ambiguity = (bool)ugrab(8);
	    rtcm->rtcmtypes.rtcm3_1004.rtk_data[i].L1.CNR = (bool)ugrab(8) * CARRIER_NOISE_RATIO_UNITS;
	    rtcm->rtcmtypes.rtcm3_1004.rtk_data[i].L2.indicator = (unsigned char)ugrab(2);
	    temp = (unsigned long)ugrab(24);
	    if (temp == INVALID_PSEUDORANGE)
		rtcm->rtcmtypes.rtcm3_1004.rtk_data[i].L2.pseudorange  = 0;
	    else
		rtcm->rtcmtypes.rtcm3_1004.rtk_data[i].L2.pseudorange  = temp * PSEUDORANGE_RESOLUTION;
	    temp = (long)sgrab(20);
	    if (temp == INVALID_PSEUDORANGE)
		rtcm->rtcmtypes.rtcm3_1004.rtk_data[i].L2.rangediff  = 0;
	    else
		rtcm->rtcmtypes.rtcm3_1004.rtk_data[i].L2.rangediff  = temp * PSEUDORANGE_DIFF_RESOLUTION;
	    rtcm->rtcmtypes.rtcm3_1004.rtk_data[i].L2.locktime = (unsigned char)sgrab(7);
	    rtcm->rtcmtypes.rtcm3_1004.rtk_data[i].L2.ambiguity = (bool)ugrab(8);
	    rtcm->rtcmtypes.rtcm3_1004.rtk_data[i].L2.CNR = (bool)ugrab(8) * CARRIER_NOISE_RATIO_UNITS;
	}
	assert(bitcount == 64 + 125*rtcm->rtcmtypes.rtcm3_1004.header.satcount);
	break;

    case 1005:	/* Stationary Antenna Reference Point, No Height Information */
	rtcm->rtcmtypes.rtcm3_1005.station_id = (unsigned short)ugrab(12);
	ugrab(6);	/* reserved */
	if ((bool)ugrab(1))
	    rtcm->rtcmtypes.rtcm3_1005.system = NAVSYSTEM_GPS;
	else if ((bool)ugrab(1))
	    rtcm->rtcmtypes.rtcm3_1005.system = NAVSYSTEM_GLONASS;
	else if ((bool)ugrab(1))
	    rtcm->rtcmtypes.rtcm3_1005.system = NAVSYSTEM_GALILEO;
	rtcm->rtcmtypes.rtcm3_1005.reference_station = (bool)ugrab(1);
	rtcm->rtcmtypes.rtcm3_1005.ecef_x = sgrab(38) * ANTENNA_POSITION_RESOLUTION;
	rtcm->rtcmtypes.rtcm3_1005.single_receiver = ugrab(1);
	ugrab(1);
	rtcm->rtcmtypes.rtcm3_1005.ecef_y = sgrab(38) * ANTENNA_POSITION_RESOLUTION;
	ugrab(2);
	rtcm->rtcmtypes.rtcm3_1005.ecef_z = sgrab(38) * ANTENNA_POSITION_RESOLUTION;
	assert(bitcount == 152);
	break;

    case 1006:	/* Stationary Antenna Reference Point, with Height Information */
	rtcm->rtcmtypes.rtcm3_1006.station_id = (unsigned short)ugrab(12);
	ugrab(6);	/* reserved */
	if ((bool)ugrab(1))
	    rtcm->rtcmtypes.rtcm3_1006.system = NAVSYSTEM_GPS;
	else if ((bool)ugrab(1))
	    rtcm->rtcmtypes.rtcm3_1006.system = NAVSYSTEM_GLONASS;
	else if ((bool)ugrab(1))
	    rtcm->rtcmtypes.rtcm3_1006.system = NAVSYSTEM_GALILEO;
	rtcm->rtcmtypes.rtcm3_1006.reference_station = (bool)ugrab(1);
	rtcm->rtcmtypes.rtcm3_1006.ecef_x = sgrab(38) * ANTENNA_POSITION_RESOLUTION;
	rtcm->rtcmtypes.rtcm3_1006.single_receiver = ugrab(1);
	ugrab(1);
	rtcm->rtcmtypes.rtcm3_1006.ecef_y = sgrab(38) * ANTENNA_POSITION_RESOLUTION;
	ugrab(2);
	rtcm->rtcmtypes.rtcm3_1006.ecef_z = sgrab(38) * ANTENNA_POSITION_RESOLUTION;
	rtcm->rtcmtypes.rtcm3_1006.height = ugrab(16) * ANTENNA_POSITION_RESOLUTION;
	assert(bitcount == 168);
	break;

    case 1007:	/* Antenna Descriptor */
	rtcm->rtcmtypes.rtcm3_1007.station_id = (unsigned short)ugrab(12);
	n = (unsigned long)ugrab(8);
	(void)memcpy(rtcm->rtcmtypes.rtcm3_1007.descriptor, buf + 4, n);
	rtcm->rtcmtypes.rtcm3_1007.descriptor[n] = '\0';
	bitcount += 8 * n;
	rtcm->rtcmtypes.rtcm3_1007.setup_id = ugrab(8);
	assert(bitcount == (int)(40 + 8*n));
	break;

    case 1008:	/* Antenna Descriptor & Serial Number */
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
	assert(bitcount == (int)(48 + 8*(n+n2)));
	break;

    case 1009:	/* GLONASS Basic RTK, L1 Only */
	rtcm->rtcmtypes.rtcm3_1009.header.station_id = (unsigned short)ugrab(12);
	rtcm->rtcmtypes.rtcm3_1009.header.tow = (time_t)ugrab(27);
	rtcm->rtcmtypes.rtcm3_1009.header.sync = (bool)ugrab(1);
	rtcm->rtcmtypes.rtcm3_1009.header.satcount = (ushort)ugrab(5);
	rtcm->rtcmtypes.rtcm3_1009.header.smoothing = (bool)ugrab(1);
	rtcm->rtcmtypes.rtcm3_1009.header.interval   = (ushort)ugrab(3);
	for (i = 0; i < rtcm->rtcmtypes.rtcm3_1009.header.satcount; i++) {
	    rtcm->rtcmtypes.rtcm3_1009.rtk_data[i].ident = (ushort)ugrab(6);
	    rtcm->rtcmtypes.rtcm3_1009.rtk_data[i].L1.indicator = (bool)ugrab(1);
	    rtcm->rtcmtypes.rtcm3_1009.rtk_data[i].L1.channel = (ushort)ugrab(5);
	    temp = (unsigned long)ugrab(25);
	    if (temp == INVALID_PSEUDORANGE)
		rtcm->rtcmtypes.rtcm3_1009.rtk_data[i].L1.pseudorange  = 0;
	    else
		rtcm->rtcmtypes.rtcm3_1009.rtk_data[i].L1.pseudorange  = temp * PSEUDORANGE_RESOLUTION;
	    temp = (long)sgrab(20);
	    if (temp == INVALID_PSEUDORANGE)
		rtcm->rtcmtypes.rtcm3_1009.rtk_data[i].L1.rangediff  = 0;
	    else
		rtcm->rtcmtypes.rtcm3_1009.rtk_data[i].L1.rangediff  = temp * PSEUDORANGE_DIFF_RESOLUTION;
	    rtcm->rtcmtypes.rtcm3_1009.rtk_data[i].L1.locktime = (unsigned char)sgrab(7);
	}
	assert(bitcount == 61 + 64*rtcm->rtcmtypes.rtcm3_1009.header.satcount);
	break;

    case 1010:	/* GLONASS Extended RTK, L1 Only */
	rtcm->rtcmtypes.rtcm3_1010.header.station_id = (unsigned short)ugrab(12);
	rtcm->rtcmtypes.rtcm3_1010.header.tow = (time_t)ugrab(27);
	rtcm->rtcmtypes.rtcm3_1010.header.sync = (bool)ugrab(1);
	rtcm->rtcmtypes.rtcm3_1010.header.satcount = (ushort)ugrab(5);
	rtcm->rtcmtypes.rtcm3_1010.header.smoothing = (bool)ugrab(1);
	rtcm->rtcmtypes.rtcm3_1010.header.interval   = (ushort)ugrab(3);
	for (i = 0; i < rtcm->rtcmtypes.rtcm3_1010.header.satcount; i++) {
	    rtcm->rtcmtypes.rtcm3_1010.rtk_data[i].ident = (ushort)ugrab(6);
	    rtcm->rtcmtypes.rtcm3_1010.rtk_data[i].L1.indicator = (bool)ugrab(1);
	    rtcm->rtcmtypes.rtcm3_1010.rtk_data[i].L1.channel = (ushort)ugrab(5);
	    temp = (unsigned long)ugrab(25);
	    if (temp == INVALID_PSEUDORANGE)
		rtcm->rtcmtypes.rtcm3_1010.rtk_data[i].L1.pseudorange  = 0;
	    else
		rtcm->rtcmtypes.rtcm3_1010.rtk_data[i].L1.pseudorange  = temp * PSEUDORANGE_RESOLUTION;
	    temp = (long)sgrab(20);
	    if (temp == INVALID_PSEUDORANGE)
		rtcm->rtcmtypes.rtcm3_1010.rtk_data[i].L1.rangediff  = 0;
	    else
		rtcm->rtcmtypes.rtcm3_1010.rtk_data[i].L1.rangediff  = temp * PSEUDORANGE_DIFF_RESOLUTION;
	    rtcm->rtcmtypes.rtcm3_1010.rtk_data[i].L1.locktime = (unsigned char)sgrab(7);
	    rtcm->rtcmtypes.rtcm3_1010.rtk_data[i].L1.ambiguity = (bool)ugrab(7);
	    rtcm->rtcmtypes.rtcm3_1010.rtk_data[i].L1.CNR = (bool)ugrab(8) * CARRIER_NOISE_RATIO_UNITS;
	}
	assert(bitcount == 61 + 79*rtcm->rtcmtypes.rtcm3_1010.header.satcount);
	break;

    case 1011:	/* GLONASS Basic RTK, L1 & L2 */
	rtcm->rtcmtypes.rtcm3_1011.header.station_id = (unsigned short)ugrab(12);
	rtcm->rtcmtypes.rtcm3_1011.header.tow = (time_t)ugrab(27);
	rtcm->rtcmtypes.rtcm3_1011.header.sync = (bool)ugrab(1);
	rtcm->rtcmtypes.rtcm3_1011.header.satcount = (ushort)ugrab(5);
	rtcm->rtcmtypes.rtcm3_1011.header.smoothing = (bool)ugrab(1);
	rtcm->rtcmtypes.rtcm3_1011.header.interval   = (ushort)ugrab(3);
	for (i = 0; i < rtcm->rtcmtypes.rtcm3_1011.header.satcount; i++) {
	    rtcm->rtcmtypes.rtcm3_1011.rtk_data[i].ident = (ushort)ugrab(6);
	    rtcm->rtcmtypes.rtcm3_1011.rtk_data[i].L1.indicator = (bool)ugrab(1);
	    rtcm->rtcmtypes.rtcm3_1011.rtk_data[i].L1.channel = (ushort)ugrab(5);
	    temp = (unsigned long)ugrab(25);
	    if (temp == INVALID_PSEUDORANGE)
		rtcm->rtcmtypes.rtcm3_1011.rtk_data[i].L1.pseudorange  = 0;
	    else
		rtcm->rtcmtypes.rtcm3_1011.rtk_data[i].L1.pseudorange  = temp * PSEUDORANGE_RESOLUTION;
	    temp = (long)sgrab(20);
	    if (temp == INVALID_PSEUDORANGE)
		rtcm->rtcmtypes.rtcm3_1011.rtk_data[i].L1.rangediff  = 0;
	    else
		rtcm->rtcmtypes.rtcm3_1011.rtk_data[i].L1.rangediff  = temp * PSEUDORANGE_DIFF_RESOLUTION;
	    rtcm->rtcmtypes.rtcm3_1011.rtk_data[i].L1.locktime = (unsigned char)sgrab(7);
	    rtcm->rtcmtypes.rtcm3_1011.rtk_data[i].L1.ambiguity = (bool)ugrab(7);
	    rtcm->rtcmtypes.rtcm3_1011.rtk_data[i].L1.CNR = (bool)ugrab(8) * CARRIER_NOISE_RATIO_UNITS;
	    rtcm->rtcmtypes.rtcm3_1011.rtk_data[i].L2.indicator = (bool)ugrab(1);
	    rtcm->rtcmtypes.rtcm3_1011.rtk_data[i].L2.channel = (ushort)ugrab(5);
	    temp = (unsigned long)ugrab(25);
	    if (temp == INVALID_PSEUDORANGE)
		rtcm->rtcmtypes.rtcm3_1011.rtk_data[i].L2.pseudorange  = 0;
	    else
		rtcm->rtcmtypes.rtcm3_1011.rtk_data[i].L2.pseudorange  = temp * PSEUDORANGE_RESOLUTION;
	    temp = (long)sgrab(20);
	    if (temp == INVALID_PSEUDORANGE)
		rtcm->rtcmtypes.rtcm3_1011.rtk_data[i].L2.rangediff  = 0;
	    else
		rtcm->rtcmtypes.rtcm3_1011.rtk_data[i].L2.rangediff  = temp * PSEUDORANGE_DIFF_RESOLUTION;
	    rtcm->rtcmtypes.rtcm3_1011.rtk_data[i].L2.locktime = (unsigned char)sgrab(7);
	    rtcm->rtcmtypes.rtcm3_1011.rtk_data[i].L2.ambiguity = (bool)ugrab(7);
	    rtcm->rtcmtypes.rtcm3_1011.rtk_data[i].L2.CNR = (bool)ugrab(8) * CARRIER_NOISE_RATIO_UNITS;
	}
	assert(bitcount == 61 + 107*rtcm->rtcmtypes.rtcm3_1011.header.satcount);
	break;

    case 1012:	/* GLONASS Extended RTK, L1 & L2 */
	rtcm->rtcmtypes.rtcm3_1012.header.station_id = (unsigned short)ugrab(12);
	rtcm->rtcmtypes.rtcm3_1012.header.tow = (time_t)ugrab(27);
	rtcm->rtcmtypes.rtcm3_1012.header.sync = (bool)ugrab(1);
	rtcm->rtcmtypes.rtcm3_1012.header.satcount = (ushort)ugrab(5);
	rtcm->rtcmtypes.rtcm3_1012.header.smoothing = (bool)ugrab(1);
	rtcm->rtcmtypes.rtcm3_1012.header.interval   = (ushort)ugrab(3);
	for (i = 0; i < rtcm->rtcmtypes.rtcm3_1012.header.satcount; i++) {
	    rtcm->rtcmtypes.rtcm3_1012.rtk_data[i].ident = (ushort)ugrab(6);
	    rtcm->rtcmtypes.rtcm3_1012.rtk_data[i].L1.indicator = (bool)ugrab(1);
	    rtcm->rtcmtypes.rtcm3_1012.rtk_data[i].L1.channel = (ushort)ugrab(5);
	    temp = (unsigned long)ugrab(25);
	    if (temp == INVALID_PSEUDORANGE)
		rtcm->rtcmtypes.rtcm3_1012.rtk_data[i].L1.pseudorange  = 0;
	    else
		rtcm->rtcmtypes.rtcm3_1012.rtk_data[i].L1.pseudorange  = temp * PSEUDORANGE_RESOLUTION;
	    temp = (long)sgrab(20);
	    if (temp == INVALID_PSEUDORANGE)
		rtcm->rtcmtypes.rtcm3_1012.rtk_data[i].L1.rangediff  = 0;
	    else
		rtcm->rtcmtypes.rtcm3_1012.rtk_data[i].L1.rangediff  = temp * PSEUDORANGE_DIFF_RESOLUTION;
	    rtcm->rtcmtypes.rtcm3_1012.rtk_data[i].L1.locktime = (unsigned char)sgrab(7);
	    rtcm->rtcmtypes.rtcm3_1012.rtk_data[i].L2.indicator = (bool)ugrab(1);
	    temp = (unsigned long)ugrab(25);
	    if (temp == INVALID_PSEUDORANGE)
		rtcm->rtcmtypes.rtcm3_1012.rtk_data[i].L2.pseudorange  = 0;
	    else
		rtcm->rtcmtypes.rtcm3_1012.rtk_data[i].L2.pseudorange  = temp * PSEUDORANGE_RESOLUTION;
	    temp = (long)sgrab(20);
	    if (temp == INVALID_PSEUDORANGE)
		rtcm->rtcmtypes.rtcm3_1012.rtk_data[i].L2.rangediff  = 0;
	    else
		rtcm->rtcmtypes.rtcm3_1012.rtk_data[i].L2.rangediff  = temp * PSEUDORANGE_DIFF_RESOLUTION;
	    rtcm->rtcmtypes.rtcm3_1012.rtk_data[i].L2.locktime = (unsigned char)sgrab(7);
	}
	assert(bitcount == 61 + 130*rtcm->rtcmtypes.rtcm3_1012.header.satcount);
	break;

    case 1013:	/* System Parameters */
	rtcm->rtcmtypes.rtcm3_1013.station_id =(unsigned short)ugrab(12);
	rtcm->rtcmtypes.rtcm3_1013.mjd = (unsigned short)ugrab(16);
	rtcm->rtcmtypes.rtcm3_1013.sod = (unsigned short)ugrab(17);
	rtcm->rtcmtypes.rtcm3_1013.ncount = (unsigned long)ugrab(5);
	rtcm->rtcmtypes.rtcm3_1013.leapsecs = (unsigned char)ugrab(8);
	for (i = 0; i < rtcm->rtcmtypes.rtcm3_1013.ncount; i++) {
	    rtcm->rtcmtypes.rtcm3_1013.announcements[i].id =  (unsigned short)ugrab(12);
	    rtcm->rtcmtypes.rtcm3_1013.announcements[i].sync =  (bool)ugrab(1);
	    rtcm->rtcmtypes.rtcm3_1013.announcements[i].interval =  (unsigned short)ugrab(16);
	}
	assert(bitcount == 70 + 29*rtcm->rtcmtypes.rtcm3_1013.ncount);
	break;

    case 1014:
	rtcm->rtcmtypes.rtcm3_1014.network_id =(int)ugrab(8);
	rtcm->rtcmtypes.rtcm3_1014.subnetwork_id =(int)ugrab(4);
	rtcm->rtcmtypes.rtcm3_1014.stationcount =(char)ugrab(5);
	rtcm->rtcmtypes.rtcm3_1014.master_id =(int)ugrab(12);
	rtcm->rtcmtypes.rtcm3_1014.aux_id =(int)ugrab(12);
	rtcm->rtcmtypes.rtcm3_1014.d_lat =(unsigned short)ugrab(20) * ANTENNA_DEGREE_RESOLUTION;
	rtcm->rtcmtypes.rtcm3_1014.d_lon =(unsigned short)ugrab(21) * ANTENNA_DEGREE_RESOLUTION;
	rtcm->rtcmtypes.rtcm3_1014.d_alt =(unsigned short)ugrab(23) / 1000;
	assert(bitcount == 117);
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
	n = rtcm->rtcmtypes.rtcm3_1029.unicode_units = (unsigned long)ugrab(8);
	(void)memcpy(rtcm->rtcmtypes.rtcm3_1029.text, buf+9, n);
	bitcount += 8*n;
	assert(bitcount == (int)(72+8*n));
	break;
    }
#undef sgrab
#undef ugrab
    /*@ +evalorder +sefparams +mayaliasunique @*/    
}

void rtcm3_dump(struct rtcm3_t *rtcm, FILE *fp)
/* dump the contents of a parsed RTCM104 message */
{
    int i;

    char *systems[] = {"GPS", "Glonass", "Galileo", "unknown"};

    (void)fprintf(fp, "%u (%u):\n", rtcm->type, rtcm->length);

#define BOOL(c)	(c!=0 ? 't' : 'f')
#define CODE(x) (unsigned int)(x)
#define INT(x) (unsigned int)(x)
    switch(rtcm->type) {
	case 1001:
	    (void)fprintf(fp, 
			  "  #station_id=%u, tow=%d sync=%c smoothing=%c interval=%u satcount=%u", 
			  rtcm->rtcmtypes.rtcm3_1001.header.station_id,
			  (int)rtcm->rtcmtypes.rtcm3_1001.header.tow,
			  BOOL(rtcm->rtcmtypes.rtcm3_1001.header.sync),
			  BOOL(rtcm->rtcmtypes.rtcm3_1001.header.smoothing),
			  rtcm->rtcmtypes.rtcm3_1001.header.interval,
			  rtcm->rtcmtypes.rtcm3_1001.header.satcount);
	    for (i = 0; i < rtcm->rtcmtypes.rtcm3_1001.header.satcount; i++) {
		(void)fprintf(fp, 
			      "    ident=%u\n      L1: ind=%u prange=%8.1f delta=%6.4f lockt=%u\n", 
			      rtcm->rtcmtypes.rtcm3_1001.rtk_data[i].ident,
			      CODE(rtcm->rtcmtypes.rtcm3_1003.rtk_data[i].L1.indicator),
			      rtcm->rtcmtypes.rtcm3_1001.rtk_data[i].L1.pseudorange,
			      rtcm->rtcmtypes.rtcm3_1001.rtk_data[i].L1.rangediff,
			      INT(rtcm->rtcmtypes.rtcm3_1001.rtk_data[i].L1.locktime));
	    }		
	    break;

	case 1002:
	    (void)fprintf(fp, 
			  "  #station_id=%u, tow=%d sync=%c smoothing=%c interval=%u satcount=%u", 
			  rtcm->rtcmtypes.rtcm3_1002.header.station_id,
			  (int)rtcm->rtcmtypes.rtcm3_1002.header.tow,
			  BOOL(rtcm->rtcmtypes.rtcm3_1002.header.sync),
			  BOOL(rtcm->rtcmtypes.rtcm3_1002.header.smoothing),
			  rtcm->rtcmtypes.rtcm3_1002.header.interval,
			  rtcm->rtcmtypes.rtcm3_1002.header.satcount);	    
	    for (i = 0; i < rtcm->rtcmtypes.rtcm3_1002.header.satcount; i++) {
		(void)fprintf(fp, 
			      "    ident=%u\n      L1: ind=%u prange=%8.1f delta=%6.4f lockt=%u amb=%u CNR=%.2f\n",
			      rtcm->rtcmtypes.rtcm3_1002.rtk_data[i].ident,
			      CODE(rtcm->rtcmtypes.rtcm3_1003.rtk_data[i].L1.indicator),
			      rtcm->rtcmtypes.rtcm3_1002.rtk_data[i].L1.pseudorange,
			      rtcm->rtcmtypes.rtcm3_1002.rtk_data[i].L1.rangediff,
			      INT(rtcm->rtcmtypes.rtcm3_1002.rtk_data[i].L1.locktime),
			      INT(rtcm->rtcmtypes.rtcm3_1002.rtk_data[i].L1.ambiguity),
			      rtcm->rtcmtypes.rtcm3_1002.rtk_data[i].L1.CNR);
	    }		
	    break;

	case 1003:
	    (void)fprintf(fp,
			  "  #station_id=%u, tow=%d sync=%c smoothing=%c interval=%u satcount=%u", 
			  rtcm->rtcmtypes.rtcm3_1003.header.station_id,
			  (int)rtcm->rtcmtypes.rtcm3_1003.header.tow,
			  BOOL(rtcm->rtcmtypes.rtcm3_1003.header.sync),
			  BOOL(rtcm->rtcmtypes.rtcm3_1003.header.smoothing),
			  rtcm->rtcmtypes.rtcm3_1003.header.interval,
			  rtcm->rtcmtypes.rtcm3_1003.header.satcount);	    
	    for (i = 0; i < rtcm->rtcmtypes.rtcm3_1003.header.satcount; i++) {
		(void)fprintf(fp, 
			      "    ident=%u\n      L1: ind=%u prange=%8.1f delta=%6.4f lockt=%u\n      L2: ind=%u prange=%8.1f delta=%6.4f lockt=%u\n", 
			      rtcm->rtcmtypes.rtcm3_1003.rtk_data[i].ident,
			      CODE(rtcm->rtcmtypes.rtcm3_1003.rtk_data[i].L1.indicator),
			      rtcm->rtcmtypes.rtcm3_1003.rtk_data[i].L1.pseudorange,
			      rtcm->rtcmtypes.rtcm3_1003.rtk_data[i].L1.rangediff,
			      INT(rtcm->rtcmtypes.rtcm3_1003.rtk_data[i].L1.locktime),
			      CODE(rtcm->rtcmtypes.rtcm3_1003.rtk_data[i].L2.indicator),
			      rtcm->rtcmtypes.rtcm3_1003.rtk_data[i].L2.pseudorange,
			      rtcm->rtcmtypes.rtcm3_1003.rtk_data[i].L2.rangediff,
			      INT(rtcm->rtcmtypes.rtcm3_1003.rtk_data[i].L2.locktime));
	    }		
	    break;

	case 1004:
	    (void)fprintf(fp, 
			  "  #station_id=%u, tow=%d sync=%c smoothing=%c interval=%u satcount=%u\n", 
			  rtcm->rtcmtypes.rtcm3_1004.header.station_id,
			  (int)rtcm->rtcmtypes.rtcm3_1004.header.tow,
			  BOOL(rtcm->rtcmtypes.rtcm3_1004.header.sync),
			  BOOL(rtcm->rtcmtypes.rtcm3_1004.header.smoothing),
			  rtcm->rtcmtypes.rtcm3_1004.header.interval,
			  rtcm->rtcmtypes.rtcm3_1004.header.satcount);
	    for (i = 0; i < rtcm->rtcmtypes.rtcm3_1004.header.satcount; i++) {
		(void)fprintf(fp, 
			      "    ident=%u\n      L1: ind=%u prange=%8.1f delta=%6.4f lockt=%u amb=%u CNR=%.2f\n      L2: ind=%u prange=%8.1f delta=%6.4f lockt=%u amb=%u CNR=%.2f\n", 
			      rtcm->rtcmtypes.rtcm3_1004.rtk_data[i].ident,
			      CODE(rtcm->rtcmtypes.rtcm3_1004.rtk_data[i].L1.indicator),
			      rtcm->rtcmtypes.rtcm3_1004.rtk_data[i].L1.pseudorange,
			      rtcm->rtcmtypes.rtcm3_1004.rtk_data[i].L1.rangediff,
			      INT(rtcm->rtcmtypes.rtcm3_1004.rtk_data[i].L1.locktime),
			      INT(rtcm->rtcmtypes.rtcm3_1002.rtk_data[i].L1.ambiguity),
			      rtcm->rtcmtypes.rtcm3_1002.rtk_data[i].L1.CNR,
			      CODE(rtcm->rtcmtypes.rtcm3_1004.rtk_data[i].L2.indicator),
			      rtcm->rtcmtypes.rtcm3_1004.rtk_data[i].L2.pseudorange,
			      rtcm->rtcmtypes.rtcm3_1004.rtk_data[i].L2.rangediff,
			      INT(rtcm->rtcmtypes.rtcm3_1004.rtk_data[i].L2.locktime),
			      INT(rtcm->rtcmtypes.rtcm3_1004.rtk_data[i].L2.ambiguity),
			      rtcm->rtcmtypes.rtcm3_1004.rtk_data[i].L2.CNR);
	    }		
	    break;

	case 1005:
	    (void)fprintf(fp, 
			  "  station_id=%u, %s refstation=%c sro=%c x=%f y=%f z=%f\n", 
			  rtcm->rtcmtypes.rtcm3_1005.station_id,
			  systems[rtcm->rtcmtypes.rtcm3_1005.system],
			  BOOL(rtcm->rtcmtypes.rtcm3_1005.reference_station),
			  BOOL(rtcm->rtcmtypes.rtcm3_1005.single_receiver),
			  rtcm->rtcmtypes.rtcm3_1005.ecef_x,
			  rtcm->rtcmtypes.rtcm3_1005.ecef_y,
			  rtcm->rtcmtypes.rtcm3_1005.ecef_z);
	    break;

	case 1006:
	    (void)fprintf(fp, 
			  "  station_id=%u, %s refstation=%c sro=%c x=%f y=%f z=%f a=%f\n", 
			  rtcm->rtcmtypes.rtcm3_1006.station_id,
			  systems[rtcm->rtcmtypes.rtcm3_1006.system],
			  BOOL(rtcm->rtcmtypes.rtcm3_1006.reference_station),
			  BOOL(rtcm->rtcmtypes.rtcm3_1006.single_receiver),
			  rtcm->rtcmtypes.rtcm3_1006.ecef_x,
			  rtcm->rtcmtypes.rtcm3_1006.ecef_y,
			  rtcm->rtcmtypes.rtcm3_1006.ecef_z,
			  rtcm->rtcmtypes.rtcm3_1006.height);
	    break;

	case 1007:
	    (void)fprintf(fp, 
			  "  station_id=%u, desc=%s setup-id=%u\n",
			  rtcm->rtcmtypes.rtcm3_1007.station_id,
			  rtcm->rtcmtypes.rtcm3_1007.descriptor,
			  INT(rtcm->rtcmtypes.rtcm3_1007.setup_id));
	    break;

	case 1008:
	    (void)fprintf(fp, 
			  "  station_id=%u, desc=%s setup-id=%u serial=%s\n",
			  rtcm->rtcmtypes.rtcm3_1008.station_id,
			  rtcm->rtcmtypes.rtcm3_1008.descriptor,
			  INT(rtcm->rtcmtypes.rtcm3_1008.setup_id),
			  rtcm->rtcmtypes.rtcm3_1008.serial);
	    break;

	case 1009:
	    (void)fprintf(fp, 
			  "  #station_id=%u, tow=%d sync=%c smooting=%c interval=%u satcount=%u", 
			  rtcm->rtcmtypes.rtcm3_1009.header.station_id,
			  (int)rtcm->rtcmtypes.rtcm3_1009.header.tow,
			  BOOL(rtcm->rtcmtypes.rtcm3_1009.header.sync),
			  BOOL(rtcm->rtcmtypes.rtcm3_1009.header.smoothing),
			  rtcm->rtcmtypes.rtcm3_1009.header.interval,
			  rtcm->rtcmtypes.rtcm3_1009.header.satcount);
	    for (i = 0; i < rtcm->rtcmtypes.rtcm3_1009.header.satcount; i++) {
		(void)fprintf(fp, 
			      "    ident=%u\n      L1: ind=%u channel=%u prange=%8.1f delta=%6.4f lockt=%u\n", 
			      rtcm->rtcmtypes.rtcm3_1009.rtk_data[i].ident,
			      CODE(rtcm->rtcmtypes.rtcm3_1003.rtk_data[i].L1.indicator),
			      INT(rtcm->rtcmtypes.rtcm3_1009.rtk_data[i].L1.channel),
			      rtcm->rtcmtypes.rtcm3_1009.rtk_data[i].L1.pseudorange,
			      rtcm->rtcmtypes.rtcm3_1009.rtk_data[i].L1.rangediff,
			      INT(rtcm->rtcmtypes.rtcm3_1009.rtk_data[i].L1.locktime));
	    }		
	    break;

	case 1010:
	    (void)fprintf(fp, 
			  "  #station_id=%u, tow=%d sync=%c smooting=%c interval=%u satcount=%u", 
			  rtcm->rtcmtypes.rtcm3_1010.header.station_id,
			  (int)rtcm->rtcmtypes.rtcm3_1010.header.tow,
			  BOOL(rtcm->rtcmtypes.rtcm3_1010.header.sync),
			  BOOL(rtcm->rtcmtypes.rtcm3_1010.header.smoothing),
			  rtcm->rtcmtypes.rtcm3_1010.header.interval,
			  rtcm->rtcmtypes.rtcm3_1010.header.satcount);
	    for (i = 0; i < rtcm->rtcmtypes.rtcm3_1010.header.satcount; i++) {
		(void)fprintf(fp, 
			      "    ident=%u\n      L1: ind=%u channel=%u prange=%8.1f delta=%6.4f lockt=%u amb=%u CNR=%.2f\n", 
			      rtcm->rtcmtypes.rtcm3_1010.rtk_data[i].ident,
			      CODE(rtcm->rtcmtypes.rtcm3_1003.rtk_data[i].L1.indicator),
			      INT(rtcm->rtcmtypes.rtcm3_1010.rtk_data[i].L1.channel),
			      rtcm->rtcmtypes.rtcm3_1010.rtk_data[i].L1.pseudorange,
			      rtcm->rtcmtypes.rtcm3_1010.rtk_data[i].L1.rangediff,
			      INT(rtcm->rtcmtypes.rtcm3_1010.rtk_data[i].L1.locktime),
			      INT(rtcm->rtcmtypes.rtcm3_1010.rtk_data[i].L1.ambiguity),
			      rtcm->rtcmtypes.rtcm3_1010.rtk_data[i].L1.CNR);
	    }		
	    break;

	case 1011:
	    (void)fprintf(fp, 
			  "  #station_id=%u, tow=%d sync=%c smooting=%c interval=%u satcount=%u", 
			  rtcm->rtcmtypes.rtcm3_1011.header.station_id,
			  (int)rtcm->rtcmtypes.rtcm3_1011.header.tow,
			  BOOL(rtcm->rtcmtypes.rtcm3_1011.header.sync),
			  BOOL(rtcm->rtcmtypes.rtcm3_1011.header.smoothing),
			  rtcm->rtcmtypes.rtcm3_1011.header.interval,
			  rtcm->rtcmtypes.rtcm3_1011.header.satcount);
	    for (i = 0; i < rtcm->rtcmtypes.rtcm3_1011.header.satcount; i++) {
		(void)fprintf(fp, 
			      "    ident=%u\n      L1: ind=%u channel=%u prange=%8.1f delta=%6.4f lockt=%u\n      L2: ind=%u prange=%8.1f delta=%6.4f lockt=%u\n", 
			      rtcm->rtcmtypes.rtcm3_1011.rtk_data[i].ident,
			      CODE(rtcm->rtcmtypes.rtcm3_1011.rtk_data[i].L1.indicator),
			      INT(rtcm->rtcmtypes.rtcm3_1011.rtk_data[i].L1.channel),
			      rtcm->rtcmtypes.rtcm3_1011.rtk_data[i].L1.pseudorange,
			      rtcm->rtcmtypes.rtcm3_1011.rtk_data[i].L1.rangediff,
			      INT(rtcm->rtcmtypes.rtcm3_1011.rtk_data[i].L1.locktime),
			      CODE(rtcm->rtcmtypes.rtcm3_1011.rtk_data[i].L2.indicator),
			      rtcm->rtcmtypes.rtcm3_1011.rtk_data[i].L2.pseudorange,
			      rtcm->rtcmtypes.rtcm3_1011.rtk_data[i].L2.rangediff,
			      INT(rtcm->rtcmtypes.rtcm3_1011.rtk_data[i].L2.locktime));
	    }
	    break;

	case 1012:
	    (void)fprintf(fp, 
			  "  #station_id=%u, tow=%d sync=%c smooting=%c interval=%u satcount=%u", 
			  rtcm->rtcmtypes.rtcm3_1012.header.station_id,
			  (int)rtcm->rtcmtypes.rtcm3_1012.header.tow,
			  BOOL(rtcm->rtcmtypes.rtcm3_1012.header.sync),
			  BOOL(rtcm->rtcmtypes.rtcm3_1012.header.smoothing),
			  rtcm->rtcmtypes.rtcm3_1012.header.interval,
			  rtcm->rtcmtypes.rtcm3_1012.header.satcount);
	    for (i = 0; i < rtcm->rtcmtypes.rtcm3_1012.header.satcount; i++) {
		(void)fprintf(fp, 
			      "    ident=%u\n      L1: ind=%u channel=%u prange=%8.1f delta=%6.4f lockt=%u amb=%u CNR=%.2f\n      L2: ind=%u prange=%8.1f delta=%6.4f lockt=%u amb=%u CNR=%.2f\n", 
			      rtcm->rtcmtypes.rtcm3_1012.rtk_data[i].ident,
			      CODE(rtcm->rtcmtypes.rtcm3_1012.rtk_data[i].L1.indicator),
			      INT(rtcm->rtcmtypes.rtcm3_1012.rtk_data[i].L1.channel),
			      rtcm->rtcmtypes.rtcm3_1012.rtk_data[i].L1.pseudorange,
			      rtcm->rtcmtypes.rtcm3_1012.rtk_data[i].L1.rangediff,
			      INT(rtcm->rtcmtypes.rtcm3_1012.rtk_data[i].L1.locktime),
			      INT(rtcm->rtcmtypes.rtcm3_1012.rtk_data[i].L1.ambiguity),
			      rtcm->rtcmtypes.rtcm3_1012.rtk_data[i].L1.CNR,
			      CODE(rtcm->rtcmtypes.rtcm3_1012.rtk_data[i].L2.indicator),
			      rtcm->rtcmtypes.rtcm3_1012.rtk_data[i].L2.pseudorange,
			      rtcm->rtcmtypes.rtcm3_1012.rtk_data[i].L2.rangediff,
			      INT(rtcm->rtcmtypes.rtcm3_1012.rtk_data[i].L2.locktime),
			      INT(rtcm->rtcmtypes.rtcm3_1012.rtk_data[i].L2.ambiguity),
			      rtcm->rtcmtypes.rtcm3_1012.rtk_data[i].L2.CNR);
	    }
	    break;

	case 1013:
	    (void)fprintf(fp,
			  "  station_id=%u, mjd=%u sec=%u leapsecs=%u ncount=%u\n",
			  rtcm->rtcmtypes.rtcm3_1013.station_id,
			  rtcm->rtcmtypes.rtcm3_1013.mjd,
			  rtcm->rtcmtypes.rtcm3_1013.sod,
			  INT(rtcm->rtcmtypes.rtcm3_1013.leapsecs),
			  INT(rtcm->rtcmtypes.rtcm3_1013.ncount));
	for (i = 0; i < rtcm->rtcmtypes.rtcm3_1013.ncount; i++)
	    (void)fprintf(fp,
			  "    id=%u sync=%c interval=%u\n",
			  rtcm->rtcmtypes.rtcm3_1013.announcements[i].id,
			  BOOL(rtcm->rtcmtypes.rtcm3_1013.announcements[i].sync),
			  rtcm->rtcmtypes.rtcm3_1013.announcements[i].interval);
	    break;

	case 1014:
	    (void)fprintf(fp,
			  "    netid=%u subnetid=%u statcount=%u master=%u aux=%u lat=%f, lon=%f, alt=%f\n",
			  rtcm->rtcmtypes.rtcm3_1014.network_id,
			  rtcm->rtcmtypes.rtcm3_1014.subnetwork_id,
			  (uint)rtcm->rtcmtypes.rtcm3_1014.stationcount,
			  rtcm->rtcmtypes.rtcm3_1014.master_id,
			  rtcm->rtcmtypes.rtcm3_1014.aux_id,
			  rtcm->rtcmtypes.rtcm3_1014.d_lat,
			  rtcm->rtcmtypes.rtcm3_1014.d_lon,
			  rtcm->rtcmtypes.rtcm3_1014.d_alt);
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
	    (void)fprintf(fp,
			  "  station_id=%u, mjd=%u sec=%u len=%u units=%u msg=%s\n",
			  rtcm->rtcmtypes.rtcm3_1029.station_id,
			  rtcm->rtcmtypes.rtcm3_1029.mjd,
			  rtcm->rtcmtypes.rtcm3_1029.sod,
			  INT(rtcm->rtcmtypes.rtcm3_1029.len),
			  INT(rtcm->rtcmtypes.rtcm3_1029.unicode_units),
			  (char *)rtcm->rtcmtypes.rtcm3_1029.text);
	    break;

	default:
	    (void)fprintf(fp, "    Unknown content\n"); 
	    break;
    }
#undef CODE
#undef BOOL
#undef INT
}

/*@ +type @*/

#endif /* RTCM104V3_ENABLE */
