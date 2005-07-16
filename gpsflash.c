/*
 * This is the GPS-type-independent part of the gpsflash program.
 *
 * Copyright (c) 2005 Chris Kuethe <chris.kuethe@gmail.com>
 */
#include "gpsd.h"
#include "gpsflash.h"

/* block size when writing to the serial port. related to FIFO size */
#define WRBLK 128

static char *progname;

static void
usage(void){
	fprintf(stderr, "Usage: %s [-v] [-l <loader_file>] -p <tty> -f <firmware_file>\n", progname);
}

int
serialSpeed(int pfd, struct termios *term, int speed){
	int rv;
	int r = 0;

	switch(speed){
#ifdef B115200
	case 115200:
		speed = B115200;
		break;
#endif
#ifdef B57600
	case 57600:
		speed = B57600;
		break;
#endif
	case 38400:
		speed = B38400;
		break;
#ifdef B28800
	case 28800:
		speed = B28800;
		break;
#endif
	case 19200:
		speed = B19200;
		break;
#ifdef B14400
	case 14400:
		speed = B14400;
		break;
#endif
	case 9600:
		speed = B9600;
		break;
	case 4800:
		speed = B9600;
		break;
	default:
		errno = EINVAL;
		return -1;
	}

	/* set UART speed */
	(int)tcgetattr(pfd, term);
	/*@ ignore @*/
	cfsetispeed(term, speed);
	cfsetospeed(term, speed);
	/*@ end @*/
	while (((rv = tcsetattr(pfd, TCSAFLUSH, term)) == -1) && \
	    (errno == EINTR) && (r < 3)) {
		/* retry up to 3 times on EINTR */
		(void)usleep(1000);
		r++;
	}

	if(rv == -1)
		return -1;
	else
		return 0;
}


int
serialConfig(int pfd, struct termios *term, int speed){
	int rv;
	int r = 0;

	/* get the current terminal settings */
	(void)tcgetattr(pfd, term);
	/* set the port into "raw" mode. */
	/*@i@*/cfmakeraw(term);
	term->c_lflag &=~ (ICANON);
	/* Enable serial I/O, ignore modem lines */
	term->c_cflag |= (CLOCAL | CREAD);
	/* No output postprocessing */
	term->c_oflag &=~ (OPOST);
	/* 8 data bits */
	term->c_cflag |= CS8;
	term->c_iflag &=~ (ISTRIP);
	/* No parity */
	term->c_iflag &=~ (INPCK);
	term->c_cflag &=~ (PARENB | PARODD);
	/* 1 Stop bit */
	term->c_cflag &=~ (CSIZE | CSTOPB);
	/* No flow control */
	term->c_iflag &=~ (IXON | IXOFF);
#if defined(CCTS_OFLOW) && defined(CRTS_IFLOW) && defined(MDMBUF)
	term->c_oflag &=~ (CCTS_OFLOW | CRTS_IFLOW | MDMBUF);
#endif
#if defined(CRTSCTS)
	term->c_oflag &=~ (CRTSCTS);
#endif

	/* we'd like to read back at least 2 characters in .2sec */
	/*@i@*/term->c_cc[VMIN] = 2;
	/*@i@*/term->c_cc[VTIME] = 2;

	/* apply all the funky control settings */
	while (((rv = tcsetattr(pfd, TCSAFLUSH, term)) == -1) && \
	    (errno == EINTR) && (r < 3)) {
		/* retry up to 3 times on EINTR */
		(void)usleep(1000);
		r++;
	}

	if(rv == -1)
		return -1;
	
	/* and if that all worked, try change the UART speed */
	return serialSpeed(pfd, term, speed);
}

int
binary_send(int pfd, char *data, size_t ls){
	unsigned char *msg;
	size_t nbr, nbs, nbx;
	ssize_t r;

	/*@ -compdef @*/
	if((msg = malloc(ls+10)) == NULL){
		return -1; /* oops. bail out */
	}

	nbr = ls+10; nbs = WRBLK ; nbx = 0;
	while(nbr){
		if(nbr > WRBLK )
			nbs = WRBLK ;
		else
			nbs = nbr;

r0:		if((r = write(pfd, msg+nbx, nbs)) == -1){
			if (errno == EAGAIN){ /* retry */
				(void)tcdrain(pfd); /* wait a moment */
				errno = 0; /* clear errno */
				nbr -= r; /* number bytes remaining */
				nbx += r; /* number bytes sent */
				goto r0;
			} else {
			        (void)free(msg);
				return -1; /* oops. bail out */
			}
		}
		nbr -= r;
		nbx += r;
	}
	/*@ +compdef @*/

	(void)free(msg);
	return 0;
}


