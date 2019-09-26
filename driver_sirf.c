/*
 * This is the gpsd driver for SiRF GPSes operating in binary mode.
 * It also handles early u-bloxes that were SiRF derivatives.
 *
 * The advantages: Reports climb/sink rate (raw-mode clients won't see this).
 * Also, we can flag DGPS satellites used in the skyview when SBAS is in use.
 * The disadvantages: Doesn't return PDOP or VDOP, just HDOP.
 *
 * Chris Kuethe, our SiRF expert, tells us:
 *
 * "I don't see any indication in any of my material that PDOP, GDOP
 * or VDOP are output. There are quantities called Estimated
 * {Horizontal Position, Vertical Position, Time, Horizonal Velocity}
 * Error, but those are apparently only valid when SiRFDRive is
 * active."
 *
 * "(SiRFdrive is their Dead Reckoning augmented firmware. It
 * allows you to feed odometer ticks, gyro and possibly
 * accelerometer inputs to the chip to allow it to continue
 * to navigate in the absence of satellite information, and
 * to improve fixes when you do have satellites.)"
 *
 * "[When we need RINEX data, we can get it from] SiRF Message #5.
 *  If it's no longer implemented on your receiver, messages
 * 7, 28, 29 and 30 will give you the same information."
 *
 * There is a known problem with the SiRF IV: it is prone to freeze
 * when being switched back to NMEA mode from SiRF binary. The
 * failure is randomly flaky, you may get away with several mode
 * flips before triggering it.  Powering off the device resets and
 * unfreezes it. We have tries waiting on command acknowledges as
 * the manual advises; this does not fix the problem.
 *
 * This file is Copyright (c) 2010-2018 by the GPSD project
 * SPDX-License-Identifier: BSD-2-clause
 */

#include "gpsd_config.h"  /* must be before all includes */

#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "gpsd.h"
#include "bits.h"
#include "strfuncs.h"
#include "timespec.h"
#if defined(SIRF_ENABLE) && defined(BINARY_ENABLE)

#define HI(n)           ((n) >> 8)
#define LO(n)           ((n) & 0xff)

/*
 * According to the protocol reference, if you don't get ACK/NACK in response
 * to a control send withing 6 seconds, you should just retry.
 */
#define SIRF_RETRY_TIME 6

/* Poll Software Version MID 132 */
static unsigned char versionprobe[] = {
    0xa0, 0xa2, 0x00, 0x02,
    0x84,               /* MID 132 */
    0x00,               /* unused */
    0x00, 0x00, 0xb0, 0xb3
};

#ifdef RECONFIGURE_ENABLE
/* Poll Navigation Parameters MID 152
 * query for MID 19 */
static unsigned char navparams[] = {
    0xa0, 0xa2, 0x00, 0x02,
    0x98,               /* MID 152 */
    0x00,
    0x00, 0x00, 0xb0, 0xb3
};

/* DGPS Source MID 133 */
static unsigned char dgpscontrol[] = {
    0xa0, 0xa2, 0x00, 0x07,
    0x85,               /* MID 133 */
    0x01,               /* use SBAS */
    0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0xb0, 0xb3
};

/* Set SBAS Parameters MID 170 */
static unsigned char sbasparams[] = {
    0xa0, 0xa2, 0x00, 0x06,
    0xaa,               /* MID 170 */
    0x00,               /* SBAS PRN */
    0x01,               /* SBAS Mode */
    0x00,               /* Auto PRN */
    0x00, 0x00,
    0x00, 0x00, 0xb0, 0xb3
};

/* Set Message Rate MID 166 */
static unsigned char requestecef[] = {
    0xa0, 0xa2, 0x00, 0x08,
    0xa6,               /* MID 166 */
    0x00,               /* enable 1 */
    0x02,               /* MID 2 */
    0x01,               /* once per Sec */
    0x00, 0x00,         /* unused */
    0x00, 0x00,         /* unused */
    0x00, 0x00, 0xb0, 0xb3
};

/* Set Message Rate MID 166 */
static unsigned char requesttracker[] = {
    0xa0, 0xa2, 0x00, 0x08,
    0xa6,               /* MID 166 */
    0x00,               /* enable 1 */
    0x04,               /* MID 4 */
    0x03,               /* every 3 sec */
    0x00, 0x00,         /* unused */
    0x00, 0x00,         /* unused */
    0x00, 0x00, 0xb0, 0xb3
};

/* disable MID XX */
static unsigned char unsetmidXX[] = {
    0xa0, 0xa2, 0x00, 0x08,
    0xa6,               /* MID 166 */
    0x00,               /* enable XX */
    0x00,               /* MID 0xXX */
    0x00,               /* rate: never */
    0x00, 0x00,         /* reserved */
    0x00, 0x00,         /* reserved */
    0x00, 0x00, 0xb0, 0xb3
};

/* message to enable:
 *   MID 7 Clock Status
 *   MID 8 50Bps subframe data
 *   MID 17 Differential  Corrections
 *   MID 28 Nav Lib Measurement Data
 *   MID 29 Nav Lib DGPS Data
 *   MID 30 Nav Lib SV State Data
 *   MID 31 Nav Lib Initialization data
 * at 1Hz rate */
static unsigned char enablesubframe[] = {
    0xa0, 0xa2, 0x00, 0x19,
    0x80,                       /* MID 128 initialize Data Source */
    0x00, 0x00, 0x00, 0x00,     /* EXEF X */
    0x00, 0x00, 0x00, 0x00,     /* ECEF Y */
    0x00, 0x00, 0x00, 0x00,     /* ECEF Z */
    0x00, 0x00, 0x00, 0x00,     /* clock drift */
    0x00, 0x00, 0x00, 0x00,     /* time of week */
    0x00, 0x00,                 /* week number */
    0x0C,                       /* Chans 1-12 */
    /* change the next 0x10 to 0x08
     * for factory reset */
    /* 0x10 turns on MIDs 7, 8, 17, 28, 29, 30 and 31 */
    0x10,
    0x00, 0x00, 0xb0, 0xb3
};

/* disable subframe data */
static unsigned char disablesubframe[] = {
    0xa0, 0xa2, 0x00, 0x19,
    0x80,                       /* MID 128 initialize Data Source */
    0x00, 0x00, 0x00, 0x00,     /* EXEF X */
    0x00, 0x00, 0x00, 0x00,     /* ECEF Y */
    0x00, 0x00, 0x00, 0x00,     /* ECEF Z */
    0x00, 0x00, 0x00, 0x00,     /* clock drift */
    0x00, 0x00, 0x00, 0x00,     /* time of week */
    0x00, 0x00,                 /* week number */
    0x0C,                       /* Chans 1-12 */

    /* 0x00 turns off MIDs 7, 8, 17, 28, 29, 30 and 31 */
    0x00,                       /* reset bit map */

    0x00, 0x00, 0xb0, 0xb3
};

/* mode control MID */
static unsigned char modecontrol[] = {
    0xa0, 0xa2, 0x00, 0x0e,
    0x88,                       /* MID 136 Mode Control */
    0x00, 0x00,                 /* pad bytes */
    0x00,                       /* degraded mode off */
    0x00, 0x00,                 /* pad bytes */
    0x00, 0x00,                 /* altitude */
    0x00,                       /* altitude hold auto */
    0x00,                       /* use last computed alt */
    0x00,                       /* reserved */
    0x00,                       /* disable degraded mode */
    0x00,                       /* disable dead reckoning */
    0x01,                       /* enable track smoothing */
    0x00, 0x00, 0xb0, 0xb3
};

/* enable 1 PPS Time MID 52 *
 * using Set Message Rate MID 166 */
static unsigned char enablemid52[] = {
    0xa0, 0xa2, 0x00, 0x08,
    0xa6,                       /* MID 166 */
    0x00,                       /* enable/disable one message */
    0x34,                       /* MID 52 */
    0x01,                       /* sent once per second */
    0x00, 0x00, 0x00, 0x00,     /* reserved, set to zero */
    0x00, 0xdb, 0xb0, 0xb3
};
#endif /* RECONFIGURE_ENABLE */


static gps_mask_t sirf_msg_debug(struct gps_device_t *,
                                 unsigned char *, size_t);
static gps_mask_t sirf_msg_errors(struct gps_device_t *,
                                  unsigned char *, size_t);
static gps_mask_t sirf_msg_navdata(struct gps_device_t *, unsigned char *,
                                   size_t);
static gps_mask_t sirf_msg_navsol(struct gps_device_t *, unsigned char *,
                                  size_t);
static gps_mask_t sirf_msg_nlmd(struct gps_device_t *, unsigned char *,
                                size_t);
static gps_mask_t sirf_msg_ppstime(struct gps_device_t *, unsigned char *,
                                   size_t);
static gps_mask_t sirf_msg_nl(struct gps_device_t *, unsigned char *,
                                   size_t);
static gps_mask_t sirf_msg_ee(struct gps_device_t *, unsigned char *,
                                   size_t);
static gps_mask_t sirf_msg_svinfo(struct gps_device_t *, unsigned char *,
                                  size_t);
static gps_mask_t sirf_msg_swversion(struct gps_device_t *, unsigned char *,
                                     size_t);
static gps_mask_t sirf_msg_sysparam(struct gps_device_t *, unsigned char *,
                                    size_t);
static gps_mask_t sirf_msg_dgpsstatus(struct gps_device_t *, unsigned char *,
                                      size_t);
static gps_mask_t sirf_msg_ublox(struct gps_device_t *, unsigned char *,
                                 size_t);


static bool sirf_write(struct gps_device_t *session, unsigned char *msg)
{
    unsigned int crc;
    size_t i, len;
    bool ok;
    unsigned int type = (unsigned int)msg[4];

    /* do not write if -b (readonly) option set */
    if (session->context->readonly)
        return true;

    /*
     * Control strings spaced too closely together confuse the SiRF
     * IV.  This wasn't an issue on older SiRFs, but they've gone to a
     * lower-powered processor that apparently has trouble keeping up.
     * Now you have to wait for the ACK, otherwise chaos ensues.
     * Add instrumentation to reveal when this may happen.
     */
    /* can also be false because ACK was received after last send */
    if (session->driver.sirf.need_ack > 0) {
        GPSD_LOG(LOG_WARN, &session->context->errout,
                 "SiRF: warning, write of MID %#02x while "
                 "awaiting ACK for %#02x.\n",
                 type, session->driver.sirf.need_ack);
    }

    len = (size_t) ((msg[2] << 8) | msg[3]);

    /* calculate CRC */
    crc = 0;
    /* coverity_submit[tainted_data] */
    for (i = 0; i < len; i++)
        crc += (int)msg[4 + i];
    crc &= 0x7fff;

    /* enter CRC after payload */
    msg[len + 4] = (unsigned char)((crc & 0xff00) >> 8);
    msg[len + 5] = (unsigned char)(crc & 0x00ff);

    GPSD_LOG(LOG_PROG, &session->context->errout,
             "SiRF: Writing MID %#02x:\n", type);
    ok = (gpsd_write(session, (const char *)msg, len + 8) ==
          (ssize_t) (len + 8));

    session->driver.sirf.need_ack = type;
    return ok;
}

#ifdef CONTROLSEND_ENABLE
static ssize_t sirf_control_send(struct gps_device_t *session, char *msg,
                                 size_t len)
{
    session->msgbuf[0] = (char)0xa0;
    session->msgbuf[1] = (char)0xa2;
    session->msgbuf[2] = (len >> 8) & 0xff;
    session->msgbuf[3] = len & 0xff;
    memcpy(session->msgbuf + 4, msg, len);
    session->msgbuf[len + 6] = (char)0xb0;
    session->msgbuf[len + 7] = (char)0xb3;
    session->msgbuflen = len + 8;

    /* *INDENT-OFF* */
    return sirf_write(session,
              (unsigned char *)session->msgbuf) ? (int)session->msgbuflen : -1;
    /* *INDENT-ON* */
}
#endif /* CONTROLSEND_ENABLE */

