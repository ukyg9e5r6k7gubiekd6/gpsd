/*
 * Handle the Rockwell binary packet format supported by the EarthMate GPS.
 *
 * Everything exported from here lives in the structure earthmate at the end.
 */
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <time.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <syslog.h>
#include <unistd.h>

#include "outdata.h"
#include "nmea.h"
#include "gpsd.h"
#define BUFSIZE 4096

extern struct session_t session;

#define PI 3.14159265358979323846

enum {
    EM_HUNT_FF, EM_HUNT_81, EM_HUNT_ID, EM_HUNT_WC,
    EM_HUNT_FLAGS, EM_HUNT_CS, EM_HUNT_DATA, EM_HUNT_A
};

#define O(x) (x-6)

static unsigned short sn = 0;
static int eminit;

struct header {
    unsigned short sync;
    unsigned short id;
    unsigned short ndata;
    unsigned short flags;
    unsigned short csum;
};

static void analyze(struct header *, unsigned short *, fd_set *, fd_set *);

static unsigned short em_checksum(unsigned short *w, int n)
{
    unsigned short csum = 0;

    while (n--)
	csum += *(w++);
    csum = -csum;
    return csum;
}

/* em_spew - Takes a message type, an array of data words, and a length
   for the array, and prepends a 5 word header (including checksum).
   The data words are expected to be checksummed */

#if defined (WORDS_BIGENDIAN)

/* data is assumed to contain len/2 unsigned short words
 * we change the endianness to little, when needed.
 */
static int end_write(int fd, void *d, int len)
{
    char buf[BUFSIZE];
    char *p = buf;
    char *data = (char *)d;
    int i = len;

    while (i>0) {
	*p++ = *(data+1);
	*p++ = *data;
	data += 2;
	i -= 2;
    }
    return write(fd, buf, len);
}

#else
#define end_write write
#endif

static void em_spew(int type, unsigned short *dat, int dlen)
{
    struct header h;

    h.flags = 0;

    h.sync = 0x81ff;
    h.id = type;
    h.ndata = dlen - 1;
    h.csum = em_checksum((unsigned short *) &h, 4);

    if (session.fdout != -1) {
	end_write(session.fdout, &h, sizeof(h));
	end_write(session.fdout, dat, sizeof(unsigned short) * dlen);
    }
}

static long putlong(char *dm, int sign)
{
    double tmpl;
    long rad;

    tmpl = fabs(atof(dm));

    rad = (floor(tmpl/100) + (fmod(tmpl, 100.0)/60)) * 100000000*PI/180;

    if (sign)
	rad = -rad;

    return rad;
}

static void em_init()
{
    unsigned short data[22];
    time_t t;
    struct tm *tm;

    eminit = 0;

    if (session.initpos.latitude && session.initpos.longitude) {
      t = time(NULL);
      tm = gmtime(&t);

      if (sn++ > 32767)
	  sn = 0;
      
      memset(data, 0, sizeof(data));
      
      data[0] = sn;		/* sequence number */

      data[1] = (1 << 2) | (1 << 3);
      data[2] = data[3] = data[4] = 0;
      data[5] = tm->tm_mday;
      data[6] = tm->tm_mon + 1;
      data[7] = tm->tm_year + 1900;
      data[8] = tm->tm_hour;
      data[9] = tm->tm_min;
      data[10] = tm->tm_sec;
      *(long *) (data + 11) = putlong(session.initpos.latitude, (session.initpos.latd == 'S') ? 1 : 0);
      *(long *) (data + 13) = putlong(session.initpos.longitude, (session.initpos.lond == 'W') ? 1 : 0);
      data[15] = data[16] = 0;
      data[17] = data[18] = data[19] = data[20] = 0;
      data[21] = em_checksum(data, 21);

      em_spew(1200, data, 22);
    }
}

static void send_rtcm(char *rtcmbuf, int rtcmbytes)
{
    unsigned short data[34];
    int n = 1 + (rtcmbytes/2 + rtcmbytes%2);

    if (sn++ > 32767)
	sn = 0;

    memset(data, 0, sizeof(data));

    data[0] = sn;		/* sequence number */
    memcpy(&data[1], rtcmbuf, rtcmbytes);
    data[n] = em_checksum(data, n);

    em_spew(1351, data, n+1);
}

