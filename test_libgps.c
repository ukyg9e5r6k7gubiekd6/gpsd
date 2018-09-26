/*
 * A simple command-line exerciser for the library.
 * Not really useful for anything but debugging.
 * SPDX-License-Identifier: BSD-2-clause
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <ctype.h>

#include "gps.h"
#include "libgps.h"
#include "gpsdclient.h"

#include <unistd.h>
#include <getopt.h>
#include <signal.h>

static void onsig(int sig)
{
    (void)fprintf(stderr, "libgps: died with signal %d\n", sig);
    exit(EXIT_FAILURE);
}

#ifdef SOCKET_EXPORT_ENABLE
/* must start zeroed, otherwise the unit test will try to chase garbage pointer fields. */
static struct gps_data_t gpsdata;
#endif

int main(int argc, char *argv[])
{
    struct gps_data_t collect;
    struct fixsource_t source;
    char buf[BUFSIZ];
    int option;
    bool batchmode = false;
    bool forwardmode = false;
    char *fmsg = NULL;
#ifdef CLIENTDEBUG_ENABLE
    int debug = 0;
#endif

    (void)signal(SIGSEGV, onsig);
#ifdef SIGBUS
    (void)signal(SIGBUS, onsig);
#endif

    while ((option = getopt(argc, argv, "bf:hsD:?")) != -1) {
	switch (option) {
	case 'b':
	    batchmode = true;
	    break;
	case 'f':
	    forwardmode = true;
	    fmsg = optarg;
	    break;
	case 's':
	    (void)
		printf("Sizes: fix=%zd gpsdata=%zd rtcm2=%zd rtcm3=%zd "
                       "ais=%zd compass=%zd raw=%zd devices=%zd policy=%zd "
                       "version=%zd, noise=%zd\n",
		 sizeof(struct gps_fix_t),
		 sizeof(struct gps_data_t), sizeof(struct rtcm2_t),
		 sizeof(struct rtcm3_t), sizeof(struct ais_t),
		 sizeof(struct attitude_t), sizeof(struct rawdata_t),
		 sizeof(collect.devices), sizeof(struct gps_policy_t),
		 sizeof(struct version_t), sizeof(struct gst_t));
	    exit(EXIT_SUCCESS);
#ifdef CLIENTDEBUG_ENABLE
	case 'D':
	    debug = atoi(optarg);
	    break;
#endif
	case '?':
	case 'h':
	default:
	    (void)fputs("usage: test_libgps [-b] [-f fwdmsg] [-D lvl] [-s] [server[:port:[device]]]\n", stderr);
	    exit(EXIT_FAILURE);
	}
    }

    /* Grok the server, port, and device. */
    if (optind < argc) {
	gpsd_source_spec(argv[optind], &source);
    } else
	gpsd_source_spec(NULL, &source);

#ifdef CLIENTDEBUG_ENABLE
    gps_enable_debug(debug, stdout);
#endif
    if (batchmode) {
#ifdef SOCKET_EXPORT_ENABLE
	while (fgets(buf, sizeof(buf), stdin) != NULL) {
	    if (buf[0] == '{' || isalpha( (int) buf[0])) {
		gps_unpack(buf, &gpsdata);
#ifdef LIBGPS_DEBUG
		libgps_dump_state(&gpsdata);
#endif
	    }
	}
#endif
    } else if (gps_open(source.server, source.port, &collect) != 0) {
	(void)fprintf(stderr,
		      "test_libgps: no gpsd running or network error: %d, %s\n",
		      errno, gps_errstr(errno));
	exit(EXIT_FAILURE);
    } else if (forwardmode) {
	if (gps_send(&collect, fmsg) == -1) {
	  (void)fprintf(stderr,
			"test_libgps: gps send error: %d, %s\n",
			errno, gps_errstr(errno));
	}
	if (gps_read(&collect, NULL, 0) == -1) {
	  (void)fprintf(stderr,
			"test_libgps: gps read error: %d, %s\n",
			errno, gps_errstr(errno));
	}
#ifdef SOCKET_EXPORT_ENABLE
#ifdef LIBGPS_DEBUG
	libgps_dump_state(&collect);
#endif
#endif
	(void)gps_close(&collect);
    } else {
	int tty = isatty(0);

	if (tty)
	    (void)fputs("This is the gpsd exerciser.\n", stdout);
	for (;;) {
	    if (tty)
		(void)fputs("> ", stdout);
	    if (fgets(buf, sizeof(buf), stdin) == NULL) {
		if (tty)
		    putchar('\n');
		break;
	    }
	    collect.set = 0;
	    (void)gps_send(&collect, buf);
	    (void)gps_read(&collect, NULL, 0);
#ifdef SOCKET_EXPORT_ENABLE
#ifdef LIBGPS_DEBUG
	    libgps_dump_state(&collect);
#endif
#endif
	}
	(void)gps_close(&collect);
    }
    return 0;
}

