/* test harness for bits.h
 *
 * This file is Copyright (c) 2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "bits.h"

/*@ -duplicatequals -formattype */
static unsigned char buf[80];
static union int_float i_f;
static union long_double l_d;
static char sb1, sb2;
static unsigned char ub1, ub2;
static short sw1, sw2;
static unsigned short uw1, uw2;
static int sl1, sl2;
static unsigned int ul1, ul2;
static long long sL1, sL2;
static uint64_t uL1, uL2;
static float f1;
static double d1;


static char /*@ observer @*/ *hexdump(const void *binbuf, size_t len)
{
    static char hexbuf[BUFSIZ];
    size_t i, j = 0;
    const char *ibuf = (const char *)binbuf;
    const char *hexchar = "0123456789abcdef";

    /*@ -shiftimplementation @*/
    for (i = 0; i < len; i++) {
	hexbuf[j++] = hexchar[(ibuf[i] & 0xf0) >> 4];
	hexbuf[j++] = hexchar[ibuf[i] & 0x0f];
    }
    /*@ +shiftimplementation @*/
    hexbuf[j] = '\0';
    return hexbuf;
}

static void bedumpall(void)
{
    (void)printf("getsb: %016" PRIx64 " %016" PRIx64 " %016" PRIx64 " %016" PRIx64 "\n",
		 (uint64_t) sb1, (uint64_t) sb2,
		 (uint64_t) getsb(buf, 0), (uint64_t) getsb(buf, 8));
    (void)printf("getub: %016" PRIx64 " %016" PRIx64 " %016" PRIx64 " %016" PRIx64 "\n",
		 (uint64_t) ub1, (uint64_t) ub2,
		 (uint64_t) getub(buf, 0), (uint64_t) getub(buf, 8));
    (void)printf("getbes16: %016" PRIx64 " %016" PRIx64 " %016" PRIx64 " %016" PRIx64 "\n",
		 (uint64_t) sw1, (uint64_t) sw2,
		 (uint64_t) getbes16(buf, 0), (uint64_t) getbes16(buf, 8));
    (void)printf("getbeu16: %016" PRIx64 " %016" PRIx64 " %016" PRIx64 " %016" PRIx64 "\n",
		 (uint64_t) uw1, (uint64_t) uw2,
		 (uint64_t) getbeu16(buf, 0), (uint64_t) getbeu16(buf, 8));
    (void)printf("getbes32: %016" PRIx64 " %016" PRIx64 " %016" PRIx64 " %016" PRIx64 "\n",
		 (uint64_t) sl1, (uint64_t) sl2,
		 (uint64_t) getbes32(buf, 0), (uint64_t) getbes32(buf, 8));
    (void)printf("getbeu32: %016" PRIx64 " %016" PRIx64 " %016" PRIx64 " %016" PRIx64 "\n",
		 (uint64_t) ul1, (uint64_t) ul2,
		 (uint64_t) getbeu32(buf, 0), (uint64_t) getbeu32(buf, 8));
    (void)printf("getbes64: %016" PRIx64 " %016" PRIx64 " %016" PRIx64 " %016" PRIx64 "\n",
		 (uint64_t) sL1, (uint64_t) sL2,
		 (uint64_t) getbes64(buf, 0), (uint64_t) getbes64(buf, 8));
    (void)printf("getbeu64: %016" PRIx64 " %016" PRIx64 " %016" PRIx64 " %016" PRIx64 "\n",
		 (uint64_t) uL1, (uint64_t) uL2,
		 (uint64_t) getbeu64(buf, 0), (uint64_t) getbeu64(buf, 8));
    (void)printf("getbef: %f %f\n", f1, getbef(buf, 24));
    (void)printf("getbed: %.16f %.16f\n", d1, getbed(buf, 16));
}

static void ledumpall(void)
{
    (void)printf("getsb: %016" PRIx64 " %016" PRIx64 " %016" PRIx64 " %016" PRIx64 "\n",
		 (uint64_t) sb1, (uint64_t) sb2,
		 (uint64_t) getsb(buf, 0), (uint64_t) getsb(buf, 8));
    (void)printf("getub: %016" PRIx64 " %016" PRIx64 " %016" PRIx64 " %016" PRIx64 "\n",
		 (uint64_t) ub1, (uint64_t) ub2,
		 (uint64_t) getub(buf, 0), (uint64_t) getub(buf, 8));
    (void)printf("getles16: %016" PRIx64 " %016" PRIx64 " %016" PRIx64 " %016" PRIx64 "\n",
		 (uint64_t) sw1, (uint64_t) sw2,
		 (uint64_t) getles16(buf, 0), (uint64_t) getles16(buf, 8));
    (void)printf("getleu16: %016" PRIx64 " %016" PRIx64 " %016" PRIx64 " %016" PRIx64 "\n",
		 (uint64_t) uw1, (uint64_t) uw2,
		 (uint64_t) getleu16(buf, 0), (uint64_t) getleu16(buf, 8));
    (void)printf("getles32: %016" PRIx64 " %016" PRIx64 " %016" PRIx64 " %016" PRIx64 "\n",
		 (uint64_t) sl1, (uint64_t) sl2,
		 (uint64_t) getles32(buf, 0), (uint64_t) getles32(buf, 8));
    (void)printf("getleu32: %016" PRIx64 " %016" PRIx64 " %016" PRIx64 " %016" PRIx64 "\n",
		 (uint64_t) ul1, (uint64_t) ul2,
		 (uint64_t) getleu32(buf, 0), (uint64_t) getleu32(buf, 8));
    (void)printf("getles64: %016" PRIx64 " %016" PRIx64 " %016" PRIx64 " %016" PRIx64 "\n",
		 (uint64_t) sL1, (uint64_t) sL2,
		 (uint64_t) getles64(buf, 0), (uint64_t) getles64(buf, 8));
    (void)printf("getleu64: %016" PRIx64 " %016" PRIx64 " %016" PRIx64 " %016" PRIx64 "\n",
		 (uint64_t) uL1, (uint64_t) uL2,
		 (uint64_t) getleu64(buf, 0), (uint64_t) getleu64(buf, 8));
    (void)printf("getlef: %f %f\n", f1, getlef(buf, 24));
    (void)printf("getled: %.16f %.16f\n", d1, getled(buf, 16));
}

