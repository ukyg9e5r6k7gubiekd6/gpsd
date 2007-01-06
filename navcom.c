/*
 * Driver for Navcom receivers using propietary NCT messages, a binary protocol.
 *
 * Vendor website: http://www.navcomtech.com/
 * Technical references: http://www.navcomtech.com/support/docs.cfm
 *
 * Tested with two SF-2040G models
 *
 * At this stage, this driver implements the following commands:
 *
 * 0x20: Data Request (tell the unit which responses you want)
 * 0x3f: LED Configuration (controls the front panel LEDs -- for testing)
 * 0x1c: Test Support Block (again, blinks the front panel lights)
 *
 * and it understands the following responses:
 *
 * 0xb1: PVT Block (pos., vel., time., DOPs)
 * 0x86: Channel Status (satellites visible + tracked)
 * 0xae: Identification Block (type of receiver, options available, etc.)
 *
 * FIXME - Position errors theoretically are being reported at the one-sigma level.
 *         However, field tests suggest the values to be more consistent with
 *         two-sigma. Need to clear this up.
 * FIXME - I'm not too sure of the way I have computed the vertical positional error
 *         I have used FOM as a scaling factor for VDOP, thusly VRMS = FOM/HDOP*VDOP
 * TODO - Read 0x83 blocks (Ionosphere and UTC data) for transforming GPS time to UTC
 * TODO - Lots of other things in mind, but the important stuff seems to be there.
 *
 * By Diego Berge. Contact via web form at http://www.nippur.net/survey/xuc/contact
 * (the form is in Catalan, but you'll figure it out)
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <inttypes.h>
#include <stdio.h>
#include "gpsd_config.h"
#include "gpsd.h"

#if defined(NAVCOM_ENABLE) && defined(BINARY_ENABLE)
#define LITTLE_ENDIAN_PROTOCOL
#include "bits.h"

/* Have data which is 24 bits long */
#define getsl24(buf,off)  ((int32_t)((u_int32_t)getub((buf), (off)+2)<<24 | (u_int32_t)getub((buf), (off)+1)<<16 | (u_int32_t)getub((buf), (off))<<8)>>8)
#define getul24(buf,off) ((u_int32_t)((u_int32_t)getub((buf), (off)+2)<<24 | (u_int32_t)getub((buf), (off)+1)<<16 | (u_int32_t)getub((buf), (off))<<8)>>8)

#define NAVCOM_CHANNELS	26

static u_int8_t checksum(unsigned char *buf, size_t len)
{
    size_t n;
    u_int8_t csum = 0x00;
    for(n = 0; n < len; n++)
      csum ^= buf[n];
    return csum;
}

static bool navcom_send_cmd(struct gps_device_t *session, unsigned char *cmd, size_t len)
{
    gpsd_report(LOG_RAW, "Sending Navcom command 0x%02x: %s\n",
                cmd[3], gpsd_hexdump(cmd, len));
    return gpsd_write(session, cmd, len);
}

/* Data Request */
static void navcom_cmd_0x20(struct gps_device_t *session, u_int8_t block_id, u_int16_t rate)
{
    unsigned char msg[14];
    putbyte(msg, 0, 0x02);
    putbyte(msg, 1, 0x99);
    putbyte(msg, 2, 0x66);
    putbyte(msg, 3, 0x20);	/* Cmd ID */
    putword(msg, 4, 0x000a);	/* Length */
    putbyte(msg, 6, 0x00);	/* Action */
    putbyte(msg, 7, 0x00);      /* Count of blocks */
    putbyte(msg, 8, block_id);	/* Data Block ID */
    putbyte(msg, 9, 0x02);	/* Logical Ports */
    putword(msg, 10, rate);	/* Data rate */
    putbyte(msg, 12, checksum(msg+3, 9));
    putbyte(msg, 13, 0x03);
    navcom_send_cmd(session, msg, 14);
}

