/* $Id$ */
/**
 * True North Technologies - Revolution 2X Digital compass
 *
 * More info: http://www.tntc.com/
 *
 * This is a digital compass which uses magnetometers to measure
 * the strength of the earth's magnetic field. Based on these
 * measurements it provides a compass heading using NMEA
 * formatted output strings. I threw this into gpsd since it
 * already has convienient NMEA parsing support. Also because
 * I use the compass to suplement the heading provided by
 * another gps unit. A gps heading is unreliable at slow 
 * speed or no speed.
 *
 */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <math.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <stdarg.h>

#include "gpsd.h"

#ifdef TNT_ENABLE

enum {
#include "packet_states.h"
};

static void tnt_add_checksum(char *sentence)
{
    unsigned char sum = '\0';
    char c, *p = sentence;

    if (*p == '@') {
	p++;
    } else {
        gpsd_report(1, "Bad TNT sentence: '%s'\n", sentence);
    }
    while ( ((c = *p) != '*') && (c != '\0')) {
	sum ^= c;
	p++;
    }
    *p++ = '*';
    /*@i@*/snprintf(p, 4, "%02X\r\n", sum);
}

static int tnt_send(int fd, const char *fmt, ... )
{
    int status;
    char buf[BUFSIZ];
    va_list ap;

    va_start(ap, fmt) ;
    (void)vsnprintf(buf, sizeof(buf)-5, fmt, ap);
    va_end(ap);
    strlcat(buf, "*", BUFSIZ);
    tnt_add_checksum(buf);
    status = (int)write(fd, buf, strlen(buf));
    if (status == (int)strlen(buf)) {
	gpsd_report(2, "=> GPS: %s\n", buf);
	return status;
    } else {
	gpsd_report(2, "=> GPS: %s FAILED\n", buf);
	return -1;
    }
}

#define TNT_SNIFF_RETRIES       100
/*
 * The True North compass won't start talking
 * unless you ask it to. So to identify it we
 * need to query for it's ID string.
 */
static int tnt_packet_sniff(struct gps_device_t *session)
{
    unsigned int n, count = 0;

    gpsd_report(5, "tnt_packet_sniff begins\n");
    for (n = 0; n < TNT_SNIFF_RETRIES; n++) 
    {
      count = 0;
      (void)tnt_send(session->gpsdata.gps_fd, "@X?");
      if (ioctl(session->gpsdata.gps_fd, FIONREAD, &count) < 0)
          return BAD_PACKET;
      if (count == 0) {
          //int delay = 10000000000.0 / session->gpsdata.baudrate;
          //gpsd_report(5, "usleep(%d)\n", delay);
          //usleep(delay);
          gpsd_report(5, "sleep(1)\n");
          (void)sleep(1);
      } else if (packet_get(session) >= 0) {
        if((session->packet_type == NMEA_PACKET)&&(session->packet_state == NMEA_RECOGNIZED))
        {
          gpsd_report(5, "tnt_packet_sniff returns %d\n",session->packet_type);
          return session->packet_type;
        }
      }
    }

    gpsd_report(5, "tnt_packet_sniff found no packet\n");
    return BAD_PACKET;
}


static void tnt_initializer(struct gps_device_t *session)
{
  // Send codes to start the flow of data
  //tnt_send(session->gpsdata.gps_fd, "@BA?"); // Query current rate
  //tnt_send(session->gpsdata.gps_fd, "@BA=8"); // Start HTM packet at 1Hz
  /*
   * Sending this twice seems to make it more reliable!!
   * I think it gets the input on the unit synced up.
   */
  (void)tnt_send(session->gpsdata.gps_fd, "@BA=26"); // Start HTM packet at 2400 per minute
  (void)tnt_send(session->gpsdata.gps_fd, "@BA=26"); // Start HTM packet at 2400 per minute
}

static bool tnt_probe(struct gps_device_t *session)
{
  unsigned int *ip;
#ifdef FIXED_PORT_SPEED
    /* just the one fixed port speed... */
    static unsigned int rates[] = {FIXED_PORT_SPEED};
#else /* FIXED_PORT_SPEED not defined */
  /* The supported baud rates */
  static unsigned int rates[] = {38400, 19200, 2400, 4800, 9600 };
#endif /* FIXED_PORT_SPEED defined */

  gpsd_report(1, "Probing TrueNorth Compass\n");

  /*
   * Only block until we get at least one character, whatever the
   * third arg of read(2) says.
   */
  /*@ ignore @*/
  memset(session->ttyset.c_cc,0,sizeof(session->ttyset.c_cc));
  session->ttyset.c_cc[VMIN] = 1;
  /*@ end @*/

  session->ttyset.c_cflag &= ~(PARENB | PARODD | CRTSCTS);
  session->ttyset.c_cflag |= CREAD | CLOCAL;
  session->ttyset.c_iflag = session->ttyset.c_oflag = session->ttyset.c_lflag = (tcflag_t) 0;

  session->baudindex = 0;
  for (ip = rates; ip < rates + sizeof(rates)/sizeof(rates[0]); ip++)
      if (ip == rates || *ip != rates[0])
      {
          gpsd_report(1, "hunting at speed %d\n", *ip);
          gpsd_set_speed(session, *ip, 'N',1);
          if (tnt_packet_sniff(session) != BAD_PACKET)
              return true;
      }
  return false;
}

struct gps_type_t trueNorth = {
    .typename       = "True North",	/* full name of type */
    .trigger        = " TNT1500",
    .channels       = 0,		/* not an actual GPS at all */
    .probe          = tnt_probe,	/* probe by sending ID query */
    .initializer    = tnt_initializer,	/* probe for True North Digital Compass */
    .get_packet     = packet_get,		/* how to get a packet */
    .parse_packet   = nmea_parse_input,	/* how to interpret a packet */
    .rtcm_writer    = NULL,	        /* Don't send */
    .speed_switcher = NULL,		/* no speed switcher */
    .mode_switcher  = NULL,		/* no mode switcher */
    .rate_switcher  = NULL,		/* no wrapup */
    .cycle_chars    = -1,		/* not relevant, no rate switch */
    .wrapup         = NULL,		/* no wrapup */
    .cycle          = 20,		/* updates per second */
};
#endif
