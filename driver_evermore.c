/*
 * DEPRECATED September 2019
 *
 * September 2019: Looks like Everymore converted most of their products
 * over to using to SiRF and u-blox chips a long time ago.  They still offer
 * the EB-E26 and EB-E36-5Hz modules, but these appear to be NMEA only.  Their
 * web site has not documentation: http://www.evermoregps.com.tw/
 *
 * May 2013: The binary bits were commented out.  Then removed in
 * September 2019, but will live forever in the git history...
 *
 * This is the gpsd driver for EverMore GPSes.  They have both an NMEA and
 * a binary reporting mode, with the interesting property that they will
 * cheerfully accept binary commands (such as speed changes) while in NMEA
 * mode.
 *
 * This driver seems to be a subset of driver_sirf.c.  Is it needed at all?
 *
 * Binary mode would give us atomic fix reports, but it has one large drawback:
 * the Navigation Data Out message doesn't report a leap-second offset, so it
 * is not actually possible to collect a leap-second offset from it. Therefore
 * we'll normally run the driver in NMEA mode.
 *
 * About the only thing binary mode gives that NMEA won't is TDOP and raw
 * pseudoranges, but gpsd does its own DOPs from skyview. By default we'll
 * trade away raw data to get accurate time.
 *
 * The vendor site is <http://www.emt.com.tw>.
 *
 * This driver was written by Petr Slansky based on a framework by Eric S.
 * Raymond.  The following remarks are by Petr Slansky.
 *
 * Snooping on the serial the communication between a Windows program and
 * an Evermore chipset reveals some messages not described in the vendor
 * documentation (Issue C of Aug 2002):
 *
 * 10 02 06 84 00 00 00 84 10 03	switch to binary mode (84 00 00 00)
 * 10 02 06 84 01 00 00 85 10 03	switch to NMEA mode (84 01 00 00)
 *
 * 10 02 06 89 01 00 00 8a 10 03        set baud rate 4800
 * 10 02 06 89 01 01 00 8b 10 03        set baud rate 9600
 * 10 02 06 89 01 02 00 8c 10 03        set baud rate 19200
 * 10 02 06 89 01 03 00 8d 10 03        set baud rate 38400
 *
 * 10 02 06 8D 00 01 00 8E 10 03        switch to datum ID 001 (WGS-84)
 * 10 02 06 8D 00 D8 00 65 10 03        switch to datum ID 217 (WGS-72)
 *
 * These don't entail a reset of GPS as the 0x80 message does.
 *
 * 10 02 04 38 85 bd 10 03     answer from GPS to 0x85 message; ACK message
 * 10 02 04 38 8d c5 10 03     answer from GPS to 0x8d message; ACK message
 * 10 02 04 38 8e c6 10 03     answer from GPS to 0x8e message; ACK message
 * 10 02 04 38 8f c7 10 03     answer from GPS to 0x8f message; ACK message
 *
 * The chip sometimes sends vendor extension messages with the prefix
 * $PEMT,100. After restart, it sends a $PEMT,100 message describing the
 * chip's configuration. Here is a sample:
 *
 * $PEMT,100,05.42g,100303,180,05,1,20,15,08,0,0,2,1*5A
 * 100 - message type
 * 05.42g - firmware version
 * 100303 - date of firmware release DDMMYY
 * 180 -  datum ID; 001 is WGS-84
 * 05 - default elevation mask; see message 0x86
 * 1 - default DOP select, 1 is auto DOP mask; see message 0x87
 * 20 - default GDOP; see message 0x87
 * 15 - default PDOP
 * 08 - default HDOP
 * 0 - Normal mode, without 1PPS
 * 0 - default position pinning control (0 disable, 1 enable)
 * 2 - altitude hold mode (0 disable, 1 always, 2 auto)
 * 1 - 2/1 satellite nav mode (0,1,2,3,4)
 *          0 disable 2/1 sat nav mode
 *          1 hold direction (2 sat)
 *          2 clock hold only (2 sat)
 *          3 direction hold then clock hold (1 sat)
 *          4 clock hold then direction hold (1 sat)
 *
 * Message $PEMT,100 could be forced with message 0x85 (restart):
 * 10 02 12 85 00 00 00 00 00 01 01 00 00 00 00 00 00 00 00 87 10 03
 * 0x85 ID, Restart
 * 0x00 restart mode (0 default, 1 hot, 2 warm, 3 cold, 4 test)
 * 0x00 test start search PRN (1-32)
 * 0x00 UTC second (0-59)
 * 0x00 UTC Minute (0-59)
 * 0x00 UTC Hour (0-23)
 * 0x01 UTC Day (1-31)
 * 0x01 UTC Month (1-12)
 * 0x0000 UTC year (1980+x, uint16)
 * 0x0000 Latitude WGS-84 (+/-900, 1/10 degree, + for N, int16)
 * 0x0000 Longtitude WGS-84 (+/-1800, 1/10 degree, + for E, int16)
 * 0x0000 Altitude WGS-84 (-1000..+18000, meters, int16)
 * 0x87 CRC
 *
 * With message 0x8e it is possible to define how often each NMEA
 * message is sent (0-255 seconds). It is possible with message 0x8e
 * to activate PEMT,101 messages that have information about time,
 * position, velocity and HDOP.
 *
 * $PEMT,101,1,02,00.0,300906190446,5002.5062,N,01427.6166,E,00259,000,0000*27
 * $PEMT,101,2,06,02.1,300906185730,5002.7546,N,01426.9524,E,00323,020,0011*26
 * 101 - message type, Compact Navigation Solution
 * 2 - position status (1,2,3,4,5,6)
 *      (1 invalid, 2 2D fix, 3 3D fix, 4 2D with DIFF, 5 3D with DIFF,
 *       6 2/1 sat degrade mode)
 * 06 - number of used satelites
 * 02.1 - DOP (00.0 no fix, HDOP 2D fix, PDOP 3D fix)
 * 300906185730 - date and time, UTC ddmmyyHHMMSS (30/09/2006 18:57:30)
 * 5002.7546,N - Latitude (degree)
 * 01426.9524,E - Longitude (degree)
 * 00323 - Altitude (323 metres)
 * 020 - heading (20 degrees from true north)
 * 0011 - speed over ground (11 metres per second); documentation says km per h
 *
 * This is an exampe of an 0x8e message that activates all NMEA sentences
 * with 1s period:
 * 10 02 12 8E 7F 01 01 01 01 01 01 01 01 00 00 00 00 00 00 15 10 03
 *
 * There is a way to probe for this chipset. When binary message 0x81 is sent:
 * 10 02 04 81 13 94 10 03
 *
 * EverMore will reply with message like this:
 * *10 *02 *0D *20 E1 00 00 *00 0A 00 1E 00 32 00 5B *10 *03
 * bytes marked with * are fixed
 * Message in reply is information about logging configuration of GPS
 *
 * Another way to detect the EverMore chipset is to send one of the messages
 * 0x85, 0x8d, 0x8e or 0x8f and check for a reply.
 * The reply message from an EverMore GPS will look like this:
 * *10 *02 *04 *38 8d c5 *10 *03
 * 8d indicates that message 0x8d was sent;
 * c5 is EverMore checksum, other bytes are fixed
 *
 * This file is Copyright (c) 2010-2018 by the GPSD project
 * SPDX-License-Identifier: BSD-2-clause
 */

