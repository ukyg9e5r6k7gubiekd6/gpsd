/* $Id$
 *
 * A prototype driver.  Doesn't run, doesn't even compile.
 */

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <unistd.h>
#include <time.h>
#include <stdio.h>

#include "gpsd_config.h"
#include "gpsd.h"
#if defined(PROTO_ENABLE) && defined(BINARY_ENABLE)

#include "bits.h"

/*@ +charint -usedef -compdef @*/
static bool proto_write(int fd, unsigned char *msg, size_t msglen) 
{
   bool      ok;

   /* CONSTRUCT THE MESSAGE */

   /* we may need to dump the message */
   gpsd_report(LOG_IO, "writing proto control type %02x:%s\n", 
	       msg[0], gpsd_hexdump(msg, msglen));
   ok = (write(fd, msg, msglen) == (ssize_t)msglen);
   (void)tcdrain(fd);
   return(ok);
}
/*@ -charint +usedef +compdef @*/

/*@ +charint @*/
gps_mask_t proto_parse(struct gps_device_t *session, unsigned char *buf, size_t len)
{
    size_t i;
    int used, visible;
    double version;

    if (len == 0)
	return 0;

    /* we may need to dump the raw packet */
    gpsd_report(LOG_RAW, "raw proto packet type 0x%02x length %d: %s\n", buf[0], len, buf2);

    (void)snprintf(session->gpsdata.tag, sizeof(session->gpsdata.tag),
		   "PROTO%d",(int)buf[0]);

    switch (getub(buf, 0))
    {
	/* DISPATCH ON FIRST BYTE OF PAYLOAD */

    default:
	gpsd_report(LOG_WARN, "unknown packet id %d length %d: %s\n", buf[0], len, gpsd_hexdump(buf, len));
	return 0;
    }
}
/*@ -charint @*/

static gps_mask_t parse_input(struct gps_device_t *session)
{
    gps_mask_t st;

    if (session->packet_type == PROTO_PACKET){
	st = proto_parse(session, session->outbuffer, session->outbuflen);
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

static bool set_speed(struct gps_device_t *session, speed_t speed)
{
    /* set operating mode here */
}

static void set_mode(struct gps_device_t *session, int mode)
{
    if (mode == 0) {
	(void)gpsd_switch_driver(session, "Generic NMEA");
	(void)proto_set_mode(session, session->gpsdata.baudrate, false);
	session->gpsdata.driver_mode = 0;
    }
}

static void probe_subtype(struct gps_device_t *session, unsigned int seq)
{
    /*
     * Probe for subtypes here. If possible, get the software version and
     * store it in session->subtype.  The seq values don't actually mean 
     * anything, but conditionalizing probes on them gives the device 
     * time to respond to each one.
     */
}

static bool probe_detect(struct gps_device_t *session)
{
   /* Your testing code here */
   if (test==satisfied)
      return true;
   return false;
}

static void proto_wrapup(struct gps_device_t *session)
{
   /*
    * Do release actions that are independent of whether the .configurator 
    * method ran or not.
    */
}

static void configurator(struct gps_device_t *session)
{
    /* Change sentence mix and set reporting modes as needed */
}

/* This is everything we export */
struct gps_type_t proto_binary = {
    /* Full name of type */
    .typename         = "Prototype driver",
    /* Response string that identifies device */
    .trigger          = NULL,
    /* Number of satellite channels supported by the device */
    .channels         = 12,
    /* Startup-time device detector */
    .probe_detect     = probe_detect,
    /* Wakeup to be done before each baud hunt */
    .probe_wakeup     = NULL,
    /* Initialize the device and get subtype */
    .probe_subtype    = probe_subtype,
#ifdef ALLOW_RECONFIGURE
    /* Enable what reports we need */
    .configurator     = configurator,
#endif /* ALLOW_RECONFIGURE */
    /* Packet getter (using default routine) */
    .get_packet       = packet_get,
    /* Parse message packets */
    .parse_packet     = parse_input,
    /* RTCM handler (using default routine) */
    .rtcm_writer      = pass_rtcm,
    /* Speed (baudrate) switch */
    .speed_switcher   = set_speed,
    /* Switch to NMEA mode */
    .mode_switcher    = set_mode,
    /* Message delivery rate switcher */
    .rate_switcher    = NULL,
    /* Number of chars per report cycle */
    .cycle_chars      = -1,
#ifdef ALLOW_RECONFIGURE
    /* Undo the actions of .configurator */
    .revert         = NULL,
#endif /* ALLOW_RECONFIGURE */
    /* Puts device back to original settings */
    .wrapup           = proto_wrapup,
    /* Number of updates per second */
    .cycle            = 1
};
#endif /* defined(PROTO_ENABLE) && defined(BINARY_ENABLE) */
