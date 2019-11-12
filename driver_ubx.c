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
 * This file is Copyright (c) 2010-2019 by the GPSD project
 * SPDX-License-Identifier: BSD-2-clause
 *
 */

#include "gpsd_config.h"  /* must be before all includes */

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


#include "gpsd.h"
#if defined(UBLOX_ENABLE) && defined(BINARY_ENABLE)
#include "driver_ubx.h"

#include "bits.h"
#include "timespec.h"

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
#define UBX_PREFIX_LEN          6
#define UBX_CLASS_OFFSET        2
#define UBX_TYPE_OFFSET         3

/* because we hates magic numbers forever */
#define USART1_ID               1
#define USART2_ID               2
#define USB_ID                  3
#define UBX_PROTOCOL_MASK       0x01
#define NMEA_PROTOCOL_MASK      0x02
#define RTCM_PROTOCOL_MASK      0x04
#define UBX_CFG_LEN             20
#define outProtoMask            14

static gps_mask_t ubx_parse(struct gps_device_t *session, unsigned char *buf,
                            size_t len);
static gps_mask_t ubx_msg_nav_eoe(struct gps_device_t *session,
                                  unsigned char *buf, size_t data_len);
static gps_mask_t ubx_msg_nav_dop(struct gps_device_t *session,
                                  unsigned char *buf, size_t data_len);
static void ubx_msg_inf(struct gps_device_t *session, unsigned char *buf,
                        size_t data_len);
static gps_mask_t ubx_msg_nav_posecef(struct gps_device_t *session,
                                      unsigned char *buf, size_t data_len);
static gps_mask_t ubx_msg_nav_pvt(struct gps_device_t *session,
                                  unsigned char *buf, size_t data_len);
static void ubx_msg_mon_ver(struct gps_device_t *session,
                                      unsigned char *buf, size_t data_len);
static gps_mask_t ubx_msg_nav_sat(struct gps_device_t *session,
                                  unsigned char *buf, size_t data_len);
static gps_mask_t ubx_msg_nav_sol(struct gps_device_t *session,
                                  unsigned char *buf, size_t data_len);
static gps_mask_t ubx_msg_nav_svinfo(struct gps_device_t *session,
                                     unsigned char *buf, size_t data_len);
static gps_mask_t ubx_msg_nav_timegps(struct gps_device_t *session,
                                      unsigned char *buf, size_t data_len);
static gps_mask_t ubx_msg_nav_velecef(struct gps_device_t *session,
                                      unsigned char *buf, size_t data_len);
static void ubx_msg_sbas(struct gps_device_t *session, unsigned char *buf,
                         size_t data_len);
static gps_mask_t ubx_msg_tim_tp(struct gps_device_t *session,
                                 unsigned char *buf, size_t data_len);
#ifdef RECONFIGURE_ENABLE
static void ubx_mode(struct gps_device_t *session, int mode);
#endif /* RECONFIGURE_ENABLE */

/* make up an NMEA 4.0 (extended) PRN based on gnssId:svId,
 * using Appendix A from * u-blox ZED-F9P Interface Description
 *
 * Return PRN, or zero for error
 */
static short ubx2_to_prn(int gnssId, int svId)
{
    short nmea_PRN;

    if (1 > svId) {
        /* skip 0 svId */
        return 0;
    }

    switch (gnssId) {
    case 0:
        /* GPS, 1-32 maps to 1-32 */
        if (32 < svId) {
            /* skip bad svId */
            return 0;
        }
        nmea_PRN = svId;
        break;
    case 1:
        /* SBAS, 120..151, 152..158 maps to 33..64, 152..158 */
        if (120 > svId) {
            /* Huh? */
            return 0;
        } else if (151 >= svId) {
            nmea_PRN = svId - 87;
        } else if (158 >= svId) {
            nmea_PRN = svId;
        } else {
            /* Huh? */
            return 0;
        }
        break;
    case 2:
        /* Galileo, 1..36 ->  301-336 */
        /* Galileo, 211..246 ->  301-336 */
        if (36 >= svId) {
            nmea_PRN = svId + 300;
        } else if (211 > svId) {
            /* skip bad svId */
            return 0;
        } else if (246 >= svId) {
            nmea_PRN = svId + 90;
        } else {
            /* skip bad svId */
            return 0;
        }
        break;
    case 3:
        /* BeiDou, 1..37 -> to 401-437 */
        /* BeiDou, 159..163,33..64 -> to 401-437 */
        if (37 >= svId) {
            nmea_PRN = svId + 400;
        } else {
            /* skip bad svId */
            return 0;
        }
        break;
    case 4:
        /* IMES, 1-10 -> to 173-182, per u-blox 8/NMEA 4.0 extended */
        if (10 < svId) {
            /* skip bad svId */
            return 0;
        }
        nmea_PRN = svId + 172;
        break;
    case 5:
        /* QZSS, 1-5 maps to 193-197 */
        /* ZED-F9T also see 198 and 199 */
        if (7 < svId) {
            /* skip bad svId */
            return 0;
        }
        nmea_PRN = svId + 192;
        break;
    case 6:
        /* GLONASS, 1-32 maps to 65-96 */
        if (32 < svId) {
            /* skip bad svId */
            /* 255 == tracked, but unidentified, skip */
            return 0;
        }
        nmea_PRN = svId + 64;
        break;
    default:
        /* Huh? */
        return 0;
    }

    return nmea_PRN;
}

/* Convert a ubx PRN to an NMEA 4.0 (extended) PRN and ubx gnssid, svid
 *
 * return 0 on fail
 */
static short ubx_to_prn(int ubx_PRN, unsigned char *gnssId,
                        unsigned char *svId)
{
    *gnssId = 0;
    *svId = 0;

    if (1 > ubx_PRN) {
        /* skip 0 PRN */
        return 0;
    } else if (32 >= ubx_PRN) {
        /* GPS 1..32 -> 1..32 */
        *gnssId = 0;
        *svId = ubx_PRN;
    } else if (64 >= ubx_PRN) {
        /* BeiDou, 159..163,33..64 -> 1..5,6..37 */
        *gnssId = 3;
        *svId = ubx_PRN - 27;
    } else if (96 >= ubx_PRN) {
        /* GLONASS 65..96 -> 1..32 */
        *gnssId = 6;
        *svId = ubx_PRN - 64;
    } else if (120 > ubx_PRN) {
        /* Huh? */
        return 0;
    } else if (158 >= ubx_PRN) {
        /* SBAS 120..158 -> 120..158 */
        *gnssId = 1;
        *svId = ubx_PRN;
    } else if (163 >= ubx_PRN) {
        /* BeiDou, 159..163 -> 1..5 */
        *gnssId = 3;
        *svId = ubx_PRN - 158;
    } else if (173 > ubx_PRN) {
        /* Huh? */
        return 0;
    } else if (182 >= ubx_PRN) {
        /* IMES 173..182 -> 1..5, in u-blox 8, bot u-blox 9 */
        *gnssId = 4;
        *svId = ubx_PRN - 172;
    } else if (193 > ubx_PRN) {
        /* Huh? */
        return 0;
    } else if (199 >= ubx_PRN) {
        /* QZSS 193..197 -> 1..5 */
        /* ZED-F9T also see 198 and 199 */
        *gnssId = 5;
        *svId = ubx_PRN - 192;
    } else if (211 > ubx_PRN) {
        /* Huh? */
        return 0;
    } else if (246 >= ubx_PRN) {
        /* Galileo 211..246 -> 1..36 */
        *gnssId = 2;
        *svId = ubx_PRN - 210;
    } else {
        /* greater than 246
         * GLONASS (255), unused, or other unknown */
        return 0;
    }
    return ubx2_to_prn(*gnssId, *svId);
}

/**
 * Receiver/Software Version
 * UBX-MON-VER
 *
 * sadly more info than fits in session->swtype for now.
 * so squish the data hard.
 */
static void
ubx_msg_mon_ver(struct gps_device_t *session, unsigned char *buf,
                size_t data_len)
{
    size_t n = 0;       /* extended info counter */
    char obuf[128];     /* temp version string buffer */
    char *cptr;

    if (40 > data_len) {
        GPSD_LOG(LOG_WARN, &session->context->errout,
                 "Runt MON-VER message, payload len %zd", data_len);
        return;
    }

    /* save SW and HW Version as subtype */
    (void)snprintf(obuf, sizeof(obuf),
                   "SW %.30s,HW %.10s",
                   (char *)&buf[UBX_MESSAGE_DATA_OFFSET + 0],
                   (char *)&buf[UBX_MESSAGE_DATA_OFFSET + 30]);

    /* save what we can */
    (void)strlcpy(session->subtype, obuf, sizeof(session->subtype));
    /* find PROTVER= */
    cptr = strstr(session->subtype, "PROTVER=");
    if (NULL != cptr) {
        int protver = atoi(cptr + 8);
        if (9 < protver) {
            /* protver 10, u-blox 5, is the oldest we know */
            session->driver.ubx.protver = protver;
        }
    }

    obuf[0] = '\0';
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
    (void)strlcpy(session->subtype1, obuf, sizeof(session->subtype1));
    /* output SW and HW Version at LOG_INFO */
    GPSD_LOG(LOG_INF, &session->context->errout,
             "UBX-MON-VER: %s %s\n",
             session->subtype, session->subtype1);
}

/*
 * UBX-NAV-HPPOSECEF - High Precision Position Solution in ECEF
 */
static gps_mask_t
ubx_msg_nav_hpposecef(struct gps_device_t *session, unsigned char *buf,
                size_t data_len)
{
    gps_mask_t mask = ECEF_SET;
    int version;

    if (28 > data_len) {
        GPSD_LOG(LOG_WARN, &session->context->errout,
                 "Runt UBX-NAV-HPPOSECEF message, payload len %zd", data_len);
        return 0;
    }

    version = getub(buf, 0);
    session->driver.ubx.iTOW = getleu32(buf, 4);
    session->newdata.ecef.x = ((getles32(buf, 8) +
                                (getsb(buf, 20) * 1e-2)) * 1e-2);
    session->newdata.ecef.y = ((getles32(buf, 12) +
                                (getsb(buf, 21) * 1e-2)) * 1e-2);
    session->newdata.ecef.z = ((getles32(buf, 16) +
                                (getsb(buf, 22) * 1e-2)) * 1e-2);

    session->newdata.ecef.pAcc = getleu32(buf, 24) * 1e-4;
    /* (long long) cast for 32-bit compat */
    GPSD_LOG(LOG_DATA, &session->context->errout,
        "UBX-NAV-HPPOSECEF: version %d iTOW=%lld ECEF x=%.4f y=%.4f z=%.4f "
        "pAcc=%.4f\n",
        version,
        (long long)session->driver.ubx.iTOW,
        session->newdata.ecef.x,
        session->newdata.ecef.y,
        session->newdata.ecef.z,
        session->newdata.ecef.pAcc);
    return mask;
}

 /**
 * High Precision Geodetic Position Solution
 * UBX-NAV-HPPOSLLH, Class 1, ID x14
 *
 * No mode, so limited usefulness.
 */
