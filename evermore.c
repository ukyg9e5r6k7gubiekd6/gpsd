/*
 * This is the gpsd driver for Evermore GPSes operating in binary mode.
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

#define GET_ORIGIN 1
#include "bits.h"

#define HI(n)		((n) >> 8)
#define LO(n)		((n) & 0xff)

/*@ +charint@ */
static bool evermore_write(int fd, unsigned char *msg, unsigned char msglen) {
   unsigned int       crc;
   size_t    i, len;
   unsigned char stuffed[MAX_PACKET_LENGTH], buf[MAX_PACKET_LENGTH*3+1], *cp;
   bool      ok;

   /* prepare a DLE-stuffed copy of the message */
   cp = stuffed;
   *cp++ = 0x10;  /* message starts with DLE STX */
   *cp++ = 0x02;

   len = msglen + 2;  /* msglen < 254 !! */
   *cp++ = LO(len);   /* message length */
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
   *cp++ = LO(crc);  
   if (crc == 0x10) *cp++ = 0x10;

   *cp++ = 0x10;   /* message ends with DLE ETX */
   *cp++ = 0x03;

   len = (size_t)(cp - stuffed);

   /* we may need to dump the message */
   buf[0] = '\0';
   for (i = 0; i < len; i++)
       (void)snprintf((char*)buf+strlen((char *)buf),sizeof((char*)buf)-strlen((char*)buf),
		      " %02x", (unsigned)stuffed[i]);
   gpsd_report(4, "writing Evermore control type %02x:%s\n", msg[0], buf);
   ok = (write(fd, stuffed, len) == (ssize_t)len);
   (void)tcdrain(fd);
   return(ok);
}
/*@ -charint @*/

/*@ +charint @*/
gps_mask_t evermore_parse(struct gps_device_t *session, unsigned char *buf, size_t len)
{
    //gps_mask_t mask;
    unsigned char buf2[MAX_PACKET_LENGTH*3+2], *cp, *tp;
    size_t i;
    int used, channels, visible;

    if (len == 0)
	return 0;

    /* we may need to dump the raw packet */
    buf2[0] = '\0';
    for (i = 0; i < len; i++)
	(void)snprintf((char*)buf2+strlen((char*)buf2), 
		       sizeof(buf2)-strlen((char*)buf2),
		       "%02x", (unsigned int)buf[i]);
    strcat((char*)buf2, "\n");
    gpsd_report(5, "raw Evermore packet type 0x%02x length %d: %s\n", buf[0], len, buf2);

    /* time to unstuff it and discard the header and footer */
    cp = buf + 2;
    tp = buf2;
    if (*cp == 0x10)
	cp++;
    len = (size_t)*cp;
    ++cp;
    for (i = 0; i < len; i++) {
	*tp = cp[i];
	if (*tp == 0x10)
	    ++i;
    }

    (void)snprintf(session->gpsdata.tag, sizeof(session->gpsdata.tag),
		   "EID%d",(int)buf2[0]);

    switch (buf2[0])
    {
    case 0x02:	/* Navigation Data Output */
	session->gpsdata.newdata.time = session->gpsdata.sentence_time
	    = gpstime_to_unix(getsw(buf2, 2), getul(buf2, 4)*1e-2) - session->context->leap_seconds;
	ecef_to_wgs84fix(&session->gpsdata, 
			 getsl(buf2, 8)*1.0, getsl(buf2, 12)*1.0, getsl(buf2, 16)*1.0,
			 getsw(buf2, 20)/10.0, getsw(buf2, 22)/10.0, getsw(buf2, 24)/10.0);
	used = getub(buf2, 26) & 0x03;
	if (used < 3)
	    session->gpsdata.newdata.mode = MODE_NO_FIX;
	else if (used == 3)
	    session->gpsdata.newdata.mode = MODE_2D;
	else
	    session->gpsdata.newdata.mode = MODE_3D;
	//visible = getub(buf2, 26) & 0xb0;
	gpsd_report(4, "NDO 0x02:  mode=%d, status=%d\n",
		    session->gpsdata.newdata.mode,
		    session->gpsdata.status);
	return TIME_SET | LATLON_SET | TRACK_SET | SPEED_SET | MODE_SET;

    case 0x04:	/* DOP Data Output */
	session->gpsdata.newdata.time = session->gpsdata.sentence_time
	    = gpstime_to_unix(getsw(buf2, 2), getul(buf2, 4)*1e-2) - session->context->leap_seconds;
	session->gpsdata.gdop = (double)getub(buf2, 8)/0.1;
	session->gpsdata.pdop = (double)getub(buf2, 9)/0.1;
	session->gpsdata.hdop = (double)getub(buf2, 10)/0.1;
	session->gpsdata.vdop = (double)getub(buf2, 11)/0.1;
	session->gpsdata.tdop = (double)getub(buf2, 12)/0.1;
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
	gpsd_report(4, "DDO 0x04: mode=%d, status=%d\n", 
		    session->gpsdata.newdata.mode,
		    session->gpsdata.status);
	return TIME_SET | DOP_SET | MODE_SET | STATUS_SET;

    case 0x06:	/* Channel Status Output */
	session->gpsdata.newdata.time = session->gpsdata.sentence_time
	    = gpstime_to_unix(getsw(buf2, 2), getul(buf2, 4)*1e-2) - session->context->leap_seconds;
	channels = (int)getub(buf2, 8);
	/* FIXME: read full status for each channel */
	gpsd_report(4, "CSO 0x04: %d channels\n", channels);
	return TIME_SET;

    case 0x08:	/* Measurement Data Output */
	session->context->leap_seconds = (int)getuw(buf2, 8);
	session->context->valid |= LEAP_SECOND_VALID;
	session->gpsdata.newdata.time = session->gpsdata.sentence_time
	    = gpstime_to_unix(getsw(buf2, 2), getul(buf2, 4)*1e-2) - session->context->leap_seconds;
	visible = getub(buf2, 10);
	/* FIXME: read full statellite status for each channel */
	gpsd_report(4, "MDO 0x04: visible=%d\n", visible);
	return TIME_SET | SATELLITE_SET;

    default:
	buf[0] = '\0';
	for (i = 0; i < len; i++)
	    (void)snprintf((char*)buf+strlen((char*)buf), 
			   sizeof(buf)-strlen((char*)buf),
			   "%02x", (unsigned int)buf2[i]);
	gpsd_report(3, "unknown Evermore packet id %d length %d: %s\n", buf2[0], len, buf);
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
    } else if (session->packet_type == NMEA_PACKET) {
	st = nmea_parse((char *)session->outbuffer, &session->gpsdata);
	session->gpsdata.driver_mode = 0;
	return st;
    } else
	return 0;
}