int
srecord_send(int pfd, char *data, size_t len){
	int r, i;
	size_t tl;
	char sendbuf[85], recvbuf[8];

	/* srecord loading is interactive. send line, get reply */
	/* when sending S-records, check for SA/S5 or SE */

	memset(recvbuf, 0, 8);
	i = 0;
	while(strlen(data)){
		/* grab a line of firmware, ignore line endings */
		if ((r = (int)strlen(data))){
			memset(sendbuf,0,85);
			if((r = sscanf(data, "%80s", sendbuf)) == EOF)
				return 0;

			tl = strlen(sendbuf);
			if ((tl < 1) || (tl > 80))
				return -1;

			data += tl;
			len -= tl;

			while((data[0] != 'S') && (data[0] != '\0'))
				data++;

			sendbuf[tl] = '\r';
			sendbuf[tl+1] = '\n';
			tl += 2;

			if ((++i % 1000) == 0)
				printf ("%6d\n", i);

			(void)tcflush(pfd, TCIFLUSH);
			if((r = (int)write(pfd, sendbuf, tl+2)) != (int)tl+2)
				return -1; /* oops. bail out */

			(void)tcdrain(pfd);
			if((r = (int)read(pfd, recvbuf, 7)) == -1)
				return -1; /* oops. bail out */

			if (!((recvbuf[0] == 'S') && ((recvbuf[1] == 'A') || (recvbuf[1] == '5'))))
				return -1; /* oops. bail out */
		}
	}
	return 0;
}