static gps_mask_t
ubx_msg_nav_hpposllh(struct gps_device_t *session, unsigned char *buf,
                   size_t data_len)
{
    int version;
    gps_mask_t mask = 0;

    if (36 > data_len) {
        GPSD_LOG(LOG_WARN, &session->context->errout,
                 "Runt NAV-HPPOSLLH message, payload len %zd", data_len);
        return mask;
    }

    mask = ONLINE_SET | HERR_SET | VERR_SET | LATLON_SET | ALTITUDE_SET;

    version = getub(buf, 0);
    session->driver.ubx.iTOW = getles32(buf, 4);
    session->newdata.longitude = (1e-7 * (getles32(buf, 8) +
                                          (getsb(buf, 24) * 1e-2)));
    session->newdata.latitude = (1e-7 * (getles32(buf, 12) + \
                                         (getsb(buf, 25) * 1e-2)));
    /* altitude WGS84 */
    session->newdata.altHAE = (1e-3 * (getles32(buf, 16) + \
                                         (getsb(buf, 26) * 1e-2)));
    /* altitude MSL */
    session->newdata.altMSL = (1e-3 * (getles32(buf, 20) + \
                                       (getsb(buf, 27) * 1e-2)));
    /* Let gpsd_error_model() deal with geoid_sep */

    /* Horizontal accuracy estimate in .1 mm, unknown est type */
    session->newdata.eph = getleu32(buf, 28) * 1e-4;
    /* Vertical accuracy estimate in .1 mm, unknown est type */
    session->newdata.epv = getleu32(buf, 32) * 1e-4;

    GPSD_LOG(LOG_DATA, &session->context->errout,
        "UBX-NAV-HPPOSLLH: version %d iTOW=%lld lat=%.4f lon=%.4f "
        "altHAE=%.4f\n",
        version,
        (long long)session->driver.ubx.iTOW,
        session->newdata.latitude,
        session->newdata.longitude,
        session->newdata.altHAE);
    return mask;
}

/*
 * Navigation Position ECEF message
 */
static gps_mask_t
ubx_msg_nav_posecef(struct gps_device_t *session, unsigned char *buf,
                    size_t data_len)
{
    gps_mask_t mask = ECEF_SET;

    if (20 > data_len) {
        GPSD_LOG(LOG_WARN, &session->context->errout,
                 "Runt UBX-NAV-POSECEF message, payload len %zd", data_len);
        return 0;
    }

    session->driver.ubx.iTOW = getleu32(buf, 0);
    /* all in cm */
    session->newdata.ecef.x = getles32(buf, 4) * 1e-2;
    session->newdata.ecef.y = getles32(buf, 8) * 1e-2;
    session->newdata.ecef.z = getles32(buf, 12) * 1e-2;
    session->newdata.ecef.pAcc = getleu32(buf, 16) * 1e-2;

    /* (long long) cast for 32-bit compat */
    GPSD_LOG(LOG_DATA, &session->context->errout,
        "UBX-NAV-POSECEF: iTOW=%lld ECEF x=%.2f y=%.2f z=%.2f pAcc=%.2f\n",
        (long long)session->driver.ubx.iTOW,
        session->newdata.ecef.x,
        session->newdata.ecef.y,
        session->newdata.ecef.z,
        session->newdata.ecef.pAcc);
    return mask;
}

/**
 * Navigation Position Velocity Time solution message
 * UBX-NAV-PVT Class 1, ID 7
 *
 * Not in u-blox 5 or 6, present in u-blox 7
 */
static gps_mask_t
ubx_msg_nav_pvt(struct gps_device_t *session, unsigned char *buf,
                size_t data_len)
{
    uint8_t valid;
    uint8_t flags;
    uint8_t fixType;
    struct tm unpacked_date;
    int *status = &session->gpsdata.status;
    int *mode = &session->newdata.mode;
    gps_mask_t mask = 0;
    char ts_buf[TIMESPEC_LEN];

    /* u-blox 6 and 7 are 84 bytes, u-blox 8 and 9 are 92 bytes  */
    if (84 > data_len) {
        GPSD_LOG(LOG_WARN, &session->context->errout,
                 "Runt NAV-PVT message, payload len %zd", data_len);
        return 0;
    }

    session->driver.ubx.iTOW = getleu32(buf, 0);
    valid = (unsigned int)getub(buf, 11);
    fixType = (unsigned char)getub(buf, 20);
    flags = (unsigned int)getub(buf, 21);

    switch (fixType) {
    case UBX_MODE_TMONLY:
        // 5 - Surveyed-in, so a precise 3D.
        *mode = MODE_3D;
        *status = STATUS_TIME;
        mask |= STATUS_SET | MODE_SET;
        break;

    case UBX_MODE_3D:
        // 3
        // FALLTHROUGH
    case UBX_MODE_GPSDR:
        // 4
        if (*mode != MODE_3D) {
            *mode = MODE_3D;
            mask |= MODE_SET;
        }
        if ((flags & UBX_NAV_PVT_FLAG_DGPS) == UBX_NAV_PVT_FLAG_DGPS) {
            if (*status != STATUS_DGPS_FIX) {
                *status = STATUS_DGPS_FIX;
                mask |= STATUS_SET;
            }
        } else {
            if (*status != STATUS_FIX) {
                *status = STATUS_FIX;
                mask |= STATUS_SET;
            }
        }
        mask |=   LATLON_SET;
        break;

    case UBX_MODE_2D:
        // 2
        // FALLTHROUGH
    case UBX_MODE_DR:           /* consider this too as 2D */
        // 1
        if (*mode != MODE_2D) {
            *mode = MODE_2D;
            mask |= MODE_SET;
        };
        if (*status != STATUS_FIX) {
            *status = STATUS_FIX;
            mask |= STATUS_SET;
        }
        mask |= LATLON_SET | SPEED_SET;
        break;

    case UBX_MODE_NOFIX:
        // 0
        // FALLTHROUGH
    default:
        // huh?
        if (*mode != MODE_NO_FIX) {
            *mode = MODE_NO_FIX;
            mask |= MODE_SET;
        };
        if (*status != STATUS_NO_FIX) {
            *status = STATUS_NO_FIX;
            mask |= STATUS_SET;
        }
        break;
    }

    if ((valid & UBX_NAV_PVT_VALID_DATE_TIME) == UBX_NAV_PVT_VALID_DATE_TIME) {
        unpacked_date.tm_year = (uint16_t)getleu16(buf, 4) - 1900;
        unpacked_date.tm_mon = (uint8_t)getub(buf, 6) - 1;
        unpacked_date.tm_mday = (uint8_t)getub(buf, 7);
        unpacked_date.tm_hour = (uint8_t)getub(buf, 8);
        unpacked_date.tm_min = (uint8_t)getub(buf, 9);
        unpacked_date.tm_sec = (uint8_t)getub(buf, 10);
        unpacked_date.tm_isdst = 0;
        unpacked_date.tm_wday = 0;
        unpacked_date.tm_yday = 0;
        session->newdata.time.tv_sec = mkgmtime(&unpacked_date);
        /* field 16, nano, can be negative! So normalize */
        session->newdata.time.tv_nsec = getles32(buf, 16);
        TS_NORM(&session->newdata.time);
        mask |= TIME_SET | NTPTIME_IS | GOODTIME_IS;
    }

    session->newdata.longitude = 1e-7 * getles32(buf, 24);
    session->newdata.latitude = 1e-7 * getles32(buf, 28);
    /* altitude WGS84 */
    session->newdata.altHAE = 1e-3 * getles32(buf, 32);
    /* altitude MSL */
    session->newdata.altMSL = 1e-3 * getles32(buf, 36);
    /* Let gpsd_error_model() deal with geoid_sep */

    session->newdata.speed = 1e-3 * (int32_t)getles32(buf, 60);
    /* u-blox calls this Heading of motion (2-D) */
    session->newdata.track = 1e-5 * (int32_t)getles32(buf, 64);
    mask |= LATLON_SET | ALTITUDE_SET | SPEED_SET | TRACK_SET;

    /* Height Accuracy estimate, unknown details */
    session->newdata.eph = (double)(getles32(buf, 40) / 1000.0);
    /* Velocity Accuracy estimate, unknown details */
    session->newdata.epv = (double)(getles32(buf, 44) / 1000.0);
    /* Speed Accuracy estimate, unknown details */
    session->newdata.eps = (double)(getles32(buf, 48) / 1000.0);
    /* let gpsd_error_model() do the rest */

    mask |= HERR_SET | SPEEDERR_SET | VERR_SET;

    GPSD_LOG(LOG_DATA, &session->context->errout,
         "NAV-PVT: flags=%02x time=%s lat=%.2f lon=%.2f altHAE=%.2f "
         "track=%.2f speed=%.2f climb=%.2f mode=%d status=%d used=%d\n",
         flags,
         timespec_str(&session->newdata.time, ts_buf, sizeof(ts_buf)),
         session->newdata.latitude,
         session->newdata.longitude,
         session->newdata.altHAE,
         session->newdata.track,
         session->newdata.speed,
         session->newdata.climb,
         session->newdata.mode,
         session->gpsdata.status,
         session->gpsdata.satellites_used);
    if (92 <= data_len) {
        /* u-blox 8 and 9 extended */
        double magDec = NAN;
        double magAcc = NAN;
#ifdef __UNUSED
        if (flags & UBX_NAV_PVT_FLAG_HDG_OK) {
            /* u-blox calls this Heading of vehicle (2-D)
             * why is it different than earlier track? */
            session->newdata.track = (double)(getles32(buf, 84) * 1e-5);
        }
#endif /* __UNUSED */
        if (valid & UBX_NAV_PVT_VALID_MAG) {
            magDec = (double)(getles16(buf, 88) * 1e-2);
            magAcc = (double)(getleu16(buf, 90) * 1e-2);
        }
        GPSD_LOG(LOG_DATA, &session->context->errout,
             "  headVeh %.5f magDec %.2f magAcc %.2f\n",
             session->newdata.track, magDec, magAcc);
    }
    return mask;
}


 /**
 * High Precision Relative Positioning Information in NED frame
 * UBX-NAV-RELPOSNED, Class 1, ID x3c
 * HP GNSS only, protver 20+
 */
static gps_mask_t
ubx_msg_nav_relposned(struct gps_device_t *session, unsigned char *buf,
                      size_t data_len)
{
    int version;
    unsigned refStationId, flags;
    double accN, accE, accD;
    gps_mask_t mask = 0;

    if (40 > data_len) {
        GPSD_LOG(LOG_WARN, &session->context->errout,
                 "Runt NAV-RELPOSNED message, payload len %zd", data_len);
        return mask;
    }

    mask = NED_SET;

    version = getub(buf, 0);
    refStationId = getleu16(buf, 2);
    session->driver.ubx.iTOW = getles32(buf, 4);
    session->newdata.NED.relPosN = (1e-2 * (getles32(buf, 8) +
        (getsb(buf, 20) * 1e-2)));
    session->newdata.NED.relPosE = (1e-2 * (getles32(buf, 12) +
        (getsb(buf, 21) * 1e-2)));
    session->newdata.NED.relPosD = (1e-2 * (getles32(buf, 16) +
        (getsb(buf, 22) * 1e-2)));

    accN = 1e-4 * getles32(buf, 24);
    accE = 1e-4 * getles32(buf, 28);
    accD = 1e-4 * getles32(buf, 32);
    flags = getleu32(buf, 36);

    GPSD_LOG(LOG_DATA, &session->context->errout,
        "UBX-NAV-RELPOSNED: version %d iTOW=%lld refStationId %u flags x%x\n"
        "UBX-NAV-RELPOSNED: relPos N=%.4f E=%.4f D=%.4f\n"
        "UBX-NAV-RELPOSNED: acc N=%.4f E=%.4f D=%.4f\n",
        version,
        (long long)session->driver.ubx.iTOW,
        refStationId,
        flags,
        session->newdata.NED.relPosN,
        session->newdata.NED.relPosE,
        session->newdata.NED.relPosD,
        accN, accE, accD);

    if (5 != (flags & 5)) {
        /* gnssFixOK or relPosValid are false, no fix */
        return 0;
    }
    return mask;
}

/**
 * Navigation solution message: UBX-NAV-SOL
 *
 * UBX-NAV-SOL deprecated in u-blox 6, gone in u-blox 9.
 * Use UBX-NAV-PVT instead
 */
