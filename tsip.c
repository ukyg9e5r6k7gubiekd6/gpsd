/*
 * Handle the Trimble TSIP packet format
 * by Rob Janssen, PE1CHL.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define __USE_ISOC99	1	/* needed to get log2() from math.h */
#include <math.h>
#include "gpsd.h"

#ifdef TSIP_ENABLE

union int_float {
    int i;
    float f;
};

union long_double {
    long long l;
    double d;
};

#define putb(off,b)	{ buf[off] = (unsigned char)(b); }
#define putw(off,w)	{ putb(off,(w) >> 8); putb(off+1,w); }
#define putl(off,l)	{ putw(off,(l) >> 16); putw(off+2,l); }

#define getb(off)	(buf[off])
#define getw(off)	((short)((getb(off) << 8) | getb(off+1)))
#define getl(off)	((int)((getb(off) << 24) | (getb(off+1) << 16) \
				| (getb(off+3) << 8) | getb(off+4)))
#define getL(off)	((long long)(((long long)getl(off) << 32) | ((long long)getl(off+4) & 0xffffffffL)))
#define getf(off)	(i_f.i = getl(off), i_f.f)
#define getd(off)	(l_d.l = getL(off), l_d.d)

static int tsip_write(int fd, unsigned int id, unsigned char *buf, int len)
{
    int i;
    unsigned char buf2[BUFSIZ];

    buf2[0] = '\0';
    for (i = 0; i < len; i++)
	(void)snprintf(buf2+strlen(buf2), sizeof(buf)-strlen(buf), 
		      "%02x", buf[i]);
    gpsd_report(5, "Sent TSIP packet id 0x%02x: %s\n",id,buf2);

    buf2[0] = '\x10';
    buf2[1] = (unsigned char)id;
    if (write(fd,buf2,2) != 2)
	return -1;

    while (len-- > 0) {
	if (*buf == '\x10')
	    if (write(fd,buf2,1) != 1)
		return -1;

	if (write(fd,buf++,1) != 1)
	    return -1;
    }

    buf2[1] = '\x03';
    if (write(fd,buf2,2) != 2)
	return -1;

    return 0;
}

static void tsip_initializer(struct gps_device_t *session)
{
    unsigned char buf[100];

    /* TSIP is ODD parity 1 stopbit, change it */
    gpsd_set_speed(session, session->gpsdata.baudrate, 'O', 1);

    /* I/O Options */
    putb(0,0x1e);		/* Position: DP, MSL, LLA */
    putb(1,0x02);		/* Velocity: ENU */
    putb(2,0x00);		/* Time: GPS */
    putb(3,0x08);		/* Aux: dBHz */
    (void)tsip_write(session->gpsdata.gps_fd, 0x35, buf, 4);

    /* Request Software Versions */
    (void)tsip_write(session->gpsdata.gps_fd, 0x1f, buf, 0);

    /* Request Current Time */
    (void)tsip_write(session->gpsdata.gps_fd, 0x21, buf, 0);

    /* Request GPS Systems Message */
    (void)tsip_write(session->gpsdata.gps_fd, 0x28, buf, 0);
}

static bool tsip_speed_switch(struct gps_device_t *session, unsigned int speed)
{
    unsigned char buf[100];

    putb(0,0xff);		/* current port */
    putb(1,(round(log(speed/300)/M_LN2))+2); /* input baudrate */
    putb(2,buf[1]);		/* output baudrate */
    putb(3,8);			/* character width (8 bits) */
    putb(4,1);			/* parity (odd) */
    putb(5,0);			/* stop bits (1 stopbit) */
    putb(6,0);			/* flow control (none) */
    putb(7,0x02);		/* input protocol (TSIP) */
    putb(8,0x02);		/* input protocol (TSIP) */
    putb(9,0);			/* reserved */
    (void)tsip_write(session->gpsdata.gps_fd, 0xbc, buf, 10);

    return true;	/* it would be nice to error-check this */
}

