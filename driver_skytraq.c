/*
 * This is the gpsd driver for Skytraq GPSes operating in binary mode.
 *
 * This file is Copyright (c) 2016 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <strings.h>
#include <math.h>
#include <ctype.h>
#include <unistd.h>

#include "gpsd.h"
#include "bits.h"
#include "strfuncs.h"
#if defined(SKYTRAQ_ENABLE)

#define HI(n)		((n) >> 8)
#define LO(n)		((n) & 0xff)

/*
 * No ACK/NAK?  Just rety after 6 seconds
 */
#define SKY_RETRY_TIME	6
#define SKY_CHANNELS	48	/* max channels allowed in format */

static gps_mask_t sky_parse(struct gps_device_t *, unsigned char *, size_t);

static gps_mask_t sky_msg_80(struct gps_device_t *, unsigned char *, size_t);
static gps_mask_t sky_msg_DC(struct gps_device_t *, unsigned char *, size_t);
static gps_mask_t sky_msg_DD(struct gps_device_t *, unsigned char *, size_t);
static gps_mask_t sky_msg_DE(struct gps_device_t *, unsigned char *, size_t);
static gps_mask_t sky_msg_DF(struct gps_device_t *, unsigned char *, size_t);
static gps_mask_t sky_msg_E0(struct gps_device_t *, unsigned char *, size_t);
static gps_mask_t sky_msg_E2(struct gps_device_t *, unsigned char *, size_t);
static gps_mask_t sky_msg_E3(struct gps_device_t *, unsigned char *, size_t);

#ifdef __UNUSED
/* Poll Software Version MID 2 */
static unsigned char versionprobe[] = {
    0xa0, 0xa1, 0x00, 0x02,
    0x02,		/* MID 2 */
    0x01,		/* System */
    0x00, 0x0d, 0x0a
};

// cppcheck-suppress unusedFunction
static bool sky_write(struct gps_device_t *session, unsigned char *msg)
{
    unsigned int crc;
    size_t i, len;
    bool ok;
    unsigned int type = (unsigned int)msg[4];

    len = (size_t) ((msg[2] << 8) | msg[3]);

    /* calculate Checksum */
    crc = 0;
    /* coverity_submit[tainted_data] */
    for (i = 0; i < len; i++)
	crc ^= (int)msg[4 + i];

    /* enter CRC after payload */
    msg[len + 4] = (unsigned char)(crc & 0x00ff);

    gpsd_log(&session->context->errout, LOG_PROG,
	     "Skytraq: Writing control type %02x:\n", type);
    ok = (gpsd_write(session, (const char *)msg, len+7) == (ssize_t) (len+7));

    return (ok);
}
#endif /* __UNUSED */

/*
 * decode MID 0x*), Software Version
 *
 * 10 bytes
 */
static gps_mask_t sky_msg_80(struct gps_device_t *session,
				  unsigned char *buf, size_t len)
{
    unsigned int kver_x;  /* kernel version */
    unsigned int kver_y;  /* kernel version */
    unsigned int kver_z;  /* kernel version */
    unsigned int over_x;  /* ODM version */
    unsigned int over_y;  /* ODM version */
    unsigned int over_z;  /* ODM version */
    unsigned int rev_yy;   /* revision */
    unsigned int rev_mm;   /* revision */
    unsigned int rev_dd;   /* revision */

    if ( 14 != len)
	return 0;

    kver_x  = getbeu16(buf, 2);
    kver_y  = getub(buf, 4);
    kver_z  = getub(buf, 5);
    over_x  = getbeu16(buf, 6);
    over_y  = getub(buf, 8);
    over_z  = getub(buf, 9);
    rev_yy  = getbeu16(buf, 10);
    rev_mm  = getub(buf, 12);
    rev_dd  = getub(buf, 13);

    (void)snprintf(session->subtype, sizeof(session->subtype) - 1,
	     "Skytraq: kver=%u.%u,%u, over=%u.%u,%u, rev=%u.%u.%u",
	    kver_x, kver_y, kver_z,
	    over_x, over_y, over_z,
	    rev_yy, rev_mm, rev_dd);

    gpsd_log(&session->context->errout, LOG_DATA,
	     "Skytraq: MID 0x80: kver=%u.%u,%u, over=%u.%u,%u, rev=%u.%u.%u\n",
	    kver_x, kver_y, kver_z,
	    over_x, over_y, over_z,
	    rev_yy, rev_mm, rev_dd);
    return 0;
}

