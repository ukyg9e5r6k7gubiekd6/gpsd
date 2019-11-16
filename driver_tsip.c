/*
 * Handle the Trimble TSIP packet format
 * by Rob Janssen, PE1CHL.
 * Acutime Gold support by Igor Socec <igorsocec@gmail.com>
 * Trimble RES multi-constelation support by Nuno Goncalves <nunojpg@gmail.com>
 *
 * Week counters are not limited to 10 bits. It's unknown what
 * the firmware is doing to disambiguate them, if anything; it might just
 * be adding a fixed offset based on a hidden epoch value, in which case
 * unhappy things will occur on the next rollover.
 *
 * This file is Copyright (c) 2010-2019 by the GPSD project
 * SPDX-License-Identifier: BSD-2-clause
 */

#include "gpsd_config.h"  /* must be before all includes */

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "gpsd.h"
#include "bits.h"
#include "strfuncs.h"
#include "timespec.h"

#ifdef TSIP_ENABLE
// RES SMT 360 has 32 max channels, use 64 for next gen
#define TSIP_CHANNELS  64

/* defines for Set or Request I/O Options (0x35)
 * SMT 360 default: IO1_DP|IO1_LLA, IO2_ENU, 0, IO4_DBHZ */
// byte 1 Position
#define IO1_ECEF 1
#define IO1_LLA 2
#define IO1_MSL 4
#define IO1_DP 0x10
// IO1_8F20 not in SMT 360
#define IO1_8F20 0x20
// byte 2 Velocity
#define IO2_VECEF 1
#define IO2_ENU 2
// byte 3 Timing
#define IO3_UTC 1
// byte 4 Aux/Reserved
#define IO4_RAW 1
#define IO4_DBHZ 8

#define SEMI_2_DEG      (180.0 / 2147483647)    /* 2^-31 semicircle to deg */

void configuration_packets_acutime_gold(struct gps_device_t *session);
void configuration_packets_res360(struct gps_device_t *session);
void configuration_packets_generic(struct gps_device_t *session);

/* convert TSIP SV Type to satellite_t.gnssid and satellite_t.svid
 * return gnssid directly, svid indirectly through pointer */
static unsigned char tsip_gnssid(unsigned svtype, short prn,
                                 unsigned char *svid)
{
    unsigned char gnssid;

    *svid = 0;

    switch (svtype) {
    case 0:
        if (0 < prn && 33 > prn) {
            gnssid = GNSSID_GPS;
            *svid = prn;
        } else if (32 < prn && 55 > prn) {
            // RES SMT 360 and ICM SMT 360 put SBAS in 33-54
            gnssid = GNSSID_SBAS;
            *svid = prn + 87;
        } else if (64 < prn && 97 > prn) {
            // RES SMT 360 and ICM SMT 360 put GLONASS in 65-96
            gnssid = GNSSID_GLO;
            *svid = prn - 64;
        } else if (96 < prn && 134 > prn) {
            // RES SMT 360 and ICM SMT 360 put Galileo in 97-133
            gnssid = GNSSID_GAL;
            *svid = prn - 96;
        } else if (119 < prn && 139 > prn) {
            // Copernicus (II) put SBAS in 120-138
            gnssid = GNSSID_SBAS;
            *svid = prn + 87;
        } else if (183 == prn) {
            gnssid = GNSSID_QZSS;
            *svid = 1;
        } else if (192 >= prn && 193 >= prn) {
            gnssid = GNSSID_QZSS;
            *svid = prn - 190;
        } else if (200 == prn) {
            gnssid = GNSSID_QZSS;
            *svid = 4;
        } else if (200 < prn && 238 > prn) {
            // BeidDou in 201-237
            gnssid = GNSSID_BD;
            *svid = prn - 200;
        }
        // else: huh?
        break;
    case 1:
        gnssid = GNSSID_GLO;  // GLONASS
        *svid = prn - 64;
        break;
    case 2:
        gnssid = GNSSID_BD;  // BeiDou
        *svid = prn - 200;
        break;
    case 3:
        gnssid = GNSSID_GAL;  // Galileo
        *svid = prn - 96;
        break;
    case 5:
        gnssid = GNSSID_QZSS;  // QZSS
        switch (prn) {
        case 183:
            *svid = 1;
            break;
        case 192:
            *svid = 2;
            break;
        case 193:
            *svid = 3;
            break;
        case 200:
            *svid = 4;
            break;
        default:
            *svid = prn;
            break;
        }
        break;
    case 4:
        // FALLTHROUGH
    case 6:
        // FALLTHROUGH
    case 7:
        // FALLTHROUGH
    default:
        svid = 0;
        gnssid = 0;
        break;
    }
    return gnssid;
}

static int tsip_write(struct gps_device_t *session,
                      unsigned int id, unsigned char *buf, size_t len)
{
    char *ep, *cp;
    char obuf[100];
    size_t olen = len;

    session->msgbuf[0] = '\x10';
    session->msgbuf[1] = (char)id;
    ep = session->msgbuf + 2;
    for (cp = (char *)buf; olen-- > 0; cp++) {
        if (*cp == '\x10')
            *ep++ = '\x10';
        *ep++ = *cp;
    }
    *ep++ = '\x10';
    *ep++ = '\x03';
    session->msgbuflen = (size_t) (ep - session->msgbuf);
    GPSD_LOG(LOG_PROG, &session->context->errout,
             "TSIP: Sent packet id 0x %s\n",
             gpsd_hexdump(obuf, sizeof(obuf), &session->msgbuf[1], len + 1));
    if (gpsd_write(session, session->msgbuf, session->msgbuflen) !=
        (ssize_t) session->msgbuflen)
        return -1;

    return 0;
}

/* tsip_detect()
 *
 * see if it looks like a TSIP device (speaking 9600O81) is listening and
 * return 1 if found, 0 if not
 */
static bool tsip_detect(struct gps_device_t *session)
{
    char buf[BUFSIZ];
    bool ret = false;
    int myfd;
    speed_t old_baudrate;
    char old_parity;
    unsigned int old_stopbits;

    old_baudrate = session->gpsdata.dev.baudrate;
    old_parity = session->gpsdata.dev.parity;
    old_stopbits = session->gpsdata.dev.stopbits;
    // FIXME.  Should respect fixed speed/framing
    gpsd_set_speed(session, 9600, 'O', 1);

    /* request firmware revision and look for a valid response */
    putbyte(buf, 0, 0x10);
    putbyte(buf, 1, 0x1f);
    putbyte(buf, 2, 0x10);
    putbyte(buf, 3, 0x03);
    myfd = session->gpsdata.gps_fd;
    if (write(myfd, buf, 4) == 4) {
        unsigned int n;
        for (n = 0; n < 3; n++) {
            if (!nanowait(myfd, NS_IN_SEC))
                break;
            if (generic_get(session) >= 0) {
                if (session->lexer.type == TSIP_PACKET) {
                    GPSD_LOG(LOG_RAW, &session->context->errout,
                             "TSIP: tsip_detect found\n");
                    ret = true;
                    break;
                }
            }
        }
    }

    if (!ret)
        /* return serial port to original settings */
        gpsd_set_speed(session, old_baudrate, old_parity, old_stopbits);

    return ret;
}

