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

static u_int16_t sirf_write(int fd, u_int8_t *msg) {
   int       i, len, ok, crc;
   char	     buf[MAX_PACKET_LENGTH*2];

   len = (msg[2] << 8) | msg[3];

   /* calculate CRC */
   crc = 0;
   for (i = 0; i < len; i++)
	crc += msg[4 + i];
   crc &= 0x7fff;

   /* enter CRC after payload */
   msg[len + 4] = (u_int8_t)((crc & 0xff00) >> 8);
   msg[len + 5] = (u_int8_t)( crc & 0x00ff);

   buf[0] = '\0';
   for (i = 0; i < len+8; i++)
       sprintf(buf+strlen(buf), " %02x", msg[i]);
   gpsd_report(4, "Writing SiRF control type %02x:%s\n", msg[4], buf);
   ok = write(fd, msg, len+8) == len+8;
   tcdrain(fd);
   return(ok);
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
		     0x00,			/* reserved */
                     0x00, 0x00, 0xb0, 0xb3};

   msg[7] = HI(speed);
   msg[8] = LO(speed);
   return (sirf_write(ttyfd, msg));
}

static int sirf_to_nmea(int ttyfd, int speed) 
/* switch from binary to NMEA at specified baud */
{
   u_int8_t msg[] = {0xa0, 0xa2, 0x00, 0x18,
                     0x81, 0x02,
                     0x01, 0x01, /* GGA */
                     0x00, 0x00, /* suppress GLL */
                     0x01, 0x01, /* GSA */
                     0x05, 0x01, /* GSV */
                     0x01, 0x01, /* RMC */
                     0x00, 0x00, /* suppress VTG */
                     0x00, 0x01, 0x00, 0x01,
                     0x00, 0x01, 0x00, 0x01,
                     0x12, 0xc0, /* 4800 bps */
                     0x00, 0x00, 0xb0, 0xb3};

   msg[26] = HI(speed);
   msg[27] = LO(speed);
   return (sirf_write(ttyfd, msg));
}

/*
 * Handle the SiRF-II binary packet format.
 * 
 * SiRF message 2 (Measure Navigation Data Out) gives us everything we want
 * except PDOP, VDOP, and altitude with respect to MSL.  SiRF message 41
 * (Geodetic Navigation Information) adds MSLfix.altitude, but many of its
 * other fields are garbage in firmware versions before 232.  So...we
 * use all the data from message 2 *except* altitude, which we get from 
 * message 41.
 */

#define getb(off)	(buf[off])
#define getw(off)	((short)((getb(off) << 8) | getb(off+1)))
#define getl(off)	((int)((getw(off) << 16) | (getw(off+2) & 0xffff)))

static void sirfbin_mode(struct gps_session_t *session, int mode)
{
    if (mode == 0) {
	gpsd_switch_driver(session, "SiRF-II NMEA");
	sirf_to_nmea(session->gpsdata.gps_fd,session->gpsdata.baudrate);
	session->gpsdata.driver_mode = 0;
    }
}

