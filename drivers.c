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
#ifdef PROFILING
    struct timeval tv;
    gettimeofday(&tv, NULL);
    session->gNMEAdata.d_xmit_time = TIME2DOUBLE(tv);
#endif /* PROFILING */

    if (!session->outbuflen)
	packet_get_nmea(session);

#ifdef PROFILING
    gettimeofday(&tv, NULL);
    session->gNMEAdata.d_recv_time = TIME2DOUBLE(tv);
#endif /* PROFILING */

    gpsd_report(2, "<= GPS: %s", session->outbuffer);
    if (session->outbuffer[0] == '$'  && session->outbuffer[1] == 'G') {
	if (nmea_parse(session->outbuffer, &session->gNMEAdata) < 0)
	    gpsd_report(2, "unknown sentence: \"%s\"\n", session->outbuffer);
#ifdef PROFILING
	else {
	    gettimeofday(&tv, NULL);
	    session->gNMEAdata.d_decode_time = TIME2DOUBLE(tv);
	}
#endif /* PROFILING */
    } else {
#ifdef NON_NMEA_ENABLE
	struct gps_type_t **dp;

	/* maybe this is a trigger string for a driver we know about? */
	for (dp = gpsd_drivers; *dp; dp++) {
	    char	*trigger = (*dp)->trigger;

	    if (trigger && !strncmp(session->outbuffer, trigger, strlen(trigger)) && isatty(session->gNMEAdata.gps_fd)) {
		gpsd_report(1, "found %s.\n", (*dp)->typename);
		session->device_type = *dp;
		if (session->device_type->initializer)
		    session->device_type->initializer(session);
		packet_discard(session);
		return;
	    }
	}
#endif /* NON_NMEA_ENABLE */
	gpsd_report(1, "unknown exception: \"%s\"\n", session->outbuffer);
    }

    /* also copy the sentence up to clients in raw mode */
    if (session->gNMEAdata.raw_hook)
	session->gNMEAdata.raw_hook(session->outbuffer);

    packet_discard(session);
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
    'n', 		/* select explicitly with -T n */
    "Generic NMEA",	/* full name of type */
    NULL,		/* no recognition string, it's the default */
    nmea_initializer,		/* probe for SiRF II */
    nmea_handle_input,	/* read text sentence */
    nmea_write_rtcm,	/* write RTCM data straight */
    NULL,		/* no speed switcher */
    NULL,		/* no wrapup */
    0,			/* perform baud-rate hunting */
    1,			/* 1 stop bit */
    1,			/* updates every second */
};

#ifdef SIRFII_ENABLE
/**************************************************************************
 *
 * SiRF-II
 *
 **************************************************************************/

static void sirf_initializer(struct gps_session_t *session)
{
    if (!strcmp(session->device_type->typename, "SiRF-II")) 
	nmea_send(session->gNMEAdata.gps_fd, "$PSRF105,0");
}

static int sirf_switcher(struct gps_session_t *session, int speed) 
/* switch GPS to specified mode at 8N1, optionally to binary */
{
    if (nmea_send(session->gNMEAdata.gps_fd, "$PSRF100,1,%d,8,1,0", speed) < 0)
	return 0;
#ifdef UNRELIABLE_SYNC
    gpsd_drain(session->gNMEAdata.gps_fd);
#endif /* UNRELIABLE_SYNC */
    return 1;
}

struct gps_type_t sirfII = {
    's', 		/* select explicitly with -T s */
    "SiRF-II NMEA",	/* full name of type */
    "$Ack Input105.",	/* expected response to SiRF PSRF105 */
    sirf_initializer,		/* no initialization */
    nmea_handle_input,	/* read text sentence */
    nmea_write_rtcm,	/* write RTCM data straight */
    sirf_switcher,	/* we can change speeds */
    NULL,		/* no wrapup */
    0,			/* perform baud-rate hunting */
    1,			/* 1 stop bit */
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
    time_t t;
    struct tm *tm;

    /* TripMate requires this response to the ASTRAL it sends at boot time */
    nmea_send(session->gNMEAdata.gps_fd, "$IIGPQ,ASTRAL");
    /* stop it sending PRWIZCH */
    nmea_send(session->gNMEAdata.gps_fd, "$PRWIILOG,ZCH,V,,");
    if (session->latitude && session->longitude) {
	t = time(NULL);
	tm = gmtime(&t);
	if (tm->tm_year > 100)
	    tm->tm_year = tm->tm_year - 100;
	nmea_send(session->gNMEAdata.gps_fd,
		"$PRWIINIT,V,,,%s,%c,%s,%c,100.0,0.0,M,0.0,T,%02d%02d%02d,%02d%02d%02d",
		session->latitude, session->latd, 
		session->longitude, session->lond,
		tm->tm_hour, tm->tm_min, tm->tm_sec,
		tm->tm_mday, tm->tm_mon + 1, tm->tm_year);
    }
}

struct gps_type_t tripmate = {
    't', 			/* select explicitly with -T t */
    "Delorme TripMate",		/* full name of type */
    "ASTRAL",			/* tells us to switch */
    tripmate_initializer,	/* wants to see lat/long for faster fix */
    nmea_handle_input,		/* read text sentence */
    nmea_write_rtcm,		/* send RTCM data straight */
    NULL,			/* no speed switcher */
    NULL,			/* no wrapup */
    4800,			/* default speed to connect at */
    1,				/* 1 stop bit */
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
    'e',			/* select explicitly with -T e */
    "Delorme EarthMate (pre-2003, Zodiac chipset)",	/* full name of type */
    "EARTHA",			/* tells us to switch to Earthmate */
    earthmate_initializer,	/* switch us to Zodiac mode */
    nmea_handle_input,		/* read text sentence */
    NULL,			/* don't send RTCM data */
    NULL,			/* no speed switcher */
    NULL,			/* no wrapup code */
    9600,			/* connecting at 4800 will fail */
    1,				/* 1 stop bit */
    1,				/* updates every second */
};
#endif /* EARTHMATE_ENABLE */

#ifdef LOGFILE_ENABLE
/**************************************************************************
 *
 * Logfile playback driver
 *
 **************************************************************************/

struct gps_type_t logfile = {
    'l',			/* select explicitly with -T l */
    "Logfile",			/* full name of type */
    NULL,			/* no recognition string */
    NULL,			/* no initializer */
    nmea_handle_input,		/* read text sentence */
    NULL,			/* don't send RTCM data */
    NULL,			/* no speed switcher */
    NULL,			/* no wrapup code */
    0,				/* don't set a speed */
    1,				/* 1 stop bit (not used) */
    -1,				/* should never time out */
};
#endif /* LOGFILE_ENABLE */

extern struct gps_type_t garmin_binary;

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
#ifdef ZODIAC_ENABLE
    &garmin_binary,
#endif /* ZODIAC_ENABLE */
#ifdef LOGFILE_ENABLE
    &logfile,
#endif /* LOGFILE_ENABLE */
    NULL,
};
struct gps_type_t **gpsd_drivers = &gpsd_driver_array[0];