/* This is the meat of parsing all the TSIP packets */
static gps_mask_t tsip_parse_input(struct gps_device_t *session)
{
    int i, j, len, count;
    gps_mask_t mask = 0;
    unsigned int id;
    unsigned short week;
    uint8_t u1, u2, u3, u4, u5, u6, u7, u8, u9, u10;
    int16_t s1, s2, s3, s4;
    int32_t sl1, sl2, sl3;
    uint32_t ul1, ul2;
    float f1, f2, f3, f4;
    double d1, d2, d3, d4, d5;
    time_t now;
    unsigned char buf[BUFSIZ];
    char buf2[BUFSIZ];
    uint32_t tow;             // time of week in milli seconds
    double ftow;              // time of week in seconds
    double temp;              // tempurature in degrees C
    double fqErr;             // PPS Offset. positive is slow.
    timespec_t ts_tow;
    char ts_buf[TIMESPEC_LEN];
    int bad_len = 0;
    const char *name;

    if (session->lexer.type != TSIP_PACKET) {
        // this should not happen
        GPSD_LOG(LOG_INF, &session->context->errout,
                 "TSIP: tsip_analyze packet type %d\n",
                 session->lexer.type);
        return 0;
    }

    if (session->lexer.outbuflen < 4 || session->lexer.outbuffer[0] != 0x10) {
        /* packet too short, or does not start with DLE */
        GPSD_LOG(LOG_INF, &session->context->errout,
                 "TSIP: tsip_analyze packet bad packet\n");
        return 0;
    }

    // get receive time, first
    (void)time(&now);

    /* remove DLE stuffing and put data part of message in buf */

    memset(buf, 0, sizeof(buf));
    buf2[len = 0] = '\0';
    for (i = 2; i < (int)session->lexer.outbuflen; i++) {
        if (session->lexer.outbuffer[i] == 0x10)
            if (session->lexer.outbuffer[++i] == 0x03)
                break;

        // FIXME  expensive way to do hex
        str_appendf(buf2, sizeof(buf2),
                       "%02x", buf[len++] = session->lexer.outbuffer[i]);
    }

    id = (unsigned)session->lexer.outbuffer[1];
    GPSD_LOG(LOG_DATA, &session->context->errout,
             "TSIP: got packet id 0x%02x length %d: %s\n",
             id, len, buf2);

    // session->cycle_end_reliable = true;
    switch (id) {
    case 0x13:
        /* Packet Received
         * Present in:
         *   pre-2000 models
         *   ICM SMT 360 (2018)
         *   RES SMT 360 (2018)
         * Not present in:
         *   Copernicus II
         */
        if (1 > len) {
            bad_len = 1;
            break;
        }
        u1 = getub(buf, 0);     // Packet ID of non-parsable packet
        if (2 <= len) {
            u2 = getub(buf, 1);     // Data byte 0 of non-parsable packet
        } else {
            u2 = 0;
        }
        GPSD_LOG(LOG_WARN, &session->context->errout,
                 "TSIP: Report Packet (0x13): type x%02x %02x "
                 "cannot be parsed\n",
                 u1, u2);
        // ignore the rest of the bad data
        if ((int)u1 == 0x8e && (int)u2 == 0x23) {
            /* no Compact Super Packet 0x8e-23 */
            GPSD_LOG(LOG_WARN, &session->context->errout,
                     "TSIP: No 0x8e-23, use LFwEI (0x8f-20)\n");

            /* Request LFwEI Super Packet instead
             * SMT 360 does not support 0x8e-20 either */
            putbyte(buf, 0, 0x20);
            putbyte(buf, 1, 0x01);      /* auto-report */
            (void)tsip_write(session, 0x8e, buf, 2);
        }
        break;

    case 0x1c:        // Hardware/Software Version Information
        /* Present in:
         *   Acutime Gold
         *   Lassen iQ (2005) fw 1.16+
         *   Copernicus (2006)
         *   Copernicus II (2009)
         *   Thunderbolt E (2012)
         *   RES SMT 360 (2018)
         *   ICM SMT 360 (2018)
         *   RES360 17x22 (2018)
         *   Acutime 360
         * Not Present in:
         *   pre-2000 models
         *   ACE II (1999)
         *   ACE III (2000)
         *   Lassen SQ (2002)
         *   Lassen iQ (2005) pre fw 1.16
         */
        u1 = (uint8_t) getub(buf, 0);
        // decode by sub-code
        switch (u1) {
        case 0x81:
                /* Firmware component version information (0x1c-81)
                 * polled by 0x1c-01
                 * Present in:
                 *   Copernicus II (2009)
                 */
                // byte 1, reserved
                u2 = getub(buf, 2);       // Major version
                u3 = getub(buf, 3);       // Minor version
                u4 = getub(buf, 4);       // Build number
                u5 = getub(buf, 5);       // Build Month
                u6 = getub(buf, 6);       // Build Day
                ul1 = getbeu16(buf, 7);   // Build Year
                u7 = getub(buf, 9);       // Length of product name
                // check for valid module name length
                if (40 < u7) {
                    u7 = 40;
                }
                // check for valid module name length, again
                if (u7 > (len - 10)) {
                    u7 = len - 10;
                }
                /* Product name in ASCII */
                memcpy(buf2, &buf[10], u7);
                buf2[u7] = '\0';

                (void)snprintf(session->subtype, sizeof(session->subtype),
                               "fw %u.%u %u %02u/%02u/%04u %.40s",
                               u2, u3, u4, u6, u5, ul1, buf2);
                GPSD_LOG(LOG_PROG, &session->context->errout,
                         "TSIP: Firmware version (0x1c-81): %s\n",
                         session->subtype);

                mask |= DEVICEID_SET;
                if ('\0' == session->subtype1[0]) {
                    // request actual subtype1 from 0x1c-83
                    putbyte(buf, 0, 0x03);
                    (void)tsip_write(session, 0x1c, buf, 1);
                }
                break;

        case 0x83:
                /* Hardware component version information (0x1c-83)
                 * polled by 0x1c-03
                 * Not Present in:
                 *   Copernicus II (2009)
                 */
                ul1 = getbeu32(buf, 1);  // Serial number
                u2 = getub(buf, 5);      // Build day
                u3 = getub(buf, 6);      // Build month
                ul2 = getbeu16(buf, 7);  // Build year
                u4 = getub(buf, 9);      // Build hour
                /* Hardware Code */
                session->driver.tsip.hardware_code = getbeu16(buf, 10);
                u5 = getub(buf, 12);     /* Length of Hardware ID */
                // check for valid module name length
                // copernicus ii is 27 long
                if (40 < u5) {
                    u5 = 40;
                }
                // check for valid module name length, again
                if (u5 > (len - 13)) {
                    u5 = len - 13;
                }
                memcpy(buf2, &buf[13], u5);
                buf2[u5] = '\0';

                (void)snprintf(session->subtype1, sizeof(session->subtype1),
                               "hw %u %02u/%02u/%04u %02u %04u %.40s",
                               ul1, u2, u3, ul2, u4,
                               session->driver.tsip.hardware_code,
                               buf2);
                GPSD_LOG(LOG_PROG, &session->context->errout,
                         "TSIP: Hardware version (0x1c-83): %s\n",
                         session->subtype1);

                mask |= DEVICEID_SET;

                /* Detecting device by Hardware Code */
                switch (session->driver.tsip.hardware_code) {
                case 3001:            // Acutime Gold
                    session->driver.tsip.subtype = TSIP_ACUTIME_GOLD;
                    configuration_packets_acutime_gold(session);
                    break;
                case 3023:            // RES SMT 360
                    session->driver.tsip.subtype = TSIP_RESSMT360;
                    configuration_packets_res360(session);
                    break;
                case 3026:            // ICM SMT 360
                    session->driver.tsip.subtype = TSIP_ICMSMT360;
                    configuration_packets_res360(session);
                    break;
                case 3031:            // RES360 17x22
                    session->driver.tsip.subtype = TSIP_RES36017x22;
                    configuration_packets_res360(session);
                    break;
                case 1001:            // Lassen iQ
                    // FALLTHROUGH
                case 1002:            // Copernicus, Copernicus II
                    // FALLTHROUGH
                case 3007:            // Thunderbolt E
                    // FALLTHROUGH
                case 3032:            // Acutime 360
                    // FALLTHROUGH
                default:
                    configuration_packets_generic(session);
                    break;
                }
                break;
        default:
                GPSD_LOG(LOG_ERROR, &session->context->errout,
                         "TSIP: Unhandled subpacket ID 0x1c-%x\n", u1);
                break;
        }
        break;
    case 0x41:
        /* GPS Time (0x41).  polled by 0x21
         * Note: this is not the time of current fix
         * Present in:
         *   pre-2000 models
         *   Copernicus II (2009)
         *   ICM SMT 360 (2018)
         *   RES SMT 360 (2018)
         */
        if (len != 10) {
            bad_len = 10;
            break;
        }
        session->driver.tsip.last_41 = now;     /* keep timestamp for request */
        ftow = getbef32((char *)buf, 0);        /* gpstime */
        week = getbeu16(buf, 4);        /* week */
        f2 = getbef32((char *)buf, 6);  /* leap seconds */
        if (ftow >= 0.0 && f2 > 10.0) {
            session->context->leap_seconds = (int)round(f2);
            session->context->valid |= LEAP_SECOND_VALID;
            DTOTS(&ts_tow, ftow);
            session->newdata.time =
                gpsd_gpstime_resolv(session, week, ts_tow);
            mask |= TIME_SET | NTPTIME_IS;
            /* Note: this is not the time of current fix
             * Do not use in tsip.last_tow */
        }
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "TSIP: GPS Time (0x41): tow %.2f week %u ls %.1f %s\n",
                 ftow, week, f2,
                 timespec_str(&session->newdata.time, ts_buf, sizeof(ts_buf)));
        break;
    case 0x42:
        /* Single-Precision Position Fix, XYZ ECEF
         * Present in:
         *   pre-2000 models
         *   Copernicus II (2009)
         *   ICM SMT 360 (2018)
         *   RES SMT 360 (2018)
         */
        if (16 > len) {
            bad_len = 16;
            break;
        }
        session->newdata.ecef.x = getbef32((char *)buf, 0);  /* X */
        session->newdata.ecef.y = getbef32((char *)buf, 4);  /* Y */
        session->newdata.ecef.z = getbef32((char *)buf, 8);  /* Z */
        ftow = getbef32((char *)buf, 12); /* time-of-fix */
        DTOTS(&ts_tow, ftow);
        session->newdata.time = gpsd_gpstime_resolv(session,
                                                    session->context->gps_week,
                                                    ts_tow);
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "TSIP: SP-XYZ (0x42): %f %f %f ftow %f\n",
                 session->newdata.ecef.x,
                 session->newdata.ecef.y,
                 session->newdata.ecef.z,
                 ftow);
        mask = ECEF_SET | TIME_SET | NTPTIME_IS;
        if (!TS_EQ(&ts_tow, &session->driver.tsip.last_tow)) {
            mask |= CLEAR_IS;
            session->driver.tsip.last_tow = ts_tow;
        }
        break;
    case 0x43:
        /* Velocity Fix, XYZ ECEF
         * Present in:
         *   pre-2000 models
         *   ICM SMT 360 (2018)
         *   RES SMT 360 (2018)
         * Not Present in:
         *   Copernicus II (2009)
         */
        if (len != 20) {
            bad_len = 20;
            break;
        }
        session->newdata.ecef.vx = getbef32((char *)buf, 0);  // X velocity
        session->newdata.ecef.vy = getbef32((char *)buf, 4);  // Y velocity
        session->newdata.ecef.vz = getbef32((char *)buf, 8);  // Z velocity
        f4 = getbef32((char *)buf, 12); /* bias rate */
        ftow = getbef32((char *)buf, 16); /* time-of-fix */
        DTOTS(&ts_tow, ftow);
        session->newdata.time = gpsd_gpstime_resolv(session,
                                                    session->context->gps_week,
                                                    ts_tow);
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "TSIP: Vel XYZ (0x43): %f %f %f %f ftow %f\n",
                 session->newdata.ecef.vx,
                 session->newdata.ecef.vy,
                 session->newdata.ecef.vz,
                 f4, ftow);
        mask = VECEF_SET | TIME_SET | NTPTIME_IS;
        if (!TS_EQ(&ts_tow, &session->driver.tsip.last_tow)) {
            mask |= CLEAR_IS;
            session->driver.tsip.last_tow = ts_tow;
        }
        break;
    case 0x45:
        /* Software Version Information (0x45)
         * Present in:
         *   pre-2000 models
         *   ACE II (1999)
         *   ACE III (2000)
         *   Lassen SQ (2002)
         *   Lassen iQ (2005)
         *   Copernicus II (2009)
         *   ICM SMT 360
         *   RES SMT 360
         *   Probably all TSIP
         */
        if (10 > len) {
            bad_len = 10;
            break;
        }
        // convert 2 digit years to 4 digit years
        ul1 = getub(buf, 3);
        if (80 > ul1) {
            ul1 += 2000;
        } else {
            ul1 += 1900;
        }
        ul2 = getub(buf, 8);
        if (80 > ul2) {
            ul2 += 2000;
        } else {
            ul2 += 1900;
        }
        /* ACE calls these "NAV processor firmware" and
         * "SIG processor firmware".
         * RES SMT 360 calls these "application" and "GPS core".
         */
        (void)snprintf(session->subtype, sizeof(session->subtype),
                       "sw %d.%d %02d/%02d/%04d hw %d.%d %02d/%02d/%04d",
                       getub(buf, 0),
                       getub(buf, 1),
                       getub(buf, 4),
                       getub(buf, 2),
                       ul1,
                       getub(buf, 5),
                       getub(buf, 6),
                       getub(buf, 9),
                       getub(buf, 7),
                       ul2);
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "TSIP: Software version (0x45): %s\n", session->subtype);
        mask |= DEVICEID_SET;
        break;
    case 0x46:
        /* Health of Receiver (0x46).  Poll with 0x26
         * Present in:
         *   pre-2000 models
         *   Copernicus II (2009)
         *   ICM SMT 360 (2018)
         *   RES SMT 360 (2018)
         *   all models?
         * RES SMT 360 says use 0x8f-ab or 0x8f-ac instead
         */
        if ( 2 > len) {
            bad_len = 2;
            break;
        }
        session->driver.tsip.last_46 = now;
        u1 = getub(buf, 0);     /* Status code */
        /* Error codes, model dependent
         * 0x01 -- no battery, always set on RES SMT 360
         * 0x10 -- antenna fault
         * 0x20 -- antenna is shorted
         */
        u2 = getub(buf, 1);
        if ((uint8_t)0 != u1) {
            session->gpsdata.status = STATUS_NO_FIX;
            mask |= STATUS_SET;
        } else if (session->gpsdata.status < STATUS_FIX) {
            session->gpsdata.status = STATUS_FIX;
            mask |= STATUS_SET;
        }
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "TSIP: Receiver Health (0x46): %x %x\n", u1, u2);
        break;
    case 0x47:
        /* Signal Levels for all Satellites
         * Present in:
         *   pre-2000 models
         *   Copernicus II (2009)
         *   ICM SMT 360 (2018)
         *   RES SMT 360 (2018)
         */
        if (1 > len) {
            bad_len = 1;
            break;
        }
        gpsd_zero_satellites(&session->gpsdata);
        count = (int)getub(buf, 0);     /* satellite count */
        if (len != (5 * count + 1)) {
            bad_len = 5 * count + 1;
            break;
        }
        buf2[0] = '\0';
        for (i = 0; i < count; i++) {
            u1 = getub(buf, 5 * i + 1);
            if ((f1 = getbef32((char *)buf, 5 * i + 2)) < 0)
                f1 = 0.0;
            for (j = 0; j < TSIP_CHANNELS; j++)
                if (session->gpsdata.skyview[j].PRN == (short)u1) {
                    session->gpsdata.skyview[j].ss = f1;
                    break;
                }
            str_appendf(buf2, sizeof(buf2), " %d=%.1f", (int)u1, f1);
        }
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "TSIP: Signal Levels (0x47): (%d):%s\n", count, buf2);
        mask |= SATELLITE_SET;
        break;
    case 0x48:
        /* GPS System Message
         * Present in:
         *   pre-2000 models
         * Not Present in:
         *   Copernicus II (2009)
         *   ICM SMT 360 (2018)
         *   RES SMT 360 (2018)
         */
        buf[len] = '\0';
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "TSIP: GPS System Message (0x48): %s\n", buf);
        break;
    case 0x4a:
        /* Single-Precision Position LLA
         * Present in:
         *   pre-2000 models
         *   Copernicus II (2009)
         *   ICM SMT 360 (2018)
         *   RES SMT 360 (2018)
         */
        if (len != 20) {
            bad_len = 20;
            break;
        }
        session->newdata.latitude = getbef32((char *)buf, 0) * RAD_2_DEG;
        session->newdata.longitude = getbef32((char *)buf, 4) * RAD_2_DEG;
        /* depending on GPS config, could be either WGS84 or MSL */
        d1 = getbef32((char *)buf, 8);
        if (0 == session->driver.tsip.alt_is_msl) {
            session->newdata.altHAE = d1;
        } else {
            session->newdata.altMSL = d1;
        }

        //f1 = getbef32((char *)buf, 12);       clock bias */
        ftow = getbef32((char *)buf, 16);       /* time-of-fix */
        if ((session->context->valid & GPS_TIME_VALID)!=0) {
            DTOTS(&ts_tow, ftow);
            session->newdata.time =
                gpsd_gpstime_resolv(session, session->context->gps_week,
                                    ts_tow);
            mask |= TIME_SET | NTPTIME_IS;
            if (!TS_EQ(&ts_tow, &session->driver.tsip.last_tow)) {
                mask |= CLEAR_IS;
                session->driver.tsip.last_tow = ts_tow;
            }
        }
        // this seems to be often first in cycle
        // REPORT_IS here breaks reports in read-only mode
        mask |= LATLON_SET | ALTITUDE_SET;
        GPSD_LOG(LOG_DATA, &session->context->errout,
                 "TSIP: SP-LLA (0x4a): time=%s lat=%.2f lon=%.2f "
                 "alt=%.2f\n",
                 timespec_str(&session->newdata.time, ts_buf, sizeof(ts_buf)),
                 session->newdata.latitude,
                 session->newdata.longitude, d1);
        break;
    case 0x4b:
        /* Machine/Code ID and Additional Status (0x4b)
         * polled by i0x25 or 0x26.  Sent with 0x46.
         * Present in:
         *   pre-2000 models
         *   Copernicus II (2009)
         *   ICM SMT 360 (2018)
         *   RES SMT 360 (2018)
         *   all receivers?
         */
        if (len != 3) {
            bad_len = 3;
            break;
        }
        session->driver.tsip.machine_id = getub(buf, 0);  /* Machine ID */
        /* Status 1
         * bit 1 -- No RTC at power up
         * bit 3 -- almanac not complete and current */
        u2 = getub(buf, 1);
        u3 = getub(buf, 2);     /* Status 2/Superpacket Support */
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "TSIP: Machine ID (0x4b): %02x %02x %02x\n",
                 session->driver.tsip.machine_id,
                 u2, u3);

        if ('\0' == session->subtype[0]) {
            // better than nothing
            switch (session->driver.tsip.machine_id) {
            case 1:
                // should use better name from superpacket
                name = " SMT 360";
                /* request actual subtype from 0x1c-81
                 * which in turn requests 0x1c-83 */
                putbyte(buf, 0, 0x01);
                (void)tsip_write(session, 0x1c, buf, 1);
                break;
            case 0x32:
                name = " Acutime 360";
                break;
            case 0x5a:
                name = " Lassen iQ";
                /* request actual subtype from 0x1c-81
                 * which in turn requests 0x1c-83.
                 * Only later firmware Lassen iQ supports this */
                putbyte(buf, 0, 0x01);
                (void)tsip_write(session, 0x1c, buf, 1);
                break;
            case 0x61:
                name = " Acutime 2000";
                break;
            case 0x62:
                name = " ACE UTC";
                break;
            case 0x96:
                // Also Copernicus II
                name = " Copernicus, Thunderbolt E";
                /* so request actual subtype from 0x1c-81
                 * which in turn requests 0x1c-83 */
                putbyte(buf, 0, 0x01);
                (void)tsip_write(session, 0x1c, buf, 1);
                break;
            default:
                 name = "";
            }
            (void)snprintf(session->subtype, sizeof(session->subtype),
                           "Machine ID x%x%s",
                           session->driver.tsip.machine_id, name);
        }
        if (u3 != session->driver.tsip.superpkt) {
            session->driver.tsip.superpkt = u3;
            GPSD_LOG(LOG_PROG, &session->context->errout,
                     "TSIP: Switching to Super Packet mode %d\n", u3);
            switch (u3){
            default:
                // FALLTHROUGH
            case 0:
                // old Trimble, no superpackets
                break;
            case 1:
                // 1 == superpacket is acutime 360, support 0x8f-20

                /* set I/O Options for Super Packet output */
                /* Position: 8F20, ECEF, DP */
                putbyte(buf, 0, IO1_8F20|IO1_DP|IO1_ECEF);
                putbyte(buf, 1, 0x00);          /* Velocity: none (via SP) */
                putbyte(buf, 2, 0x00);          /* Time: GPS */
                putbyte(buf, 3, IO4_DBHZ);      /* Aux: dBHz */
                (void)tsip_write(session, 0x35, buf, 4);
                break;
            case 2:
                // 2 == SMT 360, no 0x8f-20
                break;
            }
        }
        break;
    case 0x4c:
        /* Operating Parameters Report (0x4c).  Polled by 0x2c
         * Present in:
         *   pre-2000 models
         *   Lassen iQ, but not documented
         * Not Present in:
         *   Copernicus II (2009)
         *   ICM SMT 360 (2018)
         *   RES SMT 360 (2018)
         */
        if (len != 17) {
            bad_len = 17;
            break;
        }
        u1 = getub(buf, 0);               // Dynamics Code
        f1 = getbef32((char *)buf, 1);    // Elevation Mask
        f2 = getbef32((char *)buf, 5);    // Signal Level Mask
        f3 = getbef32((char *)buf, 9);    // PDOP Mask
        f4 = getbef32((char *)buf, 13);   // PDOP Switch
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "TSIP: Operating Params (0x4c): x%02x %f %f %f %f\n",
                 u1, f1, f2, f3, f4);
        break;
    case 0x54:
        /* Bias and Bias Rate Report (0x54)
         * Present in:
         *   pre-2000 models
         *   Acutime 360
         *   ICM SMT 360  (undocumented)
         *   RES SMT 360  (undocumented)
         * Not Present in:
         *   Copernicus II (2009)
         */
         {
            float  bias, bias_rate;
            bias = getbef32((char *)buf, 0);         // Bias
            bias_rate = getbef32((char *)buf, 4);    // Bias rate
            ftow = getbef32((char *)buf, 8);         // tow
            DTOTS(&ts_tow, ftow);
            session->newdata.time =
                gpsd_gpstime_resolv(session, session->context->gps_week,
                                    ts_tow);
            GPSD_LOG(LOG_PROG, &session->context->errout,
                     "TSIP: Bias and Bias Rate Report (0x54) %f %f %f\n",
                     bias, bias_rate, ftow);
            mask |= TIME_SET | NTPTIME_IS;
            if (!TS_EQ(&ts_tow, &session->driver.tsip.last_tow)) {
                mask |= CLEAR_IS;
                session->driver.tsip.last_tow = ts_tow;
            }
         }
         break;
    case 0x55:
        /* IO Options (0x55), polled by 0x35
         * Present in:
         *   pre-2000 models
         *   ICM SMT 360 (2018)
         *   RES SMT 360 (2018)
         *   all TSIP?
         *
         * Lassen iQ defaults: 02 02 00 00
         * RES SMT 360 defaults:  12 02 00 08
         */
        if (len != 4) {
            bad_len = 4;
            break;
        }
        u1 = getub(buf, 0);     /* Position */
        // decode HAE/MSL from Position byte
        if (IO1_MSL == (IO1_MSL & u1)) {
            session->driver.tsip.alt_is_msl = 1;
        } else {
            session->driver.tsip.alt_is_msl = 0;
        }
        u2 = getub(buf, 1);     /* Velocity */
        /* Timing
         * bit 0 - reserved use 0x8e-a2 ?
         */
        u3 = getub(buf, 2);
        /* Aux
         * bit 0 - packet 0x5a (raw data)
         * bit 3 -- Output dbHz
         */
        u4 = getub(buf, 3);
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "TSIP: IO Options (0x55): %02x %02x %02x %02x\n",
                 u1, u2, u3, u4);
        if ((u1 & 0x20) != (uint8_t) 0) {
            /* Try to get Super Packets */
            /* Turn off 0x8f-20 LFwEI Super Packet */
            putbyte(buf, 0, 0x20);
            putbyte(buf, 1, 0x00);      /* disabled */
            (void)tsip_write(session, 0x8e, buf, 2);

            /* Turn on Compact Super Packet 0x8f-23 */
            putbyte(buf, 0, 0x23);
            putbyte(buf, 1, 0x01);      /* enabled */
            (void)tsip_write(session, 0x8e, buf, 2);
            session->driver.tsip.req_compact = now;
        }
        break;
    case 0x56:
        /* Velocity Fix, East-North-Up (ENU)
         * Present in:
         *   pre-2000 models
         *   Copernicus II (2009)
         *   ICM SMT 360 (2018)
         *   RES SMT 360 (2018)
         */
        if (len != 20) {
            bad_len = 20;
            break;
        }
        f1 = getbef32((char *)buf, 0);  /* East velocity */
        f2 = getbef32((char *)buf, 4);  /* North velocity */
        f3 = getbef32((char *)buf, 8);  /* Up velocity */
        f4 = getbef32((char *)buf, 12); /* clock bias rate */
        ftow = getbef32((char *)buf, 16); /* time-of-fix */
        DTOTS(&ts_tow, ftow);
        session->newdata.time = gpsd_gpstime_resolv(session,
                                                    session->context->gps_week,
                                                    ts_tow);
        session->newdata.NED.velN = f2;
        session->newdata.NED.velE = f1;
        session->newdata.NED.velD = -f3;
        mask |= VNED_SET | TIME_SET | NTPTIME_IS;
        if (!TS_EQ(&ts_tow, &session->driver.tsip.last_tow)) {
            mask |= CLEAR_IS;
            session->driver.tsip.last_tow = ts_tow;
        }
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "TSIP: Vel ENU (0x56): %f %f %f %f ftow %f\n",
                 f1, f2, f3, f4, ftow);
        break;
    case 0x57:
        /* Information About Last Computed Fix
         * Present in:
         *   pre-2000 models
         *   Copernicus II (2009)
         *   ICM SMT 360 (2018)
         *   RES SMT 360 (2018)
         */
        if (len != 8) {
            bad_len = 8;
            break;
        }
        u1 = getub(buf, 0);                     /* Source of information */
        u2 = getub(buf, 1);                     /* Mfg. diagnostic */
        ftow = getbef32((char *)buf, 2);        /* gps_time */
        week = getbeu16(buf, 6);                /* tsip.gps_week */
        if (getub(buf, 0) == 0x01) {
            /* good current fix */
            DTOTS(&ts_tow, ftow);
            (void)gpsd_gpstime_resolv(session, week, ts_tow);
            mask |= TIME_SET | NTPTIME_IS;
            if (!TS_EQ(&ts_tow, &session->driver.tsip.last_tow)) {
                mask |= CLEAR_IS;
                session->driver.tsip.last_tow = ts_tow;
            }
        }
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "TSIP: Fix info (0x57): %02x %02x %u %f\n", u1, u2, week, f1);
        break;
    case 0x5a:
        /* Raw Measurement Data
         * Present in:
         *   pre-2000 models
         *   Copernicus II (2009)
         *   ICM SMT 360 (2018)
         *   RES SMT 360 (2018)
         */
        if (25 > len) {
            bad_len = 25;
            break;
        }
        // Useless without the pseudorange...
        u1 = getub(buf, 0);             // PRN 1-237
        f1 = getbef32((char *)buf, 1);  // sample length
        f2 = getbef32((char *)buf, 5);  // Signal Level, dbHz
        f3 = getbef32((char *)buf, 9);  // Code phase, 1/16th chip
        f4 = getbef32((char *)buf, 13); // Doppler, Hz @ L1
        d1 = getbed64((char *)buf, 17); // Time of Measurement
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "TSIP: Raw Measurement Data (0x5a): %d %f %f %f %f %f\n",
                 u1, f1, f2, f3, f4, d1);
        break;
    case 0x5c:
        /* Satellite Tracking Status (0x5c) polled by 0x3c
         *
         * GPS only, no WAAS reported here or used in fix
         * Present in:
         *   pre-2000 models
         *   Copernicus, Copernicus II
         *   Thunderbold E
         * Not Present in:
         *   ICM SMT 360 (2018)
         *   RES SMT 360 (2018)
         */
        if (len != 24) {
            bad_len = 24;
            break;
        }
        u1 = getub(buf, 0);     /* PRN 1-32 */
        u2 = getub(buf, 1);     /* slot:chan */
        u3 = getub(buf, 2);     /* Acquisition flag */
        u4 = getub(buf, 3);     /* Ephemeris flag */
        f1 = getbef32((char *)buf, 4);  /* Signal level */
        ftow = getbef32((char *)buf, 8);  /* time of Last measurement */
        d1 = getbef32((char *)buf, 12) * RAD_2_DEG;     /* Elevation */
        d2 = getbef32((char *)buf, 16) * RAD_2_DEG;     /* Azimuth */
        i = (int)(u2 >> 3);     /* channel number */
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "TSIP: Satellite Tracking Status (0x5c): Ch %2d PRN %3d "
                 "es %d Acq %d Eph %2d SNR %4.1f LMT %.04f El %4.1f Az %5.1f\n",
                 i, u1, u2 & 7, u3, u4, f1, ftow, d1, d2);
        if (i < TSIP_CHANNELS) {
            session->gpsdata.skyview[i].PRN = (short)u1;
            session->gpsdata.skyview[i].svid = (unsigned char)u1;
            session->gpsdata.skyview[i].gnssid = GNSSID_GPS;
            session->gpsdata.skyview[i].ss = (double)f1;
            session->gpsdata.skyview[i].elevation = (double)d1;
            session->gpsdata.skyview[i].azimuth = (double)d2;
            session->gpsdata.skyview[i].used = false;
            session->gpsdata.skyview[i].gnssid = tsip_gnssid(0, u1,
                &session->gpsdata.skyview[i].svid);
            if (0.1 < f1) {
                // check used list, if ss is non-zero
                for (j = 0; j < session->gpsdata.satellites_used; j++) {
                    if (session->gpsdata.skyview[i].PRN != 0 &&
                        session->driver.tsip.sats_used[j] != 0) {
                        session->gpsdata.skyview[i].used = true;
                    }
                }
            }
            /* when polled by 0x3c, all the skyview times will be the same
             * in one cluster */
            if (0.0 < ftow) {
                DTOTS(&ts_tow, ftow);
                session->gpsdata.skyview_time =
                    gpsd_gpstime_resolv(session, session->context->gps_week,
                                        ts_tow);
                /* do not save in session->driver.tsip.last_tow
                 * as this is skyview time, not fix time */
            }
            if (++i == session->gpsdata.satellites_visible) {
                mask |= SATELLITE_SET;  /* last of the series */
            }
            if (i > session->gpsdata.satellites_visible) {
                // FIXME! how does this ever decrease??
                session->gpsdata.satellites_visible = i;
            }
        }
        break;

     case 0x5d:
        /* GNSS Satellite Tracking Status (multi-GNSS operation) (0x5d)
         * polled by 0x3c
         *
         * GNSS only, no WAAS reported here or used in fix
         * Present in:
         *   ICM SMT 360 (2018)
         *   RES SMT 360 (2018)
         * Not Present in:
         *   pre-2000 models
         *   Copernicus, Copernicus II
         *   Thunderbold E
         */
        if (len != 26) {
            bad_len = 26;
            break;
        }
        u1 = getub(buf, 0);     /* PRN */
        u2 = getub(buf, 1);     /* chan */
        u3 = getub(buf, 2);     /* Acquisition flag */
        u4 = getub(buf, 3);     /* SV used in Position or Time calculation*/
        f1 = getbef32((char *)buf, 4);  /* Signal level */
        // This can be one second behind the TPV on RES SMT 360
        ftow = getbef32((char *)buf, 8);  /* time of Last measurement */
        d1 = getbef32((char *)buf, 12) * RAD_2_DEG;     /* Elevation */
        d2 = getbef32((char *)buf, 16) * RAD_2_DEG;     /* Azimuth */
        u5 = getub(buf, 20);    /* old measurement flag */
        u6 = getub(buf, 21);    /* integer msec flag */
        u7 = getub(buf, 22);    /* bad data flag */
        u8 = getub(buf, 23);    /* data collection flag */
        u9 = getub(buf, 24);    /* Used flags */
        u10 = getub(buf, 25);   /* SV Type */

        i = u2;                 /* channel number */
        GPSD_LOG(LOG_PROG, &session->context->errout,
                "TSIP: Satellite Tracking Status (0x5d): Ch %2d Con %d PRN %3d "
                "Acq %d Use %d SNR %4.1f LMT %.04f El %4.1f Az %5.1f Old %d "
                "Int %d Bad %d Col %d TPF %d SVT %d\n",
                i, u10, u1, u3, u4, f1, ftow, d1, d2, u5, u6, u7, u8, u9, u10);
        if (i < TSIP_CHANNELS) {
            session->gpsdata.skyview[i].PRN = (short)u1;
            session->gpsdata.skyview[i].ss = (double)f1;
            session->gpsdata.skyview[i].elevation = (double)d1;
            session->gpsdata.skyview[i].azimuth = (double)d2;
            session->gpsdata.skyview[i].used = (bool)u4;
            session->gpsdata.skyview[i].gnssid = tsip_gnssid(u10, u1,
                &session->gpsdata.skyview[i].svid);

            /* when polled by 0x3c, all the skyview times will be the same
             * in one cluster */
            if (0.0 < ftow) {
                DTOTS(&ts_tow, ftow);
                session->gpsdata.skyview_time =
                    gpsd_gpstime_resolv(session, session->context->gps_week,
                                        ts_tow);
                /* do not save in session->driver.tsip.last_tow
                 * as this is skyview time, not fix time */
            }
            if (++i == session->gpsdata.satellites_visible) {
                mask |= SATELLITE_SET;  /* last of the series */
            }
            if (i > session->gpsdata.satellites_visible) {
                // FIXME! how does this ever decrease??
                session->gpsdata.satellites_visible = i;
            }
        }
        break;
    case 0x6c:
        /* Satellite Selection List (0x6c) polled by 0x24
         *
         * Present in:
         *   ICM SMT 360 (2018)
         *   RES SMT 360 (2018)
         * Not present in:
         *   pre-2000 models
         *   Copernicus II (2009)
         *   Lassen SQ (2002)
         *   Lassen iQ (2005) */
        if (18 > len) {
            bad_len = 18;
            break;
        }
        u1 = getub(buf, 0);          // fix dimension, mode
        count = (int)getub(buf, 17);
        if (len != (18 + count)) {
            bad_len = 18 + count;
            break;
        }

        // why same as 6d?
        session->driver.tsip.last_6d = now;     /* keep timestamp for request */
        /*
         * This looks right, but it sets a spurious mode value when
         * the satellite constellation looks good to the chip but no
         * actual fix has yet been acquired.  We should set the mode
         * field (which controls gpsd's fix reporting) only from sentences
         * that convey actual fix information, like 0x8f-20, but some
         * TSIP do not support 0x8f-20, and 0x6c may be all we got.
         */
        switch (u1 & 7) {       /* dimension */
        case 1:       // clock fix (surveyed in)
            // FALLTHROUGH
        case 5:       // Overdetermined clock fix
            session->gpsdata.status = STATUS_TIME;
            session->newdata.mode = MODE_3D;
            break;
        case 3:
            session->gpsdata.status = STATUS_FIX;
            session->newdata.mode = MODE_2D;
            break;
        case 4:
            session->gpsdata.status = STATUS_FIX;
            session->newdata.mode = MODE_3D;
            break;
        case 2:
            // FALLTHROUGH
        case 6:
            // FALLTHROUGH
        case 7:
            // FALLTHROUGH
        default:
            session->gpsdata.status = STATUS_NO_FIX;
            session->newdata.mode = MODE_NO_FIX;
            break;
        }
        if (8 == (u1 & 8)) {
            // Surveyed in
            session->gpsdata.status = STATUS_TIME;
        }
        mask |= MODE_SET;

        session->gpsdata.satellites_used = count;
        session->gpsdata.dop.pdop = getbef32((char *)buf, 1);
        session->gpsdata.dop.hdop = getbef32((char *)buf, 5);
        session->gpsdata.dop.vdop = getbef32((char *)buf, 9);
        // RES SMT 360 and ICM SMT 360 always report tdop == 1
        session->gpsdata.dop.tdop = getbef32((char *)buf, 13);
        session->gpsdata.dop.gdop =
            sqrt(pow(session->gpsdata.dop.pdop, 2) +
                 pow(session->gpsdata.dop.tdop, 2));

        memset(session->driver.tsip.sats_used, 0,
                sizeof(session->driver.tsip.sats_used));
        buf2[0] = '\0';
        for (i = 0; i < count; i++) {
            session->driver.tsip.sats_used[i] = (short)getub(buf, 18 + i);
            if (session->context->errout.debug >= LOG_DATA) {
                str_appendf(buf2, sizeof(buf2),
                               " %d", session->driver.tsip.sats_used[i]);
            }
        }
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "TSIP: AIVSS (0x6c): mode %d status %d used %d "
                 "pdop %.1f hdop %.1f vdop %.1f tdop %.1f gdop %.1f Used %s\n",
                 session->newdata.mode,
                 session->gpsdata.status,
                 session->gpsdata.satellites_used,
                 session->gpsdata.dop.pdop,
                 session->gpsdata.dop.hdop,
                 session->gpsdata.dop.vdop,
                 session->gpsdata.dop.tdop,
                 session->gpsdata.dop.gdop,
                 buf2);
        mask |= DOP_SET | MODE_SET | STATUS_SET | USED_IS;
        break;
    case 0x6d:
        /* All-In-View Satellite Selection (0x6d) polled by 0x24
         *
         * Present in:
         *   pre-2000 models
         *   Copernicus II (2009)
         *   Lassen SQ
         *   Lassen iQ
         * Not present in:
         *   ICM SMT 360 (2018)
         *   RES SMT 360 (2018)
         */
        if (1 > len) {
            bad_len = 1;
            break;
        }
        u1 = getub(buf, 0);     /* nsvs/dimension */
        count = (int)((u1 >> 4) & 0x0f);
        if (len != (17 + count)) {
            bad_len = 17 + count;
            break;
        }
        session->driver.tsip.last_6d = now;     /* keep timestamp for request */
        /*
         * This looks right, but it sets a spurious mode value when
         * the satellite constellation looks good to the chip but no
         * actual fix has yet been acquired.  We should set the mode
         * field (which controls gpsd's fix reporting) only from sentences
         * that convey actual fix information, like 0x8f-20, but some
         * TSIP do not support 0x8f-20, and 0x6c may be all we got.
         */
        if (0 != isfinite(session->gpsdata.fix.longitude)) {
            // have a fix
            switch (u1 & 7) {   /* dimension */
            case 1:       // clock fix (surveyed in)
                // FALLTHROUGH
            case 5:       // Overdetermined clock fix
                session->gpsdata.status = STATUS_TIME;
                session->newdata.mode = MODE_3D;
                break;
            case 3:
                session->gpsdata.status = STATUS_FIX;
                session->newdata.mode = MODE_2D;
                break;
            case 4:
                session->gpsdata.status = STATUS_FIX;
                session->newdata.mode = MODE_3D;
                break;
            case 2:
                // FALLTHROUGH
            case 6:
                // FALLTHROUGH
            case 7:
                // FALLTHROUGH
            default:
                session->gpsdata.status = STATUS_NO_FIX;
                session->newdata.mode = MODE_NO_FIX;
                break;
            }
        } else {
            session->gpsdata.status = STATUS_NO_FIX;
            session->newdata.mode = MODE_NO_FIX;
        }
        mask |= MODE_SET;

        session->gpsdata.satellites_used = count;
        session->gpsdata.dop.pdop = getbef32((char *)buf, 1);
        session->gpsdata.dop.hdop = getbef32((char *)buf, 5);
        session->gpsdata.dop.vdop = getbef32((char *)buf, 9);
        session->gpsdata.dop.tdop = getbef32((char *)buf, 13);
        session->gpsdata.dop.gdop =
            sqrt(pow(session->gpsdata.dop.pdop, 2) +
                 pow(session->gpsdata.dop.tdop, 2));

        memset(session->driver.tsip.sats_used, 0,
               sizeof(session->driver.tsip.sats_used));
        buf2[0] = '\0';
        for (i = 0; i < count; i++) {
            // negative PRN means sat unhealthy
            session->driver.tsip.sats_used[i] = (short)getub(buf, 17 + i);
            if (session->context->errout.debug >= LOG_DATA) {
                str_appendf(buf2, sizeof(buf2),
                               " %d", session->driver.tsip.sats_used[i]);
            }
        }
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "TSIP: AIVSS (0x6d) status=%d used=%d "
                 "pdop=%.1f hdop=%.1f vdop=%.1f tdop=%.1f gdop=%.1f used:%s\n",
                 session->gpsdata.status,
                 session->gpsdata.satellites_used,
                 session->gpsdata.dop.pdop,
                 session->gpsdata.dop.hdop,
                 session->gpsdata.dop.vdop,
                 session->gpsdata.dop.tdop,
                 session->gpsdata.dop.gdop, buf2);
        mask |= DOP_SET | STATUS_SET | USED_IS;
        break;
    case 0x82:
        /* Differential Position Fix Mode (0x82) poll with 0x62-ff
         * Sent after every position fix in Auto GPS/DGPS,
         * so potential cycle ender
         *
         * Present in:
         *   pre-2000 models
         *   Copernicus II (2009)
         *   Lassen iQ, deprecated use 0xbb instead
         * Not Present in:
         *   ICM SMT 360 (2018)
         *   RES SMT 360 (2018)
         */
        if (len != 1) {
            bad_len = 1;
            break;
        }
        u1 = getub(buf, 0);     /* fix mode */
        if (session->gpsdata.status == STATUS_FIX && (u1 & 0x01) != 0) {
            session->gpsdata.status = STATUS_DGPS_FIX;
            mask |= STATUS_SET;
        }
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "TSIP: DPFM (0x82) mode %d status=%d\n",
                 u1, session->gpsdata.status);
        break;
    case 0x83:
        /* Double-Precision XYZ Position Fix and Bias Information
         * Present in:
         *   pre-2000 models
         *   Copernicus II (2009)
         *   ICM SMT 360 (2018)
         *   RES SMT 360 (2018)
         */
        if (36 > len) {
            bad_len = 36;
            break;
        }
        session->newdata.ecef.x = getbed64((char *)buf, 0);  /* X */
        session->newdata.ecef.y = getbed64((char *)buf, 8);  /* Y */
        session->newdata.ecef.z = getbed64((char *)buf, 16); /* Z */
        d4 = getbed64((char *)buf, 24); /* clock bias */
        ftow = getbef32((char *)buf, 32); /* time-of-fix */
        DTOTS(&ts_tow, ftow);
        session->newdata.time = gpsd_gpstime_resolv(session,
                                                    session->context->gps_week,
                                                    ts_tow);
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "TSIP: DP-XYZ (0x83) %f %f %f %f tow %f\n",
                 session->newdata.ecef.x,
                 session->newdata.ecef.y,
                 session->newdata.ecef.z,
                 d4, ftow);
        mask = ECEF_SET | TIME_SET | NTPTIME_IS;
        if (!TS_EQ(&ts_tow, &session->driver.tsip.last_tow)) {
            mask |= CLEAR_IS;
            session->driver.tsip.last_tow = ts_tow;
        }
        break;
    case 0x84:
        /* Double-Precision LLA Position Fix and Bias Information
         * Present in:
         *   pre-2000 models
         *   Copernicus II (2009)
         *   ICM SMT 360 (2018)
         *   RES SMT 360 (2018)
         */
        if (len != 36) {
            bad_len = 36;
            break;
        }
        session->newdata.latitude = getbed64((char *)buf, 0) * RAD_2_DEG;
        session->newdata.longitude = getbed64((char *)buf, 8) * RAD_2_DEG;
        /* depending on GPS config, could be either WGS84 or MSL */
        d1 = getbed64((char *)buf, 16);
        if (0 == session->driver.tsip.alt_is_msl) {
            session->newdata.altHAE = d1;
        } else {
            session->newdata.altMSL = d1;
        }
        mask |= ALTITUDE_SET;
        //d1 = getbed64((char *)buf, 24);       clock bias */
        ftow = getbef32((char *)buf, 32);       /* time-of-fix */
        if ((session->context->valid & GPS_TIME_VALID)!=0) {
            DTOTS(&ts_tow, ftow);
            session->newdata.time =
                gpsd_gpstime_resolv(session, session->context->gps_week,
                                    ts_tow);
            mask |= TIME_SET | NTPTIME_IS;
            if (!TS_EQ(&ts_tow, &session->driver.tsip.last_tow)) {
                mask |= CLEAR_IS;
                session->driver.tsip.last_tow = ts_tow;
            }
        }
        mask |= LATLON_SET;
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "TSIP: DP-LLA (0x84) time=%s lat=%.2f lon=%.2f alt=%.2f\n",
                 timespec_str(&session->newdata.time, ts_buf, sizeof(ts_buf)),
                 session->newdata.latitude,
                 session->newdata.longitude, d1);
        break;
    case 0x8f:
        /* Super Packet.
         * Present in:
         *   pre-2000 models
         *   ACE II
         *   ACE III
         *   Copernicus II (2009)
         *   ICM SMT 360
         *   RES SMT 360
         */
        u1 = (uint8_t) getub(buf, 0);
        switch (u1) {           /* sub-code ID */
        case 0x15:
            /* Current Datum Values
             * Not Present in:
             *   pre-2000 models
             *   Copernicus II (2009)
             *   ICM SMT 360 (2018)
             *   RES SMT 360 (2018)
             */
            if (len != 43) {
                bad_len = 43;
                break;
            }
            s1 = getbes16(buf, 1);      /* Datum Index */
            d1 = getbed64((char *)buf, 3);      /* DX */
            d2 = getbed64((char *)buf, 11);     /* DY */
            d3 = getbed64((char *)buf, 19);     /* DZ */
            d4 = getbed64((char *)buf, 27);     /* A-axis */
            d5 = getbed64((char *)buf, 35);     /* Eccentricity Squared */
            GPSD_LOG(LOG_PROG, &session->context->errout,
                     "TSIP: Current Datum (0x8f-15) %d %f %f %f %f %f\n",
                     s1, d1, d2, d3, d4, d5);
            break;

        case 0x20:
            /* Last Fix with Extra Information (binary fixed point) 0x8f-20
             * Only ouput when fix is available.
             * CSK sez "why does my Lassen SQ output oversize packets?"
             * Present in:
             *   pre-2000 models
             *   ACE II
             *   Copernicus, Copernicus II (64-bytes)
             * Not present in:
             *   ICM SMT 360
             *   RES SMT 360
             */
            if ((len != 56) && (len != 64)) {
                bad_len = 56;
                break;
            }
            s1 = getbes16(buf, 2);      /* east velocity */
            s2 = getbes16(buf, 4);      /* north velocity */
            s3 = getbes16(buf, 6);      /* up velocity */
            tow = getbeu32(buf, 8);     /* time in ms */
            sl1 = getbes32(buf, 12);    /* latitude */
            ul2 = getbeu32(buf, 16);    /* longitude */
            // Lassen iQ, and copernicus (ii) doc says this is always altHAE
            sl2 = getbes32(buf, 20);    /* altitude */
            u1 = getub(buf, 24);        /* velocity scaling */
            u2 = getub(buf, 27);        /* fix flags */
            u3 = getub(buf, 28);        /* num svs */
            u4 = getub(buf, 29);        /* utc offset */
            week = getbeu16(buf, 30);   /* tsip.gps_week */
            /* PRN/IODE data follows */
            GPSD_LOG(LOG_PROG, &session->context->errout,
                     "TSIP: LFwEI (0x8f-20) %d %d %d tow %u %d "
                     " %u %u %x %x %u leap %u week %d\n",
                     s1, s2, s3, tow, sl1, ul2, sl2, u1, u2, u3, u4, week);

            if ((u1 & 0x01) != (uint8_t) 0)     /* check velocity scaling */
                d5 = 0.02;
            else
                d5 = 0.005;

            // 0x8000 is over-range
            if ((int16_t)0x8000 != s2) {
                d2 = (double)s2 * d5;   /* north velocity m/s */
                session->newdata.NED.velN = d2;
            }
            if ((int16_t)0x8000 != s1) {
                d1 = (double)s1 * d5;   /* east velocity m/s */
                session->newdata.NED.velE = d1;
            }
            if ((int16_t)0x8000 != s3) {
                d3 = (double)s3 * d5;       /* up velocity m/s */
                session->newdata.NED.velD = -d3;
            }

            session->newdata.latitude = (double)sl1 * SEMI_2_DEG;
            session->newdata.longitude = (double)ul2 * SEMI_2_DEG;
            if (session->newdata.longitude > 180.0)
                session->newdata.longitude -= 360.0;
            // Lassen iQ doc says this is always altHAE in mm
            session->newdata.altHAE = (double)sl2 * 1e-3;
            mask |= ALTITUDE_SET;

            session->gpsdata.status = STATUS_NO_FIX;
            session->newdata.mode = MODE_NO_FIX;
            if ((u2 & 0x01) == (uint8_t) 0) {   /* Fix Available */
                session->gpsdata.status = STATUS_FIX;
                if ((u2 & 0x02) != (uint8_t) 0) /* DGPS Corrected */
                    session->gpsdata.status = STATUS_DGPS_FIX;
                if ((u2 & 0x04) != (uint8_t) 0) /* Fix Dimension */
                    session->newdata.mode = MODE_2D;
                else
                    session->newdata.mode = MODE_3D;
            }
            session->gpsdata.satellites_used = (int)u3;
            if ((int)u4 > 10) {
                session->context->leap_seconds = (int)u4;
                session->context->valid |= LEAP_SECOND_VALID;
                /* check for week rollover
                 * Trimble uses 15 bit weeks, but can guess the epoch wrong
                 * Can not be in gpsd_gpstime_resolv() because that
                 * may see BUILD_LEAPSECONDS instead of leap_seconds
                 * from receiver.
                 */
                if (17 < u4 && 1930 > week) {
                    // leap second 18 added in gps week 1930
                    week += 1024;
                    if (1930 > week) {
                        // and again?
                        week += 1024;
                    }
                }
            }
            MSTOTS(&ts_tow, tow);
            session->newdata.time = gpsd_gpstime_resolv(session, week,
                                                        ts_tow);
            mask |= TIME_SET | NTPTIME_IS | LATLON_SET |
                    STATUS_SET | MODE_SET | VNED_SET;
            if (!TS_EQ(&ts_tow, &session->driver.tsip.last_tow)) {
                mask |= CLEAR_IS;
                session->driver.tsip.last_tow = ts_tow;
            }
            GPSD_LOG(LOG_DATA, &session->context->errout,
                     "TSIP: LFwEI (0x8f-20): time=%s lat=%.2f lon=%.2f "
                     "altHAE=%.2f mode=%d status=%d\n",
                     timespec_str(&session->newdata.time, ts_buf,
                                  sizeof(ts_buf)),
                     session->newdata.latitude, session->newdata.longitude,
                     session->newdata.altHAE,
                     session->newdata.mode, session->gpsdata.status);
            break;
        case 0x23:
            /* Compact Super Packet (0x8f-23)
             * Present in:
             *   Copernicus, Copernicus II
             * Not present in:
             *   pre-2000 models
             *   Lassen iQ
             *   ICM SMT 360
             *   RES SMT 360
             */
            session->driver.tsip.req_compact = 0;
            /* CSK sez "i don't trust this to not be oversized either." */
            if (29 > len) {
                bad_len = 29;
                break;
            }
            tow = getbeu32(buf, 1);             /* time in ms */
            week = getbeu16(buf, 5);            /* tsip.gps_week */
            u1 = getub(buf, 7);                 /* utc offset */
            u2 = getub(buf, 8);                 /* fix flags */
            sl1 = getbes32(buf, 9);             /* latitude */
            ul2 = getbeu32(buf, 13);            /* longitude */
            // Copernicus (ii) doc says this is always altHAE in mm
            sl3 = getbes32(buf, 17);    /* altitude */
            /* set xNED here */
            s2 = getbes16(buf, 21);     /* east velocity */
            s3 = getbes16(buf, 23);     /* north velocity */
            s4 = getbes16(buf, 25);     /* up velocity */
            GPSD_LOG(LOG_PROG, &session->context->errout,
                     "TSIP: CSP (0x8f-23): %u %d %u %u %d %u %d %d %d %d\n",
                     tow, week, u1, u2, sl1, ul2, sl3, s2, s3, s4);
            if ((int)u1 > 10) {
                session->context->leap_seconds = (int)u1;
                session->context->valid |= LEAP_SECOND_VALID;
            }
            MSTOTS(&ts_tow, tow);
            session->newdata.time =
                gpsd_gpstime_resolv(session, week, ts_tow);
            session->gpsdata.status = STATUS_NO_FIX;
            session->newdata.mode = MODE_NO_FIX;
            if ((u2 & 0x01) == (uint8_t) 0) {   /* Fix Available */
                session->gpsdata.status = STATUS_FIX;
                if ((u2 & 0x02) != (uint8_t) 0) /* DGPS Corrected */
                    session->gpsdata.status = STATUS_DGPS_FIX;
                if ((u2 & 0x04) != (uint8_t) 0) /* Fix Dimension */
                    session->newdata.mode = MODE_2D;
                else
                    session->newdata.mode = MODE_3D;
            }
            session->newdata.latitude = (double)sl1 * SEMI_2_DEG;
            session->newdata.longitude = (double)ul2 * SEMI_2_DEG;
            if (session->newdata.longitude > 180.0)
                session->newdata.longitude -= 360.0;
            // Copernicus (ii) doc says this is always altHAE in mm
            session->newdata.altHAE = (double)sl3 * 1e-3;
            mask |= ALTITUDE_SET;
            if ((u2 & 0x20) != (uint8_t) 0)     /* check velocity scaling */
                d5 = 0.02;
            else
                d5 = 0.005;
            d1 = (double)s2 * d5;       /* east velocity m/s */
            d2 = (double)s3 * d5;       /* north velocity m/s */
            d3 = (double)s4 * d5;       /* up velocity m/s */
            session->newdata.NED.velN = d2;
            session->newdata.NED.velE = d1;
            session->newdata.NED.velD = -d3;

            mask |= TIME_SET | NTPTIME_IS | LATLON_SET |
                    STATUS_SET | MODE_SET | VNED_SET;
            if (!TS_EQ(&ts_tow, &session->driver.tsip.last_tow)) {
                mask |= CLEAR_IS;
                session->driver.tsip.last_tow = ts_tow;
            }
            GPSD_LOG(LOG_PROG, &session->context->errout,
                     "TSIP: SP-CSP (0x8f-23): time %s lat %.2f lon %.2f "
                     "altHAE %.2f mode %d status %d\n",
                     timespec_str(&session->newdata.time, ts_buf,
                                  sizeof(ts_buf)),
                     session->newdata.latitude, session->newdata.longitude,
                     session->newdata.altHAE,
                     session->newdata.mode, session->gpsdata.status);
            break;

        case 0xa5:
            /* Packet Broadcast Mask (0x8f-a5) polled by 0x8e-a5
             *
             * Present in:
             *   ICM SMT 360
             *   RES SMT 360
             * Not Present in:
             *   pre-2000 models
             *   Copernicus II (2009)
             */
            {
                uint16_t mask0, mask1;
                if (5 > len) {
                    bad_len = 5;
                    break;
                }
                mask0 = getbeu16(buf, 1);    // Mask 0
                mask1 = getbeu16(buf, 3);    // Mask 1
                GPSD_LOG(LOG_DATA, &session->context->errout,
                         "TSIP: PBM (0x8f-a5) mask0 x%04x mask1 x%04x\n",
                         mask0, mask1);
            }
            // RES SMT 360 default 5, 0
            break;

        case 0xa6:
            /* Self-Survey Comman (0x8f-a6) polled by 0x8e-a6
             *
             * Present in:
             *   ICM SMT 360
             *   RES SMT 360
             * Not Present in:
             *   pre-2000 models
             *   Copernicus II (2009)
             */
            if (3 > len) {
                bad_len = 3;
                break;
            }
            u2 = getub(buf, 1);          // Command
            u3 = getub(buf, 2);          // Status
            GPSD_LOG(LOG_DATA, &session->context->errout,
                     "TSIP: SSC (0x8f-a6) command x%x status x%x\n",
                     u2, u3);
            break;

        case 0xab:
            /* Thunderbolt Timing Superpacket
             * Not Present in:
             *   pre-2000 models
             *   Copernicus II (2009)
             */
            if (17 > len) {
                bad_len = 17;
                break;
            }
            session->driver.tsip.last_41 = now; // keep timestamp for request
            // we assume the receiver not in some crazy mode, and is GPS time
            tow = getbeu32(buf, 1);             // gpstime in seconds
            ts_tow.tv_sec = tow;
            ts_tow.tv_nsec = 0;
            week = getbeu16(buf, 5);            // week
            /* leap seconds */
            session->context->leap_seconds = (int)getbes16(buf, 7);
            u2 = buf[9];                // Time Flag
            // should check time valid?
            /* ignore the broken down time, use the GNSS time.
             * Hope it is not BeiDou time */

            // how do we know leap valid?
            session->context->valid |= LEAP_SECOND_VALID;
            session->newdata.time = gpsd_gpstime_resolv(session, week, ts_tow);
            mask |= TIME_SET | NTPTIME_IS;
            if (!TS_EQ(&ts_tow, &session->driver.tsip.last_tow)) {
                mask |= CLEAR_IS;
                session->driver.tsip.last_tow = ts_tow;
            }

            GPSD_LOG(LOG_PROG, &session->context->errout,
                     "TSIP: SP-TTS (0x8f-ab) tow %u wk %u ls %d flag x%x "
                     "time %s mask %s\n",
                     tow, week, session->context->leap_seconds, u2,
                     timespec_str(&session->newdata.time, ts_buf,
                                  sizeof(ts_buf)),
                     gps_maskdump(mask));
            break;


        case 0xac:
            /* Supplemental Timing Packet (0x8f-ac)
             * present in:
             *   ThunderboltE
             *   ICM SMT 360
             *   RES SMT 360
             * Not Present in:
             *   pre-2000 models
             *   Lassen iQ
             *   Copernicus II (2009)
             */
            if (len != 68) {
                bad_len = 68;
                break;
            }

            // byte 0 is Subpacket ID
            u2 = getub(buf, 1);         /* Receiver Mode */
            u3 = getub(buf, 12);        /* GNSS Decoding Status */
            // ignore 2, Disciplining Mode
            // ignore 3, Self-Survey Progress
            // ignore 4-7, Holdover Duration
            // ignore 8-9, Critical Alarms
            // ignore 10-11, Minor Alarms
            // ignore 12, GNSS Decoding Status
            // ignore 13, Disciplining Activity
            // ignore 14, PPS indication
            // ignore 15, PPS reference
            /* PPS Offset in ns
             * save as (long)pico seconds
             * can't really use it as it is not referenced to any PPS */
            fqErr = getbef32((char *)buf, 16);
            session->gpsdata.qErr = (long)(fqErr * 1000);
            // ignore 20-23, Clock Offset
            // ignore 24-27, DAC Value
            // ignore 28-31, DAC Voltage
            // 32-35, Temperature degrees C
            temp = getbef32((char *)buf, 32);
            session->newdata.latitude = getbed64((char *)buf, 36) * RAD_2_DEG;
            session->newdata.longitude = getbed64((char *)buf, 44) * RAD_2_DEG;
            // SMT 360 doc says this is always altHAE in meters
            session->newdata.altHAE = getbed64((char *)buf, 52);
            // ignore 60-63, always zero
            // ignore 64-67, reserved

            if (u3 != (uint8_t)0) {
                // not exactly true, could be sort of Dead Reckoning
                session->gpsdata.status = STATUS_NO_FIX;
                mask |= STATUS_SET;
            } else {
                if (session->gpsdata.status < STATUS_FIX) {
                    session->gpsdata.status = STATUS_FIX;
                    mask |= STATUS_SET;
                }
            }

            /* Decode Fix modes */
            switch (u2 & 7) {
            case 0:     /* Auto */
                /*
                * According to the Thunderbolt Manual, the
                * first byte of the supplemental timing packet
                * simply indicates the configuration of the
                * device, not the actual lock, so we need to
                * look at the decode status.
                */
                switch (u3) {
                case 0:   /* "Doing Fixes" */
                    session->newdata.mode = MODE_3D;
                    break;
                case 0x0B: /* "Only 3 usable sats" */
                    session->newdata.mode = MODE_2D;
                    break;
                case 0x1:   /* "Don't have GPS time" */
                    // FALLTHROUGH
                case 0x3:   /* "PDOP is too high" */
                    // FALLTHROUGH
                case 0x8:   /* "No usable sats" */
                    // FALLTHROUGH
                case 0x9:   /* "Only 1 usable sat" */
                    // FALLTHROUGH
                case 0x0A:  /* "Only 2 usable sats */
                    // FALLTHROUGH
                case 0x0C:  /* "The chosen sat is unusable" */
                    // FALLTHROUGH
                case 0x10:  /* TRAIM rejected the fix */
                    // FALLTHROUGH
                default:
                    session->newdata.mode = MODE_NO_FIX;
                    break;
                }
                break;
            case 6:             /* Clock Hold 2D */
                // FALLTHROUGH
            case 3:             /* 2D Position Fix */
                //session->gpsdata.status = STATUS_FIX;
                session->newdata.mode = MODE_2D;
                break;
            case 7:             /* Thunderbolt overdetermined clock */
                // FALLTHROUGH
            case 4:             /* 3D position Fix */
                //session->gpsdata.status = STATUS_FIX;
                session->newdata.mode = MODE_3D;
                break;
            default:
                //session->gpsdata.status = STATUS_NO_FIX;
                session->newdata.mode = MODE_NO_FIX;
                break;
            }

            mask |= LATLON_SET | ALTITUDE_SET | MODE_SET;
            GPSD_LOG(LOG_PROG, &session->context->errout,
                     "TSIP: SP-TPS (0x8f-ac) lat=%.2f lon=%.2f altHAE=%.2f "
                     "mode %d temp %.1f fqErr %.4f\n",
                     session->newdata.latitude,
                     session->newdata.longitude,
                     session->newdata.altHAE,
                     session->newdata.mode,
                     temp, fqErr);
            break;

        case 0x02:
            /* UTC Information
             * Present in:
             *   ICM SMT 360 (2018)
             *   RES SMT 360 (2018)
             * Not Present in:
             *   pre-2000 models
             *   Copernicus II (2009)
             */
            // FALLTHROUGH
        case 0x21:
            /* Request accuracy information
             * Present in:
             *   Copernicus II (2009)
             * Not Present in:
             *   pre-2000 models
             */
            // FALLTHROUGH
        case 0x2a:
            /* Request Fix and Channel Tracking info, Type 1
             * Present in:
             *   Copernicus II (2009)
             * Not Present in:
             *   pre-2000 models
             */
            // FALLTHROUGH
        case 0x2b:
            /* Request Fix and Channel Tracking info, Type 2
             * Present in:
             *   Copernicus II (2009)
             * Not Present in:
             *   pre-2000 models
             */
            // FALLTHROUGH
        case 0x41:
            /* Stored manufacturing operating parameters
             * Present in:
             *   ICM SMT 360 (2018)
             *   RES SMT 360 (2018)
             * Not Present in:
             *   pre-2000 models
             *   Copernicus II (2009)
             */
            // FALLTHROUGH
        case 0x42:
            /* Stored production parameters
             * Present in:
             *   ICM SMT 360 (2018)
             *   RES SMT 360 (2018)
             * Not Present in:
             *   pre-2000 models
             *   Copernicus II (2009)
             */
            // FALLTHROUGH
        case 0x4a:
            /* PPS characteristics
             * Present in:
             *   ICM SMT 360 (2018)
             *   RES SMT 360 (2018)
             *   Copernicus II (2009)
             * Not Present in:
             *   pre-2000 models
             */
            // FALLTHROUGH
        case 0x4e:
            /* PPS Output options
             * Present in:
             *   ICM SMT 360 (2018)
             *   RES SMT 360 (2018)
             * Not Present in:
             *   pre-2000 models
             *   Copernicus II (2009)
             */
            // FALLTHROUGH
        case 0x4f:
            /* Set PPS Width
             * Present in:
             *   Copernicus II (2009)
             * Not Present in:
             *   pre-2000 models
             *   ICM SMT 360 (2018)
             *   RES SMT 360 (2018)
             */
            // FALLTHROUGH
        case 0x60:
            /* DR Calibration and Status Report
             * Present in:
             *   pre-2000 models
             * Not Present in:
             *   Copernicus II (2009)
             *   ICM SMT 360 (2018)
             *   RES SMT 360 (2018)
             */
            // FALLTHROUGH
        case 0x62:
            /* GPS/DR Position/Velocity Report
             * Present in:
             *   pre-2000 models
             * Not Present in:
             *   Copernicus II (2009)
             *   ICM SMT 360 (2018)
             *   RES SMT 360 (2018)
             */
            // FALLTHROUGH
        case 0x64:
            /* Firmware Version and Configuration Report
             * Present in:
             *   pre-2000 models
             * Not Present in:
             *   Copernicus II (2009)
             *   ICM SMT 360 (2018)
             *   RES SMT 360 (2018)
             */
            // FALLTHROUGH
        case 0x6b:
            /* Last Gyroscope Readings Report
             * Present in:
             *   pre-2000 models
             * Not Present in:
             *   Copernicus II (2009)
             *   ICM SMT 360 (2018)
             *   RES SMT 360 (2018)
             */
            // FALLTHROUGH
        case 0x6d:
            /* Last Odometer Readings Report
             * Present in:
             *   pre-2000 models
             * Not Present in:
             *   Copernicus II (2009)
             *   ICM SMT 360 (2018)
             *   RES SMT 360 (2018)
             */
            // FALLTHROUGH
        case 0x6f:
            /* Firmware Version Name Report
             * Present in:
             *   pre-2000 models
             * Not Present in:
             *   Copernicus II (2009)
             *   ICM SMT 360 (2018)
             *   RES SMT 360 (2018)
             */
            // FALLTHROUGH
        case 0x70:
            /* Beacon Channel Status Report
             * Present in:
             *   pre-2000 models
             * Not Present in:
             *   Copernicus II (2009)
             *   ICM SMT 360 (2018)
             *   RES SMT 360 (2018)
             */
            // FALLTHROUGH
        case 0x71:
            /* DGPS Station Database Reports
             * Present in:
             *   pre-2000 models
             * Not Present in:
             *   Copernicus II (2009)
             *   ICM SMT 360 (2018)
             *   RES SMT 360 (2018)
             */
            // FALLTHROUGH
        case 0x73:
            /* Beacon Channel Control Acknowledgment
             * Present in:
             *   pre-2000 models
             * Not Present in:
             *   Copernicus II (2009)
             *   ICM SMT 360 (2018)
             *   RES SMT 360 (2018)
             */
            // FALLTHROUGH
        case 0x74:
            /* Clear Beacon Database Acknowledgment
             * Present in:
             *   pre-2000 models
             * Not Present in:
             *   Copernicus II (2009)
             *   ICM SMT 360 (2018)
             *   RES SMT 360 (2018)
             */
            // FALLTHROUGH
        case 0x75:
            /* FFT Start Acknowledgment
             * Present in:
             *   pre-2000 models
             * Not Present in:
             *   Copernicus II (2009)
             *   ICM SMT 360 (2018)
             *   RES SMT 360 (2018)
             */
            // FALLTHROUGH
        case 0x76:
            /* FFT Stop Acknowledgment
             * Present in:
             *   pre-2000 models
             * Not Present in:
             *   Copernicus II (2009)
             *   ICM SMT 360 (2018)
             *   RES SMT 360 (2018)
             */
            // FALLTHROUGH
        case 0x77:
            /* FFT Reports
             * Present in:
             *   pre-2000 models
             * Not Present in:
             *   Copernicus II (2009)
             *   ICM SMT 360 (2018)
             *   RES SMT 360 (2018)
             */
            // FALLTHROUGH
        case 0x78:
            /* RTCM Reports
             * Present in:
             *   pre-2000 models
             * Not Present in:
             *   Copernicus II (2009)
             *   ICM SMT 360 (2018)
             *   RES SMT 360 (2018)
             */
            // FALLTHROUGH
        case 0x79:
            /* Beacon Station Attributes Acknowledgment
             * Present in:
             *   pre-2000 models
             * Not Present in:
             *   Copernicus II (2009)
             *   ICM SMT 360 (2018)
             *   RES SMT 360 (2018)
             */
            // FALLTHROUGH
        case 0x7a:
            /* Beacon Station Attributes Report
             * Present in:
             *   pre-2000 models
             * Not Present in:
             *   Copernicus II (2009)
             *   ICM SMT 360 (2018)
             *   RES SMT 360 (2018)
             */
            // FALLTHROUGH
        case 0x7b:
            /* DGPS Receiver RAM Configuration Block Report
             * Present in:
             *   pre-2000 models
             * Not Present in:
             *   Copernicus II (2009)
             *   ICM SMT 360 (2018)
             *   RES SMT 360 (2018)
             */
            // FALLTHROUGH
        case 0x7c:
            /* DGPS Receiver Configuration Block Acknowledgment
             * Present in:
             *   pre-2000 models
             * Not Present in:
             *   Copernicus II (2009)
             *   ICM SMT 360 (2018)
             *   RES SMT 360 (2018)
             */
            // FALLTHROUGH
        case 0x7e:
            /* Satellite Line-of-Sight (LOS) Message
             * Present in:
             *   pre-2000 models
             * Not Present in:
             *   Copernicus II (2009)
             *   ICM SMT 360 (2018)
             *   RES SMT 360 (2018)
             */
            // FALLTHROUGH
        case 0x7f:
            /* DGPS Receiver ROM Configuration Block Report
             * Present in:
             *   pre-2000 models
             * Not Present in:
             *   Copernicus II (2009)
             *   ICM SMT 360 (2018)
             *   RES SMT 360 (2018)
             */
            // FALLTHROUGH
        case 0x80:
            /* DGPS Service Provider System Information Report
             * Present in:
             *   pre-2000 models
             * Not Present in:
             *   Copernicus II (2009)
             *   ICM SMT 360 (2018)
             *   RES SMT 360 (2018)
             */
            // FALLTHROUGH
        case 0x81:
            /* Decoder Station Information Report and Selection Acknowledgment
             * Present in:
             *   pre-2000 models
             * Not Present in:
             *   Copernicus II (2009)
             *   ICM SMT 360 (2018)
             *   RES SMT 360 (2018)
             */
            // FALLTHROUGH
        case 0x82:
            /* Decoder Diagnostic Information Report
             * Present in:
             *   pre-2000 models
             * Not Present in:
             *   Copernicus II (2009)
             *   ICM SMT 360 (2018)
             *   RES SMT 360 (2018)
             */
            // FALLTHROUGH
        case 0x84:
            /* Satellite FFT Control Acknowledgment
             * Present in:
             *   pre-2000 models
             * Not Present in:
             *   Copernicus II (2009)
             *   ICM SMT 360 (2018)
             *   RES SMT 360 (2018)
             */
            // FALLTHROUGH
        case 0x85:
            /* DGPS Source Tracking Status Report
             * Present in:
             *   pre-2000 models
             * Not Present in:
             *   Copernicus II (2009)
             *   ICM SMT 360 (2018)
             *   RES SMT 360 (2018)
             */
            // FALLTHROUGH
        case 0x86:
            /* Clear Satellite Database Acknowledgment
             * Present in:
             *   pre-2000 models
             * Not Present in:
             *   Copernicus II (2009)
             *   ICM SMT 360 (2018)
             *   RES SMT 360 (2018)
             */
            // FALLTHROUGH
        case 0x87:
            /* Network Statistics Report
             * Present in:
             *   pre-2000 models
             * Not Present in:
             *   Copernicus II (2009)
             *   ICM SMT 360 (2018)
             *   RES SMT 360 (2018)
             */
            // FALLTHROUGH
        case 0x88:
            /* Diagnostic Output Options Report
             * Present in:
             *   pre-2000 models
             * Not Present in:
             *   Copernicus II (2009)
             *   ICM SMT 360 (2018)
             *   RES SMT 360 (2018)
             */
            // FALLTHROUGH
        case 0x89:
            /* DGPS Source Control Report /Acknowledgment
             * Present in:
             *   pre-2000 models
             * Not Present in:
             *   Copernicus II (2009)
             *   ICM SMT 360 (2018)
             *   RES SMT 360 (2018)
             */
            // FALLTHROUGH
        case 0x8a:
            /* Service Provider Information Report and Acknowledgment
             * Present in:
             *   pre-2000 models
             * Not Present in:
             *   Copernicus II (2009)
             *   ICM SMT 360 (2018)
             *   RES SMT 360 (2018)
             */
            // FALLTHROUGH
        case 0x8b:
            /* Service Provider Activation Information Report & Acknowledgment
             * Present in:
             *   pre-2000 models
             * Not Present in:
             *   Copernicus II (2009)
             *   ICM SMT 360 (2018)
             *   RES SMT 360 (2018)
             */
            // FALLTHROUGH
        case 0x8e:
            /* Service Provider Data Load Report
             * Present in:
             *   pre-2000 models
             * Not Present in:
             *   Copernicus II (2009)
             *   ICM SMT 360 (2018)
             *   RES SMT 360 (2018)
             */
            // FALLTHROUGH
        case 0x8f:
            /* Receiver Identity Report
             * Present in:
             *   pre-2000 models
             * Not Present in:
             *   Copernicus II (2009)
             *   ICM SMT 360 (2018)
             *   RES SMT 360 (2018)
             */
            // FALLTHROUGH
        case 0x90:
            /* Guidance Status Report
             * Present in:
             *   pre-2000 models
             * Not Present in:
             *   Copernicus II (2009)
             *   ICM SMT 360 (2018)
             *   RES SMT 360 (2018)
             */
            // FALLTHROUGH
        case 0x91:
            /* Guidance Configuration Report
             * Present in:
             *   pre-2000 models
             * Not Present in:
             *   Copernicus II (2009)
             *   ICM SMT 360 (2018)
             *   RES SMT 360 (2018)
             */
            // FALLTHROUGH
        case 0x92:
            /* Lightbar Configuration Report
             * Present in:
             *   pre-2000 models
             * Not Present in:
             *   Copernicus II (2009)
             *   ICM SMT 360 (2018)
             *   RES SMT 360 (2018)
             */
            // FALLTHROUGH
        case 0x94:
            /* Guidance Operation Acknowledgment
             * Present in:
             *   pre-2000 models
             * Not Present in:
             *   Copernicus II (2009)
             *   ICM SMT 360 (2018)
             *   RES SMT 360 (2018)
             */
            // FALLTHROUGH
        case 0x95:
            /* Button Box Configuration Type Report
             * Present in:
             *   pre-2000 models
             * Not Present in:
             *   Copernicus II (2009)
             *   ICM SMT 360 (2018)
             *   RES SMT 360 (2018)
             */
            // FALLTHROUGH
        case 0x96:
            /* Point Manipulation Report
             * Present in:
             *   pre-2000 models
             * Not Present in:
             *   Copernicus II (2009)
             *   ICM SMT 360 (2018)
             *   RES SMT 360 (2018)
             */
            // FALLTHROUGH
        case 0x97:
            /* Utility Information Report
             * Present in:
             *   pre-2000 models
             * Not Present in:
             *   Copernicus II (2009)
             *   ICM SMT 360 (2018)
             *   RES SMT 360 (2018)
             */
            // FALLTHROUGH
        case 0x98:
            /* Individual Button Configuration Report
             * Present in:
             *   pre-2000 models
             * Not Present in:
             *   Copernicus II (2009)
             *   ICM SMT 360 (2018)
             *   RES SMT 360 (2018)
             */
            // FALLTHROUGH
        case 0x9a:
            /* Differential Correction Information Report
             * Present in:
             *   pre-2000 models
             * Not Present in:
             *   Copernicus II (2009)
             *   ICM SMT 360 (2018)
             *   RES SMT 360 (2018)
             */
            // FALLTHROUGH
        case 0xa0:
            /* DAC value
             * Present in:
             *   ICM SMT 360 (2018)
             *   RES SMT 360 (2018)
             * Not Present in:
             *   pre-2000 models
             *   Copernicus II (2009)
             */
            // FALLTHROUGH
        case 0xa2:
            /* UTC/GPS timing
             * Present in:
             *   ICM SMT 360 (2018)
             *   RES SMT 360 (2018)
             * Not Present in:
             *   pre-2000 models
             *   Copernicus II (2009)
             */
            // FALLTHROUGH
        case 0xa3:
            /* Oscillator disciplining command
             * Present in:
             *   ICM SMT 360 (2018)
             *   RES SMT 360 (2018)
             * Not Present in:
             *   pre-2000 models
             *   Copernicus II (2009)
             */
            // FALLTHROUGH
        case 0xa8:
            /* Oscillator disciplining parameters
             * Present in:
             *   ICM SMT 360 (2018)
             *   RES SMT 360 (2018)
             * Not Present in:
             *   pre-2000 models
             *   Copernicus II (2009)
             */
            // FALLTHROUGH
        case 0xa9:
            /* self-survey parameters
             * Present in:
             *   ICM SMT 360 (2018)
             *   RES SMT 360 (2018)
             * Not Present in:
             *   pre-2000 models
             *   Copernicus II (2009)
             */
            // FALLTHROUGH
        default:
            GPSD_LOG(LOG_WARN, &session->context->errout,
                     "TSIP: Unhandled TSIP superpacket type 0x8f-%02x\n",
                     u1);
        }
        break;
    case 0xbb:
        /* Navigation Configuration
         * Present in:
         *   pre-2000 models
         *   Copernicus II (2009)
         *   ICM SMT 360 (2018)
         *   RES SMT 360 (2018)
         */
        if (len != 40 && len != 43) {
            /* see packet.c for explamation */
            bad_len = 40;
            break;
        }
        u1 = getub(buf, 0);             // Subcode, always zero?
        u2 = getub(buf, 1);             // Operating Dimension
        u3 = getub(buf, 2);             // DGPS Mode (not in Acutime Gold)
        u4 = getub(buf, 3);             // Dynamics Code
        f1 = getbef32((char *)buf, 5);  // Elevation Mask
        f2 = getbef32((char *)buf, 9);  // AMU Mask
        f3 = getbef32((char *)buf, 13); // DOP Mask
        f4 = getbef32((char *)buf, 17); // DOP Switch
        u5 = getub(buf, 21);            // DGPS Age Limit (not in Acutime Gold)
        /* Constellation
         * bit 0 - GPS
         * bit 1 - GLONASS
         * bit 2 - reserved
         * bit 3 - BeiDou
         * bit 4 - Galileo
         * bit 5 - QZSS
         * bit 6 - reserved
         * bit 7 - reserved
         */
        u6 = getub(buf, 27);
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "TSIP: Navigation Configuration (0xbb) %u %u %u %u %f %f %f "
                 "%f %u x%x\n",
                 u1, u2, u3, u4, f1, f2, f3, f4, u5, u6);
        // RES SMT 360 defaults to Mode 7, Constellation 3
        break;

    case 0x1a:
        /* TSIP RTCM Wrapper Command
         * Present in:
         *   pre-2000 models
         * Not Present in:
         *   ICM SMT 360 (2018)
         *   RES SMT 360 (2018)
         *   Copernicus II (2009)
         */
        // FALLTHROUGH
    case 0x2e:
        /* Request GPS Time
         * Present in:
         *   ICM SMT 360 (2018)
         *   RES SMT 360 (2018)
         * Not Present in:
         *   pre-2000 models
         *   Copernicus II (2009)
         */
        // FALLTHROUGH
    case 0x32:
        /* Request Unit Position
         * Present in:
         *   ICM SMT 360 (2018)
         *   RES SMT 360 (2018)
         * Not Present in:
         *   pre-2000 models
         *   Copernicus II (2009)
         */
        // FALLTHROUGH
    case 0x38:
        /* Request SV System data
         * Present in:
         *   ICM SMT 360 (2018)
         *   RES SMT 360 (2018)
         * Not Present in:
         *   pre-2000 models
         *   Copernicus II (2009)
         */
        // FALLTHROUGH
    case 0x40:
        /* Almanac Data for Single Satellite Report
         * Present in:
         *   pre-2000 models
         * Not Present in:
         *   ICM SMT 360 (2018)
         *   RES SMT 360 (2018)
         *   Copernicus II (2009)
         */
        // FALLTHROUGH
    case 0x44:
        /* Non-Overdetermined Satellite Selection Report
         * Present in:
         *   pre-2000 models
         * Not Present in:
         *   ICM SMT 360 (2018)
         *   RES SMT 360 (2018)
         *   Copernicus II (2009)
         */
        // FALLTHROUGH
    case 0x49:
        /* Almanac Health Page
         * Present in:
         *   pre-2000 models
         * Not Present in:
         *   Copernicus II (2009)
         */
        // FALLTHROUGH
    case 0x4d:
        /* Oscillator Offset
         * Present in:
         *   pre-2000 models
         *   Copernicus II (2009)
         */
        // FALLTHROUGH
    case 0x4e:
        /* Response to set GPS time
         * Present in:
         *   pre-2000 models
         *   Copernicus II (2009)
         *   ICM SMT 360 (2018)
         *   RES SMT 360 (2018)
         */
        // FALLTHROUGH
    case 0x4f:
        /* UTC Parameters Report
         * Present in:
         *   pre-2000 models
         * Not Present in:
         *   ICM SMT 360 (2018)
         *   RES SMT 360 (2018)
         *   Copernicus II (2009)
         */
        // FALLTHROUGH
    case 0x53:
        /* Analog-to-Digital Readings Report
         * Present in:
         *   pre-2000 models
         * Not Present in:
         *   ICM SMT 360 (2018)
         *   RES SMT 360 (2018)
         *   Copernicus II (2009)
         */
        // FALLTHROUGH
    case 0x58:
        /* Satellite System Data/Acknowledge from Receiver
         * Present in:
         *   pre-2000 models
         *   Copernicus II (2009)
         *   ICM SMT 360 (2018)
         *   RES SMT 360 (2018)
         */
        // FALLTHROUGH
    case 0x59:
        /* Status of Satellite Disable or Ignore Health
         * aka Satellite Attribute Database Status Report
         * Present in:
         *   pre-2000 models
         *   ICM SMT 360 (2018)
         *   RES SMT 360 (2018)
         * Not Present in:
         *   Copernicus II (2009)
         */
        // FALLTHROUGH
    case 0x5b:
        /* Satellite Ephemeris Status
         * Present in:
         *   pre-2000 models
         * Not Present in:
         *   Copernicus II (2009)
         *   ICM SMT 360 (2018)
         *   RES SMT 360 (2018)
         */
        // FALLTHROUGH
    case 0x5e:
        /* Additional Fix Status Report
         * Present in:
         *   pre-2000 models
         * Not Present in:
         *   Copernicus II (2009)
         *   ICM SMT 360 (2018)
         *   RES SMT 360 (2018)
         */
        // FALLTHROUGH
    case 0x5f:
        /* Severe Failure Notification
         * Present in:
         *   pre-2000 models
         * Not Present in:
         *   ICM SMT 360 (2018)
         *   RES SMT 360 (2018)
         *   Copernicus II (2009)
         */
        // FALLTHROUGH
    case 0x60:
        /* Differential GPS Pseudorange Corrections Report
         * Present in:
         *   pre-2000 models
         * Not Present in:
         *   ICM SMT 360 (2018)
         *   RES SMT 360 (2018)
         *   Copernicus II (2009)
         */
        // FALLTHROUGH
    case 0x61:
        /* Differential GPS Delta Pseudorange Corrections Report
         * Present in:
         *   pre-2000 models
         * Not Present in:
         *   ICM SMT 360 (2018)
         *   RES SMT 360 (2018)
         *   Copernicus II (2009)
         */
        // FALLTHROUGH
    case 0x6a:
        /* Differential Corrections Used in the Fix Repor
         * Present in:
         *   pre-2000 models
         * Not Present in:
         *   ICM SMT 360 (2018)
         *   RES SMT 360 (2018)
         *   Copernicus II (2009)
         */
        // FALLTHROUGH
    case 0x6e:
        /* Synchronized Measurements
         * Present in:
         *   pre-2000 models
         * Not Present in:
         *   Copernicus II (2009)
         *   ICM SMT 360 (2018)
         *   RES SMT 360 (2018)
         */
        // FALLTHROUGH
    case 0x6f:
        /* Synchronized Measurements Report
         * Present in:
         *   pre-2000 models
         * Not Present in:
         *   Copernicus II (2009)
         *   ICM SMT 360 (2018)
         *   RES SMT 360 (2018)
         */
        // FALLTHROUGH
    case 0x70:
        /* Filter Report
         * Present in:
         *   pre-2000 models
         * Not Present in:
         *   Copernicus II (2009)
         *   ICM SMT 360 (2018)
         *   RES SMT 360 (2018)
         */
        // FALLTHROUGH
    case 0x76:
        /* Overdetermined Mode Report
         * Present in:
         *   pre-2000 models
         * Not Present in:
         *   ICM SMT 360 (2018)
         *   RES SMT 360 (2018)
         *   Copernicus II (2009)
         */
        // FALLTHROUGH
    case 0x78:
        /* Maximum PRC Age Report
         * Present in:
         *   pre-2000 models
         * Not Present in:
         *   ICM SMT 360 (2018)
         *   RES SMT 360 (2018)
         *   Copernicus II (2009)
         */
        // FALLTHROUGH
    case 0x7a:
        /* NMEA settings
         * Not Present in:
         *   pre-2000 models
         *   Copernicus II (2009)
         *   ICM SMT 360 (2018)
         *   RES SMT 360 (2018)
         */
        // FALLTHROUGH
    case 0x7b:
        /* NMEA interval and message mask response
         * Present in:
         *   pre-2000 models
         *   ICM SMT 360 (2018)
         *   RES SMT 360 (2018)
         * Not Present in:
         *   Copernicus II (2009)
         */
        // FALLTHROUGH
    case 0x7d:
        /* Position Fix Rate Configuration Reports
         * Present in:
         *   pre-2000 models
         * Not Present in:
         *   ICM SMT 360 (2018)
         *   RES SMT 360 (2018)
         *   Copernicus II (2009)
         */
        // FALLTHROUGH
    case 0x85:
        /* Differential Correction Status Report
         * Present in:
         *   pre-2000 models
         * Not Present in:
         *   ICM SMT 360 (2018)
         *   RES SMT 360 (2018)
         *   Copernicus II (2009)
         */
        // FALLTHROUGH
    case 0x87:
        /* Reference Station Parameters Report
         * Present in:
         *   pre-2000 models
         * Not Present in:
         *   ICM SMT 360 (2018)
         *   RES SMT 360 (2018)
         *   Copernicus II (2009)
         */
        // FALLTHROUGH
    case 0x89:
        /* Receiver acquisition sensitivity mode
         * Present in:
         *   Copernicus II (2009)
         * Not Present in:
         *   pre-2000 models
         *   ICM SMT 360 (2018)
         *   RES SMT 360 (2018)
         */
        // FALLTHROUGH
    case 0x88:
        /* Mobile Differential Parameters Report
         * Present in:
         *   pre-2000 models
         * Not Present in:
         *   ICM SMT 360 (2018)
         *   RES SMT 360 (2018)
         *   Copernicus II (2009)
         */
        // FALLTHROUGH
    case 0x8b:
        /* QA/QC Reports
         * Present in:
         *   pre-2000 models
         * Not Present in:
         *   ICM SMT 360 (2018)
         *   RES SMT 360 (2018)
         *   Copernicus II (2009)
         */
        // FALLTHROUGH
    case 0x8d:
        /* Average Position Reports
         * Present in:
         *   pre-2000 models
         * Not Present in:
         *   ICM SMT 360 (2018)
         *   RES SMT 360 (2018)
         *   Copernicus II (2009)
         */
        // FALLTHROUGH
    case 0xb0:
        /* PPS and Event Report Packets
         * Present in:
         *   pre-2000 models
         * Not Present in:
         *   ICM SMT 360 (2018)
         *   RES SMT 360 (2018)
         *   Copernicus II (2009)
         */
        // FALLTHROUGH
    case 0xbc:
        /* Receiver port configuration
         * Present in:
         *   pre-2000 models
         *   Copernicus II (2009)
         * Not Present in:
         *   ICM SMT 360 (2018)
         *   RES SMT 360 (2018)
         */
        // FALLTHROUGH
    case 0xc1:
        /* Bit Mask for GPIOs in Standby Mode
         * Present in:
         *   Copernicus II (2009)
         *   ICM SMT 360 (2018)
         *   RES SMT 360 (2018)
         * Not Present in:
         *   pre-2000 models
         */
        // FALLTHROUGH
    case 0xc2:
        /* SBAS SV Mask
         * Present in:
         *   Copernicus II (2009)
         *   ICM SMT 360 (2018)
         *   RES SMT 360 (2018)
         * Not Present in:
         *   pre-2000 models
         */
        // FALLTHROUGH
    default:
        GPSD_LOG(LOG_WARN, &session->context->errout,
                 "TSIP: Unhandled packet type x%02x\n", id);
        break;
    }

