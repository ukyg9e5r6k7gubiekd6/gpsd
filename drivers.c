/*
 * Drivers for generic NMEA device, TripMate and Zodiac EarthMate in text mode.
 */
#include "config.h"
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <string.h>

#include "gps.h"
#include "gpsd.h"

/**************************************************************************
 *
 * Generic driver -- straight NMEA 0183
 *
 **************************************************************************/

void gpsd_NMEA_handle_message(struct gps_session_t *session, char *sentence)
/* visible so the direct-connect clients can use it */
{
    gpscli_report(2, "<= GPS: %s\n", sentence);
    if (*sentence == '$')
    {
	if (nmea_parse(sentence, &session->gNMEAdata) < 0)
	    /*
	     * Some of the stuff that comes out of supposedly NMEA-compliant
	     * GPses is a doozy.  For example, my BU-303 occasionally and
	     * randomly issues an ID message like this:
	     *
	     *  $Version 231.000.000_A2
	     *  $TOW: 506058
	     *  $WK:  1284
	     *  $POS: 1222777  -4734973 4081037
	     *  $CLK: 95872
	     *  $CHNL:12
	     *  $Baud rate: 4800  System clock: 12.277MHz
	     *  $HW Type: S2AM
	     *  $Asic Version: 0x23
	     *  $Clock Source: GPSCLK
	     *  $Internal Beacon: None
	     *
	     * Other SiRF-II-based GPSses probably do the same.
	     */
	    gpscli_report(2, "unknown sentence: \"%s\"\n", sentence);
    }
    else
    {
	struct gps_type_t **dp;

	/* maybe this is a trigger string for a driver we know about? */
	for (dp = gpsd_drivers; dp < gpsd_drivers + sizeof(gpsd_drivers)/sizeof(gpsd_drivers[0]); dp++)
	{
	    char	*trigger = (*dp)->trigger;

	    if (trigger && !strncmp(trigger, sentence, strlen(trigger)) && isatty(session->fdout)) {
		gpscli_report(1, "found %s.", (*dp)->typename);
		session->device_type = &zodiac_b;
		session->device_type->initializer(session);
		return;
	    }
	}
	gpscli_report(1, "unknown exception: \"%s\"\n", sentence);
    }
}

static int nmea_handle_input(struct gps_session_t *session)
{
    static unsigned char buf[BUFSIZE];	/* that is more then a sentence */
    static int offset = 0;

    while (offset < BUFSIZE) {
	if (read(session->fdin, buf + offset, 1) != 1)
	    return 1;

	if (buf[offset] == '\n' || buf[offset] == '\r') {
	    buf[offset] = '\0';
	    if (strlen(buf)) {
	        gpsd_NMEA_handle_message(session, buf);
		/* also copy the sentence up to clients in raw mode */
		strcat(buf, "\r\n");
		if (session->gNMEAdata.raw_hook)
		    session->gNMEAdata.raw_hook(buf);
	    }
	    offset = 0;
	    return 1;
	}

	offset++;
	buf[offset] = '\0';
    }
    offset = 0;			/* discard input ! */
    return 1;
}

static int nmea_write_rtcm(struct gps_session_t *session, char *buf, int rtcmbytes)
{
    return write(session->fdout, buf, rtcmbytes);
}

struct gps_type_t nmea =
{
    'n', 		/* select explicitly with -T n */
    "Generic NMEA",	/* full name of type */
    NULL,		/* no recognition string, it's the default */
    NULL,		/* no initialization */
    nmea_handle_input,	/* read text sentence */
    nmea_write_rtcm,	/* write RTCM data straight */
    NULL,		/* no wrapup */
    4800,		/* default speed to connect at */
    1,			/* updates every second */
};

/**************************************************************************
 *
 * FV18 -- doesn't send GPGSAs, uses 7N2
 *
 **************************************************************************/

void fv18_initializer(struct gps_session_t *session)
{
    /* FV18 sends 1 start bit, 8 bits, 1 stop bit, looking like 7N2 */ 
    gpsd_set_7N2();
    /* tell it to send GSAs so we'll know if 3D is accurate */
    write(session->fdout, "$PFEC,GPint,GSA01,DTM00,ZDA00,RMC01,GLL01*39\r\n",46);
}

struct gps_type_t fv18 =
{
    'f', 		/* select explicitly with -T f */
    "FV18",		/* full name of type */
    NULL,		/* no recognition string */
    fv18_initializer,	/* to be sent unconditionally */
    nmea_handle_input,	/* read text sentence */
    nmea_write_rtcm,	/* write RTCM data straight */
    NULL,		/* no wrapup */
    4800,		/* default speed to connect at */
    1,			/* updates every second */
};


