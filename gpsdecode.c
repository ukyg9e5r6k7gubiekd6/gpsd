/*
 * This file is Copyright (c) 2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>
#ifndef S_SPLINT_S
#include <unistd.h>
#endif /* S_SPLINT_S */

#include "gpsd.h"
#include "gps_json.h"

static int verbose = 0;
static bool scaled = true;
static bool json = true;

/**************************************************************************
 *
 * Generic machinery
 *
 **************************************************************************/

void gpsd_report(int errlevel, const char *fmt, ...)
/* assemble command in printf(3) style, use stderr or syslog */
{
    if (errlevel <= verbose) {
	char buf[BUFSIZ];
	va_list ap;

	(void)strlcpy(buf, "gpsdecode: ", BUFSIZ);
	va_start(ap, fmt);
	(void)vsnprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), fmt,
			ap);
	va_end(ap);
	(void)fputs(buf, stdout);
    }
}

#ifdef AIVDM_ENABLE
static void aivdm_csv_dump(struct ais_t *ais, char *buf, size_t buflen)
{
    (void)snprintf(buf, buflen, "%u|%u|%09u|", ais->type, ais->repeat,
		   ais->mmsi);
    /*@ -formatcode @*/
    switch (ais->type) {
    case 1:			/* Position Report */
    case 2:
    case 3:
	(void)snprintf(buf + strlen(buf), buflen - strlen(buf),
		       "%u|%d|%u|%u|%d|%d|%u|%u|%u|0x%x|%u|0x%x",
		       ais->type1.status,
		       ais->type1.turn,
		       ais->type1.speed,
		       (uint) ais->type1.accuracy,
		       ais->type1.lon,
		       ais->type1.lat,
		       ais->type1.course,
		       ais->type1.heading,
		       ais->type1.second,
		       ais->type1.maneuver,
		       (uint) ais->type1.raim, ais->type1.radio);
	break;
    case 4:			/* Base Station Report */
    case 11:			/* UTC/Date Response */
	(void)snprintf(buf + strlen(buf), buflen - strlen(buf),
		       "%04u-%02u-%02uT%02u:%02u:%02uZ|%u|%d|%d|%u|%u|0x%x",
		       ais->type4.year,
		       ais->type4.month,
		       ais->type4.day,
		       ais->type4.hour,
		       ais->type4.minute,
		       ais->type4.second,
		       (uint) ais->type4.accuracy,
		       ais->type4.lon,
		       ais->type4.lat,
		       ais->type4.epfd,
		       (uint) ais->type4.raim, ais->type4.radio);
	break;
    case 5:			/* Ship static and voyage related data */
	(void)snprintf(buf + strlen(buf), buflen - strlen(buf),
		       "%u|%u|%s|%s|%u|%u|%u|%u|%u|%u|%02u-%02uT%02u:%02uZ|%u|%s|%u",
		       ais->type5.imo,
		       ais->type5.ais_version,
		       ais->type5.callsign,
		       ais->type5.shipname,
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
		       ais->type5.destination, ais->type5.dte);
	break;
    case 6:			/* Binary Message */
	(void)snprintf(buf + strlen(buf), buflen - strlen(buf),
		       "%u|%u|%u|%u|%u|%zd:%s",
		       ais->type6.seqno,
		       ais->type6.dest_mmsi,
		       (uint) ais->type6.retransmit,
		       ais->type6.dac,
		       ais->type6.fid,
		       ais->type6.bitcount,
		       gpsd_hexdump(ais->type6.bitdata,
				    (ais->type6.bitcount + 7) / 8));
	break;
    case 7:			/* Binary Acknowledge */
    case 13:			/* Safety Related Acknowledge */
	(void)snprintf(buf + strlen(buf), buflen - strlen(buf),
		       "%u|%u|%u|%u",
		       ais->type7.mmsi1,
		       ais->type7.mmsi2, ais->type7.mmsi3, ais->type7.mmsi4);
	break;
    case 8:			/* Binary Broadcast Message */
	(void)snprintf(buf + strlen(buf), buflen - strlen(buf),
		       "%u|%u|%zd:%s",
		       ais->type8.dac,
		       ais->type8.fid,
		       ais->type8.bitcount,
		       gpsd_hexdump(ais->type8.bitdata,
				    (ais->type8.bitcount + 7) / 8));
	break;
    case 9:
	(void)snprintf(buf + strlen(buf), buflen - strlen(buf),
		       "%u|%u|%u|%d|%d|%u|%u|0x%x|%u|%u|0x%x",
		       ais->type9.alt,
		       ais->type9.speed,
		       (uint) ais->type9.accuracy,
		       ais->type9.lon,
		       ais->type9.lat,
		       ais->type9.course,
		       ais->type9.second,
		       ais->type9.regional,
		       ais->type9.dte,
		       (uint) ais->type9.raim, ais->type9.radio);
	break;
    case 10:			/* UTC/Date Inquiry */
	(void)snprintf(buf + strlen(buf), buflen - strlen(buf),
		       "%u", ais->type10.dest_mmsi);
	break;
    case 12:			/* Safety Related Message */
	(void)snprintf(buf + strlen(buf), buflen - strlen(buf),
		       "%u|%u|%u|%s",
		       ais->type12.seqno,
		       ais->type12.dest_mmsi,
		       (uint) ais->type12.retransmit, ais->type12.text);
	break;
    case 14:			/* Safety Related Broadcast Message */
	(void)snprintf(buf + strlen(buf), buflen - strlen(buf),
		       "%s", ais->type14.text);
	break;
    case 15:
	(void)snprintf(buf + strlen(buf), buflen - strlen(buf),
		       "%u|%u|%u|%u|%u|%u|%u|%u",
		       ais->type15.mmsi1,
		       ais->type15.type1_1,
		       ais->type15.offset1_1,
		       ais->type15.type1_2,
		       ais->type15.offset1_2,
		       ais->type15.mmsi2,
		       ais->type15.type2_1, ais->type15.offset2_1);
	break;
    case 16:
	(void)snprintf(buf + strlen(buf), buflen - strlen(buf),
		       "%u|%u|%u|%u|%u|%u",
		       ais->type16.mmsi1,
		       ais->type16.offset1,
		       ais->type16.increment1,
		       ais->type16.mmsi2,
		       ais->type16.offset2, ais->type16.increment2);
	break;
    case 17:
	(void)snprintf(buf + strlen(buf), buflen - strlen(buf),
		       "%d|%d|%zd:%s",
		       ais->type17.lon,
		       ais->type17.lat,
		       ais->type17.bitcount,
		       gpsd_hexdump(ais->type17.bitdata,
				    (ais->type17.bitcount + 7) / 8));
	break;
    case 18:
	(void)snprintf(buf + strlen(buf), buflen - strlen(buf),
		       "%u|%u|%u|%d|%d|%u|%u|%u|0x%x|%u|%u|%u|%u|%u|%u|0x%x",
		       ais->type18.reserved,
		       ais->type18.speed,
		       (uint) ais->type18.accuracy,
		       ais->type18.lon,
		       ais->type18.lat,
		       ais->type18.course,
		       ais->type18.heading,
		       ais->type18.second,
		       ais->type18.regional,
		       (uint) ais->type18.cs,
		       (uint) ais->type18.display,
		       (uint) ais->type18.dsc,
		       (uint) ais->type18.band,
		       (uint) ais->type18.msg22,
		       (uint) ais->type18.raim, ais->type18.radio);
	break;
    case 19:
	(void)snprintf(buf + strlen(buf), buflen - strlen(buf),
		       "%u|%u|%u|%d|%d|%u|%u|%u|0x%x|%s|%u|%u|%u|%u|%u|%u|%u|%u|%u",
		       ais->type19.reserved,
		       ais->type19.speed,
		       (uint) ais->type19.accuracy,
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
		       (uint) ais->type19.raim,
		       ais->type19.dte, (uint) ais->type19.assigned);
	break;
    case 20:			/* Data Link Management Message */
	(void)snprintf(buf + strlen(buf), buflen - strlen(buf),
		       "%u|%u|%u|%u|%u|%u|%u|%u|%u|%u|%u|%u|%u|%u|%u|%u",
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
		       ais->type20.timeout4, ais->type20.increment4);
	break;
    case 21:			/* Aid to Navigation */
	(void)snprintf(buf + strlen(buf), buflen - strlen(buf),
		       "%u|%s|%u|%d|%d|%u|%u|%u|%u|%u|%u|%u|0x%x|%u|%u",
		       ais->type21.aid_type,
		       ais->type21.name,
		       (uint) ais->type21.accuracy,
		       ais->type21.lon,
		       ais->type21.lat,
		       ais->type21.to_bow,
		       ais->type21.to_stern,
		       ais->type21.to_port,
		       ais->type21.to_starboard,
		       ais->type21.epfd,
		       ais->type21.second,
		       ais->type21.regional,
		       (uint) ais->type21.off_position,
		       (uint) ais->type21.raim,
		       (uint) ais->type21.virtual_aid);
	break;
    case 22:			/* Channel Management */
	if (!ais->type22.addressed)
	    (void)snprintf(buf + strlen(buf), buflen - strlen(buf),
			   "%u|%u|%u|%u|%d|%d|%d|%d|%u|%u|%u|%u",
			   ais->type22.channel_a,
			   ais->type22.channel_b,
			   ais->type22.txrx,
			   (uint) ais->type22.power,
			   ais->type22.area.ne_lon,
			   ais->type22.area.ne_lat,
			   ais->type22.area.sw_lon,
			   ais->type22.area.sw_lat,
			   (uint) ais->type22.addressed,
			   (uint) ais->type22.band_a,
			   (uint) ais->type22.band_b, ais->type22.zonesize);
	else
	    (void)snprintf(buf + strlen(buf), buflen - strlen(buf),
			   "%u|%u|%u|%u|%u|%u|%u|%u|%u|%u",
			   ais->type22.channel_a,
			   ais->type22.channel_b,
			   ais->type22.txrx,
			   (uint) ais->type22.power,
			   ais->type22.mmsi.dest1,
			   ais->type22.mmsi.dest2,
			   (uint) ais->type22.addressed,
			   (uint) ais->type22.band_a,
			   (uint) ais->type22.band_b, ais->type22.zonesize);
	break;
    case 23:			/* Group Management Command */
	(void)snprintf(buf + strlen(buf), buflen - strlen(buf),
		       "%d|%d|%d|%d|%u|%u|%u|%u|%u",
		       ais->type23.ne_lon,
		       ais->type23.ne_lat,
		       ais->type23.sw_lon,
		       ais->type23.sw_lat,
		       ais->type23.stationtype,
		       ais->type23.shiptype,
		       ais->type23.txrx,
		       ais->type23.interval, ais->type23.quiet);
	break;
    case 24:			/* Class B CS Static Data Report */
	(void)snprintf(buf + strlen(buf), buflen - strlen(buf),
		       "%s|", ais->type24.shipname);
	(void)snprintf(buf + strlen(buf), buflen - strlen(buf),
		       "%u|", ais->type24.shiptype);
	(void)snprintf(buf + strlen(buf), buflen - strlen(buf),
		       "%s|%s|", ais->type24.vendorid, ais->type24.callsign);
	if (AIS_AUXILIARY_MMSI(ais->mmsi)) {
	    (void)snprintf(buf + strlen(buf), buflen - strlen(buf),
			   "%u", ais->type24.mothership_mmsi);
	} else {
	    (void)snprintf(buf + strlen(buf), buflen - strlen(buf),
			   "%u|%u|%u|%u",
			   ais->type24.dim.to_bow,
			   ais->type24.dim.to_stern,
			   ais->type24.dim.to_port,
			   ais->type24.dim.to_starboard);
	}
	break;
    case 25:			/* Binary Message, Single Slot */
	(void)snprintf(buf + strlen(buf), buflen - strlen(buf),
		       "%u|%u|%u|%u|%zd:%s\r\n",
		       (uint) ais->type25.addressed,
		       (uint) ais->type25.structured,
		       ais->type25.dest_mmsi,
		       ais->type25.app_id,
		       ais->type25.bitcount,
		       gpsd_hexdump(ais->type25.bitdata,
				    (ais->type25.bitcount + 7) / 8));
	break;
    case 26:			/* Binary Message, Multiple Slot */
	(void)snprintf(buf + strlen(buf), buflen - strlen(buf),
		       "%u|%u|%u|%u|%zd:%s:%u\r\n",
		       (uint) ais->type26.addressed,
		       (uint) ais->type26.structured,
		       ais->type26.dest_mmsi,
		       ais->type26.app_id,
		       ais->type26.bitcount,
		       gpsd_hexdump(ais->type26.bitdata,
				    (ais->type26.bitcount + 7) / 8),
		       ais->type26.radio);
	break;
    default:
	(void)snprintf(buf + strlen(buf),
		       buflen - strlen(buf),
		       "unknown AIVDM message content.");
	break;
    }
    /*@ +formatcode @*/
    (void)strlcat(buf, "\r\n", buflen);
}
#endif

