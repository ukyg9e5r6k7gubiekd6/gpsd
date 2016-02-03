/*
 * UBX driver.  All capabilities are common to Antaris4 and u-blox 6.
 * Reference manuals are at
 * http://www.u-blox.com/en/download/documents-a-resources/u-blox-6-gps-modules-resources.html
 *
 * updated for u-blox 8
 * http://www.ublox.com/images/downloads/Product_Docs/u-bloxM8_ReceiverDescriptionProtocolSpec_%28UBX-13003221%29_Public.pdf
 *
 * Week counters are not limited to 10 bits. It's unknown what
 * the firmware is doing to disambiguate them, if anything; it might just
 * be adding a fixed offset based on a hidden epoch value, in which case
 * unhappy things will occur on the next rollover.
 *
 * For the Antaris 4, the default leap-second offset (before getting one from
 * the sats, one presumes) is 0sec; for the u-blox 6 it's 15sec.
 *
 * This file is Copyright (c) 2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 *
 */

#include <stdio.h>
#include <stdbool.h>
#include <assert.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

#include "gpsd.h"
#if defined(UBLOX_ENABLE) && defined(BINARY_ENABLE)
#include "driver_ubx.h"

#include "bits.h"

/*
 * A ubx packet looks like this:
 * leader: 0xb5 0x62
 * message class: 1 byte
 * message type: 1 byte
 * length of payload: 2 bytes
 * payload: variable length
 * checksum: 2 bytes
 *
 * see also the FV25 and UBX documents on reference.html
 */
#define UBX_PREFIX_LEN		6
#define UBX_CLASS_OFFSET	2
#define UBX_TYPE_OFFSET		3

/* because we hates magic numbers forever */
#define USART1_ID		1
#define USART2_ID		2
#define USB_ID			3
#define UBX_PROTOCOL_MASK	0x01
#define NMEA_PROTOCOL_MASK	0x02
#define RTCM_PROTOCOL_MASK	0x04
#define UBX_CFG_LEN		20
#define outProtoMask		14

static gps_mask_t ubx_parse(struct gps_device_t *session, unsigned char *buf,
			    size_t len);
static gps_mask_t ubx_msg_nav_dop(struct gps_device_t *session,
				  unsigned char *buf, size_t data_len);
static void ubx_msg_inf(struct gps_device_t *session, unsigned char *buf, size_t data_len);
static gps_mask_t ubx_msg_nav_pvt(struct gps_device_t *session,
				  unsigned char *buf, size_t data_len);
static void ubx_msg_sbas(struct gps_device_t *session, unsigned char *buf);
static gps_mask_t ubx_msg_nav_sol(struct gps_device_t *session,
				  unsigned char *buf, size_t data_len);
static gps_mask_t ubx_msg_nav_svinfo(struct gps_device_t *session,
				     unsigned char *buf, size_t data_len);
static gps_mask_t ubx_msg_nav_timegps(struct gps_device_t *session,
				      unsigned char *buf, size_t data_len);
static void ubx_msg_mon_ver(struct gps_device_t *session,
				      unsigned char *buf, size_t data_len);
#ifdef RECONFIGURE_ENABLE
static void ubx_mode(struct gps_device_t *session, int mode);
#endif /* RECONFIGURE_ENABLE */

/**
 * Receiver/Software Version
 * UBX-MON-VER
 *
 * sadly more info than fits in session->swtype for now.
 * so squish the data hard, max is maybe 100?
 */
static void
ubx_msg_mon_ver(struct gps_device_t *session, unsigned char *buf,
		size_t data_len)
{
    size_t n = 0;	/* extended info counter */
    char obuf[128];     /* temp version string buffer */

    if ( 44 > data_len ) {
	/* incomplete message */
        return;
    }

    /* save SW and HW Version as subtype */
    (void)snprintf(obuf, sizeof(obuf),
		   "SW %.30s,HW %.10s",
		   (char *)&buf[UBX_MESSAGE_DATA_OFFSET + 0],
		   (char *)&buf[UBX_MESSAGE_DATA_OFFSET + 30]);

    /* get n number of Extended info strings.  what is max n? */
    for ( n = 0; ; n++ ) {
        size_t start_of_str = UBX_MESSAGE_DATA_OFFSET + 40 + (30 * n);

        if ( (start_of_str + 2 ) > data_len ) {
	    /* last one can be shorter than 30 */
            /* no more data */
            break;
        }
	(void)strlcat(obuf, ",", sizeof(obuf));
	(void)strlcat(obuf, (char *)&buf[start_of_str], sizeof(obuf));
    }
    /* save what we can */
    (void)strlcpy(session->subtype, obuf, sizeof(session->subtype));

    /* output SW and HW Version at LOG_INFO */
    gpsd_log(&session->context->errout, LOG_INF,
	     "UBX_MON_VER: %.*s\n",
             (int)sizeof(obuf), obuf);
}

/**
 * Navigation Position Velocity Time  solution message
 */
static gps_mask_t
ubx_msg_nav_pvt(struct gps_device_t *session, unsigned char *buf,
		size_t data_len)
{
    unsigned int flags;
    gps_mask_t mask = 0;

    if (data_len != 92)
	return 0;

    flags = (unsigned int)getub(buf, 21);

    /* TODO: finish decoding UBX_MON_PVT
     * no need until depreacaed UBX_MON_SOL is dead
     */
    gpsd_log(&session->context->errout, LOG_DATA,
	     "NAV-PVT: flags:%02x\n", flags);
    return mask;
}

/**
 * Navigation solution message
 */
