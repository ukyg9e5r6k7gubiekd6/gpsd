/*
 * Driver for the iTalk binary protocol used by FasTrax
 *
 * Week counters are not limited to 10 bits. It's unknown what
 * the firmware is doing to disambiguate them, if anything; it might just
 * be adding a fixed offset based on a hidden epoch value, in which case
 * unhappy things will occur on the next rollover.
 *
 * This file is Copyright (c) 2010-2018 by the GPSD project
 * SPDX-License-Identifier: BSD-2-clause
 *
 */

#include "gpsd_config.h"  /* must be before all includes */

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

#include "gpsd.h"
#if defined(ITRAX_ENABLE) && defined(BINARY_ENABLE)

#include "bits.h"
#include "driver_italk.h"
#include "timespec.h"

static gps_mask_t italk_parse(struct gps_device_t *, unsigned char *, size_t);
static gps_mask_t decode_itk_navfix(struct gps_device_t *, unsigned char *,
                                    size_t);
static gps_mask_t decode_itk_prnstatus(struct gps_device_t *, unsigned char *,
                                       size_t);
static gps_mask_t decode_itk_utcionomodel(struct gps_device_t *,
                                          unsigned char *, size_t);
static gps_mask_t decode_itk_subframe(struct gps_device_t *, unsigned char *,
                                      size_t);

/* NAVIGATION_MSG, message id 7 */
static gps_mask_t decode_itk_navfix(struct gps_device_t *session,
                                    unsigned char *buf, size_t len)
{
    unsigned short flags, pflags;
    timespec_t ts_tow;
    uint32_t tow;	     /* Time of week [ms] */
    char ts_buf[TIMESPEC_LEN];

    gps_mask_t mask = 0;
    if (len != 296) {
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "ITALK: bad NAV_FIX (len %zu, should be 296)\n",
                 len);
        return -1;
    }

    flags = (unsigned short) getleu16(buf, 7 + 4);
    //cflags = (unsigned short) getleu16(buf, 7 + 6);
    pflags = (unsigned short) getleu16(buf, 7 + 8);

    session->gpsdata.status = STATUS_NO_FIX;
    session->newdata.mode = MODE_NO_FIX;
    mask = ONLINE_SET | MODE_SET | STATUS_SET | CLEAR_IS;

    /* just bail out if this fix is not marked valid */
    if (0 != (pflags & FIX_FLAG_MASK_INVALID)
        || 0 == (flags & FIXINFO_FLAG_VALID))
        return mask;

    tow = getleu32(buf, 7 + 84);   /* tow in ms */
    MSTOTS(&ts_tow, tow);
    session->newdata.time = gpsd_gpstime_resolv(session,
        (unsigned short) getles16(buf, 7 + 82), ts_tow);
    mask |= TIME_SET | NTPTIME_IS;

    session->newdata.ecef.x = (double)(getles32(buf, 7 + 96) / 100.0);
    session->newdata.ecef.y = (double)(getles32(buf, 7 + 100) / 100.0);
    session->newdata.ecef.z = (double)(getles32(buf, 7 + 104) / 100.0);
    session->newdata.ecef.vx = (double)(getles32(buf, 7 + 186) / 1000.0);
    session->newdata.ecef.vy = (double)(getles32(buf, 7 + 190) / 1000.0);
    session->newdata.ecef.vz = (double)(getles32(buf, 7 + 194) / 1000.0);
    mask |= ECEF_SET | VECEF_SET;
    /* this eph does not look right, badly documented.
     * let gpsd_error_model() handle it
     * session->newdata.eph = (double)(getles32(buf, 7 + 252) / 100.0);
     */
    session->newdata.eps = (double)(getles32(buf, 7 + 254) / 100.0);
    /* compute epx/epy in gpsd_error_model(), not here */
    mask |= HERR_SET;

