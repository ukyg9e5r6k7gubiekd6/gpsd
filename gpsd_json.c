/****************************************************************************

NAME
   gpsd_json.c - move data between in-core and JSON structures

DESCRIPTION
   This module uses the generic JSON parser to get data from JSON
representations to gpsd core strctures, and vice_versa.

***************************************************************************/

#include <math.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include "gpsd.h"
#include "gps_json.h"
#include "revision.h"

/*
 * Manifest names for the gnss_type enum - must be kept synced with it.
 * Also, masks so we can tell what packet types correspond to each class.
 */
struct classmap_t classmap[CLASSMAP_NITEMS] = {
    /* name	typemask	packetmask */
    {"ANY",	0,       	0},
    {"GPS",	SEEN_GPS, 	GPS_TYPEMASK},
    {"RTCM2",	SEEN_RTCM2,	PACKET_TYPEMASK(RTCM2_PACKET)},
    {"RTCM3",	SEEN_RTCM3,	PACKET_TYPEMASK(RTCM3_PACKET)},
    {"AIS",	SEEN_AIS,  	PACKET_TYPEMASK(AIVDM_PACKET)},
};

char *json_stringify(/*@out@*/char *to, size_t len, /*@in@*/const char *from)
/* escape double quotes and control characters inside a JSON string */
{
    /*@-temptrans@*/
    const char *sp;
    char *tp;

    tp = to;
    /* 
     * The limit is len-6 here because we need to be leave room for
     * each character to generate an up to 6-character Java-style
     * escape
     */
    for (sp = from; *sp!='\0' && ((tp - to) < ((int)len-5)); sp++) {
	if (iscntrl(*sp)) {
	    *tp++ = '\\';
	    switch (*sp) {
	    case '\b':
		*tp++ = 'b';
		break;
	    case '\f':
		*tp++ = 'f';
		break;
	    case '\n':
		*tp++ = 'n';
		break;
	    case '\r':
		*tp++ = 'r';
		break;
	    case '\t':
		*tp++ = 't';
		break;
	    default:
		/* ugh, we'd prefer a C-style escape here, but this is JSON */
		(void)snprintf(tp, 5, "%u04x", (unsigned int)*sp);
		tp += strlen(tp);
	    }
	} else {
	    if (*sp == '"' || *sp == '\\')
		*tp++ = '\\';
	    *tp++ = *sp;
	}
    }
    *tp = '\0';

    return to;
    /*@+temptrans@*/
}

void json_version_dump(/*@out@*/char *reply, size_t replylen)
{
    (void)snprintf(reply, replylen,
		   "{\"class\":\"VERSION\",\"release\":\"%s\",\"rev\":\"%s\",\"proto_major\":%d,\"proto_minor\":%d}\r\n", 
		   VERSION, REVISION, 
		   GPSD_PROTO_MAJOR_VERSION, GPSD_PROTO_MINOR_VERSION);
}

void json_tpv_dump(const struct gps_data_t *gpsdata, struct gps_fix_t *fixp, 
		   /*@out@*/char *reply, size_t replylen)
{
    assert(replylen > 2);
    (void)strlcpy(reply, "{\"class\":\"TPV\",", replylen);
    (void)snprintf(reply+strlen(reply),
		   replylen-strlen(reply),
		   "\"tag\":\"%s\",",
		   gpsdata->tag[0]!='\0' ? gpsdata->tag : "-");
    (void)snprintf(reply+strlen(reply),
		   replylen-strlen(reply),
		   "\"device\":\"%s\",",
		   gpsdata->dev.path);
    if (isnan(fixp->time)==0)
	(void)snprintf(reply+strlen(reply),
		       replylen-strlen(reply),
		       "\"time\":%.3f,",
		       fixp->time);
    if (isnan(fixp->ept)==0)
	(void)snprintf(reply+strlen(reply),
		       replylen-strlen(reply),
		       "\"ept\":%.3f,",
		       fixp->ept);
    if (isnan(fixp->latitude)==0)
	(void)snprintf(reply+strlen(reply),
		       replylen-strlen(reply),
		       "\"lat\":%.9f,",
		       fixp->latitude);
    if (isnan(fixp->longitude)==0)
	(void)snprintf(reply+strlen(reply),
		       replylen-strlen(reply),
		       "\"lon\":%.9f,",
		       fixp->longitude);
    if (isnan(fixp->altitude)==0)
	(void)snprintf(reply+strlen(reply),
		       replylen-strlen(reply),
		       "\"alt\":%.3f,",
		       fixp->altitude);
    if (isnan(fixp->epx)==0)
	(void)snprintf(reply+strlen(reply),
		       replylen-strlen(reply),
		       "\"epx\":%.3f,",
		       fixp->epx);
    if (isnan(fixp->epy)==0)
	(void)snprintf(reply+strlen(reply),
		       replylen-strlen(reply),
		       "\"epy\":%.3f,",
		       fixp->epy);
    if (isnan(fixp->epv)==0)
	(void)snprintf(reply+strlen(reply),
		       replylen-strlen(reply),
		       "\"epv\":%.3f,",
		       fixp->epv);
    if (isnan(fixp->track)==0)
	(void)snprintf(reply+strlen(reply),
		       replylen-strlen(reply),
		       "\"track\":%.4f,",
		       fixp->track);
    if (isnan(fixp->speed)==0)
	(void)snprintf(reply+strlen(reply),
		       replylen-strlen(reply),
		       "\"speed\":%.3f,",
		       fixp->speed);
    if (isnan(fixp->climb)==0)
	(void)snprintf(reply+strlen(reply),
		       replylen-strlen(reply),
		       "\"climb\":%.3f,",
		       fixp->climb);
    if (isnan(fixp->epd)==0)
	(void)snprintf(reply+strlen(reply),
		       replylen-strlen(reply),
		       "\"epd\":%.4f,",
		       fixp->epd);
    if (isnan(fixp->eps)==0)
	(void)snprintf(reply+strlen(reply),
		       replylen-strlen(reply),
		       "\"eps\":%.2f,", fixp->eps);
    if (isnan(fixp->epc)==0)
	(void)snprintf(reply+strlen(reply),
		       replylen-strlen(reply),
		       "\"epc\":%.2f,", fixp->epc);
    if (fixp->mode > 0)
	(void)snprintf(reply+strlen(reply),
		       replylen-strlen(reply),
		       "\"mode\":%d,", fixp->mode);
    if (reply[strlen(reply)-1] == ',')
	reply[strlen(reply)-1] = '\0';	/* trim trailing comma */
    (void)strlcat(reply, "}\r\n", sizeof(reply)-strlen(reply));
}

