/* $Id$ */
/*****************************************************************************

This is a decoder for RTCM-104 2.x, an obscure and complicated serial
protocol used for broadcasting pseudorange corrections from
differential-GPS reference stations.  The applicable
standard is

RTCM RECOMMENDED STANDARDS FOR DIFFERENTIAL NAVSTAR GPS SERVICE,
RTCM PAPER 194-93/SC 104-STD

Ordering instructions are accessible from <http://www.rtcm.org/>
under "Publications".  This describes version 2.1 of the RTCM specification.
RTCM-104 was later incrementally revised up to a 2.3 level before being 
completely redesigned as level 3.0.

Also applicable is ITU-R M.823: "Technical characteristics of
differential transmissions for global navigation satellite systems
from maritime radio beacons in the frequency band 283.5 - 315 kHz in
region 1 and 285 - 325 kHz in regions 2 & 3."

The RTCM 2.x protocol uses as a transport layer the GPS satellite downlink
protocol described in IS-GPS-200, the Navstar GPS Interface
Specification.  This code relies on the lower-level packet-assembly
code for that protocol in isgps.c.

The lower layer's job is done when it has assembled a message of up to
33 words of clean parity-checked data.  At this point this upper layer
takes over.  struct rtcm2_msg_t is overlaid on the buffer and the bitfields
are used to extract pieces of it.  Those pieces are copied and (where
necessary) reassembled into a struct rtcm2_t.

This code and the contents of isgps.c are evolved from code by Wolfgang
Rupprecht.  Wolfgang's decoder was loosely based on one written by
John Sager in 1999 (in particular the dump function emits a close
descendant of Sager's dump format).  Here are John Sager's original
notes:

The RTCM decoder prints a legible representation of the input data.
The RTCM SC-104 specification is copyrighted, so I cannot
quote it - in fact, I have never read it! Most of the information
used to develop the decoder came from publication ITU-R M.823.
This is a specification of the data transmitted from LF DGPS
beacons in the 300kHz band. M.823 contains most of those parts of
RTCM SC-104 directly relevant to the air interface (there
are one or two annoying and vital omissions!). Information
about the serial interface format was gleaned from studying
the output of a beacon receiver test program made available on
Starlink's website.

*****************************************************************************/

#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <math.h> 		/* for round() */

#include "gpsd_config.h"
#include "gpsd.h"
#include "driver_rtcm2.h"

#ifdef RTCM104V2_ENABLE

#define PREAMBLE_PATTERN 0x66

static unsigned int tx_speed[] = { 25, 50, 100, 110, 150, 200, 250, 300 };

#define DIMENSION(a) (unsigned)(sizeof(a)/sizeof(a[0]))

