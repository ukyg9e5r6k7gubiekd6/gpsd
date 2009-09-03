/* $Id$ */
/*
 * Driver for the iTalk binary protocol used by FasTrax
 */
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <unistd.h>
#include <time.h>
#include <stdio.h>

#include "gpsd_config.h"
#include "gpsd.h"
#if defined(ITRAX_ENABLE) && defined(BINARY_ENABLE)

#include "bits.h"
#include "driver_italk.h"

static gps_mask_t italk_parse(struct gps_device_t *, unsigned char *, size_t);
static gps_mask_t decode_itk_navfix(struct gps_device_t *, unsigned char *, size_t);
static gps_mask_t decode_itk_prnstatus(struct gps_device_t *, unsigned char *, size_t);
static gps_mask_t decode_itk_utcionomodel(struct gps_device_t *, unsigned char *, size_t);
static gps_mask_t decode_itk_subframe(struct gps_device_t *, unsigned char *, size_t);

static gps_mask_t decode_itk_navfix(struct gps_device_t *session, unsigned char *buf, size_t len)
{
    unsigned int tow;
    unsigned short gps_week, flags, cflags, pflags;
    gps_mask_t mask = 0;
    double epx, epy, epz, evx, evy, evz;
    double t;

    if (len != 296){
	gpsd_report(LOG_PROG, "ITALK: bad NAV_FIX (len %zu, should be 296)\n",
		    len);
	return -1;
    }

    flags = getleuw(buf, 7 + 4);
    cflags = getleuw(buf, 7 + 6);
    pflags = getleuw(buf, 7 + 8);

    session->gpsdata.status = STATUS_NO_FIX;
    session->gpsdata.fix.mode = MODE_NO_FIX;
    session->cycle_state = cycle_start;
    mask =  ONLINE_SET | MODE_SET | STATUS_SET;

    /* just bail out if this fix is not marked valid */
    if (0 != (pflags & FIX_FLAG_MASK_INVALID) || 0 == (flags & FIXINFO_FLAG_VALID))
	return mask;

    gps_week = (ushort)getlesw(buf, 7 + 82);
    tow = getleul(buf, 7 + 84);
    t = gpstime_to_unix((int)gps_week, tow/1000.0) - session->context->leap_seconds;
    session->gpsdata.sentence_time = t;
    session->gpsdata.fix.time = t;
    mask |= TIME_SET;

    epx = (double)(getlesl(buf, 7 + 96)/100.0);
    epy = (double)(getlesl(buf, 7 + 100)/100.0);
    epz = (double)(getlesl(buf, 7 + 104)/100.0);
    evx = (double)(getlesl(buf, 7 + 186)/1000.0);
    evy = (double)(getlesl(buf, 7 + 190)/1000.0);
    evz = (double)(getlesl(buf, 7 + 194)/1000.0);
    ecef_to_wgs84fix(&session->gpsdata, epx, epy, epz, evx, evy, evz);
    mask |= LATLON_SET | ALTITUDE_SET | SPEED_SET | TRACK_SET | CLIMB_SET  ;
    session->gpsdata.fix.epx = session->gpsdata.fix.epy = (double)(getlesl(buf, 7 + 252)/100.0);
    session->gpsdata.fix.eps = (double)(getlesl(buf, 7 + 254)/100.0);

    #define MAX(a,b) (((a) > (b)) ? (a) : (b))
    session->gpsdata.satellites_used =
	(int)MAX(getleuw(buf, 7 + 12), getleuw(buf, 7 + 14));
    mask |= USED_SET ;

    if (flags & FIX_CONV_DOP_VALID){
	session->gpsdata.hdop = (double)(getleuw(buf, 7 + 56)/100.0);
	session->gpsdata.gdop = (double)(getleuw(buf, 7 + 58)/100.0);
	session->gpsdata.pdop = (double)(getleuw(buf, 7 + 60)/100.0);
	session->gpsdata.vdop = (double)(getleuw(buf, 7 + 62)/100.0);
	session->gpsdata.tdop = (double)(getleuw(buf, 7 + 64)/100.0);
	mask |= HDOP_SET | GDOP_SET | PDOP_SET | VDOP_SET | TDOP_SET;
    }

    if ((pflags & FIX_FLAG_MASK_INVALID) == 0 && (flags & FIXINFO_FLAG_VALID) != 0){
	if (pflags & FIX_FLAG_3DFIX)
	    session->gpsdata.fix.mode = MODE_3D;
	else
	    session->gpsdata.fix.mode = MODE_2D;

	if (pflags & FIX_FLAG_DGPS_CORRECTION)
	    session->gpsdata.status = STATUS_DGPS_FIX;
	else
	    session->gpsdata.status = STATUS_FIX;
    }

    return mask;
}

