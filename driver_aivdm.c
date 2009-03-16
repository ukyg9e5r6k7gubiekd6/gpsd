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

static void from_sixbit(char *bitvec, int start, int count, char *to)
{
    /*@ +type @*/
    const char sixchr[64] = "@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^- !\"#$%&`()*+,-./0123456789:;<=>?";
    int i;

    /* six-bit to ASCII */
    for (i = 0; i < count-1; i++)
	to[i] = sixchr[ubits(bitvec, start + 6*i, 6)];
    to[count-1] = '\0';
    /* trim spaces on right end */
    for (i = count-2; i >= 0; i--)
	if (to[i] == ' ')
	    to[i] = '\0';
	else
	    break;
    /*@ -type @*/
}

/*@ +charint @*/
bool aivdm_decode(char *buf, size_t buflen, struct aivdm_context_t *ais_context)
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
    unsigned char *data, *cp = ais_context->fieldcopy;
    struct ais_t *ais = &ais_context->decoded;
    unsigned char ch;
    int i;

    if (buflen == 0)
	return false;

    /* we may need to dump the raw packet */
    gpsd_report(LOG_PROG, "AIVDM packet length %ld: %s", buflen, buf);

    /* extract packet fields */
    (void)strlcpy((char *)ais_context->fieldcopy, 
		  (char*)buf,
		  buflen);
    ais_context->field[nfields++] = (unsigned char *)buf;
    for (cp = ais_context->fieldcopy; 
	 cp < ais_context->fieldcopy + buflen;
	 cp++)
	if (*cp == ',') {
	    *cp = '\0';
	    ais_context->field[nfields++] = cp + 1;
	}
    ais_context->part = atoi((char *)ais_context->field[1]);
    ais_context->await = atoi((char *)ais_context->field[2]);
    data = ais_context->field[5];
    gpsd_report(LOG_PROG, "part=%d, awaiting=%d, data=%s\n",
		ais_context->part, ais_context->await,
		data);

    /* assemble the binary data */
    if (ais_context->part == 1) {
	(void)memset(ais_context->bits, '\0', sizeof(ais_context->bits));
	ais_context->bitlen = 0;
    }

    /* wacky 6-bit encoding, shades of FIELDATA */
    /*@ +charint @*/
    for (cp = data; cp < data + strlen((char *)data); cp++) {
	ch = *cp;
	ch -= 48;
	if (ch >= 40)
	    ch -= 8;
	gpsd_report(LOG_RAW, "%c: %s\n", *cp, sixbits[ch]);
	for (i = 5; i >= 0; i--) {
	    if ((ch >> i) & 0x01) {
		ais_context->bits[ais_context->bitlen / 8] |= (1 << (7 - ais_context->bitlen % 8));
	    }
	    ais_context->bitlen++;
	}
    }
    /*@ -charint @*/

    /* time to pass buffered-up data to where it's actually processed? */
    if (ais_context->part == ais_context->await) {
	size_t clen = (ais_context->bitlen + 7)/8;
	gpsd_report(LOG_INF, "AIVDM payload is %zd bits, %zd chars: %s\n",
		    ais_context->bitlen, clen,
		    gpsd_hexdump_wrapper(ais_context->bits,
					 clen, LOG_INF));


#define UBITS(s, l)	ubits((char *)ais_context->bits, s, l)
#define SBITS(s, l)	sbits((char *)ais_context->bits, s, l)
#define UCHARS(s, to)	from_sixbit((char *)ais_context->bits, s, sizeof(to), to)
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
	    ais->type4.year = UBITS(38, 14);
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
	    UCHARS(70, ais->type5.callsign);
	    UCHARS(112, ais->type5.vessel_name);
	    ais->type5.ship_type    = UBITS(232, 8);
	    ais->type5.to_bow       = UBITS(240, 9);
	    ais->type5.to_stern     = UBITS(249, 9);
	    ais->type5.to_port      = UBITS(258, 9);
	    ais->type5.to_starboard = UBITS(264, 9);
	    ais->type5.epfd         = UBITS(270, 4);
	    ais->type5.minute       = UBITS(274, 6);
	    ais->type5.hour         = UBITS(280, 5);
	    ais->type5.day          = UBITS(285, 5);
	    ais->type5.month        = UBITS(290, 4);
	    ais->type5.draught      = UBITS(293, 9);
	    UCHARS(302, ais->type5.destination);
	    ais->type5.dte          = UBITS(422, 1);
	    ais->type5.spare        = UBITS(423, 1);
	    break;
	case 9: /* Standard SAR Aircraft Position Report */
	    ais->type9.altitude = UBITS(38, 12);
	    ais->type9.sog = UBITS(50, 10);
	    ais->type9.accuracy = (bool)UBITS(60, 1);
	    ais->type9.longitude = SBITS(61, 28);
	    ais->type9.latitude = SBITS(89, 27);
	    ais->type9.cog = UBITS(116, 12);
	    ais->type9.utc_second = UBITS(128, 6);
	    ais->type9.regional = UBITS(134, 8);
	    ais->type9.dte = UBITS(142, 1);
	    ais->type9.spare = UBITS(143, 3);
	    ais->type9.radio = UBITS(146, 22);
	    gpsd_report(LOG_INF,
			"Alt=%d SOG=%d Q=%d Lon=%d Lat=%d COG=%d Sec=%d\n",
			ais->type9.altitude,
			ais->type9.sog, 
			(uint)ais->type9.accuracy,
			ais->type9.longitude, 
			ais->type9.latitude, 
			ais->type9.cog, 
			ais->type9.utc_second);
	    break;
	default:
	    gpsd_report(LOG_ERROR, "Unparsed AIVDM message type %d.\n",ais->id);
	    break;
	} 