void rtcm2_unpack(/*@out@*/struct rtcm2_t *tp, char *buf)
/* break out the raw bits into the content fields */
{
    int len;
    unsigned int n, w;
    struct rtcm2_msg_t *msg = (struct rtcm2_msg_t *)buf;

    tp->type = msg->w1.msgtype;
    tp->length = msg->w2.frmlen;
    tp->zcount = msg->w2.zcnt * ZCOUNT_SCALE;
    tp->refstaid = msg->w1.refstaid;
    tp->seqnum = msg->w2.sqnum;
    tp->stathlth = msg->w2.stathlth;

    len = (int)tp->length;
    n = 0;
    switch (tp->type) {
    case 1:
    case 9:
	{
	    struct b_correction_t    *m = &msg->msg_type.type1.corrections[0];

	    while (len >= 0) {
		if (len >= 2) {
		    tp->ranges.sat[n].ident      = m->w3.satident1;
		    tp->ranges.sat[n].udre       = m->w3.udre1;
		    tp->ranges.sat[n].issuedata  = m->w4.issuedata1;
		    tp->ranges.sat[n].rangerr    = m->w3.pc1 * 
			(m->w3.scale1 ? PCLARGE : PCSMALL);
		    tp->ranges.sat[n].rangerate  = m->w4.rangerate1 * 
					(m->w3.scale1 ? RRLARGE : RRSMALL);
		    n++;
		}
		if (len >= 4) {
		    tp->ranges.sat[n].ident      = m->w4.satident2;
		    tp->ranges.sat[n].udre       = m->w4.udre2;
		    tp->ranges.sat[n].issuedata  = m->w6.issuedata2;
		    tp->ranges.sat[n].rangerr    = m->w5.pc2 * 
			(m->w4.scale2 ? PCLARGE : PCSMALL);
		    tp->ranges.sat[n].rangerate  = m->w5.rangerate2 * 
			(m->w4.scale2 ? RRLARGE : RRSMALL);
		    n++;
		}
		if (len >= 5) {
		    tp->ranges.sat[n].ident       = m->w6.satident3;
		    tp->ranges.sat[n].udre        = m->w6.udre3;
		    tp->ranges.sat[n].issuedata   = m->w7.issuedata3;
		    /*@ -shiftimplementation @*/
		    tp->ranges.sat[n].rangerr     = ((m->w6.pc3_h<<8)|(m->w7.pc3_l)) *
					(m->w6.scale3 ? PCLARGE : PCSMALL);
		    tp->ranges.sat[n].rangerate   = m->w7.rangerate3 * 
					(m->w6.scale3 ? RRLARGE : RRSMALL);
		    /*@ +shiftimplementation @*/
		    n++;
		}
		len -= 5;
		m++;
	    }
	    tp->ranges.nentries = n;
	}
	break;
    case 3:
        {
	    struct rtcm2_msg3    *m = &msg->msg_type.type3;

	    if ((tp->ecef.valid = len >= 4)) {
		tp->ecef.x = ((m->w3.x_h<<8)|(m->w4.x_l))*XYZ_SCALE;
		tp->ecef.y = ((m->w4.y_h<<16)|(m->w5.y_l))*XYZ_SCALE;
		tp->ecef.z = ((m->w5.z_h<<24)|(m->w6.z_l))*XYZ_SCALE;
	    }
	}
	break;
    case 4:
	if ((tp->reference.valid = len >= 2)){
	    struct rtcm2_msg4    *m = &msg->msg_type.type4;

	    tp->reference.system =
		    (m->w3.dgnss==0) ? NAVSYSTEM_GPS :
			    ((m->w3.dgnss==1) ? NAVSYSTEM_GLONASS : NAVSYSTEM_UNKNOWN);
	    tp->reference.sense = (m->w3.dat != 0) ? SENSE_GLOBAL : SENSE_LOCAL;
	    if (m->w3.datum_alpha_char1){
		tp->reference.datum[n++] = (char)(m->w3.datum_alpha_char1);
	    }
	    if (m->w3.datum_alpha_char2){
		tp->reference.datum[n++] = (char)(m->w3.datum_alpha_char2);
	    }
	    if (m->w4.datum_sub_div_char1){
		tp->reference.datum[n++] = (char)(m->w4.datum_sub_div_char1);
	    }
	    if (m->w4.datum_sub_div_char2){
		tp->reference.datum[n++] = (char)(m->w4.datum_sub_div_char2);
	    }
	    if (m->w4.datum_sub_div_char3){
		tp->reference.datum[n++] = (char)(m->w4.datum_sub_div_char3);
	    }
	    tp->reference.datum[n++] = '\0';
	    if (len >= 4) {
		tp->reference.dx = m->w5.dx * DXYZ_SCALE;
		tp->reference.dy = ((m->w5.dy_h << 8) | m->w6.dy_l) * DXYZ_SCALE;
		tp->reference.dz = m->w6.dz * DXYZ_SCALE;
	    } else 
		tp->reference.sense = SENSE_INVALID;
	}
	break;
    case 5:
	for (n = 0; n < (unsigned)len; n++) {
	    struct consat_t *csp = &tp->conhealth.sat[n];
	    struct b_health_t *m = &msg->msg_type.type5.health[n];

	    csp->ident = m->sat_id;
	    csp->iodl = m->issue_of_data_link!=0;
	    csp->health = m->data_health;
	    /*@i@*/csp->snr = (m->cn0?(m->cn0+CNR_OFFSET):SNR_BAD);
	    csp->health_en = m->health_enable;
	    csp->new_data = m->new_nav_data!=0;
	    csp->los_warning = m->loss_warn!=0;
	    csp->tou = m->time_unhealthy*TU_SCALE;
	}
	tp->conhealth.nentries = n;
	break;
    case 7:
	for (w = 0; w < (unsigned)len; w++) {
	    struct station_t *np = &tp->almanac.station[n];
	    struct b_station_t *mp = &msg->msg_type.type7.almanac[w];

	    np->latitude = mp->w3.lat * LA_SCALE;
	    /*@i@*/np->longitude = ((mp->w3.lon_h << 8) | mp->w4.lon_l) * LO_SCALE;
	    np->range = mp->w4.range;
	    np->frequency = (((mp->w4.freq_h << 6) | mp->w5.freq_l) * FREQ_SCALE) + FREQ_OFFSET;
	    np->health = mp->w5.health;
	    np->station_id = mp->w5.station_id,
	    np->bitrate = tx_speed[mp->w5.bit_rate];
	    n++;
	}
	tp->almanac.nentries = (unsigned)(len/3);
	break;
    case 16:
	/*@ -boolops @*/
	for (w = 0; w < (unsigned)len; w++){
	    if (!msg->msg_type.type16.txt[w].byte1) {
		break;
	    }
	    tp->message[n++] = (char)(msg->msg_type.type16.txt[w].byte1);
	    if (!msg->msg_type.type16.txt[w].byte2) {
		break;
	    }
	    tp->message[n++] = (char)(msg->msg_type.type16.txt[w].byte2);
	    if (!msg->msg_type.type16.txt[w].byte3) {
		break;
	    }
	    tp->message[n++] = (char)(msg->msg_type.type16.txt[w].byte3);
	}
	/*@ +boolops @*/
	tp->message[n++] = '\0';
	break;

    default:
	memcpy(tp->words, msg->msg_type.rtcm2_msgunk, (RTCM2_WORDS_MAX-2)*sizeof(isgps30bits_t));
	break;
    }
}

