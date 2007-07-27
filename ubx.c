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

static gps_mask_t ubx_parse(struct gps_device_t *session, unsigned char *buf, size_t len);
static gps_mask_t ubx_msg_nav_sol(struct gps_device_t *session, unsigned char *buf, size_t data_len);
static gps_mask_t ubx_msg_nav_dop(struct gps_device_t *session, unsigned char *buf, size_t data_len);
static gps_mask_t ubx_msg_nav_timegps(struct gps_device_t *session, unsigned char *buf, size_t data_len);
static gps_mask_t ubx_msg_nav_svinfo(struct gps_device_t *session, unsigned char *buf, size_t data_len);
static void       ubx_msg_inf(unsigned char *buf, size_t data_len);

/**
 * Navigation solution message
 */
static gps_mask_t
ubx_msg_nav_sol(struct gps_device_t *session, unsigned char *buf, size_t data_len)
{
    unsigned short gw;
    unsigned int tow, flags;
    double epx, epy, epz, evx, evy, evz;
    unsigned char navmode;
    gps_mask_t mask;
    double t;

    if (data_len != 52)
	return 0;

    flags = (unsigned int)getub(buf, 11);
    mask =  ONLINE_SET;
    if ((flags & (UBX_SOL_VALID_WEEK |UBX_SOL_VALID_TIME)) != 0){
	tow = getul(buf, 0);
	gw = (unsigned short)getsw(buf, 8);
	session->driver.ubx.gps_week = gw;

	t = gpstime_to_unix((int)session->driver.ubx.gps_week, tow/1000.0) - session->context->leap_seconds;
	session->gpsdata.sentence_time = t;
	session->gpsdata.fix.time = t;
	mask |= TIME_SET;
#ifdef NTPSHM_ENABLE
	/* TODO overhead */
	if (session->context->enable_ntpshm)
	    (void)ntpshm_put(session, session->gpsdata.sentence_time);
#endif
    }

    epx = (double)(getsl(buf, 12)/100.0);
    epy = (double)(getsl(buf, 16)/100.0);
    epz = (double)(getsl(buf, 20)/100.0);
    evx = (double)(getsl(buf, 28)/100.0);
    evy = (double)(getsl(buf, 32)/100.0);
    evz = (double)(getsl(buf, 36)/100.0);
    ecef_to_wgs84fix(&session->gpsdata, epx, epy, epz, evx, evy, evz);
    mask |= LATLON_SET | ALTITUDE_SET | SPEED_SET | TRACK_SET | CLIMB_SET  ;
    session->gpsdata.fix.eph = (double)(getsl(buf, 24)/100.0);
    session->gpsdata.fix.eps = (double)(getsl(buf, 40)/100.0);
    session->gpsdata.pdop = (double)(getuw(buf, 44)/100.0);
    session->gpsdata.satellites_used = (int)getub(buf, 47);
    mask |= PDOP_SET ;

    navmode = getub(buf, 10);
    switch (navmode){
    case UBX_MODE_TMONLY:
    case UBX_MODE_3D:
	session->gpsdata.fix.mode = MODE_3D;
	break;
    case UBX_MODE_2D:
    case UBX_MODE_DR:	    /* consider this too as 2D */
    case UBX_MODE_GPSDR:    /* XXX DR-aided GPS may be valid 3D */
	session->gpsdata.fix.mode = MODE_2D;
	break;
    default:
	session->gpsdata.fix.mode = MODE_NO_FIX;
    }

    if ((flags & UBX_SOL_FLAG_DGPS) != 0)
	session->gpsdata.status = STATUS_DGPS_FIX;
    else if (session->gpsdata.fix.mode != MODE_NO_FIX)
	session->gpsdata.status = STATUS_FIX;

    mask |= MODE_SET | STATUS_SET | CYCLE_START_SET | USED_SET ;

    return mask;
}

/**
 * Dilution of precision message
 */
static gps_mask_t
ubx_msg_nav_dop(struct gps_device_t *session, unsigned char *buf, size_t data_len)
{
    if (data_len != 18)
	return 0;

    session->gpsdata.gdop = (double)(getuw(buf, 4)/100.0);
    session->gpsdata.pdop = (double)(getuw(buf, 6)/100.0);
    session->gpsdata.tdop = (double)(getuw(buf, 8)/100.0);
    session->gpsdata.vdop = (double)(getuw(buf, 10)/100.0);
    session->gpsdata.hdop = (double)(getuw(buf, 12)/100.0);

    return DOP_SET;
}

/**
 * GPS Leap Seconds
 */
static gps_mask_t
ubx_msg_nav_timegps(struct gps_device_t *session, unsigned char *buf, size_t data_len)
{
    unsigned int gw, tow, flags;
    double t;

    if (data_len != 16)
	return 0;

    tow = getul(buf, 0);
    gw = (uint)getsw(buf, 8);
    if (gw > session->driver.ubx.gps_week)
	session->driver.ubx.gps_week = gw;

    flags = (unsigned int)getub(buf, 11);
    if ((flags & 0x7) != 0)
    	session->context->leap_seconds = (int)getub(buf, 10);

    t = gpstime_to_unix((int)session->driver.ubx.gps_week, tow/1000.0) - session->context->leap_seconds;
    session->gpsdata.sentence_time = session->gpsdata.fix.time = t;

    return TIME_SET | ONLINE_SET;
}

