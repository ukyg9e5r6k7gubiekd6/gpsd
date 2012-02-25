/*
 * Driver for AIS/AIVDM messages.
 *
 * See the file AIVDM.txt on the GPSD website for documentation and references.
 *
 * Code for message types 1-15, 18-21, and 24 has been tested against
 * live data with known-good decodings. Code for message types 16-17,
 * 22-23, and 25-27 has not.  The IMO special messages in types 6 and 8
 * are also untested.
 *
 * This file is Copyright (c) 2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 */
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "gpsd.h"
#include "bits.h"

/*
 * Parse the data from the device
 */

#define DAC1FID31_AIRTEMP_OFFSET		600
#define DAC1FID31_DEWPOINT_OFFSET		200
#define DAC1FID31_PRESSURE_OFFSET		800
#define DAC1FID11_LEVEL_OFFSET			10
#define DAC1FID31_LEVEL_OFFSET			100
#define DAC1FID31_WATERTEMP_OFFSET		100

static void from_sixbit(char *bitvec, uint start, int count, char *to)
{
    /*@ +type @*/
#ifdef S_SPLINT_S
    /* the real string causes a splint internal error */
    const char sixchr[] = "abcd";
#else
    const char sixchr[64] =
	"@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^- !\"#$%&'()*+,-./0123456789:;<=>?";
#endif /* S_SPLINT_S */
    int i;
    char newchar;

    /* six-bit to ASCII */
    for (i = 0; i < count - 1; i++) {
	newchar = sixchr[ubits(bitvec, start + 6 * i, 6U)];
	if (newchar == '@')
	    break;
	else
	    to[i] = newchar;
    }
    to[i] = '\0';
    /* trim spaces on right end */
    for (i = count - 2; i >= 0; i--)
	if (to[i] == ' ' || to[i] == '@')
	    to[i] = '\0';
	else
	    break;
    /*@ -type @*/
}