static gps_mask_t
ubx_msg_nav_sol(struct gps_device_t *session, unsigned char *buf,
		size_t data_len)
{
    unsigned int flags;
    double epx, epy, epz, evx, evy, evz;
    unsigned char navmode;
    gps_mask_t mask;

    if (data_len != 52)
	return 0;

    flags = (unsigned int)getub(buf, 11);
    mask = 0;
    if ((flags & (UBX_SOL_VALID_WEEK | UBX_SOL_VALID_TIME)) != 0) {
	unsigned short gw;
	unsigned int tow;
	tow = (unsigned int)getleu32(buf, 0);
	gw = (unsigned short)getles16(buf, 8);
	session->newdata.time = gpsd_gpstime_resolve(session, gw, tow / 1000.0);
	mask |= TIME_SET | PPSTIME_IS;
    }

    epx = (double)(getles32(buf, 12) / 100.0);
    epy = (double)(getles32(buf, 16) / 100.0);
    epz = (double)(getles32(buf, 20) / 100.0);
    evx = (double)(getles32(buf, 28) / 100.0);
    evy = (double)(getles32(buf, 32) / 100.0);
    evz = (double)(getles32(buf, 36) / 100.0);
    ecef_to_wgs84fix(&session->newdata, &session->gpsdata.separation,
		     epx, epy, epz, evx, evy, evz);
    mask |= LATLON_SET | ALTITUDE_SET | SPEED_SET | TRACK_SET | CLIMB_SET;

    if (session->driver.ubx.last_herr > 0.0) {
	session->newdata.epx = session->newdata.epy = session->driver.ubx.last_herr;
	mask |= HERR_SET;
	session->driver.ubx.last_herr = 0.0;
    }

    if (session->driver.ubx.last_verr > 0.0) {
	session->newdata.epv = session->driver.ubx.last_verr;
	mask |= VERR_SET;
	session->driver.ubx.last_verr = 0.0;
    }

    session->newdata.eps = (double)(getles32(buf, 40) / 100.0);
    mask |= SPEEDERR_SET;

    /* Better to have a single point of truth about DOPs */
    //session->gpsdata.dop.pdop = (double)(getleu16(buf, 44)/100.0);
    session->gpsdata.satellites_used = (int)getub(buf, 47);

    navmode = (unsigned char)getub(buf, 10);
    switch (navmode) {
    case UBX_MODE_TMONLY:
    case UBX_MODE_3D:
	session->newdata.mode = MODE_3D;
	break;
    case UBX_MODE_2D:
    case UBX_MODE_DR:		/* consider this too as 2D */
    case UBX_MODE_GPSDR:	/* FIX-ME: DR-aided GPS may be valid 3D */
	session->newdata.mode = MODE_2D;
	break;
    default:
	session->newdata.mode = MODE_NO_FIX;
    }

    if ((flags & UBX_SOL_FLAG_DGPS) != 0)
	session->gpsdata.status = STATUS_DGPS_FIX;
    else if (session->newdata.mode != MODE_NO_FIX)
	session->gpsdata.status = STATUS_FIX;

    mask |= MODE_SET | STATUS_SET;
    gpsd_log(&session->context->errout, LOG_DATA,
	     "NAVSOL: time=%.2f lat=%.2f lon=%.2f alt=%.2f track=%.2f speed=%.2f climb=%.2f mode=%d status=%d used=%d\n",
	     session->newdata.time,
	     session->newdata.latitude,
	     session->newdata.longitude,
	     session->newdata.altitude,
	     session->newdata.track,
	     session->newdata.speed,
	     session->newdata.climb,
	     session->newdata.mode,
	     session->gpsdata.status,
	     session->gpsdata.satellites_used);
    return mask;
}

 /**
 * Geodetic position solution message
 */
static gps_mask_t
ubx_msg_nav_posllh(struct gps_device_t *session, unsigned char *buf,
		   size_t data_len UNUSED)
{
    session->driver.ubx.last_herr = (double)(getleu32(buf, 20) / 1000.0);
    session->driver.ubx.last_verr = (double)(getleu32(buf, 24) / 1000.0);
    return 0;
}

/**
 * Dilution of precision message
 */
static gps_mask_t
ubx_msg_nav_dop(struct gps_device_t *session, unsigned char *buf,
		size_t data_len)
{
    if (data_len != 18)
	return 0;

    /*
     * We make a deliberate choice not to clear DOPs from the
     * last skyview here, but rather to treat this as a supplement
     * to our calculations from the visibility matrix, trusting
     * the firmware algorithms over ours.
     */
    session->gpsdata.dop.gdop = (double)(getleu16(buf, 4) / 100.0);
    session->gpsdata.dop.pdop = (double)(getleu16(buf, 6) / 100.0);
    session->gpsdata.dop.tdop = (double)(getleu16(buf, 8) / 100.0);
    session->gpsdata.dop.vdop = (double)(getleu16(buf, 10) / 100.0);
    session->gpsdata.dop.hdop = (double)(getleu16(buf, 12) / 100.0);
    gpsd_log(&session->context->errout, LOG_DATA,
	     "NAVDOP: gdop=%.2f pdop=%.2f "
	     "hdop=%.2f vdop=%.2f tdop=%.2f mask={DOP}\n",
	     session->gpsdata.dop.gdop,
	     session->gpsdata.dop.hdop,
	     session->gpsdata.dop.vdop,
	     session->gpsdata.dop.pdop, session->gpsdata.dop.tdop);
    return DOP_SET;
}

/**
 * GPS Leap Seconds
 */
