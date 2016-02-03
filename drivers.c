/*
 * This file is Copyright (c) 2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>
#include <ctype.h>
#include <unistd.h>

#include "gpsd.h"
#include "bits.h"		/* for getbeu16(), to extract big-endian words */
#include "strfuncs.h"

ssize_t generic_get(struct gps_device_t *session)
{
    return packet_get(session->gpsdata.gps_fd, &session->lexer);
}

gps_mask_t generic_parse_input(struct gps_device_t *session)
{
    if (session->lexer.type == BAD_PACKET)
	return 0;
    else if (session->lexer.type == COMMENT_PACKET) {
	gpsd_set_century(session);
	return 0;
#ifdef NMEA0183_ENABLE
    } else if (session->lexer.type == NMEA_PACKET) {
	const struct gps_type_t **dp;
	gps_mask_t st = 0;
	char *sentence = (char *)session->lexer.outbuffer;

	if (sentence[strlen(sentence)-1] != '\n')
	    gpsd_log(&session->context->errout, LOG_IO,
		     "<= GPS: %s\n", sentence);
	else
	    gpsd_log(&session->context->errout, LOG_IO,
		     "<= GPS: %s", sentence);

	if ((st=nmea_parse(sentence, session)) == 0) {
	    gpsd_log(&session->context->errout, LOG_WARN,
		     "unknown sentence: \"%s\"\n",	sentence);
	}
	for (dp = gpsd_drivers; *dp; dp++) {
	    char *trigger = (*dp)->trigger;

	    if (trigger!=NULL && str_starts_with(sentence, trigger)) {
		gpsd_log(&session->context->errout, LOG_PROG,
			 "found trigger string %s.\n", trigger);
		if (*dp != session->device_type) {
		    (void)gpsd_switch_driver(session, (*dp)->type_name);
		    if (session->device_type != NULL
			&& session->device_type->event_hook != NULL)
			session->device_type->event_hook(session,
							 event_triggermatch);
		    st |= DEVICEID_SET;
		}
	    }
	}
	return st;
#endif /* NMEA0183_ENABLE */
    } else {
	gpsd_log(&session->context->errout, LOG_SHOUT,
		 "packet type %d fell through (should never happen): %s.\n",
		 session->lexer.type, gpsd_prettydump(session));
	return 0;
    }
}

/**************************************************************************
 *
 * Generic driver -- make no assumptions about the device type
 *
 **************************************************************************/

/* *INDENT-OFF* */
const struct gps_type_t driver_unknown = {
    .type_name      = "Unknown",	/* full name of type */
    .packet_type    = COMMENT_PACKET,	/* associated lexer packet type */
    .flags	    = DRIVER_NOFLAGS,	/* no flags set */
    .trigger	    = NULL,		/* it's the default */
    .channels       = 12,		/* consumer-grade GPS */
    .probe_detect   = NULL,		/* no probe */
    .get_packet     = generic_get,	/* use generic packet getter */
    .parse_packet   = generic_parse_input,	/* how to interpret a packet */
    .rtcm_writer    = NULL,		/* write RTCM data straight */
    .init_query     = NULL,		/* non-perturbing initial query */
    .event_hook     = NULL,		/* lifetime event handler */
#ifdef RECONFIGURE_ENABLE
    .speed_switcher = NULL,		/* no speed switcher */
    .mode_switcher  = NULL,		/* no mode switcher */
    .rate_switcher  = NULL,		/* no sample-rate switcher */
    .min_cycle      = 1,		/* not relevant, no rate switch */
#endif /* RECONFIGURE_ENABLE */
#ifdef CONTROLSEND_ENABLE
    .control_send   = NULL,		/* how to send control strings */
#endif /* CONTROLSEND_ENABLE */
#ifdef TIMEHINT_ENABLE
    .time_offset     = NULL,		/* no method for NTP fudge factor */
#endif /* TIMEHINT_ENABLE */
};
/* *INDENT-ON* */

#ifdef NMEA0183_ENABLE
/**************************************************************************
 *
 * NMEA 0183
 *
 * This is separate from the 'unknown' driver because we don't want to
 * ship NMEA subtype probe strings to a device until we've seen at
 * least one NMEA packet.  This avoids spamming devices that might
 * actually be USB modems or other things in USB device class FF that
 * just happen to have one of 'our' adaptor chips in front of them.
 *
 **************************************************************************/

static void nmea_event_hook(struct gps_device_t *session, event_t event)
{
    if (session->context->readonly)
	return;
    /*
     * This is where we try to tickle NMEA devices into revealing their
     * inner natures.
     */
    if (event == event_configure) {
	/*
	 * The reason for splitting these probes up by packet sequence
	 * number, interleaving them with the first few packet receives,
	 * is because many generic-NMEA devices get confused if you send
	 * too much at them in one go.
	 *
	 * A fast response to an early probe will change drivers so the
	 * later ones won't be sent at all.  Thus, for best overall
	 * performance, order these to probe for the most popular types
	 * soonest.
	 *
	 * Note: don't make the trigger strings identical to the probe,
	 * because some NMEA devices (notably SiRFs) will just echo
	 * unknown strings right back at you. A useful dodge is to append
	 * a comma to the trigger, because that won't be in the response
	 * unless there is actual following data.
	 */
	switch (session->lexer.counter) {
#ifdef NMEA0183_ENABLE
	case 0:
	    /* probe for Garmin serial GPS -- expect $PGRMC followed by data */
	    gpsd_log(&session->context->errout, LOG_PROG,
		     "=> Probing for Garmin NMEA\n");
	    (void)nmea_send(session, "$PGRMCE");
	    break;
#endif /* NMEA0183_ENABLE */
#ifdef SIRF_ENABLE
	case 1:
	    /*
	     * We used to try to probe for SiRF by issuing
	     * "$PSRF105,1" and expecting "$Ack Input105.".  But it
	     * turns out this only works for SiRF-IIs; SiRF-I and
	     * SiRF-III don't respond.  Sadly, the MID132 binary
	     * request for firmware version is ignored in NMEA mode.
	     * Thus the only reliable probe is to try to flip the SiRF
	     * into binary mode, cluing in the library to revert it on
	     * close.
	     *
	     * SiRFs dominate the consumer-grade GPS-mouse market, so
	     * we used to put this test first. Unfortunately this
	     * causes problems for gpsctl, as it cannot select the
	     * NMEA driver without switching the device back to binary
	     * mode!  Fix this if we ever find a nondisruptive probe
	     * string.
	     */
	    gpsd_log(&session->context->errout, LOG_PROG,
		     "=> Probing for SiRF\n");
	    (void)nmea_send(session,
			    "$PSRF100,0,%d,%d,%d,0",
			    session->gpsdata.dev.baudrate,
			    9 - session->gpsdata.dev.stopbits,
			    session->gpsdata.dev.stopbits);
	    session->back_to_nmea = true;
	    break;
#endif /* SIRF_ENABLE */
#ifdef NMEA0183_ENABLE
	case 2:
	    /* probe for the FV-18 -- expect $PFEC,GPint followed by data */
	    gpsd_log(&session->context->errout, LOG_PROG,
		     "=> Probing for FV-18\n");
	    (void)nmea_send(session, "$PFEC,GPint");
	    break;
	case 3:
	    /* probe for the Trimble Copernicus */
	    gpsd_log(&session->context->errout, LOG_PROG,
		     "=> Probing for Trimble Copernicus\n");
	    (void)nmea_send(session, "$PTNLSNM,0139,01");
	    break;
#endif /* NMEA0183_ENABLE */
#ifdef EVERMORE_ENABLE
	case 4:
	    gpsd_log(&session->context->errout, LOG_PROG,
		     "=> Probing for Evermore\n");
	    /* Enable checksum and GGA(1s), GLL(0s), GSA(1s), GSV(1s), RMC(1s), VTG(0s), PEMT101(0s) */
	    /* EverMore will reply with: \x10\x02\x04\x38\x8E\xC6\x10\x03 */
	    (void)gpsd_write(session,
			     "\x10\x02\x12\x8E\x7F\x01\x01\x00\x01\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x13\x10\x03",
			     22);
	    break;
#endif /* EVERMORE_ENABLE */
#ifdef GPSCLOCK_ENABLE
	case 5:
	    /* probe for Furuno Electric GH-79L4-N (GPSClock); expect $PFEC,GPssd */
	    gpsd_log(&session->context->errout, LOG_PROG,
		     "=> Probing for GPSClock\n");
	    (void)nmea_send(session, "$PFEC,GPsrq");
	    break;
#endif /* GPSCLOCK_ENABLE */
#ifdef ASHTECH_ENABLE
	case 6:
	    /* probe for Ashtech -- expect $PASHR,RID */
	    gpsd_log(&session->context->errout, LOG_PROG,
		     "=> Probing for Ashtech\n");
	    (void)nmea_send(session, "$PASHQ,RID");
	    break;
#endif /* ASHTECH_ENABLE */
#ifdef UBLOX_ENABLE
	case 7:
	    /* probe for UBX -- query port configuration */
	    gpsd_log(&session->context->errout, LOG_PROG,
		     "=> Probing for UBX\n");
	    (void)ubx_write(session, 0x06, 0x00, NULL, 0);
	    break;
#endif /* UBLOX_ENABLE */
#ifdef MTK3301_ENABLE
	case 8:
	    /* probe for MTK-3301 -- expect $PMTK705 */
	    gpsd_log(&session->context->errout, LOG_PROG,
		     "=> Probing for MediaTek\n");
	    (void)nmea_send(session, "$PMTK605");
	    break;
#endif /* MTK3301_ENABLE */
	default:
	    break;
	}
    }
}

