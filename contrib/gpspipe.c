/*
 * gpspipe
 *
 * a simple program to connect to a gpsd daemon and dump the received data
 * to stdout
 *
 * This will dump the raw NMEA from gpsd to stdout
 *      gpspipe -r
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
 * TODO
 *      add "-p [device]" 
 *          This would force gpspipe to create a bidirectional pipe device
 *          for output.  Then programs that expect to connect to a raw GPS
 *          device could conenct to that.
 *
 *      man pages
 *      use autoconf to create configure, Makefile, etc.
 */

#include <errno.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include "../gpsd.h"

static void usage(const char *prog) {
	fprintf(stderr, "%s: connect to local gpsd and dump data to stdout\n\n"
		"-h show this help\n"
		"-r Dump raw NMEA\n"
	        "-w Dump gpsd native data\n\n"
	        "-t time stamp the data\n\n"
	        "-n [count] exit after count packets\n\n"
	        "You must specify one, or both, of -r/-w\n",
		prog);
}

int main( int argc, char **argv) {

	int s = 0;
        char buf[4096];
	char *cstr = NULL;
        unsigned int wrote = 0;
        int dump_nmea = 0;
        int dump_gpsd = 0;
        int timestamp = 0;
	int new_line = 1;
	long count = -1;
	char option;
	extern char *optarg;


	while ((option = getopt(argc, argv, "?hrwtn:")) != -1) {
		switch (option) {
		case 'n':
			count = strtol(optarg, 0, 0);
			break;
		case 'r':
			dump_nmea = 1;
			break;
		case 't':
			timestamp = 1;
			break;
		case 'w':
			dump_gpsd = 1;
			break;
		case '?':
		case 'h':
		default:
			usage( argv[0] );
			exit(1);
		}
	}
	if ( dump_nmea ) {
		if ( dump_gpsd ) {
			cstr = "rw\n";
		} else {
			cstr = "r\n";
		}
	} else if ( dump_gpsd ) {
		cstr = "w\n";
	} else {
		usage( argv[0] );
		exit(1);
	}
	s = netlib_connectsock( "127.0.0.1", "2947", "tcp");
	if ( s < 0 ) {
		fprintf( stderr, "%s: could not connect to gpsd, %s(%d)\n"
			, argv[0] , strerror(errno), errno);
		exit (1);
	}

	wrote = write( s, cstr, strlen(cstr) );
	if ( strlen(cstr) != wrote ) {
		fprintf( stderr, "%s: write error, %s(%d)\n", argv[0]
			, strerror(errno), errno);
		exit (1);
	}

	for(;;) {
		int i = 0;
		int readbytes = 0;

		readbytes = read( s, buf, sizeof(buf));
		if ( readbytes > 0 ) {
		    for ( i = 0 ; i < readbytes ; i++ ) {
			char c = buf[i];
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
		
			if ( '\n' == c ) {
			    new_line = 1;
			    /* flush after eveery good line */
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
	exit(0);
}