#define MAX(a,b) (((a) > (b)) ? (a) : (b))
    session->gpsdata.satellites_used =
        (int)MAX(getleu16(buf, 7 + 12), getleu16(buf, 7 + 14));
    mask |= USED_IS;

    if (flags & FIX_CONV_DOP_VALID) {
        session->gpsdata.dop.hdop = (double)(getleu16(buf, 7 + 56) / 100.0);
        session->gpsdata.dop.gdop = (double)(getleu16(buf, 7 + 58) / 100.0);
        session->gpsdata.dop.pdop = (double)(getleu16(buf, 7 + 60) / 100.0);
        session->gpsdata.dop.vdop = (double)(getleu16(buf, 7 + 62) / 100.0);
        session->gpsdata.dop.tdop = (double)(getleu16(buf, 7 + 64) / 100.0);
        mask |= DOP_SET;
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

    GPSD_LOG(LOG_DATA, &session->context->errout,
             "NAV_FIX: time=%s, ecef x:%.2f y:%.2f z:%.2f altHAE=%.2f "
             "speed=%.2f track=%.2f climb=%.2f mode=%d status=%d gdop=%.2f "
             "pdop=%.2f hdop=%.2f vdop=%.2f tdop=%.2f\n",
             timespec_str(&session->newdata.time, ts_buf, sizeof(ts_buf)),
             session->newdata.ecef.x,
             session->newdata.ecef.y, session->newdata.ecef.z,
             session->newdata.altHAE, session->newdata.speed,
             session->newdata.track, session->newdata.climb,
             session->newdata.mode, session->gpsdata.status,
             session->gpsdata.dop.gdop, session->gpsdata.dop.pdop,
             session->gpsdata.dop.hdop, session->gpsdata.dop.vdop,
             session->gpsdata.dop.tdop);
    return mask;
}

static gps_mask_t decode_itk_prnstatus(struct gps_device_t *session,
                                       unsigned char *buf, size_t len)
{
    gps_mask_t mask;

    if (len < 62) {
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "ITALK: runt PRN_STATUS (len=%zu)\n", len);
        mask = 0;
    } else {
        unsigned int i, nsv, nchan, st;
        uint32_t msec = getleu32(buf, 7 + 6);
        timespec_t ts_tow;
        char ts_buf[TIMESPEC_LEN];

        MSTOTS(&ts_tow, msec);

        session->gpsdata.skyview_time = gpsd_gpstime_resolv(session,
            (unsigned short)getleu16(buf, 7 + 4), ts_tow);
        gpsd_zero_satellites(&session->gpsdata);
        nchan = (unsigned int)getleu16(buf, 7 + 50);
        if (nchan > MAX_NR_VISIBLE_PRNS)
            nchan = MAX_NR_VISIBLE_PRNS;
        for (i = st = nsv = 0; i < nchan; i++) {
            unsigned int off = 7 + 52 + 10 * i;
            unsigned short flags;
            bool used;

            flags = (unsigned short) getleu16(buf, off);
            used = (bool)(flags & PRN_FLAG_USE_IN_NAV);
            session->gpsdata.skyview[st].PRN = (short)(getleu16(buf, off + 4) & 0xff);
            session->gpsdata.skyview[st].elevation =
                (double)(getles16(buf, off + 6) & 0xff);
            session->gpsdata.skyview[st].azimuth =
                (double)(getles16(buf, off + 8) & 0xff);
            session->gpsdata.skyview[st].ss =
                (double)(getleu16(buf, off + 2) & 0xff);
            session->gpsdata.skyview[st].used = used;
            if (session->gpsdata.skyview[st].PRN > 0) {
                st++;
                if (used)
                    nsv++;
            }
        }
        session->gpsdata.satellites_visible = (int)st;
        session->gpsdata.satellites_used = (int)nsv;
        mask = USED_IS | SATELLITE_SET;;

        GPSD_LOG(LOG_DATA, &session->context->errout,
                 "PRN_STATUS: time=%s visible=%d used=%d "
                 "mask={USED|SATELLITE}\n",
                 timespec_str(&session->newdata.time, ts_buf, sizeof(ts_buf)),
                 session->gpsdata.satellites_visible,
                 session->gpsdata.satellites_used);
    }

    return mask;
}

static gps_mask_t decode_itk_utcionomodel(struct gps_device_t *session,
                                          unsigned char *buf, size_t len)
{
    int leap;
    unsigned short flags;
    timespec_t ts_tow;
    uint32_t tow;	     /* Time of week [ms] */
    char ts_buf[TIMESPEC_LEN];

    if (len != 64) {
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "ITALK: bad UTC_IONO_MODEL (len %zu, should be 64)\n",
                 len);
        return 0;
    }

    flags = (unsigned short) getleu16(buf, 7);
    if (0 == (flags & UTC_IONO_MODEL_UTCVALID))
        return 0;

    leap = (int)getleu16(buf, 7 + 24);
    if (session->context->leap_seconds < leap)
        session->context->leap_seconds = leap;

    tow = getleu32(buf, 7 + 38);    /* in ms */
    MSTOTS(&ts_tow, tow);
    session->newdata.time = gpsd_gpstime_resolv(session,
        (unsigned short) getleu16(buf, 7 + 36), ts_tow);
    GPSD_LOG(LOG_DATA, &session->context->errout,
             "UTC_IONO_MODEL: time=%s mask={TIME}\n",
             timespec_str(&session->newdata.time, ts_buf, sizeof(ts_buf)));
    return TIME_SET | NTPTIME_IS;
}