#ifdef __UNUSED__
// #if 1
    // full reset
    putbyte(buf, 0, 0x46);
    (void)tsip_write(session, 0x1e, buf, 1);
#endif

    if (bad_len) {
        GPSD_LOG(LOG_WARNING, &session->context->errout,
                 "TSIP: ID x%02x wrong len %d s/b >= %d \n", id, len, bad_len);
    } else {
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "TSIP: ID x%02x mask %s\n", id, gps_maskdump(mask));
    }
    /* See if it is time to send some request packets for reports that.
     * The receiver won't send at fixed intervals */

    if ((now - session->driver.tsip.last_41) > 5) {
        /* Request Current Time returns 0x41.
         * Easiest way to get GPS weeks and current leap seconds */
        (void)tsip_write(session, 0x21, buf, 0);
        session->driver.tsip.last_41 = now;
    }

    if ((now - session->driver.tsip.last_6d) > 5) {
        /* Request GPS Receiver Position Fix Mode
         * Returns 0x44, 0x6c, or 0x6d. */
        (void)tsip_write(session, 0x24, buf, 0);
        session->driver.tsip.last_6d = now;
#ifdef __UNUSED__
// #if 1
        // request Receiver Configuration (0xbb)
        putbyte(buf, 0, 0x00);
        (void)tsip_write(session, 0xbb, buf, 1);
        // request Packet Broadcast Mask
        putbyte(buf, 0, 0xa5);
        (void)tsip_write(session, 0x8e, buf, 1);
#endif // UNUSED
    }

    if (1 > session->driver.tsip.superpkt &&
        (now - session->driver.tsip.last_48) > 60) {
        /* Request GPS System Message
         * Returns 0x48.
         * not supported on:
         *  Lassen SQ (2002)
         *  Lassen iQ (2005)
         *  ICM SMT 360
         *  RES SMT 360
         *  and post 2005
         * SuperPackets replaced 0x28 */
        (void)tsip_write(session, 0x28, buf, 0);
        session->driver.tsip.last_48 = now;
    }

    if ((now - session->driver.tsip.last_5c) >= 5) {
        /* Request Current Satellite Tracking Status
         * Returns: 0x5c or 0x5d
         *  5c in PS only devices
         *  5d in multi-gnss devices */
        putbyte(buf, 0, 0x00);  /* All satellites */
        (void)tsip_write(session, 0x3c, buf, 1);
        session->driver.tsip.last_5c = now;
    }

    if ((now - session->driver.tsip.last_46) > 5) {
        /* Request Health of Receiver
         * Returns 0x46 and 0x4b. */
        (void)tsip_write(session, 0x26, buf, 0);
        session->driver.tsip.last_46 = now;
    }
    if ((session->driver.tsip.req_compact > 0) &&
        ((now - session->driver.tsip.req_compact) > 5)) {
        /* Compact Superpacket requested but no response
         * Not in:
         * ICM SMT 360
         * RES SMT 360
          */
        session->driver.tsip.req_compact = 0;
        GPSD_LOG(LOG_WARN, &session->context->errout,
                 "TSIP: No Compact Super Packet (0x8f-23), "
                 "try LFwEI (0x8f-20)\n");

        /* Request LFwEI Super Packet 0x8f-20 */
        putbyte(buf, 0, 0x20);
        putbyte(buf, 1, 0x01);  /* enabled */
        (void)tsip_write(session, 0x8e, buf, 2);
    }

    return mask;
}

