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
 * bad code by: Gary E. Miller <gem@rellim.com>
 * all rights given to the gpsd project to release under whatever open source
 * license they use.  A thank would be nice if you use this code.
 *
 * just needs to be linked to netlib like this:
 *	gcc gpspipe.c ../netlib.c -o gpspipe
 *
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <fcntl.h>
#include <termios.h>
#include "gpsd_config.h"
#include "gpsd.h"


/* NMEA-0183 standard baud rate */
#define BAUDRATE B4800

/* POSIX compliant source */
#define _POSIX_SOURCE 1

/* Serial port vars */
static struct termios oldtio,newtio;
static int fd;
#define SERBUF 255
static char serbuf[SERBUF];

/* open the serial port and set it up */
static int open_serial(char* device) {

	/*  Open modem device for reading and writing and not as controlling
	 *  tty. */
	if((fd = open(device, O_RDWR|O_NOCTTY)) < 0) {
		fprintf(stderr,"Error opening serial port\n");
		exit(1);
	}

	/* Save current serial port settings for later */
	if ( tcgetattr(fd, &oldtio) != 0 ) {
		fprintf(stderr,"Error reading serial port settings\n");
		exit(1);
	}

	/* Clear struct for new port settings. */
	/*@i@*/bzero(&newtio, sizeof(newtio));

	/* make it raw */
	(void)cfmakeraw(&newtio);
	/* set speed */
	/*@i@*/(void)cfsetospeed(&newtio, BAUDRATE);
	 
	/* Clear the modem line and activate the settings for the port. */
	(void)tcflush(fd,TCIFLUSH);
	if ( tcsetattr(fd,TCSANOW,&newtio) != 0 ) {
		(void)fprintf(stderr,"Error setting serial port settings\n");
		exit(1);
	}

	return(fd);
}


/* Send a string to the serial port. */
static int send_string(char *buf, size_t len) {

	/* Send the string and  Return the result. */
	return ((int)write(fd, buf, len ) );
}


static void usage(void) {
	fprintf(stderr, "Usage: gpspipe [OPTIONS] [server[:port]]\n\n"
	        "SVN ID: $Id$ \n"
		"-h show this help\n"
		"-r Dump raw NMEA\n"
		"-R Dump super-raw mode (gps binary)\n"
	        "-w Dump gpsd native data\n"
	        "-j turn on server-side buffering\n"
	        "-t time stamp the data\n"
		"-s [serial dev] emulate a 4800bps NMEA GPS on serial port (use with '-r')\n"
	        "-n [count] exit after count packets\n"
	        "-V print version and exit\n\n"
	        "You must specify one, or both, of -r/-w\n"
		);
}