/* Changes the LED settings in the receiver */
static void navcom_cmd_0x3f(struct gps_device_t *session)
{
    unsigned char msg[12];
    putbyte(msg, 0, 0x02);
    putbyte(msg, 1, 0x99);
    putbyte(msg, 2, 0x66);
    putbyte(msg, 3, 0x3f);	/* Cmd ID */
    putword(msg, 4, 0x0008);
    putbyte(msg, 6, 0x01);	/* Action */
    putbyte(msg, 7, 0x00);	/* Reserved */
    putbyte(msg, 8, 0x02);	/* Link LED setting */
    putbyte(msg, 9, 0x0a);	/* Battery LED setting */
    putbyte(msg, 10, checksum(msg+3, 7));
    putbyte(msg, 11, 0x03);
    navcom_send_cmd(session, msg, 12);
}

/* Test Support Block - Blinks the LEDs */
static void navcom_cmd_0x1c(struct gps_device_t *session, u_int8_t mode)
{
    unsigned char msg[12];
    putbyte(msg, 0, 0x02);
    putbyte(msg, 1, 0x99);
    putbyte(msg, 2, 0x66);
    putbyte(msg, 3, 0x1c);	/* Cmd ID */
    putword(msg, 4, 0x0008);
    putbyte(msg, 6, 0x00);
    putbyte(msg, 7, mode);	/* 0x01 or 0x02 */
    putbyte(msg, 8, mode);
    putbyte(msg, 9, 0x00);
    putbyte(msg, 10, checksum(msg+3, 7));
    putbyte(msg, 11, 0x03);
    navcom_send_cmd(session, msg, 12);
}


static void navcom_probe_subtype(struct gps_device_t *session, unsigned int seq)
{
    /* Request the following messages: 0x83, 0x84, 0x86, 0xb0, 0xb1 */
    if (!seq) {
        navcom_cmd_0x3f(session);
	navcom_cmd_0x1c(session, 0x02);
        navcom_cmd_0x20(session, 0xae, 0x0000); /* Identification Block */
        navcom_cmd_0x20(session, 0xb1, 0x000a); /* PVT Block */
        navcom_cmd_0x20(session, 0xb0, 0x000a); /* Raw Meas Data Block */
        navcom_cmd_0x20(session, 0x86, 0x000a); /* Channel Status */
    }
#ifdef __UNUSED__
    if ((seq % 20) == 0)
	navcom_cmd_0x1c(session, 0x01);
    else if ((seq % 10) == 0)
	navcom_cmd_0x1c(session, 0x02);
#endif /* __UNUSED__ */
}

static void navcom_ping(struct gps_device_t *session)
{
    navcom_cmd_0x20(session, 0x06, 0x012c); /* Acknowledgment Block */
    navcom_cmd_0x20(session, 0x86, 0x000a); /* Channel Status */
}

