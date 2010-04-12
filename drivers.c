/*
 * This file is Copyright (c) 2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 */
#include <sys/types.h>
#include "gpsd_config.h"
#ifdef HAVE_SYS_IOCTL_H
 #include <sys/ioctl.h>
#endif /* HAVE_SYS_IOCTL_H */
#include <sys/time.h>
#include <stdlib.h>
#ifndef S_SPLINT_S
#include <unistd.h>
#endif /* S_SPLINT_S */
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <math.h>
#include <stdarg.h>

#include "gpsd.h"
#include "bits.h"	/* for getbeuw(), to extract big-endiamn words */

extern const struct gps_type_t zodiac_binary;
extern const struct gps_type_t ubx_binary;
extern const struct gps_type_t sirf_binary;

ssize_t generic_get(struct gps_device_t *session)
{
    return packet_get(session->gpsdata.gps_fd, &session->packet);
}

#if defined(NMEA_ENABLE) || defined(SIRF_ENABLE) || defined(EVERMORE_ENABLE)  || defined(ITRAX_ENABLE)  || defined(NAVCOM_ENABLE)
ssize_t pass_rtcm(struct gps_device_t *session, char *buf, size_t rtcmbytes)
/* most GPSes take their RTCM corrections straight up */
{
    return gpsd_write(session, buf, rtcmbytes);
}
#endif

#ifdef NMEA_ENABLE
/**************************************************************************
 *
 * Generic driver -- straight NMEA 0183
 *
 **************************************************************************/

gps_mask_t nmea_parse_input(struct gps_device_t *session)
{
    const struct gps_type_t **dp;

    if (session->packet.type == COMMENT_PACKET) {
	return 0;
    } else if (session->packet.type != NMEA_PACKET ) {
	for (dp = gpsd_drivers; *dp; dp++) {
	    if (session->packet.type == (*dp)->packet_type) {
		gpsd_report(LOG_WARN, "%s packet seen when NMEA expected.\n",
			    (*dp)->type_name);
		(void)gpsd_switch_driver(session, (*dp)->type_name);
		return (*dp)->parse_packet(session);
	    }
	}
	return 0;
    } else /* session->packet.type == NMEA_PACKET) */ {
	gps_mask_t st = 0;

	/*
	 * The general trigger string mechanism doesn't work if the
	 * trigger is a sentence explicitly recognized in the NMEA
	 * driver.  This should probably be fixed.
	 */
#ifdef OCEANSERVER_ENABLE
	if (strncmp((char *)session->packet.outbuffer, "$C", 2)==0 || strncmp((char *)session->packet.outbuffer, "$OHPR", 5)==0) {
	    (void)gpsd_switch_driver(session, "OceanServer Digital Compas OS5000");
	    return  1;
	}
#endif /* OCEANSERVER_ENABLE */
#ifdef TNT_ENABLE_NOGOOD
	if (strncmp((char *)session->packet.outbuffer, "$PTNTHTM", 8)==0) {
	    (void)gpsd_switch_driver(session, "True North");
	    return  1;
	}
#endif /* TNT_ENABLE */

	/* 
	 * Some packets do not end in \n, append one
	 * for good logging
	 */
	gpsd_report(LOG_IO, "<= GPS: %s\n", session->packet.outbuffer);

	if ((st=nmea_parse((char *)session->packet.outbuffer, session))==0) {
#ifdef UBX_ENABLE
	    if(strncmp((char *)session->packet.outbuffer, "$GPTXT,01,01,02,MOD", 19)==0) {
		ubx_catch_model(session, session->packet.outbuffer, session->packet.outbuflen);
		(void)gpsd_switch_driver(session, "uBlox UBX binary");
		return 0;
	    }
#endif /* UBX_ENABLE */
	    gpsd_report(LOG_WARN, "unknown sentence: \"%s\"\n", session->packet.outbuffer);
	}
	for (dp = gpsd_drivers; *dp; dp++) {
	    char	*trigger = (*dp)->trigger;

	    if (trigger!=NULL && strncmp((char *)session->packet.outbuffer, trigger, strlen(trigger))==0) {
		gpsd_report(LOG_PROG, "found %s.\n", trigger);
		if (*dp != session->device_type) {
		    (void)gpsd_switch_driver(session, (*dp)->type_name);
		    st |= DEVICEID_IS;
		}
	    }
	}
#ifdef NTPSHM_ENABLE
	if (session->context->enable_ntpshm &&
	    (st & TIME_IS) != 0 &&
	    (session->gpsdata.fix.time!=session->last_fixtime)) {
	    (void)ntpshm_put(session, session->gpsdata.fix.time, 0);
	    session->last_fixtime = session->gpsdata.fix.time;
	}
#endif /* NTPSHM_ENABLE */
	return st;
    }
}