/* *INDENT-OFF* */
const struct gps_type_t driver_nmea0183 = {
    .type_name      = "NMEA0183",	/* full name of type */
    .packet_type    = NMEA_PACKET,	/* associated lexer packet type */
    .flags	    = DRIVER_NOFLAGS,	/* remember this */
    .trigger	    = NULL,		/* it's the default */
    .channels       = 12,		/* consumer-grade GPS */
    .probe_detect   = NULL,		/* no probe */
    .get_packet     = generic_get,	/* use generic packet getter */
    .parse_packet   = generic_parse_input,	/* how to interpret a packet */
    .rtcm_writer    = gpsd_write,	/* write RTCM data straight */
    .init_query     = NULL,		/* non-perturbing initial query */
    .event_hook     = nmea_event_hook,	/* lifetime event handler */
#ifdef RECONFIGURE_ENABLE
    .speed_switcher = NULL,		/* no speed switcher */
#ifdef BINARY_ENABLE
    .mode_switcher  = NULL,		/* maybe switchable if it was a SiRF */
#else
    .mode_switcher  = NULL,		/* no binary mode to revert to */
#endif /* BINARY_ENABLE */
    .rate_switcher  = NULL,		/* no sample-rate switcher */
    .min_cycle      = 1,		/* not relevant, no rate switch */
#endif /* RECONFIGURE_ENABLE */
#ifdef CONTROLSEND_ENABLE
    .control_send   = nmea_write,	/* how to send control strings */
#endif /* CONTROLSEND_ENABLE */
#ifdef TIMEHINT_ENABLE
    .time_offset     = NULL,		/* no method for NTP fudge factor */
#endif /* TIMEHINT_ENABLE */
};
/* *INDENT-ON* */

#if defined(GARMIN_ENABLE) && defined(NMEA0183_ENABLE)
/**************************************************************************
 *
 * Garmin NMEA
 *
 **************************************************************************/

#ifdef RECONFIGURE_ENABLE
static void garmin_mode_switch(struct gps_device_t *session, int mode)
/* only does anything in one direction, going to Garmin binary driver */
{
    if (mode == MODE_BINARY) {
	(void)nmea_send(session, "$PGRMC1,1,2,1,,,,2,W,N");
	(void)nmea_send(session, "$PGRMI,,,,,,,R");
	(void)usleep(333);	/* standard Garmin settling time */
    }
}
#endif /* RECONFIGURE_ENABLE */

static void garmin_nmea_event_hook(struct gps_device_t *session,
				   event_t event)
{
    if (session->context->readonly)
	return;

    if (event == event_driver_switch) {
	/* forces a reconfigure as the following packets come in */
	session->lexer.counter = 0;
    }
    if (event == event_configure) {
	/*
	 * And here's that reconfigure.  It's split up like this because
	 * receivers like the Garmin GPS-10 don't handle having having a lot of
	 * probes shoved at them very well.
	 */
	switch (session->lexer.counter) {
	case 0:
	    /* reset some config, AutoFix, WGS84, PPS
	     * Set the PPS pulse length to 40ms which leaves the Garmin 18-5hz
	     * with a 160ms low state.
	     * NOTE: new PPS only takes effect after next power cycle
	     */
	    (void)nmea_send(session, "$PGRMC,A,,100,,,,,,A,,1,2,1,30");
	    break;
	case 1:
	    /* once a sec, no averaging, NMEA 2.3, WAAS */
	    (void)nmea_send(session, "$PGRMC1,1,1,1,,,,2,W,N");
	    break;
	case 2:
	    /* get some more config info */
	    (void)nmea_send(session, "$PGRMC1E");
	    break;
	case 3:
	    /* turn off all output except GGA */
	    (void)nmea_send(session, "$PGRMO,,2");
	    (void)nmea_send(session, "$PGRMO,GPGGA,1");
	    break;
	case 4:
	    /* enable GPGGA, GPGSA, GPGSV, GPRMC on Garmin serial GPS */
	    (void)nmea_send(session, "$PGRMO,GPGSA,1");
	    break;
	case 5:
	    (void)nmea_send(session, "$PGRMO,GPGSV,1");
	    break;
	case 6:
	    (void)nmea_send(session, "$PGRMO,GPRMC,1");
	    break;
	case 7:
	    (void)nmea_send(session, "$PGRMO,PGRME,1");
	    break;
	}
    }
}

/* *INDENT-OFF* */
const struct gps_type_t driver_garmin = {
    .type_name      = "Garmin NMEA",	/* full name of type */
    .packet_type    = NMEA_PACKET,	/* associated lexer packet type */
    .flags	    = DRIVER_STICKY,	/* remember this */
    .trigger	    = "$PGRMC,",	/* Garmin private */
    .channels       = 12,		/* not used by this driver */
    .probe_detect   = NULL,		/* no probe */
    .get_packet     = generic_get,	/* use generic packet getter */
    .parse_packet   = generic_parse_input,	/* how to interpret a packet */
    .rtcm_writer    = NULL,		/* some do, some don't, skip for now */
    .init_query     = NULL,		/* non-perturbing initial query */
    .event_hook     = garmin_nmea_event_hook,	/* lifetime event handler */
#ifdef RECONFIGURE_ENABLE
    .speed_switcher = NULL,			/* no speed switcher */
    .mode_switcher  = garmin_mode_switch,	/* mode switcher */
    .rate_switcher  = NULL,		/* no sample-rate switcher */
    .min_cycle      = 1,		/* not relevant, no rate switch */
#endif /*RECONFIGURE_ENABLE */
#ifdef CONTROLSEND_ENABLE
    .control_send   = nmea_write,	/* how to send control strings */
#endif /* CONTROLSEND_ENABLE */
#ifdef TIMEHINT_ENABLE
    .time_offset     = NULL,		/* no method for NTP fudge factor */
#endif /* TIMEHINT_ENABLE */
};
/* *INDENT-ON* */
#endif /* GARMIN_ENABLE && NMEA0183_ENABLE */

#ifdef ASHTECH_ENABLE
/**************************************************************************
 *
 * Ashtech (then Thales, now Magellan Professional) Receivers
 *
 **************************************************************************/

static void ashtech_event_hook(struct gps_device_t *session, event_t event)
{
    if (session->context->readonly)
	return;

    if (event == event_wakeup)
	(void)nmea_send(session, "$PASHQ,RID");
    if (event == event_identified) {
	/* turn WAAS on. can't hurt... */
	(void)nmea_send(session, "$PASHS,WAS,ON");
	/* reset to known output state */
	(void)nmea_send(session, "$PASHS,NME,ALL,A,OFF");
	/* then turn on some useful sentences */
#ifdef __future__
	/* we could parse these, but they're oversize so they get dropped */
	(void)nmea_send(session, "$PASHS,NME,POS,A,ON");
	(void)nmea_send(session, "$PASHS,NME,SAT,A,ON");
#else
	(void)nmea_send(session, "$PASHS,NME,GGA,A,ON");
	(void)nmea_send(session, "$PASHS,NME,GSA,A,ON");
	(void)nmea_send(session, "$PASHS,NME,GSV,A,ON");
	(void)nmea_send(session, "$PASHS,NME,RMC,A,ON");
#endif
	(void)nmea_send(session, "$PASHS,NME,ZDA,A,ON");
    }
}

/* *INDENT-OFF* */
const struct gps_type_t driver_ashtech = {
    .type_name      = "Ashtech",	/* full name of type */
    .packet_type    = NMEA_PACKET,	/* associated lexer packet type */
    .flags	    = DRIVER_STICKY,	/* remember this */
    .trigger	    = "$PASHR,RID,",	/* Ashtech receivers respond thus */
    .channels       = 24,		/* not used, GG24 has 24 channels */
    .probe_detect   = NULL,		/* no probe */
    .get_packet     = generic_get,	/* how to get a packet */
    .parse_packet   = generic_parse_input,	/* how to interpret a packet */
    .rtcm_writer    = gpsd_write,	/* write RTCM data straight */
    .init_query     = NULL,		/* non-perturbing initial query */
    .event_hook     = ashtech_event_hook, /* lifetime event handler */
#ifdef RECONFIGURE_ENABLE
    .speed_switcher = NULL,		/* no speed switcher */
    .mode_switcher  = NULL,		/* no mode switcher */
    .rate_switcher  = NULL,		/* no sample-rate switcher */
    .min_cycle      = 1,		/* not relevant, no rate switch */
#endif /* RECONFIGURE_ENABLE */
#ifdef CONTROLSEND_ENABLE
    .control_send   = nmea_write,	/* how to send control strings */
#endif /* CONTROLSEND_ENABLE */
#ifdef TIMEHINT_ENABLE
    .time_offset     = NULL,		/* no method for NTP fudge factor */
#endif /* TIMEHINT_ENABLE */
};
/* *INDENT-ON* */
#endif /* ASHTECH_ENABLE */

#ifdef FV18_ENABLE
/**************************************************************************
 *
 * FV18 -- uses 2 stop bits, needs to be told to send GSAs
 *
 **************************************************************************/

static void fv18_event_hook(struct gps_device_t *session, event_t event)
{
    if (session->context->readonly)
	return;

    /*
     * Tell an FV18 to send GSAs so we'll know if 3D is accurate.
     * Suppress GLL and VTG.  Enable ZDA so dates will be accurate for replay.
     * It's possible we might not need to redo this on event_reactivate,
     * but doing so is safe and cheap.
     */
    if (event == event_identified || event == event_reactivate)
	(void)nmea_send(session,
			"$PFEC,GPint,GSA01,DTM00,ZDA01,RMC01,GLL00,VTG00,GSV05");
}

