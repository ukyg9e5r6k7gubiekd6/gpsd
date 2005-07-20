/*
 * This is the gpsd driver for EverMore GPSes operating in binary mode.
 *
 * About the only thing this gives us that NMEA won't is TDOP.
 * But we'll get atomic position reports from it, which is good.
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <stdio.h>

#include "gpsd.h"
#if defined(EVERMORE_ENABLE) && defined(BINARY_ENABLE)

#define LITTLE_ENDIAN_PROTOCOL
#define GET_ORIGIN 1
#include "bits.h"

/*@ +charint -usedef -compdef @*/
static bool evermore_write(int fd, unsigned char *msg, size_t msglen)
{
   unsigned int       crc;
   size_t    i, len;
   unsigned char stuffed[MAX_PACKET_LENGTH], *cp;
   bool      ok;

   /* prepare a DLE-stuffed copy of the message */
   cp = stuffed;
   *cp++ = 0x10;  /* message starts with DLE STX */
   *cp++ = 0x02;

   len = (size_t)(msglen + 2);   /* msglen < 254 !! */
   *cp++ = (unsigned char)len;   /* message length */
   if (len == 0x10) *cp++ = 0x10;
   
   /* payload */
   crc = 0;
   for (i = 0; i < msglen; i++) {
       *cp++ = msg[i];
       crc += msg[i];
       if (msg[i] == 0x10) *cp++ = 0x10;
   }

   crc &= 0xff;

   /* enter CRC after payload */
   *cp++ = crc;  
   if (crc == 0x10) *cp++ = 0x10;

   *cp++ = 0x10;   /* message ends with DLE ETX */
   *cp++ = 0x03;

   len = (size_t)(cp - stuffed);

   /* we may need to dump the message */
   gpsd_report(4, "writing EverMore control type 0x%02x: %s\n", msg[0], 
	       gpsd_hexdump(stuffed, len));
   ok = (write(fd, stuffed, len) == (ssize_t)len);
   (void)tcdrain(fd);
   return(ok);
}
/*@ -charint +usedef +compdef @*/