static void nmea_event_hook(struct gps_device_t *session, event_t event)
{
    /*
     * This is where we try to tickle NMEA devices into erevrealing their
     * inner natures.
     */
    if (event == event_configure) {
	/* change this guard if the probe count goes up */ 
	if (session->packet.counter <= 8)
	    gpsd_report(LOG_WARN, "=> Probing device subtype %d\n", session->packet.counter);
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
	switch (session->packet.counter) {
#ifdef NMEA_ENABLE
	case 0:
	    /* probe for Garmin serial GPS -- expect $PGRMC followed by data*/
	    (void)nmea_send(session, "$PGRMCE");
	    break;
#endif /* NMEA_ENABLE */
#ifdef SIRF_ENABLE
	case 1:
	    /*
	     * We used to try to probe for SiRF by issuing "$PSRF105,1"
	     * and expecting "$Ack Input105.".  But it turns out this
	     * only works for SiRF-IIs; SiRF-I and SiRF-III don't respond.
	     * Thus the only reliable probe is to try to flip the SiRF into
	     * binary mode, cluing in the library to revert it on close.
	     *
	     * SiRFs dominate the GPS-mouse market, so we used to put this test 
	     * first. Unfortunately this causes problems for gpsctl, as it cannot
	     * select the NMEA driver without switching the device back to
	     * binary mode!  Fix this if we ever find a nondisruptive probe string.
	     */
	    (void)nmea_send(session,
			    "$PSRF100,0,%d,%d,%d,0",
			    session->gpsdata.dev.baudrate,
			    9-session->gpsdata.dev.stopbits,
			    session->gpsdata.dev.stopbits);
	    session->back_to_nmea = true;
	    break;
#endif /* SIRF_ENABLE */
#ifdef NMEA_ENABLE
	case 2:
	    /* probe for the FV-18 -- expect $PFEC,GPint followed by data */
	    (void)nmea_send(session, "$PFEC,GPint");
	    break;
	case 3:
	    /* probe for the Trimble Copernicus */
	    (void)nmea_send(session, "$PTNLSNM,0139,01");
	    break;
#endif /* NMEA_ENABLE */
#ifdef EVERMORE_ENABLE
	case 4:
	    /* Enable checksum and GGA(1s), GLL(0s), GSA(1s), GSV(1s), RMC(1s), VTG(0s), PEMT101(1s) */
	    /* EverMore will reply with: \x10\x02\x04\x38\x8E\xC6\x10\x03 */
	    (void)gpsd_write(session,
			     "\x10\x02\x12\x8E\x7F\x01\x01\x00\x01\x01\x01\x00\x01\x00\x00\x00\x00\x00\x00\x13\x10\x03", 22);
	    break;
#endif /* EVERMORE_ENABLE */
#ifdef ITRAX_ENABLE
	case 5:
	    /* probe for iTrax, looking for "$PFST,OK" */
	    (void)nmea_send(session, "$PFST");
	    break;
#endif /* ITRAX_ENABLE */
#ifdef GPSCLOCK_ENABLE
	case 6:
	    /* probe for Furuno Electric GH-79L4-N (GPSClock); expect $PFEC,GPssd */
	    (void)nmea_send(session, "$PFEC,GPsrq");
	    break;
#endif /* GPSCLOCK_ENABLE */
#ifdef ASHTECH_ENABLE
	case 7:
	    /* probe for Ashtech -- expect $PASHR,RID */
	    (void)nmea_send(session, "$PASHQ,RID");
	    break;
#endif /* ASHTECH_ENABLE */
#ifdef UBX_ENABLE
	case 8:
	    /* probe for UBX -- query software version */
	    (void)ubx_write(session, 0x0au, 0x04, NULL, 0);
	    break;
#endif /* UBX_ENABLE */
#ifdef MTK3301_ENABLE
	case 9:
	    /* probe for MTK-3301 -- expect $PMTK705 */
	    (void)nmea_send(session, "$PMTK605");
	    break;
#endif /* MTK3301_ENABLE */
	default:
	    break;
	}
    }
}

#ifdef ALLOW_RECONFIGURE
static void nmea_mode_switch(struct gps_device_t *session, int mode)
{
    if (mode == MODE_BINARY) {
#if defined(SIRF_ENABLE) && defined(BINARY_ENABLE)
	if ( 0 != (SIRF_PACKET & session->observed)) {
		/* it was SiRF binary once, do it again */
		sirf_binary.mode_switcher(session, mode);
	}
#endif
    }
}
#endif /* ALLOW_RECONFIGURE */

const struct gps_type_t nmea = {
    .type_name      = "Generic NMEA",	/* full name of type */
    .packet_type    = NMEA_PACKET,	/* associated lexer packet type */
    .trigger	    = NULL,		/* it's the default */
    .channels       = 12,		/* consumer-grade GPS */
    .probe_detect   = NULL,		/* no probe */
    .get_packet     = generic_get,	/* use generic packet getter */
    .parse_packet   = nmea_parse_input,	/* how to interpret a packet */
    .rtcm_writer    = pass_rtcm,	/* write RTCM data straight */
    .event_hook     = nmea_event_hook,	/* lifetime event handler */
#ifdef ALLOW_RECONFIGURE
    .speed_switcher = NULL,		/* no speed switcher */
    .mode_switcher  = nmea_mode_switch,	/* no mode switcher */
    .rate_switcher  = NULL,		/* no sample-rate switcher */
    .min_cycle      = 1,		/* not relevant, no rate switch */
#endif /* ALLOW_RECONFIGURE */
#ifdef ALLOW_CONTROLSEND
    .control_send   = nmea_write,	/* how to send control strings */
#endif /* ALLOW_CONTROLSEND */
};


#if defined(GARMIN_ENABLE) && defined(NMEA_ENABLE)
/**************************************************************************
 *
 * Garmin NMEA
 *
 **************************************************************************/

#ifdef ALLOW_RECONFIGURE
static void garmin_mode_switch(struct gps_device_t *session, int mode)
/* only does anything in one direction, going to Garmin binary driver */
{
    if (mode == MODE_BINARY) {
	(void)nmea_send(session, "$PGRMC1,1,2,1,,,,2,W,N");
	(void)nmea_send(session, "$PGRMI,,,,,,,R");
	(void)usleep(333);	/* standard Garmin settling time */
	session->gpsdata.dev.driver_mode = MODE_BINARY;
    }
}
#endif /* ALLOW_RECONFIGURE */

