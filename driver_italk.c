/*
 * This file is Copyright (c) 2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 *
 * Driver for the iTalk binary protocol used by FasTrax
 */
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#ifndef S_SPLINT_S
#include <unistd.h>
#endif /* S_SPLINT_S */
#include <time.h>
#include <stdio.h>

#include "gpsd.h"
#if defined(ITRAX_ENABLE) && defined(BINARY_ENABLE)

#include "bits.h"
#include "driver_italk.h"

static gps_mask_t italk_parse(struct gps_device_t *, unsigned char *, size_t);
static gps_mask_t decode_itk_navfix(struct gps_device_t *, unsigned char *,
				    size_t);
static gps_mask_t decode_itk_prnstatus(struct gps_device_t *, unsigned char *,
				       size_t);
static gps_mask_t decode_itk_utcionomodel(struct gps_device_t *,
					  unsigned char *, size_t);
static gps_mask_t decode_itk_subframe(struct gps_device_t *, unsigned char *,
				      size_t);

static gps_mask_t decode_itk_navfix(struct gps_device_t *session,
				    unsigned char *buf, size_t len)
{
    unsigned int tow;
    unsigned short gps_week, flags, cflags, pflags;
    gps_mask_t mask = 0;
    double epx, epy, epz, evx, evy, evz, eph;
    double t;

    if (len != 296) {
	gpsd_report(LOG_PROG, "ITALK: bad NAV_FIX (len %zu, should be 296)\n",
		    len);
	return -1;
    }

    flags = (ushort) getleuw(buf, 7 + 4);
    cflags = (ushort) getleuw(buf, 7 + 6);
    pflags = (ushort) getleuw(buf, 7 + 8);

    session->gpsdata.status = STATUS_NO_FIX;
    session->newdata.mode = MODE_NO_FIX;
    mask = ONLINE_IS | MODE_IS | STATUS_IS | CLEAR_IS;

    /* just bail out if this fix is not marked valid */
    if (0 != (pflags & FIX_FLAG_MASK_INVALID)
	|| 0 == (flags & FIXINFO_FLAG_VALID))
	return mask;

    gps_week = (ushort) getlesw(buf, 7 + 82);
    session->context->gps_week = gps_week;
    tow = (uint) getleul(buf, 7 + 84);
    session->context->gps_tow = tow / 1000.0;
    t = gpstime_to_unix((int)gps_week,session->context->gps_tow)
			- session->context->leap_seconds;
    session->newdata.time = t;
    mask |= TIME_IS;

    epx = (double)(getlesl(buf, 7 + 96) / 100.0);
    epy = (double)(getlesl(buf, 7 + 100) / 100.0);
    epz = (double)(getlesl(buf, 7 + 104) / 100.0);
    evx = (double)(getlesl(buf, 7 + 186) / 1000.0);
    evy = (double)(getlesl(buf, 7 + 190) / 1000.0);
    evz = (double)(getlesl(buf, 7 + 194) / 1000.0);
    ecef_to_wgs84fix(&session->newdata, &session->gpsdata.separation,
		     epx, epy, epz, evx, evy, evz);
    mask |= LATLON_IS | ALTITUDE_IS | SPEED_IS | TRACK_IS | CLIMB_IS;
    eph = (double)(getlesl(buf, 7 + 252) / 100.0);
    /* eph is a circular error, sqrt(epx**2 + epy**2) */
    session->newdata.epx = session->newdata.epy = eph / sqrt(2);
    session->newdata.eps = (double)(getlesl(buf, 7 + 254) / 100.0);

#define MAX(a,b) (((a) > (b)) ? (a) : (b))
    session->gpsdata.satellites_used =
	(int)MAX(getleuw(buf, 7 + 12), getleuw(buf, 7 + 14));
    mask |= USED_IS;

    if (flags & FIX_CONV_DOP_VALID) {
	clear_dop(&session->gpsdata.dop);
	session->gpsdata.dop.hdop = (double)(getleuw(buf, 7 + 56) / 100.0);
	session->gpsdata.dop.gdop = (double)(getleuw(buf, 7 + 58) / 100.0);
	session->gpsdata.dop.pdop = (double)(getleuw(buf, 7 + 60) / 100.0);
	session->gpsdata.dop.vdop = (double)(getleuw(buf, 7 + 62) / 100.0);
	session->gpsdata.dop.tdop = (double)(getleuw(buf, 7 + 64) / 100.0);
	mask |= DOP_IS;
    }

    if ((pflags & FIX_FLAG_MASK_INVALID) == 0
	&& (flags & FIXINFO_FLAG_VALID) != 0) {
	if (pflags & FIX_FLAG_3DFIX)
	    session->newdata.mode = MODE_3D;
	else
	    session->newdata.mode = MODE_2D;

	if (pflags & FIX_FLAG_DGPS_CORRECTION)
	    session->gpsdata.status = STATUS_DGPS_FIX;
	else
	    session->gpsdata.status = STATUS_FIX;
    }

    gpsd_report(LOG_DATA,
		"NAV_FIX: time=%.2f, lat=%.2f lon=%.2f alt=%.f speed=%.2f track=%.2f climb=%.2f mode=%d status=%d gdop=%.2f pdop=%.2f hdop=%.2f vdop=%.2f tdop=%.2f mask=%s\n",
		session->newdata.time, session->newdata.latitude,
		session->newdata.longitude, session->newdata.altitude,
		session->newdata.speed, session->newdata.track,
		session->newdata.climb, session->newdata.mode,
		session->gpsdata.status, session->gpsdata.dop.gdop,
		session->gpsdata.dop.pdop, session->gpsdata.dop.hdop,
		session->gpsdata.dop.vdop, session->gpsdata.dop.tdop,
		gpsd_maskdump(mask));
    return mask;
}

