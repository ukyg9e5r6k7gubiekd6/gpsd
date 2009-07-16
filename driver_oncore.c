/* $Id$ */

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
#if defined(ONCORE_ENABLE) && defined(BINARY_ENABLE)

#include "bits.h"

/*
 * These routines are specific to this driver
 */

static	gps_mask_t oncore_parse_input(struct gps_device_t *);
static	gps_mask_t oncore_dispatch(struct gps_device_t *, unsigned char *, size_t );
static	gps_mask_t oncore_msg_navsol(struct gps_device_t *, unsigned char *, size_t );
static	gps_mask_t oncore_msg_utctime(struct gps_device_t *, unsigned char *, size_t );
static	gps_mask_t oncore_msg_svinfo(struct gps_device_t *, unsigned char *, size_t );

/*
 * These methods may be called elsewhere in gpsd
 */
static	ssize_t oncore_control_send(struct gps_device_t *, char *, size_t );
static	bool oncore_probe_detect(struct gps_device_t *);
static	void oncore_probe_wakeup(struct gps_device_t *);
static	void oncore_probe_subtype(struct gps_device_t *, unsigned int );
static	void oncore_configurator(struct gps_device_t *, unsigned int );
static	bool oncore_set_speed(struct gps_device_t *, speed_t, char, int );
static	void oncore_set_mode(struct gps_device_t *, int );
static	void oncore_revert(struct gps_device_t *);
static	void oncore_wrapup(struct gps_device_t *);

/*
 * Decode the navigation solution message
 */
static gps_mask_t
oncore_msg_navsol(struct gps_device_t *session, unsigned char *buf, size_t data_len)
{
    gps_mask_t mask;
    unsigned char flags, mon, day, hour, min, sec;
    unsigned short year, dop;
    unsigned int nsec;
    double lat, lon, alt;
    float speed, track;

    if (data_len != 76)
	return 0;

    mask = ONLINE_SET;
    gpsd_report(LOG_IO, "oncore NAVSOL - navigation data\n");

    flags = getub(buf, 72);
    if ((flags & 0xcb) != 0){
	gpsd_report(LOG_WARN, "oncore NAVSOL no fix - flags 0x%02x\n", flags);
	return mask;
    }

    mon = getub(buf, 4);
    day = getub(buf, 5);
    year = getbeuw(buf, 6);
    hour = getub(buf, 8);
    min = getub(buf, 9);
    sec = getub(buf, 10);
    nsec = getbeul(buf, 11);
    fprintf(stderr, "%u/%u/%u %02u:%02u:%02u.%09u\t", year, mon, day, hour, min, sec, nsec);

    lat = getbesl(buf, 15) / 3600000.0;
    lon = getbesl(buf, 19) / 3600000.0;
    alt = getbesl(buf, 23) / 100.0;
    speed = getbeuw(buf, 31) / 100.0;
    track = getbeuw(buf, 33) / 10.0;
    dop = getbeuw(buf, 35) / 1.0;
    fprintf(stderr, "%lf %lf %.2lfm | %.2fm/s %.1fdeg dop=%.1f\n", lat, lon, alt, speed, track, dop);
    mask |= LATLON_SET | ALTITUDE_SET | SPEED_SET | TRACK_SET ;

#if 0
    session->gpsdata.fix.eph = GET_POSITION_ERROR();
    session->gpsdata.fix.eps = GET_SPEED_ERROR();
    session->gpsdata.satellites_used = GET_SATELLITES_USED();
    session->gpsdata.hdop = GET_HDOP();
    session->gpsdata.vdop = GET_VDOP();
    /* other DOP if available */
    mask |= HDOP_SET | VDOP_SET | USED_SET;

    session->gpsdata.fix.mode = GET_FIX_MODE();
    session->gpsdata.status = GET_FIX_STATUS();

    /* CYCLE_START_SET if this message starts a reporting period */
    mask |= MODE_SET | STATUS_SET | CYCLE_START_SET ;
#endif
    return mask;
}

/**
 * GPS Leap Seconds
 */
static gps_mask_t
oncore_msg_utctime(struct gps_device_t *session, unsigned char *buf, size_t data_len)
{
#if 0
    double t;

    if (data_len != UTCTIME_MSG_LEN)
	return 0;

    gpsd_report(LOG_IO, "oncore UTCTIME - navigation data\n");
    /* if this protocol has a way to test message validity, use it */
    flags = GET_FLAGS();
    if ((flags & ONCORE_TIME_VALID) == 0)
	return 0;

    tow = GET_MS_TIMEOFWEEK();
    gps_week = GET_WEEKNUMBER();
    session->context->leap_seconds = GET_GPS_LEAPSECONDS();

    t = gpstime_to_unix(gps_week, tow/1000.0) - session->context->leap_seconds;
    session->gpsdata.sentence_time = session->gpsdata.fix.time = t;

    return TIME_SET | ONLINE_SET;
#endif
    return 0;
}

