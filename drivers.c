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

static void nmea_handle_input(struct gps_session_t *session)
{
    while (!session->outbuflen)
	packet_get_nmea(session);

    gpsd_report(2, "<= GPS: %s", session->outbuffer);
    if (session->outbuffer[0] == '$'  && session->outbuffer[1] == 'G') {
	if (nmea_parse(session->outbuffer, &session->gNMEAdata) < 0)
	    gpsd_report(2, "unknown sentence: \"%s\"\n", session->outbuffer);
    } else {
#if NON_NMEA_ENABLE
	struct gps_type_t **dp;

	/* maybe this is a trigger string for a driver we know about? */
	for (dp = gpsd_drivers; *dp; dp++) {
	    char	*trigger = (*dp)->trigger;

	    if (trigger && !strncmp(session->outbuffer, trigger, strlen(trigger)) && isatty(session->gNMEAdata.gps_fd)) {
		gpsd_report(1, "found %s.\n", (*dp)->typename);
		session->device_type = *dp;
		if (session->device_type->initializer)
		    session->device_type->initializer(session);
		packet_accept(session);
		return;
	    }
	}
#endif /* NON_NMEA_ENABLE */
	gpsd_report(1, "unknown exception: \"%s\"\n", session->outbuffer);
    }

    /* also copy the sentence up to clients in raw mode */
    gpsd_raw_hook(session, session->outbuffer);
    packet_accept(session);
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
    nmea_handle_input,	/* read text sentence */
    nmea_write_rtcm,	/* write RTCM data straight */
    NULL,		/* no speed switcher */
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

#if !BINARY_ENABLE
static void sirf_initializer(struct gps_session_t *session)
{
    if (!strcmp(session->device_type->typename, "SiRF-II")) 
	nmea_send(session->gNMEAdata.gps_fd, "$PSRF105,0");
}
#endif /* BINARY_ENABLE */

static int sirf_switcher(struct gps_session_t *session, int speed) 
/* switch GPS to specified mode at 8N1, optionally to binary */
{
    if (nmea_send(session->gNMEAdata.gps_fd, "$PSRF100,1,%d,8,1,0", speed) < 0)
	return 0;
    tcdrain(session->gNMEAdata.gps_fd);
    /* 
     * This definitely fails below 40 milliseconds on a BU-303b.
     * 50ms is also verified by Chris Kuethe on 
     *        Pharos iGPS360 + GSW 2.3.1ES + prolific
     *        Rayming TN-200 + GSW 2.3.1 + ftdi
     *        Rayming TN-200 + GSW 2.3.2 + ftdi
     * so it looks pretty solid.
     */
    usleep(50000);
    return 1;
}

struct gps_type_t sirfII = {
    "SiRF-II NMEA",	/* full name of type */
#if BINARY_ENABLE
    NULL,		/* recognizing SiRF flips us to binary */
    NULL,		/* no probe */
    NULL,		/* no initialization */
#else
    "$Ack Input105.",	/* expected response to SiRF PSRF105 */
    NULL,		/* no probe */
    sirf_initializer,	/* turn off debugging messages */
#endif /* BINARY_ENABLE */
    nmea_handle_input,	/* read text sentence */
    nmea_write_rtcm,	/* write RTCM data straight */
    sirf_switcher,	/* we can change speeds */
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
    nmea_handle_input,		/* read text sentence */
    nmea_write_rtcm,		/* send RTCM data straight */
    NULL,			/* no speed switcher */
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
 * Use the NMEA driver for that one.
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
    nmea_handle_input,		/* read text sentence */
    NULL,			/* don't send RTCM data */
    NULL,			/* no speed switcher */
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
#if ZODIAC_ENABLE
    &zodiac_binary,
#endif /* ZODIAC_ENABLE */
#if GARMIN_ENABLE
    &garmin_binary,
#endif /* GARMIN_ENABLE */
    &sirf_binary,
#ifdef LOGFILE_ENABLE
    &logfile,
#endif /* LOGFILE_ENABLE */
    NULL,
};
struct gps_type_t **gpsd_drivers = &gpsd_driver_array[0];
