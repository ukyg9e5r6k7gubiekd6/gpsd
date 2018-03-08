/*
 * Encode AIS messages.
 *
 * See the file AIVDM.txt on the GPSD website for documentation and references.
 *
 * This file is build from driver_ais.c
 *
 * This file is Copyright (c) 2013 by the GPSD project
 * SPDX-License-Identifier: BSD-2-clause
 */
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "gpsd.h"
#include "bits.h"
#include <stdint.h>

#ifdef AIVDM_ENABLE

#define AIS_MSG_PART2_FLAG 0x100

static unsigned char convtab[] = {"0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVW`abcdefghijklmnopqrstuvw"};
static unsigned char contab1[] = {0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
				  0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
				  0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,0x29,0x2a,0x2b,0x2c,0x2d,0x2e,0x2f,
				  0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x3b,0x3c,0x3d,0x3e,0x3f,
				  0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
				  0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,
				  0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
				  0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff};

static void ais_addbits(unsigned char *bits,
			unsigned int   start,
			unsigned int   len,
			uint64_t       data)
{
    unsigned int  l;
    unsigned int  pos;
    uint64_t      mask;
    unsigned int  mask1;

    mask  = 0x1;
    pos   = (start+len-1) / 6;
    mask1 = 0x20;
    mask1 = mask1 >> ((start+len-1) % 6);

    if (len == 0) {
        return;
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
    return;
}


static void ais_addchar(unsigned char *bits,
			unsigned int   start,
			unsigned int   len,
			char          *data)
{
    unsigned int l;
    unsigned int flag;

    for(l=0,flag=0;l<len;l++) {
        unsigned char a, b;
	
	a = (unsigned char) data[l];
	if (a == (unsigned char)'\0') {
	    flag = 1;
	}
	if (flag == 0) {
	    b = contab1[a & 0x7f];
	} else {
	  b = (unsigned char) '\0';
	}
	ais_addbits(bits, start+6*l, 6, (uint64_t)b);
    }
    return;
}


static void ais_adddata(unsigned char *bits,
		   unsigned int   start,
		   unsigned int   len,
		   char          *data)
{
    unsigned int l;

    for(l=0;l<len;l++) {
        ais_addbits(bits, start+6*l, 6, (uint64_t)data[l]);
    }
    return;
}


static void ais_binary_to_ascii(unsigned char *bits, unsigned int len)
{
    unsigned int l;

    if (len == 0) {
        bits[0] = '\0';
        return;
    }

    for (l=0;l<len;l+=6) {
        bits[l/6] = convtab[bits[l/6] & 0x3f];
    }
    return;
}


unsigned int ais_binary_encode(struct ais_t *ais,
			       unsigned char *bits,
                               int flag)
{
    unsigned int len;

    len = 0;

    if (flag != 0) {
        flag = AIS_MSG_PART2_FLAG;
    }
    ais_addbits(bits,  0,  6, (uint64_t)ais->type);
    ais_addbits(bits,  6,  2, (uint64_t)ais->repeat);
    ais_addbits(bits,  8, 30, (uint64_t)ais->mmsi);
    switch (flag | ais->type) {
    case 1:	/* Position Report */
    case 2:
    case 3:
        ais_addbits(bits,  38,  4, (uint64_t)ais->type1.status);
        ais_addbits(bits,  42,  8, (uint64_t)ais->type1.turn);
        ais_addbits(bits,  50, 10, (uint64_t)ais->type1.speed);
	ais_addbits(bits,  60,  1, (uint64_t)ais->type1.accuracy);
	ais_addbits(bits,  61, 28, (uint64_t)ais->type1.lon);
	ais_addbits(bits,  89, 27, (uint64_t)ais->type1.lat);
	ais_addbits(bits, 116, 12, (uint64_t)ais->type1.course);
	ais_addbits(bits, 128,  9, (uint64_t)ais->type1.heading);
	ais_addbits(bits, 137,  6, (uint64_t)ais->type1.second);
	ais_addbits(bits, 143,  2, (uint64_t)ais->type1.maneuver);
/*	ais_addbits(bits, 145,  3, (uint64_t)ais->type1.spare); */
	ais_addbits(bits, 148,  1, (uint64_t)ais->type1.raim);
	ais_addbits(bits, 149, 19, (uint64_t)ais->type1.radio);
	len = 149 + 19;
	break;
    case 4: 	/* Base Station Report */
    case 11:	/* UTC/Date Response */
        ais_addbits(bits,  38, 14, (uint64_t)ais->type4.year);
	ais_addbits(bits,  52,  4, (uint64_t)ais->type4.month);
	ais_addbits(bits,  56,  5, (uint64_t)ais->type4.day);
	ais_addbits(bits,  61,  5, (uint64_t)ais->type4.hour);
	ais_addbits(bits,  66,  6, (uint64_t)ais->type4.minute);
	ais_addbits(bits,  72,  6, (uint64_t)ais->type4.second);
	ais_addbits(bits,  78,  1, (uint64_t)ais->type4.accuracy);
	ais_addbits(bits,  79, 28, (uint64_t)ais->type4.lon);
	ais_addbits(bits, 107, 27, (uint64_t)ais->type4.lat);
	ais_addbits(bits, 134,  4, (uint64_t)ais->type4.epfd);
/*	ais_addbits(bits, 138, 10, (uint64_t)ais->type4.spare); */
	ais_addbits(bits, 148,  1, (uint64_t)ais->type4.raim);
	ais_addbits(bits, 149, 19, (uint64_t)ais->type4.radio);
	len = 149 + 19;
        break;
    case 5:     /* Ship static and voyage related data */
        ais_addbits(bits,  38,  2, (uint64_t)ais->type5.ais_version);
	ais_addbits(bits,  40, 30, (uint64_t)ais->type5.imo);
	ais_addchar(bits,  70,  7,           ais->type5.callsign);
	ais_addchar(bits, 112, 20,           ais->type5.shipname);
	ais_addbits(bits, 232,  8, (uint64_t)ais->type5.shiptype);
	ais_addbits(bits, 240,  9, (uint64_t)ais->type5.to_bow);
	ais_addbits(bits, 249,  9, (uint64_t)ais->type5.to_stern);
	ais_addbits(bits, 258,  6, (uint64_t)ais->type5.to_port);
	ais_addbits(bits, 264,  6, (uint64_t)ais->type5.to_starboard);
	ais_addbits(bits, 270,  4, (uint64_t)ais->type5.epfd);
	ais_addbits(bits, 274,  4, (uint64_t)ais->type5.month);
	ais_addbits(bits, 278,  5, (uint64_t)ais->type5.day);
	ais_addbits(bits, 283,  5, (uint64_t)ais->type5.hour);
	ais_addbits(bits, 288,  6, (uint64_t)ais->type5.minute);
	ais_addbits(bits, 294,  8, (uint64_t)ais->type5.draught);
	ais_addchar(bits, 302, 20,           ais->type5.destination);
	ais_addbits(bits, 422,  1, (uint64_t)ais->type5.dte);
/*      ais_addbits(bits, 423,  1, (uint64_t)ais->type5.spare); */
	len = 423 + 1;
        break;	
    case 9:     /* Standard SAR Aircraft Position Report */
        ais_addbits(bits,  38, 12, (uint64_t)ais->type9.alt);
	ais_addbits(bits,  50, 10, (uint64_t)ais->type9.speed);
	ais_addbits(bits,  60,  1, (uint64_t)ais->type9.accuracy);
	ais_addbits(bits,  61, 28, (uint64_t)ais->type9.lon);
	ais_addbits(bits,  89, 27, (uint64_t)ais->type9.lat);
	ais_addbits(bits, 116, 12, (uint64_t)ais->type9.course);
	ais_addbits(bits, 128,  6, (uint64_t)ais->type9.second);
	ais_addbits(bits, 134,  8, (uint64_t)ais->type9.regional);
	ais_addbits(bits, 142,  1, (uint64_t)ais->type9.dte);
/*	ais_addbits(bits, 143,  3, (uint64_t)ais->type9.spare); */
	ais_addbits(bits, 146,  1, (uint64_t)ais->type9.assigned);
	ais_addbits(bits, 147,  1, (uint64_t)ais->type9.raim);
	ais_addbits(bits, 148, 19, (uint64_t)ais->type9.radio);
	len = 148 + 19;
        break;
    case 18:	/* Standard Class B CS Position Report */
      	ais_addbits(bits,  38,  8, (uint64_t)ais->type18.reserved);
	ais_addbits(bits,  46, 10, (uint64_t)ais->type18.speed);
	ais_addbits(bits,  56,  1, (uint64_t)ais->type18.accuracy);
	ais_addbits(bits,  57, 28, (uint64_t)ais->type18.lon);
	ais_addbits(bits,  85, 27, (uint64_t)ais->type18.lat);
	ais_addbits(bits, 112, 12, (uint64_t)ais->type18.course);
	ais_addbits(bits, 124,  9, (uint64_t)ais->type18.heading);
	ais_addbits(bits, 133,  6, (uint64_t)ais->type18.second);
	ais_addbits(bits, 139,  2, (uint64_t)ais->type18.regional);
	ais_addbits(bits, 141,  1, (uint64_t)ais->type18.cs);
	ais_addbits(bits, 142,  1, (uint64_t)ais->type18.display);
	ais_addbits(bits, 143,  1, (uint64_t)ais->type18.dsc);
	ais_addbits(bits, 144,  1, (uint64_t)ais->type18.band);
	ais_addbits(bits, 145,  1, (uint64_t)ais->type18.msg22);
	ais_addbits(bits, 146,  1, (uint64_t)ais->type18.assigned);
	ais_addbits(bits, 147,  1, (uint64_t)ais->type18.raim);
	ais_addbits(bits, 148, 20, (uint64_t)ais->type18.radio);
	len = 148 + 20;
        break;
    case 19:	/* Extended Class B CS Position Report */
        ais_addbits(bits,  38,  8, (uint64_t)ais->type19.reserved);
	ais_addbits(bits,  46, 10, (uint64_t)ais->type19.speed);
	ais_addbits(bits,  56,  1, (uint64_t)ais->type19.accuracy);
	ais_addbits(bits,  57, 28, (uint64_t)ais->type19.lon);
	ais_addbits(bits,  85, 27, (uint64_t)ais->type19.lat);
	ais_addbits(bits, 112, 12, (uint64_t)ais->type19.course);
	ais_addbits(bits, 124,  9, (uint64_t)ais->type19.heading);
	ais_addbits(bits, 133,  6, (uint64_t)ais->type19.second);
	ais_addbits(bits, 139,  4, (uint64_t)ais->type19.regional);
	ais_addchar(bits, 143, 20,           ais->type19.shipname);
	ais_addbits(bits, 263,  8, (uint64_t)ais->type19.shiptype);
	ais_addbits(bits, 271,  9, (uint64_t)ais->type19.to_bow);
	ais_addbits(bits, 280,  9, (uint64_t)ais->type19.to_stern);
	ais_addbits(bits, 289,  6, (uint64_t)ais->type19.to_port);
	ais_addbits(bits, 295,  6, (uint64_t)ais->type19.to_starboard);
	ais_addbits(bits, 299,  4, (uint64_t)ais->type19.epfd);
	ais_addbits(bits, 302,  1, (uint64_t)ais->type19.raim);
	ais_addbits(bits, 305,  1, (uint64_t)ais->type19.dte);
	ais_addbits(bits, 306,  1, (uint64_t)ais->type19.assigned);
/*      ais_addbits(bits, 307,  5, (uint64_t)ais->type19.spare); */
	len = 307 + 5;
        break;
    case 21:	/* Aid-to-Navigation Report */
        ais_addbits(bits,  38,  5, (uint64_t)ais->type21.aid_type);
	ais_addchar(bits,  43, 20,           ais->type21.name);
	ais_addbits(bits, 163,  1, (uint64_t)ais->type21.accuracy);
	ais_addbits(bits, 164, 28, (uint64_t)ais->type21.lon);
	ais_addbits(bits, 192, 27, (uint64_t)ais->type21.lat);
	ais_addbits(bits, 219,  9, (uint64_t)ais->type21.to_bow);
	ais_addbits(bits, 228,  9, (uint64_t)ais->type21.to_stern);
	ais_addbits(bits, 237,  6, (uint64_t)ais->type21.to_port);
	ais_addbits(bits, 243,  6, (uint64_t)ais->type21.to_starboard);
	ais_addbits(bits, 249,  4, (uint64_t)ais->type21.epfd);
        ais_addbits(bits, 253,  6, (uint64_t)ais->type21.second);
	ais_addbits(bits, 259,  1, (uint64_t)ais->type21.off_position);
	ais_addbits(bits, 260,  8, (uint64_t)ais->type21.regional);
	ais_addbits(bits, 268,  1, (uint64_t)ais->type21.raim);
	ais_addbits(bits, 269,  1, (uint64_t)ais->type21.virtual_aid);
	ais_addbits(bits, 270,  1, (uint64_t)ais->type21.assigned);
/*      ais_addbits(bits, 271,  1, (uint64_t)ais->type21.spare);            */
	len = 271 + 1;
	if (strlen(ais->type21.name) > 20) {
	    unsigned int extralen = (unsigned int)(strlen(ais->type21.name) - 20);
	    ais_addchar(bits, 272, extralen, ais->type21.name + 20);
	    len += extralen * 6;
	}
        break;
    case 24:	/* Class B CS Static Data Report Part 1 */
        if (ais->type24.part == part_a) {
            ais_addbits(bits,  38,  2, (uint64_t)0);
            ais_addchar(bits,  40, 20,           ais->type24.shipname);
/*          ais_addbits(bits, 160,  8, (uint64_t)ais->type24.a.spare); */
	    len = 160;
	}
        break;
    case 24 | AIS_MSG_PART2_FLAG: /* Class B CS Static Data Report Part 2 */
        if ((ais->type24.part == part_b) || (ais->type24.part == both)) {
            ais_addbits(bits,  38,  2, (uint64_t)1);
	    ais_addbits(bits,  40,  8, (uint64_t)ais->type24.shiptype);
	    ais_addchar(bits,  48,  3,          &ais->type24.vendorid[0]);
	    ais_adddata(bits,  66,  3,          &ais->type24.vendorid[3]);
	    ais_addchar(bits,  90,  7,          ais->type24.callsign);
	    if (AIS_AUXILIARY_MMSI(ais->mmsi)) {
	        ais_addbits(bits, 132, 30, (uint64_t)ais->type24.mothership_mmsi);
	    } else {
	        ais_addbits(bits, 132,  9, (uint64_t)ais->type24.dim.to_bow);
	        ais_addbits(bits, 141,  9, (uint64_t)ais->type24.dim.to_stern);
	        ais_addbits(bits, 150,  6, (uint64_t)ais->type24.dim.to_port);
	        ais_addbits(bits, 156,  6, (uint64_t)ais->type24.dim.to_starboard);
	    }
/*          ais_addbits(bits, 162,  6,          ais->type24.b.spare); */
	    len = 162 + 6;
	}
        break;
    case 27:	/* Long Range AIS Broadcast message */
        ais_addbits(bits,  38,  1, (uint64_t)ais->type27.accuracy);
	ais_addbits(bits,  39,  1, (uint64_t)ais->type27.raim);
	ais_addbits(bits,  40,  4, (uint64_t)ais->type27.status);
	ais_addbits(bits,  44, 18, (uint64_t)ais->type27.lon);
	ais_addbits(bits,  62, 17, (uint64_t)ais->type27.lat);
	ais_addbits(bits,  79,  6, (uint64_t)ais->type27.speed);
	ais_addbits(bits,  85,  9, (uint64_t)ais->type27.course);
	ais_addbits(bits,  94,  1, (uint64_t)ais->type27.gnss);
        break;
    }
    ais_binary_to_ascii(bits, len);
    return len;
}
#endif /* AIVDM_ENABLE */
