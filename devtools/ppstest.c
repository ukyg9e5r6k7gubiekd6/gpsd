#include <termios.h>
#include <sys/types.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>

/*
 * Test to see if TIOCMGET/TIOCMIWAIT can be made to work
 * Call with the serial device name argument,  and possibly -p or -w options
 *
 * Based on code fond here:
 * http://tech.groups.yahoo.com/group/ts-7000/message/803 
 */
int main(int argc, char **argv) 
{
    int fd;
    struct termios newtio;
    unsigned char rx[132];
    enum {dump, poll, wait} mode = dump;
    int option;
    char *device;

    while ((option = getopt(argc, argv, "pw")) != -1) {
	switch (option) {
	case 'p':            /* poll for PPS */
	    mode = poll;
	    break;
	case 'w':            /* wait for PPS state change */ 
	    mode = wait;
	    break;	    
	}
    }
    device = argv[optind];

    // try to open the serial port
    fd = open(device, O_RDWR | O_NOCTTY | O_NDELAY);
    // how'd that go?
    if (fd < 0) {
	// not so good
	perror("Unable to open device /dev/ttyAM0\n");
	return 1;
    }
    fprintf(stderr, "Successfully opened serial device %s\n", device);

    /* initialize the serial port */
    memset(&newtio, '\0', sizeof(newtio));
    newtio.c_cflag = CS8 | CREAD | CRTSCTS;
    tcflush(fd, TCIOFLUSH);
    tcsetattr(fd,TCSANOW,&newtio);

    // Figure out which mode we are operating in, based on the command
    // line argument.
    //
    // Operating Modes:
    // mode = dump - Dump out serial data from port
    // mode = wait - Use TIOCMIWAIT to detect changes on the DCD line -
    // mode = poll - Use TIOCMGET to detect changes on the DCD line via polling

    // Select operation based on mode
    switch (mode) 
    {
    case dump:
	// just dump out any characters arriving on the serial port
	fprintf(stderr, "Testing Serial Interface. Dumping data from %s\n", device);
	int readCnt;
	while (1) {
	    while (read(fd, rx, 132) > 0) {
		rx[131] = 0;
		fprintf(stderr, "%s", rx);
	    }
	    sleep(1);
	}
	break;

    case wait:
	// wait for DCD transition to be reported via interrupt - fails
	fprintf(stderr, "Testing TIOCMIWAIT. Waiting for DCD on %s\n", device);
	while (ioctl(fd, TIOCMIWAIT, TIOCM_CAR) == 0) {
	    // Problem: The following lines is never executed
	    fprintf(stderr, "DCD Transition on %s\n", device);
	}
	fprintf(stderr, "TIOCMIWAIT returns non zero value on %s!\n", device);
	break;

    case poll:
	// poll the DCD line looking for transition; when found, report time
	// and various intervals between successive transitions.
	fprintf(stderr, "Testing TIOCMGET. Polling DCD on %s\n", device);

	struct timeval tv_jw;
	int state, lastState;

	// get the current state of the DCD line
	if (ioctl(fd, TIOCMGET, &lastState) != 0) {
	    fprintf(stderr, "TIOCMGET fails on %s\n", device);
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
		fprintf(stderr, "TIOCMGET fails on %s\n", device);
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
				device, state, curTime, diff, total/samples);
		    }
		    else {
			fprintf(stderr, "DCD transition on %s: %d: %.6f, %6f - wacky diff\n",
				device, state, curTime, diff);
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
	(void)fprintf(stderr, "Unknown mode\n");
    }

    (void)close(fd);
    return 0;
}
