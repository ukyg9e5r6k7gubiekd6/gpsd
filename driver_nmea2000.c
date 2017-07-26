/*
 * NMEA2000 over CAN.
 *
 * This file is Copyright (c) 2012 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 */

/* need this for strnlen() and struct ifreq */
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <time.h>
#include <math.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>

#include "gpsd.h"
#include "libgps.h"
#if defined(NMEA2000_ENABLE)
#include "driver_nmea2000.h"
#include "bits.h"

#include <linux/can.h>
#include <linux/can/raw.h>

#define LOG_FILE 1
#define NMEA2000_NETS 4
#define NMEA2000_UNITS 256
#define CAN_NAMELEN 32
#define MIN(a,b) ((a < b) ? a : b)

#define NMEA2000_DEBUG_AIS 0
#define NMEA2000_FAST_DEBUG 0

static struct gps_device_t *nmea2000_units[NMEA2000_NETS][NMEA2000_UNITS];
static char can_interface_name[NMEA2000_NETS][CAN_NAMELEN+1];

typedef struct PGN
    {
    unsigned int  pgn;
    unsigned int  fast;
    unsigned int  type;
    gps_mask_t    (* func)(unsigned char *bu, int len, struct PGN *pgn, struct gps_device_t *session);
    const char    *name;
    } PGN;


#if LOG_FILE
FILE *logFile = NULL;
#endif /* of if LOG_FILE */

extern bool __attribute__ ((weak)) gpsd_add_device(const char *device_name, bool flag_nowait);

#define SHIFT32 0x100000000l

static int scale_int(int32_t var, const int64_t factor)
{
        int64_t ret;

        ret   = var;
        ret  *= factor;
        ret >>= 32;

        return((int)ret);
}

static void print_data(struct gps_context_t *context,
		       unsigned char *buffer, int len, PGN *pgn)
{
#ifdef LIBGPS_DEBUG
    if ((libgps_debuglevel >= LOG_IO) != 0) {
	int   l1, l2, ptr;
	char  bu[128];

        ptr = 0;
        l2 = sprintf(&bu[ptr], "got data:%6u:%3d: ", pgn->pgn, len);
	ptr += l2;
        for (l1=0;l1<len;l1++) {
            if (((l1 % 20) == 0) && (l1 != 0)) {
	        gpsd_log(&context->errout, LOG_IO,"%s\n", bu);
		ptr = 0;
                l2 = sprintf(&bu[ptr], "                   : ");
		ptr += l2;
            }
            l2 = sprintf(&bu[ptr], "%02ux ", (unsigned int)buffer[l1]);
	    ptr += l2;
        }
        gpsd_log(&context->errout, LOG_IO,"%s\n", bu);
    }
#else
    (void)context;
    (void)buffer;
    (void)len;
    (void)pgn;
#endif
}

static gps_mask_t get_mode(struct gps_device_t *session)
{
    if (session->driver.nmea2000.mode_valid & 1) {
        session->newdata.mode = session->driver.nmea2000.mode;
    } else {
        session->newdata.mode = MODE_NOT_SEEN;
    }

    if (session->driver.nmea2000.mode_valid & 2) {
        return MODE_SET | USED_IS;
    } else {
        return MODE_SET;
    }
}


static int decode_ais_header(struct gps_context_t *context,
    unsigned char *bu, int len, struct ais_t *ais, unsigned int mask)
{
    if (len > 4) {
        ais->type   = (unsigned int) ( bu[0]       & 0x3f);
	ais->repeat = (unsigned int) ((bu[0] >> 6) & 0x03);
	ais->mmsi   = (unsigned int)  getleu32(bu, 1);
	ais->mmsi  &= mask;
	gpsd_log(&context->errout, LOG_INF,
		 "NMEA2000 AIS  message type %u, MMSI %09d:\n",
		 ais->type, ais->mmsi);
	return(1);
    } else {
        ais->type   =  0;
	ais->repeat =  0;
	ais->mmsi   =  0;
	gpsd_log(&context->errout, LOG_ERROR,
		 "NMEA2000 AIS  message type %u, too short message.\n",
		 ais->type);
    }
    return(0);
}


static void decode_ais_channel_info(unsigned char *bu,
				    int len,
				    unsigned int offset,
				    struct gps_device_t *session)
{
    unsigned int pos, bpos;
    uint16_t x;

    pos = offset / 8;
    bpos = offset % 8;
    if (pos >= (unsigned int)len) {
        session->driver.aivdm.ais_channel = 'A';
	return;
    }
    x = getleu16(bu, pos);
    x = (uint16_t)((x >> bpos) & 0x1f);
    switch (x) {
    case 1:
    case 3:
        session->driver.aivdm.ais_channel = 'B';
	break;
    default:
        session->driver.aivdm.ais_channel = 'A';
	break;
    }
    return;
}


static int ais_turn_rate(int rate)
{
    if (rate < 0) {
        return(-ais_turn_rate(-rate));
    }
    return((int)(4.733 * sqrt(rate * RAD_2_DEG * .0001 * 60.0)));
}


static double ais_direction(unsigned int val, double scale)
{
    if ((val == 0xffff) && (scale == 1.0)) {
        return(511.0);
    }
    return(val * RAD_2_DEG * 0.0001 * scale);
}


/*
 *   PGN 59392: ISO  Acknowledgment
 */
static gps_mask_t hnd_059392(unsigned char *bu, int len, PGN *pgn, struct gps_device_t *session)
{
    print_data(session->context, bu, len, pgn);
    gpsd_log(&session->context->errout, LOG_DATA,
	     "pgn %6d(%3d):\n", pgn->pgn, session->driver.nmea2000.unit);
    return(0);
}


/*
 *   PGN 60928: ISO  Address Claim
 */
static gps_mask_t hnd_060928(unsigned char *bu, int len, PGN *pgn, struct gps_device_t *session)
{
    print_data(session->context, bu, len, pgn);
    gpsd_log(&session->context->errout, LOG_DATA,
	     "pgn %6d(%3d):\n", pgn->pgn, session->driver.nmea2000.unit);
    return(0);
}


/*
 *   PGN 126208: NMEA Command/Request/Acknowledge
 */
static gps_mask_t hnd_126208(unsigned char *bu, int len, PGN *pgn, struct gps_device_t *session)
{
    print_data(session->context, bu, len, pgn);
    gpsd_log(&session->context->errout, LOG_DATA,
	     "pgn %6d(%3d):\n", pgn->pgn, session->driver.nmea2000.unit);
    return(0);
}


/*
 *   PGN 126464: ISO Transmit/Receive PGN List
 */
static gps_mask_t hnd_126464(unsigned char *bu, int len, PGN *pgn, struct gps_device_t *session)
{
    print_data(session->context, bu, len, pgn);
    gpsd_log(&session->context->errout, LOG_DATA,
	     "pgn %6d(%3d):\n", pgn->pgn, session->driver.nmea2000.unit);
    return(0);
}


/*
 *   PGN 126996: ISO  Product Information
 */
static gps_mask_t hnd_126996(unsigned char *bu, int len, PGN *pgn, struct gps_device_t *session)
{
    print_data(session->context, bu, len, pgn);
    gpsd_log(&session->context->errout, LOG_DATA,
	     "pgn %6d(%3d):\n", pgn->pgn, session->driver.nmea2000.unit);
    return(0);
}


/*
 *   PGN 127258: GNSS Magnetic Variation
 */
static gps_mask_t hnd_127258(unsigned char *bu, int len, PGN *pgn, struct gps_device_t *session)
{
    print_data(session->context, bu, len, pgn);
    gpsd_log(&session->context->errout, LOG_DATA,
	     "pgn %6d(%3d):\n", pgn->pgn, session->driver.nmea2000.unit);
    return(0);
}


/*
 *   PGN 129025: GNSS Position Rapid Update
 */
static gps_mask_t hnd_129025(unsigned char *bu, int len, PGN *pgn, struct gps_device_t *session)
{
    print_data(session->context, bu, len, pgn);
    gpsd_log(&session->context->errout, LOG_DATA,
	     "pgn %6d(%3d):\n", pgn->pgn, session->driver.nmea2000.unit);

    session->newdata.latitude = getles32(bu, 0) * 1e-7;
    session->newdata.longitude = getles32(bu, 4) * 1e-7;

    return LATLON_SET | get_mode(session);
}


/*
 *   PGN 129026: GNSS COG and SOG Rapid Update
 */
static gps_mask_t hnd_129026(unsigned char *bu, int len, PGN *pgn, struct gps_device_t *session)
{
    print_data(session->context, bu, len, pgn);
    gpsd_log(&session->context->errout, LOG_DATA,
	     "pgn %6d(%3d):\n", pgn->pgn, session->driver.nmea2000.unit);

    session->driver.nmea2000.sid[0]  =  bu[0];

    session->newdata.track           =  getleu16(bu, 2) * 1e-4 * RAD_2_DEG;
    session->newdata.speed           =  getleu16(bu, 4) * 1e-2;

    return SPEED_SET | TRACK_SET | get_mode(session);
}


/*
 *   PGN 126992: GNSS System Time
 */
