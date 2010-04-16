/*
 * This file is Copyright (c) 2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 */
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#ifndef S_SPLINT_S
#include <unistd.h>
#endif /* S_SPLINT_S */
#include <time.h>
#include <stdio.h>

#include "gpsd.h"

#if defined(SUPERSTAR2_ENABLE) && defined(BINARY_ENABLE)
#include "bits.h"
#include "driver_superstar2.h"

/*
 * These routines are specific to this driver
 */

static gps_mask_t superstar2_parse_input(struct gps_device_t *);
static gps_mask_t superstar2_dispatch(struct gps_device_t *,
				      unsigned char *, size_t);
static gps_mask_t superstar2_msg_ack(struct gps_device_t *,
				     unsigned char *, size_t);
static gps_mask_t superstar2_msg_navsol_lla(struct gps_device_t *,
					    unsigned char *, size_t);
#ifdef __UNUSED__
static gps_mask_t superstar2_msg_navsol_ecef(struct gps_device_t *,
					     unsigned char *, size_t);
#endif /* __UNUSED__ */
static gps_mask_t superstar2_msg_timing(struct gps_device_t *,
					unsigned char *, size_t);
static gps_mask_t superstar2_msg_svinfo(struct gps_device_t *,
					unsigned char *, size_t);
static gps_mask_t superstar2_msg_iono_utc(struct gps_device_t *,
					  unsigned char *, size_t);
static gps_mask_t superstar2_msg_ephemeris(struct gps_device_t *,
					   unsigned char *, size_t);

/*
 * These methods may be called elsewhere in gpsd
 */
static ssize_t superstar2_control_send(struct gps_device_t *, char *, size_t);
static void superstar2_event_hook(struct gps_device_t *, event_t);
static bool superstar2_set_speed(struct gps_device_t *, speed_t, char, int);
static void superstar2_set_mode(struct gps_device_t *, int);
static ssize_t superstar2_write(struct gps_device_t *, char *, size_t);


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
		    "ACK %d %d %d %d %d\n",
		    buf[5], buf[6], buf[7], buf[8], buf[9]);
    return 0;
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
    mask = 0;

    /*@ +charint @*/
    flags = (unsigned char)getub(buf, 72);
    if ((flags & 0x0f) != 0x03)	/* mode 3 is navigation */
	return mask;
    /*@ -charint @*/

    /* extract time data */
    (void)memset(&tm, '\0', sizeof(tm));
    tm.tm_hour = (int)getub(buf, 4) & 0x1f;
    tm.tm_min = (int)getub(buf, 5);
    d = getled(buf, 6);
    tm.tm_sec = (int)d;
    tm.tm_mday = (int)getub(buf, 14);
    tm.tm_mon = (int)getub(buf, 15) - 1;
    tm.tm_year = (int)getleuw(buf, 16) - 1900;
    session->newdata.time = timegm(&tm) + (d - tm.tm_sec);
    mask |= TIME_IS;

    /* extract the local tangential plane (ENU) solution */
    session->newdata.latitude = getled(buf, 18) * RAD_2_DEG;
    session->newdata.longitude = getled(buf, 26) * RAD_2_DEG;
    session->newdata.altitude = getlef(buf, 34);
    session->newdata.speed = getlef(buf, 38);
    session->newdata.track = getlef(buf, 42) * RAD_2_DEG;
    session->newdata.climb = getlef(buf, 54);
    mask |= LATLON_IS | ALTITUDE_IS | SPEED_IS | TRACK_IS | CLIMB_IS;

    session->gpsdata.satellites_used = (int)getub(buf, 71) & 0x0f;
    clear_dop(&session->gpsdata.dop);
    /*@i3@*/ session->gpsdata.dop.hdop = getleuw(buf, 66) * 0.1;
    /*@i3@*/ session->gpsdata.dop.vdop = getleuw(buf, 68) * 0.1;
    /* other DOP if available */
    mask |= DOP_IS | USED_IS;

    flags = (unsigned char)getub(buf, 70);
    switch (flags & 0x1f) {
    case 2:
	session->newdata.mode = MODE_3D;
	session->gpsdata.status = STATUS_FIX;
	break;
    case 4:
	session->newdata.mode = MODE_3D;
	session->gpsdata.status = STATUS_DGPS_FIX;
	break;
    case 5:
	session->newdata.mode = MODE_2D;
	session->gpsdata.status = STATUS_DGPS_FIX;
	break;
    case 3:
    case 6:
	session->newdata.mode = MODE_2D;
	session->gpsdata.status = STATUS_FIX;
	break;
    default:
	session->gpsdata.status = STATUS_NO_FIX;
	session->newdata.mode = MODE_NO_FIX;
    }

    mask |= MODE_IS | STATUS_IS;
    gpsd_report(LOG_DATA,
		"NAVSOL_LLA: time=%.2f lat=%.2f lon=%.2f alt=%.2f track=%.2f speed=%.2f climb=%.2f mode=%d status=%d hdop=%.2f hdop=%.2f used=%d mask=%s\n",
		session->newdata.time,
		session->newdata.latitude,
		session->newdata.longitude,
		session->newdata.altitude,
		session->newdata.track,
		session->newdata.speed,
		session->newdata.climb,
		session->newdata.mode,
		session->gpsdata.status,
		session->gpsdata.dop.hdop,
		session->gpsdata.dop.vdop,
		session->gpsdata.satellites_used, gpsd_maskdump(mask));
    return mask;
}

