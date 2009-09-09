/* $Id$
 *
 * A prototype driver.  Doesn't run, doesn't even compile.
 *
 * For new driver authors: replace "_PROTO_" and "_proto_" with the name of
 * your new driver. That will give you a skeleton with all the required
 * functions defined.
 *
 * Once that is done, you will likely have to define a large number of
 * flags and masks. From there, you will be able to start extracting
 * useful quantities. There are roughed-in decoders for the navigation
 * solution, satellite status and gps-utc offset. These are the 3 key
 * messages that gpsd needs. Some protocols transmit error estimates
 * separately from the navigation solution; if developing a driver for
 * such a protocol you will need to add a decoder function for that
 * message.
 *
 * For anyone hacking this driver skeleton: "_PROTO_" and "_proto_" are now
 * reserved tokens. We suggest that they only ever be used as prefixes,
 * but if they are used infix, they must be used in a way that allows a
 * driver author to find-and-replace to create a unique namespace for
 * driver functions.
 *
 * If using vi, ":%s/_PROTO_/MYDRIVER/g" and ":%s/_proto_/mydriver/g"
 * should produce a source file that comes very close to being useful.
 * You will also need to add hooks for your new driver to:
 * Makefile.am
 * drivers.c
 * gpsd.h-tail
 * libgpsd_core.c
 * packet.c
 * packet_states.h
 *
 * see http://svn.berlios.de/viewvc/gpsd/trunk/?sortby=date&pathrev=5078
 * for an example of how a new driver arrived.
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
#if defined(_PROTO__ENABLE) && defined(BINARY_ENABLE)

#include "bits.h"

/*
 * These routines are specific to this driver
 */

static	gps_mask_t _proto__parse_input(struct gps_device_t *);
static	gps_mask_t _proto__dispatch(struct gps_device_t *, unsigned char *, size_t );
static	gps_mask_t _proto__msg_navsol(struct gps_device_t *, unsigned char *, size_t );
static	gps_mask_t _proto__msg_utctime(struct gps_device_t *, unsigned char *, size_t );
static	gps_mask_t _proto__msg_svinfo(struct gps_device_t *, unsigned char *, size_t );

/*
 * These methods may be called elsewhere in gpsd
 */
static	ssize_t _proto__control_send(struct gps_device_t *, char *, size_t );
static	bool _proto__probe_detect(struct gps_device_t *);
static	void _proto__probe_wakeup(struct gps_device_t *);
static	void _proto__probe_subtype(struct gps_device_t *, unsigned int );
static	void _proto__configurator(struct gps_device_t *, unsigned int );
static	bool _proto__set_speed(struct gps_device_t *, speed_t, char, int );
static	void _proto__set_mode(struct gps_device_t *, int );
static	void _proto__revert(struct gps_device_t *);
static	void _proto__wrapup(struct gps_device_t *);

/*
 * Decode the navigation solution message
 */
static gps_mask_t
_proto__msg_navsol(struct gps_device_t *session, unsigned char *buf, size_t data_len)
{
    gps_mask_t mask;
    int flags;
    double Px, Py, Pz, Vx, Vy, Vz;

    if (data_len != _PROTO__NAVSOL_MSG_LEN)
	return 0;

    gpsd_report(LOG_IO, "_proto_ NAVSOL - navigation data\n");
    /* if this protocol has a way to test message validity, use it */
    flags = GET_FLAGS();
    if ((flags & _PROTO__SOLUTION_VALID) == 0)
	return 0;

    mask = ONLINE_SET;

    /* extract ECEF navigation solution here */
    /* or extract the local tangential plane (ENU) solution */
    [Px, Py, Pz, Vx, Vy, Vz] = GET_ECEF_FIX();
    ecef_to_wgs84fix(&session->gpsdata, Px, Py, Pz, Vx, Vy, Vz);
    mask |= LATLON_SET | ALTITUDE_SET | SPEED_SET | TRACK_SET | CLIMB_SET  ;

    session->gpsdata.fix.epx = GET_LONGITUDE_ERROR();
    session->gpsdata.fix.epy = GET_LATITUDE_ERROR();
    session->gpsdata.fix.eps = GET_SPEED_ERROR();
    session->gpsdata.satellites_used = GET_SATELLITES_USED();
    session->gpsdata.hdop = GET_HDOP();
    session->gpsdata.vdop = GET_VDOP();
    /* other DOP if available */
    mask |= HDOP_SET | VDOP_SET | USED_SET;

    session->gpsdata.fix.mode = GET_FIX_MODE();
    session->gpsdata.status = GET_FIX_STATUS();

    /*
     * Set cycle_state to the value cycle_start to clue the daemon
     * in about when to clear fix information.  Set it to cycle_end
     * when the sentence is reliably the last in a reporting cycle.
     */
    session->cycle_state = STATE;
    mask |= MODE_SET | STATUS_SET;

    return mask;
}

