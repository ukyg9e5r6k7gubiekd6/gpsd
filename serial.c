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
    case B57600: return(57600);
    default: return(115200);
    }
}

int gpsd_set_speed(struct gps_device_t *session, 
		   unsigned int speed, unsigned int stopbits)
{
    unsigned int	rate;

    if (speed < 300)
	rate = B0;
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
    else if (speed < 115200)
      rate =  B57600;
    else
      rate =  B115200;

    tcflush(session->gpsdata.gps_fd, TCIOFLUSH);	/* toss stale data */
    if (rate!=cfgetispeed(&session->ttyset) || stopbits!=session->gpsdata.stopbits) {
	cfsetispeed(&session->ttyset, (speed_t)rate);
	cfsetospeed(&session->ttyset, (speed_t)rate);
	session->ttyset.c_cflag &=~ CSIZE;
	session->ttyset.c_cflag |= (CSIZE & (stopbits==2 ? CS7 : CS8));
	if (tcsetattr(session->gpsdata.gps_fd, TCSANOW, &session->ttyset) != 0)
	    return 0;
	tcflush(session->gpsdata.gps_fd, TCIOFLUSH);
    }

    session->gpsdata.stopbits = stopbits;
    session->gpsdata.baudrate = speed;

    if ((session->packet_type = packet_sniff(session)) == BAD_PACKET)
	return 0;

    return 1;
}

int gpsd_open(struct gps_device_t *session)
{
    unsigned int *ip;
    unsigned int stopbits;
    /* every rate we're likely to see on a GPS */
    static unsigned int rates[] = {0, 4800, 9600, 19200, 38400, 57600};

    gpsd_report(1, "opening GPS data source at '%s'\n", session->gpsdata.gps_device);
    if ((session->gpsdata.gps_fd = open(session->gpsdata.gps_device, O_RDWR|O_NOCTTY)) < 0) {
	gpsd_report(1, "device open failed: %s\n", strerror(errno));
	return -1;
    }

    session->packet_type = BAD_PACKET;
    if (isatty(session->gpsdata.gps_fd)) {
#ifdef NON_NMEA_ENABLE
	struct gps_type_t **dp;

	for (dp = gpsd_drivers; *dp; dp++) {
           tcflush(session->gpsdata.gps_fd, TCIOFLUSH);  /* toss stale data */
	    if ((*dp)->probe && (*dp)->probe(session)) {
		gpsd_report(3, "probe found %s driver...\n", (*dp)->typename);
		session->device_type = *dp;
		return session->gpsdata.gps_fd;
	    }
 	}
	gpsd_report(3, "no probe matched...\n");
#endif /* NON_NMEA_ENABLE */

	/* Save original terminal parameters */
	if (tcgetattr(session->gpsdata.gps_fd,&session->ttyset_old) != 0)
	  return -1;
	memcpy(&session->ttyset,&session->ttyset_old,sizeof(session->ttyset));
	/*
	 * Tip from Chris Kuethe: the FIDI chip used in the Trip-Nav
	 * 200 (and possibly other USB GPSes) gets completely hosed
	 * in the presence of flow control.  Thus, turn off CRTSCTS.
	 */
	session->ttyset.c_cflag &= ~(PARENB | CRTSCTS);
	session->ttyset.c_cflag |= CREAD | CLOCAL;
	session->ttyset.c_iflag = session->ttyset.c_oflag = session->ttyset.c_lflag = (tcflag_t) 0;
	session->ttyset.c_oflag = (ONLCR);

	rates[0] = gpsd_get_speed(&session->ttyset_old);
	for (stopbits = 1; stopbits <= 2; stopbits++)
	    for (ip = rates; ip < rates + sizeof(rates)/sizeof(rates[0]); ip++)
		if (ip == rates || *ip != rates[0])
		{
		    gpsd_report(1, 
				"hunting at speed %d, %dN%d\n",
				*ip, 9-stopbits, stopbits);
		    if (gpsd_set_speed(session, *ip, stopbits))
			return session->gpsdata.gps_fd;
		}
	session->gpsdata.gps_fd = -1;
    }
    return session->gpsdata.gps_fd;
}

void gpsd_close(struct gps_device_t *session)
{
    if (session->gpsdata.gps_fd != -1) {
	if (isatty(session->gpsdata.gps_fd)) {
	    /* force hangup on close on systems that don't do HUPCL properly */
	    cfsetispeed(&session->ttyset, (speed_t)B0);
	    cfsetospeed(&session->ttyset, (speed_t)B0);
	    tcsetattr(session->gpsdata.gps_fd, TCSANOW, &session->ttyset);
	}
	/* this is the clean way to do it */
	session->ttyset_old.c_cflag |= HUPCL;
	tcsetattr(session->gpsdata.gps_fd,TCSANOW,&session->ttyset_old);
	close(session->gpsdata.gps_fd);
    }
}
