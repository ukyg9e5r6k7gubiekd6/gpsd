/*
 * A simple command-line exerciser for the library.
 * Not really useful for anything but debugging.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <ctype.h>

#define LIBGPS_DEBUG

#include "gpsd.h"

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
    char buf[BUFSIZ];
    int option;
    bool batchmode = false;
#ifdef CLIENTDEBUG_ENABLE
    int debug = 0;
#endif

    (void)signal(SIGSEGV, onsig);
    (void)signal(SIGBUS, onsig);

    while ((option = getopt(argc, argv, "bhsD:?")) != -1) {
	switch (option) {
	case 'b':
	    batchmode = true;
	    break;
	case 's':
	    (void)
		printf
		("Sizes: fix=%zd gpsdata=%zd rtcm2=%zd rtcm3=%zd ais=%zd compass=%zd raw=%zd devices=%zd policy=%zd version=%zd, noise=%zd\n",
		 sizeof(struct gps_fix_t),
		 sizeof(struct gps_data_t), sizeof(struct rtcm2_t),
		 sizeof(struct rtcm3_t), sizeof(struct ais_t),
		 sizeof(struct attitude_t), sizeof(struct rawdata_t),
		 sizeof(collect.devices), sizeof(struct policy_t),
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
	    (void)fputs("usage: test_libgps [-b] [-D lvl] [-s]\n", stderr);
	    exit(EXIT_FAILURE);
	}
    }

#ifdef CLIENTDEBUG_ENABLE
    gps_enable_debug(debug, stdout);
#endif
    if (batchmode) {
#ifdef SOCKET_EXPORT_ENABLE
	while (fgets(buf, sizeof(buf), stdin) != NULL) {
	    if (buf[0] == '{' || isalpha(buf[0])) {
		gps_unpack(buf, &gpsdata);
		libgps_dump_state(&gpsdata);
	    }
	}
#endif
    } else if (gps_open(NULL, 0, &collect) <= 0) {
	(void)fputs("Daemon is not running.\n", stdout);
	exit(EXIT_FAILURE);
    } else if (optind < argc) {
	(void)strlcpy(buf, argv[optind], sizeof(buf));
	(void)strlcat(buf, "\n", sizeof(buf));
	(void)gps_send(&collect, buf);
	(void)gps_read(&collect);
#ifdef SOCKET_EXPORT_ENABLE
	libgps_dump_state(&collect);
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
	    (void)gps_read(&collect);
#ifdef SOCKET_EXPORT_ENABLE
	    libgps_dump_state(&collect);
#endif
	}
	(void)gps_close(&collect);
    }

    return 0;
}