static gps_mask_t
ubx_msg_nav_timegps(struct gps_device_t *session, unsigned char *buf,
		    size_t data_len)
{
    unsigned int gw, tow, flags;

    if (data_len != 16)
	return 0;

    tow = (unsigned int)getleu32(buf, 0);
    gw = (unsigned int)getles16(buf, 8);
    flags = (unsigned int)getub(buf, 11);
    if ((flags & 0x7) != 0)
	session->context->leap_seconds = (int)getub(buf, 10);
    session->newdata.time = gpsd_gpstime_resolve(session,
					      (unsigned short int)gw,
					      (double)tow / 1000.0);

    gpsd_log(&session->context->errout, LOG_DATA,
	     "TIMEGPS: time=%.2f leap=%d, mask={TIME}\n",
	     session->newdata.time, session->context->leap_seconds);
    return TIME_SET | PPSTIME_IS;
}

/**
 * GPS Satellite Info
 */
static gps_mask_t
ubx_msg_nav_svinfo(struct gps_device_t *session, unsigned char *buf,
		   size_t data_len)
{
    unsigned int i, nchan, nsv, st;

    if (data_len < 152) {
	gpsd_log(&session->context->errout, LOG_PROG,
		 "runt svinfo (datalen=%zd)\n", data_len);
	return 0;
    }
    nchan = (unsigned int)getub(buf, 4);
    if (nchan > MAXCHANNELS) {
	gpsd_log(&session->context->errout, LOG_WARN,
		 "Invalid NAV SVINFO message, >%d reported visible",
		 MAXCHANNELS);
	return 0;
    }
    gpsd_zero_satellites(&session->gpsdata);
    nsv = 0;
    for (i = st = 0; i < nchan; i++) {
	unsigned int off = 8 + 12 * i;
	bool used = (bool)(getub(buf, off + 2) & 0x01);
	if ((int)getub(buf, off + 4) == 0)
	    continue;		/* LEA-5H seems to have a bug reporting sats it does not see or hear */
	session->gpsdata.skyview[st].PRN = (short)getub(buf, off + 1);
	session->gpsdata.skyview[st].ss = (float)getub(buf, off + 4);
	session->gpsdata.skyview[st].elevation = (short)getsb(buf, off + 5);
	session->gpsdata.skyview[st].azimuth = (short)getles16(buf, off + 6);
	session->gpsdata.skyview[st].used = used;
	if (session->gpsdata.skyview[st].PRN == 0)
	    continue;
	if (used || session->gpsdata.skyview[st].PRN == (short)session->driver.ubx.sbas_in_use) {
	    nsv++;
	    session->gpsdata.skyview[st].used = true;
	}
	st++;
    }

    session->gpsdata.skyview_time = NAN;
    session->gpsdata.satellites_visible = (int)st;
    session->gpsdata.satellites_used = (int)nsv;
    gpsd_log(&session->context->errout, LOG_DATA,
	     "SVINFO: visible=%d used=%d mask={SATELLITE|USED}\n",
	     session->gpsdata.satellites_visible,
	     session->gpsdata.satellites_used);
    return SATELLITE_SET | USED_IS;
}

/*
 * SBAS Info
 */
static void ubx_msg_sbas(struct gps_device_t *session, unsigned char *buf)
{
#ifdef __UNUSED_DEBUG__
    unsigned int i, nsv;

    gpsd_log(&session->context->errout, LOG_WARN,
	     "SBAS: %d %d %d %d %d\n",
	     (int)getub(buf, 4), (int)getub(buf, 5), (int)getub(buf, 6),
	     (int)getub(buf, 7), (int)getub(buf, 8));

    nsv = (int)getub(buf, 8);
    for (i = 0; i < nsv; i++) {
	int off = 12 + 12 * i;
	gpsd_log(&session->context->errout, LOG_WARN,
		 "SBAS info on SV: %d\n", (int)getub(buf, off));
    }
#endif /* __UNUSED_DEBUG__ */
/* really 'in_use' depends on the sats info, EGNOS is still in test */
/* In WAAS areas one might also check for the type of corrections indicated */
    session->driver.ubx.sbas_in_use = (unsigned char)getub(buf, 4);
}

/*
 * Raw Subframes
 */
static gps_mask_t ubx_msg_sfrb(struct gps_device_t *session, unsigned char *buf)
{
    unsigned int i, chan, svid;
    uint32_t words[10];

    chan = (unsigned int)getub(buf, 0);
    svid = (unsigned int)getub(buf, 1);
    gpsd_log(&session->context->errout, LOG_PROG,
	     "UBX_RXM_SFRB: %u %u\n", chan, svid);

    /* UBX does all the parity checking, but still bad data gets through */
    for (i = 0; i < 10; i++) {
	words[i] = (uint32_t)getleu32(buf, 4 * i + 2) & 0xffffff;
    }

    return gpsd_interpret_subframe(session, svid, words);
}

static void ubx_msg_inf(struct gps_device_t *session, unsigned char *buf, size_t data_len)
{
    unsigned short msgid;
    static char txtbuf[MAX_PACKET_LENGTH];

    msgid = (unsigned short)((buf[2] << 8) | buf[3]);
    if (data_len > MAX_PACKET_LENGTH - 1)
	data_len = MAX_PACKET_LENGTH - 1;

    (void)strlcpy(txtbuf, (char *)buf + UBX_PREFIX_LEN, sizeof(txtbuf));
    txtbuf[data_len] = '\0';
    switch (msgid) {
    case UBX_INF_DEBUG:
	gpsd_log(&session->context->errout, LOG_PROG, "UBX_INF_DEBUG: %s\n", txtbuf);
	break;
    case UBX_INF_TEST:
	gpsd_log(&session->context->errout, LOG_PROG, "UBX_INF_TEST: %s\n", txtbuf);
	break;
    case UBX_INF_NOTICE:
	gpsd_log(&session->context->errout, LOG_INF, "UBX_INF_NOTICE: %s\n", txtbuf);
	break;
    case UBX_INF_WARNING:
	gpsd_log(&session->context->errout, LOG_WARN, "UBX_INF_WARNING: %s\n", txtbuf);
	break;
    case UBX_INF_ERROR:
	gpsd_log(&session->context->errout, LOG_WARN, "UBX_INF_ERROR: %s\n", txtbuf);
	break;
    default:
	break;
    }
    return;
}