/* *INDENT-OFF* */
const struct gps_type_t driver_fv18 = {
    .type_name      = "San Jose Navigation FV18",	/* full name of type */
    .packet_type    = NMEA_PACKET,	/* associated lexer packet type */
    .flags	    = DRIVER_STICKY,	/* remember this */
    .trigger	    = "$PFEC,GPint,",	/* FV18s should echo the probe */
    .channels       = 12,		/* not used by this driver */
    .probe_detect   = NULL,		/* no probe */
    .get_packet     = generic_get,	/* how to get a packet */
    .parse_packet   = generic_parse_input,	/* how to interpret a packet */
    .rtcm_writer    = gpsd_write,	/* write RTCM data straight */
    .init_query     = NULL,		/* non-perturbing initial query */
    .event_hook     = fv18_event_hook,	/* lifetime event handler */
#ifdef RECONFIGURE_ENABLE
    .speed_switcher = NULL,		/* no speed switcher */
    .mode_switcher  = NULL,		/* no mode switcher */
    .rate_switcher  = NULL,		/* no sample-rate switcher */
    .min_cycle      = 1,		/* not relevant, no rate switch */
#endif /* RECONFIGURE_ENABLE */
#ifdef CONTROLSEND_ENABLE
    .control_send   = nmea_write,	/* how to send control strings */
#endif /* CONTROLSEND_ENABLE */
#ifdef TIMEHINT_ENABLE
    .time_offset     = NULL,		/* no method for NTP fudge factor */
#endif /* TIMEHINT_ENABLE */
};
/* *INDENT-ON* */
#endif /* FV18_ENABLE */

#ifdef GPSCLOCK_ENABLE
/**************************************************************************
 *
 * Furuno Electric GPSClock (GH-79L4)
 *
 **************************************************************************/

/*
 * Based on http://www.tecsys.de/fileadmin/user_upload/pdf/gh79_1an_intant.pdf
 */

/* *INDENT-OFF* */
const struct gps_type_t driver_gpsclock = {
    .type_name      = "Furuno Electric GH-79L4",	/* full name of type */
    .packet_type    = NMEA_PACKET,	/* associated lexer packet type */
    .flags	    = DRIVER_STICKY,	/* remember this */
    .trigger	    = "$PFEC,GPssd",	/* GPSClock should return this */
    .channels       = 12,		/* not used by this driver */
    .probe_detect   = NULL,		/* no probe */
    .get_packet     = generic_get,	/* how to get a packet */
    .parse_packet   = generic_parse_input,	/* how to interpret a packet */
    .rtcm_writer    = gpsd_write,	/* write RTCM data straight */
    .init_query     = NULL,		/* non-perturbing initial query */
    .event_hook     = NULL,		/* no lifetime event handler */
#ifdef RECONFIGURE_ENABLE
    .speed_switcher = NULL,		/* no speed switcher */
    .mode_switcher  = NULL,		/* no mode switcher */
    .rate_switcher  = NULL,		/* sample rate is fixed */
    .min_cycle      = 1,		/* sample rate is fixed */
#endif /* RECONFIGURE_ENABLE */
#ifdef CONTROLSEND_ENABLE
    .control_send   = nmea_write,	/* how to send control strings */
#endif /* CONTROLSEND_ENABLE */
#ifdef TIMEHINT_ENABLE
    .time_offset     = NULL,		/* no method for NTP fudge factor */
#endif /* TIMEHINT_ENABLE */
};
/* *INDENT-ON* */
#endif /* GPSCLOCK_ENABLE */

#ifdef TRIPMATE_ENABLE
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

static void tripmate_event_hook(struct gps_device_t *session, event_t event)
{
    if (session->context->readonly)
	return;
    /* TripMate requires this response to the ASTRAL it sends at boot time */
    if (event == event_identified)
	(void)nmea_send(session, "$IIGPQ,ASTRAL");
    /* stop it sending PRWIZCH */
    if (event == event_identified || event == event_reactivate)
	(void)nmea_send(session, "$PRWIILOG,ZCH,V,,");
}

/* *INDENT-OFF* */
static const struct gps_type_t driver_tripmate = {
    .type_name     = "Delorme TripMate",	/* full name of type */
    .packet_type   = NMEA_PACKET,		/* lexer packet type */
    .flags	   = DRIVER_STICKY,		/* no rollover or other flags */
    .trigger       ="ASTRAL",			/* tells us to switch */
    .channels      = 12,			/* consumer-grade GPS */
    .probe_detect  = NULL,			/* no probe */
    .get_packet    = generic_get,		/* how to get a packet */
    .parse_packet  = generic_parse_input,	/* how to interpret a packet */
    .rtcm_writer   = gpsd_write,		/* send RTCM data straight */
    .init_query    = NULL,			/* non-perturbing query */
    .event_hook    = tripmate_event_hook,	/* lifetime event handler */
#ifdef RECONFIGURE_ENABLE
    .speed_switcher= NULL,			/* no speed switcher */
    .mode_switcher = NULL,			/* no mode switcher */
    .rate_switcher = NULL,			/* no sample-rate switcher */
    .min_cycle     = 1,				/* no rate switch */
#endif /* RECONFIGURE_ENABLE */
#ifdef CONTROLSEND_ENABLE
    .control_send  = nmea_write,	/* how to send control strings */
#endif /* CONTROLSEND_ENABLE */
#ifdef TIMEHINT_ENABLE
    .time_offset     = NULL,		/* no method for NTP fudge factor */
#endif /* TIMEHINT_ENABLE */
};
/* *INDENT-ON* */
#endif /* TRIPMATE_ENABLE */

#ifdef EARTHMATE_ENABLE
/**************************************************************************
 *
 * Zodiac EarthMate textual mode
 *
 * Note: This is the pre-2003 version using Zodiac binary protocol.
 * There is a good HOWTO at <http://www.hamhud.net/ka9mva/earthmate.htm>.
 * It has been replaced with a design that uses a SiRF chipset.
 *
 **************************************************************************/

static void earthmate_event_hook(struct gps_device_t *session, event_t event)
{
    if (session->context->readonly)
	return;
    if (event == event_triggermatch) {
	(void)gpsd_write(session, "EARTHA\r\n", 8);
	(void)usleep(10000);
	(void)gpsd_switch_driver(session, "Zodiac");
    }
}

/* *INDENT-OFF* */
static const struct gps_type_t driver_earthmate = {
    .type_name     = "Pre-2003 Delorme EarthMate",
    .packet_type   = NMEA_PACKET,	/* associated lexer packet type */
    .flags	   = DRIVER_STICKY,		/* no rollover or other flags */
    .trigger       = "EARTHA",			/* Earthmate trigger string */
    .channels      = 12,			/* not used by NMEA parser */
    .probe_detect  = NULL,			/* no probe */
    .get_packet    = generic_get,		/* how to get a packet */
    .parse_packet  = generic_parse_input,	/* how to interpret a packet */
    .rtcm_writer   = NULL,			/* don't send RTCM data */
    .init_query     = NULL,			/* non-perturbing query */
    .event_hook    = earthmate_event_hook,	/* lifetime event handler */
#ifdef RECONFIGURE_ENABLE
    .speed_switcher= NULL,			/* no speed switcher */
    .mode_switcher = NULL,			/* no mode switcher */
    .rate_switcher = NULL,			/* no sample-rate switcher */
    .min_cycle     = 1,				/* no rate switch */
#endif /* RECONFIGURE_ENABLE */
#ifdef CONTROLSEND_ENABLE
    .control_send  = nmea_write,	/* never actually used. */
#endif /* CONTROLSEND_ENABLE */
#ifdef TIMEHINT_ENABLE
    .time_offset     = NULL,		/* no method for NTP fudge factor */
#endif /* TIMEHINT_ENABLE */
};
/* *INDENT-ON* */
#endif /* EARTHMATE_ENABLE */

#endif /* NMEA0183_ENABLE */

#ifdef TNT_ENABLE
/**************************************************************************
 * True North Technologies - Revolution 2X Digital compass
 *
 * More info: http://www.tntc.com/
 *
 * This is a digital compass which uses magnetometers to measure the
 * strength of the earth's magnetic field. Based on these measurements
 * it provides a compass heading using NMEA formatted output strings.
 * This is useful to supplement the heading provided by another GPS
 * unit. A GPS heading is unreliable at slow speed or no speed.
 *
 **************************************************************************/

static ssize_t tnt_control_send(struct gps_device_t *session,
				char *msg, size_t len UNUSED)
/* send a control string in TNT native formal */
{
    ssize_t status;
    unsigned char sum = '\0';
    char c, *p = msg;

    if (*p == '@') {
	p++;
    }
#ifdef __UNUSED__
    else {
	gpsd_log(&session->context->errout, LOG_ERROR,
		 "Bad TNT sentence: '%s'\n", msg);
    }
#endif /* __UNUSED__ */
    while (((c = *p) != '\0')) {
	sum ^= c;
	p++;
    }
    (void)snprintf(p, 6, "*%02X\r\n", (unsigned int)sum);

    status = gpsd_write(session, msg, strlen(msg));
    return status;
}

