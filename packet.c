/****************************************************************************

NAME:
   packet.c -- a packet-sniffing engine for reading from GPS devices

DESCRIPTION:

Initial conditions of the problem:

1. We have an file descriptor open for read. The device on the other end is 
   sending packets at us.  

2. It may require more than one read to gather a packet.  However, the
   data returned from a read never spans packet boundaries.
  
3. There may be leading garbage before the first packet.  After the first
   start-of-packet, the input should be well-formed.  If it isn't, we've
   got the wrong packet type.

The problem: how do we recognize which kind of packet we're getting?

No need to handle Garmin, we know that type by the fact we're connected
to the driver.  But we need to be able to tell the others apart and 
distinguish them from baud barf.

***************************************************************************/
#include <ctype.h>
#include <stdio.h>
#include <setjmp.h>
#include "gpsd.h"

#define TESTMAIN

/* start of header, someday */

/*
 * The point of the binary packets is to have higher bit density than NMEA.
 * So any one binary packet should take up less space than the longest
 * permissible NMEA packet.
 */
#define MAX_PACKET_LENGTH	NMEA_MAX

struct parse_state {
    int	fd;
    unsigned char inbuffer[MAX_PACKET_LENGTH+1];
    unsigned short inbuflen;
    unsigned char *inbufptr;
    unsigned char outbuffer[MAX_PACKET_LENGTH+1];
    unsigned short outbuflen;
    jmp_buf packet_error;
};

#define BAD_PACKET	-1
#define NMEA_PACKET	0
#define SIRF_PACKET	1
#define ZODIAC_PACKET	2

/* all between "start of header" and this line goes in the header someday */

static int getch(struct parse_state *pstate)
{
    if (pstate->inbufptr >= pstate->inbuffer + pstate->inbuflen)
    {
	int st;

#ifndef TESTMAIN
	st = read(pstate->fd, 
		  pstate->inbuffer + pstate->inbuflen, 
		  MAX_PACKET_LENGTH - pstate->inbuflen);
	if (st < 0)
#endif /* !TESTMAIN */
	    longjmp(pstate->packet_error, 1);
	pstate->inbuflen += st;
    }
    return pstate->outbuffer[pstate->outbuflen++] = *pstate->inbufptr++;
}

int packet_reread(struct parse_state *pstate)
/* packet type grab failed, set us up for reread */
{
    pstate->inbufptr = pstate->inbuffer;
    pstate->outbuflen = 0;
    return 0;
}

void packet_discard(struct parse_state *pstate)
/* discard data in the packet-out buffer */
{
    memset(pstate->outbuffer, '\0', sizeof(pstate->outbuffer)); 
    pstate->outbuflen = 0;
}

void packet_flush(struct parse_state *pstate)
/* discard data in the buffer and prepare to receive a packet */
{
    memset(pstate->inbuffer, '\0', sizeof(pstate->inbuffer)); 
    pstate->inbuflen = 0;
    pstate->inbufptr = pstate->inbuffer;
}

void packet_shift(struct parse_state *pstate)
/* packet grab failed, shift the input buffer to discard a character */ 
{
    pstate->inbufptr = memmove(pstate->inbuffer, 
			       pstate->inbuffer+1, 
			       --pstate->inbuflen);
    pstate->inbuffer[pstate->inbuflen] = '\0';
    packet_discard(pstate);	/* clear out copied garbage */
}

static int get_nmea(struct parse_state *pstate)
{
    unsigned short checksum, crc, n;
    unsigned char c, csum[3], *trailer;

    if (getch(pstate) !='$')
	return packet_reread(pstate);
    c = getch(pstate);
    if (c == 'G' && getch(pstate) == 'P')	/* NMEA sentence */
	/* OK */;
    else if (c == 'P')				/* vendor private extension */
	/* OK */;
    else
	return packet_reread(pstate);
    for (;;) {
	c = getch(pstate);
	if (c == '\r')
	    break;
	if (!isprint(c))
	    return packet_reread(pstate);
	if (pstate->outbuflen > NMEA_MAX)
	    return packet_reread(pstate);
    }
    if (getch(pstate) != '\n')
	return packet_reread(pstate);
    if (*(trailer = &pstate->outbuffer[pstate->outbuflen-5]) == '*') {
	crc = 0;
	for (n = 1; n < pstate->outbuflen-5; n++)
	    crc ^= pstate->outbuffer[n];
	sprintf(csum, "%02X", crc);
	if (toupper(csum[0])!=toupper(trailer[1])
	    || toupper(csum[1])!=toupper(trailer[2]))
	    return packet_reread(pstate);
    }
    pstate->outbuffer[pstate->outbuflen] = '\0';
    packet_flush(pstate);
    return 1;
}

