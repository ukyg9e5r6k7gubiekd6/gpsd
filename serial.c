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

#if defined(__CYGWIN__)
/* Workaround for Cygwin, which is missing cfmakeraw */
/* Pasted from man page; added in serial.c arbitrarily */
void cfmakeraw(struct termios *termios_p)
{
    termios_p->c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL|IXON);
    termios_p->c_oflag &= ~OPOST;
    termios_p->c_lflag &= ~(ECHO|ECHONL|ICANON|ISIG|IEXTEN);
    termios_p->c_cflag &= ~(CSIZE|PARENB);
    termios_p->c_cflag |= CS8;
}
#endif /* defined(__CYGWIN__) */

speed_t gpsd_get_speed(struct termios* ttyctl)
{
    speed_t code = cfgetospeed(ttyctl);
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
		   speed_t speed, unsigned char parity, unsigned int stopbits)
{
    speed_t	rate;

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

    if (rate!=cfgetispeed(&session->ttyset) || (unsigned int)parity!=session->gpsdata.parity || stopbits!=session->gpsdata.stopbits) {

	/*@ignore@*/
	(void)cfsetispeed(&session->ttyset, rate);
	(void)cfsetospeed(&session->ttyset, rate);
	/*@end@*/
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
 	}
	session->ttyset.c_cflag &=~ CSIZE;
	session->ttyset.c_cflag |= (CSIZE & (stopbits==2 ? CS7 : CS8));
	if (tcsetattr(session->gpsdata.gps_fd, TCSANOW, &session->ttyset) != 0)
	    return;
	(void)tcflush(session->gpsdata.gps_fd, TCIOFLUSH);
    }
    gpsd_report(1, "speed %d, %d%c%d\n", speed, 9-stopbits, parity, stopbits);

    session->gpsdata.baudrate = (unsigned int)speed;
    session->gpsdata.parity = (unsigned int)parity;
    session->gpsdata.stopbits = stopbits;
    packet_reset(session);
}

int gpsd_open(struct gps_device_t *session)
{
    gpsd_report(1, "opening GPS data source at '%s'\n", session->gpsdata.gps_device);
    if ((session->gpsdata.gps_fd = open(session->gpsdata.gps_device, O_RDWR|O_NONBLOCK|O_NOCTTY)) < 0) {
	gpsd_report(1, "device open failed: %s\n", strerror(errno));
	return -1;
    }

#ifdef FIXED_PORT_SPEED
    session->saved_baud = FIXED_PORT_SPEED;
#endif

    if (session->saved_baud != -1) {
        /*@i@*/(void)cfsetispeed(&session->ttyset, (speed_t)session->saved_baud);
        /*@i@*/(void)cfsetospeed(&session->ttyset, (speed_t)session->saved_baud);
	(void)tcsetattr(session->gpsdata.gps_fd, TCSANOW, &session->ttyset);
	(void)tcflush(session->gpsdata.gps_fd, TCIOFLUSH);
    }

    session->packet_type = BAD_PACKET;
    if (isatty(session->gpsdata.gps_fd)!=0) {
#ifdef NON_NMEA_ENABLE
	struct gps_type_t **dp;

	for (dp = gpsd_drivers; *dp; dp++) {
	    (void)tcflush(session->gpsdata.gps_fd, TCIOFLUSH);  /* toss stale data */
	    if ((*dp)->probe!=NULL && (*dp)->probe(session)!=0) {
		gpsd_report(3, "probe found %s driver...\n", (*dp)->typename);
		/*@i1@*/session->device_type = *dp;
		if (session->device_type->initializer)
		    session->device_type->initializer(session);
		/*@i1@*/return session->gpsdata.gps_fd;
	    }
 	}
	gpsd_report(3, "no probe matched...\n");
#endif /* NON_NMEA_ENABLE */

	/* Save original terminal parameters */
	if (tcgetattr(session->gpsdata.gps_fd,&session->ttyset_old) != 0)
	  return -1;
	(void)memcpy(&session->ttyset,
		     &session->ttyset_old, sizeof(session->ttyset));
	/*
	 * Only block until we get at least one character, whatever the
	 * third arg of read(2) says.
	 */
	/*@ ignore @*/
	memset(session->ttyset.c_cc,0,sizeof(session->ttyset.c_cc));
	session->ttyset.c_cc[VMIN] = 1;
	/*@ end @*/
	/*
	 * Tip from Chris Kuethe: the FIDI chip used in the Trip-Nav
	 * 200 (and possibly other USB GPSes) gets completely hosed
	 * in the presence of flow control.  Thus, turn off CRTSCTS.
	 */
	session->ttyset.c_cflag &= ~(PARENB | PARODD | CRTSCTS);
	session->ttyset.c_cflag |= CREAD | CLOCAL;
	session->ttyset.c_iflag = session->ttyset.c_oflag = session->ttyset.c_lflag = (tcflag_t) 0;

	session->baudindex = 0;
	gpsd_set_speed(session, 
		       gpsd_get_speed(&session->ttyset_old), 'N', 1);
    }
    return session->gpsdata.gps_fd;
}