static bool tnt_send(struct gps_device_t *session, const char *fmt, ...)
/* printf(3)-like TNT command generator */
{
    char buf[BUFSIZ];
    va_list ap;
    ssize_t sent;

    va_start(ap, fmt);
    (void)vsnprintf(buf, sizeof(buf) - 5, fmt, ap);
    va_end(ap);
    sent = tnt_control_send(session, buf, strlen(buf));
    if (sent == (ssize_t) strlen(buf)) {
	gpsd_log(&session->context->errout, LOG_IO,
		 "=> GPS: %s\n", buf);
	return true;
    } else {
	gpsd_log(&session->context->errout, LOG_WARN,
		 "=> GPS: %s FAILED\n", buf);
	return false;
    }
}

#ifdef RECONFIGURE_ENABLE
static bool tnt_speed(struct gps_device_t *session,
		      speed_t speed, char parity UNUSED, int stopbits UNUSED)
{
    /*
     * Baud rate change followed by device reset.
     * See page 40 of Technical Guide 1555-B.  We need:
     * 2400 -> 1, 4800 -> 2, 9600 -> 3, 19200 -> 4, 38400 -> 5
     */
    unsigned int val = speed / 2400u;	/* 2400->1, 4800->2, 9600->4, 19200->8...  */
    unsigned int i = 0;

    /* fast way to compute log2(val) */
    while ((val >> i) > 1)
	++i;
    return tnt_send(session, "@B6=%d", i + 1)
	&& tnt_send(session, "@F28.6=1");
}
#endif /* RECONFIGURE_ENABLE */

static void tnt_event_hook(struct gps_device_t *session, event_t event)
/* TNT lifetime event hook */
{
    if (session->context->readonly)
	return;
    if (event == event_wakeup) {
	(void)tnt_send(session, "@F0.3=1");	/* set run mode */
	(void)tnt_send(session, "@F2.2=1");	/* report in degrees */
    }
}

/* *INDENT-OFF* */
const struct gps_type_t driver_trueNorth = {
    .type_name      = "True North",	/* full name of type */
    .packet_type    = NMEA_PACKET,	/* associated lexer packet type */
    .flags	    = DRIVER_STICKY,	/* remember this */
    .trigger	    = "$PTNTHTM",	/* their proprietary sentence */
    .channels       = 0,		/* not an actual GPS at all */
    .probe_detect   = NULL,		/* no probe in run mode */
    .get_packet     = generic_get,	/* how to get a packet */
    .parse_packet   = generic_parse_input,	/* how to interpret a packet */
    .rtcm_writer    = NULL,		/* Don't send */
    .init_query     = NULL,		/* non-perturbing initial query */
    .event_hook     = tnt_event_hook,	/* lifetime event handler */
#ifdef RECONFIGURE_ENABLE
    .speed_switcher = tnt_speed,	/* no speed switcher */
    .mode_switcher  = NULL,		/* no mode switcher */
    .rate_switcher  = NULL,		/* no wrapup */
    .min_cycle      = 0.5,		/* fixed at 20 samples per second */
#endif /* RECONFIGURE_ENABLE */
#ifdef CONTROLSEND_ENABLE
    .control_send   = tnt_control_send,	/* how to send control strings */
#endif /* CONTROLSEND_ENABLE */
#ifdef TIMEHINT_ENABLE
    .time_offset     = NULL,
#endif /* TIMEHINT_ENABLE */
};
/* *INDENT-ON* */
#endif

#ifdef OCEANSERVER_ENABLE
/**************************************************************************
 * OceanServer - Digital Compass, OS5000 Series
 *
 * More info: http://www.ocean-server.com/download/OS5000_Compass_Manual.pdf
 *
 * This is a digital compass which uses magnetometers to measure the
 * strength of the earth's magnetic field. Based on these measurements
 * it provides a compass heading using NMEA formatted output strings.
 * This is useful to supplement the heading provided by another GPS
 * unit. A GPS heading is unreliable at slow speed or no speed.
 *
 **************************************************************************/

static int oceanserver_send(struct gpsd_errout_t *errout,
			    const int fd, const char *fmt, ...)
{
    int status;
    char buf[BUFSIZ];
    va_list ap;

    va_start(ap, fmt);
    (void)vsnprintf(buf, sizeof(buf) - 5, fmt, ap);
    va_end(ap);
    (void)strlcat(buf, "", sizeof(buf));
    status = (int)write(fd, buf, strlen(buf));
    (void)tcdrain(fd);
    if (status == (int)strlen(buf)) {
	gpsd_log(errout, LOG_IO, "=> GPS: %s\n", buf);
	return status;
    } else {
	gpsd_log(errout, LOG_WARN, "=> GPS: %s FAILED\n", buf);
	return -1;
    }
}

static void oceanserver_event_hook(struct gps_device_t *session,
				   event_t event)
{
    if (session->context->readonly)
	return;
    if (event == event_configure && session->lexer.counter == 0) {
	/* report in NMEA format */
	(void)oceanserver_send(&session->context->errout,
			       session->gpsdata.gps_fd, "2\n");
	/* ship all fields */
	(void)oceanserver_send(&session->context->errout,
			       session->gpsdata.gps_fd, "X2047");
    }
}

/* *INDENT-OFF* */
static const struct gps_type_t driver_oceanServer = {
    .type_name      = "OceanServer OS5000", /* full name of type */
    .packet_type    = NMEA_PACKET,	/* associated lexer packet type */
    .flags	    = DRIVER_STICKY,	/* no rollover or other flags */
    .trigger	    = "$OHPR,",		/* detect their main sentence */
    .channels       = 0,		/* not an actual GPS at all */
    .probe_detect   = NULL,
    .get_packet     = generic_get,	/* how to get a packet */
    .parse_packet   = generic_parse_input,	/* how to interpret a packet */
    .rtcm_writer    = NULL,		/* Don't send */
    .init_query     = NULL,		/* non-perturbing initial query */
    .event_hook     = oceanserver_event_hook,
#ifdef RECONFIGURE_ENABLE
    .speed_switcher = NULL,		/* no speed switcher */
    .mode_switcher  = NULL,		/* no mode switcher */
    .rate_switcher  = NULL,		/* no wrapup */
    .min_cycle      = 1,		/* not relevant, no rate switch */
#endif /* RECONFIGURE_ENABLE */
#ifdef CONTROLSEND_ENABLE
    .control_send   = nmea_write,	/* how to send control strings */
#endif /* CONTROLSEND_ENABLE */
#ifdef TIMEHINT_ENABLE
    .time_offset     = NULL,
#endif /* TIMEHINT_ENABLE */
};
/* *INDENT-ON* */
#endif

#ifdef FURY_ENABLE
/**************************************************************************
 *
 * Jackson Labs Fury, a high-precision laboratory clock
 *
 * Will also support other Jackon Labs boards, including the Firefly.
 *
 * Note: you must either build with fixed_port_speed=115200 or tweak the
 * speed on the port to 115200 before running.  The device's default mode
 * does not stream output, so our hunt loop will simply time out otherwise.
 *
 **************************************************************************/

static bool fury_rate_switcher(struct gps_device_t *session, double rate)
{
    char buf[78];
    double inverted;

    /* rate is a frequency, but the command takes interval in # of seconds */
    if (rate == 0.0)
	inverted = 0.0;
    else
	inverted = 1.0/rate;
    if (inverted > 256)
	return false;
    (void)snprintf(buf, sizeof(buf), "GPS:GPGGA %d\r\n", (int)inverted);
    (void)gpsd_write(session, buf, strlen(buf));
    return true;
}

static void fury_event_hook(struct gps_device_t *session, event_t event)
{
    if (event == event_wakeup && gpsd_get_speed(session) == 115200)
	(void)fury_rate_switcher(session, 1.0);
    else if (event == event_deactivate)
	(void)fury_rate_switcher(session, 0.0);
}


/* *INDENT-OFF* */
static const struct gps_type_t driver_fury = {
    .type_name      = "Jackson Labs Fury", /* full name of type */
    .packet_type    = NMEA_PACKET,	/* associated lexer packet type */
    .flags	    = DRIVER_STICKY,	/* no rollover or other flags */
    .trigger	    = NULL,		/* detect their main sentence */
    .channels       = 0,		/* not an actual GPS at all */
    .probe_detect   = NULL,
    .get_packet     = generic_get,	/* how to get a packet */
    .parse_packet   = generic_parse_input,	/* how to interpret a packet */
    .rtcm_writer    = NULL,		/* Don't send */
    .init_query     = NULL,		/* non-perturbing initial query */
    .event_hook     = fury_event_hook,
#ifdef RECONFIGURE_ENABLE
    .speed_switcher = NULL,		/* no speed switcher */
    .mode_switcher  = NULL,		/* no mode switcher */
    .rate_switcher  = fury_rate_switcher,
    .min_cycle      = 1,		/* has rate switch */
#endif /* RECONFIGURE_ENABLE */
#ifdef CONTROLSEND_ENABLE
    .control_send   = nmea_write,	/* how to send control strings */
#endif /* CONTROLSEND_ENABLE */
#ifdef TIMEHINT_ENABLE
    .time_offset     = NULL,
#endif /* TIMEHINT_ENABLE */
};
/* *INDENT-ON* */

#endif /* FURY_ENABLE */

#ifdef RTCM104V2_ENABLE
/**************************************************************************
 *
 * RTCM-104 (v2), used for broadcasting DGPS corrections and by DGPS radios
 *
 **************************************************************************/

