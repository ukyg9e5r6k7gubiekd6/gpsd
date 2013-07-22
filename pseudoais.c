/*
 * Encode AIS messages.
 *
 * See the file AIVDM.txt on the GPSD website for documentation and references.
 *
 * This file is build from driver_ais.c
 *
 * This file is Copyright (c) 2013 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 */
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "gpsd.h"
#include "bits.h"
#include <stdint.h>

static unsigned char convtab[] = {"0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVW`abcdefghijklmnopqrstuvw"};

static unsigned int ais_addbits(unsigned char *bits,
				unsigned int   start,
				unsigned int   len,
				uint64_t       data)
{
    unsigned int  l;
    unsigned int  pos;
    uint64_t      mask;
    unsigned char mask1;

    mask  = 0x1;
    pos   = (start+len-1) / 6;
    mask1 = 0x20;
    mask1 = mask1 >> ((start+len-1) % 6);

    if (len == 0) {
        return 0;
    }

    for(l=0;l<len;l++) {
        if (data & mask) {
	    bits[pos] |= mask1; 
	}
	mask  <<= 1;
	mask1 <<= 1;
	if (mask1 == 0x40) {
	    pos   -= 1;
	    mask1  = 0x1;
	}
    }
    return 0;
}


static unsigned int ais_binary_to_ascii(unsigned char *bits, unsigned int len)
{
    unsigned int l;

    if (len == 0) {
        bits[0] = 0;
        return 0;
    }

    for (l=0;l<len;l+=6) {
        bits[l/6] = convtab[bits[l/6] & 0x3f];
    }
    return 0;
}


unsigned int ais_binary_encode(struct ais_t *ais,
			       unsigned char *bits)
{
    unsigned int len;

    len = 0;

    ais_addbits(bits,  0,  6, ais->type);
    ais_addbits(bits,  6,  2, ais->repeat);
    ais_addbits(bits,  8, 30, ais->mmsi);
    switch (ais->type) {
    case 1:	/* Position Report */
    case 2:
    case 3:
        ais_addbits(bits,  38,  4, ais->type1.status);
        ais_addbits(bits,  42,  8, ais->type1.turn);
        ais_addbits(bits,  50, 10, ais->type1.speed);
	ais_addbits(bits,  60,  1, ais->type1.accuracy);
	ais_addbits(bits,  61, 28, ais->type1.lon);
	ais_addbits(bits,  89, 27, ais->type1.lat);
	ais_addbits(bits, 116, 12, ais->type1.course);
	ais_addbits(bits, 128,  9, ais->type1.heading);
	ais_addbits(bits, 137,  6, ais->type1.second);
	ais_addbits(bits, 143,  2, ais->type1.maneuver);
/*	ais_addbits(bits, 145,  3, ais->type1.spare); */
	ais_addbits(bits, 148,  1, ais->type1.raim);
	ais_addbits(bits, 149, 19, ais->type1.radio);
	len = 149 + 19;
	break;
    case 4: 	/* Base Station Report */
    case 11:	/* UTC/Date Response */
        ais_addbits(bits,  38, 14, ais->type4.year);
	ais_addbits(bits,  52,  4, ais->type4.month);
	ais_addbits(bits,  56,  5, ais->type4.day);
	ais_addbits(bits,  61,  5, ais->type4.hour);
	ais_addbits(bits,  66,  6, ais->type4.minute);
	ais_addbits(bits,  72,  6, ais->type4.second);
	ais_addbits(bits,  78,  1, ais->type4.accuracy);
	ais_addbits(bits,  79, 28, ais->type4.lon);
	ais_addbits(bits, 107, 27, ais->type4.lat);
	ais_addbits(bits, 134,  4, ais->type4.epfd);
/*	ais_addbits(bits, 138, 10, ais->type4.spare); */
	ais_addbits(bits, 148,  1, ais->type4.raim);
	ais_addbits(bits, 149, 19, ais->type4.radio);
	len = 149 + 19;
        break;
    case 5:     /* Ship static and voyage related data */
        break;
    case 9:     /* Standard SAR Aircraft Position Report */
        break;
    case 18:	/* Standard Class B CS Position Report */
      	ais_addbits(bits,  38,  8, ais->type18.reserved);
	ais_addbits(bits,  46, 10, ais->type18.speed);
	ais_addbits(bits,  56,  1, ais->type18.accuracy);
	ais_addbits(bits,  57, 28, ais->type18.lon);
	ais_addbits(bits,  85, 27, ais->type18.lat);
	ais_addbits(bits, 112, 12, ais->type18.course);
	ais_addbits(bits, 124,  9, ais->type18.heading);
	ais_addbits(bits, 133,  6, ais->type18.second);
	ais_addbits(bits, 139,  2, ais->type18.regional);
	ais_addbits(bits, 141,  1, ais->type18.cs);
	ais_addbits(bits, 142,  1, ais->type18.display);
	ais_addbits(bits, 143,  1, ais->type18.dsc);
	ais_addbits(bits, 144,  1, ais->type18.band);
	ais_addbits(bits, 145,  1, ais->type18.msg22);
	ais_addbits(bits, 146,  1, ais->type18.assigned);
	ais_addbits(bits, 147,  1, ais->type18.raim);
	ais_addbits(bits, 148, 20, ais->type18.radio);
	len = 148 + 20;
        break;
    case 19:	/* Extended Class B CS Position Report */
        break;
    case 21:	/* Aid-to-Navigation Report */
        break;
    case 24:	/* Class B CS Static Data Report */
        break;
    case 27:	/* Long Range AIS Broadcast message */
        break;
    }
    ais_binary_to_ascii(bits, len);
    return len;
}
