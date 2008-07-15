/* $Id$ */
/*
 * bits.h - extract binary data from message buffer
 *
 * These macros extract bytes, words, longwords, floats or doubles from
 * a message that contains these items in either MSB-first or LSB-first 
 * byte order.  To specify which, define one of LITTLE_ENDIAN_PROTOCOL
 * or BIG_ENDIAN_PROTOCOL before including this header.
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
 */

#ifndef BITS_H
#define BITS_H
union int_float {
    int32_t i;
    float f;
};

union long_double {
    int64_t l;
    double d;
};
#endif /* BITS_H */

#ifndef GET_ORIGIN
#define GET_ORIGIN	0
#endif
#ifndef PUT_ORIGIN
#define PUT_ORIGIN	0
#endif

/* these are independent of byte order */
#define getsb(buf, off)	((int8_t)buf[(off)-(GET_ORIGIN)])
#define getub(buf, off)	((u_int8_t)buf[(off)-(GET_ORIGIN)])
#define putbyte(buf,off,b) do {buf[(off)-(PUT_ORIGIN)] = (unsigned char)(b);} while (0)

#ifdef LITTLE_ENDIAN_PROTOCOL

#define getlesw(buf, off)	((int16_t)(((u_int16_t)getub((buf),   (off)+1) << 8) | (u_int16_t)getub((buf), (off))))
#define getleuw(buf, off)	((u_int16_t)(((u_int16_t)getub((buf), (off)+1) << 8) | (u_int16_t)getub((buf), (off))))
#define getlesl(buf, off)	((int32_t)(((u_int16_t)getleuw((buf),  (off)+2) << 16) | (u_int16_t)getleuw((buf), (off))))
#define getleul(buf, off)	((u_int32_t)(((u_int16_t)getleuw((buf),(off)+2) << 16) | (u_int16_t)getleuw((buf), (off))))

#define putleword(buf, off, w) do {putbyte(buf, (off)+1, (w) >> 8); putbyte(buf, (off), (w));} while (0)
#define putlelong(buf, off, l) do {putleword(buf, (off)+2, (l) >> 16); putleword(buf, (off), (l));} while (0)
#define getlesL(buf, off)	((int64_t)(((u_int64_t)getleul(buf, (off)+4) << 32) | getleul(buf, (off))))
#define getleuL(buf, off)	((u_int64_t)(((u_int64_t)getleul(buf, (off)+4) << 32) | getleul(buf, (off))))

#define getlef(buf, off)	(i_f.i = getlesl(buf, off), i_f.f)
#define getled(buf, off)	(l_d.l = getlesL(buf, off), l_d.d)
#else

/* SiRF and most other GPS protocols use big-endian (network byte order) */
#define getsw(buf, off)	((int16_t)(((u_int16_t)getub(buf, (off)) << 8) | (u_int16_t)getub(buf, (off)+1)))
#define getuw(buf, off)	((u_int16_t)(((u_int16_t)getub(buf, (off)) << 8) | (u_int16_t)getub(buf, (off)+1)))
#define getsl(buf, off)	((int32_t)(((u_int16_t)getuw(buf, (off)) << 16) | getuw(buf, (off)+2)))
#define getul(buf, off)	((u_int32_t)(((u_int16_t)getuw(buf, (off)) << 16) | getuw(buf, (off)+2)))
#define getsL(buf, off)	((int64_t)(((u_int64_t)getul(buf, (off)) << 32) | getul(buf, (off)+4)))
#define getuL(buf, off)	((u_int64_t)(((u_int64_t)getul(buf, (off)) << 32) | getul(buf, (off)+4)))

#define putword(buf,off,w) do {putbyte(buf, (off) ,(w) >> 8); putbyte(buf, (off)+1, (w));} while (0)
#define putlong(buf,off,l) do {putword(buf, (off) ,(l) >> 16); putword(buf, (off)+2, (l));} while (0)

#define getf(buf, off)	(i_f.i = getsl(buf, off), i_f.f)
#define getd(buf, off)	(l_d.l = getsL(buf, off), l_d.d)
#endif



/* Zodiac protocol description uses 1-origin indexing by little-endian word */
#define getwordz(buf, n)	( (buf[2*(n)-2])	\
		| (buf[2*(n)-1] << 8))
#define getlongz(buf, n)	( (buf[2*(n)-2])	\
		| (buf[2*(n)-1] << 8) \
		| (buf[2*(n)+0] << 16) \
		| (buf[2*(n)+1] << 24))
#define getstringz(to, from, s, e)			\
    (void)memcpy(to, from+2*(s)-2, 2*((e)-(s)+1))
