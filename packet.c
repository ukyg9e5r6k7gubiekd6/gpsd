/****************************************************************************

NAME:
   packet.c -- a packet-sniffing engine for reading from GPS devices

DESCRIPTION:

Initial conditions of the problem:

1. We have a file descriptor open for (possibly non-blockin) read. The device 
   on the other end is sending packets at us.  

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
#include "gpsd.h"

#ifdef TESTMAIN
#include <stdarg.h>

static int verbose = 0;

void gpsd_report(int errlevel, const char *fmt, ... )
/* assemble command in printf(3) style, use stderr or syslog */
{
    if (errlevel <= verbose) {
	char buf[BUFSIZ];
	va_list ap;

	buf[0] = '\0';
	va_start(ap, fmt) ;
	(void)vsnprintf(buf + strlen(buf), sizeof(buf)-strlen(buf), fmt, ap);
	va_end(ap);

	(void)fputs(buf, stderr);
    }
}
#endif /* TESTMAIN */

/* 
 * The packet-recognition state machine.  It doesn't do checksums,
 * caller is responsible for that part.  It can be fooled by garbage
 * that looks like the head of a binary packet followed by a NMEA
 * packet; in that case it won't reset until it notices that the
 * binary trailer is not where it should be, and the NMEA packet will
 * be lost.  The reverse scenario is not possible because none of the
 * binary leader character can occur in an NMEA packet.  Caller should
 * consume a packet when it sees one of the *_RECOGNIZED states.
 *
 * Error handling is brutally simple; any time we see an unexpected
 * character, go to GROUND_STATE and reset the machine.  Because
 * another good packet will be usually along in less than a second
 * repeating the same data, Boyer-Moore-like attempts to do parallel
 * recognition beyond the headers would make no sense in this
 * application, they'd just add complexity.
 *
 * This state machine allows the following talker IDs:
 *      GP -- Global Positioning System.
 *      II -- Integrated Instrumentation (Raytheon's SeaTalk system).
 *	IN -- Integrated Navigation (Garmin uses this).
 */

enum {
   GROUND_STATE,	/* we don't know what packet type to expect */
   NMEA_DOLLAR,		/* we've seen first character of NMEA leader */

   NMEA_PUB_LEAD,	/* seen second character of NMEA G leader */
   NMEA_LEADER_END,	/* seen end char of NMEA leader, in body */
   NMEA_CR,	   	/* seen terminating \r of NMEA packet */
   NMEA_RECOGNIZED,	/* saw trailing \n of NMEA packet */

   SEATALK_LEAD_1,	/* SeaTalk/Garmin packet leader 'I' */

#ifdef TRIPMATE_ENABLE
   ASTRAL_1,		/* ASTRAL leader A */
   ASTRAL_2,	 	/* ASTRAL leader S */
   ASTRAL_3,		/* ASTRAL leader T */
   ASTRAL_4,		/* ASTRAL leader R */
   ASTRAL_5,		/* ASTRAL leader A */
#endif /* TRIPMATE_ENABLE */

#ifdef EARTHMATE_ENABLE
   EARTHA_1,		/* EARTHA leader E */
   EARTHA_2,		/* EARTHA leader A */
   EARTHA_3,		/* EARTHA leader R */
   EARTHA_4,		/* EARTHA leader T */
   EARTHA_5,		/* EARTHA leader H */
#endif /* EARTHMATE_ENABLE */

   SIRF_LEADER_1,	/* we've seen first character of SiRF leader */
   SIRF_LEADER_2,	/* seen second character of SiRF leader */
   SIRF_ACK_LEAD_1,	/* seen A of possible SiRF Ack */
   SIRF_ACK_LEAD_2,	/* seen c of possible SiRF Ack */
   SIRF_LENGTH_1,	/* seen first byte of SiRF length */
   SIRF_PAYLOAD,	/* we're in a SiRF payload part */
   SIRF_DELIVERED,	/* saw last byte of SiRF payload/checksum */
   SIRF_TRAILER_1,	/* saw first byte of SiRF trailer */ 
   SIRF_RECOGNIZED,	/* saw second byte of SiRF trailer */

   TSIP_LEADER,		/* we've seen the TSIP leader (DLE) */
   TSIP_PAYLOAD,	/* we're in TSIP payload */
   TSIP_DLE,		/* we've seen a DLE in TSIP payload */
   TSIP_RECOGNIZED,	/* found end of the TSIP packet */