/*
 * decode MID 0xDC, Measurement Time
 *
 * 10 bytes
 */
static gps_mask_t sky_msg_DC(struct gps_device_t *session,
				  unsigned char *buf, size_t len)
{
    unsigned int iod;   /* Issue of data 0 - 255 */
    unsigned int wn;    /* week number 0 - 65535 */
    unsigned int tow;   /* receiver tow 0 - 604799999 in mS */
    unsigned int mp;    /* measurement period 1 - 1000 ms */
    /* calculated */
    double	f_tow;  /* tow in seconds */
    unsigned int msec;  /* mSec part of tow */

    if ( 10 != len)
	return 0;

    iod = (unsigned int)getub(buf, 1);
    wn = getbeu16(buf, 2);
    tow = getbeu32(buf, 4);
    f_tow = (double)(tow / 1000);
    msec = tow % 1000;
    mp = getbeu16(buf, 8);

    /* should this be newdata.skyview_time? */
    session->gpsdata.skyview_time = gpsd_gpstime_resolve(session, wn, f_tow );

    gpsd_log(&session->context->errout, LOG_DATA,
	     "Skytraq: MID 0xDC: iod=%u, wn=%u, tow=%u, mp=%u, t=%lld.%03u\n",
	     iod, wn, tow, mp,
	     (long long)session->gpsdata.skyview_time, msec);
    return 0;
}

/*
 * decode MID 0xDD, Raw Measurements
 *
 */
static gps_mask_t sky_msg_DD(struct gps_device_t *session,
				  unsigned char *buf, size_t len UNUSED)
{
    unsigned int iod;   /* Issue of data 0 - 255 */
    unsigned int nmeas; /* number of measurements */

    iod = (unsigned int)getub(buf, 1);
    nmeas = (unsigned int)getub(buf, 2);

    gpsd_log(&session->context->errout, LOG_DATA,
	     "Skytraq: MID 0xDD: iod=%u, nmeas=%u\n",
	     iod, nmeas);
    return 0;
}

/*
 * decode MID 0xDE, SV and channel status
 *
 * max payload: 3 + (Num_sats * 10) = 483 bytes
 */
static gps_mask_t sky_msg_DE(struct gps_device_t *session,
				  unsigned char *buf, size_t len UNUSED)
{
    int st, nsv;
    unsigned int i;
    unsigned int iod;   /* Issue of data 0 - 255 */
    unsigned int nsvs;  /* number of SVs in this packet */

    iod = (unsigned int)getub(buf, 1);
    nsvs = (unsigned int)getub(buf, 2);
    /* too many sats? */
    if ( SKY_CHANNELS < nsvs )
	return 0;

    gpsd_zero_satellites(&session->gpsdata);
    for (i = st = nsv =  0; i < nsvs; i++) {
	int off = 3 + (10 * i); /* offset into buffer of start of this sat */
	bool good;	      /* do we have a good record ? */
	unsigned short sv_stat;
	unsigned short chan_stat;
	unsigned short ura;

	session->gpsdata.skyview[st].PRN = (short)getub(buf, off + 1);
	sv_stat = (unsigned short)getub(buf, off + 2);
	ura = (unsigned short)getub(buf, off + 3);
	session->gpsdata.skyview[st].ss = (float)getub(buf, off + 4);
	session->gpsdata.skyview[st].elevation =
	    (short)getbes16(buf, off + 5);
	session->gpsdata.skyview[st].azimuth =
	    (short)getbes16(buf, off + 7);
	chan_stat = (unsigned short)getub(buf, off + 9);

	session->gpsdata.skyview[st].used = (bool)(chan_stat & 0x30);
	good = session->gpsdata.skyview[st].PRN != 0 &&
	    session->gpsdata.skyview[st].azimuth != 0 &&
	    session->gpsdata.skyview[st].elevation != 0;

	gpsd_log(&session->context->errout, LOG_DATA,
		 "Skytraq: PRN=%2d El=%d Az=%d ss=%3.2f stat=%02x,%02x ura=%d %c\n",
		session->gpsdata.skyview[st].PRN,
		session->gpsdata.skyview[st].elevation,
		session->gpsdata.skyview[st].azimuth,
		session->gpsdata.skyview[st].ss,
		chan_stat, sv_stat, ura,
		good ? '*' : ' ');

	if ( good ) {
	    st += 1;
	    if (session->gpsdata.skyview[st].used)
		nsv++;
	}
    }

    session->gpsdata.satellites_visible = st;
    session->gpsdata.satellites_used = nsv;

    gpsd_log(&session->context->errout, LOG_DATA,
	     "Skytraq: MID 0xDE: nsvs=%u visible=%u iod=%u\n", nsvs,
	     session->gpsdata.satellites_visible, iod);
    return SATELLITE_SET | USED_IS;
}

