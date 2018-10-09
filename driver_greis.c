/*
 * A Javad GNSS Receiver External Interface Specification (GREIS) driver.
 *
 * Author(s):
 * - Gregory Fong <gregory.fong@virginorbit.com>
 *
 * Documentation for GREIS can be found at:
 *   http://www.javad.com/downloads/javadgnss/manuals/GREIS/GREIS_Reference_Guide.pdf
 *
 * The version used for reference is that which
 * "Reflects Firmware Version 3.6.7, Last revised: August 25, 2016".
 *
 * This assumes little endian byte order in messages, which is the default, but
 * that is configurable. A future improvement could change to read the
 * information in [MF] Message Format.
 *
 * This file is Copyright (c) 2017 Virgin Orbit
 * SPDX-License-Identifier: BSD-2-clause
 */

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>

#include "bits.h"
#include "driver_greis.h"
#include "gpsd.h"

#if defined(GREIS_ENABLE) && defined(BINARY_ENABLE)

#define HEADER_LENGTH 5

static ssize_t greis_write(struct gps_device_t *session,
			   const char *msg, size_t msglen);
static const char disable_messages[] = "\%dm\%dm";
static const char get_vendor[] = "\%vendor\%print,/par/rcv/vendor";
static const char get_ver[] = "\%ver\%print,rcv/ver";
static const char set_update_rate_4hz[] = "\%msint\%set,/par/raw/msint,250";

/* Where applicable, the order here is how these will be received per cycle. */
/* TODO: stop hardcoding the cycle time, make it selectable */
static const char enable_messages_4hz[] =
    "\%em\%em,,jps/{RT,UO,GT,PV,SG,DP,SI,EL,AZ,EC,SS,ET}:0.25";

/*
 * GREIS message handlers. The checksum has been already confirmed valid in the
 * packet acceptance logic, so we don't need to retest it here.
 */

/**
 * Handle the message [RE] Reply
 */
static gps_mask_t greis_msg_RE(struct gps_device_t *session,
			       unsigned char *buf, size_t len)
{
    if (0 == memcmp(buf, "%ver%", 5)) {
        strlcpy(session->subtype, (const char*)&buf[5],
	        sizeof(session->subtype));
	gpsd_log(&session->context->errout, LOG_DATA,
		 "GREIS: RE, ->subtype: %s\n", session->subtype);
        return DEVICEID_SET;
    }

    gpsd_log(&session->context->errout, LOG_INFO,
	     "GREIS: RE %3zd, reply: %.*s\n", len, (int)len, buf);
    return 0;
}

/**
 * Handle the message [ER] Reply
 */
static gps_mask_t greis_msg_ER(struct gps_device_t *session,
			       unsigned char *buf, size_t len)
{
    gpsd_log(&session->context->errout, LOG_WARN,
	     "GREIS: ER %3zd, reply: %.*s\n", len, (int)len, buf);
    return 0;
}

/**
 * Handle the message [~~](RT) Receiver Time.
 */
static gps_mask_t greis_msg_RT(struct gps_device_t *session,
			       unsigned char *buf, size_t len)
{
    if (len < 5) {
	gpsd_log(&session->context->errout, LOG_WARN,
		 "GREIS: RT bad len %zu\n", len);
	return 0;
    }

    session->driver.greis.rt_tod = getleu32(buf, 0);

    session->driver.greis.seen_rt = true;
    session->driver.greis.seen_az = false;
    session->driver.greis.seen_ec = false;
    session->driver.greis.seen_el = false;
    session->driver.greis.seen_si = false;
    gpsd_log(&session->context->errout, LOG_DATA,
	     "GREIS: RT, tod: %lu\n",
	     (unsigned long)session->driver.greis.rt_tod);

    return CLEAR_IS;
}

/**
 * Handle the message [UO] GPS UTC Time Parameters.
 */