#ifdef RECONFIGURE_ENABLE
static bool sirfbin_speed(struct gps_device_t *session, speed_t speed, char parity, int stopbits)
/* change speed in binary mode */
{
    static unsigned char msg[] = {
        0xa0, 0xa2, 0x00, 0x09,
        0x86,                   /* byte 4:
                                 * Set Binary Serial Port
                                 * MID 134 */
        0x00, 0x00, 0x12, 0xc0, /* bytes 5-8: 4800 bps */
        0x08,                   /* byte  9: 8 data bits */
        0x01,                   /* byte 10: 1 stop bit */
        0x00,                   /* byte 11: no parity */
        0x00,                   /* byte 12: reserved pad */
        0x00, 0x00, 0xb0, 0xb3
    };
    GPSD_LOG(LOG_PROG, &session->context->errout,
             "SiRF: sirf_speed(%u,%c,%d)\n",
             (unsigned int)speed, parity, stopbits);
    if (9600 >= speed) {
        GPSD_LOG(LOG_WARN, &session->context->errout,
                 "SiRF may lag at 9600bps or less.\n");
    }

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
    msg[5] = (unsigned char)((speed >> 24) & 0xff);
    msg[6] = (unsigned char)((speed >> 16) & 0xff);
    msg[7] = (unsigned char)((speed >> 8) & 0xff);
    msg[8] = (unsigned char)(speed & 0xff);
    msg[10] = (unsigned char)stopbits;
    msg[11] = (unsigned char)parity;
    return (sirf_write(session, msg));
}

/* switch from binary to NMEA at specified baud */
/* FIXME: does not seem to work... */
static bool sirf_to_nmea(struct gps_device_t *session, speed_t speed)
{
    static unsigned char msg[] = { 0xa0, 0xa2, 0x00, 0x18,
        0x81, 0x02,
        0x01, 0x01,             /* GGA */
        0x00, 0x00,             /* suppress GLL */
        0x01, 0x01,             /* GSA */
        0x05, 0x01,             /* GSV */
        0x01, 0x01,             /* RMC */
        0x00, 0x00,             /* suppress VTG */
        0x00, 0x01,             /* suppress MSS */
        0x00, 0x01,             /* suppress EPE */
        0x00, 0x01,             /* suppress EPE */
        0x00, 0x01,             /* suppress ZDA */
        0x00, 0x00,             /* unused */
        0x12, 0xc0,             /* 4800 bps */
        0xb0, 0xb3
    };

    if (speed >= 0xffff) {
        GPSD_LOG(LOG_ERROR, &session->context->errout,
            "SiRF: can't switch from SiRF to NMEA because "
            " current speed %u is big.",
            (unsigned int)speed);
        return false;
    }

    /* stop binary initialization */
    session->cfg_stage = UINT_MAX;

    msg[26] = (unsigned char)HI(speed);
    msg[27] = (unsigned char)LO(speed);
    return sirf_write(session, msg);
}

static void sirfbin_mode(struct gps_device_t *session, int mode)
{
    if (mode == MODE_NMEA) {
        (void)sirf_to_nmea(session, session->gpsdata.dev.baudrate);
    } else if (mode == MODE_BINARY) {
        char parity = '0';
        switch (session->gpsdata.dev.parity) {
        default:
        case 'N':
            parity = '0';
            break;
        case 'O':
            parity = '1';
            break;
        case 'E':
            parity = '2';
            break;

        }
        // gpsd only supports 8[NO]1 or 7[EO]2
        // thus the strange use of stopbits
        (void)nmea_send(session,
                        "$PSRF100,0,%d,%d,%d,%c",
                        session->gpsdata.dev.baudrate,
                        9 - session->gpsdata.dev.stopbits,
                        session->gpsdata.dev.stopbits, parity);
        /* reset binary init steps */
        session->cfg_stage = 0;
        session->cfg_step = 0;
    }
}
#endif /* RECONFIGURE_ENABLE */

/* Debug messages MID 255 (0xff) */
static gps_mask_t sirf_msg_debug(struct gps_device_t *device,
                                 unsigned char *buf, size_t len)
{
    char msgbuf[MAX_PACKET_LENGTH * 3 + 2];
    int i;

    memset(msgbuf, 0, (int)sizeof(msgbuf));

    /* FIXME: always/only ID 255 */
    if (0xe1 == buf[0]) {       /* Development statistics messages */
        if (2 > len) {
            /* too short */
            return 0;
        }
        for (i = 2; i < (int)len; i++)
            str_appendf(msgbuf, sizeof(msgbuf), "%c", buf[i] ^ 0xff);
        GPSD_LOG(LOG_PROG, &device->context->errout,
                 "SiRF: MID 0xe1 (225) SID %#0x %s\n", buf[1], msgbuf);
    } else if (0xff == (unsigned char)buf[0]) { /* Debug messages */
        for (i = 1; i < (int)len; i++)
            if (isprint(buf[i]))
                str_appendf(msgbuf, sizeof(msgbuf), "%c", buf[i]);
            else
                str_appendf(msgbuf, sizeof(msgbuf),
                               "\\x%02x", (unsigned int)buf[i]);
        GPSD_LOG(LOG_PROG, &device->context->errout,
                 "SiRF: DBG 0xff: %s\n", msgbuf);
    }
    return 0;
}

/* decode Error ID Data MID 10 (0x0a) */
static gps_mask_t sirf_msg_errors(struct gps_device_t *device,
                                  unsigned char *buf,
                                  size_t len UNUSED)
{
    /* FIXME: decode count: bytes 4 and 5 */
    switch (getbeu16(buf, 1)) {
    case 2:
        /* ErrId_CS_SVParity */
        GPSD_LOG(LOG_PROG, &device->context->errout,
                 "SiRF: EID 0x0a type 2: Subframe %u error on PRN %u\n",
                 getbeu32(buf, 9), getbeu32(buf, 5));
        break;

    case 4107:
        GPSD_LOG(LOG_PROG, &device->context->errout,
                 "SiRF: EID 0x0a type 4107: neither KF nor LSQ fix.\n");
        break;

    default:
        GPSD_LOG(LOG_PROG, &device->context->errout,
                 "SiRF: EID 0x0a: Error MID %d\n",
                 getbeu16(buf, 1));
        break;
    }
    return 0;
}

/* Navigation Library Measurement Data MID 28 (0x1c) */
static gps_mask_t sirf_msg_nlmd(struct gps_device_t *session,
                                unsigned char *buf UNUSED, size_t len)
{

    double gps_tow = 0.0;

    if (len != 56)
        return 0;

    /* oh barf, SiRF claims to be IEEE754 but supports two
     * different double orders, neither IEEE754 */
    /* FIXME - decode the time, since this is the first MID with a
     * good time stamp this will be good for ntpshm time */
    GPSD_LOG(LOG_PROG, &session->context->errout,
             "SiRF: MID 0x1c, NLMD, gps_tow: %f\n",
             (double)gps_tow);

    return 0;
}

/* MID_SiRFNavNotification MID 51 (0x33) */
static gps_mask_t sirf_msg_navnot(struct gps_device_t *session,
                                unsigned char *buf, size_t len)
{
    const char *definition = "Unknown";
    gps_mask_t mask = 0;

    if (len < 3)
        return 0;

    switch (buf[1]) {
    case 1:
        /* last message sent every cycle */
        definition = "SID_GPS_SIRFNAV_COMPLETE";
        /* so push a report now */
        mask = REPORT_IS;
        break;
    case 2:
        definition = "SID_GPS_SIRFNAV_TIMING";
        break;
    case 3:
        definition = "SID_GPS_DEMO_TIMING";
        break;
    case 4:
        definition = "SID_GPS_SIRFNAV_TIME_TAGS";
        break;
    case 5:
        definition = "SID_GPS_NAV_IS801_PSEUDORANGE_DATA";
        break;
    case 6:
        definition = "GPS_TRACKER_LOADER_STATE";
        break;
    case 7:
        definition = "SSB_SIRFNAV_START";
        break;
    case 8:
        definition = "SSB_SIRFNAV_STOP";
        break;
    case 9:
        definition = "SSB_RESULT";
        break;
    case 16:
        definition = "DEMO_TEST_STATUS";
        break;
    case 17:
        definition = "DEMO_TEST_STATE";
        break;
    case 18:
        definition = "DEMO_TEST_DATA";
        break;
    case 19:
        definition = "DEMO_TEST_STATS";
        break;
    case 20:
        definition = "DEMO_TEST_ERROR";
        break;
    default:
        definition = "Unknown";
        break;
    }

    GPSD_LOG(LOG_PROG, &session->context->errout,
             "SiRF IV: NavNotification 51 (0x33), SID: %d (%s), len %lu\n",
             buf[1], definition, (long unsigned)len);

    return mask;
}

/* Multiconstellation Navigation Data response MID 67,1 (0x43)
 * SIRF_MSG_SSB_GNSS_NAV_DATA
 * this replaces the deprecated MID 41 */