/**
 * GPS Satellite Info
 */
static gps_mask_t
ubx_msg_nav_svinfo(struct gps_device_t *session, unsigned char *buf, size_t data_len)
{
    unsigned int i, tow, st, nchan, nsv;;

    if (data_len < 152 ) {
	gpsd_report(LOG_PROG, "runt svinfo (datalen=%d)\n", data_len);
	return 0;
    }
    tow = getul(buf, 0);
//    session->gpsdata.sentence_time = gpstime_to_unix(gps_week, tow) 
//				- session->context->leap_seconds;
    /*@ +charint @*/
    nchan = getub(buf, 4);
    if (nchan > 16){
	gpsd_report(LOG_WARN, "Invalid NAV SVINFO message, >16 reported");
	return 0;
    }
    /*@ -charint @*/
    gpsd_zero_satellites(&session->gpsdata);
    st = nsv = 0;
    for (i = 0; i < nchan; i++) {
	int off = 8 + 12 * i;
	bool good;
	session->gpsdata.PRN[st]	= (int)getub(buf, off+1);
	/*@ -predboolothers */
	if (getub(buf, off+2) & 0x01)
	    session->gpsdata.used[st] = session->gpsdata.PRN[st];

	/*@ +predboolothers */
	session->gpsdata.ss[st]		= (int)getub(buf, off+4);
	session->gpsdata.elevation[st]	= (int)getsb(buf, off+5);
	session->gpsdata.azimuth[st]	= (int)getsw(buf, off+6);
	good = session->gpsdata.PRN[st]!=0 && 
	    session->gpsdata.azimuth[st]!=0 && 
	    session->gpsdata.elevation[st]!=0 &&
	    (int)getub(buf, off+11); /* quality indicator. 0 = channel idle */
	if (good!=0)
	    st++;
    }
    session->gpsdata.satellites = (int)st;
    return SATELLITE_SET;
}

static void
ubx_msg_inf(unsigned char *buf, size_t data_len)
{
    unsigned short msgid;
    static char txtbuf[MAX_PACKET_LENGTH];

    msgid = (buf[2] << 8) | buf[3];
    if (data_len > MAX_PACKET_LENGTH-1)
	data_len = MAX_PACKET_LENGTH-1;

    strlcpy(txtbuf, buf+6, MAX_PACKET_LENGTH); txtbuf[data_len] = '\0';
    switch (msgid) {
	case UBX_INF_DEBUG:
	    gpsd_report(LOG_PROG, "UBX_INF_DEBUG: %s\n", txtbuf);
	    break;
	case UBX_INF_TEST:
	    gpsd_report(LOG_PROG, "UBX_INF_TEST: %s\n", txtbuf);
	    break;
	case UBX_INF_NOTICE:
	    gpsd_report(LOG_INF, "UBX_INF_NOTICE: %s\n", txtbuf);
	    break;
	case UBX_INF_WARNING:
	    gpsd_report(LOG_WARN, "UBX_INF_WARNING: %s\n", txtbuf);
	    break;
	case UBX_INF_ERROR:
	    gpsd_report(LOG_WARN, "UBX_INF_ERROR: %s\n", txtbuf);
	    break;
	default:
	    break;
    }
    return ;
}

