#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <ctype.h>

#include "gpsd.h"

static int verbose = RTCM_ERRLEVEL_BASE;

void gpsd_report(int errlevel, const char *fmt, ... )
/* assemble command in printf(3) style, use stderr or syslog */
{
    if (errlevel <= verbose) {
	char buf[BUFSIZ];
	va_list ap;

	strcpy(buf, "rtcmdecode: ");
	va_start(ap, fmt) ;
	(void)vsnprintf(buf + strlen(buf), sizeof(buf)-strlen(buf), fmt, ap);
	va_end(ap);
	(void)fputs(buf, stdout);
    }
}

static void decode(FILE *fpin, FILE *fpout)
/* RTCM-104 bits on fpin to dump format on fpout */
{
     int             c;
    struct gps_device_t device;
    enum isgpsstat_t res;
    off_t count;
    char buf[BUFSIZ];

    isgps_init(&device);

    count = 0;
    while ((c = fgetc(fpin)) != EOF) {
	res = rtcm_decode(&device, (unsigned int)c);
	if (verbose >= RTCM_ERRLEVEL_BASE + 3) 
	    fprintf(fpout, "%08lu: '%c' [%02x] -> %d\n", 
		   (unsigned long)count++, (isprint(c)?c:'.'), (unsigned)(c & 0xff), res);
	if (res == ISGPS_MESSAGE) {
	    rtcm_dump(&device, buf, sizeof(buf));
	    (void)fputs(buf, fpout);
	}
    }
}

static void passthrough(FILE *fpin, FILE *fpout)
/* dump format on stdin to dump format on stdout (self-inversion test) */
{
    char buf[BUFSIZ];
    struct gps_device_t rtcmdata;

    memset(&rtcmdata, 0, sizeof(rtcmdata));
    while (fgets(buf, (int)sizeof(buf), fpin) != NULL) {
	int status;

	/* pass through comment lines without interpreting */
	if (buf[0] == '#') {
	    (void)fputs(buf, fpout);
	    continue;
	}

	status = rtcm_undump(&rtcmdata.gpsdata.rtcm, buf);

	if (status == 0) {
	    (void)rtcm_repack(&rtcmdata);
	    (void)rtcm_unpack(&rtcmdata);
	    (void)rtcm_dump(&rtcmdata, buf, sizeof(buf));
	    (void)fputs(buf, fpout);
	    memset(&rtcmdata, 0, sizeof(rtcmdata));
	} else if (status < 0) {
	    (void) fprintf(stderr, "rtcmdecode: bailing out with status %d\n", status);
	    exit(1);
	}
    }
}

int main(int argc, char **argv)
{
    char buf[BUFSIZ];
    int c;
    bool striphdr = false;
    bool pass = false;

    while ((c = getopt(argc, argv, "hpv:")) != EOF) {
	switch (c) {
	case 'h':	/* not documented, used for debugging */
	    striphdr = true;
	    break;

	case 'p':	/* not documented, used for debugging */
	    pass = true;
	    break;

	case 'v':		/* verbose */
	    verbose = RTCM_ERRLEVEL_BASE + atoi(optarg);
	    break;

	case '?':
	default:
	    (void)fputs("rtcmdecode [-v]\n", stderr);
	    exit(1);
	}
    }
    argc -= optind;
    argv += optind;

    /* strip lines with leading # */
    if (striphdr) {
	while ((c = getchar()) == '#')
	    (void)fgets(buf, (int)sizeof(buf), stdin);
	(void)ungetc(c, stdin);
    }

    if (pass)
	passthrough(stdin, stdout);
    else
	decode(stdin, stdout);
    exit(0);
}

/* rtcmdecode.c ends here */