bool rtcm2_repack(struct rtcm2_t *tp, isgps30bits_t *buf)
/* repack the content fields into the raw bits */
{
    int len, sval;
    unsigned int n, w, uval;
    struct rtcm2_msg_t  *msg = (struct rtcm2_msg_t *)buf;
    struct rtcm2_msghw1 *wp  = (struct rtcm2_msghw1 *)buf;

    msg->w1.msgtype = tp->type;
    msg->w2.frmlen = tp->length;
    msg->w2.zcnt = (unsigned) round(tp->zcount / ZCOUNT_SCALE);
    msg->w1.refstaid = tp->refstaid;
    msg->w2.sqnum = tp->seqnum;
    msg->w2.stathlth = tp->stathlth;

    len = (int)tp->length;
    n = 0;
    switch (tp->type) {
    case 1:	/* S */
    case 9:
	{
	    struct b_correction_t    *m = &msg->msg_type.type1.corrections[0];

	    while (len >= 0) {
		if (len >= 2) {
		    struct rangesat_t *ssp = &tp->ranges.sat[n];
		    m->w3.satident1 = ssp->ident;
		    m->w3.udre1 = ssp->udre;
		    m->w4.issuedata1 = ssp->issuedata;
		    m->w3.scale1 = (unsigned)((ssp->rangerr > MAXPCSMALL) ||
					      (ssp->rangerr < (-MAXPCSMALL)) ||
					      (ssp->rangerate > MAXRRSMALL) ||
					      (ssp->rangerate < (-MAXRRSMALL)));
		    m->w3.pc1 = (int) round(ssp->rangerr / (m->w3.scale1 ? PCLARGE : PCSMALL));
		    m->w4.rangerate1 = (int) round(ssp->rangerate / (m->w3.scale1 ? RRLARGE : RRSMALL));
		    n++;
		}
		if (len >= 4) {
		    struct rangesat_t *ssp = &tp->ranges.sat[n];
		    m->w4.satident2 = ssp->ident;
		    m->w4.udre2 = ssp->udre;
		    m->w6.issuedata2 = ssp->issuedata;
		    m->w4.scale2 = (unsigned)((ssp->rangerr > MAXPCSMALL) ||
					      (ssp->rangerr < (-MAXPCSMALL)) ||
					      (ssp->rangerate > MAXRRSMALL) ||
					      (ssp->rangerate < (-MAXRRSMALL)));
		    m->w5.pc2 = (int) round(ssp->rangerr / (m->w4.scale2 ? PCLARGE : PCSMALL));
		    m->w5.rangerate2 = (int) round(ssp->rangerate / (m->w4.scale2 ? RRLARGE : RRSMALL));
		    n++;
		}
		if (len >= 5) {
		    struct rangesat_t *ssp = &tp->ranges.sat[n];
		    m->w6.satident3 = ssp->ident;
		    m->w6.udre3 = ssp->udre;
		    m->w7.issuedata3 = ssp->issuedata;
		    m->w6.scale3 = (unsigned)((ssp->rangerr > MAXPCSMALL) ||
					      (ssp->rangerr < (-MAXPCSMALL)) ||
					      (ssp->rangerate > MAXRRSMALL) ||
					      (ssp->rangerate < (-MAXRRSMALL)));
		    sval = (int) round(ssp->rangerr / (m->w6.scale3 ? PCLARGE : PCSMALL));
		    /*@ -shiftimplementation @*/
		    m->w6.pc3_h = sval >> 8;
		    /*@ +shiftimplementation @*/
		    m->w7.pc3_l = (unsigned)sval & 0xff;
		    m->w7.rangerate3 = (int) round(ssp->rangerate / (m->w6.scale3 ? RRLARGE : RRSMALL));
		    n++;
		}
		len -= 5;
		m++;
	    }
	    tp->ranges.nentries = n;
	}
	break;
    case 3:	/* R */
	if (tp->ecef.valid) {
	    struct rtcm2_msg3    *m = &msg->msg_type.type3;
	    unsigned x = (unsigned) round(tp->ecef.x / XYZ_SCALE);
	    unsigned y = (unsigned) round(tp->ecef.y / XYZ_SCALE);
	    unsigned z = (unsigned) round(tp->ecef.z / XYZ_SCALE);

	    m->w4.x_l = x & 0xff;
	    m->w3.x_h = x >> 8;
	    m->w5.y_l = y & 0xffff;
	    m->w4.y_h = y >> 16;
	    m->w6.z_l = z & 0xffffff;
	    m->w5.z_h = z >> 24;
	}
	break;
    case 4:	/* D */
	if (tp->reference.valid) {
	    struct rtcm2_msg4    *m = &msg->msg_type.type4;

	    m->w3.dgnss = tp->reference.system;
	    m->w3.dat = (unsigned)(tp->reference.sense == SENSE_GLOBAL);
	    /*@ -predboolothers -type @*/
	    if (tp->reference.datum[0])
		m->w3.datum_alpha_char1 = tp->reference.datum[0];
	    else
		m->w3.datum_alpha_char1 = 0;
	    if (tp->reference.datum[1])
		m->w3.datum_alpha_char2 = tp->reference.datum[1];
	    else
		m->w3.datum_alpha_char2 = 0;
	    if (tp->reference.datum[2])
		m->w4.datum_sub_div_char1 = tp->reference.datum[2];
	    else
		m->w4.datum_sub_div_char1 = 0;
	    if (tp->reference.datum[3])
		m->w4.datum_sub_div_char2 = tp->reference.datum[3];
	    else
		m->w4.datum_sub_div_char2 = 0;
	    if (tp->reference.datum[4])
		m->w4.datum_sub_div_char3 = tp->reference.datum[4];
	    else
		m->w4.datum_sub_div_char3 = 0;
	    /*@ +predboolothers +type @*/
	    if (tp->reference.system != NAVSYSTEM_UNKNOWN) {
		m->w5.dx = (uint)round(tp->reference.dx / DXYZ_SCALE);
		uval = (uint)round(tp->reference.dy / DXYZ_SCALE);
		m->w5.dy_h = uval >> 8;
		m->w6.dy_l = uval & 0xff;
		m->w6.dz = (uint)round(tp->reference.dz / DXYZ_SCALE);
	    }
	}
	break;
    case 5:	/* C */
	for (n = 0; n < (unsigned)len; n++) {
	    struct consat_t *csp = &tp->conhealth.sat[n];
	    struct b_health_t *m = &msg->msg_type.type5.health[n];

	    m->sat_id = csp->ident;
	    m->issue_of_data_link = (unsigned)csp->iodl;
	    m->data_health = csp->health;
	    m->cn0 = (csp->snr == SNR_BAD) ? 0 : (unsigned)csp->snr-CNR_OFFSET;
	    m->health_enable = csp->health_en;
	    m->new_nav_data = (unsigned)csp->new_data;
	    m->loss_warn = (unsigned)csp->los_warning;
	    m->time_unhealthy = (unsigned)(csp->tou / TU_SCALE);
	}
	break;
    case 7:	/* A */
	for (w = 0; w < (RTCM2_WORDS_MAX - 2)/ 3; w++) {
	    struct station_t *np = &tp->almanac.station[n++];
	    struct b_station_t *mp = &msg->msg_type.type7.almanac[w];

	    mp->w3.lat = (int) round(np->latitude / LA_SCALE);
	    sval = (int) round(np->longitude / LO_SCALE);
	    /*@ -shiftimplementation @*/
	    mp->w3.lon_h = sval >> 8;
	    /*@ +shiftimplementation @*/
	    mp->w4.lon_l = (unsigned)sval & 0xff;
	    mp->w4.range = np->range;
	    uval = (unsigned) round(((np->frequency-FREQ_OFFSET) / FREQ_SCALE));
	    mp->w4.freq_h = uval >> 6;
	    mp->w5.freq_l = uval & 0x3f;
	    mp->w5.health = np->health;
	    mp->w5.station_id = np->station_id;
	    mp->w5.bit_rate = 0;
	    for (uval = 0; uval < (unsigned)(sizeof(tx_speed)/sizeof(tx_speed[0])); uval++)
		if (tx_speed[uval] == np->bitrate) {
		    mp->w5.bit_rate = uval;
		    break;
		}
	    if (mp->w5.bit_rate == 0)
		return false;
	}
	tp->almanac.nentries = n;
	break;
    case 16:	/* T */
	/*@ -boolops @*/
	for (w = 0; w < RTCM2_WORDS_MAX - 2; w++){
	    if (!tp->message[n]) {
		break;
	    }
	    msg->msg_type.type16.txt[w].byte1 = (unsigned)tp->message[n++];
	    if (!tp->message[n]) {
		break;
	    }
	    msg->msg_type.type16.txt[w].byte2 = (unsigned)tp->message[n++];
	    if (!tp->message[n]) {
		break;
	    }
	    msg->msg_type.type16.txt[w].byte3 = (unsigned)tp->message[n++];
	}
	msg->w2.frmlen = w+1;
	/*@ +boolops @*/
	break;

    default:	/* U */
	memcpy(msg->msg_type.rtcm2_msgunk, tp->words, (RTCM2_WORDS_MAX-2)*sizeof(isgps30bits_t));
	break;
    }

    /* compute parity for each word in the message */
    for (w = 0; w < tp->length; w++)
	wp[w].parity = isgps_parity(buf[w]);

    /* FIXME: must do inversion here */
    return true;
}

