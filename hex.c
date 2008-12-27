/* $Id$ */
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

#include "gpsd_config.h"
#include "gpsd.h"

int gpsd_hexdump_level = -1;
/*
 * A wrapper around gpsd_hexdump to prevent wasting cpu time by hexdumping
 * buffers and copying strings that will never be printed. only messages at
 * level "N" and lower will be printed. By way of example, without any -D
 * options, gpsd probably won't ever call the real gpsd_hexdump. At -D2,
 * LOG_PROG (and higher) won't get to call the real gpsd_hexdump. For high
 * speed, chatty protocols, this can save a lot of CPU.
 */
char *gpsd_hexdump_wrapper(const void *binbuf, size_t binbuflen,
    int msg_debug_level)
{
#ifndef SQUELCH_ENABLE
    if (msg_debug_level <= gpsd_hexdump_level)
	return gpsd_hexdump(binbuf, binbuflen);
#endif /* SQUELCH_ENABLE */
    return "";
}

char /*@ observer @*/ *gpsd_hexdump(const void *binbuf, size_t binbuflen)
{
    static char hexbuf[MAX_PACKET_LENGTH*2+1];
#ifndef SQUELCH_ENABLE
    size_t i, j = 0;
    size_t len = (size_t)((binbuflen > MAX_PACKET_LENGTH) ? MAX_PACKET_LENGTH : binbuflen);
    const char *ibuf = (const char *)binbuf;
    const char *hexchar = "0123456789abcdef";

    if (NULL == binbuf || 0 == binbuflen) 
	return "";

    /*@ -shiftimplementation @*/
    for (i = 0; i < len; i++) {
	hexbuf[j++] = hexchar[ (ibuf[i]&0xf0)>>4 ];
	hexbuf[j++] = hexchar[ ibuf[i]&0x0f ];
    }
    /*@ +shiftimplementation @*/
    hexbuf[j] ='\0';
#else /* SQUELCH defined */
    hexbuf[0] = '\0';
#endif /* SQUELCH_ENABLE */
    return hexbuf;
}

int gpsd_hexpack(char *src, char *dst, int len){
    int i, k, l;

    l = (int)(strlen(src) / 2);
    if ((l < 1) || (l > len))
	return -1;

    bzero(dst, len);
    for (i = 0; i < l; i++)
	if ((k = hex2bin(src+i*2)) != -1)
	    dst[i] = (char)(k & 0xff);
	else
	    return -1;
    return l;
}

/*@ +charint -shiftimplementation @*/
int hex2bin(char *s)
{
    int a, b;

    a = s[0] & 0xff;
    b = s[1] & 0xff;

    if ((a >= 'a') && (a <= 'f'))
	a = a + 10 - 'a';
    else if ((a >= 'A') && (a <= 'F'))
	a = a + 10 - 'A';
    else if ((a >= '0') && (a <= '9'))
	a -= '0';
    else
	return -1;

    if ((b >= 'a') && (b <= 'f'))
	b = b + 10 - 'a';
    else if ((b >= 'A') && (b <= 'F'))
	b = b + 10 - 'A';
    else if ((b >= '0') && (b <= '9'))
	b -= '0';
    else
	return -1;

    return ((a<<4) + b);
}
/*@ -charint +shiftimplementation @*/
