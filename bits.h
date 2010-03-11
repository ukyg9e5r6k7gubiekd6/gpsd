/* $Id$
 *
 * This file is Copyright (c)2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 */
#ifndef _GPSD_BITS_H_
#define _GPSD_BITS_H_

/*
 * bits.h - extract binary data from message buffer
 *
 * These macros extract bytes, words, longwords, floats, doubles, or
 * bitfields of arbitrary length and size from a message that contains
 * these items in either MSB-first or LSB-first byte order.
 * 
 * By defining the GET_ORIGIN and PUT_ORIGIN macros before including
 * this header, it's possible to change the origin of the indexing.
 *
 * Assumptions:
 *  char is 8 bits, short is 16 bits, int is 32 bits, long long is 64 bits,
 *  float is 32 bits IEEE754, double is 64 bits IEEE754.
 *
 * The use of fixed-length types in the casts enforces these.
 * Both 32- and 64-bit systems with gcc are OK with this set.
 *
 * This file is Copyright (c)2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 */

#include <stdint.h>

union int_float {
    int32_t i;
    float f;
};

union long_double {
    int64_t l;
    double d;
};

#ifndef GET_ORIGIN
#define GET_ORIGIN	0
#endif
#ifndef PUT_ORIGIN
#define PUT_ORIGIN	0
#endif

/* these are independent of byte order */
#define getsb(buf, off)	((int8_t)buf[(off)-(GET_ORIGIN)])
#define getub(buf, off)	((uint8_t)buf[(off)-(GET_ORIGIN)])
#define putbyte(buf,off,b) do {buf[(off)-(PUT_ORIGIN)] = (unsigned char)(b);} while (0)

/* little-endian access */
#define getlesw(buf, off)	((int16_t)(((uint16_t)getub((buf),   (off)+1) << 8) | (uint16_t)getub((buf), (off))))
#define getleuw(buf, off)	((uint16_t)(((uint16_t)getub((buf), (off)+1) << 8) | (uint16_t)getub((buf), (off))))
#define getlesl(buf, off)	((int32_t)(((uint16_t)getleuw((buf),  (off)+2) << 16) | (uint16_t)getleuw((buf), (off))))
#define getleul(buf, off)	((uint32_t)(((uint16_t)getleuw((buf),(off)+2) << 16) | (uint16_t)getleuw((buf), (off))))

#define putleword(buf, off, w) do {putbyte(buf, (off)+1, (uint)(w) >> 8); putbyte(buf, (off), (w));} while (0)
#define putlelong(buf, off, l) do {putleword(buf, (off)+2, (uint)(l) >> 16); putleword(buf, (off), (l));} while (0)
#define getlesL(buf, off)	((int64_t)(((uint64_t)getleul(buf, (off)+4) << 32) | getleul(buf, (off))))
#define getleuL(buf, off)	((uint64_t)(((uint64_t)getleul(buf, (off)+4) << 32) | getleul(buf, (off))))

#define getlef(buf, off)	(i_f.i = getlesl(buf, off), i_f.f)
#define getled(buf, off)	(l_d.l = getlesL(buf, off), l_d.d)

/* SiRF and most other GPS protocols use big-endian (network byte order) */
#define getbesw(buf, off)	((int16_t)(((uint16_t)getub(buf, (off)) << 8) | (uint16_t)getub(buf, (off)+1)))
#define getbeuw(buf, off)	((uint16_t)(((uint16_t)getub(buf, (off)) << 8) | (uint16_t)getub(buf, (off)+1)))
#define getbesl(buf, off)	((int32_t)(((uint16_t)getbeuw(buf, (off)) << 16) | getbeuw(buf, (off)+2)))
#define getbeul(buf, off)	((uint32_t)(((uint16_t)getbeuw(buf, (off)) << 16) | getbeuw(buf, (off)+2)))
#define getbesL(buf, off)	((int64_t)(((uint64_t)getbeul(buf, (off)) << 32) | getbeul(buf, (off)+4)))
#define getbeuL(buf, off)	((uint64_t)(((uint64_t)getbeul(buf, (off)) << 32) | getbeul(buf, (off)+4)))

#define putbeword(buf,off,w) do {putbyte(buf, (off) ,(w) >> 8); putbyte(buf, (off)+1, (w));} while (0)
#define putbelong(buf,off,l) do {putbeword(buf, (off) ,(l) >> 16); putbeword(buf, (off)+2, (l));} while (0)

#define getbef(buf, off)	(i_f.i = getbesl(buf, off), i_f.f)
#define getbed(buf, off)	(l_d.l = getbesL(buf, off), l_d.d)


/* Zodiac protocol description uses 1-origin indexing by little-endian word */
#define getwordz(buf, n)	( (buf[2*(n)-2])	\
		| (buf[2*(n)-1] << 8))
#define getlongz(buf, n)	( (buf[2*(n)-2])	\
		| (buf[2*(n)-1] << 8) \
		| (buf[2*(n)+0] << 16) \
		| (buf[2*(n)+1] << 24))
#define getstringz(to, from, s, e)			\
    (void)memcpy(to, from+2*(s)-2, 2*((e)-(s)+1))

/* bitfield extraction */
extern unsigned long long ubits(char buf[], unsigned int, unsigned int);
extern signed long long sbits(char buf[], unsigned int, unsigned int);

#endif /* _GPSD_BITS_H_ */
