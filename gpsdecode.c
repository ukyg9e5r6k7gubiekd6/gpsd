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
static bool labeled = true;

static struct gps_device_t session;
static struct gps_context_t context;

/**************************************************************************
 *
 * AIVDM decoding
 *
 **************************************************************************/

static void  aivdm_dump(struct ais_t *ais, FILE *fp)
{
    (void)fprintf(fp, "%d:%d:%09d:", ais->id, ais->ri, ais->mmsi);
    switch (ais->id) {
    case 1:	/* Position Report */
    case 2:
    case 3:
	(void)fprintf(fp,
		      "%d:%d:%d:%d:%d:%d:%d:%d:%d\n",
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
	(void)fprintf(fp,
		      "%4d:%02d:%02d:%02d:%02d:%02d:%d:%d:%d:%d\n",
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
#if 0
    case 5: /* Ship static and voyage related data */
	(void)fprintf(fp,
		      "IMO: %d\n",
		      ais->type5.imo_id);
	break;
#endif
    default:
	gpsd_report(LOG_ERROR, "Unparsed AIVDM message type %d.\n",ais->id);
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
	if (gpsd_poll(&session) & ERROR_SET) {
	    gpsd_report(LOG_ERROR,"Error during packet fetch.\n");
	    break;
	}
	if (session.packet.type == RTCM2_PACKET) {
	    rtcm2_unpack(&rtcm2, (char *)session.packet.isgps.buf);
	    rtcm2_dump(&rtcm2, buf, sizeof(buf));
	    (void)fputs(buf, fpout);
	}
	else if (session.packet.type == RTCM3_PACKET) {
	    rtcm3_unpack(&rtcm3, (char *)session.packet.outbuffer);
	    rtcm3_dump(&rtcm3, stdout);
	}
	else if (session.packet.type == AIVDM_PACKET) {
	    aivdm_decode(&session, &session.driver.aivdm.decoded);
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

    while ((c = getopt(argc, argv, "def:hpVv:")) != EOF) {
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

	case 'p':	/* undocumented, used for regression-testing */
	    mode = passthrough;
	    break;

	case 'v':
	    verbose = atoi(optarg);
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