static gps_mask_t decode_itk_prnstatus(struct gps_device_t *session,
				       unsigned char *buf, size_t len)
{
    unsigned int i, tow, nsv, nchan, st;
    unsigned short gps_week;
    double t;
    gps_mask_t mask;

    if (len < 62) {
	gpsd_report(LOG_PROG, "ITALK: runt PRN_STATUS (len=%zu)\n", len);
	mask = ERROR_IS;
    } else {
	gps_week = (ushort) getleuw(buf, 7 + 4);
	session->context->gps_week = gps_week;
	tow = (uint) getleul(buf, 7 + 6);
	session->context->gps_tow = tow / 1000.0;
	t = gpstime_to_unix((int)gps_week,session->context->gps_tow)
			    - session->context->leap_seconds;
	session->gpsdata.skyview_time = t;

	gpsd_zero_satellites(&session->gpsdata);
	nsv = 0;
	nchan = (unsigned int)getleuw(buf, 7 + 50);
	if (nchan > MAX_NR_VISIBLE_PRNS)
	    nchan = MAX_NR_VISIBLE_PRNS;
	for (i = st = 0; i < nchan; i++) {
	    unsigned int off = 7 + 52 + 10 * i;
	    unsigned short flags;

	    flags = (ushort) getleuw(buf, off);
	    session->gpsdata.ss[i] = (float)(getleuw(buf, off + 2) & 0xff);
	    session->gpsdata.PRN[i] = (int)getleuw(buf, off + 4) & 0xff;
	    session->gpsdata.elevation[i] = (int)getlesw(buf, off + 6) & 0xff;
	    session->gpsdata.azimuth[i] = (int)getlesw(buf, off + 8) & 0xff;
	    if (session->gpsdata.PRN[i]) {
		st++;
		if (flags & PRN_FLAG_USE_IN_NAV)
		    session->gpsdata.used[nsv++] = session->gpsdata.PRN[i];
	    }
	}
	session->gpsdata.satellites_visible = (int)st;
	session->gpsdata.satellites_used = (int)nsv;
	mask = USED_IS | SATELLITE_IS;;

	gpsd_report(LOG_DATA,
		    "PRN_STATUS: time=%.2f visible=%d used=%d mask={USED|SATELLITE}\n",
		    session->newdata.time,
		    session->gpsdata.satellites_visible,
		    session->gpsdata.satellites_used);
    }

    return mask;
}

