/*
 * Copyright (c) 2005 Chris Kuethe <chris.kuethe@gmail.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "gpsflash.h"

char *progname;

int
main(int argc, char **argv){

	int ch;
	int lfd, ffd, pfd;
	int ls,  fs;
	int fflag = 0, lflag = 0, pflag = 0;
	struct stat sb;
	char *fname = NULL;
	char *lname = "dlgsp2.bin";
	char *port = NULL;
	char *warning;
	struct termios *term;
	sigset_t sigset;

	char *firmware = NULL;
	char *loader = NULL;

	progname = argv[0];

	while ((ch = getopt(argc, argv, "f:l:p:")) != -1)
		switch (ch) {
		case 'f':
			fname = optarg;
			fflag = 1;
			break;
		case 'l':
			lname = optarg;
			lflag = 1;
			break;
		case 'p':
			port = optarg;
			pflag = 1;
			break;
		default:
			usage();
			/* NOTREACHED */
		}

	argc -= optind;
	argv += optind;

	/* nasty little trick to hopefully make people read the manual */
	if (  ((warning = getenv("I_READ_THE_README")) == NULL) ||
	      (strcmp(warning, "why oh why didn't i take the blue pill") == 0 )){
		fprintf(stderr, "RTFM, luser!\n");
		unlink(progname);
		exit(1);
	}

	/* make sure we have meaningful flags */
	if (!(fflag && pflag))
		usage();
	
	/* Open the loader file */
	if((lfd = open(lname, O_RDONLY, 0444)) == -1)
		err(1, "open(%s)", lname);

	/* fstat() its file descriptor. Need the size, and avoid races */
	if(fstat(lfd, &sb) == -1)
		err(1, "fstat(%s)", lname);

	/* minimal sanity check on loader size. also prevents bad malloc() */
	ls = (int)sb.st_size;
	if ((ls < MIN_LD_SIZE) || (ls > MAX_LD_SIZE)){
		fprintf(stderr, "preposterous loader size: %d\n", ls);
		return 1;
	}

	/* malloc a loader buffer */
	if ((loader = malloc(ls)) == NULL)
		err(1, "malloc(%d)", ls);

	if ((read(lfd, loader, ls)) != ls)
		err(1, "read(%d)", ls);

	/* don't care if close fails - kernel will force close on exit() */
	close(lfd);

	/* Open the firmware image file */
	if((ffd = open(fname, O_RDONLY, 0444)) == -1)
		err(1, "open(%s)", fname);

	/* fstat() its file descriptor. Need the size, and avoid races */
	if(fstat(ffd, &sb) == -1)
		err(1, "fstat(%s)", fname);

	/* minimal sanity check on firmware size. also prevents bad malloc() */
	fs = (int)sb.st_size;
	if ((fs < MIN_FW_SIZE) || (fs > MAX_FW_SIZE)){
		fprintf(stderr, "preposterous firmware size: %d\n", fs);
		return 1;
	}

	/* malloc an image buffer */
	if ((firmware = malloc(fs+1)) == NULL)
		err(1, "malloc(%d)", fs);

	if ((read(ffd, firmware, fs)) != fs)
		err(1, "read(%d)", fs);

	firmware[fs] = '\0';

	/* don't care if close fails - kernel will force close on exit() */
	close(ffd);

	/* did we just read some S-records? */
	if (!((firmware[0] == 'S') && ((firmware[1] >= '0') && (firmware[1] <= '9')))){ /* srec? */
		fprintf(stderr, "%s: not an S-record file\n", fname);
		return(1);
	}

	/* malloc a loader buffer */
	if ((term = malloc(sizeof(struct termios))) == NULL)
		err(1, "malloc(%d)", sizeof(struct termios));

	/* Open the serial port, blocking is OK */
	if((pfd = open(port, O_RDWR | O_NOCTTY , 0600)) == -1)
		err(1, "open(%s)", port);

	/* call serialConfig to set control lines and termios bits */
	if(serialConfig(pfd, term, 38400) == -1)
		err(1, "serialConfig()");

	/* the firware upload defaults to 38k4, so lets go there */
	if(sirfSetProto(pfd, term, PROTO_SIRF, 38400) == -1)
		err(1, "sirfSetProto()");

	/* once we get here, we are uninterruptable. handle signals */
	sigemptyset(&sigset);
	sigaddset(&sigset, SIGINT);
	sigaddset(&sigset, SIGHUP);
	sigaddset(&sigset, SIGQUIT);
	sigaddset(&sigset, SIGTSTP);
	sigaddset(&sigset, SIGSTOP);
	sigaddset(&sigset, SIGKILL);

	if(sigprocmask(SIG_BLOCK, &sigset, NULL) == -1)
		err(1,"sigprocmask");

	/* send the magic update command */
	if(sirfSendUpdateCmd(pfd) == -1)
		err(1, "sirfSendUpdateCmd()");

	/* wait a moment for the receiver to switch to boot rom */
	sleep(2);

	/* send the bootstrap/flash programmer */
	if(sirfSendLoader(pfd, term, loader, ls) == -1)
		err(1, "sirfSendLoader()");

	/* again we wait, this time for our uploaded code to start running */
	sleep(2);

	/* and now, poke the actual firmware over */
	if(sirfSendFirmware(pfd, firmware, fs) == -1)
		err(1, "sirfSendFirmware()");

	/* waitaminnit, and drop back to NMEA@4800 for luser apps */
	sleep(5);
	if(sirfSetProto(pfd, term, PROTO_NMEA, 4800) == -1)
		err(1, "sirfSetProto()");

	/* return() from main(), to take advantage of SSP compilers */
	return 0;
}

void
usage(void){
	fprintf(stderr, "Usage: %s [-v] [-l <loader_file>] -p <tty> -f <firmware_file>\n", progname);
	fprintf(stderr, "	loader_file defaults to \"dlgsp2.bin\"\n");
	fprintf(stderr, "Receiver will be reset to 4800bps NMEA after flash\n");
	exit(1);
}

/*
	dlgsp2.bin looks for this header
	unsigned char hdr[] = "S00600004844521B\r\n";
*/