static gps_mask_t
ubx_msg_nav_sol(struct gps_device_t *session, unsigned char *buf,
                size_t data_len)
{
    unsigned int flags;
    unsigned char navmode;
    gps_mask_t mask;
    char ts_buf[TIMESPEC_LEN];

    if (52 > data_len) {
        GPSD_LOG(LOG_WARN, &session->context->errout,
                 "Runt NAV-SOL message, payload len %zd", data_len);
        return 0;
    }

    session->driver.ubx.iTOW = getleu32(buf, 0);
    flags = (unsigned int)getub(buf, 11);
    mask = 0;
#define DATE_VALID      (UBX_SOL_VALID_WEEK | UBX_SOL_VALID_TIME)
    if ((flags & DATE_VALID) == DATE_VALID) {
        unsigned short week;
        timespec_t ts_tow;

        MSTOTS(&ts_tow, session->driver.ubx.iTOW);
        ts_tow.tv_nsec += (long)getles32(buf, 4);
        week = (unsigned short)getles16(buf, 8);
        session->newdata.time = gpsd_gpstime_resolv(session, week, ts_tow);
        mask |= TIME_SET | NTPTIME_IS | GOODTIME_IS;
    }
#undef DATE_VALID

    session->newdata.ecef.x = getles32(buf, 12) / 100.0;
    session->newdata.ecef.y = getles32(buf, 16) / 100.0;
    session->newdata.ecef.z = getles32(buf, 20) / 100.0;
    session->newdata.ecef.pAcc = getleu32(buf, 24) / 100.0;
    session->newdata.ecef.vx = getles32(buf, 28) / 100.0;
    session->newdata.ecef.vy = getles32(buf, 32) / 100.0;
    session->newdata.ecef.vz = getles32(buf, 36) / 100.0;
    session->newdata.ecef.vAcc = getleu32(buf, 40) / 100.0;
    mask |= ECEF_SET | VECEF_SET;

    session->newdata.eps = (double)(getles32(buf, 40) / 100.0);
    mask |= SPEEDERR_SET;

    /* Better to have a single point of truth about DOPs */
    //session->gpsdata.dop.pdop = (double)(getleu16(buf, 44)/100.0);
    session->gpsdata.satellites_used = (int)getub(buf, 47);

    navmode = (unsigned char)getub(buf, 10);
    switch (navmode) {
    case UBX_MODE_TMONLY:
        /* Surveyed-in, better not have moved */
        session->newdata.mode = MODE_3D;
        session->gpsdata.status = STATUS_TIME;
        break;
    case UBX_MODE_3D:
        session->newdata.mode = MODE_3D;
        session->gpsdata.status = STATUS_FIX;
        break;
    case UBX_MODE_2D:
        session->newdata.mode = MODE_2D;
        session->gpsdata.status = STATUS_FIX;
        break;
    case UBX_MODE_DR:           /* consider this too as 2D */
        session->newdata.mode = MODE_2D;
        session->gpsdata.status = STATUS_DR;
        break;
    case UBX_MODE_GPSDR:        /* DR-aided GPS is valid 3D */
        session->newdata.mode = MODE_3D;
        session->gpsdata.status = STATUS_GNSSDR;
        break;
    default:
        session->newdata.mode = MODE_NO_FIX;
        session->gpsdata.status = STATUS_NO_FIX;
        break;
    }

    if ((flags & UBX_SOL_FLAG_DGPS) != 0)
        session->gpsdata.status = STATUS_DGPS_FIX;

    mask |= MODE_SET | STATUS_SET;

    GPSD_LOG(LOG_DATA, &session->context->errout,
             "UBX-NAV-SOL: time=%s ecef x:%.2f y:%.2f z:%.2f track=%.2f "
             "speed=%.2f climb=%.2f mode=%d status=%d used=%d\n",
             timespec_str(&session->newdata.time, ts_buf, sizeof(ts_buf)),
             session->newdata.ecef.x,
             session->newdata.ecef.y,
             session->newdata.ecef.z,
             session->newdata.track,
             session->newdata.speed,
             session->newdata.climb,
             session->newdata.mode,
             session->gpsdata.status,
             session->gpsdata.satellites_used);
    return mask;
}


/**
 * Navigation time to leap second: UBX-NAV-TIMELS
 *
 * Sets leap_notify if leap second is < 23 hours away.
 * Not in u-blox 5
 */
static void ubx_msg_nav_timels(struct gps_device_t *session,
                               unsigned char *buf, size_t data_len)
{
    int version;
    unsigned int flags;
    int valid_curr_ls;
    int valid_time_to_ls_event;

#define UBX_TIMELS_VALID_CURR_LS 0x01
#define UBX_TIMELS_VALID_TIME_LS_EVT 0x01

    if (24 > data_len) {
        GPSD_LOG(LOG_WARN, &session->context->errout,
                 "UBX-NAV-TIMELS: unexpected length %zd, expecting 24\n",
                 data_len);
        return;
    }

    session->driver.ubx.iTOW = getles32(buf, 0);
    version = getsb(buf, 4);
    /* Only version 0 is defined so far. */
    flags = (unsigned int)getub(buf, 23);
    GPSD_LOG(LOG_PROG, &session->context->errout,
             "UBX-NAV-TIMELS: flags 0x%x message version %d\n",
             flags, version);
    valid_curr_ls = flags & UBX_TIMELS_VALID_CURR_LS;
    valid_time_to_ls_event = flags & UBX_TIMELS_VALID_TIME_LS_EVT;
    if (valid_curr_ls) {
        unsigned int src_of_curr_ls = getub(buf,8);
        int curr_ls = getsb(buf,9);
        char *src = "Unknown";
        static char *srcOfCurrLs[] = {
            "firmware",
            "GPS GLONASS difference",
            "GPS",
            "SBAS",
            "BeiDou",
            "Galileo",
            "Aided data",
            "Configured"
        };

        if (src_of_curr_ls < (sizeof(srcOfCurrLs) / sizeof(srcOfCurrLs[0])))
            src = srcOfCurrLs[src_of_curr_ls];

        GPSD_LOG(LOG_DATA, &session->context->errout,
                 "UBX-NAV-TIMELS: source_of_current_leapsecond=%u:%s "
                 "curr_ls=%d\n",
                 src_of_curr_ls, src,curr_ls);
        session->context->leap_seconds = curr_ls;
        session->context->valid |= LEAP_SECOND_VALID;
    } /* Valid current leap second */

    if (valid_time_to_ls_event) {
        char *src = "Unknown";
        unsigned int src_of_ls_change;
        unsigned short dateOfLSGpsWn, dateOfLSGpsDn;
        int lsChange = getsb(buf, 11);
        int timeToLsEvent = getles32(buf, 12);
        static char *srcOfLsChange[] = {
            "No Source",
            "Undefined",
            "GPS",
            "SBAS",
            "BeiDou",
            "Galileo",
            "GLONASS",
        };

        src_of_ls_change = getub(buf,10);
        if (src_of_ls_change <
            (sizeof(srcOfLsChange) / sizeof(srcOfLsChange[0]))) {
            src = srcOfLsChange[src_of_ls_change];
        }

        dateOfLSGpsWn = getles16(buf,16);
        dateOfLSGpsDn = getles16(buf,18);
        GPSD_LOG(LOG_DATA, &session->context->errout,
                 "UBX-NAV-TIMELS: source_of_leapsecond_change %u:%s "
                 "leapSecondChage %d timeToLsEvent %d\n",
                 src_of_ls_change,src,lsChange,timeToLsEvent);

        GPSD_LOG(LOG_DATA, &session->context->errout,
                 "UBX-NAV-TIMELS: dateOfLSGpsWn=%d dateOfLSGpsDn=%d\n",
                 dateOfLSGpsWn,dateOfLSGpsDn);
        if ((0 != lsChange) && (0 < timeToLsEvent) &&
            ((60 * 60 * 23) > timeToLsEvent)) {
            if (1 == lsChange) {
                session->context->leap_notify = LEAP_ADDSECOND;
                GPSD_LOG(LOG_INF, &session->context->errout,
                         "UBX-NAV-TIMELS: Positive leap second today\n");
            } else if (-1 == lsChange) {
                session->context->leap_notify = LEAP_DELSECOND;
                GPSD_LOG(LOG_INF, &session->context->errout,
                         "UBX-NAV-TIMELS: Negative leap second today\n");
            }
        } else {
            session->context->leap_notify = LEAP_NOWARNING;
            GPSD_LOG(LOG_DATA, &session->context->errout,
                     "UBX-NAV-TIMELS: leap_notify %d, none today\n",
                     session->context->leap_notify);
        }
    }
}

 /**
 * Geodetic position solution message
 * UBX-NAV-POSLLH, Class 1, ID 2
 *
 * No mode, so limited usefulness
 */
static gps_mask_t
ubx_msg_nav_posllh(struct gps_device_t *session, unsigned char *buf,
                   size_t data_len UNUSED)
{
    gps_mask_t mask = 0;

    if (28 > data_len) {
        GPSD_LOG(LOG_WARN, &session->context->errout,
                 "Runt NAV-POSLLH message, payload len %zd", data_len);
        return 0;
    }

    mask = ONLINE_SET | HERR_SET | VERR_SET | LATLON_SET | ALTITUDE_SET;

    session->driver.ubx.iTOW = getles32(buf, 0);
    session->newdata.longitude = 1e-7 * getles32(buf, 4);
    session->newdata.latitude = 1e-7 * getles32(buf, 8);
    /* altitude WGS84 */
    session->newdata.altHAE = 1e-3 * getles32(buf, 12);
    /* altitude MSL */
    session->newdata.altMSL = 1e-3 * getles32(buf, 16);
    /* Let gpsd_error_model() deal with geoid_sep */

    /* Horizontal accuracy estimate in mm, unknown type */
    session->newdata.eph = getleu32(buf, 20) * 1e-3;
    /* Vertical accuracy estimate in mm, unknown type */
    session->newdata.epv = getleu32(buf, 24) * 1e-3;

    GPSD_LOG(LOG_DATA, &session->context->errout,
        "UBX-NAV-POSLLH: iTOW=%lld lat=%.3f lon=%.3f altHAE=%.3f "
        "eph %.3f epv %.3f\n",
        (long long)session->driver.ubx.iTOW,
        session->newdata.latitude,
        session->newdata.longitude,
        session->newdata.altHAE,
        session->newdata.eph,
        session->newdata.epv);
    return mask;
}

/**
 * Dilution of precision message
 */
static gps_mask_t
ubx_msg_nav_dop(struct gps_device_t *session, unsigned char *buf,
                size_t data_len)
{
    if (18 > data_len) {
        GPSD_LOG(LOG_WARN, &session->context->errout,
                 "Runt RXM-SFRB message, payload len %zd", data_len);
        return 0;
    }

    session->driver.ubx.iTOW = getles32(buf, 0);
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
    GPSD_LOG(LOG_DATA, &session->context->errout,
             "NAVDOP: gdop=%.2f pdop=%.2f "
             "hdop=%.2f vdop=%.2f tdop=%.2f mask={DOP}\n",
             session->gpsdata.dop.gdop,
             session->gpsdata.dop.hdop,
             session->gpsdata.dop.vdop,
             session->gpsdata.dop.pdop, session->gpsdata.dop.tdop);
    return DOP_SET;
}

/**
 * End of Epoch
 * Not in u-blox 5, 6 or 7
 * Present in u-blox 8 and 9
 */
static gps_mask_t
ubx_msg_nav_eoe(struct gps_device_t *session, unsigned char *buf,
                size_t data_len)
{
    if (4 > data_len) {
        GPSD_LOG(LOG_WARN, &session->context->errout,
                 "Runt NAV-EOE message, payload len %zd", data_len);
        return 0;
    }

    if (18 > session->driver.ubx.protver) {
        /* this GPS is at least protver 18 */
        session->driver.ubx.protver = 18;
    }
    session->driver.ubx.iTOW = getles32(buf, 0);
    GPSD_LOG(LOG_DATA, &session->context->errout, "EOE: iTOW=%lld\n",
             (long long)session->driver.ubx.iTOW);
    /* nothing to report, but the iTOW for cycle ender is good */
    return 0;
}

