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

#define putbyte(off,b)	{ buf[off] = (unsigned char)(b); }
#define putword(off,w)	{ putbyte(off,(w) >> 8); putbyte(off+1,w); }
#define putlong(off,l)	{ putword(off,(l) >> 16); putword(off+2,l); }

#define getbyte(off)	(buf[off])
#define getword(off)	((short)((getbyte(off) << 8) | getbyte(off+1)))
#define getl(off)	((int)((getbyte(off) << 24) | (getbyte(off+1) << 16) \
				| (getbyte(off+3) << 8) | getbyte(off+4)))
#define getL(off)	((long long)(((unsigned long long)buf[off]<<56) \
				     | ((unsigned long long)buf[off+1]<<48) \
				     | ((unsigned long long)buf[off+2]<<40) \
				     | ((unsigned long long)buf[off+3]<<32) \
				     | ((unsigned long long)buf[off+4]<<24) \
				     | ((unsigned long long)buf[off+5]<<16) \
				     | ((unsigned long long)buf[off+6]<<8) \
				     | (unsigned long long)buf[off+7]))
#define getf(off)	(i_f.i = getl(off), i_f.f)
#define getd(off)	(l_d.l = getL(off), l_d.d)

static int tsip_write(int fd, unsigned int id, unsigned char *buf, int len)
{
    int i;
    char buf2[BUFSIZ];

    buf2[0] = '\0';
    for (i = 0; i < len; i++)
	(void)snprintf(buf2+strlen(buf2), 
		       sizeof(buf2)-strlen(buf2), 
		       "%02x", (unsigned)buf[i]);
    gpsd_report(5, "Sent TSIP packet id 0x%02x: %s\n",id,buf2);

    /*@ +charint @*/
    buf2[0] = '\x10';
    buf2[1] = (char)id;
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
    /*@ -charint @*/
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
    putbyte(0,0x1e);		/* Position: DP, MSL, LLA */
    putbyte(1,0x02);		/* Velocity: ENU */
    putbyte(2,0x00);		/* Time: GPS */
    putbyte(3,0x08);		/* Aux: dBHz */
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

    putbyte(0,0xff);		/* current port */
    putbyte(1,(round(log((double)speed/300)/M_LN2))+2); /* input baudrate */
    putbyte(2,buf[1]);		/* output baudrate */
    putbyte(3,8);			/* character width (8 bits) */
    putbyte(4,1);			/* parity (odd) */
    putbyte(5,0);			/* stop bits (1 stopbit) */
    putbyte(6,0);			/* flow control (none) */
    putbyte(7,0x02);		/* input protocol (TSIP) */
    putbyte(8,0x02);		/* input protocol (TSIP) */
    putbyte(9,0);			/* reserved */
    (void)tsip_write(session->gpsdata.gps_fd, 0xbc, buf, 10);

    return true;	/* it would be nice to error-check this */
}