/*@ +charint -fixedformalarray -usedef -branchstate @*/
bool aivdm_decode(const char *buf, size_t buflen,
		  struct aivdm_context_t ais_contexts[AIVDM_CHANNELS],
		  struct ais_t *ais,
		  int debug)
{
#ifdef __UNUSED_DEBUG__
    char *sixbits[64] = {
	"000000", "000001", "000010", "000011", "000100",
	"000101", "000110", "000111", "001000", "001001",
	"001010", "001011", "001100", "001101", "001110",
	"001111", "010000", "010001", "010010", "010011",
	"010100", "010101", "010110", "010111", "011000",
	"011001", "011010", "011011", "011100", "011101",
	"011110", "011111", "100000", "100001", "100010",
	"100011", "100100", "100101", "100110", "100111",
	"101000", "101001", "101010", "101011", "101100",
	"101101", "101110", "101111", "110000", "110001",
	"110010", "110011", "110100", "110101", "110110",
	"110111", "111000", "111001", "111010", "111011",
	"111100", "111101", "111110", "111111",
    };
#endif /* __UNUSED_DEBUG__ */
    int nfrags, ifrag, nfields = 0;
    unsigned char *field[NMEA_MAX*2];
    unsigned char fieldcopy[NMEA_MAX*2+1];
    unsigned char *data, *cp;
    unsigned char ch, pad;
    struct aivdm_context_t *ais_context;
    bool imo;
    int i;
    unsigned int u;

    if (buflen == 0)
	return false;

    /* we may need to dump the raw packet */
    gpsd_report(LOG_PROG, "AIVDM packet length %zd: %s\n", buflen, buf);

    /* first clear the result, making sure we don't return garbage */
    memset(ais, 0, sizeof(*ais));

    /* discard overlong sentences */
    if (strlen(buf) > sizeof(fieldcopy)-1) {
	gpsd_report(LOG_ERROR, "overlong AIVDM packet.\n");
	return false;
    }

    /* extract packet fields */
    (void)strlcpy((char *)fieldcopy, buf, sizeof(fieldcopy));
    field[nfields++] = (unsigned char *)buf;
    for (cp = fieldcopy;
	 cp < fieldcopy + buflen; cp++)
	if (*cp == ',') {
	    *cp = '\0';
	    field[nfields++] = cp + 1;
	}

    /* discard sentences with exiguous commas; catches run-ons */
    if (nfields < 7) {
	gpsd_report(LOG_ERROR, "malformed AIVDM packet.\n");
	return false;
    }

    switch (field[4][0]) {
    /* FIXME: if fields[4] == "12", it doesn't detect the error */
    case '\0':
	/*
	 * Apparently an empty channel is normal for AIVDO sentences,
	 * which makes sense as they don't come in over radio.  This
	 * is going to break if there's ever an AIVDO type 24, though.
	 */
	if (strncmp((const char *)field[0], "!AIVDO", 6) != 0)
	    gpsd_report(LOG_ERROR, "invalid empty AIS channel. Assuming 'A'\n");
	ais_context = &ais_contexts[0];
	break;
    case '1':
	gpsd_report(LOG_ERROR, "invalid AIS channel 0x%0x '%c'. Assuming 'A'\n",
	                       field[4][0], (field[4][0] != '\0' ? field[4][0]:' '));
	/*@fallthrough@*/
    case 'A':
	ais_context = &ais_contexts[0];
	break;
    case '2':
	gpsd_report(LOG_ERROR, "invalid AIS channel '2'. Assuming 'B'.\n");
	/*@fallthrough@*/
    case 'B':
	ais_context = &ais_contexts[1];
	break;
    default:
	gpsd_report(LOG_ERROR, "invalid AIS channel 0x%0X .\n", field[4][0]);
	return false;
    }

    nfrags = atoi((char *)field[1]); /* number of fragments to expect */
    ifrag = atoi((char *)field[2]); /* fragment id */
    data = field[5];
    pad = field[6][0]; /* number of padding bits */
    gpsd_report(LOG_PROG, "nfrags=%d, ifrag=%d, decoded_frags=%d, data=%s\n",
		nfrags, ifrag, ais_context->decoded_frags, data);

    /* assemble the binary data */

    /* check fragment ordering */
    if (ifrag != ais_context->decoded_frags + 1) {
	gpsd_report(LOG_ERROR, "invalid fragment #%d received, expected #%d.\n",
	                       ifrag, ais_context->decoded_frags + 1);
	if (ifrag != 1)
	    return false;
        /* else, ifrag==1: Just discard all that was previously decoded and
         * simply handle that packet */
        ais_context->decoded_frags = 0;
    }
    if (ifrag == 1) {
	(void)memset(ais_context->bits, '\0', sizeof(ais_context->bits));
	ais_context->bitlen = 0;
    }

    /* wacky 6-bit encoding, shades of FIELDATA */
    /*@ +charint @*/
    for (cp = data; cp < data + strlen((char *)data); cp++) {
	ch = *cp;
	ch -= 48;
	if (ch >= 40)
	    ch -= 8;
#ifdef __UNUSED_DEBUG__
	gpsd_report(LOG_RAW, "%c: %s\n", *cp, sixbits[ch]);
#endif /* __UNUSED_DEBUG__ */
	/*@ -shiftnegative @*/
	for (i = 5; i >= 0; i--) {
	    if ((ch >> i) & 0x01) {
		ais_context->bits[ais_context->bitlen / 8] |=
		    (1 << (7 - ais_context->bitlen % 8));
	    }
	    ais_context->bitlen++;
	}
	/*@ +shiftnegative @*/
    }
    if (isdigit(pad))
	ais_context->bitlen -= (pad - '0');	/* ASCII assumption */
    /*@ -charint @*/

    /* time to pass buffered-up data to where it's actually processed? */
    if (ifrag == nfrags) {
	if (debug >= LOG_INF) { 
	    size_t clen = (ais_context->bitlen + 7) / 8;
	    gpsd_report(LOG_INF, "AIVDM payload is %zd bits, %zd chars: %s\n",
			ais_context->bitlen, clen,
			gpsd_hexdump((char *)ais_context->bits, clen));
	}

        /* clear waiting fragments count */
        ais_context->decoded_frags = 0;

#define BITS_PER_BYTE	8
#define UBITS(s, l)	ubits((char *)ais_context->bits, s, l)
#define SBITS(s, l)	sbits((char *)ais_context->bits, s, l)
#define UCHARS(s, to)	from_sixbit((char *)ais_context->bits, s, sizeof(to), to)
	ais->type = UBITS(0, 6);
	ais->repeat = UBITS(6, 2);
	ais->mmsi = UBITS(8, 30);
	gpsd_report(LOG_INF, "AIVDM message type %d, MMSI %09d:\n",
		    ais->type, ais->mmsi);
	/*
	 * Something about the shape of this switch statement confuses
	 * GNU indent so badly that there is no point in trying to be
	 * finer-grained than leaving it all alone.
	 */
	/* *INDENT-OFF* */
	switch (ais->type) {
	case 1:	/* Position Report */
	case 2:
	case 3:
	    if (ais_context->bitlen != 168) {
		gpsd_report(LOG_WARN, "AIVDM message type %d size not 168 bits (%zd).\n",
			    ais->type,
			    ais_context->bitlen);
		return false;
	    }
	    ais->type1.status		= UBITS(38, 4);
	    ais->type1.turn		= SBITS(42, 8);
	    ais->type1.speed		= UBITS(50, 10);
	    ais->type1.accuracy	        = UBITS(60, 1)!=0;
	    ais->type1.lon		= SBITS(61, 28);
	    ais->type1.lat		= SBITS(89, 27);
	    ais->type1.course		= UBITS(116, 12);
	    ais->type1.heading	        = UBITS(128, 9);
	    ais->type1.second		= UBITS(137, 6);
	    ais->type1.maneuver	        = UBITS(143, 2);
	    //ais->type1.spare	        = UBITS(145, 3);
	    ais->type1.raim		= UBITS(148, 1)!=0;
	    ais->type1.radio		= UBITS(149, 20);
	    break;
	case 4: 	/* Base Station Report */
	case 11:	/* UTC/Date Response */
	    if (ais_context->bitlen != 168) {
		gpsd_report(LOG_WARN, "AIVDM message type %d size not 168 bits (%zd).\n",
			    ais->type,
			    ais_context->bitlen);
		return false;
	    }
	    ais->type4.year		= UBITS(38, 14);
	    ais->type4.month		= UBITS(52, 4);
	    ais->type4.day		= UBITS(56, 5);
	    ais->type4.hour		= UBITS(61, 5);
	    ais->type4.minute		= UBITS(66, 6);
	    ais->type4.second		= UBITS(72, 6);
	    ais->type4.accuracy		= UBITS(78, 1)!=0;
	    ais->type4.lon		= SBITS(79, 28);
	    ais->type4.lat		= SBITS(107, 27);
	    ais->type4.epfd		= UBITS(134, 4);
	    //ais->type4.spare		= UBITS(138, 10);
	    ais->type4.raim		= UBITS(148, 1)!=0;
	    ais->type4.radio		= UBITS(149, 19);
	    break;
	case 5: /* Ship static and voyage related data */
	    if (ais_context->bitlen != 424) {
		gpsd_report(LOG_WARN, "AIVDM message type 5 size not 424 bits (%zd).\n",
			    ais_context->bitlen);
		return false;
	    }
	    ais->type5.ais_version  = UBITS(38, 2);
	    ais->type5.imo          = UBITS(40, 30);
	    UCHARS(70, ais->type5.callsign);
	    UCHARS(112, ais->type5.shipname);
	    ais->type5.shiptype     = UBITS(232, 8);
	    ais->type5.to_bow       = UBITS(240, 9);
	    ais->type5.to_stern     = UBITS(249, 9);
	    ais->type5.to_port      = UBITS(258, 6);
	    ais->type5.to_starboard = UBITS(264, 6);
	    ais->type5.epfd         = UBITS(270, 4);
	    ais->type5.month        = UBITS(274, 4);
	    ais->type5.day          = UBITS(278, 5);
	    ais->type5.hour         = UBITS(283, 5);
	    ais->type5.minute       = UBITS(288, 6);
	    ais->type5.draught      = UBITS(294, 8);
	    UCHARS(302, ais->type5.destination);
	    ais->type5.dte          = UBITS(422, 1);
	    //ais->type5.spare        = UBITS(423, 1);
	    break;
	case 6: /* Addressed Binary Message */
	    if (ais_context->bitlen < 88 || ais_context->bitlen > 1008) {
		gpsd_report(LOG_WARN, "AIVDM message type 6 size is out of range (%zd).\n",
			    ais_context->bitlen);
		return false;
	    }
	    ais->type6.seqno          = UBITS(38, 2);
	    ais->type6.dest_mmsi      = UBITS(40, 30);
	    ais->type6.retransmit     = UBITS(70, 1)!=0;
	    //ais->type6.spare        = UBITS(71, 1);
	    ais->type6.dac            = UBITS(72, 10);
	    ais->type6.fid            = UBITS(82, 6);
	    ais->type6.bitcount       = ais_context->bitlen - 88;
	    imo = false;
	    if (ais->type6.dac == 1)
		switch (ais->type6.fid) {
		case 12:	/* IMO236 - Dangerous cargo indication */
		    UCHARS(88, ais->type6.dac1fid12.lastport);
		    ais->type6.dac1fid12.lmonth		= UBITS(118, 4);
		    ais->type6.dac1fid12.lday		= UBITS(122, 5);
		    ais->type6.dac1fid12.lhour		= UBITS(127, 5);
		    ais->type6.dac1fid12.lminute	= UBITS(132, 6);
		    UCHARS(138, ais->type6.dac1fid12.nextport);
		    ais->type6.dac1fid12.nmonth		= UBITS(168, 4);
		    ais->type6.dac1fid12.nday		= UBITS(172, 5);
		    ais->type6.dac1fid12.nhour		= UBITS(177, 5);
		    ais->type6.dac1fid12.nminute	= UBITS(182, 6);
		    UCHARS(188, ais->type6.dac1fid12.dangerous);
		    UCHARS(308, ais->type6.dac1fid12.imdcat);
		    ais->type6.dac1fid12.unid		= UBITS(332, 13);
		    ais->type6.dac1fid12.amount		= UBITS(345, 10);
		    ais->type6.dac1fid12.unit		= UBITS(355, 2);
		    /* skip 3 bits */
		    break;
		case 14:	/* IMO236 - Tidal Window */
		    ais->type6.dac1fid32.month	= UBITS(88, 4);
		    ais->type6.dac1fid32.day	= UBITS(92, 5);
#define ARRAY_BASE 97
#define ELEMENT_SIZE 93
		    for (u = 0; ARRAY_BASE + (ELEMENT_SIZE*u) <= ais_context->bitlen; u++) {
			int a = ARRAY_BASE + (ELEMENT_SIZE*u);
			struct tidal_t *tp = &ais->type6.dac1fid32.tidals[u];
			tp->lat	= SBITS(a + 0, 27);
			tp->lon	= SBITS(a + 27, 28);
			tp->from_hour	= UBITS(a + 55, 5);
			tp->from_min	= UBITS(a + 60, 6);
			tp->to_hour	= UBITS(a + 66, 5);
			tp->to_min	= UBITS(a + 71, 6);
			tp->cdir	= UBITS(a + 77, 9);
			tp->cspeed	= UBITS(a + 86, 7);
		    }
		    ais->type6.dac1fid32.ntidals = i;
#undef ARRAY_BASE
#undef ELEMENT_SIZE
		    break;
		case 15:	/* IMO236 - Extended Ship Static and Voyage Related Data */
		    ais->type6.dac1fid15.airdraught	= UBITS(56, 11);
		    break;
		case 16:	/* IMO236 - Number of persons on board */
		    if (ais->type6.bitcount == 136)
			ais->type6.dac1fid16.persons = UBITS(88, 13);/* 289 */
		    else
			ais->type6.dac1fid16.persons = UBITS(55, 13);/* 236 */
		    imo = true;
		    break;
		case 18:	/* IMO289 - Clearance time to enter port */
		    ais->type6.dac1fid18.linkage	= UBITS(88, 10);
		    ais->type6.dac1fid18.month	= UBITS(98, 4);
		    ais->type6.dac1fid18.day	= UBITS(102, 5);
		    ais->type6.dac1fid18.hour	= UBITS(107, 5);
		    ais->type6.dac1fid18.minute	= UBITS(112, 6);
		    UCHARS(118, ais->type6.dac1fid18.portname);
		    UCHARS(238, ais->type6.dac1fid18.destination);
		    ais->type6.dac1fid18.lon	= SBITS(268, 25);
		    ais->type6.dac1fid18.lat	= SBITS(293, 24);
		    /* skip 43 bits */
		    break;
		case 20:	/* IMO289 - Berthing data - addressed */
		    ais->type6.dac1fid20.linkage	= UBITS(88, 10);
		    ais->type6.dac1fid20.berth_length	= UBITS(98, 9);
		    ais->type6.dac1fid20.berth_depth	= UBITS(107, 8);
		    ais->type6.dac1fid20.position	= UBITS(115, 3);
		    ais->type6.dac1fid20.month		= UBITS(118, 4);
		    ais->type6.dac1fid20.day		= UBITS(122, 5);
		    ais->type6.dac1fid20.hour		= UBITS(127, 5);
		    ais->type6.dac1fid20.minute		= UBITS(132, 6);
		    ais->type6.dac1fid20.availability	= UBITS(138, 1);
		    ais->type6.dac1fid20.agent		= UBITS(139, 2);
		    ais->type6.dac1fid20.fuel		= UBITS(141, 2);
		    ais->type6.dac1fid20.chandler	= UBITS(143, 2);
		    ais->type6.dac1fid20.stevedore	= UBITS(145, 2);
		    ais->type6.dac1fid20.electrical	= UBITS(147, 2);
		    ais->type6.dac1fid20.water		= UBITS(149, 2);
		    ais->type6.dac1fid20.customs	= UBITS(151, 2);
		    ais->type6.dac1fid20.cartage	= UBITS(153, 2);
		    ais->type6.dac1fid20.crane		= UBITS(155, 2);
		    ais->type6.dac1fid20.lift		= UBITS(157, 2);
		    ais->type6.dac1fid20.medical	= UBITS(159, 2);
		    ais->type6.dac1fid20.navrepair	= UBITS(161, 2);
		    ais->type6.dac1fid20.provisions	= UBITS(163, 2);
		    ais->type6.dac1fid20.shiprepair	= UBITS(165, 2);
		    ais->type6.dac1fid20.surveyor	= UBITS(167, 2);
		    ais->type6.dac1fid20.steam		= UBITS(169, 2);
		    ais->type6.dac1fid20.tugs		= UBITS(171, 2);
		    ais->type6.dac1fid20.solidwaste	= UBITS(173, 2);
		    ais->type6.dac1fid20.liquidwaste	= UBITS(175, 2);
		    ais->type6.dac1fid20.hazardouswaste	= UBITS(177, 2);
		    ais->type6.dac1fid20.ballast	= UBITS(179, 2);
		    ais->type6.dac1fid20.additional	= UBITS(181, 2);
		    ais->type6.dac1fid20.regional1	= UBITS(183, 2);
		    ais->type6.dac1fid20.regional2	= UBITS(185, 2);
		    ais->type6.dac1fid20.future1	= UBITS(187, 2);
		    ais->type6.dac1fid20.future2	= UBITS(189, 2);
		    UCHARS(191, ais->type6.dac1fid20.berth_name);
		    ais->type6.dac1fid20.berth_lon	= SBITS(311, 25);
		    ais->type6.dac1fid20.berth_lat	= SBITS(336, 24);
		    break;
		case 23:        /* IMO289 - Area notice - addressed */
		    break;
		case 25:	/* IMO289 - Dangerous cargo indication */
		    ais->type6.dac1fid25.unit 	= UBITS(88, 2);
		    ais->type6.dac1fid25.amount	= UBITS(90, 10);
		    for (i = 0;	100 + i*17 < (int)ais_context->bitlen; i++) {
			ais->type6.dac1fid25.cargos[i].code 	= UBITS(100 + i*17, 4);
			ais->type6.dac1fid25.cargos[i].subtype	= UBITS(104 + i*17, 13);
		    }
		    ais->type6.dac1fid25.ncargos = i;
		    break;
		case 28:	/* IMO289 - Route info - addressed */
		    ais->type6.dac1fid28.linkage	= UBITS(88, 10);
		    ais->type6.dac1fid28.sender		= UBITS(98, 3);
		    ais->type6.dac1fid28.rtype		= UBITS(101, 5);
		    ais->type6.dac1fid28.month		= UBITS(106, 4);
		    ais->type6.dac1fid28.day		= UBITS(110, 5);
		    ais->type6.dac1fid28.hour		= UBITS(115, 5);
		    ais->type6.dac1fid28.minute		= UBITS(120, 6);
		    ais->type6.dac1fid28.duration	= UBITS(126, 18);
		    ais->type6.dac1fid28.waycount	= UBITS(144, 5);
#define ARRAY_BASE 149
#define ELEMENT_SIZE 55
		    for (i = 0; i < ais->type6.dac1fid28.waycount; u++) {
			int a = ARRAY_BASE + (ELEMENT_SIZE*i);
			ais->type6.dac1fid28.waypoints[i].lon = SBITS(a+0, 28);
			ais->type6.dac1fid28.waypoints[i].lat = SBITS(a+28,27);
		    }
#undef ARRAY_BASE
#undef ELEMENT_SIZE
		    break;
		case 30:	/* IMO289 - Text description - addressed */
		    ais->type6.dac1fid30.linkage   = UBITS(88, 10);
		    from_sixbit((char *)ais_context->bits,
				98, ais_context->bitlen-98,
				ais->type6.dac1fid30.text);
		    break;
		case 32:	/* IMO289 - Tidal Window */
		    ais->type6.dac1fid32.month	= UBITS(88, 4);
		    ais->type6.dac1fid32.day	= UBITS(92, 5);
#define ARRAY_BASE 97
#define ELEMENT_SIZE 88
		    for (u = 0; ARRAY_BASE + (ELEMENT_SIZE*u) <= ais_context->bitlen; u++) {
			int a = ARRAY_BASE + (ELEMENT_SIZE*u);
			struct tidal_t *tp = &ais->type6.dac1fid32.tidals[u];
			tp->lon	= SBITS(a + 0, 25);
			tp->lat	= SBITS(a + 25, 24);
			tp->from_hour	= UBITS(a + 49, 5);
			tp->from_min	= UBITS(a + 54, 6);
			tp->to_hour	= UBITS(a + 60, 5);
			tp->to_min	= UBITS(a + 65, 6);
			tp->cdir	= UBITS(a + 71, 9);
			tp->cspeed	= UBITS(a + 80, 8);
		    }
		    ais->type6.dac1fid32.ntidals = u;
#undef ARRAY_BASE
#undef ELEMENT_SIZE
		    break;
		}
	    if (!imo)
		(void)memcpy(ais->type6.bitdata,
			     (char *)ais_context->bits + (88 / BITS_PER_BYTE),
			     (ais->type6.bitcount + 7) / 8);
	    break;
	case 7: /* Binary acknowledge */
	case 13: /* Safety Related Acknowledge */
	{
	    unsigned int mmsi[4];
	    if (ais_context->bitlen < 72 || ais_context->bitlen > 158) {
		gpsd_report(LOG_WARN, "AIVDM message type %d size is out of range (%zd).\n",
			    ais->type,
			    ais_context->bitlen);
		return false;
	    }
	    for (u = 0; u < sizeof(mmsi)/sizeof(mmsi[0]); u++)
		if (ais_context->bitlen > 40 + 32*u)
		    mmsi[u] = UBITS(40 + 32*u, 30);
		else
		    mmsi[u] = 0;
	    /*@ -usedef @*/
	    ais->type7.mmsi1 = mmsi[0];
	    ais->type7.mmsi2 = mmsi[1];
	    ais->type7.mmsi3 = mmsi[2];
	    ais->type7.mmsi4 = mmsi[3];
	    /*@ +usedef @*/
	    break;
	}
	case 8: /* Binary Broadcast Message */
	    if (ais_context->bitlen < 56 || ais_context->bitlen > 1008) {
		gpsd_report(LOG_WARN, "AIVDM message type 8 size is out of range (%zd).\n",
			    ais_context->bitlen);
		return false;
	    }
	    //ais->type8.spare        = UBITS(38, 2);
	    ais->type8.dac            = UBITS(40, 10);
	    ais->type8.fid            = UBITS(50, 6);
	    ais->type8.bitcount       = ais_context->bitlen - 56;
	    imo = false;
	    if (ais->type8.dac == 1)
		switch (ais->type8.fid) {
		case 11:        /* IMO236 - Meteorological/Hydrological data */
		    /* layout is almost identical to FID=31 from IMO289 */
		    ais->type8.dac1fid31.lat		= SBITS(56, 24);
		    ais->type8.dac1fid31.lon		= SBITS(80, 25);
		    ais->type8.dac1fid31.accuracy       = false;
		    ais->type8.dac1fid31.day		= UBITS(105, 5);
		    ais->type8.dac1fid31.hour		= UBITS(110, 5);
		    ais->type8.dac1fid31.minute		= UBITS(115, 6);
		    ais->type8.dac1fid31.wspeed		= UBITS(121, 7);
		    ais->type8.dac1fid31.wgust		= UBITS(128, 7);
		    ais->type8.dac1fid31.wdir		= UBITS(135, 9);
		    ais->type8.dac1fid31.wgustdir	= UBITS(144, 9); 
		    ais->type8.dac1fid31.airtemp	= SBITS(153, 11)
			- DAC1FID31_AIRTEMP_OFFSET;
		    ais->type8.dac1fid31.humidity	= UBITS(164, 7);
		    ais->type8.dac1fid31.dewpoint	= UBITS(171, 10)
			- DAC1FID31_DEWPOINT_OFFSET;
		    ais->type8.dac1fid31.pressure	= UBITS(181, 9)
			- DAC1FID31_PRESSURE_OFFSET;
		    ais->type8.dac1fid31.pressuretend	= UBITS(190, 2);
		    ais->type8.dac1fid31.visgreater     = false;
		    ais->type8.dac1fid31.visibility	= UBITS(192, 8);
		    ais->type8.dac1fid31.waterlevel	= UBITS(200, 9)
			- DAC1FID11_LEVEL_OFFSET;
		    ais->type8.dac1fid31.leveltrend	= UBITS(209, 2);
		    ais->type8.dac1fid31.cspeed		= UBITS(211, 8);
		    ais->type8.dac1fid31.cdir		= UBITS(219, 9);
		    ais->type8.dac1fid31.cspeed2	= UBITS(228, 8);
		    ais->type8.dac1fid31.cdir2		= UBITS(236, 9);
		    ais->type8.dac1fid31.cdepth2	= UBITS(245, 5);
		    ais->type8.dac1fid31.cspeed3	= UBITS(250, 8);
		    ais->type8.dac1fid31.cdir3		= UBITS(258, 9);
		    ais->type8.dac1fid31.cdepth3	= UBITS(267, 5);
		    ais->type8.dac1fid31.waveheight	= UBITS(272, 8);
		    ais->type8.dac1fid31.waveperiod	= UBITS(280, 6);
		    ais->type8.dac1fid31.wavedir	= UBITS(286, 9);
		    ais->type8.dac1fid31.swellheight	= UBITS(295, 8);
		    ais->type8.dac1fid31.swellperiod	= UBITS(303, 6);
		    ais->type8.dac1fid31.swelldir	= UBITS(309, 9);
		    ais->type8.dac1fid31.seastate	= UBITS(318, 4);
		    ais->type8.dac1fid31.watertemp	= UBITS(322, 10)
			- DAC1FID31_WATERTEMP_OFFSET;
		    ais->type8.dac1fid31.preciptype	= UBITS(332, 3);
		    ais->type8.dac1fid31.salinity	= UBITS(335, 9);
		    ais->type8.dac1fid31.ice		= UBITS(344, 2);
		    imo = true;
		    break;
		case 13:        /* IMO236 - Fairway closed */
		    UCHARS(56, ais->type8.dac1fid13.reason);
		    UCHARS(176, ais->type8.dac1fid13.closefrom);
		    UCHARS(296, ais->type8.dac1fid13.closeto);
		    ais->type8.dac1fid13.radius 	= UBITS(416, 10);
		    ais->type8.dac1fid13.extunit	= UBITS(426, 2);
		    ais->type8.dac1fid13.fday   	= UBITS(428, 5);
		    ais->type8.dac1fid13.fmonth 	= UBITS(433, 4);
		    ais->type8.dac1fid13.fhour  	= UBITS(437, 5);
		    ais->type8.dac1fid13.fminute	= UBITS(442, 6);
		    ais->type8.dac1fid13.tday   	= UBITS(448, 5);
		    ais->type8.dac1fid13.tmonth 	= UBITS(453, 4);
		    ais->type8.dac1fid13.thour  	= UBITS(457, 5);
		    ais->type8.dac1fid13.tminute	= UBITS(462, 6);
		    /* skip 4 bits */
		    break;
		case 15:        /* IMO236 - Extended ship and voyage */
		    ais->type8.dac1fid15.airdraught	= UBITS(56, 11);
		    /* skip 5 bits */
		    break;
		case 17:        /* IMO289 - VTS-generated/synthetic targets */
#define ARRAY_BASE 56
#define ELEMENT_SIZE 122
		    for (u = 0; ARRAY_BASE + (ELEMENT_SIZE*u) <= ais_context->bitlen; u++) {
			struct target_t *tp = &ais->type8.dac1fid17.targets[u];
			int a = ARRAY_BASE + (ELEMENT_SIZE*u);
			tp->idtype = UBITS(a + 0, 2);
			switch (tp->idtype) {
			case DAC1FID17_IDTYPE_MMSI:
			    tp->id.mmsi	= UBITS(a + 2, 42);
			    break;
			case DAC1FID17_IDTYPE_IMO:
			    tp->id.imo	= UBITS(a + 2, 42);
			    break;
			case DAC1FID17_IDTYPE_CALLSIGN:
			    UCHARS(a+2, tp->id.callsign);
			    break;
			default:
			    UCHARS(a+2, tp->id.other);
			    break;
			}
			/* skip 4 bits */
			tp->lat	= SBITS(a + 48, 24);
			tp->lon	= SBITS(a + 72, 25);
			tp->course	= UBITS(a + 97, 9);
			tp->second	= UBITS(a + 106, 6);
			tp->speed	= UBITS(a + 112, 10);
		    }
		    ais->type8.dac1fid17.ntargets = u;
#undef ARRAY_BASE
#undef ELEMENT_SIZE
		    break;
		case 19:        /* IMO289 - Marine Traffic Signal */
		    ais->type8.dac1fid19.linkage	= UBITS(56, 10);
		    UCHARS(66, ais->type8.dac1fid19.station);
		    ais->type8.dac1fid19.lon	= SBITS(186, 25);
		    ais->type8.dac1fid19.lat	= SBITS(211, 24);
		    ais->type8.dac1fid19.status	= UBITS(235, 2);
		    ais->type8.dac1fid19.signal	= UBITS(237, 5);
		    ais->type8.dac1fid19.hour	= UBITS(242, 5);
		    ais->type8.dac1fid19.minute	= UBITS(247, 6);
		    ais->type8.dac1fid19.nextsignal	= UBITS(253, 5);
		    /* skip 102 bits */
		    break;
		case 21:        /* IMO289 - Weather obs. report from ship */
		    break;
		case 22:        /* IMO289 - Area notice - broadcast */
		    break;
		case 24:        /* IMO289 - Extended ship static & voyage-related data */
		    break;
		case 26:        /* IMO289 - Environmental */
		    break;
		case 27:        /* IMO289 - Route information - broadcast */
		    ais->type8.dac1fid27.linkage	= UBITS(56, 10);
		    ais->type8.dac1fid27.sender	= UBITS(66, 3);
		    ais->type8.dac1fid27.rtype	= UBITS(69, 5);
		    ais->type8.dac1fid27.month	= UBITS(74, 4);
		    ais->type8.dac1fid27.day	= UBITS(78, 5);
		    ais->type8.dac1fid27.hour	= UBITS(83, 5);
		    ais->type8.dac1fid27.minute	= UBITS(88, 6);
		    ais->type8.dac1fid27.duration	= UBITS(94, 18);
		    ais->type8.dac1fid27.waycount	= UBITS(112, 5);
#define ARRAY_BASE 117
#define ELEMENT_SIZE 55
		    for (i = 0; i < ais->type8.dac1fid27.waycount; i++) {
			int a = ARRAY_BASE + (ELEMENT_SIZE*i);
			ais->type8.dac1fid27.waypoints[i].lon	= SBITS(a + 0, 28);
			ais->type8.dac1fid27.waypoints[i].lat	= SBITS(a + 28, 27);
		    }
#undef ARRAY_BASE
#undef ELEMENT_SIZE
		    break;
		case 29:        /* IMO289 - Text Description - broadcast */
		    ais->type8.dac1fid29.linkage   = UBITS(56, 10);
		    from_sixbit((char *)ais_context->bits,
				66, ais_context->bitlen-66,
				ais->type8.dac1fid29.text);
		    break;
		case 31:        /* IMO289 - Meteorological/Hydrological data */
		    ais->type8.dac1fid31.lat		= SBITS(56, 24);
		    ais->type8.dac1fid31.lon		= SBITS(80, 25);
		    ais->type8.dac1fid31.accuracy       = (bool)UBITS(105, 1);
		    ais->type8.dac1fid31.day		= UBITS(106, 5);
		    ais->type8.dac1fid31.hour		= UBITS(111, 5);
		    ais->type8.dac1fid31.minute		= UBITS(116, 6);
		    ais->type8.dac1fid31.wspeed		= UBITS(122, 7);
		    ais->type8.dac1fid31.wgust		= UBITS(129, 7);
		    ais->type8.dac1fid31.wdir		= UBITS(136, 9);
		    ais->type8.dac1fid31.wgustdir	= UBITS(145, 9); 
		    ais->type8.dac1fid31.airtemp	= SBITS(154, 11)
			- DAC1FID31_AIRTEMP_OFFSET;
		    ais->type8.dac1fid31.humidity	= UBITS(165, 7);
		    ais->type8.dac1fid31.dewpoint	= UBITS(172, 10)
			- DAC1FID31_DEWPOINT_OFFSET;
		    ais->type8.dac1fid31.pressure	= UBITS(182, 9)
			- DAC1FID31_PRESSURE_OFFSET;
		    ais->type8.dac1fid31.pressuretend	= UBITS(191, 2);
		    ais->type8.dac1fid31.visgreater	= UBITS(193, 1);
		    ais->type8.dac1fid31.visibility	= UBITS(194, 7);
		    ais->type8.dac1fid31.waterlevel	= UBITS(200, 12)
			- DAC1FID31_LEVEL_OFFSET;
		    ais->type8.dac1fid31.leveltrend	= UBITS(213, 2);
		    ais->type8.dac1fid31.cspeed		= UBITS(215, 8);
		    ais->type8.dac1fid31.cdir		= UBITS(223, 9);
		    ais->type8.dac1fid31.cspeed2	= UBITS(232, 8);
		    ais->type8.dac1fid31.cdir2		= UBITS(240, 9);
		    ais->type8.dac1fid31.cdepth2	= UBITS(249, 5);
		    ais->type8.dac1fid31.cspeed3	= UBITS(254, 8);
		    ais->type8.dac1fid31.cdir3		= UBITS(262, 9);
		    ais->type8.dac1fid31.cdepth3	= UBITS(271, 5);
		    ais->type8.dac1fid31.waveheight	= UBITS(276, 8);
		    ais->type8.dac1fid31.waveperiod	= UBITS(284, 6);
		    ais->type8.dac1fid31.wavedir	= UBITS(290, 9);
		    ais->type8.dac1fid31.swellheight	= UBITS(299, 8);
		    ais->type8.dac1fid31.swellperiod	= UBITS(307, 6);
		    ais->type8.dac1fid31.swelldir	= UBITS(313, 9);
		    ais->type8.dac1fid31.seastate	= UBITS(322, 4);
		    ais->type8.dac1fid31.watertemp	= UBITS(326, 10)
			- DAC1FID31_WATERTEMP_OFFSET;
		    ais->type8.dac1fid31.preciptype	= UBITS(336, 3);
		    ais->type8.dac1fid31.salinity	= UBITS(339, 9);
		    ais->type8.dac1fid31.ice		= UBITS(348, 2);
		    imo = true;
		    break;
		}
	    /* land here if we failed to match a known DAC/FID */
	    if (!imo)
		(void)memcpy(ais->type8.bitdata,
			 (char *)ais_context->bits + (56 / BITS_PER_BYTE),
			     (ais->type8.bitcount + 7) / 8);
	    break;
	case 9: /* Standard SAR Aircraft Position Report */
	    if (ais_context->bitlen != 168) {
		gpsd_report(LOG_WARN, "AIVDM message type 9 size not 168 bits (%zd).\n",
			    ais_context->bitlen);
		return false;
	    }
	    ais->type9.alt		= UBITS(38, 12);
	    ais->type9.speed		= UBITS(50, 10);
	    ais->type9.accuracy		= (bool)UBITS(60, 1);
	    ais->type9.lon		= SBITS(61, 28);
	    ais->type9.lat		= SBITS(89, 27);
	    ais->type9.course		= UBITS(116, 12);
	    ais->type9.second		= UBITS(128, 6);
	    ais->type9.regional		= UBITS(134, 8);
	    ais->type9.dte		= UBITS(142, 1);
	    //ais->type9.spare		= UBITS(143, 3);
	    ais->type9.assigned		= UBITS(146, 1)!=0;
	    ais->type9.raim		= UBITS(147, 1)!=0;
	    ais->type9.radio		= UBITS(148, 19);
	    break;
	case 10: /* UTC/Date inquiry */
	    if (ais_context->bitlen != 72) {
		gpsd_report(LOG_WARN, "AIVDM message type 10 size not 72 bits (%zd).\n",
			    ais_context->bitlen);
		return false;
	    }
	    //ais->type10.spare        = UBITS(38, 2);
	    ais->type10.dest_mmsi      = UBITS(40, 30);
	    //ais->type10.spare2       = UBITS(70, 2);
	    break;
	case 12: /* Safety Related Message */
	    if (ais_context->bitlen < 72 || ais_context->bitlen > 1008) {
		gpsd_report(LOG_WARN, "AIVDM message type 12 size is out of range (%zd).\n",
			    ais_context->bitlen);
		return false;
	    }
	    ais->type12.seqno          = UBITS(38, 2);
	    ais->type12.dest_mmsi      = UBITS(40, 30);
	    ais->type12.retransmit     = (bool)UBITS(70, 1);
	    //ais->type12.spare        = UBITS(71, 1);
	    from_sixbit((char *)ais_context->bits,
			72, ais_context->bitlen-72,
			ais->type12.text);
	    break;
	case 14:	/* Safety Related Broadcast Message */
	    if (ais_context->bitlen < 40 || ais_context->bitlen > 1008) {
		gpsd_report(LOG_WARN, "AIVDM message type 14 size is out of range (%zd).\n",
			    ais_context->bitlen);
		return false;
	    }
	    //ais->type14.spare          = UBITS(38, 2);
	    from_sixbit((char *)ais_context->bits,
			40, ais_context->bitlen-40,
			ais->type14.text);
	    break;
	case 15:	/* Interrogation */
	    if (ais_context->bitlen < 88 || ais_context->bitlen > 168) {
		gpsd_report(LOG_WARN, "AIVDM message type 15 size is out of range (%zd).\n",
			    ais_context->bitlen);
	        return false;
	    }
	    (void)memset(&ais->type15, '\0', sizeof(ais->type15));
	    //ais->type14.spare         = UBITS(38, 2);
	    ais->type15.mmsi1		= UBITS(40, 30);
	    ais->type15.type1_1		= UBITS(70, 6);
	    ais->type15.type1_1		= UBITS(70, 6);
	    ais->type15.offset1_1	= UBITS(76, 12);
	    //ais->type14.spare2        = UBITS(88, 2);
	    if (ais_context->bitlen > 90) {
		ais->type15.type1_2	= UBITS(90, 6);
		ais->type15.offset1_2	= UBITS(96, 12);
		//ais->type14.spare3    = UBITS(108, 2);
		if (ais_context->bitlen > 110) {
		    ais->type15.mmsi2	= UBITS(110, 30);
		    ais->type15.type2_1	= UBITS(140, 6);
		    ais->type15.offset2_1	= UBITS(146, 12);
		    //ais->type14.spare4	= UBITS(158, 2);
		}
	    }
	    break;
	case 16:	/* Assigned Mode Command */
	    if (ais_context->bitlen != 96 && ais_context->bitlen != 144) {
		gpsd_report(LOG_WARN, "AIVDM message type 16 size is out of range (%zd).\n",
			    ais_context->bitlen);
		return false;
	    }
	    ais->type16.mmsi1		= UBITS(40, 30);
	    ais->type16.offset1		= UBITS(70, 12);
	    ais->type16.increment1	= UBITS(82, 10);
	    if (ais_context->bitlen < 144)
		ais->type16.mmsi2=ais->type16.offset2=ais->type16.increment2 = 0;
	    else {
		ais->type16.mmsi2	= UBITS(92, 30);
		ais->type16.offset2	= UBITS(122, 12);
		ais->type16.increment2	= UBITS(134, 10);
	    }
	    break;
	case 17:	/* GNSS Broadcast Binary Message */
	    if (ais_context->bitlen < 80 || ais_context->bitlen > 816) {
		gpsd_report(LOG_WARN, "AIVDM message type 17 size is out of range (%zd).\n",
			    ais_context->bitlen);
		return false;
	    }
	    //ais->type17.spare         = UBITS(38, 2);
	    ais->type17.lon		= UBITS(40, 18);
	    ais->type17.lat		= UBITS(58, 17);
	    //ais->type17.spare	        = UBITS(75, 4);
	    ais->type17.bitcount        = ais_context->bitlen - 80;
	    (void)memcpy(ais->type17.bitdata,
			 (char *)ais_context->bits + (80 / BITS_PER_BYTE),
			 (ais->type17.bitcount + 7) / 8);
	    break;
	case 18:	/* Standard Class B CS Position Report */
	    if (ais_context->bitlen != 168) {
		gpsd_report(LOG_WARN, "AIVDM message type 18 size not 168 bits (%zd).\n",
			    ais_context->bitlen);
		return false;
	    }
	    ais->type18.reserved	= UBITS(38, 8);
	    ais->type18.speed		= UBITS(46, 10);
	    ais->type18.accuracy	= UBITS(56, 1)!=0;
	    ais->type18.lon		= SBITS(57, 28);
	    ais->type18.lat		= SBITS(85, 27);
	    ais->type18.course		= UBITS(112, 12);
	    ais->type18.heading		= UBITS(124, 9);
	    ais->type18.second		= UBITS(133, 6);
	    ais->type18.regional	= UBITS(139, 2);
	    ais->type18.cs		= UBITS(141, 1)!=0;
	    ais->type18.display 	= UBITS(142, 1)!=0;
	    ais->type18.dsc     	= UBITS(143, 1)!=0;
	    ais->type18.band    	= UBITS(144, 1)!=0;
	    ais->type18.msg22   	= UBITS(145, 1)!=0;
	    ais->type18.assigned	= UBITS(146, 1)!=0;
	    ais->type18.raim		= UBITS(147, 1)!=0;
	    ais->type18.radio		= UBITS(148, 20);
	    break;
	case 19:	/* Extended Class B CS Position Report */
	    if (ais_context->bitlen != 312) {
		gpsd_report(LOG_WARN, "AIVDM message type 19 size not 312 bits (%zd).\n",
			    ais_context->bitlen);
		return false;
	    }
	    ais->type19.reserved     = UBITS(38, 8);
	    ais->type19.speed        = UBITS(46, 10);
	    ais->type19.accuracy     = UBITS(56, 1)!=0;
	    ais->type19.lon          = SBITS(57, 28);
	    ais->type19.lat          = SBITS(85, 27);
	    ais->type19.course       = UBITS(112, 12);
	    ais->type19.heading      = UBITS(124, 9);
	    ais->type19.second       = UBITS(133, 6);
	    ais->type19.regional     = UBITS(139, 4);
	    UCHARS(143, ais->type19.shipname);
	    ais->type19.shiptype     = UBITS(263, 8);
	    ais->type19.to_bow       = UBITS(271, 9);
	    ais->type19.to_stern     = UBITS(280, 9);
	    ais->type19.to_port      = UBITS(289, 6);
	    ais->type19.to_starboard = UBITS(295, 6);
	    ais->type19.epfd         = UBITS(299, 4);
	    ais->type19.raim         = UBITS(302, 1)!=0;
	    ais->type19.dte          = UBITS(305, 1)!=0;
	    ais->type19.assigned     = UBITS(306, 1)!=0;
	    //ais->type19.spare      = UBITS(307, 5);
	    break;
	case 20:	/* Data Link Management Message */
	    if (ais_context->bitlen < 72 || ais_context->bitlen > 160) {
		gpsd_report(LOG_WARN, "AIVDM message type 20 size is out of range (%zd).\n",
			    ais_context->bitlen);
		return false;
	    }
	    //ais->type20.spare		= UBITS(38, 2);
	    ais->type20.offset1		= UBITS(40, 12);
	    ais->type20.number1		= UBITS(52, 4);
	    ais->type20.timeout1	= UBITS(56, 3);
	    ais->type20.increment1	= UBITS(59, 11);
	    ais->type20.offset2		= UBITS(70, 12);
	    ais->type20.number2		= UBITS(82, 4);
	    ais->type20.timeout2	= UBITS(86, 3);
	    ais->type20.increment2	= UBITS(89, 11);
	    ais->type20.offset3		= UBITS(100, 12);
	    ais->type20.number3		= UBITS(112, 4);
	    ais->type20.timeout3	= UBITS(116, 3);
	    ais->type20.increment3	= UBITS(119, 11);
	    ais->type20.offset4		= UBITS(130, 12);
	    ais->type20.number4		= UBITS(142, 4);
	    ais->type20.timeout4	= UBITS(146, 3);
	    ais->type20.increment4	= UBITS(149, 11);
	    break;
	case 21:	/* Aid-to-Navigation Report */
	    if (ais_context->bitlen < 272 || ais_context->bitlen > 360) {
		gpsd_report(LOG_WARN, "AIVDM message type 21 size is out of range (%zd).\n",
			    ais_context->bitlen);
		return false;
	    }
	    ais->type21.aid_type = UBITS(38, 5);
	    from_sixbit((char *)ais_context->bits,
			43, 21, ais->type21.name);
	    if (strlen(ais->type21.name) == 20 && ais_context->bitlen > 272)
		from_sixbit((char *)ais_context->bits,
			    272, (ais_context->bitlen - 272)/6,
			    ais->type21.name+20);
	    ais->type21.accuracy     = UBITS(163, 1);
	    ais->type21.lon          = SBITS(164, 28);
	    ais->type21.lat          = SBITS(192, 27);
	    ais->type21.to_bow       = UBITS(219, 9);
	    ais->type21.to_stern     = UBITS(228, 9);
	    ais->type21.to_port      = UBITS(237, 6);
	    ais->type21.to_starboard = UBITS(243, 6);
	    ais->type21.epfd         = UBITS(249, 4);
	    ais->type21.second       = UBITS(253, 6);
	    ais->type21.off_position = UBITS(259, 1)!=0;
	    ais->type21.regional     = UBITS(260, 8);
	    ais->type21.raim         = UBITS(268, 1)!=0;
	    ais->type21.virtual_aid  = UBITS(269, 1)!=0;
	    ais->type21.assigned     = UBITS(270, 1)!=0;
	    //ais->type21.spare      = UBITS(271, 1);
	    break;
	case 22:	/* Channel Management */
	    if (ais_context->bitlen != 168) {
		gpsd_report(LOG_WARN, "AIVDM message type 22 size not 168 bits (%zd).\n",
			    ais_context->bitlen);
		return false;
	    }
	    ais->type22.channel_a    = UBITS(40, 12);
	    ais->type22.channel_b    = UBITS(52, 12);
	    ais->type22.txrx         = UBITS(64, 4);
	    ais->type22.power        = UBITS(68, 1);
	    ais->type22.addressed    = UBITS(139, 1);
	    if (!ais->type22.addressed) {
		ais->type22.area.ne_lon       = SBITS(69, 18);
		ais->type22.area.ne_lat       = SBITS(87, 17);
		ais->type22.area.sw_lon       = SBITS(104, 18);
		ais->type22.area.sw_lat       = SBITS(122, 17);
	    } else {
		ais->type22.mmsi.dest1             = SBITS(69, 30);
		ais->type22.mmsi.dest2             = SBITS(104, 30);
	    }
	    ais->type22.band_a       = UBITS(140, 1);
	    ais->type22.band_b       = UBITS(141, 1);
	    ais->type22.zonesize     = UBITS(142, 3);
	    break;
	case 23:	/* Group Assignment Command */
	    if (ais_context->bitlen != 160) {
		gpsd_report(LOG_WARN, "AIVDM message type 23 size not 160 bits (%zd).\n",
			    ais_context->bitlen);
		return false;
	    }
	    ais->type23.ne_lon       = SBITS(40, 18);
	    ais->type23.ne_lat       = SBITS(58, 17);
	    ais->type23.sw_lon       = SBITS(75, 18);
	    ais->type23.sw_lat       = SBITS(93, 17);
	    ais->type23.stationtype  = UBITS(110, 4);
	    ais->type23.shiptype     = UBITS(114, 8);
	    ais->type23.txrx         = UBITS(144, 4);
	    ais->type23.interval     = UBITS(146, 4);
	    ais->type23.quiet        = UBITS(150, 4);
	    break;
	case 24:	/* Class B CS Static Data Report */
	    switch (UBITS(38, 2)) {
	    case 0:
		if (ais_context->bitlen != 160) {
		    gpsd_report(LOG_WARN, "AIVDM message type 24A size not 160 bits (%zd).\n",
				ais_context->bitlen);
		    return false;
		}
		if (ais_context->mmsi24) {
		    gpsd_report(LOG_WARN,
		                "AIVDM message type 24 collision on channel %c : Discarding previous sentence 24A from %09u.\n",
		                field[4][0],
		                ais_context->mmsi24);
		    /* no return false */
		}
		ais_context->mmsi24 = ais->mmsi;
		UCHARS(40, ais_context->shipname24);
		//ais->type24.a.spare	= UBITS(160, 8);
		return false;	/* data only partially decoded */
	    case 1:
		if (ais_context->bitlen != 168) {
		    gpsd_report(LOG_WARN, "AIVDM message type 24B size not 168 bits (%zd).\n",
				ais_context->bitlen);
		    return false;
		}
		if (ais_context->mmsi24 != ais->mmsi) {
		    if (ais_context->mmsi24)
			gpsd_report(LOG_WARN,
			            "AIVDM message type 24 collision on channel %c: MMSI mismatch: %09u vs %09u.\n",
			            field[4][0],
			            ais_context->mmsi24, ais->mmsi);
		    else
			gpsd_report(LOG_WARN,
			            "AIVDM message type 24 collision on channel %c: 24B sentence from %09u without 24A.\n",
			            field[4][0],
			            ais->mmsi);
		    return false;
		}
		(void)strlcpy(ais->type24.shipname,
			      ais_context->shipname24,
			      sizeof(ais_context->shipname24));
		ais->type24.shiptype = UBITS(40, 8);
		UCHARS(48, ais->type24.vendorid);
		UCHARS(90, ais->type24.callsign);
		if (AIS_AUXILIARY_MMSI(ais->mmsi)) {
		    ais->type24.mothership_mmsi   = UBITS(132, 30);
		} else {
		    ais->type24.dim.to_bow        = UBITS(132, 9);
		    ais->type24.dim.to_stern      = UBITS(141, 9);
		    ais->type24.dim.to_port       = UBITS(150, 6);
		    ais->type24.dim.to_starboard  = UBITS(156, 6);
		}
		//ais->type24.b.spare	    = UBITS(162, 8);
		ais_context->mmsi24 = 0; /* reset last know 24A for collision detection */
		break;
	    default:
		gpsd_report(LOG_WARN, "AIVDM message type 24 of subtype unknown.\n");
		return false;
	    }
	    break;
	case 25:	/* Binary Message, Single Slot */
	    /* this check and the following one reject line noise */
	    if (ais_context->bitlen < 40 || ais_context->bitlen > 168) {
		gpsd_report(LOG_WARN, "AIVDM message type 25 size not between 40 to 168 bits (%zd).\n",
			    ais_context->bitlen);
		return false;
	    }
	    ais->type25.addressed	= (bool)UBITS(38, 1);
	    ais->type25.structured	= (bool)UBITS(39, 1);
	    if (ais_context->bitlen < (unsigned)(40 + (16*ais->type25.structured) + (30*ais->type25.addressed))) {
		gpsd_report(LOG_WARN, "AIVDM message type 25 too short for mode.\n");
		return false;
	    }
	    if (ais->type25.addressed)
		ais->type25.dest_mmsi   = UBITS(40, 30);
	    if (ais->type25.structured)
		ais->type25.app_id      = UBITS(40+ais->type25.addressed*30,16);
	    /*
	     * Not possible to do this right without machinery we
	     * don't yet have.  The problem is that if the addressed
	     * bit is on the bitfield start won't be on a byte
	     * boundary. Thus the formulas below (and in message type 26)
	     * will work perfectly for brodacst messages, but for addressed
	     * messages the retrieved data will be led by thr 30 bits of
	     * the destination MMSI
	     */
	    ais->type25.bitcount       = ais_context->bitlen - 40 - 16*ais->type25.structured;
	    (void)memcpy(ais->type25.bitdata,
			 (char *)ais_context->bits+5 + 2 * ais->type25.structured,
			 (ais->type25.bitcount + 7) / 8);
	    break;
	case 26:	/* Binary Message, Multiple Slot */
	    if (ais_context->bitlen < 60 || ais_context->bitlen > 1004) {
		gpsd_report(LOG_WARN, "AIVDM message type 26 size is out of range (%zd).\n",
			    ais_context->bitlen);
		return false;
	    }
	    ais->type26.addressed	= (bool)UBITS(38, 1);
	    ais->type26.structured	= (bool)UBITS(39, 1);
	    if ((signed)ais_context->bitlen < 40 + 16*ais->type26.structured + 30*ais->type26.addressed + 20) {
		gpsd_report(LOG_WARN, "AIVDM message type 26 too short for mode.\n");
		return false;
	    }
	    if (ais->type26.addressed)
		ais->type26.dest_mmsi   = UBITS(40, 30);
	    if (ais->type26.structured)
		ais->type26.app_id      = UBITS(40+ais->type26.addressed*30,16);
	    ais->type26.bitcount        = ais_context->bitlen - 60 - 16*ais->type26.structured;
	    (void)memcpy(ais->type26.bitdata,
			 (char *)ais_context->bits+5 + 2 * ais->type26.structured,
			 (ais->type26.bitcount + 7) / 8);
	    break;
	case 27:	/* Long Range AIS Broadcast message */
	    ais->type27.accuracy        = (bool)UBITS(38, 1);
	    ais->type27.raim		= UBITS(39, 1)!=0;
	    ais->type27.status		= UBITS(40, 4);
	    ais->type27.lon		= SBITS(44, 18);
	    ais->type27.lat		= SBITS(62, 17);
	    ais->type27.speed		= UBITS(79, 6);
	    ais->type27.course		= UBITS(85, 9);
	    ais->type27.gnss            = (bool)UBITS(94, 1);
	    break;
	default:
	    gpsd_report(LOG_ERROR, "Unparsed AIVDM message type %d.\n",ais->type);
	    return false;
	}
	/* *INDENT-ON* */
#undef UCHARS
#undef SBITS
#undef UBITS
#undef BITS_PER_BYTE

	/* data is fully decoded */
	return true;
    }

    /* we're still waiting on another sentence */
    ais_context->decoded_frags++;
    return false;
}

/*@ -charint +fixedformalarray +usedef +branchstate @*/

/* driver_aivdm.c ends here */