#ifdef CONTROLSEND_ENABLE
static ssize_t tsip_control_send(struct gps_device_t *session,
                                 char *buf, size_t buflen)
/* not used by the daemon, it's for gpsctl and friends */
{
    return (ssize_t) tsip_write(session,
                                (unsigned int)buf[0],
                                (unsigned char *)buf + 1, buflen - 1);
}
#endif /* CONTROLSEND_ENABLE */

static void tsip_init_query(struct gps_device_t *session)
{
    unsigned char buf[100];

    /* Use 0x1C-03 to Request Hardware Version Information (0x1C-83) */
    putbyte(buf, 0, 0x03); /* Subcode */
    (void)tsip_write(session, 0x1c, buf, 1);
    /*
     * After HW information packet is received, a
     * decision is made how to configure the device.
     */
}

static void tsip_event_hook(struct gps_device_t *session, event_t event)
{
    unsigned char buf[100];

    GPSD_LOG(LOG_SPIN, &session->context->errout,
             "TSIP: event_hook event %d ro %d\n",
             event, session->context->readonly);

    if (session->context->readonly)
        return;
    switch (event) {
    case event_identified:
        // FALLTHROUGH
    case event_reactivate:
        // FIXME: reactivate style should depend on model
        /*
         * Set basic configuration, using Set or Request I/O Options (0x35).
         * in case no hardware config response comes back.
         */
        /* Position: enable: Double Precision, LLA
         *           disable: ECEF */
        putbyte(buf, 0, IO1_8F20|IO1_DP|IO1_LLA);
        /* Velocity: enable: ENU, disable vECEF */
        putbyte(buf, 1, IO2_ENU);
        /* Time: enable: 0x42, 0x43, 0x4a
         *       disable: 0x83, 0x84, 0x56 */
        putbyte(buf, 2, 0x00);
        /* Aux: enable: 0x5A, dBHz */
        putbyte(buf, 3, IO4_DBHZ);
        (void)tsip_write(session, 0x35, buf, 4);
        break;
    case event_configure:
        // this seems to get called on every packet...
        if (session->lexer.counter == 0) {
            /* but the above if() makes it never execute
             * formerely tried to force 801 here, but luckily it
             * never fired as some Trimble are 8N1 */
        }
        break;
    case event_deactivate:
        // used to revert serial port parms here.  No need   for that.
        // FALLTHROUGH
    default:
        break;
    }
}

