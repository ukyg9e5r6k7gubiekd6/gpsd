/*
 * This is the gpsd driver for SiRF-II GPSes operating in binary mode.
 *
 * The advantage: Reports climb/sink rate (raw-mode clients won't see this).
 * The disadvantages: Doesn't return PDOP or VDOP, just HDOP.
 *
 * Chris Kuethe, our SiRF expert, tells us:
 * 
 * "I don't see any indication in any of my material that PDOP, GDOP
 * or VDOP are output. There are quantities called Estimated
 * {Horizontal Position, Vertical Position, Time, Horizonal Velocity}
 * Error, but those are apparently only valid when SiRFDRive is
 * active."
 *
 * "(SiRFdrive is their Dead Reckoning augmented firmware. It
 * allows you to feed odometer ticks, gyro and possibly 
 * accelerometer inputs to the chip to allow it to continue 
 * to navigate in the absence of satellite information, and 
 * to improve fixes when you do have satellites.)"
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
#if defined(SIRFII_ENABLE) && defined(BINARY_ENABLE)

#define HI(n)		((n) >> 8)
#define LO(n)		((n) & 0xff)

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

/*
 * Handle the SiRF-II binary packet format.
 * 
 * SiRF message 2 (Measure Navigation Data Out) gives us everything we want
 * except PDOP, VDOP, and altitude with respect to MSL.  SiRF message 41
 * (Geodetic Navigation Information) adds MSL altitude, but many of its
 * other fields are garbage in firmware versions before 232.  So...we
 * use all the data from message 2 *except* altitude, which we get from 
 * message 41.
 */

#define getb(off)	(buf[off])
#define getw(off)	((short)((getb(off) << 8) | getb(off+1)))
#define getl(off)	((int)((getw(off) << 16) | (getw(off+2) & 0xffff)))

/*
 * The 'week' part of GPS dates are specified in weeks since 0000 on 06 
 * January 1980, with a rollover at 1024.  At time of writing the last 
 * rollover happened at 0000 22 August 1999.  Time-of-week is in seconds.
 *
 * This code copes with both conventional GPS weeks and the "extended" 
 * 15-or-16-bit version with no wraparound that is supposed to appear in
 * the Geodetic Navigation Information (0x29) packet.  Some firmware 
 * versions (notably 231) actually ship the wrapped 10-bit week, despite
 * what the protocol reference says.
 */
#define GPS_EPOCH	315982800		/* GPS epoch in Unix time */
#define SECS_PER_WEEK	(60*60*24*7)		/* seconds per week */
#define GPS_ROLLOVER	(1024*SECS_PER_WEEK)	/* rollover period */

static void extract_time(struct gps_session_t *session, int week, double tow)
{
    struct tm when;
    time_t intfixtime;
    double fixtime;

    if (week >= GPS_ROLLOVER)
	fixtime = GPS_EPOCH + (week * SECS_PER_WEEK) + tow;
    else {
	time_t now, last_rollover;
	time(&now);
	last_rollover = GPS_EPOCH+((now-GPS_EPOCH)/GPS_ROLLOVER)*GPS_ROLLOVER;
	fixtime = last_rollover + (week * SECS_PER_WEEK) + tow;
    }

    intfixtime = (int)fixtime;
    gmtime_r(&intfixtime, &when);
    session->year = when.tm_year + 1900;
    session->month = when.tm_mon + 1;
    session->day = when.tm_mday;
    session->hours = when.tm_hour;
    session->minutes = when.tm_min;
    session->seconds = fixtime - (intfixtime / 60) * 60;
    strftime(session->gNMEAdata.utc, sizeof(session->gNMEAdata.utc),
	     "%Y-%m-%dT%H:%M:", &when);
    sprintf(session->gNMEAdata.utc+strlen(session->gNMEAdata.utc),
	    "%02.3f", session->seconds);
    session->gNMEAdata.gps_time = fixtime;
}