/*
 * decode MID 0xDF, Nav status (PVT)
 *
 * 81 bytes
 */
static gps_mask_t sky_msg_DF(struct gps_device_t *session,
				  unsigned char *buf, size_t len)
{
    unsigned int iod;   /* Issue of data 0 - 255 */
    unsigned short navstat;
    unsigned int wn;    /* week number 0 - 65535 */
    double f_tow;         /* receiver tow Sec */
    double clock_bias;
    double clock_drift;
    gps_mask_t mask = 0;

    if ( 81 != len)
	return 0;

    iod = (unsigned int)getub(buf, 1);

    /* fix status is byte 2 */
    navstat = (unsigned short)getub(buf, 2);
    session->gpsdata.status = STATUS_NO_FIX;
    session->newdata.mode = MODE_NO_FIX;
    switch ( navstat ) {
    case 1:
	/* fix prediction, ignore */
	break;
    case 2:
	session->gpsdata.status = STATUS_FIX;
	session->newdata.mode = MODE_2D;
	break;
    case 3:
	session->gpsdata.status = STATUS_FIX;
	session->newdata.mode = MODE_3D;
	mask |= ALTITUDE_SET | CLIMB_SET;
	break;
    case 4:
	session->gpsdata.status = STATUS_DGPS_FIX;
	session->newdata.mode = MODE_3D;
	mask |= ALTITUDE_SET | CLIMB_SET;
	break;
    default:
	break;
    }

    wn = getbeu16(buf, 3);
    f_tow = getbed64((const char *)buf, 5);

    /* position/velocity is bytes 13-48, meters and m/s */
    ecef_to_wgs84fix(&session->newdata, &session->gpsdata.separation,
		     (double)getbed64((const char *)buf, 13),
		     (double)getbed64((const char *)buf, 21),
		     (double)getbed64((const char *)buf, 29),
		     (double)getbef32((const char *)buf, 37),
		     (double)getbef32((const char *)buf, 41),
		     (double)getbef32((const char *)buf, 46));

    clock_bias = getbed64((const char *)buf, 49);
    clock_drift = getbes32(buf, 57);

    session->gpsdata.dop.gdop = getbef32((const char *)buf, 61);
    session->gpsdata.dop.pdop = getbef32((const char *)buf, 65);
    session->gpsdata.dop.hdop = getbef32((const char *)buf, 69);
    session->gpsdata.dop.vdop = getbef32((const char *)buf, 73);
    session->gpsdata.dop.tdop = getbef32((const char *)buf, 77);

    session->newdata.time = gpsd_gpstime_resolve(session, wn, f_tow );

    gpsd_log(&session->context->errout, LOG_DATA,
	    "Skytraq: MID 0xDF: iod=%u, stat=%u, wn=%u, tow=%f, t=%.6f "
	    "cb: %f, cd: %f "
	    "gdop: %.2f, pdop: %.2f, hdop: %.2f, vdop: %.2f, tdop: %.2f\n",
	    iod, navstat, wn, f_tow,
	    session->newdata.time,
	    clock_bias, clock_drift,
	    session->gpsdata.dop.gdop,
	    session->gpsdata.dop.pdop,
	    session->gpsdata.dop.hdop,
	    session->gpsdata.dop.vdop,
	    session->gpsdata.dop.tdop);

    mask |= TIME_SET | LATLON_SET | TRACK_SET |
	SPEED_SET | STATUS_SET | MODE_SET | DOP_SET | CLEAR_IS | REPORT_IS;
    return mask;
}

