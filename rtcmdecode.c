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

static void decode(void)
{
    int             c;
    struct gps_device_t device;
    enum isgpsstat_t res;
    off_t count;
    char buf[BUFSIZ];

    isgps_init(&device);

    count = 0;
    while ((c = getchar()) != EOF) {
	res = rtcm_decode(&device, (unsigned int)c);
	if (verbose >= RTCM_ERRLEVEL_BASE + 3) 
	    printf("%08lu: '%c' [%02x] -> %d\n", 
		   (unsigned long)count++, (isprint(c)?c:'.'), (unsigned)(c & 0xff), res);
	if (res == ISGPS_MESSAGE) {
	    rtcm_dump(&device, buf, sizeof(buf));
	    (void)fputs(buf, stdout);
	}
    }
}

int main(int argc, char **argv)
{
    char buf[BUFSIZ];
    int c;
    bool striphdr = false;

    while ((c = getopt(argc, argv, "hv:")) != EOF) {
	switch (c) {
	case 'h':
	    striphdr = true;
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

    decode();
    exit(0);
}

/* rtcmdecode.c ends here */