static gps_mask_t greis_msg_UO(struct gps_device_t *session,
			       unsigned char *buf, size_t len)
{
    /*
     * For additional details on these parameters and the computation done using
     * them, refer to the Javad GREIS spec mentioned at the top of this file and
     * also to ICD-GPS-200C, Revision IRN-200C-004 April 12, 2000. At the time
     * of writing, that could be found at
     * https://www.navcen.uscg.gov/pubs/gps/icd200/ICD200Cw1234.pdf .
     */
    uint32_t tot;	    /* Reference time of week [s] */
    uint16_t wnt;	    /* Reference week number [dimensionless] */
    int8_t dtls;	    /* Delta time due to leap seconds [s] */
    uint8_t dn;		    /* 'Future' reference day number [1..7] */
    uint16_t wnlsf;	    /* 'Future' reference week number [dimensionless] */
    int8_t dtlsf;	    /* 'Future' delta time due to leap seconds [s] */

    if (len < 24) {
	gpsd_log(&session->context->errout, LOG_WARN,
		 "GREIS: UO bad len %zu\n", len);
	return 0;
    }

    tot = getleu32(buf, 12);
    wnt = getleu16(buf, 16);
    dtls = getsb(buf, 18);
    dn = getub(buf, 19);
    wnlsf = getleu16(buf, 20);
    dtlsf = getsb(buf, 22);
    session->driver.greis.seen_uo = true;

    /*
     * See ICD-GPS-200C 20.3.3.5.2.4 "Universal Coordinated Time (UTC)".
     * I totally ripped this off of driver_navcom.c.  Might want to dedupe at
     * some point.
     */
    if ((wnt % 256U) * 604800U + tot < wnlsf * 604800U + dn * 86400U) {
	/* Current time is before effectivity time of the leap second event */
	session->context->leap_seconds = dtls;
    } else {
	session->context->leap_seconds = dtlsf;
    }

    gpsd_log(&session->context->errout, LOG_DATA,
	     "GREIS: UO, leap_seconds: %d\n", session->context->leap_seconds);

    return 0;
}

/**
 * Handle the message [GT] GPS Time.
 */
static gps_mask_t greis_msg_GT(struct gps_device_t *session,
			       unsigned char *buf, size_t len)
{
    uint32_t tow;	     /* Time of week [ms] */
    uint16_t wn;	     /* GPS week number (modulo 1024) [dimensionless] */

    if (len < 7) {
	gpsd_log(&session->context->errout, LOG_WARN,
		 "GREIS: GT bad len %zu\n", len);
	return 0;
    }

    if (!session->driver.greis.seen_uo) {
	gpsd_log(&session->context->errout, LOG_WARN,
		 "GREIS: can't use GT until after UO has supplied leap second data\n");
	return 0;
    }

    tow = getleu32(buf, 0);
    wn = getleu16(buf, 4);

    session->newdata.time = gpsd_gpstime_resolve(session, wn, tow / 1000.0);

    gpsd_log(&session->context->errout, LOG_DATA,
	     "GREIS: GT, tow: %u, wn: %u, time: %.2f\n", tow, wn,
	     session->newdata.time);

    return TIME_SET | NTPTIME_IS | ONLINE_SET;
}

/**
 * Handle the message [PV] Cartesian Position and Velocity.
 */
static gps_mask_t greis_msg_PV(struct gps_device_t *session,
			       unsigned char *buf, size_t len)
{
    double x, y, z;	    /* Cartesian coordinates [m] */
    float p_sigma;	    /* Position spherical error probability (SEP) [m] */
    float vx, vy, vz;	    /* Cartesian velocities [m/s] */
    float v_sigma;	    /* Velocity SEP [m/s] */
    uint8_t solution_type;

    if (len < 46) {
	gpsd_log(&session->context->errout, LOG_WARN,
		 "GREIS: PV bad len %zu\n", len);
	return 0;
    }

    x = getled64((char *)buf, 0);
    y = getled64((char *)buf, 8);
    z = getled64((char *)buf, 16);
    p_sigma = getlef32((char *)buf, 24);
    vx = getlef32((char *)buf, 28);
    vy = getlef32((char *)buf, 32);
    vz = getlef32((char *)buf, 36);
    v_sigma = getlef32((char *)buf, 40);
    solution_type = getub(buf, 44);

    session->newdata.ecef.x = x;
    session->newdata.ecef.y = y;
    session->newdata.ecef.z = z;
    session->newdata.ecef.pAcc = p_sigma;
    session->newdata.ecef.vx = vx;
    session->newdata.ecef.vy = vy;
    session->newdata.ecef.vz = vz;
    session->newdata.ecef.vAcc = v_sigma;
    ecef_to_wgs84fix(&session->newdata, &session->gpsdata.separation,
		     x, y, z, vx, vy, vz);

    /* GREIS Reference Guide 3.4.2 "General Notes" part "Solution Types" */
    if (solution_type > 0 && solution_type < 5) {
	session->newdata.mode = MODE_3D;
	if (solution_type > 1)
	    session->gpsdata.status = STATUS_DGPS_FIX;
	else
	    session->gpsdata.status = STATUS_FIX;
    }

    gpsd_log(&session->context->errout, LOG_DATA,
	     "GREIS: PV, ECEF x=%.2f y=%.2f z=%.2f pAcc=%.2f\n",
	     session->newdata.ecef.x,
	     session->newdata.ecef.y,
	     session->newdata.ecef.z,
	     session->newdata.ecef.pAcc);

    gpsd_log(&session->context->errout, LOG_DATA,
	     "GREIS: PV, ECEF vx=%.2f vy=%.2f vz=%.2f vAcc=%.2f\n",
	     session->newdata.ecef.vx,
	     session->newdata.ecef.vy,
	     session->newdata.ecef.vz,
	     session->newdata.ecef.vAcc);

    gpsd_log(&session->context->errout, LOG_DATA,
	     "GREIS: PV, lat: %.2f, lon: %.2f, alt: %.2f, solution_type: %d\n",
	     session->newdata.latitude,
	     session->newdata.longitude, session->newdata.altitude,
	     solution_type);

    return LATLON_SET | ALTITUDE_SET | CLIMB_SET | TRACK_SET | SPEED_SET |
	MODE_SET | STATUS_SET | ECEF_SET | VECEF_SET;
}

