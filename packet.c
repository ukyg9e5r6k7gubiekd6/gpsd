/* $Id$ */
/****************************************************************************

NAME:
   packet.c -- a packet-sniffing engine for reading from GPS devices

DESCRIPTION:

Initial conditions of the problem:

1. We have a file descriptor open for (possibly non-blocking) read. The device 
   on the other end is sending packets at us.  

2. It may require more than one read to gather a packet.  Reads may span packet
   boundaries.
  
3. There may be leading garbage before the first packet.  After the first
   start-of-packet, the input should be well-formed.

The problem: how do we recognize which kind of packet we're getting?

No need to handle Garmin USB binary, we know that type by the fact we're 
connected to the Garmin kernel driver.  But we need to be able to tell the 
others apart and distinguish them from baud barf.

***************************************************************************/
#include <sys/types.h>
#include <ctype.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include "gpsd_config.h"
#include "gpsd.h"

/* 
 * The packet-recognition state machine.  It can be fooled by garbage
 * that looks like the head of a binary packet followed by a NMEA
 * packet; in that case it won't reset until it notices that the
 * binary trailer is not where it should be, and the NMEA packet will
 * be lost.  The reverse scenario is not possible because none of the
 * binary leader characters can occur in an NMEA packet.  Caller should
 * consume a packet when it sees one of the *_RECOGNIZED states.
 * It's good practice to follow the _RECOGNIZED transition with one
 * that recognizes a leader of the same packet type rather than
 * dropping back to ground state -- this for example will prevent
 * the state machine from hopping between recognizing TSIP and
 * EverMore packets that both start with a DLE.
 *
 * Error handling is brutally simple; any time we see an unexpected
 * character, go to GROUND_STATE and reset the machine (except that a
 * $ in an NMEA payload only resets back to NMEA_DOLLAR state).  Because
 * another good packet will be usually along in less than a second
 * repeating the same data, Boyer-Moore-like attempts to do parallel
 * recognition beyond the headers would make no sense in this
 * application, they'd just add complexity.
 *
 * This state machine allows the following talker IDs:
 *      GP -- Global Positioning System.
 *      II -- Integrated Instrumentation (Raytheon's SeaTalk system).
 *	IN -- Integrated Navigation (Garmin uses this).
 *
 */

enum {
#include "packet_states.h"
};

#define DLE	0x10
#define STX	0x02
#define ETX	0x03

