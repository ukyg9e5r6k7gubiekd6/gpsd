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
#ifndef _GPSD_BITS_H_
#define _GPSD_BITS_H_

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
#define getles16(buf, off)	((int16_t)(((uint16_t)getub((buf),   (off)+1) << 8) | (uint16_t)getub((buf), (off))))
#define getleu16(buf, off)	((uint16_t)(((uint16_t)getub((buf), (off)+1) << 8) | (uint16_t)getub((buf), (off))))
#define getles32(buf, off)	((int32_t)(((uint16_t)getleu16((buf),  (off)+2) << 16) | (uint16_t)getleu16((buf), (off))))
#define getleu32(buf, off)	((uint32_t)(((uint16_t)getleu16((buf),(off)+2) << 16) | (uint16_t)getleu16((buf), (off))))

#define putle16(buf, off, w) do {putbyte(buf, (off)+1, (uint)(w) >> 8); putbyte(buf, (off), (w));} while (0)
#define putle32(buf, off, l) do {putle16(buf, (off)+2, (uint)(l) >> 16); putle16(buf, (off), (l));} while (0)
#define getles64(buf, off)	((int64_t)(((uint64_t)getleu32(buf, (off)+4) << 32) | getleu32(buf, (off))))
#define getleu64(buf, off)	((uint64_t)(((uint64_t)getleu32(buf, (off)+4) << 32) | getleu32(buf, (off))))

#define getlef(buf, off)	(i_f.i = getles32(buf, off), i_f.f)
#define getled(buf, off)	(l_d.l = getles64(buf, off), l_d.d)

/* SiRF and most other GPS protocols use big-endian (network byte order) */
#define getbes16(buf, off)	((int16_t)(((uint16_t)getub(buf, (off)) << 8) | (uint16_t)getub(buf, (off)+1)))
#define getbeu16(buf, off)	((uint16_t)(((uint16_t)getub(buf, (off)) << 8) | (uint16_t)getub(buf, (off)+1)))
#define getbes32(buf, off)	((int32_t)(((uint16_t)getbeu16(buf, (off)) << 16) | getbeu16(buf, (off)+2)))
#define getbeu32(buf, off)	((uint32_t)(((uint16_t)getbeu16(buf, (off)) << 16) | getbeu16(buf, (off)+2)))
#define getbes64(buf, off)	((int64_t)(((uint64_t)getbeu32(buf, (off)) << 32) | getbeu32(buf, (off)+4)))
#define getbeu64(buf, off)	((uint64_t)(((uint64_t)getbeu32(buf, (off)) << 32) | getbeu32(buf, (off)+4)))

#define putbe16(buf,off,w) do {putbyte(buf, (off) ,(w) >> 8); putbyte(buf, (off)+1, (w));} while (0)
#define putbe32(buf,off,l) do {putbe16(buf, (off) ,(l) >> 16); putbe16(buf, (off)+2, (l));} while (0)

#define getbef(buf, off)	(i_f.i = getbes32(buf, off), i_f.f)
#define getbed(buf, off)	(l_d.l = getbes64(buf, off), l_d.d)


/* Zodiac protocol description uses 1-origin indexing by little-endian word */
#define get16z(buf, n)	( (buf[2*(n)-2])	\
		| (buf[2*(n)-1] << 8))
#define get32z(buf, n)	( (buf[2*(n)-2])	\
		| (buf[2*(n)-1] << 8) \
		| (buf[2*(n)+0] << 16) \
		| (buf[2*(n)+1] << 24))
#define getstringz(to, from, s, e)			\
    (void)memcpy(to, from+2*(s)-2, 2*((e)-(s)+1))

/* bitfield extraction */
extern unsigned long long ubits(char buf[], unsigned int, unsigned int);
extern signed long long sbits(char buf[], unsigned int, unsigned int);

#endif /* _GPSD_BITS_H_ */