/*@ +charint @*/
gps_mask_t evermore_parse(struct gps_device_t *session, unsigned char *buf, size_t len)
{
    unsigned char buf2[MAX_PACKET_LENGTH], *cp, *tp;
    size_t i, datalen;
    unsigned int used, visible, satcnt;
    double version;

    if (len == 0)
	return 0;

    /* time to unstuff it and discard the header and footer */
    cp = buf + 2;
    tp = buf2;
    if (*cp == 0x10) cp++;
    datalen = (size_t)*cp++;
   
    gpsd_report(7, "raw EverMore packet type 0x%02x length %d: %s\n", *cp, len, gpsd_hexdump(buf, len));

    datalen -= 2;

    for (i = 0; i < (size_t)datalen; i++) {
	*tp = *cp++;
	if (*tp == 0x10) cp++;
	tp++;
    }

    /*@ -usedef -compdef @*/
    gpsd_report(5, "EverMore packet type 0x%02x length %d: %s\n", buf2[0], datalen, gpsd_hexdump(buf2, datalen));
    /*@ +usedef +compdef @*/

    (void)snprintf(session->gpsdata.tag, sizeof(session->gpsdata.tag),
		   "EID%d",(int)buf2[0]);

    switch (getub(buf2, 1))
    {
    case 0x02:	/* Navigation Data Output */
	session->gpsdata.newdata.time = session->gpsdata.sentence_time
	    = gpstime_to_unix((int)getuw(buf2, 2), getul(buf2, 4)*0.01) - session->context->leap_seconds;
	ecef_to_wgs84fix(&session->gpsdata, 
			 getsl(buf2, 8)*1.0, getsl(buf2, 12)*1.0, getsl(buf2, 16)*1.0,
			 getsw(buf2, 20)/10.0, getsw(buf2, 22)/10.0, getsw(buf2, 24)/10.0);
	used = getub(buf2, 26) & 0x0f;
	visible = (getub(buf2, 26) & 0xf0) >> 4;
	version = getuw(buf2, 27)/100.0;
	/* that's all the information in this packet */
	if (used < 3)
	    session->gpsdata.newdata.mode = MODE_NO_FIX;
	else if (used == 3)
	    session->gpsdata.newdata.mode = MODE_2D;
	else
	    session->gpsdata.newdata.mode = MODE_3D;
	gpsd_report(4, "NDO 0x02: version %3.2f, mode=%d, status=%d, visible=%d, used=%d\n",
		    version,
		    session->gpsdata.newdata.mode,
		    session->gpsdata.status,
		    visible,
		    used);
	return TIME_SET | LATLON_SET | TRACK_SET | SPEED_SET | MODE_SET;

    case 0x04:	/* DOP Data Output */
	session->gpsdata.newdata.time = session->gpsdata.sentence_time
	    = gpstime_to_unix((int)getuw(buf2, 2), getul(buf2, 4)*0.01) - session->context->leap_seconds;
	session->gpsdata.gdop = (double)getub(buf2, 8)*0.1;
	session->gpsdata.pdop = (double)getub(buf2, 9)*0.1;
	session->gpsdata.hdop = (double)getub(buf2, 10)*0.1;
	session->gpsdata.vdop = (double)getub(buf2, 11)*0.1;
	session->gpsdata.tdop = (double)getub(buf2, 12)*0.1;
	switch (getub(buf2, 13)) {
	case 0:	/* no position fix */
	case 1:	/* manual calls this "1D navigation" */
	    session->gpsdata.status = STATUS_NO_FIX;
	    session->gpsdata.newdata.mode = MODE_NO_FIX;
	    break;
	case 2:	/* 2D navigation */
	    session->gpsdata.status = STATUS_FIX;
	    session->gpsdata.newdata.mode = MODE_2D;
	    break;
	case 3:	/* 3D navigation */
	    session->gpsdata.status = STATUS_FIX;
	    session->gpsdata.newdata.mode = MODE_3D;
	    break;
	case 4:	/* 3D navigation with DGPS */
	    session->gpsdata.status = STATUS_DGPS_FIX;
	    session->gpsdata.newdata.mode = MODE_3D;
	    break;
	}
	/* that's all the information in this packet */
	gpsd_report(4, "DDO 0x04: mode=%d, status=%d\n", 
		    session->gpsdata.newdata.mode,
		    session->gpsdata.status);
	return TIME_SET | DOP_SET | MODE_SET | STATUS_SET;

    case 0x06:	/* Channel Status Output */
	session->gpsdata.newdata.time = session->gpsdata.sentence_time
	    = gpstime_to_unix((int)getuw(buf2, 2), getul(buf2, 4)*0.01) - session->context->leap_seconds;
	session->gpsdata.satellites = (int)getub(buf2, 8);
	session->gpsdata.satellites_used = 0;
	memset(session->gpsdata.used, 0, sizeof(session->gpsdata.used));
	if (session->gpsdata.satellites > 12) {
		gpsd_report(4, "Warning: EverMore packet has information about %d satellites!\n",
				session->gpsdata.satellites);
	}
	if (session->gpsdata.satellites > MAXCHANNELS) session->gpsdata.satellites = MAXCHANNELS;
	satcnt = 0;
	for (i = 0; i < (size_t)session->gpsdata.satellites; i++) {
	    int prn;
	    // channel = getub(buf2, 7*i+7+2)
            prn = (int)getub(buf2, 7*i+7+3);
	    if (prn == 0) continue;  /* satellite record is not valid */
	    session->gpsdata.PRN[satcnt] = prn;
	    session->gpsdata.azimuth[satcnt] = (int)getuw(buf2, 7*i+7+4);
	    session->gpsdata.elevation[satcnt] = (int)getub(buf2, 7*i+7+6);
	    session->gpsdata.ss[satcnt] = (int)getub(buf2, 7*i+7+7);
	    /*
	     * Status bits at offset 8:
	     * bit0 = 1 satellite acquired
	     * bit1 = 1 code-tracking loop locked
	     * bit2 = 1 carrier-tracking loop locked
	     * bit3 = 1 data-bit synchronization done
	     * bit4 = 1 frame synchronization done
	     * bit5 = 1 ephemeris data collected
	     * bit6 = 1 used for position fix
	     */
	    if (getub(buf2, 7*i+7+8) & 0x40) {
		session->gpsdata.used[session->gpsdata.satellites_used++]=prn;
	    }

	    satcnt++;
		
	}
	session->gpsdata.satellites = (int)satcnt;
	/* that's all the information in this packet */
	gpsd_report(4, "CSO 0x04: %d satellites used\n", 
		    session->gpsdata.satellites_used);
	return TIME_SET | SATELLITE_SET | USED_SET;

    case 0x08:	/* Measurement Data Output */
	// clock offset is a manufacturer diagnostic
	// (int)getuw(buf2, 8);  /* clock offset, 29000..29850 ?? */
	session->gpsdata.newdata.time = session->gpsdata.sentence_time
	    = gpstime_to_unix((int)getuw(buf2, 2), getul(buf2, 4)*0.01) - session->context->leap_seconds;
	visible = getub(buf2, 10);
	gpsd_report(5, "TIME: %04x %d %d\n", getuw(buf2, 8), getuw(buf2, 8), session->context->leap_seconds); /* debug */
	/* FIXME: read full statellite status for each channel */
	/* gpsd_report(4, "MDO 0x04: visible=%d\n", visible); */
	gpsd_report(4, "MDO 0x04:\n");
	return TIME_SET;

    default:
	gpsd_report(3, "ID: 0x%02x\n", getub(buf2, 0));
	gpsd_report(3, "unknown EverMore packet id 0x%02x length %d: %s\n", buf2[0], datalen, gpsd_hexdump(buf2, datalen));
	return 0;
    }
}
/*@ -charint @*/