static gps_mask_t decode_itk_utcionomodel(struct gps_device_t *session,
					  unsigned char *buf, size_t len)
{
    unsigned int tow;
    int leap;
    unsigned short gps_week, flags;
    double t;

    if (len != 64) {
	gpsd_report(LOG_PROG,
		    "ITALK: bad UTC_IONO_MODEL (len %zu, should be 64)\n",
		    len);
	return ERROR_IS;
    }

    flags = (ushort) getleuw(buf, 7);
    if (0 == (flags & UTC_IONO_MODEL_UTCVALID))
	return 0;

    leap = (int)getleuw(buf, 7 + 24);
    if (session->context->leap_seconds < leap)
	session->context->leap_seconds = leap;

    gps_week = (ushort) getleuw(buf, 7 + 36);
    session->context->gps_week = gps_week;
    tow = (uint) getleul(buf, 7 + 38);
    session->context->gps_tow = tow / 1000.0;
    t = gpstime_to_unix((int)gps_week,session->context->gps_tow)
			- session->context->leap_seconds;
    session->newdata.time = t;

    gpsd_report(LOG_DATA,
		"UTC_IONO_MODEL: time=%.2f mask={TIME}\n",
		session->newdata.time);
    return TIME_IS;
}

static gps_mask_t decode_itk_subframe(struct gps_device_t *session,
				      unsigned char *buf, size_t len)
{
    unsigned short flags, prn, sf;
    unsigned int i, words[10];

    if (len != 64) {
	gpsd_report(LOG_PROG,
		    "ITALK: bad SUBFRAME (len %zu, should be 64)\n", len);
	return ERROR_IS;
    }

    flags = (ushort) getleuw(buf, 7 + 4);
    prn = (ushort) getleuw(buf, 7 + 6);
    sf = (ushort) getleuw(buf, 7 + 8);
    gpsd_report(LOG_PROG, "iTalk 50B SUBFRAME prn %u sf %u - decode %s %s\n",
		prn, sf,
		flags & SUBFRAME_WORD_FLAG_MASK ? "error" : "ok",
		flags & SUBFRAME_GPS_PREAMBLE_INVERTED ? "(inverted)" : "");
    if (flags & SUBFRAME_WORD_FLAG_MASK)
	return ONLINE_IS | ERROR_IS;	// don't try decode an erroneous packet

    /*
     * Timo says "SUBRAME message contains decoded navigation message subframe
     * words with parity checking done but parity bits still present."
     */
    for (i = 0; i < 10; i++)
	words[i] = (unsigned int)(getleul(buf, 7 + 14 + 4*i) >> 6) & 0xffffff;

    gpsd_interpret_subframe(session, words);
    return ONLINE_IS;
}

