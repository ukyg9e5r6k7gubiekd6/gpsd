/* $Id$ */
/*
 * Handle the Trimble TSIP packet format
 * by Rob Janssen, PE1CHL.
 */
#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#ifndef S_SPLINT_S
#include <unistd.h>
#endif /* S_SPLINT_S */
#include <math.h>
#include "gpsd_config.h"
#include "gpsd.h"
#include "bits.h"

#define USE_SUPERPACKET	1			/* use Super Packet mode? */

#define SEMI_2_DEG	(180.0 / 2147483647)	/* 2^-31 semicircle to deg */

#ifdef TSIP_ENABLE
#define TSIP_CHANNELS	12

static int tsip_write(struct gps_device_t *session, 
		      unsigned int id, /*@null@*/unsigned char *buf, size_t len)
{
    char *ep, *cp;

    gpsd_report(LOG_IO, "Sent TSIP packet id 0x%02x: %s\n", id,
	gpsd_hexdump_wrapper(buf, len, LOG_IO));

    /*@ +charint @*/
    session->msgbuf[0] = '\x10';
    session->msgbuf[1] = (char)id;
    ep = session->msgbuf + 2;
     /*@ -nullderef @*/
    for (cp = (char *)buf; len-- > 0; cp++) {
	if (*cp == '\x10')
	    *ep++ = '\x10';
	*ep++ = *cp;
    }
    /*@ +nullderef @*/
    *ep++ = '\x10';
    *ep++ = '\x03';
    session->msgbuflen = (size_t)(ep - session->msgbuf);
    /*@ -charint @*/
    if (gpsd_write(session,session->msgbuf,session->msgbuflen) !=
	(ssize_t)session->msgbuflen)
	return -1;

    return 0;
}

