/* $Id$ */
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
 * All rights given to the gpsd project to release under whatever open source
 * license they use.  A thank you would be nice if you use this code.
 */

#include <sys/types.h>
#include <sys/stat.h>
#ifndef S_SPLINT_S
#include <sys/socket.h>
#include <unistd.h>
#endif /* S_SPLINT_S */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <fcntl.h>
#include <termios.h>
#include <assert.h>
#include "gpsd.h"
#include "gpsdclient.h"
#include "revision.h"

static int fd_out = 1;		/* output initially goes to standard output */ 
static void spinner(unsigned int, unsigned int);

/* NMEA-0183 standard baud rate */
#define BAUDRATE B4800

/* Serial port variables */
static struct termios oldtio, newtio;
static char serbuf[255];

static void daemonize(void) 
/* Daemonize me. */
{
    int i;
    pid_t pid;

    /* Run as my child. */
    pid=fork();
    if (pid == -1) exit(1); /* fork error */
    if (pid>0) exit(0); /* parent exits */

    /* Obtain a new process group. */
    (void)setsid();

    /* Close all open descriptors. */
    for(i=getdtablesize();i>=0;--i)
	(void)close(i);

    /* Reopen STDIN, STDOUT, STDERR to /dev/null. */
    i=open("/dev/null",O_RDWR);	/* STDIN */
    /*@ -sefparams @*/
    assert(dup(i) != -1); 	/* STDOUT */
    assert(dup(i) != -1);		/* STDERR */

    /* Know thy mask. */
    (void)umask(0x033);

    /* Run from a known spot. */
    assert(chdir("/") != -1);
    /*@ +sefparams @*/

    /* Catch child sig */
    (void)signal(SIGCHLD,SIG_IGN);

    /* Ignore tty signals */
    (void)signal(SIGTSTP,SIG_IGN);
    (void)signal(SIGTTOU,SIG_IGN);
    (void)signal(SIGTTIN,SIG_IGN);
}

static void open_serial(char* device)
/* open the serial port and set it up */
{
    /* 
     * Open modem device for reading and writing and not as controlling
     * tty.
     */
    if ((fd_out = open(device, O_RDWR|O_NOCTTY)) == -1) {
	fprintf(stderr, "gpspipe: error opening serial port\n");
	exit(1);
    }

    /* Save current serial port settings for later */
    if (tcgetattr(fd_out, &oldtio) != 0) {
	fprintf(stderr, "gpspipe: error reading serial port settings\n");
	exit(1);
    }

    /* Clear struct for new port settings. */
    /*@i@*/bzero(&newtio, sizeof(newtio));

    /* make it raw */
    (void)cfmakeraw(&newtio);
    /* set speed */
    /*@i@*/(void)cfsetospeed(&newtio, BAUDRATE);

    /* Clear the modem line and activate the settings for the port. */
    (void)tcflush(fd_out,TCIFLUSH);
    if (tcsetattr(fd_out,TCSANOW,&newtio) != 0) {
	(void)fprintf(stderr, "gpspipe: error configuring serial port\n");
	exit(1);
    }
}

static void usage(void)
{
    (void)fprintf(stderr, "Usage: gpspipe [OPTIONS] [server[:port[:device]]]\n\n"
		  "-d Run as a daemon.\n"
		  "-f [file] Write output to file.\n"
		  "-h Show this help.\n"
		  "-r Dump raw NMEA.\n"
		  "-R Dump super-raw mode (GPS binary).\n"
		  "-w Dump gpsd native data.\n"
		  "-l Sleep for ten seconds before connecting to gpsd.\n"
		  "-t Time stamp the data.\n"
		  "-s [serial dev] emulate a 4800bps NMEA GPS on serial port (use with '-r').\n"
		  "-n [count] exit after count packets.\n"
		  "-v Print a little spinner.\n"
		  "-V Print version and exit.\n\n"
		  "You must specify one, or both, of -r/-w.\n"
		  "You must use -f if you use -d.\n"
	);
}