static gps_mask_t rtcm104v2_analyze(struct gps_device_t *session)
{
    rtcm2_unpack(&session->gpsdata.rtcm2, (char *)session->lexer.isgps.buf);
    /* extra guard prevents expensive hexdump calls */
    if (session->context->errout.debug >= LOG_RAW)
	gpsd_log(&session->context->errout, LOG_RAW,
		 "RTCM 2.x packet type 0x%02x length %d words from %zd bytes: %s\n",
		 session->gpsdata.rtcm2.type,
		 session->gpsdata.rtcm2.length + 2,
		 session->lexer.isgps.buflen,
		 gpsd_hexdump(session->msgbuf, sizeof(session->msgbuf),
				 (char *)session->lexer.isgps.buf,
				 (session->gpsdata.rtcm2.length +
				  2) * sizeof(isgps30bits_t)));
    session->cycle_end_reliable = true;
    return RTCM2_SET;
}

/* *INDENT-OFF* */
static const struct gps_type_t driver_rtcm104v2 = {
    .type_name     = "RTCM104V2",	/* full name of type */
    .packet_type   = RTCM2_PACKET,	/* associated lexer packet type */
    .flags	   = DRIVER_NOFLAGS,	/* no rollover or other flags */
    .trigger       = NULL,		/* no recognition string */
    .channels      = 0,			/* not used */
    .probe_detect  = NULL,		/* no probe */
    .get_packet    = generic_get,	/* how to get a packet */
    .parse_packet  = rtcm104v2_analyze,	/*  */
    .rtcm_writer   = NULL,		/* don't send RTCM data,  */
    .init_query     = NULL,		/* non-perturbing initial query */
    .event_hook    = NULL,		/* no event_hook */
#ifdef RECONFIGURE_ENABLE
    .speed_switcher= NULL,		/* no speed switcher */
    .mode_switcher = NULL,		/* no mode switcher */
    .rate_switcher = NULL,		/* no sample-rate switcher */
    .min_cycle     = 1,			/* not relevant, no rate switch */
#endif /* RECONFIGURE_ENABLE */
#ifdef CONTROLSEND_ENABLE
    .control_send   = NULL,		/* how to send control strings */
#endif /* CONTROLSEND_ENABLE */
#ifdef TIMEHINT_ENABLE
    .time_offset     = NULL,
#endif /* TIMEHINT_ENABLE */
};
/* *INDENT-ON* */
#endif /* RTCM104V2_ENABLE */
#ifdef RTCM104V3_ENABLE
/**************************************************************************
 *
 * RTCM-104 (v3), used for broadcasting DGPS corrections and by DGPS radios
 *
 **************************************************************************/

static gps_mask_t rtcm104v3_analyze(struct gps_device_t *session)
{
    uint16_t type = getbeu16(session->lexer.inbuffer, 3) >> 4;

    gpsd_log(&session->context->errout, LOG_RAW, "RTCM 3.x packet %d\n", type);
    rtcm3_unpack(session->context,
		 &session->gpsdata.rtcm3,
		 (char *)session->lexer.outbuffer);
    session->cycle_end_reliable = true;
    return RTCM3_SET;
}

/* *INDENT-OFF* */
static const struct gps_type_t driver_rtcm104v3 = {
    .type_name     = "RTCM104V3",	/* full name of type */
    .packet_type   = RTCM3_PACKET,	/* associated lexer packet type */
    .flags	   = DRIVER_NOFLAGS,	/* no rollover or other flags */
    .trigger       = NULL,		/* no recognition string */
    .channels      = 0,			/* not used */
    .probe_detect  = NULL,		/* no probe */
    .get_packet    = generic_get,	/* how to get a packet */
    .parse_packet  = rtcm104v3_analyze,	/*  */
    .rtcm_writer   = NULL,		/* don't send RTCM data,  */
    .init_query    = NULL,		/* non-perturbing initial query */
    .event_hook    = NULL,		/* no event hook */
#ifdef RECONFIGURE_ENABLE
    .speed_switcher= NULL,		/* no speed switcher */
    .mode_switcher = NULL,		/* no mode switcher */
    .rate_switcher = NULL,		/* no sample-rate switcher */
    .min_cycle     = 1,			/* not relevant, no rate switch */
#endif /* RECONFIGURE_ENABLE */
#ifdef CONTROLSEND_ENABLE
    .control_send   = NULL,		/* how to send control strings */
#endif /* CONTROLSEND_ENABLE */
#ifdef TIMEHINT_ENABLE
    .time_offset     = NULL,
#endif /* TIMEHINT_ENABLE */
};
/* *INDENT-ON* */
#endif /* RTCM104V3_ENABLE */

#ifdef GARMINTXT_ENABLE
/**************************************************************************
 *
 * Garmin Simple Text protocol
 *
 **************************************************************************/

/* *INDENT-OFF* */
static const struct gps_type_t driver_garmintxt = {
    .type_name     = "Garmin Simple Text",		/* full name of type */
    .packet_type   = GARMINTXT_PACKET,	/* associated lexer packet type */
    .flags	   = DRIVER_NOFLAGS,	/* no rollover or other flags */
    .trigger       = NULL,		/* no recognition string */
    .channels      = 0,			/* not used */
    .probe_detect  = NULL,		/* no probe */
    .get_packet    = generic_get,	/* how to get a packet */
    .parse_packet  = garmintxt_parse,	/* how to parse one */
    .rtcm_writer   = NULL,		/* don't send RTCM data,  */
    .init_query     = NULL,		/* non-perturbing initial query */
    .event_hook    = NULL,		/* no event hook */
#ifdef RECONFIGURE_ENABLE
    .speed_switcher= NULL,		/* no speed switcher */
    .mode_switcher = NULL,		/* no mode switcher */
    .rate_switcher = NULL,		/* no sample-rate switcher */
    .min_cycle     = 1,			/* not relevant, no rate switch */
#endif /* RECONFIGURE_ENABLE */
#ifdef CONTROLSEND_ENABLE
    .control_send   = NULL,		/* how to send control strings */
#endif /* CONTROLSEND_ENABLE */
#ifdef TIMEHINT_ENABLE
    .time_offset     = NULL,
#endif /* TIMEHINT_ENABLE */
};
/* *INDENT-ON* */
#endif /* GARMINTXT_ENABLE */

#ifdef MTK3301_ENABLE
/**************************************************************************
 *
 * MediaTek MTK-3301 and 3329
 *
 * OEMs for several GPS vendors, notably including Garmin and FasTrax.
 * Website at <http://www.mediatek.com/>.
 *
 * The Trimble Condor appears to be an MTK3329.  It behaves as an MTK3301
 * and positively acknowledges all 3301 sentences as valid. It ignores $PMTK
 * sentence fields that are not implemented in the Trimble Condor. It does
 * not have power-save mode and ignores $PMTK320.  For $PMTK314 it silently
 * ignores periodic enabling of messages that aren't supported.
 *
 **************************************************************************/

static void mtk3301_event_hook(struct gps_device_t *session, event_t event)
{
/*
0  NMEA_SEN_GLL,  GPGLL   interval - Geographic Position - Latitude longitude
1  NMEA_SEN_RMC,  GPRMC   interval - Recommended Minimum Specific GNSS Sentence
2  NMEA_SEN_VTG,  GPVTG   interval - Course Over Ground and Ground Speed
3  NMEA_SEN_GGA,  GPGGA   interval - GPS Fix Data
4  NMEA_SEN_GSA,  GPGSA   interval - GNSS DOPS and Active Satellites
5  NMEA_SEN_GSV,  GPGSV   interval - GNSS Satellites in View
6  NMEA_SEN_GRS,  GPGRS   interval - GNSS Range Residuals
7  NMEA_SEN_GST,  GPGST   interval - GNSS Pseudorange Errors Statistics
13 NMEA_SEN_MALM, PMTKALM interval - GPS almanac information
14 NMEA_SEN_MEPH, PMTKEPH interval - GPS ephemeris information
15 NMEA_SEN_MDGP, PMTKDGP interval - GPS differential correction information
16 NMEA_SEN_MDBG, PMTKDBG interval – MTK debug information
17 NMEA_SEN_ZDA,  GPZDA   interval - Time & Date
18 NMEA_SEN_MCHN, PMTKCHN interval – GPS channel status

"$PMTK314,1,1,1,1,1,5,1,1,0,0,0,0,0,0,0,0,0,1,0"

*/
    if (session->context->readonly)
	return;
    if (event == event_triggermatch) {
	(void)nmea_send(session, "$PMTK320,0");	/* power save off */
	(void)nmea_send(session, "$PMTK300,1000,0,0,0.0,0.0");/* Fix interval */
	(void)nmea_send(session,
			"$PMTK314,0,1,0,1,1,5,1,1,0,0,0,0,0,0,0,0,0,1,0");
	(void)nmea_send(session, "$PMTK301,2");	/* DGPS is WAAS */
	(void)nmea_send(session, "$PMTK313,1");	/* SBAS enable */

        /* PMTK_API_Q_OUTPUT_CTL - Query PPS pulse width - Trimble only?
         * http://trl.trimble.com/docushare/dsweb/Get/Document-482603/CONDOR_UG_2C_75263-00.pdf *
         * badly documented */
	 (void)nmea_send(session, "$PMTK424");	
    }
}

#ifdef RECONFIGURE_ENABLE
static bool mtk3301_rate_switcher(struct gps_device_t *session, double rate)
{
    char buf[78];

    unsigned int milliseconds = (unsigned int)(1000 * rate);
    if (rate > 1)
	milliseconds = 1000;
    else if (rate < 0.2)
	milliseconds = 200;

    (void)snprintf(buf, sizeof(buf), "$PMTK300,%u,0,0,0,0", milliseconds);
    (void)nmea_send(session, buf);	/* Fix interval */
    return true;
}
#endif /* RECONFIGURE_ENABLE */

