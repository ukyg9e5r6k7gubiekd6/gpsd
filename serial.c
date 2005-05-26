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

void gpsd_set_speed(struct gps_device_t *session, 
		   unsigned int speed, unsigned int parity, unsigned int stopbits)
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

    if (rate!=cfgetispeed(&session->ttyset) || parity!=session->gpsdata.parity || stopbits!=session->gpsdata.stopbits) {

	cfsetispeed(&session->ttyset, (speed_t)rate);
	cfsetospeed(&session->ttyset, (speed_t)rate);
 	session->ttyset.c_iflag &=~ (PARMRK | INPCK);
 	session->ttyset.c_cflag &=~ (CSIZE | CSTOPB | PARENB | PARODD);
 	session->ttyset.c_cflag |= (stopbits==2 ? CS7|CSTOPB : CS8);
 	switch (parity)
 	{
 	case 'E':
 	    session->ttyset.c_iflag |= INPCK;
 	    session->ttyset.c_cflag |= PARENB;
 	    break;
 	case 'O':
 	    session->ttyset.c_iflag |= INPCK;
 	    session->ttyset.c_cflag |= PARENB | PARODD;
 	    break;
 	}	session->ttyset.c_cflag &=~ CSIZE;
	session->ttyset.c_cflag |= (CSIZE & (stopbits==2 ? CS7 : CS8));
	if (tcsetattr(session->gpsdata.gps_fd, TCSANOW, &session->ttyset) != 0)
	    return;
	tcflush(session->gpsdata.gps_fd, TCIOFLUSH);
    }
    gpsd_report(1, "speed %d, %d%c%d\n", speed, 9-stopbits, parity, stopbits);

    session->gpsdata.baudrate = speed;
    session->gpsdata.parity = parity;
    session->gpsdata.stopbits = stopbits;
    packet_reset(session);
}

int gpsd_open(struct gps_device_t *session)
{
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
		if (session->device_type->initializer)
		    session->device_type->initializer(session);
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
	session->ttyset.c_cflag &= ~(PARENB | PARODD | CBAUDEX | CRTSCTS);
	session->ttyset.c_cflag |= CREAD | CLOCAL;
	session->ttyset.c_iflag = session->ttyset.c_oflag = session->ttyset.c_lflag = (tcflag_t) 0;

	session->counter = session->baudindex = 0;
	gpsd_set_speed(session, 
		       gpsd_get_speed(&session->ttyset_old), 'N', 1);
    }
    return session->gpsdata.gps_fd;
}

/*
 * This constant controls how long the packet sniffer will spend looking
 * for a packet leader before it gives up.  It *must* be larger than
 * MAX_PACKET_LENGTH or we risk never syncing up at all.  Large values
 * will produce annoying startup lag.
 */
#define SNIFF_RETRIES	600

int gpsd_next_hunt_setting(struct gps_device_t *session)
/* advance to the next hunt setting  */
{
    /* every rate we're likely to see on a GPS */
    static unsigned int rates[] = {0, 4800, 9600, 19200, 38400, 57600};

    if (session->counter++ >= SNIFF_RETRIES) {
	session->counter = 0;
	if (session->baudindex++ >= sizeof(rates)/sizeof(rates[0])) {
	    session->baudindex = 0;
	    if (session->gpsdata.stopbits++ >= 2)
		return 0;			/* hunt is over, no sync */
	}
	gpsd_set_speed(session, 
		       rates[session->baudindex],
		       'N', session->gpsdata.stopbits);
    }

    return 1;	/* keep hunting */
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
	(void)close(session->gpsdata.gps_fd);
    }
}