#include "gpsd_config.h"  /* must be before all includes */

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

#include "gpsd.h"
#if defined(EVERMORE_ENABLE) && defined(BINARY_ENABLE)

#include "bits.h"

#define EVERMORE_CHANNELS	12

static ssize_t evermore_control_send(struct gps_device_t *session, char *buf,
				     size_t len)
{
    unsigned int crc;
    size_t i;
    char *cp;

    /* prepare a DLE-stuffed copy of the message */
    cp = session->msgbuf;
    *cp++ = 0x10;		/* message starts with DLE STX */
    *cp++ = 0x02;

    session->msgbuflen = (size_t) (len + 2);	/* len < 254 !! */
    *cp++ = (char)session->msgbuflen;	/* message length */
    if (session->msgbuflen == 0x10)
	*cp++ = 0x10;

    /* payload */
    crc = 0;
    for (i = 0; i < len; i++) {
	*cp++ = buf[i];
	if (buf[i] == 0x10)
	    *cp++ = 0x10;
	crc += buf[i];
    }

    crc &= 0xff;

    /* enter CRC after payload */
    *cp++ = crc;
    if (crc == 0x10)
	*cp++ = 0x10;

    *cp++ = 0x10;		/* message ends with DLE ETX */
    *cp++ = 0x03;

    session->msgbuflen = (size_t) (cp - session->msgbuf);

    return gpsd_write(session, session->msgbuf, session->msgbuflen);
}


