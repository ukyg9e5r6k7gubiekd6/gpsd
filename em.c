#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <netinet/in.h>
#include "gpsd.h"
#include "nmea.h"

#define PI 3.14159265358979323846

enum {
    EM_HUNT_FF, EM_HUNT_81, EM_HUNT_ID, EM_HUNT_WC, EM_HUNT_FLAGS, EM_HUNT_CS, EM_HUNT_DATA
};

#define O(x) (x-6)

struct header {
    unsigned short sync;
    unsigned short id;
    unsigned short ndata;
    unsigned short flags;
    unsigned short csum;
};

unsigned short em_checksum(unsigned short *w, int n)
{
    unsigned short csum = 0;

    while (n--) csum += *(w++);
    csum = -csum;
    return csum;
}

#ifdef TONMEA
em_tonmea()
{
    struct header h;
    unsigned short data[5];
    static sn;

    if (sn > 32767)
	sn = 0;

    h.sync = 0x81ff;
    h.id = 1331;
    h.ndata = 3;
    h.flags = 0;
    h.csum = em_checksum(&h, 4);

    data[0] = sn;		// sequence number
    data[1] = 0;		// reserved
    data[2] = 1;		// nmea protocol
    data[3] = em_checksum(data, 3);

    write(gNMEAdata.fdout, &h, sizeof(h));
    write(gNMEAdata.fdout, data, sizeof(unsigned short) * 5);
}

#endif

static long getlong(void *p)
{
    return *(long *) p;
}

static void handle1000(unsigned short *p)
{

#if 0
    fprintf(stderr, "date: %d %d %d  %d:%d:%d\n",
	    p[O(19)], p[O(20)], p[O(21)], p[O(22)], p[O(23)], p[O(24)]);

    fprintf(stderr, "  solution invalid:\n");
    fprintf(stderr, "    altitude: %d\n", (p[O(10)] & 1) ? 1 : 0);
    fprintf(stderr, "    not enought satellites: %d\n", (p[O(10)] & 4) ? 1 : 0);
    fprintf(stderr, "    exceed max EHPE: %d\n", (p[O(10)] & 8) ? 1 : 0);
    fprintf(stderr, "    exceed max EVPE: %d\n", (p[O(10)] & 16) ? 1 : 0);
    fprintf(stderr, "Number of measurements in solution: %d\n", p[O(12)]);
    fprintf(stderr, "Lat: %f\n", 180.0 / (PI / ((double) getlong(p + O(27)) / 100000000)));
    fprintf(stderr, "Lon: %f\n", 180.0 / (PI / ((double) getlong(p + O(29)) / 100000000)));
    fprintf(stderr, "Alt: %f\n", (double) getlong(p + O(31)) / 100.0);
    fprintf(stderr, "Map datum: %d\n\n", p[O(39)]);
#endif


    sprintf(gNMEAdata.utc, "%02d/%02d/%d %02d:%02d:%02d",
	    p[O(19)], p[O(20)], p[O(21)], p[O(22)], p[O(23)], p[O(24)]);

    gNMEAdata.latitude = 180.0 / (PI / ((double) getlong(p + O(27)) / 100000000));
    gNMEAdata.longitude = 180.0 / (PI / ((double) getlong(p + O(29)) / 100000000));
    gNMEAdata.speed = ((double) getlong(p + O(34)) / 100000000) * (1609.344 / 3600);
    gNMEAdata.altitude = (double) getlong(p + O(31)) / 100.0;

    gNMEAdata.status = (p[O(10)] & 0x1c) ? 0 : 1;

    if (gNMEAdata.status) {
	gNMEAdata.mode = (p[O(10)] & 1) ? 2 : 3;
    } else {
	gNMEAdata.mode = 1;
    }
}

static void handle1002(unsigned short *p)
{
}

void analyze(struct header *h, unsigned short *p)
{
    if (p[h->ndata] == em_checksum(p, h->ndata)) {
	switch (h->id) {
	case 1000:
	    handle1000(p);
	    break;

	case 1002:
	    handle1002(p);
	    break;
	}
    }
}


static int putword(unsigned short *p, unsigned char c, unsigned int n)
{
    *(((unsigned char *) p) + n) = c;
    if (n == 0)
	return 1;
    else
	return 0;
}


static void em_eat(unsigned char c)
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
	    analyze(&h, data);
	    free(data);
	    state = EM_HUNT_FF;
	}
	break;

    }

}

int handle_EMinput(int input)
{
    unsigned char c;

    if (read(input, &c, 1) != 1)
	return 1;
    em_eat(c);
    return 0;
}
