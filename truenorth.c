/* $Id$ */
/**
 * True North Technologies - Revolution 2X Digital compass
 *
 * More info: http://www.tntc.com/
 *
 * This is a digital compass which uses magnetometers to measure
 * the strength of the earth's magnetic field. Based on these
 * measurements it provides a compass heading using NMEA
 * formatted output strings. I threw this into gpsd since it
 * already has convienient NMEA parsing support. Also because
 * I use the compass to suplement the heading provided by
 * another gps unit. A gps heading is unreliable at slow 
 * speed or no speed.
 *
 */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <math.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <stdarg.h>

#include "gpsd.h"

#ifdef TNT_ENABLE

static void tnt_add_checksum(char *sentence)
{
    unsigned char sum = '\0';
    char c, *p = sentence;

    if (*p == '@') {
	p++;
    } else {
        gpsd_report(1, "Bad TNT sentence: '%s'\n", sentence);
    }
    while ( ((c = *p) != '*') && (c != '\0')) {
	sum ^= c;
	p++;
    }
    *p++ = '*';
    /*@i@*/snprintf(p, 4, "%02X\r\n", sum);
}

static int tnt_send(int fd, const char *fmt, ... )
{
    int status;
    char buf[BUFSIZ];
    va_list ap;

    va_start(ap, fmt) ;
    (void)vsnprintf(buf, sizeof(buf)-5, fmt, ap);
    va_end(ap);
    strlcat(buf, "*", BUFSIZ);
    tnt_add_checksum(buf);
    status = (int)write(fd, buf, strlen(buf));
    tcdrain(fd);
    if (status == (int)strlen(buf)) {
	gpsd_report(2, "=> GPS: %s\n", buf);
	return status;
    } else {
	gpsd_report(2, "=> GPS: %s FAILED\n", buf);
	return -1;
    }
}

/*
 * The True North compass won't start talking unless you ask it to. To
 * wake it up, we query for its ID string just after each speed change
 * in the autobaud hunt.  Then we send codes to start the flow of data.
 */
static int tnt_wakeup(struct gps_device_t *session)
{
    (void)tnt_send(session->gpsdata.gps_fd, "@X?");
    //tnt_send(session->gpsdata.gps_fd, "@BA?"); // Query current rate
    //tnt_send(session->gpsdata.gps_fd, "@BA=8"); // Start HTM packet at 1Hz
    /*
     * Sending this twice seems to make it more reliable!!
     * I think it gets the input on the unit synced up.
     * The intent is to start HTM packet reporting at 1200 per minute.
     */
    (void)tnt_send(session->gpsdata.gps_fd, "@BA=15");
    (void)tnt_send(session->gpsdata.gps_fd, "@BA=15");
}

struct gps_type_t trueNorth = {
    .typename       = "True North",	/* full name of type */
    .trigger        = " TNT1500",
    .channels       = 0,		/* not an actual GPS at all */
    .wakeup         = tnt_wakeup,	/* wakeup by sending ID query */
    .probe          = NULL,		/* no probe */
    .initializer    = NULL,		/* no initializer */
    .get_packet     = packet_get,	/* how to get a packet */
    .parse_packet   = nmea_parse_input,	/* how to interpret a packet */
    .rtcm_writer    = NULL,	        /* Don't send */
    .speed_switcher = NULL,		/* no speed switcher */
    .mode_switcher  = NULL,		/* no mode switcher */
    .rate_switcher  = NULL,		/* no wrapup */
    .cycle_chars    = -1,		/* not relevant, no rate switch */
    .wrapup         = NULL,		/* no wrapup */
    .cycle          = 20,		/* updates per second */
};
#endif