static gps_mask_t tsip_analyze(struct gps_device_t *session)
{
    int i, j, len, count;
    gps_mask_t mask = 0;
    unsigned int id;
    u_int8_t u1,u2,u3,u4,u5;
    int16_t s1,s2,s3,s4;
    int32_t sl1,sl2,sl3;
    u_int32_t ul1,ul2;
    float f1,f2,f3,f4,f5;
    double d1,d2,d3,d4,d5;
    union int_float i_f;
    union long_double l_d;
    time_t now;
    unsigned char buf[BUFSIZ];
    char buf2[BUFSIZ];

    if (session->packet.type != TSIP_PACKET) {
	gpsd_report(LOG_INF, "tsip_analyze packet type %d\n",session->packet.type);
	return 0;
    }

    /*@ +charint @*/
    if (session->packet.outbuflen < 4 || session->packet.outbuffer[0] != 0x10)
	return 0;

    /* remove DLE stuffing and put data part of message in buf */

    memset(buf, 0, sizeof(buf));
    buf2[len = 0] = '\0';
    for (i = 2; i < (int)session->packet.outbuflen; i++) {
	if (session->packet.outbuffer[i] == 0x10)
	    if (session->packet.outbuffer[++i] == 0x03)
		break;

	(void)snprintf(buf2+strlen(buf2),
		      sizeof(buf2)-strlen(buf2),
		      "%02x", buf[len++] = session->packet.outbuffer[i]);
    }
    /*@ -charint @*/

    (void)snprintf(session->gpsdata.tag, sizeof(session->gpsdata.tag),
		   "ID%02x", id = (unsigned)session->packet.outbuffer[1]);

    gpsd_report(LOG_IO, "TSIP packet id 0x%02x length %d: %s\n",id,len,buf2);
    (void)time(&now);

    switch (id) {
    case 0x13:		/* Packet Received */
	u1 = getub(buf,0);
	u2 = getub(buf,1);
	gpsd_report(LOG_WARN, "Received packet of type %02x cannot be parsed\n",u1);
#if USE_SUPERPACKET
	if ((int)u1 == 0x8e && (int)u2 == 0x23) { /* no Compact Super Packet */
	    gpsd_report(LOG_WARN, "No Compact Super Packet, use LFwEI\n");

	    /* Request LFwEI Super Packet */
	    putbyte(buf,0,0x20);
	    putbyte(buf,1,0x01);		/* enabled */
	    (void)tsip_write(session, 0x8e, buf, 2);
	}
#endif /* USE_SUPERPACKET */
	break;
    case 0x41:		/* GPS Time */
	if (len != 10)
	    break;
	session->driver.tsip.last_41 = now;			/* keep timestamp for request */
	f1 = getbef(buf,0);			/* gpstime */
	s1 = getbesw(buf,4);			/* week */
	f2 = getbef(buf,6);			/* leap seconds */
	if (f1 >= 0.0 && f2 > 10.0) {
	    session->driver.tsip.gps_week = s1;
	    session->context->leap_seconds = (int)round(f2);
	    session->context->valid |= LEAP_SECOND_VALID;

	    session->gpsdata.sentence_time = gpstime_to_unix((int)s1, f1) - f2;

#ifdef NTPSHM_ENABLE
	    if (session->context->enable_ntpshm)
		(void)ntpshm_put(session,session->gpsdata.sentence_time+0.075);
#endif
	    mask |= TIME_SET;
	}
	gpsd_report(LOG_INF, "GPS Time %f %d %f\n",f1,s1,f2);
	break;
    case 0x42:		/* Single-Precision Position Fix, XYZ ECEF */
	if (len != 16)
	    break;
	f1 = getbef(buf,0);			/* X */
	f2 = getbef(buf,4);			/* Y */
	f3 = getbef(buf,8);			/* Z */
	f4 = getbef(buf,12);			/* time-of-fix */
	gpsd_report(LOG_INF, "GPS Position XYZ %f %f %f %f\n",f1,f2,f3,f4);
	break;
    case 0x43:		/* Velocity Fix, XYZ ECEF */
	if (len != 20)
	    break;
	f1 = getbef(buf,0);			/* X velocity */
	f2 = getbef(buf,4);			/* Y velocity */
	f3 = getbef(buf,8);			/* Z velocity */
	f4 = getbef(buf,12);			/* bias rate */
	f5 = getbef(buf,16);			/* time-of-fix */
	gpsd_report(LOG_INF, "GPS Velocity XYZ %f %f %f %f %f\n",f1,f2,f3,f4,f5);
	break;
    case 0x45:		/* Software Version Information */
	if (len != 10)
	    break;
	/*@ -formattype @*/
	(void)snprintf(session->subtype, sizeof(session->subtype),
		       "%d.%d %02d%02d%02d %d.%d %02d%02d%02d",
		       getub(buf,0),getub(buf,1),getub(buf,4),getub(buf,2),getub(buf,3),
		       getub(buf,5),getub(buf,6),getub(buf,9),getub(buf,7),getub(buf,8));
	/*@ +formattype @*/
	gpsd_report(LOG_INF, "Software version: %s\n", session->subtype);
	mask |= DEVICEID_SET;
	break;
    case 0x46:		/* Health of Receiver */
	if (len != 2)
	    break;
	session->driver.tsip.last_46 = now;
	u1 = getub(buf,0);			/* Status code */
	u2 = getub(buf,1);			/* Antenna/Battery */
	if (u1 != (u_int8_t)0) {
	    session->gpsdata.status = STATUS_NO_FIX;
	    mask |= STATUS_SET;
	}
	else {
	    if (session->gpsdata.status < STATUS_FIX) {
		session->gpsdata.status = STATUS_FIX;
		mask |= STATUS_SET;
	    }
	}
	gpsd_report(LOG_PROG, "Receiver health %02x %02x\n",u1,u2);
	break;
    case 0x47:		/* Signal Levels for all Satellites */
	count = (int)getub(buf,0);		/* satellite count */
	if (len != (5*count + 1))
	    break;
	buf2[0] = '\0';
	for (i = 0; i < count; i++) {
	    u1 = getub(buf,5*i + 1);
	    if ((f1 = getbef(buf,5*i + 2)) < 0)
		f1 = 0.0;
	    for (j = 0; j < TSIP_CHANNELS; j++)
		if (session->gpsdata.PRN[j] == (int)u1) {
		    session->gpsdata.ss[j] = f1;
		    break;
		}
	    (void)snprintf(buf2+strlen(buf2), sizeof(buf2)-strlen(buf2),
			   " %d=%.1f",(int)u1,f1);
	}
	gpsd_report(LOG_PROG, "Signal Levels (%d):%s\n",count,buf2);
	mask |= SATELLITE_SET;
	break;
    case 0x48:		/* GPS System Message */
	buf[len] = '\0';
	gpsd_report(LOG_PROG, "GPS System Message: %s\n",buf);
	break;
    case 0x4a:		/* Single-Precision Position LLA */
	if (len != 20)
	    break;
	session->gpsdata.fix.latitude  = getbef(buf,0) * RAD_2_DEG;
	session->gpsdata.fix.longitude = getbef(buf,4) * RAD_2_DEG;
	session->gpsdata.fix.altitude  = getbef(buf,8);
	f1 = getbef(buf,12);			/* clock bias */
	f2 = getbef(buf,16);			/* time-of-fix */
	if (session->driver.tsip.gps_week) {
	    session->gpsdata.fix.time = session->gpsdata.sentence_time =
		gpstime_to_unix((int)session->driver.tsip.gps_week, f2) - session->context->leap_seconds;
	    mask |= TIME_SET;
	}
	gpsd_report(LOG_PROG, "GPS LLA %f %f %f %f\n",
		    session->gpsdata.fix.time,
		    session->gpsdata.fix.latitude,
		    session->gpsdata.fix.longitude,
		    session->gpsdata.fix.altitude);
	session->cycle_state |= CYCLE_START;
	mask |= LATLON_SET | ALTITUDE_SET;
	break;
    case 0x4b:		/* Machine/Code ID and Additional Status */
	if (len != 3)
	    break;
	u1 = getub(buf,0);			/* Machine ID */
	u2 = getub(buf,1);			/* Status 1 */
	u3 = getub(buf,2);			/* Status 2 */
	gpsd_report(LOG_INF, "Machine ID %02x %02x %02x\n",u1,u2,u3);
#if USE_SUPERPACKET
	if ((u3 & 0x01) != (u_int8_t)0 && !session->driver.tsip.superpkt) {
	    gpsd_report(LOG_PROG, "Switching to Super Packet mode\n");

	    /* set new I/O Options for Super Packet output */
	    putbyte(buf,0,0x2c);		/* Position: SP, MSL */
	    putbyte(buf,1,0x00);		/* Velocity: none (via SP) */
	    putbyte(buf,2,0x00);		/* Time: GPS */
	    putbyte(buf,3,0x2c); /* Aux: dBHz, raw, signal, satpos */
	    (void)tsip_write(session, 0x35, buf, 4);
	    session->driver.tsip.superpkt = true;
	}
#endif /* USE_SUPERPACKET */
	break;
    case 0x55:		/* IO Options */
	if (len != 4)
	    break;
	u1 = getub(buf,0);			/* Position */
	u2 = getub(buf,1);			/* Velocity */
	u3 = getub(buf,2);			/* Timing */
	u4 = getub(buf,3);			/* Aux */
	gpsd_report(LOG_INF, "IO Options %02x %02x %02x %02x\n",u1,u2,u3,u4);
#if USE_SUPERPACKET
	if ((u1 & 0x20) != (u_int8_t)0) {	/* Output Super Packets? */
	    /* No LFwEI Super Packet */
	    putbyte(buf,0,0x20);
	    putbyte(buf,1,0x00);		/* disabled */
	    (void)tsip_write(session, 0x8e, buf, 2);

	    /* Request Compact Super Packet */
	    putbyte(buf,0,0x23);
	    putbyte(buf,1,0x01);		/* enabled */
	    (void)tsip_write(session, 0x8e, buf, 2);
	}
#endif /* USE_SUPERPACKET */
	break;
    case 0x56:		/* Velocity Fix, East-North-Up (ENU) */
	if (len != 20)
	    break;
	f1 = getbef(buf,0);			/* East velocity */
	f2 = getbef(buf,4);			/* North velocity */
	f3 = getbef(buf,8);			/* Up velocity */
	f4 = getbef(buf,12);			/* clock bias rate */
	f5 = getbef(buf,16);			/* time-of-fix */
	session->gpsdata.fix.climb = f3;
	/*@ -evalorder @*/
	session->gpsdata.fix.speed = sqrt(pow(f2,2) + pow(f1,2));
	/*@ +evalorder @*/
	if ((session->gpsdata.fix.track = atan2(f1,f2) * RAD_2_DEG) < 0)
	    session->gpsdata.fix.track += 360.0;
	gpsd_report(LOG_INF, "GPS Velocity ENU %f %f %f %f %f\n",f1,f2,f3,f4,f5);
	mask |= SPEED_SET | TRACK_SET | CLIMB_SET;
	break;
    case 0x57:		/* Information About Last Computed Fix */
	if (len != 8)
	    break;
	u1 = getub(buf,0);			/* Source of information */
	u2 = getub(buf,1);			/* Mfg. diagnostic */
	f1 = getbef(buf,2);			/* gps_time */
	s1 = getbesw(buf,6);			/* tsip.gps_week */
	/*@ +charint @*/
	if (getub(buf,0) == 0x01)		/* good current fix? */
	    session->driver.tsip.gps_week = s1;
	/*@ -charint @*/
	gpsd_report(LOG_INF, "Fix info %02x %02x %d %f\n",u1,u2,s1,f1);
	break;
    case 0x58:		/* Satellite System Data/Acknowledge from Receiver */
	break;
    case 0x59:		/* Status of Satellite Disable or Ignore Health */
	break;
    case 0x5a:		/* Raw Measurement Data */
	if (len != 29)
	    break;
	f1 = getbef(buf,5);			/* Signal Level */
	f2 = getbef(buf,9);			/* Code phase */
	f3 = getbef(buf,13);			/* Doppler */
	d1 = getbed(buf,17);			/* Time of Measurement */
	gpsd_report(LOG_PROG, "Raw Measurement Data %d %f %f %f %f\n",getub(buf,0),f1,f2,f3,d1);
	break;
    case 0x5c:		/* Satellite Tracking Status */
	if (len != 24)
	    break;
	session->driver.tsip.last_5c = now;		/* keep timestamp for request */
	u1 = getub(buf,0);			/* PRN */
	u2 = getub(buf,1);			/* chan */
	u3 = getub(buf,2);			/* Acquisition flag */
	u4 = getub(buf,3);			/* Ephemeris flag */
	f1 = getbef(buf,4);			/* Signal level */
	f2 = getbef(buf,8);			/* time of Last measurement */
	d1 = getbef(buf,12) * RAD_2_DEG;		/* Elevation */
	d2 = getbef(buf,16) * RAD_2_DEG;		/* Azimuth */
	i = (int)(u2 >> 3);			/* channel number */
	gpsd_report(LOG_INF, "Satellite Tracking Status: Ch %2d PRN %3d Res %d Acq %d Eph %2d SNR %4.1f LMT %.04f El %4.1f Az %5.1f\n",i,u1,u2&7,u3,u4,f1,f2,d1,d2);
	if (i < TSIP_CHANNELS) {
	    if (d1 >= 0.0) {
		session->gpsdata.PRN[i] = (int)u1;
		session->gpsdata.ss[i] = f1;
		session->gpsdata.elevation[i] = (int)round(d1);
		session->gpsdata.azimuth[i] = (int)round(d2);
	    } else {
		session->gpsdata.PRN[i] = session->gpsdata.elevation[i] 
		    = session->gpsdata.azimuth[i] = 0;
		session->gpsdata.ss[i] = 0.0;
	    }
	    if (++i == session->gpsdata.satellites)
		mask |= SATELLITE_SET;		/* last of the series */
	    if (i > session->gpsdata.satellites)
		session->gpsdata.satellites = i;
	}
	break;
    case 0x6d:		/* All-In-View Satellite Selection */
	u1 = getub(buf,0);			/* nsvs/dimension */
	count = (int)((u1 >> 4) & 0x0f);
	if (len != (17 + count))
	    break;
	session->driver.tsip.last_6d = now;		/* keep timestamp for request */
	switch (u1 & 7)				/* dimension */
	{
	case 3:
	    //session->gpsdata.status = STATUS_FIX;
	    session->gpsdata.fix.mode = MODE_2D;
	    break;
	case 4:
	    //session->gpsdata.status = STATUS_FIX;
	    session->gpsdata.fix.mode = MODE_3D;
	    break;
	default:
	    //session->gpsdata.status = STATUS_NO_FIX;
	    session->gpsdata.fix.mode = MODE_NO_FIX;
	    break;
	}
	session->gpsdata.satellites_used = count;
	session->gpsdata.pdop = getbef(buf,1);
	session->gpsdata.hdop = getbef(buf,5);
	session->gpsdata.vdop = getbef(buf,9);
	session->gpsdata.tdop = getbef(buf,13);
	/*@ -evalorder @*/
	session->gpsdata.gdop = sqrt(pow(session->gpsdata.pdop,2)+pow(session->gpsdata.tdop,2));
	/*@ +evalorder @*/

	memset(session->gpsdata.used,0,sizeof(session->gpsdata.used));
	buf2[0] = '\0';
	/*@ +charint @*/
	for (i = 0; i < count; i++)
	    (void)snprintf(buf2+strlen(buf2), sizeof(buf2)-strlen(buf2),
			" %d",session->gpsdata.used[i] = getub(buf,17 + i));
	/*@ -charint @*/

	gpsd_report(LOG_INF, "Sat info: mode %d, satellites used %d: %s\n",
	    session->gpsdata.fix.mode, session->gpsdata.satellites_used,buf2);
	gpsd_report(LOG_INF,
	    "Sat info: DOP P=%.1f H=%.1f V=%.1f T=%.1f G=%.1f\n",
	    session->gpsdata.pdop, session->gpsdata.hdop,
	    session->gpsdata.vdop, session->gpsdata.tdop,
	    session->gpsdata.gdop);
	mask |= HDOP_SET | VDOP_SET | PDOP_SET | TDOP_SET | GDOP_SET | STATUS_SET | MODE_SET | USED_SET;
	break;
    case 0x6e:		/* Synchronized Measurements */
	break;
    case 0x6f:		/* Synchronized Measurements Report */
	/*@ +charint @*/
	if (len < 20 || getub(buf,0) != 1 || getub(buf,1) != 2)
	    break;
	/*@ -charint @*/
	s1 = getbesw(buf,2);			/* number of bytes */
	u1 = getub(buf,20);			/* number of SVs */
	break;
    case 0x70:		/* Filter Report */
	break;
    case 0x7a:		/* NMEA settings */
	break;
    case 0x82:		/* Differential Position Fix Mode */
	if (len != 1)
	    break;
	u1 = getub(buf,0);			/* fix mode */
	/*@ +charint @*/
	if (session->gpsdata.status == STATUS_FIX && (u1 & 0x01)!=0) {
	    session->gpsdata.status = STATUS_DGPS_FIX;
	    mask |= STATUS_SET;
	}
	/*@ -charint @*/
	gpsd_report(LOG_INF, "DGPS mode %d\n",u1);
	break;
    case 0x83:		/* Double-Precision XYZ Position Fix and Bias Information */
	if (len != 36)
	    break;
	d1 = getbed(buf,0);			/* X */
	d2 = getbed(buf,8);			/* Y */
	d3 = getbed(buf,16);			/* Z */
	d4 = getbed(buf,24);			/* clock bias */
	f1 = getbef(buf,32);			/* time-of-fix */
	gpsd_report(LOG_INF, "GPS Position XYZ %f %f %f %f %f\n",d1,d2,d3,d4,f1);
	break;
    case 0x84:		/* Double-Precision LLA Position Fix and Bias Information */
	if (len != 36)
	    break;
	session->gpsdata.fix.latitude  = getbed(buf,0) * RAD_2_DEG;
	session->gpsdata.fix.longitude = getbed(buf,8) * RAD_2_DEG;
	session->gpsdata.fix.altitude  = getbed(buf,16);
	d1 = getbed(buf,24);			/* clock bias */
	f1 = getbef(buf,32);			/* time-of-fix */
	if (session->driver.tsip.gps_week) {
	    session->gpsdata.fix.time = session->gpsdata.sentence_time =
		gpstime_to_unix((int)session->driver.tsip.gps_week, f1) - session->context->leap_seconds;
	    mask |= TIME_SET;
	}
	gpsd_report(LOG_INF, "GPS DP LLA %f %f %f %f\n",
		    session->gpsdata.fix.time,
		    session->gpsdata.fix.latitude,
		    session->gpsdata.fix.longitude,
		    session->gpsdata.fix.altitude);
	session->cycle_state |= CYCLE_START;
	mask |= LATLON_SET | ALTITUDE_SET;
	break;
    case 0x8f:		/* Super Packet.  Well...  */
	/*@ +charint @*/
	(void)snprintf(session->gpsdata.tag+strlen(session->gpsdata.tag),
		       sizeof(session->gpsdata.tag)-strlen(session->gpsdata.tag),
		       "%02x", u1 = getub(buf,0));
	/*@ -charint @*/
	switch (u1)				/* sub-packet ID */
	{
	case 0x15:	/* Current Datum Values */
	    if (len != 43)
		break;
	    s1 = getbesw(buf,1);			/* Datum Index */
	    d1 = getbed(buf,3);			/* DX */
	    d2 = getbed(buf,11);			/* DY */
	    d3 = getbed(buf,19);			/* DZ */
	    d4 = getbed(buf,27);			/* A-axis */
	    d5 = getbed(buf,35);			/* Eccentricity Squared */
	    gpsd_report(LOG_INF, "Current Datum %d %f %f %f %f %f\n",s1,d1,d2,d3,d4,d5);
	    break;

	case 0x20:	/* Last Fix with Extra Information (binary fixed point) */
	    /* XXX CSK sez "why does my Lassen iQ output oversize packets?" */
	    if ((len != 56) && (len != 64))
		break;
	    s1 = getbesw(buf,2);			/* east velocity */
	    s2 = getbesw(buf,4);			/* north velocity */
	    s3 = getbesw(buf,6);			/* up velocity */
	    ul1 = getbeul(buf,8);			/* time */
	    sl1 = getbesl(buf,12);		/* latitude */
	    ul2 = getbeul(buf,16);		/* longitude */
	    sl2 = getbesl(buf,20);		/* altitude */
	    u1 = getub(buf,24);			/* velocity scaling */
	    u2 = getub(buf,27);			/* fix flags */
	    u3 = getub(buf,28);			/* num svs */
	    u4 = getub(buf,29);			/* utc offset */
	    s4 = getbesw(buf,30);			/* tsip.gps_week */
						/* PRN/IODE data follows */
	    gpsd_report(LOG_RAW, "LFwEI %d %d %d %u %d %u %u %x %x %u %u %d\n",s1,s2,s3,ul1,sl1,ul2,sl2,u1,u2,u3,u4,s4);

	    if ((u1 & 0x01) != (u_int8_t)0)	/* check velocity scaling */
		d5 = 0.02;
	    else
		d5 = 0.005;
	    d1 = s1 * d5;			/* east velocity m/s */
	    d2 = s2 * d5;			/* north velocity m/s */
	    session->gpsdata.fix.climb = s3 * d5; /* up velocity m/s */
	    /*@ -evalorder @*/
	    session->gpsdata.fix.speed = sqrt(pow(d2,2) + pow(d1,2));
	    /*@ +evalorder @*/
	    if ((session->gpsdata.fix.track = atan2(d1,d2) * RAD_2_DEG) < 0)
		session->gpsdata.fix.track += 360.0;
	    session->gpsdata.fix.latitude  = sl1 * SEMI_2_DEG;
	    if ((session->gpsdata.fix.longitude = ul2 * SEMI_2_DEG) > 180.0)
		session->gpsdata.fix.longitude -= 360.0;
	    session->gpsdata.separation = wgs84_separation(session->gpsdata.fix.latitude, session->gpsdata.fix.longitude);
	    session->gpsdata.fix.altitude  = sl2 * 1e-3 - session->gpsdata.separation;;
	    session->gpsdata.status = STATUS_NO_FIX;
	    session->gpsdata.fix.mode = MODE_NO_FIX;
	    if ((u2 & 0x01) == (u_int8_t)0) {	/* Fix Available */
		session->gpsdata.status = STATUS_FIX;
		if ((u2 & 0x02) != (u_int8_t)0)	/* DGPS Corrected */
		    session->gpsdata.status = STATUS_DGPS_FIX;
		if ((u2 & 0x04) != (u_int8_t)0)	/* Fix Dimension */
		    session->gpsdata.fix.mode = MODE_2D;
		else
		    session->gpsdata.fix.mode = MODE_3D;
	    }
	    session->gpsdata.satellites_used = (int)u3;
	    if ((int)u4 > 10) {
		session->context->leap_seconds = (int)u4;
		session->context->valid |= LEAP_SECOND_VALID;
	    }
	    session->driver.tsip.gps_week = s4;
	    session->gpsdata.fix.time = session->gpsdata.sentence_time =
		gpstime_to_unix((int)s4, ul1 * 1e-3) - session->context->leap_seconds;
	    session->cycle_state |= CYCLE_START;
	    mask |= TIME_SET | LATLON_SET | ALTITUDE_SET | SPEED_SET | TRACK_SET | CLIMB_SET | STATUS_SET | MODE_SET; 
	    break;
	case 0x23:	/* Compact Super Packet */
	    /* XXX CSK sez "i don't trust this to not be oversized either." */
	    if (len < 29)
		break;
	    ul1 = getbeul(buf,1);			/* time */
	    s1 = getbesw(buf,5);			/* tsip.gps_week */
	    u1 = getub(buf,7);			/* utc offset */
	    u2 = getub(buf,8);			/* fix flags */
	    sl1 = getbesl(buf,9);			/* latitude */
	    ul2 = getbeul(buf,13);		/* longitude */
	    sl3 = getbesl(buf,17);		/* altitude */
	    s2 = getbesw(buf,21);			/* east velocity */
	    s3 = getbesw(buf,23);			/* north velocity */
	    s4 = getbesw(buf,25);			/* up velocity */
	    gpsd_report(LOG_INF, "CSP %u %d %u %u %d %u %d %d %d %d\n",ul1,s1,u1,u2,sl1,ul2,sl3,s2,s3,s4);
	    session->driver.tsip.gps_week = s1;
	    if ((int)u1 > 10) {
		session->context->leap_seconds = (int)u1;
		session->context->valid |= LEAP_SECOND_VALID;
	    }
	    session->gpsdata.fix.time = session->gpsdata.sentence_time =
		gpstime_to_unix((int)s1, ul1 * 1e-3) - session->context->leap_seconds;
	    session->gpsdata.status = STATUS_NO_FIX;
	    session->gpsdata.fix.mode = MODE_NO_FIX;
	    if ((u2 & 0x01) == (u_int8_t)0) {	/* Fix Available */
		session->gpsdata.status = STATUS_FIX;
		if ((u2 & 0x02) != (u_int8_t)0)	/* DGPS Corrected */
		    session->gpsdata.status = STATUS_DGPS_FIX;
		if ((u2 & 0x04) != (u_int8_t)0)	/* Fix Dimension */
		    session->gpsdata.fix.mode = MODE_2D;
		else
		    session->gpsdata.fix.mode = MODE_3D;
	    }
	    session->gpsdata.fix.latitude  = sl1 * SEMI_2_DEG;
	    if ((session->gpsdata.fix.longitude = ul2 * SEMI_2_DEG) > 180.0)
		session->gpsdata.fix.longitude -= 360.0;
	    session->gpsdata.separation = wgs84_separation(session->gpsdata.fix.latitude, session->gpsdata.fix.longitude);
	    session->gpsdata.fix.altitude  = sl3 * 1e-3 - session->gpsdata.separation;;
	    if ((u2 & 0x20) != (u_int8_t)0)	/* check velocity scaling */
		d5 = 0.02;
	    else
		d5 = 0.005;
	    d1 = s2 * d5;			/* east velocity m/s */
	    d2 = s3 * d5;			/* north velocity m/s */
	    session->gpsdata.fix.climb = s4 * d5; /* up velocity m/s */
	    /*@ -evalorder @*/
	    session->gpsdata.fix.speed = sqrt(pow(d2,2) + pow(d1,2)) * MPS_TO_KNOTS;
	    /*@ +evalorder @*/
	    if ((session->gpsdata.fix.track = atan2(d1,d2) * RAD_2_DEG) < 0)
		session->gpsdata.fix.track += 360.0;
	    session->cycle_state |= CYCLE_START;
	    mask |= TIME_SET | LATLON_SET | ALTITUDE_SET | SPEED_SET | TRACK_SET | CLIMB_SET | STATUS_SET | MODE_SET; 
	    break;


	case 0xab:		/* Thunderbolt Timing Superpacket */
	  if (len != 17) {
	    gpsd_report(4, "pkt 0xab len=%d\n", len);
	    break;
	  }
	  session->driver.tsip.last_41 = now;	/* keep timestamp for request */
	  ul1 = getbeul(buf,1);			/* gpstime */
	  s1 = (short)getbeuw(buf,5);			/* week */
	  s2 = getbesw(buf,7);			/* leap seconds */

	  session->driver.tsip.gps_week = s1;
	  if ((int)u1 > 10) {
	    session->context->leap_seconds = (int)s2;
	    session->context->valid |= LEAP_SECOND_VALID;

	    session->gpsdata.fix.time =  session->gpsdata.sentence_time =
		gpstime_to_unix((int)s1, (double)ul1) - (double)s2;
#ifdef NTPSHM_ENABLE
	    if (session->context->enable_ntpshm)
		(void)ntpshm_put(session,session->gpsdata.sentence_time+0.075);
#endif
	    mask |= TIME_SET;
	  }

	  gpsd_report(4, "GPS Time %u %d %d\n", ul1, s1, s2);
	  break;


	case 0xac:		/* Thunderbolt Position Superpacket */
	  if (len != 68) {
	    gpsd_report(4, "pkt 0xac len=%d\n", len);

	    break;
	  }
	  session->gpsdata.fix.latitude  = getbed(buf,36) * RAD_2_DEG;
	  session->gpsdata.fix.longitude = getbed(buf,44) * RAD_2_DEG;
	  session->gpsdata.fix.altitude  = getbed(buf,52);
	  f1 = getbef(buf,16);			/* clock bias */

	  u1 = getub(buf, 12);			/* GPS Decoding Status */
	  u2 = getub(buf, 1);			/* Reciever Mode */
	  if (u1 != (u_int8_t)0) {
	    session->gpsdata.status = STATUS_NO_FIX;
	    mask |= STATUS_SET;
	  }
	  else {
	    if (session->gpsdata.status < STATUS_FIX) {
	      session->gpsdata.status = STATUS_FIX;
	      mask |= STATUS_SET;
	    }
	  }

	  /* Decode Fix modes */
	  switch (u2 & 7) {
	  case 6:		/* Clock Hold 2D */
	  case 3:		/* 2D Position Fix */
	    //session->gpsdata.status = STATUS_FIX;
	    session->gpsdata.fix.mode = MODE_2D;
	    break;
	  case 7:		/* Thunderbolt overdetermined clock */
	  case 4:		/* 3D position Fix */
	    //session->gpsdata.status = STATUS_FIX;
	    session->gpsdata.fix.mode = MODE_3D;
	    break;
	  default:
	    //session->gpsdata.status = STATUS_NO_FIX;
	    session->gpsdata.fix.mode = MODE_NO_FIX;
	    break;
	  }

	  gpsd_report(4, "GPS status=%u mode=%u\n", u1, u2);
	  gpsd_report(4, "GPS DP LLA %f %f %f\n",session->gpsdata.fix.latitude,
		      session->gpsdata.fix.longitude,
		      session->gpsdata.fix.altitude);

	  session->cycle_state |= CYCLE_START;
	  mask |= LATLON_SET | ALTITUDE_SET | STATUS_SET | MODE_SET;
	  break;


	default:
	    gpsd_report(LOG_WARN,"Unhandled TSIP superpacket type 0x%02x\n",u1);
	}
	break;
    case 0xbb:		/* Navigation Configuration */
	if (len != 40)
	    break;
	u1 = getub(buf,0);			/* Subcode */
	u2 = getub(buf,1);			/* Operating Dimension */
	u3 = getub(buf,2);			/* DGPS Mode */
	u4 = getub(buf,3);			/* Dynamics Code */
	f1 = getbef(buf,5);			/* Elevation Mask */
	f2 = getbef(buf,9);			/* AMU Mask */
	f3 = getbef(buf,13);			/* DOP Mask */
	f4 = getbef(buf,17);			/* DOP Switch */
	u5 = getub(buf,21);			/* DGPS Age Limit */
	gpsd_report(LOG_INF, "Navigation Configuration %u %u %u %u %f %f %f %f %u\n",u1,u2,u3,u4,f1,f2,f3,f4,u5);
	break;
    default:
	gpsd_report(LOG_WARN,"Unhandled TSIP packet type 0x%02x\n",id);
	break;
    }

    /* see if it is time to send some request packets for reports that */
    /* the receiver won't send at fixed intervals */

    if ((now - session->driver.tsip.last_41) > 5) {
	/* Request Current Time */
	(void)tsip_write(session, 0x21, buf, 0);
	session->driver.tsip.last_41 = now;
    }

    if ((now - session->driver.tsip.last_6d) > 5) {
	/* Request GPS Receiver Position Fix Mode */
	(void)tsip_write(session, 0x24, buf, 0);
	session->driver.tsip.last_6d = now;
    }

    if ((now - session->driver.tsip.last_48) > 60) {
	/* Request GPS System Message */
	(void)tsip_write(session, 0x28, buf, 0);
	session->driver.tsip.last_48 = now;
    }

    if ((now - session->driver.tsip.last_5c) >= 5) {
	/* Request Current Satellite Tracking Status */
	putbyte(buf,0,0x00);		/* All satellites */
	(void)tsip_write(session, 0x3c, buf, 1);
	session->driver.tsip.last_5c = now;
    }

    if ((now - session->driver.tsip.last_46) > 5) {
	/* Request Health of Receiver */
	(void)tsip_write(session, 0x26, buf, 0);
	session->driver.tsip.last_46 = now;
    }

    return mask;
}

