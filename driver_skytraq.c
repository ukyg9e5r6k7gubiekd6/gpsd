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

static gps_mask_t sky_msg_DC(struct gps_device_t *, unsigned char *, size_t);
static gps_mask_t sky_msg_DD(struct gps_device_t *, unsigned char *, size_t);
static gps_mask_t sky_msg_DE(struct gps_device_t *, unsigned char *, size_t);

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

    session->gpsdata.skyview_time = gpsd_gpstime_resolve(session, wn, f_tow );

    gpsd_log(&session->context->errout, 1, /* LOG_DATA, */
	     "Skytraq: MID 0xDC: iod=%u, wn=%u, tow=%u, mp=%u, t=%lu.%3u\n",
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

    gpsd_log(&session->context->errout, 1, /* LOG_DATA, */
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
    int st, i, nsv;
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
// #ifndef UNUSED
	gpsd_log(&session->context->errout, 1, /* PROG, */
		 "Skytraq: PRN=%2d El=%d Az=%d ss=%3.2f stat=%02x,%02x ura=%d %c\n",
		session->gpsdata.skyview[st].PRN,
		session->gpsdata.skyview[st].elevation,
		session->gpsdata.skyview[st].azimuth,
		session->gpsdata.skyview[st].ss,
		chan_stat, sv_stat, ura,
		good ? '*' : ' ');
// #endif /* UNUSED */
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
    return SATELLITE_SET;
}


static gps_mask_t sky_parse(struct gps_device_t * session, unsigned char *buf,
		      size_t len)
{

    if (len == 0)
	return 0;

    buf += 4;   /* skip the leaders and length */
    len -= 7;   /* don't count the leaders, length, csum and terminators */
    // session->driver.sirf.lastid = buf[0];

    /* could change if the set of messages we enable does */
    session->cycle_end_reliable = true;

    switch (buf[0]) {
    case 0xDC:
	return sky_msg_DC(session, buf, len);

    case 0xDD:
	return sky_msg_DD(session, buf, len);

    case 0xDE:
	return sky_msg_DE(session, buf, len);

    default:
	gpsd_log(&session->context->errout, LOG_PROG,
		 "Skytraq: Unknown packet id %d length %zd\n",
		 buf[0], len);
	return 0;
    }
    return 0;
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
