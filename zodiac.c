/*
 * Handle the Rockwell binary packet format supported by the old Zodiac chipset
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define __USE_ISOC99	1	/* needed to get log2() from math.h */
#include <math.h>
#include "gpsd.h"

#ifdef ZODIAC_ENABLE

struct header {
    unsigned short sync;
    unsigned short id;
    unsigned short ndata;
    unsigned short flags;
    unsigned short csum;
};

static unsigned short zodiac_checksum(unsigned short *w, int n)
{
    unsigned short csum = 0;

    while (n-- > 0)
	csum += *(w++);
    return -csum;
}

/* zodiac_spew - Takes a message type, an array of data words, and a length
   for the array, and prepends a 5 word header (including checksum).
   The data words are expected to be checksummed */
#if defined (WORDS_BIGENDIAN)
/* data is assumed to contain len/2 unsigned short words
 * we change the endianness to little, when needed.
 */
static int end_write(int fd, void *d, int len)
{
    char buf[BUFSIZ];
    char *p = buf;
    char *data = (char *)d;

    while (len>0) {
	*p++ = *(data+1); *p++ = *data;
	data += 2; len -= 2;
    }
    return write(fd, buf, len);
}
#else
#define end_write write
#endif

static void zodiac_spew(struct gps_device_t *session, int type, unsigned short *dat, int dlen)
{
    struct header h;
    int i;
    char buf[BUFSIZ];

    h.sync = 0x81ff;
    h.id = (unsigned short)type;
    h.ndata = (unsigned short)(dlen - 1);
    h.flags = 0;
    h.csum = zodiac_checksum((unsigned short *) &h, 4);

    if (session->gpsdata.gps_fd != -1) {
	(void)end_write(session->gpsdata.gps_fd, &h, sizeof(h));
	(void)end_write(session->gpsdata.gps_fd, dat, sizeof(unsigned short) * dlen);
    }

    (void)snprintf(buf, sizeof(buf),
		   "%04x %04x %04x %04x %04x",
		   h.sync,h.id,h.ndata,h.flags,h.csum);
    for (i = 0; i < dlen; i++)
	(void)snprintf(buf+strlen(buf), sizeof(buf)-strlen(buf),
		       " %04x", dat[i]);

    gpsd_report(5, "Sent Zodiac packet: %s\n",buf);
}

static bool zodiac_speed_switch(struct gps_device_t *session, speed_t speed)
{
    unsigned short data[15];

    if (session->sn++ > 32767)
	session->sn = 0;
      
    memset(data, 0, sizeof(data));
    /* data is the part of the message starting at word 6 */
    data[0] = session->sn;		/* sequence number */
    data[1] = 1;			/* port 1 data valid */
    data[2] = 1;			/* port 1 character width (8 bits) */
    data[3] = 0;			/* port 1 stop bits (1 stopbit) */
    data[4] = 0;			/* port 1 parity (none) */
    data[5] = (short)(round(log(speed/300)/M_LN2))+1; /* port 1 speed */
    data[14] = zodiac_checksum(data, 14);

    zodiac_spew(session, 1330, data, 15);

    return true;	/* it would be nice to error-check this */
}

static void send_rtcm(struct gps_device_t *session, 
		      char *rtcmbuf, size_t rtcmbytes)
{
    unsigned short data[34];
    int n = 1 + (rtcmbytes/2 + rtcmbytes%2);

    if (session->sn++ > 32767)
	session->sn = 0;

    memset(data, 0, sizeof(data));
    data[0] = session->sn;		/* sequence number */
    memcpy(&data[1], rtcmbuf, rtcmbytes);
    data[n] = zodiac_checksum(data, n);

    zodiac_spew(session, 1351, data, n+1);
}

static size_t zodiac_send_rtcm(struct gps_device_t *session,
			char *rtcmbuf, size_t rtcmbytes)
{
    int len;

    while (rtcmbytes > 0) {
	len = rtcmbytes>64?64:rtcmbytes;
	send_rtcm(session, rtcmbuf, len);
	rtcmbytes -= len;
	rtcmbuf += len;
    }
    return 1;
}

/* Zodiac protocol description uses 1-origin indexing by little-endian word */
#define getw(n)	( (session->outbuffer[2*(n)-2]) \
		| (session->outbuffer[2*(n)-1] << 8))
#define getl(n)	( (session->outbuffer[2*(n)-2]) \
		| (session->outbuffer[2*(n)-1] << 8) \
		| (session->outbuffer[2*(n)+0] << 16) \
		| (session->outbuffer[2*(n)+1] << 24))

