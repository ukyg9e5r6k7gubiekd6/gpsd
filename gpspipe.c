/*
 * gpspipe
 *
 * a simple program to connect to a gpsd daemon and dump the received data
 * to stdout
 *
 * This will dump the raw NMEA from gpsd to stdout
 *      gpspipe -r
 *
 * This will dump the super-raw data (gps binary) from gpsd to stdout
 *      gpspipe -R
 *
 * This will dump the GPSD sentences from gpsd to stdout
 *      gpspipe -w
 *
 * This will dump the GPSD and the NMEA sentences from gpsd to stdout
 *      gpspipe -wr
 *
 * Original code by: Gary E. Miller <gem@rellim.com>.  Cleanup by ESR.
 *
 * This file is Copyright (c) 2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 *
 */

#include <stdlib.h>
#include "gpsd_config.h"
#ifndef S_SPLINT_S
#if HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif /* HAVE_SYS_SOCKET_H */
#include <unistd.h>
#endif /* S_SPLINT_S */
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#if HAVE_TERMIOS
#include <termios.h>
#endif /* HAVE_TERMIOS */
#include <assert.h>
#include "gpsd.h"
#include "gpsdclient.h"
#include "revision.h"

static struct gps_data_t gpsdata;
static void spinner(unsigned int, unsigned int);

/* NMEA-0183 standard baud rate */
#define BAUDRATE B4800

/* Serial port variables */
static struct termios oldtio, newtio;
static int fd_out = 1;		/* output initially goes to standard output */
static char serbuf[255];
static int debug;

static void daemonize(void)
/* Daemonize me. */
{
    int i;
    pid_t pid;

    /* Run as my child. */
    pid = fork();
    if (pid == -1)
	exit(1);		/* fork error */
    if (pid > 0)
	exit(0);		/* parent exits */

    /* Obtain a new process group. */
    (void)setsid();

    /* Close all open descriptors. */
    for (i = getdtablesize(); i >= 0; --i)
	(void)close(i);

    /* Reopen STDIN, STDOUT, STDERR to /dev/null. */
    i = open("/dev/null", O_RDWR);	/* STDIN */
    /*@ -sefparams @*/
    assert(dup(i) != -1);	/* STDOUT */
    assert(dup(i) != -1);	/* STDERR */

    /* Know thy mask. */
    (void)umask(0x033);

    /* Run from a known spot. */
    assert(chdir("/") != -1);
    /*@ +sefparams @*/

    /* Catch child sig */
    (void)signal(SIGCHLD, SIG_IGN);

    /* Ignore tty signals */
    (void)signal(SIGTSTP, SIG_IGN);
    (void)signal(SIGTTOU, SIG_IGN);
    (void)signal(SIGTTIN, SIG_IGN);
}

static void open_serial(char *device)
/* open the serial port and set it up */
{
    /* 
     * Open modem device for reading and writing and not as controlling
     * tty.
     */
    if ((fd_out = open(device, O_RDWR | O_NOCTTY)) == -1) {
	fprintf(stderr, "gpspipe: error opening serial port\n");
	exit(1);
    }

    /* Save current serial port settings for later */
    if (tcgetattr(fd_out, &oldtio) != 0) {
	fprintf(stderr, "gpspipe: error reading serial port settings\n");
	exit(1);
    }

    /* Clear struct for new port settings. */
    /*@i@*/ bzero(&newtio, sizeof(newtio));

    /* make it raw */
    (void)cfmakeraw(&newtio);
    /* set speed */
    /*@i@*/ (void)cfsetospeed(&newtio, BAUDRATE);

    /* Clear the modem line and activate the settings for the port. */
    (void)tcflush(fd_out, TCIFLUSH);
    if (tcsetattr(fd_out, TCSANOW, &newtio) != 0) {
	(void)fprintf(stderr, "gpspipe: error configuring serial port\n");
	exit(1);
    }
}