static bool evermore_protocol(struct gps_device_t *session, int protocol)
{
    char tmp8;
    char evrm_protocol_config[] = {
	(char)0x84,		/* 0: msg ID, Protocol Configuration */
	(char)0x00,		/* 1: mode; EverMore binary(0), NMEA(1) */
	(char)0x00,		/* 2: reserved */
	(char)0x00,		/* 3: reserved */
    };
    GPSD_LOG(LOG_PROG, &session->context->errout,
	     "evermore_protocol(%d)\n", protocol);
    tmp8 = (protocol != 0) ? 1 : 0;
    /* NMEA : binary */
    evrm_protocol_config[1] = tmp8;
    return (evermore_control_send
	    (session, evrm_protocol_config,
	     sizeof(evrm_protocol_config)) != -1);
}

static bool evermore_nmea_config(struct gps_device_t *session, int mode)
/* mode = 0 : EverMore default */
/* mode = 1 : gpsd best */
/* mode = 2 : EverMore search, activate PEMT101 message */
{
    unsigned char tmp8;
    unsigned char evrm_nmeaout_config[] = {
	0x8e,			/*  0: msg ID, NMEA Message Control */
	0xff,			/*  1: NMEA sentence bitmask, GGA(0), GLL(1), GSA(2), GSV(3), ... */
	0x01,			/*  2: nmea checksum no(0), yes(1) */
	1,			/*  3: GPGGA, interval 0-255s */
	0,			/*  4: GPGLL, interval 0-255s */
	1,			/*  5: GPGSA, interval 0-255s */
	1,			/*  6: GPGSV, interval 0-255s */
	1,			/*  7: GPRMC, interval 0-255s */
	0,			/*  8: GPVTG, interval 0-255s */
	0,			/*  9: PEMT,101, interval 0-255s */
	0, 0, 0, 0, 0, 0,	/* 10-15: reserved */
    };
    GPSD_LOG(LOG_PROG, &session->context->errout,
	     "evermore_nmea_config(%d)\n", mode);
    tmp8 = (mode == 1) ? 5 : 1;
    /* NMEA GPGSV, gpsd  */
    evrm_nmeaout_config[6] = tmp8;	/* GPGSV, 1s or 5s */
    tmp8 = (mode == 2) ? 1 : 0;
    /* NMEA PEMT101 */
    evrm_nmeaout_config[9] = tmp8;	/* PEMT101, 1s or 0s */
    return (evermore_control_send(session, (char *)evrm_nmeaout_config,
				  sizeof(evrm_nmeaout_config)) != -1);
}

static void evermore_mode(struct gps_device_t *session, int mode)
{
    GPSD_LOG(LOG_PROG, &session->context->errout,
	     "evermore_mode(%d)\n", mode);
    if (mode == MODE_NMEA) {
	/* NMEA */
	(void)evermore_protocol(session, 1);
	(void)evermore_nmea_config(session, 1);	/* configure NMEA messages for gpsd */
    } else {
	/* binary */
	(void)evermore_protocol(session, 0);
    }
}

static void evermore_event_hook(struct gps_device_t *session, event_t event)
{
    if (session->context->readonly)
	return;
    /*
     * FIX-ME: It might not be necessary to call this on reactivate.
     * Experiment to see if the holds its settings through a close.
     */
    if (event == event_identified || event == event_reactivate) {
	/*
	 * We used to run this driver in binary mode, but that has the
	 * problem that Evermore binary mode doesn't report a
	 * leap-second correction in the Navigation Data Out sentence.
	 * So, run it in NMEA mode to getbUTC corrected by firmware.
	 * Fortunately the Evermore firmware interprets binary
	 * commands in NMEA mode, so nothing else needs to change.
	 */
	(void)evermore_mode(session, 0);	/* switch GPS to NMEA mode */
	(void)evermore_nmea_config(session, 1);	/* configure NMEA messages for gpsd (GPGSV every 5s) */
    } else if (event == event_deactivate) {
	(void)evermore_nmea_config(session, 0);	/* configure NMEA messages to default */
    }
}

