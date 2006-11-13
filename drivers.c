/* $Id$ */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <math.h>
#include <stdarg.h>

#include "gpsd_config.h"
#include "gpsd.h"

extern struct gps_type_t zodiac_binary;

#if defined(NMEA_ENABLE) || defined(SIRF_ENABLE) || defined(EVERMORE_ENABLE)  || defined(ITALK_ENABLE) 
ssize_t pass_rtcm(struct gps_device_t *session, char *buf, size_t rtcmbytes)
/* most GPSes take their RTCM corrections straight up */
{
    return write(session->gpsdata.gps_fd, buf, rtcmbytes);
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
    if (session->packet_type == SIRF_PACKET) {
	gpsd_report(LOG_WARN, "SiRF packet seen when NMEA expected.\n");
#ifdef SIRF_ENABLE
	return sirf_parse(session, session->outbuffer, session->outbuflen);
#else
	return 0;
#endif /* SIRF_ENABLE */
    } else if (session->packet_type == EVERMORE_PACKET) {
	gpsd_report(LOG_WARN, "EverMore packet seen when NMEA expected.\n");
#ifdef EVERMORE_ENABLE
	(void)gpsd_switch_driver(session, "EverMore NMEA");
	return evermore_parse(session, session->outbuffer, session->outbuflen);
#else
	return 0;
#endif /* EVERMORE_ENABLE */
    } else if (session->packet_type == GARMIN_PACKET) {
	gpsd_report(LOG_WARN, "Garmin packet seen when NMEA expected.\n");
#ifdef GARMIN_ENABLE
	/* we might never see a trigger, have this as a backstop */
	(void)gpsd_switch_driver(session, "Garmin Serial binary");
	return garmin_ser_parse(session);
#else
	return 0;
#endif /* GARMIN_ENABLE */
    } else if (session->packet_type == NMEA_PACKET) {
	gps_mask_t st = 0;
	gpsd_report(LOG_IO, "<= GPS: %s", session->outbuffer);
	if ((st=nmea_parse((char *)session->outbuffer, session))==0) {
#ifdef NON_NMEA_ENABLE
	    struct gps_type_t **dp;

	    /* maybe this is a trigger string for a driver we know about? */
	    for (dp = gpsd_drivers; *dp; dp++) {
		char	*trigger = (*dp)->trigger;

		if (trigger!=NULL && strncmp((char *)session->outbuffer, trigger, strlen(trigger))==0 && isatty(session->gpsdata.gps_fd)!=0) {
		    gpsd_report(LOG_PROG, "found %s.\n", trigger);
		    (void)gpsd_switch_driver(session, (*dp)->typename);
		    return 1;
		}
	    }
#endif /* NON_NMEA_ENABLE */
	    gpsd_report(LOG_WARN, "unknown sentence: \"%s\"\n", session->outbuffer);
	}
#ifdef NMEADISC
	if (st & TIME_SET && session->gpsdata.ldisc == 0) {
	    int ldisc = NMEADISC;

	    if (ioctl(session->gpsdata.gps_fd, TIOCSETD, &ldisc) == -1)
		gpsd_report(LOG_ERROR, "can't set nmea discipline\n");
	    else
		session->gpsdata.ldisc = NMEADISC;
	}
#endif
#ifdef NTPSHM_ENABLE
	/* this magic number is derived from observation */
	if (session->context->enable_ntpshm &&
	    (st & TIME_SET) != 0 &&
	    (session->gpsdata.fix.time!=session->last_fixtime)) {
	    /* this magic number is derived from observation */
	    /* GPS-18/USB -> 0.100 */
	    /* GPS-18/LVC at 19200 -> 0.125 */
	    /* GPS-18/LVC at 4800 -> 0.525*/
	    /* Rob Jensen reports 0.675 */
	    (void)ntpshm_put(session, session->gpsdata.fix.time + 0.400);
	    session->last_fixtime = session->gpsdata.fix.time;
	}
#endif /* NTPSHM_ENABLE */
	return st;
    } else
	return 0;
}

