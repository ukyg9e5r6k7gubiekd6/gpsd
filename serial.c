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
static int rates[] = {4800, 9600, 19200, 38400, 57600};

int gpsd_set_speed(struct gps_session_t *session, unsigned int speed)
{
    char	buf[NMEA_MAX+1];
    unsigned int	n, rate, ok;

    if (speed < 300)
	rate = 0;
    else if (speed < 1200)
      rate =  B300;
    else if (speed < 2400)
      rate =  B1200;
    else if (speed < 4800)
      rate =  B2400;
    else if (speed < 9600)
      rate =  B4800;
    else if (speed < 19200)
      rate =  B9600;
    else if (speed < 38400)
      rate =  B19200;
    else if (speed < 57600)
      rate =  B38400;
    else
      rate =  B57600;

    if (speed != cfgetispeed(&session->ttyset)) {
	tcflush(session->gNMEAdata.gps_fd, TCIOFLUSH);
	cfsetispeed(&session->ttyset, (speed_t)rate);
	cfsetospeed(&session->ttyset, (speed_t)rate);
	if (tcsetattr(session->gNMEAdata.gps_fd, TCSANOW, &session->ttyset) != 0)
	    return 0;
	tcflush(session->gNMEAdata.gps_fd, TCIOFLUSH);

	usleep(3000000);	/* allow the UART time to settle */
    }

    if (session->device_type->validate_buffer) {
	n = 0;
	while (n < NMEA_MAX) {
	    n += read(session->gNMEAdata.gps_fd, buf+n, sizeof(buf)-n-1);
	    if (n > 2 && buf[n-2] == '\r' && buf[n-1] == '\n')
		break;
	}
	gpsd_report(4, "validating %d bytes.\n", n);
	ok = session->device_type->validate_buffer(buf, n);
    } else {
	/* this has the effect of disabling baud hunting on future connects */
	gpsd_report(4, "no buffer validation.\n");
	ok = 1;
    }

    if (ok)
	session->gNMEAdata.baudrate = speed;
    return ok;
}

int gpsd_open(struct gps_session_t *session)
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
	session->ttyset.c_cflag |= (CSIZE & (session->gNMEAdata.stopbits==2 ? CS7 : CS8)) | CREAD | CLOCAL;
	session->ttyset.c_iflag = session->ttyset.c_oflag = session->ttyset.c_lflag = (tcflag_t) 0;
	session->ttyset.c_oflag = (ONLCR);

	if (session->gNMEAdata.baudrate) {
	    gpsd_report(1, "setting speed %d, %d stopbits, no parity\n", 
			session->gNMEAdata.baudrate, 
			session->gNMEAdata.stopbits);
	    if (gpsd_set_speed(session, session->gNMEAdata.baudrate)) {
		return session->gNMEAdata.gps_fd;
	    }
	}
	for (ip = rates; ip < rates + sizeof(rates)/sizeof(rates[0]); ip++) 
	    if (*ip != session->gNMEAdata.baudrate) {
		gpsd_report(1, "hunting at speed %d, %d stopbits, no parity\n",
			    *ip, session->gNMEAdata.stopbits);
		if (gpsd_set_speed(session, *ip))
		    return session->gNMEAdata.gps_fd;
	}
	session->gNMEAdata.gps_fd = -1;
    }
    return session->gNMEAdata.gps_fd;
}

#ifdef UNRELIABLE_SYNC
void gpsd_drain(int ttyfd)
{
    tcdrain(ttyfd);
    /* 
     * This definitely fails below 40 milliseconds on a BU-303b.
     * 50ms is also verified by Chris Kuethe on 
     *        Pharos iGPS360 + GSW 2.3.1ES + prolific
     *        Rayming TN-200 + GSW 2.3.1 + ftdi
     *        Rayming TN-200 + GSW 2.3.2 + ftdi
     * so it looks pretty solid.
     */
    usleep(50000);
}
#endif /* UNRELIABLE_SYNC */

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