gps_mask_t ubx_parse(struct gps_device_t * session, unsigned char *buf,
		     size_t len)
{
    size_t data_len;
    unsigned short msgid;
    gps_mask_t mask = 0;

    /* the packet at least contains a head long enough for an empty message */
    if (len < UBX_PREFIX_LEN)
	return 0;

    session->cycle_end_reliable = true;

    /* extract message id and length */
    msgid = (buf[2] << 8) | buf[3];
    data_len = (size_t) getles16(buf, 4);
    switch (msgid) {
    case UBX_NAV_POSECEF:
	gpsd_log(&session->context->errout, LOG_DATA, "UBX_NAV_POSECEF\n");
	break;
    case UBX_NAV_POSLLH:
	gpsd_log(&session->context->errout, LOG_DATA, "UBX_NAV_POSLLH\n");
	mask = ubx_msg_nav_posllh(session, &buf[UBX_PREFIX_LEN], data_len);
	break;
    case UBX_NAV_STATUS:
	gpsd_log(&session->context->errout, LOG_DATA, "UBX_NAV_STATUS\n");
	break;
    case UBX_NAV_DOP:
	gpsd_log(&session->context->errout, LOG_PROG, "UBX_NAV_DOP\n");
	mask = ubx_msg_nav_dop(session, &buf[UBX_PREFIX_LEN], data_len);
	break;
    case UBX_NAV_SOL:
        /* UBX-NAV-SOL deprecated, use UBX-NAV-PVT instead */
	gpsd_log(&session->context->errout, LOG_PROG, "UBX_NAV_SOL\n");
	mask =
	    ubx_msg_nav_sol(session, &buf[UBX_PREFIX_LEN],
			    data_len) | (CLEAR_IS | REPORT_IS);
	break;
    case UBX_NAV_PVT:
	gpsd_log(&session->context->errout, LOG_PROG, "UBX_NAV_PVT\n");
	mask = ubx_msg_nav_pvt(session, &buf[UBX_PREFIX_LEN], data_len);
        break;
    case UBX_NAV_POSUTM:
	gpsd_log(&session->context->errout, LOG_DATA, "UBX_NAV_POSUTM\n");
	break;
    case UBX_NAV_VELECEF:
	gpsd_log(&session->context->errout, LOG_DATA, "UBX_NAV_VELECEF\n");
	break;
    case UBX_NAV_VELNED:
	gpsd_log(&session->context->errout, LOG_DATA, "UBX_NAV_VELNED\n");
	break;
    case UBX_NAV_TIMEGPS:
	gpsd_log(&session->context->errout, LOG_PROG, "UBX_NAV_TIMEGPS\n");
	mask = ubx_msg_nav_timegps(session, &buf[UBX_PREFIX_LEN], data_len);
	break;
    case UBX_NAV_TIMEUTC:
	gpsd_log(&session->context->errout, LOG_DATA, "UBX_NAV_TIMEUTC\n");
	break;
    case UBX_NAV_CLOCK:
	gpsd_log(&session->context->errout, LOG_DATA, "UBX_NAV_CLOCK\n");
	break;
    case UBX_NAV_SVINFO:
	gpsd_log(&session->context->errout, LOG_PROG, "UBX_NAV_SVINFO\n");
	mask = ubx_msg_nav_svinfo(session, &buf[UBX_PREFIX_LEN], data_len);

	/* this is a hack to move some initialization until after we
	 * get some u-blox message so we know the GPS is alive */
	if ('\0' == session->subtype[0]) {
	    /* one time only */
	    (void)strlcpy(session->subtype, "Unknown", 8);
	    /* request SW and HW Versions */
	    (void)ubx_write(session, UBX_CLASS_MON, 0x04, NULL, 0);
	}

	break;
    case UBX_NAV_DGPS:
	gpsd_log(&session->context->errout, LOG_DATA, "UBX_NAV_DGPS\n");
	break;
    case UBX_NAV_SBAS:
	gpsd_log(&session->context->errout, LOG_DATA, "UBX_NAV_SBAS\n");
	ubx_msg_sbas(session, &buf[6]);
	break;
    case UBX_NAV_EKFSTATUS:
	gpsd_log(&session->context->errout, LOG_DATA, "UBX_NAV_EKFSTATUS\n");
	break;

    case UBX_RXM_RAW:
	gpsd_log(&session->context->errout, LOG_DATA, "UBX_RXM_RAW\n");
	break;
    case UBX_RXM_SFRB:
	mask = ubx_msg_sfrb(session, &buf[UBX_PREFIX_LEN]);
	break;
    case UBX_RXM_SVSI:
	gpsd_log(&session->context->errout, LOG_PROG, "UBX_RXM_SVSI\n");
	break;
    case UBX_RXM_ALM:
	gpsd_log(&session->context->errout, LOG_DATA, "UBX_RXM_ALM\n");
	break;
    case UBX_RXM_EPH:
	gpsd_log(&session->context->errout, LOG_DATA, "UBX_RXM_EPH\n");
	break;
    case UBX_RXM_POSREQ:
	gpsd_log(&session->context->errout, LOG_DATA, "UBX_RXM_POSREQ\n");
	break;

    case UBX_MON_SCHED:
	gpsd_log(&session->context->errout, LOG_DATA, "UBX_MON_SCHED\n");
	break;
    case UBX_MON_IO:
	gpsd_log(&session->context->errout, LOG_DATA, "UBX_MON_IO\n");
	break;
    case UBX_MON_IPC:
	gpsd_log(&session->context->errout, LOG_DATA, "UBX_MON_IPC\n");
	break;
    case UBX_MON_VER:
	gpsd_log(&session->context->errout, LOG_DATA, "UBX_MON_VER\n");
	ubx_msg_mon_ver(session, buf, data_len);
	break;
    case UBX_MON_EXCEPT:
	gpsd_log(&session->context->errout, LOG_DATA, "UBX_MON_EXCEPT\n");
	break;
    case UBX_MON_MSGPP:
	gpsd_log(&session->context->errout, LOG_DATA, "UBX_MON_MSGPP\n");
	break;
    case UBX_MON_RXBUF:
	gpsd_log(&session->context->errout, LOG_DATA, "UBX_MON_RXBUF\n");
	break;
    case UBX_MON_TXBUF:
	gpsd_log(&session->context->errout, LOG_DATA, "UBX_MON_TXBUF\n");
	break;
    case UBX_MON_HW:
	gpsd_log(&session->context->errout, LOG_DATA, "UBX_MON_HW\n");
	break;
    case UBX_MON_USB:
	gpsd_log(&session->context->errout, LOG_DATA, "UBX_MON_USB\n");
	break;

    case UBX_INF_DEBUG:
	/* FALLTHROUGH */
    case UBX_INF_TEST:
	/* FALLTHROUGH */
    case UBX_INF_NOTICE:
	/* FALLTHROUGH */
    case UBX_INF_WARNING:
	/* FALLTHROUGH */
    case UBX_INF_ERROR:
	ubx_msg_inf(session, buf, data_len);
	break;

    case UBX_CFG_PRT:
	session->driver.ubx.port_id = (unsigned char)buf[UBX_MESSAGE_DATA_OFFSET + 0];
	gpsd_log(&session->context->errout, LOG_INF, "UBX_CFG_PRT: port %d\n",
		 session->driver.ubx.port_id);
	break;

    case UBX_TIM_TP:
	gpsd_log(&session->context->errout, LOG_DATA, "UBX_TIM_TP\n");
	break;
    case UBX_TIM_TM:
	gpsd_log(&session->context->errout, LOG_DATA, "UBX_TIM_TM\n");
	break;
    case UBX_TIM_TM2:
	gpsd_log(&session->context->errout, LOG_DATA, "UBX_TIM_TM2\n");
	break;
    case UBX_TIM_SVIN:
	gpsd_log(&session->context->errout, LOG_DATA, "UBX_TIM_SVIN\n");
	break;

    case UBX_ACK_NAK:
	gpsd_log(&session->context->errout, LOG_WARN,
		 "UBX_ACK_NAK, class: %02x, id: %02x\n",
		 buf[UBX_CLASS_OFFSET],
		 buf[UBX_TYPE_OFFSET]);
	break;
    case UBX_ACK_ACK:
	gpsd_log(&session->context->errout, LOG_DATA,
		 "UBX_ACK_ACK, class: %02x, id: %02x\n",
		 buf[UBX_CLASS_OFFSET],
		 buf[UBX_TYPE_OFFSET]);
	break;

    default:
	gpsd_log(&session->context->errout, LOG_WARN,
		 "UBX: unknown packet id 0x%04hx (length %zd)\n",
		 msgid, len);
    }
    return mask | ONLINE_SET;
}