/* PVT Block */
static gps_mask_t handle_0xb1(struct gps_device_t *session)
{
    unsigned char *buf = session->packet.outbuffer + 3;
    uint16_t week;
    uint32_t tow;
    uint32_t sats_used;
    int32_t lat, lon;
    /* Resolution of lat/lon values (2^-11) */
    #define LL_RES (0.00048828125)
    uint8_t lat_fraction, lon_fraction;
    /* Resolution of lat/lon fractions (2^-15) */
    #define LL_FRAC_RES (0.000030517578125)
    uint8_t nav_mode;
    int32_t ellips_height, altitude;
    /* Resolution of height and altitude values (2.0^-10) */
    #define EL_RES (0.0009765625)
    long vel_north, vel_east, vel_up;
    /* Resolution of velocity values (2.0^-10) */
    #define VEL_RES (0.0009765625)
    double track;
    uint8_t fom, gdop, pdop, hdop, vdop, tdop;
    /* This value means "undefined" */
    #define DOP_UNDEFINED (255)
    
#ifdef __UNUSED__
    uint16_t max_dgps_age;
    uint8_t dgps_conf;
    uint8_t ext_nav_mode;
    int16_t ant_height_adj;
    int32_t set_delta_north, set_delta_east, set_delta_up;
    uint8_t nav_failure_code;
#endif /* __UNUSED__ */

    /* FIXME - Need to read block 0x86 to get up-to-date leap seconds */
    /* Timestamp */
    week = getuw(buf, 3);
    tow = getul(buf, 5);
    session->gpsdata.fix.time = session->gpsdata.sentence_time = gpstime_to_unix(week, tow/1000.0) - session->context->leap_seconds;
    gpsd_report(LOG_RAW+1, "Navcom packet type 0xb1 - week = %d tow=%f unixtime=%f\n",
                week, tow/1000.0, session->gpsdata.fix.time);

    /* Satellites used */
    unsigned char n;
    sats_used = getul(buf, 9);
    session->gpsdata.satellites_used = 0;
    for(n = 0; n < 31; n++) {
    	if (sats_used & (0x01 << n))
    	  session->gpsdata.used[session->gpsdata.satellites_used++] = n+1;
    }

    /* Get latitude, longitude */
    lat = getsl(buf, 13);
    lon = getsl(buf, 17);
    lat_fraction = (getub(buf, 21) >> 4);
    lon_fraction = (getub(buf, 21) & 0x0f);

    session->gpsdata.fix.latitude = (double)(lat * LL_RES + lat_fraction * LL_FRAC_RES ) / 3600;
    session->gpsdata.fix.longitude = (double)(lon * LL_RES + lon_fraction * LL_FRAC_RES ) / 3600;
    gpsd_report(LOG_RAW, "Navcom packet type 0xb1 - lat = %f (%d, %08x), lon = %f (%d, %08x)\n",
		session->gpsdata.fix.latitude, lat, lat, session->gpsdata.fix.longitude, lon, lon);

    /* Nav mode */
    nav_mode = getub(buf, 22);
    if ((nav_mode & 0xc0) == 0xc0) {
	    session->gpsdata.fix.mode = MODE_3D;
    	if (nav_mode & 0x03)
    	    session->gpsdata.status = STATUS_DGPS_FIX;
    	else
    	    session->gpsdata.status = STATUS_FIX;
    }
    else if (nav_mode & 0x80) {
	    session->gpsdata.fix.mode = MODE_2D;
    	if (nav_mode & 0x03)
    	    session->gpsdata.status = STATUS_DGPS_FIX;
    	else
    	    session->gpsdata.status = STATUS_FIX;
    }
    else {
	    session->gpsdata.fix.mode = MODE_NO_FIX;
	session->gpsdata.status = STATUS_NO_FIX;
    }
    
    /* Height Data */
    ellips_height = getsl(buf, 23);
    altitude = getsl(buf, 27);

    session->gpsdata.fix.altitude = (double)(altitude * EL_RES);
    session->gpsdata.separation = (double)(ellips_height - altitude)*EL_RES;

    /* Speed Data */
    vel_north = getsl24(buf, 31);
    vel_east = getsl24(buf, 34);
    /* vel_up = getsl24(buf, 37); */
    vel_up = getsl24(buf, 37);
    
    track = atan2(vel_east, vel_north);
    if (track < 0)
    	track += 2 * PI;
    session->gpsdata.fix.track = track * RAD_2_DEG;
    /* FIXME Confirm what the tech spec means by (2^-10 m/s) +/- 8192m/s */
    session->gpsdata.fix.speed = sqrt(pow(vel_east,2) + pow(vel_north,2)) * VEL_RES;
    session->gpsdata.fix.climb = vel_up * VEL_RES;
    gpsd_report(LOG_RAW+1, "Navcom packet type 0xb1 - velocities - track = %f, speed = %f, climb = %f\n",
		session->gpsdata.fix.track,
		session->gpsdata.fix.speed,
		session->gpsdata.fix.climb);

    /* Quality indicators */
    fom  = getub(buf, 40);
    gdop = getub(buf, 41);
    pdop = getub(buf, 42);
    hdop = getub(buf, 43);
    vdop = getub(buf, 44);
    tdop = getub(buf, 45);
    
    session->gpsdata.fix.eph = fom/100.0;
    /* FIXME This cannot possibly be right */
    /* I cannot find where to get VRMS from in the Navcom output, though */
    session->gpsdata.fix.epv = (double)fom/(double)hdop*(double)vdop/100.0;
    
    if (gdop == DOP_UNDEFINED)
        session->gpsdata.gdop = NAN;
    else
        session->gpsdata.gdop = gdop/10.0;
    if (pdop == DOP_UNDEFINED)
        session->gpsdata.pdop = NAN;
    else
        session->gpsdata.pdop = pdop/10.0;
    if (hdop == DOP_UNDEFINED)
        session->gpsdata.hdop = NAN;
    else
        session->gpsdata.hdop = hdop/10.0;
    if (vdop == DOP_UNDEFINED)
        session->gpsdata.vdop = NAN;
    else
        session->gpsdata.vdop = vdop/10.0;
    if (tdop == DOP_UNDEFINED)
        session->gpsdata.tdop = NAN;
    else
        session->gpsdata.tdop = tdop/10.0;
    
    gpsd_report(LOG_RAW+1, "hrms = %f, gdop = %f, pdop = %f, hdop = %f, vdop = %f, tdop = %f\n",
		session->gpsdata.fix.eph, session->gpsdata.gdop, session->gpsdata.pdop,
		session->gpsdata.hdop, session->gpsdata.vdop, session->gpsdata.tdop);
    
    return LATLON_SET | ALTITUDE_SET | CLIMB_SET | SPEED_SET | TRACK_SET | TIME_SET
        | STATUS_SET | MODE_SET | USED_SET | HERR_SET | VERR_SET | DOP_SET | CYCLE_START_SET;
}

