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
		aivdm_dump(&aivdm.decoded, scaled, json, buf, sizeof(buf));
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
	status = gps_unpack(inbuf, &gpsdata);
	if (status < 0) {
	    (void) fprintf(stderr, "gpsdecode: bailing out with status %d on line %d\n", status, lineno);
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

    while ((c = getopt(argc, argv, "dejpuVD:")) != EOF) {
	switch (c) {
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