/*
 * decode MID 0xE0, GPS Subframe data
 *
 * len 33 bytes
 *
 */
static gps_mask_t sky_msg_E0(struct gps_device_t *session,
				  unsigned char *buf, size_t len UNUSED)
{
    int i;
    unsigned int prn;   /* GPS sat PRN */
    unsigned int subf;  /* subframe 1-5 */
    /* the words are preprocessed, not raw, just the 24bits of data */
    uint32_t words[10];  /* subframe 1-5 */

    if ( 33 != len)
	return 0;

    prn = (unsigned int)getub(buf, 1);
    subf = (unsigned int)getub(buf, 2);
    for ( i = 0; i < 10; i++ ) {
	words[i] = (uint32_t)getbeu24(buf, 3 + (i * 3));
    }

    gpsd_log(&session->context->errout, LOG_DATA,
	     "Skytraq: 50B MID 0xE0: prn=%u, subf=%u\n",
	     prn, subf);

    return gpsd_interpret_subframe(session, prn, words);
}

/*
 * pretend to decode MID 0xE2, Beiduo D1 Subframe data
 *
 * from Beidou Standard BDS-SIS-ICD-2.0
 * D1, with the data rate of 50 bps, is broadcasted by the MEO/IGSO satellites
 *
 * len 31 bytes
 *
 */
static gps_mask_t sky_msg_E2(struct gps_device_t *session,
				  unsigned char *buf, size_t len)
{
    int i;
    unsigned int prn;   /* BeidouPS sat PRN 206-214 */
    unsigned int subf;  /* subframe 1-5 */
    /* the words are preprocessed, not raw, just the 28 bytes of data */
    uint8_t bytes[28];  /* raw data */

    if ( 31 != len)
	return 0;

    prn = (unsigned int)getub(buf, 1);
    subf = (unsigned int)getub(buf, 2);
    for ( i = 0; i < 28; i++ ) {
	bytes[i] = getub(buf, 3 + i);
    }

    /* extra guard prevents expensive hexdump calls */
    if (session->context->errout.debug >= LOG_PROG) {
	gpsd_log(&session->context->errout, LOG_PROG,
		 "Skytraq: Beidou D1 subframe PRN %d Subframe %d "
	         "length %zd byte:%s\n",
		 prn, subf,
		 len,
		 gpsd_hexdump(session->msgbuf, sizeof(session->msgbuf),
				 (char *)bytes, 28));
    }

    return ONLINE_SET;
}

/*
 * pretend to decode MID 0xE3, Beiduo D2 Subframe data
 *
 * from Beidou Standard BDS-SIS-ICD-2.0
 * D2, with the data rate of 500 bps, is broadcasted by the GEO satellites.
 *
 * len 31 bytes
 *
 */