static void nextstate(struct gps_device_t *session, unsigned char c)
{
#ifdef RTCM104_ENABLE
    enum isgpsstat_t	isgpsstat;    
#endif /* RTCM104_ENABLE */
/*@ +charint */
    switch(session->packet_state)
    {
    case GROUND_STATE:
#ifdef NMEA_ENABLE
	if (c == '$') {
	    session->packet_state = NMEA_DOLLAR;
	    break;
	}
#endif /* NMEA_ENABLE */

#ifdef TNT_ENABLE
        if (c == '@') {
	    session->packet_state = TNT_LEADER;
	    break;
	}
#endif
#ifdef SIRF_ENABLE
        if (c == 0xa0) {
	    session->packet_state = SIRF_LEADER_1;
	    break;
	}
#endif /* SIRF_ENABLE */
#if defined(TSIP_ENABLE) || defined(EVERMORE_ENABLE) || defined(GARMIN_ENABLE)
        if (c == DLE) {
	    session->packet_state = DLE_LEADER;
	    break;
	}
#endif /* TSIP_ENABLE || EVERMORE_ENABLE || GARMIN_ENABLE */
#ifdef TRIPMATE_ENABLE
        if (c == 'A') {
#ifdef RTCM104_ENABLE
	    (void)rtcm_decode(session, c);
#endif /* RTCM104_ENABLE */
	    session->packet_state = ASTRAL_1;
	    break;
	}
#endif /* TRIPMATE_ENABLE */
#ifdef EARTHMATE_ENABLE
        if (c == 'E') {
#ifdef RTCM104_ENABLE
	    (void)rtcm_decode(session, c);
#endif /* RTCM104_ENABLE */
	    session->packet_state = EARTHA_1;
	    break;
	}
#endif /* EARTHMATE_ENABLE */
#ifdef ZODIAC_ENABLE
	if (c == 0xff) {
	    session->packet_state = ZODIAC_LEADER_1;
	    break;
	}
#endif /* ZODIAC_ENABLE */
#ifdef ITALK_ENABLE
	if (c == '<') {
	    session->packet_state = ITALK_LEADER_1;
	    break;
	}
#endif /* ITALK_ENABLE */
#ifdef RTCM104_ENABLE
	if (rtcm_decode(session, c) == ISGPS_SYNC) {
	    session->packet_state = RTCM_SYNC_STATE;
	    break;
	}
#endif /* RTCM104_ENABLE */
	break;
	/*@ +casebreak @*/
#ifdef NMEA_ENABLE
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
#ifdef TNT_ENABLE
    case TNT_LEADER:
          session->packet_state = NMEA_LEADER_END;
        break;
#endif
    case NMEA_LEADER_END:
	if (c == '\r')
	    session->packet_state = NMEA_CR;
	else if (c == '\n')
	    /* not strictly correct, but helps for interpreting logfiles */
	    session->packet_state = NMEA_RECOGNIZED;
	else if (c == '$')
	    /* faster recovery from missing sentence trailers */
	    session->packet_state = NMEA_DOLLAR;
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
	if (c == 'S') {
#ifdef RTCM104_ENABLE
	    (void)rtcm_decode(session, c);
#endif /* RTCM104_ENABLE */
	    session->packet_state = ASTRAL_2;
	} else
	    session->packet_state = GROUND_STATE;
	break;
    case ASTRAL_2:
	if (c == 'T') {
#ifdef RTCM104_ENABLE
	    (void)rtcm_decode(session, c);
#endif /* RTCM104_ENABLE */
	    session->packet_state = ASTRAL_3;
	} else
	    session->packet_state = GROUND_STATE;
	break;
    case ASTRAL_3:
	if (c == 'R') {
#ifdef RTCM104_ENABLE
	    (void)rtcm_decode(session, c);
#endif /* RTCM104_ENABLE */
	    session->packet_state = ASTRAL_5;
	} else
	    session->packet_state = GROUND_STATE;
	break;
    case ASTRAL_4:
	if (c == 'A') {
#ifdef RTCM104_ENABLE
	    (void)rtcm_decode(session, c);
#endif /* RTCM104_ENABLE */
	    session->packet_state = ASTRAL_2;
	} else
	    session->packet_state = GROUND_STATE;
	break;
    case ASTRAL_5:
	if (c == 'L') {
#ifdef RTCM104_ENABLE
	    (void)rtcm_decode(session, c);
#endif /* RTCM104_ENABLE */
	    session->packet_state = NMEA_RECOGNIZED;
	} else
	    session->packet_state = GROUND_STATE;
	break;
#endif /* TRIPMATE_ENABLE */
#ifdef EARTHMATE_ENABLE
    case EARTHA_1:
	if (c == 'A') {
#ifdef RTCM104_ENABLE
	    (void)rtcm_decode(session, c);
#endif /* RTCM104_ENABLE */
	    session->packet_state = EARTHA_2;
	} else
	    session->packet_state = GROUND_STATE;
	break;
    case EARTHA_2:
	if (c == 'R') {
#ifdef RTCM104_ENABLE
	    (void)rtcm_decode(session, c);
#endif /* RTCM104_ENABLE */
	    session->packet_state = EARTHA_3;
	} else
	    session->packet_state = GROUND_STATE;
	break;
    case EARTHA_3:
	if (c == 'T') {
#ifdef RTCM104_ENABLE
	    (void)rtcm_decode(session, c);
#endif /* RTCM104_ENABLE */
	    session->packet_state = EARTHA_4;
	} else
	    session->packet_state = GROUND_STATE;
	break;
    case EARTHA_4:
	if (c == 'H') {
#ifdef RTCM104_ENABLE
	    (void)rtcm_decode(session, c);
#endif /* RTCM104_ENABLE */
	    session->packet_state = EARTHA_5;
	} else
	    session->packet_state = GROUND_STATE;
	break;
    case EARTHA_5:
	if (c == 'A') {
#ifdef RTCM104_ENABLE
	    (void)rtcm_decode(session, c);
#endif /* RTCM104_ENABLE */
	    session->packet_state = NMEA_RECOGNIZED;
	} else
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
#endif /* NMEA_ENABLE */
#ifdef SIRF_ENABLE
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
#endif /* SIRF_ENABLE */
#if defined(TSIP_ENABLE) || defined(EVERMORE_ENABLE) || defined(GARMIN_ENABLE)
    case DLE_LEADER:
#ifdef EVERMORE_ENABLE
	if (c == STX)
	    session->packet_state = EVERMORE_LEADER_2;
	else
#endif /* EVERMORE_ENABLE */
#if defined(TSIP_ENABLE) || defined(GARMIN_ENABLE)
	/* garmin is special case of TSIP */
	/* check last because there's no checksum */
	if (c >= 0x13)
	    session->packet_state = TSIP_PAYLOAD;
	else
#endif /* TSIP_ENABLE */
	    session->packet_state = GROUND_STATE;
	break;
#endif /* TSIP_ENABLE || EVERMORE_ENABLE || GARMIN_ENABLE */
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
		gpsd_report(LOG_IO, "Zodiac Header checksum 0x%hx expecting 0x%hx\n", 
		       sum, getword(4));
		session->packet_state = GROUND_STATE;
		break;
	    }
	}
	gpsd_report(LOG_RAW+1,"Zodiac header id=%hd len=%hd flags=%hx\n", getword(1), getword(2), getword(3));
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
#ifdef EVERMORE_ENABLE
    case EVERMORE_LEADER_1:
	if (c == STX)
	    session->packet_state = EVERMORE_LEADER_2;
	else
	    session->packet_state = GROUND_STATE;
	break;
    case EVERMORE_LEADER_2:
	session->packet_length = (size_t)c;
	if (c == DLE)
	    session->packet_state = EVERMORE_PAYLOAD_DLE;
	else
	    session->packet_state = EVERMORE_PAYLOAD;
	break;
    case EVERMORE_PAYLOAD:
	if (c == DLE)
	    session->packet_state = EVERMORE_PAYLOAD_DLE;
	else if (--session->packet_length == 0)
	    session->packet_state = GROUND_STATE;
	break;
    case EVERMORE_PAYLOAD_DLE:
        switch (c) {
           case DLE: session->packet_state = EVERMORE_PAYLOAD; break;
           case ETX: session->packet_state = EVERMORE_RECOGNIZED; break;
           default: session->packet_state = GROUND_STATE;
        }
    break;
    case EVERMORE_RECOGNIZED:
        if (c == DLE)
	    session->packet_state = EVERMORE_LEADER_1;
	else
	    session->packet_state = GROUND_STATE;
	break;
