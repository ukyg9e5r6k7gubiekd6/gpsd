/*
 * This is the gpsd driver for SiRF-II GPSes operating in binary mode.
 * It also handles uBlox, a SiRF derivative.
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

static size_t sirf_write(int fd, unsigned char *msg) {
   unsigned int       crc;
   size_t    i, len, ok;
   char	     buf[MAX_PACKET_LENGTH*2];

   len = (size_t)((msg[2] << 8) | msg[3]);

   /* calculate CRC */
   crc = 0;
   for (i = 0; i < len; i++)
	crc += (int)msg[4 + i];
   crc &= 0x7fff;

   /* enter CRC after payload */
   msg[len + 4] = (unsigned char)((crc & 0xff00) >> 8);
   msg[len + 5] = (unsigned char)( crc & 0x00ff);

   buf[0] = '\0';
   for (i = 0; i < len+8; i++)
       (void)snprintf(buf+strlen(buf),sizeof(buf)-strlen(buf),
		      " %02x", (unsigned)msg[i]);
   gpsd_report(4, "Writing SiRF control type %02x:%s\n", msg[4], buf);
   ok = (write(fd, msg, len+8) == (ssize_t)(len+8));
   (void)tcdrain(fd);
   return(ok);
}

static size_t sirf_speed(int ttyfd, speed_t speed) 
/* change speed in binary mode */
{
    /*@ +charint @*/
   unsigned char msg[] = {0xa0, 0xa2, 0x00, 0x09,
                     0x86, 
                     0x0, 0x0, 0x12, 0xc0,	/* 4800 bps */
		     0x08,			/* 8 data bits */
		     0x01,			/* 1 stop bit */
		     0x00,			/* no parity */
		     0x00,			/* reserved */
                     0x00, 0x00, 0xb0, 0xb3};
   /*@ -charint @*/

   msg[7] = (unsigned char)HI(speed);
   msg[8] = (unsigned char)LO(speed);
   return (sirf_write(ttyfd, msg));
}