void json_sky_dump(const struct gps_data_t *datap, 
		   /*@out@*/char *reply, size_t replylen)
{
    int i, j, used, reported = 0;
    assert(replylen > 2);
    (void)strlcpy(reply, "{\"class\":\"SKY\",", replylen);
    (void)snprintf(reply+strlen(reply),
		   replylen- strlen(reply),
		   "\"tag\":\"%s\",",
		   datap->tag[0]!='\0' ? datap->tag : "-");
    (void)snprintf(reply+strlen(reply),
		   replylen-strlen(reply),
		   "\"device\":\"%s\",",
		   datap->dev.path);
    if (isnan(datap->skyview_time)==0)
	(void)snprintf(reply+strlen(reply),
		       replylen-strlen(reply),
		       "\"time\":%.3f,",
		       datap->skyview_time);
    if (isnan(datap->dop.xdop)==0)
	(void)snprintf(reply+strlen(reply),
		       replylen-strlen(reply),
		       "\"xdop\":%.2f,", datap->dop.xdop);
    if (isnan(datap->dop.ydop)==0)
	(void)snprintf(reply+strlen(reply),
		       replylen-strlen(reply),
		       "\"ydop\":%.2f,", datap->dop.ydop);
    if (isnan(datap->dop.vdop)==0)
	(void)snprintf(reply+strlen(reply),
		       replylen-strlen(reply),
		       "\"vdop\":%.2f,", datap->dop.vdop);
    if (isnan(datap->dop.tdop)==0)
	(void)snprintf(reply+strlen(reply),
		       replylen-strlen(reply),
		       "\"tdop\":%.2f,", datap->dop.tdop);
    if (isnan(datap->dop.hdop)==0)
	(void)snprintf(reply+strlen(reply),
		       replylen-strlen(reply),
		       "\"hdop\":%.2f,", datap->dop.hdop);
    if (isnan(datap->dop.gdop)==0)
	(void)snprintf(reply+strlen(reply),
		       replylen-strlen(reply),
		       "\"gdop\":%.2f,", datap->dop.gdop);
    if (isnan(datap->dop.pdop)==0)
	(void)snprintf(reply+strlen(reply),
		       replylen-strlen(reply),
		       "\"pdop\":%.2f,", datap->dop.pdop);
    /* insurance against flaky drivers */
    for (i = 0; i < datap->satellites_visible; i++)
	if (datap->PRN[i])
	    reported++;
    if (reported) {
	(void)strlcat(reply, "\"satellites\":[", replylen);
	for (i = 0; i < reported; i++) {
	    used = 0;
	    for (j = 0; j < datap->satellites_used; j++)
		if (datap->used[j] == datap->PRN[i]) {
		    used = 1;
		    break;
		}
	    if (datap->PRN[i]) {
		(void)snprintf(reply+strlen(reply),
			       replylen-strlen(reply),
			       "{\"PRN\":%d,\"el\":%d,\"az\":%d,\"ss\":%.0f,\"used\":%s},",
			       datap->PRN[i],
			       datap->elevation[i],datap->azimuth[i],
			       datap->ss[i],
			       used ? "true" : "false");
	    }
	}
	if (reply[strlen(reply)-1] == ',')
	    reply[strlen(reply)-1] = '\0';	/* trim trailing comma */
	(void)strlcat(reply, "]", replylen-strlen(reply));
    }
    if (reply[strlen(reply)-1] == ',')
	reply[strlen(reply)-1] = '\0';	/* trim trailing comma */
    (void)strlcat(reply, "}\r\n", replylen-strlen(reply));
    if (datap->satellites_visible != reported)
	gpsd_report(LOG_WARN,"Satellite count %d != PRN count %d\n",
		    datap->satellites_visible, reported);
}

void json_device_dump(const struct gps_device_t *device,
		     /*@out@*/char *reply, size_t replylen)
{
    char buf1[JSON_VAL_MAX*2+1];
    struct classmap_t *cmp;
    (void)strlcpy(reply, "{\"class\":\"DEVICE\",\"path\":\"", replylen);
    (void)strlcat(reply, device->gpsdata.dev.path, replylen);
    (void)strlcat(reply, "\",", replylen);
    if (device->gpsdata.online > 0) {
	(void)snprintf(reply+strlen(reply), replylen-strlen(reply),
		       "\"activated\":%2.2f,", device->gpsdata.online);
	if (device->observed != 0) {
	    int mask = 0;
	    for (cmp = classmap; cmp < classmap+NITEMS(classmap); cmp++)
		if ((device->observed & cmp->packetmask) != 0) 
		    mask |= cmp->typemask;
	    if (mask != 0)
		(void)snprintf(reply+strlen(reply), replylen-strlen(reply),
			       "\"flags\":%d,", mask);
	}
	if (device->device_type != NULL) {
	    (void)strlcat(reply, "\"driver\":\"", replylen);
	    (void)strlcat(reply, 
			  device->device_type->type_name,
			  replylen);
	    (void)strlcat(reply, "\",", replylen);
	}
	/*@-mustfreefresh@*/
	if (device->subtype[0] != '\0') {
	    (void)strlcat(reply, "\"subtype\":\"", replylen);
	    (void)strlcat(reply, 
			  json_stringify(buf1, sizeof(buf1), device->subtype),
			  replylen);
	    (void)strlcat(reply, "\",", replylen);
	}
	/*@+mustfreefresh@*/
	if (device->is_serial) {
	    (void)snprintf(reply+strlen(reply), replylen-strlen(reply),
			   "\"native\":%d,\"bps\":%d,\"parity\":\"%c\",\"stopbits\":%u,\"cycle\":%2.2f",
			   device->gpsdata.dev.driver_mode,
			   (int)gpsd_get_speed(&device->ttyset),
			   device->gpsdata.dev.parity,
			   device->gpsdata.dev.stopbits,
			   device->gpsdata.dev.cycle);
	    if (device->device_type != NULL && device->device_type->rate_switcher != NULL)
		(void)snprintf(reply+strlen(reply), replylen-strlen(reply),
			       ",\"mincycle\":%2.2f",
			       device->device_type->min_cycle);
	}
    }
    if (reply[strlen(reply)-1] == ',')
	reply[strlen(reply)-1] = '\0';	/* trim trailing comma */
    (void)strlcat(reply, "}\r\n", replylen);
}

void json_watch_dump(const struct policy_t *ccp, 
		     /*@out@*/char *reply, size_t replylen)
{
    /*@-compdef@*/
    (void)snprintf(reply, replylen,
		   "{\"class\":\"WATCH\",\"enable\":%s,\"json\":%s,\"nmea\":%s,\"raw\":%d,\"scaled\":%s,\"timing\":%s",
		   ccp->watcher ? "true" : "false",
		   ccp->json ? "true" : "false",
		   ccp->nmea ? "true" : "false",
		   ccp->raw, 
		   ccp->scaled ? "true" : "false",
		   ccp->timing ? "true" : "false");
    if (ccp->devpath[0] != '\0')
	(void)snprintf(reply+strlen(reply), replylen-strlen(reply),
		       "\"device\":%s,", ccp->devpath);
    if (reply[strlen(reply)-1] == ',')
	reply[strlen(reply)-1] = '\0';
    (void)strlcat(reply, "}\r\n", replylen-strlen(reply));
    /*@+compdef@*/
}

