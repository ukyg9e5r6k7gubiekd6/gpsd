/*
 * gpscheck.c -- test to see if specified device is a GPS
 *
 * This will detect NMEA devices at 4800 baud or up, 8N1, including
 * all SiRF-II-based GPS mice.  It won't find newer Garmin or old
 * Zodiac GPSes, which speak binary protocols; nor will it find
 * oddballs like the San Jose Navigation FV18 that run at 7N2.
 * However, it should catch over 80% of the consumer-grade GPSes
 * available in 2005.
 *
 * Note: Because of the delays to let the device settle, this function 
 * can take 5 seconds to run when the device is not a GPS.
 *
 * Return value: -1 if any of the TTY mode sets fails, 0 if they succeeed
 * but it's not a GPS, one of the values B4800, B9600, B19200, or B38400
 * if it's a GPS. 
 *
 * Compile with -DTESTMAIN to produce a test executable that takes the name
 * of the device as a command-line argument.
 *
 * By Eric S. Raymond, February 2005.
 */

#include <unistd.h>
#include <stdlib.h>
#include <termios.h>
#include <errno.h>
#include <ctype.h>
#include <string.h>
#include <fcntl.h>
#include <stdio.h>

#define MAX_NMEA	82	/* max chars per NMEA sentence */

int gpscheck(int ttyfd)
{
    struct termios ttyset, ttyset_old;
    /* every rate we're likely to see on a GPS */
    static unsigned int rates[] = {B4800, B9600, B19200, B38400};
    unsigned int *ip;

    /* Save original terminal parameters */
    if (tcgetattr(ttyfd,&ttyset_old) != 0)
      return -1;
    memcpy(&ttyset, &ttyset_old,sizeof(ttyset));
    ttyset.c_cflag &= ~(PARENB | CRTSCTS);
    ttyset.c_cflag |= (CSIZE & CS8) | CREAD | CLOCAL;
    ttyset.c_iflag = ttyset.c_oflag = ttyset.c_lflag = (tcflag_t) 0;
    ttyset.c_oflag = (ONLCR);

    for (ip = rates; ip < rates + sizeof(rates)/sizeof(rates[0]); ip++)
    {
	char	buf[MAX_NMEA * 3 + 1], *sp, csum[3];
	int	n;
	unsigned char sum;

	/* might be able to avoid delay if device started at 4800 */
	if (cfgetispeed(&ttyset) != *ip)
	{
	    tcflush(ttyfd, TCIOFLUSH);
	    cfsetispeed(&ttyset, (speed_t)*ip);
	    if (tcsetattr(ttyfd, TCSANOW, &ttyset) != 0)
		return -1;
	    tcflush(ttyfd, TCIOFLUSH);

#ifdef TESTMAIN
	    fprintf(stderr, "gpscheck: checking at rate %d\n", *ip);
#endif /* TESTMAIN */
	    /*
	     * Give the GPS and UART this much time to settle and ship
	     * some data before trying to read after open or baud rate
	     * change.  Less than 1.25 seconds doesn't work on most
	     * UARTs.
	     */
	    usleep(1250000);
	}

	/* assumes the fd is opened for ordinary (blocking) read */
	if ((n = read(ttyfd, buf, sizeof(buf)-1)) == -1)
	    return -1;

	/*
	 * Stuff that is specific to NMEA devices starts here.
	 */

	/* If no valid NMEA prefix in the read buffer, crap out */
	if (!(sp = strstr(buf, "$GP"))) {
#ifdef TESTMAIN
	    fprintf(stderr, "gpscheck: no NMEA prefix found\n");
#endif /* TESTMAIN */
	    tcsetattr(ttyfd, TCSAFLUSH, &ttyset_old);
	    continue;
	}

	/* Check to see if we actually have a valid NMEA packet here. */
	sum = 0;
	for (++sp; *sp != '*' && *sp != '\0'; sp++) {
	    if (!isascii(*sp)) {
#ifdef TESTMAIN
	    fprintf(stderr, "gpscheck: trailing garbage in buffer\n");
#endif /* TESTMAIN */
		tcsetattr(ttyfd, TCSAFLUSH, &ttyset_old);
		continue;
	    }
	    sum ^= *sp;
	}
	sprintf(csum, "%02X", sum);
	if (*sp == '\0' 
	    	|| toupper(csum[0])!=toupper(sp[1])
	    	|| toupper(csum[1])!=toupper(sp[2])) {
#ifdef TESTMAIN
	    fprintf(stderr, "gpscheck: checksum incorrect\n");
#endif /* TESTMAIN */
	    tcsetattr(ttyfd, TCSAFLUSH, &ttyset_old);
	    continue;
	}

	/* NMEA-device-specic stuff ends here */

	/* passed all tests, looks like GPS */
	switch (*ip) {
	case B4800:  return(4800);
	case B9600:  return(9600);
	case B19200: return(19200);
	default: return(38400);
	}
    }

    return 0;
}

#ifdef TESTMAIN
int main(int argc, char **argv)
{
    int fd, st;

    fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
	perror("gpscheck");
	exit(0);
    }
    if ((st = gpscheck(fd)) > 0)
	printf("%s appears to be a GPS.\n", argv[1]);
    else
	printf("%s does not appear to be a GPS.\n", argv[1]);

    close(fd);
    exit(st <= 0);
}
#endif /* TESTMAIN */

/*
Local Variables:
compile-command: "cc -DTESTMAIN gpscheck.c -o gpscheck"
End:
*/