static gps_mask_t sirf_msg_67_1(struct gps_device_t *session,
                                  unsigned char *buf, size_t len)
{
    gps_mask_t mask = 0;
    uint32_t solution_validity;
    uint32_t solution_info;
    uint32_t gps_tow = 0;
    uint32_t msecs;                      /* tow in ms */
    uint32_t gps_tow_sub_ms = 0;
    uint16_t gps_week = 0;
    timespec_t gps_tow_ns = {0};
    timespec_t now = {0};
    int16_t time_bias = 0;
    uint8_t time_accuracy = 0;
    uint8_t time_source = 0;
    struct tm unpacked_date;
    unsigned char datum;
    int64_t clk_bias;
    uint32_t clk_bias_error;
    int32_t clk_offset;
    uint32_t clk_offset_error;
    int16_t heading_rate;             /* rate of change cog deg/s * 100 */
    uint32_t distance_travel;         /* distance traveled m * 100 */
    uint16_t distance_travel_error;   /* distance traveled error in m * 100 */

    uint32_t ehpe;                    /* Est horizontal position error * 100 */
    unsigned char num_svs_in_sol;     /* Num of satellites used in solution */
    uint32_t sv_list_1;
    uint32_t sv_list_2;
    uint32_t sv_list_3;
    uint32_t sv_list_4;
    uint32_t sv_list_5;
    uint32_t additional_info;
    int debug_base = LOG_PROG;
    char ts_buf[TIMESPEC_LEN];

    if (len < 126)
        return 0;

    GPSD_LOG(LOG_PROG, &session->context->errout,
             "SiRF V: MID 67,1 Multiconstellation Navigation Data Response \n");

    solution_validity = getbeu32(buf, 2);
    if (0 != solution_validity) {
        /* invalid fix, just give up */
        return 0;
    }

    solution_info = getbeu32(buf, 6);
    gps_week = getbeu16(buf, 10);
    msecs = getbeu32(buf, 12);
    gps_tow = msecs / 1000;
    gps_tow_ns.tv_sec = gps_tow;
    /* get ms part */
    gps_tow_sub_ms = msecs % 1000;
    /* add in the ns */
    gps_tow_ns.tv_nsec = (gps_tow_sub_ms * 1000000L) + getbeu32(buf, 16);
    now = gpsd_gpstime_resolv(session, gps_week, gps_tow_ns);
    /* we'll not use this time, instead the unpacked date below,
     * to get the right epoch */

    time_bias = getbes16(buf, 20);    /* add in the ns */
    /* time_accuracy is an odd 8 bit float */
    time_accuracy = getub(buf, 22);
    time_source = getub(buf, 23);     /* unused */

    memset(&unpacked_date, 0, sizeof(unpacked_date));
    unpacked_date.tm_year = (int)getbeu16(buf, 24) - 1900;
    unpacked_date.tm_mon = (int)getub(buf, 26) - 1;
    unpacked_date.tm_mday = (int)getub(buf, 27);
    unpacked_date.tm_hour = (int)getub(buf, 28);
    unpacked_date.tm_min = (int)getub(buf, 29);
    unpacked_date.tm_sec = (int)getbeu16(buf, 30) / 1000;
    session->context->leap_seconds = (int)getub(buf, 32);
    session->context->valid |= LEAP_SECOND_VALID;
    session->newdata.time.tv_sec = mkgmtime(&unpacked_date);
    session->newdata.time.tv_nsec = gps_tow_ns.tv_nsec;
    /* got time now */
    mask |= TIME_SET;

    datum = getub(buf, 33);
    datum_code_string(datum, session->newdata.datum,
                      sizeof(session->newdata.datum));

    clk_bias = getbes64(buf, 34) / 100.0;
    clk_bias_error = getbeu32(buf, 42) / 100.0;
    clk_offset = getbes32(buf, 46) / 100.0;
    clk_offset_error = getbeu32(buf, 50) / 100.0;
    session->newdata.latitude = getbes32(buf, 54) * 1e-7;
    session->newdata.longitude = getbes32(buf, 58) * 1e-7;
    /* altitude WGS84 */
    session->newdata.altHAE = getbes32(buf, 62) * 1e-2;
    /* altitude MSL */
    session->newdata.altMSL = getbes32(buf, 66) * 1e-2;
    /* Let gpsd_error_model() deal with geoid_sep */

    mask |= LATLON_SET;

    switch (solution_info & 0x07) {
    case 0:      /* no fix */
        session->newdata.mode = MODE_NO_FIX;
        break;
    case 1:      /* unused */
        session->newdata.mode = MODE_NO_FIX;
        break;
    case 2:      /* unused */
        session->newdata.mode = MODE_NO_FIX;
        break;
    case 3:      /* 3-SV KF Solution */
        session->newdata.mode = MODE_2D;
        break;
    case 4:      /* Four or more SV KF Solution */
        session->newdata.mode = MODE_3D;
        break;
    case 5:      /* 2-D Least-squares Solution */
        session->newdata.mode = MODE_2D;
        break;
    case 6:      /* 3-D Least-squaresSolution */
        session->newdata.mode = MODE_3D;
        break;
    case 7:      /* DR solution, assume 3D */
        session->newdata.mode = MODE_3D;
        break;
    default:     /* can't really happen */
        session->newdata.mode = MODE_NO_FIX;
        break;
    }
    mask |= MODE_SET;

    if (!(solution_info & 0x01000)) {
        /* sog - speed over ground m/s * 100 */
        session->newdata.speed = getbeu16(buf, 70) / 100.0;
        mask |= SPEED_SET;
    }
    /* cog - course over ground fm true north deg * 100  */
    session->newdata.track = getbeu16(buf, 72) / 100.0;
    mask |= TRACK_SET;

    /* climb_rate - vertical velocity m/s * 100 */
    session->newdata.climb = getbes16(buf, 74) / 100.0;

    if (session->newdata.mode == MODE_3D)
        mask |= ALTITUDE_SET | CLIMB_SET;

    heading_rate = getbes16(buf, 76);     /* rate of change cog deg/s * 100 */
    distance_travel = getbeu32(buf, 78);  /* distance traveled m * 100 */
    /* heading_error error of cog deg * 100 */
    session->newdata.epd = getbeu16(buf, 82) / 100.0;
    /* distance traveled error in m * 100 */
    distance_travel_error = getbeu16(buf, 84) / 100.0;

    ehpe = getbeu32(buf, 86);  /* Estimated horizontal position error * 100 */
    /* Estimated vertical position error * 100 */
    session->newdata.epv = getbeu32(buf, 90) / 100.0;
    /* Estimated horizontal velocity error * 100 */
    session->newdata.eps = getbeu16(buf, 94) / 100.0;
    mask |= SPEEDERR_SET;

    session->gpsdata.dop.gdop = (int)getub(buf, 96) / 5.0;
    session->gpsdata.dop.pdop = (int)getub(buf, 97) / 5.0;
    session->gpsdata.dop.hdop = (int)getub(buf, 98) / 5.0;
    session->gpsdata.dop.vdop = (int)getub(buf, 99) / 5.0;
    session->gpsdata.dop.tdop = (int)getub(buf, 100) / 5.0;
    mask |= DOP_SET;

    num_svs_in_sol = getub(buf, 101);
    sv_list_1 = getbeu32(buf, 102);
    sv_list_2 = getbeu32(buf, 106);
    sv_list_3 = getbeu32(buf, 110);
    sv_list_4 = getbeu32(buf, 114);
    sv_list_5 = getbeu32(buf, 118);
    additional_info = getbeu32(buf, 122);

    mask |= REPORT_IS; /* send it */

    /* skip all the debug pushing and popping, unless needed */
    if (session->context->errout.debug >= debug_base) {
        /* coerce time_t to long to placate older OS, like 32-bit FreeBSD,
         * where time_t is int */
        GPSD_LOG(debug_base, &session->context->errout,
                 "GPS Week %d, tow %d.%03d, time %s\n",
                 gps_week, gps_tow, gps_tow_sub_ms,
                 timespec_str(&now, ts_buf, sizeof(ts_buf)));
        GPSD_LOG(debug_base, &session->context->errout,
                 "UTC time %s leaps %u, datum %s\n",
                 timespec_str(&session->newdata.time, ts_buf, sizeof(ts_buf)),
                 session->context->leap_seconds,
                 session->newdata.datum);
        GPSD_LOG(debug_base, &session->context->errout,
                 "packed: %02d%02d%02d %02d:%02d:%02d\n",
                 unpacked_date.tm_mday, unpacked_date.tm_mon + 1,
                 unpacked_date.tm_year % 100,
                 unpacked_date.tm_hour, unpacked_date.tm_min,
                 unpacked_date.tm_sec);
        GPSD_LOG(debug_base, &session->context->errout,
                 "solution_info %08x\n", solution_info);
        GPSD_LOG(debug_base, &session->context->errout,
                 "lat %.7f lon %.7f altHAE %.2f altMSL %.2f\n",
                 session->newdata.latitude, session->newdata.longitude,
                 session->newdata.altHAE, session->newdata.altMSL);
        GPSD_LOG(debug_base, &session->context->errout,
                 "speed %.2f track %.2f climb %.2f heading_rate %d\n",
                 session->newdata.speed, session->newdata.track,
                 session->newdata.climb, heading_rate);
        GPSD_LOG(debug_base, &session->context->errout,
                 "time_bias %d time_accuracy %u, time_source %u\n",
                 time_bias, time_accuracy, time_source);
        GPSD_LOG(debug_base, &session->context->errout,
                 "distance_travel %u distance_travel_error %d\n",
                 distance_travel, distance_travel_error);
        GPSD_LOG(debug_base, &session->context->errout,
                 "clk_bias %.2f clk_bias_error %u\n",
                 clk_bias / 100.0, clk_bias_error);
        GPSD_LOG(debug_base, &session->context->errout,
                 "clk_offset %d clk_offset_error %u\n",
                 clk_offset, clk_offset_error);
        GPSD_LOG(debug_base, &session->context->errout,
                 "ehpe %d epv %.2f eps %.2f epd %.2f num_svs_in_sol %u\n",
                 ehpe, session->newdata.epv, session->newdata.eps,
                 session->newdata.epd, num_svs_in_sol);
        GPSD_LOG(debug_base, &session->context->errout,
                 "sv_list_1 %08x sv_list_2 %08x sv_list_3 %08x\n",
                 sv_list_1, sv_list_2, sv_list_3);
        GPSD_LOG(debug_base, &session->context->errout,
                 "sv_list_4 %08x sv_list_5 %08x add_info %08x\n",
                 sv_list_4, sv_list_5, additional_info);
    }

    return mask;
}

/* Multiconstellation Navigation Data response MID 67,16 (0x43)
 * this replaces the deprecated MID 41 */
static gps_mask_t sirf_msg_67_16(struct gps_device_t *session,
                                  unsigned char *buf, size_t len)
{
    gps_mask_t mask = 0;
    uint32_t gps_tow = 0;
    uint32_t gps_tow_sub_ms = 0;
    uint16_t gps_week = 0;
    timespec_t gps_tow_ns = {0};
    timespec_t now = {0};
    int16_t time_bias = 0;
    uint8_t time_accuracy = 0;
    uint8_t time_source = 0;
    uint8_t msg_info = 0;
    uint8_t num_of_sats = 0;
    unsigned int sat_num;
    int st;                    /* index into skyview */
    char ts_buf[TIMESPEC_LEN];

    if (198 > len) {
        /* always payload of 15 sats */
        return 0;
    }

    GPSD_LOG(LOG_PROG, &session->context->errout,
             "SiRF V: MID 67,16 Multiconstellation Satellite Data Response\n");

    gps_week = getbeu16(buf, 2);
    gps_tow = getbeu32(buf, 4) / 1000;
    /* get ms part, convert to ns */
    gps_tow_sub_ms = 1000000 * (getbeu32(buf, 4) % 1000);
    gps_tow_sub_ms += getbeu32(buf, 8);    /* add in the ns */
    gps_tow_ns.tv_sec = gps_tow;
    gps_tow_ns.tv_nsec = gps_tow_sub_ms;
    session->newdata.time = gpsd_gpstime_resolv(session, gps_week, gps_tow_ns);
    session->gpsdata.skyview_time = session->newdata.time;
    time_bias = getbes16(buf, 12);
    /* time_accuracy is an odd 8 bit float */
    time_accuracy = getub(buf, 14);
    time_source = getub(buf, 15);
    msg_info = getub(buf, 16);
    if (0 == (msg_info & 0x0f)) {
        /* WTF? */
        return 0;
    }
    if (1 == (msg_info & 0x0f)) {
        /* first set, zero the sats */
        gpsd_zero_satellites(&session->gpsdata);
    }
    st = ((msg_info & 0x0f) - 1) * 15;
    num_of_sats = getub(buf, 17);
    /* got time now */
    mask |= TIME_SET;

    /* skip all the debug pushing and popping, unless needed */
    if (session->context->errout.debug >= LOG_IO) {
        /* coerce time_t to long to placate older OS, like 32-bit FreeBSD,
         * where time_t is int */
        GPSD_LOG(LOG_IO, &session->context->errout,
             "GPS Week %d, tow %d.%03d, time %s\n",
             gps_week, gps_tow, gps_tow_sub_ms,
             timespec_str(&now, ts_buf, sizeof(ts_buf)));
        GPSD_LOG(LOG_IO, &session->context->errout,
             "Time bias: %u ns, accuracy %#02x, source %u, "
             "msg_info %#02x, sats %u\n",
             time_bias, time_accuracy, time_source, msg_info,
             num_of_sats);
    }

    session->gpsdata.satellites_visible = num_of_sats;
    /* used? */

    /* now decode the individual sat data */
    /* num_of_sats is total sats tracked, not the number of sats in this
       message */
    for (sat_num = 0; sat_num < num_of_sats; sat_num++) {
        unsigned offset;
        uint16_t sat_info;
        uint16_t other_info;
        unsigned char gnssId_sirf;
        unsigned char gnssId;
        unsigned char svId;
        short PRN;
        double azimuth;
        double elevation;
        short avg_cno;
        double ss;
        uint32_t status;

        offset = 18 + (sat_num * 12);
        if (offset >= len) {
            /* end of this message */
            break;
        }
        sat_info = getbeu16(buf, offset);
        if (0 == sat_info) {
            /* emtpy slot, ignore */
            continue;;
        }

        /* 0 = GPS/QZSS
           1 = SBAS
           2 = GLONASS
           3 = Galileo
           4 = BDS
         */
        gnssId_sirf = sat_info >> 13;
        svId = sat_info & 0x0ff;
        other_info = (sat_info >> 8) & 0x1f;
        /* make up a PRN based on gnssId:svId, using table 4-55
         * from (CS-303979-SP-9) SiRFstarV OSP Extensions
         * Note: the Qualcomm doc is very vague
         */
        switch (gnssId_sirf) {
        case 0:
            /* GPS, 1-32 maps to 1-32
             * 173 to 182: QZSS IMES
             * 183 to 187: QZSS SAIF
             * 193 to 202: QZSS */
            if ((173 <= svId) && (182 >= svId)){
                /* IMES */
                gnssId = 4;
                PRN = svId;
                svId -= 172;
            } else if ((193 <= svId) && (202 >= svId)){
                /* QZSS */
                gnssId = 5;
                PRN = svId;
                svId -= 192;
            } else {
                /* GPS, or?? */
                gnssId = 0;
                PRN = svId;
            }
            break;
        case 1:
            /* SBAS, 120-158 maps to 120-158 */
            if (120 > svId || 158 < svId) {
                /* skip bad svId */
                continue;
            }
            gnssId = 1;
            PRN = svId;
            break;
        case 2:
            /* GLONASS, 1-32 maps to 65-96 */
            if (1 > svId) {
                /* skip bad svId */
                continue;
            }
            if (32 < svId) {
                /* skip bad svId */
                continue;
            }
            gnssId = 6;
            PRN = svId + 64;
            break;
        case 3:
            /* Galileo, 1-36 maps to 211-246 */
            if (1 > svId) {
                /* skip bad svId */
                continue;
            }
            if (37 < svId) {
                /* skip bad svId */
                continue;
            }
            gnssId = 2;
            PRN = svId + 210;
            break;
        case 4:
            /* BeiDou, 1-37 maps to 159-163,33-64 */
            if (1 > svId) {
                /* skip bad svId */
                continue;
            } else if (6 > svId) {
                /* 1-5 maps to 159-163 */
                PRN = svId + 158;
            } else if (37 < svId) {
                /* skip bad svId */
                continue;
            } else {
                /* 6-37 maps to 33-64 */
                PRN = svId + 27;
            }
            gnssId = 3;
            break;
        default:
            /* Huh?  Skip bad gnssId */
            continue;
        }

        /* note tenths in az and el */
        azimuth = (double)getbeu16(buf, offset + 2) / 10.0;
        /* what, no negative elevation? */
        elevation = (double)getbeu16(buf, offset + 4) / 10.0;
        avg_cno = getbeu16(buf, offset + 6);
        ss = (double)avg_cno / 10.0;
        status = getbeu32(buf, offset + 8);
        if ((0 == avg_cno) && (0 >= elevation) && (0 >= azimuth)) {
            /* null data, skip it */
            continue;
        }

        session->gpsdata.skyview[st].PRN = PRN;
        session->gpsdata.skyview[st].svid = svId;
        session->gpsdata.skyview[st].gnssid = gnssId;
        session->gpsdata.skyview[st].azimuth = azimuth;
        session->gpsdata.skyview[st].elevation = elevation;
        session->gpsdata.skyview[st].ss = ss;
        if (0x08000 == (status & 0x08000)) {
            session->gpsdata.skyview[st].used = true;
        }
        GPSD_LOG(LOG_IO, &session->context->errout,
                 "sat_info %04x gnssId %u svId %3u o %2u PRN %3u az %4.1f "
                 "el %5.1f ss %5.1f\n",
                 sat_info, gnssId, svId, other_info, PRN, azimuth,
                 elevation, ss);
        st++;
        if (st == MAXCHANNELS) {
            /* filled up skyview */
            break;
        }
    }
    if ((msg_info >> 4) == (msg_info & 0x0f)) {
        /* got all the sats */
        mask |= SATELLITE_SET;
    }
    return mask;
}