static int tsip_analyze(struct gps_device_t *session)
{
    int i, len = 0, mask = 0;
    unsigned int id;
    short s1,s2;
    float f1,f2,f3,f4,f5;
    double d1,d2,d3,d4;
    union int_float i_f;
    union long_double l_d;
    time_t t;
    unsigned char buf[BUFSIZ];
    char buf2[BUFSIZ];

    if (session->packet_type != TSIP_PACKET) {
	gpsd_report(2, "tsip_analyze packet type %d\n",session->packet_type);
	return 0;
    }

    if (session->outbuflen < 4 || session->outbuffer[0] != 0x10)
	return 0;

    /* remove DLE stuffing and put data part of message in buf */

    buf2[0] = '\0';
    for (i = 2; i < session->outbuflen; i++) {
	if (session->outbuffer[i] == 0x10)
	    if (session->outbuffer[++i] == 0x03)
		break;

	(void)snprintf(buf2+strlen(buf2), 
		      sizeof(buf)-strlen(buf),
		      "%02x", buf[len++] = session->outbuffer[i]);
    }

    (void)snprintf(session->gpsdata.tag, sizeof(session->gpsdata.tag), 
		   "ID%02x", id = session->outbuffer[1]);

    gpsd_report(5, "TSIP packet id 0x%02x length %d: %s\n",id,len,buf2);

    switch (id) {
    case 0x13:		/* Packet Received */
	gpsd_report(4, "Received packet of type %02x cannot be parsed\n",
		getb(0));
	break;
    case 0x41:		/* GPS Time */
	if (len != 10)
	    break;
	f1 = getf(0);			/* gpstime */
	s1 = getw(4);			/* week */
	f2 = getf(6);			/* leap seconds */
	if (f2 > 10.0) {
	    session->gps_week = s1;
	    session->context->leap_seconds = roundf(f2);
	    session->context->valid = LEAP_SECOND_VALID;

	    session->gpsdata.sentence_time = gpstime_to_unix(s1, f1) - f2;

#ifdef NTPSHM_ENABLE
	    ntpshm_put(session, session->gpsdata.sentence_time + 0.075);
#endif
	    mask |= TIME_SET;
	}
	break;
    case 0x42:		/* Single-Precision Position Fix, XYZ ECEF */
	if (len != 16)
	    break;
	f1 = getf(0);			/* X */
	f2 = getf(4);			/* Y */
	f3 = getf(8);			/* Z */
	f4 = getf(12);			/* time-of-fix */
	gpsd_report(4, "GPS Position XYZ %f %f %f %f\n",f1,f2,f3,f4);
	break;
    case 0x43:		/* Velocity Fix, XYZ ECEF */
	if (len != 20)
	    break;
	f1 = getf(0);			/* X velocity */
	f2 = getf(4);			/* Y velocity */
	f3 = getf(8);			/* Z velocity */
	f4 = getf(12);			/* bias rate */
	f5 = getf(16);			/* time-of-fix */
	gpsd_report(4, "GPS Velocity XYZ %f %f %f %f %f\n",f1,f2,f3,f4,f5);
	break;
    case 0x45:		/* Software Version Information */
	if (len != 10)
	    break;
	gpsd_report(4, "Software versions %d.%d %02d%02d%02d %d.%d %02d%02d%02d\n",
		(int)getb(0),(int)getb(1),(int)getb(4),(int)getb(2),(int)getb(3),
		(int)getb(5),(int)getb(6),(int)getb(9),(int)getb(7),(int)getb(8));
	break;
    case 0x46:		/* Health of Receiver */
	if (len != 2)
	    break;
	gpsd_report(4, "Receiver health %02x %02x\n",getb(0),getb(1));
	break;
    case 0x47:		/* Signal Levels for all Satellites */
	s1 = (int)getb(0);			/* count */
	if (len != (5*s1 + 1))
	    break;
	gpsd_zero_satellites(&session->gpsdata);
	session->gpsdata.satellites = s1;
	buf2[0] = '\0';
	for (i = 0; i < s1; i++) {
	    session->gpsdata.PRN[i] = s2 = (int)getb(5*i + 1);
	    session->gpsdata.ss[i] = f1 = getf(5*i + 2);
	    snprintf(buf2+strlen(buf2), sizeof(buf2)-strlen(buf2),
		     " %d=%.1f",s2,f1);
	}
	gpsd_report(4, "Signal Levels (%d):%s\n",s1,buf2);
	mask |= SATELLITE_SET;
	break;
    case 0x48:		/* GPS System Message */
	buf[len] = '\0';
	gpsd_report(4, "GPS System Message: %s\n",buf);
	break;
    case 0x4a:		/* Single-Precision Position LLA */
	if (len != 20)
	    break;
	session->gpsdata.fix.latitude  = getf(0) * RAD_2_DEG;
	session->gpsdata.fix.longitude = getf(4) * RAD_2_DEG;
	session->gpsdata.fix.altitude  = getf(8);
	f1 = getf(12);			/* clock bias */
	f2 = getf(16);			/* time-of-fix */
	if (session->gps_week)
	    session->gpsdata.fix.time = session->gpsdata.sentence_time =
		gpstime_to_unix(session->gps_week, f2) - session->context->leap_seconds;
	session->gpsdata.status = STATUS_FIX;
	gpsd_report(4, "GPS LLA %f %f %f\n",session->gpsdata.fix.latitude,session->gpsdata.fix.longitude,session->gpsdata.fix.altitude);
	gpsd_binary_fix_dump(session, buf2, sizeof(buf2));
	gpsd_report(3, "<= GPS: %s", buf2);
	mask |= LATLON_SET | ALTITUDE_SET;
	break;
    case 0x4b:		/* Machine/Code ID and Additional Status */
	if (len != 3)
	    break;
	gpsd_report(4, "Machine ID %02x %02x %02x\n",getb(0),getb(1),getb(2));
	break;
    case 0x55:		/* IO Options */
	if (len != 4)
	    break;
	gpsd_report(4, "IO Options %02x %02x %02x %02x\n",getb(0),getb(1),getb(2),getb(4));
	break;
    case 0x56:		/* Velocity Fix, East-North-Up (ENU) */
	if (len != 20)
	    break;
	f1 = getf(0);			/* East velocity */
	f2 = getf(4);			/* North velocity */
	f3 = getf(8);			/* Up velocity */
	f4 = getf(12);			/* clock bias rate */
	f5 = getf(16);			/* time-of-fix */
	session->gpsdata.fix.climb = f1;
	session->gpsdata.fix.speed = sqrt(pow(f2,2) + pow(f1,2));
	if ((session->gpsdata.fix.track = atan2(f1,f2) * RAD_2_DEG) < 0)
	    session->gpsdata.fix.track += 360.0;
	gpsd_report(4, "GPS Velocity ENU %f %f %f %f %f\n",f1,f2,f3,f4,f5);
	mask |= SPEED_SET | TRACK_SET | CLIMB_SET; 
	break;
    case 0x57:		/* Information About Last Computed Fix */
	if (len != 8)
	    break;
	f1 = getf(2);			/* gps_time */
	s1 = getw(6);			/* gps_weeks */
	if (getb(0) != 0)			/* good current fix? */
	    session->gps_week = s1;
	gpsd_report(4, "Fix info %02x %02x %d %f\n",getb(0),getb(1),s1,f1);
	break;
    case 0x58:		/* Satellite System Data/Acknowledge from Receiver */
	break;
    case 0x59:		/* Status of Satellite Disable or Ignore Health */
	break;
    case 0x5a:		/* Raw Measurement Data */
	break;
    case 0x5c:		/* Satellite Tracking Status */
	break;
    case 0x6d:		/* All-In-View Satellite Selection */
	s1 = getb(0);
	switch (s1 & 7)			/* dimension */
	{
	case 3:
	    session->gpsdata.fix.mode = MODE_2D;
	    break;
	case 4:
	    session->gpsdata.fix.mode = MODE_3D;
	    break;
	default:
	    session->gpsdata.fix.mode = MODE_NO_FIX;
	    break;
	}
	session->gpsdata.satellites_used = (s1 >> 4) & 0x0f;
	session->gpsdata.pdop = getf(1);
	session->gpsdata.hdop = getf(5);
	session->gpsdata.vdop = getf(9);
	session->gpsdata.tdop = getf(13);
	session->gpsdata.gdop = sqrt(pow(session->gpsdata.pdop,2)+pow(session->gpsdata.tdop,2));

	memset(session->gpsdata.used,0,sizeof(session->gpsdata.used));
	for (i = 0; i < session->gpsdata.satellites_used; i++)
	    session->gpsdata.used[i] = getb(16 + i);

	gpsd_report(4, "Sat info: %d %d\n",session->gpsdata.fix.mode,session->gpsdata.satellites_used);
	gpsd_binary_quality_dump(session, buf2, sizeof(buf2));
	gpsd_report(3, "<= GPS: %s", buf2);
        mask |= HDOP_SET | VDOP_SET | PDOP_SET | MODE_SET;
	break;
    case 0x6e:		/* Synchronized Measurements */
	break;
    case 0x6f:		/* Synchronized Measurements Report */
	if (len < 20 || getb(0) != 1 || getb(1) != 2)
	    break;
	s1 = getw(2);			/* number of bytes */
	s2 = getb(20);			/* number of SVs */
	break;
    case 0x70:		/* Filter Report */
	break;
    case 0x7a:		/* NMEA settings */
	break;
    case 0x82:		/* Differential Position Fix Mode */
	if (len != 1)
	    break;
	if (session->gpsdata.status == STATUS_FIX && (getb(0) & 0x01))
	    session->gpsdata.status = STATUS_DGPS_FIX;
	gpsd_report(4, "DGPS mode %d\n",getb(0));
	break;
    case 0x83:		/* Double-Precision XYZ Position Fix and Bias Information */
	if (len != 36)
	    break;
	d1 = getd(0);			/* X */
	d2 = getd(8);			/* Y */
	d3 = getd(16);			/* Z */
	d4 = getd(24);			/* clock bias */
	f1 = getf(32);			/* time-of-fix */
	gpsd_report(4, "GPS Position XYZ %f %f %f %f %f\n",d1,d2,d3,d4,f1);
	break;
    case 0x84:		/* Double-Precision LLA Position Fix and Bias Information */
	if (len != 36)
	    break;
	session->gpsdata.fix.latitude  = getd(0) * RAD_2_DEG;
	session->gpsdata.fix.longitude = getd(8) * RAD_2_DEG;
	session->gpsdata.fix.altitude  = getd(16);
	f1 = getd(24);			/* clock bias */
	f2 = getf(32);			/* time-of-fix */
	if (session->gps_week)
	    session->gpsdata.fix.time = session->gpsdata.sentence_time =
		gpstime_to_unix(session->gps_week, f2) - session->context->leap_seconds;
	session->gpsdata.status = STATUS_FIX;
	gpsd_report(4, "GPS DP LLA %f %f %f\n",session->gpsdata.fix.latitude,session->gpsdata.fix.longitude,session->gpsdata.fix.altitude);
	gpsd_binary_fix_dump(session, buf2, sizeof(buf2));
	gpsd_report(3, "<= GPS: %s", buf2);
	mask |= LATLON_SET | ALTITUDE_SET;
	break;
    case 0x8f:		/* Super Packet.  Well...  */
	switch (getb(0))		/* sub-packet ID */
	{
	case 0x20:	/* Last Fix with Extra Information (binary fixed point) */
	    if (len != 56)
		break;
	    break;
	case 0x23:	/* Compact Super Packet */
	    if (len != 29)
		break;
	    break;
	default:
	    gpsd_report(4,"Unhandled TSIP superpacket type 0x%02x\n",getb(0));
	}
	break;
    default:
	gpsd_report(4,"Unhandled TSIP packet type 0x%02x\n",id);
	break;
    }

    /* see if it is time to send some request packets for reports that */
    /* the receiver won't send at fixed intervals */

    time(&t);
    if ((t - session->last_request) >= 5) {
	/* Request GPS Receiver Position Fix Mode */
	(void)tsip_write(session->gpsdata.gps_fd, 0x24, buf, 0);

	/* Request Signal Levels */
	(void)tsip_write(session->gpsdata.gps_fd, 0x27, buf, 0);

	session->last_request = t;
    }

    return mask;
}

/* this is everything we export */
struct gps_type_t tsip_binary =
{
    "Trimble TSIP",	/* full name of type */
    NULL,		/* no probe */
    NULL,		/* only switched to by some other driver */
    tsip_initializer,	/* initialization */
    packet_get,		/* how to get a packet */
    tsip_analyze,	/* read and parse message packets */
    NULL,		/* send DGPS correction */
    tsip_speed_switch,	/* change baud rate */
    NULL,		/* no mode switcher */
    NULL,		/* caller needs to supply a close hook */
    1,			/* updates every second */
};

#endif /* TSIP_ENABLE */
