/*****************************************************************************

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
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
#include <arpa/inet.h>	/* for ntohl(3) and friends */

#include "gpsd_config.h"
#include "gpsd.h"
#include "bits.h"

#ifdef RTCM104V3_ENABLE

/* scaling constants for RTCM3 real number types */
#define PSEUDORANGE_RESOLUTION		0.2	/* DF011 */
#define PSEUDORANGE_DIFF_RESOLUTION	0.0005	/* DF012 */
#define CARRIER_NOISE_RATIO_UNITS	0.25	/* DF015 */
#define ANTENNA_HEIGHT_RESOLUTION	0.0001	/* DF025-027 */
#define ANTENNA_DEGREE_RESOLUTION	25e-6	/* DF062 */
#define GPS_EPOCH_TIME_RESOLUTION	0.1	/* DF065 */
#define PHASE_CORRECTION_RESOLUTION	0.5	/* DF069-070 */

/* Other magic values */
#define INVALID_PSEUDORANGE		0x80000	/* DF012 */

static unsigned long long ufld(char buf[], unsigned int start, unsigned int width)
/* extract an bitfield from the buffer as an unsigned big-endian long */
{
    unsigned long long fld = 0;
    unsigned int i;;

    assert(width <= 64);
    for (i = 0; i < (width + 7) / 8; i++) {
	fld <<= 8;
	fld |= (unsigned char)buf[start / 8 + i];
    }
    //printf("Extracting %d:%d from %s: segment 0x%llx = %lld\n", start, width, gpsd_hexdump(buf, 12), fld, fld);

    fld &= (0xffffffff >> (start % 8));
    //printf("After masking: 0x%llx = %lld\n", fld, fld);
    fld >>= (start + width) % 8;
    
    return fld;
}

void rtcm3_unpack(/*@out@*/struct rtcm3_t *rtcm, char *buf)
/* break out the raw bits into the scaled report-structure fields */
{
    unsigned int bitcount = 0;

    /*@ -evalorder -sefparams @*/    
#define ugrab(width)	(bitcount += width, ufld(buf, bitcount-width, width))
    assert(ugrab(8) == 0xD3);
    assert(ugrab(6) == 0x00);

    rtcm->length = (uint)ugrab(10);
    rtcm->type = (uint)ugrab(12);

    switch(rtcm->type) {
	case 1001:
	    rtcm->rtcm3_1001.header.station_id = (uint)ugrab(12);
	    rtcm->rtcm3_1001.header.tow        = (time_t)ugrab(30);
	    rtcm->rtcm3_1001.header.sync       = (bool)ugrab(1);
	    rtcm->rtcm3_1001.header.satcount   = (ushort)ugrab(5);
	    rtcm->rtcm3_1001.header.smoothing  = (bool)ugrab(1);
	    rtcm->rtcm3_1001.header.interval   = (ushort)ugrab(3);
	    break;

	case 1002:
	    rtcm->rtcm3_1002.header.station_id = (uint)ugrab(12);
	    rtcm->rtcm3_1002.header.tow        = (time_t)ugrab(30);
	    rtcm->rtcm3_1002.header.sync       = (bool)ugrab(1);
	    rtcm->rtcm3_1002.header.satcount   = (ushort)ugrab(5);
	    rtcm->rtcm3_1002.header.smoothing  = (bool)ugrab(1);
	    rtcm->rtcm3_1002.header.interval   = (ushort)ugrab(3);
	    break;

	case 1003:
	    rtcm->rtcm3_1003.header.station_id = (uint)ugrab(12);
	    rtcm->rtcm3_1003.header.tow        = (time_t)ugrab(30);
	    rtcm->rtcm3_1003.header.sync       = (bool)ugrab(1);
	    rtcm->rtcm3_1003.header.satcount   = (ushort)ugrab(5);
	    rtcm->rtcm3_1003.header.smoothing  = (bool)ugrab(1);
	    rtcm->rtcm3_1003.header.interval   = (ushort)ugrab(3);
	    break;

	case 1004:
	    rtcm->rtcm3_1004.header.station_id = (uint)ugrab(12);
	    rtcm->rtcm3_1004.header.tow        = (time_t)ugrab(30);
	    rtcm->rtcm3_1004.header.sync       = (bool)ugrab(1);
	    rtcm->rtcm3_1004.header.satcount   = (ushort)ugrab(5);
	    rtcm->rtcm3_1004.header.smoothing  = (bool)ugrab(1);
	    rtcm->rtcm3_1004.header.interval   = (ushort)ugrab(3);
	    break;

	case 1005:
	    break;

	case 1006:
	    break;

	case 1007:
	    break;

	case 1008:
	    break;

	case 1009:
	    break;

	case 1010:
	    break;

	case 1011:
	    break;

	case 1012:
	    break;

	case 1013:
	    break;

	case 1014:
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
	    break;
    }
#undef ugrab
    /*@ +evalorder +sefparams @*/    
}

void rtcm3_dump(struct rtcm3_t *rtcm, /*@out@*/char buf[], size_t buflen)
/* dump the contents of a parsed RTCM104 message */
{
    size_t partlen;
    char partbuf[BUFSIZ];

    (void)snprintf(buf, buflen, "%u (%u):\n", rtcm->type, rtcm->length);
    partlen = strlen(buf); buf += partlen; buflen -= partlen;

#define CONC	partlen = strlen(partbuf); \
    		if (partlen < buflen) { \
		    (void)strcat(buf, partbuf); \
		    buf += partlen; \
		    buflen -= partlen; \
		}
    switch(rtcm->type) {
	case 1001:
	    break;

	case 1002:
	    break;

	case 1003:
	    break;

	case 1004:
	    break;

	case 1005:
	    break;

	case 1006:
	    break;

	case 1007:
	    break;

	case 1008:
	    break;

	case 1009:
	    break;

	case 1010:
	    break;

	case 1011:
	    break;

	case 1012:
	    break;

	case 1013:
	    break;

	case 1014:
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
	    (void)snprintf(partbuf, sizeof(partbuf), "    Unknown content\n"); 
	    CONC;
	    break;
    }
#undef CONC
}

#endif /* RTCM104V3_ENABLE */