/* Multiconstellation Navigation Data response MID 67 (0x43)
 * this replaces the deprecated MID 41 */
static gps_mask_t sirf_msg_67(struct gps_device_t *session,
                                  unsigned char *buf, size_t len)
{
    gps_mask_t mask = 0;

    if (len < 2)
        return 0;

    switch (buf[1]) {
    case 1:
        return sirf_msg_67_1(session, buf, len);
    case 16:
        return sirf_msg_67_16(session, buf, len);
    default:
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "SiRF V: unused MID 67 (0x43), SID: %d, len %ld\n", buf[1],
                 (long)len);
    }
    return mask;
}

/* MID_QUERY_RESP MID 81 (0x51) */
static gps_mask_t sirf_msg_qresp(struct gps_device_t *session,
                                unsigned char *buf, size_t len)
{

    if (len < 3)
        return 0;

    GPSD_LOG(LOG_PROG, &session->context->errout,
             "SiRF IV: unused MID_QUERY_RESP 0x51 (81), Q MID: %d, "
             "SID: %d Elen: %d\n",
             buf[1], buf[2], buf[3]);
    return 0;
}

/* Statistics Channel MID 225 (0xe1) */
static gps_mask_t sirf_msg_stats(struct gps_device_t *session,
                                 unsigned char *buf, size_t len)
{
    const char *definition = "Unknown";
    char output[255] = "unused";
    uint16_t ttff_reset;
    uint16_t ttff_aid;
    uint16_t ttff_nav;

    if (2 > len)
        return 0;

    switch (buf[1]) {
    case 6:
        definition = "SSB_SIRF_STATS 6";
        ttff_reset = getbeu16(buf, 2);
        ttff_aid = getbeu16(buf, 4);
        ttff_nav = getbeu16(buf, 6);
        (void)snprintf(output, sizeof(output),
                       "ttff reset %.1f, aid %.1f nav %.1f",
                       ttff_reset * 0.1, ttff_aid * 0.1, ttff_nav * 0.1);
        break;
    case 7:
        definition = "SSB_SIRF_STATS 7";
        ttff_reset = getbeu16(buf, 2);
        ttff_aid = getbeu16(buf, 4);
        ttff_nav = getbeu16(buf, 6);
        (void)snprintf(output, sizeof(output),
                       "ttff reset %.1f, aid %.1f nav %.1f",
                       ttff_reset * 0.1, ttff_aid * 0.1, ttff_nav * 0.1);
        break;
    case 32:
        definition = "SIRF_MSG_SSB_DL_COMPAT_REC_OUT ";
        break;
    case 33:
        definition = "SIRF_MSG_SSB_DL_OUT_TERM";
        break;
    case 34:
        definition = "SIRF_MSG_SSB_DL_STATUS_OUT";
        break;
    case 35:
        definition = "SIRF_MSG_SSB_SIRF_INTERNAL_OUT";
        break;
    case 65:
        definition = "SIRF_MSG_SSB_EE_SEA_PROVIDE_EPH_EXT";
        break;
    default:
        definition = "Unknown";
        break;
    }

    GPSD_LOG(LOG_PROG, &session->context->errout,
             "SiRF IV: MID 225 (0xe1), SID: %d (%s)%s\n",
             buf[1], definition, output);

    return 0;
}

/* MID_TCXO_LEARNING_OUT MID 93 (0x5d) */
static gps_mask_t sirf_msg_tcxo(struct gps_device_t *session,
                                unsigned char *buf, size_t len)
{
    const char *definition = "Unknown";
    uint32_t gps_tow = 0;
    uint16_t gps_week = 0;
    timespec_t gps_tow_ns = {0};
    char output[255] = "";
    timespec_t now = {0};
    gps_mask_t mask = 0;
    unsigned int time_status = 0;
    int clock_offset = 0;
    unsigned int temp = 0;

    if (len < 2)
        return 0;

    switch (buf[1]) {
    case 1:
        definition = "CLOCK_MODEL_DATA_BASE_OUT";
        break;
    case 2:
        definition = "TEMPERATURE_TABLE";
        break;
    case 4:
        definition = "TEMP_RECORDER_MESSAGE";
        break;
    case 5:
        definition = "EARC";
        break;
    case 6:
        definition = "RTC_ALARM";
        break;
    case 7:
        definition = "RTC_CAL";
        break;
    case 8:
        definition = "MPM_ACQUIRED";
        break;
    case 9:
        definition = "MPM_SEARCHES";
        break;
    case 10:
        definition = "MPM_PREPOS";
        break;
    case 11:
        definition = "MICRO_NAV_MEASUREMENT";
        break;
    case 12:
        definition = "TCXO_UNCEARTAINTY";
        break;
    case 13:
        definition = "SYSTEM_TIME_STAMP";
        break;
    case 18:
        if (26 > len) {
            GPSD_LOG(LOG_PROG, &session->context->errout,
                     "SiRF IV: TCXO 0x5D (93), SID: %d BAD len %zd\n",
                     buf[1], len);
            return 0;
        }

        definition = "SIRF_MSG_SSB_XO_TEMP_REC_VALUE";
        gps_tow = getbeu32(buf, 2);
        gps_week = getbeu16(buf, 6);
        time_status = getub(buf, 8);
        clock_offset = getsb(buf, 9);  /* looks like leapseconds? */
        temp = getub(buf, 22);
        gps_tow_ns.tv_sec = gps_tow / 100;
        gps_tow_ns.tv_nsec = (gps_tow % 100) * 10000000L;
        session->newdata.time = gpsd_gpstime_resolv(session, gps_week,
                                                    gps_tow_ns);

        /* skip all the debug pushing and popping, unless needed */
        if (session->context->errout.debug >= LOG_PROG) {
            /* coerce time_t to long to placate older OS, like 32-bit FreeBSD,
             * where time_t is int */
            (void)snprintf(output, sizeof(output),
                           ", GPS Week %d, tow %d, time %lld, time_status %d "
                           "ClockOffset %d, Temp %.1f",
                           gps_week, gps_tow, (long long)now.tv_sec,
                           time_status,
                           clock_offset, temp * 0.54902);
        }

        if (7 == (time_status & 7)) {
            mask |= TIME_SET;
        }
        break;
    default:
        definition = "Unknown";
        break;
    }

    GPSD_LOG(LOG_PROG, &session->context->errout,
             "SiRF IV: TCXO 0x5D (93), SID: %d (%s)%s\n",
             buf[1], definition, output);

    return mask;
}

/* Software Version String MID 6
 * response to Poll Software Version MID 132 */
static gps_mask_t sirf_msg_swversion(struct gps_device_t *session,
                                     unsigned char *buf, size_t len)
{
    double fv;
    unsigned char *cp;

    if (1 > len)
        return 0;

    if ((3 < len) && (len == (unsigned int)(buf[1] + buf[2] + 3))) {
        /* new style message, Version 4+, max 162 bytes */
        (void)strlcpy(session->subtype, (char *)buf + 3,
                      sizeof(session->subtype));
        (void)strlcat(session->subtype, ";", sizeof(session->subtype));
        (void)strlcat(session->subtype, (char *)buf + 3 + buf[1],
            sizeof(session->subtype));
        session->driver.sirf.driverstate |= SIRF_GE_232;
        /* FIXME: this only finding major version, not minor version */
        for (cp = buf+1; *cp!=(unsigned char)'\0' && isdigit(*cp)==0; cp++)
            continue;
        fv = safe_atof((const char *)cp);
    } else {
        /* old style, version 3 and below */

        (void)strlcpy(session->subtype, (char *)buf + 1,
                      sizeof(session->subtype));

        for (cp = buf+1; *cp!=(unsigned char)'\0' && isdigit(*cp)==0; cp++)
            continue;
        fv = safe_atof((const char *)cp);
        if (fv < 231) {
            session->driver.sirf.driverstate |= SIRF_LT_231;
#ifdef RECONFIGURE_ENABLE
            if (fv > 200)
                sirfbin_mode(session, 0);
#endif /* RECONFIGURE_ENABLE */
        } else if (fv < 232) {
            session->driver.sirf.driverstate |= SIRF_EQ_231;
        } else {
            session->driver.sirf.driverstate |= SIRF_GE_232;
        }
        if (strstr((char *)(buf + 1), "ES"))
            GPSD_LOG(LOG_INF, &session->context->errout,
                     "SiRF: Firmware has XTrac capability\n");
    }
    GPSD_LOG(LOG_PROG, &session->context->errout,
             "SiRF: fv: %0.2f, Driver state flags are: %0x\n",
             fv, session->driver.sirf.driverstate);
    session->driver.sirf.time_seen = 0;
    GPSD_LOG(LOG_DATA, &session->context->errout,
             "SiRF: FV MID 0x06: subtype='%s' len=%lu buf1 %u buf2 %u\n",
             session->subtype, (long)len, buf[1], buf[2]);
    return DEVICEID_SET;
}

