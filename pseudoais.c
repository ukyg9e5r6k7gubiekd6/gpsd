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

static unsigned char contab1[] = {0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
				  0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
				  0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,0x29,0x2a,0x2b,0x2c,0x2d,0x2e,0x2f,
				  0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x3b,0x3c,0x3d,0x3e,0x3f,
				  0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
				  0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,
				  0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
				  0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff};


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


static unsigned int ais_addchar(unsigned char *bits,
				unsigned int   start,
				unsigned int   len,
				char          *data)
{
    unsigned int l;
    unsigned int flag;
    
    for(l=0,flag=0;l<len;l++) {
        unsigned int a, b;
	
	a = data[l];
	if (a == 0) {
	    flag = 1;
	}
	if (flag == 0) {
	    b = contab1[a & 0x7f];
	} else {
	    b = 0;
	}
	ais_addbits(bits, start+6*l, 6, b); 
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
			       unsigned char *bits,
                               int flag)
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
        ais_addbits(bits,  38,  2, ais->type5.ais_version);
	ais_addbits(bits,  40, 30, ais->type5.imo);
	ais_addchar(bits,  70,  7, ais->type5.callsign);
	ais_addchar(bits, 112, 20, ais->type5.shipname);
	ais_addbits(bits, 232,  8, ais->type5.shiptype);
	ais_addbits(bits, 240,  9, ais->type5.to_bow);
	ais_addbits(bits, 249,  9, ais->type5.to_stern);
	ais_addbits(bits, 258,  6, ais->type5.to_port);
	ais_addbits(bits, 264,  6, ais->type5.to_starboard);
	ais_addbits(bits, 270,  4, ais->type5.epfd);
	ais_addbits(bits, 274,  4, ais->type5.month);
	ais_addbits(bits, 278,  5, ais->type5.day);
	ais_addbits(bits, 283,  5, ais->type5.hour);
	ais_addbits(bits, 288,  6, ais->type5.minute);
	ais_addbits(bits, 294,  8, ais->type5.draught);
	ais_addchar(bits, 302, 20, ais->type5.destination);
	ais_addbits(bits, 422,  1, ais->type5.dte);
/*      ais_addbits(bits, 423,  1, ais->type5.spare); */
	len = 423 + 1;
        break;	
    case 9:     /* Standard SAR Aircraft Position Report */
        ais_addbits(bits,  38, 12, ais->type9.alt);
	ais_addbits(bits,  50, 10, ais->type9.speed);
	ais_addbits(bits,  60,  1, ais->type9.accuracy);
	ais_addbits(bits,  61, 28, ais->type9.lon);
	ais_addbits(bits,  89, 27, ais->type9.lat);
	ais_addbits(bits, 116, 12, ais->type9.course);
	ais_addbits(bits, 128,  6, ais->type9.second);
	ais_addbits(bits, 134,  8, ais->type9.regional);
	ais_addbits(bits, 142,  1, ais->type9.dte);
/*	ais_addbits(bits, 143,  3, ais->type9.spare); */
	ais_addbits(bits, 146,  1, ais->type9.assigned);
	ais_addbits(bits, 147,  1, ais->type9.raim);
	ais_addbits(bits, 148, 19, ais->type9.radio);
	len = 148 + 19;
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
        ais_addbits(bits,  38,  8, ais->type19.reserved);
	ais_addbits(bits,  46, 10, ais->type19.speed);
	ais_addbits(bits,  56,  1, ais->type19.accuracy);
	ais_addbits(bits,  57, 28, ais->type19.lon);
	ais_addbits(bits,  85, 27, ais->type19.lat);
	ais_addbits(bits, 112, 12, ais->type19.course);
	ais_addbits(bits, 124,  9, ais->type19.heading);
	ais_addbits(bits, 133,  6, ais->type19.second);
	ais_addbits(bits, 139,  4, ais->type19.regional);
	ais_addchar(bits, 143, 20, ais->type19.shipname);
	ais_addbits(bits, 263,  8, ais->type19.shiptype);
	ais_addbits(bits, 271,  9, ais->type19.to_bow);
	ais_addbits(bits, 280,  9, ais->type19.to_stern);
	ais_addbits(bits, 289,  6, ais->type19.to_port);
	ais_addbits(bits, 295,  6, ais->type19.to_starboard);
	ais_addbits(bits, 299,  4, ais->type19.epfd);
	ais_addbits(bits, 302,  1, ais->type19.raim);
	ais_addbits(bits, 305,  1, ais->type19.dte);
	ais_addbits(bits, 306,  1, ais->type19.assigned);
/*      ais_addbits(bits, 307,  5, ais->type19.spare); */
	len = 307 + 5;
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