int
main(int argc, char **argv){

	int ch;
	int lfd, ffd, pfd;
	size_t ls,  fs;
	bool fflag = false, lflag = false, pflag = false;
	struct stat sb;
	struct flashloader_t *gpstype = &sirf_type;
	char *fname = NULL;
	char *lname = (char *)sirf_type.flashloader;
	char *port = NULL;
	char *warning;
	struct termios term;
	sigset_t sigset;

	char *firmware = NULL;
	char *loader = NULL;

	progname = argv[0];

	while ((ch = getopt(argc, argv, "f:l:p:")) != -1)
		switch (ch) {
		case 'f':
			fname = optarg;
			fflag = true;
			break;
		case 'l':
			lname = optarg;
			lflag = true;
			break;
		case 'p':
			port = optarg;
			pflag = true;
			break;
		default:
			usage();
			exit(0);
			/* NOTREACHED */
		}

	argc -= optind;
	argv += optind;

	/* nasty little trick to hopefully make people read the manual */
	if (  ((warning = getenv("I_READ_THE_WARNING")) == NULL) ||
	      (strcmp(warning, "why oh why didn't i take the blue pill") == 0 )){
		printf("\nThis program rewrites your receiver's flash ROM.\n");
		printf("If done improperly this will permanently ruin your\n");
		printf("receiver. We insist you read the gpsflash manpage\n");
		printf("before you break something.\n\n");
		exit(1);
	}

	/* make sure we have meaningful flags */
	if (!(fflag && pflag) || fname == NULL || port == NULL) {
		usage();
		return 1;
	}
	
	/* Open the loader file */
	if((lfd = open(lname, O_RDONLY, 0444)) == -1) {
		gpsd_report(0, "open(%s)\n", lname);
		return 1;
	}

	/* fstat() its file descriptor. Need the size, and avoid races */
	if(fstat(lfd, &sb) == -1) {
		gpsd_report(0, "fstat(%s)\n", lname);
		return 1;
	}

	/* minimal sanity check on loader size. also prevents bad malloc() */
	ls = (size_t)sb.st_size;
	if ((ls < gpstype->min_loader_size)||(ls > gpstype->max_loader_size)){
		gpsd_report(0, "preposterous loader size: %d\n", ls);
		return 1;
	}

	/* malloc a loader buffer */
	if ((loader = malloc(ls)) == NULL) {
		gpsd_report(0, "malloc(%d)\n", ls);
		return 1;
	}

	if (read(lfd, loader, ls) != (ssize_t)ls) {
		(void)free(loader);
		gpsd_report(0, "read(%d)\n", ls);
		return 1;
	}

	/* don't care if close fails - kernel will force close on exit() */
	(void)close(lfd);

	/* Open the firmware image file */
	if((ffd = open(fname, O_RDONLY, 0444)) == -1) {
		(void)free(loader);
		gpsd_report(0, "open(%s)]n", fname);
		return 1;
	}
	if(fstat(ffd, &sb) == -1) {
		(void)free(loader);
		gpsd_report(0, "fstat(%s)\n", fname);
		return 1;
	}

	/* minimal sanity check on firmware size. also prevents bad malloc() */
	fs = (size_t)sb.st_size;
	if ((fs < gpstype->min_firmware_size) || (fs > gpstype->max_firmware_size)){
		(void)free(loader);
		gpsd_report(1, "preposterous firmware size: %d\n", fs);
		return 1;
	}

	/* malloc an image buffer */
	if ((firmware = malloc(fs+1)) == NULL) {
		(void)free(loader);
		gpsd_report(0, "malloc(%u)\n", (unsigned)fs);
		return 1;
	}

	if (read(ffd, firmware, fs) != (ssize_t)fs) {
		(void)free(loader);
		(void)free(firmware);
		gpsd_report(0, "read(%u)\n", (unsigned)fs);
		return 1;
	}

	firmware[fs] = '\0';

	/* don't care if close fails - kernel will force close on exit() */
	(void)close(ffd);

	/* did we just read some S-records? */
	if (!((firmware[0] == 'S') && ((firmware[1] >= '0') && (firmware[1] <= '9')))){ /* srec? */
		(void)free(loader);
		(void)free(firmware);
		gpsd_report(0, "%s: not an S-record file\n", fname);
		return 1;
	}

	/* Open the serial port, blocking is OK */
	if((pfd = open(port, O_RDWR | O_NOCTTY , 0600)) == -1) {
		(void)free(loader);
		(void)free(firmware);
		gpsd_report(0, "open(%s)\n", port);
		return 1;
	}

	/* call serialConfig to set control lines and termios bits */
	memset(&term, 0, sizeof(term));
	if(serialConfig(pfd, &term, 38400) == -1) {
		(void)free(loader);
		(void)free(firmware);
		gpsd_report(0, "serialConfig()\n");
		return 1;
	}

	if(gpstype->port_setup(pfd, &term) == -1) {
		(void)free(loader);
		(void)free(firmware);
		gpsd_report(0, "port_setup()\n");
		return 1;
	}

	/* once we get here, we are uninterruptable. handle signals */
	(void)sigemptyset(&sigset);
	(void)sigaddset(&sigset, SIGINT);
	(void)sigaddset(&sigset, SIGHUP);
	(void)sigaddset(&sigset, SIGQUIT);
	(void)sigaddset(&sigset, SIGTSTP);
	(void)sigaddset(&sigset, SIGSTOP);
	(void)sigaddset(&sigset, SIGKILL);

	if(sigprocmask(SIG_BLOCK, &sigset, NULL) == -1) {
		(void)free(loader);
		(void)free(firmware);
		gpsd_report(0,"sigprocmask\n");
		return 1;
	}

	/* send the command to begin the update */
	if(gpstype->stage1_command!=NULL && (gpstype->stage1_command(pfd) == -1)) {
		(void)free(loader);
		(void)free(firmware);
		gpsd_report(0, "Stage 1 update command\n");
		return 1;
	}

	/* send the bootstrap/flash programmer */
	if(gpstype->loader_send(pfd, &term, loader, ls) == -1) {
		(void)free(loader);
		(void)free(firmware);
		gpsd_report(0, "Loader send\n");
		return 1;
	}
	(void)free(loader);

	/* send any command needed to demarcate the two loads */
	if(gpstype->stage2_command!=NULL && (gpstype->stage2_command(pfd) == -1)) {
	    (void)free(firmware);
	    gpsd_report(0, "Stage 2 update command\n");
	    return 1;
	}

	/* and now, poke the actual firmware over */
	if(gpstype->firmware_send(pfd, firmware, fs) == -1) {
	    (void)free(firmware);
	    gpsd_report(0, "Firmware send\n");
	    return 1;
	}
	(void)free(firmware);

	/* send any command needed to finish the firmware load */
	if(gpstype->stage3_command!=NULL && (gpstype->stage3_command(pfd) == -1)) {
	    gpsd_report(0, "Stage 3 update command\n");
	    return 1;
	}

	if(sigprocmask(SIG_UNBLOCK, &sigset, NULL) == -1) {
		gpsd_report(0,"sigprocmask\n");
		return 1;
	}

	/* type-defined wrapup, take our tty to GPS's post-flash settings */
	if(gpstype->port_wrapup(pfd, &term) == -1) {
		gpsd_report(0, "port_wrapup()\n");
		return 1;
	}

	/* return() from main(), to take advantage of SSP compilers */
	return 0;
}