/*@ +charint @*/
static gps_mask_t ubx_parse(struct gps_device_t *session, unsigned char *buf, size_t len)
{
    size_t data_len;
    unsigned short msgid;
    gps_mask_t mask = 0;

    if (len < 6)    /* the packet at least contains a head of six bytes */
	return 0;

    /* extract message id and length */
    msgid = (buf[2] << 8) | buf[3];
    data_len = (size_t)getsw(buf, 4);
    switch (msgid)
    {
	case UBX_NAV_POSECEF:
	    gpsd_report(LOG_IO, "UBX_NAV_POSECEF\n");
	    break;
	case UBX_NAV_POSLLH:
	    gpsd_report(LOG_IO, "UBX_NAV_POSLLH\n");
	    break;
	case UBX_NAV_STATUS:
	    gpsd_report(LOG_IO, "UBX_NAV_STATUS\n");
	    break;
	case UBX_NAV_DOP:
	    gpsd_report(LOG_PROG, "UBX_NAV_DOP\n");
            mask = ubx_msg_nav_dop(session, &buf[6], data_len);
	    break;
	case UBX_NAV_SOL:
	    gpsd_report(LOG_PROG, "UBX_NAV_SOL\n");
            mask = ubx_msg_nav_sol(session, &buf[6], data_len);
	    break;
	case UBX_NAV_POSUTM:
	    gpsd_report(LOG_IO, "UBX_NAV_POSUTM\n");
	    break;
	case UBX_NAV_VELECEF:
	    gpsd_report(LOG_IO, "UBX_NAV_VELECEF\n");
	    break;
	case UBX_NAV_VELNED:
	    gpsd_report(LOG_IO, "UBX_NAV_VELNED\n");
	    break;
	case UBX_NAV_TIMEGPS:
	    gpsd_report(LOG_PROG, "UBX_NAV_TIMEGPS\n");
            mask = ubx_msg_nav_timegps(session, &buf[6], data_len);
	    break;
	case UBX_NAV_TIMEUTC:
	    gpsd_report(LOG_IO, "UBX_NAV_TIMEUTC\n");
	    break;
	case UBX_NAV_CLOCK:
	    gpsd_report(LOG_IO, "UBX_NAV_CLOCK\n");
	    break;
	case UBX_NAV_SVINFO:
	    gpsd_report(LOG_PROG, "UBX_NAV_SVINFO\n");
            mask = ubx_msg_nav_svinfo(session, &buf[6], data_len);
	    break;
	case UBX_NAV_DGPS:
	    gpsd_report(LOG_IO, "UBX_NAV_DGPS\n");
	    break;
	case UBX_NAV_SBAS:
	    gpsd_report(LOG_IO, "UBX_NAV_SBAS\n");
	    break;
	case UBX_NAV_EKFSTATUS:
	    gpsd_report(LOG_IO, "UBX_NAV_EKFSTATUS\n");
	    break;

	case UBX_RXM_RAW:
	    gpsd_report(LOG_IO, "UBX_RXM_RAW\n");
	    break;
	case UBX_RXM_SFRB:
	    gpsd_report(LOG_IO, "UBX_RXM_SFRB\n");
	    break;
	case UBX_RXM_SVSI:
	    gpsd_report(LOG_PROG, "UBX_RXM_SVSI\n");
	    break;
	case UBX_RXM_ALM:
	    gpsd_report(LOG_IO, "UBX_RXM_ALM\n");
	    break;
	case UBX_RXM_EPH:
	    gpsd_report(LOG_IO, "UBX_RXM_EPH\n");
	    break;
	case UBX_RXM_POSREQ:
	    gpsd_report(LOG_IO, "UBX_RXM_POSREQ\n");
	    break;

	case UBX_MON_SCHED:
	    gpsd_report(LOG_IO, "UBX_MON_SCHED\n");
	    break;
	case UBX_MON_IO:
	    gpsd_report(LOG_IO, "UBX_MON_IO\n");
	    break;
	case UBX_MON_IPC:
	    gpsd_report(LOG_IO, "UBX_MON_IPC\n");
	    break;
	case UBX_MON_VER:
	    gpsd_report(LOG_IO, "UBX_MON_VER\n");
	    break;
	case UBX_MON_EXCEPT:
	    gpsd_report(LOG_IO, "UBX_MON_EXCEPT\n");
	    break;
	case UBX_MON_MSGPP:
	    gpsd_report(LOG_IO, "UBX_MON_MSGPP\n");
	    break;
	case UBX_MON_RXBUF:
	    gpsd_report(LOG_IO, "UBX_MON_RXBUF\n");
	    break;
	case UBX_MON_TXBUF:
	    gpsd_report(LOG_IO, "UBX_MON_TXBUF\n");
	    break;
	case UBX_MON_HW:
	    gpsd_report(LOG_IO, "UBX_MON_HW\n");
	    break;
	case UBX_MON_USB:
	    gpsd_report(LOG_IO, "UBX_MON_USB\n");
	    break;

	case UBX_INF_DEBUG:
	    /* FALLTHROUGH */
	case UBX_INF_TEST:
	    /* FALLTHROUGH */
	case UBX_INF_NOTICE:
	    /* FALLTHROUGH */
	case UBX_INF_WARNING:
	    /* FALLTHROUGH */
	case UBX_INF_ERROR:
	    ubx_msg_inf(buf, data_len);
	    break;

	case UBX_TIM_TP:
	    gpsd_report(LOG_IO, "UBX_TIM_TP\n");
	    break;
	case UBX_TIM_TM:
	    gpsd_report(LOG_IO, "UBX_TIM_TM\n");
	    break;
	case UBX_TIM_TM2:
	    gpsd_report(LOG_IO, "UBX_TIM_TM2\n");
	    break;
	case UBX_TIM_SVIN:
	    gpsd_report(LOG_IO, "UBX_TIM_SVIN\n");
	    break;
    default:
	gpsd_report(LOG_WARN, "UBX: unknown packet id 0x%04hx (length %d) %s\n", 
	    msgid, len, gpsd_hexdump(buf, len));
    }

    if (mask)
	(void)snprintf(session->gpsdata.tag, sizeof(session->gpsdata.tag),
	   "0x%04hx", msgid);

    return mask | ONLINE_SET;
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
    .channels         = 16,
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