/* subframe data MID 8 */
static gps_mask_t sirf_msg_navdata(struct gps_device_t *session,
                                   unsigned char *buf, size_t len)
{
    unsigned int i, chan, svid;
    uint32_t words[10];

    if (len != 43)
        return 0;

    chan = (unsigned int)getub(buf, 1);
    svid = (unsigned int)getub(buf, 2);

    for (i = 0; i < 10; i++) {
        words[i] = (uint32_t)getbeu32(buf, 4 * i + 3);
    }

    GPSD_LOG(LOG_DATA, &session->context->errout,
             "SiRF: NavData chan %u svid %u\n",chan,svid);

#ifdef RECONFIGURE_ENABLE
    /* SiRF recommends at least 57600 for SiRF IV nav data */
    if (!session->context->readonly && session->gpsdata.dev.baudrate < 57600) {
        /* some USB are also too slow, no way to tell which ones */
        GPSD_LOG(LOG_WARN, &session->context->errout,
                 "WARNING: SiRF: link too slow, disabling subframes.\n");
        (void)sirf_write(session, disablesubframe);
    }
#endif /* RECONFIGURE_ENABLE */

    return gpsd_interpret_subframe_raw(session, svid, words);
}

/* max channels allowed in old MID 4 SiRF format */
#define SIRF_CHANNELS   12

/* decode Measured Tracker Data response ID 4 (0x04)
 * deprecated on Sirfstar V, use MID 67,16 instead */
static gps_mask_t sirf_msg_svinfo(struct gps_device_t *session,
                                  unsigned char *buf, size_t len)
{
    int st, i, j, nsv;
    uint32_t hsec;        /* TOW in hundredths of seconds */
    timespec_t ts_tow;
    char ts_buf[TIMESPEC_LEN];

    if (len != 188)
        return 0;

    hsec = getbeu32(buf, 3);
    ts_tow.tv_sec = hsec / 100;
    ts_tow.tv_nsec = (long)((hsec % 100) * 10000000L);
    session->gpsdata.skyview_time = gpsd_gpstime_resolv(session,
        (unsigned short)getbes16(buf, 1), ts_tow);

    gpsd_zero_satellites(&session->gpsdata);
    for (i = st = nsv = 0; i < SIRF_CHANNELS; i++) {
        int cn;
        int off = 8 + 15 * i;
        bool good;
        short prn = (short)getub(buf, off);
        unsigned short stat = (unsigned short)getbeu16(buf, off + 3);
        session->gpsdata.skyview[st].PRN = prn;
        session->gpsdata.skyview[st].svid = prn;
        if (120 <= prn && 158 >= prn) {
            /* SBAS */
            session->gpsdata.skyview[st].gnssid = 1;
        } else {
            /* GPS */
            session->gpsdata.skyview[st].gnssid = 0;
        }
        session->gpsdata.skyview[st].azimuth =
            (double)(((unsigned)getub(buf, off + 1) * 3.0) / 2.0);
        session->gpsdata.skyview[st].elevation =
            (double)((unsigned)getub(buf, off + 2) / 2.0);
        cn = 0;
        for (j = 0; j < 10; j++)
            cn += (int)getub(buf, off + 5 + j);

        session->gpsdata.skyview[st].ss = (float)(cn / 10.0);
        session->gpsdata.skyview[st].used = (bool)(stat & 0x01);
        good = session->gpsdata.skyview[st].PRN != 0 &&
            session->gpsdata.skyview[st].azimuth != 0 &&
            session->gpsdata.skyview[st].elevation != 0;
#ifdef __UNUSED__
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "SiRF: PRN=%2d El=%3.2f Az=%3.2f ss=%3d stat=%04x %c\n",
                 prn,
                 getub(buf, off + 2) / 2.0,
                 (getub(buf, off + 1) * 3) / 2.0,
                 cn / 10, stat, good ? '*' : ' ');
#endif /* UNUSED */
        if (good != 0) {
            st += 1;
            if (stat & 0x01)
                nsv++;
        }
    }
    session->gpsdata.satellites_visible = st;
    session->gpsdata.satellites_used = nsv;
    /* mark SBAS sats in use if SBAS was in use as of the last MID 27 */
    for (i = 0; i < st; i++) {
        int prn = session->gpsdata.skyview[i].PRN;
        if ((120 <= prn && 158 >= prn) &&
            session->gpsdata.status == STATUS_DGPS_FIX &&
            session->driver.sirf.dgps_source == SIRF_DGPS_SOURCE_SBAS) {
            /* used does not seem right, DGPS means got the correction
             * data, not that the geometry was improved... */
            session->gpsdata.skyview[i].used = true;
        }
    }
    if (st < 3) {
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "SiRF: NTPD not enough satellites seen: %d\n", st);
    } else {
        /* SiRF says if 3 sats in view the time is good */
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "SiRF: NTPD valid time MID 0x04, seen=%#02x, time:%s, "
                 "leap:%d\n",
                 session->driver.sirf.time_seen,
                 timespec_str(&session->gpsdata.skyview_time, ts_buf,
                              sizeof(ts_buf)),
                 session->context->leap_seconds);
    }
    GPSD_LOG(LOG_DATA, &session->context->errout,
             "SiRF: MTD 0x04: visible=%d mask={SATELLITE}\n",
             session->gpsdata.satellites_visible);
    return SATELLITE_SET;
}

static double sirf_time_offset(struct gps_device_t *session)
/* return NTP time-offset fudge factor for this device */
{
    double retval = 0;

    /* we need to have seen UTC time with a valid leap-year offset */
    if ((session->driver.sirf.time_seen & TIME_SEEN_UTC_2) != 0) {
        retval = NAN;
    }

    /* the PPS time message */
    else if (session->driver.sirf.lastid == (unsigned char)52) {
        retval = 0.3;
    }

    /* u-blox EMND message */
    else if (session->driver.sirf.lastid == (unsigned char)98) {
        retval = 0.570;
    }
#ifdef __UNUSED__
    /* geodetic-data message */
    else if (session->driver.sirf.lastid == (unsigned char)41) {
        retval = 0.570;
    }
#endif /* __UNUSED__ */

    /* the Navigation Solution message */
    else if (session->driver.sirf.lastid == (unsigned char)2) {
        if (session->sourcetype == source_usb) {
            retval = 0.640;     /* USB, expect +/- 50mS jitter */
        } else {
            switch (session->gpsdata.dev.baudrate) {
            default:
                retval = 0.704; /* WAG */
                break;
            case 4800:
                retval = 0.704; /* fudge valid at 4800bps */
                break;
            case 9600:
                retval = 0.688;
                break;
            case 19200:
                retval = 0.484;
                break;
            case 38400:
                retval = 0.845; /*  0.388; ?? */
                break;
            }
        }
    }

    return retval;
}

/* Measured Navigation Data Out ID 2 (0x02) */
static gps_mask_t sirf_msg_navsol(struct gps_device_t *session,
                                  unsigned char *buf, size_t len)
{
    unsigned short navtype;
    unsigned short nav_mode2;
    unsigned short gps_week;
    uint32_t iTOW;
    timespec_t tow;
    gps_mask_t mask = 0;
    char ts_buf[TIMESPEC_LEN];

    /* later versions are 47 bytes long */
    if (41 > len)
        return 0;

    /*
     * A count of satellites used is an unsigned byte at offset 28
     * and an array of unsigned bytes listing satellite PRNs used
     * in this fix begins at offset 29, but we don't use either because
     * in JSON the used bits are reported in the SKY sentence;
     * we get that data from the svinfo packet.
     */
    /* position/velocity is bytes 1-18 */
    session->newdata.ecef.x = (double)getbes32(buf, 1);
    session->newdata.ecef.y = (double)getbes32(buf, 5);
    session->newdata.ecef.z = (double)getbes32(buf, 9);
    session->newdata.ecef.vx = (double)getbes16(buf, 13) / 8.0;
    session->newdata.ecef.vy = (double)getbes16(buf, 15) / 8.0;
    session->newdata.ecef.vz = (double)getbes16(buf, 17) / 8.0;

    mask |= ECEF_SET | VECEF_SET;

    /* fix status is byte 19 */
    navtype = (unsigned short)getub(buf, 19);
    session->gpsdata.status = STATUS_NO_FIX;
    session->newdata.mode = MODE_NO_FIX;
    if ((navtype & 0x80) != 0)
        session->gpsdata.status = STATUS_DGPS_FIX;
    else if ((navtype & 0x07) > 0 && (navtype & 0x07) < 7)
        session->gpsdata.status = STATUS_FIX;
    if ((navtype & 0x07) == 4 || (navtype & 0x07) == 6)
        session->newdata.mode = MODE_3D;
    else if (session->gpsdata.status != 0)
        session->newdata.mode = MODE_2D;
    /* byte 20 is HDOP */
    session->gpsdata.dop.hdop = (double)getub(buf, 20) / 5.0;
    /* byte 21 is nav_mode2, not clear how to interpret that */
    nav_mode2 = getub(buf, 21);

    gps_week = getbes16(buf, 22);
    iTOW = getbeu32(buf, 24);
    /* Gack.  The doc says early SiRF scales iTOW by 100, later ones
     * by 1000.  But that does not seem to be true in sirfstar V. */
    tow.tv_sec = iTOW / 100;
    tow.tv_nsec = (iTOW % 100) * 10000000L;
    session->newdata.time = gpsd_gpstime_resolv(session, gps_week, tow);

    if (session->newdata.mode <= MODE_NO_FIX) {
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "SiRF: NTPD no fix, mode: %d\n",
                 session->newdata.mode);
    } else {
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "SiRF: MID 0x02  NTPD valid time, seen %#02x time %s "
                 "leap %d nav_mode2 %#x\n",
                 session->driver.sirf.time_seen,
                 timespec_str(&session->newdata.time, ts_buf, sizeof(ts_buf)),
                 session->context->leap_seconds,
                 nav_mode2);
    }
    /* clear computed DOPs so they get recomputed. */
    session->gpsdata.dop.tdop = NAN;
    mask |= TIME_SET | STATUS_SET | MODE_SET | DOP_SET | USED_IS;
    if ( 3 <= session->gpsdata.satellites_visible ) {
        mask |= NTPTIME_IS;
    }

    GPSD_LOG(LOG_PROG, &session->context->errout,
             "SiRF: MND 0x02: Navtype %#0x, Status %d mode %d\n",
             navtype, session->gpsdata.status, session->newdata.mode);
    GPSD_LOG(LOG_DATA, &session->context->errout,
             "SiRF: MND 0x02: gpsd_week %u iTOW %u\n",
             gps_week, iTOW);
    GPSD_LOG(LOG_DATA, &session->context->errout,
             "SiRF: MND 0x02: time %s ecef x: %.2f y: %.2f z: %.2f "
             "mode %d status %d hdop %.2f used %d\n",
             timespec_str(&session->newdata.time, ts_buf, sizeof(ts_buf)),
             session->newdata.ecef.x,
             session->newdata.ecef.y, session->newdata.ecef.z,
             session->newdata.mode, session->gpsdata.status,
             session->gpsdata.dop.hdop, session->gpsdata.satellites_used);
    return mask;
}

