/*
 * Copyright (C) 2003 Arnim Laeuger <arnim.laeuger@gmx.net>
 * Issued under GPL.  Originally part of an unpublished utility
 * called sirf_ctrl.  Contributed to gpsd by the author.
 *
 * Modified to not use stderr and so each function returns 0 on success,
 * nonzero on failure.  Alsso to use gpsd's own checksum and send code.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <stdio.h>

#include "gpsd.h"

#define HI(n)	((n) >> 8)
#define LO(n)	((n) & 0xff)

static u_int16_t crc_sirf(u_int8_t *msg) {
   int       pos = 0;
   u_int16_t crc = 0;
   int       len;

   len = (msg[2] << 8) | msg[3];

   /* calculate CRC */
   while (pos != len)
      crc += msg[pos++ + 4];
   crc &= 0x7fff;

   /* enter CRC after payload */
   msg[len + 4] = (u_int8_t)((crc & 0xff00) >> 8);
   msg[len + 5] = (u_int8_t)( crc & 0x00ff);

   return(crc);
}

static int sirf_speed(int ttyfd, int speed) 
/* change speed in binary mode */
{
   u_int8_t msg[] = {0xa0, 0xa2, 0x00, 0x09,
                     0x86, 
                     0x0, 0x0, 0x12, 0xc0,	/* 4800 bps */
		     0x08,			/* 8 data bits */
		     0x01,			/* 1 stop bit */
		     0x00,			/* no parity */
		     0x01,			/* reserved */
                     0x00, 0x00, 0xb0, 0xb3};

   msg[8] = HI(speed);
   msg[9] = LO(speed);
   crc_sirf(msg);
   return (write(ttyfd, msg, 9+8) != 9+8);
}

#ifdef __UNUSED__
static int sirf_to_nmea(int ttyfd, int speed) 
/* switch from binary to NMEA at specified baud */
{
   u_int8_t msg[] = {0xa0, 0xa2, 0x00, 0x18,
                     0x81, 0x02,
                     0x01, 0x01, /* GGA */
                     0x00, 0x01, /* GLL */
                     0x01, 0x01, /* GSA */
                     0x05, 0x01, /* GSV */
                     0x01, 0x01, /* RMC */
                     0x00, 0x01, /* VTG */
                     0x00, 0x01, 0x00, 0x01,
                     0x00, 0x01, 0x00, 0x01,
                     0x12, 0xc0, /* 4800 bps */
                     0x00, 0x00, 0xb0, 0xb3};

   msg[26] = HI(speed);
   msg[27] = LO(speed);
   crc_sirf(msg);
   return (write(ttyfd, msg, 0x18+8) != 0x18+8);
}

static int sirf_waas_ctrl(int ttyfd, int enable) 
/* enable or disable WAAS */
{
   u_int8_t msg[] = {0xa0, 0xa2, 0x00, 0x07,
                     0x85, 0x00,
                     0x00, 0x00, 0x00, 0x00,
                     0x00,
                     0x00, 0x00, 0xb0, 0xb3};

   msg[5] = (u_int8_t)enable;
   crc_sirf(msg);
   return (write(ttyfd, msg, 15) != 15);
}


static int sirf_reset(int ttyfd) 
/* reset GPS parameters */
{
   u_int8_t msg[] = {0xa0, 0xa2, 0x00, 0x19,
                     0x81,
                     0x00, 0x00, 0x00, 0x00,
                     0x00, 0x00, 0x00, 0x00,
                     0x00, 0x00, 0x00, 0x00,
                     0x00, 0x00, 0x00, 0x00,
                     0x00, 0x00, 0x00, 0x00,
                     0x00, 0x00,
                     0x0c,
                     0x04,
                     0x00, 0x00, 0xb0, 0xb3};

   crc_sirf(msg);
   return (write(ttyfd, msg, 0x19+8) != 0x19+8);
}