static gps_mask_t decode_itk_subframe(struct gps_device_t *session,
                                      unsigned char *buf, size_t len)
{
    unsigned short flags, prn, sf;
    unsigned int i;
    uint32_t words[10];

    if (len != 64) {
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "ITALK: bad SUBFRAME (len %zu, should be 64)\n", len);
        return 0;
    }

    flags = (unsigned short) getleu16(buf, 7 + 4);
    prn = (unsigned short) getleu16(buf, 7 + 6);
    sf = (unsigned short) getleu16(buf, 7 + 8);
    GPSD_LOG(LOG_PROG, &session->context->errout,
             "iTalk 50B SUBFRAME prn %u sf %u - decode %s %s\n",
             prn, sf,
             (flags & SUBFRAME_WORD_FLAG_MASK) ? "error" : "ok",
             (flags & SUBFRAME_GPS_PREAMBLE_INVERTED) ? "(inverted)" : "");
    if (flags & SUBFRAME_WORD_FLAG_MASK)
        return 0;       // don't try decode an erroneous packet

    /*
     * Timo says "SUBRAME message contains decoded navigation message subframe
     * words with parity checking done but parity bits still present."
     */
    for (i = 0; i < 10; i++)
        words[i] = (uint32_t)(getleu32(buf, 7 + 14 + 4 * i) >> 6) & 0xffffff;

    return gpsd_interpret_subframe(session, prn, words);
}

static gps_mask_t decode_itk_pseudo(struct gps_device_t *session,
                                      unsigned char *buf, size_t len)
{
    unsigned short flags, n, i;
    unsigned int tow;             /* time of week, in ms */
    timespec_t ts_tow;

    n = (unsigned short) getleu16(buf, 7 + 4);
    if ((n < 1) || (n > MAXCHANNELS)){
        GPSD_LOG(LOG_INF, &session->context->errout,
                 "ITALK: bad PSEUDO channel count\n");
        return 0;
    }

    if (len != (size_t)((n+1)*36)) {
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "ITALK: bad PSEUDO len %zu\n", len);
    }

    GPSD_LOG(LOG_PROG, &session->context->errout, "iTalk PSEUDO [%u]\n", n);
    flags = (unsigned short)getleu16(buf, 7 + 6);
    if ((flags & 0x3) != 0x3)
        return 0; // bail if measurement time not valid.

    tow = (unsigned int)getleu32(buf, 7 + 38);
    MSTOTS(&ts_tow, tow);
    session->newdata.time = gpsd_gpstime_resolv(session,
        (unsigned short int)getleu16((char *)buf, 7 + 8), ts_tow);

    session->gpsdata.raw.mtime = session->newdata.time;

    /* this is so we can tell which never got set */
    for (i = 0; i < MAXCHANNELS; i++)
        session->gpsdata.raw.meas[i].svid = 0;
    for (i = 0; i < n; i++){
        session->gpsdata.skyview[i].PRN =
            getleu16(buf, 7 + 26 + (i*36)) & 0xff;
        session->gpsdata.skyview[i].ss =
            getleu16(buf, 7 + 26 + (i*36 + 2)) & 0x3f;
        session->gpsdata.raw.meas[i].satstat =
            getleu32(buf, 7 + 26 + (i*36 + 4));
        session->gpsdata.raw.meas[i].pseudorange =
            getled64((char *)buf, 7 + 26 + (i*36 + 8));
        session->gpsdata.raw.meas[i].doppler =
            getled64((char *)buf, 7 + 26 + (i*36 + 16));
        session->gpsdata.raw.meas[i].carrierphase =
            getleu16(buf, 7 + 26 + (i*36 + 28));

        session->gpsdata.raw.meas[i].codephase = NAN;
        session->gpsdata.raw.meas[i].deltarange = NAN;
    }
    /* return RAW_IS; The above decode does not give reasonable results */
    return 0;         /* do not report valid until decode is fixed */
}