#if defined(RTCM104V2_ENABLE)
void rtcm2_json_dump(const struct rtcm2_t *rtcm, /*@out@*/char buf[], size_t buflen)
/* dump the contents of a parsed RTCM104 message as JSON */
{
    /*@-mustfreefresh@*/
    char buf1[JSON_VAL_MAX*2+1];
    /*
     * Beware! Needs to stay synchronized with a JSON enumeration map in
     * the parser. This interpretation of NAVSYSTEM_GALILEO is assumed
     * from RTCM3, it's not actually documented in RTCM 2.1.
     */
    static char *navsysnames[] = {"GPS", "GLONASS", "GALILEO"};

    unsigned int n;

    (void)snprintf(buf, buflen, "{\"class\":\"RTCM2\",\"type\":%u,\"station_id\":%u,\"zcount\":%0.1f,\"seqnum\":%u,\"length\":%u,\"station_health\":%u,",
		   rtcm->type,
		   rtcm->refstaid,
		   rtcm->zcount,
		   rtcm->seqnum,
		   rtcm->length,
		   rtcm->stathlth);

    switch (rtcm->type) {
    case 1:
    case 9:
	(void)strlcat(buf, "\"satellites\":[", buflen);
	for (n = 0; n < rtcm->ranges.nentries; n++) {
	    const struct rangesat_t *rsp = &rtcm->ranges.sat[n];
	    (void)snprintf(buf + strlen(buf), buflen - strlen(buf),
			   "{\"ident\":%u,\"udre\":%u,\"issuedata\":%u,\"rangerr\":%0.3f,\"rangerate\":%0.3f},",
			   rsp->ident,
			   rsp->udre,
			   rsp->issuedata,
			   rsp->rangerr,
			   rsp->rangerate);
	}
	if (buf[strlen(buf)-1] == ',')
	    buf[strlen(buf)-1] = '\0';
	(void)strlcat(buf, "]", buflen);
	break;

    case 3:
	if (rtcm->ecef.valid)
	    (void)snprintf(buf + strlen(buf), buflen - strlen(buf),
			   "\"x\":%.2f,\"y\":%.2f,\"z\":%.2f,",
			   rtcm->ecef.x, 
			   rtcm->ecef.y,
			   rtcm->ecef.z);
	break;

    case 4:
	if (rtcm->reference.valid) {
	    (void)snprintf(buf + strlen(buf), buflen - strlen(buf),
			   "\"system\":\"%s\",\"sense\":%1d,\"datum\":\"%s\",\"dx\":%.1f,\"dy\":%.1f,\"dz\":%.1f,",
			   rtcm->reference.system >= NITEMS(navsysnames) 
			   ? "UNKNOWN"
			   : navsysnames[rtcm->reference.system],
			   rtcm->reference.sense,
			   rtcm->reference.datum,
			   rtcm->reference.dx,
			   rtcm->reference.dy,
			   rtcm->reference.dz);
	}
	break;

    case 5:
#define JSON_BOOL(x)	((x)?"true":"false")
	(void)strlcat(buf, "\"satellites\":[", buflen);
    for (n = 0; n < rtcm->conhealth.nentries; n++) {
	const struct consat_t *csp = &rtcm->conhealth.sat[n];
	(void)snprintf(buf + strlen(buf), buflen - strlen(buf),
		       "{\"ident\":%u,\"iodl\":%s,\"health\":%1u,\"snr\":%d,\"health_en\":%s,\"new_data\":%s,\"los_warning\":%s,\"tou\":%u},",
		       csp->ident,
		       JSON_BOOL(csp->iodl),
		       (unsigned)csp->health,
		       csp->snr,
		       JSON_BOOL(csp->health_en),
		       JSON_BOOL(csp->new_data),
		       JSON_BOOL(csp->los_warning),
		       csp->tou);
    }
#undef JSON_BOOL
    if (buf[strlen(buf)-1] == ',')
	buf[strlen(buf)-1] = '\0';
    (void)strlcat(buf, "]", buflen);
    break;

    case 6: 			/* NOP msg */
	break;

    case 7:
	(void)strlcat(buf, "\"satellites\":[", buflen);
	for (n = 0; n < rtcm->almanac.nentries; n++) {
	    const struct station_t *ssp = &rtcm->almanac.station[n];
	    (void)snprintf(buf + strlen(buf), buflen - strlen(buf),
			   "{\"lat\":%.4f,\"lon\":%.4f,\"range\":%u,\"frequency\":%.1f,\"health\":%u,\"station_id\":%u,\"bitrate\":%u},",
			   ssp->latitude,
			   ssp->longitude,
			   ssp->range,
			   ssp->frequency,
			   ssp->health,
			   ssp->station_id,
			   ssp->bitrate);
	}
	if (buf[strlen(buf)-1] == ',')
	    buf[strlen(buf)-1] = '\0';
	(void)strlcat(buf, "]", buflen);
	break;
    case 16:
	(void)snprintf(buf + strlen(buf), buflen - strlen(buf),
		       "\"message\":\"%s\"", json_stringify(buf1, sizeof(buf1), rtcm->message));
	break;

    default:
	(void)strlcat(buf, "\"data\":[", buflen);
	for (n = 0; n < rtcm->length; n++)
	    (void)snprintf(buf + strlen(buf), buflen - strlen(buf),
			   "\"0x%08x\",", rtcm->words[n]);
	if (buf[strlen(buf)-1] == ',')
	    buf[strlen(buf)-1] = '\0';
	(void)strlcat(buf, "]", buflen);
	break;
    }

    if (buf[strlen(buf)-1] == ',')
	buf[strlen(buf)-1] = '\0';
    (void)strlcat(buf, "}\r\n", buflen);
    /*@+mustfreefresh@*/
}
#endif /* defined(RTCM104V2_ENABLE) */

#if defined(AIVDM_ENABLE)

