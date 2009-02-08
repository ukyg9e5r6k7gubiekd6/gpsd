/* $Id$ */
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <unistd.h>
#include <time.h>
#include <stdio.h>

#include "gpsd_config.h"
#include "gpsd.h"

#if defined(SUPERSTAR2_ENABLE) && defined(BINARY_ENABLE)
#include "bits.h"
#include "superstar2.h"

/*
 * These routines are specific to this driver
 */

static	gps_mask_t superstar2_parse_input(struct gps_device_t *);
static	gps_mask_t superstar2_dispatch(struct gps_device_t *,
				       unsigned char *, size_t );
static	gps_mask_t superstar2_msg_ack(struct gps_device_t *,
				      unsigned char *, size_t );
static	gps_mask_t superstar2_msg_navsol_lla(struct gps_device_t *,
					     unsigned char *, size_t );
static	gps_mask_t superstar2_msg_navsol_ecef(struct gps_device_t *,
					      unsigned char *, size_t );
static	gps_mask_t superstar2_msg_timing(struct gps_device_t *,
					  unsigned char *, size_t );
static	gps_mask_t superstar2_msg_svinfo(struct gps_device_t *,
					 unsigned char *, size_t );

/*
 * These methods may be called elsewhere in gpsd
 */
static	ssize_t superstar2_write(struct gps_device_t *, char *, size_t );
static	void superstar2_probe_wakeup(struct gps_device_t *);
static	void superstar2_configurator(struct gps_device_t *, unsigned int );
static	bool superstar2_set_speed(struct gps_device_t *, speed_t );
static	void superstar2_set_mode(struct gps_device_t *, int );
static	void superstar2_probe_wakeup(struct gps_device_t *);
static	void superstar2_probe_subtype(struct gps_device_t *, unsigned int );

/*
 * Decode the message ACK message
 */
static gps_mask_t
superstar2_msg_ack(struct gps_device_t *session UNUSED,
			  unsigned char *buf, size_t data_len)
{
    if (data_len == 11)
	gpsd_report(LOG_PROG,
		    "superstar2 #126 - "
		    "ACK 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x\n",
		    buf[5], buf[6], buf[7], buf[8], buf[9]);
    return ONLINE_SET; /* always returns ONLINE_SET, but avoid runt packets */
}

/*
 * Decode the navigation solution message
 */
static gps_mask_t
superstar2_msg_navsol_lla(struct gps_device_t *session,
			  unsigned char *buf, size_t data_len)
{
    gps_mask_t mask;
    unsigned char flags;
    union int_float i_f;
    union long_double l_d;
    double d;
    struct tm tm;

    if (data_len != 77)
	return 0;

    gpsd_report(LOG_PROG, "superstar2 #20 - user navigation data\n");
    mask = ONLINE_SET;

    flags = getub(buf, 72);
    if ((flags & 0x0f) != 3) /* mode 3 is navigation */
	return mask;

    /* extract time data */
    bzero(&tm, sizeof(tm));
    tm.tm_hour = getub(buf, 4) & 0x1f;
    tm.tm_min = getub(buf, 5);
    d = getled(buf, 6);
    tm.tm_sec = (int)d;
    tm.tm_mday = getub(buf, 14);
    tm.tm_mon = getub(buf, 15) - 1;
    tm.tm_year = getleuw(buf, 16) - 1900;
    session->gpsdata.fix.time = session->gpsdata.sentence_time =
	timegm(&tm) + (d - tm.tm_sec);
    mask |= TIME_SET;

    /* extract the local tangential plane (ENU) solution */
    session->gpsdata.fix.latitude = getled(buf,18) * RAD_2_DEG;
    session->gpsdata.fix.longitude = getled(buf,26) * RAD_2_DEG;
    session->gpsdata.fix.altitude = getlef(buf,34);
    session->gpsdata.fix.speed = getlef(buf,38);
    session->gpsdata.fix.track = getlef(buf,42) * RAD_2_DEG;
    session->gpsdata.fix.climb = getlef(buf,54);
    mask |= LATLON_SET | ALTITUDE_SET | SPEED_SET | TRACK_SET | CLIMB_SET  ;

    session->gpsdata.satellites_used = getub(buf,71) & 0x0f;
    session->gpsdata.hdop = getleuw(buf,66) * 0.1;
    session->gpsdata.vdop = getleuw(buf,68) * 0.1;
    /* other DOP if available */
    mask |= HDOP_SET | VDOP_SET | USED_SET;

    flags = getub(buf,70);
    switch (flags & 0x1f) {
	    case 2:
		session->gpsdata.fix.mode = MODE_3D;
		session->gpsdata.status = STATUS_FIX;
		break;
	    case 4:
		session->gpsdata.fix.mode = MODE_3D;
		session->gpsdata.status = STATUS_DGPS_FIX;
		break;
	    case 5:
		session->gpsdata.fix.mode = MODE_2D;
		session->gpsdata.status = STATUS_DGPS_FIX;
		break;
	    case 3:
	    case 6:
		session->gpsdata.fix.mode = MODE_2D;
		session->gpsdata.status = STATUS_FIX;
		break;
	    default:
		session->gpsdata.status = STATUS_NO_FIX;
		session->gpsdata.fix.mode = MODE_NO_FIX;
    }

    /* CYCLE_START_SET if this message starts a reporting period */
    mask |= MODE_SET | STATUS_SET | CYCLE_START_SET ;

    return mask;
}