static int em_send_rtcm(char *rtcmbuf, int rtcmbytes)
{
    int len;

    while (rtcmbytes) {
	len = rtcmbytes>64?64:rtcmbytes;
	send_rtcm(rtcmbuf, len);
	rtcmbytes -= len;
	rtcmbuf += len;
    }
    return 1;
}


static void do_eminit()
{
    /* Make sure these are zero before 1002 handler called */
    session.gNMEAdata.pdop = session.gNMEAdata.hdop = session.gNMEAdata.vdop = 0;
    eminit = 1;
}

static long getlong(void *p)
{
    return *(long *) p;
}


static unsigned long getulong(void *p)
{
    return *(unsigned long *) p;
}


static double degtodm(double a)
{
    double d, m, t;

    d = floor(a);
    m = modf(a, &t);
    t = d * 100 + m * 60;
    return t;
}

static void handle1000(unsigned short *p)
{
#if 0
    fprintf(stderr, "date: %d %d %d  %d:%d:%d\n",
	    p[O(19)], p[O(20)], p[O(21)], p[O(22)], p[O(23)], p[O(24)]);

    fprintf(stderr, "  solution invalid:\n");
    fprintf(stderr, "    altitude: %d\n", (p[O(10)] & 1) ? 1 : 0);
    fprintf(stderr, "    no diff gps: %d\n", (p[O(10)] & 2) ? 1 : 0);
    fprintf(stderr, "    not enough satellites: %d\n", (p[O(10)] & 4) ? 1 : 0);
    fprintf(stderr, "    exceed max EHPE: %d\n", (p[O(10)] & 8) ? 1 : 0);
    fprintf(stderr, "    exceed max EVPE: %d\n", (p[O(10)] & 16) ? 1 : 0);
    fprintf(stderr, "  solution type:\n");
    fprintf(stderr, "    propagated: %d\n", (p[O(11)] & 1) ? 1 : 0);
    fprintf(stderr, "    altitude: %d\n", (p[O(11)] & 2) ? 1 : 0);
    fprintf(stderr, "    differential: %d\n", (p[O(11)] & 4) ? 1 : 0);
    fprintf(stderr, "Number of measurements in solution: %d\n", p[O(12)]);
    fprintf(stderr, "Lat: %f\n", 180.0 / (PI / ((double) getlong(p + O(27)) / 100000000)));
    fprintf(stderr, "Lon: %f\n", 180.0 / (PI / ((double) getlong(p + O(29)) / 100000000)));
    fprintf(stderr, "Alt: %f\n", (double) getlong(p + O(31)) / 100.0);
    fprintf(stderr, "Speed: %f\n", (double) getlong(p + O(34)) / 100.0) * 1.94387;
    fprintf(stderr, "Map datum: %d\n", p[O(39)]);
    fprintf(stderr, "Magnetic variation: %f\n", p[O(37)] * 180 / (PI * 10000));
    fprintf(stderr, "Course: %f\n", (p[O(36)] * 180 / (PI * 1000)));
    fprintf(stderr, "Separation: %f\n", (p[O(33)] / 100));
#endif

    sprintf(session.gNMEAdata.utc, "%02d/%02d/%d %02d:%02d:%02d",
	    p[O(19)], p[O(20)], p[O(21)], p[O(22)], p[O(23)], p[O(24)]);

    session.gNMEAdata.mag_var = p[O(37)] * 180 / (PI * 10000);	/* degrees */

    session.gNMEAdata.course = p[O(36)] * 180 / (PI * 1000);	/* degrees */

    session.gNMEAdata.satellites = p[O(12)];

    session.gNMEAdata.hours = p[O(22)];

    session.gNMEAdata.minutes = p[O(23)];

    session.gNMEAdata.seconds = p[O(24)];

    session.gNMEAdata.year = p[O(21)];

    session.gNMEAdata.month = p[O(20)];

    session.gNMEAdata.day = p[O(19)];

    session.gNMEAdata.latitude = 180.0 / (PI / ((double) getlong(p + O(27)) / 100000000));
    session.gNMEAdata.longitude = 180.0 / (PI / ((double) getlong(p + O(29)) / 100000000));
    session.gNMEAdata.speed = ((double) getulong(p + O(34)) / 100.0) * 1.94387;
    session.gNMEAdata.altitude = (double) getlong(p + O(31)) / 100.0;

    session.gNMEAdata.status = (p[O(10)] & 0x1c) ? 0 : 1;

    if (session.gNMEAdata.status) {
	session.gNMEAdata.mode = (p[O(10)] & 1) ? 2 : 3;
    } else {
	session.gNMEAdata.mode = 1;
    }
    session.gNMEAdata.ts_status = session.gNMEAdata.last_update;
    session.gNMEAdata.cmask |= C_STATUS;
    session.gNMEAdata.ts_mode = session.gNMEAdata.last_update;
    session.gNMEAdata.cmask |= C_MODE;

    session.gNMEAdata.separation = p[O(33)] / 100;	/* meters */
}

