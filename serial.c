#include "config.h"
#include <unistd.h>
#include <string.h>
#include <fcntl.h>

#if defined (HAVE_SYS_TERMIOS_H)
#include <sys/termios.h>
#else
#if defined (HAVE_TERMIOS_H)
#include <termios.h>
#endif
#endif

#include "gps.h"
#include "gpsd.h"

static struct termios ttyset, ttyset_old;

static int set_baud(long baud)
{
    int speed;

    if (baud < 200)
      baud *= 1000;
    if (baud < 2400)
      speed = B1200;
    else if (baud < 4800)
      speed = B2400;
    else if (baud < 9600)
      speed = B4800;
    else if (baud < 19200)
      speed = B9600;
    else if (baud < 38400)
      speed = B19200;
    else
      speed = B38400;

    return speed;
}


int gpsd_open(char *device_name, int device_speed, int stopbits)
{
    int ttyfd;

    gpsd_report(1, "opening GPS data source at %s\n", device_name);
    ttyfd = open(device_name, O_RDWR | O_NONBLOCK);
    if (ttyfd < 0)
	return -1;

    if (isatty(ttyfd)) {
	gpsd_report(1, "setting speed %d, 8 bits, no parity\n", device_speed);
	/* Save original terminal parameters */
	if (tcgetattr(ttyfd,&ttyset_old) != 0)
	  return -1;

	memcpy(&ttyset, &ttyset_old, sizeof(ttyset));

	device_speed = set_baud(device_speed);
	cfsetispeed(&ttyset, (speed_t)device_speed);
	cfsetospeed(&ttyset, (speed_t)device_speed);

	ttyset.c_cflag &= ~(PARENB | CRTSCTS);
	ttyset.c_cflag |= (CSIZE & (stopbits==2 ? CS7 : CS8)) | CREAD | CLOCAL;
	ttyset.c_iflag = ttyset.c_oflag = ttyset.c_lflag = (tcflag_t) 0;
	ttyset.c_oflag = (ONLCR);
	if (tcsetattr(ttyfd, TCSANOW, &ttyset) != 0)
	    return -1;
    }
    return ttyfd;
}

void gpsd_close(int ttyfd)
{
    if (ttyfd != -1) {
	if (isatty(ttyfd)) {
#if defined (USE_TERMIO)
	    ttyset.c_cflag = CBAUD & B0;
#else
	    ttyset.c_ispeed = B0;
	    ttyset.c_ospeed = B0;
#endif
            tcsetattr(ttyfd, TCSANOW, &ttyset);
	}
	/* Restore original terminal parameters */
	/* but make sure DTR goes down */
	ttyset_old.c_cflag |= HUPCL;
	tcsetattr(ttyfd,TCSANOW,&ttyset_old);

	close(ttyfd);
    }
}