static void nmea_probe_subtype(struct gps_device_t *session, unsigned int seq)
{
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
    switch (seq) {
#ifdef SIRF_ENABLE
    case 0:
	/* probe for SiRF -- expect $Ack 105. */
	(void)nmea_send(session->gpsdata.gps_fd, "$PSRF105,1");
	break;
#endif /* SIRF_ENABLE */
#ifdef NMEA_ENABLE
    case 1:
	/* probe for Garmin serial GPS -- expect $PGRMC followed by data*/
	(void)nmea_send(session->gpsdata.gps_fd, "$PGRMCE");
	break;
    case 2:
	/* probe for the FV-18 -- expect $PFEC,GPint followed by data */
	(void)nmea_send(session->gpsdata.gps_fd, "$PFEC,GPint");
	break;
#endif /* NMEA_ENABLE */
#ifdef EVERMORE_ENABLE
    case 3:
	/* probe for EverMore by trying to read the LogConfig */
	/* as a probe try to set DATUM to WGS-84 with EverMore binary command */
	(void)gpsd_write(session, "\x10\x02\x06\x8d\x00\x01\x00\x8e\x10\x03", 10);
	break;
#endif /* EVERMORE_ENABLE */
#ifdef ITRAX_ENABLE
    case 4:
	/* probe for iTrax, looking for "$PFST,OK" */
	(void)nmea_send(session->gpsdata.gps_fd, "$PFST");
	break;
#endif /* ITRAX_ENABLE */
    default:
	break;
    }
}

static void nmea_configurator(struct gps_device_t *session)
{
#ifdef ALLOW_RECONFIGURE
    /* Sony CXD2951 chips: +GGA, -GLL, +GSA, +GSV, +RMC, -VTG, +ZDA, -PSGSA */
    (void)nmea_send(session->gpsdata.gps_fd, "@NC10151010");
    /* enable GPZDA on a Motorola Oncore GT+ */
    (void)nmea_send(session->gpsdata.gps_fd, "$PMOTG,ZDA,1");
#endif /* ALLOW_RECONFIGURE */
}

static struct gps_type_t nmea = {
    .typename       = "Generic NMEA",	/* full name of type */
    .trigger        = NULL,		/* it's the default */
    .channels       = 12,		/* consumer-grade GPS */
    .probe_wakeup   = NULL,		/* no wakeup to be done before hunt */
    .probe_detect   = NULL,		/* no probe */
    .probe_subtype  = nmea_probe_subtype,	/* probe for special types */
    .configurator   = nmea_configurator,/* enable what we need */
    .get_packet     = packet_get,		/* use generic packet getter */
    .parse_packet   = nmea_parse_input,	/* how to interpret a packet */
    .rtcm_writer    = pass_rtcm,	/* write RTCM data straight */
    .speed_switcher = NULL,		/* no speed switcher */
    .mode_switcher  = NULL,		/* no mode switcher */
    .rate_switcher  = NULL,		/* no sample-rate switcher */
    .cycle_chars    = -1,		/* not relevant, no rate switch */
    .wrapup         = NULL,		/* no wrapup */
    .cycle          = 1,		/* updates every second */
};

static void garmin_nmea_configurator(struct gps_device_t *session)
{
#ifdef ALLOW_RECONFIGURE
#if defined(NMEA_ENABLE) && !defined(GARMIN_ENABLE_UNUSED)
    /* reset some config, AutoFix, WGS84, PPS */
    (void)nmea_send(session->gpsdata.gps_fd, "$PGRMC,A,,100,,,,,,A,,1,2,4,30");
    /* once a sec, no averaging, NMEA 2.3, WAAS */
    (void)nmea_send(session->gpsdata.gps_fd, "$PGRMC1,1,1,1,,,,2,W,N");
    /* get some more config info */
    (void)nmea_send(session->gpsdata.gps_fd, "$PGRMC1E");
    /* turn off all output */
    (void)nmea_send(session->gpsdata.gps_fd, "$PGRMO,,2");
    /* enable GPGGA, GPGSA, GPGSV, GPRMC on Garmin serial GPS */
    (void)nmea_send(session->gpsdata.gps_fd, "$PGRMO,GPGGA,1");
    (void)nmea_send(session->gpsdata.gps_fd, "$PGRMO,GPGSA,1");
    (void)nmea_send(session->gpsdata.gps_fd, "$PGRMO,GPGSV,1");
    (void)nmea_send(session->gpsdata.gps_fd, "$PGRMO,GPRMC,1");
    (void)nmea_send(session->gpsdata.gps_fd, "$PGRMO,PGRME,1");
#endif /* NMEA_ENABLE && !GARMIN_ENABLE */
#if GARMIN_ENABLE_UNUSED
    /* try to go binary */
    /* once a sec, binary, no averaging, NMEA 2.3, WAAS */
    (void)nmea_send(session->gpsdata.gps_fd, "$PGRMC1,1,2,1,,,,2,W,N");
    /* reset to get into binary */
    (void)nmea_send(session->gpsdata.gps_fd, "$PGRMI,,,,,,,R");
    /* probe for Garmin serial binary by trying to Product Data request */
    /* DLE, PktID, Size, data (none), CHksum, DLE, ETX 
    (void)gpsd_write(session, "\x10\xFE\x00\x02\x10\x03", 6); */
#endif /* GARMIN_ENABLE */
#endif /* ALLOW_RECONFIGURE */
}