/* Channel Status */
static gps_mask_t handle_0x86(struct gps_device_t *session)
{
    size_t n, i;
    u_int8_t prn, tracking_status, ele, ca_snr, p2_snr, channel;
    u_int16_t azm;
    unsigned char *buf = session->packet.outbuffer + 3;
    size_t msg_len = getuw(buf, 1);
    u_int16_t week = getuw(buf, 3);
    u_int32_t tow = getul(buf, 5);
    u_int16_t status = getuw(buf, 10);
    u_int8_t sats_visible = getub(buf, 12);
    u_int8_t sats_tracked = getub(buf, 13);
    u_int8_t sats_used = getub(buf, 14);
    u_int8_t pdop = getub(buf, 15);

    /* Timestamp and PDOP */
    session-> gpsdata.sentence_time = gpstime_to_unix(week, tow/1000.0) - session->context->leap_seconds;
    session->gpsdata.pdop = pdop / 10.0;

    /* Satellite count */
    session->gpsdata.satellites = sats_visible;
    session->gpsdata.satellites_used = sats_used;

    /* Fix mode */
    switch(status & 0x05)
    {
    case 0x05:
	session->gpsdata.status = STATUS_DGPS_FIX;
	break;
    case 0x01:
	session->gpsdata.status = STATUS_FIX;
	break;
    default:
	session->gpsdata.status = STATUS_NO_FIX;
    }

   gpsd_report(LOG_RAW, "Navcom packet type 0x86 - satellites: visible = %u, tracked = %u, used = %u\n",
	       sats_visible, sats_tracked, sats_used);

    /* Satellite details */
    i = 0;
    for(n = 17; n < msg_len; n += 14) {
	if(i >= MAXCHANNELS) {
            gpsd_report(LOG_ERROR, "internal error - too many satellites!\n");
            gpsd_zero_satellites(&session->gpsdata);
            return ERROR_SET;
	}
        prn = getub(buf, n);
	tracking_status = getub(buf, n+1);
	ele = getub(buf, n+5);
	azm =  getuw(buf, n+6);
	ca_snr = getub(buf, n+8);
	p2_snr = getub(buf, n+10);
	channel = getub(buf, n+13);
	if (tracking_status != 0x00) {
	    session->gpsdata.PRN[i] = (int)prn;
	    session->gpsdata.elevation[i] = ele;
	    session->gpsdata.azimuth[i] = azm;
	    session->gpsdata.ss[i++] = (p2_snr ? p2_snr : ca_snr) / 4.0;
	    gpsd_report(LOG_RAW+1, "prn = %02x, ele = %02x, azm = %04x, ss = %d\n",
                        prn, ele, azm, session->gpsdata.ss);
	}
    }

    return PDOP_SET | SATELLITE_SET | STATUS_SET;
}