static gps_mask_t hnd_126992(unsigned char *bu, int len, PGN *pgn, struct gps_device_t *session)
{
    //uint8_t        sid;
    //uint8_t        source;

    print_data(session->context, bu, len, pgn);
    gpsd_log(&session->context->errout, LOG_DATA,
	     "pgn %6d(%3d):\n", pgn->pgn, session->driver.nmea2000.unit);

    //sid        = bu[0];
    //source     = bu[1] & 0x0f;

    session->newdata.time = getleu16(bu, 2)*24*60*60 + getleu32(bu, 4)/1e4;

    return TIME_SET | get_mode(session);
}


static const int mode_tab[] = {MODE_NO_FIX, MODE_2D,  MODE_3D, MODE_NO_FIX,
			       MODE_NO_FIX, MODE_NO_FIX, MODE_NO_FIX, MODE_NO_FIX};

/*
 *   PGN 129539: GNSS DOPs
 */
static gps_mask_t hnd_129539(unsigned char *bu, int len, PGN *pgn, struct gps_device_t *session)
{
    gps_mask_t mask;
    unsigned int req_mode;
    unsigned int act_mode;

    print_data(session->context, bu, len, pgn);
    gpsd_log(&session->context->errout, LOG_DATA,
	     "pgn %6d(%3d):\n", pgn->pgn, session->driver.nmea2000.unit);

    mask                             = 0;
    session->driver.nmea2000.sid[1]  = bu[0];

    session->driver.nmea2000.mode_valid |= 1;

    req_mode = (unsigned int)((bu[1] >> 0) & 0x07);
    act_mode = (unsigned int)((bu[1] >> 3) & 0x07);

    /* This is a workaround for some GARMIN plotter, actual mode auto makes no sense for me! */
    if ((act_mode == 3) && (req_mode != 3)) {
        act_mode = req_mode;
    }

    session->driver.nmea2000.mode    = mode_tab[act_mode];

    session->gpsdata.dop.hdop        = getleu16(bu, 2) * 1e-2;
    session->gpsdata.dop.vdop        = getleu16(bu, 4) * 1e-2;
    session->gpsdata.dop.tdop        = getleu16(bu, 6) * 1e-2;
    mask                            |= DOP_SET;

    gpsd_log(&session->context->errout, LOG_DATA,
	     "pgn %6d(%3d): sid:%02x hdop:%5.2f vdop:%5.2f tdop:%5.2f\n",
	     pgn->pgn,
	     session->driver.nmea2000.unit,
	     session->driver.nmea2000.sid[1],
	     session->gpsdata.dop.hdop,
	     session->gpsdata.dop.vdop,
	     session->gpsdata.dop.tdop);

    return mask | get_mode(session);
}


/*
 *   PGN 129540: GNSS Satellites in View
 */
static gps_mask_t hnd_129540(unsigned char *bu, int len, PGN *pgn, struct gps_device_t *session)
{
    int         l1;

    print_data(session->context, bu, len, pgn);
    gpsd_log(&session->context->errout, LOG_DATA,
	     "pgn %6d(%3d):\n", pgn->pgn, session->driver.nmea2000.unit);

    session->driver.nmea2000.sid[2]           = bu[0];
    session->gpsdata.satellites_visible       = (int)bu[2];

    memset(session->gpsdata.skyview, '\0', sizeof(session->gpsdata.skyview));
    for (l1=0;l1<session->gpsdata.satellites_visible;l1++) {
        int    svt;
        double azi, elev, snr;

        elev  = getles16(bu, 3+12*l1+1) * 1e-4 * RAD_2_DEG;
        azi   = getleu16(bu, 3+12*l1+3) * 1e-4 * RAD_2_DEG;
        snr   = getles16(bu, 3+12*l1+5) * 1e-2;

        svt   = (int)(bu[3+12*l1+11] & 0x0f);

        session->gpsdata.skyview[l1].elevation  = (short) (round(elev));
	session->gpsdata.skyview[l1].azimuth    = (short) (round(azi));
        session->gpsdata.skyview[l1].ss         = snr;
        session->gpsdata.skyview[l1].PRN        = (short)bu[3+12*l1+0];
	session->gpsdata.skyview[l1].used = false;
	if ((svt == 2) || (svt == 5)) {
	    session->gpsdata.skyview[l1].used = true;
	}
    }
    session->driver.nmea2000.mode_valid |= 2;
    return  SATELLITE_SET | USED_IS;
}


/*
 *   PGN 129029: GNSS Positition Data
 */
static gps_mask_t hnd_129029(unsigned char *bu, int len, PGN *pgn, struct gps_device_t *session)
{
    gps_mask_t mask;

    print_data(session->context, bu, len, pgn);
    gpsd_log(&session->context->errout, LOG_DATA,
	     "pgn %6d(%3d):\n", pgn->pgn, session->driver.nmea2000.unit);

    mask                             = 0;
    session->driver.nmea2000.sid[3]  = bu[0];

    session->newdata.time            = getleu16(bu,1) * 24*60*60 + getleu32(bu, 3)/1e4;
    mask                            |= TIME_SET;

    session->newdata.latitude        = getles64(bu, 7) * 1e-16;
    session->newdata.longitude       = getles64(bu, 15) * 1e-16;
    mask                            |= LATLON_SET;

    session->newdata.altitude        = getles64(bu, 23) * 1e-6;
    mask                            |= ALTITUDE_SET;

//  printf("mode %x %x\n", (bu[31] >> 4) & 0x0f, bu[31]);
    switch ((bu[31] >> 4) & 0x0f) {
    case 0:
        session->gpsdata.status      = STATUS_NO_FIX;
	break;
    case 1:
        session->gpsdata.status      = STATUS_FIX;
	break;
    case 2:
        session->gpsdata.status      = STATUS_DGPS_FIX;
	break;
    case 3:
    case 4:
    case 5:
        session->gpsdata.status      = STATUS_FIX; /* Is this correct ? */
	break;
    default:
        session->gpsdata.status      = STATUS_NO_FIX;
	break;
    }
    mask                            |= STATUS_SET;

    session->gpsdata.separation      = getles32(bu, 38) / 100.0;
    session->newdata.altitude       -= session->gpsdata.separation;

    session->gpsdata.satellites_used = (int)bu[33];

    session->gpsdata.dop.hdop        = getleu16(bu, 34) * 0.01;
    session->gpsdata.dop.pdop        = getleu16(bu, 36) * 0.01;
    mask                            |= DOP_SET;

    return mask | get_mode(session);
}


/*
 *   PGN 129038: AIS  Class A Position Report
 */
static gps_mask_t hnd_129038(unsigned char *bu, int len, PGN *pgn, struct gps_device_t *session)
{
    struct ais_t *ais;

    ais =  &session->gpsdata.ais;
    print_data(session->context, bu, len, pgn);
    gpsd_log(&session->context->errout, LOG_DATA,
	     "pgn %6d(%3d):\n", pgn->pgn, session->driver.nmea2000.unit);

    if (decode_ais_header(session->context, bu, len, ais, 0xffffffffU) != 0) {
        ais->type1.lon       = (int)          scale_int(getles32(bu, 5), (int64_t)(SHIFT32 *.06L));
	ais->type1.lat       = (int)          scale_int(getles32(bu, 9), (int64_t)(SHIFT32 *.06L));
	ais->type1.accuracy  = (bool)         ((bu[13] >> 0) & 0x01);
	ais->type1.raim      = (bool)         ((bu[13] >> 1) & 0x01);
	ais->type1.second    = (unsigned int) ((bu[13] >> 2) & 0x3f);
	ais->type1.course    = (unsigned int)  ais_direction((unsigned int)getleu16(bu, 14), 10.0);
	ais->type1.speed     = (unsigned int) (getleu16(bu, 16) * MPS_TO_KNOTS * 0.01 / 0.1);
	ais->type1.radio     = (unsigned int) (getleu32(bu, 18) & 0x7ffff);
	ais->type1.heading   = (unsigned int)  ais_direction((unsigned int)getleu16(bu, 21), 1.0);
	ais->type1.turn      =                 ais_turn_rate((int)getles16(bu, 23));
	ais->type1.status    = (unsigned int) ((bu[25] >> 0) & 0x0f);
	ais->type1.maneuver  = 0; /* Not transmitted ???? */
	decode_ais_channel_info(bu, len, 163, session);

	return(ONLINE_SET | AIS_SET);
    }
    return(0);
}


/*
 *   PGN 129039: AIS  Class B Position Report
 */
