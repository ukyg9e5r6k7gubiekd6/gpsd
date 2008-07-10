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

#ifdef RTCM104V3_ENABLE

/*
 * Structures for interpreting words in an RTCM-104 3.x packet Note,
 * these structures are overlayed on the raw packet data in order to
 * decode them into bitfields; this will fail horribly if your C
 * compiler ever introduces padding between or before bit fields, or
 * between 8-bit-aligned bitfields and character arrays.
 *
 * (In practice, the only class of machines on which this is likely
 * to fail are word-aligned architectures without barrel shifters.
 * Very few of these are left in 2008.)
 */
#pragma pack(1)		/* try to tell the compiler not to pad at all */

/*
 * These structures mostly parallel the rtcm3 report structures in
 * gps.h.  The differences are that some structures have to be twinned
 * because the GPS/Galileo versions can't have the extra channel field
 * in them that the GLONASS version does, or because some packed bit
 * lengths differ.
 *
 * The bitfield widths 
 */

struct rtcm3_msg_rtk_hdr_t {	/* header data from 1001, 1002, 1003, 1004 */
    uint station_id:12;		/* Reference Station ID (DF003) */
    uint time:30;		/* GPS Epoch Time (TOW) (DF004) in ms */
    uint sync:1;		/* Synchronous GNSS Message Flag (DF005) */
    uint satcount:5;		/* # Satellite Signals Processed (DF006) */
    uint smoothing:1;		/* Divergence-free Smoothing Indicator (DF007)*/
    uint interval:3;		/* Smoothing Interval (DF008) */
};

struct rtcm3_msg_basic_rtk_t {
    uint indicator:1;		/* Indicator (DF010) */
    uint pseudorange:24;	/* Pseudorange (DF011) */
    uint rangediff:20;		/* PhaseRange – Pseudorange in meters (DF012) */
    uint locktime:7;		/* Lock time Indicator (DF013) */
};

struct rtcm3_msg_t_extended_rtk {
    uint indicator:1;		/* Indicator (DF010) */
    uint pseudorange:24;	/* Pseudorange (DF011) */
    uint rangediff:20;		/* PhaseRange – Pseudorange in meters (DF012) */
    uint locktime:7;		/* Lock time Indicator (DF013) */
    uint ambiguity:8;		/* Pseudorange Modulus Ambiguity (DF014)*/
    uint CNR:8;			/* Carrier-to-Noise Ratio (DF015) */
};

struct rtcm3_msg_glonass_rtk_hdr_t {	/* header data from 1009, 1010, 1011, 1012 */
    uint station_id:12;		/* Reference Station ID (DF003) */
    uint time:27;		/* GLONASS Epoch Time in ms (DF034) */
    uint sync:1;		/* Synchronous GNSS Message Flag (DF005) */
    uint satcount:5;		/* # Satellite Signals Processed (DF006) */
    uint smoothing:1;		/* Divergence-free Smoothing Indicator (DF007)*/
    uint interval:3;		/* Smoothing Interval (DF008) */
};

struct rtcm3_msg_basic_glonass_rtk_t {
    uint indicator:1;		/* Indicator (DF039) */
    uint channel:5;		/* Satellite Frequency Channel Number (DF040)*/	
    uint pseudorange:25;	/* Pseudorange (DF041) */
    uint rangediff:20;		/* PhaseRange – Pseudorange in meters (DF042)*/
    uint locktime:7;		/* Lock time Indicator (DF043) */
};

struct rtcm3_msg_extended_glonass_rtk_t {
    uint indicator:1;		/* Indicator (DF010) */
    uint channel:5;		/* Satellite Frequency Channel Number (DF040)*/	
    uint pseudorange:25;	/* Pseudorange (DF041) */
    uint rangediff:20;		/* PhaseRange – Pseudorange in meters (DF042) */
    uint locktime:7;		/* Lock time Indicator (DF043) */
    uint ambiguity:8;		/* Pseudorange Modulus Ambiguity (DF044)*/
    uint CNR:8;			/* Carrier-to-Noise Ratio (DF045) */
};