static int handle1000(struct gps_device_t *session)
{
    /* ticks                      = getl(6); */
    /* sequence                   = getw(8); */
    /* measurement_sequence       = getw(9); */
    session->gpsdata.status       = (getw(10) & 0x1c) ? 0 : 1;
    if (session->gpsdata.status != 0)
	session->gpsdata.fix.mode = (getw(10) & 1) ? MODE_2D : MODE_3D;
    else
	session->gpsdata.fix.mode = MODE_NO_FIX;

    /* solution_type                 = getw(11); */
    session->gpsdata.satellites_used = (int)getw(12);
    /* polar_navigation              = getw(13); */
    /* gps_week                      = getw(14); */
    /* gps_seconds                   = getl(15); */
    /* gps_nanoseconds               = getl(17); */
    session->gpsdata.nmea_date.tm_mday = (int)getw(19);
    session->gpsdata.nmea_date.tm_mon = (int)getw(20) - 1;
    session->gpsdata.nmea_date.tm_year = (int)getw(21) - 1900;
    session->gpsdata.nmea_date.tm_hour = (int)getw(22);
    session->gpsdata.nmea_date.tm_min = (int)getw(23);
    session->gpsdata.nmea_date.tm_sec = (int)getw(24);
    session->gpsdata.subseconds = getl(25) / 1e9;
    session->gpsdata.fix.time = session->gpsdata.sentence_time =
	(double)mkgmtime(&session->gpsdata.nmea_date) + session->gpsdata.subseconds;
#ifdef NTPSHM_ENABLE
    if (session->gpsdata.fix.mode > MODE_NO_FIX)
	(void)ntpshm_put(session, session->gpsdata.fix.time + 1.1);
#endif
    session->gpsdata.fix.latitude  = ((long)getl(27)) * RAD_2_DEG * 1e-8;
    session->gpsdata.fix.longitude = ((long)getl(29)) * RAD_2_DEG * 1e-8;
    session->gpsdata.fix.altitude  = ((long)getl(31)) * 1e-2;
    session->gpsdata.fix.separation = ((short)getw(33)) * 1e-2;
    session->gpsdata.fix.speed     = getl(34) * 1e-2;
    session->gpsdata.fix.track     = getw(36) * RAD_2_DEG * 1e-3;
    session->mag_var               = ((short)getw(37)) * RAD_2_DEG * 1e-4;
    session->gpsdata.fix.climb     = ((short)getw(38)) * 1e-2;
    /* map_datum                   = getw(39); */
    session->gpsdata.fix.eph       = getl(40) * 1e-2;
    session->gpsdata.fix.epv       = getl(42) * 1e-2;
    session->gpsdata.fix.ept       = getl(44) * 1e-2;
    session->gpsdata.fix.eps       = getw(46) * 1e-2;
    /* clock_bias                  = getl(47) * 1e-2; */
    /* clock_bias_sd               = getl(49) * 1e-2; */
    /* clock_drift                 = getl(51) * 1e-2; */
    /* clock_drift_sd              = getl(53) * 1e-2; */

#if 0
    gpsd_report(1, "date: %lf\n", session->gpsdata.fix.time);
    gpsd_report(1, "  solution invalid:\n");
    gpsd_report(1, "    altitude: %d\n", (getw(10) & 1) ? 1 : 0);
    gpsd_report(1, "    no diff gps: %d\n", (getw(10) & 2) ? 1 : 0);
    gpsd_report(1, "    not enough satellites: %d\n", (getw(10) & 4) ? 1 : 0);
    gpsd_report(1, "    exceed max EHPE: %d\n", (getw(10) & 8) ? 1 : 0);
    gpsd_report(1, "    exceed max EVPE: %d\n", (getw(10) & 16) ? 1 : 0);
    gpsd_report(1, "  solution type:\n");
    gpsd_report(1, "    propagated: %d\n", (getw(11) & 1) ? 1 : 0);
    gpsd_report(1, "    altitude: %d\n", (getw(11) & 2) ? 1 : 0);
    gpsd_report(1, "    differential: %d\n", (getw(11) & 4) ? 1 : 0);
    gpsd_report(1, "Number of measurements in solution: %d\n", getw(12));
    gpsd_report(1, "Lat: %f\n", getl(27) * RAD_2_DEG * 1e-8);
    gpsd_report(1, "Lon: %f\n", getl(29) * RAD_2_DEG * 1e-8);
    gpsd_report(1, "Alt: %f\n", (double) getl(31) * 1e-2);
    gpsd_report(1, "Speed: %f\n", (double) getl(34) * 1e-2 * MPS_TO_KNOTS);
    gpsd_report(1, "Map datum: %d\n", getw(39));
    gpsd_report(1, "Magnetic variation: %f\n", getw(37) * RAD_2_DEG * 1e-4);
    gpsd_report(1, "Course: %f\n", getw(36) * RAD_2_DEG * 1e-4);
    gpsd_report(1, "Separation: %f\n", getw(33) * 1e-2);
#endif

    session->gpsdata.sentence_length = 55;
    return TIME_SET|LATLON_SET|ALTITUDE_SET|CLIMB_SET|SPEED_SET|TRACK_SET|STATUS_SET|MODE_SET|HERR_SET|VERR_SET|SPEEDERR_SET;
}