#ifdef __UNUSED__
/***************************************************************************
 We've stopped interpreting GND (0x29) for the following reasons:

1) Versions of SiRF firmware still in wide circulation (and likely to be
   so for a while) don't report a valid time field, leading to annoying
   twice-per-second jitter in client displays.

2) What we wanted out of this that MND didn't give us was horizontal and
   vertical error estimates. But we have to do our own error estimation by
   computing DOPs from the skyview covariance matrix anyway, because we
   want separate epx and epy errors a la NMEA 3.0.

3) The fix-merge logic in gpsd.c is (unavoidably) NMEA-centric and
   thinks multiple sentences in one cycle should be treated as
   incremental updates.  This leads to various silly results when (as
   in GND) a subsequent sentence is (a) intended to be a complete fix
   in itself, and (b) frequently broken.

4) Ignoring this dodgy sentence allows us to go to a nice clean single
   fix update per cycle.

Code left in place in case we need to reverse this decision.

***************************************************************************/
static gps_mask_t sirf_msg_geodetic(struct gps_device_t *session,
                                    unsigned char *buf, size_t len)
{
    unsigned short navtype;
    gps_mask_t mask = 0;
    double eph;
    double dbl_tmp;

    if (len != 91)
        return 0;

    session->gpsdata.sentence_length = 91;

    navtype = (unsigned short)getbeu16(buf, 3);
    session->gpsdata.status = STATUS_NO_FIX;
    session->newdata.mode = MODE_NO_FIX;
    if (navtype & 0x80)
        session->gpsdata.status = STATUS_DGPS_FIX;
    else if ((navtype & 0x07) > 0 && (navtype & 0x07) < 7)
        session->gpsdata.status = STATUS_FIX;
    session->newdata.mode = MODE_NO_FIX;
    if ((navtype & 0x07) == 4 || (navtype & 0x07) == 6)
        session->newdata.mode = MODE_3D;
    else if (session->gpsdata.status)
        session->newdata.mode = MODE_2D;
    GPSD_LOG(LOG_PROG, &session->context->errout,
             "SiRF: GND 0x29: Navtype = 0x%0x, Status = %d, mode = %d\n",
             navtype, session->gpsdata.status, session->newdata.mode);
    mask |= STATUS_SET | MODE_SET;

    session->newdata.latitude = getbes32(buf, 23) * 1e-7;
    session->newdata.longitude = getbes32(buf, 27) * 1e-7;
    if (session->newdata.latitude != 0 && session->newdata.latitude != 0)
        mask |= LATLON_SET;

    if ((eph = getbes32(buf, 50)) > 0) {
        session->newdata.eph = eph * 1e-2;
        mask |= HERR_SET;
    }
    dbl_temp = getbes32(buf, 54) * 1e-2;
    if (0.01 < dbl_temp)
        session->newdata.epv = dbl_temp;
    if ((session->newdata.eps = getbes16(buf, 62) * 1e-2) > 0)
        mask |= SPEEDERR_SET;

    /* HDOP should be available at byte 89, but in 231 it's zero. */
    //session->gpsdata.dop.hdop = (unsigned int)getub(buf, 89) * 0.2;

    if ((session->newdata.mode > MODE_NO_FIX)
        && (session->driver.sirf.driverstate & SIRF_GE_232)) {
        struct tm unpacked_date;
        double subseconds;
        /*
         * Early versions of the SiRF protocol manual don't document
         * this sentence at all.  Some that do incorrectly
         * describe UTC Day, Hour, and Minute as 2-byte quantities,
         * not 1-byte. Chris Kuethe, our SiRF expert, tells us:
         *
         * "The Geodetic Navigation packet (0x29) was not fully
         * implemented in firmware prior to version 2.3.2. So for
         * anyone running 231.000.000 or earlier (including ES,
         * SiRFDRive, XTrac trains) you won't get UTC time. I don't
         * know what's broken in firmwares before 2.3.1..."
         *
         * To work around the incomplete implementation of this
         * packet in 231, we used to assume that only the altitude field
         * from this packet is valid.  But even this doesn't necessarily
         * seem to be the case.  Instead, we do our own computation
         * of geoid separation now.
         *
         * UTC is left all zeros in 231 and older firmware versions,
         * and misdocumented in version 1.4 of the Protocol Reference.
         *            Documented:        Real:
         * UTC year       2               2
         * UTC month      1               1
         * UTC day        2               1
         * UTC hour       2               1
         * UTC minute     2               1
         * UTC second     2               2
         *                11              8
         *
         * Documentation of this field was corrected in the 1.6 version
         * of the protocol manual.
         */
        memset(&unpacked_date, 0, sizeof(unpacked_date));
        unpacked_date.tm_year = (int)getbeu16(buf, 11) - 1900;
        unpacked_date.tm_mon = (int)getub(buf, 13) - 1;
        unpacked_date.tm_mday = (int)getub(buf, 14);
        unpacked_date.tm_hour = (int)getub(buf, 15);
        unpacked_date.tm_min = (int)getub(buf, 16);
        subseconds = getbeu16(buf, 17);
        unpacked_date.tm_sec = subseconds / 1000;
        session->newdata.time.tv_sec = mkgmtime(&unpacked_date);
        session->newdata.time.tv_nsec = (long)((subseconds % 1000) * 1000000L);

        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "SiRF: GND 0x29 UTC: %lf\n",
                 session->newdata.time);
        if (session->newdata.mode <= MODE_NO_FIX) {
            GPSD_LOG(LOG_PROG, &session->context->errou,
                     "SiRF: NTPD no fix, mode: $d\n",
                     session->newdata.mode);
        } else if (0 == unpacked_date.tm_year) {
            GPSD_LOG(LOG_PROG, &session->context->errou,
                     "SiRF: NTPD no year\n",
                     session->newdata.mode);
        } else {
            GPSD_LOG(LOG_PROG, &session->context->errout,
                     "SiRF: NTPD valid time MID 0x29, seen=%#02x\n",
                     session->driver.sirf.time_seen);
        }
        if ( 3 <= session->gpsdata.satellites_visible ) {
            mask |= NTPTIME_IS;
        }

        /* alititude WGS84 */
        session->newdata.altHAE = getbes32(buf, 31) * 1e-2;
        session->newdata.altMSL = getbes32(buf, 35) * 1e-2;
	/* Let gpsd_error_model() deal with geoid_sep and altHAE */
        /* skip 1 byte of map datum */
        session->newdata.speed = getbeu16(buf, 40) * 1e-2;
        session->newdata.track = getbeu16(buf, 42) * 1e-2;
        /* skip 2 bytes of magnetic variation */
        session->newdata.climb = getbes16(buf, 46) * 1e-2;
        mask |= TIME_SET | SPEED_SET | TRACK_SET;
        if (session->newdata.mode == MODE_3D)
            mask |= ALTITUDE_SET | CLIMB_SET;
    }
    GPSD_LOG(LOG_DATA, &session->context->errout,
             "SiRF: GND 0x29: time=%.2f lat=%.2f lon=%.2f altHAE=%.2f "
             "track=%.2f speed=%.2f mode=%d status=%d\n",
             session->newdata.time,
             session->newdata.latitude,
             session->newdata.longitude,
             session->newdata.altHAE,
             session->newdata.track,
             session->newdata.speed,
             session->newdata.mode,
             session->gpsdata.status);
    return mask;
}
#endif /* __UNUSED__ */

/* decode Navigation Parameters MID 19 (0x13) response to ID 152 */
static gps_mask_t sirf_msg_sysparam(struct gps_device_t *session,
                                    unsigned char *buf, size_t len)
{

    if (len < 65)
        return 0;

    /* save these to restore them in the revert method */
    session->driver.sirf.nav_parameters_seen = true;
    session->driver.sirf.altitude_hold_mode = (unsigned char)getub(buf, 5);
    session->driver.sirf.altitude_hold_source = (unsigned char)getub(buf, 6);
    session->driver.sirf.altitude_source_input = getbes16(buf, 7);
    session->driver.sirf.degraded_mode = (unsigned char)getub(buf, 9);
    session->driver.sirf.degraded_timeout = (unsigned char)getub(buf, 10);
    session->driver.sirf.dr_timeout = (unsigned char)getub(buf, 11);
    session->driver.sirf.track_smooth_mode = (unsigned char)getub(buf, 12);
    return 0;
}

/* DGPS status MID 27 (0x1b) */
/* only documentented from prorocol version 1.7 (2005) onwards */
static gps_mask_t sirf_msg_dgpsstatus(struct gps_device_t *session,
                                 unsigned char *buf, size_t len UNUSED)
{
    session->driver.sirf.dgps_source = (unsigned int)getub(buf, 1);
    return 0;
}

/* decode Extended Measured Navigation Data MID 98 (0x62)
 * Used in u-blox TIM GPS receivers (SiRF2-ublox)
 * "Firmware Release 2.1 UBX 1.0" */
static gps_mask_t sirf_msg_ublox(struct gps_device_t *session,
                                 unsigned char *buf, size_t len UNUSED)
{
    gps_mask_t mask;
    unsigned short navtype;
    char ts_buf[TIMESPEC_LEN];

    if (len != 39)
        return 0;

    /* this packet is only sent by u-blox firmware from version 1.32 */
    mask = LATLON_SET | ALTITUDE_SET | SPEED_SET | TRACK_SET | CLIMB_SET |
        STATUS_SET | MODE_SET | DOP_SET;
    session->newdata.latitude = (double)getbes32(buf, 1) * RAD_2_DEG * 1e-8;
    session->newdata.longitude = (double)getbes32(buf, 5) * RAD_2_DEG * 1e-8;
    /* defaults to WGS84 */
    session->newdata.altHAE = (double)getbes32(buf, 9) * 1e-3;
    session->newdata.speed = (double)getbes32(buf, 13) * 1e-3;
    session->newdata.climb = (double)getbes32(buf, 17) * 1e-3;
    session->newdata.track = (double)getbes32(buf, 21) * RAD_2_DEG * 1e-8;

    navtype = (unsigned short)getub(buf, 25);
    session->gpsdata.status = STATUS_NO_FIX;
    session->newdata.mode = MODE_NO_FIX;
    if (navtype & 0x80)
        session->gpsdata.status = STATUS_DGPS_FIX;
    else if ((navtype & 0x07) > 0 && (navtype & 0x07) < 7)
        session->gpsdata.status = STATUS_FIX;
    if ((navtype & 0x07) == 4 || (navtype & 0x07) == 6)
        session->newdata.mode = MODE_3D;
    else if (session->gpsdata.status)
        session->newdata.mode = MODE_2D;
    GPSD_LOG(LOG_PROG, &session->context->errout,
             "SiRF: EMND 0x62: Navtype = 0x%0x, Status = %d, mode = %d\n",
             navtype, session->gpsdata.status, session->newdata.mode);

    if (navtype & 0x40) {       /* UTC corrected timestamp? */
        struct tm unpacked_date;
        uint32_t msec;

        mask |= TIME_SET;
        if ( 3 <= session->gpsdata.satellites_visible ) {
            mask |= NTPTIME_IS;
        }
        memset(&unpacked_date, 0, sizeof(unpacked_date));
        unpacked_date.tm_year = (int)getbeu16(buf, 26) - 1900;
        unpacked_date.tm_mon = (int)getub(buf, 28) - 1;
        unpacked_date.tm_mday = (int)getub(buf, 29);
        unpacked_date.tm_hour = (int)getub(buf, 30);
        unpacked_date.tm_min = (int)getub(buf, 31);
        msec = getbeu16(buf, 32);
        unpacked_date.tm_sec = msec / 1000;
        session->newdata.time.tv_sec = mkgmtime(&unpacked_date);
        /* ms to ns */
        session->newdata.time.tv_nsec = (msec % 1000) * 1000000L;
        if (0 == (session->driver.sirf.time_seen & TIME_SEEN_UTC_2)) {
            GPSD_LOG(LOG_RAW, &session->context->errout,
                     "SiRF: NTPD just SEEN_UTC_2\n");
        }
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "SiRF: NTPD valid time MID 0x62, seen=%#02x\n",
                 session->driver.sirf.time_seen);
        session->driver.sirf.time_seen |= TIME_SEEN_UTC_2;
        /* The mode byte, bit 6 tells us if leap second is valid.
         * But not what the leap second is.
         * session->context->valid |= LEAP_SECOND_VALID;
         */
    }

    session->gpsdata.dop.gdop = (int)getub(buf, 34) / 5.0;
    session->gpsdata.dop.pdop = (int)getub(buf, 35) / 5.0;
    session->gpsdata.dop.hdop = (int)getub(buf, 36) / 5.0;
    session->gpsdata.dop.vdop = (int)getub(buf, 37) / 5.0;
    session->gpsdata.dop.tdop = (int)getub(buf, 38) / 5.0;
    session->driver.sirf.driverstate |= UBLOX;
    GPSD_LOG(LOG_DATA, &session->context->errout,
             "SiRF: EMD 0x62: time=%s lat=%.2f lon=%.2f altHAE=%.2f "
             "speed=%.2f track=%.2f climb=%.2f mode=%d status=%d gdop=%.2f "
             "pdop=%.2f hdop=%.2f vdop=%.2f tdop=%.2f\n",
             timespec_str(&session->newdata.time, ts_buf, sizeof(ts_buf)),
             session->newdata.latitude,
             session->newdata.longitude, session->newdata.altHAE,
             session->newdata.speed, session->newdata.track,
             session->newdata.climb, session->newdata.mode,
             session->gpsdata.status, session->gpsdata.dop.gdop,
             session->gpsdata.dop.pdop, session->gpsdata.dop.hdop,
             session->gpsdata.dop.vdop, session->gpsdata.dop.tdop);
    return mask;
}