/**
 * GPS Leap Seconds - UBX-NAV-TIMEGPS
 */
static gps_mask_t
ubx_msg_nav_timegps(struct gps_device_t *session, unsigned char *buf,
                    size_t data_len)
{
    uint8_t valid;         /* Validity Flags */
    gps_mask_t mask = 0;
    char ts_buf[TIMESPEC_LEN];

    if (16 > data_len) {
        GPSD_LOG(LOG_WARN, &session->context->errout,
                 "Runt NAV-TIMEGPS message, payload len %zd", data_len);
        return 0;
    }

    session->driver.ubx.iTOW = getles32(buf, 0);
    valid = getub(buf, 11);
    // Valid leap seconds ?
    if ((valid & UBX_TIMEGPS_VALID_LEAP_SECOND) ==
        UBX_TIMEGPS_VALID_LEAP_SECOND) {
        session->context->leap_seconds = (int)getub(buf, 10);
        session->context->valid |= LEAP_SECOND_VALID;
    }
    // Valid GPS time of week and week number
#define VALID_TIME (UBX_TIMEGPS_VALID_TIME | UBX_TIMEGPS_VALID_WEEK)
    if ((valid & VALID_TIME) == VALID_TIME) {
#undef VALID_TIME
        uint16_t week;
        double tAcc;      /* Time Accuracy Estimate in ns */
        timespec_t ts_tow;

        week = getles16(buf, 8);
	MSTOTS(&ts_tow, session->driver.ubx.iTOW);
        ts_tow.tv_nsec += (long)getles32(buf, 4);
        session->newdata.time = gpsd_gpstime_resolv(session, week, ts_tow);

        tAcc = (double)getleu32(buf, 12);     /* tAcc in ms */
        session->newdata.ept = tAcc * 1e-9;
        mask |= (TIME_SET | NTPTIME_IS);
    }

    GPSD_LOG(LOG_DATA, &session->context->errout,
             "TIMEGPS: time=%s mask={TIME}\n",
             timespec_str(&session->newdata.time, ts_buf, sizeof(ts_buf)));
    return mask;
}

/**
 * GPS Satellite Info -- new style UBX-NAV-SAT
 * Not in u-blox 5, protocol version 15+
 */
static gps_mask_t
ubx_msg_nav_sat(struct gps_device_t *session, unsigned char *buf,
                size_t data_len)
{
    unsigned int i, nchan, nsv, st, ver;

    if (8 > data_len) {
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "Runt NAV-SAT (datalen=%zd)\n", data_len);
        return 0;
    }

    session->driver.ubx.iTOW = getles32(buf, 0);
    ver = (unsigned int)getub(buf, 4);
    if (1 != ver) {
        GPSD_LOG(LOG_WARN, &session->context->errout,
                 "NAV-SAT message unknown version %d", ver);
        return 0;
    }
    nchan = (unsigned int)getub(buf, 5);
    if (nchan > MAXCHANNELS) {
        GPSD_LOG(LOG_WARN, &session->context->errout,
                 "Runt NAV-SAT message, >%d reported visible",
                 MAXCHANNELS);
        return 0;
    }
    /* two "unused" bytes at buf[6:7] */

    gpsd_zero_satellites(&session->gpsdata);
    nsv = 0;
    for (i = st = 0; i < nchan; i++) {
        unsigned int off = 8 + 12 * i;
        short nmea_PRN = 0;
        unsigned char gnssId = getub(buf, off + 0);
        short svId = (short)getub(buf, off + 1);
        unsigned char cno = getub(buf, off + 2);
        /* health data in flags. */
        uint32_t flags = getleu32(buf, off + 8);
        bool used = (bool)(flags  & 0x08);
        int tmp;
        /* Notice NO sigid! */

        nmea_PRN = ubx2_to_prn(gnssId, svId);

#ifdef __UNUSED
        /* debug */
        GPSD_LOG(LOG_ERROR, &session->context->errout,
                 "NAV-SAT gnssid %d, svid %d nmea_PRN %d\n",
                 gnssId, svId, nmea_PRN);
#endif /* __UNUSED */

        session->gpsdata.skyview[st].gnssid = gnssId;
        session->gpsdata.skyview[st].svid = svId;
        session->gpsdata.skyview[st].PRN = nmea_PRN;

        session->gpsdata.skyview[st].ss = (double)cno;
        tmp = getsb(buf, off + 3);
        if (90 >= abs(tmp)) {
            session->gpsdata.skyview[st].elevation = (double)tmp;
        }
        tmp = getles16(buf, off + 4);
        if (359 > tmp && 0 <= tmp) {
            session->gpsdata.skyview[st].azimuth = (double)tmp;
        }
        session->gpsdata.skyview[st].used = used;
        /* by some coincidence, our health flags matches u-blox's */
        session->gpsdata.skyview[st].health = (flags >> 4) & 3;
        /* sbas_in_use is not same as used */
        if (used) {
            nsv++;
            session->gpsdata.skyview[st].used = true;
        }
        st++;
    }

    /* UBX does not give us these, so recompute */
    session->gpsdata.dop.xdop = NAN;
    session->gpsdata.dop.ydop = NAN;
    session->gpsdata.skyview_time.tv_sec = 0;
    session->gpsdata.skyview_time.tv_nsec = 0;
    session->gpsdata.satellites_visible = (int)st;
    session->gpsdata.satellites_used = (int)nsv;
    GPSD_LOG(LOG_DATA, &session->context->errout,
             "SAT: visible=%d used=%d mask={SATELLITE|USED}\n",
             session->gpsdata.satellites_visible,
             session->gpsdata.satellites_used);
    return SATELLITE_SET | USED_IS;
}

/**
 * GPS Satellite Info -- deprecated - UBX-NAV-SVINFO
 * Not in u-blox 9, use UBX-NAV-SAT instead
 */
static gps_mask_t
ubx_msg_nav_svinfo(struct gps_device_t *session, unsigned char *buf,
                   size_t data_len)
{
    unsigned int i, nchan, nsv, st;

    if (8 > data_len) {
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "Runt NAV-SVINFO (datalen=%zd)\n", data_len);
        return 0;
    }

    session->driver.ubx.iTOW = getles32(buf, 0);
    nchan = (unsigned int)getub(buf, 4);
    if (nchan > MAXCHANNELS) {
        GPSD_LOG(LOG_WARN, &session->context->errout,
                 "Runt NAV SVINFO message, >%d reported visible",
                 MAXCHANNELS);
        return 0;
    }
    gpsd_zero_satellites(&session->gpsdata);
    nsv = 0;
    for (i = st = 0; i < nchan; i++) {
        unsigned int off = 8 + 12 * i;
        short nmea_PRN;
        short ubx_PRN = (short)getub(buf, off + 1);
        unsigned char snr = getub(buf, off + 4);
        bool used = (bool)(getub(buf, off + 2) & 0x01);
        unsigned char flags = getub(buf, off + 12) & 3;
        int tmp;

        nmea_PRN = ubx_to_prn(ubx_PRN,
                              &session->gpsdata.skyview[st].gnssid,
                              &session->gpsdata.skyview[st].svid);

#ifdef __UNUSED
        /* debug */
        GPSD_LOG(LOG_ERROR, &session->context->errout,
                 "NAV-SVINFO ubx_prn %d gnssid %d, svid %d nmea_PRN %d\n",
                 ubx_PRN,
                 session->gpsdata.skyview[st].gnssid,
                 session->gpsdata.skyview[st].svid, nmea_PRN);
#endif /* __UNUSED */
        if (1 > nmea_PRN) {
            /* skip bad PRN */
            continue;
        }
        session->gpsdata.skyview[st].PRN = nmea_PRN;

        session->gpsdata.skyview[st].ss = (double)snr;
        tmp = getsb(buf, off + 5);
        if (90 >= abs(tmp)) {
            session->gpsdata.skyview[st].elevation = (double)tmp;
        }
        tmp = (double)getles16(buf, off + 6);
        if (359 > tmp && 0 <= tmp) {
            session->gpsdata.skyview[st].azimuth = (double)tmp;
        }
        session->gpsdata.skyview[st].used = used;
        if (0x10 & flags) {
           session->gpsdata.skyview[st].health = SAT_HEALTH_BAD;
        } else {
           session->gpsdata.skyview[st].health = SAT_HEALTH_OK;
        }

        /* sbas_in_use is not same as used */
        if (used) {
            /* not really 'used', just integrity data from there */
            nsv++;
            session->gpsdata.skyview[st].used = true;
        }
        st++;
    }

    /* UBX does not give us these, so recompute */
    session->gpsdata.dop.xdop = NAN;
    session->gpsdata.dop.ydop = NAN;
    session->gpsdata.skyview_time.tv_sec = 0;
    session->gpsdata.skyview_time.tv_nsec = 0;
    session->gpsdata.satellites_visible = (int)st;
    session->gpsdata.satellites_used = (int)nsv;
    GPSD_LOG(LOG_DATA, &session->context->errout,
             "SVINFO: visible=%d used=%d mask={SATELLITE|USED}\n",
             session->gpsdata.satellites_visible,
             session->gpsdata.satellites_used);
    return SATELLITE_SET | USED_IS;
}

/*
 * Velocity Position ECEF message, UBX-NAV-VELECEF
 */
static gps_mask_t
ubx_msg_nav_velecef(struct gps_device_t *session, unsigned char *buf,
                size_t data_len)
{
    gps_mask_t mask = VECEF_SET;

    if (20 > data_len) {
        GPSD_LOG(LOG_WARN, &session->context->errout,
                 "Runt NAV-VELECEF message, payload len %zd", data_len);
        return 0;
    }

    session->driver.ubx.iTOW = getles32(buf, 0);
    session->newdata.ecef.vx = getles32(buf, 4) / 100.0;
    session->newdata.ecef.vy = getles32(buf, 8) / 100.0;
    session->newdata.ecef.vz = getles32(buf, 12) / 100.0;
    session->newdata.ecef.vAcc = getleu32(buf, 16) / 100.0;
    GPSD_LOG(LOG_DATA, &session->context->errout,
        "UBX-NAV-VELECEF: iTOW=%lld ECEF vx=%.2f vy=%.2f vz=%.2f vAcc=%.2f\n",
         (long long)session->driver.ubx.iTOW,
        session->newdata.ecef.vx,
        session->newdata.ecef.vy,
        session->newdata.ecef.vz,
        session->newdata.ecef.vAcc);
    return mask;
}

/*
 * Velocity NED message, UBX-NAV-VELNED
 * protocol versions 15+
 */
static gps_mask_t
ubx_msg_nav_velned(struct gps_device_t *session, unsigned char *buf,
                size_t data_len)
{
    gps_mask_t mask = VNED_SET;

    if (36 > data_len) {
        GPSD_LOG(LOG_WARN, &session->context->errout,
                 "Runt NAV-VELNED message, payload len %zd", data_len);
        return 0;
    }

    session->driver.ubx.iTOW = getles32(buf, 0);
    session->newdata.NED.velN = getles32(buf, 4) / 100.0;
    session->newdata.NED.velE = getles32(buf, 8) / 100.0;
    session->newdata.NED.velD = getles32(buf, 12) / 100.0;
    /* ignore speed for now */
    GPSD_LOG(LOG_DATA, &session->context->errout,
        "UBX-NAV-VELNED: iTOW=%lld NED velN=%.2f velE=%.2f velD=%.2f\n",
         (long long)session->driver.ubx.iTOW,
        session->newdata.NED.velN,
        session->newdata.NED.velE,
        session->newdata.NED.velD);
    return mask;
}

/*
 * SBAS Info UBX-NAV-SBAS
 * Not in u-blox 9
 * FIXME: not well decoded...
 */