/**
 * Handle the message [SG] Position and Velocity RMS Errors.
 */
static gps_mask_t greis_msg_SG(struct gps_device_t *session,
			       unsigned char *buf, size_t len)
{
    float hpos;			/* Horizontal position RMS error [m] */
    float vpos;			/* Vertical position RMS error [m] */
    float hvel;			/* Horizontal velocity RMS error [m/s] */
    float vvel;			/* Vertical velocity RMS error [m/s] */

    if (len < 18) {
	gpsd_log(&session->context->errout, LOG_WARN,
		 "GREIS: SG bad len %zu\n", len);
	return 0;
    }

    hpos = getlef32((char *)buf, 0);
    vpos = getlef32((char *)buf, 4);
    hvel = getlef32((char *)buf, 8);
    vvel = getlef32((char *)buf, 12);

    /*
     * All errors are RMS which can be approximated as 1 sigma, so we can just
     * multiply to get the length used for GPSD confidence level.
     *
     * Make the simplifying assumption that error is the same for latitude and
     * longitude, since GREIS does not provide those as precomputed components.
     * A future improvement might be to compute the individual errors from the
     * position covariance matrix, but only if this proves insufficient.
     */
    session->newdata.epx = session->newdata.epy =
	hpos * (1 / sqrt(2)) * GPSD_CONFIDENCE;
    session->newdata.epv = vpos * GPSD_CONFIDENCE;
    session->newdata.eps = hvel * GPSD_CONFIDENCE;
    session->newdata.epc = vvel * GPSD_CONFIDENCE;

    gpsd_log(&session->context->errout, LOG_DATA,
	     "GREIS: SG, epx: %.2f, epy: %.2f, eps: %.2f, epc: %.2f\n",
	     session->newdata.epx, session->newdata.epy,
	     session->newdata.eps, session->newdata.epc);

    return HERR_SET | VERR_SET | SPEEDERR_SET | CLIMBERR_SET;
}

/**
 * Handle the message [DP] Dilution of Precision.
 * Note that fill_dop() will handle the unset dops later.
 */
static gps_mask_t greis_msg_DP(struct gps_device_t *session,
			       unsigned char *buf, size_t len)
{
    if (len < 18) {
	gpsd_log(&session->context->errout, LOG_WARN,
		 "GREIS: DP bad len %zu\n", len);
	return 0;
    }

    /* clear so that computed DOPs get recomputed. */
    gps_clear_dop(&session->gpsdata.dop);

    session->gpsdata.dop.hdop = getlef32((char *)buf, 0);
    session->gpsdata.dop.vdop = getlef32((char *)buf, 4);
    session->gpsdata.dop.tdop = getlef32((char *)buf, 8);

    session->gpsdata.dop.pdop = sqrt(pow(session->gpsdata.dop.hdop, 2) +
				     pow(session->gpsdata.dop.vdop, 2));

    gpsd_log(&session->context->errout, LOG_DATA,
	     "GREIS: DP, hdop: %.2f, vdop: %.2f, tdop: %.2f, pdop: %.2f\n",
	     session->gpsdata.dop.hdop, session->gpsdata.dop.vdop,
	     session->gpsdata.dop.tdop, session->gpsdata.dop.pdop);

    return DOP_SET;
}

/**
 * Handle the message [SI] Satellite Indices.
 *
 * This message tells us how many satellites are seen and contains their
 * Universal Satellite Identifier (USI).
 */
