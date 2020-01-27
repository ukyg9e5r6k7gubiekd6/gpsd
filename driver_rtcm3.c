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

Decodes of the following types have been verified: 1004, 1005, 1006,
1008, 1012, 1013, 1029. There is good reason to believe the 1007 code
is correct, as it's identical to 1008 up to where it ends.

The 1033 decode was arrived at by looking at an rtcminspect dump and noting
that it carries an information superset of the 1008.  There are additional
Receiver and Firmware fields we're not certain to decode without access
to an RTCM3 standard at revision 4 or later, but the guess in the code
has been observed to correctly analyze a message with a nonempty Receiver
field.

This file is Copyright (c) 2010-2018 by the GPSD project
SPDX-License-Identifier: BSD-2-clause

*****************************************************************************/

#include "gpsd_config.h"  /* must be before all includes */

#include <string.h>

#include "gpsd.h"
#include "bits.h"

#ifdef RTCM104V3_ENABLE

/* scaling constants for RTCM3 real number types */
#define GPS_PSEUDORANGE_RESOLUTION      0.02    /* DF011 */
#define PSEUDORANGE_DIFF_RESOLUTION     0.0005  /* DF012,DF042 */
#define CARRIER_NOISE_RATIO_UNITS       0.25    /* DF015, DF045, DF50 */
#define ANTENNA_POSITION_RESOLUTION     0.0001  /* DF025-027 */
#define GLONASS_PSEUDORANGE_RESOLUTION  0.02    /* DF041 */
#define ANTENNA_DEGREE_RESOLUTION       25e-6   /* DF062 */
#define GPS_EPOCH_TIME_RESOLUTION       0.1     /* DF065 */
#define PHASE_CORRECTION_RESOLUTION     0.5     /* DF069-070 */


/* Other magic values */
#define GPS_INVALID_PSEUDORANGE         0x80000 /* DF012, DF018 */
#define GLONASS_INVALID_RANGEINCR       0x2000  /* DF047 */
#define GLONASS_CHANNEL_BASE            7       /* DF040 */

/* Large case statements make GNU indent very confused */
/* *INDENT-OFF* */

/* good source on message types:
 * https://software.rtcm-ntrip.org/export/HEAD/ntrip/trunk/BNC/src/bnchelp.html
 * Also look in the BNC source
 * and look at the tklib source: http://www.rtklib.com/
 */
void rtcm3_unpack(const struct gps_context_t *context,
                  struct rtcm3_t *rtcm, char *buf)
/* break out the raw bits into the scaled report-structure fields */
{
    unsigned int n, n2, n3, n4;
    int bitcount = 0;
    unsigned int i;
    signed long temp;
    bool unknown = true;              // we don't know how to decode
    const char *unknown_name = NULL;  // no decode, but maybe we know the name

#define ugrab(width)    (bitcount += width, ubits((unsigned char *)buf, \
                         bitcount-width, width, false))
#define sgrab(width)    (bitcount += width, sbits((signed char *)buf,  \
                         bitcount-width, width, false))
#define GPS_PSEUDORANGE(fld, len) \
    {temp = (unsigned long)ugrab(len);          \
    if (temp == GPS_INVALID_PSEUDORANGE)        \
        fld.pseudorange = 0;                    \
    else                                        \
        fld.pseudorange = temp * GPS_PSEUDORANGE_RESOLUTION;}