#ifdef RECONFIGURE_ENABLE
static bool evermore_speed(struct gps_device_t *session,
			   speed_t speed, char parity, int stopbits)
{
    GPSD_LOG(LOG_PROG, &session->context->errout,
	     "evermore_speed(%u%c%d)\n", (unsigned int)speed, parity,
	     stopbits);
    /* parity and stopbit switching aren't available on this chip */
    if (parity != session->gpsdata.dev.parity
	|| stopbits != (int)session->gpsdata.dev.stopbits) {
	return false;
    } else {
	unsigned char tmp8;
	unsigned char msg[] = {
	    0x89,		/*  0: msg ID, Serial Port Configuration */
	    0x01,		/*  1: bit 0 cfg for main serial, bit 1 cfg for DGPS port */
	    0x00,		/*  2: baud rate for main serial; 4800(0), 9600(1), 19200(2), 38400(3) */
	    0x00,		/*  3: baud rate for DGPS serial port; 4800(0), 9600(1), etc */
	};
	switch (speed) {
	case 4800:
	    tmp8 = 0;
	    break;
	case 9600:
	    tmp8 = 1;
	    break;
	case 19200:
	    tmp8 = 2;
	    break;
	case 38400:
	    tmp8 = 3;
	    break;
	default:
	    return false;
	}
	msg[2] = tmp8;
	return (evermore_control_send(session, (char *)msg, sizeof(msg)) !=
		-1);
    }
}

static bool evermore_rate_switcher(struct gps_device_t *session, double rate)
/* change the sample rate of the GPS */
{
    if (rate < 1 || rate > 10) {
	GPSD_LOG(LOG_ERROR, &session->context->errout,
		 "valid rate range is 1-10.\n");
	return false;
    } else {
	unsigned char evrm_rate_config[] = {
	    0x84,		/* 1: msg ID, Operating Mode Configuration */
	    0x02,		/* 2: normal mode with 1PPS */
	    0x00,		/* 3: navigation update rate */
	    0x00,		/* 4: RF/GPSBBP On Time */
	};
	evrm_rate_config[2] = (unsigned char)trunc(rate);
	return (evermore_control_send(session, (char *)evrm_rate_config,
				      sizeof(evrm_rate_config)) != -1);
    }
}
#endif /* RECONFIGURE_ENABLE */


/* this is everything we export */
/* *INDENT-OFF* */
const struct gps_type_t driver_evermore =
{
    .type_name      = "EverMore",		/* full name of type */
    .packet_type    = EVERMORE_PACKET,		/* lexer packet type */
    .flags	    = DRIVER_STICKY,		/* remember this */
    .trigger        = NULL, 			/* recognize the type */
    .channels       = EVERMORE_CHANNELS,	/* consumer-grade GPS */
    .probe_detect   = NULL,			/* no probe */
    .get_packet     = generic_get,		/* use generic one */
    .parse_packet   = generic_parse_input,	/* parse message packets */
    .rtcm_writer    = gpsd_write,		/* send RTCM data straight */
    .init_query     = NULL,			/* non-perturbing query */
    .event_hook     = evermore_event_hook,	/* lifetime event handler */
#ifdef RECONFIGURE_ENABLE
    .speed_switcher = evermore_speed,		/* we can change baud rates */
    .mode_switcher  = evermore_mode,		/* there is a mode switcher */
    .rate_switcher  = evermore_rate_switcher,	/* change sample rate */
    .min_cycle.tv_sec  = 1,		/* not relevant, no rate switch */
    .min_cycle.tv_nsec = 0,		/* not relevant, no rate switch */
#endif /* RECONFIGURE_ENABLE */
#ifdef CONTROLSEND_ENABLE
    .control_send   = evermore_control_send,	/* how to send a control string */
#endif /* CONTROLSEND_ENABLE */
    .time_offset     = NULL,		/* no method for NTP fudge factor */
};
/* *INDENT-ON* */
#endif /* defined(EVERMORE_ENABLE) && defined(BINARY_ENABLE) */
