/*
 * Handle the Rockwell binary packet format supported by the old Zodiac chipset
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define __USE_ISOC99	1	/* needed to get log2() from math.h */
#include <math.h>

#include "config.h"
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

    while (n--)
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

static void zodiac_spew(struct gps_session_t *session, int type, unsigned short *dat, int dlen)
{
    struct header h;

    h.flags = 0;
    h.sync = 0x81ff;
    h.id = type;
    h.ndata = dlen - 1;
    h.csum = zodiac_checksum((unsigned short *) &h, 4);

    if (session->gpsdata.gps_fd != -1) {
	end_write(session->gpsdata.gps_fd, &h, sizeof(h));
	end_write(session->gpsdata.gps_fd, dat, sizeof(unsigned short) * dlen);
    }
}

static int zodiac_speed_switch(struct gps_session_t *session, int speed)
{
    unsigned short data[21];

    if (session->sn++ > 32767)
	session->sn = 0;
      
    memset(data, 0, sizeof(data));
    data[0] = session->sn;		/* sequence number */
    data[1] = 1;			/* port 1 data valid */
    data[8] = 8;			/* port 1 character width */
    data[9] = 1;			/* port 1 stop bits */
    data[10] = 0;			/* port 1 parity */
    data[11] = (short)log2(speed/300)+1;	/* port 1 speed */
    data[12] = data[13] = data[14] = data[15] = 0;
    data[16] = data[17] = data[18] = data[19] = 0;
    data[20] = zodiac_checksum(data, 20);

    zodiac_spew(session, 1330, data, 21);

    return speed;	/* it would be nice to error-check this */
}

static void send_rtcm(struct gps_session_t *session, 
		      char *rtcmbuf, int rtcmbytes)
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