static gps_mask_t tsip_parse_input(struct gps_device_t *session)
{
    gps_mask_t st;

    if (session->packet.type == TSIP_PACKET){
	st = tsip_analyze(session);
	session->gpsdata.dev.driver_mode = MODE_BINARY;
	return st;
#ifdef EVERMORE_ENABLE
    } else if (session->packet.type == EVERMORE_PACKET) {
	(void)gpsd_switch_driver(session, "EverMore binary");
	st = evermore_parse(session, session->packet.outbuffer, session->packet.outbuflen);
	session->gpsdata.dev.driver_mode = MODE_BINARY;
	return st;
#endif /* EVERMORE_ENABLE */
    } else
	return 0;
}

#ifdef ALLOW_CONTROLSEND
static ssize_t tsip_control_send(struct gps_device_t *session, 
			    char *buf, size_t buflen)
/* not used by the daemon, it's for gpsctl and friends */
{
    return (ssize_t)tsip_write(session, 
		      (unsigned int)buf[0], (unsigned char *)buf+1, buflen-1);
}
#endif /* ALLOW_CONTROLSEND */

#ifdef ALLOW_RECONFIGURE
static void tsip_event_hook(struct gps_device_t *session, event_t event)
{
    if (event == event_configure && session->packet.counter == 0) {
	unsigned char buf[100];

	/* I/O Options */
	putbyte(buf,0,0x1e);		/* Position: DP, MSL, LLA */
	putbyte(buf,1,0x02);		/* Velocity: ENU */
	putbyte(buf,2,0x00);		/* Time: GPS */
	putbyte(buf,3,0x08);		/* Aux: dBHz */
	(void)tsip_write(session, 0x35, buf, 4);
    }
    if (event == event_probe_subtype) {
	unsigned char buf[100];

	switch (session->packet.counter) {
	case 0:
	    /* 
	     * TSIP is ODD parity 1 stopbit, save original values and
	     * change it XXX Thunderbolts and Copernicus use
	     * 8N1... which isn't exactly a XXX good idea due to the
	     * fragile wire format.  We must divine a XXX clever
	     * heuristic to decide if the parity change is required.
	     */
	    session->driver.tsip.parity = session->gpsdata.dev.parity;
	    session->driver.tsip.stopbits = (uint)session->gpsdata.dev.stopbits;
	    gpsd_set_speed(session, session->gpsdata.dev.baudrate, 'O', 1);
	    break;

	case 1:
	    /* Request Software Versions */
	    (void)tsip_write(session, 0x1f, NULL, 0);
	    /* Request Current Time */
	    (void)tsip_write(session, 0x21, NULL, 0);
	    /* Request GPS Systems Message */
	    (void)tsip_write(session, 0x28, NULL, 0);
	    /* Request Current Datum Values */
	    putbyte(buf,0,0x15);
	    (void)tsip_write(session, 0x8e, buf, 1);
	    /* Request Navigation Configuration */
	    putbyte(buf,0,0x03);
	    (void)tsip_write(session, 0xbb, buf, 1);
	    break;
	}
    }
    if (event == event_revert) {
	/* restore saved parity and stopbits when leaving TSIP mode */
	gpsd_set_speed(session,
		   session->gpsdata.dev.baudrate,
		   session->driver.tsip.parity,
		   session->driver.tsip.stopbits);
    }
}