/**
 * GPS Leap Seconds
 */
static gps_mask_t
_proto__msg_utctime(struct gps_device_t *session, unsigned char *buf, size_t data_len)
{
    double t;

    if (data_len != UTCTIME_MSG_LEN)
	return 0;

    gpsd_report(LOG_IO, "_proto_ UTCTIME - navigation data\n");
    /* if this protocol has a way to test message validity, use it */
    flags = GET_FLAGS();
    if ((flags & _PROTO__TIME_VALID) == 0)
	return 0;

    tow = GET_MS_TIMEOFWEEK();
    gps_week = GET_WEEKNUMBER();
    session->context->leap_seconds = GET_GPS_LEAPSECONDS();

    t = gpstime_to_unix(gps_week, tow/1000.0) - session->context->leap_seconds;
    session->gpsdata.sentence_time = session->gpsdata.fix.time = t;

    return TIME_SET | ONLINE_SET;
}

/**
 * GPS Satellite Info
 */
static gps_mask_t
_proto__msg_svinfo(struct gps_device_t *session, unsigned char *buf, size_t data_len)
{
    unsigned char i, st, nchan, nsv;
    unsigned int tow;

    if (data_len != SVINFO_MSG_LEN )
	return 0;

    gpsd_report(LOG_IO, "_proto_ SVINFO - navigation data\n");
    /* if this protocol has a way to test message validity, use it */
    flags = GET_FLAGS();
    if ((flags & _PROTO__SVINFO_VALID) == 0)
	return 0;

    /*
     * some protocols have a variable length message listing only visible
     * satellites, even if there are less than the number of channels. others
     * have a fixed length message and send empty records for idle channels
     * that are not tracking or searching. whatever the case, nchan should
     * be set to the number of satellites which might be visible.
     */
    nchan = GET_NUMBER_OF_CHANNELS();
    gpsd_zero_satellites(&session->gpsdata);
    nsv = 0; /* number of actually used satellites */
    for (i = st = 0; i < nchan; i++) {
	/* get info for one channel/satellite */
	int off = GET_CHANNEL_STATUS(i);

	session->gpsdata.PRN[i]		= PRN_THIS_CHANNEL_IS_TRACKING(i);
	session->gpsdata.ss[i]		= (float)SIGNAL_STRENGTH_FOR_CHANNEL(i);
	session->gpsdata.elevation[i]	= SV_ELEVATION_FOR_CHANNEL(i);
	session->gpsdata.azimuth[i]	= SV_AZIMUTH_FOR_CHANNEL(i);

	if (CHANNEL_USED_IN_SOLUTION(i))
	    session->gpsdata.used[nsv++] = session->gpsdata.PRN[i];

	if(session->gpsdata.PRN[i])
		st++;
    }
    session->gpsdata.satellites_used = nsv;
    session->gpsdata.satellites = st;
    return SATELLITE_SET | USED_SET;
}

/**
 * Parse the data from the device
 */
/*@ +charint @*/
gps_mask_t _proto__dispatch(struct gps_device_t *session, unsigned char *buf, size_t len)
{
    size_t i;
    int type, used, visible;

    if (len == 0)
	return 0;

    /*
     * Set this if the driver reliably signals end of cycle.
     * The core library zeroes it just before it calls each driver's
     * packet analyzer.
     */
    session->cycle_state = CYCLE_END_RELIABLE;
    if (msgid == MY_START_OF_CYCLE)
	session->cycle_state |= CYCLE_START;
    else if (msgid == MY_END_OF_CYCLE)
	session->cycle_state |= CYCLE_END;

    type = GET_MESSAGE_TYPE();

    /* we may need to dump the raw packet */
    gpsd_report(LOG_RAW, "raw _proto_ packet type 0x%02x length %d: %s\n",
	type, len, gpsd_hexdump_wrapper(buf, len, LOG_WARN));

   /*
    * XXX The tag field is only 8 bytes; be careful you do not overflow.
    * XXX Using an abbreviation (eg. "italk" -> "itk") may be useful.
    */
    (void)snprintf(session->gpsdata.tag, sizeof(session->gpsdata.tag),
	"_PROTO_%02x", type);

    switch (type)
    {
	/* Deliver message to specific decoder based on message type */

    default:
	/* XXX This gets noisy in a hurry. Change once your driver works */
	gpsd_report(LOG_WARN, "unknown packet id %d length %d: %s\n",
	    type, len, gpsd_hexdump_wrapper(buf, len, LOG_WARN));
	return 0;
    }
}
/*@ -charint @*/