#endif /* EVERMORE_ENABLE */
#ifdef ITALK_ENABLE
    case ITALK_LEADER_1:
        if (c == '!')
	    session->packet_state = ITALK_LEADER_2;
	else
	    session->packet_state = GROUND_STATE;
	break;
    case ITALK_LEADER_2:
	session->packet_length = (size_t)(c & 0xff);
	session->packet_state = ITALK_LENGTH;
	break;
    case ITALK_LENGTH:
	session->packet_length += 1;	/* fix number of words in payload */
	session->packet_length *= 2;	/* convert to number of bytes */
	session->packet_state = ITALK_PAYLOAD;
	break;
    case ITALK_PAYLOAD:
	/* lookahead for "<!" because sometimes packets are short but valid */
	if ((c == '>') && (session->inbufptr[0] == '<') && (session->inbufptr[1] == '!'))
	    session->packet_state = ITALK_RECOGNIZED;
	else if (--session->packet_length == 0)
	    session->packet_state = ITALK_DELIVERED;
	break;
    case ITALK_DELIVERED:
	if (c == '>')
	    session->packet_state = ITALK_RECOGNIZED;
	else
	    session->packet_state = GROUND_STATE;
	break;
    case ITALK_RECOGNIZED:
        if (c == '<')
	    session->packet_state = ITALK_LEADER_1;
	else
	    session->packet_state = GROUND_STATE;
	break;