/*@ -compdestroy -compdef -usedef -uniondef @*/
static void decode(FILE *fpin, FILE*fpout)
/* sensor data on fpin to dump format on fpout */
{
    struct gps_device_t session;
    struct gps_context_t context;
    struct policy_t policy;
    char buf[GPS_JSON_RESPONSE_MAX * 4];

    memset(&policy, '\0', sizeof(policy));
    policy.json = json;

    gps_context_init(&context);
    context.readonly = true;
    gpsd_init(&session, &context, NULL);
    gpsd_clear(&session);
    session.gpsdata.gps_fd = fileno(fpin);
    session.gpsdata.dev.baudrate = 38400;     /* hack to enable subframes */

    for (;;)
    {
	gps_mask_t changed = gpsd_poll(&session);

	if (changed == ERROR_IS || changed == NODATA_IS)
	    break;
	if (verbose >= 1 && TEXTUAL_PACKET_TYPE(session.packet.type))
	    (void)fputs((char *)session.packet.outbuffer, fpout);
	if ((changed & (REPORT_IS|SUBFRAME_IS|AIS_IS|RTCM2_IS|RTCM3_IS)) == 0)
	    continue;
	/*
	 * We really ought to get rid of the non-JSON cases someday.
	 * They're not used for production, only regression testing.
	 */
	else if (json) {
	    json_data_report(changed, 
			     &session.gpsdata, &policy, 
			     buf, sizeof(buf));
	    (void)fputs(buf, fpout);	
#ifdef RTCM104V3_ENABLE
	} else if (session.packet.type == RTCM3_PACKET) {
	    rtcm3_dump(&session.gpsdata.rtcm3, fpout);
#endif
#ifdef AIVDM_ENABLE
	} else if (session.packet.type == AIVDM_PACKET) {
	    if ((changed & AIS_IS)!=0) {
		aivdm_csv_dump(&session.gpsdata.ais, buf, sizeof(buf));
		(void)fputs(buf, fpout);
	    }
#endif
	}
    }
}