static int zodiac_send_rtcm(struct gps_session_t *session,
			char *rtcmbuf, int rtcmbytes)
{
    int len;

    while (rtcmbytes) {
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

static int handle1000(struct gps_session_t *session)
{
#if 0
    gpsd_report(1, "date: %%lf\n", session->gpsdata.fix.time);
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

    session->gpsdata.fix.time      = gpstime_to_unix(getw(14), 
						     getl(15)+getl(17)*1e-9);
    session->gpsdata.fix.latitude  = getl(27) * RAD_2_DEG * 1e-8;
    session->gpsdata.fix.longitude = getl(29) * RAD_2_DEG * 1e-8;
    session->gpsdata.fix.speed     = getl(34) * 1e-2 * MPS_TO_KNOTS;
    session->gpsdata.fix.altitude  = getl(31) * 1e-2;
    session->gpsdata.fix.climb     = getl(38) * 1e-2;
    session->gpsdata.fix.eph       = getl(40) * 1e-2;
    session->gpsdata.fix.epv       = getl(42) * 1e-2;
    session->gpsdata.fix.eps       = getl(46) * 1e-2;
    session->gpsdata.status        = (getw(10) & 0x1c) ? 0 : 1;
    session->mag_var               = getw(37) * RAD_2_DEG * 1e-4;
    session->gpsdata.fix.track     = getw(36) * RAD_2_DEG * 1e-4;
    session->gpsdata.satellites_used = getw(12);

    if (session->gpsdata.status)
	session->gpsdata.fix.mode = (getw(10) & 1) ? MODE_2D : MODE_3D;
    else
	session->gpsdata.fix.mode = MODE_NO_FIX;
    session->separation = getw(33) * 1e-2;	/* meters */

    session->gpsdata.sentence_length = 55;
    strcpy(session->gpsdata.tag, "1000");
    return TIME_SET|LATLON_SET||ALTITUDE_SET|CLIMB_SET|SPEED_SET|TRACK_SET|STATUS_SET|MODE_SET|HERR_SET|VERR_SET|SPEEDERR_SET;
}

static int handle1002(struct gps_session_t *session)
{
    int i, j;

    for (j = 0; j < MAXCHANNELS; j++)
	session->gpsdata.used[j] = 0;
    session->gpsdata.satellites_used = 0;
    for (i = 0; i < MAXCHANNELS; i++) {
	session->Zs[i] = getw(16 + (3 * i));
	session->Zv[i] = (getw(17 + (3 * i)) & 0xf);
#if 0
	gpsd_report(1, "Sat%02d:", i);
	gpsd_report(1, " used:%d", (getw(15 + (3 * i)) & 1) ? 1 : 0);
	gpsd_report(1, " eph:%d", (getw(15 + (3 * i)) & 2) ? 1 : 0);
	gpsd_report(1, " val:%d", (getw(15 + (3 * i)) & 4) ? 1 : 0);
	gpsd_report(1, " dgps:%d", (getw(15 + (3 * i)) & 8) ? 1 : 0);
	gpsd_report(1, " PRN:%d", getw(16 + (3 * i)));
	gpsd_report(1, " C/No:%d\n", getw(17 + (3 * i)));
#endif
	if (getw(15 + (3 * i)) & 1)
	    session->gpsdata.used[session->gpsdata.satellites_used++] = i;
	for (j = 0; j < MAXCHANNELS; j++) {
	    if (session->gpsdata.PRN[j] != getw(16 + (3 * i)))
		continue;
	    session->gpsdata.ss[j] = getw(17 + (3 * i));
	    break;
	}
    }
    return SATELLITE_SET;
}

static int handle1003(struct gps_session_t *session)
{
    int i;

    session->gpsdata.pdop = getw(10);
    session->gpsdata.hdop = getw(11);
    session->gpsdata.vdop = getw(12);
    session->gpsdata.satellites = getw(14);

    for (i = 0; i < MAXCHANNELS; i++) {
	if (i < session->gpsdata.satellites) {
	    session->gpsdata.PRN[i] = getw(15 + (3 * i));
	    session->gpsdata.azimuth[i] = getw(16 + (3 * i)) * RAD_2_DEG * 1e-4;
	    session->gpsdata.elevation[i] = getw(17 + (3 * i)) * RAD_2_DEG * 1e-4;
#if 0
	    gpsd_report(1, "Sat%02d:  PRN:%d az:%d el:%d\n", 
			i, getw(15+(3 * i)),getw(16+(3 * i)),getw(17+(3 * i)));
#endif
	} else {
	    session->gpsdata.PRN[i] = 0;
	    session->gpsdata.azimuth[i] = 0.0;
	    session->gpsdata.elevation[i] = 0.0;
	}
    }
    return SATELLITE_SET | HDOP_SET | VDOP_SET | PDOP_SET;
}

static void handle1005(struct gps_session_t *session)
{
    int i, numcorrections = getw(12);

    gpsd_report(1, "Packet: %d\n", session->sn);
    gpsd_report(1, "Station bad: %d\n", (getw(9) & 1) ? 1 : 0);
    gpsd_report(1, "User disabled: %d\n", (getw(9) & 2) ? 1 : 0);
    gpsd_report(1, "Station ID: %d\n", getw(10));
    gpsd_report(1, "Age of last correction in seconds: %d\n", getw(11));
    gpsd_report(1, "Number of corrections: %d\n", getw(12));
    for (i = 0; i < numcorrections; i++) {
	gpsd_report(1, "Sat%02d:", getw(13+i) & 0x3f);
	gpsd_report(1, "ephemeris:%d", (getw(13+i) & 64) ? 1 : 0);
	gpsd_report(1, "rtcm corrections:%d", (getw(13+i) & 128) ? 1 : 0);
	gpsd_report(1, "rtcm udre:%d", (getw(13+i) & 256) ? 1 : 0);
	gpsd_report(1, "sat health:%d", (getw(13+i) & 512) ? 1 : 0);
	gpsd_report(1, "rtcm sat health:%d", (getw(13+i) & 1024) ? 1 : 0);
	gpsd_report(1, "corrections state:%d", (getw(13+i) & 2048) ? 1 : 0);
	gpsd_report(1, "iode mismatch:%d", (getw(13+i) & 4096) ? 1 : 0);
    }
}

static int zodiac_analyze(struct gps_session_t *session)
{
    char buf[BUFSIZ];
    int i, mask = 0;
    unsigned int id = (session->outbuffer[2] << 8) | session->outbuffer[3];

    gpsd_report(5, "ID %d\n", id);
    switch (id) {
    case 1000:
	mask = handle1000(session);
	gpsd_binary_fix_dump(session, buf);
	break;
    case 1002:
	mask = handle1002(session);
	sprintf(buf, "$PRWIZCH");
	for (i = 0; i < MAXCHANNELS; i++) {
	    sprintf(buf+strlen(buf), ",%02d,%X", session->Zs[i], session->Zv[i]);
	}
	strcat(buf, "*");
	nmea_add_checksum(buf);
	gpsd_binary_quality_dump(session, buf+strlen(buf));
	break;
    case 1003:
	mask = handle1003(session);
	gpsd_binary_satellite_dump(session, buf);
	break;	
    case 1005:
	handle1005(session);
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