   ZODIAC_EXPECTED,	/* expecting Zodiac packet */
   ZODIAC_LEADER_1,	/* saw leading 0xff */
   ZODIAC_LEADER_2,	/* saw leading 0x81 */
   ZODIAC_ID_1, 	/* saw first byte of ID */
   ZODIAC_ID_2, 	/* saw second byte of ID */
   ZODIAC_LENGTH_1,	/* saw first byte of Zodiac packet length */
   ZODIAC_LENGTH_2,	/* saw second byte of Zodiac packet length */
   ZODIAC_FLAGS_1, 	/* saw first byte of FLAGS */
   ZODIAC_FLAGS_2, 	/* saw second byte of FLAGS */
   ZODIAC_HSUM_1, 	/* saw first byte of Header sum */
   ZODIAC_PAYLOAD,	/* we're in a Zodiac payload */
   ZODIAC_RECOGNIZED,	/* found end of the Zodiac packet */
};

static void nexstate(struct gps_device_t *session, unsigned char c)
{
/*@ +charint */
    switch(session->packet_state)
    {
    case GROUND_STATE:
	if (c == '$')
	    session->packet_state = NMEA_DOLLAR;
#ifdef SIRFII_ENABLE
        else if (c == 0xa0)
	    session->packet_state = SIRF_LEADER_1;
#endif /* SIRFII_ENABLE */
#ifdef TSIP_ENABLE
        else if (c == 0x10)
	    session->packet_state = TSIP_LEADER;
#endif /* TSIP_ENABLE */
#ifdef TRIPMATE_ENABLE
        else if (c == 'A')
	    session->packet_state = ASTRAL_1;
#endif /* TRIPMATE_ENABLE */
#ifdef EARTHMATE_ENABLE
        else if (c == 'E')
	    session->packet_state = EARTHA_1;
#endif /* EARTHMATE_ENABLE */
#ifdef ZODIAC_ENABLE
	else if (c == 0xff)
	    session->packet_state = ZODIAC_LEADER_1;
#endif /* ZODIAC_ENABLE */
	break;
    case NMEA_DOLLAR:
	if (c == 'G')
	    session->packet_state = NMEA_PUB_LEAD;
	else if (c == 'P')	/* vendor sentence */
	    session->packet_state = NMEA_LEADER_END;
	else if (c =='I')	/* Seatalk */
	    session->packet_state = SEATALK_LEAD_1;
	else if (c =='A')	/* SiRF Ack */
	    session->packet_state = SIRF_ACK_LEAD_1;
	else
	    session->packet_state = GROUND_STATE;
	break;
    case NMEA_PUB_LEAD:
	if (c == 'P')
	    session->packet_state = NMEA_LEADER_END;
	else
	    session->packet_state = GROUND_STATE;
	break;
    case NMEA_LEADER_END:
	if (c == '\r')
	    session->packet_state = NMEA_CR;
	else if (c == '\n')
	    /* not strictly correct, but helps for interpreting logfiles */
	    session->packet_state = NMEA_RECOGNIZED;
	else if (!isprint(c))
	    session->packet_state = GROUND_STATE;
	break;
    case NMEA_CR:
	if (c == '\n')
	    session->packet_state = NMEA_RECOGNIZED;
	else
	    session->packet_state = GROUND_STATE;
	break;
    case NMEA_RECOGNIZED:
	if (c == '$')
	    session->packet_state = NMEA_DOLLAR;
	else
	    session->packet_state = GROUND_STATE;
	break;
    case SEATALK_LEAD_1:
	if (c == 'I' || c == 'N')	/* II or IN are accepted */
	    session->packet_state = NMEA_LEADER_END;
	else
	    session->packet_state = GROUND_STATE;
	break;
#ifdef TRIPMATE_ENABLE
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
#ifdef EARTHMATE_ENABLE
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
	    session->packet_state = EARTHA_4;
	else
	    session->packet_state = GROUND_STATE;
	break;
    case EARTHA_4:
	if (c == 'H')
	    session->packet_state = EARTHA_5;
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
	else
	    session->packet_state = GROUND_STATE;
	break;
   case SIRF_ACK_LEAD_2:
	if (c == 'k')
	    session->packet_state = NMEA_LEADER_END;
	else
	    session->packet_state = GROUND_STATE;
	break;
#ifdef SIRFII_ENABLE
    case SIRF_LEADER_1:
	if (c == 0xa2)
	    session->packet_state = SIRF_LEADER_2;
	else
	    session->packet_state = GROUND_STATE;
	break;
    case SIRF_LEADER_2:
	session->packet_length = (size_t)(c << 8);
	session->packet_state = SIRF_LENGTH_1;
	break;
    case SIRF_LENGTH_1:
	session->packet_length += c + 2;
	if (session->packet_length <= MAX_PACKET_LENGTH)
	    session->packet_state = SIRF_PAYLOAD;
	else
	    session->packet_state = GROUND_STATE;
	break;
    case SIRF_PAYLOAD:
	if (--session->packet_length == 0)
	    session->packet_state = SIRF_DELIVERED;
	break;
    case SIRF_DELIVERED:
	if (c == 0xb0)
	    session->packet_state = SIRF_TRAILER_1;
	else
	    session->packet_state = GROUND_STATE;
	break;
    case SIRF_TRAILER_1:
	if (c == 0xb3)
	    session->packet_state = SIRF_RECOGNIZED;
	else
	    session->packet_state = GROUND_STATE;
	break;
    case SIRF_RECOGNIZED:
        if (c == 0xa0)
	    session->packet_state = SIRF_LEADER_1;
	else
	    session->packet_state = GROUND_STATE;
	break;
#endif /* SIRFII_ENABLE */
#ifdef TSIP_ENABLE
    case TSIP_LEADER:
	if (c < 0x13)
	    session->packet_state = GROUND_STATE;
	else
	    session->packet_state = TSIP_PAYLOAD;
	break;
    case TSIP_PAYLOAD:
	if (c == 0x10)
	    session->packet_state = TSIP_DLE;
	break;
    case TSIP_DLE:
	switch (c)
	{
	case 0x03:
	    session->packet_state = TSIP_RECOGNIZED;
	    break;
	case 0x10:
	    session->packet_state = TSIP_PAYLOAD;
	    break;
	default:
	    session->packet_state = GROUND_STATE;
	    break;
	}
	break;
    case TSIP_RECOGNIZED:
        if (c == 0x10)
	    session->packet_state = TSIP_LEADER;
	else
	    session->packet_state = GROUND_STATE;
	break;
#endif /* TSIP_ENABLE */
#ifdef ZODIAC_ENABLE
    case ZODIAC_EXPECTED:
    case ZODIAC_RECOGNIZED:
	if (c == 0xff)
	    session->packet_state = ZODIAC_LEADER_1;
	else
	    session->packet_state = GROUND_STATE;
	break;
    case ZODIAC_LEADER_1:
	if (c == 0x81)
	    session->packet_state = ZODIAC_LEADER_2;
	else
	    session->packet_state = GROUND_STATE;
	break;
    case ZODIAC_LEADER_2:
	session->packet_state = ZODIAC_ID_1;
	break;
    case ZODIAC_ID_1:
	session->packet_state = ZODIAC_ID_2;
	break;
    case ZODIAC_ID_2:
	session->packet_length = (size_t)c;
	session->packet_state = ZODIAC_LENGTH_1;
	break;
    case ZODIAC_LENGTH_1:
	session->packet_length += (c << 8);
	session->packet_state = ZODIAC_LENGTH_2;
	break;
    case ZODIAC_LENGTH_2:
	session->packet_state = ZODIAC_FLAGS_1;
	break;
    case ZODIAC_FLAGS_1:
	session->packet_state = ZODIAC_FLAGS_2;
	break;
    case ZODIAC_FLAGS_2:
	session->packet_state = ZODIAC_HSUM_1;
	break;
    case ZODIAC_HSUM_1:
	{
 #define getword(i) (short)(session->inbuffer[2*(i)] | (session->inbuffer[2*(i)+1] << 8))
	    short sum = getword(0) + getword(1) + getword(2) + getword(3);
	    sum *= -1;
	    if (sum != getword(4)) {
		gpsd_report(4, "Zodiac Header checksum 0x%hx expecting 0x%hx\n", 
		       sum, getword(4));
		session->packet_state = GROUND_STATE;
		break;
	    }
	}
	gpsd_report(6,"Zodiac header id=%hd len=%hd flags=%hx\n", getword(1), getword(2), getword(3));
 #undef getword
	if (session->packet_length == 0) {
	    session->packet_state = ZODIAC_RECOGNIZED;
	    break;
	}
	session->packet_length *= 2;		/* word count to byte count */
	session->packet_length += 2;		/* checksum */
	/* 10 bytes is the length of the Zodiac header */
	if (session->packet_length <= MAX_PACKET_LENGTH - 10)
	    session->packet_state = ZODIAC_PAYLOAD;
	else
	    session->packet_state = GROUND_STATE;
	break;
    case ZODIAC_PAYLOAD:
	if (--session->packet_length == 0)
	    session->packet_state = ZODIAC_RECOGNIZED;
	break;
#endif /* ZODIAC_ENABLE */
    }
/*@ -charint */
}