/* *INDENT-OFF* */
const struct gps_type_t driver_mtk3301 = {
    .type_name      = "MTK-3301",	/* full name of type */
    .packet_type    = NMEA_PACKET,	/* associated lexer packet type */
    .flags	    = DRIVER_STICKY,	/* remember this */
    .trigger	    = "$PMTK705,",	/* firmware release name and version */
    .channels       = 12,		/* not used by this driver */
    .probe_detect   = NULL,		/* no probe */
    .get_packet     = generic_get,	/* how to get a packet */
    .parse_packet   = generic_parse_input,	/* how to interpret a packet */
    .rtcm_writer    = gpsd_write,	/* write RTCM data straight */
    .init_query     = NULL,		/* non-perturbing initial query */
    .event_hook     = mtk3301_event_hook,	/* lifetime event handler */
#ifdef RECONFIGURE_ENABLE
    .speed_switcher = NULL,		/* no speed switcher */
    .mode_switcher  = NULL,		/* no mode switcher */
    .rate_switcher  = mtk3301_rate_switcher,		/* sample rate switcher */
    .min_cycle      = 0.2,		/* max 5Hz */
#endif /* RECONFIGURE_ENABLE */
#ifdef CONTROLSEND_ENABLE
    .control_send   = nmea_write,	/* how to send control strings */
#endif /* CONTROLSEND_ENABLE */
#ifdef TIMEHINT_ENABLE
    .time_offset     = NULL,
#endif /* TIMEHINT_ENABLE */
};
/* *INDENT-ON* */
#endif /* MTK3301_ENABLE */

#ifdef ISYNC_ENABLE
/**************************************************************************
 *
 * Spectratime LNRCLOK / GRCLOK iSync GPS-disciplined rubidium oscillators
 *
 * These devices comprise a u-blox 6 attached to a separate iSync
 * microcontroller which drives the rubidium oscillator.  The iSync
 * microcontroller can be configured to pass through the underlying
 * GPS communication channel, while still using the GPS PPSREF signal
 * to discipline the rubidium oscillator.
 *
 * The iSync can also generate its own periodic status packets in NMEA
 * format.  These describe the state of the oscillator (including
 * whether or not the oscillator PPSOUT is synced to the GPS PPSREF).
 * These packets are transmitted in the middle of the underlying GPS
 * packets, forcing us to handle interrupted NMEA packets.
 *
 * The default state of the device is to generate no periodic output.
 * We send a probe string to initiate beating of the iSync-proprietary
 * $PTNTS,B message, which is then detected as a NMEA trigger.
 *
 **************************************************************************/

static ssize_t isync_write(struct gps_device_t *session, const char *data)

{
    return gpsd_write(session, data, strlen(data));
}

static bool isync_detect(struct gps_device_t *session)
{
    speed_t old_baudrate;
    char old_parity;
    unsigned int old_stopbits;

    /* Set 9600 8N1 */
    old_baudrate = session->gpsdata.dev.baudrate;
    old_parity = session->gpsdata.dev.parity;
    old_stopbits = session->gpsdata.dev.stopbits;
    gpsd_set_speed(session, 9600, 'N', 1);

    /* Cancel pass-through mode and initiate beating of $PTNTS,B
     * message, which can subsequently be detected as a trigger.
     */
    (void)isync_write(session, "@@@@\r\nMAW0C0B\r\n");

    /* return serial port to original settings */
    gpsd_set_speed(session, old_baudrate, old_parity, old_stopbits);

    return false;
}

static void isync_event_hook(struct gps_device_t *session, event_t event)
{
    if (session->context->readonly)
	return;

    if (event == event_driver_switch) {
	session->lexer.counter = 0;
    }

    if (event == event_configure) {
	switch (session->lexer.counter) {
	case 1:
	    /* Configure timing and frequency flags:
	     *  - Thermal compensation active
	     *  - PPSREF active
	     *  - PPSOUT active
	     */
	    (void)isync_write(session, "MAW040B\r\n");
	    /* Configure tracking flags:
	     *  - Save frequency every 24 hours
	     *  - Align PPSOUT to PPSINT
	     *  - Track PPSREF
	     */
	    (void)isync_write(session, "MAW0513\r\n");
	    /* Configure tracking start flags:
	     *  - Tracking restart allowed
	     *  - Align to PPSREF
	     */
	    (void)isync_write(session, "MAW0606\r\n");
	    /* Configure tracking window:
	     *  - 4us
	     */
	    (void)isync_write(session, "MAW1304\r\n");
	    /* Configure alarm window:
	     *  - 4us
	     */
	    (void)isync_write(session, "MAW1404\r\n");
	    break;
	case 2:
	    /* Configure pulse every d second:
	     *  - pulse every second
	     */
	    (void)isync_write(session, "MAW1701\r\n");
	    /* Configure pulse origin:
	     *  - zero offset
	     */
	    (void)isync_write(session, "MAW1800\r\n");
	    /* Configure pulse width:
	     *  - 600ms
	     */
	    (void)isync_write(session, "MAW1223C34600\r\n");
	    break;
	case 3:
	    /* Configure GPS resource utilization:
	     *  - do not consider GPS messages
	     */
	    (void)isync_write(session, "MAW2200\r\n");
	    /* Restart sync */
	    (void)isync_write(session, "SY1\r\n");
	    /* Restart tracking */
	    (void)isync_write(session, "TR1\r\n");
	    break;
	case 4:
	    /* Cancel BTx messages (if any) */
	    (void)isync_write(session, "BT0\r\n");
	    /* Configure messages coming out every second:
	     *  - Oscillator status ($PTNTA) at 750ms
	     */
	    (void)isync_write(session, "MAW0B00\r\n");
	    (void)isync_write(session, "MAW0C0A\r\n");
	    break;
	case 5:
	    /* Enable GPS passthrough and force u-blox driver to
	     * select NMEA mode.
	     */
	    session->mode = 0;
	    session->drivers_identified = 0;
	    (void)isync_write(session, "@@@@GPS\r\n");
	    break;
	case 6:
	    /* Trigger detection of underlying u-blox (if necessary) */
	    (void)ubx_write(session, 0x06, 0x00, NULL, 0);
	    break;
	}
    }
}

/* *INDENT-OFF* */
const struct gps_type_t driver_isync = {
    .type_name      = "iSync",		/* full name of type */
    .packet_type    = NMEA_PACKET,	/* associated lexer packet type */
    .flags          = DRIVER_STICKY,	/* remember this */
    .trigger	    = "$PTNTS,B,",	/* iSync status message */
    .channels       = 50,		/* copied from driver_ubx */
    .probe_detect   = isync_detect,	/* how to detect at startup time */
    .get_packet     = generic_get,	/* how to get a packet */
    .parse_packet   = generic_parse_input,	/* how to interpret a packet */
    .init_query     = NULL,		/* non-perturbing initial query */
    .event_hook     = isync_event_hook,	/* lifetime event handler */
#ifdef RECONFIGURE_ENABLE
    .speed_switcher = NULL,		/* no speed switcher */
    .mode_switcher  = NULL,		/* no mode switcher */
    .rate_switcher  = NULL,		/* no sample-rate switcher */
    .min_cycle      = 1,		/* not relevant, no rate switch */
#endif /* RECONFIGURE_ENABLE */
#ifdef CONTROLSEND_ENABLE
    .control_send   = nmea_write,	/* how to send control strings */
#endif /* CONTROLSEND_ENABLE */
#ifdef TIMEHINT_ENABLE
    .time_offset     = NULL,		/* no method for NTP fudge factor */
#endif /* TIMEHINT_ENABLE */
};
/* *INDENT-ON* */
#endif /* ISYNC_ENABLE */

#ifdef AIVDM_ENABLE
/**************************************************************************
 *
 * AIVDM - ASCII armoring of binary AIS packets
 *
 **************************************************************************/