static gps_mask_t
superstar2_msg_navsol_ecef(struct gps_device_t *session,
			   unsigned char *buf, size_t data_len)
{
    gps_mask_t mask;
    unsigned char flags;
    union int_float i_f;
    union long_double l_d;
    double tm, tow;

    if (data_len != 85)
	return 0;

    gpsd_report(LOG_PROG, "superstar2 #21 - ecef navigation data\n");
    mask = ONLINE_SET;

    flags = getub(buf, 79) & 0x1f;
    if ((flags < 2) || (flags > 5))
	return mask;

    /* extract time data */
    tow = getled(buf, 4);
    session->driver.superstar2.gps_week = getleuw(buf, 12);
    tm = gpstime_to_unix((int)session->driver.superstar2.gps_week, tow) -
	session->context->leap_seconds;
    session->gpsdata.fix.time = session->gpsdata.sentence_time = tm;
    mask |= TIME_SET;

    /* extract the earth-centered, earth-fixed (ECEF) solution */
    ecef_to_wgs84fix(&session->gpsdata,
	 getled(buf, 14), getled(buf, 22), getled(buf, 30),
	 getlef(buf, 38), getlef(buf, 42), getlef(buf, 46));
    mask |= LATLON_SET | ALTITUDE_SET | SPEED_SET | TRACK_SET | CLIMB_SET ;

    session->gpsdata.satellites_used = getub(buf, 79) & 0x0f;
    session->gpsdata.hdop = getleuw(buf, 74) * 0.1;
    session->gpsdata.vdop = getleuw(buf, 76) * 0.1;
    /* other DOP if available */
    mask |= HDOP_SET | VDOP_SET | USED_SET;

    flags = getub(buf,70);
    switch (flags & 0x1f) {
	    case 2:
		session->gpsdata.fix.mode = MODE_3D;
		session->gpsdata.status = STATUS_FIX;
		break;
	    case 4:
		session->gpsdata.fix.mode = MODE_3D;
		session->gpsdata.status = STATUS_DGPS_FIX;
		break;
	    case 5:
		session->gpsdata.fix.mode = MODE_2D;
		session->gpsdata.status = STATUS_DGPS_FIX;
		break;
	    case 3:
	    case 6:
		session->gpsdata.fix.mode = MODE_2D;
		session->gpsdata.status = STATUS_FIX;
		break;
	    default:
		session->gpsdata.status = STATUS_NO_FIX;
		session->gpsdata.fix.mode = MODE_NO_FIX;
    }

    /* CYCLE_START_SET if this message starts a reporting period */
    mask |= MODE_SET | STATUS_SET | CYCLE_START_SET;

    return mask;
}