static void ubx_msg_sbas(struct gps_device_t *session, unsigned char *buf,
                         size_t data_len)
{
    unsigned int i, nsv;
    short ubx_PRN;
    short nmea_PRN;
    unsigned char gnssid = 0;
    unsigned char svid = 0;

    if (12 > data_len) {
        GPSD_LOG(LOG_WARN, &session->context->errout,
                 "Runt NAV-SBAS message, payload len %zd", data_len);
        return;
    }

    session->driver.ubx.iTOW = getles32(buf, 0);
    GPSD_LOG(LOG_DATA, &session->context->errout,
             "SBAS: %d %d %d %d %d\n",
             (int)getub(buf, 4), (int)getub(buf, 5), (int)getub(buf, 6),
             (int)getub(buf, 7), (int)getub(buf, 8));

    nsv = (int)getub(buf, 8);
    for (i = 0; i < nsv; i++) {
        int off = 12 + 12 * i;
        GPSD_LOG(LOG_DATA, &session->context->errout,
                 "SBAS info on SV: %d\n", (int)getub(buf, off));
    }
    /* really 'in_use' depends on the sats info, EGNOS is still
     * in test.  In WAAS areas one might also check for the type of
     * corrections indicated
     */

    ubx_PRN = getub(buf, 4);
    nmea_PRN = ubx_to_prn(ubx_PRN, &gnssid, &svid);
#ifdef __UNUSED
    /* debug */
    GPSD_LOG(LOG_ERROR, &session->context->errout,
             "NAV-SBAS ubx_prn %d gnssid %d, svid %d nmea_PRN %d\n",
             ubx_PRN, gnssid, svid, nmea_PRN);
#endif /* __UNUSED */
    session->driver.ubx.sbas_in_use = nmea_PRN;
}

/*
 * Multi-GNSS Raw measurement Data -- UBX-RXM-RAWX
 * Not in u-blox 5, 6 or 7
 */
static gps_mask_t ubx_rxm_rawx(struct gps_device_t *session,
                               const unsigned char *buf,
                               size_t data_len)
{
    double rcvTow;
    uint16_t week;
    int8_t leapS;
    uint8_t numMeas;
    uint8_t recStat;
    int i;
    const char * obs_code;
    timespec_t ts_tow;

    if (16 > data_len) {
        GPSD_LOG(LOG_WARN, &session->context->errout,
                 "Runt RXM-RAWX message, payload len %zd", data_len);
        return 0;
    }

    /* Note: this is "approximately" GPS TOW, this is not iTOW */
    rcvTow = getled64((const char *)buf, 0);   /* time of week in seconds */
    week = getleu16(buf, 8);
    leapS = getsb(buf, 10);
    numMeas = getub(buf, 11);
    recStat = getub(buf, 12);

    GPSD_LOG(LOG_PROG, &session->context->errout,
             "UBX-RXM-RAWX: rcvTow %f week %u leapS %d numMeas %u recStat %d\n",
             rcvTow, week, leapS, numMeas, recStat);

    if (recStat & 1) {
        /* Valid leap seconds */
        session->context->leap_seconds = leapS;
        session->context->valid |= LEAP_SECOND_VALID;
    }
    /* convert GPS weeks and "approximately" GPS TOW to UTC */
    DTOTS(&ts_tow, rcvTow);
    // Do not set newdata.time.  set gpsdata.raw.mtime
    session->gpsdata.raw.mtime = gpsd_gpstime_resolv(session, week, ts_tow);

    /* zero the measurement data */
    /* so we can tell which meas never got set */
    memset(session->gpsdata.raw.meas, 0, sizeof(session->gpsdata.raw.meas));

    for (i = 0; i < numMeas; i++) {
        int off = 32 * i;
        /* psuedorange in meters */
        double prMes = getled64((const char *)buf, off + 16);
        /* carrier phase in cycles */
        double cpMes = getled64((const char *)buf, off + 24);
        /* doppler in Hz, positive towards sat */
        double doMes = getlef32((const char *)buf, off + 32);
        uint8_t gnssId = getub(buf, off + 36);
        uint8_t svId = getub(buf, off + 37);
        /* reserved in u-blox 8, sigId in u-blox 9 */
        uint8_t sigId = getub(buf, off + 38);
        /* GLONASS frequency slot */
        uint8_t freqId = getub(buf, off + 39);
        /* carrier phase locktime in ms, max 64500ms */
        uint16_t locktime = getleu16(buf, off + 40);
        /* carrier-to-noise density ratio dB-Hz */
        uint8_t cno = getub(buf, off + 42);
        uint8_t prStdev = getub(buf, off + 43) & 0x0f;
        uint8_t cpStdev = getub(buf, off + 44) & 0x0f;
        uint8_t doStdev = getub(buf, off + 45) & 0x0f;
        /* tracking stat
         * bit 0 - prMes valid
         * bit 1 - cpMes valid
         * bit 2 - halfCycle valid
         * bit 3 - halfCycle subtracted from phase
         */
        uint8_t trkStat = getub(buf, off + 46);
        GPSD_LOG(LOG_DATA, &session->context->errout,
                 "%u:%u:%u freqId %u prMes %f cpMes %f doMes %f locktime %u\n"
                 "cno %u prStdev %u cpStdev %u doStdev %u rtkStat %u\n",
                 gnssId, svId, sigId, freqId, prMes, cpMes, doMes, locktime,
                 cno, prStdev, cpStdev, doStdev, trkStat);

        session->gpsdata.raw.meas[i].gnssid = gnssId;
        session->gpsdata.raw.meas[i].sigid = sigId;

        /* some of these are GUESSES as the u-blox codes do not
         * match RINEX codes */
        switch (gnssId) {
        case 0:       /* GPS */
            switch (sigId) {
            default:
                /* let PPP figure it out */
                /* FALLTHROUGH */
            case 0:       /* L1C/A */
                obs_code = "L1C";
                break;
            case 3:       /* L2 CL */
                obs_code = "L2C";
                break;
            case 4:       /* L2 CM */
                obs_code = "L2X";
                break;
            }
            break;
        case 1:       /* SBAS */
            /* sigId added on protVer 27, and SBAS gone in protVer 27
             * so must be L1C/A */
            svId -= 100;            /* adjust for RINEX 3 svid */

            /* SBAS can do L5I, but the code? */
            switch (sigId) {
            default:
                /* let PPP figure it out */
                /* FALLTHROUGH */
            case 0:       /* L1C/A */
                obs_code = "L1C";
                break;
            }
            obs_code = "L1C";       /* u-blox calls this L1C/A */
            break;
        case 2:       /* GALILEO */
            switch (sigId) {
            default:
                /* let PPP figure it out */
                /* FALLTHROUGH */
            case 0:       /*  */
                obs_code = "L1C";       /* u-blox calls this E1OS or E1C */
                break;
            case 1:       /*  */
                obs_code = "L1B";       /* u-blox calls this E1B */
                break;
            case 5:       /*  */
                obs_code = "L7I";       /* u-blox calls this E5bl */
                break;
            case 6:       /*  */
                obs_code = "L7Q";       /* u-blox calls this E5bQ */
                break;
            }
            break;
        case 3:       /* BeiDou */
            switch (sigId) {
            default:
                /* let PPP figure it out */
                /* FALLTHROUGH */
            case 0:       /*  */
                obs_code = "L2Q";       /* u-blox calls this B1I D1 */
                break;
            case 1:       /*  */
                obs_code = "L2I";       /* u-blox calls this B1I D2 */
                break;
            case 2:       /*  */
                obs_code = "L7Q";       /* u-blox calls this B2I D1 */
                break;
            case 3:       /*  */
                obs_code = "L7I";       /* u-blox calls this B2I D2 */
                break;
            }
            break;
        default:      /* huh? */
        case 4:       /* IMES.  really? */
            obs_code = "";       /* u-blox calls this L1 */
            break;
        case 5:       /* QZSS */
            switch (sigId) {
            default:
                /* let PPP figure it out */
                /* FALLTHROUGH */
            case 0:       /*  */
                obs_code = "L1C";       /* u-blox calls this L1C/A */
                break;
            case 4:       /*  */
                obs_code = "L2S";       /* u-blox calls this L2CM */
                break;
            case 5:       /*  */
                obs_code = "L2L";       /* u-blox calls this L2CL*/
                break;
            }
            break;
        case 6:       /* GLONASS */
            switch (sigId) {
            default:
                /* let PPP figure it out */
                /* FALLTHROUGH */
            case 0:       /*  */
                obs_code = "L1C";       /* u-blox calls this L1OF */
                break;
            case 2:       /*  */
                obs_code = "L2C";       /* u-blox calls this L2OF */
                break;
            }
            break;
        }
        (void)strlcpy(session->gpsdata.raw.meas[i].obs_code, obs_code,
                      sizeof(session->gpsdata.raw.meas[i].obs_code));

        session->gpsdata.raw.meas[i].svid = svId;
        session->gpsdata.raw.meas[i].freqid = freqId;
        session->gpsdata.raw.meas[i].snr = cno;
        session->gpsdata.raw.meas[i].satstat = trkStat;
        if (trkStat & 1) {
            /* prMes valid */
            session->gpsdata.raw.meas[i].pseudorange = prMes;
        } else {
            session->gpsdata.raw.meas[i].pseudorange = NAN;
        }
        if ((trkStat & 2) && (5 >= cpStdev)) {
            /* cpMes valid, RTKLIB uses 5 < cpStdev */
            session->gpsdata.raw.meas[i].carrierphase = cpMes;
        } else {
            session->gpsdata.raw.meas[i].carrierphase = NAN;
        }
        session->gpsdata.raw.meas[i].doppler = doMes;
        session->gpsdata.raw.meas[i].codephase = NAN;
        session->gpsdata.raw.meas[i].deltarange = NAN;
        session->gpsdata.raw.meas[i].locktime = locktime;
        if (0 == locktime) {
            /* possible slip */
            session->gpsdata.raw.meas[i].lli = 2;
        }
    }

    return RAW_IS;
}

/*
 * Raw Subframes - UBX-RXM-SFRB
 * Not in u-blox 8 or 9
 */
static gps_mask_t ubx_rxm_sfrb(struct gps_device_t *session,
                               unsigned char *buf, size_t data_len)
{
    unsigned int i, chan, svid;
    uint32_t words[10];

    if (42 > data_len) {
        GPSD_LOG(LOG_WARN, &session->context->errout,
                 "Runt RXM-SFRB message, payload len %zd", data_len);
        return 0;
    }

    chan = (unsigned int)getub(buf, 0);
    svid = (unsigned int)getub(buf, 1);
    GPSD_LOG(LOG_PROG, &session->context->errout,
             "UBX-RXM-SFRB: %u %u\n", chan, svid);

    /* UBX does all the parity checking, but still bad data gets through */
    for (i = 0; i < 10; i++) {
        words[i] = (uint32_t)getleu32(buf, 4 * i + 2) & 0xffffff;
    }

    return gpsd_interpret_subframe(session, svid, words);
}

/* UBX-INF-* */
static void ubx_msg_inf(struct gps_device_t *session, unsigned char *buf,
                        size_t data_len)
{
    unsigned short msgid;
    static char txtbuf[MAX_PACKET_LENGTH];

    /* No minimum payload length */

    msgid = (unsigned short)((buf[2] << 8) | buf[3]);
    if (data_len > MAX_PACKET_LENGTH - 1)
        data_len = MAX_PACKET_LENGTH - 1;

    (void)strlcpy(txtbuf, (char *)buf + UBX_PREFIX_LEN, sizeof(txtbuf));
    txtbuf[data_len] = '\0';
    switch (msgid) {
    case UBX_INF_DEBUG:
        GPSD_LOG(LOG_PROG, &session->context->errout, "UBX-INF-DEBUG: %s\n",
                 txtbuf);
        break;
    case UBX_INF_TEST:
        GPSD_LOG(LOG_PROG, &session->context->errout, "UBX-INF-TEST: %s\n",
                 txtbuf);
        break;
    case UBX_INF_NOTICE:
        GPSD_LOG(LOG_INF, &session->context->errout, "UBX-INF-NOTICE: %s\n",
                 txtbuf);
        break;
    case UBX_INF_WARNING:
        GPSD_LOG(LOG_WARN, &session->context->errout, "UBX-INF-WARNING: %s\n",
                 txtbuf);
        break;
    case UBX_INF_ERROR:
        GPSD_LOG(LOG_WARN, &session->context->errout, "UBX-INF-ERROR: %s\n",
                 txtbuf);
        break;
    default:
        break;
    }
    return;
}