#ifdef __UNUSED__
/*
 * This is duplicative with navsol_lla. By suppressing evaluation of it,
 * we gain the desirable feature that the fix update is atomic and 
 * exactly once per cycle.
 */

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
    mask = 0;

    /*@ +charint @*/
    flags = getub(buf, 79) & 0x1f;
    if ((flags < 2) || (flags > 5))
	return mask;
    /*@ -charint @*/

    /* extract time data */
    tow = getled(buf, 4);
    session->context->gps_week = getleuw(buf, 12);
    tm = gpstime_to_unix((int)session->context->gps_week, tow) -
	session->context->leap_seconds;
    session->newdata.time = tm;
    mask |= TIME_IS;

    /* extract the earth-centered, earth-fixed (ECEF) solution */
    /*@ -evalorder @*/
    ecef_to_wgs84fix(&session->newdata, &session->separation,
		     getled(buf, 14), getled(buf, 22), getled(buf, 30),
		     getlef(buf, 38), getlef(buf, 42), getlef(buf, 46));
    /*@ +evalorder @*/
    mask |= LATLON_IS | ALTITUDE_IS | SPEED_IS | TRACK_IS | CLIMB_IS;

    session->gpsdata.satellites_used = (int)getub(buf, 79) & 0x0f;
    clear_dop(&session->gpsdata.dop);
    session->gpsdata.dop.hdop = getleuw(buf, 74) * 0.1;
    session->gpsdata.dop.vdop = getleuw(buf, 76) * 0.1;
    /* other DOP if available */
    mask |= DOP_IS | USED_IS;

    flags = getub(buf, 70);
    switch (flags & 0x1f) {
    case 2:
	session->newdata.mode = MODE_3D;
	session->gpsdata.status = STATUS_FIX;
	break;
    case 4:
	session->newdata.mode = MODE_3D;
	session->gpsdata.status = STATUS_DGPS_FIX;
	break;
    case 5:
	session->newdata.mode = MODE_2D;
	session->gpsdata.status = STATUS_DGPS_FIX;
	break;
    case 3:
    case 6:
	session->newdata.mode = MODE_2D;
	session->gpsdata.status = STATUS_FIX;
	break;
    default:
	session->gpsdata.status = STATUS_NO_FIX;
	session->newdata.mode = MODE_NO_FIX;
    }

    mask |= MODE_IS | STATUS_IS;
    gpsd_report(LOG_DATA,
		"NAVSOL_LLA: time=%.2f lat=%.2f lon=%.2f alt=%.2f track=%.2f speed=%.2f climb=%.2f mode=%d status=%d hdop=%.2f vdop=%.2f used=%d mask=%s\n",
		session->newdata.time,
		session->newdata.latitude,
		session->newdata.longitude,
		session->newdata.altitude,
		session->newdata.track,
		session->newdata.speed,
		session->newdata.climb,
		session->newdata.mode,
		session->gpsdata.status,
		session->gpsdata.dop.hdop,
		session->gpsdata.dop.vdop,
		session->gpsdata.satellites_used, gpsd_maskdump(mask));
    return mask;
}
#endif /* __UNUSED__ */

/**
 * GPS Satellite Info
 */