/**
 * GPS Satellite Info
 */
static gps_mask_t
superstar2_msg_svinfo(struct gps_device_t *session,
		      unsigned char *buf, size_t data_len)
{
    unsigned char i, st, nchan, nsv;

    if (data_len != 67)
	return 0;

    gpsd_report(LOG_PROG, "superstar2 #33 - satellite data");

    nchan = 12;
    gpsd_zero_satellites(&session->gpsdata);
    nsv = 0; /* number of actually used satellites */
    for (i = st = 0; i < nchan; i++) {
	/* get info for one channel/satellite */
	int off = i*5 + 5;
	int porn;
	if ((porn = getub(buf, off) & 0x1f) == 0)
	    porn = (getub(buf, off+3) >> 1) + 87;

	session->gpsdata.PRN[i]		= porn;
	session->gpsdata.ss[i]		= getub(buf, off+4);
	session->gpsdata.elevation[i]	= getsb(buf, off+1);
	session->gpsdata.azimuth[i]	= (unsigned short)getub(buf, off+2) + ((unsigned short)(getub(buf, off+3) & 0x1) << 1);

	if ((getub(buf, off) & 0x60) == 0x60)
	    session->gpsdata.used[nsv++] = session->gpsdata.PRN[i];

	if(session->gpsdata.PRN[i])
	    st++;
    }
    session->gpsdata.satellites_used = nsv;
    session->gpsdata.satellites = st;
    return SATELLITE_SET | USED_SET | ONLINE_SET;
}

static gps_mask_t
superstar2_msg_version(struct gps_device_t *session,
		       unsigned char *buf, size_t data_len)
{
#define SZ 16
    char main_sw[SZ], hw_part[SZ], boot_sw[SZ], ser_num[SZ];

    /* byte 98 is device type, value = 3 means superstar2 */
    if ((data_len != 101) || ((getub(buf,98) & 0x0f) != 3))
	return 0;

    snprintf(main_sw, 15, "%s", buf+4);
    snprintf(hw_part, 15, "%s", buf+18);
    snprintf(boot_sw, 15, "%s", buf+36);
    snprintf(ser_num, 14, "%s", buf+73);

    gpsd_report(LOG_PROG,
	"superstar2 #45 - "
	"hw part %s boot sw %s main sw %s ser num %s\n",
	hw_part, boot_sw, main_sw, ser_num);
    strlcpy(session->subtype, main_sw, sizeof(session->subtype));
    return DEVICEID_SET | ONLINE_SET;
}

/**
 * GPS Leap Seconds
 */
static gps_mask_t
superstar2_msg_timing(struct gps_device_t *session, unsigned char *buf, size_t data_len)
{
    union long_double l_d;
    double d;
    struct tm tm;

    if (data_len != 65)
	return 0;

    gpsd_report(LOG_PROG, "superstar2 #113 - timing status\n");
    if ((getub(buf, 55) & 0x30) != 0)
	return ONLINE_SET;

    /* extract time data */
    bzero(&tm, sizeof(tm));
    tm.tm_mday = getsb(buf, 37);
    tm.tm_mon = getsb(buf, 38) - 1;
    tm.tm_year = getlesw(buf, 39) - 1900;

    tm.tm_hour = getsb(buf, 41);
    tm.tm_min = getsb(buf, 42);
    d = getled(buf, 43);
    tm.tm_sec = (int)d;
    session->gpsdata.sentence_time = session->gpsdata.fix.time = timegm(&tm);
    session->context->leap_seconds = getsb(buf,20);

    return TIME_SET | ONLINE_SET;
}


/**
 * Write data to the device, doing any required padding or checksumming
 */