static int sirf_dgps_source(int ttyfd, int source) 
/* set source for DGPS corrections */
{
   int i;
   u_int8_t msg1[] = {0xa0, 0xa2, 0x00, 0x07,
                      0x85,
                      0x00,                    /* DGPS source   */
                      0x00, 0x01, 0xe3, 0x34,  /* 123.7 kHz     */
                      0x00,                    /* auto bitrate  */
                      0x00, 0x00, 0xb0, 0xb3};
   u_int8_t msg2[] = {0xa0, 0xa2, 0x00, 0x03,
                      0x8a,
                      0x00, 0xff,              /* auto, 255 sec */
                      0x00, 0x00, 0xb0, 0xb3};
   u_int8_t msg3[] = {0xa0, 0xa2, 0x00, 0x09,
                      0x91,
                      0x00, 0x00, 0x12, 0xc0,  /* 4800 baud     */
                      0x08,                    /* 8 bits        */
                      0x01,                    /* 1 Stop bit    */
                      0x00,                    /* no parity     */
                      0x00,
                      0x00, 0x00, 0xb0, 0xb3};

   /*
    * set DGPS source
    */
   switch (source) {
      case DGPS_SOURCE_NONE:
         /* set no DGPS source */
         msg1[5] = 0x00;
         break;
      case DGPS_SOURCE_INTERNAL:
         /* set to internal DGPS beacon */
         msg1[5] = 0x03;
         break;
      case DGPS_SOURCE_WAAS:
         /* set to WAAS/EGNOS */
         msg1[5] = 0x01;
         for (i = 6; i < 11; i++)
            msg1[i] = 0x00;
         break;
      case DGPS_SOURCE_EXTERNAL:
         /* set to external RTCM input */
         msg1[5] = 0x02;
         break;
   }
   crc_sirf(msg1);
   if (write(ttyfd, msg1, 0x07+8) != 0x07+8)
      return 1;

   /*
    * set DGPS control to auto
    */
   if (source != DGPS_SOURCE_WAAS) {
      crc_sirf(msg2);
      if (write(ttyfd, msg2, 0x03+8) != 0x03+8)
         return 2;
   }

   /*
    * set DGPS port
    */
   if (source == DGPS_SOURCE_EXTERNAL) {
      crc_sirf(msg3);
      if (write(ttyfd, msg3, 0x09+8) == 0x09+8)
	  return 3;
   }

   return(0);
}


static int sirf_nav_lib (int ttyfd, int enable)
/* set single-channel mode */
{
   u_int8_t msg_1[] = {0xa0, 0xa2, 0x00, 0x19,
                       0x80,
                       0x00, 0x00, 0x00, 0x00, /* ECEF X       */
                       0x00, 0x00, 0x00, 0x00, /* ECEF Y       */
                       0x00, 0x00, 0x00, 0x00, /* ECEF Z       */
                       0x00, 0x00, 0x00, 0x00, /* Clock Offset */
                       0x00, 0x00, 0x00, 0x00, /* Time of Week */
                       0x00, 0x00,             /* Week Number  */
                       0x0c,                   /* Channels     */
                       0x00,                   /* Reset Config */
                       0x00, 0x00, 0xb0, 0xb3};

   if (enable == 1)
      msg_1[28] = 0x10;

   crc_sirf(msg_1);
   return (write(ttyfd, msg_1, 0x19+8) != 0x19+8);
}

static int sirf_power_mask(int ttyfd, int low)
/* set dB cutoff level below which satellite info will be ignored */
{
   u_int8_t msg[] = {0xa0, 0xa2, 0x00, 0x03,
                     0x8c, 0x1c, 0x1c,
                     0x00, 0x00, 0xb0, 0xb3};

   if (low == 1)
      msg[6] = 0x14;

   crc_sirf(msg);
   return (write(ttyfd, msg, 0x03+8) != 0x03+8);
}


static int sirf_power_save(int ttyfd, int enable)
/* enable/disable SiRF trickle-power mode */ 
{
   u_int8_t msg[] = {0xa0, 0xa2, 0x00, 0x09,
                     0x97,
                     0x00, 0x00,
                     0x03, 0xe8,
                     0x00, 0x00, 0x00, 0xc8,
                     0x00, 0x00, 0xb0, 0xb3};

   if (enable == 1) {
      /* power save: duty cycle is 20% */
      msg[7] = 0x00;
      msg[8] = 0xc8;
   }

   crc_sirf(msg);
   return (write(ttyfd, msg, 0x09+8) != 0x09+8);
}
#endif /* __UNUSED __ */

/*
 * Handle the SiRF-II binary packet format.
 */

#define getb(off)	(buf[off])
#define getw(off)	((short)((getb(off) << 8) | getb(off+1)))
#define getl(off)	((int)((getw(off) << 16) | (getw(off+2) & 0xffff)))

#define putb(off,b)	do { buf[4+off] = (unsigned char)(b); } while (0)
#define putw(off,w)	do { putb(off,(w) >> 8); putb(off+1,w); } while (0)
#define putl(off,l)	do { putw(off,(l) >> 16); putw(off+2,l); } while (0)

#define RAD2DEG		5.729577795E-7		/* RAD/10^8 to DEG */