void aivdm_json_dump(const struct ais_t *ais, bool scaled, /*@out@*/char *buf, size_t buflen)
{
    char buf1[JSON_VAL_MAX*2+1];
    char buf2[JSON_VAL_MAX*2+1];
    char buf3[JSON_VAL_MAX*2+1];

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

    static char *ship_type_legends[100] = {
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
	"Wing in ground (WIG) - all ships of this type",
	"Wing in ground (WIG) - Hazardous category A",
	"Wing in ground (WIG) - Hazardous category B",
	"Wing in ground (WIG) - Hazardous category C",
	"Wing in ground (WIG) - Hazardous category D",
	"Wing in ground (WIG) - Reserved for future use",
	"Wing in ground (WIG) - Reserved for future use",
	"Wing in ground (WIG) - Reserved for future use",
	"Wing in ground (WIG) - Reserved for future use",
	"Wing in ground (WIG) - Reserved for future use",
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
	"High speed craft (HSC) - all ships of this type",
	"High speed craft (HSC) - Hazardous category A",
	"High speed craft (HSC) - Hazardous category B",
	"High speed craft (HSC) - Hazardous category C",
	"High speed craft (HSC) - Hazardous category D",
	"High speed craft (HSC) - Reserved for future use",
	"High speed craft (HSC) - Reserved for future use",
	"High speed craft (HSC) - Reserved for future use",
	"High speed craft (HSC) - Reserved for future use",
	"High speed craft (HSC) - No additional information",
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
	"Passenger - all ships of this type",
	"Passenger - Hazardous category A",
	"Passenger - Hazardous category B",
	"Passenger - Hazardous category C",
	"Passenger - Hazardous category D",
	"Passenger - Reserved for future use",
	"Passenger - Reserved for future use",
	"Passenger - Reserved for future use",
	"Passenger - Reserved for future use",
	"Passenger - No additional information",
	"Cargo - all ships of this type",
	"Cargo - Hazardous category A",
	"Cargo - Hazardous category B",
	"Cargo - Hazardous category C",
	"Cargo - Hazardous category D",
	"Cargo - Reserved for future use",
	"Cargo - Reserved for future use",
	"Cargo - Reserved for future use",
	"Cargo - Reserved for future use",
	"Cargo - No additional information",
	"Tanker - all ships of this type",
	"Tanker - Hazardous category A",
	"Tanker - Hazardous category B",
	"Tanker - Hazardous category C",
	"Tanker - Hazardous category D",
	"Tanker - Reserved for future use",
	"Tanker - Reserved for future use",
	"Tanker - Reserved for future use",
	"Tanker - Reserved for future use",
	"Tanker - No additional information",
	"Other Type - all ships of this type",
	"Other Type - Hazardous category A",
	"Other Type - Hazardous category B",
	"Other Type - Hazardous category C",
	"Other Type - Hazardous category D",
	"Other Type - Reserved for future use",
	"Other Type - Reserved for future use",
	"Other Type - Reserved for future use",
	"Other Type - Reserved for future use",
	"Other Type - no additional information",
    };

#define SHIPTYPE_DISPLAY(n) (((n) < (unsigned int)NITEMS(ship_type_legends)) ? ship_type_legends[n] : "INVALID SHIP TYPE")

    static char *station_type_legends[16] = {
	"All types of mobiles",
	"Reserved for future use",
	"All types of Class B mobile stations",
	"SAR airborne mobile station",
	"Aid to Navigation station",
	"Class B shipborne mobile station",
	"Regional use and inland waterways",
	"Regional use and inland waterways",
	"Regional use and inland waterways",
	"Regional use and inland waterways",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
    };

#define STATIONTYPE_DISPLAY(n) (((n) < (unsigned int)NITEMS(ship_type_legends)) ? station_type_legends[n] : "INVALID STATION TYPE")

    static char *navaid_type_legends[] = {
	"Unspcified",
	"Reference point",
	"RACON",
	"Fixed offshore structure",
	"Spare, Reserved for future use.",
	"Light, without sectors",
	"Light, with sectors",
	"Leading Light Front",
	"Leading Light Rear",
	"Beacon, Cardinal N",
	"Beacon, Cardinal E",
	"Beacon, Cardinal S",
	"Beacon, Cardinal W",
	"Beacon, Port hand",
	"Beacon, Starboard hand",
	"Beacon, Preferred Channel port hand",
	"Beacon, Preferred Channel starboard hand",
	"Beacon, Isolated danger",
	"Beacon, Safe water",
	"Beacon, Special mark",
	"Cardinal Mark N",
	"Cardinal Mark E",
	"Cardinal Mark S",
	"Cardinal Mark W",
	"Port hand Mark",
	"Starboard hand Mark",
	"Preferred Channel Port hand",
	"Preferred Channel Starboard hand",
	"Isolated danger",
	"Safe Water",
	"Special Mark",
	"Light Vessel / LANBY / Rigs",
    };

#define NAVAIDTYPE_DISPLAY(n) (((n) < (unsigned int)NITEMS(navaid_type_legends[0])) ? navaid_type_legends[n] : "INVALID NAVAID TYPE")

#define JSON_BOOL(x)	((x)?"true":"false")
    (void)snprintf(buf, buflen, 
		   "{\"class\":\"AIS\",\"type\":%u,\"repeat\":%u,"
		   "\"mmsi\":%u,\"scaled\":%s,", 
		   ais->type, ais->repeat, ais->mmsi, JSON_BOOL(scaled));
    /*@ -formatcode -mustfreefresh @*/
    switch (ais->type) {
    case 1:	/* Position Report */
    case 2:
    case 3:
	if (scaled) {
	    char turnlegend[20];
	    char speedlegend[20];

	    /*
	     * Express turn as nan if not available,
	     * "fastleft"/"fastright" for fast turns.
	     */
	    if (ais->type1.turn == -128)
		(void) strlcpy(turnlegend, "\"nan\"", sizeof(turnlegend));
	    else if (ais->type1.turn == -127)
		(void) strlcpy(turnlegend, "\"fastleft\"", sizeof(turnlegend));
	    else if (ais->type1.turn == 127)
		(void) strlcpy(turnlegend, "\"fastright\"", sizeof(turnlegend));
	    else
		(void)snprintf(turnlegend, sizeof(turnlegend),
			       "%.0f",
			       ais->type1.turn * ais->type1.turn / 4.733);

	    /*
	     * Express speed as nan if not available,
	     * "fast" for fast movers.
	     */
	    if (ais->type1.speed == AIS_SPEED_NOT_AVAILABLE)
		(void) strlcpy(speedlegend, "\"nan\"", sizeof(speedlegend));
	    else if (ais->type1.speed == AIS_SPEED_FAST_MOVER)
		(void) strlcpy(speedlegend, "\"fast\"", sizeof(speedlegend));
	    else
		(void)snprintf(speedlegend, sizeof(speedlegend),
			       "%.1f", ais->type1.speed / 10.0);

	    (void)snprintf(buf+strlen(buf), buflen-strlen(buf),
			   "\"status\":\"%s\",\"turn\":%s,\"speed\":%s,"
			   "\"accuracy\":%s,\"lon\":%.4f,\"lat\":%.4f,"
			   "\"course\":%u,\"heading\":%u,\"second\":%u,"
			   "\"maneuver\":%u,\"raim\":%s,\"radio\":%u}\r\n",
			   nav_legends[ais->type1.status],
			   turnlegend,
			   speedlegend,
			   JSON_BOOL(ais->type1.accuracy),
			   ais->type1.lon / AIS_LATLON_SCALE,
			   ais->type1.lat / AIS_LATLON_SCALE,
			   ais->type1.course,
			   ais->type1.heading,
			   ais->type1.second,
			   ais->type1.maneuver,
			   JSON_BOOL(ais->type1.raim),
			   ais->type1.radio);
	} else {
	    (void)snprintf(buf+strlen(buf), buflen-strlen(buf),
			   "\"status\":%u,\"turn\":%d,\"speed\":%u,"
			   "\"accuracy\":%s,\"lon\":%d,\"lat\":%d,"
			   "\"course\":%u,\"heading\":%u,\"second\":%u,"
			   "\"maneuver\":%u,\"raim\":%s,\"radio\":%u}\r\n",
			   ais->type1.status,
			   ais->type1.turn,
			   ais->type1.speed,
			   JSON_BOOL(ais->type1.accuracy),
			   ais->type1.lon,
			   ais->type1.lat,
			   ais->type1.course,
			   ais->type1.heading,
			   ais->type1.second,
			   ais->type1.maneuver,
			   JSON_BOOL(ais->type1.raim),
			   ais->type1.radio);
	}
	break;
    case 4:	/* Base Station Report */
    case 11:	/* UTC/Date Response */
	if (scaled) {
	    (void)snprintf(buf+strlen(buf), buflen-strlen(buf),
			   "\"timestamp\":\"%4u:%02u:%02uT%02u:%02u:%02uZ\","
			   "\"accuracy\":%s,\"lon\":%.4f,\"lat\":%.4f,"
			   "\"epfd\":\"%s\",\"raim\":%s,\"radio\":%u}\r\n",
			   ais->type4.year,
			   ais->type4.month,
			   ais->type4.day,
			   ais->type4.hour,
			   ais->type4.minute,
			   ais->type4.second,
			   JSON_BOOL(ais->type4.accuracy),
			   ais->type4.lon / AIS_LATLON_SCALE,
			   ais->type4.lat / AIS_LATLON_SCALE,
			   epfd_legends[ais->type4.epfd],
			   JSON_BOOL(ais->type4.raim),
			   ais->type4.radio);
	} else {
	    (void)snprintf(buf+strlen(buf), buflen-strlen(buf),
			   "\"timestamp\":\"%04u-%02u-%02uT%02u:%02u:%02uZ\","
			   "\"accuracy\":%s,\"lon\":%d,\"lat\":%d,"
			   "\"epfd\":%u,\"raim\":%s,\"radio\":%u}\r\n",
			   ais->type4.year,
			   ais->type4.month,
			   ais->type4.day,
			   ais->type4.hour,
			   ais->type4.minute,
			   ais->type4.second,
			   JSON_BOOL(ais->type4.accuracy),
			   ais->type4.lon,
			   ais->type4.lat,
			   ais->type4.epfd,
			   JSON_BOOL(ais->type4.raim),
			   ais->type4.radio);
	}
	break;
    case 5:	/* Ship static and voyage related data */
	if (scaled) {
	    (void)snprintf(buf+strlen(buf), buflen-strlen(buf),
			   "\"imo\":%u,\"ais_version\":%u,\"callsign\":\"%s\","
			   "\"shipname\":\"%s\",\"shiptype\":\"%s\","
			   "\"to_bow\":%u,\"to_stern\":%u,\"to_port\":%u,"
			   "\"to_starboard\":%u,\"epfd\":\"%s\","
			   "\"eta\":\"%02u-%02uT%02u:%02uZ\","
			   "\"draught\":%.1f,\"destination\":\"%s\","
			   "\"dte\":%u}\r\n",
			   ais->type5.imo,
			   ais->type5.ais_version,
			   json_stringify(buf1, sizeof(buf1), ais->type5.callsign),
			   json_stringify(buf2, sizeof(buf2), ais->type5.shipname),
			   SHIPTYPE_DISPLAY(ais->type5.shiptype),
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
			   json_stringify(buf3, sizeof(buf3), ais->type5.destination),
			   ais->type5.dte);
	} else {
	    (void)snprintf(buf+strlen(buf), buflen-strlen(buf),
			   "\"imo\":%u,\"ais_version\":%u,\"callsign\":\"%s\","
			   "\"shipname\":\"%s\",\"shiptype\":%u,"
			   "\"to_bow\":%u,\"to_stern\":%u,\"to_port\":%u,"
			   "\"to_starboard\":%u,\"epfd\":%u,"
			   "\"eta\":\"%02u-%02uT%02u:%02uZ\","
			   "\"draught\":%u,\"destination\":\"%s\","
			   "\"dte\":%u}\r\n",
			   ais->type5.imo,
			   ais->type5.ais_version,
			   json_stringify(buf1, sizeof(buf1), ais->type5.callsign),
			   json_stringify(buf2, sizeof(buf2), ais->type5.shipname),
			   ais->type5.shiptype,
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
			   json_stringify(buf3, sizeof(buf3), ais->type5.destination),
			   ais->type5.dte);
	}
	break;
    case 6:	/* Binary Message */
	(void)snprintf(buf+strlen(buf), buflen-strlen(buf),
		       "\"seqno\":%u,\"dest_mmsi\":%u,"
		       "\"retransmit\":%s,\"app_id\":%u,"
		       "\"data\":\"%zd:%s\"}\r\n",
		       ais->type6.seqno,
		       ais->type6.dest_mmsi,
		       JSON_BOOL(ais->type6.retransmit),
		       ais->type6.app_id,
		       ais->type6.bitcount,
		       gpsd_hexdump(ais->type6.bitdata,
				       (ais->type6.bitcount+7)/8));
	break;
    case 7:	/* Binary Acknowledge */
    case 13:	/* Safety Related Acknowledge */
	(void)snprintf(buf+strlen(buf), buflen-strlen(buf),
		       "\"mmsi1\":%u,\"mmsi2\":%u,\"mmsi3\":%u,\"mmsi4\":%u}\r\n",  
		       ais->type7.mmsi1,
		       ais->type7.mmsi2,
		       ais->type7.mmsi3,
		       ais->type7.mmsi4);
	break;
    case 8:	/* Binary Broadcast Message */
	    (void)snprintf(buf+strlen(buf), buflen-strlen(buf),
			   "\"app_id\":%u,\"data\":\"%zd:%s\"}\r\n",  
			  ais->type8.app_id,
			  ais->type8.bitcount,
			  gpsd_hexdump(ais->type8.bitdata,
				       (ais->type8.bitcount+7)/8));
	break;
    case 9:	/* Standard SAR Aircraft Position Report */
	if (scaled) {
	    char altlegend[20];
	    char speedlegend[20];

	    /*
	     * Express altitude as nan if not available,
	     * "high" for above the reporting ceiling.
	     */
	    if (ais->type9.alt == AIS_ALT_NOT_AVAILABLE)
		(void) strlcpy(altlegend, "\"nan\"", sizeof(altlegend));
	    else if (ais->type9.alt == AIS_ALT_HIGH)
		(void) strlcpy(altlegend, "\"high\"", sizeof(altlegend));
	    else
		(void)snprintf(altlegend, sizeof(altlegend),
			       "%.1f", ais->type9.alt / 10.0);

	    /*
	     * Express speed as nan if not available,
	     * "high" for above the reporting ceiling.
	     */
	    if (ais->type9.speed == AIS_SAR_SPEED_NOT_AVAILABLE)
		(void) strlcpy(speedlegend, "\"nan\"", sizeof(speedlegend));
	    else if (ais->type9.speed == AIS_SAR_FAST_MOVER)
		(void) strlcpy(speedlegend, "\"fast\"", sizeof(speedlegend));
	    else
		(void)snprintf(speedlegend, sizeof(speedlegend),
			       "%.1f", ais->type1.speed / 10.0);

	    (void)snprintf(buf+strlen(buf), buflen-strlen(buf),
			   "\"alt\":%s,\"speed\":%s,\"accuracy\":%s,"
			   "\"lon\":%.4f,\"lat\":%.4f,\"course\":%.1f,"
			   "\"second\":%u,\"regional\":%u,\"dte\":%u,"
			   "\"raim\":%s,\"radio\":%u}\r\n",
			   altlegend,
			   speedlegend,
			   JSON_BOOL(ais->type9.accuracy),
			   ais->type9.lon / AIS_LATLON_SCALE,
			   ais->type9.lat / AIS_LATLON_SCALE,
			   ais->type9.course / 10.0,
			   ais->type9.second,
			   ais->type9.regional,
			   ais->type9.dte,
			   JSON_BOOL(ais->type9.raim),
			   ais->type9.radio);
	} else {
	    (void)snprintf(buf+strlen(buf), buflen-strlen(buf),
			   "\"alt\":%u,\"speed\":%u,\"accuracy\":%s,"
			   "\"lon\":%d,\"lat\":%d,\"course\":%u,"
			   "\"second\":%u,\"regional\":%u,\"dte\":%u,"
			   "\"raim\":%s,\"radio\":%u}\r\n",
			   ais->type9.alt,
			   ais->type9.speed,
			   JSON_BOOL(ais->type9.accuracy),
			   ais->type9.lon,
			   ais->type9.lat,
			   ais->type9.course,
			   ais->type9.second,
			   ais->type9.regional,
			   ais->type9.dte,
			   JSON_BOOL(ais->type9.raim),
			   ais->type9.radio);
	}
	break;
    case 10:	/* UTC/Date Inquiry */
	    (void)snprintf(buf+strlen(buf), buflen-strlen(buf),
			   "\"dest_mmsi\":%u}\r\n",  
			  ais->type10.dest_mmsi);
	break;
    case 12:	/* Safety Related Message */
	    (void)snprintf(buf+strlen(buf), buflen-strlen(buf),
			   "\"seqno\":%u,\"dest_mmsi\":%u,\"retransmit\":%s,\"text\":\"%s\"}\r\n",  
			   ais->type12.seqno,
			   ais->type12.dest_mmsi,
			   JSON_BOOL(ais->type12.retransmit),
			   json_stringify(buf1, sizeof(buf1), ais->type12.text));
	break;
    case 14:	/* Safety Related Broadcast Message */
	(void)snprintf(buf+strlen(buf), buflen-strlen(buf),
		       "\"text\":\"%s\"}\r\n",
		       json_stringify(buf1, sizeof(buf1), ais->type14.text));
	break;
    case 15:	/* Interrogation */
	(void)snprintf(buf+strlen(buf), buflen-strlen(buf),
		       "\"mmsi1\":%u,\"type1_1\":%u,\"offset1_1\":%u,"
		       "\"type1_2\":%u,\"offset1_2\":%u,\"mmsi2\":%u,"
		       "\"type2_1\":%u,\"offset2_1\":%u}\r\n",
		      ais->type15.mmsi1,
		      ais->type15.type1_1,
		      ais->type15.offset1_1,
		      ais->type15.type1_2,
		      ais->type15.offset1_2,
		      ais->type15.mmsi2,
		      ais->type15.type2_1,
		      ais->type15.offset2_1);
	break;
    case 16:
	(void)snprintf(buf+strlen(buf), buflen-strlen(buf),
		       "\"mmsi1\":%u,\"offset1\":%u,\"increment1\":%u,"
		       "\"mmsi2\":%u,\"offset2\":%u,\"increment2\":%u}\r\n",
		       ais->type16.mmsi1,
		       ais->type16.offset1,
		       ais->type16.increment1,
		       ais->type16.mmsi2,
		       ais->type16.offset2,
		       ais->type16.increment2);
	break;
    case 17:
	if (scaled) {
	    (void)snprintf(buf+strlen(buf), buflen-strlen(buf),
			   "\"lon\":%.1f,\"lat\":%.1f,\"data\":\"%zd:%s\"}\r\n",
			  ais->type17.lon / AIS_GNSS_LATLON_SCALE,
			  ais->type17.lat / AIS_GNSS_LATLON_SCALE,
			  ais->type17.bitcount,
			  gpsd_hexdump(ais->type17.bitdata,
				       (ais->type17.bitcount+7)/8));
	} else {
	    (void)snprintf(buf+strlen(buf), buflen-strlen(buf),
			   "\"lon\":%d,\"lat\":%d,\"data\":\"%zd:%s\"}\r\n",
			  ais->type17.lon,
			  ais->type17.lat,
			  ais->type17.bitcount,
			  gpsd_hexdump(ais->type17.bitdata,
				       (ais->type17.bitcount+7)/8));
	}
	break;
    case 18:
	if (scaled) {
	    (void)snprintf(buf+strlen(buf), buflen-strlen(buf),
			   "\"reserved\":%u,\"speed\":%.1f,\"accuracy\":%s,"
			   "\"lon\":%.4f,\"lat\":%.4f,\"course\":%.1f,"
			   "\"heading\":%u,\"second\":%u,\"regional\":%u,"
			   "\"cs\":%s,\"display\":%s,\"dsc\":%s,\"band\":%s,"
			   "\"msg22\":%s,\"raim\":%s,\"radio\":%u}\r\n",
			   ais->type18.reserved,
			   ais->type18.speed / 10.0,
			   JSON_BOOL(ais->type18.accuracy),
			   ais->type18.lon / AIS_LATLON_SCALE,
			   ais->type18.lat / AIS_LATLON_SCALE,
			   ais->type18.course / 10.0,
			   ais->type18.heading,
			   ais->type18.second,
			   ais->type18.regional,
			   JSON_BOOL(ais->type18.cs),
			   JSON_BOOL(ais->type18.display),
			   JSON_BOOL(ais->type18.dsc),
			   JSON_BOOL(ais->type18.band),
			   JSON_BOOL(ais->type18.msg22),
			   JSON_BOOL(ais->type18.raim),
			   ais->type18.radio);
	} else {
	    (void)snprintf(buf+strlen(buf), buflen-strlen(buf),
			   "\"reserved\":%u,\"speed\":%u,\"accuracy\":%s,"
			   "\"lon\":%d,\"lat\":%d,\"course\":%u,"
			   "\"heading\":%u,\"second\":%u,\"regional\":%u,"
			   "\"cs\":%s,\"display\":%s,\"dsc\":%s,\"band\":%s,"
			   "\"msg22\":%s,\"raim\":%s,\"radio\":%u}\r\n",
			   ais->type18.reserved,
			   ais->type18.speed,
			   JSON_BOOL(ais->type18.accuracy),
			   ais->type18.lon,
			   ais->type18.lat,
			   ais->type18.course,
			   ais->type18.heading,
			   ais->type18.second,
			   ais->type18.regional,
			   JSON_BOOL(ais->type18.cs),
			   JSON_BOOL(ais->type18.display),
			   JSON_BOOL(ais->type18.dsc),
			   JSON_BOOL(ais->type18.band),
			   JSON_BOOL(ais->type18.msg22),
			   JSON_BOOL(ais->type18.raim),
			   ais->type18.radio);
	}
	break;
    case 19:
	if (scaled) {
	    (void)snprintf(buf+strlen(buf), buflen-strlen(buf),
			   "\"reserved\":%u,\"speed\":%.1f,\"accuracy\":%s,"
			   "\"lon\":%.4f,\"lat\":%.4f,\"course\":%.1f,"
			   "\"heading\":%u,\"second\":%u,\"regional\":%u,"
			   "\"shipname\":\"%s\",\"shiptype\":\"%s\","
			   "\"to_bow\":%u,\"to_stern\":%u,\"to_port\":%u,"
			   "\"to_starboard\":%u,\"epfd\":\"%s\",\"raim\":%s,"
			   "\"dte\":%u,\"assigned\":%s}\r\n",
			   ais->type19.reserved,
			   ais->type19.speed / 10.0,
			   JSON_BOOL(ais->type19.accuracy),
			   ais->type19.lon / AIS_LATLON_SCALE,
			   ais->type19.lat / AIS_LATLON_SCALE,
			   ais->type19.course / 10.0,
			   ais->type19.heading,
			   ais->type19.second,
			   ais->type19.regional,
			   ais->type19.shipname,
			   SHIPTYPE_DISPLAY(ais->type19.shiptype),
			   ais->type19.to_bow,
			   ais->type19.to_stern,
			   ais->type19.to_port,
			   ais->type19.to_starboard,
			   epfd_legends[ais->type19.epfd],
			   JSON_BOOL(ais->type19.raim),
			   ais->type19.dte,
			   JSON_BOOL(ais->type19.assigned));
	} else {
	    (void)snprintf(buf+strlen(buf), buflen-strlen(buf),
			   "\"reserved\":%u,\"speed\":%u,\"accuracy\":%s,"
			   "\"lon\":%d,\"lat\":%d,\"course\":%u,"
			   "\"heading\":%u,\"second\":%u,\"regional\":%u,"
			   "\"shipname\":\"%s\",\"shiptype\":%u,"
			   "\"to_bow\":%u,\"to_stern\":%u,\"to_port\":%u,"
			   "\"to_starboard\":%u,\"epfd\":%u,\"raim\":%s,"
			   "\"dte\":%u,\"assigned\":%s}\r\n",
			   ais->type19.reserved,
			   ais->type19.speed,
			   JSON_BOOL(ais->type19.accuracy),
			   ais->type19.lon,
			   ais->type19.lat,
			   ais->type19.course,
			   ais->type19.heading,
			   ais->type19.second,
			   ais->type19.regional,
			   ais->type19.shipname,
			   ais->type19.shiptype,
			   ais->type19.to_bow,
			   ais->type19.to_stern,
			   ais->type19.to_port,
			   ais->type19.to_starboard,
			   ais->type19.epfd,
			   JSON_BOOL(ais->type19.raim),
			   ais->type19.dte,
			   JSON_BOOL(ais->type19.assigned));
	}
	break;
    case 20:	/* Data Link Management Message */
	(void)snprintf(buf+strlen(buf), buflen-strlen(buf),
		       "\"offset1\":%u,\"number1\":%u,"
		       "\"timeout1\":%u,\"increment1\":%u,"
		       "\"offset2\":%u,\"number2\":%u,"
		       "\"timeout2\":%u,\"increment2\":%u,"
		       "\"offset3\":%u,\"number3\":%u,"
		       "\"timeout3\":%u,\"increment3\":%u,"
		       "\"offset4\":%u,\"number4\":%u,"
		       "\"timeout4\":%u,\"increment4\":%u}\r\n",
		       ais->type20.offset1,
		       ais->type20.number1,
		       ais->type20.timeout1,
		       ais->type20.increment1,
		       ais->type20.offset2,
		       ais->type20.number2,
		       ais->type20.timeout2,
		       ais->type20.increment2,
		       ais->type20.offset3,
		       ais->type20.number3,
		       ais->type20.timeout3,
		       ais->type20.increment3,
		       ais->type20.offset4,
		       ais->type20.number4,
		       ais->type20.timeout4,
		       ais->type20.increment4);
	break;
    case 21: /* Aid to Navigation */
	if (scaled) {
	    (void)snprintf(buf+strlen(buf), buflen-strlen(buf),
			   "\"aid_type\":\"%s\",\"name\":\"%s\",\"lon\":%.4f,"
			   "\"lat\":%.4f,\"accuracy\":%s,\"to_bow\":%u,"
			   "\"to_stern\":%u,\"to_port\":%u,"
			   "\"to_starboard\":%u,\"epfd\":\"%s\","
			   "\"second\":%u,\"regional\":%u,"
			   "\"off_position\":%s,\"raim\":%s,"
			   "\"virtual_aid\":%s}\r\n",
			   NAVAIDTYPE_DISPLAY(ais->type21.aid_type),
			   json_stringify(buf1, sizeof(buf1), ais->type21.name),
			   ais->type21.lon / AIS_LATLON_SCALE,
			   ais->type21.lat / AIS_LATLON_SCALE,
			   JSON_BOOL(ais->type21.accuracy),
			   ais->type21.to_bow,
			   ais->type21.to_stern,
			   ais->type21.to_port,
			   ais->type21.to_starboard,
			   epfd_legends[ais->type21.epfd],
			   ais->type21.second,
			   ais->type21.regional,
			   JSON_BOOL(ais->type21.off_position),
			   JSON_BOOL(ais->type21.raim),
			   JSON_BOOL(ais->type21.virtual_aid));
	} else {
	    (void)snprintf(buf+strlen(buf), buflen-strlen(buf),
			   "\"aid_type\":%u,\"name\":\"%s\",\"accuracy\":%s,"
			   "\"lon\":%d,\"lat\":%d,\"to_bow\":%u,"
			   "\"to_stern\":%u,\"to_port\":%u,\"to_starboard\":%u,"
			   "\"epfd\":%u,\"second\":%u,\"regional\":%u,"
			   "\"off_position\":%s,\"raim\":%s,"
			   "\"virtual_aid\":%s}\r\n",
			   ais->type21.aid_type,
			   ais->type21.name,
			   JSON_BOOL(ais->type21.accuracy),
			   ais->type21.lon,
			   ais->type21.lat,
			   ais->type21.to_bow,
			   ais->type21.to_stern,
			   ais->type21.to_port,
			   ais->type21.to_starboard,
			   ais->type21.epfd,
			   ais->type21.second,
			   ais->type21.regional,
			   JSON_BOOL(ais->type21.off_position),
			   JSON_BOOL(ais->type21.raim),
			   JSON_BOOL(ais->type21.virtual_aid));
	}
	break;
    case 22:	/* Channel Management */
	(void)snprintf(buf+strlen(buf), buflen-strlen(buf),
		       "\"channel_a\":%u,\"channel_b\":%u,"
		       "\"txrx\":%u,\"power\":%s,",
		       ais->type22.channel_a,
		       ais->type22.channel_b,
		       ais->type22.txrx,
		       JSON_BOOL(ais->type22.power));
	if (ais->type22.addressed) {
	    (void)snprintf(buf+strlen(buf), buflen-strlen(buf),
			   "\"dest1\":%u,\"dest2\":%u",
			   ais->type22.mmsi.dest1, ais->type22.mmsi.dest2);
	} else if (scaled) {
	    (void)snprintf(buf+strlen(buf), buflen-strlen(buf),
			   "\"ne_lon\":\"%f\",\"ne_lat\":\"%f\","
			   "\"sw_lon\":\"%f\",\"sw_lat\":\"%f\",",
			   ais->type22.area.ne_lon / AIS_CHANNEL_LATLON_SCALE,
			   ais->type22.area.ne_lat / AIS_CHANNEL_LATLON_SCALE,
			   ais->type22.area.sw_lon / AIS_CHANNEL_LATLON_SCALE,
			   ais->type22.area.sw_lat / AIS_CHANNEL_LATLON_SCALE);
	} else {
	    (void)snprintf(buf+strlen(buf), buflen-strlen(buf),
			   "\"ne_lon\":%d,\"ne_lat\":%d,"
			   "\"sw_lon\":%d,\"sw_lat\":%d,",
			   ais->type22.area.ne_lon,
			   ais->type22.area.ne_lat,
			   ais->type22.area.sw_lon,
			   ais->type22.area.sw_lat);
	}
	(void)snprintf(buf+strlen(buf), buflen-strlen(buf),
		       "\"addressed\":%s,\"band_a\":%s,"
		       "\"band_b\":%s,\"zonesize\":%u}\r\n",
		       JSON_BOOL(ais->type22.addressed),
		       JSON_BOOL(ais->type22.band_a),
		       JSON_BOOL(ais->type22.band_b),
		       ais->type22.zonesize);
	break;
    case 23:	/* Group Assignment Command */
	if (scaled) {
	    (void)snprintf(buf+strlen(buf), buflen-strlen(buf),
			   "\"ne_lon\":\"%f\",\"ne_lat\":\"%f\","
			   "\"sw_lon\":\"%f\",\"sw_lat\":\"%f\","
			   "\"stationtype\":\"%s\",\"shiptype\":\"%s\","
			   "\"interval\":%u,\"quiet\":%u}\r\n",
			   ais->type23.ne_lon / AIS_CHANNEL_LATLON_SCALE,
			   ais->type23.ne_lat / AIS_CHANNEL_LATLON_SCALE,
			   ais->type23.sw_lon / AIS_CHANNEL_LATLON_SCALE,
			   ais->type23.sw_lat / AIS_CHANNEL_LATLON_SCALE,
			   STATIONTYPE_DISPLAY(ais->type23.stationtype),
			   SHIPTYPE_DISPLAY(ais->type23.shiptype),
			   ais->type23.interval,
			   ais->type23.quiet);
	} else {
	    (void)snprintf(buf+strlen(buf), buflen-strlen(buf),
			   "\"ne_lon\":%d,\"ne_lat\":%d,"
			   "\"sw_lon\":%d,\"sw_lat\":%d,"
			   "\"stationtype\":%u,\"shiptype\":%u,"
			   "\"interval\":%u,\"quiet\":%u}\r\n",
			   ais->type23.ne_lon,
			   ais->type23.ne_lat,
			   ais->type23.sw_lon,
			   ais->type23.sw_lat,
			   ais->type23.stationtype,
			   ais->type23.shiptype,
			   ais->type23.interval,
			   ais->type23.quiet);
	}
	break;
    case 24:	/* Class B CS Static Data Report */
	(void)snprintf(buf+strlen(buf), buflen-strlen(buf), 
		       "\"shipname\":\"%s\",",
		       json_stringify(buf1,sizeof(buf1), ais->type24.shipname));
	if (scaled) {
	    (void)snprintf(buf+strlen(buf), buflen-strlen(buf), 
			  "\"shiptype\":\"%s\",",
			  SHIPTYPE_DISPLAY(ais->type24.shiptype));
	} else {
	    (void)snprintf(buf+strlen(buf), buflen-strlen(buf),
			  "\"shiptype\":%u,",
			  ais->type24.shiptype);
	}
	(void)snprintf(buf+strlen(buf), buflen-strlen(buf),
		      "\"vendorid\":\"%s\",\"callsign\":\"%s\",",
		      ais->type24.vendorid,
		      ais->type24.callsign);
	if (AIS_AUXILIARY_MMSI(ais->mmsi)) {
	    (void)snprintf(buf+strlen(buf), buflen-strlen(buf),
			  "mothership_\"mmsi\":%u}\r\n",
			  ais->type24.mothership_mmsi);
	} else {
	    (void)snprintf(buf+strlen(buf), buflen-strlen(buf),
			  "\"to_bow\":%u,\"to_stern\":%u,"
			  "\"to_port\":%u,\"to_starboard\":%u}\r\n",
			  ais->type24.dim.to_bow,
			  ais->type24.dim.to_stern,
			  ais->type24.dim.to_port,
			  ais->type24.dim.to_starboard);
	}
	break;
    default:
	if (buf[strlen(buf)-1] == ',')
	    buf[strlen(buf)-1] = '\0';
	(void) strlcat(buf, "}\r\n", buflen);
	break;
    }
    /*@ +formatcode +mustfreefresh @*/
#undef SHOW_BOOL
}