static void handle1002(unsigned short *p)
{
    int i, j;

    for (j = 0; j < 12; j++) {
	session.gNMEAdata.used[j] = 0;
    }
    for (i = 0; i < 12; i++) {
	session.gNMEAdata.Zs[i] = p[O(16 + (3 * i))];
	session.gNMEAdata.Zv[i] = (p[O(15 + (3 * i))] & 0xf);
#if 0
	fprintf(stderr, "Sat%02d:", i);
	fprintf(stderr, " used:%d", (p[O(15 + (3 * i))] & 1) ? 1 : 0);
	fprintf(stderr, " eph:%d", (p[O(15 + (3 * i))] & 2) ? 1 : 0);
	fprintf(stderr, " val:%d", (p[O(15 + (3 * i))] & 4) ? 1 : 0);
	fprintf(stderr, " dgps:%d", (p[O(15 + (3 * i))] & 8) ? 1 : 0);
	fprintf(stderr, " PRN:%d", p[O(16 + (3 * i))]);
	fprintf(stderr, " C/No:%d\n", p[O(17 + (3 * i))]);
#endif
	for (j = 0; j < 12; j++) {
	    if (session.gNMEAdata.PRN[j] != p[O(16 + (3 * i))])
		continue;
	    session.gNMEAdata.used[j] = (p[O(15 + (3 * i))] & 1);
	    session.gNMEAdata.ss[j] = p[O(17 + (3 * i))];
	    break;
	}
    }
    session.gNMEAdata.cmask |= C_ZCH;
}

static void handle1003(unsigned short *p)
{
    int j;

    session.gNMEAdata.pdop = p[O(10)];
    session.gNMEAdata.hdop = p[O(11)];
    session.gNMEAdata.vdop = p[O(12)];
    session.gNMEAdata.in_view = p[O(14)];

    for (j = 0; j < 12; j++) {
	if (j < session.gNMEAdata.in_view) {
	    session.gNMEAdata.PRN[j] = p[O(15 + (3 * j))];
	    session.gNMEAdata.azimuth[j] = p[O(16 + (3 * j))] * 180 / (PI * 10000);
	    session.gNMEAdata.elevation[j] = p[O(17 + (3 * j))] * 180 / (PI * 10000);
#if 0
	    fprintf(stderr, "Sat%02d:", i);
	    fprintf(stderr, " PRN:%d", p[O(15 + (3 * i))]);
	    fprintf(stderr, " az:%d", p[O(16 + (3 * i))]);
	    fprintf(stderr, " el:%d", p[O(17 + (3 * i))]);
	    fprintf(stderr, "\n");
#endif
	} else {
	    session.gNMEAdata.PRN[j] = 0;
	    session.gNMEAdata.azimuth[j] = 0.0;
	    session.gNMEAdata.elevation[j] = 0.0;
	}
    }
}

static void handle1005(unsigned short *p)
{
  int i;
  int numcorrections = p[O(12)];

#if 1
  fprintf(stderr, "Station bad: %d\n", (p[O(9)] & 1) ? 1 : 0);
  fprintf(stderr, "User disabled: %d\n", (p[O(9)] & 2) ? 1 : 0);
  fprintf(stderr, "Station ID: %d\n", p[O(10)]);
  fprintf(stderr, "Age of last correction in seconds: %d\n", p[O(11)]);
  fprintf(stderr, "Number of corrections: %d\n", p[O(12)]);
  for (i = 0; i < numcorrections; i++) {
    fprintf(stderr, "Sat%02d:", p[O(13+i)] & 0x3f);
    fprintf(stderr, "ephemeris:%d", (p[O(13+i)] & 64) ? 1 : 0);
    fprintf(stderr, "rtcm corrections:%d", (p[O(13+i)] & 128) ? 1 : 0);
    fprintf(stderr, "rtcm udre:%d", (p[O(13+i)] & 256) ? 1 : 0);
    fprintf(stderr, "sat health:%d", (p[O(13+i)] & 512) ? 1 : 0);
    fprintf(stderr, "rtcm sat health:%d", (p[O(13+i)] & 1024) ? 1 : 0);
    fprintf(stderr, "corrections state:%d", (p[O(13+i)] & 2048) ? 1 : 0);
    fprintf(stderr, "iode mismatch:%d", (p[O(13+i)] & 4096) ? 1 : 0);
  }
#endif
}

