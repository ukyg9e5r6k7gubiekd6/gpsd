#include "config.h"
#include <unistd.h>
#include <string.h>
#include <fcntl.h>

#include "gpsd.h"

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

	if (device_speed < 200)
	  device_speed *= 1000;
	if (device_speed < 2400)
	  device_speed =  B1200;
	else if (device_speed < 4800)
	  device_speed =  B2400;
	else if (device_speed < 9600)
	  device_speed =  B4800;
	else if (device_speed < 19200)
	  device_speed =  B9600;
	else if (device_speed < 38400)
	  device_speed =  B19200;
	else
	  device_speed =  B38400;

	memcpy(&session->ttyset, &session->ttyset_old, sizeof(session->ttyset));
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
	}
	/* this is the clean way to do it */
	session->ttyset_old.c_cflag |= HUPCL;
	tcsetattr(session->gNMEAdata.gps_fd,TCSANOW,&session->ttyset_old);
	close(session->gNMEAdata.gps_fd);
    }
}