#endif /* defined(AIVDM_ENABLE) */

int json_device_read(const char *buf, 
		     /*@out@*/struct devconfig_t *dev, 
		     /*@null@*/const char **endptr)
{
    /*@ -fullinitblock @*/
    const struct json_attr_t json_attrs_device[] = {
	{"class",      t_check,      .dflt.check = "DEVICE"},
	
        {"path",       t_string,     .addr.string  = dev->path,
	                                .len = sizeof(dev->path)},
	{"activated",  t_real,       .addr.real = &dev->activated},
	{"flags",      t_integer,    .addr.integer = &dev->flags},
	{"driver",     t_string,     .addr.string  = dev->driver,
	                                .len = sizeof(dev->driver)},
	{"subtype",    t_string,     .addr.string  = dev->subtype,
	                                .len = sizeof(dev->subtype)},
	{"native",     t_integer,    .addr.integer = &dev->driver_mode,
				        .dflt.integer = DEVDEFAULT_NATIVE},
	{"bps",	       t_uinteger,   .addr.uinteger = &dev->baudrate,
				        .dflt.uinteger = DEVDEFAULT_BPS},
	{"parity",     t_character,  .addr.character = &dev->parity,
                                        .dflt.character = DEVDEFAULT_PARITY},
	{"stopbits",   t_uinteger,   .addr.uinteger = &dev->stopbits,
				        .dflt.uinteger = DEVDEFAULT_STOPBITS},
	{"cycle",      t_real,       .addr.real = &dev->cycle,
				        .dflt.real = NAN},
	{"mincycle",   t_real,       .addr.real = &dev->mincycle,
				        .dflt.real = NAN},
	{NULL},
    };
    /*@ +fullinitblock @*/
    int status;

    status = json_read_object(buf, json_attrs_device, endptr);
    if (status != 0)
	return status;

    return 0;
}

int json_watch_read(const char *buf, 
		    /*@out@*/struct policy_t *ccp,
		    /*@null@*/const char **endptr)
{
    /*@ -fullinitblock @*/
    struct json_attr_t chanconfig_attrs[] = {
	{"class",          t_check,    .dflt.check = "WATCH"},
	
	{"enable",         t_boolean,  .addr.boolean = &ccp->watcher,
                                          .dflt.boolean = true},
	{"json",           t_boolean,  .addr.boolean = &ccp->json,
                                          .nodefault = true},
	{"raw",	           t_integer,  .addr.integer = &ccp->raw,
	                                  .nodefault = true},
	{"nmea",	   t_boolean,  .addr.boolean = &ccp->nmea,
	                                  .nodefault = true},
	{"scaled",         t_boolean,  .addr.boolean = &ccp->scaled},
	{"timing",         t_boolean,  .addr.boolean = &ccp->timing},
	{"device",         t_string,   .addr.string = ccp->devpath,
	                                  .len = sizeof(ccp->devpath)},
	{NULL},
    };
    /*@ +fullinitblock @*/
    int status;

    status = json_read_object(buf, chanconfig_attrs, endptr);
    return status;
}

/* gpsd_json.c ends here */