static gps_mask_t hnd_129039(unsigned char *bu, int len, PGN *pgn, struct gps_device_t *session)
{
    struct ais_t *ais;

    ais =  &session->gpsdata.ais;
    print_data(session->context, bu, len, pgn);
    gpsd_log(&session->context->errout, LOG_DATA,
	     "pgn %6d(%3d):\n", pgn->pgn, session->driver.nmea2000.unit);

    if (decode_ais_header(session->context, bu, len, ais, 0xffffffffU) != 0) {
        ais->type18.lon      = (int)          scale_int(getles32(bu, 5), (int64_t)(SHIFT32 *.06L));
	ais->type18.lat      = (int)          scale_int(getles32(bu, 9), (int64_t)(SHIFT32 *.06L));
	ais->type18.accuracy = (bool)         ((bu[13] >> 0) & 0x01);
	ais->type18.raim     = (bool)         ((bu[13] >> 1) & 0x01);
	ais->type18.second   = (unsigned int) ((bu[13] >> 2) & 0x3f);
	ais->type18.course   = (unsigned int)  ais_direction((unsigned int) getleu16(bu, 14), 10.0);
	ais->type18.speed    = (unsigned int) (getleu16(bu, 16) * MPS_TO_KNOTS * 0.01 / 0.1);
	ais->type18.radio    = (unsigned int) (getleu32(bu, 18) & 0x7ffff);
	ais->type18.heading  = (unsigned int)  ais_direction((unsigned int) getleu16(bu, 21), 1.0);
	ais->type18.reserved = 0;
	ais->type18.regional = (unsigned int) ((bu[24] >> 0) & 0x03);
	ais->type18.cs	     = (bool)         ((bu[24] >> 2) & 0x01);
	ais->type18.display  = (bool)         ((bu[24] >> 3) & 0x01);
	ais->type18.dsc      = (bool)         ((bu[24] >> 4) & 0x01);
	ais->type18.band     = (bool)         ((bu[24] >> 5) & 0x01);
	ais->type18.msg22    = (bool)         ((bu[24] >> 6) & 0x01);
	ais->type18.assigned = (bool)         ((bu[24] >> 7) & 0x01);
	decode_ais_channel_info(bu, len, 163, session);

	return(ONLINE_SET | AIS_SET);
    }
    return(0);
}


/*
 *   PGN 129040: AIS Class B Extended Position Report
 */
/* No test case for this message at the moment */
static gps_mask_t hnd_129040(unsigned char *bu, int len, PGN *pgn, struct gps_device_t *session)
{
    struct ais_t *ais;

    ais =  &session->gpsdata.ais;
    print_data(session->context, bu, len, pgn);
    gpsd_log(&session->context->errout, LOG_DATA,
	     "pgn %6d(%3d):\n", pgn->pgn, session->driver.nmea2000.unit);

    if (decode_ais_header(session->context, bu, len, ais, 0xffffffffU) != 0) {
        uint16_t length, beam, to_bow, to_starboard;
	int l;

        ais->type19.lon          = (int)          scale_int(getles32(bu, 5), (int64_t)(SHIFT32 *.06L));
	ais->type19.lat          = (int)          scale_int(getles32(bu, 9), (int64_t)(SHIFT32 *.06L));
	ais->type19.accuracy     = (bool)         ((bu[13] >> 0) & 0x01);
	ais->type19.raim         = (bool)         ((bu[13] >> 1) & 0x01);
	ais->type19.second       = (unsigned int) ((bu[13] >> 2) & 0x3f);
	ais->type19.course       = (unsigned int)  ais_direction((unsigned int) getleu16(bu, 14), 10.0);
	ais->type19.speed        = (unsigned int) (getleu16(bu, 16) * MPS_TO_KNOTS * 0.01 / 0.1);
	ais->type19.reserved     = (unsigned int) ((bu[18] >> 0) & 0xff);
	ais->type19.regional     = (unsigned int) ((bu[19] >> 0) & 0x0f);
	ais->type19.shiptype     = (unsigned int) ((bu[20] >> 0) & 0xff);
	ais->type19.heading      = (unsigned int)  ais_direction((unsigned int) getleu16(bu, 21), 1.0);
	length                   =                 getleu16(bu, 24);
	beam                     =                 getleu16(bu, 26);
        to_starboard             =                 getleu16(bu, 28);
        to_bow                   =                 getleu16(bu, 30);
	if ((length == 0xffff) || (to_bow       == 0xffff)) {
	    length       = 0;
	    to_bow       = 0;
	}
	if ((beam   == 0xffff) || (to_starboard == 0xffff)) {
	    beam         = 0;
	    to_starboard = 0;
	}
	ais->type19.to_bow       = (unsigned int) (to_bow/10);
	ais->type19.to_stern     = (unsigned int) ((length-to_bow)/10);
	ais->type19.to_port      = (unsigned int) ((beam-to_starboard)/10);
	ais->type19.to_starboard = (unsigned int) (to_starboard/10);
	ais->type19.epfd         = (unsigned int) ((bu[23] >> 4) & 0x0f);
	ais->type19.dte          = (unsigned int) ((bu[52] >> 0) & 0x01);
	ais->type19.assigned     = (bool)         ((bu[52] >> 1) & 0x01);
	for (l=0;l<AIS_SHIPNAME_MAXLEN;l++) {
	    ais->type19.shipname[l] = (char) bu[32+l];
	}
	ais->type19.shipname[AIS_SHIPNAME_MAXLEN] = (char) 0;
	decode_ais_channel_info(bu, len, 422, session);

	return(ONLINE_SET | AIS_SET);
    }
    return(0);
}


/*
 *   PGN 129793: AIS UTC and Date Report
 */
static gps_mask_t hnd_129793(unsigned char *bu, int len, PGN *pgn, struct gps_device_t *session)
{
    struct ais_t *ais;

    ais =  &session->gpsdata.ais;
    print_data(session->context, bu, len, pgn);
    gpsd_log(&session->context->errout, LOG_DATA,
	     "pgn %6d(%3d):\n", pgn->pgn, session->driver.nmea2000.unit);

    if (decode_ais_header(session->context, bu, len, ais, 0xffffffffU) != 0) {
        uint32_t  time;
        uint32_t  date;
	time_t    date1;
        struct tm date2;

        ais->type4.lon          = (int)          scale_int(getles32(bu, 5), (int64_t)(SHIFT32 *.06L));
	ais->type4.lat          = (int)          scale_int(getles32(bu, 9), (int64_t)(SHIFT32 *.06L));
	ais->type4.accuracy     = (bool)         ((bu[13] >> 0) & 0x01);
	ais->type4.raim         = (bool)         ((bu[13] >> 1) & 0x01);

	time = getleu32(bu, 14);
	if (time != 0xffffffff) {
	    time                = time / 10000;
	    ais->type4.second   = time % 60; time = time / 60;
	    ais->type4.minute   = time % 60; time = time / 60;
	    ais->type4.hour     = time % 24;
	} else {
	    ais->type4.second   = AIS_SECOND_NOT_AVAILABLE;
	    ais->type4.minute   = AIS_MINUTE_NOT_AVAILABLE;
	    ais->type4.hour     = AIS_HOUR_NOT_AVAILABLE;
	}

        ais->type4.radio        = (unsigned int) (getleu32(bu, 18) & 0x7ffff);

	date = getleu16(bu, 21);
	if (date != 0xffff) {
	    date1 = (time_t)date * (24L *60L *60L);
	    (void) gmtime_r(&date1, &date2);
            ais->type4.year     = (unsigned int) (date2.tm_year+1900);
            ais->type4.month    = (unsigned int) (date2.tm_mon+1);
	    ais->type4.day      = (unsigned int) (date2.tm_mday);
	} else {
	    ais->type4.day      = AIS_DAY_NOT_AVAILABLE;
	    ais->type4.month    = AIS_MONTH_NOT_AVAILABLE;
	    ais->type4.year     = AIS_YEAR_NOT_AVAILABLE;
	}

	ais->type4.epfd         = (unsigned int) ((bu[23] >> 4) & 0x0f);

        decode_ais_channel_info(bu, len, 163, session);

	return(ONLINE_SET | AIS_SET);
    }
    return(0);
}


/*
 *   PGN 129794: AIS Class A Static and Voyage Related Data
 */