static void decode_ecef(struct gps_data_t *ud, 
			double x, double y, double z, 
			double vx, double vy, double vz)
{
    /* WGS 84 geodesy parameters */
    double lambda,phi,p,theta,n,h,vnorth,veast,heading;
    const double a = 6378137;			/* equatorial radius */
    const double f = 1 / 298.257223563;		/* flattening */
    const double b = a * (1 - f);		/* polar radius */
    const double e2 = (a*a - b*b) / (a*a);
    const double e_2 = (a*a - b*b) / (b*b);

    /* geodetic location */
    lambda = atan2(y,x);
    p = sqrt(pow(x,2) + pow(y,2));
    theta = atan2(z*a,p*b);
    phi = atan2(z + e_2*b*pow(sin(theta),3),p - e2*a*pow(cos(theta),3));
    n = a / sqrt(1.0 - e2*pow(sin(phi),2));
    h = p / cos(phi) - n;
    ud->latitude = phi * RAD_2_DEG;
    ud->longitude = lambda * RAD_2_DEG;
    REFRESH(ud->latlon_stamp);
#ifdef __UNUSED__
    ud->altitude = h;	/* height above ellipsoid rather than MSL */
    REFRESH(ud->altitude_stamp);
#endif /* __UNUSED__ */

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

static void sirfbin_mode(struct gps_session_t *session, int mode)
{
    if (mode == 0) {
	gpsd_switch_driver(session, "SiRF-II NMEA");
	sirf_to_nmea(session->gNMEAdata.gps_fd,session->gNMEAdata.baudrate);
	session->gNMEAdata.driver_mode = 0;
    }
}


void sirf_parse(struct gps_session_t *session, unsigned char *buf, int len)
{
    int	st, i, j, cn, navtype;
    char buf2[MAX_PACKET_LENGTH*3] = "";
    double fv;

    buf2[0] = '\0';
    for (i = 0; i < len; i++)
	sprintf(buf2+strlen(buf2), "%02x", buf[i]);
    gpsd_report(5, "Raw SiRF packet type %d length %d: %s\n", buf[0], len, buf2);

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
	navtype = getb(19);
	session->gNMEAdata.status = STATUS_NO_FIX;
	session->gNMEAdata.mode = MODE_NO_FIX;
	if (navtype & 0x80)
	    session->gNMEAdata.status = STATUS_DGPS_FIX;
	else if ((navtype & 0x07) > 0 && (navtype & 0x07) < 7)
	    session->gNMEAdata.status = STATUS_FIX;
	REFRESH(session->gNMEAdata.status_stamp);
	session->gNMEAdata.mode = MODE_NO_FIX;
	if ((navtype & 0x07) == 4 || (navtype & 0x07) == 6)
	    session->gNMEAdata.mode = MODE_3D;
	else if (session->gNMEAdata.status)
	    session->gNMEAdata.mode = MODE_2D;
	REFRESH(session->gNMEAdata.mode_stamp);
	gpsd_report(4, "MND 0x02: Navtype = 0x%0x, Status = %d, mode = %d\n", 
		    navtype,session->gNMEAdata.status,session->gNMEAdata.mode);
	/* byte 20 is HDOP, see below */
	/* byte 21 is "mode 2", not clear how to interpret that */ 
	extract_time(session, getw(22), getl(24)/100.0);
	gpsd_binary_fix_dump(session, buf2);
	/* fix quality data */
	session->gNMEAdata.hdop = getb(20)/5.0;
	session->gNMEAdata.satellites_used = getb(28);
	for (i = 0; i < MAXCHANNELS; i++)
	    session->gNMEAdata.used[i] = getb(29+i);
	session->gNMEAdata.pdop = session->gNMEAdata.vdop = 0.0;
	REFRESH(session->gNMEAdata.fix_quality_stamp);
	gpsd_binary_quality_dump(session, buf2 + strlen(buf2));
	gpsd_report(3, "<= GPS: %s", buf2);
	break;

    case 0x04:		/* Measured tracker data out */
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
	    // session->gNMEAdata.used[st] = (getw(off+3) == 0xbf);
	    good = session->gNMEAdata.PRN[st] && 
		session->gNMEAdata.azimuth[st] && 
		session->gNMEAdata.elevation[st];
#ifdef __UNUSED__
	    gpsd_report(4, "PRN=%2d El=%3.2f Az=%3.2f ss=%3d stat=%04x %c\n",
			getb(off), 
			getb(off+2)/2.0, 
			(getb(off+1)*3)/2.0,
			cn/10, 
			getw(off+3),
			good ? '*' : ' ');
#endif /* UNUSED */
	    if (good)
		st += 1;
	}
	session->gNMEAdata.satellites = st;
	REFRESH(session->gNMEAdata.satellite_stamp);
	gpsd_binary_satellite_dump(session, buf2);
	gpsd_report(4, "MTD 0x04: %d satellites\n", st);
	gpsd_report(3, "<= GPS: %s", buf2);
	break;

    case 0x09:		/* CPU Throughput */
	gpsd_report(4, 
		    "THR 0x09: SegStatMax=%.3f, SegStatLat=%3.f, AveTrkTime=%.3f, Last MS=%3.f\n", 
		    (float)getw(1)/186, (float)getw(3)/186, 
		    (float)getw(5)/186, getw(7));
    	break;

    case 0x06:		/* Software Version String */
	gpsd_report(4, "FV  0x06: Firmware version: %s\n", 
		    session->outbuffer+5);
	fv = atof(session->outbuffer+5);
	if (fv < 231) {
	    session->driverstate |= SIRF_LT_231;
	    sirfbin_mode(session, 0);
	} else if (fv < 232) 
	    session->driverstate |= SIRF_EQ_231;
	else
	    session->driverstate |= SIRF_GE_232;
	gpsd_report(4, "Driver state flags are: %0x\n", session->driverstate);
	break;

    case 0x0a:		/* Error ID Data */
	gpsd_report(4, "EID 0x0a: Error ID type %d\n", getw(1));
	break;

    case 0x0b:		/* Command Acknowledgement */
	gpsd_report(4, "ACK 0x0b: %02x\n",getb(1));
    	break;

    case 0x0c:		/* Command NAcknowledgement */
	gpsd_report(4, "NAK 0x0c: %02x\n",getb(1));
    	break;

    case 0x0d:		/* Visible List */
	break;

    case 0x12:		/* OK To Send */
	gpsd_report(4, "OTS 0x12: send indicator = %d\n",getb(1));
	break;

    case 0x1b:		/* DGPS status (undocumented) */
	break;

    case 0x29:		/* Geodetic Navigation Information */
	if (session->driverstate & SIRF_GE_232) {
	    /*
	     * Many versions of the SiRF protocol manual don't document 
	     * this sentence at all.  Those that do may incorrectly
	     * describe UTC Day, Hour, and Minute as 2-byte quantities,
	     * not 1-byte. Chris Kuethe, our SiRF expert, tells us:
	     *
	     * "The Geodetic Navigation packet (0x29) was not fully
	     * implemented in firmware prior to version 2.3.2. So for
	     * anyone running 231.000.000 or earlier (including ES,
	     * SiRFDRive, XTrac trains) you won't get UTC time. I don't
	     * know what's broken in firmwares before 2.3.1..."
	     * To work around the incomplete implementation of this
	     * packet in 231, we assume that only the altitude field
	     * from this packet is valid.
	     */
	    navtype = getw(3);
	    session->gNMEAdata.status = STATUS_NO_FIX;
	    session->gNMEAdata.mode = MODE_NO_FIX;
	    if (navtype & 0x80)
		session->gNMEAdata.status = STATUS_DGPS_FIX;
	    else if ((navtype & 0x07) > 0 && (navtype & 0x07) < 7)
		session->gNMEAdata.status = STATUS_FIX;
	    REFRESH(session->gNMEAdata.status_stamp);
	    session->gNMEAdata.mode = MODE_NO_FIX;
	    if ((navtype & 0x07) == 4 || (navtype & 0x07) == 6)
		session->gNMEAdata.mode = MODE_3D;
	    else if (session->gNMEAdata.status)
		session->gNMEAdata.mode = MODE_2D;
	    REFRESH(session->gNMEAdata.mode_stamp);
	    gpsd_report(4, "GNI 0x29: Navtype = 0x%0x, Status = %d, mode = %d\n", 
			navtype, session->gNMEAdata.status, session->gNMEAdata.mode);
	    /*
	     * Compute UTC from extended GPS time.  The protocol reference
	     * claims this 16-bit field is "extended" GPS weeks, but I'm
	     * seeing a quantity that has undergone 10-bit wraparound.
	     */
	    gpsd_report(5, "MID 41 GPS Week: %d  TOW: %d\n", getw(5), getl(7));
	    extract_time(session, getw(5), getl(7)/1000.00);
	    gpsd_report(5, "MID 41 UTC: %s\n", session->gNMEAdata.utc);
	    /*
	     * Skip UTC, left all zeros in 231 and older firmware versions, 
	     * and misdocumented in the Protocol Reference (version 1.4).
	     *            Documented:        Real:
	     * UTC year       2               2
	     * UTC month      1               1
	     * UTC day        2               1
	     * UTC hour       2               1
	     * UTC minute     2               1
	     * UTC second     2               2
	     *                11              8
	     */
	    /* skip 4 bytes of satellite map */
	    session->gNMEAdata.latitude = getl(23)/1e+7;
	    session->gNMEAdata.longitude = getl(27)/1e+7;
	    REFRESH(session->gNMEAdata.latlon_stamp);
	    /* skip 4 bytes of altitude from ellipsoid */
	}
	session->gNMEAdata.altitude = getl(31)/100;
	REFRESH(session->gNMEAdata.altitude_stamp);
	if (session->driverstate & SIRF_GE_232) {
	    /* skip 1 byte of map datum */
	    session->gNMEAdata.speed = getw(36)/100;
	    REFRESH(session->gNMEAdata.speed_stamp);
	    session->gNMEAdata.track = getw(38)/100;
	    REFRESH(session->gNMEAdata.track_stamp);
	    /* skip 2 bytes of magnetic variation */
	    session->gNMEAdata.climb = getw(42)/100;
	    REFRESH(session->gNMEAdata.climb_stamp);
	    /* HDOP should be available at byte 89, but in 231 it's zero. */
	    gpsd_binary_fix_dump(session, buf2);
	    gpsd_report(3, "<= GPS: %s", buf2);
	}
	break;

    case 0x32:		/* SBAS corrections */
	break;

    case 0xff:		/* Debug messages */
	buf2[0] = '\0';
	for (i = 1; i < len; i++)
	    if (isprint(buf[i]))
		sprintf(buf2+strlen(buf2), "%c", buf[i]);
	    else
		sprintf(buf2+strlen(buf2), "\\x%02x", buf[i]);
	gpsd_report(4, "DD  0xff: %s\n", buf2);
	break;

    default:
	buf2[0] = '\0';
	for (i = 0; i < len; i++)
	    sprintf(buf2+strlen(buf2), "%02x", buf[i]);
	gpsd_report(1, "Unknown SiRF packet length %d: %s\n", len, buf2);
	break;
    }
}

