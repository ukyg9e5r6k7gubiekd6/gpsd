/****************************************************************************

NAME:
   packet.c -- a packet-sniffing engine for reading from GPS devices

DESCRIPTION:

Initial conditions of the problem:

1. We have a file descriptor open for read. The device on the other end is 
   sending packets at us.  

2. It may require more than one read to gather a packet.  Reads may span packet
   boundaries.
  
3. There may be leading garbage before the first packet.  After the first
   start-of-packet, the input should be well-formed.

The problem: how do we recognize which kind of packet we're getting?

No need to handle Garmin binary, we know that type by the fact we're connected
to the driver.  But we need to be able to tell the others apart and 
distinguish them from baud barf.

***************************************************************************/
#include <ctype.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include "config.h"
#ifdef HAVE_SYS_FILIO_H
#include <sys/filio.h>	/* for FIONREAD on BSD systems */
#endif

#include "gpsd.h"

#ifdef TESTMAIN
#include <stdarg.h>

void gpsd_report(int errlevel, const char *fmt, ... )
/* assemble command in printf(3) style, use stderr or syslog */
{
    char buf[BUFSIZ];
    va_list ap;

    buf[0] = '\0';
    va_start(ap, fmt) ;
    vsnprintf(buf + strlen(buf), sizeof(buf)-strlen(buf), fmt, ap);
    va_end(ap);

    fputs(buf, stderr);
}
#endif /* TESTMAIN */

/* 
 * The packet-recognition state machine.  It doesn't do checksums,
 * caller is responsible for that part.  It can be fooled by garbage
 * that looks like the head of a SiRF packet followed by a NMEA
 * packet; in that case it won't reset until it notices that the SiRF
 * trailer is not where it should be, and the NMEA packet will be
 * lost.  The reverse scenario is not possible because the SiRF leader
 * characters can't occur in an NMEA packet.  Caller should consume a
 * packet when it sees one of the *_RECOGNIZED states.
 */

#define GROUND_STATE	0	/* we don't know what packet type to expect */
#define NMEA_DOLLAR	1	/* we've seen first charcter of NMEA leader */
#define SIRF_LEADER_1	2	/* we've seen first charcter of SiRF leader */
#define NMEA_PUB_LEAD	3	/* seen second character of NMEA G leader */
#define SIRF_LEADER_2	4	/* seen second character of SiRF leader */
#define ASTRAL_1	5	/* ASTRAL leader A */
#define ASTRAL_2	6	/* ASTRAL leader S */
#define ASTRAL_3	7	/* ASTRAL leader T */
#define ASTRAL_4	8	/* ASTRAL leader R */
#define ASTRAL_5	9	/* ASTRAL leader A */
#define EARTHA_1	10	/* EARTHA leader E */
#define EARTHA_2	11	/* EARTHA leader A */
#define EARTHA_3	12	/* EARTHA leader R */
#define EARTHA_4	13	/* EARTHA leader T */
#define EARTHA_5	14	/* EARTHA leader H */
#define NMEA_LEADER_END	15	/* seen end character of NMEA leader, in body */
#define SIRF_ACK_LEAD_1	16	/* seen A of possible SiRF Ack */
#define SIRF_ACK_LEAD_2	17	/* seen c of possible SiRF Ack */
#define SIRF_LENGTH_1	18	/* seen first byte of SiRF length */
#define NMEA_CR		19	/* seen terminating \r of NMEA packet */
#define SIRF_PAYLOAD	20	/* we're in a SiRF payload part */
#define NMEA_RECOGNIZED	21	/* saw trailing \n of NMEA packet */
#define SIRF_DELIVERED	22	/* saw last byte of SiRF payload/checksum */
#define NMEA_EXPECTED	23	/* expecting NMEA packet */
#define SIRF_TRAILER_1	24	/* saw first byte of SiRF trailer */ 
#define SIRF_RECOGNIZED	25	/* saw second byte of SiRF trailer */
#define SIRF_EXPECTED	26	/* expecting start of SiRF packet */

