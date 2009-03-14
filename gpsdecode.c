/* $Id$ */
#include <sys/types.h>
#ifndef S_SPLINT_S
#include <unistd.h>
#endif /* S_SPLINT_S */
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <ctype.h>

#include "gpsd_config.h"
#include "gpsd.h"

static int verbose = 0;
static bool scaled = true;
static bool labeled = false;

static struct gps_device_t session;
static struct gps_context_t context;

/**************************************************************************
 *
 * AIVDM decoding
 *
 **************************************************************************/

static void  aivdm_dump(struct ais_t *ais, FILE *fp)
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
	    float sog;

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

	    /* express SOG as nan if value is unknown */
	    if (ais->type123.sog == AIS_SOG_NOT_AVAILABLE)
		sog = -1.0;
	    else
		sog = ais->type123.sog / 10.0;

	    (void)fprintf(fp,
			  (labeled ? TYPE123_SCALED_LABELED : TYPE123_SCALED_UNLABELED),
			      
			  nav_legends[ais->type123.status],
			  rotlegend,
			  sog, 
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
#define TYPE5_UNSCALED_LABELED "ID=%u,AIS=%u,callsign=%s,name=%s,type=%u,bow=%u,stern=%u,port=%u,starboard=%u,epsd=%u,eta=%u:%uT%u:%uZ,draught=%u,dest=%s,dte=%u,sp=%u\n"
#define TYPE5_UNSCALED_UNLABELED "%u,%u,%s,%s,%u,%u,%u,%u,%u,%u,%u:%uT%u:%uZ,%u,%s,%u,%u\n"
#define TYPE5_SCALED_LABELED "ID=%u,AIS=%u,callsign=%s,name=%s,type=%s,bow=%u,stern=%u,port=%u,starboard=%u,epsd=%s:%u,eta=%u:%uT%u:%uZ,dest=%s,dte=%u,sp=%u\n"
#define TYPE5_SCALED_UNLABELED "%u,%u,%s,%s,%s,%u,%u,%u,%u,%s,%u:%uT%u:%uZ,%u,%s,%u,%u\n"
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
			  ais->type5.draught,
			  ais->type5.destination,
			  ais->type5.dte,
			  ais->type5.spare);
	} else {
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
    default:
	(void)fprintf(fp,"?\n");
	gpsd_report(LOG_ERROR, "Unparsed AIVDM message type %u.\n",ais->id);
	break;
    }
}

/**************************************************************************
 *
 * Generic machinery
 *
 **************************************************************************/

void gpsd_report(int errlevel, const char *fmt, ... )
/* assemble command in printf(3) style, use stderr or syslog */
{
    if (errlevel <= verbose) {
	char buf[BUFSIZ];
	va_list ap;

	(void)strlcpy(buf, "gpsdecode: ", BUFSIZ);
	va_start(ap, fmt) ;
	(void)vsnprintf(buf + strlen(buf), sizeof(buf)-strlen(buf), fmt, ap);
	va_end(ap);
	(void)fputs(buf, stdout);
    }
}

/*@ -compdestroy -compdef -usedef @*/
static void decode(FILE *fpin, FILE *fpout)
/* binary on fpin to dump format on fpout */
{
    struct rtcm2_t rtcm2;
    struct rtcm3_t rtcm3;
    char buf[BUFSIZ];

    for (;;) {
	gps_mask_t state = gpsd_poll(&session);

	if (state & ERROR_SET) {
	    gpsd_report(LOG_ERROR,"Error during packet fetch.\n");
	    break;
	}
	if (session.packet.type == COMMENT_PACKET)
	    (void)fputs((char *)session.packet.outbuffer, fpout);
	else if (session.packet.type == RTCM2_PACKET) {
	    rtcm2_unpack(&rtcm2, (char *)session.packet.isgps.buf);
	    rtcm2_dump(&rtcm2, buf, sizeof(buf));
	    (void)fputs(buf, fpout);
	}
	else if (session.packet.type == RTCM3_PACKET) {
	    rtcm3_unpack(&rtcm3, (char *)session.packet.outbuffer);
	    rtcm3_dump(&rtcm3, stdout);
	}
	else if (session.packet.type == AIVDM_PACKET) {
	    if (state & PACKET_SET)
		aivdm_dump(&session.driver.aivdm.decoded, stdout);
	} else
	    gpsd_report(LOG_ERROR, "unknown packet type %d\n", session.packet.type);
	if (packet_buffered_input(&session.packet) <= 0)
	    break;
    }
}
/*@ +compdestroy +compdef +usedef @*/

