#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>

#include "gpsd.h"

/**************************************************************************
 *
 * Generic driver -- straight NMEA 0183
 *
 **************************************************************************/

static int nmea_parse_input(struct gps_session_t *session)
{
    if (session->packet_type == SIRF_PACKET) {
	gpsd_report(2, "SiRF packet seen when NMEA expected.\n");
	sirf_parse(session, session->outbuffer, session->outbuflen);
	return 1;
    } else if (session->packet_type == NMEA_PACKET) {
	gpsd_report(2, "<= GPS: %s", session->outbuffer);
	if (session->outbuffer[0] == '$'  && session->outbuffer[1] == 'G') {
	    if (nmea_parse(session->outbuffer, &session->gNMEAdata) < 0)
		gpsd_report(2, "unknown sentence: \"%s\"\n", session->outbuffer);
	} else {
#ifdef NON_NMEA_ENABLE
	    struct gps_type_t **dp;

	    /* maybe this is a trigger string for a driver we know about? */
	    for (dp = gpsd_drivers; *dp; dp++) {
		char	*trigger = (*dp)->trigger;

		if (trigger && !strncmp(session->outbuffer, trigger, strlen(trigger)) && isatty(session->gNMEAdata.gps_fd)) {
		    gpsd_report(1, "found %s.\n", trigger);
		    gpsd_switch_driver(session, (*dp)->typename);
		    return 1;
		}
	    }
#endif /* NON_NMEA_ENABLE */
	    gpsd_report(1, "unknown exception: \"%s\"\n", session->outbuffer);
	}

	/* also copy the sentence up to clients in raw mode */
	gpsd_raw_hook(session, session->outbuffer);
	return 1;
    } else
	return 0;
}

static int nmea_write_rtcm(struct gps_session_t *session, char *buf, int rtcmbytes)
{
    return write(session->gNMEAdata.gps_fd, buf, rtcmbytes);
}

static void nmea_initializer(struct gps_session_t *session)
{
    /* tell an FV18 to send GSAs so we'll know if 3D is accurate */
    nmea_send(session->gNMEAdata.gps_fd, "$PFEC,GPint,GSA01,DTM00,ZDA00,RMC01,GLL01,GSV05");
    /* probe for SiRF-II */
    nmea_send(session->gNMEAdata.gps_fd, "$PSRF105,1");
}

struct gps_type_t nmea = {
    "Generic NMEA",	/* full name of type */
    NULL,		/* no recognition string, it's the default */
    NULL,		/* no probe */
    nmea_initializer,		/* probe for SiRF II */
    packet_get,		/* how to get a packet */
    nmea_parse_input,	/* how to interpret a packet */
    nmea_write_rtcm,	/* write RTCM data straight */
    NULL,		/* no speed switcher */
    NULL,		/* no mode switcher */
    NULL,		/* no wrapup */
    1,			/* updates every second */
};

#ifdef SIRFII_ENABLE
/**************************************************************************
 *
 * SiRF-II NMEA
 *
 * This NMEA -mode driver is a fallback in case the SiRF chipset has
 * firmware too old for binary to be useful.
 *
 **************************************************************************/

#ifndef BINARY_ENABLE
static void sirf_initializer(struct gps_session_t *session)
{
    /* nmea_send(session->gNMEAdata.gps_fd, "$PSRF105,0"); */
    nmea_send(session->gNMEAdata.gps_fd, "$PSRF103,05,00,00,01"); /* no VTG */
    nmea_send(session->gNMEAdata.gps_fd, "$PSRF103,01,00,00,01"); /* no GLL */
}
#endif /* BINARY_ENABLE */

static int sirf_switcher(struct gps_session_t *session, int nmea, int speed) 
/* switch GPS to specified mode at 8N1, optionally to binary */
{
    if (nmea_send(session->gNMEAdata.gps_fd, "$PSRF100,%d,%d,8,1,0", nmea,speed) < 0)
	return 0;
    return 1;
}

static int sirf_speed(struct gps_session_t *session, int speed)
/* change the baud rate, remaining in SiRF NMWA mode */
{
    return sirf_switcher(session, 1, speed);
}

static void sirf_mode(struct gps_session_t *session, int mode)
/* change mode to SiRF binary, speed unchanged */
{
    if (mode == 1) {
	gpsd_switch_driver(session, "SiRF-II binary");
	session->gNMEAdata.driver_mode = sirf_switcher(session, 0, session->gNMEAdata.baudrate);
    } else
	session->gNMEAdata.driver_mode = 0;
}