static gps_mask_t hnd_129794(unsigned char *bu, int len, PGN *pgn, struct gps_device_t *session)
{
    struct ais_t *ais;

    ais =  &session->gpsdata.ais;
    print_data(session->context, bu, len, pgn);
    gpsd_log(&session->context->errout, LOG_DATA,
	     "pgn %6d(%3d):\n", pgn->pgn, session->driver.nmea2000.unit);

    if (decode_ais_header(session->context, bu, len, ais, 0xffffffffU) != 0) {
        uint16_t  length, beam, to_bow, to_starboard, date;
	int       l;
	uint32_t  time;
	time_t    date1;
        struct tm date2;
        int       cpy_stop;

        ais->type5.ais_version   = (unsigned int) ((bu[73] >> 0) & 0x03);
	ais->type5.imo           = (unsigned int)  getleu32(bu,  5);
	if (ais->type5.imo == 0xffffffffU) {
	    ais->type5.imo       = 0;
	}
	ais->type5.shiptype      = (unsigned int) ((bu[36] >> 0) & 0xff);
	length                   =                 getleu16(bu, 37);
	beam                     =                 getleu16(bu, 39);
        to_starboard             =                 getleu16(bu, 41);
        to_bow                   =                 getleu16(bu, 43);
	if ((length == 0xffff) || (to_bow       == 0xffff)) {
	    length       = 0;
	    to_bow       = 0;
	}
	if ((beam   == 0xffff) || (to_starboard == 0xffff)) {
	    beam         = 0;
	    to_starboard = 0;
	}
	ais->type5.to_bow        = (unsigned int) (to_bow/10);
	ais->type5.to_stern      = (unsigned int) ((length-to_bow)/10);
	ais->type5.to_port       = (unsigned int) ((beam-to_starboard)/10);
	ais->type5.to_starboard  = (unsigned int) (to_starboard/10);
	ais->type5.epfd          = (unsigned int) ((bu[73] >> 2) & 0x0f);
	date                     =                 getleu16(bu, 45);
	time                     =                 getleu32(bu, 47);
        date1                    = (time_t)       (date*24*60*60);
	(void) gmtime_r(&date1, &date2);
	ais->type5.month         = (unsigned int) (date2.tm_mon+1);
	ais->type5.day           = (unsigned int) (date2.tm_mday);
	ais->type5.minute        = (unsigned int) (time/(10000*60));
	ais->type5.hour          = (unsigned int) (ais->type5.minute/60);
	ais->type5.minute        = (unsigned int) (ais->type5.minute-(ais->type5.hour*60));

	ais->type5.draught       = (unsigned int) (getleu16(bu, 51)/10);
	ais->type5.dte           = (unsigned int) ((bu[73] >> 6) & 0x01);

	for (l=0,cpy_stop=0;l<7;l++) {
            char next;

	    next = (char) bu[9+l];
	    if ((next < ' ') || (next > 0x7e)) {
	        cpy_stop = 1;
	    }
	    if (cpy_stop == 0) {
	        ais->type5.callsign[l] = next;
	    } else {
	        ais->type5.callsign[l] = 0;
	    }
	}
	ais->type5.callsign[7]   = (char) 0;

	for (l=0,cpy_stop=0;l<AIS_SHIPNAME_MAXLEN;l++) {
	    char next;

	    next = (char) bu[16+l];
	    if ((next < ' ') || (next > 0x7e)) {
	        cpy_stop = 1;
	    }
	    if (cpy_stop == 0) {
	        ais->type5.shipname[l] = next;
	    } else {
	        ais->type5.shipname[l] = 0;
	    }
	}
	ais->type5.shipname[AIS_SHIPNAME_MAXLEN] = (char) 0;

	for (l=0,cpy_stop=0;l<20;l++) {
            char next;

	    next = (char) bu[53+l];
	    if ((next < ' ') || (next > 0x7e)) {
	        cpy_stop = 1;
	    }
	    if (cpy_stop == 0) {
	        ais->type5.destination[l] = next;
	    } else {
	        ais->type5.destination[l] = 0;
	    }
	}
	ais->type5.destination[20] = (char) 0;
#if NMEA2000_DEBUG_AIS
	printf("AIS: MMSI:  %09u\n",
	       ais->mmsi);
	printf("AIS: name:  %-20.20s i:%8u c:%-8.8s b:%6u s:%6u p:%6u s:%6u dr:%4.1f\n",
	       ais->type5.shipname,
	       ais->type5.imo,
	       ais->type5.callsign,
	       ais->type5.to_bow,
	       ais->type5.to_stern,
	       ais->type5.to_port,
	       ais->type5.to_starboard,
	       ais->type5.draught/10.0);
	printf("AIS: arival:%-20.20s at %02u-%02u-%04d %02u:%0u\n",
	       ais->type5.destination,
	       ais->type5.day,
	       ais->type5.month,
	       date2.tm_year+1900,
	       ais->type5.hour,
	       ais->type5.minute);
#endif /* of #if NMEA2000_DEBUG_AIS */
	decode_ais_channel_info(bu, len, 592, session);
        return(ONLINE_SET | AIS_SET);
    }
    return(0);
}


/*
 *   PGN 129798: AIS SAR Aircraft Position Report
 */
/* No test case for this message at the moment */
static gps_mask_t hnd_129798(unsigned char *bu, int len, PGN *pgn, struct gps_device_t *session)
{
    struct ais_t *ais;

    ais =  &session->gpsdata.ais;
    print_data(session->context, bu, len, pgn);
    gpsd_log(&session->context->errout, LOG_DATA,
	     "pgn %6d(%3d):\n", pgn->pgn, session->driver.nmea2000.unit);

    if (decode_ais_header(session->context, bu, len, ais, 0xffffffffU) != 0) {
        ais->type9.lon       = (int)          scale_int(getles32(bu, 5), (int64_t)(SHIFT32 *.06L));
	ais->type9.lat       = (int)          scale_int(getles32(bu, 9), (int64_t)(SHIFT32 *.06L));
	ais->type9.accuracy  = (bool)         ((bu[13] >> 0) & 0x01);
	ais->type9.raim      = (bool)         ((bu[13] >> 1) & 0x01);
	ais->type9.second    = (unsigned int) ((bu[13] >> 2) & 0x3f);
	ais->type9.course    = (unsigned int)  ais_direction((unsigned int) getleu16(bu, 14), 10.0);
	ais->type9.speed     = (unsigned int) (getleu16(bu, 16) * MPS_TO_KNOTS * 0.01 / 0.1);
	ais->type9.radio     = (unsigned int) (getleu32(bu, 18) & 0x7ffff);
	ais->type9.alt       = (unsigned int) (getleu64(bu, 21)/1000000);
	ais->type9.regional  = (unsigned int) ((bu[29] >> 0) & 0xff);
	ais->type9.dte	     = (unsigned int) ((bu[30] >> 0) & 0x01);
/*      ais->type9.spare     = (bu[30] >> 1) & 0x7f; */
	ais->type9.assigned  = 0; /* Not transmitted ???? */
	decode_ais_channel_info(bu, len, 163, session);

        return(ONLINE_SET | AIS_SET);
    }
    return(0);
}


/*
 *   PGN 129802: AIS Safty Related Broadcast Message
 */
/* No test case for this message at the moment */
static gps_mask_t hnd_129802(unsigned char *bu, int len, PGN *pgn, struct gps_device_t *session)
{
    struct ais_t *ais;

    ais =  &session->gpsdata.ais;
    print_data(session->context, bu, len, pgn);
    gpsd_log(&session->context->errout, LOG_DATA,
	     "pgn %6d(%3d):\n", pgn->pgn, session->driver.nmea2000.unit);

    if (decode_ais_header(session->context, bu, len, ais, 0x3fffffff) != 0) {
        int                   l;

/*      ais->type14.channel = (bu[ 5] >> 0) & 0x1f; */
	for (l=0;l<36;l++) {
	    ais->type14.text[l] = (char) bu[6+l];
	}
	ais->type14.text[36] = (char) 0;
	decode_ais_channel_info(bu, len, 40, session);

        return(ONLINE_SET | AIS_SET);
    }
    return(0);
}


/*
 *   PGN 129809: AIS Class B CS Static Data Report, Part A
 */
static gps_mask_t hnd_129809(unsigned char *bu, int len, PGN *pgn, struct gps_device_t *session)
{
    struct ais_t *ais;

    ais =  &session->gpsdata.ais;
    print_data(session->context, bu, len, pgn);
    gpsd_log(&session->context->errout, LOG_DATA,
	     "pgn %6d(%3d):\n", pgn->pgn, session->driver.nmea2000.unit);

    if (decode_ais_header(session->context, bu, len, ais, 0xffffffffU) != 0) {
        int                   l;
	int                   index   = session->driver.aivdm.context[0].type24_queue.index;
	struct ais_type24a_t *saveptr = &session->driver.aivdm.context[0].type24_queue.ships[index];

	gpsd_log(&session->context->errout, LOG_PROG,
		 "NMEA2000: AIS message 24A from %09u stashed.\n",
		 ais->mmsi);

	for (l=0;l<AIS_SHIPNAME_MAXLEN;l++) {
	    ais->type24.shipname[l] = (char) bu[ 5+l];
	    saveptr->shipname[l] = (char) bu[ 5+l];
	}
	ais->type24.shipname[AIS_SHIPNAME_MAXLEN] = (char) 0;
	saveptr->shipname[AIS_SHIPNAME_MAXLEN] = (char) 0;
	
	saveptr->mmsi = ais->mmsi;

	index += 1;
	index %= MAX_TYPE24_INTERLEAVE;
	session->driver.aivdm.context[0].type24_queue.index = index;

	decode_ais_channel_info(bu, len, 200, session);

	ais->type24.part = part_a;
	return(ONLINE_SET | AIS_SET);
    }
    return(0);
}


/*
 *   PGN 129810: AIS Class B CS Static Data Report, Part B
 */