/*
 * The 'week' part of GPS dates are specified in weeks since 0000 on 06 
 * January 1980, with a rollover at 1024.  At time of writing the last 
 * rollover happened at 0000 22 August 1999.  Time-of-week is in seconds.
 */
#define GPS_EPOCH	315982800		/* GPS epoch in Unix time */
#define SECS_PER_WEEK	(60*60*24*7)		/* seconds per week */
#define GPS_ROLLOVER	(1024*SECS_PER_WEEK)	/* rollover period */

static time_t decode_time(int week, double tow)
{
    time_t now, last_rollover;

    time(&now);

    last_rollover = GPS_EPOCH + ((now-GPS_EPOCH)/GPS_ROLLOVER) * GPS_ROLLOVER;
    return last_rollover + (week * SECS_PER_WEEK) + tow;
}

static void decode_ecef(struct gps_data_t *ud, 
			double x, double y, double z, 
			double vx, double vy, double vz)
{
    /* WGS 84 geodesy parameters */
    const double a = 6378137;			/* equatorial radius */
    const double f = 1 / 298.257223563;		/* flattening */
    const double b = a * (1 - f);		/* polar radius */
    const double e2 = (a*a - b*b) / (a*a);
    const double e_2 = (a*a - b*b) / (b*b);
    double lambda,p,theta,phi,n,h,vnorth,veast,heading;

    /* geodetic location */
    lambda = atan2(y,x);
    p = sqrt(pow(x,2) + pow(y,2));
    theta = atan2(z*a,p*b);
    phi = atan2(z + e_2*b*pow(sin(theta),3),p - e2*a*pow(cos(theta),3));
    n = a / sqrt(1.0 - e2*pow(sin(phi),2));
    h = p / cos(phi) - n;
    ud->latitude = phi * RAD_2_DEG;
    ud->longitude = lambda * RAD_2_DEG;
    ud->altitude = h;	/* height above ellipsoid rather than MSL */
    REFRESH(ud->latlon_stamp);
    REFRESH(ud->altitude_stamp);

    /* velocity computation */
    vnorth = -vx*sin(phi)*cos(lambda)-vy*sin(phi)*sin(lambda)+vz*cos(phi);
    veast = -vx*sin(lambda)+vy*cos(lambda);
    ud->climb = vx*cos(phi)*cos(lambda)+vy*cos(phi)*sin(lambda)+vz*sin(phi);
    ud->speed = RAD_2_DEG * sqrt(pow(vnorth,2) + pow(veast,2));
    heading = atan2(veast,vnorth);
    if (heading < 0)
	heading += 2 * PI;
    ud->track = heading * RAD_2_DEG;
    REFRESH(ud->speed_stamp);
    REFRESH(ud->track_stamp);
    REFRESH(ud->climb_stamp);
}

