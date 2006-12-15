/* $Id$
 *
 * UBX driver
 */

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <unistd.h>
#include <time.h>
#include <stdio.h>

#include "gpsd_config.h"
#include "gpsd.h"
#if defined(UBX_ENABLE) && defined(BINARY_ENABLE)
#include "ubx.h"

#define LITTLE_ENDIAN_PROTOCOL
#include "bits.h"

gps_mask_t ubx_parse(struct gps_device_t *session, unsigned char *buf, size_t len);
gps_mask_t ubx_package_nav_sol(struct gps_device_t *session, unsigned char *buf, size_t data_len);

/**
 * Navigation solution package
 */
gps_mask_t
ubx_package_nav_sol(struct gps_device_t *session, unsigned char *buf, size_t data_len)
{
    unsigned short gps_week;
    unsigned int tow;

    if (data_len < 52)
	return 0;

    gps_week = getsw(buf, 8);
    tow = getul(buf, 4);
    session->gpsdata.sentence_time = gpstime_to_unix(gps_week, tow) 
			           - session->context->leap_seconds;
#ifdef NTPSHM_ENABLE
    if (session->context->enable_ntpshm)
	(void)ntpshm_put(session, session->gpsdata.sentence_time); /* TODO overhead */
#endif

    if (buf[10] == UBX_MODE_3D)
	session->gpsdata.fix.mode = MODE_3D;
    if (buf[10] == UBX_MODE_2D || 
	buf[10] == UBX_MODE_DR ||	    /* consider this too as 2D */
	buf[10] == UBX_MODE_GPSDR)	    /* conservative */
	session->gpsdata.fix.mode = MODE_2D;
    else
	session->gpsdata.fix.mode = MODE_NO_FIX;

    return 0;
}

/*@ +charint @*/
gps_mask_t ubx_parse(struct gps_device_t *session, unsigned char *buf, size_t len)
{
    unsigned short data_len;
    unsigned short msgid;

    if (len < 6)    /* the packet at least contains a head of six bytes */
	return 0;

    /* we may need to dump the raw packet */
    gpsd_report(LOG_PROG, "UBX packet type %02hhx:%02hhx length %d: %s\n", 
		buf[2], buf[3], len, gpsd_hexdump(buf, len));

    /* extract message id and length */
    msgid = (buf[2] << 8) | buf[3];
    data_len = getsw(buf, 4);
    switch (msgid)
    {
	case UBX_NAV_X:
	    gpsd_report(LOG_PROG, "UBX_NAV_X\n");
	    break;
	case UBX_NAV_SOL:
	    gpsd_report(LOG_PROG, "UBX_NAV_SOL\n");
            ubx_package_nav_sol(session, &buf[6], data_len);
	    break;
	case UBX_NAV_POSLLH:
	    gpsd_report(LOG_PROG, "UBX_NAV_POSLLH\n");
	    break;
	case UBX_NAV_STATUS:
	    gpsd_report(LOG_PROG, "UBX_NAV_STATUS\n");
	    break;
	case UBX_NAV_SVINFO:
	    gpsd_report(LOG_PROG, "UBX_NAV_SVINFO\n");
	    break;
	case UBX_MON_SCHED:
	    gpsd_report(LOG_PROG, "UBX_MON_SCHED\n");
	    break;
	case UBX_MON_IO:
	    gpsd_report(LOG_PROG, "UBX_MON_IO\n");
	    break;
	case UBX_MON_TXBUF:
	    gpsd_report(LOG_PROG, "UBX_MON_TXBUF\n");
	    break;
	case UBX_INF_NOTICE:
	    gpsd_report(LOG_PROG, "UBX_INF_NOTICE\n");
	    break;
	case UBX_INF_WARNING:
	    gpsd_report(LOG_PROG, "UBX_INF_WARNING\n");
	    break;
    default:
	gpsd_report(LOG_WARN, "UBX: unknown packet id %04hx (length: %d)\n", 
		    msgid, len);
    }
    return 0;
}
/*@ -charint @*/

static gps_mask_t parse_input(struct gps_device_t *session)
{
    gps_mask_t st;

    if (session->packet.type == UBX_PACKET){
	st = ubx_parse(session, session->packet.outbuffer, session->packet.outbuflen);
	session->gpsdata.driver_mode = 1;
	return st;
#ifdef NMEA_ENABLE
    } else if (session->packet.type == NMEA_PACKET) {
	st = nmea_parse((char *)session->packet.outbuffer, session);
	session->gpsdata.driver_mode = 0;
	return st;
#endif /* NMEA_ENABLE */
    } else
	return 0;
}

/* The methods in this code take parameters and have */
/* return values that conform to the requirements AT */
/* THE TIME THE CODE WAS WRITTEN.                    */
/*                                                   */
/* These values may well have changed by the time    */
/* you read this and methods could have been added   */
/* or deleted.                                       */
/*                                                   */
/* The latest situation can be found by inspecting   */
/* the contents of struct gps_type_t in gpsd.h.      */
/*                                                   */
/* This always contains the correct definitions that */
/* any driver must use to compile.                   */

/* This is everything we export */
struct gps_type_t ubx_binary = {
    /* Full name of type */
    .typename         = "uBlox UBX",
    /* Response string that identifies device (not active) */
    .trigger          = NULL,
    /* Number of satellite channels supported by the device */
    .channels         = 12,
    /* Startup-time device detector */
    .probe_detect     = /*probe_detect*/ NULL,
    /* Wakeup to be done before each baud hunt */
    .probe_wakeup     = /*probe_wakeup*/ NULL,
    /* Initialize the device and get subtype */
    .probe_subtype    = /*probe_subtype*/ NULL,
#ifdef ALLOW_RECONFIGURE
    /* Enable what reports we need */
    .configurator     = /*configurator*/ NULL,
#endif /* ALLOW_RECONFIGURE */
    /* Packet getter (using default routine) */
    .get_packet       = generic_get,
    /* Parse message packets */
    .parse_packet     = parse_input,
    /* RTCM handler (using default routine) */
    .rtcm_writer      = /*pass_rtcm*/ NULL,
    /* Speed (baudrate) switch */
    .speed_switcher   = /*set_speed*/ NULL,
    /* Switch to NMEA mode */
    .mode_switcher    = /* set_mode */ NULL,
    /* Message delivery rate switcher */
    .rate_switcher    = NULL,
    /* Number of chars per report cycle */
    .cycle_chars      = -1,
#ifdef ALLOW_RECONFIGURE
    /* Undo the actions of .configurator */
    .revert           = /*ubx_revert*/ NULL,
#endif /* ALLOW_RECONFIGURE */
    /* Puts device back to original settings */
    .wrapup           = /*ubx_wrapup*/ NULL,
    /* Number of updates per second */
    .cycle            = 1
};
#endif /* defined(UBX_ENABLE) && defined(BINARY_ENABLE) */