static gps_mask_t sky_msg_E3(struct gps_device_t *session,
				  unsigned char *buf, size_t len)
{
    int i;
    unsigned int prn;   /* BeidouPS sat PRN 201-205 */
    unsigned int subf;  /* subframe 1-5 */
    /* the words are preprocessed, not raw, just the 28 bytes of data */
    uint8_t bytes[28];  /* raw data */

    if ( 31 != len)
	return 0;

    prn = (unsigned int)getub(buf, 1);
    subf = (unsigned int)getub(buf, 2);
    for ( i = 0; i < 28; i++ ) {
	bytes[i] = getub(buf, 3 + i);
    }

    /* extra guard prevents expensive hexdump calls */
    if (session->context->errout.debug >= LOG_PROG) {
	gpsd_log(&session->context->errout, LOG_PROG,
		 "Skytraq: Beidou D2 subframe PRN %d Subframe %d "
	         "length %zd byte:%s\n",
		 prn, subf,
		 len,
		 gpsd_hexdump(session->msgbuf, sizeof(session->msgbuf),
				 (char *)bytes, 28));
    }


    return ONLINE_SET;
}


static gps_mask_t sky_parse(struct gps_device_t * session, unsigned char *buf,
		      size_t len)
{
    gps_mask_t mask = 0;

    if (len == 0)
	return mask;

    buf += 4;   /* skip the leaders and length */
    len -= 7;   /* don't count the leaders, length, csum and terminators */
    // session->driver.sirf.lastid = buf[0];

    /* check the checksum?? */

    /* could change if the set of messages we enable does */
    /* session->cycle_end_reliable = true; */

    switch (buf[0]) {
    case 0x80:
	/* 128 */
	return sky_msg_80(session, buf, len);

    case 0x83:
	/* 131 - ACK */
	gpsd_log(&session->context->errout, LOG_PROG,
		 "Skytraq: ACK to MID %#02x\n", buf[1]);
	break;
    case 0x84:
	/* 132 - NACK */
	gpsd_log(&session->context->errout, LOG_INF,
		 "Skytraq: NACK to MID %#02x\n", buf[1]);
	break;
    case 0xDC:
	/* 220 */
	return sky_msg_DC(session, buf, len);

    case 0xDD:
	/* 221 */
	return sky_msg_DD(session, buf, len);

    case 0xDE:
	/* 222 */
	return sky_msg_DE(session, buf, len);

    case 0xDF:
	/* 223 - Nave status (PVT)  */
	return sky_msg_DF(session, buf, len);

    case 0xE0:
	/* 224 */
	return sky_msg_E0(session, buf, len);

    case 0xE2:
	/* 226 - Beidou2 D1 Subframe data */
	return sky_msg_E2(session, buf, len);

    case 0xE3:
	/* 227 - Beidou2 D2 Subframe data */
	return sky_msg_E3(session, buf, len);

    default:
	gpsd_log(&session->context->errout, LOG_PROG,
		 "Skytraq: Unknown packet id %#02x length %zd\n",
		 buf[0], len);
    }
    return mask;
}

static gps_mask_t skybin_parse_input(struct gps_device_t *session)
{
    if (session->lexer.type ==  SKY_PACKET) {
	return  sky_parse(session, session->lexer.outbuffer,
			session->lexer.outbuflen);
#ifdef NMEA0183_ENABLE
    } else if (session->lexer.type == NMEA_PACKET) {
	return nmea_parse((char *)session->lexer.outbuffer, session);
#endif /* NMEA0183_ENABLE */
    } else
	return 0;
}

/* this is everything we export */
/* *INDENT-OFF* */
const struct gps_type_t driver_skytraq =
{
    .type_name      = "Skytraq",		/* full name of type */
    .packet_type    = SKY_PACKET,	/* associated lexer packet type */
    .flags	    = DRIVER_STICKY,	/* remember this */
    .trigger	    = NULL,		/* no trigger */
    .channels       =  SKY_CHANNELS,	/* consumer-grade GPS */
    .probe_detect   = NULL,		/* no probe */
    .get_packet     = generic_get,	/* be prepared for Skytraq or NMEA */
    .parse_packet   = skybin_parse_input,/* parse message packets */
    .rtcm_writer    = gpsd_write,	/* send RTCM data straight */
    .init_query     = NULL,              /* non-perturbing initial qury */
    .event_hook     = NULL,              /* lifetime event handler */
};
/* *INDENT-ON* */
#endif /* defined( SKYTRAQ_ENABLE) && defined(BINARY_ENABLE) */