static bool aivdm_decode(const char *buf, size_t buflen,
		  struct gps_device_t *session,
		  struct ais_t *ais,
		  int debug)
{
#ifdef __UNUSED_DEBUG__
    char *sixbits[64] = {
	"000000", "000001", "000010", "000011", "000100",
	"000101", "000110", "000111", "001000", "001001",
	"001010", "001011", "001100", "001101", "001110",
	"001111", "010000", "010001", "010010", "010011",
	"010100", "010101", "010110", "010111", "011000",
	"011001", "011010", "011011", "011100", "011101",
	"011110", "011111", "100000", "100001", "100010",
	"100011", "100100", "100101", "100110", "100111",
	"101000", "101001", "101010", "101011", "101100",
	"101101", "101110", "101111", "110000", "110001",
	"110010", "110011", "110100", "110101", "110110",
	"110111", "111000", "111001", "111010", "111011",
	"111100", "111101", "111110", "111111",
    };
#endif /* __UNUSED_DEBUG__ */
    int nfrags, ifrag, nfields = 0;
    unsigned char *field[NMEA_MAX*2];
    unsigned char fieldcopy[NMEA_MAX*2+1];
    unsigned char *data, *cp;
    int pad;
    struct aivdm_context_t *ais_context;
    int i;

    if (buflen == 0)
	return false;

    /* we may need to dump the raw packet */
    gpsd_log(&session->context->errout, LOG_PROG,
	     "AIVDM packet length %zd: %s\n", buflen, buf);

    /* first clear the result, making sure we don't return garbage */
    memset(ais, 0, sizeof(*ais));

    /* discard overlong sentences */
    if (strlen(buf) > sizeof(fieldcopy)-1) {
	gpsd_log(&session->context->errout, LOG_ERROR,
		 "overlong AIVDM packet.\n");
	return false;
    }

    /* extract packet fields */
    (void)strlcpy((char *)fieldcopy, buf, sizeof(fieldcopy));
    field[nfields++] = (unsigned char *)buf;
    for (cp = fieldcopy;
	 cp < fieldcopy + buflen; cp++)
    {
	if (
             (*cp == (unsigned char)',') ||
             (*cp == (unsigned char)'*')
           ) {
	    *cp = '\0';
	    field[nfields++] = cp + 1;
	}
    }
#ifdef __UNDEF_DEBUG_
    for(int i=0;i<nfields;i++)
        gpsd_log(&session->context->errout, LOG_DATA, "field [%d] [%s]\n",i,field[i]);
#endif

    /* discard sentences with exiguous commas; catches run-ons */
    if (nfields < 7) {
	gpsd_log(&session->context->errout, LOG_ERROR,
		 "malformed AIVDM packet.\n");
	return false;
    }

    switch (field[4][0]) {
    case '\0':
	/*
	 * Apparently an empty channel is normal for AIVDO sentences,
	 * which makes sense as they don't come in over radio.  This
	 * is going to break if there's ever an AIVDO type 24, though.
	 */
	if (!str_starts_with((const char *)field[0], "!AIVDO"))
	    gpsd_log(&session->context->errout, LOG_INF,
		     "invalid empty AIS channel. Assuming 'A'\n");
	ais_context = &session->driver.aivdm.context[0];
	session->driver.aivdm.ais_channel ='A';
	break;
    case '1':
	if (strcmp((char *)field[4], (char *)"12") == 0) {
	    gpsd_log(&session->context->errout, LOG_INF,
		     "ignoring bogus AIS channel '12'.\n");
	    return false;
	}
	/* fall through */
    case 'A':
	ais_context = &session->driver.aivdm.context[0];
	session->driver.aivdm.ais_channel ='A';
	break;
    case '2':
    case 'B':
	ais_context = &session->driver.aivdm.context[1];
	session->driver.aivdm.ais_channel ='B';
	break;
    case 'C':
        gpsd_log(&session->context->errout, LOG_INF,
		 "ignoring AIS channel C (secure AIS).\n");
        return false;
    default:
	gpsd_log(&session->context->errout, LOG_ERROR,
		 "invalid AIS channel 0x%0X .\n", field[4][0]);
	return false;
    }

    nfrags = atoi((char *)field[1]); /* number of fragments to expect */
    ifrag = atoi((char *)field[2]); /* fragment id */
    data = field[5];

    pad = 0;
    if(isdigit(field[6][0]))
        pad = field[6][0] - '0'; /* number of padding bits ASCII encoded*/
    gpsd_log(&session->context->errout, LOG_PROG,
	     "nfrags=%d, ifrag=%d, decoded_frags=%d, data=%s, pad=%d\n",
	     nfrags, ifrag, ais_context->decoded_frags, data, pad);

    /* assemble the binary data */

    /* check fragment ordering */
    if (ifrag != ais_context->decoded_frags + 1) {
	gpsd_log(&session->context->errout, LOG_ERROR,
		 "invalid fragment #%d received, expected #%d.\n",
		 ifrag, ais_context->decoded_frags + 1);
	if (ifrag != 1)
	    return false;
        /* else, ifrag==1: Just discard all that was previously decoded and
         * simply handle that packet */
        ais_context->decoded_frags = 0;
    }
    if (ifrag == 1) {
	(void)memset(ais_context->bits, '\0', sizeof(ais_context->bits));
	ais_context->bitlen = 0;
    }

    /* wacky 6-bit encoding, shades of FIELDATA */
    for (cp = data; cp < data + strlen((char *)data); cp++) {
	unsigned char ch;
	ch = *cp;
	ch -= 48;
	if (ch >= 40)
	    ch -= 8;
#ifdef __UNUSED_DEBUG__
	gpsd_log(&session->context->errout, LOG_RAW,
		 "%c: %s\n", *cp, sixbits[ch]);
#endif /* __UNUSED_DEBUG__ */
	for (i = 5; i >= 0; i--) {
	    if ((ch >> i) & 0x01) {
		ais_context->bits[ais_context->bitlen / 8] |=
		    (1 << (7 - ais_context->bitlen % 8));
	    }
	    ais_context->bitlen++;
	    if (ais_context->bitlen > sizeof(ais_context->bits)) {
		gpsd_log(&session->context->errout, LOG_INF,
			 "overlong AIVDM payload truncated.\n");
		return false;
	    }
	}
    }
    ais_context->bitlen -= pad;

    /* time to pass buffered-up data to where it's actually processed? */
    if (ifrag == nfrags) {
	if (debug >= LOG_INF) {
	    size_t clen = BITS_TO_BYTES(ais_context->bitlen);
	    gpsd_log(&session->context->errout, LOG_INF,
		     "AIVDM payload is %zd bits, %zd chars: %s\n",
		     ais_context->bitlen, clen,
		     gpsd_hexdump(session->msgbuf, sizeof(session->msgbuf),
				     (char *)ais_context->bits, clen));
	}

        /* clear waiting fragments count */
        ais_context->decoded_frags = 0;

	/* decode the assembled binary packet */
	return ais_binary_decode(&session->context->errout,
				 ais,
				 ais_context->bits,
				 ais_context->bitlen,
				 &ais_context->type24_queue);
    }

    /* we're still waiting on another sentence */
    ais_context->decoded_frags++;
    return false;
}

static gps_mask_t aivdm_analyze(struct gps_device_t *session)
{
    if (session->lexer.type == AIVDM_PACKET) {
	if (aivdm_decode
	    ((char *)session->lexer.outbuffer, session->lexer.outbuflen,
	     session, &session->gpsdata.ais,
	     session->context->errout.debug)) {
	    return ONLINE_SET | AIS_SET;
	} else
	    return ONLINE_SET;
#ifdef NMEA0183_ENABLE
    } else if (session->lexer.type == NMEA_PACKET) {
	return nmea_parse((char *)session->lexer.outbuffer, session);
#endif /* NMEA0183_ENABLE */
    } else
	return 0;
}

/* *INDENT-OFF* */
const struct gps_type_t driver_aivdm = {
    /* Full name of type */
    .type_name        = "AIVDM",    	/* associated lexer packet type */
    .packet_type      = AIVDM_PACKET,	/* numeric packet type */
    .flags	      = DRIVER_NOFLAGS,	/* no rollover or other flags */
    .trigger          = NULL,		/* identifying response */
    .channels         = 0,		/* not used by this driver */
    .probe_detect     = NULL,		/* no probe */
    .get_packet       = generic_get,	/* how to get a packet */
    .parse_packet     = aivdm_analyze,	/* how to analyze a packet */
    .rtcm_writer      = NULL,		/* don't send RTCM data,  */
    .init_query       = NULL,		/* non-perturbing initial query */
    .event_hook       = NULL,		/* lifetime event handler */
#ifdef RECONFIGURE_ENABLE
    .speed_switcher   = NULL,		/* no speed switcher */
    .mode_switcher    = NULL,		/* no mode switcher */
    .rate_switcher    = NULL,		/* no rate switcher */
    .min_cycle        = 1,		/* max 1Hz */
#endif /* RECONFIGURE_ENABLE */
#ifdef CONTROLSEND_ENABLE
    .control_send     = NULL,		/* no control sender */
#endif /* CONTROLSEND_ENABLE */
#ifdef TIMEHINT_ENABLE
    .time_offset     = NULL,		/* no NTP communication */
#endif /* TIMEHINT_ENABLE */
};
/* *INDENT-ON* */
#endif /* AIVDM_ENABLE */

#ifdef PASSTHROUGH_ENABLE
/**************************************************************************
 *
 * JSON passthrough driver
 *
 **************************************************************************/

static void path_rewrite(struct gps_device_t *session, char *prefix)
/* prepend the session path to the value of a specified attribute */
{
    /*
     * Hack the packet to reflect its origin.  This code is supposed
     * to insert the path naming the remote gpsd instance into the
     * beginning of the path attribute, followed by a # to separate it
     * from the device.
     */
    char *prefloc;

    assert(prefix != NULL && session->lexer.outbuffer != NULL);

    /* possibly the rewrite has been done already, this comw up in gpsmon */
    if (strstr((char *)session->lexer.outbuffer, session->gpsdata.dev.path) != NULL)
	return;

    for (prefloc = (char *)session->lexer.outbuffer;
	 prefloc < (char *)session->lexer.outbuffer+session->lexer.outbuflen;
	 prefloc++)
	if (str_starts_with(prefloc, prefix)) {
	    char copy[sizeof(session->lexer.outbuffer)+1];
	    (void)strlcpy(copy,
			  (char *)session->lexer.outbuffer,
			  sizeof(copy));
	    prefloc += strlen(prefix);
	    (void)strlcpy(prefloc,
			  session->gpsdata.dev.path,
			  sizeof(session->gpsdata.dev.path));
	    (void)strlcat((char *)session->lexer.outbuffer, "#",
			  sizeof(session->lexer.outbuffer));
	    (void)strlcat((char *)session->lexer.outbuffer,
			  copy + (prefloc-(char *)session->lexer.outbuffer),
			  sizeof(session->lexer.outbuffer));
	}
    session->lexer.outbuflen = strlen((char *)session->lexer.outbuffer);
}