static gps_mask_t parse_input(struct gps_device_t *session)
{
    if (session->lexer.type == UBX_PACKET) {
	return ubx_parse(session, session->lexer.outbuffer,
			 session->lexer.outbuflen);
    } else
	return generic_parse_input(session);
}

bool ubx_write(struct gps_device_t * session,
	       unsigned int msg_class, unsigned int msg_id,
	       unsigned char *msg, size_t data_len)
{
    unsigned char CK_A, CK_B;
    ssize_t count;
    size_t i;
    bool ok;

    session->msgbuf[0] = 0xb5;
    session->msgbuf[1] = 0x62;

    CK_A = CK_B = 0;
    session->msgbuf[2] = msg_class;
    session->msgbuf[3] = msg_id;
    session->msgbuf[4] = data_len & 0xff;
    session->msgbuf[5] = (data_len >> 8) & 0xff;

    assert(msg != NULL || data_len == 0);
    if (msg != NULL)
	(void)memcpy(&session->msgbuf[6], msg, data_len);

    /* calculate CRC */
    for (i = 2; i < 6; i++) {
	CK_A += session->msgbuf[i];
	CK_B += CK_A;
    }
    if (msg != NULL)
	for (i = 0; i < data_len; i++) {
	    CK_A += msg[i];
	    CK_B += CK_A;
	}

    session->msgbuf[6 + data_len] = CK_A;
    session->msgbuf[7 + data_len] = CK_B;
    session->msgbuflen = data_len + 8;


    gpsd_log(&session->context->errout, LOG_PROG,
	     "=> GPS: UBX class: %02x, id: %02x, len: %zd, crc: %02x%02x\n",
	     msg_class, msg_id, data_len,
	     CK_A, CK_B);
    count = gpsd_write(session, session->msgbuf, session->msgbuflen);
    ok = (count == (ssize_t) session->msgbuflen);
    return (ok);
}

#ifdef CONTROLSEND_ENABLE
static ssize_t ubx_control_send(struct gps_device_t *session, char *msg,
				size_t data_len)
/* not used by gpsd, it's for gpsctl and friends */
{
    return ubx_write(session, (unsigned int)msg[0], (unsigned int)msg[1],
		     (unsigned char *)msg + 2,
		     (size_t)(data_len - 2)) ? ((ssize_t) (data_len + 7)) : -1;
}
#endif /* CONTROLSEND_ENABLE */