static void garmin_nmea_event_hook(struct gps_device_t *session, event_t event)
{
    if (event == event_driver_switch) {
	/* forces a reconfigure as the following packets come in */
	session->packet.counter = 0;
    } if (event == event_configure) {
	/*
	 * And here's that reconfigure.  It's spplit up like this because
	 * receivers like the Garmin GPS-10 don't handle having having a lot of
	 * probes shoved at them very well.
	 */
	switch (session->packet.counter) {
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

const struct gps_type_t garmin = {
    .type_name      = "Garmin NMEA",	/* full name of type */
    .packet_type    = NMEA_PACKET,	/* associated lexer packet type */
    .trigger	    = "$PGRMC,",	/* Garmin private */
    .channels       = 12,		/* not used by this driver */
    .probe_detect   = NULL,		/* no probe */
    .get_packet     = generic_get,	/* use generic packet getter */
    .parse_packet   = nmea_parse_input,	/* how to interpret a packet */
    .rtcm_writer    = NULL,		/* some do, some don't, skip for now */
    .event_hook     = garmin_nmea_event_hook,	/* lifetime event handler */
#ifdef ALLOW_RECONFIGURE
    .speed_switcher = NULL,			/* no speed switcher */
    .mode_switcher  = garmin_mode_switch,	/* mode switcher */
    .rate_switcher  = NULL,		/* no sample-rate switcher */
    .min_cycle      = 1,		/* not relevant, no rate switch */
#endif /*ALLOW_RECONFIGURE */
#ifdef ALLOW_CONTROLSEND
    .control_send   = nmea_write,	/* how to send control strings */
#endif /* ALLOW_CONTROLSEND */
};
#endif /* GARMIN_ENABLE && NMEA_ENABLE */

#ifdef ASHTECH_ENABLE
/**************************************************************************
 *
 * Ashtech (then Thales, now Magellan Professional) Receivers
 *
 **************************************************************************/

static void ashtech_event_hook(struct gps_device_t *session, event_t event)
{
    if (event == event_wakeup)
	(void)nmea_send(session, "$PASHQ,RID");
    /* FIXME: Do we need to do this on reactivate as well? */
    if (event == event_identified) {
	/* turn WAAS on. can't hurt... */
	(void)nmea_send(session, "$PASHS,WAS,ON");
	/* reset to known output state */
	(void)nmea_send(session, "$PASHS,NME,ALL,A,OFF");
	/* then turn on some useful sentences */
#ifdef ASHTECH_NOTYET
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

const struct gps_type_t ashtech = {
    .type_name      = "Ashtech",	/* full name of type */
    .packet_type    = NMEA_PACKET,	/* associated lexer packet type */
    .trigger	    = "$PASHR,RID,",	/* Ashtech receivers respond thus */
    .channels       = 24,		/* not used, GG24 has 24 channels */
    .probe_detect   = NULL,		/* no probe */
    .get_packet     = generic_get,	/* how to get a packet */
    .parse_packet   = nmea_parse_input,	/* how to interpret a packet */
    .rtcm_writer    = pass_rtcm,	/* write RTCM data straight */
    .event_hook     = ashtech_event_hook, /* lifetime event handler */
#ifdef ALLOW_RECONFIGURE
    .speed_switcher = NULL,		/* no speed switcher */
    .mode_switcher  = NULL,		/* no mode switcher */
    .rate_switcher  = NULL,		/* no sample-rate switcher */
    .min_cycle      = 1,		/* not relevant, no rate switch */
#endif /* ALLOW_RECONFIGURE */
#ifdef ALLOW_CONTROLSEND
    .control_send   = nmea_write,	/* how to send control strings */
#endif /* ALLOW_CONTROLSEND */
};
#endif /* ASHTECH_ENABLE */

#ifdef FV18_ENABLE
/**************************************************************************
 *
 * FV18 -- uses 2 stop bits, needs to be told to send GSAs
 *
 **************************************************************************/

static void fv18_event_hook(struct gps_device_t *session, event_t event)
{
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

const struct gps_type_t fv18 = {
    .type_name      = "San Jose Navigation FV18",	/* full name of type */
    .packet_type    = NMEA_PACKET,	/* associated lexer packet type */
    .trigger	    = "$PFEC,GPint,",	/* FV18s should echo the probe */
    .channels       = 12,		/* not used by this driver */
    .probe_detect   = NULL,		/* no probe */
    .get_packet     = generic_get,	/* how to get a packet */
    .parse_packet   = nmea_parse_input,	/* how to interpret a packet */
    .rtcm_writer    = pass_rtcm,	/* write RTCM data straight */
    .event_hook     = fv18_event_hook,	/* lifetime event handler */
#ifdef ALLOW_RECONFIGURE
    .speed_switcher = NULL,		/* no speed switcher */
    .mode_switcher  = NULL,		/* no mode switcher */
    .rate_switcher  = NULL,		/* no sample-rate switcher */
    .min_cycle      = 1,		/* not relevant, no rate switch */
#endif /* ALLOW_RECONFIGURE */
#ifdef ALLOW_CONTROLSEND
    .control_send   = nmea_write,	/* how to send control strings */
#endif /* ALLOW_CONTROLSEND */
};
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

static void gpsclock_event_hook(struct gps_device_t *session, event_t event)
{
    /*
     * Michael St. Laurent <mikes@hartwellcorp.com> reports that you have to
     * ignore the trailing PPS edge when extracting time from this chip.
     */
    if (event == event_identified || event == event_reactivate) {
	gpsd_report(LOG_INF, "PPS trailing edge will be ignored");
	session->driver.nmea.ignore_trailing_edge = true;
    }
}

const struct gps_type_t gpsclock = {
    .type_name      = "Furuno Electric GH-79L4",	/* full name of type */
    .packet_type    = NMEA_PACKET,	/* associated lexer packet type */
    .trigger	    = "$PFEC,GPssd",	/* GPSclock should return this */
    .channels       = 12,		/* not used by this driver */
    .probe_detect   = NULL,		/* no probe */
    .get_packet     = generic_get,	/* how to get a packet */
    .parse_packet   = nmea_parse_input,	/* how to interpret a packet */
    .rtcm_writer    = pass_rtcm,	/* write RTCM data straight */
    .event_hook     = gpsclock_event_hook,	/* lifetime event handler */
#ifdef ALLOW_RECONFIGURE
    .speed_switcher = NULL,		/* no speed switcher */
    .mode_switcher  = NULL,		/* no mode switcher */
    .rate_switcher  = NULL,		/* sample rate is fixed */
    .min_cycle      = 1,		/* sample rate is fixed */
#endif /* ALLOW_RECONFIGURE */
#ifdef ALLOW_CONTROLSEND
    .control_send   = nmea_write,	/* how to send control strings */
#endif /* ALLOW_CONTROLSEND */
};
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
    /* TripMate requires this response to the ASTRAL it sends at boot time */
    if (event == event_identified)
	(void)nmea_send(session, "$IIGPQ,ASTRAL");
    /* stop it sending PRWIZCH */
    if (event == event_identified || event == event_reactivate)
	(void)nmea_send(session, "$PRWIILOG,ZCH,V,,");
}

static const struct gps_type_t tripmate = {
    .type_name     = "Delorme TripMate",	/* full name of type */
    .packet_type   = NMEA_PACKET,		/* lexer packet type */
    .trigger       ="ASTRAL",			/* tells us to switch */
    .channels      = 12,			/* consumer-grade GPS */
    .probe_detect  = NULL,			/* no probe */
    .get_packet    = generic_get,		/* how to get a packet */
    .parse_packet  = nmea_parse_input,		/* how to interpret a packet */
    .rtcm_writer   = pass_rtcm,			/* send RTCM data straight */
    .event_hook    = tripmate_event_hook,	/* lifetime event handler */
#ifdef ALLOW_RECONFIGURE
    .speed_switcher= NULL,			/* no speed switcher */
    .mode_switcher = NULL,			/* no mode switcher */
    .rate_switcher = NULL,			/* no sample-rate switcher */
    .min_cycle     = 1,				/* no rate switch */
#endif /* ALLOW_RECONFIGURE */
#ifdef ALLOW_CONTROLSEND
    .control_send  = nmea_write,	/* how to send control strings */
#endif /* ALLOW_CONTROLSEND */
};
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
    if (event == event_identified) {
	(void)gpsd_write(session, "EARTHA\r\n", 8);
	(void)usleep(10000);
	(void)gpsd_switch_driver(session, "Zodiac Binary");
    }
}

/*@ -redef @*/
static const struct gps_type_t earthmate = {
    .type_name     = "Delorme EarthMate (pre-2003, Zodiac chipset)",
    .packet_type   = NMEA_PACKET,	/* associated lexer packet type */
    .trigger       = "EARTHA",			/* Earthmate trigger string */
    .channels      = 12,			/* not used by NMEA parser */
    .probe_detect  = NULL,			/* no probe */
    .get_packet    = generic_get,		/* how to get a packet */
    .parse_packet  = nmea_parse_input,		/* how to interpret a packet */
    .rtcm_writer   = NULL,			/* don't send RTCM data */
    .event_hook    = earthmate_event_hook,	/* lifetime event handler */
#ifdef ALLOW_RECONFIGURE
    .speed_switcher= NULL,			/* no speed switcher */
    .mode_switcher = NULL,			/* no mode switcher */
    .rate_switcher = NULL,			/* no sample-rate switcher */
    .min_cycle     = 1,				/* no rate switch */
#endif /* ALLOW_RECONFIGURE */
#ifdef ALLOW_CONTROLSEND
    .control_send  = nmea_write,	/* how to send control strings */
#endif /* ALLOW_CONTROLSEND */
};
/*@ -redef @*/
#endif /* EARTHMATE_ENABLE */

#endif /* NMEA_ENABLE */

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

static void tnt_add_checksum(char *sentence)
{
    unsigned char sum = '\0';
    char c, *p = sentence;

    if (*p == '@') {
	p++;
    } else {
	gpsd_report(LOG_ERROR, "Bad TNT sentence: '%s'\n", sentence);
    }
    while ( ((c = *p) != '*') && (c != '\0')) {
	sum ^= c;
	p++;
    }
    *p++ = '*';
    /*@i@*/snprintf(p, 4, "%02X\r\n", sum);
}


static ssize_t tnt_control_send(struct gps_device_t *session, 
				char *msg, size_t len)
/* send a control string in TNT native formal */
{
    ssize_t status;

    (void)strlcat(msg, "*", BUFSIZ);
    tnt_add_checksum(msg);
    status = write(session->gpsdata.gps_fd, msg, len);
    (void)tcdrain(session->gpsdata.gps_fd);
    if (status == (ssize_t)len)
	gpsd_report(LOG_IO, "=> GPS: %s\n", msg);
    else
	gpsd_report(LOG_WARN, "=> GPS: %s FAILED\n", msg);
    return status;
}

/* for now, only supporting run mode */
#ifdef SAMPLE_MODE_SUPPORTED

static ssize_t tnt_send(struct gps_device_t *session, const char *fmt, ... )
{
    char buf[BUFSIZ];
    va_list ap;

    va_start(ap, fmt) ;
    (void)vsnprintf(buf, sizeof(buf)-5, fmt, ap);
    va_end(ap);
    return tnt_control_send(session, buf, strlen(buf));
}

enum {
#include "packet_states.h"
};
#define TNT_SNIFF_RETRIES       100
/*
 * In sample mode, the True North compass won't start talking
 * unless you ask it to. So to identify it we
 * need to query for its ID string.
 */
static int tnt_packet_sniff(struct gps_device_t *session)
{
    unsigned int n, count = 0;

    gpsd_report(LOG_RAW, "tnt_packet_sniff begins\n");
    for (n = 0; n < TNT_SNIFF_RETRIES; n++)
    {
      count = 0;
      (void)tnt_send(session->gpsdata.gps_fd, "@X?");
      if (ioctl(session->gpsdata.gps_fd, FIONREAD, &count) == -1)
	  return BAD_PACKET;
      if (count == 0) {
	  //int delay = 10000000000.0 / session->gpsdata.baudrate;
	  //gpsd_report(LOG_RAW, "usleep(%d)\n", delay);
	  //usleep(delay);
	  gpsd_report(LOG_RAW, "sleep(1)\n");
	  (void)sleep(1);
      } else if (generic_get(session) >= 0) {
	if((session->packet.type == NMEA_PACKET)&&(session->packet.state == NMEA_RECOGNIZED))
	{
	  gpsd_report(LOG_RAW, "tnt_packet_sniff returns %d\n",session->packet.type);
	  return session->packet.type;
	}
      }
    }

    gpsd_report(LOG_RAW, "tnt_packet_sniff found no packet\n");
    return BAD_PACKET;
}

static void tnt_event_hook(struct gps_device_t *session, event_t event)
{
    if (event == event_identified) {
	// Send codes to start the flow of data
	//tnt_send(session->gpsdata.gps_fd, "@BA?"); // Query current rate
	//tnt_send(session->gpsdata.gps_fd, "@BA=8"); // Start HTM packet at 1Hz
	/*
	 * Sending this twice seems to make it more reliable!!
	 * I think it gets the input on the unit synced up.
	 */
	(void)tnt_send(session->gpsdata.gps_fd, "@BA=15"); // Start HTM packet at 1200 per minute
	(void)tnt_send(session->gpsdata.gps_fd, "@BA=15"); // Start HTM packet at 1200 per minute
    }
}

static bool tnt_probe(struct gps_device_t *session)
{
  unsigned int *ip;
#ifdef FIXED_PORT_SPEED
    /* just the one fixed port speed... */
    static unsigned int rates[] = {FIXED_PORT_SPEED};
#else /* FIXED_PORT_SPEED not defined */
  /* The supported baud rates */
  static unsigned int rates[] = {38400, 19200, 2400, 4800, 9600 };
#endif /* FIXED_PORT_SPEED defined */

  gpsd_report(LOG_PROG, "Probing TrueNorth Compass\n");

  /*
   * Only block until we get at least one character, whatever the
   * third arg of read(2) says.
   */
  /*@ ignore @*/
  memset(session->ttyset.c_cc,0,sizeof(session->ttyset.c_cc));
  session->ttyset.c_cc[VMIN] = 1;
  /*@ end @*/

  session->ttyset.c_cflag &= ~(PARENB | PARODD | CRTSCTS);
  session->ttyset.c_cflag |= CREAD | CLOCAL;
  session->ttyset.c_iflag = session->ttyset.c_oflag = session->ttyset.c_lflag = (tcflag_t) 0;

  session->baudindex = 0;
  for (ip = rates; ip < rates + sizeof(rates)/sizeof(rates[0]); ip++)
      if (ip == rates || *ip != rates[0])
      {
	  gpsd_report(LOG_PROG, "hunting at speed %d\n", *ip);
	  gpsd_set_speed(session, *ip, 'N',1);
	  if (tnt_packet_sniff(session) != BAD_PACKET) {
	      return true;
	  }
      }
  return false;
}
#endif /* SAMPLE_MODE_SUPPORTED */

const struct gps_type_t trueNorth = {
    .type_name      = "True North",	/* full name of type */
    .packet_type    = NMEA_PACKET,	/* associated lexer packet type */
    .trigger	    = "$PTNTHTM",	/* their proprietary sentence */
    .channels       = 0,		/* not an actual GPS at all */
    .probe_detect   = NULL,		/* no probe in run mode */
    .get_packet     = generic_get,	/* how to get a packet */
    .parse_packet   = nmea_parse_input,	/* how to interpret a packet */
    .rtcm_writer    = NULL,		/* Don't send */
    .event_hook     = NULL,		/* lifetime event handler */
#ifdef ALLOW_RECONFIGURE
    .speed_switcher = NULL,		/* no speed switcher */
    .mode_switcher  = NULL,		/* no mode switcher */
    .rate_switcher  = NULL,		/* no wrapup */
    .min_cycle      = 0.5,		/* fixed at 20 samples per second */
#endif /* ALLOW_RECONFIGURE */
#ifdef ALLOW_CONTROLSEND
    .control_send   = tnt_control_send,	/* how to send control strings */
#endif /* ALLOW_CONTROLSEND */
};
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

static int oceanserver_send(int fd, const char *fmt, ... )
{
    int status;
    char buf[BUFSIZ];
    va_list ap;

    va_start(ap, fmt) ;
    (void)vsnprintf(buf, sizeof(buf)-5, fmt, ap);
    va_end(ap);
    (void)strlcat(buf, "", BUFSIZ);
    status = (int)write(fd, buf, strlen(buf));
    (void)tcdrain(fd);
    if (status == (int)strlen(buf)) {
	gpsd_report(LOG_IO, "=> GPS: %s\n", buf);
	return status;
    } else {
	gpsd_report(LOG_WARN, "=> GPS: %s FAILED\n", buf);
	return -1;
    }
}

static void oceanserver_event_hook(struct gps_device_t *session, event_t event)
{
    if (event == event_configure && session->packet.counter == 0) {
	/* report in NMEA format */
	(void)oceanserver_send(session->gpsdata.gps_fd, "2\n");
	/* ship all fields */
	(void)oceanserver_send(session->gpsdata.gps_fd, "X2047");
    }
}

static const struct gps_type_t oceanServer = {
    .type_name      = "OceanServer Digital Compass OS5000", /* full name of type */
    .packet_type    = NMEA_PACKET,	/* associated lexer packet type */
    .trigger	    = "$C,",
    .channels       = 0,		/* not an actual GPS at all */
    .probe_detect   = NULL,
    .get_packet     = generic_get,	/* how to get a packet */
    .parse_packet   = nmea_parse_input,	/* how to interpret a packet */
    .rtcm_writer    = NULL,		/* Don't send */
    .event_hook     = oceanserver_event_hook,
#ifdef ALLOW_RECONFIGURE
    .speed_switcher = NULL,		/* no speed switcher */
    .mode_switcher  = NULL,		/* no mode switcher */
    .rate_switcher  = NULL,		/* no wrapup */
    .min_cycle      = 1,		/* not relevant, no rate switch */
#endif /* ALLOW_RECONFIGURE */
#ifdef ALLOW_CONTROLSEND
    .control_send   = nmea_write,	/* how to send control strings */
#endif /* ALLOW_CONTROLSEND */
};
#endif

#ifdef RTCM104V2_ENABLE
/**************************************************************************
 *
 * RTCM-104 (v2), used for broadcasting DGPS corrections and by DGPS radios
 *
 **************************************************************************/

static gps_mask_t rtcm104v2_analyze(struct gps_device_t *session)
{
    rtcm2_unpack(&session->gpsdata.rtcm2, (char *)session->packet.isgps.buf);
    gpsd_report(LOG_RAW, "RTCM 2.x packet type 0x%02x length %d words: %s\n",
	session->gpsdata.rtcm2.type, session->gpsdata.rtcm2.length+2,
	gpsd_hexdump_wrapper(session->packet.isgps.buf,
	    (session->gpsdata.rtcm2.length+2)*sizeof(isgps30bits_t), LOG_RAW));
    return RTCM2_IS;
}

static const struct gps_type_t rtcm104v2 = {
    .type_name     = "RTCM104V2",	/* full name of type */
    .packet_type   = RTCM2_PACKET,	/* associated lexer packet type */
    .trigger       = NULL,		/* no recognition string */
    .channels      = 0,			/* not used */
    .probe_detect  = NULL,		/* no probe */
    .get_packet    = generic_get,	/* how to get a packet */
    .parse_packet  = rtcm104v2_analyze,	/*  */
    .rtcm_writer   = NULL,		/* don't send RTCM data,  */
    .event_hook    = NULL,		/* no event_hook */
#ifdef ALLOW_RECONFIGURE
    .speed_switcher= NULL,		/* no speed switcher */
    .mode_switcher = NULL,		/* no mode switcher */
    .rate_switcher = NULL,		/* no sample-rate switcher */
    .min_cycle     = 1,			/* not relevant, no rate switch */
#endif /* ALLOW_RECONFIGURE */
#ifdef ALLOW_CONTROLSEND
    .control_send   = nmea_write,	/* how to send control strings */
#endif /* ALLOW_CONTROLSEND */
};
#endif /* RTCM104V2_ENABLE */
#ifdef RTCM104V3_ENABLE
/**************************************************************************
 *
 * RTCM-104 (v3), used for broadcasting DGPS corrections and by DGPS radios
 *
 **************************************************************************/

static gps_mask_t rtcm104v3_analyze(struct gps_device_t *session)
{
    uint16_t length = getbeuw(session->packet.inbuffer, 1);
    uint16_t type = getbeuw(session->packet.inbuffer, 3) >> 4;

    gpsd_report(LOG_RAW, "RTCM 3.x packet type %d length %d words: %s\n",
	type, length, gpsd_hexdump_wrapper(session->packet.inbuffer,
	    (size_t)(session->gpsdata.rtcm3.length), LOG_RAW));
    return RTCM3_IS;
}

static const struct gps_type_t rtcm104v3 = {
    .type_name     = "RTCM104V3",	/* full name of type */
    .packet_type   = RTCM3_PACKET,	/* associated lexer packet type */
    .trigger       = NULL,		/* no recognition string */
    .channels      = 0,			/* not used */
    .probe_detect  = NULL,		/* no probe */
    .get_packet    = generic_get,	/* how to get a packet */
    .parse_packet  = rtcm104v3_analyze,	/*  */
    .rtcm_writer   = NULL,		/* don't send RTCM data,  */
    .event_hook    = NULL,		/* no event hook */
#ifdef ALLOW_RECONFIGURE
    .speed_switcher= NULL,		/* no speed switcher */
    .mode_switcher = NULL,		/* no mode switcher */
    .rate_switcher = NULL,		/* no sample-rate switcher */
    .min_cycle     = 1,			/* not relevant, no rate switch */
#endif /* ALLOW_RECONFIGURE */
#ifdef ALLOW_CONTROLSEND
    .control_send   = nmea_write,	/* how to send control strings */
#endif /* ALLOW_CONTROLSEND */
};
#endif /* RTCM104V3_ENABLE */

#ifdef GARMINTXT_ENABLE
/**************************************************************************
 *
 * Garmin Simple Text protocol
 *
 **************************************************************************/

static gps_mask_t garmintxt_parse_input(struct gps_device_t *session)
{
    //gpsd_report(LOG_PROG, "Garmin Simple Text packet\n");
    return garmintxt_parse(session);
}


static const struct gps_type_t garmintxt = {
    .type_name     = "Garmin Simple Text",		/* full name of type */
    .packet_type   = GARMINTXT_PACKET,	/* associated lexer packet type */
    .trigger       = NULL,		/* no recognition string */
    .channels      = 0,			/* not used */
    .probe_detect  = NULL,		/* no probe */
    .get_packet    = generic_get,	/* how to get a packet */
    .parse_packet  = garmintxt_parse_input,	/*  */
    .rtcm_writer   = NULL,		/* don't send RTCM data,  */
    .event_hook    = NULL,		/* no event hook */
#ifdef ALLOW_RECONFIGURE
    .speed_switcher= NULL,		/* no speed switcher */
    .mode_switcher = NULL,		/* no mode switcher */
    .rate_switcher = NULL,		/* no sample-rate switcher */
    .min_cycle     = 1,			/* not relevant, no rate switch */
#endif /* ALLOW_RECONFIGURE */
#ifdef ALLOW_CONTROLSEND
    .control_send   = nmea_write,	/* how to send control strings */
#endif /* ALLOW_CONTROLSEND */
};
#endif /* GARMINTXT_ENABLE */

#ifdef MTK3301_ENABLE
/**************************************************************************
 *
 * MTK-3301
 *
 **************************************************************************/
const char* mtk_reasons[4] = {"Invalid", "Unsupported", "Valid but Failed", "Valid success"};

gps_mask_t processMTK3301(int c UNUSED, char *field[], struct gps_device_t *session)
{
    int msg, reason;
    gps_mask_t mask;
    mask = 1; //ONLINE_IS;

    switch(msg = atoi(&(field[0])[4]))
    {
	case 705: /*  */
	    (void)strlcat(session->subtype,field[1],64);
	    (void)strlcat(session->subtype,"-",64);
	    (void)strlcat(session->subtype,field[2],64);
	    return 0; /* return a unknown sentence, which will cause the driver switch */
	case 001: /* ACK / NACK */
	    reason = atoi(field[2]);
	    if(atoi(field[1]) == -1)
		gpsd_report(LOG_WARN, "MTK NACK: unknown sentence\n");
	    else if(reason < 3)
		gpsd_report(LOG_WARN, "MTK NACK: %s, reason: %s\n", field[1], mtk_reasons[reason]);
	    else
		gpsd_report(LOG_WARN, "MTK ACK: %s\n", field[1]);
	    break;
	default:
	    return 0; /* ignore */
    }
    return mask;
}

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
    /* FIXME: Do we need to resend this on reactivation? */
    if(event == event_identified) {
	(void)nmea_send(session,"$PMTK320,0"); /* power save off */
	(void)nmea_send(session,"$PMTK300,1000,0,0,0.0,0.0"); /* Fix interval */
	(void)nmea_send(session,"$PMTK314,0,1,0,1,1,5,1,1,0,0,0,0,0,0,0,0,0,1,0");
	(void)nmea_send(session,"$PMTK301,2"); /* DGPS is WAAS */
	(void)nmea_send(session,"$PMTK313,1"); /* SBAS enable */
    }
}

#ifdef ALLOW_RECONFIGURE
static bool mtk3301_rate_switcher(struct gps_device_t *session, double rate)
{
    char buf[78];
    /*@i1@*/unsigned int milliseconds = 1000 * rate;
    if(rate > 1)
	milliseconds=1000;
    else if(rate < 0.2)
	milliseconds=200;
	
    (void)snprintf(buf, 78, "$PMTK300,%u,0,0,0,0", milliseconds);
    (void)nmea_send(session,buf); /* Fix interval */
    return true;
}
#endif /* ALLOW_RECONFIGURE */

const struct gps_type_t mtk3301 = {
    .type_name      = "MTK-3301",	/* full name of type */
    .packet_type    = NMEA_PACKET,	/* associated lexer packet type */
    .trigger	    = "$PMTK705,",	/* MTK-3301s send firmware release name and version */
    .channels       = 12,		/* not used by this driver */
    .probe_detect   = NULL,		/* no probe */
    .get_packet     = generic_get,	/* how to get a packet */
    .parse_packet   = nmea_parse_input,	/* how to interpret a packet */
    .rtcm_writer    = pass_rtcm,	/* write RTCM data straight */
    .event_hook     = mtk3301_event_hook,	/* lifetime event handler */
#ifdef ALLOW_RECONFIGURE
    .speed_switcher = NULL,		/* no speed switcher */
    .mode_switcher  = NULL,		/* no mode switcher */
    .rate_switcher  = mtk3301_rate_switcher,		/* sample rate switcher */
    .min_cycle      = 0.2,		/* max 5Hz */
#endif /* ALLOW_RECONFIGURE */
#ifdef ALLOW_CONTROLSEND
    .control_send   = nmea_write,	/* how to send control strings */
#endif /* ALLOW_CONTROLSEND */
};
#endif /* MTK3301_ENABLE */


#ifdef AIVDM_ENABLE
/**************************************************************************
 *
 * AIVDM
 *
 **************************************************************************/

static gps_mask_t aivdm_analyze(struct gps_device_t *session)
{
    if (session->packet.type == AIVDM_PACKET) {
	if (aivdm_decode((char *)session->packet.outbuffer, session->packet.outbuflen, &session->aivdm, &session->gpsdata.ais)) {
	    return ONLINE_IS | AIS_IS;
	}else
	    return ONLINE_IS;
#ifdef NMEA_ENABLE
    } else if (session->packet.type == NMEA_PACKET) {
	return nmea_parse((char *)session->packet.outbuffer, session);
#endif /* NMEA_ENABLE */
    } else
	return 0;
}

static const struct gps_type_t aivdm = {
    /* Full name of type */
    .type_name        = "AIVDM",
    /* Associated lexer packet type */
    .packet_type      = AIVDM_PACKET,
    /* Response string that identifies device (not active) */
    .trigger          = NULL,
    /* Number of satellite channels supported by the device */
    .channels         = 0,
    /* Startup-time device detector */
    .probe_detect     = NULL,
    /* Packet getter (using default routine) */
    .get_packet       = generic_get,
    /* Parse message packets */
    .parse_packet     = aivdm_analyze,
    /* RTCM handler (using default routine) */
    .rtcm_writer      = NULL,
    /* Handle various lifetime events */
    .event_hook       = NULL,
#ifdef ALLOW_RECONFIGURE
    /* Speed (baudrate) switch */
    .speed_switcher   = NULL,
    /* Switch to NMEA mode */
    .mode_switcher    = NULL,
    /* Message delivery rate switcher (not active) */
    .rate_switcher    = NULL,
    /* Minimum cycle time of the device */
    .min_cycle        = 1,
#endif /* ALLOW_RECONFIGURE */
#ifdef ALLOW_CONTROLSEND
    /* Control string sender - should provide checksum and headers/trailer */
    .control_send     = NULL,
#endif /* ALLOW_CONTROLSEND */
};
#endif /* AIVDM_ENABLE */

extern const struct gps_type_t garmin_usb_binary, garmin_ser_binary;
extern const struct gps_type_t tsip_binary, oncore_binary;
extern const struct gps_type_t evermore_binary, italk_binary;
extern const struct gps_type_t navcom_binary, superstar2_binary;

/*@ -nullassign @*/
/* the point of this rigamarole is to not have to export a table size */
static const struct gps_type_t *gpsd_driver_array[] = {
#ifdef NMEA_ENABLE
    &nmea,
#ifdef ASHTECH_ENABLE
    &ashtech,
#endif /* ASHTECHV18_ENABLE */
#ifdef TRIPMATE_ENABLE
    &tripmate,
#endif /* TRIPMATE_ENABLE */
#ifdef EARTHMATE_ENABLE
    &earthmate,
#endif /* EARTHMATE_ENABLE */
#ifdef GPSCLOCK_ENABLE
    &gpsclock,
#endif /* GPSCLOCK_ENABLE */
#ifdef GARMIN_ENABLE
    &garmin,
#endif /* GARMIN_ENABLE */
#ifdef MTK3301_ENABLE
    &mtk3301,
#endif /*  MTK3301_ENABLE */
#ifdef OCEANSERVER_ENABLE
    &oceanServer,
#endif /* OCEANSERVER_ENABLE */
#ifdef FV18_ENABLE
    &fv18,
#endif /* FV18_ENABLE */
#ifdef TNT_ENABLE
    &trueNorth,
#endif /* TNT_ENABLE */
#ifdef AIVDM_ENABLE
    &aivdm,
#endif /* AIVDM_ENABLE */
#endif /* NMEA_ENABLE */


#ifdef EVERMORE_ENABLE
    &evermore_binary,
#endif /* EVERMORE_ENABLE */
#ifdef GARMIN_ENABLE
    &garmin_usb_binary,
    &garmin_ser_binary,
#endif /* GARMIN_ENABLE */
#ifdef ITRAX_ENABLE
    &italk_binary,
#endif /* ITRAX_ENABLE */
#ifdef ONCORE_ENABLE
    &oncore_binary,
#endif /* ONCORE_ENABLE */
#ifdef NAVCOM_ENABLE
    &navcom_binary,
#endif /* NAVCOM_ENABLE */
#ifdef SIRF_ENABLE
    &sirf_binary,
#endif /* SIRF_ENABLE */
#ifdef SUPERSTAR2_ENABLE
    &superstar2_binary,
#endif /* SUPERSTAR2_ENABLE */
#ifdef TSIP_ENABLE
    &tsip_binary,
#endif /* TSIP_ENABLE */
#ifdef UBX_ENABLE
    &ubx_binary,
#endif /* UBX_ENABLE */
#ifdef ZODIAC_ENABLE
    &zodiac_binary,
#endif /* ZODIAC_ENABLE */

#ifdef RTCM104V2_ENABLE
    &rtcm104v2,
#endif /* RTCM104V2_ENABLE */
#ifdef RTCM104V3_ENABLE
    &rtcm104v3,
#endif /* RTCM104V3_ENABLE */
#ifdef GARMINTXT_ENABLE
    &garmintxt,
#endif /* GARMINTXT_ENABLE */
    NULL,
};
/*@ +nullassign @*/
const struct gps_type_t **gpsd_drivers = &gpsd_driver_array[0];