static void encode(FILE *fpin, FILE *fpout)
/* JSON format on fpin to JSON on fpout - idempotency test */
{
    char inbuf[BUFSIZ];
    struct policy_t policy;
    struct gps_data_t gpsdata;
    int lineno = 0;

    memset(&policy, '\0', sizeof(policy));
    policy.json = true;

    while (fgets(inbuf, (int)sizeof(inbuf), fpin) != NULL) {
	int status;

	++lineno;
	if (inbuf[0] == '#')
	    continue;
	status = libgps_json_unpack(inbuf, &gpsdata, NULL);
	if (status != 0) {
	    (void)fprintf(stderr,
			  "gpsdecode: dying with status %d (%s) on line %d\n",
			  status, json_error_string(status), lineno);
	    exit(1);
	}
	json_data_report(gpsdata.set, 
			 &gpsdata, &policy, 
			 inbuf, sizeof(inbuf));
	(void)fputs(inbuf, fpout);	
    }
}
/*@ +compdestroy +compdef +usedef @*/

int main(int argc, char **argv)
{
    int c;
    enum
    { doencode, dodecode } mode = dodecode;

    while ((c = getopt(argc, argv, "cdejpuvVD:")) != EOF) {
	switch (c) {
	case 'c':
	    json = false;
	    break;

	case 'd':
	    mode = dodecode;
	    break;

	case 'e':
	    mode = doencode;
	    break;

	case 'j':
	    json = true;
	    break;

	case 'u':
	    scaled = false;
	    break;

	case 'v':
	    verbose = 1;
	    break;
		
	case 'D':
	    verbose = atoi(optarg);
	    gpsd_hexdump_level = verbose;
#ifdef CLIENTDEBUG_ENABLE
	    json_enable_debug(verbose - 2, stderr);
#endif
	    break;

	case 'V':
	    (void)fprintf(stderr, "gpsdecode revision " VERSION "\n");
	    exit(0);

	case '?':
	default:
	    (void)fputs("gpsdecode [-v]\n", stderr);
	    exit(1);
	}
    }
    argc -= optind;
    argv += optind;

    if (mode == doencode)
	encode(stdin, stdout);
    else
	decode(stdin, stdout);
    exit(0);
}

/* gpsdecode.c ends here */