static int sirfbin_handle_input(struct gps_session_t *session, int waiting)
{
    if (!packet_get(session, waiting)) 
	return 0;

    if (session->packet_type == SIRF_PACKET){
	sirf_parse(session, session->outbuffer+4, session->outbuflen-8);
	session->gNMEAdata.driver_mode = 1;
	return 1;
    } else if (session->packet_type == NMEA_PACKET) {
	nmea_parse(session->outbuffer, &session->gNMEAdata);
	session->gNMEAdata.driver_mode = 0;
	return 1;
    } else
	return 0;
}

static void sirfbin_initializer(struct gps_session_t *session)
/* poll for software version in order to check for old firmware */
{
    if (session->packet_type == NMEA_PACKET) {
	if (session->driverstate & SIRF_LT_231) {
	    gpsd_report(1, "SiRF chipset has old firmware, falling back to  SiRF NMEA\n");
	    nmea_send(session->gNMEAdata.gps_fd, "$PSRF105,0");
	    gpsd_switch_driver(session, "SiRF-II NMEA");
	    return;
	} else {
	    gpsd_report(1, "Switching chip mode to SiRF binary.\n");
	    nmea_send(session->gNMEAdata.gps_fd, "$PSRF100,0,%d,8,1,0", session->gNMEAdata.baudrate);
	    packet_sniff(session);
	}
    }
    /* do this every time*/
    {
	//u_int8_t ratecontrol[] = {0xa0, 0xa2, 0x00, 0x08,
	//			 0xa6, 0x00, 0x04, 0x05,
	//			 0x00, 0x00, 0x00, 0x00,
	//			 0x00, 0x00, 0xb0, 0xb3};
	u_int8_t versionprobe[] = {0xa0, 0xa2, 0x00, 0x02,
				 0x84, 0x00,
				 0x00, 0x00, 0xb0, 0xb3};
	//gpsd_report(4, "Setting GSV rate to 0.2Hz...\n");
	//crc_sirf(ratecontrol);
	//write(session->gNMEAdata.gps_fd, ratecontrol, 16);
	gpsd_report(4, "Probing for firmware version...\n");
	crc_sirf(versionprobe);
	write(session->gNMEAdata.gps_fd, versionprobe, 10);
    }
}

static int sirfbin_speed(struct gps_session_t *session, int speed)
{
    return sirf_speed(session->gNMEAdata.gps_fd, speed);
}

/* this is everything we export */
struct gps_type_t sirf_binary =
{
    "SiRF-II binary",		/* full name of type */
    "$Ack Input105.",	/* expected response to SiRF PSRF105 */
    NULL,		/* no probe */
    sirfbin_initializer,	/* initialize the device */
    sirfbin_handle_input,	/* read and parse message packets */
    NULL,		/* send DGPS correction */
    sirfbin_speed,	/* we can change baud rate */
    sirfbin_mode,	/* there's a mode switcher */
    NULL,		/* caller needs to supply a close hook */
    1,			/* updates every second */
};
#endif /* defined(SIRFII_ENABLE) && defined(BINARY_ENABLE) */