static void nexstate(struct gps_session_t *session, unsigned char c)
{
    switch(session->packet_state)
    {
    case GROUND_STATE:
	if (c == '$')
	    session->packet_state = NMEA_DOLLAR;
        else if (c == 0xa0)
	    session->packet_state = SIRF_LEADER_1;
#if TRIPMATE_ENABLE
        else if (c == 'A')
	    session->packet_state = ASTRAL_1;
#endif /* TRIPMATE_ENABLE */
#if EARTHMATE_ENABLE
        else if (c == 'E')
	    session->packet_state = EARTHA_1;
#endif /* EARTHMATE_ENABLE */
	break;
    case NMEA_DOLLAR:
	if (c == 'G')
	    session->packet_state = NMEA_PUB_LEAD;
	else if (c == 'P')	/* vendor sentence */
	    session->packet_state = NMEA_LEADER_END;
	else if (c =='A')	/* SiRF Ack */
	    session->packet_state = SIRF_ACK_LEAD_1;
	else if (session->packet_type == NMEA_PACKET)
	   session->packet_state = NMEA_EXPECTED;
	else if (session->packet_type == SIRF_PACKET)
	    session->packet_state = SIRF_EXPECTED;
	else
	    session->packet_state = GROUND_STATE;
	break;
    case SIRF_LEADER_1:
	if (c == 0xa2)
	    session->packet_state = SIRF_LEADER_2;
	else if (session->packet_type == NMEA_PACKET)
	   session->packet_state = NMEA_EXPECTED;
	else if (session->packet_type == SIRF_PACKET)
	    session->packet_state = SIRF_EXPECTED;
	else
	    session->packet_state = GROUND_STATE;
	break;
#if TRIPMATE_ENABLE
    case ASTRAL_1:
	if (c == 'S')
	    session->packet_state = ASTRAL_2;
	else
	    session->packet_state = GROUND_STATE;
	break;
    case ASTRAL_2:
	if (c == 'T')
	    session->packet_state = ASTRAL_3;
	else
	    session->packet_state = GROUND_STATE;
	break;
    case ASTRAL_3:
	if (c == 'R')
	    session->packet_state = ASTRAL_5;
	else
	    session->packet_state = GROUND_STATE;
	break;
    case ASTRAL_4:
	if (c == 'A')
	    session->packet_state = ASTRAL_2;
	else
	    session->packet_state = GROUND_STATE;
	break;
    case ASTRAL_5:
	if (c == 'L')
	    session->packet_state = NMEA_RECOGNIZED;
	else
	    session->packet_state = GROUND_STATE;
	break; 
#endif /* TRIPMATE_ENABLE */
#if EARTHMATE_ENABLE
    case EARTHA_1:
	if (c == 'A')
	    session->packet_state = EARTHA_2;
	else
	    session->packet_state = GROUND_STATE;
	break;
    case EARTHA_2:
	if (c == 'R')
	    session->packet_state = EARTHA_3;
	else
	    session->packet_state = GROUND_STATE;
	break;
    case EARTHA_3:
	if (c == 'T')
	    session->packet_state = EARTHA_5;
	else
	    session->packet_state = GROUND_STATE;
	break;
    case EARTHA_4:
	if (c == 'H')
	    session->packet_state = EARTHA_2;
	else
	    session->packet_state = GROUND_STATE;
	break;
    case EARTHA_5:
	if (c == 'A')
	    session->packet_state = NMEA_RECOGNIZED;
	else
	    session->packet_state = GROUND_STATE;
	break; 
#endif /* EARTHMATE_ENABLE */
   case SIRF_ACK_LEAD_1:
	if (c == 'c')
	    session->packet_state = SIRF_ACK_LEAD_2;
	else if (session->packet_type == NMEA_PACKET)
	   session->packet_state = NMEA_EXPECTED;
	else if (session->packet_type == SIRF_PACKET)
	    session->packet_state = SIRF_EXPECTED;
	else
	    session->packet_state = GROUND_STATE;
	break;
   case SIRF_ACK_LEAD_2:
	if (c == 'k')
	    session->packet_state = NMEA_LEADER_END;
	else if (session->packet_type == NMEA_PACKET)
	   session->packet_state = NMEA_EXPECTED;
	else if (session->packet_type == SIRF_PACKET)
	    session->packet_state = SIRF_EXPECTED;
	else
	    session->packet_state = GROUND_STATE;
	break;
   case NMEA_PUB_LEAD:
	if (c == 'P')
	    session->packet_state = NMEA_LEADER_END;
	else if (session->packet_type == NMEA_PACKET)
	   session->packet_state = NMEA_EXPECTED;
	else if (session->packet_type == SIRF_PACKET)
	    session->packet_state = SIRF_EXPECTED;
	else
	    session->packet_state = GROUND_STATE;
	break;
    case SIRF_LEADER_2:
	session->packet_length = c << 8;
	session->packet_state = SIRF_LENGTH_1;
	break;
    case NMEA_LEADER_END:
	if (c == '\r')
	    session->packet_state = NMEA_CR;
	else if (isprint(c))
	    /* continue gathering body packets */;
	else if (session->packet_type == NMEA_PACKET)
	   session->packet_state = NMEA_EXPECTED;
	else if (session->packet_type == SIRF_PACKET)
	    session->packet_state = SIRF_EXPECTED;
	else
	    session->packet_state = GROUND_STATE;
	break;
    case SIRF_LENGTH_1:
	session->packet_length += c + 2;
	if (session->packet_length <= MAX_PACKET_LENGTH)
	    session->packet_state = SIRF_PAYLOAD;
	else if (session->packet_type == NMEA_PACKET)
	   session->packet_state = NMEA_EXPECTED;
	else if (session->packet_type == SIRF_PACKET)
	    session->packet_state = SIRF_EXPECTED;
	else
	    session->packet_state = GROUND_STATE;
	break;
    case NMEA_CR:
	if (c == '\n')
	    session->packet_state = NMEA_RECOGNIZED;
	else if (session->packet_type == NMEA_PACKET)
	   session->packet_state = NMEA_EXPECTED;
	else if (session->packet_type == SIRF_PACKET)
	    session->packet_state = SIRF_EXPECTED;
	else
	    session->packet_state = GROUND_STATE;
	break;
    case SIRF_PAYLOAD:
	if (--session->packet_length == 0)
	    session->packet_state = SIRF_DELIVERED;
	break;
    case NMEA_RECOGNIZED:
    case NMEA_EXPECTED:
	if (c == '$')
	    session->packet_state = NMEA_DOLLAR;
	else
	    session->packet_state = NMEA_EXPECTED;
	break;
    case SIRF_DELIVERED:
	if (c == 0xb0)
	    session->packet_state = SIRF_TRAILER_1;
	else if (session->packet_type == NMEA_PACKET)
	   session->packet_state = NMEA_EXPECTED;
	else if (session->packet_type == SIRF_PACKET)
	    session->packet_state = SIRF_EXPECTED;
	else
	    session->packet_state = GROUND_STATE;
	break;
    case SIRF_TRAILER_1:
	if (c == 0xb3)
	    session->packet_state = SIRF_RECOGNIZED;
	else if (session->packet_type == NMEA_PACKET)
	   session->packet_state = NMEA_EXPECTED;
	else if (session->packet_type == SIRF_PACKET)
	    session->packet_state = SIRF_EXPECTED;
	else
	    session->packet_state = GROUND_STATE;
	break;
    case SIRF_RECOGNIZED:
    case SIRF_EXPECTED:
        if (c == 0xa0)
	    session->packet_state = SIRF_LEADER_1;
	else
	    session->packet_state = SIRF_EXPECTED;
	break;
    }
}