static gps_mask_t
superstar2_msg_svinfo(struct gps_device_t *session,
		      unsigned char *buf, size_t data_len)
{
    int i, st, nchan, nsv;

    if (data_len != 67)
	return 0;

    gpsd_report(LOG_PROG, "superstar2 #33 - satellite data");

    nchan = 12;
    gpsd_zero_satellites(&session->gpsdata);
    nsv = 0;			/* number of actually used satellites */
    for (i = st = 0; i < nchan; i++) {
	/* get info for one channel/satellite */
	int off = i * 5 + 5;
	unsigned int porn;
	if ((porn = (unsigned int)getub(buf, off) & 0x1f) == 0)
	    porn = (unsigned int)(getub(buf, off + 3) >> 1) + 87;

	session->gpsdata.PRN[i] = (int)porn;
	session->gpsdata.ss[i] = (float)getub(buf, off + 4);
	session->gpsdata.elevation[i] = (int)getsb(buf, off + 1);
	session->gpsdata.azimuth[i] = (unsigned short)getub(buf, off + 2) +
	    ((unsigned short)(getub(buf, off + 3) & 0x1) << 1);

	/*@ +charint @*/
	if ((getub(buf, off) & 0x60) == 0x60)
	    session->gpsdata.used[nsv++] = session->gpsdata.PRN[i];
	/*@ -charint @*/

	if (session->gpsdata.PRN[i])
	    st++;
    }
    session->gpsdata.skyview_time = NAN;
    session->gpsdata.satellites_used = nsv;
    session->gpsdata.satellites_visible = st;
    gpsd_report(LOG_DATA,
		"SVINFO: visible=%d used=%d mask={SATELLITE|USED}\n",
		session->gpsdata.satellites_visible,
		session->gpsdata.satellites_used);
    return SATELLITE_IS | USED_IS;
}

static gps_mask_t
superstar2_msg_version(struct gps_device_t *session,
		       unsigned char *buf, size_t data_len)
{
#define SZ 16
    char main_sw[SZ], hw_part[SZ], boot_sw[SZ], ser_num[SZ];

    /*@ +charint @*/
    /* byte 98 is device type, value = 3 means superstar2 */
    if ((data_len != 101) || ((getub(buf, 98) & 0x0f) != 3))
	return 0;
    /*@ -charint @*/

    (void)snprintf(main_sw, 15, "%s", (char *)buf + 4);
    (void)snprintf(hw_part, 15, "%s", (char *)buf + 18);
    (void)snprintf(boot_sw, 15, "%s", (char *)buf + 36);
    (void)snprintf(ser_num, 14, "%s", (char *)buf + 73);

    gpsd_report(LOG_PROG,
		"superstar2 #45 - "
		"hw part %s boot sw %s main sw %s ser num %s\n",
		hw_part, boot_sw, main_sw, ser_num);
    (void)strlcpy(session->subtype, main_sw, sizeof(session->subtype));
    gpsd_report(LOG_DATA, "VERSION: subtype='%s' mask={DEVEICEID}\n",
		session->subtype);
    return DEVICEID_IS;
}

/**
 * GPS Leap Seconds
 */
static gps_mask_t
superstar2_msg_timing(struct gps_device_t *session, unsigned char *buf,
		      size_t data_len)
{
    gps_mask_t mask;
    union long_double l_d;
    double d;
    struct tm tm;

    if (data_len != 65)
	return 0;

    gpsd_report(LOG_PROG, "superstar2 #113 - timing status\n");
    /*@ +charint @*/
    if ((getub(buf, 55) & 0x30) != 0)
	mask = 0;
    /*@ -charint @*/
    else {
	/* extract time data */
	(void)memset(&tm, '\0', sizeof(tm));
	tm.tm_mday = (int)getsb(buf, 37);
	tm.tm_mon = (int)getsb(buf, 38) - 1;
	tm.tm_year = (int)getlesw(buf, 39) - 1900;

	tm.tm_hour = (int)getsb(buf, 41);
	tm.tm_min = (int)getsb(buf, 42);
	d = getled(buf, 43);
	tm.tm_sec = (int)d;
	session->newdata.time = timegm(&tm);
	session->context->leap_seconds = (int)getsb(buf, 20);
	mask = TIME_IS;
    }
    gpsd_report(LOG_DATA, "TIMING: time=%.2f mask={TIME}\n",
		session->newdata.time);
    return mask;
}