int sirf_parse(struct gps_session_t *session, unsigned char *buf, int len)
{
    int	st, i, j, cn, navtype, mask;
    char buf2[MAX_PACKET_LENGTH*3] = "";
    double fv;

    buf2[0] = '\0';
    for (i = 0; i < len; i++)
	sprintf(buf2+strlen(buf2), "%02x", buf[i]);
    gpsd_report(5, "Raw SiRF packet type %d length %d: %s\n", buf[0], len, buf2);

    switch (buf[0])
    {
    case 0x02:		/* Measure Navigation Data Out */
	if (!(session->driverstate & SIRF_GE_232)) {
	    /* WGS 84 geodesy parameters */
	    double x, y, z, vx, vy, vz;
	    double lambda,phi,p,theta,n,h,vnorth,veast,heading;
	    const double a = 6378137;			/* equatorial radius */
	    const double f = 1 / 298.257223563;		/* flattening */
	    const double b = a * (1 - f);		/* polar radius */
	    const double e2 = (a*a - b*b) / (a*a);
	    const double e_2 = (a*a - b*b) / (b*b);
	    int mask = 0;

	    /* save the old fix for later uncertainty computations */
	    memcpy(&session->lastfix, &session->gpsdata.fix, 
		   sizeof(struct gps_fix_t));

	    /* position/velocity is bytes 1-18 */
	    x = getl(1);
	    y = getl(5);
	    z = getl(9);
	    vx = getw(13)/8.0;
	    vy = getw(15)/8.0;
	    vz = getw(17)/8.0;

	    /* geodetic location */
	    lambda = atan2(y,x);
	    p = sqrt(pow(x,2) + pow(y,2));
	    theta = atan2(z*a,p*b);
	    phi = atan2(z + e_2*b*pow(sin(theta),3),p - e2*a*pow(cos(theta),3));
	    n = a / sqrt(1.0 - e2*pow(sin(phi),2));
	    h = p / cos(phi) - n;
	    session->gpsdata.fix.latitude = phi * RAD_2_DEG;
	    session->gpsdata.fix.longitude = lambda * RAD_2_DEG;
	    /* velocity computation */
	    vnorth = -vx*sin(phi)*cos(lambda)-vy*sin(phi)*sin(lambda)+vz*cos(phi);
	    veast = -vx*sin(lambda)+vy*cos(lambda);
	    session->gpsdata.fix.climb = vx*cos(phi)*cos(lambda)+vy*cos(phi)*sin(lambda)+vz*sin(phi);
	    session->gpsdata.fix.speed = RAD_2_DEG * sqrt(pow(vnorth,2) + pow(veast,2));
	    heading = atan2(veast,vnorth);
	    if (heading < 0)
		heading += 2 * PI;
	    session->gpsdata.fix.track = heading * RAD_2_DEG;
	    /* fix status is byte 19 */
	    navtype = getb(19);
	    session->gpsdata.status = STATUS_NO_FIX;
	    session->gpsdata.fix.mode = MODE_NO_FIX;
	    if (navtype & 0x80)
		session->gpsdata.status = STATUS_DGPS_FIX;
	    else if ((navtype & 0x07) > 0 && (navtype & 0x07) < 7)
		session->gpsdata.status = STATUS_FIX;
	    session->gpsdata.fix.mode = MODE_NO_FIX;
	    if ((navtype & 0x07) == 4 || (navtype & 0x07) == 6)
		session->gpsdata.fix.mode = MODE_3D;
	    else if (session->gpsdata.status)
		session->gpsdata.fix.mode = MODE_2D;
	    if (session->gpsdata.fix.mode == MODE_3D && (session->driverstate & SIRF_SEEN_41))
		mask |= ALTITUDE_SET;
	    gpsd_report(4, "MND 0x02: Navtype = 0x%0x, Status = %d, mode = %d\n", 
			navtype,session->gpsdata.status,session->gpsdata.fix.mode);
	    /* byte 20 is HDOP, see below */
	    /* byte 21 is "mode 2", not clear how to interpret that */ 
	    session->gpsdata.fix.time=gpstime_to_unix(getw(22), getl(24)*1e-2);
#ifdef NTPSHM_ENABLE
	    ntpshm_put(session, session->gpsdata.fix.time);
#endif /* defined(SHM_H) && defined(IPC_H) */

	    gpsd_binary_fix_dump(session, buf2);
	    /* fix quality data */
	    session->gpsdata.hdop = getb(20)/5.0;
	    session->gpsdata.satellites_used = getb(28);
	    for (i = 0; i < MAXCHANNELS; i++)
		session->gpsdata.used[i] = getb(29+i);
	    session->gpsdata.pdop = session->gpsdata.vdop = 0.0;
	    gpsd_binary_quality_dump(session, buf2 + strlen(buf2));
	    gpsd_report(3, "<= GPS: %s", buf2);
	    session->gpsdata.sentence_length = 41;
	    strcpy(session->gpsdata.tag, "MND");
	    return mask | TIME_SET | LATLON_SET | TRACK_SET | SPEED_SET | STATUS_SET | MODE_SET | HDOP_SET;
	}

    case 0x04:		/* Measured tracker data out */
	/*
	 * The freaking brain-dead SiRF chip doesn't obey its own
	 * rate-control command for 04, at least at firmware rev. 231, 
	 * so we have to do our own rate-limiting here...
	 */
	if (session->counter % 5)
	    break;
	gpsd_zero_satellites(&session->gpsdata);
	for (i = st = 0; i < MAXCHANNELS; i++) {
	    int good, off = 8 + 15 * i;
	    session->gpsdata.PRN[st]       = getb(off);
	    session->gpsdata.azimuth[st]   = (int)((getb(off+1)*3)/2.0);
	    session->gpsdata.elevation[st] = (int)(getb(off+2)/2.0);
	    cn = 0;
	    for (j = 0; j < 10; j++)
		cn += getb(off+5+j);
	    session->gpsdata.ss[st] = cn/10;
	    // session->gpsdata.used[st] = (getw(off+3) == 0xbf);
	    good = session->gpsdata.PRN[st] && 
		session->gpsdata.azimuth[st] && 
		session->gpsdata.elevation[st];
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
	session->gpsdata.satellites = st;
	gpsd_binary_satellite_dump(session, buf2);
	gpsd_report(4, "MTD 0x04: %d satellites\n", st);
	gpsd_report(3, "<= GPS: %s", buf2);
	session->gpsdata.sentence_length = 188;
	strcpy(session->gpsdata.tag, "MTD");
	return SATELLITE_SET;

    case 0x09:		/* CPU Throughput */
	gpsd_report(4, 
		    "THR 0x09: SegStatMax=%.3f, SegStatLat=%3.f, AveTrkTime=%.3f, Last MS=%3.f\n", 
		    (float)getw(1)/186, (float)getw(3)/186, 
		    (float)getw(5)/186, getw(7));
    	return 0;

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
	return 0;

    case 0x0a:		/* Error ID Data */
	switch (getw(1))
	{
	case 2:
	    gpsd_report(4, "EID 0x0a type 2: Subframe error on PRN %ld\n", getl(5));
	    break;

	case 4107:
	    gpsd_report(4, "EID 0x0a type 4177: neither KF nor LSQ fix.\n", getl(5));
	    break;

	default:
	    gpsd_report(4, "EID 0x0a: Error ID type %d\n", getw(1));
	    break;
	}
	return 0;

    case 0x0b:		/* Command Acknowledgement */
	gpsd_report(4, "ACK 0x0b: %02x\n",getb(1));
    	return 0;

    case 0x0c:		/* Command NAcknowledgement */
	gpsd_report(4, "NAK 0x0c: %02x\n",getb(1));
    	return 0;

    case 0x0d:		/* Visible List */
	return 0;

    case 0x12:		/* OK To Send */
	gpsd_report(4, "OTS 0x12: send indicator = %d\n",getb(1));
	return 0;

    case 0x1b:		/* DGPS status (undocumented) */
	return 0;

    case 0x29:		/* Geodetic Navigation Information */
	mask = 0;
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
	     *
	     * To work around the incomplete implementation of this
	     * packet in 231, we assume that only the altitude field
	     * from this packet is valid.
	     */
	    navtype = getw(3);
	    session->gpsdata.status = STATUS_NO_FIX;
	    session->gpsdata.fix.mode = MODE_NO_FIX;
	    if (navtype & 0x80)
		session->gpsdata.status = STATUS_DGPS_FIX;
	    else if ((navtype & 0x07) > 0 && (navtype & 0x07) < 7)
		session->gpsdata.status = STATUS_FIX;
	    session->gpsdata.fix.mode = MODE_NO_FIX;
	    if ((navtype & 0x07) == 4 || (navtype & 0x07) == 6)
		session->gpsdata.fix.mode = MODE_3D;
	    else if (session->gpsdata.status)
		session->gpsdata.fix.mode = MODE_2D;
	    gpsd_report(4, "GNI 0x29: Navtype = 0x%0x, Status = %d, mode = %d\n", 
			navtype, session->gpsdata.status, session->gpsdata.fix.mode);
	    /*
	     * Compute UTC from extended GPS time.  The protocol reference
	     * claims this 16-bit field is "extended" GPS weeks, but I'm
	     * seeing a quantity that has undergone 10-bit wraparound.
	     */
	    gpsd_report(5, "MID 41 GPS Week: %d  TOW: %d\n", getw(5), getl(7));
	    session->gpsdata.fix.time = gpstime_to_unix(getw(5), getl(7)*1e-4);
	    gpsd_report(5, "MID 41 UTC: %lf\n", session->gpsdata.fix.time);
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
	    session->gpsdata.fix.latitude = getl(23)*1e-7;
	    session->gpsdata.fix.longitude = getl(27)*1e-7;
	    /* skip 4 bytes of altitude from ellipsoid */
	    mask = TIME_SET | LATLON_SET | STATUS_SET | MODE_SET;
	}
	session->gpsdata.fix.altitude = getl(31)*1e-2;
	if (session->driverstate & SIRF_GE_232) {
	    /* skip 1 byte of map datum */
	    session->gpsdata.fix.speed = getw(36)*1e-2;
	    session->gpsdata.fix.track = getw(38)*1e-2;
	    /* skip 2 bytes of magnetic variation */
	    session->gpsdata.fix.climb = getw(42)*1e-2;
	    /* HDOP should be available at byte 89, but in 231 it's zero. */
	    gpsd_binary_fix_dump(session, buf2);
	    gpsd_report(3, "<= GPS: %s", buf2);
	    mask |= SPEED_SET | TRACK_SET | CLIMB_SET; 
	    session->gpsdata.sentence_length = 91;
	    strcpy(session->gpsdata.tag, "GND");
	}
	session->driverstate |= SIRF_SEEN_41;
	return mask;

    case 0x32:		/* SBAS corrections */
	return 0;

    case 0xff:		/* Debug messages */
	buf2[0] = '\0';
	for (i = 1; i < len; i++)
	    if (isprint(buf[i]))
		sprintf(buf2+strlen(buf2), "%c", buf[i]);
	    else
		sprintf(buf2+strlen(buf2), "\\x%02x", buf[i]);
	gpsd_report(4, "DD  0xff: %s\n", buf2);
	return 0;

    default:
	buf2[0] = '\0';
	for (i = 0; i < len; i++)
	    sprintf(buf2+strlen(buf2), "%02x", buf[i]);
	gpsd_report(1, "Unknown SiRF packet length %d: %s\n", len, buf2);
	return 0;
    }

    return 0;
}