static bool preamble_match(isgps30bits_t *w)
{
    return (((struct rtcm2_msghw1 *)w)->preamble == PREAMBLE_PATTERN);
}

static bool length_check(struct gps_packet_t *lexer)
{
    return lexer->isgps.bufindex >= 2 
	&& lexer->isgps.bufindex >= ((struct rtcm2_msg_t *)lexer->isgps.buf)->w2.frmlen + 2u;
}

enum isgpsstat_t rtcm2_decode(struct gps_packet_t *lexer, unsigned int c)
{
    return isgps_decode(lexer, 
			preamble_match, 
			length_check, 
			RTCM2_WORDS_MAX, 
			c);
}

void rtcm2_sager_dump(struct rtcm2_t *rtcm, /*@out@*/char buf[], size_t buflen)
/* dump the contents of a parsed RTCM104 message */
{
    unsigned int n;

    (void)snprintf(buf, buflen, "H\t%u\t%u\t%0.1f\t%u\t%u\t%u\n",
	   rtcm->type,
	   rtcm->refstaid,
	   rtcm->zcount,
	   rtcm->seqnum,
	   rtcm->length,
	   rtcm->stathlth);

    switch (rtcm->type) {
    case 1:
    case 9:
	for (n = 0; n < rtcm->ranges.nentries; n++) {
	    struct rangesat_t *rsp = &rtcm->ranges.sat[n];
	    (void)snprintf(buf + strlen(buf), buflen - strlen(buf),
			   "S\t%u\t%u\t%u\t%0.1f\t%0.3f\t%0.3f\n",
			   rsp->ident,
			   rsp->udre,
			   rsp->issuedata,
			   rtcm->zcount,
			   rsp->rangerr,
			   rsp->rangerate);
	}
	break;

    case 3:
	if (rtcm->ecef.valid)
	    (void)snprintf(buf + strlen(buf), buflen - strlen(buf),
			   "R\t%.2f\t%.2f\t%.2f\n",
			   rtcm->ecef.x, 
			   rtcm->ecef.y,
			   rtcm->ecef.z);
	break;

    case 4:
	if (rtcm->reference.valid)
	    (void)snprintf(buf + strlen(buf), buflen - strlen(buf),
			   "D\t%s\t%1d\t%s\t%.1f\t%.1f\t%.1f\n",
			   (rtcm->reference.system==NAVSYSTEM_GPS) ? "GPS"
			   : ((rtcm->reference.system==NAVSYSTEM_GLONASS) ? "GLONASS"
			      : "UNKNOWN"),
			   rtcm->reference.sense,
			   rtcm->reference.datum,
			   rtcm->reference.dx,
			   rtcm->reference.dy,
			   rtcm->reference.dz);
	break;

    case 5:
	for (n = 0; n < rtcm->conhealth.nentries; n++) {
	    struct consat_t *csp = &rtcm->conhealth.sat[n];
	    (void)snprintf(buf + strlen(buf), buflen - strlen(buf),
			   "C\t%2u\t%1u\t%1u\t%2d\t%1u\t%1u\t%1u\t%2u\n",
			   csp->ident,
			   (unsigned)csp->iodl,
			   (unsigned)csp->health,
			   csp->snr,
			   (unsigned)csp->health_en,
			   (unsigned)csp->new_data,
			   (unsigned)csp->los_warning,
			   csp->tou);
	}
	break;

    case 6: 			/* NOP msg */
	(void)strlcat(buf, "N\n", buflen);
	break;

    case 7:
	for (n = 0; n < rtcm->almanac.nentries; n++) {
	    struct station_t *ssp = &rtcm->almanac.station[n];
	    (void)snprintf(buf + strlen(buf), buflen - strlen(buf),
			   "A\t%.4f\t%.4f\t%u\t%.1f\t%u\t%u\t%u\n",
			   ssp->latitude,
			   ssp->longitude,
			   ssp->range,
			   ssp->frequency,
			   ssp->health,
			   ssp->station_id,
			   ssp->bitrate);
	}
	break;
    case 16:
	(void)snprintf(buf + strlen(buf), buflen - strlen(buf),
		       "T\t\"%s\"\n", rtcm->message);
	break;

    default:
	for (n = 0; n < rtcm->length; n++)
	    (void)snprintf(buf + strlen(buf), buflen - strlen(buf),
			   "U\t0x%08x\n", rtcm->words[n]);
	break;
    }

    (void)strlcat(buf, ".\n", buflen);
}