static int get_sirf(struct parse_state *pstate)
{
    unsigned short length, n, checksum, crc;

    if (getch(pstate) != 0xa0 || getch(pstate) != 0xa2)
	return packet_reread(pstate);
    length = (getch(pstate) << 8) | getch(pstate);
    if (length > MAX_PACKET_LENGTH-6)
	return packet_reread(pstate);
    for (n = 0; n < length; n++)
	getch(pstate);
    checksum = (getch(pstate) << 8) | getch(pstate);
    crc = 0;
    for (n = 0; n < length; n++)
	crc += pstate->outbuffer[4+n];
    crc &= 0x7fff;
    if (crc != checksum)
	return packet_reread(pstate);
    if (getch(pstate) != 0xb0 || getch(pstate) != 0xb3)
	return packet_reread(pstate);
    packet_flush(pstate);
    return 1;
}

static int get_zodiac(struct parse_state *pstate)
{
    unsigned short length, n, checksum, crc;

    if (getch(pstate) != 0x81 || getch(pstate) != 0x00)
	return packet_reread(pstate);
    getch(pstate); getch(pstate);	/* skip the ID */
    length = (getch(pstate) << 8) | getch(pstate);
    if (length > MAX_PACKET_LENGTH-10)
	return packet_reread(pstate);
    checksum = (getch(pstate) << 8) | getch(pstate);
    for (n = 0; n < length; n++)
	getch(pstate);
    crc = 0;
    for (n = 0; n < length; n++)
	crc += pstate->outbuffer[10+n];
    if (-crc != checksum)
	return packet_reread(pstate);
    packet_flush(pstate);
    return 1;
}

/* entry points begin here */

int packet_sniff(struct parse_state *pstate)
{
    if (setjmp(pstate->packet_error))
	return BAD_PACKET;
    else {
	packet_discard(pstate);
	for (;;) {
	    if (get_nmea(pstate)) {
		pstate->outbuffer[pstate->outbuflen] = '\0';
		return NMEA_PACKET;
	    } else if (get_sirf(pstate))
		return SIRF_PACKET;
	    else if (get_zodiac(pstate))
		return ZODIAC_PACKET;
	    else
		packet_shift(pstate);
	}
    }
}

#define MAKE_PACKET_GRABBER(outname, inname, maxlength)	int \
	outname(struct parse_state *pstate) \
	{ \
	    int maxgarbage = maxlength; \
	    packet_discard(pstate); \
	    while (maxgarbage--) { \
		if (inname(pstate)) { \
		    return 1; \
		} else \
		    packet_shift(pstate); \
	    } \
	    return 0; \
	}

MAKE_PACKET_GRABBER(package_get_nmea, get_nmea, NMEA_MAX * 3)
// MAKE_PACKET_GRABBER(package_get_sirf, get_sirf, MAX_PACKET_LENGTH)
// MAKE_PACKET_GRABBER(package_get_zodiac, get_zodiac, MAX_PACKET_LENGTH)

#ifdef TESTMAIN