/* Identification Block */
static gps_mask_t handle_0xae(struct gps_device_t *session)
{
    char *engconfstr, *asicstr;
    unsigned char *buf = session->packet.outbuffer + 3;
    size_t    msg_len = getuw(buf, 1);
    u_int8_t  engconf = getub(buf, 3);
    u_int8_t  asic    = getub(buf, 4);
    u_int16_t softver = getuw(buf, 5);
    u_int8_t  vermaj  = getub(buf, 7);
    u_int8_t  vermin  = getub(buf, 8);
    u_int32_t dcn     = getul24(buf, 9);
    u_int16_t dcser   = getuw(buf, 12);
    u_int8_t  dcclass = getub(buf, 14);
    u_int32_t rfcn    = getul24(buf, 15);
    u_int16_t rfcser  = getuw(buf, 18);
    u_int8_t  rfcclass = getub(buf, 20);
    u_int8_t  softtm[16] = "";
    u_int8_t  bootstr[16] = "";
    u_int16_t iopsoftver = 0x0000;
    u_int8_t  iopvermaj  = 0x00;
    u_int8_t  iopvermin  = 0x00;
    u_int8_t  ioptm[16] = "";
    u_int8_t  picver = 0x00;
    u_int8_t  slsbn = 0x00;
    u_int8_t  iopsbn = 0x00;

    memcpy(softtm, &buf[21], 16);
    memcpy(bootstr, &buf[37], 16);
    if (msg_len == 0x0037) { /* No IOP */
        slsbn = getub(buf, 53);
    } else { /* IOP Present */
        iopsoftver = getuw(buf, 53);
        iopvermaj  = getub(buf, 55);
        iopvermin  = getub(buf, 56);
        memcpy(ioptm, &buf[57], 16);
        picver     = getub(buf, 73);
        slsbn      = getub(buf, 74);
        iopsbn     = getub(buf, 75);
    }

    switch(engconf)
    {
    case 0x00:
        engconfstr = "Unknown/Undefined";
        break;
    case 0x01:
        engconfstr = "NCT 2000 S";
        break;
    case 0x02:
        engconfstr = "NCT 2000 D";
        break;
    case 0x03:
        engconfstr = "Startfire Single";
        break;
    case 0x04:
        engconfstr = "Starfire Dual";
        break;
    case 0x05:
        engconfstr = "Pole Mount RTK (Internal Radio Found)";
        break;
    case 0x06:
        engconfstr = "Pole Mount GIS (LBM Available)";
        break;
    case 0x07:
        engconfstr = "Black Box RTK (Internal Radio Found)";
        break;
    case 0x08:
        engconfstr = "Black Box GIS (LBM Available)";
        break;
    case 0x80:
        engconfstr = "R100";
        break;
    case 0x81:
        engconfstr = "R200";
        break;
    case 0x82:
        engconfstr = "R210";
        break;
    case 0x83:
        engconfstr = "R300";
        break;
    case 0x84:
        engconfstr = "R310";
        break;
    default:
        engconfstr = "?";
    }

    switch(asic)
    {
    case 0x01:
        asicstr = "A-ASIC (C/A, L1)";
        break;
    case 0x02:
        asicstr = "B-ASIC (C/A, P1, P2, L1, L2)";
        break;
    case 0x03:
        asicstr = "C-ASIC (C/A, P1, P2, L1, L2, WAAS)";
        break;
    case 0x04:
        asicstr = "M-ASIC (C/A, L1, WAAS)";
        break;
    default:
        asicstr = "?";
    }

    gpsd_report(LOG_RAW, "Navcom ID Data: "
                "Engine type: %s (%x) - "
                "ASIC type: %s (%x) - "
                "Soft. Ver: %u - "
                "Ver. Major: %u - "
                "Ver. Minor: %u - "
                "Digital Card Number: %lu - "
                "Card Serial Number: %u - "
                "Card Class: %u - "
                "RF Card Number: %lu - "
                "RF Card Serial Number: %u - "
                "RF Card Class: %u - "
                "Software Time Mark: %s - "
                "Boot String: %s - "
                "Starlight Software Build Number: %u\n",
                engconfstr, engconf, asicstr, asic, softver, vermaj, vermin,
                dcn, dcser, dcclass, rfcn, rfcser, rfcclass,
                softtm, bootstr, slsbn);
    if(iopsoftver) {
        gpsd_report(LOG_RAW, "Navcom ID Data (IOP): "
                    "IOP Soft. Ver: %u - "
                    "Major: %u - "
                    "Minor: %u - "
                    "IOP Time Mark: %s - "
                    "PIC Version: %u - "
                    "IOP Software Build Number: %u\n",
                    iopsoftver, iopvermaj, iopvermin, ioptm, picver, iopsbn);
    }

    snprintf(session->subtype, 64, "%s %s SBN: %u",
             engconfstr, asicstr, slsbn);

    return DEVICEID_SET;

}