static int sirfbin_parse_input(struct gps_session_t *session)
{
    int st;

    if (session->packet_type == SIRF_PACKET){
	st = sirf_parse(session, session->outbuffer+4, session->outbuflen-8);
	session->gpsdata.driver_mode = 1;
	return st;
    } else if (session->packet_type == NMEA_PACKET) {
	st = nmea_parse(session->outbuffer, &session->gpsdata);
	session->gpsdata.driver_mode = 0;
	return st;
    } else
	return 0;
}

static void sirfbin_initializer(struct gps_session_t *session)
/* poll for software version in order to check for old firmware */
{
    if (session->packet_type == NMEA_PACKET) {
	if (session->driverstate & SIRF_LT_231) {
	    gpsd_report(1, "SiRF chipset has old firmware, falling back to  SiRF NMEA\n");
	    nmea_send(session->gpsdata.gps_fd, "$PSRF105,0");
	    gpsd_switch_driver(session, "SiRF-II NMEA");
	    return;
	} else {
	    gpsd_report(1, "Switching chip mode to SiRF binary.\n");
	    nmea_send(session->gpsdata.gps_fd, "$PSRF100,0,%d,8,1,0", session->gpsdata.baudrate);
	    packet_sniff(session);
	}
    }
    /* do this every time*/
    {
	u_int8_t dgpscontrol[] = {0xa0, 0xa2, 0x00, 0x07,
				 0x85, 0x01, 0x00, 0x00,
				 0x00, 0x00, 0x00,
				 0x00, 0x00, 0xb0, 0xb3};
	u_int8_t sbasparams[] = {0xa0, 0xa2, 0x00, 0x06,
				 0xaa, 0x00, 0x01, 0x00,
				 0x00, 0x00,
				 0x00, 0x00, 0xb0, 0xb3};
	u_int8_t versionprobe[] = {0xa0, 0xa2, 0x00, 0x02,
				 0x84, 0x00,
				 0x00, 0x00, 0xb0, 0xb3};
	gpsd_report(4, "Setting DGPS control to use SBAS...\n");
	sirf_write(session->gpsdata.gps_fd, dgpscontrol);
	gpsd_report(4, "Setting SBAS to auto/integrity mode...\n");
	sirf_write(session->gpsdata.gps_fd, sbasparams);
	gpsd_report(4, "Probing for firmware version...\n");
	sirf_write(session->gpsdata.gps_fd, versionprobe);
    }
}

static int sirfbin_speed(struct gps_session_t *session, int speed)
{
    return sirf_speed(session->gpsdata.gps_fd, speed);
}

/* this is everything we export */
struct gps_type_t sirf_binary =
{
    "SiRF-II binary",		/* full name of type */
    "$Ack Input105.",		/* expected response to SiRF PSRF105 */
    NULL,			/* no probe */
    sirfbin_initializer,	/* initialize the device */
    packet_get,			/* how to grab a packet */
    sirfbin_parse_input,	/* read and parse message packets */
    NULL,			/* send DGPS correction */
    sirfbin_speed,		/* we can change baud rate */
    sirfbin_mode,		/* there's a mode switcher */
    NULL,			/* caller needs to supply a close hook */
    1,				/* updates every second */
};
#endif /* defined(SIRFII_ENABLE) && defined(BINARY_ENABLE) */
