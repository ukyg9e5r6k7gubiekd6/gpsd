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
/* RTCM-104 bits on fpin to dump format on fpout */
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
static void encode(FILE *fpin, FILE *fpout)
/* dump format on fpin to RTCM-104 on fpout */
{
    char buf[BUFSIZ];
    struct gps_data_t gpsdata;
    struct gps_packet_t lexer;
    int lineno = 0;

    memset(&lexer, 0, sizeof(lexer));
    while (fgets(buf, (int)sizeof(buf), fpin) != NULL) {
	int status;

	++lineno;
	if (buf[0] == '#')
	    continue;
	status = gps_unpack(buf, &gpsdata);
	if (status == 0 && (gpsdata.set & RTCM2_SET) != 0) { 
	    (void)memset(lexer.isgps.buf, 0, sizeof(lexer.isgps.buf));
	    (void)rtcm2_repack(&gpsdata.rtcm2, lexer.isgps.buf);
	    if (fwrite(lexer.isgps.buf, 
		       sizeof(isgps30bits_t), 
		       (size_t)gpsdata.rtcm2.length, fpout) != (size_t)gpsdata.rtcm2.length)
		(void) fprintf(stderr, "gpsdecode: report write failed.\n");
	    memset(&lexer, 0, sizeof(lexer));
	} else if (status < 0) {
	    (void) fprintf(stderr, "gpsdecode: bailing out with status %d on line %d\n", status, lineno);
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
    enum {doencode, dodecode} mode = dodecode;

    while ((c = getopt(argc, argv, "dhejuVD:")) != EOF) {
	switch (c) {
	case 'd':
	    mode = dodecode;
	    break;

	case 'e':
	    mode = doencode;
	    break;

	case 'h':
	    striphdr = true;
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

    /* strip lines with leading # */
    if (striphdr) {
	while ((c = getchar()) == '#')
	    if (fgets(buf, (int)sizeof(buf), stdin) == NULL)
		(void)fputs("gpsdecode: read failed\n", stderr);
	(void)ungetc(c, stdin);
    }

    if (mode == doencode)
	encode(stdin, stdout);
    else
	decode(stdin, stdout);
    exit(0);
}

/* gpsdecode.c ends here */