static gps_mask_t hnd_129810(unsigned char *bu, int len, PGN *pgn, struct gps_device_t *session)
{
    struct ais_t *ais;

    ais =  &session->gpsdata.ais;
    print_data(session->context, bu, len, pgn);
    gpsd_log(&session->context->errout, LOG_DATA,
	     "pgn %6d(%3d):\n", pgn->pgn, session->driver.nmea2000.unit);

    if (decode_ais_header(session->context, bu, len, ais, 0xffffffffU) != 0) {
        int l, i;

	ais->type24.shiptype = (unsigned int) ((bu[ 5] >> 0) & 0xff);

	for (l=0;l<7;l++) {
	    ais->type24.vendorid[l] = (char) bu[ 6+l];
	}
	ais->type24.vendorid[7] = (char) 0;

	for (l=0;l<7;l++) {
	    ais->type24.callsign[l] = (char) bu[13+l];
	}
	ais->type24.callsign[7] = (char )0;

	ais->type24.model = 0;
	ais->type24.serial = 0;

	if (AIS_AUXILIARY_MMSI(ais->mmsi)) {
	    ais->type24.mothership_mmsi   = (unsigned int) (getleu32(bu, 28));
	} else {
	    uint16_t length, beam, to_bow, to_starboard;

	    length                        =                 getleu16(bu, 20);
	    beam                          =                 getleu16(bu, 22);
	    to_starboard                  =                 getleu16(bu, 24);
	    to_bow                        =                 getleu16(bu, 26);
	    if ((length == 0xffff) || (to_bow       == 0xffff)) {
	        length       = 0;
		to_bow       = 0;
	    }
	    if ((beam   == 0xffff) || (to_starboard == 0xffff)) {
	        beam         = 0;
		to_starboard = 0;
	    }
	    ais->type24.dim.to_bow        = (unsigned int) (to_bow/10);
	    ais->type24.dim.to_stern      = (unsigned int) ((length-to_bow)/10);
	    ais->type24.dim.to_port       = (unsigned int) ((beam-to_starboard)/10);
	    ais->type24.dim.to_starboard  = (unsigned int) (to_starboard/10);
	}

	for (i = 0; i < MAX_TYPE24_INTERLEAVE; i++) {
	    if (session->driver.aivdm.context[0].type24_queue.ships[i].mmsi == ais->mmsi) {
	        for (l=0;l<AIS_SHIPNAME_MAXLEN;l++) {
		    ais->type24.shipname[l] = (char)(session->driver.aivdm.context[0].type24_queue.ships[i].shipname[l]);
		}
		ais->type24.shipname[AIS_SHIPNAME_MAXLEN] = (char) 0;

		gpsd_log(&session->context->errout, LOG_PROG,
			 "NMEA2000: AIS 24B from %09u matches a 24A.\n",
			    ais->mmsi);
		/* prevent false match if a 24B is repeated */
		session->driver.aivdm.context[0].type24_queue.ships[i].mmsi = 0;
#if NMEA2000_DEBUG_AIS
		printf("AIS: MMSI:  %09u\n", ais->mmsi);
		printf("AIS: name:  %-20.20s v:%-8.8s c:%-8.8s b:%6u s:%6u p:%6u s:%6u\n",
		       ais->type24.shipname,
		       ais->type24.vendorid,
		       ais->type24.callsign,
		       ais->type24.dim.to_bow,
		       ais->type24.dim.to_stern,
		       ais->type24.dim.to_port,
		       ais->type24.dim.to_starboard);
#endif /* of #if NMEA2000_DEBUG_AIS */

		decode_ais_channel_info(bu, len, 264, session);
		ais->type24.part = both;
		return(ONLINE_SET | AIS_SET);
	    }
	}
#if NMEA2000_DEBUG_AIS
	printf("AIS: MMSI  :  %09u\n", ais->mmsi);
	printf("AIS: vendor:  %-8.8s c:%-8.8s b:%6u s:%6u p:%6u s:%6u\n",
	       ais->type24.vendorid,
	       ais->type24.callsign,
	       ais->type24.dim.to_bow,
	       ais->type24.dim.to_stern,
	       ais->type24.dim.to_port,
	       ais->type24.dim.to_starboard);
#endif /* of #if NMEA2000_DEBUG_AIS */
	decode_ais_channel_info(bu, len, 264, session);
	ais->type24.part = part_b;
	return(ONLINE_SET | AIS_SET);
    }
    return(0);
}


/*
 *   PGN 127506: PWR DC Detailed Status
 */
static gps_mask_t hnd_127506(unsigned char *bu, int len, PGN *pgn, struct gps_device_t *session)
{
    print_data(session->context, bu, len, pgn);
    gpsd_log(&session->context->errout, LOG_DATA,
	     "pgn %6d(%3d):\n", pgn->pgn, session->driver.nmea2000.unit);
    return(0);
}


/*
 *   PGN 127508: PWR Battery Status
 */
static gps_mask_t hnd_127508(unsigned char *bu, int len, PGN *pgn, struct gps_device_t *session)
{
    print_data(session->context, bu, len, pgn);
    gpsd_log(&session->context->errout, LOG_DATA,
	     "pgn %6d(%3d):\n", pgn->pgn, session->driver.nmea2000.unit);
    return(0);
}


/*
 *   PGN 127513: PWR Battery Configuration Status
 */
static gps_mask_t hnd_127513(unsigned char *bu, int len, PGN *pgn, struct gps_device_t *session)
{
    print_data(session->context, bu, len, pgn);
    gpsd_log(&session->context->errout, LOG_DATA,
	     "pgn %6d(%3d):\n", pgn->pgn, session->driver.nmea2000.unit);
    return(0);
}


/*
 *   PGN 127245: NAV Rudder
 */
static gps_mask_t hnd_127245(unsigned char *bu, int len, PGN *pgn, struct gps_device_t *session)
{
    print_data(session->context, bu, len, pgn);
    gpsd_log(&session->context->errout, LOG_DATA,
	     "pgn %6d(%3d):\n", pgn->pgn, session->driver.nmea2000.unit);
    return(0);
}


/*
 *   PGN 127250: NAV Vessel Heading
 */
static gps_mask_t hnd_127250(unsigned char *bu, int len, PGN *pgn, struct gps_device_t *session)
{
    int aux;

    print_data(session->context, bu, len, pgn);

    session->gpsdata.attitude.heading = getleu16(bu, 1) * RAD_2_DEG * 0.0001;
//  printf("ATT 0:%8.3f\n",session->gpsdata.attitude.heading);
    aux = getles16(bu, 3);
    if (aux != 0x07fff) {
        session->gpsdata.attitude.heading += aux * RAD_2_DEG * 0.0001;
    }
//  printf("ATT 1:%8.3f %6x\n",session->gpsdata.attitude.heading, aux);
    aux = getles16(bu, 5);
    if (aux != 0x07fff) {
        session->gpsdata.attitude.heading += aux * RAD_2_DEG * 0.0001;
    }
//  printf("ATT 2:%8.3f %6x\n",session->gpsdata.attitude.heading, aux);
    session->gpsdata.attitude.mag_st = '\0';
    session->gpsdata.attitude.pitch = NAN;
    session->gpsdata.attitude.pitch_st = '\0';
    session->gpsdata.attitude.roll = NAN;
    session->gpsdata.attitude.roll_st = '\0';
    session->gpsdata.attitude.yaw = NAN;
    session->gpsdata.attitude.yaw_st = '\0';
    session->gpsdata.attitude.dip = NAN;
    session->gpsdata.attitude.mag_len = NAN;
    session->gpsdata.attitude.mag_x = NAN;
    session->gpsdata.attitude.mag_y = NAN;
    session->gpsdata.attitude.mag_z = NAN;
    session->gpsdata.attitude.acc_len = NAN;
    session->gpsdata.attitude.acc_x = NAN;
    session->gpsdata.attitude.acc_y = NAN;
    session->gpsdata.attitude.acc_z = NAN;
    session->gpsdata.attitude.gyro_x = NAN;
    session->gpsdata.attitude.gyro_y = NAN;
    session->gpsdata.attitude.temp = NAN;
    session->gpsdata.attitude.depth = NAN;

    gpsd_log(&session->context->errout, LOG_DATA,
	     "pgn %6d(%3d):\n", pgn->pgn, session->driver.nmea2000.unit);
    return(ONLINE_SET | ATTITUDE_SET);
}


/*
 *   PGN 128259: NAV Speed
 */
static gps_mask_t hnd_128259(unsigned char *bu, int len, PGN *pgn, struct gps_device_t *session)
{
    print_data(session->context, bu, len, pgn);
    gpsd_log(&session->context->errout, LOG_DATA,
	     "pgn %6d(%3d):\n", pgn->pgn, session->driver.nmea2000.unit);
    return(0);
}


/*
 *   PGN 128267: NAV Water Depth
 */
static gps_mask_t hnd_128267(unsigned char *bu, int len, PGN *pgn, struct gps_device_t *session)
{
    print_data(session->context, bu, len, pgn);

    session->gpsdata.attitude.heading = NAN;
    session->gpsdata.attitude.pitch = NAN;
    session->gpsdata.attitude.pitch_st = '\0';
    session->gpsdata.attitude.roll = NAN;
    session->gpsdata.attitude.roll_st = '\0';
    session->gpsdata.attitude.yaw = NAN;
    session->gpsdata.attitude.yaw_st = '\0';
    session->gpsdata.attitude.dip = NAN;
    session->gpsdata.attitude.mag_len = NAN;
    session->gpsdata.attitude.mag_x = NAN;
    session->gpsdata.attitude.mag_y = NAN;
    session->gpsdata.attitude.mag_z = NAN;
    session->gpsdata.attitude.acc_len = NAN;
    session->gpsdata.attitude.acc_x = NAN;
    session->gpsdata.attitude.acc_y = NAN;
    session->gpsdata.attitude.acc_z = NAN;
    session->gpsdata.attitude.gyro_x = NAN;
    session->gpsdata.attitude.gyro_y = NAN;
    session->gpsdata.attitude.temp = NAN;
    session->gpsdata.attitude.depth = getleu32(bu, 1) *.01;

    gpsd_log(&session->context->errout, LOG_DATA,
	     "pgn %6d(%3d):\n", pgn->pgn, session->driver.nmea2000.unit);
    return(ONLINE_SET | ATTITUDE_SET);
}