struct unsigned_test
{
    unsigned char *buf;
    unsigned int start, width;
    uint64_t expected;
    bool le;
    char *description;
};

/*@ -duplicatequals +ignorequals @*/
int main(void)
{
    /*@ -observertrans -usereleased @*/
    struct unsigned_test *up, unsigned_tests[] = {
	/* tests using the big buffer */
	{buf, 0,  1,  0,    false, "first bit of first byte"},
	{buf, 0,  8,  0x01, false, "first 8 bits"},
	{buf, 32, 7,  0x02, false, "first seven bits of fifth byte (0x05)"},
	{buf, 56, 12, 0x8f, false, "12 bits crossing 7th to 8th bytes (0x08ff)"},
	{buf, 78, 4, 11,    false, "2 bits crossing 8th to 9th byte (0xfefd)"},
	{buf, 0,  1,  0,    true,  "first bit of first byte"},
	{buf, 0,  8,  0x80, true,  "first 8 bits"},
	{buf, 32, 7,  0x20, true, "first seven bits of fifth byte (0x05)"},
	{buf, 56, 12, 0xf10,true, "12 bits crossing 7th to 8th bytes (0x08ff)"},
	/* sporadic tests based on found bugs */
	{(unsigned char *)"\x19\x23\f6",
	 7, 2, 2, false, "2 bits crossing 1st to 2nd byte (0x1923)"},
    };

    unsigned char *sp;

    memcpy(buf, "\x01\x02\x03\x04\x05\x06\x07\x08", 8);
    memcpy(buf + 8, "\xff\xfe\xfd\xfc\xfb\xfa\xf9\xf8", 8);
    memcpy(buf + 16, "\x40\x09\x21\xfb\x54\x44\x2d\x18", 8);
    memcpy(buf + 24, "\x40\x49\x0f\xdb", 4);
    /*@ +observertrans +usereleased @*/

    (void)fputs("Test data:", stdout);
    for (sp = buf; sp < buf + 28; sp++)
	(void)printf(" %02x", *sp);
    (void)putc('\n', stdout);

    /* big-endian test */
    /*@-type@*/
    printf("Big-endian:\n");
    sb1 = getsb(buf, 0);
    sb2 = getsb(buf, 8);
    ub1 = getub(buf, 0);
    ub2 = getub(buf, 8);
    sw1 = getbes16(buf, 0);
    sw2 = getbes16(buf, 8);
    uw1 = getbeu16(buf, 0);
    uw2 = getbeu16(buf, 8);
    sl1 = getbes32(buf, 0);
    sl2 = getbes32(buf, 8);
    ul1 = getbeu32(buf, 0);
    ul2 = getbeu32(buf, 8);
    sL1 = getbes64(buf, 0);
    sL2 = getbes64(buf, 8);
    uL1 = getbeu64(buf, 0);
    uL2 = getbeu64(buf, 8);
    f1 = getbef(buf, 24);
    d1 = getbed(buf, 16);
    /*@+type@*/
    bedumpall();

    /* little-endian test */
    printf("Little-endian:\n");
    /*@-type@*/
    sb1 = getsb(buf, 0);
    sb2 = getsb(buf, 8);
    ub1 = getub(buf, 0);
    ub2 = getub(buf, 8);
    sw1 = getles16(buf, 0);
    sw2 = getles16(buf, 8);
    uw1 = getleu16(buf, 0);
    uw2 = getleu16(buf, 8);
    sl1 = getles32(buf, 0);
    sl2 = getles32(buf, 8);
    ul1 = getleu32(buf, 0);
    ul2 = getleu32(buf, 8);
    sL1 = getles64(buf, 0);
    sL2 = getles64(buf, 8);
    uL1 = getleu64(buf, 0);
    uL2 = getleu64(buf, 8);
    f1 = getlef(buf, 24);
    d1 = getled(buf, 16);
    /*@+type@*/
    ledumpall();


    (void)printf("Testing bitfield extraction:\n");
    for (up = unsigned_tests;
	 up <
	 unsigned_tests + sizeof(unsigned_tests) / sizeof(unsigned_tests[0]);
	 up++) {
	uint64_t res = ubits((char *)buf, up->start, up->width, up->le);
	(void)printf("ubits(%s, %d, %d, %s) %s should be %" PRIx64 ", is %" PRIx64 ": %s\n",
		     hexdump(buf, strlen((char *)buf)),
		     up->start, up->width, up->le ? "true" : "false",
		     up->description, up->expected, res,
		     res == up->expected ? "succeeded" : "FAILED");
    }

    exit(0);
}
