#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <syslog.h>

#include "gpsd.h"
#include "nmea.h"
#define BUFSIZE 4096

extern int debug;

#define PI 3.14159265358979323846
extern char *latitude, *longitude;
extern char latd, lond;

enum {
    EM_HUNT_FF, EM_HUNT_81, EM_HUNT_ID, EM_HUNT_WC,
    EM_HUNT_FLAGS, EM_HUNT_CS, EM_HUNT_DATA
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

unsigned short rockwell_checksum(unsigned short *w, int n)
{
    unsigned short csum = 0;

    while (n--)
	csum += *(w++);
    csum = -csum;
    return csum;
}

static long rockwell_decode_long(void *p)
{
    return *(long *) p;
}

static unsigned long rockwell_decode_ulong(void *p)
{
    return *(unsigned long *) p;
}

static long rockwell_encode_signed_long(char *dm, int sign)
{
    double tmpl;
    long rad;

    tmpl = fabs(atof(dm));

    rad = (floor(tmpl/100) + (fmod(tmpl, 100.0)/60)) * 100000000*PI/180;

    if (sign)
	rad = -rad;

    return rad;
}

static long rockwell_encode_long(char *dm, int sign)
{
    double tmpl;
    long rad;

    tmpl = fabs(atof(dm));

    rad = (floor(tmpl/100) + (fmod(tmpl, 100.0)/60)) * 100000000*PI/180;

    if (sign)
	rad = -rad;

    return rad;
}

/* em_spew - Takes a message type, an array of data words, and a length
   for the array, and prepends a 5 word header (including checksum).
   The data words are expected to be checksummed */

static void em_spew(int type, void *dat, int dlen)
{
    struct header h;

    h.flags = 0;

    h.sync = 0x81ff;
    h.id = type;
    h.ndata = dlen - 1;
    h.csum = rockwell_checksum((unsigned short *) &h, 4);

    write(gNMEAdata.fdout, &h, sizeof(h));
    write(gNMEAdata.fdout, dat, sizeof(unsigned short) * dlen);
}

static void em_init(char *latitude, char *longitude, char latd, char lond)
{
    unsigned short data[22];
    time_t t;
    struct tm *tm;

    eminit = 0;

    t = time(NULL);
    tm = gmtime(&t);

    if (sn++ > 32767)
	sn = 0;

    memset(data, 0, sizeof(data));

    data[0] = sn;		// sequence number

    data[1] = (1 << 2) | (1 << 3);
    data[2] = data[3] = data[4] = 0;
    data[5] = tm->tm_mday;
    data[6] = tm->tm_mon + 1;
    data[7] = tm->tm_year + 1900;
    data[8] = tm->tm_hour;
    data[9] = tm->tm_min;
    data[10] = tm->tm_sec;
    *(long *) (data + 11) = rockwell_encode_signed_long(latitude, (latd == 'S') ? 1 : 0);
    *(long *) (data + 13) = rockwell_encode_signed_long(longitude, (lond == 'W') ? 1 : 0);
    data[15] = data[16] = 0;
    data[17] = data[18] = data[19] = data[20] = 0;
    data[21] = rockwell_checksum(data, 21);

    em_spew(1200, &data, 22);
}

void em_send_rtcm(unsigned short *rtcmbuf, int rtcmbytes)
{
    unsigned short data[34];
    int n = 1 + (rtcmbytes/2 + rtcmbytes%2);

    if (sn++ > 32767)
	sn = 0;

    memset(data, 0, sizeof(data));

    data[0] = sn;		// sequence number
    memcpy(&data[1], rtcmbuf, rtcmbytes*(sizeof(char)));
    data[n] = rockwell_checksum(data, n);

    em_spew(1351, &data, n+1);
}

void do_eminit()
{
    /* Make sure these are zero before 1002 handler called */
    gNMEAdata.pdop = gNMEAdata.hdop = gNMEAdata.vdop = 0;
    eminit = 1;
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
    fprintf(stderr, "Lat: %f\n", 180.0 / (PI / ((double) rockwell_decode_long(p + O(27)) / 100000000)));
    fprintf(stderr, "Lon: %f\n", 180.0 / (PI / ((double) rockwell_decode_long(p + O(29)) / 100000000)));
    fprintf(stderr, "Alt: %f\n", (double) rockwell_decode_long(p + O(31)) / 100.0);
    fprintf(stderr, "Speed: %f\n", (double) rockwell_decode_long(p + O(34)) / 100.0) * 1.94387;
    fprintf(stderr, "Map datum: %d\n", p[O(39)]);
    fprintf(stderr, "Magnetic variation: %f\n", p[O(37)] * 180 / (PI * 10000));
    fprintf(stderr, "Course: %f\n", (p[O(36)] * 180 / (PI * 1000)));
    fprintf(stderr, "Separation: %f\n", (p[O(33)] / 100));
#endif

    sprintf(gNMEAdata.utc, "%02d/%02d/%d %02d:%02d:%02d",
	    p[O(19)], p[O(20)], p[O(21)], p[O(22)], p[O(23)], p[O(24)]);

    gNMEAdata.mag_var = p[O(37)] * 180 / (PI * 10000);	// degrees

    gNMEAdata.course = p[O(36)] * 180 / (PI * 1000);	// degrees

    gNMEAdata.satellites = p[O(12)];

    gNMEAdata.hours = p[O(22)];

    gNMEAdata.minutes = p[O(23)];

    gNMEAdata.seconds = p[O(24)];

    gNMEAdata.year = p[O(21)];

    gNMEAdata.month = p[O(20)];

    gNMEAdata.day = p[O(19)];

    gNMEAdata.latitude = 180.0 / (PI / ((double) rockwell_decode_long(p + O(27)) / 100000000));
    gNMEAdata.longitude = 180.0 / (PI / ((double) rockwell_decode_long(p + O(29)) / 100000000));
    gNMEAdata.speed = ((double) rockwell_decode_ulong(p + O(34)) / 100.0) * 1.94387;
    gNMEAdata.altitude = (double) rockwell_decode_long(p + O(31)) / 100.0;

    gNMEAdata.status = (p[O(10)] & 0x1c) ? 0 : 1;

    if (gNMEAdata.status) {
	gNMEAdata.mode = (p[O(10)] & 1) ? 2 : 3;
    } else {
	gNMEAdata.mode = 1;
    }

    gNMEAdata.separation = p[O(33)] / 100;	// meters

}

static void handle1002(unsigned short *p)
{
    int i, j, k;

    gNMEAdata.ZCHseen = 1;
    for (j = 0; j < 12; j++) {
	gNMEAdata.used[j] = 0;
    }
    for (i = 0; i < 12; i++) {
	gNMEAdata.Zs[i] = p[O(16 + (3 * i))];
	gNMEAdata.Zv[i] = (p[O(15 + (3 * i))] & 0xf);
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
	    if (gNMEAdata.PRN[j] != p[O(16 + (3 * i))])
		continue;
	    gNMEAdata.used[j] = (p[O(15 + (3 * i))] & 1);
	    gNMEAdata.ss[j] = p[O(17 + (3 * i))];
	    break;
	}
    }
}