static void ubx_init_query(struct gps_device_t *session)
{
    /* MON_VER: query for version information */
    (void)ubx_write(session, UBX_CLASS_MON, 0x04, NULL, 0);
}

static void ubx_event_hook(struct gps_device_t *session, event_t event)
{
    if (session->context->readonly)
	return;
    else if (event == event_identified) {
	unsigned char msg[32];

	gpsd_log(&session->context->errout, LOG_DATA, "UBX configure\n");

	msg[0] = 0x03;		/* SBAS mode enabled, accept testbed mode */
	msg[1] = 0x07;		/* SBAS usage: range, differential corrections and integrity */
	msg[2] = 0x03;		/* use the maximum search range: 3 channels */
	msg[3] = 0x00;		/* PRN numbers to search for all set to 0 => auto scan */
	msg[4] = 0x00;
	msg[5] = 0x00;
	msg[6] = 0x00;
	msg[7] = 0x00;
	(void)ubx_write(session, 0x06u, 0x16, msg, 8);

#ifdef RECONFIGURE_ENABLE
	/*
	 * Turn off NMEA output, turn on UBX on this port.
	 */
	if (session->mode == O_OPTIMIZE) {
	    ubx_mode(session, MODE_BINARY);
	} else {
	    ubx_mode(session, MODE_NMEA);
	}
#endif /* RECONFIGURE_ENABLE */
    } else if (event == event_deactivate) {
	unsigned char msg[4] = {
	    0x00, 0x00,		/* hotstart */
	    0x01,		/* controlled software reset */
	    0x00
	};			/* reserved */

	gpsd_log(&session->context->errout, LOG_DATA, "UBX revert\n");

	/* Reverting all in one fast and reliable reset */
	(void)ubx_write(session, 0x06, 0x04, msg, 4);	/* CFG-RST */
    }
}

#ifdef RECONFIGURE_ENABLE
static void ubx_cfg_prt(struct gps_device_t *session,
			speed_t speed, const char parity, const int stopbits,
			const int mode)