static void packet_copy(struct gps_session_t *session)
/* packet grab succeeded, move to output buffer */
{
    int packetlen = session->inbufptr-session->inbuffer;
    gpsd_report(6, "Packet copy\n");
    memcpy(session->outbuffer, session->inbuffer, packetlen);
    session->outbuffer[session->outbuflen = packetlen] = '\0';
}

static void packet_discard(struct gps_session_t *session)
/* packet grab failed, shift the input buffer to discard old data */ 
{
    int remaining = session->inbuffer + session->inbuflen - session->inbufptr;
#ifndef TESTMAIN
    gpsd_report(6, "Packet discard with %d remaining\n", remaining);
#endif /* TESTMAIN */
    memmove(session->inbuffer, 
	    session->inbufptr, 
	    remaining);
    session->inbufptr = session->inbuffer;
    session->inbuflen = remaining;
}

/* entry points begin here */

int packet_get(struct gps_session_t *session, int waiting)
{
#ifndef TESTMAIN
    int newdata = read(session->gNMEAdata.gps_fd, session->inbufptr, waiting);
#else
    int newdata = waiting;
#endif /* TESTMAIN */

    if (newdata < 0 && errno != EAGAIN)
	return BAD_PACKET;

    {
	unsigned char buf[BUFSIZ], *cp, *tp = buf;
	for (cp = session->inbufptr; cp < session->inbufptr + newdata; cp++)
	    if (isgraph(*cp))
		*tp++ = *cp;
	    else {
		sprintf(tp, "\\x%02x", *cp);
		tp += 4;
	    }
	*tp = '\0';
	gpsd_report(6, "Read %d chars (total %d): %s\n", newdata, session->inbuflen+newdata, buf);
    }


    session->outbuflen = 0;
    session->inbuflen += newdata;
    while (session->inbufptr < session->inbuffer + session->inbuflen) {
	unsigned char c = *session->inbufptr++;
	nexstate(session, c);
#ifdef TESTMAIN_OLD
	if (isprint(c))
	    printf("Character %c, new state: %d\n",c,session->packet_state);
	else
	    printf("Character %02x, new state: %d\n",c,session->packet_state);
#endif /* TESTMAIN */
	if (session->packet_state == GROUND_STATE) {
#ifdef TESTMAIN
	    gpsd_report(6, "Character discarded\n", session->inbufptr[-1]);
#endif /* TESTMAIN */
	    session->inbufptr = memmove(session->inbufptr-1, 
					session->inbufptr, 
					session->inbuffer + session->inbuflen - session->inbufptr 
		);
	    session->inbuflen--;
	} else if (session->packet_state == NMEA_RECOGNIZED) {
	    int checksum_ok = 1;
	    unsigned char csum[3];
	    unsigned char *trailer = session->inbufptr-5;
	    if (*trailer == '*') {
		unsigned int n, crc = 0;
		for (n = 1; session->inbuffer + n < trailer; n++)
		    crc ^= session->inbuffer[n];
		sprintf(csum, "%02X", crc);
		checksum_ok = (toupper(csum[0])==toupper(trailer[1])
				&& toupper(csum[1])==toupper(trailer[2]));
	    }
	    if (checksum_ok) {
		session->packet_type = NMEA_PACKET;
		packet_copy(session);
	    } else if (session->packet_type == NMEA_PACKET)
		session->packet_state = NMEA_EXPECTED;
	    else
		session->packet_state = GROUND_STATE;
	    packet_discard(session);
	} else if (session->packet_state == SIRF_RECOGNIZED) {
	    unsigned char *trailer = session->inbufptr-4;
	    int checksum = (trailer[0] << 8) | trailer[1];
	    unsigned int n, crc = 0;
	    for (n = 4; n < trailer - session->inbuffer; n++)
		crc += session->inbuffer[n];
	    crc &= 0x7fff;
	    if (checksum == crc) {
		session->packet_type = SIRF_PACKET;
		packet_copy(session);
	    } else if (session->packet_type == SIRF_PACKET)
		session->packet_state = SIRF_EXPECTED;
	    else
		session->packet_state = GROUND_STATE;
	    packet_discard(session);
	}
    }

    return session->outbuflen;
}

