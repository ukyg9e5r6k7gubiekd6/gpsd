/* $Id$ */
/*
 * Driver for the iTalk binary protocol used by FasTrax
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
#if defined(ITRAX_ENABLE) && defined(BINARY_ENABLE)

#define LITTLE_ENDIAN_PROTOCOL
#include "bits.h"
#include "italk.h"
static gps_mask_t italk_parse(struct gps_device_t *, unsigned char *, size_t);
static gps_mask_t decode_itk_navfix(struct gps_device_t *, unsigned char *, size_t);
static gps_mask_t decode_itk_prnstatus(struct gps_device_t *, unsigned char *, size_t);
static gps_mask_t decode_itk_utcionomodel(struct gps_device_t *, unsigned char *, size_t);

static gps_mask_t decode_itk_navfix(struct gps_device_t *session, unsigned char *buf, size_t len)
{
	return 0;
}

static gps_mask_t decode_itk_prnstatus(struct gps_device_t *session, unsigned char *buf, size_t len)
{
    unsigned int tow;
    unsigned char i, st, nchan, nsv;
    unsigned short gps_week;
    double t;

    if (len < 62){
	gpsd_report(LOG_PROG, "ITALK: runt PRN_STATUS (len=%d)\n", len);
	return -1;
    }

    gps_week = getuw(buf, 7 + 4);
    tow = getul(buf, 7 + 6);
    t = gpstime_to_unix(gps_week, tow/1000.0) - session->context->leap_seconds;
    session->gpsdata.sentence_time = session->gpsdata.fix.time = t;

    gpsd_zero_satellites(&session->gpsdata);
    nchan = (len - 10 - 52) / 20; 
    st = nsv = 0;
    for (i = 0; i < nchan; i++) {
	int off = 7+ 52 + 20 * i;
	bool good;
	unsigned short flags;
	
	flags = getuw(buf, off);
	session->gpsdata.used[st] = ((flags & PRN_FLAG_USE_IN_NAV) ? 1 : 0)&0xff;
	session->gpsdata.ss[st]		= (int)getuw(buf, off+2)&0xff;
	session->gpsdata.PRN[st]	= (int)getuw(buf, off+4)&0xff;
	session->gpsdata.elevation[st]	= (int)getsw(buf, off+6)&0xff;
	session->gpsdata.azimuth[st]	= (int)getsw(buf, off+8)&0xff;
	good = session->gpsdata.PRN[st]!=0 && 
	    session->gpsdata.azimuth[st]!=0 && 
	    session->gpsdata.elevation[st]!=0;
	if (good!=0)
	    st++;
    }
    session->gpsdata.satellites = st;

    return SATELLITE_SET | TIME_SET;
}

static gps_mask_t decode_itk_utcionomodel(struct gps_device_t *session, unsigned char *buf, size_t len)
{
    unsigned int tow;
    unsigned short gps_week, flags, leap;
    double t;

    if (len != 64){
	gpsd_report(LOG_PROG, "ITALK: bad UTC_IONO_MODEL (len %d, should be 64)\n", len);
	return -1;
    }

    flags = getuw(buf, 7);
    if (0 == (flags & UTC_IONO_MODEL_UTCVALID))
	return 0;

    leap = getuw(buf, 7 + 24);
    if (session->context->leap_seconds < leap)
    	session->context->leap_seconds = leap;

    gps_week = getuw(buf, 7 + 36);
    tow = getul(buf, 7 + 38);
    t = gpstime_to_unix(gps_week, tow/1000.0) - session->context->leap_seconds;
    session->gpsdata.sentence_time = session->gpsdata.fix.time = t;

    return TIME_SET;
}

/*@ +charint -usedef -compdef @*/
static bool italk_write(int fd, unsigned char *msg, size_t msglen) {
   bool      ok;

   /* CONSTRUCT THE MESSAGE */

   /* we may need to dump the message */
   gpsd_report(LOG_IO, "writing italk control type %02x:%s\n", 
	       msg[0], gpsd_hexdump(msg, msglen));
#ifdef ALLOW_RECONFIGURE
   ok = (write(fd, msg, msglen) == (ssize_t)msglen);
   (void)tcdrain(fd);
#else
   ok = 0;
#endif /* ALLOW_RECONFIGURE */
   return(ok);
}
/*@ -charint +usedef +compdef @*/