/*
 *   PGN 128275: NAV Distance Log
 */
static gps_mask_t hnd_128275(unsigned char *bu, int len, PGN *pgn, struct gps_device_t *session)
{
    print_data(session->context, bu, len, pgn);
    gpsd_log(&session->context->errout, LOG_DATA,
	     "pgn %6d(%3d):\n", pgn->pgn, session->driver.nmea2000.unit);
    return(0);
}


/*
 *   PGN 129283: NAV Cross Track Error
 */
static gps_mask_t hnd_129283(unsigned char *bu, int len, PGN *pgn, struct gps_device_t *session)
{
    print_data(session->context, bu, len, pgn);
    gpsd_log(&session->context->errout, LOG_DATA,
	     "pgn %6d(%3d):\n", pgn->pgn, session->driver.nmea2000.unit);
    return(0);
}


/*
 *   PGN 129284: NAV Navigation Data
 */
static gps_mask_t hnd_129284(unsigned char *bu, int len, PGN *pgn, struct gps_device_t *session)
{
    print_data(session->context, bu, len, pgn);
    gpsd_log(&session->context->errout, LOG_DATA,
	     "pgn %6d(%3d):\n", pgn->pgn, session->driver.nmea2000.unit);
    return(0);
}


/*
 *   PGN 129285: NAV Navigation - Route/WP Information
 */
static gps_mask_t hnd_129285(unsigned char *bu, int len, PGN *pgn, struct gps_device_t *session)
{
    print_data(session->context, bu, len, pgn);
    gpsd_log(&session->context->errout, LOG_DATA,
	     "pgn %6d(%3d):\n", pgn->pgn, session->driver.nmea2000.unit);
    return(0);
}


/*
 *   PGN 130306: NAV Wind Data
 */
static gps_mask_t hnd_130306(unsigned char *bu, int len, PGN *pgn, struct gps_device_t *session)
{
    print_data(session->context, bu, len, pgn);
    gpsd_log(&session->context->errout, LOG_DATA,
	     "pgn %6d(%3d):\n", pgn->pgn, session->driver.nmea2000.unit);
    return(0);
}


/*
 *   PGN 130310: NAV Water Temp., Outside Air Temp., Atmospheric Pressure
 */
static gps_mask_t hnd_130310(unsigned char *bu, int len, PGN *pgn, struct gps_device_t *session)
{
    print_data(session->context, bu, len, pgn);
    gpsd_log(&session->context->errout, LOG_DATA,
	     "pgn %6d(%3d):\n", pgn->pgn, session->driver.nmea2000.unit);
    return(0);
}


/*
 *   PGN 130311: NAV Environmental Parameters
 */
static gps_mask_t hnd_130311(unsigned char *bu, int len, PGN *pgn, struct gps_device_t *session)
{
    print_data(session->context, bu, len, pgn);
    gpsd_log(&session->context->errout, LOG_DATA,
	     "pgn %6d(%3d):\n", pgn->pgn, session->driver.nmea2000.unit);
    return(0);
}


static const char msg_059392[] = {"ISO  Acknowledgment"};
static const char msg_060928[] = {"ISO  Address Claim"};
static const char msg_126208[] = {"NMEA Command/Request/Acknowledge"};
static const char msg_126464[] = {"ISO  Transmit/Receive PGN List"};
static const char msg_126992[] = {"GNSS System Time"};
static const char msg_126996[] = {"ISO  Product Information"};

static const char msg_127506[] = {"PWR DC Detailed Status"};
static const char msg_127508[] = {"PWR Battery Status"};
static const char msg_127513[] = {"PWR Battery Configuration Status"};

static const char msg_127258[] = {"GNSS Magnetic Variation"};
static const char msg_129025[] = {"GNSS Position Rapid Update"};
static const char msg_129026[] = {"GNSS COG and SOG Rapid Update"};
static const char msg_129029[] = {"GNSS Positition Data"};
static const char msg_129539[] = {"GNSS DOPs"};
static const char msg_129540[] = {"GNSS Satellites in View"};

static const char msg_129038[] = {"AIS  Class A Position Report"};
static const char msg_129039[] = {"AIS  Class B Position Report"};
static const char msg_129040[] = {"AIS  Class B Extended Position Report"};
static const char msg_129793[] = {"AIS  UTC and Date report"};
static const char msg_129794[] = {"AIS  Class A Static and Voyage Related Data"};
static const char msg_129798[] = {"AIS  SAR Aircraft Position Report"};
static const char msg_129802[] = {"AIS  Safty Related Broadcast Message"};
static const char msg_129809[] = {"AIS  Class B CS Static Data Report, Part A"};
static const char msg_129810[] = {"AIS  Class B CS Static Data Report, Part B"};

static const char msg_127245[] = {"NAV Rudder"};
static const char msg_127250[] = {"NAV Vessel Heading"};
static const char msg_128259[] = {"NAV Speed"};
static const char msg_128267[] = {"NAV Water Depth"};
static const char msg_128275[] = {"NAV Distance Log"};

static const char msg_129283[] = {"NAV Cross Track Error"};
static const char msg_129284[] = {"NAV Navigation Data"};
static const char msg_129285[] = {"NAV Navigation - Route/WP Information"};

static const char msg_130306[] = {"NAV Wind Data"};
static const char msg_130310[] = {"NAV Water Temp., Outside Air Temp., Atmospheric Pressure"};
static const char msg_130311[] = {"NAV Environmental Parameters"};

static const char msg_error [] = {"**error**"};

static PGN gpspgn[] = {{ 59392, 0, 0, hnd_059392, &msg_059392[0]},
		       { 60928, 0, 0, hnd_060928, &msg_060928[0]},
		       {126208, 0, 0, hnd_126208, &msg_126208[0]},
		       {126464, 1, 0, hnd_126464, &msg_126464[0]},
		       {126992, 0, 0, hnd_126992, &msg_126992[0]},
		       {126996, 1, 0, hnd_126996, &msg_126996[0]},
		       {127258, 0, 0, hnd_127258, &msg_127258[0]},
		       {129025, 0, 1, hnd_129025, &msg_129025[0]},
		       {129026, 0, 1, hnd_129026, &msg_129026[0]},
		       {129029, 1, 1, hnd_129029, &msg_129029[0]},
		       {129283, 0, 0, hnd_129283, &msg_129283[0]},
		       {129284, 1, 0, hnd_129284, &msg_129284[0]},
		       {129285, 1, 0, hnd_129285, &msg_129285[0]},
		       {129539, 0, 1, hnd_129539, &msg_129539[0]},
		       {129540, 1, 1, hnd_129540, &msg_129540[0]},
		       {0     , 0, 0, NULL,       &msg_error [0]}};

static PGN aispgn[] = {{ 59392, 0, 0, hnd_059392, &msg_059392[0]},
		       { 60928, 0, 0, hnd_060928, &msg_060928[0]},
		       {126208, 0, 0, hnd_126208, &msg_126208[0]},
		       {126464, 1, 0, hnd_126464, &msg_126464[0]},
		       {126992, 0, 0, hnd_126992, &msg_126992[0]},
		       {126996, 1, 0, hnd_126996, &msg_126996[0]},
		       {129038, 1, 2, hnd_129038, &msg_129038[0]},
		       {129039, 1, 2, hnd_129039, &msg_129039[0]},
		       {129040, 1, 2, hnd_129040, &msg_129040[0]},
		       {129793, 1, 2, hnd_129793, &msg_129793[0]},
		       {129794, 1, 2, hnd_129794, &msg_129794[0]},
		       {129798, 1, 2, hnd_129798, &msg_129798[0]},
		       {129802, 1, 2, hnd_129802, &msg_129802[0]},
		       {129809, 1, 2, hnd_129809, &msg_129809[0]},
		       {129810, 1, 2, hnd_129810, &msg_129810[0]},
		       {0     , 0, 0, NULL,       &msg_error [0]}};

static PGN pwrpgn[] = {{ 59392, 0, 0, hnd_059392, &msg_059392[0]},
		       { 60928, 0, 0, hnd_060928, &msg_060928[0]},
		       {126208, 0, 0, hnd_126208, &msg_126208[0]},
		       {126464, 1, 0, hnd_126464, &msg_126464[0]},
		       {126992, 0, 0, hnd_126992, &msg_126992[0]},
		       {126996, 1, 0, hnd_126996, &msg_126996[0]},
		       {127506, 1, 3, hnd_127506, &msg_127506[0]},
		       {127508, 1, 3, hnd_127508, &msg_127508[0]},
		       {127513, 1, 3, hnd_127513, &msg_127513[0]},
		       {0     , 0, 0, NULL,       &msg_error [0]}};

