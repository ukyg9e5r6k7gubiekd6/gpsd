/*
 * Handle the Trimble TSIP packet format
 * by Rob Janssen, PE1CHL.
 * Accutime Gold support by Igor Socec <igorsocec@gmail.com>
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
#define TSIP_CHANNELS	15

/* defines for Set or Request I/O Options (0x35)
 * SMT 360 default: IO1_DP|IO1_LLA, IO2_ENU, 0, IO4_DBHZ */
// byte 1
#define IO1_ECEF 1
#define IO1_LLA 2
#define IO1_MSL 4
#define IO1_DP 0x10
// IO1_8F20 not in SMT 360
#define IO1_8F20 0x20
// byte 2
#define IO2_VECEF 1
#define IO2_ENU 2
// byte 3
#define IO3_UTC 1
// byte 4
#define IO4_RAW 1
#define IO4_DBHZ 8

#define SEMI_2_DEG	(180.0 / 2147483647)	/* 2^-31 semicircle to deg */

void configuration_packets_accutime_gold(struct gps_device_t *session);
void configuration_packets_generic(struct gps_device_t *session);

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
    float f1, f2, f3, f4, f5;
    double d1, d2, d3, d4, d5;
    time_t now;
    unsigned char buf[BUFSIZ];
    char buf2[BUFSIZ];
    uint32_t tow;             // time of week in milli seconds
    double ftow;              // time of week in seconds
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
    case 0x13:			/* Packet Received */
	u1 = getub(buf, 0);     // Packet ID of non-parsable packet
	u2 = getub(buf, 1);     // Data byte 0 of non-parsable packet
        // ignore the rest of the bad data
	GPSD_LOG(LOG_WARN, &session->context->errout,
		 "TSIP: Report Packet (0x13): type x%02x cannot be parsed\n",
                 u1);
	if ((int)u1 == 0x8e && (int)u2 == 0x23) {
            /* no Compact Super Packet 0x8e-23 */
	    GPSD_LOG(LOG_WARN, &session->context->errout,
		     "TSIP: No 0x8e-23, use LFwEI (0x8f-20)\n");

	    /* Request LFwEI Super Packet
             * SMT 360 does not support 0x8e-20 either */
	    putbyte(buf, 0, 0x20);
	    putbyte(buf, 1, 0x01);	/* auto-report */
	    (void)tsip_write(session, 0x8e, buf, 2);
	}
	break;

    case 0x1c:        // Hardware/Software Version Information
        /* Present in:
         *  Accutime Gold
         *  Copernicus (2006)
         *  Copernicus II (2009)
         *  Thunderbolt E (2012)
         *  RES SMT 360 (2018)
         *  ICM SMT 360 (2018)
         *  RES360 17x22 (2018)
         *  Acutime 360
         * Not in:
         *  ACE II (1999)
         *  ACE III (2000)
         *  Lassen SQ (2002)
         *  Lassen iQ (2005) */
	u1 = (uint8_t) getub(buf, 0);
        // decode by subtype
	switch (u1) {
        case 0x81:       // Firmware component version information (0x1c-81)
                // 1, reserved
		u2 = getub(buf, 2); /* Major version */
		u3 = getub(buf, 3); /* Minor version */
		u4 = getub(buf, 4); /* Build number */
		u5 = getub(buf, 5); /* Month */
		u6 = getub(buf, 6); /* Day */
		ul1 = getbeu16(buf, 7); /* Year */
		u7 = getub(buf, 9); /* Length of first module name */
		for (i=0; i < (int)u7; i++) {
                    /* Product name in ASCII */
		    buf2[i] = (char)getub(buf, 10+i);
		}
		buf2[i] = '\0';

		(void)snprintf(session->subtype, sizeof(session->subtype),
			       "sw %u %u %u %02u.%02u.%04u %.62s",
			       u2, u3, u4, u6, u5, ul1, buf2);
		GPSD_LOG(LOG_INF, &session->context->errout,
			 "TSIP: Software version (0x81): %s\n",
			 session->subtype);

		mask |= DEVICEID_SET;
                break;
	case 0x83:    //  Hardware component version information (0x1c-83)
		ul1 = getbeu32(buf, 1);  /* Serial number */
		u2 = getub(buf, 5);      /* Build day */
		u3 = getub(buf, 6);      /* Build month */
		ul2 = getbeu16(buf, 7);  /* Build year */
		u4 = getub(buf, 6);      /* Build hour */
                /* Hardware Code */
                session->driver.tsip.hardware_code = getbeu16(buf, 10);
		u5 = getub(buf, 12);     /* Length of Hardware ID */
		/* coverity_submit[tainted_data] */
		for (i=0; i < (int)u5; i++) {
		    buf2[i] = (char)getub(buf, 13+i); /* Hardware ID in ASCII */
		}
		buf2[i] = '\0';

                // FIXME! This over writes date from 0x1c-83
		(void)snprintf(session->subtype, sizeof(session->subtype),
			       "hw %u %02u.%02u.%04u %02u %u %.48s",
			       ul1, u2, u3, ul2, u4,
                               session->driver.tsip.hardware_code,
                               buf2);
		GPSD_LOG(LOG_INF, &session->context->errout,
			 "TSIP: Hardware version (0x83): %s\n",
			 session->subtype);

		mask |= DEVICEID_SET;

		/* Detecting device by Hardware Code */
		switch (session->driver.tsip.hardware_code) {
		case 3001:            // Acutime Gold
                    GPSD_LOG(LOG_INF, &session->context->errout,
                             "TSIP: This device is Accutime Gold\n");
                    session->driver.tsip.subtype = TSIP_ACCUTIME_GOLD;
                    configuration_packets_accutime_gold(session);
                    break;
                case 1001:            // Lassen iQ
                    // FALLTHROUGH
                case 1002:            // Copernicus, Copernicus II
                    // FALLTHROUGH
                case 3007:            // Thunderbolt E
                    // FALLTHROUGH
                case 3023:            // RES SMT 360
                    // FALLTHROUGH
                case 3026:            // ICM SMT 360
                    // FALLTHROUGH
                case 3031:            // RES360 17x22
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
    case 0x41:			/* GPS Time */
	if (len != 10) {
            bad_len = 10;
	    break;
        }
	session->driver.tsip.last_41 = now;	/* keep timestamp for request */
	ftow = getbef32((char *)buf, 0);	/* gpstime */
	week = getbeu16(buf, 4);	/* week */
	f2 = getbef32((char *)buf, 6);	/* leap seconds */
	if (ftow >= 0.0 && f2 > 10.0) {
	    session->context->leap_seconds = (int)round(f2);
	    session->context->valid |= LEAP_SECOND_VALID;
	    DTOTS(&ts_tow, ftow);
	    session->newdata.time =
		gpsd_gpstime_resolv(session, week, ts_tow);
	    mask |= TIME_SET | NTPTIME_IS;
	}
	GPSD_LOG(LOG_INF, &session->context->errout,
		 "TSIP: GPS Time (0x41): tow %.2f week %u ls %.1f %s\n",
                 ftow, week, f2,
                 timespec_str(&session->newdata.time, ts_buf, sizeof(ts_buf)));
	break;
    case 0x42:			/* Single-Precision Position Fix, XYZ ECEF */
	if (len != 16) {
            bad_len = 16;
	    break;
        }
	f1 = getbef32((char *)buf, 0);	/* X */
	f2 = getbef32((char *)buf, 4);	/* Y */
	f3 = getbef32((char *)buf, 8);	/* Z */
	f4 = getbef32((char *)buf, 12);	/* time-of-fix */
	GPSD_LOG(LOG_INF, &session->context->errout,
		 "TSIP: GPS Position (0x42): XYZ %f %f %f %f\n",
                 f1, f2, f3, f4);
	break;
    case 0x43:			/* Velocity Fix, XYZ ECEF */
	if (len != 20) {
            bad_len = 20;
	    break;
        }
	f1 = getbef32((char *)buf, 0);	/* X velocity */
	f2 = getbef32((char *)buf, 4);	/* Y velocity */
	f3 = getbef32((char *)buf, 8);	/* Z velocity */
	f4 = getbef32((char *)buf, 12);	/* bias rate */
	f5 = getbef32((char *)buf, 16);	/* time-of-fix */
	GPSD_LOG(LOG_INF, &session->context->errout,
		 "TSIP: GPS Velocity (0x43): XYZ %f %f %f %f %f\n", f1, f2, f3,
		 f4, f5);
	break;
    case 0x45:			/* Software Version Information */
	if (len != 10) {
            bad_len = 10;
	    break;
        }
	(void)snprintf(session->subtype, sizeof(session->subtype),
		       "%d.%d %02d%02d%02d %d.%d %02d%02d%02d",
		       getub(buf, 0),
		       getub(buf, 1),
		       getub(buf, 4),
		       getub(buf, 2),
		       getub(buf, 3),
		       getub(buf, 5),
		       getub(buf, 6),
		       getub(buf, 9),
		       getub(buf, 7),
		       getub(buf, 8));
	GPSD_LOG(LOG_INF, &session->context->errout,
		 "TSIP: Software version (0x45): %s\n", session->subtype);
	mask |= DEVICEID_SET;
	break;
    case 0x46:
        /* Health of Receiver (0x46).  Poll with 0x26
         * Present on all models
         * RES SMT 360 says use 0x8f-ab or 0x8f-ac instead
         */
	if ( 2 > len) {
            bad_len = 2;
	    break;
        }
	session->driver.tsip.last_46 = now;
	u1 = getub(buf, 0);	/* Status code */
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
    case 0x47:			/* Signal Levels for all Satellites */
	if (1 > len) {
            bad_len = 1;
	    break;
        }
	gpsd_zero_satellites(&session->gpsdata);
	count = (int)getub(buf, 0);	/* satellite count */
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
    case 0x48:			/* GPS System Message */
	buf[len] = '\0';
	GPSD_LOG(LOG_PROG, &session->context->errout,
		 "TSIP: GPS System Message (0x48): %s\n", buf);
	break;
    case 0x4a:			/* Single-Precision Position LLA */
	if (len != 20) {
            bad_len = 20;
	    break;
        }
	session->newdata.latitude = getbef32((char *)buf, 0) * RAD_2_DEG;
	session->newdata.longitude = getbef32((char *)buf, 4) * RAD_2_DEG;
	/* depending on GPS config, could be either WGS84 or MSL
	 * default differs by model, usually WGS84, we try to force MSL */
	session->newdata.altMSL = getbef32((char *)buf, 8);
	//f1 = getbef32((char *)buf, 12);	clock bias */
	ftow = getbef32((char *)buf, 16);	/* time-of-fix */
	if ((session->context->valid & GPS_TIME_VALID)!=0) {
	    DTOTS(&ts_tow, ftow);
	    session->newdata.time =
		gpsd_gpstime_resolv(session, session->context->gps_week,
				    ts_tow);
	    mask |= TIME_SET | NTPTIME_IS;
	}
        // this seems to be first in cycle
        // REPORT_IS here breaks reports in read-only mode
	mask |= LATLON_SET | ALTITUDE_SET | CLEAR_IS;
	GPSD_LOG(LOG_DATA, &session->context->errout,
		 "TSIP: SP-PLLA (0x4a): time=%s lat=%.2f lon=%.2f "
                 "altMSL=%.2f\n",
                 timespec_str(&session->newdata.time, ts_buf, sizeof(ts_buf)),
		 session->newdata.latitude,
		 session->newdata.longitude,
		 session->newdata.altMSL);
	break;
    case 0x4b:
        /* Machine/Code ID and Additional Status */
        /* Present in all receivers? */
	if (len != 3) {
            bad_len = 3;
	    break;
        }
	session->driver.tsip.machine_id = getub(buf, 0);  /* Machine ID */
	u2 = getub(buf, 1);	/* Status 1 */
	u3 = getub(buf, 2);	/* Status 2/Superpacket Support */
	GPSD_LOG(LOG_INF, &session->context->errout,
		 "TSIP: Machine ID (0x4b): %02x %02x %02x\n",
                 session->driver.tsip.machine_id,
                 u2, u3);

        if ('\0' == session->subtype[0]) {
            // better than nothing
            switch (session->driver.tsip.machine_id) {
            case 1:
                // should use better name from superpacket
                name = " SMT 360";
                break;
            case 0x32:
                name = " Acutime 360";
                break;
            case 0x5a:
                name = " Lassen iQ";
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
                /* Position: 8F20, ECEF, DP, MSL, */
                putbyte(buf, 0, IO1_8F20|IO1_MSL|IO1_DP|IO1_ECEF);
                putbyte(buf, 1, 0x00);	        /* Velocity: none (via SP) */
                putbyte(buf, 2, 0x00);	        /* Time: GPS */
                putbyte(buf, 3, IO4_DBHZ);	/* Aux: dBHz */
                (void)tsip_write(session, 0x35, buf, 4);
                break;
            case 2:
                // 2 == SMT 360, no 0x8f-20
                break;
            }
	}
	break;
    case 0x55:			/* IO Options */
	if (len != 4) {
            bad_len = 4;
	    break;
        }
	u1 = getub(buf, 0);	/* Position */
        // FIXME: decode HAE/MSL from position
	u2 = getub(buf, 1);	/* Velocity */
	u3 = getub(buf, 2);	/* Timing */
	u4 = getub(buf, 3);	/* Aux */
	GPSD_LOG(LOG_INF, &session->context->errout,
		 "TSIP: IO Options (0x55): %02x %02x %02x %02x\n",
                 u1, u2, u3, u4);
	if ((u1 & 0x20) != (uint8_t) 0) {	/* Output Super Packets? */
            // Huh???
	    /* No LFwEI Super Packet */
	    putbyte(buf, 0, 0x20);
	    putbyte(buf, 1, 0x00);	/* disabled */
	    (void)tsip_write(session, 0x8e, buf, 2);

	    /* Request Compact Super Packet */
	    putbyte(buf, 0, 0x23);
	    putbyte(buf, 1, 0x01);	/* enabled */
	    (void)tsip_write(session, 0x8e, buf, 2);
	    session->driver.tsip.req_compact = now;
	}
	break;
    case 0x56:			/* Velocity Fix, East-North-Up (ENU) */
	if (len != 20) {
            bad_len = 20;
	    break;
        }
	f1 = getbef32((char *)buf, 0);	/* East velocity */
	f2 = getbef32((char *)buf, 4);	/* North velocity */
	f3 = getbef32((char *)buf, 8);	/* Up velocity */
	f4 = getbef32((char *)buf, 12);	/* clock bias rate */
	f5 = getbef32((char *)buf, 16);	/* time-of-fix */
	session->newdata.NED.velN = f2;
	session->newdata.NED.velE = f1;
	session->newdata.NED.velD = -f3;
	mask |= VNED_SET;
	GPSD_LOG(LOG_INF, &session->context->errout,
		 "TSIP: Vel ENU (0x56): %f %f %f %f %f\n",
                 f1, f2, f3, f4, f5);
	break;
    case 0x57:			/* Information About Last Computed Fix */
	if (len != 8) {
            bad_len = 8;
	    break;
        }
	u1 = getub(buf, 0);	                /* Source of information */
	u2 = getub(buf, 1);	                /* Mfg. diagnostic */
	ftow = getbef32((char *)buf, 2);	/* gps_time */
	week = getbeu16(buf, 6);	        /* tsip.gps_week */
	if (getub(buf, 0) == 0x01) {
            /* good current fix */
	    DTOTS(&ts_tow, ftow);
	    (void)gpsd_gpstime_resolv(session, week, ts_tow);
        }
	GPSD_LOG(LOG_INF, &session->context->errout,
		 "TSIP: Fix info (0x57): %02x %02x %u %f\n", u1, u2, week, f1);
	break;
    case 0x5a:			/* Raw Measurement Data */
	if (len != 29) {
            bad_len = 29;
	    break;
        }
	f1 = getbef32((char *)buf, 5);	/* Signal Level */
	f2 = getbef32((char *)buf, 9);	/* Code phase */
	f3 = getbef32((char *)buf, 13);	/* Doppler */
	d1 = getbed64((char *)buf, 17);	/* Time of Measurement */
	GPSD_LOG(LOG_PROG, &session->context->errout,
		 "TSIP: Raw Measurement Data (0x5a): %d %f %f %f %f\n",
		 getub(buf, 0), f1, f2, f3, d1);
	break;
    case 0x5c:
	/* Satellite Tracking Status (0x5c) polled by 0x3c
         *
         * GPS only, no WAAS reported here or used in fix
         * Present in:
         *  Copernicus, Copernicus II
         *  Thunderbold E
         * Note Present in:
         *  ICM SMT 360, RES SMT 360
         */
	if (len != 24) {
            bad_len = 24;
	    break;
        }
	u1 = getub(buf, 0);	/* PRN 1-32 */
	u2 = getub(buf, 1);	/* slot:chan */
	u3 = getub(buf, 2);	/* Acquisition flag */
	u4 = getub(buf, 3);	/* Ephemeris flag */
	f1 = getbef32((char *)buf, 4);	/* Signal level */
	f2 = getbef32((char *)buf, 8);	/* time of Last measurement */
	d1 = getbef32((char *)buf, 12) * RAD_2_DEG;	/* Elevation */
	d2 = getbef32((char *)buf, 16) * RAD_2_DEG;	/* Azimuth */
	i = (int)(u2 >> 3);	/* channel number */
	GPSD_LOG(LOG_INF, &session->context->errout,
		 "TSIP: Satellite Tracking Status (0x5c): Ch %2d PRN %3d "
                 "es %d Acq %d Eph %2d SNR %4.1f LMT %.04f El %4.1f Az %5.1f\n",
		 i, u1, u2 & 7, u3, u4, f1, f2, d1, d2);
	if (i < TSIP_CHANNELS) {
            session->gpsdata.skyview[i].PRN = (short)u1;
            session->gpsdata.skyview[i].svid = (unsigned char)u1;
            session->gpsdata.skyview[i].gnssid = GNSSID_GPS;
            session->gpsdata.skyview[i].ss = (double)f1;
            session->gpsdata.skyview[i].elevation = (double)d1;
            session->gpsdata.skyview[i].azimuth = (double)d2;
            session->gpsdata.skyview[i].used = false;
            if (0.1 < f1) {
                // check used list, if ss is non-zero
                for (j = 0; j < session->gpsdata.satellites_used; j++) {
                    if (session->gpsdata.skyview[i].PRN != 0 &&
                        session->driver.tsip.sats_used[j] != 0) {
                        session->gpsdata.skyview[i].used = true;
                    }
                }
            }
	    if (++i == session->gpsdata.satellites_visible) {
                // why not use GPS tow from bytes 8-11?
		session->gpsdata.skyview_time.tv_sec = 0;
		session->gpsdata.skyview_time.tv_nsec = 0;
		mask |= SATELLITE_SET;	/* last of the series */
	    }
	    if (i > session->gpsdata.satellites_visible)
		session->gpsdata.satellites_visible = i;
	}
	break;
     case 0x5d:
        /* GNSS Satellite Tracking Status (multi-GNSS operation) */
	if (len != 26) {
            bad_len = 26;
	    break;
        }
	u1 = getub(buf, 0);	/* PRN */
	u2 = getub(buf, 1);	/* chan */
	u3 = getub(buf, 2);	/* Acquisition flag */
	u4 = getub(buf, 3);	/* SV used in Position or Time calculation*/
	f1 = getbef32((char *)buf, 4);	/* Signal level */
	f2 = getbef32((char *)buf, 8);	/* time of Last measurement */
	d1 = getbef32((char *)buf, 12) * RAD_2_DEG;	/* Elevation */
	d2 = getbef32((char *)buf, 16) * RAD_2_DEG;	/* Azimuth */
	u5 = getub(buf, 20);	/* old measurement flag */
	u6 = getub(buf, 21);	/* integer msec flag */
	u7 = getub(buf, 22);	/* bad data flag */
	u8 = getub(buf, 23);	/* data collection flag */
	u9 = getub(buf, 24);	/* Used flags */
	u10 = getub(buf, 25);	/* SV Type */

	i = u2;			/* channel number */
	GPSD_LOG(LOG_INF, &session->context->errout,
		"TSIP: Satellite Tracking Status (0x5d): Ch %2d Con %d PRN %3d "
                "Acq %d Use %d SNR %4.1f LMT %.04f El %4.1f Az %5.1f Old %d "
                "Int %d Bad %d Col %d TPF %d SVT %d\n",
		i, u10, u1, u3, u4, f1, f2, d1, d2, u5, u6, u7, u8, u9, u10);
	if (i < TSIP_CHANNELS) {
	    if (d1 >= 0.0) {
		session->gpsdata.skyview[i].PRN = (short)u1;
		session->gpsdata.skyview[i].ss = (double)f1;
		session->gpsdata.skyview[i].elevation = (double)d1;
		session->gpsdata.skyview[i].azimuth = (double)d2;
		session->gpsdata.skyview[i].used = (bool)u4;
	    } else {
		session->gpsdata.skyview[i].PRN = (short)u1;
		session->gpsdata.skyview[i].elevation = NAN;
		session->gpsdata.skyview[i].azimuth = NAN;
		session->gpsdata.skyview[i].ss = NAN;
		session->gpsdata.skyview[i].used = false;
	    }
	    if (++i == session->gpsdata.satellites_visible) {
		session->gpsdata.skyview_time.tv_sec = 0;
		session->gpsdata.skyview_time.tv_nsec = 0;
		mask |= SATELLITE_SET;	/* last of the series */
	    }
	    if (i > session->gpsdata.satellites_visible)
		session->gpsdata.satellites_visible = i;
	}
	break;
    case 0x6c:
	/* Satellite Selection List (0x6c) polled by 0x24
         *
         * Present in:
         *   ICM SMT 360 (2018)
         *   RES SMT 360 (2018)
         * Not present in:
         *   Lassen SQ (2002)
         *   Lassen iQ (2005) */
	if (18 > len) {
            bad_len = 18;
	    break;
        }
	//u1 = getub(buf, 0);	/* nsvs/dimension UNUSED */
	count = (int)getub(buf, 17);
	if (len != (18 + count)) {
            bad_len = 18 + count;
	    break;
        }

        // why same as 6d?
	session->driver.tsip.last_6d = now;	/* keep timestamp for request */
	/*
	 * This looks right, but it sets a spurious mode value when
	 * the satellite constellation looks good to the chip but no
	 * actual fix has yet been acquired.  We should set the mode
	 * field (which controls gpsd's fix reporting) only from sentences
	 * that convey actual fix information, like 0x8f-20, but some
         * TSIP do not support 0x8f-20, and 0x6c may be all we got.
	 */
	switch (u1 & 7) {	/* dimension */
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
            session->driver.tsip.sats_used[i] = (short)getub(buf, 18 + i);
            if (session->context->errout.debug >= LOG_DATA) {
                str_appendf(buf2, sizeof(buf2),
                               " %d", session->driver.tsip.sats_used[i]);
            }
        }
	GPSD_LOG(LOG_DATA, &session->context->errout,
		 "TSIP: AIVSS (0x6c): status=%d used=%d "
		 "pdop=%.1f hdop=%.1f vdop=%.1f tdop=%.1f gdop=%.1f Used:%s\n",
		 session->gpsdata.status,
		 session->gpsdata.satellites_used,
		 session->gpsdata.dop.pdop,
		 session->gpsdata.dop.hdop,
		 session->gpsdata.dop.vdop,
		 session->gpsdata.dop.tdop,
		 session->gpsdata.dop.gdop,
                 buf2);
	mask |= DOP_SET | STATUS_SET | USED_IS;
	break;
    case 0x6d:
        /* All-In-View Satellite Selection (0x6d) polled by 0x24
         *
         * Present in:
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
	u1 = getub(buf, 0);	/* nsvs/dimension */
	count = (int)((u1 >> 4) & 0x0f);
	if (len != (17 + count)) {
            bad_len = 17 + count;
	    break;
        }
	session->driver.tsip.last_6d = now;	/* keep timestamp for request */
	/*
	 * This looks right, but it sets a spurious mode value when
	 * the satellite constellation looks good to the chip but no
	 * actual fix has yet been acquired.  We should set the mode
	 * field (which controls gpsd's fix reporting) only from sentences
	 * that convey actual fix information, like 0x8f-20, but some
         * TSIP do not support 0x8f-20, and 0x6c may be all we got.
	 */
	switch (u1 & 7) {	/* dimension */
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
	GPSD_LOG(LOG_DATA, &session->context->errout,
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
    case 0x82:			/* Differential Position Fix Mode */
	if (len != 1) {
            bad_len = 1;
	    break;
        }
	u1 = getub(buf, 0);	/* fix mode */
	if (session->gpsdata.status == STATUS_FIX && (u1 & 0x01) != 0) {
	    session->gpsdata.status = STATUS_DGPS_FIX;
	    mask |= STATUS_SET;
	}
	GPSD_LOG(LOG_DATA, &session->context->errout,
		 "TSIP: DPFM (0x82) mode %d status=%d\n",
                 u1, session->gpsdata.status);
	break;
    case 0x83:     /* Double-Precision XYZ Position Fix and Bias Information */
	if (len != 36) {
            bad_len = 36;
	    break;
        }
	d1 = getbed64((char *)buf, 0);	/* X */
	d2 = getbed64((char *)buf, 8);	/* Y */
	d3 = getbed64((char *)buf, 16);	/* Z */
	d4 = getbed64((char *)buf, 24);	/* clock bias */
	f1 = getbef32((char *)buf, 32);	/* time-of-fix */
	GPSD_LOG(LOG_INF, &session->context->errout,
		 "TSIP: Position (0x83) XYZ %f %f %f %f %f\n",
                 d1, d2, d3, d4, f1);
	break;
    case 0x84:     /* Double-Precision LLA Position Fix and Bias Information */
	if (len != 36) {
            bad_len = 36;
	    break;
        }
	session->newdata.latitude = getbed64((char *)buf, 0) * RAD_2_DEG;
	session->newdata.longitude = getbed64((char *)buf, 8) * RAD_2_DEG;
	/* depending on GPS config, could be either WGS84 or MSL
	 * default differs by model, usually WGS84 */
	session->newdata.altMSL = getbed64((char *)buf, 16);
	mask |= ALTITUDE_SET;
	//d1 = getbed64((char *)buf, 24);	clock bias */
	ftow = getbef32((char *)buf, 32);	/* time-of-fix */
	if ((session->context->valid & GPS_TIME_VALID)!=0) {
	    DTOTS(&ts_tow, ftow);
	    session->newdata.time =
		gpsd_gpstime_resolv(session, session->context->gps_week,
				    ts_tow);
	    mask |= TIME_SET | NTPTIME_IS;
	}
	GPSD_LOG(LOG_INF, &session->context->errout,
		 "TSIP: DP-PLLA (0x84) %s %f %f %f\n",
                 timespec_str(&session->newdata.time, ts_buf, sizeof(ts_buf)),
		 session->newdata.latitude,
		 session->newdata.longitude, session->newdata.altMSL);
        // this seems to be first in cycle
	mask |= LATLON_SET | CLEAR_IS;
	GPSD_LOG(LOG_DATA, &session->context->errout,
		 "TSIP: DP-PLLA (0x84) time=%s lat=%.2f lon=%.2f altMSL=%.2f\n",
                 timespec_str(&session->newdata.time, ts_buf, sizeof(ts_buf)),
		 session->newdata.latitude,
		 session->newdata.longitude,
		 session->newdata.altMSL);
	break;
    case 0x8f:			/* Super Packet.  Well...  */
	u1 = (uint8_t) getub(buf, 0);
	switch (u1) {		/* sub-packet ID */
	case 0x15:		/* Current Datum Values */
	    if (len != 43) {
                bad_len = 43;
                break;
            }
	    s1 = getbes16(buf, 1);	/* Datum Index */
	    d1 = getbed64((char *)buf, 3);	/* DX */
	    d2 = getbed64((char *)buf, 11);	/* DY */
	    d3 = getbed64((char *)buf, 19);	/* DZ */
	    d4 = getbed64((char *)buf, 27);	/* A-axis */
	    d5 = getbed64((char *)buf, 35);	/* Eccentricity Squared */
	    GPSD_LOG(LOG_INF, &session->context->errout,
		     "TSIP: Current Datum (0x8f-15) %d %f %f %f %f %f\n",
                     s1, d1, d2, d3, d4, d5);
	    break;

	case 0x20:
            /* Last Fix with Extra Information (binary fixed point) 0x8f-20 */
	    /* CSK sez "why does my Lassen SQ output oversize packets?" */
            /* Present in:
             *  ACE II
             * Not present in:
             *  ICM SMT 360
             *  RES SMT 360
             */
	    if ((len != 56) && (len != 64)) {
                bad_len = 56;
                break;
            }
	    s1 = getbes16(buf, 2);	/* east velocity */
	    s2 = getbes16(buf, 4);	/* north velocity */
	    s3 = getbes16(buf, 6);	/* up velocity */
	    tow = getbeu32(buf, 8) * 1000;	/* time */
	    sl1 = getbes32(buf, 12);	/* latitude */
	    ul2 = getbeu32(buf, 16);	/* longitude */
	    /* depending on GPS config, could be either WGS84 or MSL
	     * default differs by model, usually WGS84 */
	    sl2 = getbes32(buf, 20);	/* altitude */
	    u1 = getub(buf, 24);	/* velocity scaling */
	    u2 = getub(buf, 27);	/* fix flags */
	    u3 = getub(buf, 28);	/* num svs */
	    u4 = getub(buf, 29);	/* utc offset */
	    week = getbeu16(buf, 30);	/* tsip.gps_week */
	    /* PRN/IODE data follows */
	    GPSD_LOG(LOG_DATA, &session->context->errout,
		     "TSIP: LFwEI (0x8f-20) %d %d %d %u %d %u %u "
                     "%x %x %u %u %d\n",
                     s1, s2, s3, ul1, sl1, ul2, sl2, u1, u2, u3, u4, week);

	    if ((u1 & 0x01) != (uint8_t) 0)	/* check velocity scaling */
		d5 = 0.02;
	    else
		d5 = 0.005;
	    d1 = (double)s1 * d5;	/* east velocity m/s */
	    d2 = (double)s2 * d5;	/* north velocity m/s */
	    d3 = (double)s3 * d5;       /* up velocity m/s */
	    session->newdata.NED.velN = d2;
	    session->newdata.NED.velE = d1;
	    session->newdata.NED.velD = -d3;

	    session->newdata.latitude = (double)sl1 * SEMI_2_DEG;
	    session->newdata.longitude = (double)ul2 * SEMI_2_DEG;
	    if (session->newdata.longitude > 180.0)
		session->newdata.longitude -= 360.0;
	    /* depending on GPS config, could be either WGS84 or MSL
	     * default differs by model, usually WGS84, we try to force MSL */
	    session->newdata.altMSL = (double)sl2 * 1e-3;
	    mask |= ALTITUDE_SET;

	    session->gpsdata.status = STATUS_NO_FIX;
	    session->newdata.mode = MODE_NO_FIX;
	    if ((u2 & 0x01) == (uint8_t) 0) {	/* Fix Available */
		session->gpsdata.status = STATUS_FIX;
		if ((u2 & 0x02) != (uint8_t) 0)	/* DGPS Corrected */
		    session->gpsdata.status = STATUS_DGPS_FIX;
		if ((u2 & 0x04) != (uint8_t) 0)	/* Fix Dimension */
		    session->newdata.mode = MODE_2D;
		else
		    session->newdata.mode = MODE_3D;
	    }
	    session->gpsdata.satellites_used = (int)u3;
	    if ((int)u4 > 10) {
		session->context->leap_seconds = (int)u4;
		session->context->valid |= LEAP_SECOND_VALID;
	    }
	    MSTOTS(&ts_tow, tow);
	    session->newdata.time = gpsd_gpstime_resolv(session, week,
						        ts_tow);
	    mask |= TIME_SET | NTPTIME_IS | LATLON_SET |
		    STATUS_SET | MODE_SET | CLEAR_IS |
		    REPORT_IS | VNED_SET;
	    GPSD_LOG(LOG_DATA, &session->context->errout,
		     "TSIP: SP-LFEI (0x8f-20): time=%s lat=%.2f lon=%.2f "
                     "altMSL=%.2f mode=%d status=%d\n",
                     timespec_str(&session->newdata.time, ts_buf,
                                  sizeof(ts_buf)),
		     session->newdata.latitude, session->newdata.longitude,
		     session->newdata.altMSL,
		     session->newdata.mode, session->gpsdata.status);
	    break;
	case 0x23:		/* Compact Super Packet */
	    session->driver.tsip.req_compact = 0;
	    /* CSK sez "i don't trust this to not be oversized either." */
	    if (len < 29) {
                bad_len = 29;
                break;
            }
	    tow = getbeu32(buf, 1) * 1000;	/* time */
	    week = getbeu16(buf, 5);	        /* tsip.gps_week */
	    u1 = getub(buf, 7);	                /* utc offset */
	    u2 = getub(buf, 8);	                /* fix flags */
	    sl1 = getbes32(buf, 9);	        /* latitude */
	    ul2 = getbeu32(buf, 13);	        /* longitude */
	    /* depending on GPS config, could be either WGS84 or MSL
	     * default differs by model, usually WGS84 */
	    sl3 = getbes32(buf, 17);	/* altitude */
            /* set xNED here */
	    s2 = getbes16(buf, 21);	/* east velocity */
	    s3 = getbes16(buf, 23);	/* north velocity */
	    s4 = getbes16(buf, 25);	/* up velocity */
	    GPSD_LOG(LOG_INF, &session->context->errout,
		     "TSIP: CSP (0x8f-23): %u %d %u %u %d %u %d %d %d %d\n",
                     ul1, week, u1, u2, sl1, ul2, sl3, s2, s3, s4);
	    if ((int)u1 > 10) {
		session->context->leap_seconds = (int)u1;
		session->context->valid |= LEAP_SECOND_VALID;
	    }
	    MSTOTS(&ts_tow, tow);
	    session->newdata.time =
		gpsd_gpstime_resolv(session, week, ts_tow);
	    session->gpsdata.status = STATUS_NO_FIX;
	    session->newdata.mode = MODE_NO_FIX;
	    if ((u2 & 0x01) == (uint8_t) 0) {	/* Fix Available */
		session->gpsdata.status = STATUS_FIX;
		if ((u2 & 0x02) != (uint8_t) 0)	/* DGPS Corrected */
		    session->gpsdata.status = STATUS_DGPS_FIX;
		if ((u2 & 0x04) != (uint8_t) 0)	/* Fix Dimension */
		    session->newdata.mode = MODE_2D;
		else
		    session->newdata.mode = MODE_3D;
	    }
	    session->newdata.latitude = (double)sl1 * SEMI_2_DEG;
	    session->newdata.longitude = (double)ul2 * SEMI_2_DEG;
	    if (session->newdata.longitude > 180.0)
		session->newdata.longitude -= 360.0;
	    /* depending on GPS config, could be either WGS84 or MSL
	     * default differs by model, usually WGS84, we try to force MSL */
	    session->newdata.altMSL = (double)sl3 * 1e-3;
	    mask |= ALTITUDE_SET;
	    if ((u2 & 0x20) != (uint8_t) 0)	/* check velocity scaling */
		d5 = 0.02;
	    else
		d5 = 0.005;
	    d1 = (double)s2 * d5;	/* east velocity m/s */
	    d2 = (double)s3 * d5;	/* north velocity m/s */
	    d3 = (double)s4 * d5;	/* up velocity m/s */
	    session->newdata.NED.velN = d2;
	    session->newdata.NED.velE = d1;
	    session->newdata.NED.velD = -d3;

	    mask |= TIME_SET | NTPTIME_IS | LATLON_SET |
		    STATUS_SET | MODE_SET | CLEAR_IS |
		    REPORT_IS | VNED_SET;
	    GPSD_LOG(LOG_DATA, &session->context->errout,
		     "TSIP: SP-CSP 0x23: time %s lat %.2f lon %.2f "
                     "altMSL %.2f mode %d status %d\n",
                     timespec_str(&session->newdata.time, ts_buf,
                                  sizeof(ts_buf)),
		     session->newdata.latitude, session->newdata.longitude,
		     session->newdata.altMSL,
		     session->newdata.mode, session->gpsdata.status);
	    break;

	case 0xab:		/* Thunderbolt Timing Superpacket */
	    if (len != 17) {
                bad_len = 17;
                break;
	    }
	    session->driver.tsip.last_41 = now;	/* keep timestamp for request */
	    tow = getbeu32(buf, 1) * 1000;	/* gpstime */
	    week = getbeu16(buf, 5);	        /* week */
            /* leap seconds */
            session->context->leap_seconds = (int)getbes16(buf, 7);
	    u1 = buf[9];                // Time Flag
            // should check time valid?
            /* ignore the broken down time, use the GNSS time.
             * Hope it is not BeiDou time */

            // how do we know leap valid?
            session->context->valid |= LEAP_SECOND_VALID;
            MSTOTS(&ts_tow, tow);
            session->newdata.time = gpsd_gpstime_resolv(session, week, ts_tow);
            mask |= TIME_SET | NTPTIME_IS | CLEAR_IS;
            GPSD_LOG(LOG_DATA, &session->context->errout,
                     "TSIP: SP-TTS 0xab time=%s mask=%s\n",
                     timespec_str(&session->newdata.time, ts_buf,
                                  sizeof(ts_buf)),
                     gps_maskdump(mask));

	    GPSD_LOG(LOG_PROG, &session->context->errout,
		     "TSIP: SP-TTS (0x8f-ab) GPS Time %u %u %d flag x%x\n",
                     ul1, week, session->context->leap_seconds, u1);
	    break;


	case 0xac:		/* Thunderbolt Position Superpacket */
	    if (len != 68) {
                bad_len = 68;
                break;
	    }

	    u2 = getub(buf, 1);	        /* Receiver Mode */
	    u1 = getub(buf, 12);	/* GPS Decoding Status */
            // ignore 2, Disciplining Mode
            // ignore 3, Self-Survey Progress
            // ignore 4-7, Holdover Duration
            // ignore 8-9, Critical Alarms
            // ignore 10-11, Minor Alarms
            // ignore 12, GNSS Decoding Status
            // ignore 13, Disciplining Activity
            // ignore 14, PPS indication
            // ignore 15, PPS reference
	    // f1 = getbef32((char *)buf, 16);  // PPS Offset
            // ignore 20-23, Clock Offset
            // ignore 24-27, DAC Value
            // ignore 28-31, DAC Voltage
            // ignore 32-35, Temperature
	    session->newdata.latitude = getbed64((char *)buf, 36) * RAD_2_DEG;
	    session->newdata.longitude = getbed64((char *)buf, 44) * RAD_2_DEG;
	    /* depending on GPS config, could be either WGS84 or MSL
	     * default differs by model, usually WGS84, we try to force MSL */
	    session->newdata.altMSL = getbed64((char *)buf, 52);
            // ignore 60-63, always zero
            // ignore 64-67, reserved

	    if (u1 != (uint8_t) 0) {
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
                switch (u1) {
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
	    case 6:		/* Clock Hold 2D */
                // FALLTHROUGH
	    case 3:		/* 2D Position Fix */
		//session->gpsdata.status = STATUS_FIX;
		session->newdata.mode = MODE_2D;
		break;
	    case 7:		/* Thunderbolt overdetermined clock */
                // FALLTHROUGH
	    case 4:		/* 3D position Fix */
		//session->gpsdata.status = STATUS_FIX;
		session->newdata.mode = MODE_3D;
		break;
	    default:
		//session->gpsdata.status = STATUS_NO_FIX;
		session->newdata.mode = MODE_NO_FIX;
		break;
	    }

	    mask |= LATLON_SET | ALTITUDE_SET | MODE_SET | REPORT_IS;
	    GPSD_LOG(LOG_DATA, &session->context->errout,
		     "TSIP: SP-TPS (0x8f-ac) lat=%.2f lon=%.2f altMSL=%.2f "
                     "mask %s\n",
		     session->newdata.latitude,
		     session->newdata.longitude,
		     session->newdata.altMSL,
                     gps_maskdump(mask));
	    break;

	default:
	    GPSD_LOG(LOG_WARN, &session->context->errout,
		     "TSIP: Unhandled TSIP superpacket type 0x8f-%02x\n",
		     u1);
	}
	break;
    case 0xbb:			/* Navigation Configuration */
	if (len != 40 && len != 43) {
            /* see packet.c for explamation */
            bad_len = 40;
	    break;
        }
	u1 = getub(buf, 0);	/* Subcode */
	u2 = getub(buf, 1);	/* Operating Dimension */
	u3 = getub(buf, 2);	/* DGPS Mode (not enabled in Accutime Gold) */
	u4 = getub(buf, 3);	/* Dynamics Code */
	f1 = getbef32((char *)buf, 5);	/* Elevation Mask */
	f2 = getbef32((char *)buf, 9);	/* AMU Mask */
	f3 = getbef32((char *)buf, 13);	/* DOP Mask */
	f4 = getbef32((char *)buf, 17);	/* DOP Switch */
	u5 = getub(buf, 21);	/* DGPS Age Limit (not in Accutime Gold) */
	GPSD_LOG(LOG_INF, &session->context->errout,
		 "TSIP: Navigation Configuration (0xbb) %u %u %u %u %f %f %f "
                 "%f %u\n",
		 u1, u2, u3, u4, f1, f2, f3, f4, u5);
	break;

    case 0x49:			/* Almanac Health Page */
	// FALLTHROUGH
    case 0x4c:			/* Operating Parameters Report */
	// FALLTHROUGH
    case 0x54:			/* One Satellite Bias */
	// FALLTHROUGH
    case 0x58:		/* Satellite System Data/Acknowledge from Receiver */
	// FALLTHROUGH
    case 0x59:		/* Status of Satellite Disable or Ignore Health */
	// FALLTHROUGH
    case 0x5b:			/* Satellite Ephemeris Status */
	// FALLTHROUGH
    case 0x5e:			/* Additional Fix Status Report */
	// FALLTHROUGH
    case 0x6e:			/* Synchronized Measurements */
	// FALLTHROUGH
    case 0x6f:			/* Synchronized Measurements Report */
	// FALLTHROUGH
    case 0x70:			/* Filter Report */
	// FALLTHROUGH
    case 0x7a:			/* NMEA settings */
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
	/* Request Current Time
         * Returns 0x41. */
	(void)tsip_write(session, 0x21, buf, 0);
	session->driver.tsip.last_41 = now;
    }

    if ((now - session->driver.tsip.last_6d) > 5) {
	/* Request GPS Receiver Position Fix Mode
         * Returns 0x44 or 0x6d. */
	(void)tsip_write(session, 0x24, buf, 0);
	session->driver.tsip.last_6d = now;
    }

    if (1 > session->driver.tsip.superpkt &&
        (now - session->driver.tsip.last_48) > 60) {
	/* Request GPS System Message
         * Returns 0x48.
         * not supported on:
         *  Lassen SQ (2002)
         *  Lassen iQ (2005)
         *  and post 2005
         * We assume SuperPackets replaced 0x28 */
	(void)tsip_write(session, 0x28, buf, 0);
	session->driver.tsip.last_48 = now;
    }

    if ((now - session->driver.tsip.last_5c) >= 5) {
	/* Request Current Satellite Tracking Status
         * Returns: 0x5c or 0x5d
	 *  5c in PS only devices
	 *  5d in multi-gnss devices */
	putbyte(buf, 0, 0x00);	/* All satellites */
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
	/* Compact Superpacket requested but no response */
	session->driver.tsip.req_compact = 0;
	GPSD_LOG(LOG_WARN, &session->context->errout,
		 "TSIP: No Compact Super Packet, use LFwEI\n");

	/* Request LFwEI Super Packet */
	putbyte(buf, 0, 0x20);
	putbyte(buf, 1, 0x01);	/* enabled */
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
    if (session->context->readonly)
	return;
    if (event == event_identified) {
	unsigned char buf[100];

	/*
	 * Set basic configuration, using Set or Request I/O Options (0x35).
         * in case no hardware config response comes back.
	 */
        /* Position: enable: Double Precision, MSL, LLA
         *           disable: ECEF */
	putbyte(buf, 0, IO1_DP|IO1_MSL|IO1_LLA);
        /* Velocity: enable: ENU, disable vECEF */
	putbyte(buf, 1, IO2_ENU);
        /* Time: enable: 0x42, 0x43, 0x4a
         *       disable: 0x83, 0x84, 0x56 */
	putbyte(buf, 2, 0x00);
        /* Aux: enable: 0x5A, dBHz */
	putbyte(buf, 3, IO4_DBHZ);
	(void)tsip_write(session, 0x35, buf, 4);
    }
    if (event == event_configure && session->lexer.counter == 0) {
	/*
	 * TSIP is often ODD parity 1 stopbit, save original values and
	 * change it Thunderbolts and Copernicus use
	 * 8N1... which isn't exactly a good idea due to the
	 * fragile wire format.  We must divine a clever
	 * heuristic to decide if the parity change is required.
	 */
	session->driver.tsip.parity = session->gpsdata.dev.parity;
	session->driver.tsip.stopbits =
	    (unsigned int) session->gpsdata.dev.stopbits;
        // FIXME.  Should respect fixed speed/framing
	gpsd_set_speed(session, session->gpsdata.dev.baudrate, 'O', 1);
    }
    if (event == event_deactivate) {
	/* restore saved parity and stopbits when leaving TSIP mode */
	gpsd_set_speed(session,
		       session->gpsdata.dev.baudrate,
		       session->driver.tsip.parity,
		       session->driver.tsip.stopbits);
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
    putbyte(buf, 0, 0xff);	/* current port */
    /* input dev.baudrate */
    putbyte(buf, 1, (round(log((double)speed / 300) / GPS_LN2)) + 2);
    putbyte(buf, 2, getub(buf, 1));	/* output baudrate */
    putbyte(buf, 3, 3);		/* character width (8 bits) */
    putbyte(buf, 4, parity);	/* parity (normally odd) */
    putbyte(buf, 5, stopbits - 1);	/* stop bits (normally 1 stopbit) */
    putbyte(buf, 6, 0);		/* flow control (none) */
    putbyte(buf, 7, 0x02);	/* input protocol (TSIP) */
    putbyte(buf, 8, 0x02);	/* output protocol (TSIP) */
    putbyte(buf, 9, 0);		/* reserved */
    (void)tsip_write(session, 0xbc, buf, 10);

    return true;		/* it would be nice to error-check this */
}

static void tsip_mode(struct gps_device_t *session, int mode)
{
    if (mode == MODE_NMEA) {
	unsigned char buf[16];

        /* send NMEA Interval and Message Mask Command (0x7a)
	* First turn on the NMEA messages we want */
	putbyte(buf, 0, 0x00);	/* subcode 0 */
	putbyte(buf, 1, 0x01);	/* 1-second fix interval */
	putbyte(buf, 2, 0x00);	/* Reserved */
	putbyte(buf, 3, 0x00);	/* Reserved */
	putbyte(buf, 4, 0x01);	/* 1=GST, Reserved */
	/* 1=GGA, 2=GGL, 4=VTG, 8=GSV, */
	/* 0x10=GSA, 0x20=ZDA, 0x40=Reserved, 0x80=RMC  */
	putbyte(buf, 5, 0x19);

	(void)tsip_write(session, 0x7A, buf, 6);

	/* Now switch to NMEA mode */

	memset(buf, 0, sizeof(buf));

        /* Set Port Configuration (0xbc) */
        // 4800, really?
	putbyte(buf, 0, 0xff);	/* current port */
	putbyte(buf, 1, 0x06);	/* 4800 bps input */
	putbyte(buf, 2, 0x06);	/* 4800 bps output */
	putbyte(buf, 3, 0x03);	/* 8 data bits */
	putbyte(buf, 4, 0x00);	/* No parity */
	putbyte(buf, 5, 0x00);	/* 1 stop bit */
	putbyte(buf, 6, 0x00);	/* No flow control */
	putbyte(buf, 7, 0x02);	/* Input protocol TSIP */
	putbyte(buf, 8, 0x04);	/* Output protocol NMEA */
	putbyte(buf, 9, 0x00);	/* Reserved */

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
        /* Position: enable: Double Precision, MSL, LLA
         *           disable: ECEF */
	putbyte(buf, 0, IO1_DP|IO1_MSL|IO1_LLA);
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

/* configure Accutime Gold to a known state */
void configuration_packets_accutime_gold(struct gps_device_t *session)
{
	unsigned char buf[100];

	GPSD_LOG(LOG_PROG, &session->context->errout,
		 "TSIP: configuration_packets_accutime_gold()\n");

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

/* this is everything we export */
/* *INDENT-OFF* */
const struct gps_type_t driver_tsip =
{
    .type_name      = "Trimble TSIP",	/* full name of type */
    .packet_type    = TSIP_PACKET,	/* associated lexer packet type */
    .flags	    = DRIVER_STICKY,	/* remember this */
    .trigger	    = NULL,		/* no trigger */
    .channels       = TSIP_CHANNELS,	/* consumer-grade GPS */
    .probe_detect   = tsip_detect,	/* probe for 9600O81 device */
    .get_packet     = generic_get,	/* use the generic packet getter */
    .parse_packet   = tsip_parse_input,	/* parse message packets */
    .rtcm_writer    = NULL,		/* doesn't accept DGPS corrections */
    .init_query     = tsip_init_query,	/* non-perturbing initial query */
    .event_hook     = tsip_event_hook,	/* fire on various lifetime events */
#ifdef RECONFIGURE_ENABLE
    .speed_switcher = tsip_speed_switch,/* change baud rate */
    .mode_switcher  = tsip_mode,	/* there is a mode switcher */
    .rate_switcher  = NULL,		/* no rate switcher */
    .min_cycle.tv_sec  = 1,		/* not relevant, no rate switch */
    .min_cycle.tv_nsec = 0,		/* not relevant, no rate switch */
#endif /* RECONFIGURE_ENABLE */
#ifdef CONTROLSEND_ENABLE
    .control_send   = tsip_control_send,/* how to send commands */
#endif /* CONTROLSEND_ENABLE */
    .time_offset     = NULL,
};
/* *INDENT-ON* */

#endif /* TSIP_ENABLE */
