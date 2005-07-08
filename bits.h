/*
 * bits.h - extract binary data from message buffer
 *
 * These macros extract bytes, words, longwords, floats or doubles from
 * a message that contains these items in MSB-first byte order.
 * By defining the GET_ORIGIN and PUT_ORIGIN macros, it's possible to
 * change the origin of the indexing.
 *
 * Assumptions:
 *  char is 8 bits, short is 16 bits, int is 32 bits, long long is 64 bits,
 *  float is 32 bits IEEE754, double is 64 bits IEEE754.
 *
 * The use of fixed-length types in the casts enforces these.
 * Both 32- and 64-bit systems with gcc are OK with this set.
 */

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

/* SiRF and most other GPS protocols use big-endian (network byte order) */
#define getsb(buf, off)	((int8_t)buf[(off)-(GET_ORIGIN)])
#define getub(buf, off)	((u_int8_t)buf[(off)-(GET_ORIGIN)])
#define getsw(buf, off)	((int16_t)(((u_int16_t)getub(buf, off) << 8) | (u_int16_t)getub(buf, off+1)))
#define getuw(buf, off)	((u_int16_t)(((u_int16_t)getub(buf, off) << 8) | (u_int16_t)getub(buf, off+1)))
#define getsl(buf, off)	((int32_t)(((u_int16_t)getuw(buf, off) << 16) | getuw(buf, off+2)))
#define getul(buf, off)	((u_int32_t)(((u_int16_t)getuw(buf, off) << 16) | getuw(buf, off+2)))
#define getsL(buf, off)	((int64_t)(((u_int64_t)getul(buf, off) << 32) | getul(buf, off+4)))
#define getuL(buf, off)	((u_int64_t)(((u_int64_t)getul(buf, off) << 32) | getul(buf, off+4)))
#define getf(buf, off)	(i_f.i = getsl(buf, off), i_f.f)
#define getd(buf, off)	(l_d.l = getsL(buf, off), l_d.d)

#define putbyte(buf,off,b) {buf[(off)-(PUT_ORIGIN)] = (unsigned char)(b);}
#define putword(buf,off,w) {putbyte(buf,off,(w) >> 8); putbyte(buf,off+1,w);}
#define putlong(buf,off,l) {putword(buf,off,(l) >> 16); putword(buf,off+2,l);}

/* Zodiac protocol description uses 1-origin indexing by little-endian word */
#define getword(n)	( (session->outbuffer[2*(n)-2]) \
		| (session->outbuffer[2*(n)-1] << 8))
#define getlong(n)	( (session->outbuffer[2*(n)-2]) \
		| (session->outbuffer[2*(n)-1] << 8) \
		| (session->outbuffer[2*(n)+0] << 16) \
		| (session->outbuffer[2*(n)+1] << 24))