static void decode_sirf(struct gps_session_t *session,
			unsigned char *buf, int len)
{
    int	st, i, j, cn;
    struct tm *when;
    time_t intfixtime;
    double fixtime;
    char buf2[MAX_PACKET_LENGTH*3];

    switch (buf[0])
    {
    case 0x02:		/* Measure Navigation Data Out */
	/* position/velocity is bytes 1-18 */
	decode_ecef(&session->gNMEAdata,
		    (double)getl(1),
		    (double)getl(5),
		    (double)getl(9),
		    (double)getw(13)/8.0,
		    (double)getw(15)/8.0,
		    (double)getw(17)/8.0);
	/* fix status is byte 19 */
	st = getb(19);
	session->gNMEAdata.status = STATUS_NO_FIX;
	if (st & 0x80)
	    session->gNMEAdata.status = STATUS_DGPS_FIX;
	else if (st & 0x02)
	    session->gNMEAdata.status = STATUS_FIX;
	REFRESH(session->gNMEAdata.status_stamp);
	session->gNMEAdata.mode = MODE_NO_FIX;
	if (st & 0x03)
	    session->gNMEAdata.mode = MODE_3D;
	else if (st & 0x02)
	    session->gNMEAdata.mode = MODE_2D;
	REFRESH(session->gNMEAdata.mode_stamp);
	/* byte 20 is DOP, see below */
	/* byte 21 is "mode 2", not clear how to interpret that */ 
	fixtime = decode_time(getw(22), getl(24)/100.00);
	intfixtime = (int)fixtime;
	when = localtime(&intfixtime);
	session->year = when->tm_year;
	session->month = when->tm_mon;
	session->day = when->tm_mday;
	session->hours = when->tm_hour;
	session->minutes = when->tm_min;
	session->seconds = (int)rint(fixtime) % 60;
	strftime(session->gNMEAdata.utc, sizeof(session->gNMEAdata.utc),
		 "%Y-%m-%dT%H:%M:%S", when);
	/* fix quality data */
	session->gNMEAdata.satellites_used = getb(28);
	for (i = 0; i < MAXCHANNELS; i++)
	    session->gNMEAdata.used[i] = getb(29+i);
	session->gNMEAdata.pdop = getb(21)/5.0;
	/* KNOWN BUG: we don't get HDOP or VDOP from this sentence */
	session->gNMEAdata.hdop = session->gNMEAdata.vdop = 0.0;
	REFRESH(session->gNMEAdata.fix_quality_stamp);
	gpsd_binary_fix_dump(session, buf2);
	gpsd_report(3, "<= GPS: %s", buf2);
	break;

    case 0x04:		/* Measured tracker data out */
	//decode_time(getw(1),getl(3)/100.0);
	// ch = getb(7);
	gpsd_zero_satellites(&session->gNMEAdata);
	for (i = st = 0; i < MAXCHANNELS; i++) {
	    int good, off = 8 + 15 * i;
	    session->gNMEAdata.PRN[st]       = getb(off);
	    session->gNMEAdata.azimuth[st]   = (int)((getb(off+1)*3)/2.0);
	    session->gNMEAdata.elevation[st] = (int)(getb(off+2)/2.0);
	    cn = 0;
	    for (j = 0; j < 10; j++)
		cn += getb(off+5+j);
	    session->gNMEAdata.ss[st] = cn/10;
	    session->gNMEAdata.used[st] = (getw(off+3) == 0xbf);
	    good = session->gNMEAdata.PRN[st] && 
		session->gNMEAdata.azimuth[st] && 
		session->gNMEAdata.elevation[st];
	    gpsd_report(4, "PRN=%2d El=%3.2f Az=%3.2f ss=%3d stat=%04x %c\n",
			getb(off), 
			getb(off+2)/2.0, 
			(getb(off+1)*3)/2.0,
			cn/10, 
			getw(off+3),
			good ? '*' : ' ');
	    if (good)
	    st += 1;
	}
	session->gNMEAdata.satellites = st;
	gpsd_report(4, "%d satellites\n", session->gNMEAdata.satellites);
	REFRESH(session->gNMEAdata.satellite_stamp);
	gpsd_binary_satellite_dump(session, buf2);
	gpsd_report(3, "<= GPS: %s", buf2);
	break;

    case 0x09:		/* CPU Throughput */
	gpsd_report(4, 
		    "SegStatMax=%.3f, SegStatLat=%3.f, AveTrkTime=%.3f, Last MS=%3.f\n", 
		    (float)getw(1)/186, (float)getw(3)/186, 
		    (float)getw(5)/186, getw(7));
    	break;

    case 0x0a:		/* Undocumented packet type */
	/* typically length 15.  Sample: a0a20a100b00000025b0b3 */
	break;

    case 0x0b:		/* Command Acknowledgement */
	gpsd_report(4, "ACK %02x\n",getb(1));
    	break;

    case 0x0c:		/* Command NAcknowledgement */
	gpsd_report(4, "NAK %02x\n",getb(1));
    	break;

    case 0x1b:		/* Undocumented packet type */
	break;

    case 0x29:		/* Undocumented packet type */
	break;

    case 0x32:	/* Undocumented packet type */
	/* Sample: a0a23278001200000000000000000000bcb0b3 */
	break;

    default:
	buf2[0] = '\0';
	for (i = 0; i < len; i++)
	    sprintf(buf2+strlen(buf2), "%02x", buf[i]);
	gpsd_report(1, "Unknown SiRF packet length %d: %s\n", len, buf2);
	break;
    }
}

static void sirfbin_handle_input(struct gps_session_t *session)
{
    packet_get_sirf(session);
    decode_sirf(session, session->outbuffer+4, session->outbuflen-4);
    packet_accept(session);
}

static int sirfbin_switch(struct gps_session_t *session, int speed)
{
    return sirf_speed(session->gNMEAdata.gps_fd, speed);
}

/* this is everything we export */
struct gps_type_t sirf_binary =
{
    's',		/* invoke with -T s */
    "SIRF-II binary",	/* full name of type */
    NULL,		/* only switched to by some other driver */
    NULL,		/* initialize the device */
    sirfbin_handle_input,/* read and parse message packets */
    NULL,		/* send DGPS correction */
    sirfbin_switch,	/* we can change baud rate */
    NULL,		/* caller needs to supply a close hook */
    1,			/* updates every second */
};