static bool tsip_speed_switch(struct gps_device_t *session, 
			      unsigned int speed, 
			      char parity, int stopbits)
{
    unsigned char buf[100];

    switch (parity) {
    case 'E':
    case 2:
	parity = (char)2;
	break;
    case 'O':
    case 1:
	parity = (char)1;
	break;
    case 'N':
    case 0:
    default:
	parity = (char)0;
	break;
    }

    putbyte(buf,0,0xff);		/* current port */
    putbyte(buf,1,(round(log((double)speed/300)/M_LN2))+2); /* input dev.baudrate */
    putbyte(buf,2,getub(buf,1));	/* output baudrate */
    putbyte(buf,3,3);			/* character width (8 bits) */
    putbyte(buf,4,parity);		/* parity (normally odd) */
    putbyte(buf,5,stopbits-1);		/* stop bits (normally 1 stopbit) */
    putbyte(buf,6,0);			/* flow control (none) */
    putbyte(buf,7,0x02);		/* input protocol (TSIP) */
    putbyte(buf,8,0x02);		/* output protocol (TSIP) */
    putbyte(buf,9,0);			/* reserved */
    (void)tsip_write(session, 0xbc, buf, 10);

    return true;	/* it would be nice to error-check this */
}

static void tsip_mode(struct gps_device_t *session, int mode)
{
    unsigned char buf[16];

    if (mode == MODE_NMEA) {
        /* First turn on the NMEA messages we want */

        putbyte(buf,0,0x00);		/* subcode 0 */
        putbyte(buf,1,0x01);            /* 1-second fix interval */
        putbyte(buf,2,0x00);		/* Reserved */
        putbyte(buf,3,0x00);		/* Reserved */
        putbyte(buf,4,0x01);		/* 0=RMC, 1-7=Reserved */
        putbyte(buf,5,0x19);		/* 0=GGA, 1=GGL, 2=VTG, 3=GSV, */
                                        /* 4=GSA, 5=ZDA, 6-7=Reserved  */

        (void)tsip_write(session, 0x7A, buf, 6);

        /* Now switch to NMEA mode */

        memset(buf, 0, sizeof(buf));

        putbyte(buf,0,0xff);		/* current port */
        putbyte(buf,1,0x06);            /* 4800 bps input */
        putbyte(buf,2,0x06);		/* 4800 bps output */
        putbyte(buf,3,0x03);		/* 8 data bits */
        putbyte(buf,4,0x00);		/* No parity */
        putbyte(buf,5,0x00);		/* 1 stop bit */
        putbyte(buf,6,0x00);		/* No flow control */
        putbyte(buf,7,0x02);		/* Input protocol TSIP */
        putbyte(buf,8,0x04);		/* Output protocol NMEA */
        putbyte(buf,9,0x00);		/* Reserved */

        (void)tsip_write(session, 0xBC, buf, 10);

    } else if (mode == MODE_BINARY) {
        /* The speed switcher also puts us back in TSIP, so call it */
        /* with the default 9600 8O1. */
	// FIXME: Should preserve the current speed.
        (void)tsip_speed_switch(session, 9600, 'O', 1);

    } else {
        gpsd_report(LOG_ERROR, "unknown mode %i requested\n", mode);
    }
}
#endif /* ALLOW_RECONFIGURE */