/*@ +charint @*/
gps_mask_t navcom_parse(struct gps_device_t *session, unsigned char *buf, size_t len)
{
    unsigned char cmd_id;
    unsigned char *payload;
    unsigned int  msg_len;

    if (len == 0)
	return 0;

    cmd_id = getub(buf, 3);
    payload = &buf[6];
    msg_len = getuw(buf, 4);
   
    /*@ -usedef -compdef @*/
    gpsd_report(LOG_RAW, "Navcom packet type 0x%02x, length %d: %s\n",
        cmd_id, msg_len, gpsd_hexdump(buf, len));
    /*@ +usedef +compdef @*/

    (void)snprintf(session->gpsdata.tag, sizeof(session->gpsdata.tag),
		   "0x%02x",cmd_id);

    switch (cmd_id)
    {
    case 0xb1:
	return handle_0xb1(session);
    case 0x86:
	return handle_0x86(session);
    case 0xae:
        return handle_0xae(session);
    default:
	gpsd_report(LOG_IO, "Unknown or unimplemented Navcom packet id 0x%02x, length %d\n",
		    cmd_id, msg_len);
	return 0;
    }
}
/*@ -charint @*/

static gps_mask_t navcom_parse_input(struct gps_device_t *session)
{
    gps_mask_t st;

    if (session->packet.type == NAVCOM_PACKET){
	st = navcom_parse(session, session->packet.outbuffer, session->packet.outbuflen);
	session->gpsdata.driver_mode = 1;  /* binary */
	return st;
#ifdef NMEA_ENABLE
    } else if (session->packet.type == NMEA_PACKET) {
	st = nmea_parse((char *)session->packet.outbuffer, session);
	session->gpsdata.driver_mode = 0;  /* NMEA */
	return st;
#endif /* NMEA_ENABLE */
    } else
	return 0;
}


/* this is everything we export */
struct gps_type_t navcom_binary =
{
    .typename       = "Navcom binary",  	/* full name of type */
    .trigger        = "\x02\x99\x66",
    .channels       = NAVCOM_CHANNELS,		/* 12 L1 + 12 L2 + 2 L-Band */
    .probe_wakeup   = navcom_ping,		/* wakeup to be done before hunt */
    .probe_detect   = NULL,			/* no probe */
    .probe_subtype  = navcom_probe_subtype,	/* subtype probing */
#ifdef ALLOW_RECONFIGURE
    .configurator   = NULL,			/* no reconfigure */
#endif /* ALLOW_RECONFIGURE */
    .get_packet     = generic_get,		/* use generic one */
    .parse_packet   = navcom_parse_input,	/* parse message packets */
    .rtcm_writer    = pass_rtcm,		/* send RTCM data straight */
    .speed_switcher = NULL,			/* we can change baud rates */
    .mode_switcher  = NULL,			/* there is a mode switcher */
    .rate_switcher  = NULL,			/* no sample-rate switcher */
    .cycle_chars    = -1,			/* ignore, no rate switch */
#ifdef ALLOW_RECONFIGURE
    .revert         = NULL,			/* no reversion code */
#endif /* ALLOW_RECONFIGURE */
    .wrapup         = NULL,			/* ignore, no wrapup */
    .cycle          = 1,			/* updates every second */
};

#endif /* defined(NAVCOM_ENABLE) && defined(BINARY_ENABLE) */