/**
 * Raw Measurements
 */
static gps_mask_t
superstar2_msg_measurement(struct gps_device_t *session, unsigned char *buf,
			   size_t data_len UNUSED)
{
    gps_mask_t mask = 0;
#ifdef RAW_ENABLE
    int i, n;
    unsigned long ul;
    double t;
    union long_double l_d;

    gpsd_report(LOG_PROG, "superstar2 #23 - measurement block\n");

    n = (int)getub(buf, 6);	/* number of measurements */
    t = getled(buf, 7);		/* measurement time */
    for (i = 0; i < n; i++) {
	session->gpsdata.raw.mtime[i] = t;
	session->gpsdata.PRN[i] = (int)getub(buf, 11 * i + 15) & 0x1f;
	session->gpsdata.ss[i] = (double)getub(buf, 11 * i * 15 + 1) / 4.0;
	session->gpsdata.raw.codephase[i] =
	    (double)getleul(buf, 11 * i * 15 + 2);
	ul = (unsigned long)getleul(buf, 11 * i * 15 + 6);

	session->gpsdata.raw.satstat[i] = (unsigned int)(ul & 0x03L);
	session->gpsdata.raw.carrierphase[i] = (double)((ul >> 2) & 0x03ffL);
	session->gpsdata.raw.pseudorange[i] = (double)(ul >> 12);
    }

    mask |= RAW_IS;
#endif /* RAW_ENABLE */
    return mask;
}

/* request for ionospheric and utc time data #75 */
/*@ +charint @*/
static unsigned char iono_utc_msg[] = { 0x01, 0x4b, 0xb4, 0x00, 0x00, 0x01 };

/*@ -charint @*/


/**
 * Ionospheric/UTC parameters
 */
static gps_mask_t
superstar2_msg_iono_utc(struct gps_device_t *session, unsigned char *buf,
			size_t data_len UNUSED)
{
    unsigned int i, u;

    i = (unsigned int)getub(buf, 12);
    u = (unsigned int)getub(buf, 21);
    gpsd_report(LOG_PROG,
		"superstar2 #75 - ionospheric & utc data: iono %s utc %s\n",
		i ? "ok" : "bad", u ? "ok" : "bad");
    session->driver.superstar2.last_iono = time(NULL);

    return 0;
}


/**
 * Ephemeris
 */
static gps_mask_t
superstar2_msg_ephemeris(struct gps_device_t *session, unsigned char *buf,
			 size_t data_len UNUSED)
{
    unsigned int prn;
    prn = (unsigned int)(getub(buf, 4) & 0x1f);
    gpsd_report(LOG_PROG, "superstar2 #22 - ephemeris data - prn %u\n", prn);

    /* ephemeris data updates fairly slowly, but when it does, poll UTC */
    if ((time(NULL) - session->driver.superstar2.last_iono) > 60)
	(void)superstar2_write(session, (char *)iono_utc_msg,
			       sizeof(iono_utc_msg));

    return ONLINE_IS;
}


static ssize_t
superstar2_write(struct gps_device_t *session, char *msg, size_t msglen)
{
    unsigned short c = 0;
    ssize_t i;

    for (i = 0; i < (ssize_t) (msglen - 2); i++)
	c += (unsigned short)msg[i];
    c += 0x100;
    // c = htons(c); // XXX is this needed on big-endian machines?
    (void)memcpy(msg + (int)msg[3] + 4, &c, 2);
    gpsd_report(LOG_IO, "writing superstar2 control type %d len %zu:%s\n",
		(int)msg[1] & 0x7f, msglen,
		gpsd_hexdump_wrapper(msg, msglen, LOG_IO));
    return (i = gpsd_write(session, msg, msglen));
}

/**
 * Parse the data from the device
 */