static void handle1003(unsigned short *p)
{
    int i, j, k;
    char *bufp, *bufp2;

    gNMEAdata.pdop = p[O(10)];
    gNMEAdata.hdop = p[O(11)];
    gNMEAdata.vdop = p[O(12)];
    gNMEAdata.in_view = p[O(14)];

    for (j = 0; j < 12; j++) {
	if (j < gNMEAdata.in_view) {
	    gNMEAdata.PRN[j] = p[O(15 + (3 * j))];
	    gNMEAdata.azimuth[j] = p[O(16 + (3 * j))] * 180 / (PI * 10000);
	    gNMEAdata.elevation[j] = p[O(17 + (3 * j))] * 180 / (PI * 10000);
#if 0
	    fprintf(stderr, "Sat%02d:", i);
	    fprintf(stderr, " PRN:%d", p[O(15 + (3 * i))]);
	    fprintf(stderr, " az:%d", p[O(16 + (3 * i))]);
	    fprintf(stderr, " el:%d", p[O(17 + (3 * i))]);
	    fprintf(stderr, "\n");
#endif
	} else {
	    gNMEAdata.PRN[j] = 0;
	    gNMEAdata.azimuth[j] = 0.0;
	    gNMEAdata.elevation[j] = 0.0;
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
    int i = 0, j = 0, k = 0, nmea = 0;
    int fd, nfds;

    if (p[h->ndata] == rockwell_checksum(p, h->ndata)) {
	if (debug > 5)
	    fprintf(stderr, "id %d\n", h->id);
	switch (h->id) {
	case 1000:
	    handle1000(p);
	    bufp = buf;
	    if (gNMEAdata.mode > 1) {
		sprintf(bufp,
			"$GPGGA,%02d%02d%02d,%lf,%c,%lf,%c,%d,%02d,%.2f,%.1f,%c,%f,%c,%s,%s*",
		   gNMEAdata.hours, gNMEAdata.minutes, gNMEAdata.seconds,
			degtodm(fabs(gNMEAdata.latitude)),
			((gNMEAdata.latitude > 0) ? 'N' : 'S'),
			degtodm(fabs(gNMEAdata.longitude)),
			((gNMEAdata.longitude > 0) ? 'E' : 'W'),
		    gNMEAdata.mode, gNMEAdata.satellites, gNMEAdata.hdop,
			gNMEAdata.altitude, 'M', gNMEAdata.separation, 'M', "", "");
		add_checksum(bufp + 1);
		bufp = bufp + strlen(bufp);
	    }
	    sprintf(bufp,
		    "$GPRMC,%02d%02d%02d,%c,%lf,%c,%lf,%c,%f,%f,%02d%02d%02d,%02f,%c*",
		    gNMEAdata.hours, gNMEAdata.minutes, gNMEAdata.seconds,
		    gNMEAdata.status ? 'A' : 'V', degtodm(fabs(gNMEAdata.latitude)),
		    ((gNMEAdata.latitude > 0) ? 'N' : 'S'),
		    degtodm(fabs(gNMEAdata.longitude)),
		((gNMEAdata.longitude > 0) ? 'E' : 'W'), gNMEAdata.speed,
		    gNMEAdata.course, gNMEAdata.day, gNMEAdata.month,
		    (gNMEAdata.year % 100), gNMEAdata.mag_var,
		    (gNMEAdata.mag_var > 0) ? 'E' : 'W');
	    add_checksum(bufp + 1);
	    nmea = 1000;
	    break;
	case 1002:
	    handle1002(p);
	    bufp2 = bufp = buf;
	    sprintf(bufp, "$GPGSA,%c,%d,", 'A', gNMEAdata.mode);
	    j = 0;
	    for (i = 0; i < 12; i++) {
		if (gNMEAdata.used[i]) {
		    bufp = bufp + strlen(bufp);
		    sprintf(bufp, "%02d,", gNMEAdata.PRN[i]);
		    j++;
		}
	    }
	    for (i = j; i < 12; i++) {
		bufp = bufp + strlen(bufp);
		sprintf(bufp, ",");
	    }
	    bufp = bufp + strlen(bufp);
	    sprintf(bufp, "%.2f,%.2f,%.2f*", gNMEAdata.pdop, gNMEAdata.hdop,
		    gNMEAdata.vdop);
	    add_checksum(bufp2 + 1);
	    bufp2 = bufp = bufp + strlen(bufp);
	    sprintf(bufp, "$PRWIZCH");
	    bufp = bufp + strlen(bufp);
	    for (i = 0; i < 12; i++) {
		sprintf(bufp, ",%02d,%X", gNMEAdata.Zs[i], gNMEAdata.Zv[i]);
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
	    j = (gNMEAdata.in_view / 4) + (((gNMEAdata.in_view % 4) > 0) ? 1 : 0);
	    while (i < 12) {
		if (i % 4 == 0)
		    sprintf(bufp, "$GPGSV,%d,%d,%02d", j, (i / 4) + 1, gNMEAdata.in_view);
		bufp += strlen(bufp);
		if (i <= gNMEAdata.in_view && gNMEAdata.elevation[i])
		    sprintf(bufp, ",%02d,%02d,%03d,%02d", gNMEAdata.PRN[i],
			    gNMEAdata.elevation[i], gNMEAdata.azimuth[i], gNMEAdata.ss[i]);
		else
		    sprintf(bufp, ",%02d,00,000,%02d,", gNMEAdata.PRN[i],
			    gNMEAdata.ss[i]);
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
	nfds = getdtablesize();
	if (debug > 4)
	    fprintf(stderr, "%s", buf);
	for (fd = 0; fd < nfds; fd++)
	    if (FD_ISSET(fd, nmea_fds))
		if (write(fd, buf, strlen(buf)) < 0) {
		    FD_CLR(fd, afds);
		    FD_CLR(fd, nmea_fds);
		}
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

	    if (h.csum == rockwell_checksum((unsigned short *) &h, 4)) {
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

int handle_EMinput(int input, fd_set * afds, fd_set * nmea_fds)
{
    unsigned char c;

    if (read(input, &c, 1) != 1)
	return 1;
    em_eat(c, afds, nmea_fds);
    return 0;
}
