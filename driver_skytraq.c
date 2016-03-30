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
#define SKY_CHANNELS	28	/* max channels allowed in format */

static gps_mask_t sky_parse(struct gps_device_t *, unsigned char *, size_t);


static gps_mask_t sky_parse(struct gps_device_t * session, unsigned char *buf,
		      size_t len)
{

    if (len == 0)
	return 0;

    buf += 4;
    len -= 8;
    // session->driver.sirf.lastid = buf[0];

    /* could change if the set of messages we enable does */
    session->cycle_end_reliable = true;

    switch (buf[0]) {
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