#ifdef RECONFIGURE_ENABLE
static bool tsip_speed_switch(struct gps_device_t *session,
                              speed_t speed, char parity, int stopbits)
{
    unsigned char buf[100];

    switch (parity) {
    case 'E':
    case 2:
        parity = (char)2;
        break;
    case 'O':
    case 1:
        parity = (char)1;
        break;
    case 'N':
    case 0:
    default:
        parity = (char)0;
        break;
    }

    /* Set Port Configuration (0xbc) */
    putbyte(buf, 0, 0xff);      /* current port */
    /* input dev.baudrate */
    putbyte(buf, 1, (round(log((double)speed / 300) / GPS_LN2)) + 2);
    putbyte(buf, 2, getub(buf, 1));     /* output baudrate */
    putbyte(buf, 3, 3);         /* character width (8 bits) */
    putbyte(buf, 4, parity);    /* parity (normally odd) */
    putbyte(buf, 5, stopbits - 1);      /* stop bits (normally 1 stopbit) */
    putbyte(buf, 6, 0);         /* flow control (none) */
    putbyte(buf, 7, 0x02);      /* input protocol (TSIP) */
    putbyte(buf, 8, 0x02);      /* output protocol (TSIP) */
    putbyte(buf, 9, 0);         /* reserved */
    (void)tsip_write(session, 0xbc, buf, 10);

    return true;                /* it would be nice to error-check this */
}