static gps_mask_t greis_msg_SI(struct gps_device_t *session,
			       unsigned char *buf, size_t len)
{
    int i;

    if (len < 1) {
	gpsd_log(&session->context->errout, LOG_WARN,
		 "GREIS: SI bad len %zu\n", len);
	return 0;
    }

    gpsd_zero_satellites(&session->gpsdata);
    /* FIXME: check against MAXCHANNELS? */
    session->gpsdata.satellites_visible = len - 1;
    for (i = 0; i < session->gpsdata.satellites_visible; i++) {
	/* This isn't really PRN, this is USI.  Convert it. */
        unsigned short PRN = getub(buf, i);
	session->gpsdata.skyview[i].PRN = PRN;

        /* fit into gnssid:svid */
	if (0 == PRN) {
            /* skip 0 PRN */
	    continue;
        } else if ((1 <= PRN) && (37 >= PRN)) {
            /* GPS */
            session->gpsdata.skyview[i].gnssid = 0;
            session->gpsdata.skyview[i].svid = PRN;
        } else if ((38 <= PRN) && (69 >= PRN)) {
            /* GLONASS */
            session->gpsdata.skyview[i].gnssid = 6;
            session->gpsdata.skyview[i].svid = PRN - 37;
        } else if (70 == PRN) {
            /* GLONASS, again */
            session->gpsdata.skyview[i].gnssid = 6;
            session->gpsdata.skyview[i].svid = 255;
        } else if ((71 <= PRN) && (119 >= PRN)) {
            /* Galileo */
            session->gpsdata.skyview[i].gnssid = 2;
            session->gpsdata.skyview[i].svid = PRN - 70;
        } else if ((120 <= PRN) && (142 >= PRN)) {
            /* SBAS */
            session->gpsdata.skyview[i].gnssid = 1;
            session->gpsdata.skyview[i].svid = PRN - 119;
        } else if ((193 <= PRN) && (197 >= PRN)) {
            /* QZSS */
            session->gpsdata.skyview[i].gnssid = 5;
            session->gpsdata.skyview[i].svid = PRN - 192;
        } else if ((211 <= PRN) && (247 >= PRN)) {
            /* BeiDou */
            session->gpsdata.skyview[i].gnssid = 3;
            session->gpsdata.skyview[i].svid = PRN - 210;
        }
    }

    session->driver.greis.seen_si = true;
    gpsd_log(&session->context->errout, LOG_DATA,
	     "GREIS: SI, satellites_visible: %d\n",
	     session->gpsdata.satellites_visible);

    return 0;
}

/**
 * Handle the message [EL] Satellite Elevations.
 */
static gps_mask_t greis_msg_EL(struct gps_device_t *session,
			       unsigned char *buf, size_t len)
{
    int i;

    if (!session->driver.greis.seen_si) {
	gpsd_log(&session->context->errout, LOG_WARN,
		 "GREIS: can't use EL until after SI provides indices\n");
	return 0;
    }

    /* check against number of satellites + checksum */
    if (len < session->gpsdata.satellites_visible + 1U) {
	gpsd_log(&session->context->errout, LOG_WARN,
		 "GREIS: EL bad len %zu, needed at least %d\n", len,
		 session->gpsdata.satellites_visible + 1);
	return 0;
    }

    for (i = 0; i < session->gpsdata.satellites_visible; i++) {
        short elevation;

        /* GREIS elevation is -90 to 90 degrees */
        /* GREIS uses 127 for n/a */
        /* gpsd uses -91 for n/a, so adjust acordingly */
	elevation = getub(buf, i);
        if ((-90 > elevation) || (90 < elevation)) elevation = -91;
	session->gpsdata.skyview[i].elevation = elevation;
    }

    session->driver.greis.seen_el = true;
    gpsd_log(&session->context->errout, LOG_DATA, "GREIS: EL\n");

    return 0;
}

/**
 * Handle the message [AZ] Satellite Azimuths.
 */
static gps_mask_t greis_msg_AZ(struct gps_device_t *session,
			       unsigned char *buf, size_t len)
{
    int i;

    if (!session->driver.greis.seen_si) {
	gpsd_log(&session->context->errout, LOG_WARN,
		 "GREIS: can't use AZ until after SI provides indices\n");
	return 0;
    }

    /* check against number of satellites + checksum */
    if (len < session->gpsdata.satellites_visible + 1U) {
	gpsd_log(&session->context->errout, LOG_WARN,
		 "GREIS: AZ bad len %zu, needed at least %d\n", len,
		 session->gpsdata.satellites_visible + 1);
	return 0;
    }

    for (i = 0; i < session->gpsdata.satellites_visible; i++) {
        short azimuth;

        /* GREIS azimuth is 0 to 180, multiply by 2 for 0 to 360 */
        /* GREIS uses 255 for n/a */
        /* gpsd azimuth is 0 to 359, so adjust acordingly */
	azimuth = getub(buf, i) * 2;
        if (360 == azimuth) azimuth = 0;
        else if (360 < azimuth) azimuth = -1;
	session->gpsdata.skyview[i].azimuth = azimuth;
    }

    session->driver.greis.seen_az = true;
    gpsd_log(&session->context->errout, LOG_DATA, "GREIS: AZ\n");

    return 0;
}