#endif /* ITALK_ENABLE */
#ifdef TSIP_ENABLE
    case TSIP_LEADER:
        /* unused case */
	if (c >= 0x13)
	    session->packet_state = TSIP_PAYLOAD;
	else
	    session->packet_state = GROUND_STATE;
	break;
    case TSIP_PAYLOAD:
	if (c == DLE)
	    session->packet_state = TSIP_DLE;
	break;
    case TSIP_DLE:
	switch (c)
	{
	case ETX:
	    session->packet_state = TSIP_RECOGNIZED;
	    break;
	case DLE:
	    session->packet_state = TSIP_PAYLOAD;
	    break;
	default:
	    session->packet_state = GROUND_STATE;
	    break;
	}
	break;
    case TSIP_RECOGNIZED:
        if (c == DLE)
	    /*
	     * Don't go to TSIP_LEADER state -- TSIP packets aren't
	     * checksummed, so false positives are easy.  We might be
	     * looking at another DLE-stuffed protocol like EverMore
             * or Garmin streaming binary.
	     */
	    session->packet_state = DLE_LEADER;
	else
	    session->packet_state = GROUND_STATE;
	break;
#endif /* TSIP_ENABLE */
#ifdef RTCM104_ENABLE
    case RTCM_SYNC_STATE:
    case RTCM_SKIP_STATE:
	isgpsstat = rtcm_decode(session, c);
	if (isgpsstat == ISGPS_MESSAGE) {
	    session->packet_state = RTCM_RECOGNIZED;
	    break;
	} else if (isgpsstat == ISGPS_NO_SYNC)
	    session->packet_state = GROUND_STATE;
	break;

    case RTCM_RECOGNIZED:
	if (rtcm_decode(session, c) == ISGPS_SYNC) {
	    session->packet_state = RTCM_SYNC_STATE;
	    break;
	} else
	    session->packet_state = GROUND_STATE;
	break;
#endif /* RTCM104_ENABLE */
    }
/*@ -charint */
}

#define STATE_DEBUG

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
	gpsd_report(LOG_RAW+1, "Packet type %d accepted %d = %s\n",
		packet_type, packetlen,
		gpsd_hexdump(session->outbuffer, session->outbuflen));