int main( int argc, char **argv) {

	int s = 0;
        char buf[4096];
	char *cstr = NULL;
	char *jstr = NULL;
        ssize_t wrote = 0;
        bool dump_super_raw = false;
        bool dump_nmea = false;
        bool dump_gpsd = false;
        bool jitter_flag = false;
        bool timestamp = false;
	bool new_line = true;
	long count = -1;
	int option;
        char *arg = NULL, *colon1, *colon2, *device = NULL; 
        char *port = DEFAULT_GPSD_PORT, *server = "127.0.0.1";
	//extern char *optarg;

	char *serialport = NULL;

	while ((option = getopt(argc, argv, "?hrRwjtVn:s:")) != -1) {
		switch (option) {
		case 'n':
			count = strtol(optarg, 0, 0);
			break;
		case 'r':
			dump_nmea = true;
			break;
		case 'R':
			dump_super_raw = true;
			break;
		case 't':
			timestamp = true;
			break;
		case 'w':
			dump_gpsd = true;
			break;
		case 'j':
			jitter_flag = true;
			break;
		case 'V':
	                (void)fprintf(stderr, "%s: SVN ID: $Id$ \n", argv[0]);
			exit(0);
		case 's':
		        serialport = optarg;
		        break;
		case '?':
		case 'h':
		default:
			usage( );
			exit(1);
		}
	}

	if( serialport != NULL && !dump_nmea ) {
	  fprintf(stderr,"Use of '-s' requires '-r'\n");
	  exit(1);
	}

	if ( dump_super_raw ) {
		cstr = "R=2\n";
	        /* super raw overrides NMEA and GPSD modes */
		dump_nmea = false;
		dump_gpsd = false;
	} else if ( dump_nmea ) {
		if ( dump_gpsd ) {
			cstr = "rw\n";
		} else {
			cstr = "r\n";
		}
	} else if ( dump_gpsd ) {
		cstr = "w\n";
	} else {
		usage( );
		exit(1);
	}
	/* Grok the server, port, and device. */
	/*@ -branchstate @*/
	if (optind < argc) {
		arg = strdup(argv[optind]);
		/*@i@*/colon1 = strchr(arg, ':');
		server = arg;
		if (colon1 != NULL) {
			if (colon1 == arg) {
				server = NULL;
			} else {
				*colon1 = '\0';
			}
			port = colon1 + 1;
			colon2 = strchr(port, ':');
			if (colon2 != NULL) {
				if (colon2 == port) {
					port = NULL;
				} else {
					*colon2 = '\0';
				}
				device = colon2 + 1;
			}
		}
		colon1 = colon2 = NULL;
	}
	/*@ +branchstate @*/

	/* Open the serial port and set it up. */
	if(serialport) {
	    (int)open_serial(serialport);
	}

	/*@ -nullpass @*/
	s = netlib_connectsock( server, port, "tcp");
	if ( s < 0 ) {
		fprintf( stderr, "%s: could not connect to gpsd %s:%s, %s(%d)\n"
			, argv[0] , server, port, strerror(errno), errno);
		exit (1);
	}
	/*@ +nullpass @*/

	if ( jitter_flag ) {
	  jstr = "j1\n";
	  wrote = write( s, "jstr", strlen("jstr") );
	  if ( (ssize_t)strlen("jstr") != wrote ) {
	    fprintf( stderr, "%s: write error, %s(%d)\n", argv[0]
		     , strerror(errno), errno);
	    exit (1);
	  }
	}
	  

	wrote = write( s, cstr, strlen(cstr) );
	if ( (ssize_t)strlen(cstr) != wrote ) {
		fprintf( stderr, "%s: write error, %s(%d)\n", argv[0]
			, strerror(errno), errno);
		exit (1);
	}

	for(;;) {
		int i = 0;
		int j = 0;
		int readbytes = 0;

		readbytes = (int)read( s, buf, sizeof(buf));
		if ( readbytes > 0 ) {
		    for ( i = 0 ; i < readbytes ; i++ ) {
			char c = buf[i];
			if( j < (SERBUF - 1) ) {
				serbuf[j++] = buf[i];
			}
			if ( new_line && timestamp ) {
				time_t now = time(NULL);

				new_line = 0;
				if ( 0 > fprintf( stdout, "%.24s :",
					 ctime( &now )) ) {
					fprintf( stderr
						, "%s: Write Error, %s(%d)\n"
						, argv[0]
						, strerror(errno), errno);
					exit (1);
				}
			}
			if ( EOF == fputc( c, stdout) ) {
				fprintf( stderr, "%s: Write Error, %s(%d)\n"
					, argv[0]
					, strerror(errno), errno);
				exit (1);
			}

		
			if ( c == '\n' ) {

			    if( serialport != NULL) {
			      if ( -1 == send_string( serbuf, (size_t)j )) {
			        fprintf( stderr, "%s: Serial port write Error, %s(%d)\n"
				       , argv[0]
				       , strerror(errno), errno);
			        exit (1);
			      }
			      j = 0;
			    }

			    new_line = true;
			    /* flush after every good line */
			    if (  fflush( stdout ) ) {
				fprintf( stderr, "%s: fflush Error, %s(%d)\n"
					, argv[0]
					, strerror(errno), errno);
				exit (1);
			    }
			    if ( 0 <= count ) {
			        if ( 0 >= --count ) {
				    /* completed count */
			            exit(0);
			        }
			    }
			       
			}
		    }
		} else if ( readbytes < 0 ) {
			fprintf( stderr, "%s: Read Error %s(%d)\n", argv[0]
				, strerror(errno), errno);
		
			exit(1);
		}
	}

#ifdef __UNUSED__
	if( serialport != NULL ) {
		/* Restore the old serial port settings. */
		if ( tcsetattr(fd, TCSANOW, &oldtio) != 0 ) {
			fprintf(stderr, "Error restoring serial port settings\n");
			exit(1);
		}
	}

	exit(0);
#endif /* __UNUSED__ */  
}