#undef UCHARS
#undef SBITS
#undef UBITS

	/* data is fully decoded */
	return true;
    }

    /* we're still waiting on another sentence */
    return false;
}
/*@ -charint @*/

void  aivdm_dump(struct ais_t *ais, bool scaled, bool labeled, FILE *fp)
{
    static char *nav_legends[] = {
	"Under way using engine",
	"At anchor",
	"Not under command",
	"Restricted manoeuverability",
	"Constrained by her draught",
	"Moored",
	"Aground",
	"Engaged in fishing",
	"Under way sailing",
	"Reserved for HSC",
	"Reserved for WIG",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"Not defined",
    };
    static char *epfd_legends[] = {
	"Undefined",
	"GPS",
	"GLONASS",
	"Combined GPS/GLONASS",
	"Loran-C",
	"Chayka",
	"Integrated navigation system",
	"Surveyed",
	"Galileo",
    };

    static char *type_legends[100] = {
	"Not available",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
	"Wing in ground (WIG), all ships of this type",
	"Wing in ground (WIG), Hazardous category A",
	"Wing in ground (WIG), Hazardous category B",
	"Wing in ground (WIG), Hazardous category C",
	"Wing in ground (WIG), Hazardous category D",
	"Wing in ground (WIG), Reserved for future use",
	"Wing in ground (WIG), Reserved for future use",
	"Wing in ground (WIG), Reserved for future use",
	"Wing in ground (WIG), Reserved for future use",
	"Wing in ground (WIG), Reserved for future use",
	"Fishing",
	"Towing",
	"Towing: length exceeds 200m or breadth exceeds 25m",
	"Dredging or underwater ops",
	"Diving ops",
	"Military ops",
	"Sailing",
	"Pleasure Craft",
	"Reserved",
	"Reserved",
	"High speed craft (HSC), all ships of this type",
	"High speed craft (HSC), Hazardous category A",
	"High speed craft (HSC), Hazardous category B",
	"High speed craft (HSC), Hazardous category C",
	"High speed craft (HSC), Hazardous category D",
	"High speed craft (HSC), Reserved for future use",
	"High speed craft (HSC), Reserved for future use",
	"High speed craft (HSC), Reserved for future use",
	"High speed craft (HSC), Reserved for future use",
	"High speed craft (HSC), No additional information",
	"Pilot Vessel",
	"Search and Rescue vessel",
	"Tug",
	"Port Tender",
	"Anti-pollution equipment",
	"Law Enforcement",
	"Spare - Local Vessel",
	"Spare - Local Vessel",
	"Medical Transport",
	"Ship according to RR Resolution No. 18",
	"Passenger, all ships of this type",
	"Passenger, Hazardous category A",
	"Passenger, Hazardous category B",
	"Passenger, Hazardous category C",
	"Passenger, Hazardous category D",
	"Passenger, Reserved for future use",
	"Passenger, Reserved for future use",
	"Passenger, Reserved for future use",
	"Passenger, Reserved for future use",
	"Passenger, No additional information",
	"Cargo, all ships of this type",
	"Cargo, Hazardous category A",
	"Cargo, Hazardous category B",
	"Cargo, Hazardous category C",
	"Cargo, Hazardous category D",
	"Cargo, Reserved for future use",
	"Cargo, Reserved for future use",
	"Cargo, Reserved for future use",
	"Cargo, Reserved for future use",
	"Cargo, No additional information",
	"Tanker, all ships of this type",
	"Tanker, Hazardous category A",
	"Tanker, Hazardous category B",
	"Tanker, Hazardous category C",
	"Tanker, Hazardous category D",
	"Tanker, Reserved for future use",
	"Tanker, Reserved for future use",
	"Tanker, Reserved for future use",
	"Tanker, Reserved for future use",
	"Tanker, No additional information",
	"Other Type, all ships of this type",
	"Other Type, Hazardous category A",
	"Other Type, Hazardous category B",
	"Other Type, Hazardous category C",
	"Other Type, Hazardous category D",
	"Other Type, Reserved for future use",
	"Other Type, Reserved for future use",
	"Other Type, Reserved for future use",
	"Other Type, Reserved for future use",
	"Other Type, no additional information",
    };

    if (labeled)
	(void)fprintf(fp, "type=%d,ri=%d,MMSI=%09d,", ais->id, ais->ri, ais->mmsi);
    else
	(void)fprintf(fp, "%d,%d,%09d,", ais->id, ais->ri, ais->mmsi);
    switch (ais->id) {
    case 1:	/* Position Report */
    case 2:
    case 3:
#define TYPE123_UNSCALED_UNLABELED "%u,%d,%u,%u,%d,%d,%u,%u,%u,%x,%d,%x\n"
#define TYPE123_UNSCALED_LABELED   "st=%u,ROT=%u,SOG=%u,fq=%u,lon=%d,lat=%d,cog=%u,hd=%u,sec=%u,reg=%x,sp=%d,radio=%x\n"
#define TYPE123_SCALED_UNLABELED "%s,%s,%.1f,%u,%.4f,%.4f,%u,%u,%u,%x,%d,%x\n"
#define TYPE123_SCALED_LABELED   "st=%s,ROT=%s,SOG=%.1f,fq=%u,lon=%.4f,lat=%.4f,cog=%u,hd=%u,sec=%u,reg=%x,sp=%d,radio=%x\n"
	if (scaled) {
	    char rotlegend[10];

	    /* 
	     * Express ROT as nan if not available, 
	     * "fastleft"/"fastright" for fast turns.
	     */
	    if (ais->type123.rot == -128)
		(void) strlcpy(rotlegend, "nan", sizeof(rotlegend));
	    else if (ais->type123.rot == -127)
		(void) strlcpy(rotlegend, "fastleft", sizeof(rotlegend));
	    else if (ais->type123.rot == 127)
		(void) strlcpy(rotlegend, "fastright", sizeof(rotlegend));
	    else
		(void)snprintf(rotlegend, sizeof(rotlegend),
			       "%.0f",
			       ais->type123.rot * ais->type123.rot / 4.733);

	    (void)fprintf(fp,
			  (labeled ? TYPE123_SCALED_LABELED : TYPE123_SCALED_UNLABELED),
			      
			  nav_legends[ais->type123.status],
			  rotlegend,
			  ais->type123.sog / 10.0, 
			  (uint)ais->type123.accuracy,
			  ais->type123.longitude / AIS_LATLON_SCALE, 
			  ais->type123.latitude / AIS_LATLON_SCALE, 
			  ais->type123.cog, 
			  ais->type123.heading, 
			  ais->type123.utc_second,
			  ais->type123.regional,
			  ais->type123.spare,
			  ais->type123.radio);
	} else {
	    (void)fprintf(fp,
			  (labeled ? TYPE123_UNSCALED_LABELED : TYPE123_UNSCALED_UNLABELED),
			  ais->type123.status,
			  ais->type123.rot,
			  ais->type123.sog, 
			  (uint)ais->type123.accuracy,
			  ais->type123.longitude,
			  ais->type123.latitude,
			  ais->type123.cog, 
			  ais->type123.heading, 
			  ais->type123.utc_second,
			  ais->type123.regional,
			  ais->type123.spare,
			  ais->type123.radio);
	}
#undef TYPE123_UNSCALED_UNLABELED
#undef TYPE123_UNSCALED_LABELED
#undef TYPE123_SCALED_UNLABELED
#undef TYPE123_SCALED_LABELED
	break;
    case 4:	/* Base Station Report */
#define TYPE4_UNSCALED_UNLABELED "%04u:%02u:%02uT%02u:%02u:%02uZ,%u,%d,%d,%u,%u,%x\n"
#define TYPE4_UNSCALED_LABELED "%4u:%02u:%02uT%02u:%02u:%02uZ,q=%u,lon=%d,lat=%d,epfd=%u,sp=%u,radio=%x\n"
#define TYPE4_SCALED_UNLABELED	"%4u:%02u:%02uT%02u:%02u:%02uZ,%u,%.4f,%.4f,%s,%u,%x\n"
#define TYPE4_SCALED_LABELED "%4u:%02u:%02uT%02u:%02u:%02uZ,q=%u,lon=%.4f,lat=%.4f,epfd=%s,sp=%u,radio=%x\n"
	if (scaled) {
	    (void)fprintf(fp,
			  (labeled ? TYPE4_SCALED_LABELED : TYPE4_SCALED_UNLABELED),
			  ais->type4.year,
			  ais->type4.month,
			  ais->type4.day,
			  ais->type4.hour,
			  ais->type4.minute,
			  ais->type4.second,
			  (uint)ais->type4.accuracy,
			  ais->type4.latitude / AIS_LATLON_SCALE, 
			  ais->type4.longitude / AIS_LATLON_SCALE,
			  epfd_legends[ais->type4.epfd],
			  ais->type4.spare,
			  ais->type4.radio);
	} else {
	    (void)fprintf(fp,
			  (labeled ? TYPE4_UNSCALED_LABELED : TYPE4_UNSCALED_UNLABELED),
			  ais->type4.year,
			  ais->type4.month,
			  ais->type4.day,
			  ais->type4.hour,
			  ais->type4.minute,
			  ais->type4.second,
			  (uint)ais->type4.accuracy,
			  ais->type4.latitude, 
			  ais->type4.longitude,
			  ais->type4.epfd,
			  ais->type4.spare,
			  ais->type4.radio);
	}
#undef TYPE4_UNSCALED_UNLABELED
#undef TYPE4_UNSCALED_LABELED
#undef TYPE4_SCALED_UNLABELED
#undef TYPE4_SCALED_LABELED
	break;
    case 5: /* Ship static and voyage related data */
#define TYPE5_SCALED_LABELED "ID=%u,AIS=%u,callsign=%s,name=%s,type=%s,bow=%u,stern=%u,port=%u,starboard=%u,epsd=%s,eta=%u:%uT%u:%uZ,draught=%.1f,dest=%s,dte=%u,sp=%u\n"
#define TYPE5_SCALED_UNLABELED "%u,%u,%s,%s,%s,%u,%u,%u,%u,%s,%u:%uT%u:%uZ,%.1f,%s,%u,%u\n"
	if (scaled) {
	    (void)fprintf(fp,
			  (labeled ? TYPE5_SCALED_LABELED : TYPE5_SCALED_UNLABELED),
			  ais->type5.imo_id,
			  ais->type5.ais_version,
			  ais->type5.callsign,
			  ais->type5.vessel_name,
			  type_legends[ais->type5.ship_type],
			  ais->type5.to_bow,
			  ais->type5.to_stern,
			  ais->type5.to_port,
			  ais->type5.to_starboard,
			  epfd_legends[ais->type5.epfd],
			  ais->type5.month,
			  ais->type5.day,
			  ais->type5.hour,
			  ais->type5.minute,
			  ais->type5.draught / 10.0,
			  ais->type5.destination,
			  ais->type5.dte,
			  ais->type5.spare);
	} else {
#define TYPE5_UNSCALED_LABELED "ID=%u,AIS=%u,callsign=%s,name=%s,type=%u,bow=%u,stern=%u,port=%u,starboard=%u,epsd=%u,eta=%u:%uT%u:%uZ,draught=%u,dest=%s,dte=%u,sp=%u\n"
#define TYPE5_UNSCALED_UNLABELED "%u,%u,%s,%s,%u,%u,%u,%u,%u,%u,%u:%uT%u:%uZ,%u,%s,%u,%u\n"
	    (void)fprintf(fp,
			  (labeled ? TYPE5_UNSCALED_LABELED : TYPE5_UNSCALED_UNLABELED),
			  ais->type5.imo_id,
			  ais->type5.ais_version,
			  ais->type5.callsign,
			  ais->type5.vessel_name,
			  ais->type5.ship_type,
			  ais->type5.to_bow,
			  ais->type5.to_stern,
			  ais->type5.to_port,
			  ais->type5.to_starboard,
			  ais->type5.epfd,
			  ais->type5.month,
			  ais->type5.day,
			  ais->type5.hour,
			  ais->type5.minute,
			  ais->type5.draught,
			  ais->type5.destination,
			  ais->type5.dte,
			  ais->type5.spare);
	}
#undef TYPE5_UNSCALED_UNLABELED
#undef TYPE5_UNSCALED_LABELED
#undef TYPE5_SCALED_UNLABELED
#undef TYPE5_SCALED_LABELED
	break;
    case 9:
#define TYPE9_UNSCALED_UNLABELED "%u,%u,%u,%d,%d,%u,%u,%x,%u,%d,%x\n"
#define TYPE9_UNSCALED_LABELED   "alt=%u,SOG=%u,fq=%u,lon=%d,lat=%d,cog=%u,sec=%u,reg=%x,dte=%u,sp=%d,radio=%x\n"
#define TYPE9_SCALED_UNLABELED "%u,%u,%u,%.4f,%.4f,%.1f,%u,%x,%u,%d,%x\n"
#define TYPE9_SCALED_LABELED   "alt=%u,SOG=%u,fq=%u,lon=%.4f,lat=%.4f,cog=%.1f,sec=%u,reg=%x,dte=%u,sp=%d,radio=%x\n"
	if (scaled) {
	    (void)fprintf(fp,
			  (labeled ? TYPE9_SCALED_LABELED : TYPE9_SCALED_UNLABELED),
			      
			  ais->type9.altitude,
			  ais->type9.sog, 
			  (uint)ais->type9.accuracy,
			  ais->type9.longitude / AIS_LATLON_SCALE, 
			  ais->type9.latitude / AIS_LATLON_SCALE, 
			  ais->type9.cog / 10.0, 
			  ais->type9.utc_second,
			  ais->type9.regional,
			  ais->type9.dte, 
			  ais->type9.spare,
			  ais->type9.radio);
	} else {
	    (void)fprintf(fp,
			  (labeled ? TYPE9_UNSCALED_LABELED : TYPE9_UNSCALED_UNLABELED),
			  ais->type9.altitude,
			  ais->type9.sog, 
			  (uint)ais->type9.accuracy,
			  ais->type9.longitude,
			  ais->type9.latitude,
			  ais->type9.cog, 
			  ais->type9.utc_second,
			  ais->type9.regional,
			  ais->type9.dte, 
			  ais->type9.spare,
			  ais->type9.radio);
	}
#undef TYPE9_UNSCALED_UNLABELED
#undef TYPE9_UNSCALED_LABELED
#undef TYPE9_SCALED_UNLABELED
#undef TYPE9_SCALED_LABELED
	break;
    default:
	gpsd_report(LOG_ERROR, "Unparsed AIVDM message type %u.\n",ais->id);
	break;
    }
}

#endif /* defined(AIVDM_ENABLE) */