/*@ +charint @*/
static gps_mask_t italk_parse(struct gps_device_t *session, unsigned char *buf, size_t len)
{
    int mask = 0, type;

    if (len == 0)
	return 0;

    type = getub(buf, 4);
    /* we may need to dump the raw packet */
    gpsd_report(LOG_RAW, "raw italk packet type 0x%02x length %d: %s\n", type, len, gpsd_hexdump(buf, len));

    switch (type)
    {
    case ITALK_NAV_FIX:
	gpsd_report(LOG_IO, "iTalk NAV_FIX len %d\n", len);
	mask = decode_itk_navfix(session, buf, len);
	break;
    case ITALK_PRN_STATUS:
	gpsd_report(LOG_IO, "iTalk PRN_STATUS len %d\n", len);
	mask = decode_itk_prnstatus(session, buf, len);
	break;
    case ITALK_UTC_IONO_MODEL:
	gpsd_report(LOG_IO, "iTalk UTC_IONO_MODEL len %d\n", len);
	mask = decode_itk_utcionomodel(session, buf, len);
	break;

    case ITALK_ACQ_DATA:
	gpsd_report(LOG_IO, "iTalk ACQ_DATA len %d\n", len);
	break;
    case ITALK_TRACK:
	gpsd_report(LOG_IO, "iTalk TRACK len %d\n", len);
	break;
    case ITALK_PSEUDO:
	gpsd_report(LOG_IO, "iTalk PSEUDO len %d\n", len);
	break;
    case ITALK_RAW_ALMANAC:
	gpsd_report(LOG_IO, "iTalk RAW_ALMANAC len %d\n", len);
	break;
    case ITALK_RAW_EPHEMERIS:
	gpsd_report(LOG_IO, "iTalk RAW_EPHEMERIS len %d\n", len);
	break;
    case ITALK_SUBFRAME:
	gpsd_report(LOG_IO, "iTalk SUBFRAME len %d\n", len);
	break;
    case ITALK_BIT_STREAM:
	gpsd_report(LOG_IO, "iTalk BIT_STREAM len %d\n", len);
	break;

    case ITALK_AGC:
    case ITALK_SV_HEALTH:
    case ITALK_PRN_PRED:
    case ITALK_FREQ_PRED:
    case ITALK_DBGTRACE:
    case ITALK_START:
    case ITALK_STOP:
    case ITALK_SLEEP:
    case ITALK_STATUS:
    case ITALK_ITALK_CONF:
    case ITALK_SYSINFO:
    case ITALK_ITALK_TASK_ROUTE:
    case ITALK_PARAM_CTRL:
    case ITALK_PARAMS_CHANGED:
    case ITALK_START_COMPLETED:
    case ITALK_STOP_COMPLETED:
    case ITALK_LOG_CMD:
    case ITALK_SYSTEM_START:
    case ITALK_STOP_SEARCH:
    case ITALK_SEARCH:
    case ITALK_PRED_SEARCH:
    case ITALK_SEARCH_DONE:
    case ITALK_TRACK_DROP:
    case ITALK_TRACK_STATUS:
    case ITALK_HANDOVER_DATA:
    case ITALK_CORE_SYNC:
    case ITALK_WAAS_RAWDATA:
    case ITALK_ASSISTANCE:
    case ITALK_PULL_FIX:
    case ITALK_MEMCTRL:
    case ITALK_STOP_TASK:
	gpsd_report(LOG_IO, "iTalk not processing packet: id 0x%02x length %d\n", type, len);
	break;
    default:
	gpsd_report(LOG_IO, "iTalk unknown packet: id 0x%02x length %d\n", type, len);
    }
    if (mask == -1)
	mask = 0;
    else
	(void)snprintf(session->gpsdata.tag, sizeof(session->gpsdata.tag),
	   "ITK-%02x",type);

    return mask | ONLINE_SET;
}
/*@ -charint @*/