static int handle1002(struct gps_device_t *session)
{
    int i, j, status, prn;

    session->gpsdata.satellites_used = 0;
    memset(session->gpsdata.used,0,sizeof(session->gpsdata.used));
    /* ticks                      = getl(6); */
    /* sequence                   = getw(8); */
    /* measurement_sequence       = getw(9); */
    /* gps_week                   = getw(10); */
    /* gps_seconds                = getl(11); */
    /* gps_nanoseconds            = getl(13); */
    for (i = 0; i < MAXCHANNELS; i++) {
	session->Zv[i] = status = getw(15 + (3 * i));
	session->Zs[i] = prn = getw(16 + (3 * i));
#if 0
	gpsd_report(1, "Sat%02d:\n", i);
	gpsd_report(1, " used:%d\n", (status & 1) ? 1 : 0);
	gpsd_report(1, " eph:%d\n", (status & 2) ? 1 : 0);
	gpsd_report(1, " val:%d\n", (status & 4) ? 1 : 0);
	gpsd_report(1, " dgps:%d\n", (status & 8) ? 1 : 0);
	gpsd_report(1, " PRN:%d\n", prn);
	gpsd_report(1, " C/No:%d\n", getw(17 + (3 * i)));
#endif
	if (status & 1)
	    session->gpsdata.used[session->gpsdata.satellites_used++] = prn;
	for (j = 0; j < MAXCHANNELS; j++) {
	    if (session->gpsdata.PRN[j] != prn)
		continue;
	    session->gpsdata.ss[j] = getw(17 + (3 * i));
	    break;
	}
    }
    return SATELLITE_SET;
}

static int handle1003(struct gps_device_t *session)
{
    int i;

    /* ticks              = getl(6); */
    /* sequence           = getw(8); */
    session->gpsdata.gdop = getw(9) * 1e-2;
    session->gpsdata.pdop = getw(10) * 1e-2;
    session->gpsdata.hdop = getw(11) * 1e-2;
    session->gpsdata.vdop = getw(12) * 1e-2;
    session->gpsdata.tdop = getw(13) * 1e-2;
    session->gpsdata.satellites = getw(14);

    for (i = 0; i < MAXCHANNELS; i++) {
	if (i < session->gpsdata.satellites) {
	    session->gpsdata.PRN[i] = getw(15 + (3 * i));
	    session->gpsdata.azimuth[i] = ((short)getw(16 + (3 * i))) * RAD_2_DEG * 1e-4;
	    if (session->gpsdata.azimuth[i] < 0)
		session->gpsdata.azimuth[i] += 360;
	    session->gpsdata.elevation[i] = ((short)getw(17 + (3 * i))) * RAD_2_DEG * 1e-4;
#if 0
	    gpsd_report(1, "Sat%02d:  PRN:%d az:%d el:%d\n", 
			i, getw(15+(3 * i)),getw(16+(3 * i)),getw(17+(3 * i)));
#endif
	} else {
	    session->gpsdata.PRN[i] = 0;
	    session->gpsdata.azimuth[i] = 0;
	    session->gpsdata.elevation[i] = 0;
	}
    }
    return SATELLITE_SET | HDOP_SET | VDOP_SET | PDOP_SET;
}