static gps_mask_t tsip_analyze(struct gps_device_t *session)
{
    int i, len;
    gps_mask_t mask = 0;
    unsigned int id, u1;
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

    /*@ +charint @*/
    if (session->outbuflen < 4 || session->outbuffer[0] != 0x10)
	return 0;

    /* remove DLE stuffing and put data part of message in buf */

    memset(buf, 0, sizeof(buf));
    buf2[len = 0] = '\0';
    for (i = 2; i < (int)session->outbuflen; i++) {
	if (session->outbuffer[i] == 0x10)
	    if (session->outbuffer[++i] == 0x03)
		break;

	(void)snprintf(buf2+strlen(buf2), 
		      sizeof(buf2)-strlen(buf2),
		      "%02x", buf[len++] = session->outbuffer[i]);
    }
    /*@ -charint @*/

    (void)snprintf(session->gpsdata.tag, sizeof(session->gpsdata.tag), 
		   "ID%02x", id = (unsigned)session->outbuffer[1]);

    gpsd_report(5, "TSIP packet id 0x%02x length %d: %s\n",id,len,buf2);

    switch (id) {
    case 0x13:		/* Packet Received */
	gpsd_report(4, "Received packet of type %02x cannot be parsed\n",
		getbyte(0));
	break;
    case 0x41:		/* GPS Time */
	if (len != 10)
	    break;
	f1 = getf(0);			/* gpstime */
	s1 = getword(4);			/* week */
	f2 = getf(6);			/* leap seconds */
	if (f2 > 10.0) {
	    session->gps_week = (unsigned)s1;
	    /*@i@*/session->context->leap_seconds = roundf(f2);
	    session->context->valid = LEAP_SECOND_VALID;

	    session->gpsdata.sentence_time = gpstime_to_unix(s1, f1) - f2;

#ifdef NTPSHM_ENABLE
	    (void)ntpshm_put(session, session->gpsdata.sentence_time + 0.075);
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
		(int)getbyte(0),(int)getbyte(1),(int)getbyte(4),(int)getbyte(2),(int)getbyte(3),
		(int)getbyte(5),(int)getbyte(6),(int)getbyte(9),(int)getbyte(7),(int)getbyte(8));
	break;
    case 0x46:		/* Health of Receiver */
	if (len != 2)
	    break;
	gpsd_report(4, "Receiver health %02x %02x\n",getbyte(0),getbyte(1));
	break;
    case 0x47:		/* Signal Levels for all Satellites */
	s1 = (short)getbyte(0);			/* count */
	if (len != (5*s1 + 1))
	    break;
	gpsd_zero_satellites(&session->gpsdata);
	session->gpsdata.satellites = s1;
	buf2[0] = '\0';
	for (i = 0; i < s1; i++) {
	    session->gpsdata.PRN[i] = s2 = (short)getbyte(5*i + 1);
	    f1 = getf(5*i + 2);
	    session->gpsdata.ss[i] = (int)f1;
	    (void)snprintf(buf2+strlen(buf2), sizeof(buf2)-strlen(buf2),
			   " %d=%1f",s2,f1);
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
		gpstime_to_unix((int)session->gps_week, f2) - session->context->leap_seconds;
	session->gpsdata.status = STATUS_FIX;
	gpsd_report(4, "GPS LLA %f %f %f\n",session->gpsdata.fix.latitude,session->gpsdata.fix.longitude,session->gpsdata.fix.altitude);
	gpsd_binary_fix_dump(session, buf2, sizeof(buf2));
	gpsd_report(3, "<= GPS: %s", buf2);
	mask |= LATLON_SET | ALTITUDE_SET;
	break;
    case 0x4b:		/* Machine/Code ID and Additional Status */
	if (len != 3)
	    break;
	gpsd_report(4, "Machine ID %02x %02x %02x\n",getbyte(0),getbyte(1),getbyte(2));
	break;
    case 0x55:		/* IO Options */
	if (len != 4)
	    break;
	gpsd_report(4, "IO Options %02x %02x %02x %02x\n",getbyte(0),getbyte(1),getbyte(2),getbyte(4));
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
	/*@ -evalorder @*/
	session->gpsdata.fix.speed = sqrt(pow(f2,2) + pow(f1,2));
	/*@ +evalorder @*/
	if ((session->gpsdata.fix.track = atan2(f1,f2) * RAD_2_DEG) < 0)
	    session->gpsdata.fix.track += 360.0;
	gpsd_report(4, "GPS Velocity ENU %f %f %f %f %f\n",f1,f2,f3,f4,f5);
	mask |= SPEED_SET | TRACK_SET | CLIMB_SET; 
	break;
    case 0x57:		/* Information About Last Computed Fix */
	if (len != 8)
	    break;
	f1 = getf(2);			/* gps_time */
	s1 = getword(6);			/* gps_weeks */
	if ((int)getbyte(0) != 0)			/* good current fix? */
	    session->gps_week = (unsigned)s1;
	gpsd_report(4, "Fix info %02x %02x %d %f\n",getbyte(0),getbyte(1),s1,f1);
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
	u1 = (unsigned)getbyte(0);
	switch (u1 & 7)			/* dimension */
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
	session->gpsdata.satellites_used = (int)((u1 >> 4) & 0x0f);
	session->gpsdata.pdop = getf(1);
	session->gpsdata.hdop = getf(5);
	session->gpsdata.vdop = getf(9);
	session->gpsdata.tdop = getf(13);
	/*@ -evalorder @*/
	session->gpsdata.gdop = sqrt(pow(session->gpsdata.pdop,2)+pow(session->gpsdata.tdop,2));
	/*@ +evalorder @*/

	memset(session->gpsdata.used,0,sizeof(session->gpsdata.used));
	for (i = 0; i < session->gpsdata.satellites_used; i++)
	    session->gpsdata.used[i] = (int)getbyte(16 + i);

	gpsd_report(4, "Sat info: %d %d\n",session->gpsdata.fix.mode,session->gpsdata.satellites_used);
	gpsd_binary_quality_dump(session, buf2, sizeof(buf2));
	gpsd_report(3, "<= GPS: %s", buf2);
        mask |= HDOP_SET | VDOP_SET | PDOP_SET | MODE_SET;
	break;
    case 0x6e:		/* Synchronized Measurements */
	break;
    case 0x6f:		/* Synchronized Measurements Report */
	if (len < 20 || (int)getbyte(0) != 1 || (int)getbyte(1) != 2)
	    break;
	s1 = getword(2);			/* number of bytes */
	s2 = (short)getbyte(20);			/* number of SVs */
	break;
    case 0x70:		/* Filter Report */
	break;
    case 0x7a:		/* NMEA settings */
	break;
    case 0x82:		/* Differential Position Fix Mode */
	if (len != 1)
	    break;
	if (session->gpsdata.status==STATUS_FIX && ((int)getbyte(0) & 0x01)!=0)
	    session->gpsdata.status = STATUS_DGPS_FIX;
	gpsd_report(4, "DGPS mode %d\n",getbyte(0));
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
	d1 = getd(24);			/* clock bias */
	f2 = getf(32);			/* time-of-fix */
	if (session->gps_week)
	    session->gpsdata.fix.time = session->gpsdata.sentence_time =
		gpstime_to_unix((int)session->gps_week, f2) - session->context->leap_seconds;
	session->gpsdata.status = STATUS_FIX;
	gpsd_report(4, "GPS DP LLA %f %f %f\n",session->gpsdata.fix.latitude,session->gpsdata.fix.longitude,session->gpsdata.fix.altitude);
	gpsd_binary_fix_dump(session, buf2, sizeof(buf2));
	gpsd_report(3, "<= GPS: %s", buf2);
	mask |= LATLON_SET | ALTITUDE_SET;
	break;
    case 0x8f:		/* Super Packet.  Well...  */
	switch (getbyte(0))		/* sub-packet ID */
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
	    gpsd_report(4,"Unhandled TSIP superpacket type 0x%02x\n",getbyte(0));
	}
	break;
    default:
	gpsd_report(4,"Unhandled TSIP packet type 0x%02x\n",id);
	break;
    }

    /* see if it is time to send some request packets for reports that */
    /* the receiver won't send at fixed intervals */

    (void)time(&t);
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