static struct gps_type_t garmin = {
    .typename       = "Garmin Serial",	/* full name of type */
    .trigger        = "$PGRMC,",	/* Garmin private */
    .channels       = 12,		/* not used by this driver */
    .probe_wakeup   = NULL,		/* no wakeup to be done before hunt */
    .probe_detect   = NULL,		/* no probe */
    .probe_subtype   = NULL,		/* no further querying */
    .configurator   = garmin_nmea_configurator,/* enable what we need */
    .get_packet     = packet_get,	/* use generic packet getter */
    .parse_packet   = nmea_parse_input,	/* how to interpret a packet */
    .rtcm_writer    = NULL,		/* some do, some don't, skip for now */
    .speed_switcher = NULL,		/* no speed switcher */
    .mode_switcher  = NULL,		/* no mode switcher */
    .rate_switcher  = NULL,		/* no sample-rate switcher */
    .cycle_chars    = -1,		/* not relevant, no rate switch */
    .wrapup         = NULL,		/* no wrapup */
    .cycle          = 1,		/* updates every second */
};

#if FV18_ENABLE
/**************************************************************************
 *
 * FV18 -- uses 2 stop bits, needs to be told to send GSAs
 *
 **************************************************************************/

static void fv18_configure(struct gps_device_t *session)
{
    /*
     * Tell an FV18 to send GSAs so we'll know if 3D is accurate.
     * Suppress GLL and VTG.  Enable ZDA so dates will be accurate for replay.
     */
    (void)nmea_send(session->gpsdata.gps_fd,
		    "$PFEC,GPint,GSA01,DTM00,ZDA01,RMC01,GLL00,VTG00,GSV05");
}

static struct gps_type_t fv18 = {
    .typename       = "San Jose Navigation FV18",	/* full name of type */
    .trigger        = "$PFEC,GPint,",	/* FV18s should echo the probe */
    .channels       = 12,		/* not used by this driver */
    .probe_wakeup   = NULL,		/* no wakeup to be done before hunt */
    .probe_detect   = NULL,		/* mo probe */
    .probe_subtype  = NULL,		/* to be sent unconditionally */
    .configurator   = fv18_configure,	/* change its sentence set */
    .get_packet     = packet_get,	/* how to get a packet */
    .parse_packet   = nmea_parse_input,	/* how to interpret a packet */
    .rtcm_writer    = pass_rtcm,	/* write RTCM data straight */
    .speed_switcher = NULL,		/* no speed switcher */
    .mode_switcher  = NULL,		/* no mode switcher */
    .rate_switcher  = NULL,		/* no sample-rate switcher */
    .cycle_chars    = -1,		/* not relevant, no rate switch */
    .wrapup         = NULL,		/* no wrapup */
    .cycle          = 1,		/* updates every second */
};
#endif /* FV18_ENABLE */

/**************************************************************************
 *
 * SiRF NMEA
 *
 * Mostly this is a stopover on the way to SiRF binary mode, but NMEA methods
 * are included in case we're building without SiRF binary support.
 *
 **************************************************************************/

static void sirf_probe_subtype(struct gps_device_t *session, unsigned int seq)
{
    if (seq == 0)
	(void)nmea_send(session->gpsdata.gps_fd, "$PSRF105,0");
}

static bool sirf_switcher(struct gps_device_t *session, int nmea, unsigned int speed) 
/* switch GPS to specified mode at 8N1, optionally to binary */
{
    if (nmea_send(session->gpsdata.gps_fd, "$PSRF100,%d,%d,8,1,0", nmea,speed) < 0)
	return false;
    return true;
}