static void tsip_mode(struct gps_device_t *session, int mode)
{
    if (mode == MODE_NMEA) {
        unsigned char buf[16];

        /* send NMEA Interval and Message Mask Command (0x7a)
        * First turn on the NMEA messages we want */
        putbyte(buf, 0, 0x00);  /* subcode 0 */
        putbyte(buf, 1, 0x01);  /* 1-second fix interval */
        putbyte(buf, 2, 0x00);  /* Reserved */
        putbyte(buf, 3, 0x00);  /* Reserved */
        putbyte(buf, 4, 0x01);  /* 1=GST, Reserved */
        /* 1=GGA, 2=GGL, 4=VTG, 8=GSV, */
        /* 0x10=GSA, 0x20=ZDA, 0x40=Reserved, 0x80=RMC  */
        putbyte(buf, 5, 0x19);

        (void)tsip_write(session, 0x7A, buf, 6);

        /* Now switch to NMEA mode */

        memset(buf, 0, sizeof(buf));

        /* Set Port Configuration (0xbc) */
        // 4800, really?
        putbyte(buf, 0, 0xff);  /* current port */
        putbyte(buf, 1, 0x06);  /* 4800 bps input */
        putbyte(buf, 2, 0x06);  /* 4800 bps output */
        putbyte(buf, 3, 0x03);  /* 8 data bits */
        putbyte(buf, 4, 0x00);  /* No parity */
        putbyte(buf, 5, 0x00);  /* 1 stop bit */
        putbyte(buf, 6, 0x00);  /* No flow control */
        putbyte(buf, 7, 0x02);  /* Input protocol TSIP */
        putbyte(buf, 8, 0x04);  /* Output protocol NMEA */
        putbyte(buf, 9, 0x00);  /* Reserved */

        (void)tsip_write(session, 0xBC, buf, 10);

    } else if (mode == MODE_BINARY) {
        /* The speed switcher also puts us back in TSIP, so call it */
        /* with the default 9600 8O1. */
        // FIXME: Should preserve the current speed.
        // (void)tsip_speed_switch(session, 9600, 'O', 1);
        // FIXME: should config TSIP binary!
        ;

    } else {
        GPSD_LOG(LOG_ERROR, &session->context->errout,
                 "TSIP: unknown mode %i requested\n", mode);
    }
}
#endif /* RECONFIGURE_ENABLE */