/**
 * Time Pulse Timedata - UBX-TIM-TP
 */
static gps_mask_t
ubx_msg_tim_tp(struct gps_device_t *session, unsigned char *buf,
               size_t data_len)
{
    gps_mask_t mask = ONLINE_SET;
    uint32_t towMS;
    uint32_t towSubMS;
    int32_t qErr;
    uint16_t week;
    uint8_t flags;
    uint8_t refInfo;
    timespec_t ts_tow;

    if (16 > data_len) {
        GPSD_LOG(LOG_WARN, &session->context->errout,
                 "Runt TIM-TP message, payload len %zd", data_len);
        return 0;
    }

    towMS = getleu32(buf, 0);
    // towSubMS always seems zero, which will match the PPS
    towSubMS = getleu32(buf, 4);
    qErr = getles32(buf, 8);
    week = getleu16(buf, 12);
    flags = buf[14];
    refInfo = buf[15];

    /* are we UTC, and towSubMs is zero? */
    if (3 == (flags & 0x03) &&
        0 == towSubMS) {

        // leap already added!?!?
        int saved_leap = session->context->leap_seconds;
        // remove it!
        session->context->leap_seconds = 0;

        /* good, save qErr and qErr_time */
        session->gpsdata.qErr = qErr;
	MSTOTS(&ts_tow, towMS);
	session->gpsdata.qErr_time = gpsd_gpstime_resolv(session, week, ts_tow);

        // restore leap
        session->context->leap_seconds = saved_leap;

#ifdef __UNUSED
        {
	 struct gps_device_t *ppsonly;
         // FIXME!! should be up a layer so other drivers can use it
         // FIXME!! this qErr can only apply to one PPS!
	 /* propagate this in-band-time to all PPS-only devices */
	 for (ppsonly = devices; ppsonly < devices + MAX_DEVICES; ppsonly++)
	     if (ppsonly->sourcetype == source_pps)
		 pps_thread_qErrin(&ppsonly->pps_thread, qErr,
                                   session->gpsdata.qErr_time);
        }
#endif /* __UNUSED */

    }

    /* cast for 32 bit compatibility */
    GPSD_LOG(LOG_DATA, &session->context->errout,
             "TIM-TP: towMS %lu, towSubMS %lu, qErr %ld week %u "
             "flags %#x, refInfo %#x\n",
             (unsigned long)towMS, (unsigned long)towSubMS, (long)qErr,
              week, flags, refInfo);
    return mask;
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
    session->driver.ubx.iTOW = -1;        /* set by decoder */

    /* extract message id and length */
    msgid = (buf[2] << 8) | buf[3];
    data_len = (size_t) getles16(buf, 4);

    switch (msgid) {
    case UBX_ACK_ACK:
        if (2 <= data_len) {
            GPSD_LOG(LOG_DATA, &session->context->errout,
                     "UBX-ACK-ACK, class: %02x, id: %02x\n",
                     buf[UBX_MESSAGE_DATA_OFFSET],
                     buf[UBX_MESSAGE_DATA_OFFSET + 1]);
        }
        break;
    case UBX_ACK_NAK:
        if (2 <= data_len) {
            GPSD_LOG(LOG_WARN, &session->context->errout,
                     "UBX-ACK-NAK, class: %02x, id: %02x\n",
                     buf[UBX_MESSAGE_DATA_OFFSET],
                     buf[UBX_MESSAGE_DATA_OFFSET + 1]);
        }
        break;

    case UBX_CFG_PRT:
        if (session->driver.ubx.port_id != buf[UBX_MESSAGE_DATA_OFFSET + 0] ) {
            session->driver.ubx.port_id = buf[UBX_MESSAGE_DATA_OFFSET + 0];
            GPSD_LOG(LOG_INF, &session->context->errout,
                     "UBX-CFG-PRT: port %d\n", session->driver.ubx.port_id);

#ifdef RECONFIGURE_ENABLE
            /* Need to reinitialize since port changed */
            if (session->mode == O_OPTIMIZE) {
                ubx_mode(session, MODE_BINARY);
            } else {
                ubx_mode(session, MODE_NMEA);
            }
#endif /* RECONFIGURE_ENABLE */
        }
        break;

    case UBX_INF_DEBUG:
        /* FALLTHROUGH */
    case UBX_INF_ERROR:
        /* FALLTHROUGH */
    case UBX_INF_NOTICE:
        /* FALLTHROUGH */
    case UBX_INF_TEST:
        /* FALLTHROUGH */
    case UBX_INF_USER:
        /* FALLTHROUGH */
    case UBX_INF_WARNING:
        ubx_msg_inf(session, buf, data_len);
        break;

    case UBX_MON_EXCEPT:
        GPSD_LOG(LOG_DATA, &session->context->errout, "UBX-MON-EXCEPT\n");
        break;
    case UBX_MON_GNSS:
        GPSD_LOG(LOG_DATA, &session->context->errout, "UBX-MON-GNSS\n");
        break;
    case UBX_MON_HW:
        GPSD_LOG(LOG_DATA, &session->context->errout, "UBX-MON-HW\n");
        break;
    case UBX_MON_HW2:
        GPSD_LOG(LOG_DATA, &session->context->errout, "UBX-MON-HW2\n");
        break;
    case UBX_MON_IO:
        GPSD_LOG(LOG_DATA, &session->context->errout, "UBX-MON-IO\n");
        break;
    case UBX_MON_IPC:
        GPSD_LOG(LOG_DATA, &session->context->errout, "UBX-MON-IPC\n");
        break;
    case UBX_MON_MSGPP:
        GPSD_LOG(LOG_DATA, &session->context->errout, "UBX-MON-MSGPP\n");
        break;
    case UBX_MON_PATCH:
        GPSD_LOG(LOG_DATA, &session->context->errout, "UBX-MON-PATCH\n");
        break;
    case UBX_MON_RXBUF:
        GPSD_LOG(LOG_DATA, &session->context->errout, "UBX-MON-RXBUF\n");
        break;
    case UBX_MON_RXR:
        GPSD_LOG(LOG_DATA, &session->context->errout, "UBX-MON-RXR\n");
        break;
    case UBX_MON_SCHED:
        GPSD_LOG(LOG_DATA, &session->context->errout, "UBX-MON-SCHED\n");
        break;
    case UBX_MON_SMGR:
        GPSD_LOG(LOG_DATA, &session->context->errout, "UBX-MON-SMGR\n");
        break;
    case UBX_MON_TXBUF:
        GPSD_LOG(LOG_DATA, &session->context->errout, "UBX-MON-TXBUF\n");
        break;
    case UBX_MON_USB:
        GPSD_LOG(LOG_DATA, &session->context->errout, "UBX-MON-USB\n");
        break;
    case UBX_MON_VER:
        GPSD_LOG(LOG_DATA, &session->context->errout, "UBX-MON-VER\n");
        ubx_msg_mon_ver(session, buf, data_len);
        break;

    case UBX_NAV_AOPSTATUS:
        GPSD_LOG(LOG_DATA, &session->context->errout, "UBX-NAV-AOPSTATUS\n");
        break;
    case UBX_NAV_ATT:
        GPSD_LOG(LOG_DATA, &session->context->errout, "UBX-NAV-ATT\n");
        break;
    case UBX_NAV_CLOCK:
        GPSD_LOG(LOG_DATA, &session->context->errout, "UBX-NAV-CLOCK\n");
        break;
    case UBX_NAV_DGPS:
        GPSD_LOG(LOG_DATA, &session->context->errout, "UBX-NAV-DGPS\n");
        break;
    case UBX_NAV_DOP:
        /* DOP seems to be the last NAV sent in a cycle */
        GPSD_LOG(LOG_PROG, &session->context->errout, "UBX-NAV-DOP\n");
        mask = ubx_msg_nav_dop(session, &buf[UBX_PREFIX_LEN], data_len);
        break;
    case UBX_NAV_EKFSTATUS:
        GPSD_LOG(LOG_DATA, &session->context->errout, "UBX-NAV-EKFSTATUS\n");
        break;
    case UBX_NAV_EOE:
        GPSD_LOG(LOG_DATA, &session->context->errout, "UBX-NAV-EOE\n");
        mask = ubx_msg_nav_eoe(session, &buf[UBX_PREFIX_LEN], data_len);
        break;
    case UBX_NAV_GEOFENCE:
        GPSD_LOG(LOG_DATA, &session->context->errout, "UBX-NAV-GEOFENCE\n");
        break;
    case UBX_NAV_HPPOSECEF:
        GPSD_LOG(LOG_DATA, &session->context->errout, "UBX-NAV-HPPOSECEF\n");
        mask = ubx_msg_nav_hpposecef(session, &buf[UBX_PREFIX_LEN], data_len);
        break;
    case UBX_NAV_HPPOSLLH:
        GPSD_LOG(LOG_DATA, &session->context->errout, "UBX-NAV-HPPOSLLH\n");
        mask = ubx_msg_nav_hpposllh(session, &buf[UBX_PREFIX_LEN], data_len);
        break;
    case UBX_NAV_ODO:
        GPSD_LOG(LOG_DATA, &session->context->errout, "UBX-NAV-ODO\n");
        break;
    case UBX_NAV_ORB:
        GPSD_LOG(LOG_DATA, &session->context->errout, "UBX-NAV-ORB\n");
        break;
    case UBX_NAV_POSECEF:
        GPSD_LOG(LOG_DATA, &session->context->errout, "UBX-NAV-POSECEF\n");
        mask = ubx_msg_nav_posecef(session, &buf[UBX_PREFIX_LEN], data_len);
        break;
    case UBX_NAV_POSLLH:
        GPSD_LOG(LOG_DATA, &session->context->errout, "UBX-NAV-POSLLH\n");
        mask = ubx_msg_nav_posllh(session, &buf[UBX_PREFIX_LEN], data_len);
        break;
    case UBX_NAV_POSUTM:
        GPSD_LOG(LOG_DATA, &session->context->errout, "UBX-NAV-POSUTM\n");
        break;
    case UBX_NAV_PVT:
        GPSD_LOG(LOG_DATA, &session->context->errout, "UBX-NAV-PVT\n");
        mask = ubx_msg_nav_pvt(session, &buf[UBX_PREFIX_LEN], data_len);
        mask |= REPORT_IS;
        break;
    case UBX_NAV_RELPOSNED:
        GPSD_LOG(LOG_DATA, &session->context->errout, "UBX-NAV-RELPOSNED\n");
        mask = ubx_msg_nav_relposned(session, &buf[UBX_PREFIX_LEN], data_len);
        break;
    case UBX_NAV_RESETODO:
        GPSD_LOG(LOG_DATA, &session->context->errout, "UBX-NAV-RESETODO\n");
        break;
    case UBX_NAV_SIG:
        GPSD_LOG(LOG_DATA, &session->context->errout, "UBX-NAV-SIG\n");
        break;
    case UBX_NAV_SAT:
        GPSD_LOG(LOG_DATA, &session->context->errout, "UBX-NAV-SAT\n");
        mask = ubx_msg_nav_sat(session, &buf[UBX_PREFIX_LEN], data_len);
        break;
    case UBX_NAV_SBAS:
        GPSD_LOG(LOG_DATA, &session->context->errout, "UBX-NAV-SBAS\n");
        ubx_msg_sbas(session, &buf[UBX_PREFIX_LEN], data_len);
        break;
    case UBX_NAV_SOL:
        /* UBX-NAV-SOL deprecated in u-blox 6, gone in u-blox 9.
         * Use UBX-NAV-PVT instead */
        GPSD_LOG(LOG_PROG, &session->context->errout, "UBX-NAV-SOL\n");
        mask = ubx_msg_nav_sol(session, &buf[UBX_PREFIX_LEN], data_len);
        mask |= REPORT_IS;
        break;
    case UBX_NAV_STATUS:
        GPSD_LOG(LOG_DATA, &session->context->errout, "UBX-NAV-STATUS\n");
        break;
    case UBX_NAV_SVIN:
        GPSD_LOG(LOG_DATA, &session->context->errout, "UBX-NAV-SVIN\n");
        break;
    case UBX_NAV_SVINFO:
        /* UBX-NAV-SVINFO deprecated, use UBX-NAV-SAT instead */
        GPSD_LOG(LOG_PROG, &session->context->errout, "UBX-NAV-SVINFO\n");
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
    case UBX_NAV_TIMEBDS:
        GPSD_LOG(LOG_DATA, &session->context->errout, "UBX-NAV-TIMEBDS\n");
        break;
    case UBX_NAV_TIMEGAL:
        GPSD_LOG(LOG_DATA, &session->context->errout, "UBX-NAV-TIMEGAL\n");
        break;
    case UBX_NAV_TIMEGLO:
        GPSD_LOG(LOG_DATA, &session->context->errout, "UBX-NAV-TIMEGLO\n");
        break;
    case UBX_NAV_TIMEGPS:
        GPSD_LOG(LOG_PROG, &session->context->errout, "UBX-NAV-TIMEGPS\n");
        mask = ubx_msg_nav_timegps(session, &buf[UBX_PREFIX_LEN], data_len);
        break;
    case UBX_NAV_TIMELS:
        ubx_msg_nav_timels(session, &buf[UBX_PREFIX_LEN], data_len);
        break;
    case UBX_NAV_TIMEUTC:
        GPSD_LOG(LOG_DATA, &session->context->errout, "UBX-NAV-TIMEUTC\n");
        break;
    case UBX_NAV_VELECEF:
        GPSD_LOG(LOG_DATA, &session->context->errout, "UBX-NAV-VELECEF\n");
        mask = ubx_msg_nav_velecef(session, &buf[UBX_PREFIX_LEN], data_len);
        break;
    case UBX_NAV_VELNED:
        GPSD_LOG(LOG_DATA, &session->context->errout, "UBX-NAV-VELNED\n");
        mask = ubx_msg_nav_velned(session, &buf[UBX_PREFIX_LEN], data_len);
        break;

    case UBX_RXM_ALM:
        GPSD_LOG(LOG_DATA, &session->context->errout, "UBX-RXM-ALM\n");
        break;
    case UBX_RXM_EPH:
        GPSD_LOG(LOG_DATA, &session->context->errout, "UBX-RXM-EPH\n");
        break;
    case UBX_RXM_IMES:
        GPSD_LOG(LOG_DATA, &session->context->errout, "UBX-RXM-IMES\n");
        break;
    case UBX_RXM_MEASX:
        GPSD_LOG(LOG_PROG, &session->context->errout, "UBX-RXM-MEASX\n");
        break;
    case UBX_RXM_PMREQ:
        GPSD_LOG(LOG_DATA, &session->context->errout, "UBX-RXM-PMREQ\n");
        break;
    case UBX_RXM_POSREQ:
        GPSD_LOG(LOG_DATA, &session->context->errout, "UBX-RXM-POSREQ\n");
        break;
    case UBX_RXM_RAW:
        GPSD_LOG(LOG_DATA, &session->context->errout, "UBX-RXM-RAW\n");
        break;
    case UBX_RXM_RAWX:
        mask = ubx_rxm_rawx(session, &buf[UBX_PREFIX_LEN], data_len);
        break;
    case UBX_RXM_RLM:
        GPSD_LOG(LOG_DATA, &session->context->errout, "UBX-RXM-RLM\n");
        break;
    case UBX_RXM_RTCM:
        GPSD_LOG(LOG_DATA, &session->context->errout, "UBX-RXM-RTCM\n");
        break;
    case UBX_RXM_SFRB:
        mask = ubx_rxm_sfrb(session, &buf[UBX_PREFIX_LEN], data_len);
        break;
    case UBX_RXM_SFRBX:
        GPSD_LOG(LOG_PROG, &session->context->errout, "UBX-RXM-SFRBX\n");
        break;
    case UBX_RXM_SVSI:
        GPSD_LOG(LOG_PROG, &session->context->errout, "UBX-RXM-SVSI\n");
        break;

    case UBX_TIM_DOSC:
        GPSD_LOG(LOG_DATA, &session->context->errout, "UBX-TIM-DOSC\n");
        break;
    case UBX_TIM_FCHG:
        GPSD_LOG(LOG_DATA, &session->context->errout, "UBX-TIM-FCHG\n");
        break;
    case UBX_TIM_HOC:
        GPSD_LOG(LOG_DATA, &session->context->errout, "UBX-TIM-HOC\n");
        break;
    case UBX_TIM_SMEAS:
        GPSD_LOG(LOG_DATA, &session->context->errout, "UBX-TIM-SMEAS\n");
        break;
    case UBX_TIM_SVIN:
        GPSD_LOG(LOG_DATA, &session->context->errout, "UBX-TIM-SVIN\n");
        break;
    case UBX_TIM_TM:
        GPSD_LOG(LOG_DATA, &session->context->errout, "UBX-TIM-TM\n");
        break;
    case UBX_TIM_TM2:
        GPSD_LOG(LOG_DATA, &session->context->errout, "UBX-TIM-TM2\n");
        break;
    case UBX_TIM_TP:
        mask = ubx_msg_tim_tp(session, &buf[UBX_PREFIX_LEN], data_len);
        break;
    case UBX_TIM_TOS:
        GPSD_LOG(LOG_DATA, &session->context->errout, "UBX-TIM-TOS\n");
        break;
    case UBX_TIM_VCOCAL:
        GPSD_LOG(LOG_DATA, &session->context->errout, "UBX-TIM-VCOCAL\n");
        break;
    case UBX_TIM_VRFY:
        GPSD_LOG(LOG_DATA, &session->context->errout, "UBX-TIM-VRFY\n");
        break;

    default:
        GPSD_LOG(LOG_WARN, &session->context->errout,
                 "UBX: unknown packet id 0x%04hx (length %zd)\n",
                 msgid, len);
    }
    /* end of cycle ? */
    if (session->driver.ubx.end_msgid == msgid) {
        /* end of cycle, report it */
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "UBX: cycle end %x\n", msgid);
        mask |= REPORT_IS;
    }
    /* start of cycle ? */
    if ( -1 < session->driver.ubx.iTOW) {
        /* this sentence has a good time */
        /* debug
        GPSD_LOG(LOG_ERROR, &session->context->errout,
                 "UBX:      time %.2f      msgid %x\n",
                 session->newdata.time, msgid);
        GPSD_LOG(LOG_ERROR, &session->context->errout,
                 "     last_time %s last_msgid %x\n",
                 timespec_str(&session->driver.ubx.last_time, ts_buf,
                              sizeof(ts_buf)),
                 session->driver.ubx.last_msgid);
         */
        /* iTOW is to ms, can go forward or backwards */
        if ((session->driver.ubx.last_iTOW != session->driver.ubx.iTOW) &&
            (session->driver.ubx.end_msgid !=
             session->driver.ubx.last_msgid)) {
            /* time changed, new cycle ender */
            session->driver.ubx.end_msgid = session->driver.ubx.last_msgid;
            session->driver.ubx.last_iTOW = session->driver.ubx.iTOW;
            /* debug
            GPSD_LOG(LOG_ERROR, &session->context->errout,
                     "UBX: new ender %x, iTOW %.2f\n",
                     session->driver.ubx.end_msgid, iTOW);
             */
        }
        session->driver.ubx.last_msgid = msgid;
        /* FIXME: last_time never used... */
        session->driver.ubx.last_time = session->newdata.time;
    } else {
        /* no time */
        /* debug
        GPSD_LOG(LOG_ERROR, &session->context->errout,
                 "UBX: No time, msgid %x\n", msgid);
         */
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

    /* do not write if -b (readonly) option set */
    if (session->context->readonly)
        return true;

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


    GPSD_LOG(LOG_PROG, &session->context->errout,
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
    /* UBX-MON-VER: query for version information */
    (void)ubx_write(session, UBX_CLASS_MON, 0x04, NULL, 0);
}

static void ubx_event_hook(struct gps_device_t *session, event_t event)
{
    if (session->context->readonly)
        return;
    else if (event == event_identified) {
        GPSD_LOG(LOG_DATA, &session->context->errout, "UBX identified\n");

        /* no longer set UBX-CFG-SBAS here, u-blox 9 does not have it */

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
        /* There used to be a hotstart/reset here.
         * That caused u-blox USB to re-enumerate.
         * Sometimes to a new device name.
         * Bad.  Don't do that anymore...
         */
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
    /* FIXME!  Bad test, port_id == 0 is valid too.  DDC (I2X) = port 0 */
    if (session->driver.ubx.port_id != 0) {
        buf[0] = session->driver.ubx.port_id;
    }
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
     * has port ID 3 the way it "ought" to.
     */
    else if (strstr(session->gpsdata.dev.path, "/ACM") != NULL) {
        /* using the built in USB port */
        session->driver.ubx.port_id = buf[0] = USB_ID;
    } else {
        /* A guess.  Could be UART2, or SPI, or DDC port */
        session->driver.ubx.port_id = buf[0] = USART1_ID;
    }

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
     *   prtcfg.mode = (1<<4) | // compatibility with ANTARIS 4
     *                 (1<<7) | // charLen = 11 = 8 bit
     *                 (1<<6) | // charLen = 11 = 8 bit
     *                 (1<<11); // parity = 10x = none
     */
    usart_mode |= (1<<4);       /* reserved1 Antaris 4 compatibility bit */
    usart_mode |= (1<<7);       /* high bit of charLen */

    /* u-blox 5+ binary only supports 8N1 */
    switch (parity) {
    case (int)'E':
    case 2:
        usart_mode |= (1<<7);           /* 7E */
        break;
    case (int)'O':
    case 1:
        usart_mode |= (1<<9) | (1<<7);  /* 7O */
        break;
    case (int)'N':
    case 0:
    default:
        usart_mode |= (1<<11) | (3<<6); /* 8N */
        break;
    }

    if (stopbits == 2)
        usart_mode |= (1<<13);

    putle32(buf, 4, usart_mode);

    /* enable all input protocols by default */
    /* FIXME!  RTCM3 needs to be set too */
    buf[12] = NMEA_PROTOCOL_MASK | UBX_PROTOCOL_MASK | RTCM_PROTOCOL_MASK;

    /* FIXME?  RTCM/RTCM3 needs to be set too? */
    buf[outProtoMask] = (mode == MODE_NMEA
                         ? NMEA_PROTOCOL_MASK : UBX_PROTOCOL_MASK);
    (void)ubx_write(session, 0x06u, 0x00, buf, sizeof(buf));

    GPSD_LOG(LOG_DATA, &session->context->errout,
        "UBX ubx_cfg_prt mode:%d, port:%d\n", mode, buf[0]);

    /* selectively enable output protocols */
    if (mode == MODE_NMEA) {
        /*
         * We have to club the GR601-W over the head to make it stop emitting
         * UBX after we've told it to start. Turning off the UBX protocol
         * mask, by itself, seems to be ineffective.
         */

        unsigned char msg[3];

        msg[0] = 0x01;          /* class */
        msg[1] = 0x04;          /* msg id  = UBX-NAV-DOP */
        msg[2] = 0x00;          /* rate */
        (void)ubx_write(session, 0x06u, 0x01, msg, 3);

        /* UBX-NAV-SOL deprecated in u-blox 6, gone in u-blox 9.
         * UBX-NAV-PVT for later models.  Turn both off */
        msg[0] = 0x01;          /* class */
        msg[1] = 0x06;          /* msg id  = NAV-SOL */
        msg[2] = 0x00;          /* rate */
        (void)ubx_write(session, 0x06u, 0x01, msg, 3);

        msg[0] = 0x01;          /* class */
        msg[1] = 0x07;          /* msg id  = NAV-PVT */
        msg[2] = 0x00;          /* rate */
        (void)ubx_write(session, 0x06u, 0x01, msg, 3);

        msg[0] = 0x01;          /* class */
        msg[1] = 0x20;          /* msg id  = UBX-NAV-TIMEGPS */
        msg[2] = 0x00;          /* rate */
        (void)ubx_write(session, 0x06u, 0x01, msg, 3);

        /* NAV-SVINFO became NAV-SAT */
        msg[0] = 0x01;          /* class */
        msg[1] = 0x30;          /* msg id  = NAV-SVINFO */
        msg[2] = 0x00;          /* rate */
        (void)ubx_write(session, 0x06u, 0x01, msg, 3);
        msg[0] = 0x01;          /* class */
        msg[1] = 0x35;          /* msg id  = NAV-SAT */
        msg[2] = 0x00;          /* rate */
        (void)ubx_write(session, 0x06u, 0x01, msg, 3);

        msg[0] = 0x01;          /* class */
        msg[1] = 0x32;          /* msg id  = NAV-SBAS */
        msg[2] = 0x00;          /* rate */
        (void)ubx_write(session, 0x06u, 0x01, msg, 3);

        /* UBX-NAV-EOE */
        msg[0] = 0x01;          /* class */
        msg[1] = 0x61;          /* msg id  = NAV-EOE */
        msg[2] = 0x00;          /* rate */
        (void)ubx_write(session, 0x06u, 0x01, msg, 3);

        /* try to improve the sentence mix. in particular by enabling ZDA */
        msg[0] = 0xf0;          /* class */
        msg[1] = 0x09;          /* msg id  = GBS */
        msg[2] = 0x01;          /* rate */
        (void)ubx_write(session, 0x06u, 0x01, msg, 3);
        msg[0] = 0xf0;          /* class */
        msg[1] = 0x00;          /* msg id  = GGA */
        msg[2] = 0x01;          /* rate */
        (void)ubx_write(session, 0x06u, 0x01, msg, 3);
        msg[0] = 0xf0;          /* class */
        msg[1] = 0x02;          /* msg id  = GSA */
        msg[2] = 0x01;          /* rate */
        (void)ubx_write(session, 0x06u, 0x01, msg, 3);
        msg[0] = 0xf0;          /* class */
        msg[1] = 0x07;          /* msg id  = GST */
        msg[2] = 0x01;          /* rate */
        (void)ubx_write(session, 0x06u, 0x01, msg, 3);
        msg[0] = 0xf0;          /* class */
        msg[1] = 0x03;          /* msg id  = GSV */
        msg[2] = 0x01;          /* rate */
        (void)ubx_write(session, 0x06u, 0x01, msg, 3);
        msg[0] = 0xf0;          /* class */
        msg[1] = 0x04;          /* msg id  = RMC */
        msg[2] = 0x01;          /* rate */
        (void)ubx_write(session, 0x06u, 0x01, msg, 3);
        msg[0] = 0xf0;          /* class */
        msg[1] = 0x05;          /* msg id  = VTG */
        msg[2] = 0x01;          /* rate */
        (void)ubx_write(session, 0x06u, 0x01, msg, 3);
        msg[0] = 0xf0;          /* class */
        msg[1] = 0x08;          /* msg id  = ZDA */
        msg[2] = 0x01;          /* rate */
        (void)ubx_write(session, 0x06u, 0x01, msg, 3);
    } else { /* MODE_BINARY */
        /*
         * Just enabling the UBX protocol for output is not enough to
         * actually get UBX output; the sentence mix is initially empty.
         * Fix that...
         */

        /* FIXME: possibly sending too many messages without waiting
         * for u-blox ACK, over running its input buffer.
         *
         * for example, the UBX-MON-VER fails here, but works in other
         * contexts
         */
        unsigned char msg[3] = {0, 0, 0};
        /* request SW and HW Versions */
        (void)ubx_write(session, UBX_CLASS_MON, 0x04, msg, 0);

        msg[0] = 0x01;          /* class */
        msg[1] = 0x04;          /* msg id  = UBX-NAV-DOP */
        msg[2] = 0x01;          /* rate */
        (void)ubx_write(session, 0x06u, 0x01, msg, 3);

        /* UBX-NAV-SOL deprecated in u-blox 6, gone in u-blox 9.
         * Use UBX-NAV-PVT after u-blox 7 */
        if (10 > session->driver.ubx.protver) {
            /* unknown version, enable both */
            msg[0] = 0x01;              /* class */
            msg[1] = 0x06;              /* msg id  = NAV-SOL */
            msg[2] = 0x01;              /* rate */
            (void)ubx_write(session, 0x06u, 0x01, msg, 3);
            msg[0] = 0x01;              /* class */
            msg[1] = 0x07;              /* msg id  = NAV-PVT */
            msg[2] = 0x01;              /* rate */
            (void)ubx_write(session, 0x06u, 0x01, msg, 3);
        } else if (15 > session->driver.ubx.protver) {
            /* before u-blox 8, just NAV-SOL */
            /* do not do both to avoid NACKs */
            msg[0] = 0x01;              /* class */
            msg[1] = 0x06;              /* msg id  = NAV-SOL */
            msg[2] = 0x01;              /* rate */
            (void)ubx_write(session, 0x06u, 0x01, msg, 3);
        } else {
            /* u-blox 8 or later */
            msg[0] = 0x01;              /* class */
            msg[1] = 0x07;              /* msg id  = NAV-PVT */
            msg[2] = 0x01;              /* rate */
            (void)ubx_write(session, 0x06u, 0x01, msg, 3);
        }

        /* UBX-NAV-TIMEGPS is a great cycle ender */
        msg[0] = 0x01;          /* class */
        msg[1] = 0x20;          /* msg id  = UBX-NAV-TIMEGPS */
        msg[2] = 0x01;          /* rate */
        (void)ubx_write(session, 0x06u, 0x01, msg, 3);

        /* UBX-NAV-SVINFO deprecated in u-blox 8, gone in u-blox 9.
         * Use UBX-NAV-SAT after u-blox 7 */
        if (10 > session->driver.ubx.protver) {
            /* unknown version, enable both */
            msg[0] = 0x01;              /* class */
            msg[1] = 0x30;              /* msg id  = NAV-SVINFO */
            msg[2] = 0x0a;              /* rate */
            (void)ubx_write(session, 0x06u, 0x01, msg, 3);
            msg[0] = 0x01;              /* class */
            msg[1] = 0x35;              /* msg id  = NAV-SAT */
            msg[2] = 0x0a;              /* rate */
            (void)ubx_write(session, 0x06u, 0x01, msg, 3);
        } else if (15 > session->driver.ubx.protver) {
            /* before u-blox 8, just NAV-SVINFO */
            /* do not do both to avoid NACKs */
            msg[0] = 0x01;              /* class */
            msg[1] = 0x30;              /* msg id  = NAV-SVINFO */
            msg[2] = 0x0a;              /* rate */
            (void)ubx_write(session, 0x06u, 0x01, msg, 3);
        } else {
            /* u-blox 8 or later */
            msg[0] = 0x01;              /* class */
            msg[1] = 0x35;              /* msg id  = NAV-SAT */
            msg[2] = 0x0a;              /* rate */
            (void)ubx_write(session, 0x06u, 0x01, msg, 3);
        }

        if (24 > session->driver.ubx.protver) {
            /* Gone after u-blox 8 */
            msg[0] = 0x01;              /* class */
            msg[1] = 0x32;              /* msg id  = NAV-SBAS */
            msg[2] = 0x0a;              /* rate */
            (void)ubx_write(session, 0x06u, 0x01, msg, 3);
        }

        msg[0] = 0x01;          /* class */
        msg[1] = 0x01;          /* msg id  = UBX-NAV-POSECEF */
        msg[2] = 0x01;          /* rate */
        (void)ubx_write(session, 0x06u, 0x01, msg, 3);

        msg[0] = 0x01;          /* class */
        msg[1] = 0x11;          /* msg id  = UBX-NAV-VELECEF */
        msg[2] = 0x01;          /* rate */
        (void)ubx_write(session, 0x06u, 0x01, msg, 3);

        msg[0] = 0x01;          /* class */
        msg[1] = 0x26;          /* msg id  = UBX-NAV-TIMELS */
        msg[2] = 0xff;          /* about every 4 minutes if nav rate is 1Hz */
        (void)ubx_write(session, 0x06, 0x01, msg, 3);

        if (18 <= session->driver.ubx.protver) {
            /* first in u-blox 8 */
            /* UBX-NAV-EOE makes a good cycle ender */
            msg[0] = 0x01;              /* class */
            msg[1] = 0x61;              /* msg id  = NAV-EOE */
            msg[2] = 0x00;              /* rate */
            (void)ubx_write(session, 0x06u, 0x01, msg, 3);
        }
    }
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
        0x00, 0x00,     /* U2: Measurement rate (ms) */
        0x00, 0x01,     /* U2: Navigation rate (cycles) */
        0x00, 0x00,     /* U2: Alignment to reference time: 0 = UTC, !0 = GPS */
    };

    /* clamp to cycle times that i know work on my receiver */
    if (cycletime > 1000.0)
        cycletime = 1000.0;
    if (cycletime < 200.0)
        cycletime = 200.0;

    GPSD_LOG(LOG_DATA, &session->context->errout,
             "UBX rate change, report every %f secs\n", cycletime);
    s = (unsigned short)cycletime;
    msg[0] = (unsigned char)(s >> 8);
    msg[1] = (unsigned char)(s & 0xff);

    return ubx_write(session, 0x06, 0x08, msg, 6);      /* CFG-RATE */
}
#endif /* RECONFIGURE_ENABLE */

/* This is everything we export */
/* *INDENT-OFF* */
const struct gps_type_t driver_ubx = {
    .type_name        = "u-blox",    /* Full name of type */
    .packet_type      = UBX_PACKET,     /* associated lexer packet type */
    .flags            = DRIVER_STICKY,  /* remember this */
    .trigger          = NULL,
    /* Number of satellite channels supported by the device */
    .channels         = 50,
    .probe_detect     = NULL,           /* Startup-time device detector */
    /* Packet getter (using default routine) */
    .get_packet       = generic_get,
    .parse_packet     = parse_input,    /* Parse message packets */
     /* RTCM handler (using default routine) */
    .rtcm_writer      = gpsd_write,
    .init_query       = ubx_init_query, /* non-perturbing initial query */
    .event_hook       = ubx_event_hook, /* Fire on various lifetime events */
#ifdef RECONFIGURE_ENABLE
    .speed_switcher   = ubx_speed,      /* Speed (baudrate) switch */
    .mode_switcher    = ubx_mode,       /* Mode switcher */
    .rate_switcher    = ubx_rate,       /* Message delivery rate switcher */
    .min_cycle.tv_sec  = 0,		/* not relevant, no rate switch */
    .min_cycle.tv_nsec = 250000000,     /* Maximum 4Hz sample rate */
#endif /* RECONFIGURE_ENABLE */
#ifdef CONTROLSEND_ENABLE
    .control_send     = ubx_control_send,/* how to send a control string */
#endif /* CONTROLSEND_ENABLE */
    .time_offset     = NULL,            /* no method for NTP fudge factor */
};
/* *INDENT-ON* */
#endif /* defined(UBLOX_ENABLE) && defined(BINARY_ENABLE) */