/* decode PPS Time MID 52 (0x34) */
static gps_mask_t sirf_msg_ppstime(struct gps_device_t *session,
                                   unsigned char *buf, size_t len)
{
    gps_mask_t mask = 0;

    if (len < 19)
        return 0;

    GPSD_LOG(LOG_PROG, &session->context->errout,
             "SiRF: PPS 0x34: Status = %#02x\n",
             getub(buf, 14));
    if (((int)getub(buf, 14) & 0x07) == 0x07) { /* valid UTC time? */
        struct tm unpacked_date;
        memset(&unpacked_date, 0, sizeof(unpacked_date));
        unpacked_date.tm_hour = (int)getub(buf, 1);
        unpacked_date.tm_min = (int)getub(buf, 2);
        unpacked_date.tm_sec = (int)getub(buf, 3);
        unpacked_date.tm_mday = (int)getub(buf, 4);
        unpacked_date.tm_mon = (int)getub(buf, 5) - 1;
        unpacked_date.tm_year = (int)getbeu16(buf, 6) - 1900;
        session->newdata.time.tv_sec = mkgmtime(&unpacked_date);
        session->newdata.time.tv_nsec = 0;
        session->context->leap_seconds = (int)getbeu16(buf, 8);
        // Ignore UTCOffsetFrac1
        session->context->valid |= LEAP_SECOND_VALID;
        if (0 == (session->driver.sirf.time_seen & TIME_SEEN_UTC_2)) {
            GPSD_LOG(LOG_RAW, &session->context->errout,
                     "SiRF: NTPD just SEEN_UTC_2\n");
        }
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "SiRF: NTPD valid time MID 0x34, seen=%#02x, leap=%d\n",
                 session->driver.sirf.time_seen,
                 session->context->leap_seconds);
        session->driver.sirf.time_seen |= TIME_SEEN_UTC_2;
        mask |= TIME_SET;
        if ( 3 <= session->gpsdata.satellites_visible ) {
            mask |= NTPTIME_IS;
        }
    }
    return mask;
}

/* decode Navigation Library Measurement Data MID 28 (0x38) */
static gps_mask_t sirf_msg_nl(struct gps_device_t *session,
                                   unsigned char *buf, size_t len)
{

    if (len != 67)
        return 0;

    switch ( buf[1] ) {
    case 1:
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "SiRF IV: unused NL 0x38, SubID: 1, GPS Data\n");
        break;
    case 2:
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "SiRF IV: unused NL 0x38, SubID: 2, EE Integrity\n");
        break;
    case 3:
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "SiRF IV: unused NL 0x38, SubID: 3, EE Integrity\n");
        break;
    case 4:
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "SiRF IV: unused NL 0x38, SubID: 4, EE Clock Bias\n");
        break;
    case 5:
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "SiRF IV: unused NL 0x38, SubID: 4, 50bps\n");
        break;
    case 32:
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "SiRF IV: unused NL 0x38, SubID: 4, ECLM ACK/NACK\n");
        break;
    case 33:
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "SiRF IV: unused NL 0x38, SubID: 4, ECLM EE Age\n");
        break;
    case 34:
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "SiRF IV: unused NL 0x38, SubID: 4, ECLM SGEE Age\n");
        break;
    case 35:
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "SiRF IV: unused NL 0x38, SubID: 4, ECLM Download Intiate\n");
        break;
    case 255:
        GPSD_LOG(LOG_PROG, &session->context->errout,
            "SiRF IV: unused NL 0x38, SubID: 4, EE ACK\n");
        break;
    default:
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "SiRF IV: unused NL 0x38, unknown SubID: %d\n",
                 buf[1]);
    }

    return 0;
}

/* decode  Extended Ephemeris Data MID 56 (0x38) */
static gps_mask_t sirf_msg_ee(struct gps_device_t *session,
                                   unsigned char *buf, size_t len)
{

    if (len != 67)
        return 0;

    switch ( buf[1] ) {
    case 1:
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "SiRF IV: unused EE 0x40, SubID: 1\n");
        break;
    case 2:
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "SiRF IV: unused EE 0x40, SubID: 2, PRN: %d\n",
                 buf[2]);
        break;
    default:
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "SiRF IV: unused EE 0x40, unknown SubID: %d\n",
                 buf[1]);
    }

    return 0;
}


gps_mask_t sirf_parse(struct gps_device_t * session, unsigned char *buf,
                      size_t len)
{

    if (len == 0)
        return 0;

    buf += 4;
    len -= 8;
    /* cast for 32/64 bit compatiility */
    GPSD_LOG(LOG_RAW, &session->context->errout,
             "SiRF: Raw packet type %#04x len %ld\n", buf[0],
             (long)len);
    session->driver.sirf.lastid = buf[0];

    /* could change if the set of messages we enable does */
    session->cycle_end_reliable = true;

    switch (buf[0]) {
    case 0x02:                  /* Measure Navigation Data Out MID 2 */
        if ((session->driver.sirf.driverstate & UBLOX) == 0)
            return sirf_msg_navsol(session, buf,
                                   len) | (CLEAR_IS | REPORT_IS);
        else {
            GPSD_LOG(LOG_PROG, &session->context->errout,
                     "SiRF: MID 2 (0x02) MND skipped, u-blox flag is on.\n");
            return 0;
        }
    case 0x04:                  /* Measured tracker data out MID 4 */
        return sirf_msg_svinfo(session, buf, len);

    case 0x05:                  /* Raw Tracker Data Out MID 5 */
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "SiRF: unused MID 5 (0x05) Raw Tracker Data\n");
        return 0;

    case 0x06:                  /* Software Version String MID 6 */
        return sirf_msg_swversion(session, buf, len);

    case 0x07:                  /* Clock Status Data MID 7 */
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "SiRF: unused MID 7 (0x07) CLK\n");
        return 0;

    case 0x08:                  /* subframe data MID 8 */
        /* extract leap-second from this */
        /*
         * Chris Kuethe says:
         * "Message 8 is generated as the data is received. It is not
         * buffered on the chip. So when you enable message 8, you'll
         * get one subframe every 6 seconds.  Of the data received, the
         * almanac and ephemeris are buffered and stored, so you can
         * query them at will. Alas, the time parameters are not
         * stored, which is really lame, as the UTC-GPS correction
         * changes 1 second every few years. Maybe."
         */
        return sirf_msg_navdata(session, buf, len);

    case 0x09:                  /* CPU Throughput MID 9 (0x09) */
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "SiRF: THR 0x09: SegStatMax=%.3f, SegStatLat=%3.f, "
                 "AveTrkTime=%.3f, Last MS=%u\n",
                 (float)getbeu16(buf, 1) / 186, (float)getbeu16(buf, 3) / 186,
                 (float)getbeu16(buf, 5) / 186, getbeu16(buf, 7));
        return 0;

    case 0x0a:                  /* Error ID Data MID 10 */
        return sirf_msg_errors(session, buf, len);

    case 0x0b:                  /* Command Acknowledgement MID 11 */
        if (2 > len) {
            return 0;
        }
        if (2 == len) {
            GPSD_LOG(LOG_PROG, &session->context->errout,
                     "SiRF: ACK 0x0b: %#02x\n", getub(buf, 1));
        } else {
            /* SiRF III+, has ACK ID */
            GPSD_LOG(LOG_PROG, &session->context->errout,
                     "SiRF: ACK 0x0b: %#02x/%02x\n",
                     getub(buf, 1), getub(buf, 2));
        }
        session->driver.sirf.need_ack = 0;
        return 0;

    case 0x0c:                  /* Command NAcknowledgement MID 12 */
        if (2 > len) {
            return 0;
        }
        if (2 == len) {
            GPSD_LOG(LOG_PROG, &session->context->errout,
                     "SiRF: NACK 0x0c: %#02x\n", getub(buf, 1));
        } else {
            /* SiRF III+, has NACK ID */
            GPSD_LOG(LOG_PROG, &session->context->errout,
                     "SiRF: NACK 0x0c: %#02x/%02x\n",
                     getub(buf, 1), getub(buf, 2));
        }
        /* ugh -- there's no alternative but silent failure here */
        session->driver.sirf.need_ack = 0;
        return 0;

    case 0x0d:                  /* Visible List MID 13 */
        /* no data her not already in MID 67,16 */
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "SiRF: unused MID 13 (0x0d) Visible List, len %zd\n", len);
        return 0;

    case 0x0e:                  /* Almanac Data MID 14 */
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "SiRF: unused MID 14 (0x0e) ALM\n");
        return 0;

    case 0x0f:                  /* Ephemeris Data MID 15 */
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "SiRF: unused MID 15 (0x0f) EPH\n");
        return 0;

    case 0x11:                  /* Differential Corrections MID 17 */
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "SiRF: unused MID 17 (0x11) DIFF\n");
        return 0;

    case 0x12:                  /* OK To Send MID 18 (0x12) */
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "SiRF: MID 18 (0x12) OkToSend: OK = %d\n",
                 getub(buf, 1));
        return 0;

    case 0x13:                  /* Navigation Parameters MID 19 (0x13) */
        return sirf_msg_sysparam(session, buf, len);

    case 0x1b:                  /* DGPS status MID 27 */
        return sirf_msg_dgpsstatus(session, buf, len);

    case 0x1c:                  /* Navigation Library Measurement Data MID 28 */
        return sirf_msg_nlmd(session, buf, len);

    case 0x1d:                  /* Navigation Library DGPS Data MID 29 */
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "SiRF: unused MID 29 (0x1d) NLDG\n");
        return 0;

    case 0x1e:                  /* Navigation Library SV State Data MID 30 */
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "SiRF: unused MID 30 (0x1e) NLSV\n");
        return 0;

    case 0x1f:          /* Navigation Library Initialization Data MID 31 */
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "SiRF: unused MID 32 (0x1f) NLID\n");
        return 0;

    case 0x29:                  /* Geodetic Navigation Data MID 41 */
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "SiRF: unused MID 41 (0x29) Geodetic Nav Data\n");
        return 0;

    case 0x32:                  /* SBAS corrections MID 50 */
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "SiRF: unused MID 50 (0x32) SBAS\n");
        return 0;

    case 0x33:                /* MID_SiRFNavNotification MID 51, 0x33 */
        return sirf_msg_navnot(session, buf, len);

    case 0x34:                  /* PPS Time MID 52 */
        /*
         * Carl Carter from SiRF writes: "We do not output on the
         * second (unless you are using MID 52).  We make
         * measurements in the receiver in time with an internal
         * counter that is not slaved to GPS time, so the measurements
         * are made at a time that wanders around the second.  Then,
         * after the measurements are made (all normalized to the same
         * point in time) we dispatch the navigation software to make
         * a solution, and that solution comes out some 200 to 300 ms
         * after the measurement time.  So you may get a message at
         * 700 ms after the second that uses measurements time tagged
         * 450 ms after the second.  And if some other task jumps up
         * and delays things, that message may not come out until 900
         * ms after the second.  Things can get out of sync to the
         * point that if you try to resolve the GPS time of our 1 PPS
         * pulses using the navigation messages, you will find it
         * impossible to be consistent.  That is why I added
         * MID 52 to our system -- it is tied to the creation of the 1
         * PPS and always comes out right around the top of the
         * second."
         */
        return sirf_msg_ppstime(session, buf, len);

    case 0x38:                /* EE Output MID 56 */
        return sirf_msg_ee(session, buf, len);

    case 0x40:                /* Nav Library MID 64 */
        return sirf_msg_nl(session, buf, len);

    case 0x43:                /* Multiconstellation Nav Data Response MID 67 */
        return sirf_msg_67(session, buf, len);

    case 0x47:                /* Hardware Config MID 71 */
        /* MID_HW_CONFIG_REQ */
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "SiRF IV: unused MID 71 (0x47) Hardware Config Request, "
                 "len %zd\n", len);
        return 0;

    case 0x51:                /* MID_QUERY_RESP MID 81 */
        return sirf_msg_qresp(session, buf, len);

    case 0x5c:                /* Controller Interference Report MID 92 */
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "SiRF IV: unused MID 92 (0x5c) CW Interference Report\n");
        return 0;

    case 0x5d:                /* TCXO Output MID 93 */
        return sirf_msg_tcxo(session, buf, len);

    case 0x62:          /* u-blox Extended Measured Navigation Data MID 98 */
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "SiRF: MID 98 (0x62) u-blox EMND\n");
        return sirf_msg_ublox(session, buf, len) | (CLEAR_IS | REPORT_IS);

    case 0x80:                  /* Initialize Data Source MID 128 */
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "SiRF: unused MID 128 (0x80) INIT\n");
        return 0;

    case 0xe1:                  /* statistics messages MID 225 */
        return sirf_msg_stats(session, buf, len);

    case 0xff:                  /* Debug messages MID 255 */
        (void)sirf_msg_debug(session, buf, len);
        return 0;

    default:
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "SiRF: Unknown packet id %d (%#x) length %zd\n",
                 buf[0], buf[0], len);
        return 0;
    }
}