static bool sirf_speed(struct gps_device_t *session, unsigned int speed)
/* change the baud rate, remaining in SiRF NMEA mode */
{
    return sirf_switcher(session, 1, speed);
}

static void sirf_mode(struct gps_device_t *session, int mode)
/* change mode to SiRF binary, speed unchanged */
{
    if (mode == 1) {
	(void)gpsd_switch_driver(session, "SiRF binary");
	session->gpsdata.driver_mode = (unsigned int)sirf_switcher(session, 0, session->gpsdata.baudrate);
	session->gpsdata.driver_mode = 1;
    } else
	session->gpsdata.driver_mode = 0;
}

static void sirf_configurator(struct gps_device_t *session)
{
#ifdef ALLOW_RECONFIGURE
#if defined(BINARY_ENABLE)
    sirf_mode(session, 1);	/* throw us to SiRF binary */
#else    
    (void)nmea_send(session->gpsdata.gps_fd, "$PSRF103,05,00,00,01"); /* no VTG */
    (void)nmea_send(session->gpsdata.gps_fd, "$PSRF103,01,00,00,01"); /* no GLL */
#endif /* BINARY_ENABLE */
#endif /* ALLOW_RECONFIGURE */
}

static struct gps_type_t sirf_nmea = {
    .typename      = "SiRF NMEA",	/* full name of type */
    .trigger       = "$Ack Input105.",	/* expected response to SiRF PSRF105 */
    .channels      = 12,		/* not used by the NMEA parser */
    .probe_wakeup  = NULL,		/* no wakeup to be done before hunt */
    .probe_detect  = NULL,		/* no probe */
    .probe_subtype = sirf_probe_subtype,	/* probe for type info */
    .configurator  = sirf_configurator,	/* turn off debuging messages */
    .get_packet    = packet_get,	/* how to get a packet */
    .parse_packet  = nmea_parse_input,	/* how to interpret a packet */
    .rtcm_writer   = pass_rtcm,		/* write RTCM data straight */
    .speed_switcher= sirf_speed,	/* we can change speeds */
    .mode_switcher = sirf_mode,		/* there's a mode switch */
    .rate_switcher = NULL,		/* no sample-rate switcher */
    .cycle_chars   = -1,		/* not relevant, no rate switch */
    .wrapup         = NULL,		/* no wrapup */
    .cycle          = 1,		/* updates every second */
};

/**************************************************************************
 *
 * EverMore NMEA
 *
 * Mostly this is a stopover on the way to EverMore binary mode, but NMEA methods
 * are included in case we're building without EverMore binary support.
 *
 **************************************************************************/
#ifdef EVERMORE_ENABLE


static bool evermore_speed(struct gps_device_t *session, unsigned int speed)
/* change the baud rate, remaining in SiRF NMEA mode */
{

        gpsd_report(LOG_PROG, "evermore_speed(%d)\n", speed);
	const char *emt_speedcfg;
	switch (speed) {
	    case 4800:  emt_speedcfg = "\x10\x02\x06\x89\x01\x00\x00\x8a\x10\x03"; break;
	    case 9600:  emt_speedcfg = "\x10\x02\x06\x89\x01\x01\x00\x8b\x10\x03"; break;
	    case 19200: emt_speedcfg = "\x10\x02\x06\x89\x01\x02\x00\x8c\x10\x03"; break;
	    case 38400: emt_speedcfg = "\x10\x02\x06\x89\x01\x03\x00\x8d\x10\x03"; break;
	    default: return false;
	}
    (void)gpsd_write(session, emt_speedcfg, 10);
    return true;
}

static void evermore_mode(struct gps_device_t *session, int mode)
/* change mode to EverMore binary, speed unchanged */
{
    gpsd_report(LOG_PROG, "evermore_mode(%d)\n", mode);
    if (mode == 1) {
	(void)gpsd_switch_driver(session, "EverMore binary");
	session->gpsdata.driver_mode = 1;
    } else
	(void)gpsd_switch_driver(session, "EverMore NMEA");
	session->gpsdata.driver_mode = 0;
}

