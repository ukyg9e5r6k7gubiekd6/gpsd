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

#if ZODIAC_ENABLE
enum {
    ZODIAC_HUNT_FF, ZODIAC_HUNT_81, ZODIAC_HUNT_ID, ZODIAC_HUNT_WC,
    ZODIAC_HUNT_FLAGS, ZODIAC_HUNT_CS, ZODIAC_HUNT_DATA, ZODIAC_HUNT_A
};

#define O(x) (x-6)

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
   for the array, and prepends a 5 word header (including nmea_checksum).
   The data words are expected to be nmea_checksummed */
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

    if (session->gNMEAdata.gps_fd != -1) {
	end_write(session->gNMEAdata.gps_fd, &h, sizeof(h));
	end_write(session->gNMEAdata.gps_fd, dat, sizeof(unsigned short) * dlen);
    }
}

static long putlong(char *dm, int sign)
{
    double tmpl;
    long rad;

    tmpl = fabs(atof(dm));
    rad = (floor(tmpl/100) + (fmod(tmpl, 100.0)/60)) * 100000000*DEG_2_RAD;
    if (sign)
	rad = -rad;
    return rad;
}

static void zodiac_init(struct gps_session_t *session)
{
    unsigned short data[22];
    time_t t;
    struct tm tm;

    if (session->latitude && session->longitude) {
      t = time(NULL);
      gmtime_r(&t, &tm);

      if (session->sn++ > 32767)
	  session->sn = 0;
      
      memset(data, 0, sizeof(data));
      data[0] = session->sn;		/* sequence number */
      data[1] = (1 << 2) | (1 << 3);
      data[2] = data[3] = data[4] = 0;
      data[5] = tm.tm_mday; data[6] = tm.tm_mon+1; data[7]= tm.tm_year+1900; 
      data[8] = tm.tm_hour; data[9] = tm.tm_min; data[10] = tm.tm_sec;
      *(long *) (data + 11) = putlong(session->latitude, (session->latd == 'S') ? 1 : 0);
      *(long *) (data + 13) = putlong(session->longitude, (session->lond == 'W') ? 1 : 0);
      data[15] = data[16] = 0;
      data[17] = data[18] = data[19] = data[20] = 0;
      data[21] = zodiac_checksum(data, 21);

      zodiac_spew(session, 1200, data, 22);
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
#ifdef UNRELIABLE_SYNC
    gpsd_drain(session->gNMEAdata.gps_fd);
#endif /* UNRELIABLE_SYNC */
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

static long getlong(void *p)
{
    return *(long *) p;
}

static unsigned long getulong(void *p)
{
    return *(unsigned long *) p;
}

static void handle1000(struct gps_session_t *session, unsigned short *p)
{
    sprintf(session->gNMEAdata.utc, "%04d/%02d/%dT%02d:%02d:%02dZ",
	    p[O(19)], p[O(20)], p[O(21)], p[O(22)], p[O(23)], p[O(24)]);

#if 0
    gpsd_report(1, "date: %s\n", session->gNMEAdata.utc);
    gpsd_report(1, "  solution invalid:\n");
    gpsd_report(1, "    altitude: %d\n", (p[O(10)] & 1) ? 1 : 0);
    gpsd_report(1, "    no diff gps: %d\n", (p[O(10)] & 2) ? 1 : 0);
    gpsd_report(1, "    not enough satellites: %d\n", (p[O(10)] & 4) ? 1 : 0);
    gpsd_report(1, "    exceed max EHPE: %d\n", (p[O(10)] & 8) ? 1 : 0);
    gpsd_report(1, "    exceed max EVPE: %d\n", (p[O(10)] & 16) ? 1 : 0);
    gpsd_report(1, "  solution type:\n");
    gpsd_report(1, "    propagated: %d\n", (p[O(11)] & 1) ? 1 : 0);
    gpsd_report(1, "    altitude: %d\n", (p[O(11)] & 2) ? 1 : 0);
    gpsd_report(1, "    differential: %d\n", (p[O(11)] & 4) ? 1 : 0);
    gpsd_report(1, "Number of measurements in solution: %d\n", p[O(12)]);
    gpsd_report(1, "Lat: %f\n", 180.0 / (PI / ((double) getlong(p + O(27)) / 100000000)));
    gpsd_report(1, "Lon: %f\n", 180.0 / (PI / ((double) getlong(p + O(29)) / 100000000)));
    gpsd_report(1, "Alt: %f\n", (double) getlong(p + O(31)) / 100.0);
    gpsd_report(1, "Speed: %f\n", (double) getlong(p + O(34)) / 100.0) * 1.94387;
    gpsd_report(1, "Map datum: %d\n", p[O(39)]);
    gpsd_report(1, "Magnetic variation: %f\n", p[O(37)] * 180 / (PI * 10000));
    gpsd_report(1, "Course: %f\n", (p[O(36)] * 180 / (PI * 1000)));
    gpsd_report(1, "Separation: %f\n", (p[O(33)] / 100));
#endif

    session->hours = p[O(22)]; 
    session->minutes = p[O(23)]; 
    session->seconds = p[O(24)];
    session->year = p[O(21)];
    session->month = p[O(20)];
    session->day = p[O(19)];

    session->gNMEAdata.latitude = 180.0 / (PI / ((double) getlong(p + O(27)) / 100000000));
    session->gNMEAdata.longitude = 180.0 / (PI / ((double) getlong(p + O(29)) / 100000000));
    session->gNMEAdata.speed = ((double) getulong(p + O(34)) / 100.0) * 1.94387;
    session->gNMEAdata.altitude = (double) getlong(p + O(31)) / 100.0;
    session->gNMEAdata.status = (p[O(10)] & 0x1c) ? 0 : 1;
    session->mag_var = p[O(37)] * 180 / (PI * 10000);	/* degrees */
    session->gNMEAdata.track = p[O(36)] * 180 / (PI * 1000);	/* degrees */
    session->gNMEAdata.satellites_used = p[O(12)];

    if (session->gNMEAdata.status)
	session->gNMEAdata.mode = (p[O(10)] & 1) ? 2 : 3;
    else
	session->gNMEAdata.mode = 1;
    REFRESH(session->gNMEAdata.status_stamp);
    REFRESH(session->gNMEAdata.mode_stamp);

    session->separation = p[O(33)] / 100;	/* meters */
}

static void handle1002(struct gps_session_t *session, unsigned short *p)
{
    int i, j;

    for (j = 0; j < MAXCHANNELS; j++)
	session->gNMEAdata.used[j] = 0;
    session->gNMEAdata.satellites_used = 0;
    for (i = 0; i < MAXCHANNELS; i++) {
	session->Zs[i] = p[O(16 + (3 * i))];
	session->Zv[i] = (p[O(15 + (3 * i))] & 0xf);
#if 0
	gpsd_report(1, "Sat%02d:", i);
	gpsd_report(1, " used:%d", (p[O(15 + (3 * i))] & 1) ? 1 : 0);
	gpsd_report(1, " eph:%d", (p[O(15 + (3 * i))] & 2) ? 1 : 0);
	gpsd_report(1, " val:%d", (p[O(15 + (3 * i))] & 4) ? 1 : 0);
	gpsd_report(1, " dgps:%d", (p[O(15 + (3 * i))] & 8) ? 1 : 0);
	gpsd_report(1, " PRN:%d", p[O(16 + (3 * i))]);
	gpsd_report(1, " C/No:%d\n", p[O(17 + (3 * i))]);
#endif
	if (p[O(15 + (3 * i))] & 1)
	    session->gNMEAdata.used[session->gNMEAdata.satellites_used++] = i;
	for (j = 0; j < MAXCHANNELS; j++) {
	    if (session->gNMEAdata.PRN[j] != p[O(16 + (3 * i))])
		continue;
	    session->gNMEAdata.ss[j] = p[O(17 + (3 * i))];
	    break;
	}
    }
    REFRESH(session->gNMEAdata.satellite_stamp);
}

static void handle1003(struct gps_session_t *session, unsigned short *p)
{
    int j;

    session->gNMEAdata.pdop = p[O(10)];
    session->gNMEAdata.hdop = p[O(11)];
    session->gNMEAdata.vdop = p[O(12)];
    session->gNMEAdata.satellites = p[O(14)];

    for (j = 0; j < MAXCHANNELS; j++) {
	if (j < session->gNMEAdata.satellites) {
	    session->gNMEAdata.PRN[j] = p[O(15 + (3 * j))];
	    session->gNMEAdata.azimuth[j] = p[O(16 + (3 * j))] * 180 / (PI * 10000);
	    session->gNMEAdata.elevation[j] = p[O(17 + (3 * j))] * 180 / (PI * 10000);
#if 0
	    gpsd_report(1, "Sat%02d:  PRN:%d az:%d el:%d\n", 
			i, p[O(15+(3*i))], p[O(16+(3*i))], p[O(17+(3*i))]);
#endif
	} else {
	    session->gNMEAdata.PRN[j] = 0;
	    session->gNMEAdata.azimuth[j] = 0.0;
	    session->gNMEAdata.elevation[j] = 0.0;
	}
    }
    REFRESH(session->gNMEAdata.fix_quality_stamp);
    REFRESH(session->gNMEAdata.satellite_stamp);
}

static void handle1005(struct gps_session_t *session, unsigned short *p)
{
    int i, numcorrections = p[O(12)];

    gpsd_report(1, "Packet: %d\n", session->sn);
    gpsd_report(1, "Station bad: %d\n", (p[O(9)] & 1) ? 1 : 0);
    gpsd_report(1, "User disabled: %d\n", (p[O(9)] & 2) ? 1 : 0);
    gpsd_report(1, "Station ID: %d\n", p[O(10)]);
    gpsd_report(1, "Age of last correction in seconds: %d\n", p[O(11)]);
    gpsd_report(1, "Number of corrections: %d\n", p[O(12)]);
    for (i = 0; i < numcorrections; i++) {
	gpsd_report(1, "Sat%02d:", p[O(13+i)] & 0x3f);
	gpsd_report(1, "ephemeris:%d", (p[O(13+i)] & 64) ? 1 : 0);
	gpsd_report(1, "rtcm corrections:%d", (p[O(13+i)] & 128) ? 1 : 0);
	gpsd_report(1, "rtcm udre:%d", (p[O(13+i)] & 256) ? 1 : 0);
	gpsd_report(1, "sat health:%d", (p[O(13+i)] & 512) ? 1 : 0);
	gpsd_report(1, "rtcm sat health:%d", (p[O(13+i)] & 1024) ? 1 : 0);
	gpsd_report(1, "corrections state:%d", (p[O(13+i)] & 2048) ? 1 : 0);
	gpsd_report(1, "iode mismatch:%d", (p[O(13+i)] & 4096) ? 1 : 0);
    }
}

static void analyze(struct gps_session_t *session, 
		    struct header *h, unsigned short *p)
{
    char buf[BUFSIZ];
    int i = 0;

    if (p[h->ndata] == zodiac_checksum(p, h->ndata)) {
	gpsd_report(5, "id %d\n", h->id);
	switch (h->id) {
	case 1000:
	    handle1000(session, p);
	    gpsd_binary_fix_dump(session, buf);
	    break;
	case 1002:
	    handle1002(session, p);
	    gpsd_binary_quality_dump(session, buf);
	    sprintf(buf+strlen(buf), "$PRWIZCH");
	    for (i = 0; i < MAXCHANNELS; i++) {
		sprintf(buf+strlen(buf), ",%02d,%X", session->Zs[i], session->Zv[i]);
	    }
	    strcat(buf, "*");
	    nmea_add_checksum(buf);
	    break;
	case 1003:
	    handle1003(session, p);
	    gpsd_binary_satellite_dump(session, buf);
	    break;	
	case 1005:
	    handle1005(session, p);
	    return;	
	}
    }
    gpsd_report(4, "%s", buf);
    if (session->gNMEAdata.raw_hook)
	session->gNMEAdata.raw_hook(buf);
}

static int putword(unsigned short *p, unsigned char c, unsigned int n)
{
    *(((unsigned char *) p) + n) = c;
    return (n == 0);
}

static void zodiac_handle_input(struct gps_session_t *session)
{
    unsigned char c;

    if (read(session->gNMEAdata.gps_fd, &c, 1) == 1)
    {
	static int state = ZODIAC_HUNT_FF;
	static struct header h;
	static unsigned int byte, words;
	static unsigned short *data;

	switch (state) {
	case ZODIAC_HUNT_FF:
	    if (c == 0xff)
		state = ZODIAC_HUNT_81;
	    if (c == 'E')
		state = ZODIAC_HUNT_A;
	    break;
	case ZODIAC_HUNT_A:
	    /* A better be right after E */
	    if ((c == 'A') && (session->gNMEAdata.gps_fd != -1))
		write(session->gNMEAdata.gps_fd, "EARTHA\r\n", 8);
	    state = ZODIAC_HUNT_FF;
	    break;
	case ZODIAC_HUNT_81:
	    if (c == 0x81)
		state = ZODIAC_HUNT_ID;
	    h.sync = 0x81ff;
	    byte = 0;
	    break;
	case ZODIAC_HUNT_ID:
	    if (!(byte = putword(&(h.id), c, byte)))
		state = ZODIAC_HUNT_WC;
	    break;
	case ZODIAC_HUNT_WC:
	    if (!(byte = putword(&(h.ndata), c, byte)))
		state = ZODIAC_HUNT_FLAGS;
	    break;
	case ZODIAC_HUNT_FLAGS:
	    if (!(byte = putword(&(h.flags), c, byte)))
		state = ZODIAC_HUNT_CS;
	    break;
	case ZODIAC_HUNT_CS:
	    if (!(byte = putword(&(h.csum), c, byte))) {
		if (h.csum == zodiac_checksum((unsigned short *) &h, 4)) {
		    state = ZODIAC_HUNT_DATA;
		    data = (unsigned short *) malloc((h.ndata + 1) * 2);
		    words = 0;
		} else
		    state = ZODIAC_HUNT_FF;
	    }
	    break;
	case ZODIAC_HUNT_DATA:
	    if (!(byte = putword(data + words, c, byte)))
		words++;
	    if (words == (unsigned int)h.ndata + 1) {
		analyze(session, &h, data);
		free(data);
		state = ZODIAC_HUNT_FF;
	    }
	    break;
	}
    }
}

/* caller needs to specify a wrapup function */

/* this is everything we export */
struct gps_type_t zodiac_binary =
{
    "Zodiac binary",	/* full name of type */
    NULL,		/* no probe */
    NULL,		/* only switched to by some other driver */
    zodiac_init,	/* initialize the device */
    zodiac_handle_input,/* read and parse message packets */
    zodiac_send_rtcm,	/* send DGPS correction */
    zodiac_speed_switch,/* we can change baud rate */
    NULL,		/* caller needs to supply a close hook */
    1,			/* updates every second */
};

#endif /* ZODIAC_ENABLE */
