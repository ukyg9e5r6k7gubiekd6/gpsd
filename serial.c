#include "config.h"
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#if defined(HAVE_SYS_MODEM_H)
#include <sys/modem.h>
#endif /* HAVE_SYS_MODEM_H */
#include "gpsd.h"
/* Workaround for HP-UX 11.23, which is missing CRTSCTS */
#ifndef CRTSCTS
#  ifdef CNEW_RTSCTS
#    define CRTSCTS CNEW_RTSCTS
#  else
#    define CRTSCTS 0
#  endif /* CNEW_RTSCTS */
#endif /* !CRTSCTS */

int gpsd_set_speed(int ttyfd, struct termios *ttyctl, int device_speed)
{
    if (device_speed < 300)
	device_speed = 0;
    else if (device_speed < 1200)
      device_speed =  B300;
    else if (device_speed < 2400)
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

    cfsetispeed(ttyctl, (speed_t)device_speed);
    cfsetospeed(ttyctl, (speed_t)device_speed);
    if (tcsetattr(ttyfd, TCSAFLUSH, ttyctl) != 0)
	return 0;
    /*
     * for unknown reasons, the TCSAFLUSH above is not sufficient to
     * flush the stale data from previous hunting out of the buffer.
     */
    tcflush(ttyfd, TCIOFLUSH);

    /*
     * Give the GPS and UART this much time to settle and ship some data
     * before trying to read after open or baud rate change.  Less than
     * 1.25 seconds doesn't work om most UARTs. 
     */
    usleep(1250000);
    return 1;
}

int gpsd_get_speed(struct termios* ttyctl)
{
    int code = cfgetospeed(ttyctl);
    switch (code) {
    case B0:     return(0);
    case B300:   return(300);
    case B1200:  return(1200);
    case B2400:  return(2400);
    case B4800:  return(4800);
    case B9600:  return(9600);
    case B19200: return(19200);
    default: return(38400);
    }
}

/* every rate we're likely to see on a GPS */
static int rates[] = {4800, 9600, 19200, 38400};

static int connect_at_speed(int ttyfd, struct gps_session_t *session, int speed)
{
    char	buf[4*NMEA_MAX+1];
    int		n;

    gpsd_set_speed(ttyfd, &session->ttyset, speed);
    /*
     * Magic -- relies on the fact that the UARTS on GPSes never seem to take 
     * longer than 3 NMEA sentences to sync.
     */
    n = read(ttyfd, buf, sizeof(buf)-1);
    if (session->device_type->validate_buffer)
	return session->device_type->validate_buffer(buf, n);
    else
	return 1;
}

int gpsd_open(int device_speed, int stopbits, struct gps_session_t *session)
{
    int ttyfd, *ip;

    gpsd_report(1, "opening GPS data source at %s\n", session->gpsd_device);
    if ((ttyfd = open(session->gpsd_device, O_RDWR)) < 0)
	return -1;

    if (isatty(ttyfd)) {
	/* Save original terminal parameters */
	if (tcgetattr(ttyfd,&session->ttyset_old) != 0)
	  return -1;
	memcpy(&session->ttyset,&session->ttyset_old,sizeof(session->ttyset));
	/*
	 * Tip from Chris Kuethe: the FIDI chip used in the Trip-Nav
	 * 200 (and possibly other USB GPSes) gets completely hosed
	 * in the presence of flow control.  Thus, turn off CRTSCTS.
	 */
	session->ttyset.c_cflag &= ~(PARENB | CRTSCTS);
	session->ttyset.c_cflag |= (CSIZE & (stopbits==2 ? CS7 : CS8)) | CREAD | CLOCAL;
	session->ttyset.c_iflag = session->ttyset.c_oflag = session->ttyset.c_lflag = (tcflag_t) 0;
	session->ttyset.c_oflag = (ONLCR);

	if (device_speed) {
	    gpsd_report(1, "setting speed %d, %d stopbits, no parity\n", 
			device_speed, stopbits);
	    if (connect_at_speed(ttyfd, session, device_speed)) {
		session->gNMEAdata.baudrate = device_speed;
		return ttyfd;
	    }
	} else
	    for (ip = rates; ip < rates + sizeof(rates)/sizeof(rates[0]); ip++) {
		gpsd_report(1, "hunting at speed %d, %d stopbits, no parity\n", 
			    *ip, stopbits);
		if (connect_at_speed(ttyfd, session, *ip)) {
		    session->gNMEAdata.baudrate = *ip;
		    return ttyfd;
		}
	    }
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