int main(int argc, char *argv[])
{
    struct map {
	char		*legend;
	unsigned char	test[MAX_PACKET_LENGTH];
	int		testlen;
	int		garbage_offset;
	int		type;
    };
    struct map testloads[] = {
	/* NMEA tests */
	{
	    "NMEA packet with checksum (1)",
	    "$GPVTG,308.74,T,,M,0.00,N,0.0,K*68\r\n",
	    36,
	    0,
	    NMEA_PACKET,
	},
	{
	    "NMEA packet with checksum (2)",
	    "$GPGGA,110534.994,4002.1425,N,07531.2585,W,0,00,50.0,172.7,M,-33.8,M,0.0,0000*7A\r\n",
	    82,
	    0,
	    NMEA_PACKET,
	},
	{
	    "NMEA packet with checksum and leading garbage",
	    "\000\277\000\277$GPVTG,308.74,T,,M,0.00,N,0.0,K*68\r\n",
	    40,
	    4,
	    NMEA_PACKET,
	},
	{
	    "NMEA packet without checksum",
	    "$PSRF105,1\r\n",
	    12,
	    0,
	    NMEA_PACKET,
	},
	{
	    "NMEA packet with wrong checksum",
	    "$GPVTG,308.74,T,,M,0.00,N,0.0,K*28\r\n",
	    36,
	    0,
	    BAD_PACKET,
	},
	/* SiRF tests */
	{
	    "SiRF WAAS version ID",
	    {
		0xA0, 0xA2, 0x00, 0x15, 
		0x06, 0x06, 0x31, 0x2E, 0x32, 0x2E, 0x30, 0x44, 
		0x4B, 0x49, 0x54, 0x31, 0x31, 0x39, 0x20, 0x53, 
		0x4D, 0x00, 0x00, 0x00, 0x00,
		0x03, 0x82, 0xB0, 0xB3},
	    29,
	    0,
	    SIRF_PACKET,
	},
	{
	    "SiRF WAAS version ID with leading garbage",
	    {
		0xff, 0x00, 0xff,
		0xA0, 0xA2, 0x00, 0x15, 
		0x06, 0x06, 0x31, 0x2E, 0x32, 0x2E, 0x30, 0x44, 
		0x4B, 0x49, 0x54, 0x31, 0x31, 0x39, 0x20, 0x53, 
		0x4D, 0x00, 0x00, 0x00, 0x00,
		0x03, 0x82, 0xB0, 0xB3},
	    32,
	    3,
	    SIRF_PACKET,
	},
	{
	    "SiRF WAAS version ID with wrong checksum",
	    {
		0xA0, 0xA2, 0x00, 0x15, 
		0x06, 0x06, 0x31, 0x2E, 0x32, 0x2E, 0x30, 0x44, 
		0x4B, 0x49, 0x54, 0x31, 0x31, 0x39, 0x20, 0x53, 
		0x4D, 0x00, 0x00, 0x00, 0x00,
		0x03, 0x00, 0xB0, 0xB3},
	    29,
	    0,
	    BAD_PACKET,
	},
	{
	    "SiRF WAAS version ID with bad length",
	    {
		0xA0, 0xA2, 0xff, 0x15, 
		0x06, 0x06, 0x31, 0x2E, 0x32, 0x2E, 0x30, 0x44, 
		0x4B, 0x49, 0x54, 0x31, 0x31, 0x39, 0x20, 0x53, 
		0x4D, 0x00, 0x00, 0x00, 0x00,
		0x03, 0x82, 0xB0, 0xB3},
	    29,
	    0,
	    BAD_PACKET,
	},
    };
    struct map *mp;
    struct parse_state state;
    int st;
    unsigned char *cp;

    for (mp = testloads; mp < testloads + sizeof(testloads)/sizeof(testloads[0]); mp++) {
	if (setjmp(state.packet_error))
	    st = BAD_PACKET;
	else {
	    packet_flush(&state);
	    memcpy(state.inbuffer, mp->test, state.inbuflen = mp->testlen);
	    state.inbufptr = state.inbuffer;
	    st = packet_sniff(&state);
	}
	if (st != mp->type)
	    printf("%s test FAILED (packet type wrong).\n", mp->legend);
	else if (memcmp(mp->test + mp->garbage_offset, state.outbuffer, state.outbuflen))
	    printf("%s test FAILED (data garbled).\n", mp->legend);
	else
	    printf("%s test succeeded.\n", mp->legend);
#ifdef DUMPIT
	for (cp = state.outbuffer; 
	     cp < state.outbuffer + state.outbuflen; 
	     cp++) {
	    if (st != NMEA_PACKET)
		printf(" 0x%02x", *cp);
	    else if (*cp == '\r')
		fputs("\\r", stdout);
	    else if (*cp == '\n')
		fputs("\\n", stdout);
	    else if (isprint(*cp))
		putchar(*cp);
	    else
		printf("\\x%02x", *cp);
	}
	putchar('\n');
#endif /* DUMPIT */
    }
}
#endif /* TESTMAIN */