#ifdef ENABLE_TRIPMATE
/**************************************************************************
 *
 * TripMate -- extended NMEA, gets faster fix when primed with lat/long/time
 *
 **************************************************************************/

/*
 * Some technical FAQs on the TripMate:
 * http://vancouver-webpages.com/pub/peter/tripmate.faq
 * http://www.asahi-net.or.jp/~KN6Y-GTU/tripmate/trmfaqe.html
 * Maybe we could use the logging-interval command?
 * According to file://localhost/home/esr/svn/gpsd/trunk/hardware.html,
 * sending "$PRWIIPRO,0,RBIN\r\n" snd waiting 1.5 seconds 
 * will switch the TripMate into Rockwell binary mode at 4800.
 *
 * The TripMate was discontinued sometime before November 1998
 * and was replaced by the Zodiac EarthMate.  In 2003, the Zodiac
 * chipset in the EarthMate was replaced with the SiRF 2.
 */

void tripmate_initializer(struct gps_session_t *session)
{
    char buf[82];
    time_t t;
    struct tm *tm;

    /* TripMate requires this response to the ASTRAL it sends at boot time */
    write(session->fdout, "$IIGPQ,ASTRAL*73\r\n", 18);
    /* stop it sending PRWIZCH */
    write(session->fdout, "$PRWIILOG,ZCH,V,,", 17);
    if (session->initpos.latitude && session->initpos.longitude) {
	t = time(NULL);
	tm = gmtime(&t);

	if(tm->tm_year > 100)
	    tm->tm_year = tm->tm_year - 100;

	sprintf(buf,
		"$PRWIINIT,V,,,%s,%c,%s,%c,100.0,0.0,M,0.0,T,%02d%02d%02d,%02d%02d%02d*",
		session->initpos.latitude, session->initpos.latd, 
		session->initpos.longitude, session->initpos.lond,
		tm->tm_hour, tm->tm_min, tm->tm_sec,
		tm->tm_mday, tm->tm_mon + 1, tm->tm_year);
	nmea_add_checksum(buf + 1);	/* add c-sum + cr/lf */
	if (session->fdout != -1) {
	    write(session->fdout, buf, strlen(buf));
	    gpscli_report(1, "=> GPS: %s", buf);
	}
    }
}

struct gps_type_t tripmate =
{
    't', 			/* select explicitly with -T t */
    "Delorme TripMate",		/* full name of type */
    "ASTRAL",			/* tells us to switch */
    tripmate_initializer,	/* wants to see lat/long for faster fix */
    nmea_handle_input,		/* read text sentence */
    nmea_write_rtcm,		/* send RTCM data straight */
    NULL,			/* no wrapup */
    4800,			/* default speed to connect at */
    1,				/* updates every second */
};
#endif /* ENABLE_TRIPMATE */


#ifdef ZODIAC_ENABLE
/**************************************************************************
 *
 * Zodiac EarthMate textual mode
 *
 **************************************************************************/

/*
 * Treat this as a straight NMEA device unless we get the exception
 * code back that says to go binary.  In that case process_exception() 
 * will flip us over to the zodiac_b driver.  But, connect at 9600
 * rather than 4800.  The Rockwell chipset does not accept DGPS in text 
 * mode.
 */

struct gps_type_t zodiac_a =
{
    'e',			/* select explicitly with -T e */
    "Delorme EarthMate (pre-2003, Zodiac chipset)",	/* full name of type */
    "EARTHA",			/* tells us to switch to Earthmate-B */
    NULL,			/* no initializer */
    nmea_handle_input,		/* read text sentence */
    NULL,			/* don't send RTCM data */
    NULL,			/* no wrapup code */
    9600,			/* connecting at 4800 will fail */
    1,				/* updates every second */
};
#endif /* ZODIAC_ENABLE */

/**************************************************************************
 *
 * Logfile playback driver
 *
 **************************************************************************/

struct gps_type_t logfile =
{
    'l',			/* select explicitly with -T l */
    "Logfile",			/* full name of type */
    NULL,			/* no recognition string */
    NULL,			/* no initializer */
    nmea_handle_input,		/* read text sentence */
    NULL,			/* don't send RTCM data */
    NULL,			/* no wrapup code */
    0,				/* don't set a speed */
    -1,				/* should never time out */
};

/* the point of this rigamarole is to not have to export a table size */
static struct gps_type_t *gpsd_driver_array[] = {
    &nmea, 
    &fv18,
#ifdef ENABLE_TRIPMATE
    &tripmate,
#endif /* ENABLE_TRIPMATE */
#ifdef ZODIAC_ENABLE
    &zodiac_a, 
    &zodiac_b,
#endif /* ZODIAC_ENABLE */
    &logfile,
    NULL,
};
struct gps_type_t **gpsd_drivers = &gpsd_driver_array[0];
