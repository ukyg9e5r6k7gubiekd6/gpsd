/*
 * bits.h - extract binary data from message buffer
 *
 * these macros extract bytes, words, longwords, floats or doubles from
 * a message that contains these items in MSB-first byte order.
 *
 * the macros access a local buffer named "buf" which must be declared
 * as unsigned char buf[SIZE];
 *
 * assumptions:
 *  char is 8 bits, short is 16 bits, int is 32 bits, long long is 64 bits,
 *  float is 32 bits IEEE754, double is 64 bits IEEE754.
 *
 * it would be possible to use types like int16_t from header files to enforce
 * these assumptions, but splint does not understand those and will scream.
 * also, using such explicitly sized types usually causes warnings at many
 * other places in a program (like when calling library routines).  we will
 * need to consider this again when we want to port to an architecture which
 * implements differently sized types.  it looks like 64bit systems with
 * gcc are OK.
 */

union int_float {
    int i;
    float f;
};

union long_double {
    long long l;
    double d;
};

#define getsb(off)	((char)buf[off])
#define getub(off)	(buf[off])
#define getsw(off)	((short)(((unsigned)getub(off) << 8) | (unsigned)getub(off+1)))
#define getuw(off)	((unsigned short)(((unsigned)getub(off) << 8) | (unsigned)getub(off+1)))
#define getsl(off)	((int)(((unsigned)getuw(off) << 16) | getuw(off+2)))
#define getul(off)	((unsigned int)(((unsigned)getuw(off) << 16) | getuw(off+2)))
#define getsL(off)	((long long)(((unsigned long long)getul(off) << 32) | getul(off+4)))
#define getuL(off)	((unsigned long long)(((unsigned long long)getul(off) << 32) | getul(off+4)))
#define getf(off)	(i_f.i = getsl(off), i_f.f)
#define getd(off)	(l_d.l = getsL(off), l_d.d)