/*@ -compdestroy @*/
static void pass(FILE *fpin, FILE *fpout)
/* dump format on stdin to dump format on stdout (self-inversion test) */
{
    char buf[BUFSIZ];
    struct gps_packet_t lexer;
    struct rtcm2_t rtcm;

    memset(&lexer, 0, sizeof(lexer));
    memset(&rtcm, 0, sizeof(rtcm));
    while (fgets(buf, (int)sizeof(buf), fpin) != NULL) {
	int status;

	/* pass through comment lines without interpreting */
	if (buf[0] == '#') {
	    (void)fputs(buf, fpout);
	    continue;
	}
	/* ignore trailer lines as we'll regenerate these */
	else if (buf[0] == '.')
	    continue;

	status = rtcm2_undump(&rtcm, buf);

	if (status == 0) {
	    (void)memset(lexer.isgps.buf, 0, sizeof(lexer.isgps.buf));
	    (void)rtcm2_repack(&rtcm, lexer.isgps.buf);
	    (void)rtcm2_unpack(&rtcm, (char *)lexer.isgps.buf);
	    (void)rtcm2_dump(&rtcm, buf, sizeof(buf));
	    (void)fputs(buf, fpout);
	    memset(&lexer, 0, sizeof(lexer));
	    memset(&rtcm, 0, sizeof(rtcm));
	} else if (status < 0) {
	    (void) fprintf(stderr, "gpsdecode: bailing out with status %d\n", status);
	    exit(1);
	}
    }
}
/*@ +compdestroy @*/

/*@ -compdestroy @*/
static void encode(FILE *fpin, FILE *fpout)
/* dump format on fpin to RTCM-104 on fpout */
{
    char buf[BUFSIZ];
    struct gps_packet_t lexer;
    struct rtcm2_t rtcm;

    memset(&lexer, 0, sizeof(lexer));
    while (fgets(buf, (int)sizeof(buf), fpin) != NULL) {
	int status;

	status = rtcm2_undump(&rtcm, buf);

	if (status == 0) {
	    (void)memset(lexer.isgps.buf, 0, sizeof(lexer.isgps.buf));
	    (void)rtcm2_repack(&rtcm, lexer.isgps.buf);
	    if (fwrite(lexer.isgps.buf, 
		       sizeof(isgps30bits_t), 
		       (size_t)rtcm.length, fpout) != (size_t)rtcm.length)
		(void) fprintf(stderr, "gpsdecode: report write failed.\n");
	    memset(&lexer, 0, sizeof(lexer));
	} else if (status < 0) {
	    (void) fprintf(stderr, "gpsdecode: bailing out with status %d\n", status);
	    exit(1);
	}
    }
}
/*@ +compdestroy @*/

int main(int argc, char **argv)
{
    char buf[BUFSIZ];
    int c;
    bool striphdr = false;
    enum {doencode, dodecode, passthrough} mode = dodecode;

    while ((c = getopt(argc, argv, "def:hlpuVD:")) != EOF) {
	switch (c) {
	case 'd':
	    mode = dodecode;
	    break;

	case 'e':
	    mode = doencode;
	    break;

	case 'f':
	    (void)freopen(optarg, "r", stdin);
	    break;

	case 'h':
	    striphdr = true;
	    break;

	case 'l':
	    labeled = true;
	    break;

	case 'p':	/* undocumented, used for regression-testing */
	    mode = passthrough;
	    break;

	case 'u':
	    scaled = false;
	    break;

	case 'D':
	    verbose = atoi(optarg);
	    gpsd_hexdump_level = verbose;
	    break;

	case 'V':
	    (void)fprintf(stderr, "SVN ID: $Id$ \n");
	    exit(0);

	case '?':
	default:
	    (void)fputs("gpsdecode [-v]\n", stderr);
	    exit(1);
	}
    }
    argc -= optind;
    argv += optind;

    gpsd_init(&session, &context, (char *)NULL);
    session.gpsdata.gps_fd = fileno(stdin);

    /* strip lines with leading # */
    if (striphdr) {
	while ((c = getchar()) == '#')
	    if (fgets(buf, (int)sizeof(buf), stdin) == NULL)
		(void)fputs("gpsdecode: read failed\n", stderr);
	(void)ungetc(c, stdin);
    }

    if (mode == passthrough)
	pass(stdin, stdout);
    else if (mode == doencode)
	encode(stdin, stdout);
    else
	decode(stdin, stdout);
    exit(0);
}

/* gpsdecode.c ends here */