static gps_mask_t evermore_parse_input(struct gps_device_t *session)
{
    gps_mask_t st;

    if (session->packet_type == EVERMORE_PACKET){
	st = evermore_parse(session, session->outbuffer, session->outbuflen);
	session->gpsdata.driver_mode = 1;
	return st;
#ifdef NMEA_ENABLE
    } else if (session->packet_type == NMEA_PACKET) {
	st = nmea_parse((char *)session->outbuffer, session);
	session->gpsdata.driver_mode = 0;
	return st;
#endif /* NMEA_ENABLE */
    } else
	return 0;
}

/* configure various EverMore settings to default */
/* TODO: Datumnn ID set to 1 (WGS-84), msg 0x80 */
static bool evermore_default(struct gps_device_t *session, bool mode)
{
    bool ok = true;
    /*@ +charint @*/
    unsigned char msg1[] = {0x86,	/*  0: msg ID */
	    		    5};         /*  Elevation Mask, degree 0..89 */
   
    unsigned char msg2[] = {0x87,       /*  0: msg ID */
	    		    1,          /*  1: DOP mask, GDOP(0), auto(1), PDOP(2), HDOP(3), no mask(4) */
			    20,         /*  2: GDOP, 1..99 */
			    15,         /*  3: PDOP, 1..99 */
			    8};         /*  4: HDOP, 1..99 */

   unsigned char msg3[] = {0x89,	/*  0: msg ID */
	    		   0,           /*  1: operation mode, normal(0), power save(1), 1PPS(2) */
                           1,           /*  2: navigation update rate, 1/Hz, 1..10 */
                           4};          /*  3: RF/GPSBBP On time, 160ms(0), 220(1), 280(2), 340(3), 440(4) */

    ok |= evermore_write(session->gpsdata.gps_fd, msg1, sizeof(msg1));
    ok |= evermore_write(session->gpsdata.gps_fd, msg2, sizeof(msg2));
    ok |= evermore_write(session->gpsdata.gps_fd, msg3, sizeof(msg3));
    /*@ +charint @*/
    return ok;
}