#ifdef STATE_DEBUG
static char *buffer_dump(unsigned char *base, unsigned char *end)
/* dump the state of a specified buffer */
{
    static unsigned char buf[BUFSIZ];
    unsigned char *cp, *tp = buf;
    for (cp = base; cp < end; cp++)
	if (isgraph(*cp)) {
	    *tp++ = *cp;
	}else {
	    (void)snprintf(tp, sizeof(buf)-(tp-buf), "\\x%02x", *cp);
	    tp += 4;
	}
    *tp = '\0';
    return buf;
}
#endif /* STATE_DEBUG */

static void packet_accept(struct gps_device_t *session, int packet_type)
/* packet grab succeeded, move to output buffer */
{
    size_t packetlen = session->inbufptr-session->inbuffer;
    if (packetlen < sizeof(session->outbuffer)) {
	memcpy(session->outbuffer, session->inbuffer, packetlen);
	session->outbuflen = packetlen;
	session->outbuffer[packetlen] = '\0';
	session->packet_type = packet_type;
#ifdef STATE_DEBUG
	gpsd_report(6, "Packet type %d accepted %d = %s\n", 
		packet_type, packetlen,
		buffer_dump(session->outbuffer, 
			    session->outbuffer+session->outbuflen));
#endif /* STATE_DEBUG */
    } else {
	gpsd_report(1, "Rejected too long packet type %d len %d\n",
		packet_type,packetlen);
    }
}