#define RANGEDIFF(fld, len) \
    temp = (long)sgrab(len);                    \
    if (temp == GPS_INVALID_PSEUDORANGE)        \
        fld.rangediff = 0;                      \
    else                                        \
        fld.rangediff = temp * PSEUDORANGE_DIFF_RESOLUTION;

    memset(rtcm, 0, sizeof(struct rtcm3_t));
    //assert(ugrab(8) == 0xD3);
    //assert(ugrab(6) == 0x00);
    ugrab(14);

    rtcm->length = (unsigned int)ugrab(10);
    rtcm->type = (unsigned int)ugrab(12);

    GPSD_LOG(LOG_RAW, &context->errout, "RTCM3: type %d payload length %d\n",
             rtcm->type, rtcm->length);

    // RTCM3 message type numbers start at 1001
    switch (rtcm->type) {
    case 1001:
        /* GPS Basic RTK, L1 Only */
        rtcm->rtcmtypes.rtcm3_1001.header.station_id = (unsigned int)ugrab(12);
        rtcm->rtcmtypes.rtcm3_1001.header.tow = (time_t)ugrab(30);
        rtcm->rtcmtypes.rtcm3_1001.header.sync = (bool)ugrab(1);
        rtcm->rtcmtypes.rtcm3_1001.header.satcount = (unsigned short)ugrab(5);
        rtcm->rtcmtypes.rtcm3_1001.header.smoothing = (bool)ugrab(1);
        rtcm->rtcmtypes.rtcm3_1001.header.interval = (unsigned short)ugrab(3);
#define R1001 rtcm->rtcmtypes.rtcm3_1001.rtk_data[i]
        for (i = 0; i < rtcm->rtcmtypes.rtcm3_1001.header.satcount; i++) {
            R1001.ident = (unsigned short)ugrab(6);
            R1001.L1.indicator = (unsigned char)ugrab(1);
            GPS_PSEUDORANGE(R1001.L1, 24);
            RANGEDIFF(R1001.L1, 20);
            R1001.L1.locktime = (unsigned char)sgrab(7);
        }
#undef R1001
        unknown = false;
        break;

    case 1002:
        /* GPS Extended RTK, L1 Only */
        rtcm->rtcmtypes.rtcm3_1002.header.station_id = (unsigned int)ugrab(12);
        rtcm->rtcmtypes.rtcm3_1002.header.tow = (time_t)ugrab(30);
        rtcm->rtcmtypes.rtcm3_1002.header.sync = (bool)ugrab(1);
        rtcm->rtcmtypes.rtcm3_1002.header.satcount = (unsigned short)ugrab(5);
        rtcm->rtcmtypes.rtcm3_1002.header.smoothing = (bool)ugrab(1);
        rtcm->rtcmtypes.rtcm3_1002.header.interval = (unsigned short)ugrab(3);
#define R1002 rtcm->rtcmtypes.rtcm3_1002.rtk_data[i]
        for (i = 0; i < rtcm->rtcmtypes.rtcm3_1002.header.satcount; i++) {
            R1002.ident = (unsigned short)ugrab(6);
            R1002.L1.indicator = (unsigned char)ugrab(1);
            GPS_PSEUDORANGE(R1002.L1, 24);
            RANGEDIFF(R1002.L1, 20);
            R1002.L1.locktime = (unsigned char)sgrab(7);
            R1002.L1.ambiguity = (unsigned char)ugrab(8);
            R1002.L1.CNR = (ugrab(8)) * CARRIER_NOISE_RATIO_UNITS;
        }
#undef R1002
        unknown = false;
        break;

    case 1003:
        /* GPS Basic RTK, L1 & L2 */
        rtcm->rtcmtypes.rtcm3_1003.header.station_id = (unsigned int)ugrab(12);
        rtcm->rtcmtypes.rtcm3_1003.header.tow = (time_t)ugrab(30);
        rtcm->rtcmtypes.rtcm3_1003.header.sync = (bool)ugrab(1);
        rtcm->rtcmtypes.rtcm3_1003.header.satcount = (unsigned short)ugrab(5);
        rtcm->rtcmtypes.rtcm3_1003.header.smoothing = (bool)ugrab(1);
        rtcm->rtcmtypes.rtcm3_1003.header.interval = (unsigned short)ugrab(3);
#define R1003 rtcm->rtcmtypes.rtcm3_1003.rtk_data[i]
        for (i = 0; i < rtcm->rtcmtypes.rtcm3_1003.header.satcount; i++) {
            R1003.ident = (unsigned short)ugrab(6);
            R1003.L1.indicator =
                (unsigned char)ugrab(1);
            GPS_PSEUDORANGE(R1003.L1, 24);
            RANGEDIFF(R1003.L1, 20);
            R1003.L1.locktime = (unsigned char)sgrab(7);
            R1003.L2.indicator = (unsigned char)ugrab(2);
            GPS_PSEUDORANGE(R1003.L2, 24);
            temp = (long)sgrab(20);
            if (temp == GPS_INVALID_PSEUDORANGE)
                R1003.L2.rangediff = 0;
            else
                R1003.L2.rangediff = temp * PSEUDORANGE_DIFF_RESOLUTION;
            R1003.L2.locktime = (unsigned char)sgrab(7);
        }
#undef R1003
        unknown = false;
        break;

    case 1004:
        /* GPS Extended RTK, L1 & L2 */
        rtcm->rtcmtypes.rtcm3_1004.header.station_id = (unsigned int)ugrab(12);
        rtcm->rtcmtypes.rtcm3_1004.header.tow = (time_t)ugrab(30);
        rtcm->rtcmtypes.rtcm3_1004.header.sync = (bool)ugrab(1);
        rtcm->rtcmtypes.rtcm3_1004.header.satcount = (unsigned short)ugrab(5);
        rtcm->rtcmtypes.rtcm3_1004.header.smoothing = (bool)ugrab(1);
        rtcm->rtcmtypes.rtcm3_1004.header.interval = (unsigned short)ugrab(3);
#define R1004 rtcm->rtcmtypes.rtcm3_1004.rtk_data[i]
        for (i = 0; i < rtcm->rtcmtypes.rtcm3_1004.header.satcount; i++) {
            R1004.ident = (unsigned short)ugrab(6);
            R1004.L1.indicator = (bool)ugrab(1);
            GPS_PSEUDORANGE(R1004.L1, 24);
            RANGEDIFF(R1004.L1, 20);
            R1004.L1.locktime = (unsigned char)sgrab(7);
            R1004.L1.ambiguity = (unsigned char)ugrab(8);
            R1004.L1.CNR = ugrab(8) * CARRIER_NOISE_RATIO_UNITS;
            R1004.L2.indicator = (unsigned char)ugrab(2);
            GPS_PSEUDORANGE(R1004.L2, 14);
            RANGEDIFF(R1004.L2, 20);
            R1004.L2.locktime = (unsigned char)sgrab(7);
            R1004.L2.CNR = ugrab(8) * CARRIER_NOISE_RATIO_UNITS;
        }
#undef R1004
        unknown = false;
        break;

    case 1005:
        /* Stationary Antenna Reference Point, No Height Information
         * 19 bytes */
#define R1005 rtcm->rtcmtypes.rtcm3_1005
        R1005.station_id = (unsigned short)ugrab(12);
        ugrab(6);               /* reserved */
        R1005.system = ugrab(3);
        R1005.reference_station = (bool)ugrab(1);
        R1005.ecef_x = sgrab(38) * ANTENNA_POSITION_RESOLUTION;
        R1005.single_receiver = ugrab(1);
        ugrab(1);
        R1005.ecef_y = sgrab(38) * ANTENNA_POSITION_RESOLUTION;
        ugrab(2);
        R1005.ecef_z = sgrab(38) * ANTENNA_POSITION_RESOLUTION;
#undef R1005
        unknown = false;
        break;

    case 1006:
        /* Stationary Antenna Reference Point, with Height Information
         * 21 bytes */
#define R1006 rtcm->rtcmtypes.rtcm3_1006
        R1006.station_id = (unsigned short)ugrab(12);
        (void)ugrab(6);         /* reserved */
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
        unknown = false;
        break;

    case 1007:
        /* Antenna Description
         * 5 to 36 bytes */
        rtcm->rtcmtypes.rtcm3_1007.station_id = (unsigned short)ugrab(12);
        n = (unsigned long)ugrab(8);
        (void)memcpy(rtcm->rtcmtypes.rtcm3_1007.descriptor, buf + 7, n);
        rtcm->rtcmtypes.rtcm3_1007.descriptor[n] = '\0';
        bitcount += 8 * n;
        rtcm->rtcmtypes.rtcm3_1007.setup_id = ugrab(8);
        unknown = false;
        break;

    case 1008:
        /* Antenna Description & Serial Number
         * 6 to 68 bytes */
        rtcm->rtcmtypes.rtcm3_1008.station_id = (unsigned short)ugrab(12);
        n = (unsigned long)ugrab(8);
        (void)memcpy(rtcm->rtcmtypes.rtcm3_1008.descriptor, buf + 7, n);
        rtcm->rtcmtypes.rtcm3_1008.descriptor[n] = '\0';
        bitcount += 8 * n;
        rtcm->rtcmtypes.rtcm3_1008.setup_id = ugrab(8);
        n2 = (unsigned long)ugrab(8);
        (void)memcpy(rtcm->rtcmtypes.rtcm3_1008.serial, buf + 9 + n, n2);
        rtcm->rtcmtypes.rtcm3_1008.serial[n2] = '\0';
        //bitcount += 8 * n2;
        unknown = false;
        break;

    case 1009:
        /* GLONASS Basic RTK, L1 Only */
        rtcm->rtcmtypes.rtcm3_1009.header.station_id =
            (unsigned short)ugrab(12);
        rtcm->rtcmtypes.rtcm3_1009.header.tow = (time_t)ugrab(27);
        rtcm->rtcmtypes.rtcm3_1009.header.sync = (bool)ugrab(1);
        rtcm->rtcmtypes.rtcm3_1009.header.satcount = (unsigned short)ugrab(5);
        rtcm->rtcmtypes.rtcm3_1009.header.smoothing = (bool)ugrab(1);
        rtcm->rtcmtypes.rtcm3_1009.header.interval = (unsigned short)ugrab(3);
#define R1009 rtcm->rtcmtypes.rtcm3_1009.rtk_data[i]
        for (i = 0; i < rtcm->rtcmtypes.rtcm3_1009.header.satcount; i++) {
            R1009.ident = (unsigned short)ugrab(6);
            R1009.L1.indicator = (bool)ugrab(1);
            R1009.L1.channel = (short)ugrab(5) - GLONASS_CHANNEL_BASE;
            R1009.L1.pseudorange = ugrab(25) * GLONASS_PSEUDORANGE_RESOLUTION;
            RANGEDIFF(R1009.L1, 20);
            R1009.L1.locktime = (unsigned char)sgrab(7);
        }
#undef R1009
        unknown = false;
        break;

    case 1010:
        /* GLONASS Extended RTK, L1 Only */
        rtcm->rtcmtypes.rtcm3_1010.header.station_id =
            (unsigned short)ugrab(12);
        rtcm->rtcmtypes.rtcm3_1010.header.tow = (time_t)ugrab(27);
        rtcm->rtcmtypes.rtcm3_1010.header.sync = (bool)ugrab(1);
        rtcm->rtcmtypes.rtcm3_1010.header.satcount = (unsigned short)ugrab(5);
        rtcm->rtcmtypes.rtcm3_1010.header.smoothing = (bool)ugrab(1);
        rtcm->rtcmtypes.rtcm3_1010.header.interval = (unsigned short)ugrab(3);
#define R1010 rtcm->rtcmtypes.rtcm3_1010.rtk_data[i]
        for (i = 0; i < rtcm->rtcmtypes.rtcm3_1010.header.satcount; i++) {
            R1010.ident = (unsigned short)ugrab(6);
            R1010.L1.indicator = (bool)ugrab(1);
            R1010.L1.channel = (short)ugrab(5) - GLONASS_CHANNEL_BASE;
            R1010.L1.pseudorange = ugrab(25) * GLONASS_PSEUDORANGE_RESOLUTION;
            RANGEDIFF(R1010.L1, 20);
            R1010.L1.locktime = (unsigned char)sgrab(7);
            R1010.L1.ambiguity = (unsigned char)ugrab(7);
            R1010.L1.CNR = ugrab(8) * CARRIER_NOISE_RATIO_UNITS;
        }
#undef R1010
        unknown = false;
        break;

    case 1011:
        /* GLONASS Basic RTK, L1 & L2 */
        rtcm->rtcmtypes.rtcm3_1011.header.station_id =
            (unsigned short)ugrab(12);
        rtcm->rtcmtypes.rtcm3_1011.header.tow = (time_t)ugrab(27);
        rtcm->rtcmtypes.rtcm3_1011.header.sync = (bool)ugrab(1);
        rtcm->rtcmtypes.rtcm3_1011.header.satcount = (unsigned short)ugrab(5);
        rtcm->rtcmtypes.rtcm3_1011.header.smoothing = (bool)ugrab(1);
        rtcm->rtcmtypes.rtcm3_1011.header.interval = (unsigned short)ugrab(3);
#define R1011 rtcm->rtcmtypes.rtcm3_1011.rtk_data[i]
        for (i = 0; i < rtcm->rtcmtypes.rtcm3_1011.header.satcount; i++) {
            R1011.ident = (unsigned short)ugrab(6);
            R1011.L1.indicator = (bool)ugrab(1);
            R1011.L1.channel = (short)ugrab(5) - GLONASS_CHANNEL_BASE;
            R1011.L1.pseudorange = ugrab(25) * GLONASS_PSEUDORANGE_RESOLUTION;
            RANGEDIFF(R1011.L1, 20);
            R1011.L1.locktime = (unsigned char)sgrab(7);
            R1011.L1.ambiguity = (unsigned char)ugrab(7);
            R1011.L1.CNR = ugrab(8) * CARRIER_NOISE_RATIO_UNITS;
            R1011.L2.indicator = (bool)ugrab(1);
            R1011.L2.channel = (short)ugrab(5) - GLONASS_CHANNEL_BASE;
            R1011.L2.pseudorange = ugrab(25) * GLONASS_PSEUDORANGE_RESOLUTION;
            RANGEDIFF(R1011.L2, 20);
            R1011.L2.locktime = (unsigned char)sgrab(7);
            R1011.L2.ambiguity = (unsigned char)ugrab(7);
            R1011.L2.CNR = ugrab(8) * CARRIER_NOISE_RATIO_UNITS;
        }
#undef R1011
        unknown = false;
        break;

    case 1012:
        /* GLONASS Extended RTK, L1 & L2 */
        rtcm->rtcmtypes.rtcm3_1012.header.station_id =
            (unsigned short)ugrab(12);
        rtcm->rtcmtypes.rtcm3_1012.header.tow = (time_t)ugrab(27);
        rtcm->rtcmtypes.rtcm3_1012.header.sync = (bool)ugrab(1);
        rtcm->rtcmtypes.rtcm3_1012.header.satcount = (unsigned short)ugrab(5);
        rtcm->rtcmtypes.rtcm3_1012.header.smoothing = (bool)ugrab(1);
        rtcm->rtcmtypes.rtcm3_1012.header.interval = (unsigned short)ugrab(3);
#define R1012 rtcm->rtcmtypes.rtcm3_1012.rtk_data[i]
        for (i = 0; i < rtcm->rtcmtypes.rtcm3_1012.header.satcount; i++) {
            unsigned int rangeincr;
            R1012.ident = (unsigned short)ugrab(6);
            R1012.L1.indicator = (bool)ugrab(1);
            R1012.L1.channel = (short)ugrab(5) - GLONASS_CHANNEL_BASE;
            R1012.L1.pseudorange = ugrab(25) * GLONASS_PSEUDORANGE_RESOLUTION;
            RANGEDIFF(R1012.L1, 20);
            R1012.L1.locktime = (unsigned char)ugrab(7);
            R1012.L1.ambiguity = (unsigned char)ugrab(7);
            R1012.L1.CNR = (unsigned char)ugrab(8) * CARRIER_NOISE_RATIO_UNITS;
            R1012.L2.indicator = (bool)ugrab(2);
            rangeincr = ugrab(14);
            if (rangeincr == GLONASS_INVALID_RANGEINCR)
                R1012.L2.pseudorange = 0;
            else
                R1012.L2.pseudorange = (rangeincr *
                                        GLONASS_PSEUDORANGE_RESOLUTION);
            RANGEDIFF(R1012.L2, 20);
            R1012.L2.locktime = (unsigned char)sgrab(7);
            R1012.L2.CNR = (unsigned char)ugrab(8) * CARRIER_NOISE_RATIO_UNITS;
        }
#undef R1012
        unknown = false;
        break;

    case 1013:
        /* System Parameters */
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
        unknown = false;
        break;

    case 1014:
        /* Network Auxiliary Station Data
         * coordinate difference between one Aux station and the master station
         */
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
        unknown = false;
        break;

    case 1015:
        /* RTCM 3.1
         * GPS Ionospheric Correction Differences for all satellites
         * between the master station and one auxiliary station
         * 9 bytes minimum
         */
        unknown_name = "GPS Ionospheric Correction Differences";
        break;

    case 1016:
        /* RTCM 3.1
         * GPS Geometric Correction Differences for all satellites between
         * the master station and one auxiliary station.
         * 9 bytes minimum
         */
        unknown_name = "GPS Geometric Correction Differences";
        break;

    case 1017:
        /* RTCM 3.1
         * GPS Combined Geometric and Ionospheric Correction Differences
         * for all satellites between one Aux station and the master station
         * (same content as both types 1015 and 1016 together, but less size)
         * 9 bytes minimum
         */
        unknown_name = "GPS Combined Geometric and Ionospheric "
                       "Correction Differences";
        break;

    case 1018:
        /* RTCM 3.1
         * Reserved for alternative Ionospheric Correction Difference Message
         */
        unknown_name = "Reserved for alternative Ionospheric Correction "
                       "Differences";
        break;

    case 1019:
        /* RTCM 3.1 - 1020
         * GPS Ephemeris
         * 62 bytes
         */
        /* TODO: rtklib has C code for this one.  */
        unknown_name = "GPS Ephemeris";
        break;

    case 1020:
        /* RTCM 3.1 - 1020
         * GLONASS Ephemeris
         * 45 bytes
         */
        /* TODO: rtklib has C code for this one.  */
        unknown_name = "GLO Ephemeris";
        break;

    case 1021:
        /* RTCM 3.1
         * Helmert / Abridged Molodenski Transformation parameters
         */
        unknown_name = "Helmert / Abridged Molodenski Transformation "
                       "parameters";
        break;

    case 1022:
        /* RTCM 3.1
         * Molodenski-Badekas transformation parameters
         */
        unknown_name = "Molodenski-Badekas transformation parameters";
        break;

    case 1023:
        /* RTCM 3.1
         * Residuals Ellipsoidal Grid Representation
         */
        unknown_name = "Residuals Ellipsoidal Grid Representation";
        break;

    case 1024:
        /* RTCM 3.1
         * Residuals Plane Grid Representation
         */
        unknown_name = "Residuals Plane Grid Representation";
        break;

    case 1025:
        /* RTCM 3.1
         * Projection Parameters, Projection Types other than LCC2SP
         */
        unknown_name = "Projection Parameters, Projection Types other "
                       "than LCC2SP";
        break;

    case 1026:
        /* RTCM 3.1
         * Projection Parameters, Projection Type LCC2SP
         * (Lambert Conic Conformal)
         */
        unknown_name = "Projection Parameters, Projection Type LCC2SP";
        break;

    case 1027:
        /* RTCM 3.1
         * Projection Parameters, Projection Type OM (Oblique Mercator)
         */
        unknown_name = "Projection Parameters, Projection Type OM";
        break;

    case 1028:
        /* RTCM 3.1
         * Reserved for global to plate fixed transformation
         */
        unknown_name = "Reserved, Global to Plate Transformation";
        break;

    case 1029:
        /* Text in UTF8 format
         * 9 bytes minimum
         * (max. 127 multibyte characters and max. 255 bytes)
         */
        rtcm->rtcmtypes.rtcm3_1029.station_id = (unsigned short)ugrab(12);
        rtcm->rtcmtypes.rtcm3_1029.mjd = (unsigned short)ugrab(16);
        rtcm->rtcmtypes.rtcm3_1029.sod = (unsigned short)ugrab(17);
        rtcm->rtcmtypes.rtcm3_1029.len = (unsigned long)ugrab(7);
        rtcm->rtcmtypes.rtcm3_1029.unicode_units = (size_t)ugrab(8);
        (void)memcpy(rtcm->rtcmtypes.rtcm3_1029.text,
                     buf + 12, rtcm->rtcmtypes.rtcm3_1029.unicode_units);
        unknown = false;
        break;

    case 1030:
        /* RTCM 3.1
         * GPS Network RTK Residual Message
         */
        unknown_name = "GPS Network RTK Residual";
        break;

    case 1031:
        /* RTCM 3.1
         * GLONASS Network RTK Residual Message
         */
        unknown_name = "GLONASS Network RTK Residual";
        break;

    case 1032:
        /* RTCM 3.1
         * Physical Reference Station Position message
         */
        unknown_name = "Physical Reference Station Position";
        break;

    case 1033:                  /* see note in header */
        /* Receiver and Antenna Descriptor
         * Type1033 is a combined Message Types 1007 and 1008
         * and hence contains antenna descriptor and serial number
         * as well as receiver descriptor and serial number.
         */
        /* TODO: rtklib has C code for this one.  */
        rtcm->rtcmtypes.rtcm3_1033.station_id = (unsigned short)ugrab(12);
        n = (unsigned long)ugrab(8);
        (void)memcpy(rtcm->rtcmtypes.rtcm3_1033.descriptor, buf + 7, n);
        rtcm->rtcmtypes.rtcm3_1033.descriptor[n] = '\0';
        bitcount += 8 * n;
        rtcm->rtcmtypes.rtcm3_1033.setup_id = ugrab(8);
        n2 = (unsigned long)ugrab(8);
        (void)memcpy(rtcm->rtcmtypes.rtcm3_1033.serial, buf + 9 + n, n2);
        rtcm->rtcmtypes.rtcm3_1033.serial[n2] = '\0';
        bitcount += 8 * n2;
        n3 = (unsigned long)ugrab(8);
        (void)memcpy(rtcm->rtcmtypes.rtcm3_1033.receiver, buf + 10+n+n2, n3);
        rtcm->rtcmtypes.rtcm3_1033.receiver[n3] = '\0';
        bitcount += 8 * n3;
        n4 = (unsigned long)ugrab(8);
        (void)memcpy(rtcm->rtcmtypes.rtcm3_1033.firmware, buf + 11+n+n2+n3, n3);
        rtcm->rtcmtypes.rtcm3_1033.firmware[n4] = '\0';
        //bitcount += 8 * n4;
        // TODO: next is receiver serial number
        unknown = false;
        break;

    case 1034:
        /* RTCM 3.2
         * GPS Network FKP Gradient Message
         */
        unknown_name = "GPS Network FKP Gradient";
        break;

    case 1035:
        /* RTCM 3.2
         * GLONASS Network FKP Gradient Message
         */
        unknown_name = "GLO Network FKP Gradient";
        break;

    case 1037:
        /* RTCM 3.2
         * GLONASS Ionospheric Correction Differences
         */
        unknown_name = "GLO Ionospheric Correction Differences";
        break;

    case 1038:
        /* RTCM 3.2
         * GLONASS Geometric Correction Differences
         */
        unknown_name = "GLO Geometric Correction Differences";
        break;

    case 1039:
        /* RTCM 3.2
         * GLONASS Combined Geometric and Ionospheric Correction Differences
         */
        unknown_name = "GLONASS Combined Geometric and Ionospheric "
                       "Correction Differences";
        break;

    case 1042:
        /* RTCM 3.x - 1043
         * BeiDou Ephemeris
         * length ?
         */
        unknown_name = "BD Ephemeris";
        break;

    case 1043:
        /* RTCM 3.x - 1043
         * SBAS Ephemeris
         * length 29
         */
        unknown_name = "SBAS Ephemeris";
        break;

    case 1044:
        /* RTCM 3.x - 1044
         * QZSS ephemeris
         * length 61
         */
        /* TODO: rtklib has C code for this one.  */
        unknown_name = "QZSS Ephemeris";
        break;

    case 1045:
        /* RTCM 3.2 - 1045
         * Galileo F/NAV Ephemeris Data
         * 64 bytes
         */
        /* TODO: rtklib has C code for this one.  */
        unknown_name = "GAL F/NAV Ephemeris Data";
        break;

    case 1046:
        /* RTCM 3.x - 1046
         * Galileo I/NAV Ephemeris Data
         * length 63
         */
        /* TODO: rtklib has C code for this one.  */
        unknown_name = "GAL I/NAV Ephemeris Data";
        break;

    case 1057:
        /* RTCM 3.2
         * SSR GPS Orbit Correction
         */
        unknown_name = "SSR GPS Orbit Correction";
        break;

    case 1058:
        /* RTCM 3.2
         * SSR GPS Clock Correction
         */
        unknown_name = "SSR GPS Clock Correction";
        break;

    case 1059:
        /* RTCM 3.2
         * SSR GPS Code Bias
         */
        unknown_name = "SSR GPS Code Bias";
        break;

    case 1060:
        /* RTCM 3.2
         * SSR GPS Combined Orbit and Clock Correction
         */
        unknown_name = "SSR GPS Combined Orbit and Clock Correction";
        break;

    case 1061:
        /* RTCM 3.2
         * SSR GPS URA
         */
        unknown_name = "SSR GPS URA";
        break;

    case 1062:
        /* RTCM 3.2
         * SSR GPS High Rate Clock Correction
         */
        unknown_name = "SSR GPS High Rate Clock Correction";
        break;

    case 1063:
        /* RTCM 3.2
         * SSR GLO Orbit Correction
         */
        unknown_name = "SSR GLO Orbit Correction";
        break;

    case 1064:
        /* RTCM 3.2
         * SSR GLO Clock Correction
         */
        unknown_name = "SSR GLO Clock Correction";
        break;

    case 1065:
        /* RTCM 3.2
         * SSR GLO Code Correction
         */
        unknown_name = "SSR GLO ode Correction";
        break;

    case 1066:
        /* RTCM 3.2
         * SSR GLO Combined Orbit and Clock Correction
         */
        unknown_name = "SSR GLO Combined Orbit and Clock Correction";
        break;

    case 1067:
        /* RTCM 3.2
         * SSR GLO URA
         */
        unknown_name = "SSR GLO URA";
        break;

    case 1068:
        /* RTCM 3.2
         * SSR GPS High Rate Clock Correction
         */
        unknown_name = "SSR GLO High Rate Clock Correction";
        break;

    case 1070:
        /* RTCM 3.x
         * Reserved for MSM
         */
        unknown_name = "Reserved for MSM";
        break;

    case 1071:
        /* RTCM 3.2
         * GPS Multi Signal Message 1
         */
        unknown_name = "GPS Multi Signal Message 1";
        break;

    case 1072:
        /* RTCM 3.2
         * GPS Multi Signal Message 2
         */
        unknown_name = "GPS Multi Signal Message 2";
        break;

    case 1073:
        /* RTCM 3.2
         * GPS Multi Signal Message 3
         */
        unknown_name = "GPS Multi Signal Message 3";
        break;

    case 1074:
        /* RTCM 3.2
         * GPS Multi Signal Message 4
         */
        unknown_name = "GPS Multi Signal Message 4";
        break;

    case 1075:
        /* RTCM 3.2
         * GPS Multi Signal Message 5
         */
        unknown_name = "GPS Multi Signal Message 5";
        break;

    case 1076:
        /* RTCM 3.2
         * GPS Multi Signal Message 6
         */
        unknown_name = "GPS Multi Signal Message 6";
        break;

    case 1077:
        /* RTCM 3.2 - 1077
         * GPS Multi Signal Message 7
         * Full GPS pseudo-ranges, carrier phases, Doppler and
         * signal strength (high resolution)
         * length 438
         */
        /* TODO: rtklib has C code for this one.  */
        unknown_name = "GPS MSM7";
        break;

    case 1078:
        /* RTCM 3.x
         * Reserved for MSM
         */
        unknown_name = "Reserved for MSM";
        break;

    case 1079:
        /* RTCM 3.x
         * Reserved for MSM
         */
        unknown_name = "Reserved for MSM";
        break;

    case 1080:
        /* RTCM 3.x
         * Reserved for MSM
         */
        unknown_name = "Reserved for MSM";
        break;

    case 1081:
        /* RTCM 3.2
         * GLONASS Multi Signal Message 1
         */
        unknown_name = "GLO Multi Signal Message 1";
        break;

    case 1082:
        /* RTCM 3.2
         * GLONASS Multi Signal Message 2
         */
        unknown_name = "GLO Multi Signal Message 2";
        break;

    case 1083:
        /* RTCM 3.2
         * GLONASS Multi Signal Message 4
         */
        unknown_name = "GLO Multi Signal Message 3";
        break;

    case 1084:
        /* RTCM 3.2
         * GLONASS Multi Signal Message 4
         */
        unknown_name = "GLO Multi Signal Message 4";
        break;

    case 1085:
        /* RTCM 3.2
         * GLONASS Multi Signal Message 5
         */
        unknown_name = "GLO Multi Signal Message 5";
        break;

    case 1086:
        /* RTCM 3.2
         * GLONASS Multi Signal Message 6
         */
        unknown_name = "GLO Multi Signal Message 6";
        break;

    case 1087:
        /* RTCM 3.2 - 1087
         * GLONASS Multi Signal Message 7
         * Full GLONASS pseudo-ranges, carrier phases, Doppler and
         * signal strength (high resolution)
         * length 417 or 427
         */
        /* TODO: rtklib has C code for this one.  */
        unknown_name = "GLO Multi Signal Message 7";
        break;

    case 1088:
        /* RTCM 3.x
         * Reserved for MSM
         */
        unknown_name = "Reserved for MSM";
        break;

    case 1089:
        /* RTCM 3.x
         * Reserved for MSM
         */
        unknown_name = "Reserved for MSM";
        break;

    case 1090:
        /* RTCM 3.x
         * Reserved for MSM
         */
        unknown_name = "Reserved for MSM";
        break;

    case 1091:
        /* RTCM 3.2
         * Galileo Multi Signal Message 1
         */
        unknown_name = "GAL Multi Signal Message 1";
        break;

    case 1092:
        /* RTCM 3.2
         * Galileo Multi Signal Message 2
         */
        unknown_name = "GAL Multi Signal Message 2";
        break;

    case 1093:
        /* RTCM 3.2
         * Galileo Multi Signal Message 3
         */
        unknown_name = "GAL Multi Signal Message 3";
        break;

    case 1094:
        /* RTCM 3.2
         * Galileo Multi Signal Message 4
         */
        unknown_name = "GAL Multi Signal Message 4";
        break;

    case 1095:
        /* RTCM 3.2
         * Galileo Multi Signal Message 5
         */
        unknown_name = "GAL Multi Signal Message 5";
        break;

    case 1096:
        /* RTCM 3.2
         * Galileo Multi Signal Message 6
         */
        unknown_name = "GAL Multi Signal Message 6";
        break;

    case 1097:
        /* RTCM 3.2 - 1097
         * Galileo Multi Signal Message 7
         * Full Galileo pseudo-ranges, carrier phases, Doppler and
         * signal strength (high resolution)
         * length 96
         */
        /* TODO: rtklib has C code for this one.  */
        unknown_name = "GAL Multi Signal Message 7";
        break;

    case 1098:
        /* RTCM 3.x
         * Reserved for MSM
         */
        unknown_name = "Reserved for MSM";
        break;

    case 1099:
        /* RTCM 3.x
         * Reserved for MSM
         */
        unknown_name = "Reserved for MSM";
        break;

    case 1100:
        /* RTCM 3.x
         * Reserved for MSM
         */
        unknown_name = "Reserved for MSM";
        break;

    case 1101:
        /* RTCM 3.3
         * SBAS Multi Signal Message 1
         */
        unknown_name = "SBAS Multi Signal Message 1";
        break;

    case 1102:
        /* RTCM 3.3
         * SBAS Multi Signal Message 2
         */
        unknown_name = "SBAS Multi Signal Message 2";
        break;

    case 1103:
        /* RTCM 3.3
         * SBAS Multi Signal Message 3
         */
        unknown_name = "SBAS Multi Signal Message 3";
        break;

    case 1104:
        /* RTCM 3.3
         * SBAS Multi Signal Message 4
         */
        unknown_name = "SBAS Multi Signal Message 4";
        break;

    case 1105:
        /* RTCM 3.3
         * SBAS Multi Signal Message 5
         */
        unknown_name = "SBAS Multi Signal Message 5";
        break;

    case 1106:
        /* RTCM 3.3
         * SBAS Multi Signal Message 6
         */
        unknown_name = "SBAS Multi Signal Message 6";
        break;

    case 1107:
        /* RTCM 3.3 - 1107
         * 'Multiple Signal Message
         * Full SBAS pseudo-ranges, carrier phases, Doppler and
         * signal strength (high resolution)
         * length 96
         */
        /* TODO: rtklib has C code for this one.  */
        unknown_name = "SBAS Multi Signal Message 7";
        break;

    case 1108:
        /* RTCM 3.x
         * Reserved for MSM
         */
        unknown_name = "Reserved for MSM";
        break;

    case 1109:
        /* RTCM 3.x
         * Reserved for MSM
         */
        unknown_name = "Reserved for MSM";
        break;

    case 1110:
        /* RTCM 3.x
         * Reserved for MSM
         */
        unknown_name = "Reserved for MSM";
        break;

    case 1111:
        /* RTCM 3.3
         * QZSS Multi Signal Message 1
         */
        unknown_name = "QZSS Multi Signal Message 1";
        break;

    case 1112:
        /* RTCM 3.3
         * QZSS Multi Signal Message 2
         */
        unknown_name = "QZSS Multi Signal Message 2";
        break;

    case 1113:
        /* RTCM 3.3
         * QZSS Multi Signal Message 3
         */
        unknown_name = "QZSS Multi Signal Message 3";
        break;

    case 1114:
        /* RTCM 3.3
         * QZSS Multi Signal Message 4
         */
        unknown_name = "QZSS Multi Signal Message 4";
        break;

    case 1115:
        /* RTCM 3.3
         * QZSS Multi Signal Message 5
         */
        unknown_name = "QZSS Multi Signal Message 5";
        break;

    case 1116:
        /* RTCM 3.3
         * QZSS Multi Signal Message 6
         */
        unknown_name = "QZSS Multi Signal Message 6";
        break;

    case 1117:
        /* RTCM 3.3
         * QZSS Multi Signal Message 7
         */
        unknown_name = "QZSS Multi Signal Message 7";
        break;

    case 1118:
        /* RTCM 3.x
         * Reserved for MSM
         */
        unknown_name = "Reserved for MSM";
        break;

    case 1119:
        /* RTCM 3.x
         * Reserved for MSM
         */
        unknown_name = "Reserved for MSM";
        break;

    case 1120:
        /* RTCM 3.x
         * Reserved for MSM
         */
        unknown_name = "Reserved for MSM";
        break;

    case 1121:
        /* RTCM 3.2 A.1
         * BD Multi Signal Message 1
         */
        unknown_name = "BD Multi Signal Message 1";
        break;

    case 1122:
        /* RTCM 3.2 A.1
         * BD Multi Signal Message 2
         */
        unknown_name = "BD Multi Signal Message 2";
        break;

    case 1123:
        /* RTCM 3.2 A.1
         * BD Multi Signal Message 3
         */
        unknown_name = "BD Multi Signal Message 3";
        break;

    case 1124:
        /* RTCM 3.2 A.1
         * BD Multi Signal Message 4
         */
        unknown_name = "BD Multi Signal Message 4";
        break;

    case 1125:
        /* RTCM 3.2 A.1
         * BeiDou Multi Signal Message 5
         */
        unknown_name = "BD Multi Signal Message 5";
        break;

    case 1126:
        /* RTCM 3.2 A.1
         * BeiDou Multi Signal Message 6
         */
        unknown_name = "BD Multi Signal Message 6";
        break;

    case 1127:
        /* RTCM 3.2 A.1
         * BeiDou Multi Signal Message 7
         */
        unknown_name = "BD Multi Signal Message 7";
        break;

    case 1128:
        /* RTCM 3.x
         * Reserved for MSM
         */
        unknown_name = "Reserved for MSM";
        break;

    case 1229:
        /* RTCM 3.x
         * Reserved for MSM
         */
        unknown_name = "Reserved for MSM";
        break;

    case 1230:
        /* RTCM 3.2
         * GLONASS L1 and L2, C/A and P, Code-Phase Biases.
         */
        unknown_name = "GLO L1 and L2 Code-Phase Biases";
        unknown = false;
        rtcm->rtcmtypes.rtcm3_1230.station_id = (unsigned short)ugrab(12);
        rtcm->rtcmtypes.rtcm3_1230.bias_indicator = (unsigned char)ugrab(1);
        (void)ugrab(1);         /* reserved */
        rtcm->rtcmtypes.rtcm3_1230.signals_mask = (unsigned char)ugrab(3);
        // actual mask order is undocumented...
        if (1 & rtcm->rtcmtypes.rtcm3_1230.signals_mask) {
            rtcm->rtcmtypes.rtcm3_1230.l1_ca_bias = ugrab(16);
        }
        if (2 & rtcm->rtcmtypes.rtcm3_1230.signals_mask) {
            rtcm->rtcmtypes.rtcm3_1230.l1_p_bias = ugrab(16);
        }
        if (4 & rtcm->rtcmtypes.rtcm3_1230.signals_mask) {
            rtcm->rtcmtypes.rtcm3_1230.l2_ca_bias = ugrab(16);
        }
        if (8 & rtcm->rtcmtypes.rtcm3_1230.signals_mask) {
            rtcm->rtcmtypes.rtcm3_1230.l2_p_bias = ugrab(16);
        }
        break;

    case 4072:
        /* RTCM 3.x
         * u-blox Proprietary
         * Mitsubishi Electric Corp Proprietary
         * 4072.0 Reference station PVT (u-blox proprietary)
         * 4072.1 Additional reference station information (u-blox proprietary)
         */
        unknown_name = "u-blox Proprietary";
        break;

    case 4073:
        /* RTCM 3.x
         * Unicore Communications Proprietary
         */
        unknown_name = "Alberding GmbH Proprietary";
        break;

    case 4075:
        /* RTCM 3.x
         * Alberding GmbH Proprietary
         */
        unknown_name = "Alberding GmbH Proprietary";
        break;

    case 4076:
        /* RTCM 3.x
         * International GNSS Service Proprietary
         */
        unknown_name = "International GNSS Service Proprietary";
        break;

    case 4077:
        /* RTCM 3.x
         * Hemisphere GNSS Proprietary
         */
        unknown_name = "Hemisphere GNSS Proprietary";
        break;

    case 4078:
        /* RTCM 3.x
         * ComNav Technology Proprietary
         */
        unknown_name = "ComNav Technology Proprietary";
        break;

    case 4079:
        /* RTCM 3.x
         * SubCarrier Systems Corp Proprietary
         */
        unknown_name = "SubCarrier Systems Corp Proprietary";
        break;

    case 4080:
        /* RTCM 3.x
         * NavCom Technology, Inc.
         */
        unknown_name = "NavCom Technology, Inc.";
        break;

    case 4081:
        /* RTCM 3.x
         * Seoul National Universtiry GNSS Lab Proprietary
         */
        unknown_name = "Seoul National Universtiry GNSS Lab Proprietery";
        break;

    case 4082:
        /* RTCM 3.x
         * Cooperative Research Centre for Spatial Information Proprietary
         */
        unknown_name = "Cooperative Research Centre for Spatial Information "
                       "Proprietary";
        break;

    case 4083:
        /* RTCM 3.x
         * German Aerospace Center Proprietary
         */
        unknown_name = "German Aerospace Center Proprietary";
        break;

    case 4084:
        /* RTCM 3.x
         * Geodetics Inc Proprietary
         */
        unknown_name = "Geodetics Inc Proprietary";
        break;

    case 4085:
        /* RTCM 3.x
         * European GNSS Supervisory Authority Proprietary
         */
        unknown_name = "European GNSS Supervisory Authority Proprietary";
        break;

    case 4086:
        /* RTCM 3.x
         * InPosition GmbH Proprietary
         */
        unknown_name = "InPosition GmbH Proprietary";
        break;

    case 4087:
        /* RTCM 3.x
         * Fugro Proprietary
         */
        unknown_name = "Fugro Proprietary";
        break;

    case 4088:
        /* RTCM 3.x
         * IfEN GmbH Proprietary
         */
        unknown_name = "IfEN GmbH Proprietary";
        break;

    case 4089:
        /* RTCM 3.x
         * Septentrio Satellite Navigation Proprietary
         */
        unknown_name = "Septentrio Satellite Navigation Proprietary";
        break;

    case 4090:
        /* RTCM 3.x
         * Geo++ Proprietary
         */
        unknown_name = "Geo++ Proprietary";
        break;

    case 4091:
        /* RTCM 3.x
         * Topcon Positioning Systems Proprietary
         */
        unknown_name = "Topcon Positioning Systems Proprietary";
        break;

    case 4092:
        /* RTCM 3.x
         * Leica Geosystems Proprietary
         */
        unknown_name = "Leica Geosystems Proprietary";
        break;

    case 4093:
        /* RTCM 3.x
         * NovAtel Proprietary
         */
        unknown_name = "NovAtel Pr.orietary";
        break;

    case 4094:
        /* RTCM 3.x
         * Trimble Proprietary
         */
        unknown_name = "Trimble Proprietary";
        break;

    case 4095:
        /* RTCM 3.x
         * Ashtech/Magellan Proprietary
         */
        unknown_name = "Ashtech/Magellan Proprietary";
        break;

    default:
        break;
    }
#undef RANGEDIFF
#undef GPS_PSEUDORANGE
#undef sgrab
#undef ugrab
    if ( unknown ) {
        /*
         * Leader bytes, message length, and checksum won't be copied.
         * The first 12 bits of the copied payload will be the type field.
         */
        memcpy(rtcm->rtcmtypes.data, buf+3, rtcm->length);
        if (NULL == unknown_name) {
	    GPSD_LOG(LOG_PROG, &context->errout,
		     "RTCM3: unknown type %d, length %d\n",
		     rtcm->type, rtcm->length);
        } else {
	    GPSD_LOG(LOG_PROG, &context->errout,
		     "RTCM3: %s (type %d), length %d\n",
                     unknown_name, rtcm->type, rtcm->length);
        }
    }

}

/* *INDENT-ON* */

#endif /* RTCM104V3_ENABLE */

// vim: set expandtab shiftwidth=4