/* generate and send a configuration block */
{
    unsigned long usart_mode = 0;
    unsigned char buf[UBX_CFG_LEN];

    memset(buf, '\0', UBX_CFG_LEN);

    /*
     * When this is called from gpsd, the initial probe for UBX should
     * have picked up the device's port number from the CFG_PRT response.
     */
    if (session->driver.ubx.port_id != 0)
	buf[0] = session->driver.ubx.port_id;
    /*
     * This default can be hit if we haven't sent a CFG_PRT query yet,
     * which can happen in gpsmon because it doesn't autoprobe.
     *
     * What we'd like to do here is dispatch to USART1_ID or
     * USB_ID intelligently based on whether this is a USB or RS232
     * source.  Unfortunately the GR601-W screws that up by being
     * a USB device with port_id 1.  So we bite the bullet and
     * default to port 1.
     *
     * Without further logic, this means gpsmon wouldn't be able to
     * change the speed on the EVK 6H's USB port.  But! To pick off
     * the EVK 6H on Linux as a special case, we notice that its
     * USB device name is /dev/ACMx - it presents as a USB modem.
     *
     * This logic will fail on any USB u-blox device that presents
     * as an ordinary USB serial device (/dev/USB*) and actually
     * has port ID 3 the way it ought to.
     */
    else if (strstr(session->gpsdata.dev.path, "/ACM") != NULL)
	buf[0] = USB_ID;
    else
	buf[0] = USART1_ID;

    putle32(buf, 8, speed);

    /*
     * u-blox tech support explains the default contents of the mode
     * field as follows:
     *
     * D0 08 00 00     mode (LSB first)
     *
     * re-ordering bytes: 000008D0
     * dividing into fields: 000000000000000000 00 100 0 11 0 1 0000
     * nStopbits = 00 = 1
     * parity = 100 = none
     * charLen = 11 = 8-bit
     * reserved1 = 1
     *
     * The protocol reference further gives the following subfield values:
     * 01 = 1.5 stop bits (?)
     * 10 = 2 stopbits
     * 000 = even parity
     * 001 = odd parity
     * 10x = no parity
     * 10 = 7 bits
     *
     * Some UBX reference code amplifies this with:
     *
     *   prtcfg.mode = (1<<4) |	// compatibility with ANTARIS 4
     *                 (1<<7) |	// charLen = 11 = 8 bit
     *                 (1<<6) |	// charLen = 11 = 8 bit
     *                 (1<<11);	// parity = 10x = none
     */
    usart_mode |= (1<<4);	/* reserved1 Antaris 4 compatibility bit */
    usart_mode |= (1<<7);	/* high bit of charLen */

    switch (parity) {
    case (int)'E':
    case 2:
	usart_mode |= (1<<7);		/* 7E */
	break;
    case (int)'O':
    case 1:
	usart_mode |= (1<<9) | (1<<7);	/* 7O */
	break;
    case (int)'N':
    case 0:
    default:
	usart_mode |= (1<<11) | (3<<6);	/* 8N */
	break;
    }

    if (stopbits == 2)
	usart_mode |= (1<<13);

    putle32(buf, 4, usart_mode);

    /* enable all input protocols by default */
    buf[12] = NMEA_PROTOCOL_MASK | UBX_PROTOCOL_MASK | RTCM_PROTOCOL_MASK;

    /* selectively enable output protocols */
    if (mode == MODE_NMEA) {
	/*
	 * We have to club the GR601-W over the head to make it stop emitting
	 * UBX after we've told it to start. Turning off the UBX protocol
	 * mask, by itself, seems to be ineffective.
	 */

	unsigned char msg[3];

	msg[0] = 0x01;		/* class */
	msg[1] = 0x04;		/* msg id  = UBX_NAV_DOP */
	msg[2] = 0x00;		/* rate */
	(void)ubx_write(session, 0x06u, 0x01, msg, 3);

        /* UBX-NAV-SOL deprecated, use UBX-NAV-PVT instead */
	msg[0] = 0x01;		/* class */
	msg[1] = 0x06;		/* msg id  = NAV-SOL */
	msg[2] = 0x00;		/* rate */
	(void)ubx_write(session, 0x06u, 0x01, msg, 3);
	msg[0] = 0x01;		/* class */
	msg[1] = 0x20;		/* msg id  = UBX_NAV_TIMEGPS */
	msg[2] = 0x00;		/* rate */
	(void)ubx_write(session, 0x06u, 0x01, msg, 3);
	msg[0] = 0x01;		/* class */
	msg[1] = 0x30;		/* msg id  = NAV-SVINFO */
	msg[2] = 0x00;		/* rate */
	(void)ubx_write(session, 0x06u, 0x01, msg, 3);
	msg[0] = 0x01;		/* class */
	msg[1] = 0x32;		/* msg id  = NAV-SBAS */
	msg[2] = 0x00;		/* rate */
	(void)ubx_write(session, 0x06u, 0x01, msg, 3);

	/* try to improve the sentence mix. in particular by enabling ZDA */
	msg[0] = 0xf0;		/* class */
	msg[1] = 0x09;		/* msg id  = GBS */
	msg[2] = 0x01;		/* rate */
	(void)ubx_write(session, 0x06u, 0x01, msg, 3);
	msg[0] = 0xf0;		/* class */
	msg[1] = 0x00;		/* msg id  = GGA */
	msg[2] = 0x01;		/* rate */
	(void)ubx_write(session, 0x06u, 0x01, msg, 3);
	msg[0] = 0xf0;		/* class */
	msg[1] = 0x02;		/* msg id  = GSA */
	msg[2] = 0x01;		/* rate */
	(void)ubx_write(session, 0x06u, 0x01, msg, 3);
	msg[0] = 0xf0;		/* class */
	msg[1] = 0x07;		/* msg id  = GST */
	msg[2] = 0x01;		/* rate */
	(void)ubx_write(session, 0x06u, 0x01, msg, 3);
	msg[0] = 0xf0;		/* class */
	msg[1] = 0x03;		/* msg id  = GSV */
	msg[2] = 0x01;		/* rate */
	(void)ubx_write(session, 0x06u, 0x01, msg, 3);
	msg[0] = 0xf0;		/* class */
	msg[1] = 0x04;		/* msg id  = RMC */
	msg[2] = 0x01;		/* rate */
	(void)ubx_write(session, 0x06u, 0x01, msg, 3);
	msg[0] = 0xf0;		/* class */
	msg[1] = 0x05;		/* msg id  = VTG */
	msg[2] = 0x01;		/* rate */
	(void)ubx_write(session, 0x06u, 0x01, msg, 3);
	msg[0] = 0xf0;		/* class */
	msg[1] = 0x08;		/* msg id  = ZDA */
	msg[2] = 0x01;		/* rate */
	(void)ubx_write(session, 0x06u, 0x01, msg, 3);

	buf[outProtoMask] &= ~UBX_PROTOCOL_MASK;
	buf[outProtoMask] |= NMEA_PROTOCOL_MASK;
    } else { /* MODE_BINARY */
	/*
	 * Just enabling the UBX protocol for output is not enough to
	 * actually get UBX output; the sentence mix is initially empty.
	 * Fix that...
	 */

        /* FIXME: possibly sending too many messages without waiting
         * for u-blox ACK, over running its input buffer.
         *
         * for example, the UBX_MON_VER fails here, but works in other
         * contexts
         */
	unsigned char msg[3] = {0, 0, 0};
        /* request SW and HW Versions */
	(void)ubx_write(session, UBX_CLASS_MON, 0x04, msg, 0);

	msg[0] = 0x01;		/* class */
	msg[1] = 0x04;		/* msg id  = UBX_NAV_DOP */
	msg[2] = 0x01;		/* rate */
	(void)ubx_write(session, 0x06u, 0x01, msg, 3);

        /* UBX-NAV-SOL deprecated, use UBX-NAV-PVT instead */
	msg[0] = 0x01;		/* class */
	msg[1] = 0x06;		/* msg id  = NAV-SOL */
	msg[2] = 0x01;		/* rate */
	(void)ubx_write(session, 0x06u, 0x01, msg, 3);

#ifdef __UNUSED__
        /* leave here for testing.  No need to enable until gpsd
         * can decode UBX-MON-VER */
	msg[0] = 0x01;		/* class */
	msg[1] = 0x07;		/* msg id  = NAV-PVT */
	msg[2] = 0x01;		/* rate */
	(void)ubx_write(session, 0x06u, 0x01, msg, 3);
#endif /* __UNUSED __ */


	msg[0] = 0x01;		/* class */
	msg[1] = 0x20;		/* msg id  = UBX_NAV_TIMEGPS */
	msg[2] = 0x01;		/* rate */
	(void)ubx_write(session, 0x06u, 0x01, msg, 3);
	msg[0] = 0x01;		/* class */
	msg[1] = 0x30;		/* msg id  = NAV-SVINFO */
	msg[2] = 0x0a;		/* rate */
	(void)ubx_write(session, 0x06u, 0x01, msg, 3);
	msg[0] = 0x01;		/* class */
	msg[1] = 0x32;		/* msg id  = NAV-SBAS */
	msg[2] = 0x0a;		/* rate */
	(void)ubx_write(session, 0x06u, 0x01, msg, 3);


#ifdef __UNUSED__
	/*
	 * In theory this should turn off NMEA reporting even if
	 * clearing the NMEA protocol mask does not.  In practice it
	 * doesn't work on the GR601-W.  If it did, we could get rid
	 * of the crocky code that detects unsuppressed NMEA and
	 * suppresses UBX.
	 */
	msg[0] = 0xf0;		/* class */
	msg[1] = 0x09;		/* msg id  = GBS */
	msg[2] = 0x00;		/* rate */
	(void)ubx_write(session, 0x06u, 0x01, msg, 3);
	msg[0] = 0xf0;		/* class */
	msg[1] = 0x00;		/* msg id  = GGA */
	msg[2] = 0x00;		/* rate */
	(void)ubx_write(session, 0x06u, 0x01, msg, 3);
	msg[0] = 0xf0;		/* class */
	msg[1] = 0x02;		/* msg id  = GSA */
	msg[2] = 0x00;		/* rate */
	(void)ubx_write(session, 0x06u, 0x01, msg, 3);
	msg[0] = 0xf0;		/* class */
	msg[1] = 0x07;		/* msg id  = GST */
	msg[2] = 0x00;		/* rate */
	(void)ubx_write(session, 0x06u, 0x01, msg, 3);
	msg[0] = 0xf0;		/* class */
	msg[1] = 0x03;		/* msg id  = GSV */
	msg[2] = 0x00;		/* rate */
	(void)ubx_write(session, 0x06u, 0x01, msg, 3);
	msg[0] = 0xf0;		/* class */
	msg[1] = 0x04;		/* msg id  = RMC */
	msg[2] = 0x00;		/* rate */
	(void)ubx_write(session, 0x06u, 0x01, msg, 3);
	msg[0] = 0xf0;		/* class */
	msg[1] = 0x05;		/* msg id  = VTG */
	msg[2] = 0x00;		/* rate */
	(void)ubx_write(session, 0x06u, 0x01, msg, 3);
	msg[0] = 0xf0;		/* class */
	msg[1] = 0x08;		/* msg id  = ZDA */
	msg[2] = 0x00;		/* rate */
	(void)ubx_write(session, 0x06u, 0x01, msg, 3);
#endif /* __UNUSED __ */

	buf[outProtoMask] &= ~NMEA_PROTOCOL_MASK;
	buf[outProtoMask] |= UBX_PROTOCOL_MASK;
    }

    (void)ubx_write(session, 0x06u, 0x00, buf, sizeof(buf));
}

