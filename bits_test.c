/* test harness for bits.h */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bits.h"

/*@ -duplicatequals +ignorequals @*/
int main(void)
{
    unsigned char buf[80];
    union int_float i_f;
    union long_double l_d;
    char sb1,sb2;
    unsigned char ub1,ub2;
    short sw1,sw2;
    unsigned short uw1,uw2;
    int sl1,sl2;
    unsigned int ul1,ul2;
    long long sL1,sL2;
    unsigned long long uL1,uL2;
    float f1;
    double d1;

    memcpy(buf,"\x01\x02\x03\x04\x05\x06\x07\x08",8);
    memcpy(buf+8,"\xff\xfe\xfd\xfc\xfb\xfa\xf9\xf8",8);
    memcpy(buf+16,"\x40\x09\x21\xfb\x54\x44\x2d\x18",8);
    memcpy(buf+24,"\x40\x49\x0f\xdb",4);

    sb1 = getsb(buf, 0);
    sb2 = getsb(buf, 8);
    ub1 = getub(buf, 0);
    ub2 = getub(buf, 8);
    sw1 = getsw(buf, 0);
    sw2 = getsw(buf, 8);
    uw1 = getuw(buf, 0);
    uw2 = getuw(buf, 8);
    sl1 = getsl(buf, 0);
    sl2 = getsl(buf, 8);
    ul1 = getul(buf, 0);
    ul2 = getul(buf, 8);
    sL1 = getsL(buf, 0);
    sL2 = getsL(buf, 8);
    uL1 = getuL(buf, 0);
    uL2 = getuL(buf, 8);
    f1 = getf(buf, 24);
    d1 = getd(buf, 16);

    (void)printf("getsb: %016llx %016llx %016llx %016llx\n",
		(unsigned long long)sb1, (unsigned long long)sb2,
		(unsigned long long)getsb(buf, 0), (unsigned long long)getsb(buf, 8));
    (void)printf("getub: %016llx %016llx %016llx %016llx\n",
		(unsigned long long)ub1, (unsigned long long)ub2,
		(unsigned long long)getub(buf, 0), (unsigned long long)getub(buf, 8));
    (void)printf("getsw: %016llx %016llx %016llx %016llx\n",
		(unsigned long long)sw1, (unsigned long long)sw2,
		(unsigned long long)getsw(buf, 0), (unsigned long long)getsw(buf, 8));
    (void)printf("getuw: %016llx %016llx %016llx %016llx\n",
		(unsigned long long)uw1, (unsigned long long)uw2,
		(unsigned long long)getuw(buf, 0), (unsigned long long)getuw(buf, 8));
    (void)printf("getsl: %016llx %016llx %016llx %016llx\n",
		(unsigned long long)sl1, (unsigned long long)sl2,
		(unsigned long long)getsl(buf, 0), (unsigned long long)getsl(buf, 8));
    (void)printf("getul: %016llx %016llx %016llx %016llx\n",
		(unsigned long long)ul1, (unsigned long long)ul2,
		(unsigned long long)getul(buf, 0), (unsigned long long)getul(buf, 8));
    (void)printf("getsL: %016llx %016llx %016llx %016llx\n",
		(unsigned long long)sL1, (unsigned long long)sL2,
		(unsigned long long)getsL(buf, 0), (unsigned long long)getsL(buf, 8));
    (void)printf("getuL: %016llx %016llx %016llx %016llx\n",
		(unsigned long long)uL1, (unsigned long long)uL2,
		(unsigned long long)getuL(buf, 0), (unsigned long long)getuL(buf, 8));
    (void)printf("getf: %f %f\n", f1, getf(buf, 24));
    (void)printf("getd: %.16f %.16f\n", d1, getd(buf, 16));

    exit(0);
}
