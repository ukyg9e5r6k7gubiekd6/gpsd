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
/* extract a bitfield from the buffer as an unsigned big-endian long */
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

static signed long long sfld(char buf[], unsigned int start, unsigned int width)
/* extract a bitfield from the buffer as a signed big-endian long */
{
    unsigned long long un = ufld(buf, start, width);
    signed long long fld;

    if (un & (1 << width))
	fld = -(un & ~(1 << width));
    else
	fld = (signed long long)un;
    
    return fld;
}

void rtcm3_unpack(/*@out@*/struct rtcm3_t *rtcm, char *buf)
/* break out the raw bits into the scaled report-structure fields */
{
    unsigned int bitcount = 0;
    unsigned i;
    signed long temp;

    /*@ -evalorder -sefparams @*/    
#define ugrab(width)	(bitcount += width, ufld(buf, bitcount-width, width))
#define sgrab(width)	(bitcount += width, sfld(buf, bitcount-width, width))
    assert(ugrab(8) == 0xD3);
    assert(ugrab(6) == 0x00);

    rtcm->length = (uint)ugrab(10);
    rtcm->type = (uint)ugrab(12);

    switch(rtcm->type) {
	case 1001:
	    rtcm->rtcm3_1001.header.msgnum     = (uint)ugrab(12);
	    rtcm->rtcm3_1001.header.station_id = (uint)ugrab(12);
	    rtcm->rtcm3_1001.header.tow        = (time_t)ugrab(30);
	    rtcm->rtcm3_1001.header.sync       = (bool)ugrab(1);
	    rtcm->rtcm3_1001.header.satcount   = (ushort)ugrab(5);
	    rtcm->rtcm3_1001.header.smoothing  = (bool)ugrab(1);
	    rtcm->rtcm3_1001.header.interval   = (ushort)ugrab(3);
	    for (i = 0; i < rtcm->rtcm3_1001.header.satcount; i++) {
		rtcm->rtcm3_1001.rtk_data[i].ident = (ushort)ugrab(6);
		rtcm->rtcm3_1001.rtk_data[i].L1.indicator = (unsigned char)ugrab(1);
		temp = (long)sgrab(24);
		if (temp == INVALID_PSEUDORANGE)
		    rtcm->rtcm3_1001.rtk_data[i].L1.pseudorange  = 0;
		else
		    rtcm->rtcm3_1001.rtk_data[i].L1.pseudorange  = temp * PSEUDORANGE_RESOLUTION;
		if (temp == INVALID_PSEUDORANGE)
		    rtcm->rtcm3_1001.rtk_data[i].L1.rangediff  = 0;
		else
		    rtcm->rtcm3_1001.rtk_data[i].L1.rangediff  = temp * PSEUDORANGE_DIFF_RESOLUTION;
		rtcm->rtcm3_1001.rtk_data[i].L1.locktime = (unsigned char)sgrab(7);
	    }
	    break;

	case 1002:
	    rtcm->rtcm3_1002.header.msgnum     = (uint)ugrab(12);
	    rtcm->rtcm3_1002.header.station_id = (uint)ugrab(12);
	    rtcm->rtcm3_1002.header.tow        = (time_t)ugrab(30);
	    rtcm->rtcm3_1002.header.sync       = (bool)ugrab(1);
	    rtcm->rtcm3_1002.header.satcount   = (ushort)ugrab(5);
	    rtcm->rtcm3_1002.header.smoothing  = (bool)ugrab(1);
	    rtcm->rtcm3_1002.header.interval   = (ushort)ugrab(3);
	    for (i = 0; i < rtcm->rtcm3_1001.header.satcount; i++) {
		rtcm->rtcm3_1002.rtk_data[i].ident = (ushort)ugrab(6);
		rtcm->rtcm3_1002.rtk_data[i].L1.indicator = (unsigned char)ugrab(1);
		temp = (long)sgrab(24);
		if (temp == INVALID_PSEUDORANGE)
		    rtcm->rtcm3_1002.rtk_data[i].L1.pseudorange  = 0;
		else
		    rtcm->rtcm3_1002.rtk_data[i].L1.pseudorange  = temp * PSEUDORANGE_RESOLUTION;
		if (temp == INVALID_PSEUDORANGE)
		    rtcm->rtcm3_1002.rtk_data[i].L1.rangediff  = 0;
		else
		    rtcm->rtcm3_1002.rtk_data[i].L1.rangediff  = temp * PSEUDORANGE_DIFF_RESOLUTION;
		rtcm->rtcm3_1002.rtk_data[i].L1.locktime = (unsigned char)sgrab(7);
		rtcm->rtcm3_1002.rtk_data[i].L1.ambiguity = (bool)ugrab(8);
		rtcm->rtcm3_1002.rtk_data[i].L1.CNR = (bool)ugrab(8) * CARRIER_NOISE_RATIO_UNITS;
	    }
	    break;

	case 1003:
	    rtcm->rtcm3_1003.header.msgnum     = (uint)ugrab(12);
	    rtcm->rtcm3_1003.header.station_id = (uint)ugrab(12);
	    rtcm->rtcm3_1003.header.tow        = (time_t)ugrab(30);
	    rtcm->rtcm3_1003.header.sync       = (bool)ugrab(1);
	    rtcm->rtcm3_1003.header.satcount   = (ushort)ugrab(5);
	    rtcm->rtcm3_1003.header.smoothing  = (bool)ugrab(1);
	    rtcm->rtcm3_1003.header.interval   = (ushort)ugrab(3);
	    for (i = 0; i < rtcm->rtcm3_1001.header.satcount; i++) {
		rtcm->rtcm3_1003.rtk_data[i].ident = (ushort)ugrab(6);
		rtcm->rtcm3_1003.rtk_data[i].L1.indicator = (unsigned char)ugrab(1);
		temp = (long)sgrab(24);
		if (temp == INVALID_PSEUDORANGE)
		    rtcm->rtcm3_1003.rtk_data[i].L1.pseudorange  = 0;
		else
		    rtcm->rtcm3_1003.rtk_data[i].L1.pseudorange  = temp * PSEUDORANGE_RESOLUTION;
		if (temp == INVALID_PSEUDORANGE)
		    rtcm->rtcm3_1003.rtk_data[i].L1.rangediff  = 0;
		else
		    rtcm->rtcm3_1003.rtk_data[i].L1.rangediff  = temp * PSEUDORANGE_DIFF_RESOLUTION;
		rtcm->rtcm3_1003.rtk_data[i].L1.locktime = (unsigned char)sgrab(7);
		rtcm->rtcm3_1003.rtk_data[i].L2.indicator = (unsigned char)ugrab(2);
		temp = (long)sgrab(24);
		if (temp == INVALID_PSEUDORANGE)
		    rtcm->rtcm3_1003.rtk_data[i].L2.pseudorange  = 0;
		else
		    rtcm->rtcm3_1003.rtk_data[i].L2.pseudorange  = temp * PSEUDORANGE_RESOLUTION;
		if (temp == INVALID_PSEUDORANGE)
		    rtcm->rtcm3_1003.rtk_data[i].L2.rangediff  = 0;
		else
		    rtcm->rtcm3_1003.rtk_data[i].L2.rangediff  = temp * PSEUDORANGE_DIFF_RESOLUTION;
		rtcm->rtcm3_1003.rtk_data[i].L2.locktime = (unsigned char)sgrab(7);
	    }
	    break;

	case 1004:
	    rtcm->rtcm3_1004.header.msgnum     = (uint)ugrab(12);
	    rtcm->rtcm3_1004.header.station_id = (uint)ugrab(12);
	    rtcm->rtcm3_1004.header.tow        = (time_t)ugrab(30);
	    rtcm->rtcm3_1004.header.sync       = (bool)ugrab(1);
	    rtcm->rtcm3_1004.header.satcount   = (ushort)ugrab(5);
	    rtcm->rtcm3_1004.header.smoothing  = (bool)ugrab(1);
	    rtcm->rtcm3_1004.header.interval   = (ushort)ugrab(3);
	    for (i = 0; i < rtcm->rtcm3_1001.header.satcount; i++) {
		rtcm->rtcm3_1004.rtk_data[i].ident = (ushort)ugrab(6);
		rtcm->rtcm3_1004.rtk_data[i].L1.indicator = (bool)ugrab(1);
		temp = (long)sgrab(24);
		if (temp == INVALID_PSEUDORANGE)
		    rtcm->rtcm3_1004.rtk_data[i].L1.pseudorange  = 0;
		else
		    rtcm->rtcm3_1004.rtk_data[i].L1.pseudorange  = temp * PSEUDORANGE_RESOLUTION;
		if (temp == INVALID_PSEUDORANGE)
		    rtcm->rtcm3_1004.rtk_data[i].L1.rangediff  = 0;
		else
		    rtcm->rtcm3_1004.rtk_data[i].L1.rangediff  = temp * PSEUDORANGE_DIFF_RESOLUTION;
		rtcm->rtcm3_1004.rtk_data[i].L1.locktime = (unsigned char)sgrab(7);
		rtcm->rtcm3_1004.rtk_data[i].L1.ambiguity = (bool)ugrab(8);
		rtcm->rtcm3_1004.rtk_data[i].L1.CNR = (bool)ugrab(8) * CARRIER_NOISE_RATIO_UNITS;
		rtcm->rtcm3_1004.rtk_data[i].L2.indicator = (unsigned char)ugrab(2);
		temp = (long)sgrab(24);
		if (temp == INVALID_PSEUDORANGE)
		    rtcm->rtcm3_1004.rtk_data[i].L2.pseudorange  = 0;
		else
		    rtcm->rtcm3_1004.rtk_data[i].L2.pseudorange  = temp * PSEUDORANGE_RESOLUTION;
		if (temp == INVALID_PSEUDORANGE)
		    rtcm->rtcm3_1004.rtk_data[i].L2.rangediff  = 0;
		else
		    rtcm->rtcm3_1004.rtk_data[i].L2.rangediff  = temp * PSEUDORANGE_DIFF_RESOLUTION;
		rtcm->rtcm3_1004.rtk_data[i].L2.locktime = (unsigned char)sgrab(7);
		rtcm->rtcm3_1004.rtk_data[i].L2.ambiguity = (bool)ugrab(8);
		rtcm->rtcm3_1004.rtk_data[i].L2.CNR = (bool)ugrab(8) * CARRIER_NOISE_RATIO_UNITS;
	    }
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
#undef sgrab
#undef ugrab
    /*@ +evalorder +sefparams @*/    
}

void rtcm3_dump(struct rtcm3_t *rtcm, FILE *fp)
/* dump the contents of a parsed RTCM104 message */
{
    int i;

    (void)fprintf(fp, "%u (%u):\n", rtcm->type, rtcm->length);

#define BOOL(c)	(c!=0 ? 't' : 'f')
#define CODE(x) (x)
    switch(rtcm->type) {
	case 1001:
	    (void)fprintf(fp, 
			  "  #%d station_id=%d, tow=%d sync=%c smoothing=%c interval=%d satcount=%d", 
			  rtcm->rtcm3_1001.header.msgnum,
			  rtcm->rtcm3_1001.header.station_id,
			  (int)rtcm->rtcm3_1001.header.tow,
			  BOOL(rtcm->rtcm3_1001.header.sync),
			  BOOL(rtcm->rtcm3_1001.header.smoothing),
			  rtcm->rtcm3_1001.header.interval,
			  rtcm->rtcm3_1001.header.satcount);
	    for (i = 0; i < rtcm->rtcm3_1001.header.satcount; i++) {
		(void)fprintf(fp, 
			      "    ident=%d\n      L1: ind=%d prange=%8.1f delta=%6.4f lockt=%d\n", 
			      rtcm->rtcm3_1001.rtk_data[i].ident,
			      CODE(rtcm->rtcm3_1003.rtk_data[i].L1.indicator),
			      rtcm->rtcm3_1001.rtk_data[i].L1.pseudorange,
			      rtcm->rtcm3_1001.rtk_data[i].L1.rangediff,
			      rtcm->rtcm3_1001.rtk_data[i].L1.locktime);
	    }		
	    break;

	case 1002:
	    (void)fprintf(fp, 
			  "  #%d station_id=%d, tow=%d sync=%c smoothing=%c interval=%d satcount=%d", 
			  rtcm->rtcm3_1002.header.msgnum,
			  rtcm->rtcm3_1002.header.station_id,
			  (int)rtcm->rtcm3_1002.header.tow,
			  BOOL(rtcm->rtcm3_1002.header.sync),
			  BOOL(rtcm->rtcm3_1002.header.smoothing),
			  rtcm->rtcm3_1002.header.interval,
			  rtcm->rtcm3_1002.header.satcount);	    
	    for (i = 0; i < rtcm->rtcm3_1002.header.satcount; i++) {
		(void)fprintf(fp, 
			      "    ident=%d\n      L1: ind=%d prange=%8.1f delta=%6.4f lockt=%d amb=%d CNR=%.2f\n",
			      rtcm->rtcm3_1002.rtk_data[i].ident,
			      CODE(rtcm->rtcm3_1003.rtk_data[i].L1.indicator),
			      rtcm->rtcm3_1002.rtk_data[i].L1.pseudorange,
			      rtcm->rtcm3_1002.rtk_data[i].L1.rangediff,
			      rtcm->rtcm3_1002.rtk_data[i].L1.locktime,
			      rtcm->rtcm3_1002.rtk_data[i].L1.ambiguity,
			      rtcm->rtcm3_1002.rtk_data[i].L1.CNR);
	    }		
	    break;

	case 1003:
	    (void)fprintf(fp,
			  "  #%d station_id=%d, tow=%d sync=%c smoothing=%c interval=%d satcount=%d", 
			  rtcm->rtcm3_1003.header.msgnum,
			  rtcm->rtcm3_1003.header.station_id,
			  (int)rtcm->rtcm3_1003.header.tow,
			  BOOL(rtcm->rtcm3_1003.header.sync),
			  BOOL(rtcm->rtcm3_1003.header.smoothing),
			  rtcm->rtcm3_1003.header.interval,
			  rtcm->rtcm3_1003.header.satcount);	    
	    for (i = 0; i < rtcm->rtcm3_1003.header.satcount; i++) {
		(void)fprintf(fp, 
			      "    ident=%d\n      L1: ind=%d prange=%8.1f delta=%6.4f lockt=%d\n      L2: ind=%d prange=%8.1f delta=%6.4f lockt=%d\n", 
			      rtcm->rtcm3_1003.rtk_data[i].ident,
			      CODE(rtcm->rtcm3_1003.rtk_data[i].L1.indicator),
			      rtcm->rtcm3_1003.rtk_data[i].L1.pseudorange,
			      rtcm->rtcm3_1003.rtk_data[i].L1.rangediff,
			      rtcm->rtcm3_1003.rtk_data[i].L1.locktime,
			      CODE(rtcm->rtcm3_1003.rtk_data[i].L2.indicator),
			      rtcm->rtcm3_1003.rtk_data[i].L2.pseudorange,
			      rtcm->rtcm3_1003.rtk_data[i].L2.rangediff,
			      rtcm->rtcm3_1003.rtk_data[i].L2.locktime);
	    }		
	    break;

	case 1004:
	    (void)fprintf(fp, 
			  "  #%d station_id=%d, tow=%d sync=%c smoothing=%c interval=%d satcount=%d\n", 
			  rtcm->rtcm3_1004.header.msgnum,
			  rtcm->rtcm3_1004.header.station_id,
			  (int)rtcm->rtcm3_1004.header.tow,
			  BOOL(rtcm->rtcm3_1004.header.sync),
			  BOOL(rtcm->rtcm3_1004.header.smoothing),
			  rtcm->rtcm3_1004.header.interval,
			  rtcm->rtcm3_1004.header.satcount);
	    for (i = 0; i < rtcm->rtcm3_1004.header.satcount; i++) {
		(void)fprintf(fp, 
			      "    ident=%d\n      L1: ind=%d prange=%8.1f delta=%6.4f lockt=%d amb=%d CNR=%.2f\n      L2: ind=%d prange=%8.1f delta=%6.4f lockt=%d amb=%d CNR=%.2f\n", 
			      rtcm->rtcm3_1004.rtk_data[i].ident,
			      CODE(rtcm->rtcm3_1004.rtk_data[i].L1.indicator),
			      rtcm->rtcm3_1004.rtk_data[i].L1.pseudorange,
			      rtcm->rtcm3_1004.rtk_data[i].L1.rangediff,
			      rtcm->rtcm3_1004.rtk_data[i].L1.locktime,
			      rtcm->rtcm3_1002.rtk_data[i].L1.ambiguity,
			      rtcm->rtcm3_1002.rtk_data[i].L1.CNR,
			      CODE(rtcm->rtcm3_1004.rtk_data[i].L2.indicator),
			      rtcm->rtcm3_1004.rtk_data[i].L2.pseudorange,
			      rtcm->rtcm3_1004.rtk_data[i].L2.rangediff,
			      rtcm->rtcm3_1004.rtk_data[i].L2.locktime,
			      rtcm->rtcm3_1004.rtk_data[i].L2.ambiguity,
			      rtcm->rtcm3_1004.rtk_data[i].L2.CNR);
	    }		
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
	    (void)fprintf(fp, "    Unknown content\n"); 
	    break;
    }
#undef CODE
#undef BOOL
}

#endif /* RTCM104V3_ENABLE */