static gps_mask_t italk_parse_input(struct gps_device_t *session)
{
    gps_mask_t st;

    if (session->packet.type == ITALK_PACKET){
	st = italk_parse(session, session->packet.outbuffer, session->packet.outbuflen);
	session->gpsdata.driver_mode = 1;	/* binary */
	return st;
#ifdef NMEA_ENABLE
    } else if (session->packet.type == NMEA_PACKET) {
	st = nmea_parse((char *)session->packet.outbuffer, session);
	session->gpsdata.driver_mode = 0;	/* NMEA */
	return st;
#endif /* NMEA_ENABLE */
    } else
	return 0;
}

static bool italk_set_mode(struct gps_device_t *session UNUSED, 
			      speed_t speed UNUSED, bool mode UNUSED)
{
    /*@ +charint @*/
    unsigned char msg[] = {0,};

    /* HACK THE MESSAGE */

    return italk_write(session->gpsdata.gps_fd, msg, sizeof(msg));
    /*@ +charint @*/
}

static bool italk_speed(struct gps_device_t *session, speed_t speed)
{
    return italk_set_mode(session, speed, true);
}

static void italk_mode(struct gps_device_t *session, int mode)
{
    if (mode == 0) {
	(void)gpsd_switch_driver(session, "Generic NMEA");
	(void)italk_set_mode(session, session->gpsdata.baudrate, false);
	session->gpsdata.driver_mode = 0;	/* NMEA */
    } else
	session->gpsdata.driver_mode = 1;	/* binary */
}

#ifdef ALLOW_RECONFIGURE
static void italk_configurator(struct gps_device_t *session, int seq)
{
    if (seq == 0 && session->packet.type == NMEA_PACKET)
	(void)italk_set_mode(session, session->gpsdata.baudrate, true);
}
#endif /* ALLOW_RECONFIGURE */

static void italk_ping(struct gps_device_t *session)
/* send a "ping". it may help us detect an itrax more quickly */
{
    char *ping = "<?>";
    (void)italk_write(session->gpsdata.gps_fd, ping, 3);
}

/* this is everything we export */
struct gps_type_t italk_binary =
{
    .typename       = "iTalk binary",	/* full name of type */
    .trigger        = NULL,		/* recognize the type */
    .channels       = 12,		/* consumer-grade GPS */
    .probe_wakeup   = italk_ping,	/* no wakeup to be done before hunt */
    .probe_detect   = NULL,        	/* how to detect at startup time */
    .probe_subtype  = NULL,        	/* initialize the device */
#ifdef ALLOW_RECONFIGURE
    .configurator   = italk_configurator,/* configure the device */
#endif /* ALLOW_RECONFIGURE */
    .get_packet     = generic_get,	/* use generic packet grabber */
    .parse_packet   = italk_parse_input,/* parse message packets */
    .rtcm_writer    = pass_rtcm,	/* send RTCM data straight */
    .speed_switcher = italk_speed,	/* we can change baud rates */
    .mode_switcher  = italk_mode,	/* there is a mode switcher */
    .rate_switcher  = NULL,		/* no sample-rate switcher */
    .cycle_chars    = -1,		/* not relevant, no rate switch */
#ifdef ALLOW_RECONFIGURE
    .revert         = NULL,		/* no setting-reversion method */
#endif /* ALLOW_RECONFIGURE */
    .wrapup         = NULL,		/* no close hook */
    .cycle          = 1,		/* updates every second */
};
#endif /* defined(ITRAX_ENABLE) && defined(BINARY_ENABLE) */
