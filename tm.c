/*
 * Drivers for generic NMEA device, TripMate and EarthMate in text mode.
 */
#include "config.h"
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <string.h>

#include "gpsd.h"

#define BUFSIZE 4096

/**************************************************************************
 *
 * process_exception() -- handle returned sentences in non-NMEA form
 *
 **************************************************************************/

/*
 * This code is shared by all the NMEA variants. It's where we handle
 * the funky non-NMEA sentences that tell us about extensions.
 */

static void process_exception(struct gpsd_t *session, char *sentence)
{
    if (!strncmp("ASTRAL", sentence, 6) && isatty(session->fdout)) {
	write(session->fdout, "$IIGPQ,ASTRAL*73\r\n", 18);
	gpscli_report(1, "found a TripMate, initializing...");
	session->device_type = &tripmate;
	tripmate.initializer(session);
    } else if ((!strncmp("EARTHA", sentence, 6) && isatty(session->fdout))) {
	write(session->fdout, "EARTHA\r\n", 8);
	gpscli_report(1, "found an EarthMate (id).");
	session->device_type = &earthmate_b;
	earthmate_b.initializer(session);
    } else if (session->debug > 1) {
	gpscli_report(1, "unknown exception: \"%s\"\n", sentence);
    }
}


/**************************************************************************
 *
 * Generic driver -- straight NMEA 0183
 *
 **************************************************************************/

void gps_NMEA_handle_message(struct gpsd_t *session, char *sentence)
/* visible so the direct-connect clients can use it */
{
    gpscli_report(2, "<= GPS: %s\n", sentence);
    if (*sentence == '$')
    {
	if (gps_process_NMEA_message(sentence + 1, &session->gNMEAdata) < 0)
	    gpscli_report(2, "Unknown sentence: \"%s\"\n", sentence);
    }
    else
	process_exception(session, sentence);

    gpscli_report(3,
	   "Lat: %f Lon: %f Alt: %f Sat: %d Mod: %d Time: %s\n",
	   session->gNMEAdata.latitude,
	   session->gNMEAdata.longitude,
	   session->gNMEAdata.altitude,
	   session->gNMEAdata.satellites,
	   session->gNMEAdata.mode,
	   session->gNMEAdata.utc);
}

static int nmea_handle_input(struct gpsd_t *session)
{
    static unsigned char buf[BUFSIZE];	/* that is more then a sentence */
    static int offset = 0;

    while (offset < BUFSIZE) {
	if (read(session->fdin, buf + offset, 1) != 1)
	    return 1;

	if (buf[offset] == '\n' || buf[offset] == '\r') {
	    buf[offset] = '\0';
	    if (strlen(buf)) {
	        gps_NMEA_handle_message(session, buf);
		/* also copy the sentence up to clients in raw mode */
		strcat(buf, "\r\n");
		if (session->raw_hook)
		    session->raw_hook(buf);
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

static int nmea_write_rctm(struct gpsd_t *session, char *buf, int rtcmbytes)
{
    return write(session->fdout, buf, rtcmbytes);
}

struct gps_type_t nmea =
{
    'n', 		/* select explicitly with -T n */
    "NMEA",		/* full name of type */
    NULL,		/* no initialization */
    nmea_handle_input,	/* read text sentence */
    nmea_write_rctm,	/* write RCTM data straight */
    NULL,		/* no wrapup */
    4800,		/* default speed to connect at */
};

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
 */

void tripmate_initializer(struct gpsd_t *session)
{
    char buf[82];
    time_t t;
    struct tm *tm;

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
	gps_add_checksum(buf + 1);	/* add c-sum + cr/lf */
	if (session->fdout != -1) {
	    write(session->fdout, buf, strlen(buf));
	    gpscli_report(1, "=> GPS: %s", buf);
	}
    }
}

struct gps_type_t tripmate =
{
    't', 			/* select explicitly with -T t */
    "TripMate",			/* full name of type */
    tripmate_initializer,	/* wants to see lat/long for faster fix */
    nmea_handle_input,		/* read text sentence */
    nmea_write_rctm,		/* send RCTM data straight */
    NULL,			/* no wrapup */
    4800,			/* default speed to connect at */
};


/**************************************************************************
 *
 * EarthMate textual mode
 *
 **************************************************************************/

/*
 * Treat this as a straight NMEA device unless we get the exception
 * code back that says to go binary.  In that case process_exception() 
 * will flip us over to the earthmate_b driver.  But, connect at 9600
 * rather than 4800.
 */

struct gps_type_t earthmate_a =
{
    'e',			/* select explicitly with -T e */
    "EarthMate (a)",		/* full name of type */
    NULL,			/* no initializer */
    nmea_handle_input,		/* read text sentence */
    NULL,			/* don't send RCTM data */
    NULL,			/* no wrapup code */
    9600,			/* connecting at 4800 will fail */
};

/**************************************************************************
 *
 * Logfile playback driver
 *
 **************************************************************************/

struct gps_type_t logfile =
{
    'l',			/* select explicitly with -T l */
    "Logfile",			/* full name of type */
    NULL,			/* no initializer */
    nmea_handle_input,		/* read text sentence */
    NULL,			/* don't send RCTM data */
    NULL,			/* no wrapup code */
    0,				/* don't set a speed */
};