static void evermore_configure(struct gps_device_t *session)
{
    gpsd_report(LOG_PROG, "evermore_configure\n");
#ifdef ALLOW_RECONFIGURE
    /* enable checksum and messages GGA(1s), GLL(0s), GSA(1s), GSV(5s), RMC(1s), VTG(0s), PEMT100(0s) */
    const char *emt_nmea_cfg = 
	    "\x10\x02\x12\x8E\xFF\x01\x01\x00\x01\x05\x01\x00\x00\x00\x00\x00\x00\x00\x00\x96\x10\x03";
    (void)gpsd_write(session, emt_nmea_cfg, 22);
#endif /* ALLOW_RECONFIGUR/ */
}

static struct gps_type_t evermore_nmea = {
    .typename      = "EverMore NMEA",	/* full name of type */
    .trigger       = "$PEMT,",		/* expected response to probe */
    .channels      = 12,		/* not used by the NMEA parser */
    .probe_wakeup  = NULL,		/* no wakeup to be done before hunt */
    .probe_detect  = NULL,		/* no probe */
    .probe_subtype = NULL,		/* probe for type info */
    .configurator  = evermore_configure,/* turn off debuging messages */
    .get_packet    = packet_get,	/* how to get a packet */
    .parse_packet  = nmea_parse_input,	/* how to interpret a packet */
    .rtcm_writer   = NULL,		/* write RTCM data straight */
    .speed_switcher= evermore_speed,	/* we can change speeds */
    .mode_switcher = evermore_mode,	/* there's a mode switch */
    .rate_switcher = NULL,		/* no sample-rate switcher */
    .cycle_chars   = -1,		/* not relevant, no rate switch */
    .wrapup         = NULL,		/* no wrapup */
    .cycle          = 1,		/* updates every second */
};
#endif

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

static void tripmate_probe_subtype(struct gps_device_t *session, unsigned int seq)
{
    /* TripMate requires this response to the ASTRAL it sends at boot time */
    if (seq == 0)
	(void)nmea_send(session->gpsdata.gps_fd, "$IIGPQ,ASTRAL");
}

static void tripmate_configurator(struct gps_device_t *session)
{
#ifdef ALLOW_RECONFIGURE
    /* stop it sending PRWIZCH */
    (void)nmea_send(session->gpsdata.gps_fd, "$PRWIILOG,ZCH,V,,");
#endif /* ALLOW_RECONFIGURE */
}

static struct gps_type_t tripmate = {
    .typename      = "Delorme TripMate",	/* full name of type */
    .trigger       ="ASTRAL",			/* tells us to switch */
    .channels      = 12,			/* consumer-grade GPS */
    .probe_wakeup  = NULL,			/* no wakeup before hunt */
    .probe_detect  = NULL,			/* no probe */
    .probe_subtype = tripmate_probe_subtype,	/* send unconditionally */
    .configurator  = tripmate_configurator,	/* send unconditionally */
    .get_packet    = packet_get,		/* how to get a packet */
    .parse_packet  = nmea_parse_input,		/* how to interpret a packet */
    .rtcm_writer   = pass_rtcm,			/* send RTCM data straight */
    .speed_switcher= NULL,			/* no speed switcher */
    .mode_switcher = NULL,			/* no mode switcher */
    .rate_switcher = NULL,			/* no sample-rate switcher */
    .cycle_chars   = -1,			/* no rate switch */
    .wrapup         = NULL,			/* no wrapup */
    .cycle          = 1,			/* updates every second */
};
#endif /* TRIPMATE_ENABLE */

#ifdef EARTHMATE_ENABLE
/**************************************************************************
 *
 * Zodiac EarthMate textual mode
 *
 * Note: This is the pre-2003 version using Zodiac binary protocol.
 * It has been replaced with a design that uses a SiRF chipset.
 *
 **************************************************************************/

static struct gps_type_t earthmate;

/*
 * There is a good HOWTO at <http://www.hamhud.net/ka9mva/earthmate.htm>.
 */

static void earthmate_close(struct gps_device_t *session)
{
    /*@i@*/session->device_type = &earthmate;
}

static void earthmate_probe_subtype(struct gps_device_t *session, unsigned int seq)
{
    if (seq == 0) {
	(void)write(session->gpsdata.gps_fd, "EARTHA\r\n", 8);
	(void)usleep(10000);
	/*@i@*/session->device_type = &zodiac_binary;
	zodiac_binary.wrapup = earthmate_close;
	if (zodiac_binary.probe_subtype) zodiac_binary.probe_subtype(session, seq);
    }
}