static gps_mask_t decode_itk_prnstatus(struct gps_device_t *session, unsigned char *buf, size_t len)
{
    unsigned int i, tow, nsv, nchan, st;
    unsigned short gps_week;
    double t;

    if (len < 62){
	gpsd_report(LOG_PROG, "ITALK: runt PRN_STATUS (len=%zu)\n", len);
	return ERROR_SET;
    }

    gps_week = getleuw(buf, 7 + 4);
    tow = getleul(buf, 7 + 6);
    t = gpstime_to_unix((int)gps_week, tow/1000.0) - session->context->leap_seconds;
    session->gpsdata.sentence_time = session->gpsdata.fix.time = t;

    gpsd_zero_satellites(&session->gpsdata);
    nsv = 0;
    nchan = (unsigned int)getleuw(buf, 7 +50);
    if (nchan > MAX_NR_VISIBLE_PRNS)
	    nchan = MAX_NR_VISIBLE_PRNS;
    for (i = st = 0; i < nchan; i++) {
	unsigned int off = 7+ 52 + 10 * i;
	unsigned short flags;

	flags = getleuw(buf, off);
	session->gpsdata.ss[i]		= (float)(getleuw(buf, off+2)&0xff);
	session->gpsdata.PRN[i]		= (int)getleuw(buf, off+4)&0xff;
	session->gpsdata.elevation[i]	= (int)getlesw(buf, off+6)&0xff;
	session->gpsdata.azimuth[i]	= (int)getlesw(buf, off+8)&0xff;
	if (session->gpsdata.PRN[i]){
	    st++;
	    if (flags & PRN_FLAG_USE_IN_NAV)
		session->gpsdata.used[nsv++] = session->gpsdata.PRN[i];
	}
    }
    session->gpsdata.satellites = (int)st;
    session->gpsdata.satellites_used = (int)nsv;

    return USED_SET | SATELLITE_SET | TIME_SET;
}

static gps_mask_t decode_itk_utcionomodel(struct gps_device_t *session, unsigned char *buf, size_t len)
{
    unsigned int tow;
    int leap;
    unsigned short gps_week, flags;
    double t;

    if (len != 64){
	gpsd_report(LOG_PROG,
		    "ITALK: bad UTC_IONO_MODEL (len %zu, should be 64)\n", len);
	return ERROR_SET;
    }

    flags = getleuw(buf, 7);
    if (0 == (flags & UTC_IONO_MODEL_UTCVALID))
	return 0;

    leap = (int)getleuw(buf, 7 + 24);
    if (session->context->leap_seconds < leap)
	session->context->leap_seconds = leap;

    gps_week = getleuw(buf, 7 + 36);
    tow = getleul(buf, 7 + 38);
    t = gpstime_to_unix((int)gps_week, tow/1000.0) - session->context->leap_seconds;
    session->gpsdata.sentence_time = session->gpsdata.fix.time = t;

    return TIME_SET;
}