/**
 * Handle the message [EC] SNR (CA/L1).
 * EC really outputs CNR, but what gpsd refers to as SNR _is_ CNR.
 */
static gps_mask_t greis_msg_EC(struct gps_device_t *session,
			       unsigned char *buf, size_t len)
{
    int i;

    if (!session->driver.greis.seen_si) {
	gpsd_log(&session->context->errout, LOG_WARN,
		 "GREIS: can't use EC until after SI provides indices\n");
	return 0;
    }

    /* check against number of satellites + checksum */
    if (len < session->gpsdata.satellites_visible + 1U) {
	gpsd_log(&session->context->errout, LOG_WARN,
		 "GREIS: EC bad len %zu, needed at least %d\n", len,
		 session->gpsdata.satellites_visible + 1);
	return 0;
    }

    for (i = 0; i < session->gpsdata.satellites_visible; i++)
	session->gpsdata.skyview[i].ss = getub(buf, i);

    session->driver.greis.seen_ec = true;
    gpsd_log(&session->context->errout, LOG_DATA, "GREIS: EC\n");

    return 0;
}

/**
 * Handle the message [SS] Satellite Navigation Status.
 */
static gps_mask_t greis_msg_SS(struct gps_device_t *session,
			       unsigned char *buf, size_t len)
{
    int i;
    int used_count = 0;

    if (!session->driver.greis.seen_si) {
	gpsd_log(&session->context->errout, LOG_WARN,
		 "GREIS: can't use SS until after SI provides indices\n");
	return 0;
    }

    /* check against number of satellites + solution type + checksum */
    if (len < session->gpsdata.satellites_visible + 2U) {
	gpsd_log(&session->context->errout, LOG_WARN,
		 "GREIS: SI bad len %zu, needed at least %d\n", len,
		 session->gpsdata.satellites_visible + 2);
	return 0;
    }

    for (i = 0; i < session->gpsdata.satellites_visible; i++) {
	/*
	 * From the GREIS Reference Guide: "Codes [0...3], [40...62], and
	 * [64...255] indicate that given satellite is used in position
	 * computation and show which measurements are used. The rest of codes
	 * indicate that satellite is not used in position computation and
	 * indicate why this satellite is excluded from position computation."
	 * Refer to Table 3-4 "Satellite Navigation Status" for the specific
	 * code meanings.
	 */
	uint8_t nav_status = getub(buf, i);
	session->gpsdata.skyview[i].used =
	    (nav_status <= 3) ||
	    (nav_status >= 40 && nav_status <= 62) ||
	    (nav_status >= 64);

	if (session->gpsdata.skyview[i].used)
	    used_count++;
    }
    session->gpsdata.satellites_used = used_count;

    gpsd_log(&session->context->errout, LOG_DATA,
	     "GREIS: SS, satellites_used: %d\n",
	     session->gpsdata.satellites_used);

    return used_count ? USED_IS : 0;
}


/**
 * Handle the message [::](ET) Epoch Time.
 * This should be kept as the last message in each epoch.
 */