/**
 * GPS Satellite Info
 */
static gps_mask_t
oncore_msg_svinfo(struct gps_device_t *session, unsigned char *buf, size_t data_len)
{
    unsigned char i, st, nchan, nsv;
    unsigned int tow;

    if (data_len != 92 )
	return 0;

    gpsd_report(LOG_IO, "oncore SVINFO - satellite data\n");

    /*
     * some protocols have a variable length message listing only visible
     * satellites, even if there are less than the number of channels. others
     * have a fixed length message and send empty records for idle channels
     * that are not tracking or searching. whatever the case, nchan should
     * be set to the number of satellites which might be visible.
     */
    nchan = getub(buf, 4);
    gpsd_zero_satellites(&session->gpsdata);
    nsv = 0; /* number of actually used satellites */
    for (i = st = 0; i < nchan; i++) {
	/* get info for one channel/satellite */
	int off = 5+7*i;

	session->gpsdata.PRN[i]		= getub(buf, off);
	session->gpsdata.elevation[i]	= getub(buf, off+3);
	session->gpsdata.azimuth[i]	= getbeuw(buf, off+4);

	/*
	if (CHANNEL_USED_IN_SOLUTION(i))
	    session->gpsdata.used[nsv++] = session->gpsdata.PRN[i];

	if(session->gpsdata.PRN[i])
		st++;
		*/
    }
    session->gpsdata.satellites_used = nsv;
    session->gpsdata.satellites = st;
    return SATELLITE_SET;
}

/**
 * Parse the data from the device
 */
/*@ +charint @*/
gps_mask_t oncore_dispatch(struct gps_device_t *session, unsigned char *buf, size_t len)
{
    size_t i;
    int type, used, visible;

    if (len == 0)
	return 0;

    type = buf[2]<<8 | buf[3];

    /* we may need to dump the raw packet */
    gpsd_report(LOG_RAW, "raw oncore packet type 0x%04x length %d: %s\n",
	type, len, gpsd_hexdump_wrapper(buf, len, LOG_WARN));

   /*
    * XXX The tag field is only 8 bytes; be careful you do not overflow.
    * XXX Using an abbreviation (eg. "italk" -> "itk") may be useful.
    */
    (void)snprintf(session->gpsdata.tag, sizeof(session->gpsdata.tag),
	"MOT-%c%c", type>>8, type&0xff);

    switch (type)
    {
    case 'Bb':
	    return oncore_msg_svinfo(session, buf, len);
    case 'Ea':
	    return oncore_msg_navsol(session, buf, len);

    default:
	/* XXX This gets noisy in a hurry. Change once your driver works */
	gpsd_report(LOG_WARN, "unknown packet id @@%c%c length %d: %s\n",
	    type>>8, type&0xff, len, gpsd_hexdump_wrapper(buf, len, LOG_WARN));
	return 0;
    }
}
/*@ -charint @*/

/**********************************************************
 *
 * Externally called routines below here
 *
 **********************************************************/

static bool oncore_probe_detect(struct gps_device_t *session)
{
   /*
    * This method is used to elicit a positively identifying
    * response from a candidate device. Some drivers may use
    * this to test for the presence of a certain kernel module.
    */
   int test, satisfied;

   /* Your testing code here */
   test=satisfied=0;
   if (test==satisfied)
      return true;
   return false;
}

static void oncore_probe_wakeup(struct gps_device_t *session)
{
   /*
    * Code to make the device ready to communicate. This is
    * run every time we are about to try a different baud
    * rate in the autobaud sequence. Only needed if the
    * device is in some kind of sleeping state. If a wakeup
    * is not needed this method can be elided and the probe_wakeup
    * member of the gps_type_t structure can be set to NULL.
    */
}

static void oncore_probe_subtype(struct gps_device_t *session, unsigned int seq)
{
    /*
     * Probe for subtypes here. If possible, get the software version and
     * store it in session->subtype.  The seq values don't actually mean 
     * anything, but conditionalizing probes on them gives the device 
     * time to respond to each one.
     */
}

#ifdef ALLOW_CONTROLSEND
/**
 * Write data to the device, doing any required padding or checksumming
 */
/*@ +charint -usedef -compdef @*/
static ssize_t oncore_control_send(struct gps_device_t *session,
			   char *msg, size_t msglen)
{
   bool ok;

   /* CONSTRUCT THE MESSAGE */

   /* 
    * This copy to a public assembly buffer 
    * enables gpsmon to snoop the control message
    * acter it has been sent.
    */
   session->msgbuflen = msglen;
   (void)memcpy(session->msgbuf, msg, msglen);

   /* we may need to dump the message */
    return gpsd_write(session, session->msgbuf, session->msgbuflen);
   gpsd_report(LOG_IO, "writing oncore control type %02x:%s\n",
	       msg[0], gpsd_hexdump_wrapper(session->msgbuf, session->msgbuflen, LOG_IO));
   return gpsd_write(session, session->msgbuf, session->msgbuflen);
}
/*@ -charint +usedef +compdef @*/
#endif /* ALLOW_CONTROLSEND */