static void packet_discard(struct gps_device_t *session)
/* shift the input buffer to discard all data up to current input pointer */ 
{
    size_t discard = session->inbufptr - session->inbuffer;
    size_t remaining = session->inbuflen - discard;
    session->inbufptr = memmove(session->inbuffer, 
				session->inbufptr, 
				remaining);
    session->inbuflen = remaining;
#ifdef STATE_DEBUG
    gpsd_report(6, "Packet discard of %d, chars remaining is %d = %s\n", 
		discard, remaining, 
		buffer_dump(session->inbuffer, 
			    session->inbuffer + session->inbuflen));
#endif /* STATE_DEBUG */
}

static void character_discard(struct gps_device_t *session)
/* shift the input buffer to discard one character and reread data */
{
    memmove(session->inbuffer, session->inbuffer+1, (size_t)--session->inbuflen);
    session->inbufptr = session->inbuffer;
#ifdef STATE_DEBUG
    gpsd_report(6, "Character discarded, buffer %d chars = %s\n",
		session->inbuflen,
		buffer_dump(session->inbuffer, 
			    session->inbuffer + session->inbuflen));
#endif /* STATE_DEBUG */
}

/* entry points begin here */

ssize_t packet_get(struct gps_device_t *session)
/* grab a packet; returns ether BAD_PACKET or the length */
{
    int newdata;
#ifndef TESTMAIN
    /*@ -modobserver @*/
    newdata = (int)read(session->gpsdata.gps_fd, session->inbufptr, 
			sizeof(session->inbuffer)-(session->inbufptr-session->inbuffer));
    /*@ +modobserver @*/
#else
    newdata = waiting;
#endif /* TESTMAIN */

    if (newdata < 0 && errno != EAGAIN)
	return BAD_PACKET;
    else if (newdata == 0 || (newdata < 0 && errno == EAGAIN))
	return 0;

#ifdef STATE_DEBUG
    gpsd_report(6, "Read %d chars to buffer offset %d (total %d): %s\n", 
		newdata,
		session->inbufptr-session->inbuffer,
		session->inbuflen+newdata, 
		buffer_dump(session->inbufptr, session->inbufptr + newdata));
#endif /* STATE_DEBUG */

    session->outbuflen = 0;
    session->inbuflen += newdata;
    while (session->inbufptr < session->inbuffer + session->inbuflen) {
	/*@ -modobserver @*/
	unsigned char c = *session->inbufptr++;
	/*@ +modobserver @*/
	nexstate(session, c);
	if (isprint(c))
	    gpsd_report(7, "Character '%c', new state: %d\n",c,session->packet_state);
	else
	    gpsd_report(7, "Character 0x%02x, new state: %d\n",c,session->packet_state);
	if (session->packet_state == GROUND_STATE) {
	    character_discard(session);
	} else if (session->packet_state == NMEA_RECOGNIZED) {
	    bool checksum_ok = true;
	    char csum[3];
	    char *trailer = (char *)session->inbufptr-5;
	    if (*trailer == '*') {
		unsigned int n, crc = 0;
		for (n = 1; (char *)session->inbuffer + n < trailer; n++)
		    crc ^= session->inbuffer[n];
		(void)snprintf(csum, sizeof(csum), "%02X", crc);
		checksum_ok = (toupper(csum[0])==toupper(trailer[1])
				&& toupper(csum[1])==toupper(trailer[2]));
	    }
	    if (checksum_ok)
		packet_accept(session, NMEA_PACKET);
	    session->packet_state = GROUND_STATE;
	    packet_discard(session);
	} else if (session->packet_state == SIRF_RECOGNIZED) {
	    unsigned char *trailer = session->inbufptr-4;
	    unsigned int checksum = (unsigned)((trailer[0] << 8) | trailer[1]);
	    unsigned int n, crc = 0;
	    for (n = 4; n < (unsigned)(trailer - session->inbuffer); n++)
		crc += (int)session->inbuffer[n];
	    crc &= 0x7fff;
	    if (checksum == crc)
		packet_accept(session, SIRF_PACKET);
	    session->packet_state = GROUND_STATE;
	    packet_discard(session);
#ifdef TSIP_ENABLE
	} else if (session->packet_state == TSIP_RECOGNIZED) {
	    if ((session->inbufptr - session->inbuffer) >= 4)
		packet_accept(session, TSIP_PACKET);
	    session->packet_state = GROUND_STATE;
	    packet_discard(session);
#endif /* TSIP_ENABLE */
#ifdef ZODIAC_ENABLE
	} else if (session->packet_state == ZODIAC_RECOGNIZED) {
 #define getword(i) (short)(session->inbuffer[2*(i)] | (session->inbuffer[2*(i)+1] << 8))
	    short len, n, sum;
	    len = getword(2);
	    for (n = sum = 0; n < len; n++)
		sum += getword(5+n);
	    sum *= -1;
	    if (len == 0 || sum == getword(5 + len)) {
		packet_accept(session, ZODIAC_PACKET);
	    } else {
		gpsd_report(4,
		    "Zodiac Data checksum 0x%hx over length %hd, expecting 0x%hx\n", 
			sum, len, getword(5 + len));
	    }
	    session->packet_state = GROUND_STATE;
	    packet_discard(session);
#undef getword
#endif /* ZODIAC_ENABLE */
	}
    }

    return (ssize_t)session->outbuflen;
}