/*@ +charint @*/
static gps_mask_t italk_parse(struct gps_device_t *session,
			      unsigned char *buf, size_t len)
{
    unsigned int type;
    gps_mask_t mask = 0;

    if (len == 0)
	return 0;

    type = (uint) getub(buf, 4);
    /* we may need to dump the raw packet */
    gpsd_report(LOG_RAW, "raw italk packet type 0x%02x length %zu: %s\n",
		type, len, gpsd_hexdump_wrapper(buf, len, LOG_RAW));

    session->cycle_end_reliable = true;

    switch (type) {
    case ITALK_NAV_FIX:
	gpsd_report(LOG_IO, "iTalk NAV_FIX len %zu\n", len);
	mask = decode_itk_navfix(session, buf, len) | (CLEAR_IS | REPORT_IS);
	break;
    case ITALK_PRN_STATUS:
	gpsd_report(LOG_IO, "iTalk PRN_STATUS len %zu\n", len);
	mask = decode_itk_prnstatus(session, buf, len);
	break;
    case ITALK_UTC_IONO_MODEL:
	gpsd_report(LOG_IO, "iTalk UTC_IONO_MODEL len %zu\n", len);
	mask = decode_itk_utcionomodel(session, buf, len);
	break;

    case ITALK_ACQ_DATA:
	gpsd_report(LOG_IO, "iTalk ACQ_DATA len %zu\n", len);
	break;
    case ITALK_TRACK:
	gpsd_report(LOG_IO, "iTalk TRACK len %zu\n", len);
	break;
    case ITALK_PSEUDO:
	gpsd_report(LOG_IO, "iTalk PSEUDO len %zu\n", len);
	break;
    case ITALK_RAW_ALMANAC:
	gpsd_report(LOG_IO, "iTalk RAW_ALMANAC len %zu\n", len);
	break;
    case ITALK_RAW_EPHEMERIS:
	gpsd_report(LOG_IO, "iTalk RAW_EPHEMERIS len %zu\n", len);
	break;
    case ITALK_SUBFRAME:
	mask = decode_itk_subframe(session, buf, len);
	break;
    case ITALK_BIT_STREAM:
	gpsd_report(LOG_IO, "iTalk BIT_STREAM len %zu\n", len);
	break;

    case ITALK_AGC:
    case ITALK_SV_HEALTH:
    case ITALK_PRN_PRED:
    case ITALK_FREQ_PRED:
    case ITALK_DBGTRACE:
    case ITALK_START:
    case ITALK_STOP:
    case ITALK_SLEEP:
    case ITALK_STATUS:
    case ITALK_ITALK_CONF:
    case ITALK_SYSINFO:
    case ITALK_ITALK_TASK_ROUTE:
    case ITALK_PARAM_CTRL:
    case ITALK_PARAMS_CHANGED:
    case ITALK_START_COMPLETED:
    case ITALK_STOP_COMPLETED:
    case ITALK_LOG_CMD:
    case ITALK_SYSTEM_START:
    case ITALK_STOP_SEARCH:
    case ITALK_SEARCH:
    case ITALK_PRED_SEARCH:
    case ITALK_SEARCH_DONE:
    case ITALK_TRACK_DROP:
    case ITALK_TRACK_STATUS:
    case ITALK_HANDOVER_DATA:
    case ITALK_CORE_SYNC:
    case ITALK_WAAS_RAWDATA:
    case ITALK_ASSISTANCE:
    case ITALK_PULL_FIX:
    case ITALK_MEMCTRL:
    case ITALK_STOP_TASK:
	gpsd_report(LOG_IO,
		    "iTalk not processing packet: id 0x%02x length %zu\n",
		    type, len);
	break;
    default:
	gpsd_report(LOG_IO, "iTalk unknown packet: id 0x%02x length %zu\n",
		    type, len);
    }
    if (mask == ERROR_IS)
	mask = 0;
    else
	(void)snprintf(session->gpsdata.tag, sizeof(session->gpsdata.tag),
		       "ITK-%02x", type);

    return mask | ONLINE_IS;
}

/*@ -charint @*/

static gps_mask_t italk_parse_input(struct gps_device_t *session)
{
    gps_mask_t st;

    if (session->packet.type == ITALK_PACKET) {
	st = italk_parse(session, session->packet.outbuffer,
			 session->packet.outbuflen);
	session->gpsdata.dev.driver_mode = MODE_BINARY;	/* binary */
	return st;
#ifdef NMEA_ENABLE
    } else if (session->packet.type == NMEA_PACKET) {
	st = nmea_parse((char *)session->packet.outbuffer, session);
	session->gpsdata.dev.driver_mode = MODE_NMEA;	/* NMEA */
	return st;
#endif /* NMEA_ENABLE */
    } else
	return 0;
}