int main( int argc, char **argv)
{
    int sock = 0;
    char buf[4096];
    ssize_t wrote = 0;
    bool timestamp = false;
    bool daemon = false;
    bool binary = false;
    bool sleepy = false;
    bool new_line = true;
    bool raw = false;
    bool watch = false;
    long count = -1;
    bool nopipe = false;
    int option;
    unsigned int vflag = 0, l = 0;
    FILE * fp;

    struct fixsource_t source;
    char *port = DEFAULT_GPSD_PORT, *server = "127.0.0.1";
    char *serialport = NULL;
    char *filename = NULL;

    while ((option = getopt(argc, argv, "?dlhrRwtvVn:Ns:f:")) != -1) {
	switch (option) {
	case 'n':
	    count = strtol(optarg, 0, 0);
	    break;
	case 'N':	/* not documented - diagnotic option */
	    nopipe = true;
	    break;
	case 'r':
	    raw = true;
	    break;
	case 'R':
	    binary=true;
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
	case 'v':
	    vflag++;
	    break;
	case 'w':
	    watch = true;
	    break;
	case 'V':
	    (void)fprintf(stderr, "%s: %s (revision %s)\n", 
			  argv[0], VERSION, REVISION);
	    exit(0);
	case 's':
	    serialport = optarg;
	    break;
	case 'f':
	    filename = optarg;
	    break;
	case '?':
	case 'h':
	default:
	    usage();
	    exit(1);
	}
    }

    /* Grok the server, port, and device. */
    if (optind < argc) {
	gpsd_source_spec(argv[optind], &source);
    } else
	gpsd_source_spec(NULL, &source);

    /*
     * Assemble the initialization command. 
     * FIXME: Should be done with a slightly enhanced gps_stream(),
     * but we're in feature freeze.
     */
    (void)strlcpy(buf, "?WATCH={\"enable\":true,", sizeof(buf));
    if (watch)
	(void)strlcat(buf, "\"json\":true,", sizeof(buf));
    else
	(void)strlcat(buf, "\"json\":false,", sizeof(buf));
    if (raw)
	/* 
	 * Yes, -r invokes NMEA mode rather than proper raw mode.
	 * This emulates the behavior under the old protocol.
	 */
	(void)strlcat(buf, "\"nmea\":true,", sizeof(buf));
    if (binary)
	(void)strlcat(buf, "\"raw\":2,", sizeof(buf));
    if (source.device != NULL)
	(void)snprintf(buf, sizeof(buf), "\"path\":\"%s\",", source.device);
    if (buf[strlen(buf)-1] == ',')
	buf[strlen(buf)-1] = '\0';
    (void)strlcat(buf, "}\r\n", sizeof(buf));

    /* diagnostic option -- lets us see the generated initializartion command */
    if (nopipe)
	(void)fputs(buf, stdout);

    if (serialport!=NULL && raw) {
	(void)fprintf(stderr, "gpsipipe: use of '-s' requires '-r'.\n");
	exit(1);
    }

    if (filename==NULL && daemon) {
	(void)fprintf(stderr, "gpsipipe: use of '-d' requires '-f'.\n");
	exit(1);
    }

    if (!raw && !watch && !binary) {
	(void)fprintf(stderr, "gpspipe: one of '-R', '-r' or '-w' is required.\n");
	exit(1);
    }

    if (nopipe)
	exit(0);

    /* Daemonize if the user requested it. */
    if (daemon)
      daemonize();

    /* Sleep for ten seconds if the user requested it. */
    if (sleepy)
	(void)sleep(10);

    /* Open the output file if the user requested it.  If the user
       requested '-R', we use the 'b' flag in fopen() to "do the right
       thing" in non-linux/unix OSes. */
    if (filename==NULL) {
      fp = stdout;
    } else {
      if (binary)
	fp = fopen(filename,"wb");
      else
	fp = fopen(filename,"w");

      if (fp == NULL) {
	(void)fprintf(stderr,
		      "gpspipe: unable to open output file:  %s\n",
		      filename);
	exit(1);
      }
    }

    /* Open the serial port and set it up. */
    if (serialport)
	open_serial(serialport);

    /*@ -nullpass @*/
    sock = netlib_connectsock(source.server, source.port, "tcp");
    if (sock == -1) {
	(void)fprintf(stderr,
		      "gpspipe: could not connect to gpsd %s:%s, %s(%d)\n",
		      server, port, strerror(errno), errno);
	exit(1);
    }
    /*@ +nullpass @*/

    /* ship the assembled options */
    wrote = write(sock, buf, strlen(buf));
    if ((ssize_t)strlen(buf) != wrote) {
	(void)fprintf(stderr, "gpspipe: write error, %s(%d)\n",
		      strerror(errno), errno);
	exit(1);
    }

    if ((isatty(STDERR_FILENO) == 0) || daemon)
	vflag = 0;

    for(;;) {
	int i = 0;
	int j = 0;
	int readbytes = 0;

	if (vflag)
	    spinner(vflag, l++);
	readbytes = (int)read(sock, buf, sizeof(buf));
	if (readbytes > 0) {
	    for (i = 0 ; i < readbytes ; i++) {
		char c = buf[i];
		if (j < (int)(sizeof(serbuf) - 1)) {
		    serbuf[j++] = buf[i];
		}
		if (new_line && timestamp) {
		    time_t now = time(NULL);

		    new_line = 0;
		    if (fprintf(fp, "%.24s :", ctime(&now)) <= 0) {
			(void)fprintf(stderr,
				      "gpspipe: write error, %s(%d)\n",
				      strerror(errno), errno);
			exit(1);
		    }
		}
		if (fputc(c, fp) == EOF) {
		    fprintf( stderr, "gpspipe: Write Error, %s(%d)\n",
			     strerror(errno), errno);
		    exit(1);
		}

		if (c == '\n') {
		    if (serialport != NULL) {
			if (write(fd_out, serbuf, (size_t)j) == -1) {
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
			(void)fprintf(stderr, "gpspipe: fflush Error, %s(%d)\n",
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
		(void) fprintf(stderr, "gpspipe: read error %s(%d)\n",
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

    exit(0);
#endif /* __UNUSED__ */
}

static void spinner (unsigned int v, unsigned int num) {
    char *spin = "|/-\\";

    (void)fprintf(stderr, "\010%c", spin[(num/(1<<(v-1))) % 4]);
    (void)fflush(stderr);
    return;
}