/**********************************************************
 *
 * Externally called routines below here
 *
 **********************************************************/

static bool _proto__probe_detect(struct gps_device_t *session)
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

static void _proto__probe_wakeup(struct gps_device_t *session)
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

static void _proto__probe_subtype(struct gps_device_t *session, unsigned int seq)
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
static ssize_t _proto__control_send(struct gps_device_t *session,
			   char *msg, size_t msglen)
{
   bool ok;

   /* CONSTRUCT THE MESSAGE */

   /* 
    * This copy to a public assembly buffer 
    * enables gpsmon to snoop the control message
    * after it has been sent.
    */
   session->msgbuflen = msglen;
   (void)memcpy(session->msgbuf, msg, msglen);

   /* we may need to dump the message */
    return gpsd_write(session, session->msgbuf, session->msgbuflen);
   gpsd_report(LOG_IO, "writing _proto_ control type %02x:%s\n",
	       msg[0], gpsd_hexdump_wrapper(session->msgbuf, session->msgbuflen, LOG_IO));
   return gpsd_write(session, session->msgbuf, session->msgbuflen);
}
/*@ -charint +usedef +compdef @*/
#endif /* ALLOW_CONTROLSEND */

#ifdef ALLOW_RECONFIGURE
static void _proto__configurator(struct gps_device_t *session, unsigned int seq)
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
static gps_mask_t _proto__parse_input(struct gps_device_t *session)
{
    gps_mask_t st;

    if (session->packet.type == _PROTO__PACKET){
	st = _proto__dispatch(session, session->packet.outbuffer, session->packet.outbuflen);
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

static bool _proto__set_speed(struct gps_device_t *session, 
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
static void _proto__set_mode(struct gps_device_t *session, int mode)
{
    if (mode == MODE_NMEA) {
	// _proto__to_nmea(session->gpsdata.gps_fd,session->gpsdata.baudrate); /* send the mode switch control string */
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

static void _proto__revert(struct gps_device_t *session)
{
   /*
    * Reverse what the .configurator method changed.
    */
}
#endif /* ALLOW_RECONFIGURE */

static void _proto__wrapup(struct gps_device_t *session)
{
   /*
    * Do release actions that are independent of whether the .configurator 
    * method ran or not.
    */
}

/* The methods in this code take parameters and have */
/* return values that conform to the requirements AT */
/* THE TIME THE CODE WAS WRITTEN.                    */
/*                                                   */
/* These values may well have changed by the time    */
/* you read this and methods could have been added   */
/* or deleted. Unused methods can be set to NULL.    */
/*                                                   */
/* The latest version can be found by inspecting   */
/* the contents of struct gps_type_t in gpsd.h.      */
/*                                                   */
/* This always contains the correct definitions that */
/* any driver must use to compile.                   */

/* This is everything we export */
const struct gps_type_t _proto__binary = {
    /* Full name of type */
    .type_name        = "_proto_ binary",
    /* Associated lexer packet type */
    .packet_type      = _PROTO__PACKET,
    /* Response string that identifies device (not active) */
    .trigger          = NULL,
    /* Number of satellite channels supported by the device */
    .channels         = 12,
    /* Startup-time device detector */
    .probe_detect     = _proto__probe_detect,
    /* Wakeup to be done before each baud hunt */
    .probe_wakeup     = _proto__probe_wakeup,
    /* Initialize the device and get subtype */
    .probe_subtype    = _proto__probe_subtype,
    /* Packet getter (using default routine) */
    .get_packet       = generic_get,
    /* Parse message packets */
    .parse_packet     = _proto__parse_input,
    /* RTCM handler (using default routine) */
    .rtcm_writer      = pass_rtcm,
#ifdef ALLOW_CONTROLSEND
    /* Control string sender - should provide checksum and headers/trailer */
    .control_send   = _proto__control_send,
#endif /* ALLOW_CONTROLSEND */
#ifdef ALLOW_RECONFIGURE
    /* Enable what reports we need */
    .configurator     = _proto__configurator,
    /* Speed (baudrate) switch */
    .speed_switcher   = _proto__set_speed,
    /* Switch to NMEA mode */
    .mode_switcher    = _proto__set_mode,
    /* Message delivery rate switcher (not active) */
    .rate_switcher    = NULL,
    /* Minimum cycle time of the device */
    .min_cycle        = 1,
    /* Undo the actions of .configurator */
    .revert           = _proto__revert,
#endif /* ALLOW_RECONFIGURE */
    /* Puts device back to original settings */
    .wrapup           = _proto__wrapup,
};
#endif /* defined(_PROTO__ENABLE) && defined(BINARY_ENABLE) */