#ifdef ALLOW_RECONFIGURE
static void oncore_configurator(struct gps_device_t *session, unsigned int seq)
{
    /* 
     * Change sentence mix and set reporting modes as needed.
     * If your device has a default cycle time other than 1 second,
     * set session->device->gpsdata.cycle here.
     */
}

/*
 * This is the entry point to the driver. When the packet sniffer recognizes
 * a packet for this driver, it calls this method which passes the packet to
 * the binary processor or the nmea processor, depending on the session type.
 */
static gps_mask_t oncore_parse_input(struct gps_device_t *session)
{
    gps_mask_t st;

    if (session->packet.type == ONCORE_PACKET){
	st = oncore_dispatch(session, session->packet.outbuffer, session->packet.outbuflen);
	session->gpsdata.driver_mode = MODE_BINARY;
	return st;
#ifdef NMEA_ENABLE
    } else if (session->packet.type == NMEA_PACKET) {
	st = nmea_parse((char *)session->packet.outbuffer, session);
	session->gpsdata.driver_mode = MODE_NMEA;
	return st;
#endif /* NMEA_ENABLE */
    } else
	return 0;
}

static bool oncore_set_speed(struct gps_device_t *session, 
			      speed_t speed, char parity, int stopbits)
{
    /* 
     * Set port operating mode, speed, parity, stopbits etc. here.
     * Note: parity is passed as 'N'/'E'/'O', but you should program 
     * defensively and allow 0/1/2 as well.
     */
}

/*
 * Switch between NMEA and binary mode, if supported
 */
static void oncore_set_mode(struct gps_device_t *session, int mode)
{
    if (mode == MODE_NMEA) {
	// oncore_to_nmea(session->gpsdata.gps_fd,session->gpsdata.baudrate); /* send the mode switch control string */
	session->gpsdata.driver_mode = MODE_NMEA;
	/* 
	 * Anticipatory switching works only when the packet getter is the
	 * generic one and it recognizes packets of the type this driver 
	 * is expecting.  This should be the normal case.
	 */
	(void)gpsd_switch_driver(session, "Generic NMEA");
    } else {
	session->back_to_nmea = false;
	session->gpsdata.driver_mode = MODE_BINARY;
    }
}

static void oncore_revert(struct gps_device_t *session)
{
   /*
    * Reverse what the .configurator method changed.
    */
}
#else
#warning WTF - no ALLOW_RECONFIGURE
#endif /* ALLOW_RECONFIGURE */

static void oncore_wrapup(struct gps_device_t *session)
{
   /*
    * Do release actions that are independent of whether the .configurator 
    * method ran or not.
    */
}

/* This is everything we export */
const struct gps_type_t oncore_binary = {
    /* Full name of type */
    .type_name        = "oncore binary",
    /* associated lexer packet type */
    .packet_type    = ONCORE_PACKET,
    /* Response string that identifies device (not active) */
    .trigger          = NULL,
    /* Number of satellite channels supported by the device */
    .channels         = 12,
    /* Startup-time device detector */
    .probe_detect     = oncore_probe_detect,
    /* Wakeup to be done before each baud hunt */
    .probe_wakeup     = oncore_probe_wakeup,
    /* Initialize the device and get subtype */
    .probe_subtype    = oncore_probe_subtype,
    /* Packet getter (using default routine) */
    .get_packet       = generic_get,
    /* Parse message packets */
    .parse_packet     = oncore_parse_input,
    /* RTCM handler (using default routine) */
    .rtcm_writer      = pass_rtcm,
#ifdef ALLOW_CONTROLSEND
    /* Control string sender - should provide checksum and headers/trailer */
    .control_send   = oncore_control_send,
#endif /* ALLOW_CONTROLSEND */
#ifdef ALLOW_RECONFIGURE
    /* Enable what reports we need */
    .configurator     = oncore_configurator,
    /* Speed (baudrate) switch */
    .speed_switcher   = oncore_set_speed,
    /* Switch to NMEA mode */
    .mode_switcher    = oncore_set_mode,
    /* Message delivery rate switcher (not active) */
    .rate_switcher    = NULL,
    /* Minimum cycle time of the device */
    .min_cycle        = 1,
    /* Undo the actions of .configurator */
    .revert           = oncore_revert,
#endif /* ALLOW_RECONFIGURE */
    /* Puts device back to original settings */
    .wrapup           = oncore_wrapup,
};
#endif /* defined(ONCORE_ENABLE) && defined(BINARY_ENABLE) */