void rtcm2_json_dump(struct rtcm2_t *rtcm, /*@out@*/char buf[], size_t buflen)
/* dump the contents of a parsed RTCM104 message as JSON */
{
    unsigned int n;

    (void)snprintf(buf, buflen, "{\"class\":\"RTCM2\",\"type\":%u,\"station_id\":%u,\"zcount\":%0.1f,\"seqnum\":%u,\"length\":%u,\"station_health\":%u,",
	   rtcm->type,
	   rtcm->refstaid,
	   rtcm->zcount,
	   rtcm->seqnum,
	   rtcm->length,
	   rtcm->stathlth);

    switch (rtcm->type) {
    case 1:
    case 9:
	(void)snprintf(buf + strlen(buf), buflen - strlen(buf),
		       "\"reported\":%u,\"satellites\":[", rtcm->ranges.nentries);
	for (n = 0; n < rtcm->ranges.nentries; n++) {
	    struct rangesat_t *rsp = &rtcm->ranges.sat[n];
	    (void)snprintf(buf + strlen(buf), buflen - strlen(buf),
			   "{\"ident\":%u,\"udre\":%u,\"issuedata\":%u,\"rangerr\":%0.3f,\"rangerate\":%0.3f},",
			   rsp->ident,
			   rsp->udre,
			   rsp->issuedata,
			   rsp->rangerr,
			   rsp->rangerate);
	}
	if (buf[strlen(buf)-1] == ',')
	    buf[strlen(buf)-1] = '\0';
	(void)strlcat(buf, "]", buflen);
	break;

    case 3:
	if (rtcm->ecef.valid)
	    (void)snprintf(buf + strlen(buf), buflen - strlen(buf),
			   "\"x\":%.2f,\"y\":%.2f,\"z\":%.2f",
			   rtcm->ecef.x, 
			   rtcm->ecef.y,
			   rtcm->ecef.z);
	break;

    case 4:
	if (rtcm->reference.valid)
	    (void)snprintf(buf + strlen(buf), buflen - strlen(buf),
			   "\"system\":%s,\"sense\":%1d,\"datum\":%s,\"dx\":%.1f,\"dy\":%.1f,\"dz\":%.1f",
			   (rtcm->reference.system==NAVSYSTEM_GPS) ? "GPS"
			   : ((rtcm->reference.system==NAVSYSTEM_GLONASS) ? "GLONASS"
			      : "UNKNOWN"),
			   rtcm->reference.sense,
			   rtcm->reference.datum,
			   rtcm->reference.dx,
			   rtcm->reference.dy,
			   rtcm->reference.dz);
	break;

    case 5:
	(void)snprintf(buf + strlen(buf), buflen - strlen(buf),
		       "\"reported\":%u,\"satellites\":[", 
		       rtcm->conhealth.nentries);
	for (n = 0; n < rtcm->conhealth.nentries; n++) {
	    struct consat_t *csp = &rtcm->conhealth.sat[n];
	    (void)snprintf(buf + strlen(buf), buflen - strlen(buf),
			   "{\"ident\":%2u,\"iodl\":%1u,\"health\":%1u,\"snr\":%2d,\"health_en\":%1u,\"new_data\":%1u,\"los_warning\":%1u,\"tou\":%2u},",
			   csp->ident,
			   (unsigned)csp->iodl,
			   (unsigned)csp->health,
			   csp->snr,
			   (unsigned)csp->health_en,
			   (unsigned)csp->new_data,
			   (unsigned)csp->los_warning,
			   csp->tou);
	}
	if (buf[strlen(buf)-1] == ',')
	    buf[strlen(buf)-1] = '\0';
	(void)strlcat(buf, "]", buflen);
	break;

    case 6: 			/* NOP msg */
	break;

    case 7:
	(void)snprintf(buf + strlen(buf), buflen - strlen(buf),
		       "\"reported\":%u,\"satellites\":[", rtcm->almanac.nentries);
	for (n = 0; n < rtcm->almanac.nentries; n++) {
	    struct station_t *ssp = &rtcm->almanac.station[n];
	    (void)snprintf(buf + strlen(buf), buflen - strlen(buf),
			   "{\"latitude\":%.4f,\"longitude\":%.4f,\"range\":%u,\"frequency\":%.1f,\"health\":%u,\"station_id\":%u,\"bitrate\":%u}",
			   ssp->latitude,
			   ssp->longitude,
			   ssp->range,
			   ssp->frequency,
			   ssp->health,
			   ssp->station_id,
			   ssp->bitrate);
	}
	if (buf[strlen(buf)-1] == ',')
	    buf[strlen(buf)-1] = '\0';
	(void)strlcat(buf, "]", buflen);
	break;
    case 16:
	(void)snprintf(buf + strlen(buf), buflen - strlen(buf),
		       "\"message\":\"%s\"\n", rtcm->message);
	break;

    default:
	(void)snprintf(buf + strlen(buf), buflen - strlen(buf),
		       "\"length\":%u,\"satellites\":[", rtcm->length);
	for (n = 0; n < rtcm->length; n++)
	    (void)snprintf(buf + strlen(buf), buflen - strlen(buf),
			   "\"0x%08x\",", rtcm->words[n]);
	if (buf[strlen(buf)-1] == ',')
	    buf[strlen(buf)-1] = '\0';
	(void)strlcat(buf, "]", buflen);
	break;
    }

    (void)strlcat(buf, "}\r\n", buflen);
}

