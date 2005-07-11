/*
 * Driver for the iTalk binary protocol used by FasTrax
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
#if defined(ITALK_ENABLE) && defined(BINARY_ENABLE)

#include "bits.h"

/*@ +charint -usedef -compdef @*/
static bool italk_write(int fd, unsigned char *msg, size_t msglen) {
   size_t    i, len;
   unsigned char buf[MAX_PACKET_LENGTH*3+1];
   bool      ok;

   /* CONSTRUCT THE MESSAGE */

   /* we may need to dump the message */
   buf[0] = '\0';
   for (i = 0; i < msglen; i++)
       (void)snprintf((char*)buf+strlen((char *)buf),sizeof((char*)buf)-strlen((char*)buf),
		      " %02x", msg[i]);
   len = (size_t)strlen(buf);
   gpsd_report(4, "writing italk control type %02x:%s\n", msg[0], buf);
   ok = (write(fd, buf, len) == (ssize_t)len);
   (void)tcdrain(fd);
   return(ok);
}
/*@ -charint +usedef +compdef @*/

/*@ +charint @*/
static gps_mask_t italk_parse(struct gps_device_t *session, unsigned char *buf, size_t len)
{
    unsigned char buf2[MAX_PACKET_LENGTH*3+2];
    size_t i;

    if (len == 0)
	return 0;

    /* we may need to dump the raw packet */
    buf2[0] = '\0';
    for (i = 0; i < len; i++)
	(void)snprintf((char*)buf2+strlen((char*)buf2), 
		       sizeof(buf2)-strlen((char*)buf2),
		       "%02x", (unsigned int)buf[i]);
    strcat((char*)buf2, "\n");
    gpsd_report(5, "raw italk packet type 0x%02x length %d: %s\n", buf[0], len, buf2);

    (void)snprintf(session->gpsdata.tag, sizeof(session->gpsdata.tag),
		   "ITALK%d",(int)buf2[0]);

    switch (getub(buf2, 0))
    {
	/* DISPATCH ON FIRST BYTE OF PAYLOAD */

    default:
	buf[0] = '\0';
	for (i = 0; i < len; i++)
	    (void)snprintf((char*)buf+strlen((char*)buf), 
			   sizeof(buf)-strlen((char*)buf),
			   "%02x", (unsigned int)buf2[i]);
	gpsd_report(3, "unknown Italk packet id %d length %d: %s\n", buf2[0], len, buf);
	return 0;
    }
}
/*@ -charint @*/

static gps_mask_t italk_parse_input(struct gps_device_t *session)
{
    gps_mask_t st;

    if (session->packet_type == ITALK_PACKET){
	st = italk_parse(session, session->outbuffer, session->outbuflen);
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

static bool italk_set_mode(struct gps_device_t *session, 
			      speed_t speed, bool mode)
{
    /*@ +charint @*/
    unsigned char msg[] = {/* FILL ME*/};

    /* HACK THE MESSAGE */

    return italk_write(session->gpsdata.gps_fd, msg, sizeof(msg));
    /*@ +charint @*/
}

static bool italk_speed(struct gps_device_t *session, speed_t speed)
{
    return italk_set_mode(session, speed, true);
}

static void italk_mode(struct gps_device_t *session, int mode)
{
    if (mode == 0) {
	(void)gpsd_switch_driver(session, "Generic NMEA");
	(void)italk_set_mode(session, session->gpsdata.baudrate, false);
	session->gpsdata.driver_mode = 0;
    }
}

static void italk_initializer(struct gps_device_t *session)
/* poll for software version in order to check for old firmware */
{
    if (session->packet_type == NMEA_PACKET)
	(void)italk_set_mode(session, session->gpsdata.baudrate, true);
}

/* this is everything we export */
struct gps_type_t italk_binary =
{
    "iTalk bi=nary",	/* full name of type */
    NULL,		/* recognize the type */
    NULL,		/* no probe */
    italk_initializer,	/* initialize the device */
    packet_get,		/* how to grab a packet */
    italk_parse_input,	/* read and parse message packets */
    pass_rtcm,	/* send RTCM data straight */
    italk_speed,	/* we can change baud rates */
    italk_mode,		/* there is a mode switcher */
    NULL,		/* caller needs to supply a close hook */
    1,			/* updates every second */
};
#endif /* defined(ITALK_ENABLE) && defined(BINARY_ENABLE) */