/* this is everything we export */
const struct gps_type_t tsip_binary =
{
    .type_name      = "Trimble TSIP",	/* full name of type */
    .packet_type    = TSIP_PACKET,	/* associated lexer packet type */
    .trigger        = NULL,		/* no trigger */
    .channels       = TSIP_CHANNELS,	/* consumer-grade GPS */
    .probe_wakeup   = NULL,		/* no wakeup to be done before hunt */
    .probe_detect   = NULL,		/* no probe */
    .get_packet     = generic_get,	/* use the generic packet getter */
    .parse_packet   = tsip_parse_input,	/* parse message packets */
    .rtcm_writer    = NULL,		/* doesn't accept DGPS corrections */
#ifdef ALLOW_RECONFIGURE
    .control_send   = tsip_control_send,/* how to send commands */
#endif /* ALLOW_CONTROLSEND */
#ifdef ALLOW_RECONFIGURE
    .event_hook     = tsip_event_hook,	/* initial mode sets */
    .speed_switcher = tsip_speed_switch,/* change baud rate */
    .mode_switcher  = tsip_mode,	/* there is a mode switcher */
    .rate_switcher  = NULL,		/* no rate switcher */
    .min_cycle      = 1,		/* not relevant, no rate switcher */
#endif /* ALLOW_RECONFIGURE */
};

#endif /* TSIP_ENABLE */