struct rtcm3_msg_network_rtk_header_t {
    uint msgnum:12;		/* Message number (DF002) */
    uint network_id:8;		/* Network ID (DF059) */
    uint subnetwork_id:4;	/* Subnetwork ID (DF072) */
    uint time:23;		/* GPS Epoch Time (TOW) in ms (DF065) */
    uint multimesg:1;		/* GPS Multiple Message Indicator (DF066) */
    uint master_id:12;		/* Master Reference Station ID (DF060) */
    uint aux_id:12;		/* Auxilary Reference Station ID (DF061) */
    uint satcount:4;		/* # of GPS satellites (DF067) */
};

struct rtcm3_msg_correction_diff_t {
    uint ident:6;		/* satellite ID (DF068) */
    uint ambiguity:2;		/* Ambiguity status flag (DF074) */
    uint nonsync:3;		/* Non Sync Count (DF075) */
    int geometric_diff:17;	/* Geometric Carrier Phase 
				   Correction Difference (DF070) */
    uint iode:8;		/* GPS IODE (DF071) */
    uint ionospheric_diff:17;	/* Ionospheric Carrier Phase 
				   Correction Difference (DF069) */
};

struct rtcm3_msg_t {
    /* frame header */
    uint preamble:8;	/* fixed, 0xD3 */
    uint version:6;	/* reserved version field, fixed to 0 in 3.1 */
    uint length:10;	/* payload length, inclusive of checksum */

    /* type field */
    uint type:12;	/* RTCM3 message type */

