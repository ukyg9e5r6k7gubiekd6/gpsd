/* $Id$ */
/*
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
	gpsd_report(LOG_WARN, "unknown Proto packet id %d length %d: %s\n", buf[0], len, gpsd_hexdump(buf, len));
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

static void probe_subtype(struct gps_device_t *session)
{
    /* probe for subtypes here */
}

static void configurator(struct gps_device_t *session)
{
    if (session->packet_type == NMEA_PACKET)
	(void)proto_set_mode(session, session->gpsdata.baudrate, true);
}

/* this is everything we export */
struct gps_type_t proto_binary =
{
    .typename       = "Prototype driver",	/* full name of type */
    .trigger        = NULL,		/* recognize the type */
    .channels       = 12,		/* used for dumping binary packets */
    .probe_detect   = NULL,		/* no probe */
    .probe_wakeup   = NULL,		/* no wakeup to be done before hunt */
    .probe_subtype  = probe_subtype,	/* initialize the device */
#ifdef ALLOW_RECONFIGURE
    .configurator   = configurator,	/* configure the proper sentences */
#endif /* ALLOW_RECONFIGURE */
    .get_packet     = packet_get,	/* use generic packet getter */
    .parse_packet   = parse_input,	/* parse message packets */
    .rtcm_writer    = pass_rtcm,	/* send RTCM data straight */
    .speed_switcher = set_speed,	/* we can change baud rates */
    .mode_switcher  = set_mode,		/* there is a mode switcher */
    .rate_switcher  = NULL,		/* no rate switcher */
    .cycle_chars    = -1,		/* not relevant, no rate switcher */
#ifdef ALLOW_RECONFIGURE
    .revert         = NULL,		/* no setting-reversion method */
#endif /* ALLOW_RECONFIGURE */
    .wrapup         = NULL,		/* no close hook */
    .cycle          = 1,		/* updates every second */
};
#endif /* defined(PROTO_ENABLE) && defined(BINARY_ENABLE) */
