#include "config.h"
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "gpsd.h"
#include "nmea.h"

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

    while (n--) {
	csum += *(w++);
    }
    csum = -csum;
    return csum;
}

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