    union {
	/* 1001-1013 were present in the 3.0 version */
	struct {
	    struct rtcm3_msg_rtk_hdr_t	header;
	    struct {
		uint satellite_id:6;	/* GPS satellite ID (DF009) */
		struct rtcm3_msg_basic_rtk_t L1;
	    } rtk_data[RTCM3_MAX_SATELLITES];
	} rtcm3_msg_t_1001;
	struct {
	    struct rtcm3_msg_rtk_hdr_t	header;
	    struct {
		uint satellite_id:6;	/* GPS satellite ID (DF009) */
		struct rtcm3_msg_t_extended_rtk L1;
	    } rtk_data[RTCM3_MAX_SATELLITES];
	} rtcm3_msg_t_1002;
	struct {
	    struct rtcm3_msg_rtk_hdr_t	header;
	    struct {
		uint satellite_id:6;	/* GPS satellite ID (DF009) */
		struct rtcm3_msg_basic_rtk_t L1;
		struct rtcm3_msg_basic_rtk_t L2;
	    } rtk_data[RTCM3_MAX_SATELLITES];
	} rtcm3_msg_t_1003;
	struct {
	    struct rtcm3_msg_rtk_hdr_t header;
	    struct {
		uint satellite_id:6;	/* GPS satellite ID (DF009) */
		struct rtcm3_msg_t_extended_rtk L1;
		struct rtcm3_msg_t_extended_rtk L2;
	    } rtk_data[RTCM3_MAX_SATELLITES];
	} rtcm3_msg_t_1004;
	struct {
	    uint station_id:12;		/* Reference Station ID (DF003) */
	    uint reserved1:6;		/* Reserved for ITRF Realization 
					   Year (DF021) */
	    uint gps_indicator:1;	/* GPS Indicator (DF022) */
	    uint glonass_indicator:1;	/* GLONASS Indicator (DF023) */
	    uint galileo_indicator:1;	/* Reserved for Galileo Indicator
					   (DF024) */
	    uint reference_station:1;	/* Reference Station Indicator
					   (DF141) */
	    long long int ecef_x:38;	/* Antenna Reference point ECEF-X
					   (DF025) */
	    uint single_receiver:1;	/* Single Receiver Oscillator Indicator
					   (DF142) */
	    uint reserved2:1;		/* Reserved (DF001) */
	    long long int ecef_y:38;	/* Antenna Reference point ECEF-X
					   (DF026) */
	    uint reserved3:1;		/* Reserved (DF001) */
	    long long int ecef_z:38;	/* Antenna Reference point ECEF-Z
					   (DF026) */
	} rtcm3_msg_t_1005;
	struct {
	    uint station_id:12;		/* Reference Station ID (DF003) */
	    uint reserved1:6;		/* Reserved for ITRF Realization 
					   Year (DF021) */
	    uint gps_indicator:1;	/* GPS Indicator (DF022) */
	    uint glonass_indicator:1;	/* GLONASS Indicator (DF023) */
	    uint galileo_indicator:1;	/* Reserved for Galileo Indicator
					   (DF024) */
	    uint reference_station:1;	/* Reference Station Indicator
					   (DF141) */
	    long long int ecef_x:38;	/* Antenna Reference point ECEF-X
					   (DF025) */
	    uint single_receiver:1;	/* Single Receiver Oscillator Indicator
					   (DF142) */
	    uint reserved2:1;		/* Reserved (DF001) */
	    long long int ecef_y:38;	/* Antenna Reference point ECEF-X
					   (DF026) */
	    uint reserved3:1;		/* Reserved (DF001) */
	    long long int ecef_z:38;	/* Antenna Reference point ECEF-Z
					   (DF026) */
	    uint height:16;		/* Antenna height (DF028) */
	} rtcm3_msg_t_1006;
	struct {
	    uint station_id:12;		/* Reference Station ID (DF003) */
	    uint counter:8;		/* Descriptor Counter (DF029) */
	    /*
	     * The spec designers screwed up here; they put the Setup
	     * ID field after the variable-length descriptor string so
	     * it doesn't have a fixed bit offset from start of
	     * message.  The unpacking code will have to mine the
	     * byte out of the part of the data that descriptor[] overlays.
	     */
	    char descriptor[RTCM3_MAX_DESCRIPTOR+2];	/* Descriptor (DF030) */
	} rtcm3_msg_t_1007;
	struct {
	    /* 
	     * And a similar design error here. There are actually two 
	     * variable-length descriptors in this message and two fixed-
	     * length fields between them.
	     */ 
	    uint station_id:12;		/* Reference Station ID (DF003) */
	    uint counter:8;		/* Descriptor Counter (DF029) */
	    char rawdata[64];
	} rtcm3_msg_t_1008;
	struct {
	    struct rtcm3_msg_glonass_rtk_hdr_t	header;
	    struct {
		uint satellite_id:6;	/* GLONASS Satellite ID (DF038) */
		struct rtcm3_msg_basic_glonass_rtk_t L1;
	    } rtk_data[RTCM3_MAX_SATELLITES];
	} rtcm3_msg_t_1009;
	struct {
	    struct rtcm3_msg_glonass_rtk_hdr_t	header;
	    struct {
		uint satellite_id:6;	/* GLONASS Satellite ID (DF038) */
		struct rtcm3_msg_extended_glonass_rtk_t L1;
	    } rtk_data[RTCM3_MAX_SATELLITES];
	} rtcm3_msg_t_1010;
	struct {
	    struct rtcm3_msg_glonass_rtk_hdr_t	header;
	    struct {
		uint satellite_id:6;	/* GLONASS Satellite ID (DF038) */
		struct rtcm3_msg_extended_glonass_rtk_t L1;
		struct rtcm3_msg_extended_glonass_rtk_t L2;
	    } rtk_data[RTCM3_MAX_SATELLITES];
	} rtcm3_msg_t_1011;
	struct {
	    struct rtcm3_msg_glonass_rtk_hdr_t	header;
	    struct {
		uint satellite_id:6;	/* GLONASS Satellite ID (DF038) */
		struct rtcm3_msg_extended_glonass_rtk_t L1;
		struct rtcm3_msg_extended_glonass_rtk_t L2;
	    } rtk_data[RTCM3_MAX_SATELLITES];
	} rtcm3_msg_t_1012;
	struct {
	    uint msgnum:12;		/* Message number (DF002) */
	    uint station_id:12;		/* Reference Station ID (DF003) */
	    uint mjd:16;		/* Modified Julian Day (MJD) Number
					   (DF051)*/
	    uint sod:17;		/* Seconds of Day (UTC) (DF052) */
	    uint ncount:5;		/* Count of announcements (DF053) */
	    uint leapsecs:8;		/* Leap Seconds, GPS-UTC (DF053) */
	    struct {
		uint id:12;		/* ID (DF055) */
		uint sync:1;		/* Sync flag (DF056) */
		uint interval:16;	/* transmission interval (DF057) */
	    } announcements[RTCM3_MAX_ANNOUNCEMENTS];
	} rtcm3_msg_t_1013;
	/* 1014-1017 were added in the 3.1 version */
	struct {
	    uint msgnum:12;		/* Message number (DF002) */
	    uint network_id:8;		/* Network ID (DF059) */
	    uint subnetwork_id:8;	/* Network ID (DF072) */
	    uint stationcount:5;	/* # auxiliary stations transmitted
					   (DF058)*/
	    uint master_id:12;		/* Master Reference Station ID
					   (DF060) */
	    uint aux_id:12;		/* Auxilary Reference Station ID
					  (DF061) */
	    int d_lat:20;		/* Aux-Master Delta Latitude (DF062) */	
	    int d_lon:21;		/* Aux-Master Delta Longitude (DF063)*/	
	    int d_alt:23;		/* Aux-Master Delta Height (DF064) */	
	} rtcm3_msg_t_1014;
	struct {
	    struct rtcm3_msg_network_rtk_header_t	header;
	    struct {
		uint ident:6;		/* satellite ID (DF068) */
		uint ambiguity:2;	/* Ambiguity status flag (DF074) */
		uint nonsync:3;		/* Non Sync Count (DF075) */
		uint ionospheric_diff:17;	/* Ionospheric Carrier Phase 
						   Correction Difference 
						   (DF069) */
	    } corrections[RTCM3_MAX_SATELLITES];
	} rtcm3_msg_t_1015;
	struct {
	    struct rtcm3_msg_network_rtk_header_t	header;
	    struct {
		uint ident:6;		/* satellite ID (DF068) */
		uint ambiguity:2;	/* Ambiguity status flag (DF074) */
		uint nonsync:3;		/* Non Sync Count (DF075) */
		int geometric_diff:17;	/* Geometric Carrier Phase 
					   Correction Difference (DF070) */
		uint iode:8;		/* GPS IODE (DF071) */
	    } corrections[RTCM3_MAX_SATELLITES];
	} rtcm3_msg_t_1016;
	struct {
	    struct rtcm3_msg_network_rtk_header_t	header;
	    struct {
		uint ident:6;		/* satellite ID (DF068) */
		uint ambiguity:2;	/* Ambiguity status flag (DF074) */
		uint nonsync:3;		/* Non Sync Count (DF075) */
		int geometric_diff:17;	/* Geometric Carrier Phase 
					   Correction Difference (DF070) */
		uint iode:8;		/* GPS IODE (DF071) */
		uint ionospheric_diff:17;	/* Ionospheric Carrier Phase 
						   Correction Difference 
						   (DF069) */
	    } corrections[RTCM3_MAX_SATELLITES];
	} rtcm3_msg_t_1017;
	struct {
	    uint msgnum:12;	/* Message number (DF002) */
	    uint ident:6;	/* Satellite ID (DF009) */
	    uint week:10;	/* GPS Week Number (DF076) */
	    uint sv_accuracy;	/* GPS SV ACCURACY (DF077) */
	    uint code:2;	/* GPS CODE ON L2 (DF078) */
	    uint idot:14;	/* GPS IDOT (DF079) */
	    uint iode;		/* GPS IODE (DF071) */
	    /* ephemeris fields, not scaled */
	    uint t_sub_oc:16;
	    signed int a_sub_f2:8;
	    signed int a_sub_f1:16;
	    signed int a_sub_f0:22;
	    uint iodc:10;
	    signed int C_sub_rs:16;
	    signed int delta_sub_n:16;
	    signed int M_sub_0:32;
	    signed int C_sub_uc:16;
	    uint e:32;
	    signed int C_sub_us:16;
	    uint sqrt_sub_A:32;
	    uint t_sub_oe:16;
	    signed int C_sub_ic:16;
	    signed int OMEGA_sub_0:32;
	    signed int C_sub_is:16;
	    signed int i_sub_0:32;
	    signed int C_sub_rc:16;
	    signed int argument_of_perigee:32;
	    signed int omegadot:24;
	    signed int t_sub_GD:8;
	    uint sv_health:6;
	    uint P_data:1;
	    uint fit_interval:1;
	} rtcm3_msg_t_1019;
	struct {
	    uint msgnum:12;	/* Message number */
	    uint ident:6;	/* Satellite ID */
	    uint channel:5;	/* Satellite Frequency Channel Number */
	    /* ephemeris fields, not scaled */
	    uint C_sub_n1;
	    uint health_avAilability_indicator:1;
	    uint P1:2;
	    unsigned short t_sub_k:12;
	    uint msb_of_B_sub_n:1;
	    uint P2:1;
	    uint t_sub_b:7;
	    signed int x_sub_n_t_of_t_sub_b_prime:24;
	    signed int x_sub_n_t_of_t_sub_b:27;
	    signed int x_sub_n_t_of_t_sub_b_prime_prime:5;
	    signed int y_sub_n_t_of_t_sub_b_prime:24;
	    signed int y_sub_n_t_of_t_sub_b:27;
	    signed int y_sub_n_t_of_t_sub_b_prime_prime:5;
	    signed int z_sub_n_t_of_t_sub_b_prime:24;
	    signed int z_sub_n_t_of_t_sub_b:27;
	    signed int z_sub_n_t_of_t_sub_b_prime_prime:5;
	    uint P3:1;
	    signed int gamma_sub_n_of_t_sub_b:11;
	    uint MP:2;
	    uint Ml_n:1;
	    signed int tau_n_of_t_sub_b:22;
	    signed int M_delta_tau_sub_n:5;
	    uint E_sub_n:5;
	    uint MP4:1;
	    uint MF_sub_T:4;
	    uint MN_sub_T:11;
	    uint MM:2;
	    uint additioinal_data_availability:1;
	    uint N_sup_A:11;
	    uint tau_sub_c:32;
	    uint M_N_sub_4:5;
	    signed int M_tau_sub_GPS:22;
	    uint M_l_sub_n:1;
	    //uint reserved:7;
	} rtcm3_msg_t_1020;
	struct {
	    uint msgnum:12;		/* Message number (DF002) */
	    uint station_id:12;		/* Reference Station ID (DF003) */
	    uint mjd:16;		/* Modified Julian Day (MJD) Number
					   (DF051) */
	    uint sod:17;		/* Seconds of Day (UTC) (DF052) */
	    uint len:7;			/* # Chars to follow (DF138) */
	    uint unicode_units:8;	/* # Unicode units (DF139) */
	    unsigned char text[128];
	} rtcm3_msg_t_1029;
    };
};

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

void rtcm3_unpack(/*@out@*/struct rtcm3_t *tp, char *buf)
/* break out the raw bits into the scaled report-structure fields */
{
    struct rtcm3_msg_t *msg = (struct rtcm3_msg_t *)buf;

    assert(msg->preamble == 0xD3);
    assert(msg->version == 0x00);
    tp->type = msg->type;
    tp->length = msg->length;

    // FIXME: Decoding of packet content goes here
}

#endif /* RTCM104V3_ENABLE */
