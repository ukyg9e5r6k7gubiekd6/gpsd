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
    case B38400: return(38400);
    default: return(57600);
    }
}

/* every rate we're likely to see on a GPS */
static int rates[] = {4800, 9600, 19200, 38400};

int gpsd_set_speed(struct gps_session_t *session, int speed)
{
    char	buf[20*NMEA_MAX+1];
    int		n;

    if (speed < 300)
	speed = 0;
    else if (speed < 1200)
      speed =  B300;
    else if (speed < 2400)
      speed =  B1200;
    else if (speed < 4800)
      speed =  B2400;
    else if (speed < 9600)
      speed =  B4800;
    else if (speed < 19200)
      speed =  B9600;
    else if (speed < 38400)
      speed =  B19200;
    else if (speed < 57600)
      speed =  38400;
    else
      speed =  57600;

    tcflush(session->gNMEAdata.gps_fd, TCIOFLUSH);
    cfsetispeed(&session->ttyset, (speed_t)speed);
    cfsetospeed(&session->ttyset, (speed_t)speed);
    if (tcsetattr(session->gNMEAdata.gps_fd, TCSANOW, &session->ttyset) != 0)
	return 0;
    tcflush(session->gNMEAdata.gps_fd, TCIOFLUSH);

    /*
     * Magic -- relies on the fact that the UARTS on GPSes never seem to take 
     * longer than 3 NMEA sentences to sync.
     */
    if (session->device_type->validate_buffer) {
	n = read(session->gNMEAdata.gps_fd, buf, sizeof(buf)-1);
	return session->device_type->validate_buffer(buf, n);
    } else
	return 1;
}

int gpsd_open(int device_speed, int stopbits, struct gps_session_t *session)
{
    int *ip;

    gpsd_report(1, "opening GPS data source at %s\n", session->gpsd_device);
    if ((session->gNMEAdata.gps_fd = open(session->gpsd_device, O_RDWR|O_NOCTTY|O_SYNC)) < 0)
	return -1;

    if (isatty(session->gNMEAdata.gps_fd)) {
	/* Save original terminal parameters */
	if (tcgetattr(session->gNMEAdata.gps_fd,&session->ttyset_old) != 0)
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
	    if (gpsd_set_speed(session, device_speed)) {
		session->gNMEAdata.baudrate = device_speed;
		return session->gNMEAdata.gps_fd;
	    }
	} else
	    for (ip = rates; ip < rates + sizeof(rates)/sizeof(rates[0]); ip++) {
		gpsd_report(1, "hunting at speed %d, %d stopbits, no parity\n", 
			    *ip, stopbits);
		if (gpsd_set_speed(session, *ip)) {
		    session->gNMEAdata.baudrate = *ip;
		    return session->gNMEAdata.gps_fd;
		}
	    }
	return -1;
    }
    return session->gNMEAdata.gps_fd;
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