/*@ -redef @*/
static struct gps_type_t earthmate = {
    .typename      = "Delorme EarthMate (pre-2003, Zodiac chipset)",
    .trigger       = "EARTHA",			/* Earthmate trigger string */
    .channels      = 12,			/* not used by NMEA parser */
    .probe_wakeup  = NULL,		/* no wakeup to be done before hunt */
    .probe_detect  = NULL,			/* no probe */
    .probe_subtype = earthmate_probe_subtype,	/* switch us to Zodiac mode */
    .configurator  = NULL,			/* no configuration here */
    .get_packet    = packet_get,		/* how to get a packet */
    .parse_packet  = nmea_parse_input,		/* how to interpret a packet */
    .rtcm_writer   = NULL,			/* don't send RTCM data */
    .speed_switcher= NULL,			/* no speed switcher */
    .mode_switcher = NULL,			/* no mode switcher */
    .rate_switcher = NULL,			/* no sample-rate switcher */
    .cycle_chars   = -1,			/* no rate switch */
    .wrapup         = NULL,			/* no wrapup code */
    .cycle          = 1,			/* updates every second */
};
/*@ -redef @*/
#endif /* EARTHMATE_ENABLE */


#ifdef ITRAX_ENABLE
/**************************************************************************
 *
 * The NMEA mode of the iTrax chipset, as used in the FastTrax and others.
 *
 * As described by v1.31 of the NMEA Protocol Specification for the
 * iTrax02 Evaluation Kit, 2003-06-12.
 * v1.18 of the  manual, 2002-19-6, describes effectively
 * the same protocol, but without ZDA.
 *
 **************************************************************************/

/*
 * Enable GGA=0x2000, RMC=0x8000, GSA=0x0002, GSV=0x0001, ZDA=0x0004.
 * Disable GLL=0x1000, VTG=0x4000, FOM=0x0020, PPS=0x0010.
 * This is 82+75+67+(3*60)+34 = 438 characters 
 * 
 * 1200   => at most 1 fix per 4 seconds
 * 2400   => at most 1 fix per 2 seconds
 * 4800   => at most 1 fix per 1 seconds
 * 9600   => at most 2 fixes per second
 * 19200  => at most 4 fixes per second
 * 57600  => at most 13 fixes per second
 * 115200 => at most 26 fixes per second
 *
 * We'd use FOM, but they don't specify a confidence interval.
 */
#define ITRAX_MODESTRING	"$PFST,NMEA,A007,%d\r\n"

static int literal_send(int fd, const char *fmt, ... )
/* ship a raw command to the GPS */
{
    int status;
    char buf[BUFSIZ];
    va_list ap;

    va_start(ap, fmt) ;
    (void)vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    status = (int)write(fd, buf, strlen(buf));
    if (status == (int)strlen(buf)) {
	gpsd_report(LOG_IO, "=> GPS: %s\n", buf);
	return status;
    } else {
	gpsd_report(LOG_WARN, "=> GPS: %s FAILED\n", buf);
	return -1;
    }
}

static void itrax_probe_subtype(struct gps_device_t *session, unsigned int seq)
/* start it reporting */
{
    if (seq == 0) {
	/* initialize GPS clock with current system time */ 
	struct tm when;
	double integral, fractional;
	time_t intfixtime;
	char buf[31], frac[6];
	fractional = modf(timestamp(), &integral);
	intfixtime = (time_t)integral;
	(void)gmtime_r(&intfixtime, &when);
	/* FIXME: so what if my local clock is wrong? */
	(void)strftime(buf, sizeof(buf), "$PFST,INITAID,%H%M%S.XX,%d%m%y\r\n", &when);
	(void)snprintf(frac, sizeof(frac), "%.2f", fractional);
	buf[21] = frac[2]; buf[22] = frac[3];
	(void)literal_send(session->gpsdata.gps_fd, buf);
	/* maybe this should be considered a reconfiguration? */
	(void)literal_send(session->gpsdata.gps_fd, "$PFST,START\r\n");
    }
}

static void itrax_configurator(struct gps_device_t *session)
/* set synchronous mode */
{
#ifdef ALLOW_RECONFIGURE
    (void)literal_send(session->gpsdata.gps_fd, "$PFST,SYNCMODE,1\r\n");
    (void)literal_send(session->gpsdata.gps_fd, 
		    ITRAX_MODESTRING, session->gpsdata.baudrate);
#endif /* ALLOW_RECONFIGURE */
}