static bool evermore_set_mode(struct gps_device_t *session, 
			      speed_t speed, bool mode)
{
	unsigned char tmp8;
    /*@ +charint @*/
    unsigned char msg[] = {0x80,
			   0x00, 0x00,		/* GPS week */
			   0x00, 0x00, 0x00, 0x00,	/* GPS TOW */
			   0x00, 0x00,		/* Latitude */
			   0x00, 0x00,		/* Longitude */
			   0x00, 0x00,		/* Altitude */
			   0x00, 0x00,		/* Datum ID WGS84 */
			   0x01,			/* hot start */
			   0x40,			/* checksum + binary */
			   0,			/* baud rate */
			  };
    switch (speed) {
    case 4800:  tmp8 = 0; break;
    case 9600:  tmp8 = 1; break;
    case 19200: tmp8 = 2; break;
    case 38400: tmp8 = 3; break;
    default: return false;
    }
    msg[17]=tmp8;
    if (mode) {
        gpsd_report(1, "Switching chip mode to Evermore binary.\n");
	msg[16] |= 0x40;
    }
    return evermore_write(session->gpsdata.gps_fd, msg, sizeof(msg));
    /*@ +charint @*/
}


static bool evermore_speed(struct gps_device_t *session, speed_t speed)
{
    return evermore_set_mode(session, speed, true);
}

static void evermore_mode(struct gps_device_t *session, int mode)
{
    if (mode == 0) {
	(void)gpsd_switch_driver(session, "Generic NMEA");
	(void)evermore_set_mode(session, session->gpsdata.baudrate, false);
	session->gpsdata.driver_mode = 0;
    }
}

static void evermore_initializer(struct gps_device_t *session)
/* poll for software version in order to check for old firmware */
{
    if (session->packet_type == NMEA_PACKET)
	(void)evermore_set_mode(session, session->gpsdata.baudrate, true);
}

/* this is everything we export */
struct gps_type_t evermore_binary =
{
    "Evermore binary",		/* full name of type */
    "$PEMT,100,05.",		/* recognize the type */
    NULL,			/* no probe */
    evermore_initializer,	/* initialize the device */
    packet_get,			/* how to grab a packet */
    evermore_parse_input,	/* read and parse message packets */
    NULL,			/* send DGPS correction */
    evermore_speed,		/* we can change baud rates */
    evermore_mode,		/* there is a mode switcher */
    NULL,			/* caller needs to supply a close hook */
    1,				/* updates every second */
};
#endif /* defined(EVERMORE_ENABLE) && defined(BINARY_ENABLE) */