static gps_mask_t greis_msg_ET(struct gps_device_t *session,
			       unsigned char *buf, size_t len)
{
    uint32_t tod;
    gps_mask_t mask = 0;

    if (len < 5) {
	gpsd_log(&session->context->errout, LOG_WARN,
		 "GREIS: ET bad len %zu\n", len);
	return 0;
    }

    if (!session->driver.greis.seen_rt) {
	gpsd_log(&session->context->errout, LOG_WARN,
		 "GREIS: got ET, but no preceding RT for epoch\n");
	return 0;
    }

    tod = getleu32(buf, 0);
    if (tod != session->driver.greis.rt_tod) {
	gpsd_log(&session->context->errout, LOG_WARN,
		 "GREIS: broken epoch, RT had %lu, but ET has %lu\n",
		 (unsigned long)session->driver.greis.rt_tod,
		 (unsigned long)tod);
	return 0;
    }

    /* Skyview time does not differ from time in GT message */
    session->gpsdata.skyview_time = NAN;

    gpsd_log(&session->context->errout, LOG_DEBUG,
	     "GREIS: ET, seen: az %d, ec %d, el %d, rt %d, si %d, uo %d\n",
	     (int)session->driver.greis.seen_az,
	     (int)session->driver.greis.seen_ec,
	     (int)session->driver.greis.seen_el,
	     (int)session->driver.greis.seen_rt,
	     (int)session->driver.greis.seen_si,
	     (int)session->driver.greis.seen_uo);

    /* Make sure we got the satellite data, then report it. */
    if ((session->driver.greis.seen_az && session->driver.greis.seen_ec &&
	 session->driver.greis.seen_el && session->driver.greis.seen_si)) {
        /* Skyview seen, update it.  Go even if no seen_ss or none visible */
        mask |= SATELLITE_SET;
    } else {
	gpsd_log(&session->context->errout, LOG_WARN,
		 "GREIS: ET: missing satellite details in this epoch\n");
    }

    gpsd_log(&session->context->errout, LOG_DATA, "GREIS: ET, tod: %lu\n",
	     (unsigned long)tod);

    /* This is a good place to poll firmware version if we need it.
     * Waited until now to avoid the startup rush and out of
     * critical time path
     */
    if (0 == strlen(session->subtype)) {
	/* get version */
	(void)greis_write(session, get_ver, sizeof(get_ver) - 1);
    }
    /* The driver waits for ET to send any reports
     * Just REPORT_IS is not enough to trigger sending of reports to clients.
     * STATUS_SET seems best, if no status by now the status is no fix */
    return mask | REPORT_IS | STATUS_SET;
}

struct dispatch_table_entry {
    char id0;
    char id1;
    gps_mask_t (*handler)(struct gps_device_t *, unsigned char *, size_t);
};

static struct dispatch_table_entry dispatch_table[] = {
    {':', ':', greis_msg_ET},
    {'A', 'Z', greis_msg_AZ},
    {'D', 'P', greis_msg_DP},
    {'E', 'C', greis_msg_EC},
    {'E', 'R', greis_msg_ER},
    {'E', 'L', greis_msg_EL},
    {'G', 'T', greis_msg_GT},
    {'P', 'V', greis_msg_PV},
    {'R', 'E', greis_msg_RE},
    {'S', 'G', greis_msg_SG},
    {'S', 'I', greis_msg_SI},
    {'S', 'S', greis_msg_SS},
    {'U', 'O', greis_msg_UO},
    {'~', '~', greis_msg_RT},
};

#define dispatch_table_size (sizeof(dispatch_table) / sizeof(dispatch_table[0]))

/**
 * Parse the data from the device
 */
static gps_mask_t greis_dispatch(struct gps_device_t *session,
				 unsigned char *buf, size_t len)
{
    size_t i;
    char id0, id1;

    if (len == 0)
	return 0;

    /*
     * This is set because the device reliably signals end of cycle.
     * The core library zeroes it just before it calls each driver's
     * packet analyzer.
     */
    session->cycle_end_reliable = true;

    /* Length should have already been checked in packet.c, but just in case */
    if (len < HEADER_LENGTH) {
	gpsd_log(&session->context->errout, LOG_WARN,
		 "GREIS: Packet length %zu shorter than min length\n", len);
	return 0;
    }

    /* we may need to dump the raw packet */
    gpsd_log(&session->context->errout, LOG_RAW,
	     "GREIS: raw packet id '%c%c'\n", buf[0], buf[1]);

    id0 = buf[0];
    id1 = buf[1];
    len -= HEADER_LENGTH;
    buf += HEADER_LENGTH;

    for (i = 0; i < dispatch_table_size; i++) {
	struct dispatch_table_entry *entry = &dispatch_table[i];

	if (id0 == entry->id0 && id1 == entry->id1) {
	    return entry->handler(session, buf, len);
	}
    }

    gpsd_log(&session->context->errout, LOG_WARN,
	     "GREIS: unknown packet id '%c%c' length %zu\n", id0, id1, len);
    return 0;
}

/**********************************************************
 *
 * Externally called routines below here
 *
 **********************************************************/

/**
 * Write data to the device with checksum.
 * Returns number of bytes written on successful write, -1 otherwise.
 */