/* configure generic Trimble TSIP device to a known state */
void configuration_packets_generic(struct gps_device_t *session)
{
        unsigned char buf[100];

        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "TSIP: configuration_packets_generic()\n");

        // Set basic configuration, using Set or Request I/O Options (0x35).
        /* Position: enable: Double Precision, LLA
         *           disable: ECEF */
        putbyte(buf, 0, IO1_8F20|IO1_DP|IO1_LLA);
        /* Velocity: enable: ENU, disable ECEF */
        putbyte(buf, 1, IO2_ENU);
        /* Time: enable: 0x42, 0x43, 0x4a
         *       disable: 0x83, 0x84, 0x56 */
        putbyte(buf, 2, 0x00);
        /* Aux: enable: 0x5A, dBHz */
        putbyte(buf, 3, IO4_DBHZ);
        (void)tsip_write(session, 0x35, buf, 4);

        /* Request Software Version (0x1f), returns 0x45 */
        (void)tsip_write(session, 0x1f, NULL, 0);

        /* Current Time Request (0x21), returns 0x41 */
        (void)tsip_write(session, 0x21, NULL, 0);

        /* Set Operating Parameters (0x2c)
         * not present in:
         *   Lassen SQ (2002)
         *   Lassen iQ (2005)
         *   RES SMT 360 */
        /* dynamics code: enabled: 1=land
         *   disabled: 2=sea, 3=air, 4=static
         *   default is land */
        putbyte(buf, 0, 0x01);
        /* elevation mask, 10 degrees is a common default,
         * TSIP default is 15 */
        putbef32((char *)buf, 1, (float)10.0 * DEG_2_RAD);
        /* signal level mask
         * default is 2.0 AMU. 5.0 to 6.0 for high accuracy */
        putbef32((char *)buf, 5, (float)06.0);
        /* PDOP mask
         * default is 12. 5.0 to 6.0 for high accuracy */
        putbef32((char *)buf, 9, (float)8.0);
        /* PDOP switch
         * default is 8.0 */
        putbef32((char *)buf, 13, (float)6.0);
        (void)tsip_write(session, 0x2c, buf, 17);

        /* Set Position Fix Mode (0x22)
         * 0=auto 2D/3D, 1=time only, 3=2D, 4=3D, 10=Overdetermined clock */
        putbyte(buf, 0, 0x00);
        (void)tsip_write(session, 0x22, buf, 1);

        /* Request GPS System Message (0x48)
         * not supported on model RES SMT 360 */
        (void)tsip_write(session, 0x28, NULL, 0);

        /* Last Position and Velocity Request (0x37)
         * returns 0x57 and (0x42, 0x4a, 0x83, or 0x84) and (0x43 or 0x56)  */
        (void)tsip_write(session, 0x37, NULL, 0);
        putbyte(buf, 0, 0x15);
        (void)tsip_write(session, 0x8e, buf, 1);

        /* Primary Receiver Configuration Parameters Request (0xbb-00)
         * returns  Primary Receiver Configuration Block (0xbb-00) */
        putbyte(buf, 0, 0x00);
        (void)tsip_write(session, 0xbb, buf, 1);
}