bool gpsd_write(struct gps_device_t *session, void const *buf, size_t len)
{
     ssize_t status;
     bool ok;
     status = write(session->gpsdata.gps_fd, buf, len);	
     ok = (status == (ssize_t)len);
     (void)tcdrain(session->gpsdata.gps_fd);
     /* code that will check for data could be add here to print buffer as text or hex */
     /* no test here now, always print as hex */
     gpsd_report(5, "=> GPS: %s%s\n", gpsd_hexdump(buf, len), ok?"":" FAILED");
     return ok;
}

/*
 * This constant controls how long the packet sniffer will spend looking
 * for a packet leader before it gives up.  It *must* be larger than
 * MAX_PACKET_LENGTH or we risk never syncing up at all.  Large values
 * will produce annoying startup lag.
 */
#define SNIFF_RETRIES	256

bool gpsd_next_hunt_setting(struct gps_device_t *session)
/* advance to the next hunt setting  */
{
#ifdef FIXED_PORT_SPEED
    /* just the one fixed port speed... */
    static unsigned int rates[] = {FIXED_PORT_SPEED};
#else /* FIXED_PORT_SPEED not defined */
    /* every rate we're likely to see on a GPS */
    static unsigned int rates[] = {0, 4800, 9600, 19200, 38400, 57600};
#endif /* FIXED_PORT_SPEED defined */

    if (session->retry_counter++ >= SNIFF_RETRIES) {
	session->retry_counter = 0;
	if (session->baudindex++ >= (unsigned int)(sizeof(rates)/sizeof(rates[0]))) {
	    session->baudindex = 0;
	    if (session->gpsdata.stopbits++ >= 2)
		return false;			/* hunt is over, no sync */
	}
	gpsd_set_speed(session, 
		       rates[session->baudindex],
		       'N', session->gpsdata.stopbits);
    }

    return true;	/* keep hunting */

}

void gpsd_close(struct gps_device_t *session)
{
    if (session->gpsdata.gps_fd != -1) {
	if (isatty(session->gpsdata.gps_fd)!=0) {
	    /* force hangup on close on systems that don't do HUPCL properly */
	    /*@ ignore @*/
	    (void)cfsetispeed(&session->ttyset, (speed_t)B0);
	    (void)cfsetospeed(&session->ttyset, (speed_t)B0);
	    /*@ end @*/
	    (void)tcsetattr(session->gpsdata.gps_fd,TCSANOW, &session->ttyset);
	}
	/* this is the clean way to do it */
	session->ttyset_old.c_cflag |= HUPCL;
	(void)tcsetattr(session->gpsdata.gps_fd,TCSANOW,&session->ttyset_old);
	(void)close(session->gpsdata.gps_fd);
	session->gpsdata.gps_fd = -1;
    }
}