static void handle1005(struct gps_device_t *session UNUSED)
{
    /* ticks              = getl(6); */
    /* sequence           = getw(8); */
#if 0
    int i, numcorrections = getw(12);

    gpsd_report(1, "Packet: %d\n", session->sn);
    gpsd_report(1, "Station bad: %d\n", (getw(9) & 1) ? 1 : 0);
    gpsd_report(1, "User disabled: %d\n", (getw(9) & 2) ? 1 : 0);
    gpsd_report(1, "Station ID: %d\n", getw(10));
    gpsd_report(1, "Age of last correction in seconds: %d\n", getw(11));
    gpsd_report(1, "Number of corrections: %d\n", getw(12));
    for (i = 0; i < numcorrections; i++) {
	gpsd_report(1, "Sat%02d:\n", getw(13+i) & 0x3f);
	gpsd_report(1, "ephemeris:%d\n", (getw(13+i) & 64) ? 1 : 0);
	gpsd_report(1, "rtcm corrections:%d\n", (getw(13+i) & 128) ? 1 : 0);
	gpsd_report(1, "rtcm udre:%d\n", (getw(13+i) & 256) ? 1 : 0);
	gpsd_report(1, "sat health:%d\n", (getw(13+i) & 512) ? 1 : 0);
	gpsd_report(1, "rtcm sat health:%d\n", (getw(13+i) & 1024) ? 1 : 0);
	gpsd_report(1, "corrections state:%d\n", (getw(13+i) & 2048) ? 1 : 0);
	gpsd_report(1, "iode mismatch:%d\n", (getw(13+i) & 4096) ? 1 : 0);
    }
#endif
}

static void handle1108(struct gps_device_t *session)
{
    /* ticks              = getl(6); */
    /* sequence           = getw(8); */
    /* utc_week_seconds   = getl(14); */
    /* leap_nanoseconds   = getl(17); */
    if ((getw(19) & 3) == 3)
	session->context->leap_seconds = getw(16);
#if 0
    gpsd_report(1, "Leap seconds: %d.%09d\n", getw(16), getl(17));
    gpsd_report(1, "UTC validity: %d\n", getw(19) & 3);
#endif
}

static int zodiac_analyze(struct gps_device_t *session)
{
    char buf[BUFSIZ];
    int i, mask = 0;
    unsigned int id = (session->outbuffer[3] << 8) | session->outbuffer[2];

    if (session->packet_type != ZODIAC_PACKET) {
	gpsd_report(2, "zodiac_analyze packet type %d\n",session->packet_type);
	return 0;
    }

    buf[0] = '\0';
    for (i = 0; i < session->outbuflen; i++)
	(void)snprintf(buf+strlen(buf), sizeof(buf)-strlen(buf),
		       "%02x", session->outbuffer[i]);
    (void)strcat(buf, "\n");
    gpsd_report(5, "Raw Zodiac packet type %d length %d: %s\n",id,session->outbuflen,buf);

    if (session->outbuflen < 10)
	return 0;

    (void)snprintf(session->gpsdata.tag,sizeof(session->gpsdata.tag),"%u",id);

    switch (id) {
    case 1000:
	mask = handle1000(session);
	gpsd_binary_fix_dump(session, buf, (int)sizeof(buf));
	gpsd_report(3, "<= GPS: %s", buf);
	break;
    case 1002:
	mask = handle1002(session);
	strcpy(buf, "$PRWIZCH");
	for (i = 0; i < MAXCHANNELS; i++) {
	    (void)snprintf(buf+strlen(buf),  (int)(sizeof(buf)-strlen(buf)),
			  ",%02u,%X", session->Zs[i], session->Zv[i] & 0x0f);
	}
	(void)strcat(buf, "*");
	nmea_add_checksum(buf);
	gpsd_raw_hook(session, buf, strlen(buf),  1);
	gpsd_binary_quality_dump(session, 
				 buf+strlen(buf), 
				 (int)(sizeof(buf)-strlen(buf)));
	gpsd_report(3, "<= GPS: %s", buf);
	break;
    case 1003:
	mask = handle1003(session);
	gpsd_binary_satellite_dump(session, buf, sizeof(buf));
	gpsd_report(3, "<= GPS: %s", buf);
	break;	
    case 1005:
	handle1005(session);
	break;	
    case 1108:
	handle1108(session);
	break;
    }

    return mask;
}

/* caller needs to specify a wrapup function */

/* this is everything we export */
struct gps_type_t zodiac_binary =
{
    "Zodiac binary",	/* full name of type */
    NULL,		/* no probe */
    NULL,		/* only switched to by some other driver */
    NULL,		/* no initialization */
    packet_get,		/* how to get a packet */
    zodiac_analyze,	/* read and parse message packets */
    zodiac_send_rtcm,	/* send DGPS correction */
    zodiac_speed_switch,/* we can change baud rate */
    NULL,		/* no mode switcher */
    NULL,		/* caller needs to supply a close hook */
    1,			/* updates every second */
};

#endif /* ZODIAC_ENABLE */