static bool itrax_speed(struct gps_device_t *session, speed_t speed)
/* change the baud rate */
{
#ifdef ALLOW_RECONFIGURE
    return literal_send(session->gpsdata.gps_fd, ITRAX_MODESTRING, speed) >= 0;
#else
    return false;
#endif /* ALLOW_RECONFIGURE */
}

static bool itrax_rate(struct gps_device_t *session, double rate)
/* change the sample rate of the GPS */
{
#ifdef ALLOW_RECONFIGURE
    return literal_send(session->gpsdata.gps_fd, "$PSFT,FIXRATE,%d\r\n", rate) >= 0;
#else
    return false;
#endif /* ALLOW_RECONFIGURE */
}

static void itrax_wrap(struct gps_device_t *session)
/* stop navigation, this cuts the power drain */
{
#ifdef ALLOW_RECONFIGURE
    (void)literal_send(session->gpsdata.gps_fd, "$PFST,SYNCMODE,0\r\n");
#endif /* ALLOW_RECONFIGURE */
    (void)literal_send(session->gpsdata.gps_fd, "$PFST,STOP\r\n");
}

/*@ -redef @*/
static struct gps_type_t itrax = {
    .typename      = "iTrax",		/* full name of type */
    .trigger       = "$PFST,OK",	/* tells us to switch to Itrax */
    .channels      = 12,		/* consumer-grade GPS */
    .probe_wakeup  = NULL,		/* no wakeup to be done before hunt */
    .probe_detect  = NULL,		/* no probe */
    .probe_subtype = itrax_probe_subtype,	/* initialize */
    .configurator  = itrax_configurator,/* set synchronous mode */
    .get_packet    = packet_get,	/* how to get a packet */
    .parse_packet  = nmea_parse_input,	/* how to interpret a packet */
    .rtcm_writer   = NULL,		/* iTrax doesn't support DGPS/WAAS/EGNOS */
    .speed_switcher= itrax_speed,	/* no speed switcher */
    .mode_switcher = NULL,		/* no mode switcher */
    .rate_switcher = itrax_rate,	/* there's a sample-rate switcher */
    .cycle_chars   = 438,		/* not relevant, no rate switch */
    .wrapup         = itrax_wrap,	/* sleep the receiver */
    .cycle          = 1,		/* updates every second */
};
/*@ -redef @*/
#endif /* ITRAX_ENABLE */
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

enum {
#include "packet_states.h"
};

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
	gpsd_report(LOG_IO, "=> GPS: %s\n", buf);
	return status;
    } else {
	gpsd_report(LOG_WARN, "=> GPS: %s FAILED\n", buf);
	return -1;
    }
}

#define TNT_SNIFF_RETRIES       100
/*
 * The True North compass won't start talking
 * unless you ask it to. So to identify it we
 * need to query for it's ID string.
 */
static int tnt_packet_sniff(struct gps_device_t *session)
{
    unsigned int n, count = 0;

    gpsd_report(LOG_RAW, "tnt_packet_sniff begins\n");
    for (n = 0; n < TNT_SNIFF_RETRIES; n++) 
    {
      count = 0;
      (void)tnt_send(session->gpsdata.gps_fd, "@X?");
      if (ioctl(session->gpsdata.gps_fd, FIONREAD, &count) < 0)
          return BAD_PACKET;
      if (count == 0) {
          //int delay = 10000000000.0 / session->gpsdata.baudrate;
          //gpsd_report(LOG_RAW, "usleep(%d)\n", delay);
          //usleep(delay);
          gpsd_report(LOG_RAW, "sleep(1)\n");
          (void)sleep(1);
      } else if (packet_get(session) >= 0) {
        if((session->packet_type == NMEA_PACKET)&&(session->packet_state == NMEA_RECOGNIZED))
        {
          gpsd_report(LOG_RAW, "tnt_packet_sniff returns %d\n",session->packet_type);
          return session->packet_type;
        }
      }
    }

    gpsd_report(LOG_RAW, "tnt_packet_sniff found no packet\n");
    return BAD_PACKET;
}

static void tnt_probe_subtype(struct gps_device_t *session, unsigned int seq UNUSED)
{
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
          if (tnt_packet_sniff(session) != BAD_PACKET)
              return true;
      }
  return false;
}