static ssize_t greis_write(struct gps_device_t *session,
			   const char *msg, size_t msglen)
{
    char checksum_str[3] = {0};
    ssize_t count;

    if (session->context->readonly) {
        /* readonly mode, do not write anything */
        return -1;
    }

    if (NULL == msg) {
	/* We do sometimes write zero length to wake up GPS,
	 * so just test for NULL msg, not zero length message */
	gpsd_log(&session->context->errout, LOG_ERROR,
		 "GREIS: nothing to write\n");
	return -1;
    }

    /* Account for length + checksum marker + checksum + \r + \n + \0 */
    if (msglen + 6 > sizeof(session->msgbuf)) {
	gpsd_log(&session->context->errout, LOG_ERROR,
		 "GREIS: msgbuf is smaller than write length %zu\n", msglen);
	return -1;
    }

    if (msg != NULL)
	memcpy(&session->msgbuf[0], msg, msglen);

    if (msglen == 0) {
	/* This is a dummy write, don't give a checksum. */
	session->msgbuf[0] = '\n';
	session->msgbuflen = 1;
	gpsd_log(&session->context->errout, LOG_PROG,
		 "GREIS: Dummy write\n");
    } else {
	unsigned char checksum;

	session->msgbuflen = msglen;
	session->msgbuf[session->msgbuflen++] = '@'; /* checksum marker */

	/* calculate checksum with @, place at end, and set length to write */
	checksum = greis_checksum((unsigned char *)session->msgbuf,
				  session->msgbuflen);
	(void)snprintf(checksum_str, sizeof(checksum_str), "%02X", checksum);
	session->msgbuf[session->msgbuflen++] = checksum_str[0];
	session->msgbuf[session->msgbuflen++] = checksum_str[1];
	session->msgbuf[session->msgbuflen++] = '\r';
	session->msgbuf[session->msgbuflen++] = '\n';

	gpsd_log(&session->context->errout, LOG_PROG,
		 "GREIS: Writing command '%.*s', checksum: %s\n",
		 (int)msglen, msg, checksum_str);
    }
    session->msgbuf[session->msgbuflen] = '\0';
    count = gpsd_write(session, session->msgbuf, session->msgbuflen);

    if (count != (ssize_t)session->msgbuflen)
	return -1;
    else
	return count;
}

#ifdef CONTROLSEND_ENABLE
/**
 * Write data to the device, doing any required padding or checksumming
 */
static ssize_t greis_control_send(struct gps_device_t *session,
				  char *msg, size_t msglen)
{
    return greis_write(session, msg, msglen);
}
#endif /* CONTROLSEND_ENABLE */

static void greis_event_hook(struct gps_device_t *session, event_t event)
{
    if (session->context->readonly)
	return;

    if (event == event_wakeup) {
	/*
	 * Code to make the device ready to communicate.  Only needed if the
	 * device is in some kind of sleeping state, and only shipped to
	 * RS232C, so that gpsd won't send strings to unidentified USB devices
	 * that might not be GPSes at all.
	 */

	/*
	 * Disable any existing messages, then request vendor for
	 * identification.
	 */
	(void)greis_write(session, disable_messages,
			  sizeof(disable_messages) - 1);
	(void)greis_write(session, get_vendor, sizeof(get_vendor) - 1);
    } else if (event == event_identified || event == event_reactivate) {
	/*
	 * Fires when the first full packet is recognized from a previously
	 * unidentified device OR the device is reactivated after close. The
	 * session.lexer counter is zeroed.
	 *
	 * TODO: If possible, get the software version and store it in
	 * session->subtype.
	 */
	(void)greis_write(session, disable_messages,
			  sizeof(disable_messages) - 1);
	(void)greis_write(session, set_update_rate_4hz,
			  sizeof(set_update_rate_4hz) - 1);
	(void)greis_write(session, enable_messages_4hz,
			  sizeof(enable_messages_4hz) - 1);

	/* Store cycle time (seconds) */
	session->gpsdata.dev.cycle = 0.25;
    } else if (event == event_driver_switch) {
	/*
	 * Fires when the driver on a device is changed *after* it
	 * has been identified.
	 */
    } else if (event == event_deactivate) {
	/*
	 * Fires when the device is deactivated.  Use this to revert
	 * whatever was done at event_identified and event_configure
	 * time.
	 */
	(void)greis_write(session, disable_messages,
			  sizeof(disable_messages) - 1);
    }
}

/**
 * This is the entry point to the driver. When the packet sniffer recognizes
 * a packet for this driver, it calls this method which passes the packet to
 * the binary processor or the nmea processor, depending on the session type.
 */
