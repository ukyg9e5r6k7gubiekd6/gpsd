#include <termios.h>
#include <sys/types.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <string.h>
#include <stdlib.h>

/*
 * Test to see if TIOCMIWAIT can be made to work
 * Call with the serial device name as first argument, mode 012 as second.
 *
 * Based on code fond here:
 * http://tech.groups.yahoo.com/group/ts-7000/message/803 
 */
int main(int argc, char **argv) 
{
    int fd;
    struct termios newtio;
    unsigned char rx[132];
    char *ourDev = argv[1];
    char *ourMode = argv[2];

    // try to open the serial port
    fd = open(ourDev, O_RDWR | O_NOCTTY | O_NDELAY);
    // how'd that go?
    if (fd < 0) {
	// not so good
	perror("Unable to open device /dev/ttyAM0\n");
	return 1;
    }
    fprintf(stderr, "Successfully opened serial device %s\n", ourDev);

    /* initialize the serial port */
    memset(&newtio, '\0', sizeof(newtio));
    newtio.c_cflag = CS8 | CREAD | CRTSCTS;
    tcflush(fd, TCIOFLUSH);
    tcsetattr(fd,TCSANOW,&newtio);

    // Figure out which mode we are operating in, based on the command
    // line argument.
    //
    // Operating Modes:
    // mode = 0 - Dump out serial data from port
    // mode = 1 - Use TIOCMIWAIT to detect changes on the DCD line -
    // mode = 2 - Use TIOCMGET to detect changes on the DCD line via polling

    // Figure out which mode we are in
    int mode = 0;
    if (argc > 2)
	mode = atoi(ourMode);

    // Select operation based on mode
    switch (mode) 
    {
    case 0:
	// just dump out any characters arriving on the serial port
	fprintf(stderr, "Testing Serial Interface. Dumping data from %s\n", ourDev);
	int readCnt;
	while (1) {
	    while (read(fd, rx, 132) > 0) {
		rx[131] = 0;
		fprintf(stderr, "%s", rx);
	    }
	    sleep(1);
	}
	break;

    case 1:
	// wait for DCD transition to be reported via interrupt - fails
	fprintf(stderr, "Testing TIOCMIWAIT. Waiting for DCD on %s\n", ourDev);
	while (ioctl(fd, TIOCMIWAIT, TIOCM_CAR) == 0) {
	    // Problem: The following lines is never executed
	    fprintf(stderr, "DCD Transition on %s\n", ourDev);
	}
	fprintf(stderr, "TIOCMIWAIT returns non zero value on %s!\n", ourDev);
	break;

    case 2:
	// poll the DCD line looking for transition; when found, report time
	// and various intervals between successive transitions.
	fprintf(stderr, "Testing TIOCMGET. Polling DCD on %s\n", ourDev);

	struct timeval tv_jw;
	int state, lastState;

	// get the current state of the DCD line
	if (ioctl(fd, TIOCMGET, &lastState) != 0) {
	    fprintf(stderr, "TIOCMGET fails on %s\n", ourDev);
	    exit(1);
	}
	// turn laststate into a boolean indicating presence or absence of DCD
	lastState = (int)((lastState & TIOCM_CAR) != 0);

	double lastTime = 0;
	// for computing average length of a second, as derived from the
	// rising edge of the DCD pulse
	double total = 0;
	int samples = 0;

	// loop forever
	while (1) {
	    // get the value of the serial lines
	    if (ioctl(fd, TIOCMGET, &state) != 0) {
		// abort on error
		fprintf(stderr, "TIOCMGET fails on %s\n", ourDev);
		exit(1);
	    }
	    // recover DCD state
	    state = (int)((state & TIOCM_CAR) != 0);

	    // Transition?
	    if (state != lastState) {
		// yes. Update the last state
		lastState = state;
		// Is this a leading (rising) edge?
		if (state == 1) {
		    // yes. let's call this the top of the second
		    // note the system time
		    (void)gettimeofday(&tv_jw,NULL);
		    // turn it into a double
		    double curTime = tv_jw.tv_sec + tv_jw.tv_usec/1.0e6;
		    // how long since the last transition?
		    double diff = curTime - lastTime;
		    // Update time of 'last' DCD state transition
		    lastTime = curTime;
		    // diff should be (really close to) one second
		    // is diff within reason? (Sometimes transitions appear to be missed)
		    if (diff < 1.5) {
			// update for averaging
			total += diff;
			samples++;
			// report on the times associated with this transition
			fprintf(stderr, "DCD transition on %s: %d: %.6f, %6f, %6f\n",
				ourDev, state, curTime, diff, total/samples);
		    }
		    else {
			fprintf(stderr, "DCD transition on %s: %d: %.6f, %6f - wacky diff\n",
				ourDev, state, curTime, diff);
		    }
		}
	    }
	    // now sleep for a (very) little while
	    tv_jw.tv_sec = 0;
	    tv_jw.tv_usec = 1;
	    int retval_jw = select(1, NULL, NULL, NULL, &tv_jw);
	}
	break;

    default:
	fprintf(stderr, "Unknown mode\n");
    }

    close(fd);
    return 0;
}