int rtcm2_undump(/*@out@*/struct rtcm2_t *rtcmp, char *buf)
/* merge a line of data into an RTCM structure, return 0 if done */
{
    int fldcount, v;
    unsigned n;
    char buf2[BUFSIZ]; /* stdio.h says BUFSIZ=1024. True everywhere? */

    /*@ -usedef @*/
    switch (rtcmp->type) {
    case 0:
	fldcount = sscanf(buf, "H\t%u\t%u\t%lf\t%u\t%u\t%u\n",
			&rtcmp->type,
			&rtcmp->refstaid,
			&rtcmp->zcount,
			&rtcmp->seqnum,
			&rtcmp->length,
			&rtcmp->stathlth);
	if (fldcount != 6)
	    return -1;
	else
	    return 1;
	//break;

    case 1:
    case 9:
	{
	    struct rangesat_t *rsp = &rtcmp->ranges.sat[rtcmp->ranges.nentries++];
	    /* we ignore the third (zcount) field, it's in the parent */
	    fldcount = sscanf(buf,
			      "S\t%u\t%u\t%u\t%*f\t%lf\t%lf\n",
			      &rsp->ident,
			      &rsp->udre,
			      &rsp->issuedata,
			      &rsp->rangerr,
			      &rsp->rangerate);
	    if (fldcount != 5 || (rtcmp->type != 1 && rtcmp->type != 9))
		return (int)(-rtcmp->type-1);
	    else if (rtcmp->ranges.nentries != rtcmp->length*3/5)
		return (int)(rtcmp->type+1);
	    else
		return 0;
	}
	//break;

    case 3:
	fldcount = sscanf(buf,
			  "R\t%lf\t%lf\t%lf\n",
			  &rtcmp->ecef.x, 
			  &rtcmp->ecef.y,
			  &rtcmp->ecef.z);
	if (fldcount != 3 || rtcmp->type != 3)
	    return -4;
	else {
	    rtcmp->ecef.valid = true;
	    return 0;
	}
	//break;

    case 4:
	fldcount = sscanf(buf,
			   "D\t%1023s\t%1d\t%5s\t%lf\t%lf\t%lf\n",
			  buf2,
			  &v,
			  (char *)&rtcmp->reference.datum,
			  &rtcmp->reference.dx,
			  &rtcmp->reference.dy,
			  &rtcmp->reference.dz);
	if (fldcount != 6 || rtcmp->type != 4)
	    return -5;
	else {
	    if (strcmp(buf2, "GPS") == 0)
		rtcmp->reference.system = NAVSYSTEM_GPS;
	    else if (strcmp(buf2, "GLONASS") == 0)
		rtcmp->reference.system = NAVSYSTEM_GLONASS;
	    else
		rtcmp->reference.system = NAVSYSTEM_UNKNOWN;
	    rtcmp->reference.sense = (v == 1) ? SENSE_GLOBAL : ((v == 0) ? SENSE_LOCAL : SENSE_INVALID);
	    rtcmp->reference.valid = true;
	    return 0;
	}
	//break;

    case 5:
	{
	    struct consat_t *csp = &rtcmp->conhealth.sat[rtcmp->conhealth.nentries++];
	    unsigned int iodl, health_en, new_data, los_warning;

	    fldcount = sscanf(buf,
			      "C\t%2u\t%1u\t%1u\t%2d\t%1u\t%1u\t%1u\t%2u\n",
			      &csp->ident,
			      &iodl,
			      &csp->health,
			      &csp->snr,
			      &health_en,
			      &new_data,
			      &los_warning,
			      &csp->tou);
	    csp->iodl = iodl > 0;
	    csp->new_data = new_data > 0;
	    csp->health_en = health_en > 0;
	    csp->los_warning = los_warning > 0;
	    if (fldcount != 8 || rtcmp->type != 5)
		return -6;
	    else if (rtcmp->conhealth.nentries < rtcmp->length)
		return 6;
	    else
		return 0;
	}
	//break;

    case 6: 			/* NOP msg */
	if (buf[0] != 'N')
	    return -7;
	else
	    return 0;
	//break;

    case 7:
	{
	    struct station_t *ssp = &rtcmp->almanac.station[rtcmp->almanac.nentries++];
	    fldcount = sscanf(buf,
			      "A\t%lf\t%lf\t%u\t%lf\t%u\t%u\t%u\n",
			      &ssp->latitude,
			      &ssp->longitude,
			      &ssp->range,
			      &ssp->frequency,
			      &ssp->health,
			      &ssp->station_id,
			      &ssp->bitrate);
	    if (fldcount != 7 || rtcmp->type != 7)
		return 8;
	    else if (rtcmp->almanac.nentries < rtcmp->length/3)
		return 8;
	    else
		return 0;
	}
	//break;

    case 16:
	fldcount = sscanf(buf, "T\t\"%[^\"]\"\n", rtcmp->message);
	if (fldcount != 1)
	    return 16;
	else
	    return 0;
	//break;

    default:
	for (n = 0; n < DIMENSION(rtcmp->words); n++)
	    if (rtcmp->words[n] == 0)
		break;
	if (n >= DIMENSION(rtcmp->words))
	    return 0;
	else {
	    unsigned int u;
	    fldcount = sscanf(buf, "U\t0x%08x\n", &u);
	    if (fldcount != 1)
		return (int)(-rtcmp->type-1);
	    else {
		rtcmp->words[n] = (isgps30bits_t)u;
		if (n == rtcmp->length-1)
		    return 0;
		else
		    return (int)(rtcmp->type+1);
	    }
	}
	//break;
    }
    /*@ +usedef @*/
}

#ifdef __UNUSED__
void rtcm2_output_magnavox(isgps30bits_t *ip, FILE *fp)
/* ship an RTCM message in the format emitted by Magnavox DGPS receivers */
{
    static uint     sqnum = 0;

    ((struct rtcm2_msg_t *) ip)->w2.sqnum = sqnum++;
    sqnum &= 0x7;

    isgps_output_magnavox(ip, ((struct rtcm2_msg_t *) ip)->w2.frmlen + 2, fp);
}
#endif /* __UNUSED__ */

#endif /* RTCM104V2_ENABLE */