static ssize_t
superstar2_write(struct gps_device_t *session, char *msg, size_t msglen)
{
   unsigned short c = 0;
   size_t i;

   for (i = 0; i < msglen - 2; i++)
	c += (unsigned char)msg[i];
//   c = htons(c); // XXX is this needed on big-endian machines?
   memcpy(msg + msg[3] + 4, &c, 2);
   gpsd_report(LOG_IO, "writing superstar2 control type %02x len %zu:%s\n",
	       (unsigned char)msg[1], msglen,
	       gpsd_hexdump_wrapper(msg, msglen, LOG_IO));
   return gpsd_write(session, msg, msglen);
}

/**
 * Parse the data from the device
 */
gps_mask_t
superstar2_dispatch(struct gps_device_t *session, unsigned char *buf,
		    size_t len)
{
    int type;

    if (len == 0)
	return 0;

    type = buf[SUPERSTAR2_TYPE_OFFSET];
    (void)snprintf(session->gpsdata.tag,
	sizeof(session->gpsdata.tag), "SS2-%u", (int)type);

    switch (type)
    {
    case SUPERSTAR2_ACK: /* Message Acknowledgement */
	return superstar2_msg_ack(session, buf, len);
    case SUPERSTAR2_SVINFO: /* Satellite Visibility Data */
	return superstar2_msg_svinfo(session, buf, len);
    case SUPERSTAR2_NAVSOL_LLA: /* Navigation Data */
	return superstar2_msg_navsol_lla(session, buf, len);
    case SUPERSTAR2_NAVSOL_ECEF: /* Navigation Data */
	return superstar2_msg_navsol_ecef(session, buf, len);
    case SUPERSTAR2_VERSION: /* Hardware/Software Version */
	return superstar2_msg_version(session, buf, len);
    case SUPERSTAR2_TIMING: /* Timing Parameters */
	return superstar2_msg_timing(session, buf, len);

    default:
	/* XXX This gets noisy in a hurry. */
	gpsd_report(LOG_WARN,
	    "unknown superstar2 packet id 0x%02x length %zd: %s\n",
	    type, len, gpsd_hexdump_wrapper(buf, len, LOG_WARN));
	return 0;
    }
}

/**********************************************************
 *
 * Externally called routines below here
 *
 **********************************************************/
/* canned config messages */
/* Initiate Link ID# 63 */
static unsigned char link_msg[] = {0x01, 0x3f, 0xc0, 0x08,
	    0x55, 0x47, 0x50, 0x53, 0x2d, 0x30, 0x30, 0x30,
	    0x00, 0x00};

/* Request Hardware/Software Identification ID# 45 */
static unsigned char version_msg[] = {0x01, 0x2d, 0xd2, 0x00, 0x00, 0x01};

static void
superstar2_probe_wakeup(struct gps_device_t *session)
{
    superstar2_write(session, link_msg, sizeof(link_msg));
    usleep(300000);
    superstar2_write(session, version_msg, sizeof(version_msg));
    return;
}

static void
superstar2_probe_subtype(struct gps_device_t *session,
				     unsigned int seq)
{
    if (seq == 0){
	superstar2_write(session, link_msg, sizeof(link_msg));
	usleep(300000);
	superstar2_write(session, version_msg, sizeof(version_msg));
    }
    return;
}

static void superstar2_configurator(struct gps_device_t *session,
				    unsigned int seq UNUSED)
{
    unsigned char a;
    unsigned char message_list[] = {
	SUPERSTAR2_NAVSOL_LLA,
	SUPERSTAR2_SVINFO,
	SUPERSTAR2_TIMING,
	SUPERSTAR2_NAVSOL_ECEF,
	SUPERSTAR2_DUMMY};
    unsigned char message2_list[] = {
	SUPERSTAR2_MEASUREMENT,
	SUPERSTAR2_DUMMY};
    unsigned char tmpl_msg[] = { 0x01, 0x00, 0x00, 0x00, 0x00, 0x00};
    unsigned char tmpl2_msg[] = { 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00};

    for(a = 0; message_list[a] != 0; a++){
	/* set high bit to enable continuous output */
	tmpl_msg[1] = (unsigned char)(message_list[a] | 0x80);
	tmpl_msg[2] = (unsigned char)(tmpl_msg[1] ^ 0xff);
	superstar2_write(session, tmpl_msg, sizeof(tmpl_msg));
	usleep(20000);
    }
    for(a = 0; message2_list[a] != 0; a++){
	/* set high bit to enable continuous output */
	tmpl2_msg[1] = (unsigned char)(message2_list[a] | 0x80);
	tmpl2_msg[2] = (unsigned char)(tmpl2_msg[1] ^ 0xff);
	superstar2_write(session, tmpl2_msg, sizeof(tmpl2_msg));
	usleep(20000);
    }
    superstar2_write(session, version_msg, sizeof(version_msg));
}

