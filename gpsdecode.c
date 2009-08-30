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
#include "gps_json.h"

static int verbose = 0;
static bool scaled = true;
static bool json = false;

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

static void aivdm_csv_dump(struct ais_t *ais, char *buf, size_t buflen)
{
    (void)snprintf(buf, buflen, "%u,%u,%09u,", ais->type,ais->repeat,ais->mmsi);
    switch (ais->type) {
    case 1:	/* Position Report */
    case 2:
    case 3:
	(void)snprintf(buf+strlen(buf), buflen-strlen(buf),
		       "%u,%d,%u,%u,%d,%d,%u,%d,%u,0x%x,%d,0x%x",
		       ais->type123.status,
		       ais->type123.turn,
		       ais->type123.speed,
		       (uint)ais->type123.accuracy,
		       ais->type123.lon,
		       ais->type123.lat,
		       ais->type123.course,
		       ais->type123.heading,
		       ais->type123.second,
		       ais->type123.maneuver,
		       ais->type123.raim,
		       ais->type123.radio);
	break;
    case 4:	/* Base Station Report */
    case 11:	/* UTC/Date Response */
	(void)snprintf(buf+strlen(buf), buflen-strlen(buf),
		       "%04u:%02u:%02uT%02u:%02u:%02uZ,%u,%d,%d,%u,%u,0x%x",
		       ais->type4.year,
		       ais->type4.month,
		       ais->type4.day,
		       ais->type4.hour,
		       ais->type4.minute,
		       ais->type4.second,
		       (uint)ais->type4.accuracy,
		       ais->type4.lon,
		       ais->type4.lat,
		       ais->type4.epfd,
		       ais->type4.raim,
		       ais->type4.radio);
	break;
    case 5: /* Ship static and voyage related data */
	(void)snprintf(buf+strlen(buf), buflen-strlen(buf),
		       "%u,%u,%s,%s,%u,%u,%u,%u,%u,%u,%02u-%02uT%02u:%02uZ,%u,%s,%u",
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
		       ais->type5.destination,
		       ais->type5.dte);
	break;
    case 6:	/* Binary Message */
	(void)snprintf(buf+strlen(buf), buflen-strlen(buf),
		       "%u,%u,%u,%u,%u:%s",
		       ais->type6.seqno,
		       ais->type6.dest_mmsi,
		       ais->type6.retransmit,
		       ais->type6.application_id,
		       ais->type6.bitcount,
		       gpsd_hexdump(ais->type6.bitdata,
				       (ais->type6.bitcount+7)/8));
	break;
    case 7:	/* Binary Acknowledge */
	(void)snprintf(buf+strlen(buf), buflen-strlen(buf),
		       "%u,%u,%u,%u",
		       ais->type7.mmsi[0],
		       ais->type7.mmsi[1],
		       ais->type7.mmsi[2],
		       ais->type7.mmsi[3]);
	break;
    case 8:	/* Binary Broadcast Message */
	(void)snprintf(buf+strlen(buf), buflen-strlen(buf),
		       "%u,%u:%s",
		       ais->type8.application_id,
		       ais->type8.bitcount,
		       gpsd_hexdump(ais->type8.bitdata,
				       (ais->type8.bitcount+7)/8));
	break;
    case 9:
	(void)snprintf(buf+strlen(buf), buflen-strlen(buf),
		       "%u,%u,%u,%d,%d,%u,%u,0x%x,%u,%d,0x%x",
		       ais->type9.alt,
		       ais->type9.speed,
		       (uint)ais->type9.accuracy,
		       ais->type9.lon,
		       ais->type9.lat,
		       ais->type9.course,
		       ais->type9.second,
		       ais->type9.regional,
		       ais->type9.dte,
		       ais->type9.raim,
		       ais->type9.radio);
	break;
    case 10:	/* UTC/Date Inquiry */
	(void)snprintf(buf+strlen(buf), buflen-strlen(buf),
		       "%u",
		       ais->type10.dest_mmsi);
	break;
    case 12:	/* Safety Related Message */
	(void)snprintf(buf+strlen(buf), buflen-strlen(buf),
		       "%u,%u,%u,%s",
		       ais->type12.seqno,
		       ais->type12.dest_mmsi,
		       ais->type12.retransmit,
		       ais->type12.text);
	break;
    case 13:	/* Safety Related Acknowledge */
	(void)snprintf(buf+strlen(buf), buflen-strlen(buf),
		       "%u,%u,%u,%u",
		       ais->type13.mmsi[0],
		       ais->type13.mmsi[1],
		       ais->type13.mmsi[2],
		       ais->type13.mmsi[3]);
	break;
    case 14:	/* Safety Related Broadcast Message */
	(void)snprintf(buf+strlen(buf), buflen-strlen(buf),
		       "%s", 
		       ais->type14.text);
	break;
    case 15:
	(void)snprintf(buf+strlen(buf), buflen-strlen(buf),
		       "%u,%u,%u,%u,%u,%u,%u,%u",
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
		       "%u,%u,%u,%u,%u,%u",
		       ais->type16.mmsi1,
		       ais->type16.offset1,
		       ais->type16.increment1,
		       ais->type16.mmsi2,
		       ais->type16.offset2,
		       ais->type16.increment2);
	break;
    case 17:
	(void)snprintf(buf+strlen(buf), buflen-strlen(buf),
		       "%d,%d,%d:%s",
		       ais->type17.lon,
		       ais->type17.lat,
		       ais->type17.bitcount,
		       gpsd_hexdump(ais->type17.bitdata,
				       (ais->type17.bitcount+7)/8));
	break;
    case 18:
	(void)snprintf(buf+strlen(buf), buflen-strlen(buf),
		       "%u,%u,%u,%d,%d,%u,%u,%u,0x%x,%u,%u,%u,%u,%u,%d,0x%x",
		       ais->type18.reserved,
		       ais->type18.speed,
		       (uint)ais->type18.accuracy,
		       ais->type18.lon,
		       ais->type18.lat,
		       ais->type18.course,
		       ais->type18.heading,
		       ais->type18.second,
		       ais->type18.regional,
		       ais->type18.cs_flag,
		       ais->type18.display_flag,
		       ais->type18.dsc_flag,
		       ais->type18.band_flag,
		       ais->type18.msg22_flag,
		       ais->type18.raim,
		       ais->type18.radio);
	break;
    case 19:
	(void)snprintf(buf+strlen(buf), buflen-strlen(buf),
		       "%u,%u,%u,%d,%d,%u,%u,%u,0x%x,%s,%u,%u,%u,%u,%u,%u,%d,0x%x",
		       ais->type19.reserved,
		       ais->type19.speed,
		       (uint)ais->type19.accuracy,
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
		       ais->type19.raim,
		       ais->type19.assigned);
	break;
    case 20:	/* Data Link Management Message */
	(void)snprintf(buf+strlen(buf), buflen-strlen(buf),
		       "%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u",
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
	(void)snprintf(buf+strlen(buf), buflen-strlen(buf),
		       "%u,%s,%u,%d,%d,%u,%u,%u,%u,%u,%u,%u,0x%x,%u,%u",
		       ais->type21.type,
		       ais->type21.name,
		       ais->type21.accuracy,
		       ais->type21.lon,
		       ais->type21.lat,
		       ais->type21.to_bow,
		       ais->type21.to_stern,
		       ais->type21.to_port,
		       ais->type21.to_starboard,
		       ais->type21.epfd,
		       ais->type21.second,
		       ais->type21.regional,
		       ais->type21.off_position,
		       ais->type21.raim,
		       ais->type21.virtual_aid);
	break;
    case 22:	/* Channel Management */
	(void)snprintf(buf+strlen(buf), buflen-strlen(buf),
		       "%u,%u,%u,%u,%d,%d,%d,%d,%u,%u,%u,%u",
		       ais->type22.channel_a,
		       ais->type22.channel_b,
		       ais->type22.mode,
		       ais->type22.power,
		       ais->type22.ne_lon,
		       ais->type22.ne_lat,
		       ais->type22.sw_lon,
		       ais->type22.sw_lat,
		       ais->type22.addressed,
		       ais->type22.band_a,
		       ais->type22.band_b,
		       ais->type22.zonesize);
	break;
    case 24: /* Class B CS Static Data Report */
	(void)snprintf(buf+strlen(buf), buflen-strlen(buf), 
		      "%u,", ais->type24.part);
	if (ais->type24.part == 0) {
	    (void)snprintf(buf+strlen(buf), buflen-strlen(buf), 
			  "%s",
			  ais->type24.a.shipname);
	} else if (ais->type24.part == 1) {
	    (void)snprintf(buf+strlen(buf), buflen-strlen(buf),
			   "%u,",
			   ais->type24.b.shiptype);
	    (void)snprintf(buf+strlen(buf), buflen-strlen(buf),
			  "%s,%s,",
			  ais->type24.b.vendorid,
			  ais->type24.b.callsign);
	    if (AIS_AUXILIARY_MMSI(ais->mmsi)) {
		(void)snprintf(buf+strlen(buf), buflen-strlen(buf),
			      "%u",
			      ais->type24.b.mothership_mmsi);
	    } else {
		(void)snprintf(buf+strlen(buf), buflen-strlen(buf),
			      "%u,%u,%u,%u",
			      ais->type24.b.dim.to_bow,
			      ais->type24.b.dim.to_stern,
			      ais->type24.b.dim.to_port,
			      ais->type24.b.dim.to_starboard);
	    }
	} else
	    (void)snprintf(buf+strlen(buf),
			  buflen-strlen(buf), "illegal part value %u", ais->type24.part);
	break;
    default:
	(void)snprintf(buf+strlen(buf),
		      buflen-strlen(buf), "unknown AIVDM message content.");
	break;
    }
}

/*@ -compdestroy -compdef -usedef @*/
static void decode(FILE *fpin, FILE *fpout)
/* RTCM or AIS packets on fpin to dump format on fpout */
{
    struct gps_packet_t lexer;
    struct rtcm2_t rtcm2;
    struct rtcm3_t rtcm3;
    struct aivdm_context_t aivdm;
    char buf[BUFSIZ];

    packet_reset(&lexer);

    while (packet_get(fileno(fpin), &lexer) > 0) {
	if (lexer.type == COMMENT_PACKET)
	    continue;
	else if (lexer.type == RTCM2_PACKET) {
	    rtcm2_unpack(&rtcm2, (char *)lexer.isgps.buf);
	    if (json)
		rtcm2_json_dump(&rtcm2, buf, sizeof(buf));
	    else
		rtcm2_sager_dump(&rtcm2, buf, sizeof(buf));
	    (void)fputs(buf, fpout);
	}
	else if (lexer.type == RTCM3_PACKET) {
	    rtcm3_unpack(&rtcm3, (char *)lexer.outbuffer);
	    rtcm3_dump(&rtcm3, stdout);
	}
	else if (lexer.type == AIVDM_PACKET) {
	    /*@ -uniondef */
	    if (aivdm_decode((char *)lexer.outbuffer, lexer.outbuflen, &aivdm)){
		if (!json)
		    aivdm_csv_dump(&aivdm.decoded, buf, sizeof(buf));
		else
		    aivdm_json_dump(&aivdm.decoded, scaled, buf, sizeof(buf));
		(void)fputs(buf, fpout);
		(void)fputs("\n", fpout);
	    }
	    
	    /*@ +uniondef */
	}
    }
}
/*@ +compdestroy +compdef +usedef @*/

/*@ -compdestroy @*/
static void encode(FILE *fpin, bool repack, FILE *fpout)
/* dump format on fpin to RTCM-104 on fpout */
{
    char inbuf[BUFSIZ];
    struct gps_data_t gpsdata;
    int lineno = 0;

    memset(&gpsdata, '\0', sizeof(gpsdata));	/* avoid segfault due to garbage in thread-hook slots */
    while (fgets(inbuf, (int)sizeof(inbuf), fpin) != NULL) {
	int status;

	++lineno;
	if (inbuf[0] == '#')
	    continue;
	status = libgps_json_unpack(inbuf, &gpsdata);
	if (status != 0) {
	    (void) fprintf(stderr, "gpsdecode: bailing out with status %d (%s) on line %d\n", status, json_error_string(status), lineno);
	    exit(1);
	} if ((gpsdata.set & RTCM2_SET) != 0) { 
	    if (repack) {
		// FIXME: This code is presently broken
		struct gps_packet_t lexer;
		(void)memset(lexer.isgps.buf, 0, sizeof(lexer.isgps.buf));
	        (void)rtcm2_repack(&gpsdata.rtcm2, lexer.isgps.buf);
	        if (fwrite(lexer.isgps.buf, 
		       sizeof(isgps30bits_t), 
		       (size_t)gpsdata.rtcm2.length, fpout) != (size_t)gpsdata.rtcm2.length)
		    (void) fprintf(stderr, "gpsdecode: report write failed.\n");
		memset(&lexer, 0, sizeof(lexer));
	    } else {
		/* this works */
		char outbuf[BUFSIZ];
		rtcm2_json_dump(&gpsdata.rtcm2, outbuf, sizeof(outbuf));
		(void)fputs(outbuf, fpout);
	    }
	}
    }
}
/*@ +compdestroy @*/

int main(int argc, char **argv)
{
    int c;
    enum {doencode, dodecode} mode = dodecode;

    while ((c = getopt(argc, argv, "cdejpuVD:")) != EOF) {
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

    if (mode == doencode)
	encode(stdin, !json, stdout);
    else
	decode(stdin, stdout);
    exit(0);
}

/* gpsdecode.c ends here */