static gps_mask_t decode_itk_subframe(struct gps_device_t *session, unsigned char *buf, size_t len)
{
    unsigned short flags, prn, sf;
    unsigned int words[10];

    if (len != 64){
	gpsd_report(LOG_PROG,
		    "ITALK: bad SUBFRAME (len %zu, should be 64)\n", len);
	return ERROR_SET;
    }

    flags = getleuw(buf, 7 + 4);
    prn = getleuw(buf, 7 + 6);
    sf = getleuw(buf, 7 + 8);
    gpsd_report(LOG_PROG, "iTalk SUBFRAME prn %u sf %u - decode %s %s\n",
		prn, sf,
		flags & SUBFRAME_WORD_FLAG_MASK ? "error" : "ok",
		flags & SUBFRAME_GPS_PREAMBLE_INVERTED ? "(inverted)" : "");
    if (flags & SUBFRAME_WORD_FLAG_MASK)
	return ONLINE_SET | ERROR_SET; // don't try decode an erroneous packet

    /*
     * Timo says "SUBRAME message contains decoded navigation message subframe
     * words with parity checking done but parity bits still present."
     */
    words[0] = (getbeul(buf, 7 + 14) & 0x3fffffff) >> 6;
    words[1] = (getleul(buf, 7 + 18) & 0x3fffffff) >> 6;
    words[2] = (getleul(buf, 7 + 22) & 0x3fffffff) >> 6;
    words[3] = (getleul(buf, 7 + 26) & 0x3fffffff) >> 6;
    words[4] = (getleul(buf, 7 + 30) & 0x3fffffff) >> 6;
    words[5] = (getleul(buf, 7 + 34) & 0x3fffffff) >> 6;
    words[6] = (getleul(buf, 7 + 38) & 0x3fffffff) >> 6;
    words[7] = (getleul(buf, 7 + 42) & 0x3fffffff) >> 6;
    words[8] = (getleul(buf, 7 + 46) & 0x3fffffff) >> 6;
    words[9] = (getleul(buf, 7 + 50) & 0x3fffffff) >> 6;

    gpsd_interpret_subframe(session, words);
    return ONLINE_SET;
}