static void usage(void)
{
    (void)fprintf(stderr,
		  "Usage: gpspipe [OPTIONS] [server[:port[:device]]]\n\n"
		  "-d Run as a daemon.\n" "-f [file] Write output to file.\n"
		  "-h Show this help.\n" "-r Dump raw NMEA.\n"
		  "-R Dump super-raw mode (GPS binary).\n"
		  "-w Dump gpsd native data.\n"
		  "-l Sleep for ten seconds before connecting to gpsd.\n"
		  "-t Time stamp the data.\n"
		  "-T [format] set the timestamp format (strftime(3)-like; implies '-t')\n"
		  "-s [serial dev] emulate a 4800bps NMEA GPS on serial port (use with '-r').\n"
		  "-n [count] exit after count packets.\n"
		  "-v Print a little spinner.\n"
		  "-V Print version and exit.\n\n"
		  "You must specify one, or both, of -r/-w.\n"
		  "You must use -o if you use -d.\n");
}

/*@ -compdestroy @*/
int main(int argc, char **argv)
{
    char buf[4096];
    bool timestamp = false;
    char *format = "%c";
    char tmstr[200];
    bool daemon = false;
    bool binary = false;
    bool sleepy = false;
    bool new_line = true;
    bool raw = false;
    bool watch = false;
    long count = -1;
    int option;
    unsigned int vflag = 0, l = 0;
    FILE *fp;
    unsigned int flags;

    struct fixsource_t source;
    char *serialport = NULL;
    char *outfile = NULL;

    /*@-branchstate@*/
    flags = WATCH_ENABLE;
    while ((option = getopt(argc, argv, "?dD:lhrRwtT:vVn:s:o:")) != -1) {
	switch (option) {
	case 'D':
	    debug = atoi(optarg);
#ifdef CLIENTDEBUG_ENABLE
	    gps_enable_debug(debug, stderr);
#endif /* CLIENTDEBUG_ENABLE */
	    break;
	case 'n':
	    count = strtol(optarg, 0, 0);
	    break;
	case 'r':
	    raw = true;
	    /* 
	     * Yes, -r invokes NMEA mode rather than proper raw mode.
	     * This emulates the behavior under the old protocol.
	     */
	    flags |= WATCH_NMEA;
	    break;
	case 'R':
	    flags |= WATCH_RAW;
	    binary = true;
	    break;
	case 'd':
	    daemon = true;
	    break;
	case 'l':
	    sleepy = true;
	    break;
	case 't':
	    timestamp = true;
	    break;
	case 'T':
	    timestamp = true;
	    format = optarg;
	    break;
	case 'v':
	    vflag++;
	    break;
	case 'w':
	    flags |= WATCH_JSON;
	    watch = true;
	    break;
	case 'V':
	    (void)fprintf(stderr, "%s: %s (revision %s)\n",
			  argv[0], VERSION, REVISION);
	    exit(0);
	case 's':
	    serialport = optarg;
	    break;
	case 'o':
	    outfile = optarg;
	    break;
	case '?':
	case 'h':
	default:
	    usage();
	    exit(1);
	}
    }
    /*@+branchstate@*/

    /* Grok the server, port, and device. */
    if (optind < argc) {
	gpsd_source_spec(argv[optind], &source);
    } else
	gpsd_source_spec(NULL, &source);

    if (serialport != NULL && !raw) {
	(void)fprintf(stderr, "gpspipe: use of '-s' requires '-r'.\n");
	exit(1);
    }

    if (outfile == NULL && daemon) {
	(void)fprintf(stderr, "gpspipe: use of '-d' requires '-f'.\n");
	exit(1);
    }

    if (!raw && !watch && !binary) {
	(void)fprintf(stderr,
		      "gpspipe: one of '-R', '-r' or '-w' is required.\n");
	exit(1);
    }

    /* Daemonize if the user requested it. */
    if (daemon)
	daemonize();

    /* Sleep for ten seconds if the user requested it. */
    if (sleepy)
	(void)sleep(10);

    /* Open the output file if the user requested it.  If the user
     * requested '-R', we use the 'b' flag in fopen() to "do the right
     * thing" in non-linux/unix OSes. */
    if (outfile == NULL) {
	fp = stdout;
    } else {
	if (binary)
	    fp = fopen(outfile, "wb");
	else
	    fp = fopen(outfile, "w");

	if (fp == NULL) {
	    (void)fprintf(stderr,
			  "gpspipe: unable to open output file:  %s\n",
			  outfile);
	    exit(1);
	}
    }

    /* Open the serial port and set it up. */
    if (serialport)
	open_serial(serialport);

    /*@ -nullpass -onlytrans @*/
    if (gps_open(source.server, source.port, &gpsdata) != 0) {
	(void)fprintf(stderr,
		      "gpspipe: could not connect to gpsd %s:%s, %s(%d)\n",
		      source.server, source.port, strerror(errno), errno);
	exit(1);
    }
    /*@ +nullpass +onlytrans @*/

    if (source.device != NULL)
	flags |= WATCH_DEVICE;
    (void)gps_stream(&gpsdata, flags, source.device);

    if ((isatty(STDERR_FILENO) == 0) || daemon)
	vflag = 0;

    for (;;) {
	int i = 0;
	int j = 0;
	int readbytes = 0;

	if (vflag)
	    spinner(vflag, l++);

	/* reading directly from the socket avoids decode overhead */
	readbytes = (int)read(gpsdata.gps_fd, buf, sizeof(buf));
	if (readbytes > 0) {
	    for (i = 0; i < readbytes; i++) {
		char c = buf[i];
		if (j < (int)(sizeof(serbuf) - 1)) {
		    serbuf[j++] = buf[i];
		}
		if (new_line && timestamp) {
		    time_t now = time(NULL);

		    struct tm *tmp_now = localtime(&now);
		    (void)strftime(tmstr, sizeof(tmstr), format, tmp_now);
		    new_line = 0;
		    if (fprintf(fp, "%.24s :", tmstr) <= 0) {
			(void)fprintf(stderr,
				      "gpspipe: write error, %s(%d)\n",
				      strerror(errno), errno);
			exit(1);
		    }
		}
		if (fputc(c, fp) == EOF) {
		    fprintf(stderr, "gpspipe: Write Error, %s(%d)\n",
			    strerror(errno), errno);
		    exit(1);
		}

		if (c == '\n') {
		    if (serialport != NULL) {
			if (write(fd_out, serbuf, (size_t) j) == -1) {
			    fprintf(stderr,
				    "gpspipe: Serial port write Error, %s(%d)\n",
				    strerror(errno), errno);
			    exit(1);
			}
			j = 0;
		    }

		    new_line = true;
		    /* flush after every good line */
		    if (fflush(fp)) {
			(void)fprintf(stderr,
				      "gpspipe: fflush Error, %s(%d)\n",
				      strerror(errno), errno);
			exit(1);
		    }
		    if (count > 0) {
			if (0 >= --count) {
			    /* completed count */
			    exit(0);
			}
		    }
		}
	    }
	} else {
	    if (readbytes == -1) {
		(void)fprintf(stderr, "gpspipe: read error %s(%d)\n",
			      strerror(errno), errno);
		exit(1);
	    } else {
		exit(0);
	    }
	}
    }

#ifdef __UNUSED__
    if (serialport != NULL) {
	/* Restore the old serial port settings. */
	if (tcsetattr(fd_out, TCSANOW, &oldtio) != 0) {
	    (void)fprintf(stderr, "Error restoring serial port settings\n");
	    exit(1);
	}
    }
#endif /* __UNUSED__ */

    /*@i1@*/ exit(0);
}

/*@ +compdestroy @*/

static void spinner(unsigned int v, unsigned int num)
{
    char *spin = "|/-\\";

    (void)fprintf(stderr, "\010%c", spin[(num / (1 << (v - 1))) % 4]);
    (void)fflush(stderr);
    return;
}
