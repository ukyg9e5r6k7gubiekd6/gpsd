/*
 * Watch a specified serial port for transitions that might be 1PPS.
 *
 * Each output line is the second and nanosecond parts of a timestamp
 * followed by the names of handshake signals then asserted.  Off
 * transitions may generate lines with no signals aserted.
 *
 * If you don't see output within a second, use gpsmon or some other
 * equivalent tool to check that your device has satellite lock and is
 * getting fixes before giving up on the possibility of 1PPS.
 *
 * Also, check your cable. Cheap DB9 to DB9 cables such as those
 * issued with UPSes often carry TXD/RXD/GND only, omitting handshake
 * lines such as DCD.  Suspect this especially if the cable jacket
 * looks too skinny to hold more than three leads!
 *
 * This code requires only ANSI/POSIX. If it doesn't compile and run
 * on your Unix there is something very wrong with your Unix.
 *
 * This code by ESR, Copyright (C) 2013, under BSD terms.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>	/* needed for open() and friends */
#include <sys/ioctl.h>
#include <errno.h>
#include <time.h>
#include "revision.h"

struct assoc {
    int mask;
    char *string;
};

/*
 * Possible pins for PPS: DCD, CTS, RI, DSR. Pinouts:
 *
 * DB9  DB25  Name      Full name
 * ---  ----  ----      --------------------
 *  3     2    TXD  --> Transmit Data
 *  2     3    RXD  <-- Receive Data
 *  7     4    RTS  --> Request To Send
 *  8     5    CTS  <-- Clear To Send
 *  6     6    DSR  <-- Data Set Ready
 *  4    20    DTR  --> Data Terminal Ready
 *  1     8    DCD  <-- Data Carrier Detect
 *  9    22    RI   <-- Ring Indicator
 *  5     7    GND      Signal ground
 *
 * Note that it only makes sense to wait on handshake lines
 * activated from the receive side (DCE->DTE) here; in this
 * context "DCE" is the GPS. {CD,RI,CTS,DSR} is the
 * entire set of these.
 */
static const struct assoc hlines[] = {
    {TIOCM_CD, "TIOCM_CD"},
    {TIOCM_RI, "TIOCM_RI"},
    {TIOCM_DSR, "TIOCM_DSR"},
    {TIOCM_CTS, "TIOCM_CTS"},
};

static void usage(void)
{
	(void)fprintf(stderr, "usage: ppscheck [-h] [ -V] <device>\n");
	(void)fprintf(stderr, "                 -h   print usage\n");
	(void)fprintf(stderr, "                 -V   print cwVersion\n");
	exit(1);
}

int main(int argc, char *argv[])
{
    struct timespec ts;
    int fd;
    int c;

    while((c = getopt(argc, argv, "hV")) != -1) {
	    switch(c){
	    case 'h':
	    default:
		usage();
		exit(0);
	    case 'V':
		(void)printf("%s: %s\n", argv[0], REVISION);
		exit(EXIT_SUCCESS);
	    }
    }
    argc -= optind;
    argv += optind;

    if (argc != 1)
	    usage();

    fd = open(argv[0], O_RDONLY);

    if (fd == -1) {
	(void)fprintf(stderr,
		      "open(%s) failed: %d %.40s\n",
		      argv[0], errno, strerror(errno));
	exit(1);
    }

    (void)fprintf(stdout, "# Seconds  nanoSecs   Signals\n");
    for (;;) {
	if (ioctl(fd, TIOCMIWAIT, TIOCM_CD|TIOCM_DSR|TIOCM_RI|TIOCM_CTS) != 0) {
	    (void)fprintf(stderr,
			  "PPS ioctl(TIOCMIWAIT) failed: %d %.40s\n",
			  errno, strerror(errno));
	    break;
	} else {
	    const struct assoc *sp;
	    int handshakes;

	    (void)clock_gettime(CLOCK_REALTIME, &ts);
	    (void)ioctl(fd, TIOCMGET, &handshakes);
	    (void)fprintf(stdout, "%10ld %09ld", (long)ts.tv_sec, ts.tv_nsec);
	    for (sp = hlines;
		 sp < hlines + sizeof(hlines)/sizeof(hlines[0]);
		 sp++)
		if ((handshakes & sp->mask) != 0) {
		    (void)fputc(' ', stdout);
		    (void)fputs(sp->string, stdout);
		}
	    (void)fputc('\n', stdout);
	}
    }

    exit(EXIT_SUCCESS);
}

/* end */