/*@ +charint @*/
static gps_mask_t italk_parse(struct gps_device_t *session, unsigned char *buf, size_t len)
{
    unsigned int type;
    gps_mask_t mask = 0;

    if (len == 0)
	return 0;

    type = (uint)getub(buf, 4);
    /* we may need to dump the raw packet */
    gpsd_report(LOG_RAW, "raw italk packet type 0x%02x length %zu: %s\n",
	type, len, gpsd_hexdump_wrapper(buf, len, LOG_RAW));

    switch (type)
    {
    case ITALK_NAV_FIX:
	gpsd_report(LOG_IO, "iTalk NAV_FIX len %zu\n", len);
	mask = decode_itk_navfix(session, buf, len);
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
    if (mask == ERROR_SET)
	mask = 0;
    else
	(void)snprintf(session->gpsdata.tag, sizeof(session->gpsdata.tag),
	   "ITK-%02x",type);

    return mask | ONLINE_SET;
}
/*@ -charint @*/

static gps_mask_t italk_parse_input(struct gps_device_t *session)
{
    gps_mask_t st;

    if (session->packet.type == ITALK_PACKET){
	st = italk_parse(session, session->packet.outbuffer, session->packet.outbuflen);
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
    ssize_t      status;

    /*@ -mayaliasunique **/
    session->msgbuflen = msglen;
    (void)memcpy(session->msgbuf, msg, msglen);
    /*@ +mayaliasunique **/
    /* we may need to dump the message */
    gpsd_report(LOG_IO, "writing italk control type %02x:%s\n",
		msg[0], gpsd_hexdump_wrapper(msg, msglen, LOG_IO));
    status = write(session->gpsdata.gps_fd, msg, msglen);
    (void)tcdrain(session->gpsdata.gps_fd);
    return(status);
}
/*@ -charint +usedef +compdef @*/
#endif /* ALLOW_CONTROLSEND */

#ifdef ALLOW_RECONFIGURE
static bool italk_set_mode(struct gps_device_t *session UNUSED, 
			   speed_t speed UNUSED, 
			   char parity UNUSED, int stopbits UNUSED, 
			   bool mode UNUSED)
{
#if 0
    /*@ +charint @*/
    char msg[] = {0,};

    /* HACK THE MESSAGE */

    return (italk_control_send(session, msg, sizeof(msg)) != -1);
    /*@ +charint @*/
#endif

    return false;	/* until this actually works */
}

static bool italk_speed(struct gps_device_t *session, 
			speed_t speed, char parity, int stopbits)
{
    return italk_set_mode(session, speed, parity, stopbits, true);
}

static void italk_mode(struct gps_device_t *session,  int mode)
{
    if (mode == MODE_NMEA) {
	(void)italk_set_mode(session, 
			     session->gpsdata.dev.baudrate,
			     (char)session->gpsdata.dev.parity,
			     (int)session->gpsdata.dev.stopbits,
			     false);
    }
}

static void italk_configurator(struct gps_device_t *session, unsigned int seq)
{
    if (seq == 0 && session->packet.type == NMEA_PACKET)
	(void)italk_set_mode(session, 
			     session->gpsdata.dev.baudrate, 
			     (char)session->gpsdata.dev.parity,
			     (int)session->gpsdata.dev.stopbits,
			     true);
}
#endif /* ALLOW_RECONFIGURE */

#ifdef __not_yet__
static void italk_ping(struct gps_device_t *session)
/* send a "ping". it may help us detect an itrax more quickly */
{
    char *ping = "<?>";
    (void)gpsd_write(session, ping, 3);
}
#endif /* __not_yet__ */

/* this is everything we export */
const struct gps_type_t italk_binary =
{
    .type_name      = "iTalk binary",	/* full name of type */
    .packet_type    = ITALK_PACKET,	/* associated lexer packet type */
    .trigger	    = NULL,		/* recognize the type */
    .channels       = 12,		/* consumer-grade GPS */
    .probe_wakeup   = NULL,		/* no wakeup to be done before hunt */
    .probe_detect   = NULL,		/* how to detect at startup time */
    .probe_subtype  = NULL,		/* initialize the device */
    .get_packet     = generic_get,	/* use generic packet grabber */
    .parse_packet   = italk_parse_input,/* parse message packets */
    .rtcm_writer    = pass_rtcm,	/* send RTCM data straight */
#ifdef ALLOW_CONTROLSEND
    .control_send   = italk_control_send,	/* how to send a control string */
#endif /* ALLOW_CONTROLSEND */
#ifdef ALLOW_RECONFIGURE
    .configurator   = italk_configurator,/* configure the device */
    .speed_switcher = italk_speed,	/* we can change baud rates */
    .mode_switcher  = italk_mode,	/* there is a mode switcher */
    .rate_switcher  = NULL,		/* no sample-rate switcher */
    .min_cycle      = 1,		/* not relevant, no rate switch */
    .revert         = NULL,		/* no setting-reversion method */
#endif /* ALLOW_RECONFIGURE */
    .wrapup         = NULL,		/* no close hook */
};
#endif /* defined(ITRAX_ENABLE) && defined(BINARY_ENABLE) */

#ifdef ANCIENT_ITRAX_ENABLE
/**************************************************************************
 *
 * The NMEA mode of the iTrax chipset, as used in the FastTrax and others.
 *
 * As described by v1.31 of the NMEA Protocol Specification for the
 * iTrax02 Evaluation Kit, 2003-06-12.
 * v1.18 of the  manual, 2002-19-6, describes effectively
 * the same protocol, but without ZDA.
 *
 **************************************************************************/

/*
 * Enable GGA=0x2000, RMC=0x8000, GSA=0x0002, GSV=0x0001, ZDA=0x0004.
 * Disable GLL=0x1000, VTG=0x4000, FOM=0x0020, PPS=0x0010.
 * This is 82+75+67+(3*60)+34 = 438 characters 
 * 
 * 1200   => at most 1 fix per 4 seconds
 * 2400   => at most 1 fix per 2 seconds
 * 4800   => at most 1 fix per 1 seconds
 * 9600   => at most 2 fixes per second
 * 19200  => at most 4 fixes per second
 * 57600  => at most 13 fixes per second
 * 115200 => at most 26 fixes per second
 *
 * We'd use FOM, but they don't specify a confidence interval.
 */
#define ITRAX_MODESTRING	"$PFST,NMEA,A007,%d\r\n"

static int literal_send(int fd, const char *fmt, ... )
/* ship a raw command to the GPS */
{
    ssize_t status;
    va_list ap;

    va_start(ap, fmt) ;
    (void)vsnprintf(session->msgbuf, sizeof(session->msgbuf), fmt, ap);
    va_end(ap);
    session->msgbuflen = strlen(session->msgbuf);
    return gpsd_write(fd, session->msgbuf, session->msgbuflen);
}

static void itrax_probe_subtype(struct gps_device_t *session, unsigned int seq)
/* start it reporting */
{
    if (seq == 0) {
	/* initialize GPS clock with current system time */
	struct tm when;
	double integral, fractional;
	time_t intfixtime;
	char buf[31], frac[6];
	fractional = modf(timestamp(), &integral);
	intfixtime = (time_t)integral;
	(void)gmtime_r(&intfixtime, &when);
	/* FIXME: so what if my local clock is wrong? */
	(void)strftime(buf, sizeof(buf), "$PFST,INITAID,%H%M%S.XX,%d%m%y\r\n", &when);
	(void)snprintf(frac, sizeof(frac), "%.2f", fractional);
	buf[21] = frac[2]; buf[22] = frac[3];
	(void)literal_send(session->gpsdata.gps_fd, buf);
	/* maybe this should be considered a reconfiguration? */
	(void)literal_send(session->gpsdata.gps_fd, "$PFST,START\r\n");
    }
}

#ifdef ALLOW_RECONFIGURE
static void itrax_configurator(struct gps_device_t *session, int seq)
/* set synchronous mode */
{
    if (seq == 0) {
	(void)literal_send(session->gpsdata.gps_fd, "$PFST,SYNCMODE,1\r\n");
	(void)literal_send(session->gpsdata.gps_fd,
		    ITRAX_MODESTRING, session->gpsdata.baudrate);
    }
}

static bool itrax_speed(struct gps_device_t *session, 
			speed_t speed, char parity UNUSED, int stopbits UNUSED)
/* change the baud rate */
{
    return literal_send(session->gpsdata.gps_fd, ITRAX_MODESTRING, speed) >= 0;
    return false;
}

static bool itrax_rate(struct gps_device_t *session, double rate)
/* change the sample rate of the GPS */
{
    return literal_send(session->gpsdata.gps_fd, "$PSFT,FIXRATE,%d\r\n", rate) >= 0;
}
#endif /* ALLOW_RECONFIGURE */

static void itrax_wrap(struct gps_device_t *session)
/* stop navigation, this cuts the power drain */
{
#ifdef ALLOW_RECONFIGURE
    (void)literal_send(session->gpsdata.gps_fd, "$PFST,SYNCMODE,0\r\n");
#endif /* ALLOW_RECONFIGURE */
    (void)literal_send(session->gpsdata.gps_fd, "$PFST,STOP\r\n");
}

/*@ -redef @*/
const static struct gps_type_t itrax = {
    .type_name      = "iTrax",		/* full name of type */
    .packet_type    = NMEA_PACKET;	/* associated lexer packet type */
    .trigger        = "$PFST,OK",	/* tells us to switch to Itrax */
    .channels       = 12,		/* consumer-grade GPS */
    .probe_wakeup   = NULL,		/* no wakeup to be done before hunt */
    .probe_detect   = NULL,		/* no probe */
    .probe_subtype  = itrax_probe_subtype,	/* initialize */
    .get_packet     = generic_get,	/* how to get a packet */
    .parse_packet   = nmea_parse_input,	/* how to interpret a packet */
    .rtcm_writer    = NULL,		/* iTrax doesn't support DGPS/WAAS/EGNOS */
#ifdef ALLOW_CONTROLSEND
    .control_send   = garmin_control_send,	/* send raw bytes */
#endif /* ALLOW_CONTROLSEND */
#ifdef ALLOW_RECONFIGURE
    .configurator   = itrax_configurator,/* set synchronous mode */
    .speed_switcher = itrax_speed,	/* how to change speeds */
    .mode_switcher  = NULL,		/* no mode switcher */
    .rate_switcher  = itrax_rate,	/* there's a sample-rate switcher */
    .min_cycle      = 0,		/* no hard limit */
    .revert         = NULL,		/* no setting-reversion method */
#endif /* ALLOW_RECONFIGURE */
    .wrapup         = itrax_wrap,	/* sleep the receiver */
};
/*@ -redef @*/
#endif /* ITRAX_ENABLE */