static gps_mask_t italk_parse(struct gps_device_t *session,
                              unsigned char *buf, size_t len)
{
    unsigned int type;
    gps_mask_t mask = 0;

    if (len == 0)
        return 0;

    type = (unsigned int) getub(buf, 4);
    /* we may need to dump the raw packet */
    GPSD_LOG(LOG_RAW, &session->context->errout,
             "raw italk packet type 0x%02x\n", type);

    session->cycle_end_reliable = true;

    switch (type) {
    case ITALK_NAV_FIX:
        GPSD_LOG(LOG_DATA, &session->context->errout,
                 "iTalk NAV_FIX len %zu\n", len);
        mask = decode_itk_navfix(session, buf, len) | (CLEAR_IS | REPORT_IS);
        break;
    case ITALK_PRN_STATUS:
        GPSD_LOG(LOG_DATA, &session->context->errout,
                 "iTalk PRN_STATUS len %zu\n", len);
        mask = decode_itk_prnstatus(session, buf, len);
        break;
    case ITALK_UTC_IONO_MODEL:
        GPSD_LOG(LOG_DATA, &session->context->errout,
                 "iTalk UTC_IONO_MODEL len %zu\n", len);
        mask = decode_itk_utcionomodel(session, buf, len);
        break;

    case ITALK_ACQ_DATA:
        GPSD_LOG(LOG_DATA, &session->context->errout,
                 "iTalk ACQ_DATA len %zu\n", len);
        break;
    case ITALK_TRACK:
        GPSD_LOG(LOG_DATA, &session->context->errout,
                 "iTalk TRACK len %zu\n", len);
        break;
    case ITALK_PSEUDO:
        GPSD_LOG(LOG_DATA, &session->context->errout,
                 "iTalk PSEUDO len %zu\n", len);
        mask = decode_itk_pseudo(session, buf, len);
        break;
    case ITALK_RAW_ALMANAC:
        GPSD_LOG(LOG_DATA, &session->context->errout,
                 "iTalk RAW_ALMANAC len %zu\n", len);
        break;
    case ITALK_RAW_EPHEMERIS:
        GPSD_LOG(LOG_DATA, &session->context->errout,
                 "iTalk RAW_EPHEMERIS len %zu\n", len);
        break;
    case ITALK_SUBFRAME:
        mask = decode_itk_subframe(session, buf, len);
        break;
    case ITALK_BIT_STREAM:
        GPSD_LOG(LOG_DATA, &session->context->errout,
                 "iTalk BIT_STREAM len %zu\n", len);
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
        GPSD_LOG(LOG_DATA, &session->context->errout,
                 "iTalk not processing packet: id 0x%02x length %zu\n",
                 type, len);
        break;
    default:
        GPSD_LOG(LOG_DATA, &session->context->errout,
                 "iTalk unknown packet: id 0x%02x length %zu\n",
                 type, len);
    }

    return mask | ONLINE_SET;
}


static gps_mask_t italk_parse_input(struct gps_device_t *session)
{
    if (session->lexer.type == ITALK_PACKET) {
        return italk_parse(session, session->lexer.outbuffer,
                           session->lexer.outbuflen);;
#ifdef NMEA0183_ENABLE
    } else if (session->lexer.type == NMEA_PACKET) {
        return nmea_parse((char *)session->lexer.outbuffer, session);
#endif /* NMEA0183_ENABLE */
    } else
        return 0;
}

#ifdef __UNUSED__
static void italk_ping(struct gps_device_t *session)
/* send a "ping". it may help us detect an itrax more quickly */
{
    char *ping = "<?>";
    (void)gpsd_write(session, ping, 3);
}
#endif /* __UNUSED__ */

/* *INDENT-OFF* */
const struct gps_type_t driver_italk =
{
    .type_name      = "iTalk",          /* full name of type */
    .packet_type    = ITALK_PACKET,     /* associated lexer packet type */
    .flags          = DRIVER_STICKY,    /* no rollover or other flags */
    .trigger        = NULL,             /* recognize the type */
    .channels       = 12,               /* consumer-grade GPS */
    .probe_detect   = NULL,             /* how to detect at startup time */
    .get_packet     = generic_get,      /* use generic packet grabber */
    .parse_packet   = italk_parse_input,/* parse message packets */
    .rtcm_writer    = gpsd_write,       /* send RTCM data straight */
    .init_query     = NULL,             /* non-perturbing initial query */
    .event_hook     = NULL,             /* lifetime event handler */
#ifdef RECONFIGURE_ENABLE
    .speed_switcher = NULL,             /* no speed switcher */
    .mode_switcher  = NULL,             /* no mode switcher */
    .rate_switcher  = NULL,             /* no sample-rate switcher */
    .min_cycle.tv_sec  = 1,		/* not relevant, no rate switch */
    .min_cycle.tv_nsec = 0,		/* not relevant, no rate switch */
#endif /* RECONFIGURE_ENABLE */
#ifdef CONTROLSEND_ENABLE
    .control_send   = NULL,             /* no control string sender */
#endif /* CONTROLSEND_ENABLE */
    .time_offset     = NULL,            /* no method for NTP fudge factor */
};
/* *INDENT-ON* */
#endif /* defined(ITRAX_ENABLE) && defined(BINARY_ENABLE) */