static PGN navpgn[] = {{ 59392, 0, 0, hnd_059392, &msg_059392[0]},
		       { 60928, 0, 0, hnd_060928, &msg_060928[0]},
		       {126208, 0, 0, hnd_126208, &msg_126208[0]},
		       {126464, 1, 0, hnd_126464, &msg_126464[0]},
		       {126992, 0, 0, hnd_126992, &msg_126992[0]},
		       {126996, 1, 0, hnd_126996, &msg_126996[0]},
		       {127245, 0, 4, hnd_127245, &msg_127245[0]},
		       {127250, 0, 4, hnd_127250, &msg_127250[0]},
		       {127258, 0, 0, hnd_127258, &msg_127258[0]},
		       {128259, 0, 4, hnd_128259, &msg_128259[0]},
		       {128267, 0, 4, hnd_128267, &msg_128267[0]},
		       {128275, 1, 4, hnd_128275, &msg_128275[0]},
		       {129283, 0, 0, hnd_129283, &msg_129283[0]},
		       {129284, 1, 0, hnd_129284, &msg_129284[0]},
		       {129285, 1, 0, hnd_129285, &msg_129285[0]},
		       {130306, 0, 4, hnd_130306, &msg_130306[0]},
		       {130310, 0, 4, hnd_130310, &msg_130310[0]},
		       {130311, 0, 4, hnd_130311, &msg_130311[0]},
		       {0     , 0, 0, NULL,       &msg_error [0]}};



static PGN *search_pgnlist(unsigned int pgn, PGN *pgnlist)
{
    int l1;
    PGN *work;

    l1 = 0;
    work = NULL;
    while (pgnlist[l1].pgn != 0) {
        if (pgnlist[l1].pgn == pgn) {
	    work = &pgnlist[l1];
	    break;
	} else {
	    l1 = l1 + 1;
	    }
	}
    return work;
}

static void find_pgn(struct can_frame *frame, struct gps_device_t *session)
{
    unsigned int can_net;

    session->driver.nmea2000.workpgn = NULL;
    can_net = session->driver.nmea2000.can_net;
    if (can_net > (NMEA2000_NETS-1)) {
        gpsd_log(&session->context->errout, LOG_ERROR,
		 "NMEA2000 find_pgn: Invalid can network %d.\n", can_net);
        return;
    }

    if (frame->can_id & 0x80000000) {
	// cppcheck-suppress unreadVariable
#ifdef __UNUSED__
	unsigned int source_prio;
	unsigned int daddr;
#endif
	// cppcheck-suppress unreadVariable
	unsigned int source_pgn;
	unsigned int source_unit;

#if LOG_FILE
        if (logFile != NULL) {
	    struct timespec  msgTime;

	    clock_gettime(CLOCK_REALTIME, &msgTime);
	    (void)fprintf(logFile,
	                  "(%010ld.%06ld) can0 %08x#",
	                  (long)msgTime.tv_sec,
	                  msgTime.tv_nsec / 1000,
	                  frame->can_id & 0x1ffffff);
	    if ((frame->can_dlc & 0x0f) > 0) {
		int l1;
	        for(l1=0;l1<(frame->can_dlc & 0x0f);l1++) {
		    (void)fprintf(logFile, "%02x", frame->data[l1]);
		}
	    }
	    (void)fprintf(logFile, "\n");
	}
#endif /* of if LOG_FILE */
	session->driver.nmea2000.can_msgcnt += 1;
	source_pgn = (frame->can_id >> 8) & 0x1ffff;
#ifdef __UNUSED__
	source_prio = (frame->can_id >> 26) & 0x7;
#endif
	source_unit = frame->can_id & 0x0ff;

	if (((source_pgn & 0x0ff00) >> 8) < 240) {
#ifdef __UNUSED__
	    daddr  = source_pgn & 0x000ff;
#endif
	    source_pgn  = source_pgn & 0x1ff00;
	} else {
#ifdef __UNUSED__
	    daddr = 0xff;
#endif
	}

	if (!session->driver.nmea2000.unit_valid) {
	    unsigned int l1, l2;

	    for (l1=0;l1<NMEA2000_NETS;l1++) {
	        for (l2=0;l2<NMEA2000_UNITS;l2++) {
		    if (session == nmea2000_units[l1][l2]) {
		        session->driver.nmea2000.unit = l2;
		        session->driver.nmea2000.unit_valid = true;
			session->driver.nmea2000.can_net = l1;
			can_net = l1;
		    }
		}
	    }
	}

	if (!session->driver.nmea2000.unit_valid) {
	    session->driver.nmea2000.unit = source_unit;
	    session->driver.nmea2000.unit_valid = true;
	    nmea2000_units[can_net][source_unit] = session;
	}

	if (source_unit == session->driver.nmea2000.unit) {
	    PGN *work;
	    if (session->driver.nmea2000.pgnlist != NULL) {
	        work = search_pgnlist(source_pgn, session->driver.nmea2000.pgnlist);
	    } else {
	        PGN *pgnlist;

		pgnlist = &gpspgn[0];
		work = search_pgnlist(source_pgn, pgnlist);
		if (work == NULL) {
		    pgnlist = &aispgn[0];
		    work = search_pgnlist(source_pgn, pgnlist);
		}
		if (work == NULL) {
		    pgnlist = &pwrpgn[0];
		    work = search_pgnlist(source_pgn, pgnlist);
		}
		if (work == NULL) {
		    pgnlist = &navpgn[0];
		    work = search_pgnlist(source_pgn, pgnlist);
		}
		if ((work != NULL) && (work->type > 0)) {
		    session->driver.nmea2000.pgnlist = pgnlist;
		}
	    }
	    if (work != NULL) {
	        if (work->fast == 0) {
		    size_t l2;

		    gpsd_log(&session->context->errout, LOG_DATA,
			     "pgn %6d:%s \n", work->pgn, work->name);
		    session->driver.nmea2000.workpgn = (void *) work;
		    session->lexer.outbuflen =  frame->can_dlc & 0x0f;
		    for (l2=0;l2<session->lexer.outbuflen;l2++) {
		        session->lexer.outbuffer[l2]= frame->data[l2];
		    }
		} else if ((frame->data[0] & 0x1f) == 0) {
		    unsigned int l2;

		    session->driver.nmea2000.fast_packet_len = frame->data[1];
		    session->driver.nmea2000.idx = frame->data[0];
#if NMEA2000_FAST_DEBUG
		    gpsd_log(&session->context->errout, LOG_ERROR,
			     "Set idx    %2x    %2x %2x %6d\n",
			     frame->data[0],
			     session->driver.nmea2000.unit,
			     frame->data[1],
			     source_pgn);
#endif /* of #if NMEA2000_FAST_DEBUG */
		    session->lexer.inbuflen = 0;
		    session->driver.nmea2000.idx += 1;
		    for (l2=2;l2<8;l2++) {
		        session->lexer.inbuffer[session->lexer.inbuflen++] = frame->data[l2];
		    }
		    gpsd_log(&session->context->errout, LOG_DATA,
			     "pgn %6d:%s \n", work->pgn, work->name);
		} else if (frame->data[0] == session->driver.nmea2000.idx) {
		    unsigned int l2;

		    for (l2=1;l2<8;l2++) {
		        if (session->driver.nmea2000.fast_packet_len > session->lexer.inbuflen) {
			    session->lexer.inbuffer[session->lexer.inbuflen++] = frame->data[l2];
			}
		    }
		    if (session->lexer.inbuflen == session->driver.nmea2000.fast_packet_len) {
#if NMEA2000_FAST_DEBUG
		        gpsd_log(&session->context->errout, LOG_ERROR,
				 "Fast done  %2x %2x %2x %2x %6d\n",
				 session->driver.nmea2000.idx,
				                                                   frame->data[0],
				                                                   session->driver.nmea2000.unit,
				                                                   (unsigned int) session->driver.nmea2000.fast_packet_len,
				                                                   source_pgn);
#endif /* of #if  NMEA2000_FAST_DEBUG */
			session->driver.nmea2000.workpgn = (void *) work;
		        session->lexer.outbuflen = session->driver.nmea2000.fast_packet_len;
			for(l2=0;l2 < (unsigned int)session->lexer.outbuflen; l2++) {
			    session->lexer.outbuffer[l2] = session->lexer.inbuffer[l2];
			}
			session->driver.nmea2000.fast_packet_len = 0;
		    } else {
		        session->driver.nmea2000.idx += 1;
		    }
		} else {
		    gpsd_log(&session->context->errout, LOG_ERROR,
			     "Fast error %2x %2x %2x %2x %6d\n",
			     session->driver.nmea2000.idx,
			     frame->data[0],
			     session->driver.nmea2000.unit,
			     (unsigned int) session->driver.nmea2000.fast_packet_len,
				                                               source_pgn);
		}
	    } else {
	        gpsd_log(&session->context->errout, LOG_WARN,
			 "PGN not found %08d %08x \n",
			 source_pgn, source_pgn);
	    }
	} else {
	    // we got a unknown unit number
	    if (nmea2000_units[can_net][source_unit] == NULL) {
	        char buffer[32];

		(void) snprintf(buffer,
				sizeof(buffer),
				"nmea2000://%s:%u",
				can_interface_name[can_net],
				source_unit);
		if (gpsd_add_device != NULL) {
		    (void) gpsd_add_device(buffer, true);
		}
	    }
	}
    } else {
        // we got RTR or 2.0A CAN frame, not used
    }
}