gps_mask_t
superstar2_dispatch(struct gps_device_t * session, unsigned char *buf,
		    size_t len)
{
    int type;

    if (len == 0)
	return 0;

    type = (int)buf[SUPERSTAR2_TYPE_OFFSET];
    (void)snprintf(session->gpsdata.tag,
		   sizeof(session->gpsdata.tag), "SS2-%d", type);

    session->cycle_end_reliable = true;

    switch (type) {
    case SUPERSTAR2_ACK:	/* Message Acknowledgement */
	return superstar2_msg_ack(session, buf, len);
    case SUPERSTAR2_SVINFO:	/* Satellite Visibility Data */
	return superstar2_msg_svinfo(session, buf, len);
    case SUPERSTAR2_NAVSOL_LLA:	/* Navigation Data */
	return superstar2_msg_navsol_lla(session, buf,
					 len) | (CLEAR_IS | REPORT_IS);
#ifdef __UNUSED__
    case SUPERSTAR2_NAVSOL_ECEF:	/* Navigation Data */
	return superstar2_msg_navsol_ecef(session, buf, len);
#endif /* __UNUSED__ */
    case SUPERSTAR2_VERSION:	/* Hardware/Software Version */
	return superstar2_msg_version(session, buf, len);
    case SUPERSTAR2_TIMING:	/* Timing Parameters */
	return superstar2_msg_timing(session, buf, len);
    case SUPERSTAR2_MEASUREMENT:	/* Timing Parameters */
	return superstar2_msg_measurement(session, buf, len);
    case SUPERSTAR2_IONO_UTC:
	return superstar2_msg_iono_utc(session, buf, len);
    case SUPERSTAR2_EPHEMERIS:
	return superstar2_msg_ephemeris(session, buf, len);

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
/*@ +charint @*/
/* canned config messages */
/* Initiate Link ID# 63 */
static unsigned char link_msg[] = { 0x01, 0x3f, 0xc0, 0x08,
    0x55, 0x47, 0x50, 0x53, 0x2d, 0x30, 0x30, 0x30,
    0x00, 0x00
};

/* Request Hardware/Software Identification ID# 45 */
static unsigned char version_msg[] = { 0x01, 0x2d, 0xd2, 0x00, 0x00, 0x01 };

/*@ -charint @*/

static void superstar2_event_hook(struct gps_device_t *session, event_t event)
{
    if (event == event_wakeup) {
	(void)superstar2_write(session, (char *)link_msg, sizeof(link_msg));
	(void)usleep(320000);
	(void)superstar2_write(session, (char *)version_msg,
			       sizeof(version_msg));
	return;
    }

    /* query firmware version */
    if (event == event_identified)
	(void)superstar2_write(session, (char *)version_msg,
			       sizeof(version_msg));

    /* FIXME: check to see if this really needs to be resent on reactivation */
    if (event == event_identified || event == event_reactivate) {
	/*@ +charint @*/
	unsigned char svinfo_msg[] = { 0x01, 0xa1, 0x5e, 0x00, 0x00, 0x01 };
	unsigned char timing_msg[] = { 0x01, 0xf1, 0x0e, 0x00, 0x00, 0x01 };
	unsigned char navsol_lla_msg[] =
	    { 0x01, 0x94, 0x6b, 0x00, 0x00, 0x01 };
	unsigned char ephemeris_msg[] =
	    { 0x01, 0x96, 0x69, 0x00, 0x00, 0x01 };
	unsigned char measurement_msg[] =
	    { 0x01, 0x97, 0x68, 0x01, 0x00, 0x01, 0x01 };
	/*@ -charint @*/

	(void)superstar2_write(session, (char *)timing_msg,
			       sizeof(timing_msg));
	(void)superstar2_write(session, (char *)measurement_msg,
			       sizeof(measurement_msg));
	(void)superstar2_write(session, (char *)svinfo_msg,
			       sizeof(svinfo_msg));
	(void)superstar2_write(session, (char *)navsol_lla_msg,
			       sizeof(navsol_lla_msg));
	(void)superstar2_write(session, (char *)version_msg,
			       sizeof(version_msg));
	(void)superstar2_write(session, (char *)ephemeris_msg,
			       sizeof(ephemeris_msg));
	(void)superstar2_write(session, (char *)iono_utc_msg,
			       sizeof(iono_utc_msg));
	session->driver.superstar2.last_iono = time(NULL);
    }
}

/*
 * This is the entry point to the driver. When the packet sniffer recognizes
 * a packet for this driver, it calls this method which passes the packet to
 * the binary processor or the nmea processor, depending on the session type.
 */
static gps_mask_t superstar2_parse_input(struct gps_device_t *session)
{
    gps_mask_t st;

    if (session->packet.type == SUPERSTAR2_PACKET) {
	st = superstar2_dispatch(session, session->packet.outbuffer,
				 session->packet.length);
	session->gpsdata.dev.driver_mode = MODE_BINARY;
	return st;
#ifdef NMEA_ENABLE
    } else if (session->packet.type == NMEA_PACKET) {
	st = nmea_parse((char *)session->packet.outbuffer, session);
	(void)gpsd_switch_driver(session, "Generic NMEA");
	session->gpsdata.dev.driver_mode = MODE_NMEA;
	return st;
#endif /* NMEA_ENABLE */
    } else
	return 0;
}

#ifdef ALLOW_CONTROLSEND
static ssize_t
superstar2_control_send(struct gps_device_t *session, char *msg,
			size_t msglen)
{
    /*@ +charint -mayaliasunique @*/
    session->msgbuf[0] = 0x1;	/* SOH */
    session->msgbuf[1] = msg[0];
    session->msgbuf[2] = msg[0] ^ 0xff;
    session->msgbuf[3] = (char)(msglen + 1);
    (void)memcpy(session->msgbuf + 4, msg + 1, msglen - 1);
    session->msgbuflen = (size_t) (msglen + 5);
    /*@ -charint +mayaliasunique @*/
    return superstar2_write(session, session->msgbuf, session->msgbuflen);
}
#endif /* ALLOW_CONTROLSEND */

#ifdef ALLOW_RECONFIGURE
static bool superstar2_set_speed(struct gps_device_t *session,
				 speed_t speed, char parity, int stopbits)
{
    /* parity and stopbit switching aren't available on this chip */
    if (parity != session->gpsdata.dev.parity
	|| stopbits != (int)session->gpsdata.dev.stopbits) {
	return false;
    } else {
	/*@ +charint @*/
	unsigned char speed_msg[] =
	    { 0x01, 0x48, 0xB7, 0x01, 0x00, 0x00, 0x00 };

	/* high bit 0 in the mode word means set NMEA mode */
	speed_msg[4] = (unsigned char)(speed / 300);
	/*@ -charint @*/
	return (superstar2_write(session, (char *)speed_msg, 7) == 7);
    }
}
#endif /* ALLOW_RECONFIGURE */

static void superstar2_set_mode(struct gps_device_t *session, int mode)
{
    if (mode == MODE_NMEA) {
	/*@ +charint @*/
	unsigned char mode_msg[] =
	    { 0x01, 0x48, 0xB7, 0x01, 0x00, 0x00, 0x00 };

	/* high bit 0 in the mode word means set NMEA mode */
	mode_msg[4] = (unsigned char)(session->gpsdata.dev.baudrate / 300);
	(void)superstar2_write(session, (char *)mode_msg, 7);
	/*@ -charint @*/
    } else {
	session->back_to_nmea = false;
    }
}

/* *INDENT-OFF* */
const struct gps_type_t superstar2_binary = {
    /* Full name of type */
    .type_name		= "SuperStarII binary",
    /* Associated lexer packet type */
    .packet_type        = SUPERSTAR2_PACKET,
    /* Response string that identifies device (not active) */
    .trigger		= NULL,
    /* Number of satellite channels supported by the device */
    .channels	 	= 12,
    /* Startup-time device detector */
    .probe_detect	= NULL,
    /* Packet getter (using default routine) */
    .get_packet		= generic_get,
    /* Parse message packets */
    .parse_packet	= superstar2_parse_input,
    /* RTCM handler (using default routine) */
    .rtcm_writer	= pass_rtcm,
    /* Fire on various lifetime events */
    .event_hook		= superstar2_event_hook,
#ifdef ALLOW_RECONFIGURE
    /* Speed (baudrate) switch */
    .speed_switcher	= superstar2_set_speed,
    /* Switch to NMEA mode */
    .mode_switcher	= superstar2_set_mode,
    /* Message delivery rate switcher (not active) */
    .rate_switcher	= NULL,
    /* Minimum cycle time (not used) */
    .min_cycle	        = 1,
#endif /* ALLOW_RECONFIGURE */
#ifdef ALLOW_CONTROLSEND
    /* Control string sender - should provide checksum and trailer */
    .control_send	= superstar2_control_send,
#endif /* ALLOW_CONTROLSEND */
#ifdef NTPSHM_ENABLE
    .ntp_offset     = NULL,		/* no method for NTP fudge factor */
#endif /* NTPSHM_ ENABLE */
};
/* *INDENT-ON* */
#endif /* defined(SUPERSTAR2_ENABLE) && defined(BINARY_ENABLE) */