#endif /* STATE_DEBUG */
    } else {
	gpsd_report(LOG_ERR, "Rejected too long packet type %d len %d\n",
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
    gpsd_report(LOG_RAW+1, "Packet discard of %d, chars remaining is %d = %s\n",
		discard, remaining,
		gpsd_hexdump(session->inbuffer, session->inbuflen));
#endif /* STATE_DEBUG */
}

static void character_discard(struct gps_device_t *session)
/* shift the input buffer to discard one character and reread data */
{
    memmove(session->inbuffer, session->inbuffer+1, (size_t)--session->inbuflen);
    session->inbufptr = session->inbuffer;
#ifdef STATE_DEBUG
    gpsd_report(LOG_RAW+1, "Character discarded, buffer %d chars = %s\n",
		session->inbuflen,
		gpsd_hexdump(session->inbuffer, session->inbuflen));
#endif /* STATE_DEBUG */
}


/* entry points begin here */

/* get 0-origin big-endian words relative to start of packet buffer */
#define getword(i) (short)(session->inbuffer[2*(i)] | (session->inbuffer[2*(i)+1] << 8))


ssize_t packet_parse(struct gps_device_t *session, size_t fix)
/* grab a packet; returns either BAD_PACKET or the length */
{
#ifdef STATE_DEBUG
    gpsd_report(LOG_RAW+1, "Read %d chars to buffer offset %d (total %d): %s\n",
		fix,
		session->inbuflen,
		session->inbuflen+fix,
		gpsd_hexdump(session->inbufptr, fix));
#endif /* STATE_DEBUG */

    session->outbuflen = 0;
    session->inbuflen += fix;
    while (session->inbufptr < session->inbuffer + session->inbuflen) {
	/*@ -modobserver @*/
	unsigned char c = *session->inbufptr++;
	/*@ +modobserver @*/
	char *state_table[] = {
#include "packet_names.h"
	};
	nextstate(session, c);
	gpsd_report(LOG_RAW+2, "%08ld: character '%c' [%02x], new state: %s\n",
		    session->char_counter, 
		    (isprint(c)?c:'.'), 
		    c, 
		    state_table[session->packet_state]);
	session->char_counter++;

	if (session->packet_state == GROUND_STATE) {
	    character_discard(session);
#ifdef NMEA_ENABLE
	} else if (session->packet_state == NMEA_RECOGNIZED) {
	    bool checksum_ok = true;
	    char csum[3];
	    char *trailer = (char *)session->inbufptr-5;
	    if (*trailer == '*') {
		unsigned int n, crc = 0;
		for (n = 1; (char *)session->inbuffer + n < trailer; n++)
		    crc ^= session->inbuffer[n];
		(void)snprintf(csum, sizeof(csum), "%02X", crc);
		checksum_ok = (csum[0]==toupper(trailer[1])
				&& csum[1]==toupper(trailer[2]));
	    }
	    if (checksum_ok)
		packet_accept(session, NMEA_PACKET);
	    else
		session->packet_state = GROUND_STATE;
	    packet_discard(session);
            break;
#endif /* NMEA_ENABLE */
#ifdef SIRF_ENABLE
	} else if (session->packet_state == SIRF_RECOGNIZED) {
	    unsigned char *trailer = session->inbufptr-4;
	    unsigned int checksum = (unsigned)((trailer[0] << 8) | trailer[1]);
	    unsigned int n, crc = 0;
	    for (n = 4; n < (unsigned)(trailer - session->inbuffer); n++)
		crc += (int)session->inbuffer[n];
	    crc &= 0x7fff;
	    if (checksum == crc)
		packet_accept(session, SIRF_PACKET);
	    else
		session->packet_state = GROUND_STATE;
	    packet_discard(session);
            break;
#endif /* SIRF_ENABLE */
#if defined(TSIP_ENABLE) || defined(GARMIN_ENABLE)
	} else if (session->packet_state == TSIP_RECOGNIZED) {
            size_t packetlen = session->inbufptr - session->inbuffer;
	    if ( packetlen < 5) {
		session->packet_state = GROUND_STATE;
            } else {
		unsigned int pkt_id, ch, chksum, len;
		size_t n;
#ifdef GARMIN_ENABLE
		n = 0;
		/*@ +charint */
		if (session->inbuffer[n++] != DLE) 
		    goto not_garmin;
		pkt_id = session->inbuffer[n++]; /* packet ID */
		len = session->inbuffer[n++];
		chksum = len + pkt_id;
		if (len == DLE) {
		    if (session->inbuffer[n++] != DLE)
			goto not_garmin;
		}
		for (; len > 0; len--) {
		    chksum += session->inbuffer[n];
		    if (session->inbuffer[n++] == DLE) {
			if (session->inbuffer[n++] != DLE)
			    goto not_garmin;
		    }
		}
		/* check sum byte */
		ch = session->inbuffer[n++];
		chksum += ch;
		if (ch == DLE) {
		    if (session->inbuffer[n++] != DLE)
			goto not_garmin;
		}
		if (session->inbuffer[n++] != DLE)
		    goto not_garmin;
		if (session->inbuffer[n++] != ETX) 
		    goto not_garmin;
		/*@ +charint */
		chksum &= 0xff;
		if (chksum) {
		    gpsd_report(LOG_IO,
				"Garmin checksum failed: %02x!=0\n",chksum);
		    goto not_garmin;
		}
		/* Debug
		   gpsd_report(LOG_IO, "Garmin n= %#02x\n %s\n", n,
		   gpsd_hexdump(session->inbuffer, packetlen));
		*/
		packet_accept(session, GARMIN_PACKET);
		packet_discard(session);
		break;
	    not_garmin:;
	        gpsd_report(LOG_RAW+1,"Not Garmin\n");
#endif /* GARMIN_ENABLE */
#ifdef TSIP_ENABLE
		/* check for 3 common TSIP packet types:
		 * 0x41, GPS time, data length 10
		 * 0x42, Single Precision Fix, data length 16
		 * 0x43, Velocity Fix, data length 20
		 *
		 * <DLE>[pkt id] [data] <DLE><ETX>
		 */
		n = 0;
		/*@ +charint @*/
		if (session->inbuffer[n++] != DLE)
		    goto not_tsip;
		pkt_id = session->inbuffer[n++]; /* packet ID */
		if ((0x41 > pkt_id) || (0x43 < pkt_id))
		    goto not_tsip;
		for ( len = 0; n < packetlen; len++ ) {
		    if (session->inbuffer[n++] == DLE) {
			if (session->inbuffer[n++] != DLE)
			    goto not_tsip;
		    }
		}
		/* look for terminating ETX */
		if (session->inbuffer[n++] != ETX)
		    goto not_tsip;
		/* Debug */
		gpsd_report(LOG_IO, "TSIP n= %#02x, len= %#02x\n", n, len); 
		/*@ -ifempty */
		if ((0x41 == pkt_id) && (DLE == len))
		    /* pass */;
		else if ((0x42 == pkt_id) && (0x16 == len ))
		    /* pass */;
		else if ((0x43 == pkt_id) && (0x20 == len))
		    /* pass */;
		else
		    goto not_tsip;
		/*@ -charint +ifempty @*/
		packet_accept(session, TSIP_PACKET);
		packet_discard(session);
		break;
	    not_tsip:
		/*
		 * More attempts to recognize ambiguous TSIP-like
		 * packet types could go here.
		 */
		session->packet_state = GROUND_STATE;
		packet_discard(session);
		break;
	    }
#endif /* TSIP_ENABLE */
#endif /* TSIP_ENABLE || GARMIN_ENABLE */
#ifdef ZODIAC_ENABLE
	} else if (session->packet_state == ZODIAC_RECOGNIZED) {
	    short len, n, sum;
	    len = getword(2);
	    for (n = sum = 0; n < len; n++)
		sum += getword(5+n);
	    sum *= -1;
	    if (len == 0 || sum == getword(5 + len)) {
		packet_accept(session, ZODIAC_PACKET);
	    } else {
		gpsd_report(LOG_IO,
		    "Zodiac data checksum 0x%hx over length %hd, expecting 0x%hx\n",
			sum, len, getword(5 + len));
		session->packet_state = GROUND_STATE;
	    }
	    packet_discard(session);
            break;
#endif /* ZODIAC_ENABLE */
#ifdef EVERMORE_ENABLE
	} else if (session->packet_state == EVERMORE_RECOGNIZED) {
	    unsigned int n, crc, checksum, len;
	    n = 0;
	    /*@ +charint */
	    if (session->inbuffer[n++] != DLE) 
		goto not_evermore;
	    if (session->inbuffer[n++] != STX)
		goto not_evermore;
	    len = session->inbuffer[n++];
	    if (len == DLE) {
		if (session->inbuffer[n++] != DLE)
		    goto not_evermore;
	    }
	    len -= 2;
	    crc = 0;
	    for (; len > 0; len--) {
		crc += session->inbuffer[n];
		if (session->inbuffer[n++] == DLE) {
		    if (session->inbuffer[n++] != DLE)
			goto not_evermore;
		}
	    }
	    checksum = session->inbuffer[n++];
	    if (checksum == DLE) {
		if (session->inbuffer[n++] != DLE)
		    goto not_evermore;
	    }
	    if (session->inbuffer[n++] != DLE)
		goto not_evermore;
	    if (session->inbuffer[n++] != ETX)
		goto not_evermore;
	    crc &= 0xff;
	    if (crc != checksum) {
		gpsd_report(LOG_IO, 
			    "EverMore checksum failed: %02x != %02x\n", 
			    crc, checksum);
		goto not_evermore;
	    }
	    /*@ +charint */
	    packet_accept(session, EVERMORE_PACKET);
	    packet_discard(session);
	    break;
	not_evermore:
	    session->packet_state = GROUND_STATE;
	    packet_discard(session);
            break;
#endif /* EVERMORE_ENABLE */
#ifdef ITALK_ENABLE
	} else if (session->packet_state == ITALK_RECOGNIZED) {
	    u_int16_t len, n, sum;
	    len = (unsigned short)(session->packet_length / 2 - 1);
	    /*
	     * Skip first 9 words so we compute checksum only over data
	     * portion of packet.
	     */
	    for (n = sum = 0; n < (unsigned short)(len - 9); n++)
		sum += getword(9 + n);
	    if (len == 0 || sum == (u_int16_t)getword(len+1)) {
		gpsd_report(4, "italk checksum ok\n");
		packet_accept(session, ITALK_PACKET);
	    } else {
		gpsd_report(4, "italk checksum failed\n");
		session->packet_state = GROUND_STATE;
	    }
	    packet_discard(session);
            break;
#endif /* ITALK_ENABLE */
#ifdef RTCM104_ENABLE
	} else if (session->packet_state == RTCM_RECOGNIZED) {
	    /*
	     * RTCM packets don't have checksums.  The six bits of parity 
	     * per word and the preamble better be good enough.
	     */
	    packet_accept(session, RTCM_PACKET);
	    session->packet_state = RTCM_SYNC_STATE;
	    packet_discard(session);
            break;
#endif /* RTCM104_ENABLE */
	}
    } /* while */

    return (ssize_t)fix;
}
#undef getword

ssize_t packet_get(struct gps_device_t *session)
/* grab a packet; returns either BAD_PACKET or the length */
{
    ssize_t fix;
    /*@ -modobserver @*/
    fix = read(session->gpsdata.gps_fd, session->inbuffer+session->inbuflen,
			sizeof(session->inbuffer)-(session->inbuflen));
    /*@ +modobserver @*/
    if (fix == -1) {
        if ((errno == EAGAIN) || (errno == EINTR)) {
	    return 0;
        } else {
	    return BAD_PACKET;
        }
    }

    if (fix == 0)
	return 0;
    return packet_parse(session, (size_t)fix);
}

void packet_reset(struct gps_device_t *session)
/* return the packet machine to the ground state */
{
    session->packet_type = BAD_PACKET;
    session->packet_state = GROUND_STATE;
    session->inbuflen = 0;
    session->inbufptr = session->inbuffer;
#ifdef BINARY_ENABLE
    isgps_init(session);
#endif /* BINARY_ENABLE */
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
	session->inbufptr += session->outbuflen;
	session->outbuflen = 0;
    }
}
#endif /* __UNUSED */