#ifdef ALLOW_CONTROLSEND
/*@ +charint -usedef -compdef @*/
static ssize_t italk_control_send(struct gps_device_t *session,
				  char *msg, size_t msglen)
{
    ssize_t status;

    /*@ -mayaliasunique @*/
    session->msgbuflen = msglen;
    (void)memcpy(session->msgbuf, msg, msglen);
    /*@ +mayaliasunique @*/
    /* we may need to dump the message */
    gpsd_report(LOG_IO, "writing italk control type %02x:%s\n",
		msg[0], gpsd_hexdump_wrapper(msg, msglen, LOG_IO));
    status = write(session->gpsdata.gps_fd, msg, msglen);
    (void)tcdrain(session->gpsdata.gps_fd);
    return status;
}

/*@ -charint +usedef +compdef @*/
#endif /* ALLOW_CONTROLSEND */

static bool italk_set_mode(struct gps_device_t *session UNUSED,
			   speed_t speed UNUSED,
			   char parity UNUSED, int stopbits UNUSED,
			   bool mode UNUSED)
{
#ifdef __NOT_YET__
    /*@ +charint @*/
    char msg[] = { 0, };

    /* HACK THE MESSAGE */

    return (italk_control_send(session, msg, sizeof(msg)) != -1);
    /*@ +charint @*/
#endif

    return false;		/* until this actually works */
}

#ifdef ALLOW_RECONFIGURE
static bool italk_speed(struct gps_device_t *session,
			speed_t speed, char parity, int stopbits)
{
    return italk_set_mode(session, speed, parity, stopbits, true);
}

static void italk_mode(struct gps_device_t *session, int mode)
{
    if (mode == MODE_NMEA) {
	(void)italk_set_mode(session,
			     session->gpsdata.dev.baudrate,
			     (char)session->gpsdata.dev.parity,
			     (int)session->gpsdata.dev.stopbits, false);
    }
}
#endif /* ALLOW_RECONFIGURE */

static void italk_event_hook(struct gps_device_t *session, event_t event)
{
    /*
     * FIXME: It might not be necessary to call this on reactivate.
     * Experiment to see if the holds its settings through a close.
     */
    if ((event == event_identified || event == event_reactivate)
	&& session->packet.type == NMEA_PACKET)
	(void)italk_set_mode(session, session->gpsdata.dev.baudrate,
			     (char)session->gpsdata.dev.parity,
			     (int)session->gpsdata.dev.stopbits, true);
}

#ifdef __not_yet__
static void italk_ping(struct gps_device_t *session)
/* send a "ping". it may help us detect an itrax more quickly */
{
    char *ping = "<?>";
    (void)gpsd_write(session, ping, 3);
}
#endif /* __not_yet__ */

/* *INDENT-OFF* */
const struct gps_type_t italk_binary =
{
    .type_name      = "iTalk binary",	/* full name of type */
    .packet_type    = ITALK_PACKET,	/* associated lexer packet type */
    .trigger	    = NULL,		/* recognize the type */
    .channels       = 12,		/* consumer-grade GPS */
    .probe_detect   = NULL,		/* how to detect at startup time */
    .get_packet     = generic_get,	/* use generic packet grabber */
    .parse_packet   = italk_parse_input,/* parse message packets */
    .rtcm_writer    = pass_rtcm,	/* send RTCM data straight */
    .event_hook     = italk_event_hook,	/* lifetime event handler */
#ifdef ALLOW_RECONFIGURE
    .speed_switcher = italk_speed,	/* we can change baud rates */
    .mode_switcher  = italk_mode,	/* there is a mode switcher */
    .rate_switcher  = NULL,		/* no sample-rate switcher */
    .min_cycle      = 1,		/* not relevant, no rate switch */
#endif /* ALLOW_RECONFIGURE */
#ifdef ALLOW_CONTROLSEND
    .control_send   = italk_control_send,	/* how to send a control string */
#endif /* ALLOW_CONTROLSEND */
#ifdef NTPSHM_ENABLE
    .ntp_offset     = NULL,		/* no method for NTP fudge factor */
#endif /* NTPSHM_ ENABLE */
};
/* *INDENT-ON* */
#endif /* defined(ITRAX_ENABLE) && defined(BINARY_ENABLE) */