/*
 * This is the entry point to the driver. When the packet sniffer recognizes
 * a packet for this driver, it calls this method which passes the packet to
 * the binary processor or the nmea processor, depending on the session type.
 */
static gps_mask_t superstar2_parse_input(struct gps_device_t *session)
{
    gps_mask_t st;

    if (session->packet.type == SUPERSTAR2_PACKET){
	st = superstar2_dispatch(session, session->packet.outbuffer,
				 session->packet.length);
	session->gpsdata.driver_mode = MODE_BINARY;
	return st;
#ifdef NMEA_ENABLE
    } else if (session->packet.type == NMEA_PACKET) {
	st = nmea_parse((char *)session->packet.outbuffer, session);
	(void)gpsd_switch_driver(session, "Generic NMEA");
	session->gpsdata.driver_mode = MODE_NMEA;
	return st;
#endif /* NMEA_ENABLE */
    } else
	return 0;
}

static bool superstar2_set_speed(struct gps_device_t *session, speed_t speed)
{
    /* set port operating mode, speed, bits etc. here */
    return 0;
}

/*
 * Switch between NMEA and binary mode, if supported
 */
static void superstar2_set_mode(struct gps_device_t *session, int mode)
{
    if (mode == MODE_NMEA) {
	// superstar2_to_nmea(session->gpsdata.gps_fd,session->gpsdata.baudrate); /* send the mode switch control string */
    } else {
	session->back_to_nmea = false;
    }
}

struct gps_type_t superstar2_binary = {
    /* Full name of type */
    .type_name		= "SuperStarII binary",
    /* associated lexer packet type */
    .packet_type    = SUPERSTAR2_PACKET,
    /* Response string that identifies device (not active) */
    .trigger		= NULL,
    /* Number of satellite channels supported by the device */
    .channels	 	= 12,
    /* Control string sender - should provide checksum and trailer */
    .control_send	= superstar2_write,
    /* Startup-time device detector */
    .probe_detect	= NULL,
    /* Wakeup to be done before each baud hunt */
    .probe_wakeup	= superstar2_probe_wakeup,
    /* Initialize the device and get subtype */
    .probe_subtype	= superstar2_probe_subtype,
#ifdef ALLOW_RECONFIGURE
    /* Enable what reports we need */
    .configurator	= superstar2_configurator,
#endif /* ALLOW_RECONFIGURE */
    /* Packet getter (using default routine) */
    .get_packet		= generic_get,
    /* Parse message packets */
    .parse_packet	= superstar2_parse_input,
    /* RTCM handler (using default routine) */
    .rtcm_writer	= pass_rtcm,
    /* Speed (baudrate) switch */
    .speed_switcher	= superstar2_set_speed,
    /* Switch to NMEA mode */
    .mode_switcher	= superstar2_set_mode,
    /* Message delivery rate switcher (not active) */
    .rate_switcher	= NULL,
    /* Number of chars per report cycle (not active) */
    .cycle_chars	= -1,
#ifdef ALLOW_RECONFIGURE
    /* Undo the actions of .configurator */
    .revert		= NULL,
#endif /* ALLOW_RECONFIGURE */
    /* Puts device back to original settings */
    .wrapup		= NULL,
    /* Number of updates per second */
    .cycle		= 1
};
#endif /* defined(SUPERSTAR2_ENABLE) && defined(BINARY_ENABLE) */
