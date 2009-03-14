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
bool aivdm_decode(struct gps_device_t *session, struct ais_t *ais)
{
    char *sixbits[64] = {
	"000000", "000001", "000010", "000011", "000100",
	"000101", "000110", "000111", "001000", "001001",
	"001010", "001011", "001100", "001101",	"001110",
	"001111", "010000", "010001", "010010", "010011",
	"010100", "010101", "010110", "010111",	"011000",
	"011001", "011010", "011011", "011100",	"011101",
	"011110", "011111", "100000", "100001",	"100010",
	"100011", "100100", "100101", "100110",	"100111",
	"101000", "101001", "101010", "101011",	"101100",
	"101101", "101110", "101111", "110000",	"110001",
	"110010", "110011", "110100", "110101",	"110110",
	"110111", "111000", "111001", "111010",	"111011",
	"111100", "111101", "111110", "111111",    
    };

    int nfields = 0;    
    unsigned char *data, *cp = session->driver.aivdm.fieldcopy;    
    unsigned char ch;
    int i;

    if (session->packet.outbuflen == 0)
	return 0;

    /* we may need to dump the raw packet */
    gpsd_report(LOG_PROG, "AIVDM packet length %ld: %s", 
		session->packet.outbuflen, session->packet.outbuffer);

    /* extract packet fields */
    (void)strlcpy((char *)session->driver.aivdm.fieldcopy, 
		  (char*)session->packet.outbuffer,
		  session->packet.outbuflen);
    session->driver.aivdm.field[nfields++] = session->packet.outbuffer;
    for (cp = session->driver.aivdm.fieldcopy; 
	 cp < session->driver.aivdm.fieldcopy + session->packet.outbuflen;
	 cp++)
	if (*cp == ',') {
	    *cp = '\0';
	    session->driver.aivdm.field[nfields++] = cp + 1;
	}
    session->driver.aivdm.part = atoi((char *)session->driver.aivdm.field[1]);
    session->driver.aivdm.await = atoi((char *)session->driver.aivdm.field[2]);
    data = session->driver.aivdm.field[5];
    gpsd_report(LOG_PROG, "part=%d, awaiting=%d, data=%s\n",
		session->driver.aivdm.part, session->driver.aivdm.await,
		data);

    /* assemble the binary data */
    if (session->driver.aivdm.part == 1) {
	(void)memset(session->driver.aivdm.bits, '\0', sizeof(session->driver.aivdm.bits));
	session->driver.aivdm.bitlen = 0;
    }

    /* wacky 6-bit encoding, shades of FIELDATA */
    /*@ +charint @*/
    for (cp = data; cp < data + strlen((char *)data); cp++) {
	ch = *cp;
	if (ch < 87)
	    ch = ch - 48;
	else
	    ch = ch - 56;
	gpsd_report(LOG_RAW, "%c: %s\n", *cp, sixbits[ch]);
	for (i = 5; i >= 0; i--) {
	    if ((ch >> i) & 0x01) {
		session->driver.aivdm.bits[session->driver.aivdm.bitlen / 8] |= (1 << (7 - session->driver.aivdm.bitlen % 8));
	    }
	    session->driver.aivdm.bitlen++;
	}
    }
    /*@ -charint @*/

    /* time to pass buffered-up data to where it's actually processed? */
    if (session->driver.aivdm.part == session->driver.aivdm.await) {
	size_t clen = (session->driver.aivdm.bitlen + 7)/8;
	gpsd_report(LOG_INF, "AIVDM payload is %zd bits, %zd chars: %s\n",
		    session->driver.aivdm.bitlen, clen,
		    gpsd_hexdump_wrapper(session->driver.aivdm.bits,
					 clen, LOG_INF));


#define UBITS(s, l)	ubits((char *)session->driver.aivdm.bits, s, l)
#define SBITS(s, l)	sbits((char *)session->driver.aivdm.bits, s, l)
	ais->id = UBITS(0, 6);
	ais->ri = UBITS(7, 2);
	ais->mmsi = UBITS(8, 30);
	gpsd_report(LOG_INF, "AIVDM message type %d, MMSI %09d:\n", 
		    ais->id, ais->mmsi);
	switch (ais->id) {
	case 1:	/* Position Report */
	case 2:
	case 3:
	    ais->type123.status = UBITS(38, 4);
	    ais->type123.rot = SBITS(42, 8);
	    ais->type123.sog = UBITS(50, 10);
	    ais->type123.accuracy = (bool)UBITS(60, 1);
	    ais->type123.longitude = SBITS(61, 28);
	    ais->type123.latitude = SBITS(89, 27);
	    ais->type123.cog = UBITS(116, 12);
	    ais->type123.heading = UBITS(128, 9);
	    ais->type123.utc_second = UBITS(137, 6);
	    ais->type123.regional = UBITS(143, 3);
	    ais->type123.spare = UBITS(146, 2);
	    ais->type123.radio = UBITS(148, 20);
	    gpsd_report(LOG_INF,
			"Nav=%d ROT=%d SOG=%d Q=%d Lon=%d Lat=%d COG=%d TH=%d Sec=%d\n",
			ais->type123.status,
			ais->type123.rot,
			ais->type123.sog, 
			(uint)ais->type123.accuracy,
			ais->type123.longitude, 
			ais->type123.latitude, 
			ais->type123.cog, 
			ais->type123.heading, 
			ais->type123.utc_second);
	    break;
	case 4:	/* Base Station Report */
	    ais->type4.year = UBITS(38, 41);
	    ais->type4.month = UBITS(52, 4);
	    ais->type4.day = UBITS(56, 5);
	    ais->type4.hour = UBITS(61, 5);
	    ais->type4.minute = UBITS(66, 6);
	    ais->type4.second = UBITS(72, 6);
	    ais->type4.accuracy = (bool)UBITS(78, 1);
	    ais->type4.longitude = SBITS(79, 28);
	    ais->type4.latitude = SBITS(107, 27);
	    ais->type4.epfd = UBITS(134, 4);
	    ais->type4.spare = UBITS(138, 10);
	    ais->type4.radio = UBITS(148, 19);
	    gpsd_report(LOG_INF,
			"Date: %4d:%02d:%02dT%02d:%02d:%02d Q=%d Lat=%d  Lon=%d epfd=%d\n",
			ais->type4.year,
			ais->type4.month,
			ais->type4.day,
			ais->type4.hour,
			ais->type4.minute,
			ais->type4.second,
			(uint)ais->type4.accuracy,
			ais->type4.latitude, 
			ais->type4.longitude,
			ais->type4.epfd);
	    break;
	case 5: /* Ship static and voyage related data */
	    ais->type5.ais_version  = UBITS(38, 2);
	    ais->type5.imo_id       = UBITS(40, 30);
	    ais->type5.ais_version  = UBITS(388, 2);
	    /*@ +type @*/
	    for (i = 0; i < sizeof(ais->type5.callsign[i])-1; i++)
		ais->type5.callsign[i] = '@' + UBITS(70 + 6*i, 6);
	    ais->type5.callsign[sizeof(ais->type5.callsign[i])-1] = '\0';
	    for (i = 0; i < sizeof(ais->type5.vessel_name[i])-1; i++)
		ais->type5.vessel_name[i] = '@' + UBITS(112 + 6*i, 6);
	    ais->type5.vessel_name[sizeof(ais->type5.vessel_name[i])-1] = '\0';
	    /*@ -type @*/
	    ais->type5.ship_type    = UBITS(232, 8);
	    ais->type5.to_bow       = UBITS(240, 9);
	    ais->type5.to_stern     = UBITS(249, 9);
	    ais->type5.to_port      = UBITS(258, 9);
	    ais->type5.to_starboard = UBITS(264, 9);
	    ais->type5.epfd         = UBITS(270, 4);
	    ais->type5.month        = UBITS(274, 4);
	    ais->type5.day          = UBITS(278, 5);
	    ais->type5.hour         = UBITS(283, 5);
	    ais->type5.minute       = UBITS(288, 5);
	    ais->type5.draught      = UBITS(293, 9);
	    /*@ +type @*/
	    for (i = 0; i < sizeof(ais->type5.destination[i])-1; i++)
		ais->type5.destination[i] = '@' + UBITS(302 + 6*i, 6);
	    ais->type5.destination[sizeof(ais->type5.destination[i])-1] = '\0';
	    /*@ -type @*/
	    ais->type5.dte          = UBITS(422, 1);
	    ais->type5.spare        = UBITS(423, 1);
	    break;
	    
	default:
	    gpsd_report(LOG_ERROR, "Unparsed AIVDM message type %d.\n",ais->id);
	    break;
	} 
#undef SBITS
#undef UBITS

	/* data is fully decoded */
	return true;
    }

    /* we're still waiting on another sentence */
    return false;
}
/*@ -charint @*/

gps_mask_t aivdm_parse(struct gps_device_t *session)
{
    gps_mask_t mask = ONLINE_SET;    

    if (aivdm_decode(session, &session->driver.aivdm.decoded)) {
	mask |= PACKET_SET;
	/* 
	 * XXX The tag field is only 8 bytes, whic will truncate the MMSI; 
	 * widen it when ready for production.
	 */
	(void)snprintf(session->gpsdata.tag, sizeof(session->gpsdata.tag),
		       "AIS%d", session->driver.aivdm.decoded.mmsi);

	/* FIXME: actual driver code goes here */
    }
    
    /* not posting any data yet */
    return mask;
}

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