static size_t sirf_to_nmea(int ttyfd, speed_t speed) 
/* switch from binary to NMEA at specified baud */
{
    /*@ +charint @*/
   unsigned char msg[] = {0xa0, 0xa2, 0x00, 0x18,
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
   /*@ -charint @*/

   msg[26] = (unsigned char)HI(speed);
   msg[27] = (unsigned char)LO(speed);
   return (sirf_write(ttyfd, msg));
}

#define getb(off)	buf[off]
#define getw(off)	((getb(off) << 8) | getb(off+1))
#define getl(off)	((int)((getw(off) << 16) | (getw(off+2) & 0xffff)))

static void sirfbin_mode(struct gps_device_t *session, int mode)
{
    if (mode == 0) {
	(void)gpsd_switch_driver(session, "SiRF-II NMEA");
	(void)sirf_to_nmea(session->gpsdata.gps_fd,session->gpsdata.baudrate);
	session->gpsdata.driver_mode = 0;
    }
}

int sirf_parse(struct gps_device_t *session, unsigned char *buf, int len)
{
    int	st, i, j, cn, navtype, mask;
    char buf2[MAX_PACKET_LENGTH*3+2];
    double fv;
    /*@ +charint @*/
    unsigned char enablesubframe[] = {0xa0, 0xa2, 0x00, 0x19,
				 0x80, 0x00, 0x00, 0x00,
				 0x00, 0x00, 0x00, 0x00,
				 0x00, 0x00, 0x00, 0x00,
				 0x00, 0x00, 0x00, 0x00,
				 0x00, 0x00, 0x00, 0x00,
				 0x00, 0x00, 0x00, 0x0C,
				 0x10,
				 0x00, 0x00, 0xb0, 0xb3};
    unsigned char disablesubframe[] = {0xa0, 0xa2, 0x00, 0x19,
				  0x80, 0x00, 0x00, 0x00,
				  0x00, 0x00, 0x00, 0x00,
				  0x00, 0x00, 0x00, 0x00,
				  0x00, 0x00, 0x00, 0x00,
				  0x00, 0x00, 0x00, 0x00,
				  0x00, 0x00, 0x00, 0x0C,
				  0x00,
				  0x00, 0x00, 0xb0, 0xb3};

    /*@ -charint @*/
    if (len < 0)
	return 0;

    buf2[0] = '\0';
    for (i = 0; i < len; i++)
	(void)snprintf(buf2+strlen(buf2), 
		       sizeof(buf2)-strlen(buf2),
		       "%02x", (unsigned int)buf[i]);
    strcat(buf2, "\n");
    buf += 4;
    len -= 8;
    gpsd_report(5, "Raw SiRF packet type 0x%02x length %d: %s\n", buf[0],len,buf2);
    (void)snprintf(session->gpsdata.tag, sizeof(session->gpsdata.tag),
		   "MID%d",(int)buf[0]);

    switch (buf[0])
    {
    case 0x02:		/* Measure Navigation Data Out */
	mask = 0;
	session->gpsdata.satellites_used = (int)getb(28);
	memset(session->gpsdata.used,0,sizeof(session->gpsdata.used));
	for (i = 0; i < MAXCHANNELS; i++)
	    session->gpsdata.used[i] = (int)getb(29+i);
	if ((session->driverstate & (SIRF_GE_232 | UBLOX))==0) {
	    /* position/velocity is bytes 1-18 */
	    ecef_to_wgs84fix(&session->gpsdata.fix, 
			     (double)getl(1), (double)getl(5), (double)getl(9),
			     (int)getw(13)/8.0, (int)getw(15)/8.0, (int)getw(17)/8.0);
	    /* WGS 84 geodesy parameters */
	    /* fix status is byte 19 */
	    navtype = (int)getb(19);
	    session->gpsdata.status = STATUS_NO_FIX;
	    session->gpsdata.fix.mode = MODE_NO_FIX;
	    if ((navtype & 0x80) != 0)
		session->gpsdata.status = STATUS_DGPS_FIX;
	    else if ((navtype & 0x07) > 0 && (navtype & 0x07) < 7)
		session->gpsdata.status = STATUS_FIX;
	    if ((navtype & 0x07) == 4 || (navtype & 0x07) == 6)
		session->gpsdata.fix.mode = MODE_3D;
	    else if (session->gpsdata.status != 0)
		session->gpsdata.fix.mode = MODE_2D;
	    if (session->gpsdata.fix.mode == MODE_3D)
		mask |= ALTITUDE_SET;
	    gpsd_report(4, "MND 0x02: Navtype = 0x%0x, Status = %d, mode = %d\n", 
			navtype,session->gpsdata.status,session->gpsdata.fix.mode);
	    /* byte 20 is HDOP, see below */
	    /* byte 21 is "mode 2", not clear how to interpret that */ 
	    session->gpsdata.fix.time = session->gpsdata.sentence_time
		= gpstime_to_unix((int)getw(22), (int)getl(24)*1e-2) - session->context->leap_seconds;
#ifdef NTPSHM_ENABLE
	    if (session->gpsdata.fix.mode > MODE_NO_FIX) {
		if ((session->time_seen & TIME_SEEN_GPS_2) == 0)
		    gpsd_report(4, "valid time in message 0x02, seen=0x%02x\n",
				session->time_seen);
		session->time_seen |= TIME_SEEN_GPS_2;
		if (IS_HIGHEST_BIT(session->time_seen,TIME_SEEN_GPS_2))
		    (void)ntpshm_put(session, session->gpsdata.fix.time + 0.8);
	    }
#endif /* NTPSHM_ENABLE */

	    gpsd_binary_fix_dump(session, buf2);
	    /* fix quality data */
	    session->gpsdata.hdop = (int)getb(20)/5.0;
	    session->gpsdata.pdop = session->gpsdata.vdop = 0.0;
	    if (session->gpsdata.satellites > 0)
		dop(session->gpsdata.satellites_used, &session->gpsdata);
	    gpsd_binary_quality_dump(session, buf2 + strlen(buf2));
	    gpsd_report(3, "<= GPS: %s", buf2);
	    mask |= TIME_SET | LATLON_SET | TRACK_SET | SPEED_SET | STATUS_SET | MODE_SET | HDOP_SET;
	}
	return mask;

    case 0x04:		/* Measured tracker data out */
	gpsd_zero_satellites(&session->gpsdata);
	session->gpsdata.sentence_time
	    = gpstime_to_unix((int)getw(1), (int)getl(3)*1e-2) - session->context->leap_seconds;
	for (i = st = 0; i < MAXCHANNELS; i++) {
	    int good, off = 8 + 15 * i;
	    session->gpsdata.PRN[st]       = (int)getb(off);
	    session->gpsdata.azimuth[st]   = (int)(((int)getb(off+1)*3)/2.0);
	    session->gpsdata.elevation[st] = (int)((int)getb(off+2)/2.0);
	    cn = 0;
	    for (j = 0; j < 10; j++)
		cn += (int)getb(off+5+j);
	    session->gpsdata.ss[st] = cn/10;
	    good = session->gpsdata.PRN[st]!=0 && 
		session->gpsdata.azimuth[st]!=0 && 
		session->gpsdata.elevation[st]!=0;
#ifdef __UNUSED__
	    gpsd_report(4, "PRN=%2d El=%3.2f Az=%3.2f ss=%3d stat=%04x %c\n",
			getb(off), 
			getb(off+2)/2.0, 
			(getb(off+1)*3)/2.0,
			cn/10, 
			getw(off+3),
			good ? '*' : ' ');
#endif /* UNUSED */
	    if (good!=0)
		st += 1;
	}
	session->gpsdata.satellites = st;
#ifdef NTPSHM_ENABLE
	if (st > 3) {
	    if ((session->time_seen & TIME_SEEN_GPS_1)==0)
		gpsd_report(4, "valid time in message 0x04, seen=0x%02x\n",
			    session->time_seen);
	    session->time_seen |= TIME_SEEN_GPS_1;
	    if (IS_HIGHEST_BIT(session->time_seen,TIME_SEEN_GPS_1))
		(void)ntpshm_put(session,session->gpsdata.sentence_time+0.8);
	}
#endif /* NTPSHM_ENABLE */
	/*
	 * The freaking brain-dead SiRF chip doesn't obey its own
	 * rate-control command for 04, at least at firmware rev. 231, 
	 * so we have to do our own rate-limiting here...
	 */
	if ((session->satcounter++ % 5) != 0)
	    break;
	gpsd_binary_satellite_dump(session, buf2);
	gpsd_report(4, "MTD 0x04: %d satellites\n", st);
	gpsd_report(3, "<= GPS: %s", buf2);
	return TIME_SET | SATELLITE_SET;

    case 0x05:		/* Raw Tracker Data Out */
	return 0;

    case 0x06:		/* Software Version String */
	gpsd_report(4, "FV  0x06: Firmware version: %s\n", buf+1);
	fv = atof((char *)(buf+1));
	if (fv < 231.0) {
	    session->driverstate |= SIRF_LT_231;
	    if (fv > 200.0)
		sirfbin_mode(session, 0);
	} else if (fv < 232.0) 
	    session->driverstate |= SIRF_EQ_231;
	else {
	    /*@ +charint @*/
	    unsigned char enablemid52[] = {
		0xa0, 0xa2, 0x00, 0x08, 
		0xa6, 0x00, 0x34, 0x01, 0x00, 0x00, 0x00, 0x00,
		0x00, 0xdb, 0xb0, 0xb3};
	    /*@ -charint @*/
	    gpsd_report(4, "Enabling PPS message...\n");
	    (void)sirf_write(session->gpsdata.gps_fd, enablemid52);
	    session->driverstate |= SIRF_GE_232;
	    session->context->valid |= LEAP_SECOND_VALID;
	}
	if (strstr((char *)(buf+1), "ES"))
	    gpsd_report(4, "Firmware has XTrac capability\n");
	gpsd_report(4, "Driver state flags are: %0x\n", session->driverstate);
	session->time_seen = 0;
	if ((session->context->valid & LEAP_SECOND_VALID)==0) {
	    gpsd_report(4, "Enabling subframe transmission...\n");
	    (void)sirf_write(session->gpsdata.gps_fd, enablesubframe);
	}
	return 0;

    case 0x08:
	/*
	 * Heavy black magic begins here!
	 *
	 * A description of how to decode these bits is at
	 * <http://home-2.worldonline.nl/~samsvl/nav2eu.htm>
	 *
	 * We're after subframe 4 page 18 word 9, the leap year correction.
	 *
	 * Chris Kuethe says:
	 * "Message 8 is generated as the data is received. It is not
	 * buffered on the chip. So when you enable message 8, you'll
	 * get one subframe every 6 seconds.  Of the data received, the
	 * almanac and ephemeris are buffered and stored, so you can
	 * query them at will. Alas, the time parameters are not
	 * stored, which is really lame, as the UTC-GPS correction
	 * changes 1 second every few years. Maybe."
	 */
        {
	    unsigned int pageid, subframe, leap, words[10];
	    unsigned int chan = (unsigned int)getb(1);
	    unsigned int svid = (unsigned int)getb(2);
	    words[0] = (unsigned int)getl(3);
	    words[1] = (unsigned int)getl(7);
	    words[2] = (unsigned int)getl(11);
	    words[3] = (unsigned int)getl(15);
	    words[4] = (unsigned int)getl(19);
	    words[5] = (unsigned int)getl(23);
	    words[6] = (unsigned int)getl(27);
	    words[7] = (unsigned int)getl(31);
	    words[8] = (unsigned int)getl(35);
	    words[9] = (unsigned int)getl(39);
	    gpsd_report(4, "50B (raw): CH=%d, SV=%d %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x\n", 
	    	chan, svid, 
	    	words[0], words[1], words[2], words[3], words[4], 
	    	words[5], words[6], words[7], words[8], words[9]);
	    /*
	     * Mask off the high 2 bits and shift out the 6 parity bits.
	     * Once we've filtered, we can ignore the TEL and HOW words.
	     * We don't need to check parity here, the SiRF chipset does
	     * that and throws a subframe error if the parity is wrong.
	     */
	    for (i = 0; i < 10; i++)
		words[i] = (words[i]  & 0x3fffffff) >> 6;
	    /*
	     * "First, throw away everything that doesn't start with 8b or
	     * 74. more correctly the first byte should be 10001011. If
	     * it's 01110100, then you have a subframe with inverted
	     * polarity and each byte needs to be xored against 0xff to
	     * remove the inversion."
	     */
	    words[0] &= 0xff0000;
	    if (words[0] != 0x8b0000 && words[0] != 0x740000)
	    	break;
	    if (words[0] == 0x740000)
		for (i = 1; i < 10; i++)
		    words[i] ^= 0xffffff;
	    /*
	     * The subframe ID is in the Hand Over Word (page 80) 
	     */
	    subframe = ((words[1] >> 2) & 0x07);
	    /* we're not interested in anything but subframe 4 */
	    if (subframe != 4)
		break;
	    /*
	     * Pages 66-76a,80 of ICD-GPS-200 are the subframe structures.
	     * Subframe 4 page 18 is on page 74.
	     * See page 105 for the mapping between magic SVIDs and pages.
	     */
	    pageid = (words[2] & 0x3F0000) >> 16;
	    gpsd_report(2, "Subframe 4 SVID is %d\n", pageid);
	    if (pageid == 56) {	/* magic SVID for page 18 */
		/* once we've filtered, we can ignore the TEL and HOW words */
		gpsd_report(2, "50B: CH=%d, SV=%d SF=%d %06x %06x %06x %06x %06x %06x %06x %06x\n", 
			    chan, svid, subframe,
				words[2], words[3], words[4], words[5], 
				words[6], words[7], words[8], words[9]);
		leap = (words[8] & 0xff0000) >> 16;
		/*
		 * There appears to be some bizarre bug that randomly causes
		 * this field to come out two's-complemented.  Work around
		 * this.  At the current expected rate of issuing leap-seconds
		 * this kluge won't bite until about 2070, by which time SiRF
		 * had better have fixed their damn firmware...
		 *
		 * Carl: ...I am unsure, and suggest you
		 * experiment.  The D30 bit is in bit 30 of the 32-bit
		 * word (next to MSB), and should signal an inverted
		 * value when it is one coming over the air.  But if
		 * the bit is set and the word decodes right without
		 * inversion, then we properly caught it.  Cases where
		 * you see subframe 6 rather than 1 means we should
		 * have done the inversion but we did not.  Some other
		 * things you can watch for: in any subframe, the
		 * second word (HOW word) should have last 2 parity
		 * bits 00 -- there are bits within the rest of the
		 * word that are set as required to ensure that.  The
		 * same goes for word 10.  That means that both words
		 * 1 and 3 (the words that immediately follow words 10
		 * and 2, respectively) should always be uninverted.
		 * In these cases, the D29 and D30 from the previous
		 * words, found in the two MSBs of the word, should
		 * show 00 -- if they don't then you may find an
		 * unintended inversion due to noise on the data link.
		 */
		if (leap > 128)
		    leap ^= 0xff;
		gpsd_report(2, "leap-seconds is %d\n", leap);
		session->context->leap_seconds = (int)leap;
		session->context->valid = LEAP_SECOND_VALID;
	    }

	    if ((session->context->valid & LEAP_SECOND_VALID) != 0) {
		gpsd_report(4, "Disabling subframe transmission...\n");
		(void)sirf_write(session->gpsdata.gps_fd, disablesubframe);
	    }
	}
	break;
    case 0x09:		/* CPU Throughput */
	gpsd_report(4, 
		    "THR 0x09: SegStatMax=%.3f, SegStatLat=%3.f, AveTrkTime=%.3f, Last MS=%3.f\n", 
		    (float)getw(1)/186, (float)getw(3)/186, 
		    (float)getw(5)/186, getw(7));
    	return 0;

    case 0x0a:		/* Error ID Data */
	switch (getw(1))
	{
	case 2:
	    gpsd_report(4, "EID 0x0a type 2: Subframe %d error on PRN %ld\n", getl(9), getl(5));
	    break;

	case 4107:
	    gpsd_report(4, "EID 0x0a type 4107: neither KF nor LSQ fix.\n", getl(5));
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
	if ((session->driverstate & SIRF_GE_232) != 0) {
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
	     * packet in 231, we used to assume that only the altitude field
	     * from this packet is valid.  But even this doesn't necessarily
	     * seem to be the case.  Instead, we do our own computation 
	     * of geoid separation now.
	     */
	    navtype = (int)getw(3);
	    session->gpsdata.status = STATUS_NO_FIX;
	    session->gpsdata.fix.mode = MODE_NO_FIX;
	    if ((navtype & 0x80) != 0)
		session->gpsdata.status = STATUS_DGPS_FIX;
	    else if ((navtype & 0x07) > 0 && (navtype & 0x07) < 7)
		session->gpsdata.status = STATUS_FIX;
	    session->gpsdata.fix.mode = MODE_NO_FIX;
	    if ((navtype & 0x07) == 4 || (navtype & 0x07) == 6)
		session->gpsdata.fix.mode = MODE_3D;
	    else if (session->gpsdata.status != 0)
		session->gpsdata.fix.mode = MODE_2D;
	    gpsd_report(4, "GNI 0x29: Navtype = 0x%0x, Status = %d, mode = %d\n", 
			navtype, session->gpsdata.status, session->gpsdata.fix.mode);
	    /*
	     * UTC is left all zeros in 231 and older firmware versions, 
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
	    session->gpsdata.nmea_date.tm_year = (int)getw(11);
	    session->gpsdata.nmea_date.tm_mon = (int)getb(13)-1;
	    session->gpsdata.nmea_date.tm_mday = (int)getb(14);
	    session->gpsdata.nmea_date.tm_hour = (int)getb(15);
	    session->gpsdata.nmea_date.tm_min = (int)getb(16);
	    session->gpsdata.nmea_date.tm_sec = 0;
	    session->gpsdata.subseconds = (int)getw(17)*1e-3;
	    session->gpsdata.fix.time = session->gpsdata.sentence_time
		= mktime(&session->gpsdata.nmea_date)+session->gpsdata.subseconds;
	    gpsd_report(5, "MID 41 UTC: %lf\n", session->gpsdata.fix.time);
#ifdef NTPSHM_ENABLE
	    if (session->gpsdata.fix.mode > MODE_NO_FIX && session->gpsdata.nmea_date.tm_year != 0) {
		if ((session->time_seen & TIME_SEEN_UTC_1) == 0)
		    gpsd_report(4, "valid time in message 0x29, seen=0x%02x\n",
				session->time_seen);
		session->time_seen |= TIME_SEEN_UTC_1;
		if (IS_HIGHEST_BIT(session->time_seen,TIME_SEEN_UTC_1))
		    (void)ntpshm_put(session, session->gpsdata.fix.time + 0.8);
	    }
#endif /* NTPSHM_ENABLE */
	    /* skip 4 bytes of satellite map */
	    session->gpsdata.fix.latitude = getl(23)*1e-7;
	    session->gpsdata.fix.longitude = getl(27)*1e-7;
	    /* skip 4 bytes of altitude from ellipsoid */
	    mask = TIME_SET | LATLON_SET | STATUS_SET | MODE_SET;
	    session->gpsdata.fix.altitude = getl(31)*1e-2;
	    /* skip 1 byte of map datum */
	    session->gpsdata.fix.speed = (int)getw(36)*1e-2;
	    session->gpsdata.fix.track = (int)getw(38)*1e-2;
	    /* skip 2 bytes of magnetic variation */
	    session->gpsdata.fix.climb = (int)getw(42)*1e-2;
	    /* HDOP should be available at byte 89, but in 231 it's zero. */
	    gpsd_binary_fix_dump(session, buf2);
	    gpsd_report(3, "<= GPS: %s", buf2);
	    mask |= SPEED_SET | TRACK_SET | CLIMB_SET; 
	    session->gpsdata.sentence_length = 91;
	    strcpy(session->gpsdata.tag, "GND");
	}
	return mask;

    case 0x32:		/* SBAS corrections */
	return 0;

    case 0x34:		/* PPS Time */
	/*
	 * Carl Carter from SiRF writes: "We do not output on the
	 * second (unless you are using message ID 52).  We make
	 * measurements in the receiver in time with an internal
	 * counter that is not slaved to GPS time, so the measurements
	 * are made at a time that wanders around the second.  Then,
	 * after the measurements are made (all normalized to the same
	 * point in time) we dispatch the navigation software to make
	 * a solution, and that solution comes out some 200 to 300 ms
	 * after the measurement time.  So you may get a message at
	 * 700 ms after the second that uses measurements time tagged
	 * 450 ms after the second.  And if some other task jumps up
	 * and delays things, that message may not come out until 900
	 * ms after the second.  Things can get out of sync to the
	 * point that if you try to resolve the GPS time of our 1 PPS
	 * pulses using the navigation messages, you will find it
	 * impossible to be consistent.  That is why I added message
	 * ID 52 to our system -- it is tied to the creation of the 1
	 * PPS and always comes out right around the top of the
	 * second."
	 */
	mask = 0;
	gpsd_report(4, "PPS 0x34: Status = 0x%02x\n", getb(14));
	if (((int)getb(14) & 0x07) == 0x07) {	/* valid UTC time? */
	    session->gpsdata.nmea_date.tm_hour = (int)getb(1);
	    session->gpsdata.nmea_date.tm_min = (int)getb(2);
	    session->gpsdata.nmea_date.tm_sec = (int)getb(3);
	    session->gpsdata.nmea_date.tm_mday = (int)getb(4);
	    session->gpsdata.nmea_date.tm_mon = (int)getb(5) - 1;
	    session->gpsdata.nmea_date.tm_year = (int)getw(6) - 1900;
	    session->context->leap_seconds = (int)getw(8);
	    session->context->valid = LEAP_SECOND_VALID;
#ifdef NTPSHM_ENABLE
	    if ((session->time_seen & TIME_SEEN_UTC_2) == 0)
		gpsd_report(4, "valid time in message 0x34, seen=0x%02x\n",
				session->time_seen);
	    session->time_seen |= TIME_SEEN_UTC_2;
	    if (IS_HIGHEST_BIT(session->time_seen,TIME_SEEN_UTC_2))
		(void)ntpshm_put(session, session->gpsdata.fix.time + 0.3);
#endif /* NTPSHM_ENABLE */
	    mask |= TIME_SET;
	}
	return mask;

    case 0x62:		/* uBlox Extended Measured Navigation Data */
	/* this packet is only sent by uBlox firmware from version 1.32 */
	mask =	LATLON_SET | ALTITUDE_SET | SPEED_SET | TRACK_SET | CLIMB_SET |
		STATUS_SET | MODE_SET | HDOP_SET | VDOP_SET | PDOP_SET;
	session->gpsdata.fix.latitude = getl(1) * RAD_2_DEG * 1e-8; 
	session->gpsdata.fix.longitude = getl(5) * RAD_2_DEG * 1e-8;
	session->gpsdata.fix.separation = wgs84_separation(session->gpsdata.fix.latitude, session->gpsdata.fix.longitude);
	session->gpsdata.fix.altitude = getl(9) * 1e-3 - session->gpsdata.fix.separation;
	session->gpsdata.fix.speed = getl(13) * 1e-3;
	session->gpsdata.fix.climb = getl(17) * 1e-3;
	session->gpsdata.fix.track = getl(21) * RAD_2_DEG * 1e-8;

	navtype = (int)getb(25);
	session->gpsdata.status = STATUS_NO_FIX;
	session->gpsdata.fix.mode = MODE_NO_FIX;
	if ((navtype & 0x80) != 0)
	    session->gpsdata.status = STATUS_DGPS_FIX;
	else if ((navtype & 0x07) > 0 && (navtype & 0x07) < 7)
	    session->gpsdata.status = STATUS_FIX;
	if ((navtype & 0x07) == 4 || (navtype & 0x07) == 6)
	    session->gpsdata.fix.mode = MODE_3D;
	else if (session->gpsdata.status != 0)
	    session->gpsdata.fix.mode = MODE_2D;
	gpsd_report(4, "EMND 0x62: Navtype = 0x%0x, Status = %d, mode = %d\n", 
		    navtype, session->gpsdata.status, session->gpsdata.fix.mode);

	if ((navtype & 0x40) != 0) {		/* UTC corrected timestamp? */
	    mask |= TIME_SET;
	    session->gpsdata.nmea_date.tm_year = (int)getw(26) - 1900;
	    session->gpsdata.nmea_date.tm_mon = (int)getb(28) - 1;
	    session->gpsdata.nmea_date.tm_mday = (int)getb(29);
	    session->gpsdata.nmea_date.tm_hour = (int)getb(30);
	    session->gpsdata.nmea_date.tm_min = (int)getb(31);
	    session->gpsdata.nmea_date.tm_sec = 0;
	    session->gpsdata.subseconds = ((unsigned short)getw(32))*1e-3;
	    session->gpsdata.fix.time = session->gpsdata.sentence_time
		= mkgmtime(&session->gpsdata.nmea_date)+session->gpsdata.subseconds;
#ifdef NTPSHM_ENABLE
	    if ((session->time_seen & TIME_SEEN_UTC_2) == 0)
		gpsd_report(4, "valid time in message 0x62, seen=0x%02x\n",
				session->time_seen);
	    session->time_seen |= TIME_SEEN_UTC_2;
	    if (IS_HIGHEST_BIT(session->time_seen,TIME_SEEN_UTC_2))
		(void)ntpshm_put(session, session->gpsdata.fix.time + 0.8);
#endif /* NTPSHM_ENABLE */
	    session->context->valid = LEAP_SECOND_VALID;
	}

	gpsd_binary_fix_dump(session, buf2);
	session->gpsdata.gdop = (int)getb(34) / 5.0;
	session->gpsdata.pdop = (int)getb(35) / 5.0;
	session->gpsdata.hdop = (int)getb(36) / 5.0;
	session->gpsdata.vdop = (int)getb(37) / 5.0;
	session->gpsdata.tdop = (int)getb(38) / 5.0;
	gpsd_binary_quality_dump(session, buf2 + strlen(buf2));
	session->driverstate |= UBLOX;
	return mask;

    case 0xff:		/* Debug messages */
	buf2[0] = '\0';
	for (i = 1; i < len; i++)
	    if (isprint(buf[i]))
		(void)snprintf(buf2+strlen(buf2), 
			       sizeof(buf2)-strlen(buf2),
			       "%c", buf[i]);
	    else
		(void)snprintf(buf2+strlen(buf2), 
			       sizeof(buf2)-strlen(buf2),
			       "\\x%02x", (unsigned int)buf[i]);
	gpsd_report(4, "DD  0xff: %s\n", buf2);
	return 0;

    default:
	buf2[0] = '\0';
	for (i = 0; i < len; i++)
	    (void)snprintf(buf2+strlen(buf2), 
			   sizeof(buf2)-strlen(buf2),
			   "%02x", (unsigned int)buf[i]);
	gpsd_report(3, "Unknown SiRF packet id %d length %d: %s\n", buf[0], len, buf2);
	return 0;
    }

    return 0;
}

static int sirfbin_parse_input(struct gps_device_t *session)
{
    int st;

    if (session->packet_type == SIRF_PACKET){
	st = sirf_parse(session, session->outbuffer, session->outbuflen);
	session->gpsdata.driver_mode = 1;
	return st;
    } else if (session->packet_type == NMEA_PACKET) {
	st = nmea_parse(session->outbuffer, &session->gpsdata);
	session->gpsdata.driver_mode = 0;
	return st;
    } else
	return 0;
}

static void sirfbin_initializer(struct gps_device_t *session)
/* poll for software version in order to check for old firmware */
{
    if (session->packet_type == NMEA_PACKET) {
	gpsd_report(1, "Switching chip mode to SiRF binary.\n");
	(void)nmea_send(session->gpsdata.gps_fd, 
			"$PSRF100,0,%d,8,1,0", session->gpsdata.baudrate);
    }
    /* do this every time*/
    {
	/*@ +charint @*/
	unsigned char dgpscontrol[] = {0xa0, 0xa2, 0x00, 0x07,
				 0x85, 0x01, 0x00, 0x00,
				 0x00, 0x00, 0x00,
				 0x00, 0x00, 0xb0, 0xb3};
	unsigned char sbasparams[] = {0xa0, 0xa2, 0x00, 0x06,
				 0xaa, 0x00, 0x01, 0x00,
				 0x00, 0x00,
				 0x00, 0x00, 0xb0, 0xb3};
	unsigned char versionprobe[] = {0xa0, 0xa2, 0x00, 0x02,
				 0x84, 0x00,
				 0x00, 0x00, 0xb0, 0xb3};
	unsigned char modecontrol[] = {0xa0, 0xa2, 0x00, 0x0e,
				  0x88, 
				  0x00, 0x00,	/* pad bytes */
				  0x00,		/* degraded mode off */
				  0x00, 0x00,	/* pad bytes */
				  0x00, 0x00,	/* altitude */
				  0x00,		/* altitude hold auto */
				  0x00,		/* use last computed alt */
				  0x00,		/* reserved */
				  0x00,		/* disable degraded mode */
				  0x00,		/* disable dead reckoning */
				  0x01,		/* enable track smoothing */
				 0x00, 0x00, 0xb0, 0xb3};
	/*@ -charint @*/
	gpsd_report(4, "Setting DGPS control to use SBAS...\n");
	(void)sirf_write(session->gpsdata.gps_fd, dgpscontrol);
	gpsd_report(4, "Setting SBAS to auto/integrity mode...\n");
	(void)sirf_write(session->gpsdata.gps_fd, sbasparams);
	gpsd_report(4, "Probing for firmware version...\n");
	(void)sirf_write(session->gpsdata.gps_fd, versionprobe);
	gpsd_report(4, "Setting mode...\n");
	(void)sirf_write(session->gpsdata.gps_fd, modecontrol);
    }
}

static int sirfbin_speed(struct gps_device_t *session, unsigned int speed)
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