static bool evermore_set_mode(struct gps_device_t *session, 
			      speed_t speed, bool mode)
{
    unsigned char tmp8;
    /*@ +charint @*/
    unsigned char msg[] = {0x80,		/*  0: msg ID */
			   0x33, 0x05,		/*  1: GPS week; when 0 is here, we finish with year 1985 */
			   0x00, 0x00, 0x00, 0x00,	/*  3: GPS TOW */
			   0x00, 0x00,		/*  7: Latitude */
			   0x00, 0x00,		/*  9: Longitude */
			   0x00, 0x00,		/* 11: Altitude */
			   0x00, 0x00,		/* 13: Datum ID WGS84 */
			   0x01,		/* 15: hot start */
			   0x5d,		/* 16: cksum(6) + bin(7) + GGA(0) + GSA(2) + GSV(3) + RMC(4) */
			   0,			/* 17: baud rate */
			  };
    switch (speed) {
    case 4800:  tmp8 = 0; break;
    case 9600:  tmp8 = 1; break;
    case 19200: tmp8 = 2; break;
    case 38400: tmp8 = 3; break;
    default: return false;
    }
    msg[17] = tmp8;
    session->gpsdata.baudrate = (unsigned int)speed;
    if (mode) {
        gpsd_report(1, "Switching chip mode to EverMore binary.\n");
	msg[16] |= 0x80;  /* binary mode */
    }
    return evermore_write(session->gpsdata.gps_fd, msg, sizeof(msg));
    /*@ +charint @*/
}


static bool evermore_speed(struct gps_device_t *session, speed_t speed)
{
    gpsd_report(5, "evermore_speed call (%d)\n", speed);
    return evermore_set_mode(session, speed, true);
}

static void evermore_mode(struct gps_device_t *session, int mode)
{
    gpsd_report(5, "evermore_set_mode call (%d)\n", mode);
    if (mode == 0) {
	(void)gpsd_switch_driver(session, "Generic NMEA");
	(void)evermore_set_mode(session, session->gpsdata.baudrate, false);
	session->gpsdata.driver_mode = 0;
    }
}

static void evermore_initializer(struct gps_device_t *session)
/* poll for software version in order to check for old firmware */
{
    gpsd_report(5, "evermore_initializer call\n");
    if (session->packet_type == NMEA_PACKET)
	(void)evermore_set_mode(session, session->gpsdata.baudrate, true);
}

static void evermore_probe(struct gps_device_t *session)
/* send a binary message to probe for EverMore GPS */
/*
 * There is a way to probe for EverMore chipset. When binary message 0x81 is sent:
 * 10 02 04 81 13 94 10 03
 *
 * EverMore will reply with something like this:
 * *10 *02 *0D *20 E1 00 00 *00 0A 00 1E 00 32 00 5B *10 *03
 * bytes marked with * are fixed
 * Message in reply is information about logging configuration of GPS
 * */
{
   unsigned char msg[] = {0x81, 	/*  0: msg ID */
	    		  0x13};        /*  1: LogRead = 0x13 */

   bool ok;
   gpsd_report(5, "evermore_probe call\n");
   ok = evermore_write(session->gpsdata.gps_fd, msg, sizeof(msg));
   return;
}

static void evermore_close(struct gps_device_t *session)
/* set GPS to NMEA, 4800, GGA, GSA, GSV, RMC (default) */ 
{
	
	gpsd_report(5, "evermore_close call\n");
	(void)evermore_set_mode(session, 4800, false);
}

/* this is everything we export */
struct gps_type_t evermore_binary =
{
    .typename       = "EverMore binary",	/* full name of type */
    .trigger        = "$PEMT,100,05.",		/* recognize the type */
    .probe          = NULL,			/* no probe */
    .initializer    = evermore_initializer,	/* initialize the device */
    .get_packet     = packet_get,		/* use generic one */
    .parse_packet   = evermore_parse_input,	/* parse message packets */
    .rtcm_writer    = pass_rtcm,		/* send RTCM data straight */
    .speed_switcher = evermore_speed,		/* we can change baud rates */
    .mode_switcher  = evermore_mode,		/* there is a mode switcher */
    .rate_switcher  = NULL,			/* no sample-rate switcher */
    .cycle_chars    = -1,			/* ignore, no rate switch */
    .wrapup         = evermore_close,		/* return to NMEA */
    .cycle          = 1,			/* updates every second */
};
#endif /* defined(EVERMORE_ENABLE) && defined(BINARY_ENABLE) */
