#include "config.h"
#include <unistd.h>
#include <string.h>
#include <fcntl.h>

#include "gpsd.h"

static int set_baud(long baud)
{
    if (baud < 200)
      baud *= 1000;
    if (baud < 2400)
      return B1200;
    else if (baud < 4800)
      return B2400;
    else if (baud < 9600)
      return B4800;
    else if (baud < 19200)
      return B9600;
    else if (baud < 38400)
      return B19200;
    else
      return B38400;
}

int gpsd_open(int device_speed, int stopbits, struct gps_session_t *session)
{
    int ttyfd;

    gpsd_report(1, "opening GPS data source at %s\n", session->gpsd_device);
    if ((ttyfd = open(session->gpsd_device, O_RDWR | O_NONBLOCK)) < 0)
	return -1;

    if (isatty(ttyfd)) {
	gpsd_report(1, "setting speed %d, 8 bits, no parity\n", device_speed);
	/* Save original terminal parameters */
	if (tcgetattr(ttyfd,&session->ttyset_old) != 0)
	  return -1;

	memcpy(&session->ttyset, &session->ttyset_old, sizeof(session->ttyset));
	device_speed = set_baud(device_speed);
	cfsetispeed(&session->ttyset, (speed_t)device_speed);
	cfsetospeed(&session->ttyset, (speed_t)device_speed);
	session->ttyset.c_cflag &= ~(PARENB | CRTSCTS);
	session->ttyset.c_cflag |= (CSIZE & (stopbits==2 ? CS7 : CS8)) | CREAD | CLOCAL;
	session->ttyset.c_iflag = session->ttyset.c_oflag = session->ttyset.c_lflag = (tcflag_t) 0;
	session->ttyset.c_oflag = (ONLCR);
	if (tcsetattr(ttyfd, TCSANOW, &session->ttyset) != 0)
	    return -1;
    }
    return ttyfd;
}

void gpsd_close(struct gps_session_t *session)
{
    if (session->gNMEAdata.gps_fd != -1) {
	if (isatty(session->gNMEAdata.gps_fd)) {
	    /* force hangup on close on systems that don't do HUPCL properly */
	    cfsetispeed(&session->ttyset, (speed_t)B0);
	    cfsetospeed(&session->ttyset, (speed_t)B0);
	    tcsetattr(session->gNMEAdata.gps_fd, TCSANOW, &session->ttyset);
	    /* this is the clean way to do it */
	    session->ttyset_old.c_cflag |= HUPCL;
	    tcsetattr(session->gNMEAdata.gps_fd,TCSANOW,&session->ttyset_old);
	}
	close(session->gNMEAdata.gps_fd);
    }
}