static gps_mask_t greis_parse_input(struct gps_device_t *session)
{
    if (session->lexer.type == GREIS_PACKET) {
	return greis_dispatch(session, session->lexer.outbuffer,
			      session->lexer.outbuflen);
#ifdef NMEA0183_ENABLE
    } else if (session->lexer.type == NMEA_PACKET) {
	return nmea_parse((char *)session->lexer.outbuffer, session);
#endif /* NMEA0183_ENABLE */
    } else
	return 0;
}

#ifdef RECONFIGURE_ENABLE
/**
 * Set port operating mode, speed, parity, stopbits etc. here.
 * Note: parity is passed as 'N'/'E'/'O', but you should program
 * defensively and allow 0/1/2 as well.
 */
static bool greis_set_speed(struct gps_device_t *session,
			    speed_t speed, char parity, int stopbits)
{
    /* change on current port */
    static const char set_rate[] = "set,/par/cur/term/rate,";
    static const char set_parity[] = "set,/par/cur/term/parity,";
    static const char set_stops[] = "set,/par/cur/term/stops,";
    static const char parity_none[] = "N";
    static const char parity_even[] = "even";
    static const char parity_odd[] = "odd";

    char command[BUFSIZ] = {0};
    const char *selected_parity = NULL;

    switch (parity) {
    case 'N':
    case 0:
	selected_parity = parity_none;
	break;
    case 'E':
    case 1:
	selected_parity = parity_even;
	break;
    case 'O':
    case 2:
	selected_parity = parity_odd;
	break;
    default:
	return false;
    }

    (void)snprintf(command, sizeof(command) - 1, "%s%u && %s%s && %s%d",
	     set_rate, speed, set_parity, selected_parity,
	     set_stops, stopbits);
    return (bool)greis_write(session, command, strlen(command));
}

#if 0
/**
 * TODO: Switch between NMEA and binary mode
 */
static void greis_set_mode(struct gps_device_t *session, int mode)
{
    if (mode == MODE_NMEA) {
	/* send a mode switch control string */
    } else {
	/* send a mode switch control string */
	session->back_to_nmea = false;
    }
}
#endif

#endif /* RECONFIGURE_ENABLE */

#ifdef TIMEHINT_ENABLE
#if 0 /* TODO */
static double greis_time_offset(struct gps_device_t *session)
{
    /*
     * If NTP notification is enabled, the GPS will occasionally NTP
     * its notion of the time. This will lag behind actual time by
     * some amount which has to be determined by observation vs. (say
     * WWVB radio broadcasts) and, furthermore, may differ by baud
     * rate. This method is for computing the NTP fudge factor.  If
     * it's absent, an offset of 0.0 will be assumed, effectively
     * falling back on what's in ntp.conf. When it returns NAN,
     * nothing will be sent to NTP.
     */
    return MAGIC_CONSTANT;
}
#endif
#endif /* TIMEHINT_ENABLE */

/* This is everything we export */
/* *INDENT-OFF* */
const struct gps_type_t driver_greis = {
    /* Full name of type */
    .type_name        = "GREIS",
    /* Associated lexer packet type */
    .packet_type      = GREIS_PACKET,
    /* Driver type flags */
    .flags	      = DRIVER_STICKY,
    /* Response string that identifies device (not active) */
    .trigger          = NULL,
    /* Number of satellite channels supported by the device */
    .channels         = 128,
    /* Startup-time device detector */
    .probe_detect     = NULL,
    /* Packet getter (using default routine) */
    .get_packet       = generic_get,
    /* Parse message packets */
    .parse_packet     = greis_parse_input,
    /* non-perturbing initial query (e.g. for version) */
    .init_query        = NULL,
    /* fire on various lifetime events */
    .event_hook       = greis_event_hook,
#ifdef RECONFIGURE_ENABLE
    /* Speed (baudrate) switch */
    .speed_switcher   = greis_set_speed,
#if 0 /* TODO */
    /* Switch to NMEA mode */
    .mode_switcher    = greis_set_mode,
#endif
    /* Message delivery rate switcher (not active) */
    .rate_switcher    = NULL,
    /* Minimum cycle time of the device.
     * Default is 1/100, but this is tunable using /par/raw/msint . */
    .min_cycle        = 0.01,
#endif /* RECONFIGURE_ENABLE */
#ifdef CONTROLSEND_ENABLE
    /* Control string sender - should provide checksum and headers/trailer */
    .control_send   = greis_control_send,
#endif /* CONTROLSEND_ENABLE */
#ifdef TIMEHINT_ENABLE
#if 0 /* TODO */
    .time_offset     = greis_time_offset,
#endif
#endif /* TIMEHINT_ENABLE */
/* *INDENT-ON* */
};
#endif /* defined(GREIS_ENABLE) && defined(BINARY_ENABLE) */
