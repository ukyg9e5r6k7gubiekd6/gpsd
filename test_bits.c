/* test harness for bits.h
 *
 * This file is Copyright (c) 2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bits.h"

/*@ -duplicatequals -formattype */
typedef unsigned long long ubig;

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
static unsigned long long uL1, uL2;
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
    (void)printf("getsb: %016llx %016llx %016llx %016llx\n",
		 (ubig) sb1, (ubig) sb2,
		 (ubig) getsb(buf, 0), (ubig) getsb(buf, 8));
    (void)printf("getub: %016llx %016llx %016llx %016llx\n",
		 (ubig) ub1, (ubig) ub2,
		 (ubig) getub(buf, 0), (ubig) getub(buf, 8));
    (void)printf("getbesw: %016llx %016llx %016llx %016llx\n",
		 (ubig) sw1, (ubig) sw2,
		 (ubig) getbesw(buf, 0), (ubig) getbesw(buf, 8));
    (void)printf("getbeuw: %016llx %016llx %016llx %016llx\n",
		 (ubig) uw1, (ubig) uw2,
		 (ubig) getbeuw(buf, 0), (ubig) getbeuw(buf, 8));
    (void)printf("getbesl: %016llx %016llx %016llx %016llx\n",
		 (ubig) sl1, (ubig) sl2,
		 (ubig) getbesl(buf, 0), (ubig) getbesl(buf, 8));
    (void)printf("getbeul: %016llx %016llx %016llx %016llx\n",
		 (ubig) ul1, (ubig) ul2,
		 (ubig) getbeul(buf, 0), (ubig) getbeul(buf, 8));
    (void)printf("getbesL: %016llx %016llx %016llx %016llx\n",
		 (ubig) sL1, (ubig) sL2,
		 (ubig) getbesL(buf, 0), (ubig) getbesL(buf, 8));
    (void)printf("getbeuL: %016llx %016llx %016llx %016llx\n",
		 (ubig) uL1, (ubig) uL2,
		 (ubig) getbeuL(buf, 0), (ubig) getbeuL(buf, 8));
    (void)printf("getbef: %f %f\n", f1, getbef(buf, 24));
    (void)printf("getbed: %.16f %.16f\n", d1, getbed(buf, 16));
}

static void ledumpall(void)
{
    (void)printf("getsb: %016llx %016llx %016llx %016llx\n",
		 (ubig) sb1, (ubig) sb2,
		 (ubig) getsb(buf, 0), (ubig) getsb(buf, 8));
    (void)printf("getub: %016llx %016llx %016llx %016llx\n",
		 (ubig) ub1, (ubig) ub2,
		 (ubig) getub(buf, 0), (ubig) getub(buf, 8));
    (void)printf("getlesw: %016llx %016llx %016llx %016llx\n",
		 (ubig) sw1, (ubig) sw2,
		 (ubig) getlesw(buf, 0), (ubig) getlesw(buf, 8));
    (void)printf("getleuw: %016llx %016llx %016llx %016llx\n",
		 (ubig) uw1, (ubig) uw2,
		 (ubig) getleuw(buf, 0), (ubig) getleuw(buf, 8));
    (void)printf("getlesl: %016llx %016llx %016llx %016llx\n",
		 (ubig) sl1, (ubig) sl2,
		 (ubig) getlesl(buf, 0), (ubig) getlesl(buf, 8));
    (void)printf("getleul: %016llx %016llx %016llx %016llx\n",
		 (ubig) ul1, (ubig) ul2,
		 (ubig) getleul(buf, 0), (ubig) getleul(buf, 8));
    (void)printf("getlesL: %016llx %016llx %016llx %016llx\n",
		 (ubig) sL1, (ubig) sL2,
		 (ubig) getlesL(buf, 0), (ubig) getlesL(buf, 8));
    (void)printf("getleuL: %016llx %016llx %016llx %016llx\n",
		 (ubig) uL1, (ubig) uL2,
		 (ubig) getleuL(buf, 0), (ubig) getleuL(buf, 8));
    (void)printf("getlef: %f %f\n", f1, getlef(buf, 24));
    (void)printf("getled: %.16f %.16f\n", d1, getled(buf, 16));
}

struct unsigned_test
{
    unsigned char *buf;
    unsigned int start, width;
    unsigned long long expected;
    char *description;
};

/*@ -duplicatequals +ignorequals @*/
int main(void)
{
    /*@ -observertrans -usereleased @*/
    struct unsigned_test *up, unsigned_tests[] = {
	/* tests using the big buffer */
	{buf, 0, 1, 0, "first bit of first byte"},
	{buf, 0, 8, 0x01, "first 8 bits"},
	{buf, 32, 7, 2, "first seven bits of fifth byte"},
	{buf, 56, 12, 0x8f, "12 bits crossing 7th to 8th bytes (0x08ff)"},
	{buf, 78, 4, 11, "2 bits crossing 8th to 9th byte (0xfefd)"},
	/* sporadic tests based on found bugs */
	{(unsigned char *)"\x19\x23\f6",
	 7, 2, 2, "2 bits crossing 1st to 2nd byte (0x1923)"},
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
    sw1 = getbesw(buf, 0);
    sw2 = getbesw(buf, 8);
    uw1 = getbeuw(buf, 0);
    uw2 = getbeuw(buf, 8);
    sl1 = getbesl(buf, 0);
    sl2 = getbesl(buf, 8);
    ul1 = getbeul(buf, 0);
    ul2 = getbeul(buf, 8);
    sL1 = getbesL(buf, 0);
    sL2 = getbesL(buf, 8);
    uL1 = getbeuL(buf, 0);
    uL2 = getbeuL(buf, 8);
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
    sw1 = getlesw(buf, 0);
    sw2 = getlesw(buf, 8);
    uw1 = getleuw(buf, 0);
    uw2 = getleuw(buf, 8);
    sl1 = getlesl(buf, 0);
    sl2 = getlesl(buf, 8);
    ul1 = getleul(buf, 0);
    ul2 = getleul(buf, 8);
    sL1 = getlesL(buf, 0);
    sL2 = getlesL(buf, 8);
    uL1 = getleuL(buf, 0);
    uL2 = getleuL(buf, 8);
    f1 = getlef(buf, 24);
    d1 = getled(buf, 16);
    /*@+type@*/
    ledumpall();


    (void)printf("Testing bitfield extraction:\n");
    for (up = unsigned_tests;
	 up <
	 unsigned_tests + sizeof(unsigned_tests) / sizeof(unsigned_tests[0]);
	 up++) {
	unsigned long long res = ubits((char *)buf, up->start, up->width);
	(void)printf("ubits(%s, %d, %d) %s should be %llu, is %llu: %s\n",
		     hexdump(buf, strlen((char *)buf)),
		     up->start, up->width, up->description, up->expected, res,
		     res == up->expected ? "succeeded" : "FAILED");
    }

    exit(0);
}