static void analyze(struct header *h, unsigned short *p, fd_set * afds, fd_set * nmea_fds)
{
    unsigned char buf[BUFSIZE];
    char *bufp;
    char *bufp2;
    int i = 0, j = 0, nmea = 0;

    if (p[h->ndata] == em_checksum(p, h->ndata)) {
	if (session.debug > 5)
	    fprintf(stderr, "id %d\n", h->id);
	switch (h->id) {
	case 1000:
	    handle1000(p);
	    bufp = buf;
	    if (session.gNMEAdata.mode > 1) {
		sprintf(bufp,
			"$GPGGA,%02d%02d%02d,%f,%c,%f,%c,%d,%02d,%.2f,%.1f,%c,%f,%c,%s,%s*",
		   session.gNMEAdata.hours, session.gNMEAdata.minutes, session.gNMEAdata.seconds,
			degtodm(fabs(session.gNMEAdata.latitude)),
			((session.gNMEAdata.latitude > 0) ? 'N' : 'S'),
			degtodm(fabs(session.gNMEAdata.longitude)),
			((session.gNMEAdata.longitude > 0) ? 'E' : 'W'),
		    session.gNMEAdata.mode, session.gNMEAdata.satellites, session.gNMEAdata.hdop,
			session.gNMEAdata.altitude, 'M', session.gNMEAdata.separation, 'M', "", "");
		add_checksum(bufp + 1);
		bufp = bufp + strlen(bufp);
	    }
	    sprintf(bufp,
		    "$GPRMC,%02d%02d%02d,%c,%f,%c,%f,%c,%f,%f,%02d%02d%02d,%02f,%c*",
		    session.gNMEAdata.hours, session.gNMEAdata.minutes, session.gNMEAdata.seconds,
		    session.gNMEAdata.status ? 'A' : 'V', degtodm(fabs(session.gNMEAdata.latitude)),
		    ((session.gNMEAdata.latitude > 0) ? 'N' : 'S'),
		    degtodm(fabs(session.gNMEAdata.longitude)),
		((session.gNMEAdata.longitude > 0) ? 'E' : 'W'), session.gNMEAdata.speed,
		    session.gNMEAdata.course, session.gNMEAdata.day, session.gNMEAdata.month,
		    (session.gNMEAdata.year % 100), session.gNMEAdata.mag_var,
		    (session.gNMEAdata.mag_var > 0) ? 'E' : 'W');
	    add_checksum(bufp + 1);
	    nmea = 1000;
	    break;
	case 1002:
	    handle1002(p);
	    bufp2 = bufp = buf;
	    sprintf(bufp, "$GPGSA,%c,%d,", 'A', session.gNMEAdata.mode);
	    j = 0;
	    for (i = 0; i < 12; i++) {
		if (session.gNMEAdata.used[i]) {
		    bufp = bufp + strlen(bufp);
		    sprintf(bufp, "%02d,", session.gNMEAdata.PRN[i]);
		    j++;
		}
	    }
	    for (i = j; i < 12; i++) {
		bufp = bufp + strlen(bufp);
		sprintf(bufp, ",");
	    }
	    bufp = bufp + strlen(bufp);
	    sprintf(bufp, "%.2f,%.2f,%.2f*", session.gNMEAdata.pdop, session.gNMEAdata.hdop,
		    session.gNMEAdata.vdop);
	    add_checksum(bufp2 + 1);
	    bufp2 = bufp = bufp + strlen(bufp);
	    sprintf(bufp, "$PRWIZCH");
	    bufp = bufp + strlen(bufp);
	    for (i = 0; i < 12; i++) {
		sprintf(bufp, ",%02d,%X", session.gNMEAdata.Zs[i], session.gNMEAdata.Zv[i]);
		bufp = bufp + strlen(bufp);
	    }
	    sprintf(bufp, "*");
	    bufp = bufp + strlen(bufp);
	    add_checksum(bufp2 + 1);
	    nmea = 1002;
	    break;
	case 1003:
	    handle1003(p);
	    bufp2 = bufp = buf;
	    j = (session.gNMEAdata.in_view / 4) + (((session.gNMEAdata.in_view % 4) > 0) ? 1 : 0);
	    while (i < 12) {
		if (i % 4 == 0)
		    sprintf(bufp, "$GPGSV,%d,%d,%02d", j, (i / 4) + 1, session.gNMEAdata.in_view);
		bufp += strlen(bufp);
		if (i <= session.gNMEAdata.in_view && session.gNMEAdata.elevation[i])
		    sprintf(bufp, ",%02d,%02d,%03d,%02d", session.gNMEAdata.PRN[i],
			    session.gNMEAdata.elevation[i], session.gNMEAdata.azimuth[i], session.gNMEAdata.ss[i]);
		else
		    sprintf(bufp, ",%02d,00,000,%02d,", session.gNMEAdata.PRN[i],
			    session.gNMEAdata.ss[i]);
		bufp += strlen(bufp);
		if (i % 4 == 3) {
		    sprintf(bufp, "*");
		    add_checksum(bufp2 + 1);
		    bufp += strlen(bufp);
		    bufp2 = bufp;
		}
		i++;
	    }
	    nmea = 1003;
	    break;	
	case 1005:
	    handle1005(p);
	    break;	
	}
    }
    if (nmea > 0) {
	if (session.debug > 4)
	    fprintf(stderr, "%s", buf);

	send_nmea(afds, nmea_fds, buf);

    }
    if (eminit)
	em_init();
}