struct gps_type_t trueNorth = {
    .typename       = "True North",	/* full name of type */
    .trigger        = " TNT1500",
    .channels       = 0,		/* not an actual GPS at all */
    .probe_wakeup   = NULL,		/* this will become a real method */
    .probe_detect   = tnt_probe,	/* probe by sending ID query */
    .probe_subtype  = tnt_probe_subtype,/* probe for True North Digital Compass */
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
#ifdef RTCM104_ENABLE
/**************************************************************************
 *
 * RTCM-104, used for broadcasting DGPS corrections and by DGPS radios
 *
 **************************************************************************/

static gps_mask_t rtcm104_analyze(struct gps_device_t *session)
{
    gpsd_report(LOG_RAW, "RTCM packet type 0x%02x length %d words: %s\n", 
		session->gpsdata.rtcm.type,
		session->gpsdata.rtcm.length+2,
		gpsd_hexdump(session->driver.isgps.buf, (session->gpsdata.rtcm.length+2)*sizeof(isgps30bits_t)));
    return RTCM_SET;
}

static struct gps_type_t rtcm104 = {
    .typename      = "RTCM104",		/* full name of type */
    .trigger       = NULL,		/* no recognition string */
    .channels      = 0,			/* not used */
    .probe_wakeup  = NULL,		/* no wakeup to be done before hunt */
    .probe_detect  = NULL,		/* no probe */
    .probe_subtype = NULL,		/* no subtypes */
    .configurator  = NULL,		/* no configurator */
    .get_packet    = packet_get,	/* how to get a packet */
    .parse_packet  = rtcm104_analyze,	/* packet getter does the parsing */
    .rtcm_writer   = NULL,		/* don't send RTCM data,  */
    .speed_switcher= NULL,		/* no speed switcher */
    .mode_switcher = NULL,		/* no mode switcher */
    .rate_switcher = NULL,		/* no sample-rate switcher */
    .cycle_chars   = -1,		/* not relevant, no rate switch */
    .wrapup         = NULL,		/* no wrapup code */
    .cycle          = 1,		/* updates every second */
};
#endif /* RTCM104_ENABLE */

extern struct gps_type_t garmin_usb_binary, garmin_ser_binary;
extern struct gps_type_t sirf_binary, tsip_binary;
extern struct gps_type_t evermore_binary, italk_binary;

/*@ -nullassign @*/
/* the point of this rigamarole is to not have to export a table size */
static struct gps_type_t *gpsd_driver_array[] = {
#ifdef NMEA_ENABLE
    &nmea, 
    &sirf_nmea,
#if FV18_ENABLE
    &fv18,
    &garmin,
#endif /* FV18_ENABLE */
#if TRIPMATE_ENABLE
    &tripmate,
#endif /* TRIPMATE_ENABLE */
#if EARTHMATE_ENABLE
    &earthmate, 
#endif /* EARTHMATE_ENABLE */
#if ITRAX_ENABLE
    &itrax, 
#endif /* ITRAX_ENABLE */
#endif /* NMEA_ENABLE */
#ifdef ZODIAC_ENABLE
    &zodiac_binary,
#endif /* ZODIAC_ENABLE */
#if GARMIN_ENABLE
    &garmin_usb_binary,
    &garmin_ser_binary,
#endif /* GARMIN_ENABLE */
#ifdef SIRF_ENABLE
    &sirf_binary, 
#endif /* SIRF_ENABLE */
#ifdef EVERMORE_ENABLE
    &evermore_nmea,
#endif /* EVERMORE_ENABLE */
#ifdef TSIP_ENABLE
    &tsip_binary, 
#endif /* TSIP_ENABLE */
#ifdef TNT_ENABLE
    &trueNorth,
#endif /* TSIP_ENABLE */
#ifdef EVERMORE_ENABLE
    &evermore_binary, 
#endif /* EVERMORE_ENABLE */
#ifdef ITALK_ENABLE
    &italk_binary, 
#endif /* ITALK_ENABLE */
#ifdef RTCM104_ENABLE
    &rtcm104, 
#endif /* RTCM104_ENABLE */
    NULL,
};
/*@ +nullassign @*/
struct gps_type_t **gpsd_drivers = &gpsd_driver_array[0];