/* configure Acutime Gold to a known state */
void configuration_packets_acutime_gold(struct gps_device_t *session)
{
        unsigned char buf[100];

        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "TSIP: configuration_packets_acutime_gold()\n");

        /* Request Firmware Version (0x1c-01)
         * returns Firmware component version information (0x1x-81) */
        putbyte(buf, 0, 0x01);
        (void)tsip_write(session, 0x1c, buf, 1);

        /* Set Self-Survey Parameters (0x8e-a9) */
        putbyte(buf, 0, 0xa9); /* Subcode */
        putbyte(buf, 1, 0x01); /* Self-Survey Enable = enable */
        putbyte(buf, 2, 0x01); /* Position Save Flag = save position */
        putbe32(buf, 3, 2000); /* Self-Survey Length = 2000 fixes */
        /* Horizontal Uncertainty, 1-100, 1=best, 100=worst,
         *    default 100 */
        putbe32(buf, 7, 0);
        /* Verical Uncertainty, 1-100, 1=best, 100=worst,
         *    default 100
         * not present in RES SMT 360 */
        (void)tsip_write(session, 0x8e, buf, 11);

        /* Set PPS Output Option (0x8e-4e) */
        putbyte(buf, 0, 0x4e); /* Subcode */
        /* PPS driver switch = 2 (PPS is always output) */
        putbyte(buf, 1, 2);
        (void)tsip_write(session, 0x8e, buf, 2);

        /* Set Primary Receiver Configuration (0xbb-00) */
        putbyte(buf, 0, 0x00);   /* Subcode */
        /* Receiver mode, 7 = Force Overdetermined clock */
        putbyte(buf, 1, 0x07);
        /* Not enabled = unchanged
         * must be 0xff on RES SMT 360 */
        putbyte(buf, 2, 0xff);
        /* Dynamics code = default
         * must be 0xff on RES SMT 360 */
        putbyte(buf, 3, 0x01);
        /* Solution Mode = default
         * must be 0xff on RES SMT 360 */
        putbyte(buf, 4, 0x01);
        /* Elevation Mask = 10 deg */
        putbef32((char *)buf, 5, (float)10.0 * DEG_2_RAD);
        /* AMU Mask. 0 to 55. default is 4.0 */
        putbef32((char *)buf, 9, (float)4.0);
        /* PDOP Mask = 8.0, default = 6 */
        putbef32((char *)buf, 13, (float)8.0);
        /* PDOP Switch = 6.0, ignored in RES SMT 360 */
        putbef32((char *)buf, 17, (float)6.0);
        /* must be 0xff */
        putbyte(buf, 21, 0xff);
        /* Anti-Jam Mode, 0=Off, 1=On */
        putbyte(buf, 22, 0x0);
        /* Reserved.  Must be 0xffff */
        putbe16(buf, 23, 0xffff);
        /* Measurement Rate and Position Fix Rate = default
         * must be 0xffff on res smt 360 */
        putbe16(buf, 25, 0x0000);
        /* 27 is Constellation on RES SMT 360.
         * 1 = GPS, 2=GLONASS, 8=BeiDou, 0x10=Galileo, 5=QZSS */
        putbe32(buf, 27, 0xffffffff); /* Reserved */
        putbe32(buf, 31, 0xffffffff); /* Reserved */
        putbe32(buf, 35, 0xffffffff); /* Reserved */
        putbe32(buf, 39, 0xffffffff); /* Reserved */
        (void)tsip_write(session, 0xbb, buf, 43);

        /* Set Packet Broadcast Mask (0x8e-a5) */
        putbyte(buf, 0, 0xa5); /* Subcode */
        /* Packets bit field = default + Primary timing,
         *  Supplemental timing 32e1
         *  1=0x8f-ab, 4=0x8f-ac, 0x40=Automatic Output Packets */
        putbe16(buf, 1, 0x32e1);
        putbyte(buf, 3, 0x00); /* not used */
        putbyte(buf, 4, 0x00); /* not used */
        (void)tsip_write(session, 0x8e, buf, 5);
}

/* configure RES 360 to a known state */
void configuration_packets_res360(struct gps_device_t *session)
{
    unsigned char buf[100];

    GPSD_LOG(LOG_PROG, &session->context->errout,
             "TSIP: configuration_packets_res360()\n");

    // should already have versions 0x8f-81 and 0x8f-83.
    /* Self-Survey Parameters (0x8e-a9) is default on
     * query them? */

    /* PPS Output Option (0x8e-4e) is default on */

    /* Set Packet Broadcast Mask (0x8e-a5)
     * RES SMT 360 default 5, 0 */
    putbyte(buf, 0, 0xa5); /* Subcode */
    /* Packets bit field = default + Auto output packets
     *  1=0x8f-ab, 4=0x8f-ac, 0x40=Automatic Output Packets */
    putbe16(buf, 1, 0x0045);
    putbe16(buf, 3, 0x0000);
    (void)tsip_write(session, 0x8e, buf, 5);

    // set I/O Options
    // RES SMT 360 defaults:  12 02 00 08
    // position and velocity only sent during self-survey.
    // Position
    putbyte(buf, 0, IO1_DP|IO1_LLA|IO1_ECEF);
    // Velocity
    putbyte(buf, 1, IO2_VECEF|IO2_ENU);
    // Timing
    putbyte(buf, 2, 0x01);          // Use 0x8e-a2
    // Auxillary
    putbyte(buf, 3, 0x08);          // Packet 0x5a off, dBHz
    (void)tsip_write(session, 0x35, buf, 4);

#ifdef __UNUSED__
    // request I/O Options (0x55)
    (void)tsip_write(session, 0x35, buf, 0);

    // request Receiver Configuration (0xbb)
    putbyte(buf, 0, 0x00);
    (void)tsip_write(session, 0xbb, buf, 1);
    // Restart Self-Survey (0x8e-a6)
    // which gives us 2,000 normal fixes, before going quiet again.
    putbyte(buf, 0, 0xa6);
    putbyte(buf, 1, 0x00);
    (void)tsip_write(session, 0x8e, buf, 2);
#endif // __UNUSED__
}

/* this is everything we export */
/* *INDENT-OFF* */
const struct gps_type_t driver_tsip =
{
    .type_name      = "Trimble TSIP",   /* full name of type */
    .packet_type    = TSIP_PACKET,      /* associated lexer packet type */
    .flags          = DRIVER_STICKY,    /* remember this */
    .trigger        = NULL,             /* no trigger */
    .channels       = TSIP_CHANNELS,    /* consumer-grade GPS */
    .probe_detect   = tsip_detect,      /* probe for 9600O81 device */
    .get_packet     = generic_get,      /* use the generic packet getter */
    .parse_packet   = tsip_parse_input, /* parse message packets */
    .rtcm_writer    = NULL,             /* doesn't accept DGPS corrections */
    .init_query     = tsip_init_query,  /* non-perturbing initial query */
    .event_hook     = tsip_event_hook,  /* fire on various lifetime events */
#ifdef RECONFIGURE_ENABLE
    .speed_switcher = tsip_speed_switch,/* change baud rate */
    .mode_switcher  = tsip_mode,        /* there is a mode switcher */
    .rate_switcher  = NULL,             /* no rate switcher */
    .min_cycle.tv_sec  = 1,             /* not relevant, no rate switch */
    .min_cycle.tv_nsec = 0,             /* not relevant, no rate switch */
#endif /* RECONFIGURE_ENABLE */
#ifdef CONTROLSEND_ENABLE
    .control_send   = tsip_control_send,/* how to send commands */
#endif /* CONTROLSEND_ENABLE */
    .time_offset     = NULL,
};
/* *INDENT-ON* */

#endif /* TSIP_ENABLE */