static void ubx_mode(struct gps_device_t *session, int mode)
{
    ubx_cfg_prt(session,
		gpsd_get_speed(session),
		gpsd_get_parity(session),
		gpsd_get_stopbits(session),
		mode);
}

static bool ubx_speed(struct gps_device_t *session,
		      speed_t speed, char parity, int stopbits)
{
    ubx_cfg_prt(session,
		speed,
		parity,
		stopbits,
		(session->lexer.type == UBX_PACKET) ? MODE_BINARY : MODE_NMEA);
    return true;
}

static bool ubx_rate(struct gps_device_t *session, double cycletime)
/* change the sample rate of the GPS */
{
    unsigned short s;
    unsigned char msg[6] = {
	0x00, 0x00,		/* U2: Measurement rate (ms) */
	0x00, 0x01,		/* U2: Navigation rate (cycles) */
	0x00, 0x00,		/* U2: Alignment to reference time: 0 = UTC, !0 = GPS */
    };

    /* clamp to cycle times that i know work on my receiver */
    if (cycletime > 1000.0)
	cycletime = 1000.0;
    if (cycletime < 200.0)
	cycletime = 200.0;

    gpsd_log(&session->context->errout, LOG_DATA,
	     "UBX rate change, report every %f secs\n", cycletime);
    s = (unsigned short)cycletime;
    msg[0] = (unsigned char)(s >> 8);
    msg[1] = (unsigned char)(s & 0xff);

    return ubx_write(session, 0x06, 0x08, msg, 6);	/* CFG-RATE */
}
#endif /* RECONFIGURE_ENABLE */

/* This is everything we export */
/* *INDENT-OFF* */
const struct gps_type_t driver_ubx = {
    .type_name        = "u-blox",    /* Full name of type */
    .packet_type      = UBX_PACKET,	/* associated lexer packet type */
    .flags	      = DRIVER_STICKY,	/* remember this */
    .trigger          = NULL,
    .channels         = 50,             /* Number of satellite channels supported by the device */
    .probe_detect     = NULL,           /* Startup-time device detector */
    .get_packet       = generic_get,    /* Packet getter (using default routine) */
    .parse_packet     = parse_input,    /* Parse message packets */
    .rtcm_writer      = gpsd_write,      /* RTCM handler (using default routine) */
    .init_query       = ubx_init_query,	/* non-perturbing initial query */
    .event_hook       = ubx_event_hook,	/* Fire on various lifetime events */
#ifdef RECONFIGURE_ENABLE
    .speed_switcher   = ubx_speed,      /* Speed (baudrate) switch */
    .mode_switcher    = ubx_mode,       /* Mode switcher */
    .rate_switcher    = ubx_rate,       /* Message delivery rate switcher */
    .min_cycle        = 0.25,           /* Maximum 4Hz sample rate */
#endif /* RECONFIGURE_ENABLE */
#ifdef CONTROLSEND_ENABLE
    .control_send     = ubx_control_send,/* how to send a control string */
#endif /* CONTROLSEND_ENABLE */
#ifdef TIMEHINT_ENABLE
    .time_offset     = NULL,		/* no method for NTP fudge factor */
#endif /* TIMEHINT_ENABLE */
};
/* *INDENT-ON* */
#endif /* defined(UBLOX_ENABLE) && defined(BINARY_ENABLE) */