static int putword(unsigned short *p, unsigned char c, unsigned int n)
{
    *(((unsigned char *) p) + n) = c;
    if (n == 0)
	return 1;
    else
	return 0;
}


static void em_eat(unsigned char c, fd_set * afds, fd_set * nmea_fds)
{
    static int state = EM_HUNT_FF;
    static struct header h;

    static unsigned int byte;
    static unsigned int words;
    static unsigned short *data;

    switch (state) {

    case EM_HUNT_FF:
	if (c == 0xff)
	    state = EM_HUNT_81;
	if (c == 'E')
	    state = EM_HUNT_A;
	break;

    case EM_HUNT_A:
	/* A better be right after E */
        if ((c == 'A') && (session.fdout != -1))
	    write(session.fdout, "EARTHA\r\n", 8);
	state = EM_HUNT_FF;
	break;

    case EM_HUNT_81:
	if (c == 0x81)
	    state = EM_HUNT_ID;
	h.sync = 0x81ff;
	byte = 0;
	break;

    case EM_HUNT_ID:
	if (!(byte = putword(&(h.id), c, byte)))
	    state = EM_HUNT_WC;
	break;

    case EM_HUNT_WC:
	if (!(byte = putword(&(h.ndata), c, byte)))
	    state = EM_HUNT_FLAGS;
	break;

    case EM_HUNT_FLAGS:
	if (!(byte = putword(&(h.flags), c, byte)))
	    state = EM_HUNT_CS;
	break;

    case EM_HUNT_CS:
	if (!(byte = putword(&(h.csum), c, byte))) {

	    if (h.csum == em_checksum((unsigned short *) &h, 4)) {
		state = EM_HUNT_DATA;
		data = (unsigned short *) malloc((h.ndata + 1) * 2);
		words = 0;
	    } else
		state = EM_HUNT_FF;
	}
	break;

    case EM_HUNT_DATA:
	if (!(byte = putword(data + words, c, byte)))
	    words++;
	if (words == h.ndata + 1) {
	    analyze(&h, data, afds, nmea_fds);
	    free(data);
	    state = EM_HUNT_FF;
	}
	break;
    }
}

static int handle_EMinput(int input, fd_set * afds, fd_set * nmea_fds)
{
    unsigned char c;

    if (read(input, &c, 1) != 1)
	return 1;
    em_eat(c, afds, nmea_fds);
    return 0;
}

static void em_close(void)
{
    session.device_type = &earthmate_a;
}

/* this is everything we export */
struct gps_type_t earthmate_b =
{
    '\0',		/* cannot be explicitly selected */
    "EarthMate (b)",	/* full name of type */
    do_eminit,		/* initialize the device */
    handle_EMinput,	/* read and parse message packets */
    em_send_rtcm,	/* send DGPS correction */
    em_close,		/* wrapup function to be called on close */
    9600,		/* 4800 won't work */
};

