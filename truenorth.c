/* $Id$ */
/**
 * True North Technologies - Revolution 2X Digital compass
 *
 * More info: http://www.tntc.com/
 *
 * This is a digital compass which uses magnetometers to measure the
 * strength of the earth's magnetic field. Based on these measurements
 * it provides a compass heading using NMEA formatted output strings.
 * This is useful to supplement the heading provided by another GPS
 * unit. A GPS heading is unreliable at slow speed or no speed.
 */
#include "gpsd.h"

#ifdef TNT_ENABLE
/*
 * The True North compass won't start talking unless you ask it to. To
 * wake it up, we query for its ID string just after each speed change
 * in the autobaud hunt.  Then we send codes to start the flow of data.
 */
static void tnt_wakeup(struct gps_device_t *session)
{
    (void)nmea_send(session->gpsdata.gps_fd, "@X?");
    //nmea_send(session->gpsdata.gps_fd, "@BA?"); // Query current rate
    //nmea_send(session->gpsdata.gps_fd, "@BA=8"); // Start HTM packet at 1Hz
    /*
     * Sending this twice seems to make it more reliable!!
     * I think it gets the input on the unit synced up.
     * The intent is to start HTM packet reporting at 1200 per minute.
     */
    (void)nmea_send(session->gpsdata.gps_fd, "@BA=15");
    (void)nmea_send(session->gpsdata.gps_fd, "@BA=15");
}

struct gps_type_t trueNorth = {
    .typename       = "True North",	/* full name of type */
    .trigger        = " TNT1500",	/* how to recognize this */
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
