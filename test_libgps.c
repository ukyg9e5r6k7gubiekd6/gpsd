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

#include "gpsd.h"

#ifndef S_SPLINT_S
#include <unistd.h>
#endif /* S_SPLINT_S */
#include <getopt.h>
#include <signal.h>

static void onsig(int sig)
{
    (void)fprintf(stderr, "libgps: died with signal %d\n", sig);
    exit(1);
}

/* must start zeroed, otherwise the unit test will try to chase garbage pointer fields. */
static struct gps_data_t gpsdata;

int main(int argc, char *argv[])
{
    struct gps_data_t collect;
    char buf[BUFSIZ];
    int option;
    bool batchmode = false;
    int debug = 0;

    (void)signal(SIGSEGV, onsig);
#ifdef SIGBUS
    (void)signal(SIGBUS, onsig);
#endif /* SIGBUS */

    while ((option = getopt(argc, argv, "bhsD:?")) != -1) {
	switch (option) {
	case 'b':
	    batchmode = true;
	    break;
	case 's':
	    (void)
		printf
		("Sizes: fix=" SSIZE_T_FORMAT " gpsdata=" SSIZE_T_FORMAT " rtcm2=" SSIZE_T_FORMAT " rtcm3=" SSIZE_T_FORMAT " ais=" SSIZE_T_FORMAT " compass=" SSIZE_T_FORMAT " raw=" SSIZE_T_FORMAT " devices=" SSIZE_T_FORMAT " policy=" SSIZE_T_FORMAT " version=" SSIZE_T_FORMAT ", noise=" SSIZE_T_FORMAT "\n",
		 sizeof(struct gps_fix_t),
		 sizeof(struct gps_data_t), sizeof(struct rtcm2_t),
		 sizeof(struct rtcm3_t), sizeof(struct ais_t),
		 sizeof(struct attitude_t), sizeof(struct rawdata_t),
		 sizeof(collect.devices), sizeof(struct policy_t),
		 sizeof(struct version_t), sizeof(struct gst_t));
	    exit(0);
	case 'D':
	    debug = atoi(optarg);
	    break;
	case '?':
	case 'h':
	default:
	    (void)fputs("usage: test_libgps [-b] [-D lvl] [-s]\n", stderr);
	    exit(1);
	}
    }

    gps_enable_debug(debug, stdout);
    if (batchmode) {
	while (fgets(buf, sizeof(buf), stdin) != NULL) {
	    if (buf[0] == '{' || isalpha(buf[0])) {
		gps_unpack(buf, &gpsdata);
		libgps_dump_state(&gpsdata);
	    }
	}
    } else if (gps_open(NULL, 0, &collect) <= 0) {
	(void)fputs("Daemon is not running.\n", stdout);
	exit(1);
    } else if (optind < argc) {
	(void)strlcpy(buf, argv[optind], BUFSIZ);
	(void)strlcat(buf, "\n", BUFSIZ);
	(void)gps_send(&collect, buf);
	(void)gps_read(&collect);
	libgps_dump_state(&collect);
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
	    libgps_dump_state(&collect);
	}
	(void)gps_close(&collect);
    }

    return 0;
}

/*@-nullderef@*/