static ssize_t nmea2000_get(struct gps_device_t *session)
{
    struct can_frame frame;
    ssize_t          status;

    session->lexer.outbuflen = 0;
    status = read(session->gpsdata.gps_fd, &frame, sizeof(frame));
    if (status == (ssize_t)sizeof(frame)) {
        session->lexer.type = NMEA2000_PACKET;
	find_pgn(&frame, session);

        return frame.can_dlc & 0x0f;
    }
    return 0;
}

static gps_mask_t nmea2000_parse_input(struct gps_device_t *session)
{
    gps_mask_t mask;
    PGN *work;

//  printf("NMEA2000 parse_input called\n");
    mask = 0;
    work = (PGN *) session->driver.nmea2000.workpgn;

    if (work != NULL) {
        mask = (work->func)(&session->lexer.outbuffer[0], (int)session->lexer.outbuflen, work, session);
        session->driver.nmea2000.workpgn = NULL;
    }
    session->lexer.outbuflen = 0;

    return mask;
}


int nmea2000_open(struct gps_device_t *session)
{
    char interface_name[strlen(session->gpsdata.dev.path)+1];
    socket_t sock;
    int status;
    int unit_number;
    int can_net;
    unsigned int l;
    struct ifreq ifr;
    struct sockaddr_can addr;
    char *unit_ptr;

    INVALIDATE_SOCKET(session->gpsdata.gps_fd);

    session->driver.nmea2000.can_net = 0;
    can_net = -1;

    unit_number = -1;

    (void)strlcpy(interface_name, session->gpsdata.dev.path + 11, sizeof(interface_name));
    unit_ptr = NULL;
    for (l=0;l<strnlen(interface_name,sizeof(interface_name));l++) {
        if (interface_name[l] == ':') {
	    unit_ptr = &interface_name[l+1];
	    interface_name[l] = 0;
	    continue;
	}
	if (unit_ptr != NULL) {
	    if (isdigit(interface_name[l]) == 0) {
	        gpsd_log(&session->context->errout, LOG_ERROR,
			 "NMEA2000 open: Invalid character in unit number.\n");
	        return -1;
	    }
	}
    }

    if (unit_ptr != NULL) {
        unit_number = atoi(unit_ptr);
	if ((unit_number < 0) || (unit_number > (NMEA2000_UNITS-1))) {
	    gpsd_log(&session->context->errout, LOG_ERROR,
		     "NMEA2000 open: Unit number out of range.\n");
	    return -1;
	}
	for (l = 0; l < NMEA2000_NETS; l++) {
	    if (strncmp(can_interface_name[l],
			interface_name,
			MIN(sizeof(interface_name), sizeof(can_interface_name[l]))) == 0) {
	        can_net = l;
		break;
	    }
	}
	if (can_net < 0) {
	    gpsd_log(&session->context->errout, LOG_ERROR,
		     "NMEA2000 open: CAN device not open: %s .\n", interface_name);
	    return -1;
	}
    } else {
	for (l = 0; l < NMEA2000_NETS; l++) {
	    if (strncmp(can_interface_name[l],
			interface_name,
			MIN(sizeof(interface_name), sizeof(can_interface_name[l]))) == 0) {
	        gpsd_log(&session->context->errout, LOG_ERROR, "NMEA2000 open: CAN device duplicate open: %s .\n", interface_name);
		return -1;
	    }
	}
	for (l = 0; l < NMEA2000_NETS; l++) {
	    if (can_interface_name[l][0] == 0) {
	        can_net = l;
		break;
	    }
	}
	if (can_net < 0) {
	    gpsd_log(&session->context->errout, LOG_ERROR,
		     "NMEA2000 open: Too many CAN networks open.\n");
	    return -1;
	}
    }

    /* Create the socket */
    sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);

    if (BAD_SOCKET(sock)) {
        gpsd_log(&session->context->errout, LOG_ERROR,
		 "NMEA2000 open: can not get socket.\n");
	return -1;
    }

    status = fcntl(sock, F_SETFL, O_NONBLOCK);
    if (status != 0) {
        gpsd_log(&session->context->errout, LOG_ERROR,
		 "NMEA2000 open: can not set socket to O_NONBLOCK.\n");
	close(sock);
	return -1;
    }

    /* Locate the interface you wish to use */
    strlcpy(ifr.ifr_name, interface_name, sizeof(ifr.ifr_name));
    status = ioctl(sock, SIOCGIFINDEX, &ifr); /* ifr.ifr_ifindex gets filled
					       * with that device's index */

    if (status != 0) {
        gpsd_log(&session->context->errout, LOG_ERROR,
		 "NMEA2000 open: can not find CAN device.\n");
	close(sock);
	return -1;
    }

    /* Select that CAN interface, and bind the socket to it. */
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    status = bind(sock, (struct sockaddr*)&addr, sizeof(addr) );
    if (status != 0) {
        gpsd_log(&session->context->errout, LOG_ERROR,
		 "NMEA2000 open: bind failed.\n");
	close(sock);
	return -1;
    }

    gpsd_switch_driver(session, "NMEA2000");
    session->gpsdata.gps_fd = sock;
    session->sourcetype = source_can;
    session->servicetype = service_sensor;
    session->driver.nmea2000.can_net = can_net;

    if (unit_ptr != NULL) {
        nmea2000_units[can_net][unit_number] = session;
	session->driver.nmea2000.unit = unit_number;
	session->driver.nmea2000.unit_valid = true;
    } else {
        strlcpy(can_interface_name[can_net],
		interface_name,
		MIN(sizeof(can_interface_name[0]), sizeof(interface_name)));
	session->driver.nmea2000.unit_valid = false;
	for (l=0;l<NMEA2000_UNITS;l++) {
	    nmea2000_units[can_net][l] = NULL;
	}
    }

    session->gpsdata.dev.parity = 'n';
    session->gpsdata.dev.baudrate = 250000;
    session->gpsdata.dev.stopbits = 0;
    return session->gpsdata.gps_fd;
}

void nmea2000_close(struct gps_device_t *session)
{
    if (!BAD_SOCKET(session->gpsdata.gps_fd)) {
	gpsd_log(&session->context->errout, LOG_SPIN,
		 "close(%d) in nmea2000_close(%s)\n",
		 session->gpsdata.gps_fd, session->gpsdata.dev.path);
	(void)close(session->gpsdata.gps_fd);
	INVALIDATE_SOCKET(session->gpsdata.gps_fd);

	if (session->driver.nmea2000.unit_valid) {
	    unsigned int l1, l2;

	    for (l1=0;l1<NMEA2000_NETS;l1++) {
	        for (l2=0;l2<NMEA2000_UNITS;l2++) {
		    if (session == nmea2000_units[l1][l2]) {
		        session->driver.nmea2000.unit_valid = false;
		        session->driver.nmea2000.unit = 0;
			session->driver.nmea2000.can_net = 0;
			nmea2000_units[l1][l2] = NULL;
		    }
		}
	    }
	}
    }
}

/* *INDENT-OFF* */
const struct gps_type_t driver_nmea2000 = {
    .type_name      = "NMEA2000",       /* full name of type */
    .packet_type    = NMEA2000_PACKET,	/* associated lexer packet type */
    .flags	    = DRIVER_STICKY,	/* remember this */
    .trigger	    = NULL,		/* detect their main sentence */
    .channels       = 12,		/* not an actual GPS at all */
    .probe_detect   = NULL,
    .get_packet     = nmea2000_get,	/* how to get a packet */
    .parse_packet   = nmea2000_parse_input,	/* how to interpret a packet */
    .rtcm_writer    = NULL,		/* Don't send RTCM to this */
    .init_query     = NULL,		/* non-perturbing query */
    .event_hook     = NULL,
#ifdef RECONFIGURE_ENABLE
    .speed_switcher = NULL,		/* no speed switcher */
    .mode_switcher  = NULL,		/* no mode switcher */
    .rate_switcher  = NULL,		/* no rate switcher */
    .min_cycle      = 1,		/* nominal 1-per-second GPS cycle */
#endif /* RECONFIGURE_ENABLE */
#ifdef CONTROLSEND_ENABLE
    .control_send   = NULL,		/* how to send control strings */
#endif /* CONTROLSEND_ENABLE */
#ifdef TIMEHINT_ENABLE
    .time_offset     = NULL,
#endif /* TIMEHINT_ENABLE */
};
/* *INDENT-ON* */

/* end */

#endif /* of  defined(NMEA2000_ENABLE) */