void packet_reset(struct gps_device_t *session)
/* return the packet machine to the ground state */
{
    session->packet_type = BAD_PACKET;
    session->packet_state = GROUND_STATE;
    session->inbuflen = 0;
    session->inbufptr = session->inbuffer;
}

#ifdef __UNUSED__
void packet_pushback(struct gps_device_t *session)
/* push back the last packet grabbed */
{
    if (session->outbuflen + session->inbuflen < MAX_PACKET_LENGTH) {
	memmove(session->inbuffer+session->outbuflen,
		session->inbuffer,
		session->inbuflen);
	memmove(session->inbuffer,
		session->outbuffer,
		session->outbuflen);
	session->inbuflen += session->outbuflen;
	session->outbuflen = 0;
    }
}
#endif /* __UNUSED */

#ifdef TESTMAIN
/* To build a test main, compile with cc -DTESTMAIN -g packet.c -o packet */

int main(int argc, char *argv[])
{
    struct map {
	char		*legend;
	unsigned char	test[MAX_PACKET_LENGTH+1];
	int		testlen;
	int		garbage_offset;
	int		type;
	int		initstate;
    };
    struct map tests[] = {
	/* NMEA tests */
	{
	    "NMEA packet with checksum (1)",
	    "$GPVTG,308.74,T,,M,0.00,N,0.0,K*68\r\n",
	    36,
	    0,
	    NMEA_PACKET,
	    GROUND_STATE,
	},
	{
	    "NMEA packet with checksum (2)",
	    "$GPGGA,110534.994,4002.1425,N,07531.2585,W,0,00,50.0,172.7,M,-33.8,M,0.0,0000*7A\r\n",
	    82,
	    0,
	    NMEA_PACKET,
	    GROUND_STATE,
	},
	{
	    "NMEA packet with checksum and 4 chars of leading garbage",
	    "\xff\xbf\x00\xbf$GPVTG,308.74,T,,M,0.00,N,0.0,K*68\r\n",
	    40,
	    4,
	    NMEA_PACKET,
	    GROUND_STATE,
	},
	{
	    "NMEA packet without checksum",
	    "$PSRF105,1\r\n",
	    12,
	    0,
	    NMEA_PACKET,
	    GROUND_STATE,
	},
	{
	    "NMEA packet with wrong checksum",
	    "$GPVTG,308.74,T,,M,0.00,N,0.0,K*28\r\n",
	    36,
	    0,
	    BAD_PACKET,
	    GROUND_STATE,
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
	    GROUND_STATE,
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
	    GROUND_STATE,
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
	    GROUND_STATE,
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
	    GROUND_STATE,
	},
	/* Zodiac tests */
	{
	    "Zodiac binary 1000 Geodetic Status Output Message",
	    {
		0xff, 0x81, 0xe8, 0x03, 0x31, 0x00, 0x00, 0x00, 0xe8, 0x79, 
		0x74, 0x0e, 0x00, 0x00, 0x24, 0x00, 0x24, 0x00, 0x04, 0x00, 
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x90, 0x03, 0x23, 0x00, 
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1d, 0x00, 0x06, 0x00, 
		0xcd, 0x07, 0x00, 0x00, 0x00, 0x00, 0x16, 0x00, 0x7b, 0x0d, 
		0x00, 0x00, 0x12, 0x6b, 0xa7, 0x04, 0x41, 0x75, 0x32, 0xf8, 
		0x03, 0x1f, 0x00, 0x00, 0xe6, 0xf2, 0x00, 0x00, 0x00, 0x00, 
		0x00, 0x00, 0x11, 0xf6, 0x00, 0x00, 0x00, 0x00, 0xc0, 0x40, 
		0xd9, 0x12, 0x90, 0xd0, 0x03, 0x00, 0x00, 0xa3, 0xe1, 0x11, 
		0x10, 0x27, 0x00, 0x00, 0x00, 0x00, 0x00, 0xa3, 0xe1, 0x11, 
		0x00, 0x00, 0x00, 0x00, 0xe0, 0x93, 0x04, 0x00, 0x04, 0xaa},
	    110,
	    0,
	    ZODIAC_PACKET,
	    ZODIAC_EXPECTED,
	},
    };

    struct map *mp;
    struct gps_device_t state;
    int st;
    unsigned char *cp;

    if (argc > 1)
	verbose = atoi(argv[1]); 

    for (mp = tests; mp < tests + sizeof(tests)/sizeof(tests[0]); mp++) {
	state.packet_type = BAD_PACKET;
	state.packet_state = mp->initstate;
	state.inbuflen = 0;
	memcpy(state.inbufptr = state.inbuffer, mp->test, mp->testlen);
	gpsd_report(2, "%s starts with state %d\n", mp->legend, mp->initstate);
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
		(void)printf(" 0x%02x", *cp);
	    else if (*cp == '\r')
		(void)fputs("\\r", stdout);
	    else if (*cp == '\n')
		(void)fputs("\\n", stdout);
	    else if (isprint(*cp))
		(void)putchar(*cp);
	    else
		(void)printf("\\x%02x", *cp);
	}
	(void)putchar('\n');
#endif /* DUMPIT */
    }
}
#endif /* TESTMAIN */