static gps_mask_t json_pass_packet(struct gps_device_t *session)
{
    gpsd_log(&session->context->errout, LOG_IO,
	     "<= GPS: %s\n", (char *)session->lexer.outbuffer);

    if (strstr(session->gpsdata.dev.path, ":/") != NULL && strstr(session->gpsdata.dev.path, "localhost") == NULL)
    {
	/* devices and paths need to be edited */
	if (strstr((char *)session->lexer.outbuffer, "DEVICE") != NULL)
	    path_rewrite(session, "\"path\":\"");
	path_rewrite(session, "\"device\":\"");

	/* mark certain responses without a path or device attribute */
	if (session->gpsdata.dev.path[0] != '\0') {
	    if (strstr((char *)session->lexer.outbuffer, "VERSION") != NULL
		|| strstr((char *)session->lexer.outbuffer, "WATCH") != NULL
		|| strstr((char *)session->lexer.outbuffer, "DEVICES") != NULL) {
		session->lexer.outbuffer[session->lexer.outbuflen-1] = '\0';
		(void)strlcat((char *)session->lexer.outbuffer, ",\"remote\":\"",
			      sizeof(session->lexer.outbuffer));
		(void)strlcat((char *)session->lexer.outbuffer,
			      session->gpsdata.dev.path,
			      sizeof(session->lexer.outbuffer));
		(void)strlcat((char *)session->lexer.outbuffer, "\"}",
			      sizeof(session->lexer.outbuffer));
	    }
	    session->lexer.outbuflen = strlen((char *)session->lexer.outbuffer);
	}
    }
    gpsd_log(&session->context->errout, LOG_PROG,
	     "JSON, passing through %s\n",
	     (char *)session->lexer.outbuffer);
    return PASSTHROUGH_IS;
}

/* *INDENT-OFF* */
const struct gps_type_t driver_json_passthrough = {
    .type_name      = "JSON slave driver",	/* full name of type */
    .packet_type    = JSON_PACKET,	/* associated lexer packet type */
    .flags	    = DRIVER_NOFLAGS,	/* don't remember this */
    .trigger	    = NULL,		/* it's the default */
    .channels       = 0,		/* not used */
    .probe_detect   = NULL,		/* no probe */
    .get_packet     = generic_get,	/* use generic packet getter */
    .parse_packet   = json_pass_packet,	/* how to interpret a packet */
    .rtcm_writer    = NULL,		/* write RTCM data straight */
    .init_query     = NULL,		/* non-perturbing initial query */
    .event_hook     = NULL,		/* lifetime event handler */
#ifdef RECONFIGURE_ENABLE
    .speed_switcher = NULL,		/* no speed switcher */
    .mode_switcher  = NULL,		/* no mode switcher */
    .rate_switcher  = NULL,		/* no sample-rate switcher */
    .min_cycle      = 1,		/* not relevant, no rate switch */
#endif /* RECONFIGURE_ENABLE */
#ifdef CONTROLSEND_ENABLE
    .control_send   = NULL,		/* how to send control strings */
#endif /* CONTROLSEND_ENABLE */
#ifdef TIMEHINT_ENABLE
    .time_offset     = NULL,		/* no method for NTP fudge factor */
#endif /* TIMEHINT_ENABLE */
};
/* *INDENT-ON* */

#endif /* PASSTHROUGH_ENABLE */

#if defined(PPS_ENABLE)
/* *INDENT-OFF* */
const struct gps_type_t driver_pps = {
    .type_name      = "PPS",		/* full name of type */
    .packet_type    = BAD_PACKET,	/* associated lexer packet type */
    .flags	    = DRIVER_NOFLAGS,	/* don't remember this */
    .trigger	    = NULL,		/* it's the default */
    .channels       = 0,		/* not used */
    .probe_detect   = NULL,		/* no probe */
    .get_packet     = NULL,		/* use generic packet getter */
    .parse_packet   = NULL,		/* how to interpret a packet */
    .rtcm_writer    = NULL,		/* write RTCM data straight */
    .init_query     = NULL,		/* non-perturbing initial query */
    .event_hook     = NULL,		/* lifetime event handler */
#ifdef RECONFIGURE_ENABLE
    .speed_switcher = NULL,		/* no speed switcher */
    .mode_switcher  = NULL,		/* no mode switcher */
    .rate_switcher  = NULL,		/* no sample-rate switcher */
    .min_cycle      = 1,		/* not relevant, no rate switch */
#endif /* RECONFIGURE_ENABLE */
#ifdef CONTROLSEND_ENABLE
    .control_send   = NULL,		/* how to send control strings */
#endif /* CONTROLSEND_ENABLE */
#ifdef TIMEHINT_ENABLE
    .time_offset     = NULL,		/* no method for NTP fudge factor */
#endif /* TIMEHINT_ENABLE */
};
/* *INDENT-ON* */

#endif /* PPS_ENABLE */


extern const struct gps_type_t driver_evermore;
extern const struct gps_type_t driver_garmin_ser_binary;
extern const struct gps_type_t driver_garmin_usb_binary;
extern const struct gps_type_t driver_geostar;
extern const struct gps_type_t driver_italk;
extern const struct gps_type_t driver_navcom;
extern const struct gps_type_t driver_nmea2000;
extern const struct gps_type_t driver_oncore;
extern const struct gps_type_t driver_sirf;
extern const struct gps_type_t driver_superstar2;
extern const struct gps_type_t driver_tsip;
extern const struct gps_type_t driver_ubx;
extern const struct gps_type_t driver_zodiac;

/* the point of this rigamarole is to not have to export a table size */
static const struct gps_type_t *gpsd_driver_array[] = {
    &driver_unknown,
#ifdef NMEA0183_ENABLE
    &driver_nmea0183,
#ifdef ASHTECH_ENABLE
    &driver_ashtech,
#endif /* ASHTECH_ENABLE */
#ifdef TRIPMATE_ENABLE
    &driver_tripmate,
#endif /* TRIPMATE_ENABLE */
#ifdef EARTHMATE_ENABLE
    &driver_earthmate,
#endif /* EARTHMATE_ENABLE */
#ifdef GPSCLOCK_ENABLE
    &driver_gpsclock,
#endif /* GPSCLOCK_ENABLE */
#ifdef GARMIN_ENABLE
    &driver_garmin,
#endif /* GARMIN_ENABLE */
#ifdef MTK3301_ENABLE
    &driver_mtk3301,
#endif /*  MTK3301_ENABLE */
#ifdef OCEANSERVER_ENABLE
    &driver_oceanServer,
#endif /* OCEANSERVER_ENABLE */
#ifdef FV18_ENABLE
    &driver_fv18,
#endif /* FV18_ENABLE */
#ifdef TNT_ENABLE
    &driver_trueNorth,
#endif /* TNT_ENABLE */
#ifdef FURY_ENABLE
    &driver_fury,
#endif /* FURY_ENABLE */
#ifdef AIVDM_ENABLE
    &driver_aivdm,
#endif /* AIVDM_ENABLE */
#endif /* NMEA0183_ENABLE */

#ifdef EVERMORE_ENABLE
    &driver_evermore,
#endif /* EVERMORE_ENABLE */
#ifdef GARMIN_ENABLE
    /* be sure to try Garmin Serial Binary before Garmin USB Binary */
    &driver_garmin_ser_binary,
    &driver_garmin_usb_binary,
#endif /* GARMIN_ENABLE */
#ifdef GEOSTAR_ENABLE
    &driver_geostar,
#endif /* GEOSTAR_ENABLE */
#ifdef ITRAX_ENABLE
    &driver_italk,
#endif /* ITRAX_ENABLE */
#ifdef ONCORE_ENABLE
    &driver_oncore,
#endif /* ONCORE_ENABLE */
#ifdef NAVCOM_ENABLE
    &driver_navcom,
#endif /* NAVCOM_ENABLE */
#ifdef SIRF_ENABLE
    &driver_sirf,
#endif /* SIRF_ENABLE */
#ifdef SUPERSTAR2_ENABLE
    &driver_superstar2,
#endif /* SUPERSTAR2_ENABLE */
#ifdef TSIP_ENABLE
    &driver_tsip,
#endif /* TSIP_ENABLE */
#ifdef ISYNC_ENABLE
    &driver_isync,
#endif /* ISYNC_ENABLE */
#ifdef UBLOX_ENABLE
    &driver_ubx,
#endif /* UBLOX_ENABLE */
#ifdef ZODIAC_ENABLE
    &driver_zodiac,
#endif /* ZODIAC_ENABLE */

#ifdef NMEA2000_ENABLE
    &driver_nmea2000,
#endif /* NMEA2000_ENABLE */

#ifdef RTCM104V2_ENABLE
    &driver_rtcm104v2,
#endif /* RTCM104V2_ENABLE */
#ifdef RTCM104V3_ENABLE
    &driver_rtcm104v3,
#endif /* RTCM104V3_ENABLE */
#ifdef GARMINTXT_ENABLE
    &driver_garmintxt,
#endif /* GARMINTXT_ENABLE */

#ifdef PASSTHROUGH_ENABLE
    &driver_json_passthrough,
#endif /* PASSTHROUGH_ENABLE */
#if defined(PPS_ENABLE)
    &driver_pps,
#endif /* PPS_ENABLE */

    NULL,
};

const struct gps_type_t **gpsd_drivers = &gpsd_driver_array[0];
