/* 
 * Driver for AIS/AIVDM messages.
 *
 * The relevant standard is "ITU Recommendation M.1371":
 * <http://www.itu.int/rec/R-REC-M.1371-2-200603-I/en.
 * Alas, the distribution terms are evil.
 *
 * Also see "IALA Technical Clarifications on Recommendation ITU-R
 * M.1371-1", at 
 * <http://www.navcen.uscg.gov/enav/ais/ITU-R_M1371-1_IALA_Tech_Note1.3.pdf>
 *
 * The page http://www.bosunsmate.org/ais/ reveals part of what's going on.
 *
 * There's a sentence decoder we can test with at http://rl.se/aivdm
 *
 * Open-source code at http://sourceforge.net/projects/gnuais
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
#if defined(AIVDM_ENABLE)

#include "bits.h"

/**
 * Parse the data from the device
 */
/*@ +charint @*/
gps_mask_t aivdm_parse(struct gps_device_t *session)
{
    gps_mask_t mask = ONLINE_SET;

    if (session->packet.outbuflen == 0)
	return 0;

    /* we may need to dump the raw packet */
    gpsd_report(LOG_RAW, "raw AIVDM packet length %ld: %s\n", 
		session->packet.outbuflen, session->packet.outbuffer);

   /*
    * XXX The tag field is only 8 bytes; be careful you do not overflow.
    * XXX Using an abbreviation (eg. "italk" -> "itk") may be useful.
    */
    (void)snprintf(session->gpsdata.tag, sizeof(session->gpsdata.tag),
	"AIVDM");

    /* FIXME: actual driver code goes here */

    /* not posting any data yet */
    return mask;
}
/*@ -charint @*/

/* This is everything we export */
const struct gps_type_t aivdm = {
    /* Full name of type */
    .type_name        = "AIVDM",
    /* associated lexer packet type */
    .packet_type    = AIVDM_PACKET,
    /* Response string that identifies device (not active) */
    .trigger          = NULL,
    /* Number of satellite channels supported by the device */
    .channels         = 0,
    /* Startup-time device detector */
    .probe_detect     = NULL,
    /* Wakeup to be done before each baud hunt */
    .probe_wakeup     = NULL,
    /* Initialize the device and get subtype */
    .probe_subtype    = NULL,
    /* Packet getter (using default routine) */
    .get_packet       = generic_get,
    /* Parse message packets */
    .parse_packet     = aivdm_parse,
    /* RTCM handler (using default routine) */
    .rtcm_writer      = NULL,
#ifdef ALLOW_CONTROLSEND
    /* Control string sender - should provide checksum and headers/trailer */
    .control_send   = NULL,
#endif /* ALLOW_CONTROLSEND */
#ifdef ALLOW_RECONFIGURE
    /* Enable what reports we need */
    .configurator     = NULL,
    /* Speed (baudrate) switch */
    .speed_switcher   = NULL,
    /* Switch to NMEA mode */
    .mode_switcher    = NULL,
    /* Message delivery rate switcher (not active) */
    .rate_switcher    = NULL,
    /* Minimum cycle time of the device */
    .min_cycle        = 1,
    /* Undo the actions of .configurator */
    .revert           = NULL,
#endif /* ALLOW_RECONFIGURE */
    /* Puts device back to original settings */
    .wrapup           = NULL,
};
#endif /* defined(AIVDM_ENABLE) */

