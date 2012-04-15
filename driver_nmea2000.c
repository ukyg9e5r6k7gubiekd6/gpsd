/*
 * NMEA2000 over CAN.
 *
 * Placeholder code. actual function needs to be added.
 * This file is Copyright (c) 2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <termios.h>
#ifndef S_SPLINT_S
#include <unistd.h>
#endif /* S_SPLINT_S */

#include "gpsd.h"
#include "bits.h"

static ssize_t nmea2000_get(struct gps_device_t *session UNUSED)
{
    return 0;
}

static gps_mask_t nmea2000_parse_input(struct gps_device_t *session UNUSED)
{
    return ERROR_SET;
}

/* *INDENT-OFF* */
const struct gps_type_t nmea2000 = {
    .type_name      = "NMEA2000",       /* full name of type */
    .packet_type    = NMEA2000_PACKET,	/* associated lexer packet type */
    .flags	    = DRIVER_NOFLAGS,	/* no rollover or other flags */
    .trigger	    = NULL,		/* detect their main sentence */
    .channels       = 0,		/* not an actual GPS at all */
    .probe_detect   = NULL,
    .get_packet     = nmea2000_get,	/* how to get a packet */
    .parse_packet   = nmea2000_parse_input,	/* how to interpret a packet */
    .rtcm_writer    = NULL,		/* Don't send RTCM to this */
    .event_hook     = NULL,
#ifdef RECONFIGURE_ENABLE
    .speed_switcher = NULL,		/* no speed switcher */
    .mode_switcher  = NULL,		/* no mode switcher */
    .rate_switcher  = NULL,		/* no rate switcher */
    .min_cycle      = 1,		/* nominal 1-per-second GPS cycle */
#endif /* RECONFIGURE_ENABLE */
#ifdef CONTROLSEND_ENABLE
    .control_send   = NULL,		/* how to send control strings */
#endif /* CONTROLSEND_ENABLE */
#ifdef NTPSHM_ENABLE
    .ntp_offset     = NULL,
#endif /* NTPSHM_ ENABLE */
};
/* *INDENT-ON* */

/* end */