int packet_sniff(struct gps_session_t *session)
/* try to sync up with the packet stream */
{
    unsigned int n, count = 0;
    session->packet_type = BAD_PACKET;
    session->packet_state = GROUND_STATE;
    session->inbuflen = 0;
    session->inbufptr = session->inbuffer;

    for (n = 0; n < MAX_PACKET_LENGTH; n += count) {
	count = 0;
	if (ioctl(session->gNMEAdata.gps_fd, FIONREAD, &count) < 0)
	    return BAD_PACKET;
	if (count && packet_get(session, count))
	    return session->packet_type;
    }

    return BAD_PACKET;
}

#ifdef TESTMAIN
int main(int argc, char *argv[])
{
    struct map {
	char		*legend;
	unsigned char	test[MAX_PACKET_LENGTH+1];
	int		testlen;
	int		garbage_offset;
	int		type;
    };
    struct map tests[] = {
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
	    "NMEA packet with checksum and 4 chars of leading garbage",
	    "\xff\xbf\x00\xbf$GPVTG,308.74,T,,M,0.00,N,0.0,K*68\r\n",
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
	    "SiRF WAAS version ID with 3 chars of leading garbage",
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
    struct gps_session_t state;
    int st;
    unsigned char *cp;

    for (mp = tests; mp < tests + sizeof(tests)/sizeof(tests[0]); mp++) {
	printf("%s starts\n", mp->legend);
	state.packet_type = BAD_PACKET;
	state.packet_state = GROUND_STATE;
	memcpy(state.inbuffer, mp->test, mp->testlen);
	packet_reset(session);
	st = packet_get(&state, mp->testlen);
	if (state.packet_type != mp->type)
	    printf("%s test FAILED (packet type %d wrong).\n", mp->legend, st);
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