static gps_mask_t sirfbin_parse_input(struct gps_device_t *session)
{
    if (session->lexer.type == SIRF_PACKET) {
        return sirf_parse(session, session->lexer.outbuffer,
                        session->lexer.outbuflen);
#ifdef NMEA0183_ENABLE
    } else if (session->lexer.type == NMEA_PACKET) {
        return nmea_parse((char *)session->lexer.outbuffer, session);
#endif /* NMEA0183_ENABLE */
    } else
        return 0;
}

static void sirfbin_init_query(struct gps_device_t *session)
{
    GPSD_LOG(LOG_PROG, &session->context->errout,
             "SiRF: Probing for firmware version.\n");

    /* reset binary init steps */
    session->cfg_stage = 0;
    session->cfg_step = 0;

    /* MID 132 */
    (void)sirf_write(session, versionprobe);
    /* ask twice, SiRF IV on USB often misses the first request */
    (void)sirf_write(session, versionprobe);
}

static void sirfbin_event_hook(struct gps_device_t *session, event_t event)
{
    static unsigned char moderevert[] = {
        0xa0, 0xa2, 0x00, 0x0e,
        0x88,
        0x00, 0x00,     /* pad bytes */
        0x00,           /* degraded mode */
        0x00, 0x00,     /* pad bytes */
        0x00, 0x00,     /* altitude source */
        0x00,           /* altitude hold mode */
        0x00,           /* use last computed alt */
        0x00,           /* reserved */
        0x00,           /* degraded mode timeout */
        0x00,           /* dead reckoning timeout */
        0x00,           /* track smoothing */
        0x00, 0x00, 0xb0, 0xb3
    };

    if (session->context->readonly)
        return;

    switch (event) {
    case event_identified:
        /* FALLTHROUGH */
    case event_reactivate:
        if (session->lexer.type == NMEA_PACKET) {
            GPSD_LOG(LOG_PROG, &session->context->errout,
                     "SiRF: Switching chip mode to binary.\n");
            (void)nmea_send(session,
                            "$PSRF100,0,%d,8,1,0",
                            session->gpsdata.dev.baudrate);
        }
        break;

    case event_configure:
        /* This wakes up on every received packet.
         * Use this hook to step, slowly, through the init messages.
         * We try, but not always succeed, to wait for the ACK/NACK.
         * Send a message only every 15 times so we get an ACK/NACK
         * before next one.
         *
         * This tries to avoid overrunning the input buffer, and makes
         * it much easier to identify which messages get a NACK
         */


        if (UINT_MAX == session->cfg_stage) {
            /* init done */
            return;
        }
        session->cfg_step++;

        if ((0 < session->driver.sirf.need_ack) &&
            (15 > session->cfg_step)) {
            /* we are waiting for ACK, just wait for 15 messages */
            return;
        }
        session->cfg_step = 0;
        session->cfg_stage++;
        GPSD_LOG(LOG_PROG, &session->context->errout, "stage: %d\n",
            session->cfg_stage);


        switch (session->cfg_stage) {
        case 0:
            /* this slot used by event_identified */
            return;

        case 1:
            (void)sirf_write(session, versionprobe);
            break;
#ifdef RECONFIGURE_ENABLE
        case 2:
            /* unset MID 0x40 = 64 first since there is a flood of them */
            GPSD_LOG(LOG_PROG, &session->context->errout,
                     "SiRF: unset MID 0x40.\n");
            unsetmidXX[5] = 1;        /* enable/disable */
            unsetmidXX[6] = 0x40;     /* MID 0x40 */
            (void)sirf_write(session, unsetmidXX);
            break;

        case 3:
            /*
             * The response to this request will save the navigation
             * parameters so they can be reverted before close.
             */
            GPSD_LOG(LOG_PROG, &session->context->errout,
                     "SiRF: Requesting navigation parameters.\n");
            (void)sirf_write(session, navparams);
            break;

        case 4:
            /* unset GND (0x29 = 41), it's not reliable on SiRF II */
            GPSD_LOG(LOG_PROG, &session->context->errout,
                     "SiRF: unset MID 0x29.\n");
            unsetmidXX[5] = 1;        /* enable/disable */
            unsetmidXX[6] = 0x29;     /* MID 0x29 */
            (void)sirf_write(session, unsetmidXX);
            break;

        case 5:
            if (!session->context->readonly) {
                GPSD_LOG(LOG_PROG, &session->context->errout,
                         "SiRF: Setting Navigation Parameters.\n");
                (void)sirf_write(session, modecontrol);
            }
            break;

        case 6:
            GPSD_LOG(LOG_PROG, &session->context->errout,
                     "SiRF: Requesting periodic ecef reports.\n");
            (void)sirf_write(session, requestecef);
            break;

        case 7:
            GPSD_LOG(LOG_PROG, &session->context->errout,
                     "SiRF: Requesting periodic tracker reports.\n");
            (void)sirf_write(session, requesttracker);
            break;

        case 8:
            GPSD_LOG(LOG_PROG, &session->context->errout,
                     "SiRF: Setting DGPS control to use SBAS.\n");
            (void)sirf_write(session, dgpscontrol);
            break;

        case 9:
            GPSD_LOG(LOG_PROG, &session->context->errout,
                     "SiRF: Setting SBAS to auto/integrity mode.\n");
            (void)sirf_write(session, sbasparams);
            break;

        case 10:
            GPSD_LOG(LOG_PROG, &session->context->errout,
                     "SiRF: Enabling PPS message MID 52 (0x32).\n");
            /* Not supported on some GPS.
             * It will be NACKed is not supported */
            (void)sirf_write(session, enablemid52);
            break;

        case 11:
            /* SiRF recommends at least 57600 for SiRF IV subframe data */
            if (session->gpsdata.dev.baudrate >= 57600) {
                /* fast enough, turn on subframe data */
                GPSD_LOG(LOG_PROG, &session->context->errout,
                         "SiRF: Enabling subframe transmission.\n");
                (void)sirf_write(session, enablesubframe);
            } else {
                /* too slow, turn off subframe data */
                GPSD_LOG(LOG_PROG, &session->context->errout,
                         "SiRF: Disabling subframe transmission.\n");
                (void)sirf_write(session, disablesubframe);
            }
            break;

        case 12:
            /*
             * Disable navigation debug messages (the value 5 is magic)
             * must be done *after* subframe enable.
             */
            GPSD_LOG(LOG_PROG, &session->context->errout,
                     "SiRF: disable MID 7, 28, 29, 30, 31.\n");
            unsetmidXX[5] = 5;
            unsetmidXX[6] = 0;
            (void)sirf_write(session, unsetmidXX);
            break;

#endif /* RECONFIGURE_ENABLE */
        default:
            /* initialization is done */
            session->cfg_stage = UINT_MAX;
            session->cfg_step = 0;
            return;
        }
        break;

    case event_deactivate:

        putbyte(moderevert, 7, session->driver.sirf.degraded_mode);
        putbe16(moderevert, 10, session->driver.sirf.altitude_source_input);
        putbyte(moderevert, 12, session->driver.sirf.altitude_hold_mode);
        putbyte(moderevert, 13, session->driver.sirf.altitude_hold_source);
        putbyte(moderevert, 15, session->driver.sirf.degraded_timeout);
        putbyte(moderevert, 16, session->driver.sirf.dr_timeout);
        putbyte(moderevert, 17, session->driver.sirf.track_smooth_mode);
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "SiRF: Reverting navigation parameters...\n");
        (void)sirf_write(session, moderevert);
        break;

    case event_driver_switch:
        /* do what here? */
        break;
    case event_triggermatch:
        /* do what here? */
        break;
    case event_wakeup:
        /* do what here? */
        break;
    }
}



/* this is everything we export */
/* *INDENT-OFF* */
const struct gps_type_t driver_sirf =
{
    .type_name      = "SiRF",           /* full name of type */
    .packet_type    = SIRF_PACKET,      /* associated lexer packet type */
    .flags          = DRIVER_STICKY,    /* remember this */
    .trigger        = NULL,             /* no trigger */
    .channels       = SIRF_CHANNELS,    /* consumer-grade GPS */
    .probe_detect   = NULL,             /* no probe */
    .get_packet     = generic_get,      /* be prepared for SiRF or NMEA */
    .parse_packet   = sirfbin_parse_input,/* parse message packets */
    .rtcm_writer    = gpsd_write,       /* send RTCM data straight */
    .init_query     = sirfbin_init_query,/* non-perturbing initial qury */
    .event_hook     = sirfbin_event_hook,/* lifetime event handler */
#ifdef RECONFIGURE_ENABLE
    .speed_switcher = sirfbin_speed,    /* we can change baud rate */
    .mode_switcher  = sirfbin_mode,     /* there's a mode switcher */
    .rate_switcher  = NULL,             /* no sample-rate switcher */
    .min_cycle.tv_sec  = 1,		/* not relevant, no rate switch */
    .min_cycle.tv_nsec = 0,		/* not relevant, no rate switch */
#endif /* RECONFIGURE_ENABLE */
#ifdef CONTROLSEND_ENABLE
    .control_send   = sirf_control_send,/* how to send a control string */
#endif /* CONTROLSEND_ENABLE */
    .time_offset     = sirf_time_offset,
};
/* *INDENT-ON* */
#endif /* defined(SIRF_ENABLE) && defined(BINARY_ENABLE) */