struct gps_type_t sirfII = {
    "SiRF-II NMEA",	/* full name of type */
#ifdef BINARY_ENABLE
    NULL,		/* recognizing SiRF flips us to binary */
    NULL,		/* no probe */
    NULL,		/* no initialization */
#else
    "$Ack Input105.",	/* expected response to SiRF PSRF105 */
    NULL,		/* no probe */
    sirf_initializer,	/* turn off debugging messages */
#endif /* BINARY_ENABLE */
    packet_get,		/* how to get a packet */
    nmea_parse_input,	/* how to interpret a packet */
    nmea_write_rtcm,	/* write RTCM data straight */
    sirf_speed,		/* we can change speeds */
    sirf_mode,		/* there's a mode switch */
    NULL,		/* no wrapup */
    1,			/* updates every second */
};
#endif /* SIRFII_ENABLE */

#if TRIPMATE_ENABLE
/**************************************************************************
 *
 * TripMate -- extended NMEA, gets faster fix when primed with lat/long/time
 *
 **************************************************************************/

/*
 * Some technical FAQs on the TripMate:
 * http://vancouver-webpages.com/pub/peter/tripmate.faq
 * http://www.asahi-net.or.jp/~KN6Y-GTU/tripmate/trmfaqe.html
 * The TripMate was discontinued sometime before November 1998
 * and was replaced by the Zodiac EarthMate.
 */

static void tripmate_initializer(struct gps_session_t *session)
{
    /* TripMate requires this response to the ASTRAL it sends at boot time */
    nmea_send(session->gNMEAdata.gps_fd, "$IIGPQ,ASTRAL");
    /* stop it sending PRWIZCH */
    nmea_send(session->gNMEAdata.gps_fd, "$PRWIILOG,ZCH,V,,");
}

struct gps_type_t tripmate = {
    "Delorme TripMate",		/* full name of type */
    "ASTRAL",			/* tells us to switch */
    NULL,			/* no probe */
    tripmate_initializer,	/* wants to see lat/long for faster fix */
    packet_get,			/* how to get a packet */
    nmea_parse_input,		/* how to interpret a packet */
    nmea_write_rtcm,		/* send RTCM data straight */
    NULL,			/* no speed switcher */
    NULL,			/* no mode switcher */
    NULL,			/* no wrapup */
    1,				/* updates every second */
};
#endif /* TRIPMATE_ENABLE */

#ifdef EARTHMATE_ENABLE
/**************************************************************************
 *
 * Zodiac EarthMate textual mode
 *
 * Note: This is the pre-2003 version using Zodiac binary protocol.
 * It has been replaced with a design that uses a SiRF-II chipset.
 *
 **************************************************************************/

extern struct gps_type_t zodiac_binary, earthmate;

/*
 * There is a good HOWTO at <http://www.hamhud.net/ka9mva/earthmate.htm>.
 */

static void earthmate_close(struct gps_session_t *session)
{
    session->device_type = &earthmate;
}

static void earthmate_initializer(struct gps_session_t *session)
{
    write(session->gNMEAdata.gps_fd, "EARTHA\r\n", 8);
    sleep(30);
    session->device_type = &zodiac_binary;
    zodiac_binary.wrapup = earthmate_close;
    zodiac_binary.initializer(session);
}

struct gps_type_t earthmate = {
    "Delorme EarthMate (pre-2003, Zodiac chipset)",	/* full name of type */
    "EARTHA",			/* tells us to switch to Earthmate */
    NULL,			/* no probe */
    earthmate_initializer,	/* switch us to Zodiac mode */
    packet_get,			/* how to get a packet */
    nmea_parse_input,		/* how to interpret a packet */
    NULL,			/* don't send RTCM data */
    NULL,			/* no speed switcher */
    NULL,			/* no mode switcher */
    NULL,			/* no wrapup code */
    1,				/* updates every second */
};
#endif /* EARTHMATE_ENABLE */

extern struct gps_type_t garmin_binary, sirf_binary;

/* the point of this rigamarole is to not have to export a table size */
static struct gps_type_t *gpsd_driver_array[] = {
    &nmea, 
#ifdef SIRFII_ENABLE
    &sirfII, 
#endif /* SIRFII_ENABLE */
#if TRIPMATE_ENABLE
    &tripmate,
#endif /* TRIPMATE_ENABLE */
#if EARTHMATE_ENABLE
    &earthmate, 
#endif /* EARTHMATE_ENABLE */
#ifdef ZODIAC_ENABLE
    &zodiac_binary,
#endif /* ZODIAC_ENABLE */
#if GARMIN_ENABLE
    &garmin_binary,
#endif /* GARMIN_ENABLE */
#ifdef SIRFII_ENABLE
    &sirf_binary, 
#endif /* SIRFII_ENABLE */
    NULL,
};
struct gps_type_t **gpsd_drivers = &gpsd_driver_array[0];
