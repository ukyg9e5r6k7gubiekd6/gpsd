/****************************************************************************

NAME
   gpsd_json.c - move data between in-core and JSON structures

DESCRIPTION
   These are functions (used only by the daemon) to dump the contents
of various core data structures in JSON.

PERMISSIONS
  Written by Eric S. Raymond, 2009
  This file is Copyright (c) 2010 by the GPSD project
  BSD terms apply: see the file COPYING in the distribution root for details.

***************************************************************************/

#include <stdio.h>
#include <math.h>
#include <assert.h>
#include <string.h>
#include <ctype.h>

#include "gpsd.h"
#include "bits.h"
#include "strfuncs.h"

#ifdef SOCKET_EXPORT_ENABLE
#include "gps_json.h"
#include "revision.h"

/* *INDENT-OFF* */
#define JSON_BOOL(x)	((x)?"true":"false")

/*
 * Manifest names for the gnss_type enum - must be kept synced with it.
 * Also, masks so we can tell what packet types correspond to each class.
 */
/* the map of device class names */
struct classmap_t {
    char	*name;
    int		typemask;
    int		packetmask;
};
#define CLASSMAP_NITEMS	5

struct classmap_t classmap[CLASSMAP_NITEMS] = {
    /* name	typemask	packetmask */
    {"ANY",	0,       	0},
    {"GPS",	SEEN_GPS, 	GPS_TYPEMASK},
    {"RTCM2",	SEEN_RTCM2,	PACKET_TYPEMASK(RTCM2_PACKET)},
    {"RTCM3",	SEEN_RTCM3,	PACKET_TYPEMASK(RTCM3_PACKET)},
    {"AIS",	SEEN_AIS,  	PACKET_TYPEMASK(AIVDM_PACKET)},
};
/* *INDENT-ON* */

char *json_stringify( /*@out@*/ char *to,
		     size_t len,
		     /*@in@*/ const char *from)
/* escape double quotes and control characters inside a JSON string */
{
    /*@-temptrans@*/
    const char *sp;
    char *tp;

    tp = to;
    /*
     * The limit is len-6 here because we need to be leave room for
     * each character to generate an up to 6-character Java-style
     * escape
     */
    for (sp = from; *sp != '\0' && ((tp - to) < ((int)len - 6)); sp++) {
	if (!isascii((unsigned char) *sp) || iscntrl((unsigned char) *sp)) {
	    *tp++ = '\\';
	    switch (*sp) {
	    case '\b':
		*tp++ = 'b';
		break;
	    case '\f':
		*tp++ = 'f';
		break;
	    case '\n':
		*tp++ = 'n';
		break;
	    case '\r':
		*tp++ = 'r';
		break;
	    case '\t':
		*tp++ = 't';
		break;
	    default:
		/* ugh, we'd prefer a C-style escape here, but this is JSON */
		/* http://www.ietf.org/rfc/rfc4627.txt
		 * section 2.5, escape is \uXXXX */
		/* don't forget the NUL in the output count! */
		(void)snprintf(tp, 6, "u%04x", 0x00ff & (unsigned int)*sp);
		tp += strlen(tp);
	    }
	} else {
	    if (*sp == '"' || *sp == '\\')
		*tp++ = '\\';
	    *tp++ = *sp;
	}
    }
    *tp = '\0';

    return to;
    /*@+temptrans@*/
}

void json_version_dump( /*@out@*/ char *reply, size_t replylen)
{
    (void)snprintf(reply, replylen,
		   "{\"class\":\"VERSION\",\"release\":\"%s\",\"rev\":\"%s\",\"proto_major\":%d,\"proto_minor\":%d}\r\n",
		   VERSION, REVISION,
		   GPSD_PROTO_MAJOR_VERSION, GPSD_PROTO_MINOR_VERSION);
}

#ifdef TIMING_ENABLE
#define CONDITIONALLY_UNUSED
#else
#define CONDITIONALLY_UNUSED UNUSED
#endif /* TIMING_ENABLE */


void json_tpv_dump(const struct gps_device_t *session,
		   const struct policy_t *policy CONDITIONALLY_UNUSED,
		   /*@out@*/ char *reply, size_t replylen)
{
    const struct gps_data_t *gpsdata = &session->gpsdata;
#ifdef TIMING_ENABLE
    timestamp_t rtime = timestamp();
#endif /* TIMING_ENABLE */

    assert(replylen > sizeof(char *));
    (void)strlcpy(reply, "{\"class\":\"TPV\",", replylen);
    if (gpsdata->dev.path[0] != '\0')
	str_appendf(reply, replylen, "\"device\":\"%s\",", gpsdata->dev.path);
    str_appendf(reply, replylen, "\"mode\":%d,", gpsdata->fix.mode);
    if (isnan(gpsdata->fix.time) == 0) {
	char tbuf[JSON_DATE_MAX+1];
	str_appendf(reply, replylen,
		       "\"time\":\"%s\",",
		       unix_to_iso8601(gpsdata->fix.time, tbuf, sizeof(tbuf)));
    }
    if (isnan(gpsdata->fix.ept) == 0)
	str_appendf(reply, replylen, "\"ept\":%.3f,", gpsdata->fix.ept);
    /*
     * Suppressing TPV fields that would be invalid because the fix
     * quality doesn't support them is nice for cutting down on the
     * volume of meaningless output, but the real reason to do it is
     * that we've observed that geodetic fix computation is unstable
     * in a way that tends to change low-order digits in invalid
     * fixes. Dumping these tends to cause cross-architecture failures
     * in the regression tests.  This effect has been seen on SiRF-II
     * chips, which are quite common.
     */
    if (gpsdata->fix.mode >= MODE_2D) {
	if (isnan(gpsdata->fix.latitude) == 0)
	    str_appendf(reply, replylen,
			   "\"lat\":%.9f,", gpsdata->fix.latitude);
	if (isnan(gpsdata->fix.longitude) == 0)
	    str_appendf(reply, replylen,
			   "\"lon\":%.9f,", gpsdata->fix.longitude);
	if (gpsdata->fix.mode >= MODE_3D && isnan(gpsdata->fix.altitude) == 0)
	    str_appendf(reply, replylen,
			   "\"alt\":%.3f,", gpsdata->fix.altitude);
	if (isnan(gpsdata->fix.epx) == 0)
	    str_appendf(reply, replylen, "\"epx\":%.3f,", gpsdata->fix.epx);
	if (isnan(gpsdata->fix.epy) == 0)
	    str_appendf(reply, replylen, "\"epy\":%.3f,", gpsdata->fix.epy);
	if ((gpsdata->fix.mode >= MODE_3D) && isnan(gpsdata->fix.epv) == 0)
	    str_appendf(reply, replylen, "\"epv\":%.3f,", gpsdata->fix.epv);
	if (isnan(gpsdata->fix.track) == 0)
	    str_appendf(reply, replylen, "\"track\":%.4f,", gpsdata->fix.track);
	if (isnan(gpsdata->fix.speed) == 0)
	    str_appendf(reply, replylen, "\"speed\":%.3f,", gpsdata->fix.speed);
	if ((gpsdata->fix.mode >= MODE_3D) && isnan(gpsdata->fix.climb) == 0)
	    str_appendf(reply, replylen, "\"climb\":%.3f,", gpsdata->fix.climb);
	if (isnan(gpsdata->fix.epd) == 0)
	    str_appendf(reply, replylen, "\"epd\":%.4f,", gpsdata->fix.epd);
	if (isnan(gpsdata->fix.eps) == 0)
	    str_appendf(reply, replylen, "\"eps\":%.2f,", gpsdata->fix.eps);
	if ((gpsdata->fix.mode >= MODE_3D) && isnan(gpsdata->fix.epc) == 0)
	    str_appendf(reply, replylen, "\"epc\":%.2f,", gpsdata->fix.epc);
#ifdef TIMING_ENABLE
	if (policy->timing) {
#ifdef PPS_ENABLE
	    /*@-type -formattype@*/ /* splint is confused about struct timespec */
	    if (session->ppscount)
		str_appendf(reply, replylen,
			       "\"pps\":%.9f,", 
			       session->ppslast.clock.tv_sec + session->ppslast.clock.tv_nsec / 1e9);
	    /*@+type +formattype@*/
#endif /* PPS_ENABLE */
	    str_appendf(reply, replylen,
			   "\"sor\":%.9f,\"chars\":%lu,\"sats\":%2d,\"rtime\":%.9f,\"week\":%u,\"tow\":%.3f,\"rollovers\":%d",
			   session->sor,
			   session->chars,
			   gpsdata->satellites_used,
			   rtime,
			   session->context->gps_week,
			   session->context->gps_tow,
			   session->context->rollovers);
	}
#endif /* TIMING_ENABLE */
    }
    str_rstrip_char(reply, ',');
    (void)strlcat(reply, "}\r\n", replylen);
}

void json_noise_dump(const struct gps_data_t *gpsdata,
		   /*@out@*/ char *reply, size_t replylen)
{
    char tbuf[JSON_DATE_MAX+1];

    assert(replylen > sizeof(char *));
    (void)strlcpy(reply, "{\"class\":\"GST\",", replylen);
    if (gpsdata->dev.path[0] != '\0')
	str_appendf(reply, replylen, "\"device\":\"%s\",", gpsdata->dev.path);
	str_appendf(reply, replylen,
		   "\"time\":\"%s\",",
		   unix_to_iso8601(gpsdata->gst.utctime, tbuf, sizeof(tbuf)));
#define ADD_GST_FIELD(tag, field) do {                     \
    if (isnan(gpsdata->gst.field) == 0)              \
	str_appendf(reply, replylen, "\"" tag "\":%.3f,", gpsdata->gst.field); \
    } while(0)

    ADD_GST_FIELD("rms",    rms_deviation);
    ADD_GST_FIELD("major",  smajor_deviation);
    ADD_GST_FIELD("minor",  sminor_deviation);
    ADD_GST_FIELD("orient", smajor_orientation);
    ADD_GST_FIELD("lat",    lat_err_deviation);
    ADD_GST_FIELD("lon",    lon_err_deviation);
    ADD_GST_FIELD("alt",    alt_err_deviation);

#undef ADD_GST_FIELD

    str_rstrip_char(reply, ',');
    (void)strlcat(reply, "}\r\n", replylen);
}

void json_sky_dump(const struct gps_data_t *datap,
		   /*@out@*/ char *reply, size_t replylen)
{
    int i, reported = 0;

    assert(replylen > sizeof(char *));
    (void)strlcpy(reply, "{\"class\":\"SKY\",", replylen);
    if (datap->dev.path[0] != '\0')
	str_appendf(reply, replylen, "\"device\":\"%s\",", datap->dev.path);
    if (isnan(datap->skyview_time) == 0) {
	char tbuf[JSON_DATE_MAX+1];
	str_appendf(reply, replylen,
		       "\"time\":\"%s\",",
		       unix_to_iso8601(datap->skyview_time, tbuf, sizeof(tbuf)));
    }
    if (isnan(datap->dop.xdop) == 0)
	str_appendf(reply, replylen, "\"xdop\":%.2f,", datap->dop.xdop);
    if (isnan(datap->dop.ydop) == 0)
	str_appendf(reply, replylen, "\"ydop\":%.2f,", datap->dop.ydop);
    if (isnan(datap->dop.vdop) == 0)
	str_appendf(reply, replylen, "\"vdop\":%.2f,", datap->dop.vdop);
    if (isnan(datap->dop.tdop) == 0)
	str_appendf(reply, replylen, "\"tdop\":%.2f,", datap->dop.tdop);
    if (isnan(datap->dop.hdop) == 0)
	str_appendf(reply, replylen, "\"hdop\":%.2f,", datap->dop.hdop);
    if (isnan(datap->dop.gdop) == 0)
	str_appendf(reply, replylen, "\"gdop\":%.2f,", datap->dop.gdop);
    if (isnan(datap->dop.pdop) == 0)
	str_appendf(reply, replylen, "\"pdop\":%.2f,", datap->dop.pdop);
    /* insurance against flaky drivers */
    for (i = 0; i < datap->satellites_visible; i++)
	if (datap->skyview[i].PRN)
	    reported++;
    if (reported) {
	(void)strlcat(reply, "\"satellites\":[", replylen);
	for (i = 0; i < reported; i++) {
	    if (datap->skyview[i].PRN) {
		str_appendf(reply, replylen,
			       "{\"PRN\":%d,\"el\":%d,\"az\":%d,\"ss\":%.0f,\"used\":%s},",
			       datap->skyview[i].PRN,
			       datap->skyview[i].elevation,
			       datap->skyview[i].azimuth,
			       datap->skyview[i].ss,
			       datap->skyview[i].used ? "true" : "false");
	    }
	}
	str_rstrip_char(reply, ',');
	(void)strlcat(reply, "]", replylen);
    }
    str_rstrip_char(reply, ',');
    (void)strlcat(reply, "}\r\n", replylen);
}

void json_device_dump(const struct gps_device_t *device,
		      /*@out@*/ char *reply, size_t replylen)
{
    struct classmap_t *cmp;
    char buf1[JSON_VAL_MAX * 2 + 1];

    (void)strlcpy(reply, "{\"class\":\"DEVICE\",\"path\":\"", replylen);
    (void)strlcat(reply, device->gpsdata.dev.path, replylen);
    (void)strlcat(reply, "\",", replylen);
    if (device->device_type != NULL) {
	(void)strlcat(reply, "\"driver\":\"", replylen);
	(void)strlcat(reply, device->device_type->type_name, replylen);
	(void)strlcat(reply, "\",", replylen);
    }
    /*@-mustfreefresh@*/
    if (device->subtype[0] != '\0') {
	(void)strlcat(reply, "\"subtype\":\"", replylen);
	(void)strlcat(reply,
		      json_stringify(buf1, sizeof(buf1), device->subtype),
		      replylen);
	(void)strlcat(reply, "\",", replylen);
    }
    /*@+mustfreefresh@*/
    /*
     * There's an assumption here: Anything that we type service_sensor is
     * a serial device with the usual control parameters.
     */
    if (device->gpsdata.online > 0) {	
	str_appendf(reply, replylen,
		       "\"activated\":\"%s\",", 
		       unix_to_iso8601(device->gpsdata.online, buf1, sizeof(buf1)));
	if (device->observed != 0) {
	    int mask = 0;
	    for (cmp = classmap; cmp < classmap + NITEMS(classmap); cmp++)
		if ((device->observed & cmp->packetmask) != 0)
		    mask |= cmp->typemask;
	    if (mask != 0)
		str_appendf(reply, replylen, "\"flags\":%d,", mask);
	}
	if (device->servicetype == service_sensor) {
	    /* speed can be 0 if the device is not currently active */
	    speed_t speed = gpsd_get_speed(device);
	    if (speed != 0)
		str_appendf(reply, replylen,
			       "\"native\":%d,\"bps\":%d,\"parity\":\"%c\",\"stopbits\":%u,\"cycle\":%2.2f,",
			       device->gpsdata.dev.driver_mode,
			       (int)speed,
			       device->gpsdata.dev.parity,
			       device->gpsdata.dev.stopbits,
			       device->gpsdata.dev.cycle);
#ifdef RECONFIGURE_ENABLE
	    if (device->device_type != NULL
		&& device->device_type->rate_switcher != NULL)
		str_appendf(reply, replylen,
			       "\"mincycle\":%2.2f,",
			       device->device_type->min_cycle);
#endif /* RECONFIGURE_ENABLE */
	}
    }
    str_rstrip_char(reply, ',');
    (void)strlcat(reply, "}\r\n", replylen);
}

void json_watch_dump(const struct policy_t *ccp,
		     /*@out@*/ char *reply, size_t replylen)
{
    /*@-compdef@*/
    (void)snprintf(reply, replylen,
		   "{\"class\":\"WATCH\",\"enable\":%s,\"json\":%s,\"nmea\":%s,\"raw\":%d,\"scaled\":%s,\"timing\":%s,\"split24\":%s,",
		   ccp->watcher ? "true" : "false",
		   ccp->json ? "true" : "false",
		   ccp->nmea ? "true" : "false",
		   ccp->raw,
		   ccp->scaled ? "true" : "false",
		   ccp->timing ? "true" : "false",
		   ccp->split24 ? "true" : "false");
    if (ccp->devpath[0] != '\0')
	str_appendf(reply, replylen, "\"device\":\"%s\",", ccp->devpath);
    str_rstrip_char(reply, ',');
    (void)strlcat(reply, "}\r\n", replylen);
    /*@+compdef@*/
}

void json_subframe_dump(const struct gps_data_t *datap,
			/*@out@*/ char buf[], size_t buflen)
{
    const struct subframe_t *subframe = &datap->subframe;
    const bool scaled = datap->policy.scaled;
 
    (void)snprintf(buf, buflen, "{\"class\":\"SUBFRAME\",\"device\":\"%s\","
		   "\"tSV\":%u,\"TOW17\":%u,\"frame\":%u,\"scaled\":%s",
		   datap->dev.path,
		   (unsigned int)subframe->tSVID,
		   (unsigned int)subframe->TOW17,
		   (unsigned int)subframe->subframe_num,
		   JSON_BOOL(scaled));

    /*@-type@*/
    if ( 1 == subframe->subframe_num ) {
	if (scaled) {
	    str_appendf(buf, buflen,
			",\"EPHEM1\":{\"WN\":%u,\"IODC\":%u,\"L2\":%u,"
			"\"ura\":%u,\"hlth\":%u,\"L2P\":%u,\"Tgd\":%g,"
			"\"toc\":%lu,\"af2\":%.4g,\"af1\":%.6e,\"af0\":%.7e}",
			(unsigned int)subframe->sub1.WN,
			(unsigned int)subframe->sub1.IODC,
			(unsigned int)subframe->sub1.l2,
			subframe->sub1.ura,
			subframe->sub1.hlth,
			(unsigned int)subframe->sub1.l2p,
			subframe->sub1.d_Tgd,
			(unsigned long)subframe->sub1.l_toc,
			subframe->sub1.d_af2,
			subframe->sub1.d_af1,
			subframe->sub1.d_af0);
	} else {
	    str_appendf(buf, buflen,
			",\"EPHEM1\":{\"WN\":%u,\"IODC\":%u,\"L2\":%u,"
			"\"ura\":%u,\"hlth\":%u,\"L2P\":%u,\"Tgd\":%d,"
			"\"toc\":%u,\"af2\":%ld,\"af1\":%d,\"af0\":%d}",
			(unsigned int)subframe->sub1.WN,
			(unsigned int)subframe->sub1.IODC,
			(unsigned int)subframe->sub1.l2,
			subframe->sub1.ura,
			subframe->sub1.hlth,
			(unsigned int)subframe->sub1.l2p,
			(int)subframe->sub1.Tgd,
			(unsigned int)subframe->sub1.toc,
			(long)subframe->sub1.af2,
			(int)subframe->sub1.af1,
			(int)subframe->sub1.af0);
	}
    } else if ( 2 == subframe->subframe_num ) {
	if (scaled) {
	    str_appendf(buf, buflen,
			",\"EPHEM2\":{\"IODE\":%u,\"Crs\":%.6e,\"deltan\":%.6e,"
			"\"M0\":%.11e,\"Cuc\":%.6e,\"e\":%f,\"Cus\":%.6e,"
			"\"sqrtA\":%.11g,\"toe\":%lu,\"FIT\":%u,\"AODO\":%u}",
			(unsigned int)subframe->sub2.IODE,
			subframe->sub2.d_Crs,
			subframe->sub2.d_deltan,
			subframe->sub2.d_M0,
			subframe->sub2.d_Cuc,
			subframe->sub2.d_eccentricity,
			subframe->sub2.d_Cus,
			subframe->sub2.d_sqrtA,
			(unsigned long)subframe->sub2.l_toe,
			(unsigned int)subframe->sub2.fit,
			(unsigned int)subframe->sub2.u_AODO);
	} else {
	    str_appendf(buf, buflen,
			",\"EPHEM2\":{\"IODE\":%u,\"Crs\":%d,\"deltan\":%d,"
			"\"M0\":%ld,\"Cuc\":%d,\"e\":%ld,\"Cus\":%d,"
			"\"sqrtA\":%lu,\"toe\":%lu,\"FIT\":%u,\"AODO\":%u}",
			(unsigned int)subframe->sub2.IODE,
			(int)subframe->sub2.Crs,
			(int)subframe->sub2.deltan,
			(long)subframe->sub2.M0,
			(int)subframe->sub2.Cuc,
			(long)subframe->sub2.e,
			(int)subframe->sub2.Cus,
			(unsigned long)subframe->sub2.sqrtA,
			(unsigned long)subframe->sub2.toe,
			(unsigned int)subframe->sub2.fit,
			(unsigned int)subframe->sub2.AODO);
	}
    } else if ( 3 == subframe->subframe_num ) {
	if (scaled) {
	    str_appendf(buf, buflen,
		",\"EPHEM3\":{\"IODE\":%3u,\"IDOT\":%.6g,\"Cic\":%.6e,"
		"\"Omega0\":%.11e,\"Cis\":%.7g,\"i0\":%.11e,\"Crc\":%.7g,"
		"\"omega\":%.11e,\"Omegad\":%.6e}",
			(unsigned int)subframe->sub3.IODE,
			subframe->sub3.d_IDOT,
			subframe->sub3.d_Cic,
			subframe->sub3.d_Omega0,
			subframe->sub3.d_Cis,
			subframe->sub3.d_i0,
			subframe->sub3.d_Crc,
			subframe->sub3.d_omega,
			subframe->sub3.d_Omegad );
	} else {
	    str_appendf(buf, buflen,
		",\"EPHEM3\":{\"IODE\":%u,\"IDOT\":%u,\"Cic\":%u,"
		"\"Omega0\":%ld,\"Cis\":%d,\"i0\":%ld,\"Crc\":%d,"
		"\"omega\":%ld,\"Omegad\":%ld}",
			(unsigned int)subframe->sub3.IODE,
			(unsigned int)subframe->sub3.IDOT,
			(unsigned int)subframe->sub3.Cic,
			(long int)subframe->sub3.Omega0,
			(int)subframe->sub3.Cis,
			(long int)subframe->sub3.i0,
			(int)subframe->sub3.Crc,
			(long int)subframe->sub3.omega,
			(long int)subframe->sub3.Omegad );
	}
    } else if ( subframe->is_almanac ) {
	if (scaled) {
	    /*@-compdef@*/
	    str_appendf(buf, buflen,
			",\"ALMANAC\":{\"ID\":%d,\"Health\":%u,"
			"\"e\":%g,\"toa\":%lu,"
			"\"deltai\":%.10e,\"Omegad\":%.5e,\"sqrtA\":%.10g,"
			"\"Omega0\":%.10e,\"omega\":%.10e,\"M0\":%.11e,"
			"\"af0\":%.5e,\"af1\":%.5e}",
			(int)subframe->sub5.almanac.sv,
			(unsigned int)subframe->sub5.almanac.svh,
			subframe->sub5.almanac.d_eccentricity,
			(unsigned long)subframe->sub5.almanac.l_toa,
			subframe->sub5.almanac.d_deltai,
			subframe->sub5.almanac.d_Omegad,
			subframe->sub5.almanac.d_sqrtA,
			subframe->sub5.almanac.d_Omega0,
			subframe->sub5.almanac.d_omega,
			subframe->sub5.almanac.d_M0,
			subframe->sub5.almanac.d_af0,
			subframe->sub5.almanac.d_af1);
	} else {
	    str_appendf(buf, buflen,
			",\"ALMANAC\":{\"ID\":%d,\"Health\":%u,"
			"\"e\":%u,\"toa\":%u,"
			"\"deltai\":%d,\"Omegad\":%d,\"sqrtA\":%lu,"
			"\"Omega0\":%ld,\"omega\":%ld,\"M0\":%ld,"
			"\"af0\":%d,\"af1\":%d}",
			(int)subframe->sub5.almanac.sv,
			(unsigned int)subframe->sub5.almanac.svh,
			(unsigned int)subframe->sub5.almanac.e,
			(unsigned int)subframe->sub5.almanac.toa,
			(int)subframe->sub5.almanac.deltai,
			(int)subframe->sub5.almanac.Omegad,
			(unsigned long)subframe->sub5.almanac.sqrtA,
			(long)subframe->sub5.almanac.Omega0,
			(long)subframe->sub5.almanac.omega,
			(long)subframe->sub5.almanac.M0,
			(int)subframe->sub5.almanac.af0,
			(int)subframe->sub5.almanac.af1);
	}
    } else if ( 4 == subframe->subframe_num ) {
	str_appendf(buf, buflen,
	    ",\"pageid\":%u",
		       (unsigned int)subframe->pageid);
	switch (subframe->pageid ) {
	case 13:
	case 52:
	{
		int i;
	    /*@+charint@*/
		/* decoding of ERD to SV is non trivial and not done yet */
		str_appendf(buf, buflen,
		    ",\"ERD\":{\"ai\":%u,", subframe->sub4_13.ai);

		/* 1-index loop to construct json, rather than giant snprintf */
		for(i = 1 ; i <= 30; i++){
		    str_appendf(buf, buflen,
			"\"ERD%d\":%d,", i, subframe->sub4_13.ERD[i]);
		}
		str_rstrip_char(buf, ',');
		str_appendf(buf, buflen, "}");
		break;
	    /*@-charint@*/
	}
	case 55:
	    /* JSON is UTF-8. double quote, backslash and
	     * control charactores (U+0000 through U+001F).must be
	     * escaped. */
	    /* system message can be 24 bytes, JSON can escape all
	     * chars so up to 24*6 long. */

	    {
		char buf1[25 * 6];
		(void)json_stringify(buf1, sizeof(buf1), subframe->sub4_17.str);
		str_appendf(buf, buflen,
			       ",\"system_message\":\"%.144s\"", buf1);
	    }
	    break;
	case 56:
	    if (scaled) {
		str_appendf(buf, buflen,
			",\"IONO\":{\"a0\":%.5g,\"a1\":%.5g,\"a2\":%.5g,"
			"\"a3\":%.5g,\"b0\":%.5g,\"b1\":%.5g,\"b2\":%.5g,"
			"\"b3\":%.5g,\"A1\":%.11e,\"A0\":%.11e,\"tot\":%.5g,"
			"\"WNt\":%u,\"ls\":%d,\"WNlsf\":%u,\"DN\":%u,"
			"\"lsf\":%d}",
			    subframe->sub4_18.d_alpha0,
			    subframe->sub4_18.d_alpha1,
			    subframe->sub4_18.d_alpha2,
			    subframe->sub4_18.d_alpha3,
			    subframe->sub4_18.d_beta0,
			    subframe->sub4_18.d_beta1,
			    subframe->sub4_18.d_beta2,
			    subframe->sub4_18.d_beta3,
			    subframe->sub4_18.d_A1,
			    subframe->sub4_18.d_A0,
			    subframe->sub4_18.d_tot,
			    (unsigned int)subframe->sub4_18.WNt,
			    (int)subframe->sub4_18.leap,
			    (unsigned int)subframe->sub4_18.WNlsf,
			    (unsigned int)subframe->sub4_18.DN,
			    (int)subframe->sub4_18.lsf);
	    } else {
		str_appendf(buf, buflen,
			",\"IONO\":{\"a0\":%d,\"a1\":%d,\"a2\":%d,\"a3\":%d,"
			"\"b0\":%d,\"b1\":%d,\"b2\":%d,\"b3\":%d,"
			"\"A1\":%ld,\"A0\":%ld,\"tot\":%u,\"WNt\":%u,"
			"\"ls\":%d,\"WNlsf\":%u,\"DN\":%u,\"lsf\":%d}",
			    (int)subframe->sub4_18.alpha0,
			    (int)subframe->sub4_18.alpha1,
			    (int)subframe->sub4_18.alpha2,
			    (int)subframe->sub4_18.alpha3,
			    (int)subframe->sub4_18.beta0,
			    (int)subframe->sub4_18.beta1,
			    (int)subframe->sub4_18.beta2,
			    (int)subframe->sub4_18.beta3,
			    (long)subframe->sub4_18.A1,
			    (long)subframe->sub4_18.A0,
			    (unsigned int)subframe->sub4_18.tot,
			    (unsigned int)subframe->sub4_18.WNt,
			    (int)subframe->sub4_18.leap,
			    (unsigned int)subframe->sub4_18.WNlsf,
			    (unsigned int)subframe->sub4_18.DN,
			    (int)subframe->sub4_18.lsf);
	    }
	    break;
	case 25:
	case 63:
	{
	    int i;
	    str_appendf(buf, buflen,
			   ",\"HEALTH\":{\"data_id\":%d,",
			   (int)subframe->data_id);

		/* 1-index loop to construct json, rather than giant snprintf */
		for(i = 1 ; i <= 32; i++){
		    str_appendf(buf, buflen,
				   "\"SV%d\":%d,",
				   i, (int)subframe->sub4_25.svf[i]);
		}
		for(i = 0 ; i < 8; i++){ /* 0-index */
		    str_appendf(buf, buflen,
				   "\"SVH%d\":%d,",
				   i+25, (int)subframe->sub4_25.svhx[i]);
		}
		str_rstrip_char(buf, ',');
		str_appendf(buf, buflen, "}");

	    break;
	    }
	}
    } else if ( 5 == subframe->subframe_num ) {
	str_appendf(buf, buflen,
	    ",\"pageid\":%u",
		       (unsigned int)subframe->pageid);
	if ( 51 == subframe->pageid ) {
	    int i;
	    /*@+matchanyintegral@*/
	    /* subframe5, page 25 */
	    str_appendf(buf, buflen,
		",\"HEALTH2\":{\"toa\":%lu,\"WNa\":%u,",
			   (unsigned long)subframe->sub5_25.l_toa,
			   (unsigned int)subframe->sub5_25.WNa);
		/* 1-index loop to construct json */
		for(i = 1 ; i <= 24; i++){
		    str_appendf(buf, buflen,
				   "\"SV%d\":%d,", i, (int)subframe->sub5_25.sv[i]);
		}
		str_rstrip_char(buf, ',');
		str_appendf(buf, buflen, "}");

	    /*@-matchanyintegral@*/
	}
    }
    /*@+type@*/
    (void)strlcat(buf, "}\r\n", buflen);
    /*@+compdef@*/
}

#if defined(RTCM104V2_ENABLE)
void json_rtcm2_dump(const struct rtcm2_t *rtcm,
		     /*@null@*/const char *device,
		     /*@out@*/char buf[], size_t buflen)
/* dump the contents of a parsed RTCM104 message as JSON */
{
    /*@-mustfreefresh@*/
    char buf1[JSON_VAL_MAX * 2 + 1];
    unsigned int n;

    (void)snprintf(buf, buflen, "{\"class\":\"RTCM2\",");
    if (device != NULL && device[0] != '\0')
	str_appendf(buf, buflen, "\"device\":\"%s\",", device);
    str_appendf(buf, buflen,
		   "\"type\":%u,\"station_id\":%u,\"zcount\":%0.1f,\"seqnum\":%u,\"length\":%u,\"station_health\":%u,",
		   rtcm->type, rtcm->refstaid, rtcm->zcount, rtcm->seqnum,
		   rtcm->length, rtcm->stathlth);

    switch (rtcm->type) {
    case 1:
    case 9:
	(void)strlcat(buf, "\"satellites\":[", buflen);
	for (n = 0; n < rtcm->gps_ranges.nentries; n++) {
	    const struct gps_rangesat_t *rsp = &rtcm->gps_ranges.sat[n];
	    str_appendf(buf, buflen,
			   "{\"ident\":%u,\"udre\":%u,\"iod\":%u,\"prc\":%0.3f,\"rrc\":%0.3f},",
			   rsp->ident,
			   rsp->udre, rsp->iod,
			   rsp->prc, rsp->rrc);
	}
	str_rstrip_char(buf, ',');
	(void)strlcat(buf, "]", buflen);
	break;

    case 3:
	if (rtcm->ecef.valid)
	    str_appendf(buf, buflen,
			   "\"x\":%.2f,\"y\":%.2f,\"z\":%.2f,",
			   rtcm->ecef.x, rtcm->ecef.y, rtcm->ecef.z);
	break;

    case 4:
	if (rtcm->reference.valid) {
	    /*
	     * Beware! Needs to stay synchronized with a JSON
	     * enumeration map in the parser. This interpretation of
	     * NAVSYSTEM_GALILEO is assumed from RTCM3, it's not
	     * actually documented in RTCM 2.1.
	     */
	    static char *navsysnames[] = { "GPS", "GLONASS", "GALILEO" };
	    str_appendf(buf, buflen,
			   "\"system\":\"%s\",\"sense\":%1d,\"datum\":\"%s\",\"dx\":%.1f,\"dy\":%.1f,\"dz\":%.1f,",
			   rtcm->reference.system >= NITEMS(navsysnames)
			   ? "UNKNOWN"
			   : navsysnames[rtcm->reference.system],
			   rtcm->reference.sense,
			   rtcm->reference.datum,
			   rtcm->reference.dx,
			   rtcm->reference.dy, rtcm->reference.dz);
	}
	break;

    case 5:
	(void)strlcat(buf, "\"satellites\":[", buflen);
	for (n = 0; n < rtcm->conhealth.nentries; n++) {
	    const struct consat_t *csp = &rtcm->conhealth.sat[n];
	    str_appendf(buf, buflen,
			   "{\"ident\":%u,\"iodl\":%s,\"health\":%1u,\"snr\":%d,\"health_en\":%s,\"new_data\":%s,\"los_warning\":%s,\"tou\":%u},",
			   csp->ident,
			   JSON_BOOL(csp->iodl),
			   (unsigned)csp->health,
			   csp->snr,
			   JSON_BOOL(csp->health_en),
			   JSON_BOOL(csp->new_data),
			   JSON_BOOL(csp->los_warning), csp->tou);
	}
	str_rstrip_char(buf, ',');
	(void)strlcat(buf, "]", buflen);
	break;

    case 6:			/* NOP msg */
	break;

    case 7:
	(void)strlcat(buf, "\"satellites\":[", buflen);
	for (n = 0; n < rtcm->almanac.nentries; n++) {
	    const struct station_t *ssp = &rtcm->almanac.station[n];
	    str_appendf(buf, buflen,
			   "{\"lat\":%.4f,\"lon\":%.4f,\"range\":%u,\"frequency\":%.1f,\"health\":%u,\"station_id\":%u,\"bitrate\":%u},",
			   ssp->latitude,
			   ssp->longitude,
			   ssp->range,
			   ssp->frequency,
			   ssp->health, ssp->station_id, ssp->bitrate);
	}
	str_rstrip_char(buf, ',');
	(void)strlcat(buf, "]", buflen);
	break;

    case 13:
	str_appendf(buf, buflen,
		       "\"status\":%s,\"rangeflag\":%s,"
		       "\"lat\":%.2f,\"lon\":%.2f,\"range\":%u,",
		       JSON_BOOL(rtcm->xmitter.status),
		       JSON_BOOL(rtcm->xmitter.rangeflag),
		       rtcm->xmitter.lat,
		       rtcm->xmitter.lon,
		       rtcm->xmitter.range);
	break;

    case 14:
	str_appendf(buf, buflen,
		       "\"week\":%u,\"hour\":%u,\"leapsecs\":%u,",
		       rtcm->gpstime.week,
		       rtcm->gpstime.hour,
		       rtcm->gpstime.leapsecs);
	break;

    case 16:
	str_appendf(buf, buflen,
		       "\"message\":\"%s\"", json_stringify(buf1,
							    sizeof(buf1),
							    rtcm->message));
	break;

    case 31:
	(void)strlcat(buf, "\"satellites\":[", buflen);
	for (n = 0; n < rtcm->glonass_ranges.nentries; n++) {
	    const struct glonass_rangesat_t *rsp = &rtcm->glonass_ranges.sat[n];
	    str_appendf(buf, buflen,
			   "{\"ident\":%u,\"udre\":%u,\"change\":%s,\"tod\":%u,\"prc\":%0.3f,\"rrc\":%0.3f},",
			   rsp->ident,
			   rsp->udre,
			   JSON_BOOL(rsp->change),
			   rsp->tod,
			   rsp->prc, rsp->rrc);
	}
	str_rstrip_char(buf, ',');
	(void)strlcat(buf, "]", buflen);
	break;

    default:
	(void)strlcat(buf, "\"data\":[", buflen);
	for (n = 0; n < rtcm->length; n++)
	    str_appendf(buf, buflen, "\"0x%08x\",", rtcm->words[n]);
	str_rstrip_char(buf, ',');
	(void)strlcat(buf, "]", buflen);
	break;
    }

    str_rstrip_char(buf, ',');
    (void)strlcat(buf, "}\r\n", buflen);
    /*@+mustfreefresh@*/
}
#endif /* defined(RTCM104V2_ENABLE) */

#if defined(RTCM104V3_ENABLE)
void json_rtcm3_dump(const struct rtcm3_t *rtcm,
		     /*@null@*/const char *device,
		     /*@out@*/char buf[], size_t buflen)
/* dump the contents of a parsed RTCM104v3 message as JSON */
{
    /*@-mustfreefresh@*/
    char buf1[JSON_VAL_MAX * 2 + 1];
    unsigned short i;
    unsigned int n;

    (void)snprintf(buf, buflen, "{\"class\":\"RTCM3\",");
    if (device != NULL && device[0] != '\0')
	str_appendf(buf, buflen, "\"device\":\"%s\",", device);
    str_appendf(buf, buflen, "\"type\":%u,", rtcm->type);
    str_appendf(buf, buflen, "\"length\":%u,", rtcm->length);

#define CODE(x) (unsigned int)(x)
#define INT(x) (unsigned int)(x)
    switch (rtcm->type) {
    case 1001:
	str_appendf(buf, buflen,
		       "\"station_id\":%u,\"tow\":%d,\"sync\":\"%s\","
		       "\"smoothing\":\"%s\",\"interval\":\"%u\",",
		       rtcm->rtcmtypes.rtcm3_1001.header.station_id,
		       (int)rtcm->rtcmtypes.rtcm3_1001.header.tow,
		       JSON_BOOL(rtcm->rtcmtypes.rtcm3_1001.header.sync),
		       JSON_BOOL(rtcm->rtcmtypes.rtcm3_1001.header.smoothing),
		       rtcm->rtcmtypes.rtcm3_1001.header.interval);
	(void)strlcat(buf, "\"satellites\":[", buflen);
	for (i = 0; i < rtcm->rtcmtypes.rtcm3_1001.header.satcount; i++) {
#define R1001 rtcm->rtcmtypes.rtcm3_1001.rtk_data[i]
	    str_appendf(buf, buflen,
			   "{\"ident\":%u,\"ind\":%u,\"prange\":%8.2f,"
			   "\"delta\":%6.4f,\"lockt\":%u},",
			   R1001.ident,
			   CODE(R1001.L1.indicator),
			   R1001.L1.pseudorange,
			   R1001.L1.rangediff,
			   INT(R1001.L1.locktime));
#undef R1001
	}
	str_rstrip_char(buf, ',');
	(void)strlcat(buf, "]", buflen);
	break;

    case 1002:
	str_appendf(buf, buflen,
		       "\"station_id\":%u,\"tow\":%d,\"sync\":\"%s\","
		       "\"smoothing\":\"%s\",\"interval\":\"%u\",",
		       rtcm->rtcmtypes.rtcm3_1002.header.station_id,
		       (int)rtcm->rtcmtypes.rtcm3_1002.header.tow,
		       JSON_BOOL(rtcm->rtcmtypes.rtcm3_1002.header.sync),
		       JSON_BOOL(rtcm->rtcmtypes.rtcm3_1002.header.smoothing),
		       rtcm->rtcmtypes.rtcm3_1002.header.interval);
	(void)strlcat(buf, "\"satellites\":[", buflen);
	for (i = 0; i < rtcm->rtcmtypes.rtcm3_1002.header.satcount; i++) {
#define R1002 rtcm->rtcmtypes.rtcm3_1002.rtk_data[i]
	    str_appendf(buf, buflen,
			   "{\"ident\":%u,\"ind\":%u,\"prange\":%8.2f,"
			   "\"delta\":%6.4f,\"lockt\":%u,\"amb\":%u,"
			   "\"CNR\":%.2f},",
			   R1002.ident,
			   CODE(R1002.L1.indicator),
			   R1002.L1.pseudorange,
			   R1002.L1.rangediff,
			   INT(R1002.L1.locktime),
			   INT(R1002.L1.ambiguity),
			   R1002.L1.CNR);
#undef R1002
	}
	str_rstrip_char(buf, ',');
	(void)strlcat(buf, "]", buflen);
	break;

    case 1003:
	str_appendf(buf, buflen,
		       "\"station_id\":%u,\"tow\":%d,\"sync\":\"%s\","
		       "\"smoothing\":\"%s\",\"interval\":\"%u\",",
		       rtcm->rtcmtypes.rtcm3_1003.header.station_id,
		       (int)rtcm->rtcmtypes.rtcm3_1003.header.tow,
		       JSON_BOOL(rtcm->rtcmtypes.rtcm3_1003.header.sync),
		       JSON_BOOL(rtcm->rtcmtypes.rtcm3_1003.header.smoothing),
		       rtcm->rtcmtypes.rtcm3_1003.header.interval);
	(void)strlcat(buf, "\"satellites\":[", buflen);
	for (i = 0; i < rtcm->rtcmtypes.rtcm3_1003.header.satcount; i++) {
#define R1003 rtcm->rtcmtypes.rtcm3_1003.rtk_data[i]
	    str_appendf(buf, buflen,
			   "{\"ident\":%u,"
			   "\"L1\":{\"ind\":%u,\"prange\":%8.2f,"
			   "\"delta\":%6.4f,\"lockt\":%u},"
			   "\"L2\":{\"ind\":%u,\"prange\":%8.2f,"
			   "\"delta\":%6.4f,\"lockt\":%u},"
			   "},",
			   R1003.ident,
			   CODE(R1003.L1.indicator),
			   R1003.L1.pseudorange,
			   R1003.L1.rangediff,
			   INT(R1003.L1.locktime),
			   CODE(R1003.L2.indicator),
			   R1003.L2.pseudorange,
			   R1003.L2.rangediff,
			   INT(R1003.L2.locktime));
#undef R1003
	}
	str_rstrip_char(buf, ',');
	(void)strlcat(buf, "]", buflen);
	break;

    case 1004:
	str_appendf(buf, buflen,
		       "\"station_id\":%u,\"tow\":%d,\"sync\":\"%s\","
		       "\"smoothing\":\"%s\",\"interval\":\"%u\",",
		       rtcm->rtcmtypes.rtcm3_1004.header.station_id,
		       (int)rtcm->rtcmtypes.rtcm3_1004.header.tow,
		       JSON_BOOL(rtcm->rtcmtypes.rtcm3_1004.header.sync),
		       JSON_BOOL(rtcm->rtcmtypes.rtcm3_1004.header.smoothing),
		       rtcm->rtcmtypes.rtcm3_1004.header.interval);
	(void)strlcat(buf, "\"satellites\":[", buflen);
	for (i = 0; i < rtcm->rtcmtypes.rtcm3_1004.header.satcount; i++) {
#define R1004 rtcm->rtcmtypes.rtcm3_1004.rtk_data[i]
	    str_appendf(buf, buflen,
			   "{\"ident\":%u,"
			   "\"L1\":{\"ind\":%u,\"prange\":%8.2f,"
			   "\"delta\":%6.4f,\"lockt\":%u,"
			   "\"amb\":%u,\"CNR\":%.2f}"
			   "\"L2\":{\"ind\":%u,\"prange\":%8.2f,"
			   "\"delta\":%6.4f,\"lockt\":%u,"
			   "\"CNR\":%.2f}"
			   "},",
			   R1004.ident,
			   CODE(R1004.L1.indicator),
			   R1004.L1.pseudorange,
			   R1004.L1.rangediff,
			   INT(R1004.L1.locktime),
			   INT(R1004.L1.ambiguity),
			   R1004.L1.CNR,
			   CODE(R1004.L2.indicator),
			   R1004.L2.pseudorange,
			   R1004.L2.rangediff,
			   INT(R1004.L2.locktime),
			   R1004.L2.CNR);
#undef R1004
	}
	str_rstrip_char(buf, ',');
	(void)strlcat(buf, "]", buflen);
	break;

    case 1005:
	str_appendf(buf, buflen,
		       "\"station_id\":%u,\"system\":[",
		       rtcm->rtcmtypes.rtcm3_1005.station_id);
	if ((rtcm->rtcmtypes.rtcm3_1005.system & 0x04)!=0)
	    (void)strlcat(buf, "\"GPS\",", buflen);
	if ((rtcm->rtcmtypes.rtcm3_1005.system & 0x02)!=0)
	    (void)strlcat(buf, "\"GLONASS\",", buflen - strlen(buf));
	if ((rtcm->rtcmtypes.rtcm3_1005.system & 0x01)!=0)
	    (void)strlcat(buf, "\"GALILEO\",", buflen);
	str_rstrip_char(buf, ',');
	str_appendf(buf, buflen,
		       "],\"refstation\":%s,\"sro\":%s,"
		       "\"x\":%.4f,\"y\":%.4f,\"z\":%.4f,",
		       JSON_BOOL(rtcm->rtcmtypes.rtcm3_1005.reference_station),
		       JSON_BOOL(rtcm->rtcmtypes.rtcm3_1005.single_receiver),
		       rtcm->rtcmtypes.rtcm3_1005.ecef_x,
		       rtcm->rtcmtypes.rtcm3_1005.ecef_y,
		       rtcm->rtcmtypes.rtcm3_1005.ecef_z);
	break;

    case 1006:
	str_appendf(buf, buflen,
		       "\"station_id\":%u,\"system\":[",
		       rtcm->rtcmtypes.rtcm3_1006.station_id);
	if ((rtcm->rtcmtypes.rtcm3_1006.system & 0x04)!=0)
	    (void)strlcat(buf, "\"GPS\",", buflen);
	if ((rtcm->rtcmtypes.rtcm3_1006.system & 0x02)!=0)
	    (void)strlcat(buf, "\"GLONASS\",", buflen);
	if ((rtcm->rtcmtypes.rtcm3_1006.system & 0x01)!=0)
	    (void)strlcat(buf, "\"GALILEO\",", buflen);
	str_rstrip_char(buf, ',');
	str_appendf(buf, buflen,
		       "],\"refstation\":%s,\"sro\":%s,"
		       "\"x\":%.4f,\"y\":%.4f,\"z\":%.4f,",
		       JSON_BOOL(rtcm->rtcmtypes.rtcm3_1006.reference_station),
		       JSON_BOOL(rtcm->rtcmtypes.rtcm3_1006.single_receiver),
		       rtcm->rtcmtypes.rtcm3_1006.ecef_x,
		       rtcm->rtcmtypes.rtcm3_1006.ecef_y,
		       rtcm->rtcmtypes.rtcm3_1006.ecef_z);
	str_appendf(buf, buflen,
		       "\"h\":%.4f,",
		       rtcm->rtcmtypes.rtcm3_1006.height);
	break;

    case 1007:
	str_appendf(buf, buflen,
		       "\"station_id\":%u,\"desc\":\"%s\",\"setup_id\":%u",
		       rtcm->rtcmtypes.rtcm3_1007.station_id,
		       rtcm->rtcmtypes.rtcm3_1007.descriptor,
		       rtcm->rtcmtypes.rtcm3_1007.setup_id);
	break;

    case 1008:
	str_appendf(buf, buflen,
		       "\"station_id\":%u,\"desc\":\"%s\","
		       "\"setup_id\":%u,\"serial\":\"%s\"",
		       rtcm->rtcmtypes.rtcm3_1008.station_id,
		       rtcm->rtcmtypes.rtcm3_1008.descriptor,
		       INT(rtcm->rtcmtypes.rtcm3_1008.setup_id),
		       rtcm->rtcmtypes.rtcm3_1008.serial);
	break;

    case 1009:
	str_appendf(buf, buflen,
		       "\"station_id\":%u,\"tow\":%d,\"sync\":\"%s\","
		       "\"smoothing\":\"%s\",\"interval\":\"%u\","
		       "\"satcount\":\"%u\",",
		       rtcm->rtcmtypes.rtcm3_1009.header.station_id,
		       (int)rtcm->rtcmtypes.rtcm3_1009.header.tow,
		       JSON_BOOL(rtcm->rtcmtypes.rtcm3_1009.header.sync),
		       JSON_BOOL(rtcm->rtcmtypes.rtcm3_1009.header.smoothing),
		       rtcm->rtcmtypes.rtcm3_1009.header.interval,
		       rtcm->rtcmtypes.rtcm3_1009.header.satcount);
	(void)strlcat(buf, "\"satellites\":[", buflen);
	for (i = 0; i < rtcm->rtcmtypes.rtcm3_1009.header.satcount; i++) {
#define R1009 rtcm->rtcmtypes.rtcm3_1009.rtk_data[i]
	    str_appendf(buf, buflen,
			   "{\"ident\":%u,\"ind\":%u,\"channel\":%u,"
			   "\"prange\":%8.2f,\"delta\":%6.4f,\"lockt\":%u},",
			   R1009.ident,
			   CODE(R1009.L1.indicator),
			   R1009.L1.channel,
			   R1009.L1.pseudorange,
			   R1009.L1.rangediff,
			   INT(R1009.L1.locktime));
#undef R1009
	}
	str_rstrip_char(buf, ',');
	(void)strlcat(buf, "]", buflen);
	break;

    case 1010:
	str_appendf(buf, buflen,
		       "\"station_id\":%u,\"tow\":%d,\"sync\":\"%s\","
		       "\"smoothing\":\"%s\",\"interval\":\"%u\",",
		       rtcm->rtcmtypes.rtcm3_1010.header.station_id,
		       (int)rtcm->rtcmtypes.rtcm3_1010.header.tow,
		       JSON_BOOL(rtcm->rtcmtypes.rtcm3_1010.header.sync),
		       JSON_BOOL(rtcm->rtcmtypes.rtcm3_1010.header.smoothing),
		       rtcm->rtcmtypes.rtcm3_1010.header.interval);
	(void)strlcat(buf, "\"satellites\":[", buflen);
	for (i = 0; i < rtcm->rtcmtypes.rtcm3_1010.header.satcount; i++) {
#define R1010 rtcm->rtcmtypes.rtcm3_1010.rtk_data[i]
	    str_appendf(buf, buflen,
			   "{\"ident\":%u,\"ind\":%u,\"channel\":%u,"
			   "\"prange\":%8.2f,\"delta\":%6.4f,\"lockt\":%u,"
			   "\"amb\":%u,\"CNR\":%.2f},",
			   R1010.ident,
			   CODE(R1010.L1.indicator),
			   R1010.L1.channel,
			   R1010.L1.pseudorange,
			   R1010.L1.rangediff,
			   INT(R1010.L1.locktime),
			   INT(R1010.L1.ambiguity),
			   R1010.L1.CNR);
#undef R1010
	}
	str_rstrip_char(buf, ',');
	(void)strlcat(buf, "]", buflen);
	break;

    case 1011:
	str_appendf(buf, buflen,
		       "\"station_id\":%u,\"tow\":%d,\"sync\":\"%s\","
		       "\"smoothing\":\"%s\",\"interval\":\"%u\",",
		       rtcm->rtcmtypes.rtcm3_1011.header.station_id,
		       (int)rtcm->rtcmtypes.rtcm3_1011.header.tow,
		       JSON_BOOL(rtcm->rtcmtypes.rtcm3_1011.header.sync),
		       JSON_BOOL(rtcm->rtcmtypes.rtcm3_1011.header.smoothing),
		       rtcm->rtcmtypes.rtcm3_1011.header.interval);
	(void)strlcat(buf, "\"satellites\":[", buflen);
	for (i = 0; i < rtcm->rtcmtypes.rtcm3_1011.header.satcount; i++) {
#define R1011 rtcm->rtcmtypes.rtcm3_1011.rtk_data[i]
	    str_appendf(buf, buflen,
			   "{\"ident\":%u,\"channel\":%u,"
			   "\"L1\":{\"ind\":%u,"
			   "\"prange\":%8.2f,\"delta\":%6.4f,\"lockt\":%u},"
			   "\"L2:{\"ind\":%u,\"prange\":%8.2f,"
			   "\"delta\":%6.4f,\"lockt\":%u}"
			   "}",
			   R1011.ident,R1011.L1.channel,
			   CODE(R1011.L1.indicator),
			   R1011.L1.pseudorange,
			   R1011.L1.rangediff,
			   INT(R1011.L1.locktime),
			   CODE(R1011.L2.indicator),
			   R1011.L2.pseudorange,
			   R1011.L2.rangediff,
			   INT(R1011.L2.locktime));
#undef R1011
	}
	str_rstrip_char(buf, ',');
	(void)strlcat(buf, "]", buflen);
	break;

    case 1012:
	str_appendf(buf, buflen,
		       "\"station_id\":%u,\"tow\":%d,\"sync\":\"%s\","
		       "\"smoothing\":\"%s\",\"interval\":\"%u\",",
		       rtcm->rtcmtypes.rtcm3_1012.header.station_id,
		       (int)rtcm->rtcmtypes.rtcm3_1012.header.tow,
		       JSON_BOOL(rtcm->rtcmtypes.rtcm3_1012.header.sync),
		       JSON_BOOL(rtcm->rtcmtypes.rtcm3_1012.header.smoothing),
		       rtcm->rtcmtypes.rtcm3_1012.header.interval);
	(void)strlcat(buf, "\"satellites\":[", buflen);
	for (i = 0; i < rtcm->rtcmtypes.rtcm3_1012.header.satcount; i++) {
#define R1012 rtcm->rtcmtypes.rtcm3_1012.rtk_data[i]
	    str_appendf(buf, buflen,
			   "{\"ident\":%u,\"channel\":%u,"
			   "\"L1\":{\"ind\":%u,\"prange\":%8.2f,"
			   "\"delta\":%6.4f,\"lockt\":%u,\"amb\":%u,"
			   "\"CNR\":%.2f},"
			   "\"L2\":{\"ind\":%u,\"prange\":%8.2f,"
			   "\"delta\":%6.4f,\"lockt\":%u,"
			   "\"CNR\":%.2f},"
			   "},",
			   R1012.ident,
			   R1012.L1.channel,
			   CODE(R1012.L1.indicator),
			   R1012.L1.pseudorange,
			   R1012.L1.rangediff,
			   INT(R1012.L1.locktime),
			   INT(R1012.L1.ambiguity),
			   R1012.L1.CNR,
			   CODE(R1012.L2.indicator),
			   R1012.L2.pseudorange,
			   R1012.L2.rangediff,
			   INT(R1012.L2.locktime),
			   R1012.L2.CNR);
#undef R1012
	}
	str_rstrip_char(buf, ',');
	(void)strlcat(buf, "]", buflen);
	break;

    case 1013:
	str_appendf(buf, buflen,
		       "\"station_id\":%u,\"mjd\":%u,\"sec\":%u,"
		       "\"leapsecs\":%u,",
		       rtcm->rtcmtypes.rtcm3_1013.station_id,
		       rtcm->rtcmtypes.rtcm3_1013.mjd,
		       rtcm->rtcmtypes.rtcm3_1013.sod,
		       INT(rtcm->rtcmtypes.rtcm3_1013.leapsecs));
	for (i = 0; i < (unsigned short)rtcm->rtcmtypes.rtcm3_1013.ncount; i++)
	    str_appendf(buf, buflen,
			   "{\"id\":%u,\"sync\":\"%s\",\"interval\":%u}",
			   rtcm->rtcmtypes.rtcm3_1013.announcements[i].id,
			   JSON_BOOL(rtcm->rtcmtypes.rtcm3_1013.
				announcements[i].sync),
			   rtcm->rtcmtypes.rtcm3_1013.
			   announcements[i].interval);
	break;

    case 1014:
	str_appendf(buf, buflen,
		       "\"netid\":%u,\"subnetid\":%u,\"statcount\":%u"
		       "\"master\":%u,\"aux\":%u,\"lat\":%f,\"lon\":%f,\"alt\":%f,",
		       rtcm->rtcmtypes.rtcm3_1014.network_id,
		       rtcm->rtcmtypes.rtcm3_1014.subnetwork_id,
		       rtcm->rtcmtypes.rtcm3_1014.stationcount,
		       rtcm->rtcmtypes.rtcm3_1014.master_id,
		       rtcm->rtcmtypes.rtcm3_1014.aux_id,
		       rtcm->rtcmtypes.rtcm3_1014.d_lat,
		       rtcm->rtcmtypes.rtcm3_1014.d_lon,
		       rtcm->rtcmtypes.rtcm3_1014.d_alt);
	break;

    case 1015:
	break;

    case 1016:
	break;

    case 1017:
	break;

    case 1018:
	break;

    case 1019:
	break;

    case 1020:
	break;

    case 1029:
	/*@-formatcode@*//* splint has a bug */
	str_appendf(buf, buflen,
		       "\"station_id\":%u,\"mjd\":%u,\"sec\":%u,"
		       "\"len\":%zd,\"units\":%zd,\"msg\":\"%s\",",
		       rtcm->rtcmtypes.rtcm3_1029.station_id,
		       rtcm->rtcmtypes.rtcm3_1029.mjd,
		       rtcm->rtcmtypes.rtcm3_1029.sod,
		       rtcm->rtcmtypes.rtcm3_1029.len,
		       rtcm->rtcmtypes.rtcm3_1029.unicode_units,
		       json_stringify(buf1, sizeof(buf1),
				      (char *)rtcm->rtcmtypes.rtcm3_1029.text));
	/*@+formatcode@*/
	break;

    case 1033:
	str_appendf(buf, buflen,
		       "\"station_id\":%u,\"desc\":\"%s\","
		       "\"setup_id\":%u,\"serial\":\"%s\","
		       "\"receiver\":%s,\"firmware\":\"%s\"",
		       rtcm->rtcmtypes.rtcm3_1033.station_id,
		       rtcm->rtcmtypes.rtcm3_1033.descriptor,
		       INT(rtcm->rtcmtypes.rtcm3_1033.setup_id),
		       rtcm->rtcmtypes.rtcm3_1033.serial,
		       rtcm->rtcmtypes.rtcm3_1033.receiver,
		       rtcm->rtcmtypes.rtcm3_1033.firmware);
	break;

    default:
	(void)strlcat(buf, "\"data\":[", buflen);
	for (n = 0; n < rtcm->length; n++)
	    str_appendf(buf, buflen,
			   "\"0x%02x\",",(unsigned int)rtcm->rtcmtypes.data[n]);
	str_rstrip_char(buf, ',');
	(void)strlcat(buf, "]", buflen);
	break;
    }

    str_rstrip_char(buf, ',');
    (void)strlcat(buf, "}\r\n", buflen);
    /*@+mustfreefresh@*/
#undef CODE
#undef INT
}
#endif /* defined(RTCM104V3_ENABLE) */

#if defined(AIVDM_ENABLE)
void json_aivdm_dump(const struct ais_t *ais,
		     /*@null@*/const char *device, bool scaled,
		     /*@out@*/char *buf, size_t buflen)
{
    char buf1[JSON_VAL_MAX * 2 + 1];
    char buf2[JSON_VAL_MAX * 2 + 1];
    char buf3[JSON_VAL_MAX * 2 + 1];
    char scratchbuf[MAX_PACKET_LENGTH*2+1];
    int i;

    static char *nav_legends[] = {
	"Under way using engine",
	"At anchor",
	"Not under command",
	"Restricted manoeuverability",
	"Constrained by her draught",
	"Moored",
	"Aground",
	"Engaged in fishing",
	"Under way sailing",
	"Reserved for HSC",
	"Reserved for WIG",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"Not defined",
    };

    static char *epfd_legends[] = {
	"Undefined",
	"GPS",
	"GLONASS",
	"Combined GPS/GLONASS",
	"Loran-C",
	"Chayka",
	"Integrated navigation system",
	"Surveyed",
	"Galileo",
    };

#define EPFD_DISPLAY(n) (((n) < (unsigned int)NITEMS(epfd_legends)) ? epfd_legends[n] : "INVALID EPFD")

    static char *ship_type_legends[100] = {
	"Not available",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
	"Wing in ground (WIG) - all ships of this type",
	"Wing in ground (WIG) - Hazardous category A",
	"Wing in ground (WIG) - Hazardous category B",
	"Wing in ground (WIG) - Hazardous category C",
	"Wing in ground (WIG) - Hazardous category D",
	"Wing in ground (WIG) - Reserved for future use",
	"Wing in ground (WIG) - Reserved for future use",
	"Wing in ground (WIG) - Reserved for future use",
	"Wing in ground (WIG) - Reserved for future use",
	"Wing in ground (WIG) - Reserved for future use",
	"Fishing",
	"Towing",
	"Towing: length exceeds 200m or breadth exceeds 25m",
	"Dredging or underwater ops",
	"Diving ops",
	"Military ops",
	"Sailing",
	"Pleasure Craft",
	"Reserved",
	"Reserved",
	"High speed craft (HSC) - all ships of this type",
	"High speed craft (HSC) - Hazardous category A",
	"High speed craft (HSC) - Hazardous category B",
	"High speed craft (HSC) - Hazardous category C",
	"High speed craft (HSC) - Hazardous category D",
	"High speed craft (HSC) - Reserved for future use",
	"High speed craft (HSC) - Reserved for future use",
	"High speed craft (HSC) - Reserved for future use",
	"High speed craft (HSC) - Reserved for future use",
	"High speed craft (HSC) - No additional information",
	"Pilot Vessel",
	"Search and Rescue vessel",
	"Tug",
	"Port Tender",
	"Anti-pollution equipment",
	"Law Enforcement",
	"Spare - Local Vessel",
	"Spare - Local Vessel",
	"Medical Transport",
	"Ship according to RR Resolution No. 18",
	"Passenger - all ships of this type",
	"Passenger - Hazardous category A",
	"Passenger - Hazardous category B",
	"Passenger - Hazardous category C",
	"Passenger - Hazardous category D",
	"Passenger - Reserved for future use",
	"Passenger - Reserved for future use",
	"Passenger - Reserved for future use",
	"Passenger - Reserved for future use",
	"Passenger - No additional information",
	"Cargo - all ships of this type",
	"Cargo - Hazardous category A",
	"Cargo - Hazardous category B",
	"Cargo - Hazardous category C",
	"Cargo - Hazardous category D",
	"Cargo - Reserved for future use",
	"Cargo - Reserved for future use",
	"Cargo - Reserved for future use",
	"Cargo - Reserved for future use",
	"Cargo - No additional information",
	"Tanker - all ships of this type",
	"Tanker - Hazardous category A",
	"Tanker - Hazardous category B",
	"Tanker - Hazardous category C",
	"Tanker - Hazardous category D",
	"Tanker - Reserved for future use",
	"Tanker - Reserved for future use",
	"Tanker - Reserved for future use",
	"Tanker - Reserved for future use",
	"Tanker - No additional information",
	"Other Type - all ships of this type",
	"Other Type - Hazardous category A",
	"Other Type - Hazardous category B",
	"Other Type - Hazardous category C",
	"Other Type - Hazardous category D",
	"Other Type - Reserved for future use",
	"Other Type - Reserved for future use",
	"Other Type - Reserved for future use",
	"Other Type - Reserved for future use",
	"Other Type - no additional information",
    };

#define SHIPTYPE_DISPLAY(n) (((n) < (unsigned int)NITEMS(ship_type_legends)) ? ship_type_legends[n] : "INVALID SHIP TYPE")

    static const char *station_type_legends[] = {
	"All types of mobiles",
	"Reserved for future use",
	"All types of Class B mobile stations",
	"SAR airborne mobile station",
	"Aid to Navigation station",
	"Class B shipborne mobile station",
	"Regional use and inland waterways",
	"Regional use and inland waterways",
	"Regional use and inland waterways",
	"Regional use and inland waterways",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
    };

#define STATIONTYPE_DISPLAY(n) (((n) < (unsigned int)NITEMS(station_type_legends)) ? station_type_legends[n] : "INVALID STATION TYPE")

    static const char *navaid_type_legends[] = {
	"Unspecified",
	"Reference point",
	"RACON",
	"Fixed offshore structure",
	"Spare, Reserved for future use.",
	"Light, without sectors",
	"Light, with sectors",
	"Leading Light Front",
	"Leading Light Rear",
	"Beacon, Cardinal N",
	"Beacon, Cardinal E",
	"Beacon, Cardinal S",
	"Beacon, Cardinal W",
	"Beacon, Port hand",
	"Beacon, Starboard hand",
	"Beacon, Preferred Channel port hand",
	"Beacon, Preferred Channel starboard hand",
	"Beacon, Isolated danger",
	"Beacon, Safe water",
	"Beacon, Special mark",
	"Cardinal Mark N",
	"Cardinal Mark E",
	"Cardinal Mark S",
	"Cardinal Mark W",
	"Port hand Mark",
	"Starboard hand Mark",
	"Preferred Channel Port hand",
	"Preferred Channel Starboard hand",
	"Isolated danger",
	"Safe Water",
	"Special Mark",
	"Light Vessel / LANBY / Rigs",
    };

#define NAVAIDTYPE_DISPLAY(n) (((n) < (unsigned int)NITEMS(navaid_type_legends[0])) ? navaid_type_legends[n] : "INVALID NAVAID TYPE")

    // cppcheck-suppress variableScope
    static const char *signal_legends[] = {
	"N/A",
	"Serious emergency – stop or divert according to instructions.",
	"Vessels shall not proceed.",
	"Vessels may proceed. One way traffic.",
	"Vessels may proceed. Two way traffic.",
	"Vessels shall proceed on specific orders only.",
	"Vessels in main channel shall not proceed."
	"Vessels in main channel shall proceed on specific orders only.",
	"Vessels in main channel shall proceed on specific orders only.",
	"I = \"in-bound\" only acceptable.",
	"O = \"out-bound\" only acceptable.",
	"F = both \"in- and out-bound\" acceptable.",
	"XI = Code will shift to \"I\" in due time.",
	"XO = Code will shift to \"O\" in due time.",
	"X = Vessels shall proceed only on direction.",
    };

#define SIGNAL_DISPLAY(n) (((n) < (unsigned int)NITEMS(signal_legends[0])) ? signal_legends[n] : "INVALID SIGNAL TYPE")

    static const char *route_type[32] = {
	"Undefined (default)",
	"Mandatory",
	"Recommended",
	"Alternative",
	"Recommended route through ice",
	"Ship route plan",
	"Reserved for future use.",
	"Reserved for future use.",
	"Reserved for future use.",
	"Reserved for future use.",
	"Reserved for future use.",
	"Reserved for future use.",
	"Reserved for future use.",
	"Reserved for future use.",
	"Reserved for future use.",
	"Reserved for future use.",
	"Reserved for future use.",
	"Reserved for future use.",
	"Reserved for future use.",
	"Reserved for future use.",
	"Reserved for future use.",
	"Reserved for future use.",
	"Reserved for future use.",
	"Reserved for future use.",
	"Reserved for future use.",
	"Reserved for future use.",
	"Reserved for future use.",
	"Reserved for future use.",
	"Reserved for future use.",
	"Reserved for future use.",
	"Reserved for future use.",
	"Cancel route identified by message linkage",
    };

    // cppcheck-suppress variableScope
    static const char *idtypes[] = {
	"mmsi",
	"imo",
	"callsign",
	"other",
    };

    // cppcheck-suppress variableScope
    static const char *racon_status[] = {
	"No RACON installed",
	"RACON not monitored",
	"RACON operational",
	"RACON ERROR"
    };

    // cppcheck-suppress variableScope
    static const char *light_status[] = {
	"No light or no monitoring",
	"Light ON",
	"Light OFF",
	"Light ERROR"
    };

    // cppcheck-suppress variableScope
    static const char *rta_status[] = {
	"Operational",
	"Limited operation",
	"Out of order",
	"N/A",
    };

    // cppcheck-suppress variableScope
    const char *position_types[8] = {
	"Not available",
	"Port-side to",
	"Starboard-side to",
	"Mediterranean (end-on) mooring",
	"Mooring buoy",
	"Anchorage",
	"Reserved for future use",
	"Reserved for future use",
    };

    (void)snprintf(buf, buflen, "{\"class\":\"AIS\",");
    if (device != NULL && device[0] != '\0')
	str_appendf(buf, buflen, "\"device\":\"%s\",", device);
    str_appendf(buf, buflen,
		   "\"type\":%u,\"repeat\":%u,\"mmsi\":%u,\"scaled\":%s,",
		   ais->type, ais->repeat, ais->mmsi, JSON_BOOL(scaled));
    /*@ -formatcode -mustfreefresh @*/
    switch (ais->type) {
    case 1:			/* Position Report */
    case 2:
    case 3:
	if (scaled) {
	    char turnlegend[20];
	    char speedlegend[20];

	    /*
	     * Express turn as nan if not available,
	     * "fastleft"/"fastright" for fast turns.
	     */
	    if (ais->type1.turn == -128)
		(void)strlcpy(turnlegend, "\"nan\"", sizeof(turnlegend));
	    else if (ais->type1.turn == -127)
		(void)strlcpy(turnlegend, "\"fastleft\"", sizeof(turnlegend));
	    else if (ais->type1.turn == 127)
		(void)strlcpy(turnlegend, "\"fastright\"",
			      sizeof(turnlegend));
	    else {
		double rot1 = ais->type1.turn / 4.733;
		(void)snprintf(turnlegend, sizeof(turnlegend),
			       "%.0f", rot1 * rot1);
	    }

	    /*
	     * Express speed as nan if not available,
	     * "fast" for fast movers.
	     */
	    if (ais->type1.speed == AIS_SPEED_NOT_AVAILABLE)
		(void)strlcpy(speedlegend, "\"nan\"", sizeof(speedlegend));
	    else if (ais->type1.speed == AIS_SPEED_FAST_MOVER)
		(void)strlcpy(speedlegend, "\"fast\"", sizeof(speedlegend));
	    else
		(void)snprintf(speedlegend, sizeof(speedlegend),
			       "%.1f", ais->type1.speed / 10.0);

	    str_appendf(buf, buflen,
			   "\"status\":%u,\"status_text\":\"%s\","
			   "\"turn\":%s,\"speed\":%s,"
			   "\"accuracy\":%s,\"lon\":%.4f,\"lat\":%.4f,"
			   "\"course\":%.1f,\"heading\":%u,\"second\":%u,"
			   "\"maneuver\":%u,\"raim\":%s,\"radio\":%u}\r\n",
			   ais->type1.status,
			   nav_legends[ais->type1.status],
			   turnlegend,
			   speedlegend,
			   JSON_BOOL(ais->type1.accuracy),
			   ais->type1.lon / AIS_LATLON_DIV,
			   ais->type1.lat / AIS_LATLON_DIV,
			   ais->type1.course / 10.0,
			   ais->type1.heading,
			   ais->type1.second,
			   ais->type1.maneuver,
			   JSON_BOOL(ais->type1.raim), ais->type1.radio);
	} else {
	    str_appendf(buf, buflen,
			   "\"status\":%u,\"status_text\":\"%s\","
			   "\"turn\":%d,\"speed\":%u,"
			   "\"accuracy\":%s,\"lon\":%d,\"lat\":%d,"
			   "\"course\":%u,\"heading\":%u,\"second\":%u,"
			   "\"maneuver\":%u,\"raim\":%s,\"radio\":%u}\r\n",
			   ais->type1.status,
			   nav_legends[ais->type1.status],
			   ais->type1.turn,
			   ais->type1.speed,
			   JSON_BOOL(ais->type1.accuracy),
			   ais->type1.lon,
			   ais->type1.lat,
			   ais->type1.course,
			   ais->type1.heading,
			   ais->type1.second,
			   ais->type1.maneuver,
			   JSON_BOOL(ais->type1.raim), ais->type1.radio);
	}
	break;
    case 4:			/* Base Station Report */
    case 11:			/* UTC/Date Response */
	/* some fields have beem merged to an ISO8601 date */
	if (scaled) {
	    // The use of %u instead of %04u for the year is to allow
	    // out-of-band year values.
	    str_appendf(buf, buflen,
			   "\"timestamp\":\"%04u-%02u-%02uT%02u:%02u:%02uZ\","
			   "\"accuracy\":%s,\"lon\":%.4f,\"lat\":%.4f,"
			   "\"epfd\":%u,\"epfd_text\":\"%s\","
			   "\"raim\":%s,\"radio\":%u}\r\n",
			   ais->type4.year,
			   ais->type4.month,
			   ais->type4.day,
			   ais->type4.hour,
			   ais->type4.minute,
			   ais->type4.second,
			   JSON_BOOL(ais->type4.accuracy),
			   ais->type4.lon / AIS_LATLON_DIV,
			   ais->type4.lat / AIS_LATLON_DIV,
			   ais->type4.epfd,
			   EPFD_DISPLAY(ais->type4.epfd),
			   JSON_BOOL(ais->type4.raim), ais->type4.radio);
	} else {
	    str_appendf(buf, buflen,
			   "\"timestamp\":\"%04u-%02u-%02uT%02u:%02u:%02uZ\","
			   "\"accuracy\":%s,\"lon\":%d,\"lat\":%d,"
			   "\"epfd\":%u,\"epfd_text\":\"%s\","
			   "\"raim\":%s,\"radio\":%u}\r\n",
			   ais->type4.year,
			   ais->type4.month,
			   ais->type4.day,
			   ais->type4.hour,
			   ais->type4.minute,
			   ais->type4.second,
			   JSON_BOOL(ais->type4.accuracy),
			   ais->type4.lon,
			   ais->type4.lat,
			   ais->type4.epfd,
			   EPFD_DISPLAY(ais->type4.epfd),
			   JSON_BOOL(ais->type4.raim), ais->type4.radio);
	}
	break;
    case 5:			/* Ship static and voyage related data */
	/* some fields have beem merged to an ISO8601 partial date */
	if (scaled) {
            /* *INDENT-OFF* */
	    str_appendf(buf, buflen,
			   "\"imo\":%u,\"ais_version\":%u,\"callsign\":\"%s\","
			   "\"shipname\":\"%s\","
			   "\"shiptype\":%u,\"shiptype_text\":\"%s\","
			   "\"to_bow\":%u,\"to_stern\":%u,\"to_port\":%u,"
			   "\"to_starboard\":%u,"
			   "\"epfd\":%u,\"epfd_text\":\"%s\","
			   "\"eta\":\"%02u-%02uT%02u:%02uZ\","
			   "\"draught\":%.1f,\"destination\":\"%s\","
			   "\"dte\":%u}\r\n",
			   ais->type5.imo,
			   ais->type5.ais_version,
			   json_stringify(buf1, sizeof(buf1),
					  ais->type5.callsign),
			   json_stringify(buf2, sizeof(buf2),
					  ais->type5.shipname),
			   ais->type5.shiptype,
			   SHIPTYPE_DISPLAY(ais->type5.shiptype),
			   ais->type5.to_bow, ais->type5.to_stern,
			   ais->type5.to_port, ais->type5.to_starboard,
			   ais->type5.epfd,
			   EPFD_DISPLAY(ais->type5.epfd),
			   ais->type5.month,
			   ais->type5.day,
			   ais->type5.hour, ais->type5.minute,
			   ais->type5.draught / 10.0,
			   json_stringify(buf3, sizeof(buf3),
					  ais->type5.destination),
			   ais->type5.dte);
            /* *INDENT-ON* */
	} else {
	    str_appendf(buf, buflen,
			   "\"imo\":%u,\"ais_version\":%u,\"callsign\":\"%s\","
			   "\"shipname\":\"%s\","
			   "\"shiptype\":%u,\"shiptype_text\":\"%s\","
			   "\"to_bow\":%u,\"to_stern\":%u,\"to_port\":%u,"
			   "\"to_starboard\":%u,"
			   "\"epfd\":%u,\"epfd_text\":\"%s\","
			   "\"eta\":\"%02u-%02uT%02u:%02uZ\","
			   "\"draught\":%u,\"destination\":\"%s\","
			   "\"dte\":%u}\r\n",
			   ais->type5.imo,
			   ais->type5.ais_version,
			   json_stringify(buf1, sizeof(buf1),
					  ais->type5.callsign),
			   json_stringify(buf2, sizeof(buf2),
					  ais->type5.shipname),
			   ais->type5.shiptype,
			   SHIPTYPE_DISPLAY(ais->type5.shiptype),
			   ais->type5.to_bow,
			   ais->type5.to_stern,
			   ais->type5.to_port,
			   ais->type5.to_starboard,
			   ais->type5.epfd,
			   EPFD_DISPLAY(ais->type5.epfd),
			   ais->type5.month,
			   ais->type5.day,
			   ais->type5.hour,
			   ais->type5.minute,
			   ais->type5.draught,
			   json_stringify(buf3, sizeof(buf3),
					  ais->type5.destination),
			   ais->type5.dte);
	}
	break;
    case 6:			/* Binary Message */
	str_appendf(buf, buflen,
		       "\"seqno\":%u,\"dest_mmsi\":%u,"
		       "\"retransmit\":%s,\"dac\":%u,\"fid\":%u,",
		       ais->type6.seqno,
		       ais->type6.dest_mmsi,
		       JSON_BOOL(ais->type6.retransmit),
		       ais->type6.dac,
		       ais->type6.fid);
	if (!ais->type6.structured) {
	    str_appendf(buf, buflen,
			   "\"data\":\"%zd:%s\"}\r\n",
			   ais->type6.bitcount,
			   json_stringify(buf1, sizeof(buf1),
					  gpsd_hexdump(scratchbuf, sizeof(scratchbuf),
						       (char *)ais->type6.bitdata,
						       BITS_TO_BYTES(ais->type6.bitcount))));
	    break;
	}
	if (ais->type6.dac == 200) {
	    switch (ais->type6.fid) {
	    case 21:
		str_appendf(buf, buflen,
			       "\"country\":\"%s\",\"locode\":\"%s\",\"section\":\"%s\",\"terminal\":\"%s\",\"hectometre\":\"%s\",\"eta\":\"%u-%uT%u:%u\",\"tugs\":%u,\"airdraught\":%u}",
		    ais->type6.dac200fid21.country,
		    ais->type6.dac200fid21.locode,
		    ais->type6.dac200fid21.section,
		    ais->type6.dac200fid21.terminal,
		    ais->type6.dac200fid21.hectometre,
		    ais->type6.dac200fid21.month,
		    ais->type6.dac200fid21.day,
		    ais->type6.dac200fid21.hour,
		    ais->type6.dac200fid21.minute,
		    ais->type6.dac200fid21.tugs,
		    ais->type6.dac200fid21.airdraught);
		break;
	    case 22:
		str_appendf(buf, buflen,
			       "\"country\":\"%s\",\"locode\":\"%s\","
			       "\"section\":\"%s\","
			       "\"terminal\":\"%s\",\"hectometre\":\"%s\","
			       "\"eta\":\"%u-%uT%u:%u\","
			       "\"status\":%u,\"status_text\":\"%s\"}",
			       ais->type6.dac200fid22.country,
			       ais->type6.dac200fid22.locode,
			       ais->type6.dac200fid22.section,
			       ais->type6.dac200fid22.terminal,
			       ais->type6.dac200fid22.hectometre,
			       ais->type6.dac200fid22.month,
			       ais->type6.dac200fid22.day,
			       ais->type6.dac200fid22.hour,
			       ais->type6.dac200fid22.minute,
			       ais->type6.dac200fid22.status,
			       rta_status[ais->type6.dac200fid22.status]);
		break;
	    case 55:
		str_appendf(buf, buflen,
		    "\"crew\":%u,\"passengers\":%u,\"personnel\":%u}",

		    ais->type6.dac200fid55.crew,
		    ais->type6.dac200fid55.passengers,
		    ais->type6.dac200fid55.personnel);
		break;
	    }
	}
	else if (ais->type6.dac == 235 || ais->type6.dac == 250) {
	    switch (ais->type6.fid) {
	    case 10:	/* GLA - AtoN monitoring data */
		str_appendf(buf, buflen,
			       "\"off_pos\":%s,\"alarm\":%s,"
			       "\"stat_ext\":%u,",
			       JSON_BOOL(ais->type6.dac235fid10.off_pos),
			       JSON_BOOL(ais->type6.dac235fid10.alarm),
			       ais->type6.dac235fid10.stat_ext);
		if (scaled && ais->type6.dac235fid10.ana_int != 0)
		    str_appendf(buf, buflen,
				   "\"ana_int\":%.2f,",
				   ais->type6.dac235fid10.ana_int*0.05);
		else
		    str_appendf(buf, buflen,
				   "\"ana_int\":%u,",
				   ais->type6.dac235fid10.ana_int);
		if (scaled && ais->type6.dac235fid10.ana_ext1 != 0)
		    str_appendf(buf, buflen,
				   "\"ana_ext1\":%.2f,",
				   ais->type6.dac235fid10.ana_ext1*0.05);
		else
		    str_appendf(buf, buflen,
				   "\"ana_ext1\":%u,",
				   ais->type6.dac235fid10.ana_ext1);
		if (scaled && ais->type6.dac235fid10.ana_ext2 != 0)
		    str_appendf(buf, buflen,
				   "\"ana_ext2\":%.2f,",
				   ais->type6.dac235fid10.ana_ext2*0.05);
		else
		    str_appendf(buf, buflen,
				   "\"ana_ext2\":%u,",
				   ais->type6.dac235fid10.ana_ext2);
		str_appendf(buf, buflen,
			       "\"racon\":%u,"
			       "\"racon_text\":\"%s\","
			       "\"light\":%u,"
			       "\"light_text\":\"%s\"",
			       ais->type6.dac235fid10.racon,
			       racon_status[ais->type6.dac235fid10.racon],
			       ais->type6.dac235fid10.light,
			       light_status[ais->type6.dac235fid10.light]);
		str_rstrip_char(buf, ',');
		(void)strlcat(buf, "}\r\n", buflen);
		break;
	    }
	}
	else if (ais->type6.dac == 1) {
	    char buf4[JSON_VAL_MAX * 2 + 1];
	    switch (ais->type6.fid) {
	    case 12:	/* IMO236 -Dangerous cargo indication */
		/* some fields have beem merged to an ISO8601 partial date */
		str_appendf(buf, buflen,
			       "\"lastport\":\"%s\",\"departure\":\"%02u-%02uT%02u:%02uZ\","
			       "\"nextport\":\"%s\",\"eta\":\"%02u-%02uT%02u:%02uZ\","
			       "\"dangerous\":\"%s\",\"imdcat\":\"%s\","
			       "\"unid\":%u,\"amount\":%u,\"unit\":%u}\r\n",
			       json_stringify(buf1, sizeof(buf1),
					      ais->type6.dac1fid12.lastport),
			       ais->type6.dac1fid12.lmonth,
			       ais->type6.dac1fid12.lday,
			       ais->type6.dac1fid12.lhour,
			       ais->type6.dac1fid12.lminute,
			       json_stringify(buf2, sizeof(buf2),
					      ais->type6.dac1fid12.nextport),
			       ais->type6.dac1fid12.nmonth,
			       ais->type6.dac1fid12.nday,
			       ais->type6.dac1fid12.nhour,
			       ais->type6.dac1fid12.nminute,
			       json_stringify(buf3, sizeof(buf3),
					      ais->type6.dac1fid12.dangerous),
			       json_stringify(buf4, sizeof(buf4),
					      ais->type6.dac1fid12.imdcat),
			       ais->type6.dac1fid12.unid,
			       ais->type6.dac1fid12.amount,
			       ais->type6.dac1fid12.unit);
		break;
	    case 15:	/* IMO236 - Extended Ship Static and Voyage Related Data */
		str_appendf(buf, buflen,
		    "\"airdraught\":%u}\r\n",
		    ais->type6.dac1fid15.airdraught);
		break;
	    case 16:	/* IMO236 - Number of persons on board */
		str_appendf(buf, buflen,
			       "\"persons\":%u}\t\n", ais->type6.dac1fid16.persons);
		break;
	    case 18:	/* IMO289 - Clearance time to enter port */
		str_appendf(buf, buflen,
			       "\"linkage\":%u,\"arrival\":\"%02u-%02uT%02u:%02uZ\",\"portname\":\"%s\",\"destination\":\"%s\",",
			       ais->type6.dac1fid18.linkage,
			       ais->type6.dac1fid18.month,
			       ais->type6.dac1fid18.day,
			       ais->type6.dac1fid18.hour,
			       ais->type6.dac1fid18.minute,
			       json_stringify(buf1, sizeof(buf1),
					      ais->type6.dac1fid18.portname),
			       json_stringify(buf2, sizeof(buf2),
					      ais->type6.dac1fid18.destination));
		if (scaled)
		    str_appendf(buf, buflen,
				   "\"lon\":%.3f,\"lat\":%.3f}\r\n",
				   ais->type6.dac1fid18.lon/AIS_LATLON3_DIV,
				   ais->type6.dac1fid18.lat/AIS_LATLON3_DIV);
		else
		    str_appendf(buf, buflen,
			       "\"lon\":%d,\"lat\":%d}\r\n",
			       ais->type6.dac1fid18.lon,
			       ais->type6.dac1fid18.lat);
		break;
	    case 20:        /* IMO289 - Berthing Data */
                str_appendf(buf, buflen,
			       "\"linkage\":%u,\"berth_length\":%u,"
			       "\"position\":%u,\"position_text\":\"%s\","
			       "\"arrival\":\"%u-%uT%u:%u\","
			       "\"availability\":%u,"
			       "\"agent\":%u,\"fuel\":%u,\"chandler\":%u,"
			       "\"stevedore\":%u,\"electrical\":%u,"
			       "\"water\":%u,\"customs\":%u,\"cartage\":%u,"
			       "\"crane\":%u,\"lift\":%u,\"medical\":%u,"
			       "\"navrepair\":%u,\"provisions\":%u,"
			       "\"shiprepair\":%u,\"surveyor\":%u,"
			       "\"steam\":%u,\"tugs\":%u,\"solidwaste\":%u,"
			       "\"liquidwaste\":%u,\"hazardouswaste\":%u,"
			       "\"ballast\":%u,\"additional\":%u,"
			       "\"regional1\":%u,\"regional2\":%u,"
			       "\"future1\":%u,\"future2\":%u,"
			       "\"berth_name\":\"%s\",",
			       ais->type6.dac1fid20.linkage,
			       ais->type6.dac1fid20.berth_length,
			       ais->type6.dac1fid20.position,
			       position_types[ais->type6.dac1fid20.position],
			       ais->type6.dac1fid20.month,
			       ais->type6.dac1fid20.day,
			       ais->type6.dac1fid20.hour,
			       ais->type6.dac1fid20.minute,
			       ais->type6.dac1fid20.availability,
			       ais->type6.dac1fid20.agent,
			       ais->type6.dac1fid20.fuel,
			       ais->type6.dac1fid20.chandler,
			       ais->type6.dac1fid20.stevedore,
			       ais->type6.dac1fid20.electrical,
			       ais->type6.dac1fid20.water,
			       ais->type6.dac1fid20.customs,
			       ais->type6.dac1fid20.cartage,
			       ais->type6.dac1fid20.crane,
			       ais->type6.dac1fid20.lift,
			       ais->type6.dac1fid20.medical,
			       ais->type6.dac1fid20.navrepair,
			       ais->type6.dac1fid20.provisions,
			       ais->type6.dac1fid20.shiprepair,
			       ais->type6.dac1fid20.surveyor,
			       ais->type6.dac1fid20.steam,
			       ais->type6.dac1fid20.tugs,
			       ais->type6.dac1fid20.solidwaste,
			       ais->type6.dac1fid20.liquidwaste,
			       ais->type6.dac1fid20.hazardouswaste,
			       ais->type6.dac1fid20.ballast,
			       ais->type6.dac1fid20.additional,
			       ais->type6.dac1fid20.regional1,
			       ais->type6.dac1fid20.regional2,
			       ais->type6.dac1fid20.future1,
			       ais->type6.dac1fid20.future2,
			       json_stringify(buf1, sizeof(buf1),
					      ais->type6.dac1fid20.berth_name));
            if (scaled)
		str_appendf(buf, buflen,
			       "\"berth_lon\":%.3f,"
			       "\"berth_lat\":%.3f,"
			       "\"berth_depth\":%.1f}\r\n",
			       ais->type6.dac1fid20.berth_lon / AIS_LATLON3_DIV,
			       ais->type6.dac1fid20.berth_lat / AIS_LATLON3_DIV,
			       ais->type6.dac1fid20.berth_depth * 0.1);
            else
                str_appendf(buf, buflen,
			       "\"berth_lon\":%d,"
			       "\"berth_lat\":%d,"
			       "\"berth_depth\":%u}\r\n",
			       ais->type6.dac1fid20.berth_lon,
			       ais->type6.dac1fid20.berth_lat,
			       ais->type6.dac1fid20.berth_depth);
		break;
	    case 23:    /* IMO289 - Area notice - addressed */
		break;
	    case 25:	/* IMO289 - Dangerous cargo indication */
		str_appendf(buf, buflen,
			       "\"unit\":%u,\"amount\":%u,\"cargos\":[",
			       ais->type6.dac1fid25.unit,
			       ais->type6.dac1fid25.amount);
		for (i = 0; i < (int)ais->type6.dac1fid25.ncargos; i++)
		    str_appendf(buf, buflen,
				   "{\"code\":%u,\"subtype\":%u},",

				   ais->type6.dac1fid25.cargos[i].code,
				   ais->type6.dac1fid25.cargos[i].subtype);
		str_rstrip_char(buf, ',');
		(void)strlcat(buf, "]}\r\n", buflen);
		break;
	    case 28:	/* IMO289 - Route info - addressed */
		str_appendf(buf, buflen,
			       "\"linkage\":%u,\"sender\":%u,"
			       "\"rtype\":%u,"
			       "\"rtype_text\":\"%s\","
			       "\"start\":\"%02u-%02uT%02u:%02uZ\","
			       "\"duration\":%u,\"waypoints\":[",
			       ais->type6.dac1fid28.linkage,
			       ais->type6.dac1fid28.sender,
			       ais->type6.dac1fid28.rtype,
			       route_type[ais->type6.dac1fid28.rtype],
			       ais->type6.dac1fid28.month,
			       ais->type6.dac1fid28.day,
			       ais->type6.dac1fid28.hour,
			       ais->type6.dac1fid28.minute,
			       ais->type6.dac1fid28.duration);
		for (i = 0; i < ais->type6.dac1fid28.waycount; i++) {
		    if (scaled)
			str_appendf(buf, buflen,
			    "{\"lon\":%.4f,\"lat\":%.4f},",
			    ais->type6.dac1fid28.waypoints[i].lon / AIS_LATLON4_DIV,
			    ais->type6.dac1fid28.waypoints[i].lat / AIS_LATLON4_DIV);
		    else
			str_appendf(buf, buflen,
			    "{\"lon\":%d,\"lat\":%d},",
			    ais->type6.dac1fid28.waypoints[i].lon,
			    ais->type6.dac1fid28.waypoints[i].lat);
		}
		str_rstrip_char(buf, ',');
		(void)strlcat(buf, "]}\r\n", buflen);
		break;
	    case 30:	/* IMO289 - Text description - addressed */
		str_appendf(buf, buflen,
		       "\"linkage\":%u,\"text\":\"%s\"}\r\n",
		       ais->type6.dac1fid30.linkage,
		       json_stringify(buf1, sizeof(buf1),
				      ais->type6.dac1fid30.text));
		break;
	    case 14:	/* IMO236 - Tidal Window */
	    case 32:	/* IMO289 - Tidal Window */
	      str_appendf(buf, buflen,
		  "\"month\":%u,\"day\":%u,\"tidals\":[",
		  ais->type6.dac1fid32.month,
		  ais->type6.dac1fid32.day);
	      for (i = 0; i < ais->type6.dac1fid32.ntidals; i++) {
		  const struct tidal_t *tp =  &ais->type6.dac1fid32.tidals[i];
		  if (scaled)
		      str_appendf(buf, buflen,
			  "{\"lon\":%.3f,\"lat\":%.3f,",
			  tp->lon / AIS_LATLON3_DIV,
			  tp->lat / AIS_LATLON3_DIV);
		  else
		      str_appendf(buf, buflen,
			  "{\"lon\":%d,\"lat\":%d,",
			  tp->lon,
			  tp->lat);
		  str_appendf(buf, buflen,
		      "\"from_hour\":%u,\"from_min\":%u,\"to_hour\":%u,\"to_min\":%u,\"cdir\":%u,",
		      tp->from_hour,
		      tp->from_min,
		      tp->to_hour,
		      tp->to_min,
		      tp->cdir);
		  if (scaled)
		      str_appendf(buf, buflen,
			  "\"cspeed\":%.1f},",
			  tp->cspeed / 10.0);
		  else
		      str_appendf(buf, buflen,
			  "\"cspeed\":%u},",
			  tp->cspeed);
	      }
	      str_rstrip_char(buf, ',');
	      (void)strlcat(buf, "]}\r\n", buflen);
	      break;
	    }
	}
	break;
    case 7:			/* Binary Acknowledge */
    case 13:			/* Safety Related Acknowledge */
	str_appendf(buf, buflen,
		       "\"mmsi1\":%u,\"mmsi2\":%u,\"mmsi3\":%u,\"mmsi4\":%u}\r\n",
		       ais->type7.mmsi1,
		       ais->type7.mmsi2, ais->type7.mmsi3, ais->type7.mmsi4);
	break;
    case 8:			/* Binary Broadcast Message */
	str_appendf(buf, buflen,
		       "\"dac\":%u,\"fid\":%u,",ais->type8.dac, ais->type8.fid);
	if (!ais->type8.structured) {
	    str_appendf(buf, buflen,
			   "\"data\":\"%zd:%s\"}\r\n",
			   ais->type8.bitcount,
			   json_stringify(buf1, sizeof(buf1),
					  gpsd_hexdump(scratchbuf, sizeof(scratchbuf), 
						       (char *)ais->type8.bitdata,
						       BITS_TO_BYTES(ais->type8.bitcount))));
	    break;
	}
	if (ais->type8.dac == 1) {
	    const char *trends[] = {
		"steady",
		"increasing",
		"decreasing",
		"N/A",
	    };
	    // WMO 306, Code table 4.201
	    const char *preciptypes[] = {
		"reserved",
		"rain",
		"thunderstorm",
		"freezing rain",
		"mixed/ice",
		"snow",
		"reserved",
		"N/A",
	    };
	    const char *ice[] = {
		"no",
		"yes",
		"reserved",
		"N/A",
	    };
	    switch (ais->type8.fid) {
	    case 11:        /* IMO236 - Meteorological/Hydrological data */
		/* some fields have been merged to an ISO8601 partial date */
		/* layout is almost identical to FID=31 from IMO289 */
		if (scaled)
		    str_appendf(buf, buflen,
				   "\"lat\":%.3f,\"lon\":%.3f,",
				   ais->type8.dac1fid11.lat / AIS_LATLON3_DIV,
				   ais->type8.dac1fid11.lon / AIS_LATLON3_DIV);
		else
		    str_appendf(buf, buflen,
				   "\"lat\":%d,\"lon\":%d,",
				   ais->type8.dac1fid11.lat,
				   ais->type8.dac1fid11.lon);
		str_appendf(buf, buflen,
			       "\"timestamp\":\"%02uT%02u:%02uZ\","
			       "\"wspeed\":%u,\"wgust\":%u,\"wdir\":%u,"
			       "\"wgustdir\":%u,\"humidity\":%u,",
			       ais->type8.dac1fid11.day,
			       ais->type8.dac1fid11.hour,
			       ais->type8.dac1fid11.minute,
			       ais->type8.dac1fid11.wspeed,
			       ais->type8.dac1fid11.wgust,
			       ais->type8.dac1fid11.wdir,
			       ais->type8.dac1fid11.wgustdir,
			       ais->type8.dac1fid11.humidity);
		if (scaled)
		    str_appendf(buf, buflen,
				   "\"airtemp\":%.1f,\"dewpoint\":%.1f,"
				   "\"pressure\":%u,\"pressuretend\":\"%s\",",
				   (ais->type8.dac1fid11.airtemp - DAC1FID11_AIRTEMP_OFFSET) / DAC1FID11_AIRTEMP_DIV,
				   (ais->type8.dac1fid11.dewpoint - DAC1FID11_DEWPOINT_OFFSET) / DAC1FID11_DEWPOINT_DIV,
				   ais->type8.dac1fid11.pressure - DAC1FID11_PRESSURE_OFFSET,
				   trends[ais->type8.dac1fid11.pressuretend]);
		else
		    str_appendf(buf, buflen,
				   "\"airtemp\":%u,\"dewpoint\":%u,"
				   "\"pressure\":%u,\"pressuretend\":%u,",
				   ais->type8.dac1fid11.airtemp,
				   ais->type8.dac1fid11.dewpoint,
				   ais->type8.dac1fid11.pressure,
				   ais->type8.dac1fid11.pressuretend);

		if (scaled)
		    str_appendf(buf, buflen,
				   "\"visibility\":%.1f,",
				   ais->type8.dac1fid11.visibility / DAC1FID11_VISIBILITY_DIV);
		else
		    str_appendf(buf, buflen,
				   "\"visibility\":%u,",
				   ais->type8.dac1fid11.visibility);
		if (!scaled)
		    str_appendf(buf, buflen,
				   "\"waterlevel\":%d,",
				   ais->type8.dac1fid11.waterlevel);
		else
		    str_appendf(buf, buflen,
				   "\"waterlevel\":%.1f,",
				   (ais->type8.dac1fid11.waterlevel - DAC1FID11_WATERLEVEL_OFFSET) / DAC1FID11_WATERLEVEL_DIV);

		if (scaled) {
		    str_appendf(buf, buflen,
				   "\"leveltrend\":\"%s\","
				   "\"cspeed\":%.1f,\"cdir\":%u,"
				   "\"cspeed2\":%.1f,\"cdir2\":%u,\"cdepth2\":%u,"
				   "\"cspeed3\":%.1f,\"cdir3\":%u,\"cdepth3\":%u,"
				   "\"waveheight\":%.1f,\"waveperiod\":%u,\"wavedir\":%u,"
				   "\"swellheight\":%.1f,\"swellperiod\":%u,\"swelldir\":%u,"
				   "\"seastate\":%u,\"watertemp\":%.1f,"
				   "\"preciptype\":%u,\"preciptype_text\":\"%s\","
				   "\"salinity\":%.1f,\"ice\":%u,\"ice_text\":\"%s\"",
				   trends[ais->type8.dac1fid11.leveltrend],
				   ais->type8.dac1fid11.cspeed / DAC1FID11_CSPEED_DIV,
				   ais->type8.dac1fid11.cdir,
				   ais->type8.dac1fid11.cspeed2 / DAC1FID11_CSPEED_DIV,
				   ais->type8.dac1fid11.cdir2,
				   ais->type8.dac1fid11.cdepth2,
				   ais->type8.dac1fid11.cspeed3 / DAC1FID11_CSPEED_DIV,
				   ais->type8.dac1fid11.cdir3,
				   ais->type8.dac1fid11.cdepth3,
				   ais->type8.dac1fid11.waveheight / DAC1FID11_WAVEHEIGHT_DIV,
				   ais->type8.dac1fid11.waveperiod,
				   ais->type8.dac1fid11.wavedir,
				   ais->type8.dac1fid11.swellheight / DAC1FID11_WAVEHEIGHT_DIV,
				   ais->type8.dac1fid11.swellperiod,
				   ais->type8.dac1fid11.swelldir,
				   ais->type8.dac1fid11.seastate,
				   (ais->type8.dac1fid11.watertemp - DAC1FID11_WATERTEMP_OFFSET) / DAC1FID11_WATERTEMP_DIV,
				   ais->type8.dac1fid11.preciptype,
				   preciptypes[ais->type8.dac1fid11.preciptype],
				   ais->type8.dac1fid11.salinity / DAC1FID11_SALINITY_DIV,
				   ais->type8.dac1fid11.ice,
				   ice[ais->type8.dac1fid11.ice]);
		} else
		    str_appendf(buf, buflen,
				   "\"leveltrend\":%u,"
				   "\"cspeed\":%u,\"cdir\":%u,"
				   "\"cspeed2\":%u,\"cdir2\":%u,\"cdepth2\":%u,"
				   "\"cspeed3\":%u,\"cdir3\":%u,\"cdepth3\":%u,"
				   "\"waveheight\":%u,\"waveperiod\":%u,\"wavedir\":%u,"
				   "\"swellheight\":%u,\"swellperiod\":%u,\"swelldir\":%u,"
				   "\"seastate\":%u,\"watertemp\":%u,"
				   "\"preciptype\":%u,\"preciptype_text\":\"%s\","
				   "\"salinity\":%u,\"ice\":%u,\"ice_text\":\"%s\"",
				   ais->type8.dac1fid11.leveltrend,
				   ais->type8.dac1fid11.cspeed,
				   ais->type8.dac1fid11.cdir,
				   ais->type8.dac1fid11.cspeed2,
				   ais->type8.dac1fid11.cdir2,
				   ais->type8.dac1fid11.cdepth2,
				   ais->type8.dac1fid11.cspeed3,
				   ais->type8.dac1fid11.cdir3,
				   ais->type8.dac1fid11.cdepth3,
				   ais->type8.dac1fid11.waveheight,
				   ais->type8.dac1fid11.waveperiod,
				   ais->type8.dac1fid11.wavedir,
				   ais->type8.dac1fid11.swellheight,
				   ais->type8.dac1fid11.swellperiod,
				   ais->type8.dac1fid11.swelldir,
				   ais->type8.dac1fid11.seastate,
				   ais->type8.dac1fid11.watertemp,
				   ais->type8.dac1fid11.preciptype,
				   preciptypes[ais->type8.dac1fid11.preciptype],
				   ais->type8.dac1fid11.salinity,
				   ais->type8.dac1fid11.ice,
				   ice[ais->type8.dac1fid11.ice]);
		(void)strlcat(buf, "}\r\n", buflen);
		break;
	    case 13:        /* IMO236 - Fairway closed */
		str_appendf(buf, buflen,
			       "\"reason\":\"%s\",\"closefrom\":\"%s\","
			       "\"closeto\":\"%s\",\"radius\":%u,"
			       "\"extunit\":%u,"
			       "\"from\":\"%02u-%02uT%02u:%02u\","
			       "\"to\":\"%02u-%02uT%02u:%02u\"}\r\n",
			       json_stringify(buf1, sizeof(buf1),
					      ais->type8.dac1fid13.reason),
			       json_stringify(buf2, sizeof(buf2),
					      ais->type8.dac1fid13.closefrom),
			       json_stringify(buf3, sizeof(buf3),
					      ais->type8.dac1fid13.closeto),
			       ais->type8.dac1fid13.radius,
			       ais->type8.dac1fid13.extunit,
			       ais->type8.dac1fid13.fmonth,
			       ais->type8.dac1fid13.fday,
			       ais->type8.dac1fid13.fhour,
			       ais->type8.dac1fid13.fminute,
			       ais->type8.dac1fid13.tmonth,
			       ais->type8.dac1fid13.tday,
			       ais->type8.dac1fid13.thour,
			       ais->type8.dac1fid13.tminute);
		break;
	    case 15:        /* IMO236 - Extended ship and voyage */
		str_appendf(buf, buflen,
			       "\"airdraught\":%u}\r\n",
			       ais->type8.dac1fid15.airdraught);
		break;
	    case 16:	/* IMO289 - Number of persons on board */
		str_appendf(buf, buflen,
			       "\"persons\":%u}\t\n", ais->type6.dac1fid16.persons);
		break;
	    case 17:        /* IMO289 - VTS-generated/synthetic targets */
		(void)strlcat(buf, "\"targets\":[", buflen);
		for (i = 0; i < ais->type8.dac1fid17.ntargets; i++) {
		    str_appendf(buf, buflen,
				   "{\"idtype\":%u,\"idtype_text\":\"%s\",",
				   ais->type8.dac1fid17.targets[i].idtype,
				   idtypes[ais->type8.dac1fid17.targets[i].idtype]);
		    switch (ais->type8.dac1fid17.targets[i].idtype) {
		    case DAC1FID17_IDTYPE_MMSI:
			str_appendf(buf, buflen,
			    "\"%s\":\"%u\",",
			    idtypes[ais->type8.dac1fid17.targets[i].idtype],
			    ais->type8.dac1fid17.targets[i].id.mmsi);
			break;
		    case DAC1FID17_IDTYPE_IMO:
			str_appendf(buf, buflen,
			    "\"%s\":\"%u\",",
			    idtypes[ais->type8.dac1fid17.targets[i].idtype],
			    ais->type8.dac1fid17.targets[i].id.imo);
			break;
		    case DAC1FID17_IDTYPE_CALLSIGN:
			str_appendf(buf, buflen,
			    "\"%s\":\"%s\",",
			    idtypes[ais->type8.dac1fid17.targets[i].idtype],
			    json_stringify(buf1, sizeof(buf1),
					   ais->type8.dac1fid17.targets[i].id.callsign));
			break;
		    default:
			str_appendf(buf, buflen,
			    "\"%s\":\"%s\",",
			    idtypes[ais->type8.dac1fid17.targets[i].idtype],
			    json_stringify(buf1, sizeof(buf1),
					   ais->type8.dac1fid17.targets[i].id.other));
		    }
		    if (scaled)
			str_appendf(buf, buflen,
			    "\"lat\":%.3f,\"lon\":%.3f,",
			    ais->type8.dac1fid17.targets[i].lat / AIS_LATLON3_DIV,
			    ais->type8.dac1fid17.targets[i].lon / AIS_LATLON3_DIV);
		    else
			str_appendf(buf, buflen,
			    "\"lat\":%d,\"lon\":%d,",
			    ais->type8.dac1fid17.targets[i].lat,
			    ais->type8.dac1fid17.targets[i].lon);
		    str_appendf(buf, buflen,
			"\"course\":%u,\"second\":%u,\"speed\":%u},",
			ais->type8.dac1fid17.targets[i].course,
			ais->type8.dac1fid17.targets[i].second,
			ais->type8.dac1fid17.targets[i].speed);
		}
		str_rstrip_char(buf, ',');
		(void)strlcat(buf, "]}\r\n", buflen);
		break;
	    case 19:        /* IMO289 - Marine Traffic Signal */
		str_appendf(buf, buflen,
			       "\"linkage\":%u,\"station\":\"%s\","
			       "\"lon\":%.3f,\"lat\":%.3f,\"status\":%u,"
			       "\"signal\":%u,\"signal_text\":\"%s\","
			       "\"hour\":%u,\"minute\":%u,"
			       "\"nextsignal\":%u"
			       "\"nextsignal_text\":\"%s\""
			       "}\r\n",
			       ais->type8.dac1fid19.linkage,
			       json_stringify(buf1, sizeof(buf1),
					      ais->type8.dac1fid19.station),
			       ais->type8.dac1fid19.lon / AIS_LATLON3_DIV,
			       ais->type8.dac1fid19.lat / AIS_LATLON3_DIV,
			       ais->type8.dac1fid19.status,
			       ais->type8.dac1fid19.signal,
			       SIGNAL_DISPLAY(ais->type8.dac1fid19.signal),
			       ais->type8.dac1fid19.hour,
			       ais->type8.dac1fid19.minute,
			       ais->type8.dac1fid19.nextsignal,
			       SIGNAL_DISPLAY(ais->type8.dac1fid19.nextsignal));
		break;
	    case 21:        /* IMO289 - Weather obs. report from ship */
		break;
	    case 22:        /* IMO289 - Area notice - broadcast */
		break;
	    case 24:        /* IMO289 - Extended ship static & voyage-related data */
		break;
	    case 25:        /* IMO289 - Dangerous Cargo Indication */
		break;
	    case 27:        /* IMO289 - Route information - broadcast */
		str_appendf(buf, buflen,
			       "\"linkage\":%u,\"sender\":%u,"
			       "\"rtype\":%u,"
			       "\"rtype_text\":\"%s\","
			       "\"start\":\"%02u-%02uT%02u:%02uZ\","
			       "\"duration\":%u,\"waypoints\":[",
			       ais->type8.dac1fid27.linkage,
			       ais->type8.dac1fid27.sender,
			       ais->type8.dac1fid27.rtype,
			       route_type[ais->type8.dac1fid27.rtype],
			       ais->type8.dac1fid27.month,
			       ais->type8.dac1fid27.day,
			       ais->type8.dac1fid27.hour,
			       ais->type8.dac1fid27.minute,
			       ais->type8.dac1fid27.duration);
		for (i = 0; i < ais->type8.dac1fid27.waycount; i++) {
		    if (scaled)
			str_appendf(buf, buflen,
			    "{\"lon\":%.4f,\"lat\":%.4f},",
			    ais->type8.dac1fid27.waypoints[i].lon / AIS_LATLON4_DIV,
			    ais->type8.dac1fid27.waypoints[i].lat / AIS_LATLON4_DIV);
		    else
			str_appendf(buf, buflen,
			    "{\"lon\":%d,\"lat\":%d},",
			    ais->type8.dac1fid27.waypoints[i].lon,
			    ais->type8.dac1fid27.waypoints[i].lat);
		}
		str_rstrip_char(buf, ',');
		(void)strlcat(buf, "]}\r\n", buflen);
		break;
	    case 29:        /* IMO289 - Text Description - broadcast */
		str_appendf(buf, buflen,
		       "\"linkage\":%u,\"text\":\"%s\"}\r\n",
		       ais->type8.dac1fid29.linkage,
		       json_stringify(buf1, sizeof(buf1),
				      ais->type8.dac1fid29.text));
		break;
	    case 31:        /* IMO289 - Meteorological/Hydrological data */
		/* some fields have been merged to an ISO8601 partial date */
		/* layout is almost identical to FID=11 from IMO236 */
		if (scaled)
		    str_appendf(buf, buflen,
				   "\"lat\":%.3f,\"lon\":%.3f,",
				   ais->type8.dac1fid31.lat / AIS_LATLON3_DIV,
				   ais->type8.dac1fid31.lon / AIS_LATLON3_DIV);
		else
		    str_appendf(buf, buflen,
				   "\"lat\":%d,\"lon\":%d,",
				   ais->type8.dac1fid31.lat,
				   ais->type8.dac1fid31.lon);
		str_appendf(buf, buflen,
			       "\"accuracy\":%s,",
			       JSON_BOOL(ais->type8.dac1fid31.accuracy));
		str_appendf(buf, buflen,
			       "\"timestamp\":\"%02uT%02u:%02uZ\","
			       "\"wspeed\":%u,\"wgust\":%u,\"wdir\":%u,"
			       "\"wgustdir\":%u,\"humidity\":%u,",
			       ais->type8.dac1fid31.day,
			       ais->type8.dac1fid31.hour,
			       ais->type8.dac1fid31.minute,
			       ais->type8.dac1fid31.wspeed,
			       ais->type8.dac1fid31.wgust,
			       ais->type8.dac1fid31.wdir,
			       ais->type8.dac1fid31.wgustdir,
			       ais->type8.dac1fid31.humidity);
		if (scaled)
		    str_appendf(buf, buflen,
				   "\"airtemp\":%.1f,\"dewpoint\":%.1f,"
				   "\"pressure\":%u,\"pressuretend\":\"%s\","
				   "\"visgreater\":%s,",
				   ais->type8.dac1fid31.airtemp / DAC1FID31_AIRTEMP_DIV,
				   ais->type8.dac1fid31.dewpoint / DAC1FID31_DEWPOINT_DIV,
				   ais->type8.dac1fid31.pressure - DAC1FID31_PRESSURE_OFFSET,
				   trends[ais->type8.dac1fid31.pressuretend],
				   JSON_BOOL(ais->type8.dac1fid31.visgreater));
		else
		    str_appendf(buf, buflen,
				   "\"airtemp\":%d,\"dewpoint\":%d,"
				   "\"pressure\":%u,\"pressuretend\":%u,"
				   "\"visgreater\":%s,",
				   ais->type8.dac1fid31.airtemp,
				   ais->type8.dac1fid31.dewpoint,
				   ais->type8.dac1fid31.pressure,
				   ais->type8.dac1fid31.pressuretend,
				   JSON_BOOL(ais->type8.dac1fid31.visgreater));

		if (scaled)
		    str_appendf(buf, buflen,
				   "\"visibility\":%.1f,",
				   ais->type8.dac1fid31.visibility / DAC1FID31_VISIBILITY_DIV);
		else
		    str_appendf(buf, buflen,
				   "\"visibility\":%u,",
				   ais->type8.dac1fid31.visibility);
		if (!scaled)
		    str_appendf(buf, buflen,
				   "\"waterlevel\":%d,",
				   ais->type8.dac1fid31.waterlevel);
		else
		    str_appendf(buf, buflen,
				   "\"waterlevel\":%.1f,",
				   (ais->type8.dac1fid31.waterlevel - DAC1FID31_WATERLEVEL_OFFSET) / DAC1FID31_WATERLEVEL_DIV);

		if (scaled) {
		    str_appendf(buf, buflen,
				   "\"leveltrend\":\"%s\","
				   "\"cspeed\":%.1f,\"cdir\":%u,"
				   "\"cspeed2\":%.1f,\"cdir2\":%u,\"cdepth2\":%u,"
				   "\"cspeed3\":%.1f,\"cdir3\":%u,\"cdepth3\":%u,"
				   "\"waveheight\":%.1f,\"waveperiod\":%u,\"wavedir\":%u,"
				   "\"swellheight\":%.1f,\"swellperiod\":%u,\"swelldir\":%u,"
				   "\"seastate\":%u,\"watertemp\":%.1f,"
				   "\"preciptype\":\"%s\",\"salinity\":%.1f,\"ice\":\"%s\"",
				   trends[ais->type8.dac1fid31.leveltrend],
				   ais->type8.dac1fid31.cspeed / DAC1FID31_CSPEED_DIV,
				   ais->type8.dac1fid31.cdir,
				   ais->type8.dac1fid31.cspeed2 / DAC1FID31_CSPEED_DIV,
				   ais->type8.dac1fid31.cdir2,
				   ais->type8.dac1fid31.cdepth2,
				   ais->type8.dac1fid31.cspeed3 / DAC1FID31_CSPEED_DIV,
				   ais->type8.dac1fid31.cdir3,
				   ais->type8.dac1fid31.cdepth3,
				   ais->type8.dac1fid31.waveheight / DAC1FID31_HEIGHT_DIV,
				   ais->type8.dac1fid31.waveperiod,
				   ais->type8.dac1fid31.wavedir,
				   ais->type8.dac1fid31.swellheight / DAC1FID31_HEIGHT_DIV,
				   ais->type8.dac1fid31.swellperiod,
				   ais->type8.dac1fid31.swelldir,
				   ais->type8.dac1fid31.seastate,
				   ais->type8.dac1fid31.watertemp / DAC1FID31_WATERTEMP_DIV,
				   preciptypes[ais->type8.dac1fid31.preciptype],
				   ais->type8.dac1fid31.salinity / DAC1FID31_SALINITY_DIV,
				   ice[ais->type8.dac1fid31.ice]);
		} else
		    str_appendf(buf, buflen,
				   "\"leveltrend\":%u,"
				   "\"cspeed\":%u,\"cdir\":%u,"
				   "\"cspeed2\":%u,\"cdir2\":%u,\"cdepth2\":%u,"
				   "\"cspeed3\":%u,\"cdir3\":%u,\"cdepth3\":%u,"
				   "\"waveheight\":%u,\"waveperiod\":%u,\"wavedir\":%u,"
				   "\"swellheight\":%u,\"swellperiod\":%u,\"swelldir\":%u,"
				   "\"seastate\":%u,\"watertemp\":%d,"
				   "\"preciptype\":%u,\"salinity\":%u,\"ice\":%u",
				   ais->type8.dac1fid31.leveltrend,
				   ais->type8.dac1fid31.cspeed,
				   ais->type8.dac1fid31.cdir,
				   ais->type8.dac1fid31.cspeed2,
				   ais->type8.dac1fid31.cdir2,
				   ais->type8.dac1fid31.cdepth2,
				   ais->type8.dac1fid31.cspeed3,
				   ais->type8.dac1fid31.cdir3,
				   ais->type8.dac1fid31.cdepth3,
				   ais->type8.dac1fid31.waveheight,
				   ais->type8.dac1fid31.waveperiod,
				   ais->type8.dac1fid31.wavedir,
				   ais->type8.dac1fid31.swellheight,
				   ais->type8.dac1fid31.swellperiod,
				   ais->type8.dac1fid31.swelldir,
				   ais->type8.dac1fid31.seastate,
				   ais->type8.dac1fid31.watertemp,
				   ais->type8.dac1fid31.preciptype,
				   ais->type8.dac1fid31.salinity,
				   ais->type8.dac1fid31.ice);
		(void)strlcat(buf, "}\r\n", buflen);
		break;
	    }
	}
	else if (ais->type8.dac == 200) {
	    struct {
		const unsigned int code;
		const unsigned int ais;
		const char *legend;
	    } *cp, shiptypes[] = {
		/*
		 * The Inland AIS standard is not clear which numbers are
		 * supposed to be in the type slot.  The ranges are disjoint,
		 * so we'll match on both.
		 */
		{8000, 99, "Vessel, type unknown"},
		{8010, 79, "Motor freighter"},
		{8020, 89, "Motor tanker"},
		{8021, 80, "Motor tanker, liquid cargo, type N"},
		{8022, 80, "Motor tanker, liquid cargo, type C"},
		{8023, 89, "Motor tanker, dry cargo as if liquid (e.g. cement)"},
		{8030, 79, "Container vessel"},
		{8040, 80, "Gas tanker"},
		{8050, 79, "Motor freighter, tug"},
		{8060, 89, "Motor tanker, tug"},
		{8070, 79, "Motor freighter with one or more ships alongside"},
		{8080, 89, "Motor freighter with tanker"},
		{8090, 79, "Motor freighter pushing one or more freighters"},
		{8100, 89, "Motor freighter pushing at least one tank-ship"},
		{8110, 79, "Tug, freighter"},
		{8120, 89, "Tug, tanker"},
		{8130, 31, "Tug freighter, coupled"},
		{8140, 31, "Tug, freighter/tanker, coupled"},
		{8150, 99, "Freightbarge"},
		{8160, 99, "Tankbarge"},
		{8161, 90, "Tankbarge, liquid cargo, type N"},
		{8162, 90, "Tankbarge, liquid cargo, type C"},
		{8163, 99, "Tankbarge, dry cargo as if liquid (e.g. cement)"},
		{8170, 99, "Freightbarge with containers"},
		{8180, 90, "Tankbarge, gas"},
		{8210, 79, "Pushtow, one cargo barge"},
		{8220, 79, "Pushtow, two cargo barges"},
		{8230, 79, "Pushtow, three cargo barges"},
		{8240, 79, "Pushtow, four cargo barges"},
		{8250, 79, "Pushtow, five cargo barges"},
		{8260, 79, "Pushtow, six cargo barges"},
		{8270, 79, "Pushtow, seven cargo barges"},
		{8280, 79, "Pushtow, eigth cargo barges"},
		{8290, 79, "Pushtow, nine or more barges"},
		{8310, 80, "Pushtow, one tank/gas barge"},
		{8320, 80, "Pushtow, two barges at least one tanker or gas barge"},
		{8330, 80, "Pushtow, three barges at least one tanker or gas barge"},
		{8340, 80, "Pushtow, four barges at least one tanker or gas barge"},
		{8350, 80, "Pushtow, five barges at least one tanker or gas barge"},
		{8360, 80, "Pushtow, six barges at least one tanker or gas barge"},
		{8370, 80, "Pushtow, seven barges at least one tanker or gas barg"},
		{0, 0, "Illegal ship type value."},
	    };
	    const char *hazard_types[] = {
		"0 blue cones/lights",
		"1 blue cone/light",
		"2 blue cones/lights",
		"3 blue cones/lights",
		"4 B-Flag",
		"Unknown",
	    };
#define HTYPE_DISPLAY(n) (((n) < (unsigned int)NITEMS(hazard_types)) ? hazard_types[n] : "INVALID HAZARD TYPE")
	    const char *lstatus_types[] = {
		"N/A (default)",
		"Unloaded",
		"Loaded",
	    };
#define LSTATUS_DISPLAY(n) (((n) < (unsigned int)NITEMS(lstatus_types)) ? lstatus_types[n] : "INVALID LOAD STATUS")
	    const char *emma_types[] = {
		"Not Available",
		"Wind",
		"Rain",
		"Snow and ice",
		"Thunderstorm",
		"Fog",
		"Low temperature",
		"High temperature",
		"Flood",
		"Forest Fire",
	    };
#define EMMA_TYPE_DISPLAY(n) (((n) < (unsigned int)NITEMS(emma_types)) ? emma_types[n] : "INVALID EMMA TYPE")
	    const char *emma_classes[] = {
		"Slight",
		"Medium",
		"Strong",
	    };
#define EMMA_CLASS_DISPLAY(n) (((n) < (unsigned int)NITEMS(emma_classes)) ? emma_classes[n] : "INVALID EMMA TYPE")
	    const char *emma_winds[] = {
		"N/A",
		"North",
		"North East",
		"East",
		"South East",
		"South",
		"South West",
		"West",
		"North West",
	    };
#define EMMA_WIND_DISPLAY(n) (((n) < (unsigned int)NITEMS(emma_winds)) ? emma_winds[n] : "INVALID EMMA WIND DIRECTION")
	    const char *direction_vocabulary[] = {
		"Unknown",
		"Upstream",
		"Downstream",
		"To left bank",
		"To right bank",
	    };
#define DIRECTION_DISPLAY(n) (((n) < (unsigned int)NITEMS(direction_vocabulary)) ? direction_vocabulary[n] : "INVALID DIRECTION")
	    const char *status_vocabulary[] = {
		"Unknown",
		"No light",
		"White",
		"Yellow",
		"Green",
		"Red",
		"White flashing",
		"Yellow flashing.",
	    };
#define STATUS_DISPLAY(n) (((n) < (unsigned int)NITEMS(status_vocabulary)) ? status_vocabulary[n] : "INVALID STATUS")

	    switch (ais->type8.fid) {
	    case 10:        /* Inland ship static and voyage-related data */
		for (cp = shiptypes; cp < shiptypes + NITEMS(shiptypes); cp++)
		    if (cp->code == ais->type8.dac200fid10.shiptype
			|| cp->ais == ais->type8.dac200fid10.shiptype
			|| cp->code == 0)
			break;
		str_appendf(buf, buflen,
			       "\"vin\":\"%s\",\"length\":%u,\"beam\":%u,"
			       "\"shiptype\":%u,\"shiptype_text\":\"%s\","
			       "\"hazard\":%u,\"hazard_text\":\"%s\","
			       "\"draught\":%u,"
			       "\"loaded\":%u,\"loaded_text\":\"%s\","
			       "\"speed_q\":%s,"
			       "\"course_q\":%s,"
			       "\"heading_q\":%s}\r\n",
			       ais->type8.dac200fid10.vin,
			       ais->type8.dac200fid10.length,
			       ais->type8.dac200fid10.beam,
			       ais->type8.dac200fid10.shiptype,
			       cp->legend,
			       ais->type8.dac200fid10.hazard,
			       HTYPE_DISPLAY(ais->type8.dac200fid10.hazard),
			       ais->type8.dac200fid10.draught,
			       ais->type8.dac200fid10.loaded,
			       LSTATUS_DISPLAY(ais->type8.dac200fid10.loaded),
			       JSON_BOOL(ais->type8.dac200fid10.speed_q),
			       JSON_BOOL(ais->type8.dac200fid10.course_q),
			       JSON_BOOL(ais->type8.dac200fid10.heading_q));
		break;
	    case 23:	/* EMMA warning */
		if (!ais->type8.structured)
		    break;
		str_appendf(buf, buflen,
			       "\"start\":\"%4u-%02u-%02uT%02u:%02u\","
			       "\"end\":\"%4u-%02u-%02uT%02u:%02u\",",
			       ais->type8.dac200fid23.start_year + 2000,
			       ais->type8.dac200fid23.start_month,
			       ais->type8.dac200fid23.start_hour,
			       ais->type8.dac200fid23.start_minute,
			       ais->type8.dac200fid23.start_day,
			       ais->type8.dac200fid23.end_year + 2000,
			       ais->type8.dac200fid23.end_month,
			       ais->type8.dac200fid23.end_day,
			       ais->type8.dac200fid23.end_hour,
			       ais->type8.dac200fid23.end_minute);
		if (scaled)
		    str_appendf(buf, buflen,
			"\"start_lon\":%.4f,\"start_lat\":%.4f,\"end_lon\":%.4f,\"end_lat\":%.4f,",
			ais->type8.dac200fid23.start_lon / AIS_LATLON_DIV,
			ais->type8.dac200fid23.start_lat / AIS_LATLON_DIV,
			ais->type8.dac200fid23.end_lon / AIS_LATLON_DIV,
			ais->type8.dac200fid23.end_lat / AIS_LATLON_DIV);
		else
		    str_appendf(buf, buflen,
			"\"start_lon\":%d,\"start_lat\":%d,\"end_lon\":%d,\"end_lat\":%d,",
			ais->type8.dac200fid23.start_lon,
			ais->type8.dac200fid23.start_lat,
			ais->type8.dac200fid23.end_lon,
			ais->type8.dac200fid23.end_lat);
		str_appendf(buf, buflen,
		    "\"type\":%u,\"type_text\":\"%s\",\"min\":%d,\"max\":%d,\"class\":%u,\"class_text\":\"%s\",\"wind\":%u,\"wind_text\":\"%s\"}\r\n",

		    ais->type8.dac200fid23.type,
		    EMMA_TYPE_DISPLAY(ais->type8.dac200fid23.type),
		    ais->type8.dac200fid23.min,
		    ais->type8.dac200fid23.max,
		    ais->type8.dac200fid23.intensity,
		    EMMA_CLASS_DISPLAY(ais->type8.dac200fid23.intensity),
		    ais->type8.dac200fid23.wind,
		    EMMA_WIND_DISPLAY(ais->type8.dac200fid23.wind));
		break;
	    case 24:	/* Inland AIS Water Levels */
		str_appendf(buf, buflen,
		    "\"country\":\"%s\",\"gauges\":[",
		    ais->type8.dac200fid24.country);
		for (i = 0; i < ais->type8.dac200fid24.ngauges; i++) {
		    str_appendf(buf, buflen,
			"{\"id\":%u,\"level\":%d}",
			ais->type8.dac200fid24.gauges[i].id,
			ais->type8.dac200fid24.gauges[i].level);
		}
		str_rstrip_char(buf, ',');
		(void)strlcat(buf, "]}\r\n", buflen - strlen(buf));
		break;
	    case 40:	/* Inland AIS Signal Strength */
		if (scaled)
		    str_appendf(buf, buflen,
			"\"lon\":%.4f,\"lat\":%.4f,",
			ais->type8.dac200fid40.lon / AIS_LATLON_DIV,
			ais->type8.dac200fid40.lat / AIS_LATLON_DIV);
		else
		    str_appendf(buf, buflen,
			"\"lon\":%d,\"lat\":%d,",
			ais->type8.dac200fid40.lon,
			ais->type8.dac200fid40.lat);
		str_appendf(buf, buflen,
		    "\"form\":%u,\"facing\":%u,\"direction\":%u,\"direction_text\":\"%s\",\"status\":%u,\"status_text\":\"%s\"}\r\n",
		    ais->type8.dac200fid40.form,
		    ais->type8.dac200fid40.facing,
		    ais->type8.dac200fid40.direction,
		    DIRECTION_DISPLAY(ais->type8.dac200fid40.direction),
		    ais->type8.dac200fid40.status,
		    STATUS_DISPLAY(ais->type8.dac200fid40.status));
		break;
	    }
	}
	break;
    case 9:			/* Standard SAR Aircraft Position Report */
	if (scaled) {
	    char altlegend[20];
	    char speedlegend[20];

	    /*
	     * Express altitude as nan if not available,
	     * "high" for above the reporting ceiling.
	     */
	    if (ais->type9.alt == AIS_ALT_NOT_AVAILABLE)
		(void)strlcpy(altlegend, "\"nan\"", sizeof(altlegend));
	    else if (ais->type9.alt == AIS_ALT_HIGH)
		(void)strlcpy(altlegend, "\"high\"", sizeof(altlegend));
	    else
		(void)snprintf(altlegend, sizeof(altlegend),
			       "%u", ais->type9.alt);

	    /*
	     * Express speed as nan if not available,
	     * "high" for above the reporting ceiling.
	     */
	    if (ais->type9.speed == AIS_SAR_SPEED_NOT_AVAILABLE)
		(void)strlcpy(speedlegend, "\"nan\"", sizeof(speedlegend));
	    else if (ais->type9.speed == AIS_SAR_FAST_MOVER)
		(void)strlcpy(speedlegend, "\"fast\"", sizeof(speedlegend));
	    else
		(void)snprintf(speedlegend, sizeof(speedlegend),
			       "%u", ais->type1.speed);

	    str_appendf(buf, buflen,
			   "\"alt\":%s,\"speed\":%s,\"accuracy\":%s,"
			   "\"lon\":%.4f,\"lat\":%.4f,\"course\":%.1f,"
			   "\"second\":%u,\"regional\":%u,\"dte\":%u,"
			   "\"raim\":%s,\"radio\":%u}\r\n",
			   altlegend,
			   speedlegend,
			   JSON_BOOL(ais->type9.accuracy),
			   ais->type9.lon / AIS_LATLON_DIV,
			   ais->type9.lat / AIS_LATLON_DIV,
			   ais->type9.course / 10.0,
			   ais->type9.second,
			   ais->type9.regional,
			   ais->type9.dte,
			   JSON_BOOL(ais->type9.raim), ais->type9.radio);
	} else {
	    str_appendf(buf, buflen,
			   "\"alt\":%u,\"speed\":%u,\"accuracy\":%s,"
			   "\"lon\":%d,\"lat\":%d,\"course\":%u,"
			   "\"second\":%u,\"regional\":%u,\"dte\":%u,"
			   "\"raim\":%s,\"radio\":%u}\r\n",
			   ais->type9.alt,
			   ais->type9.speed,
			   JSON_BOOL(ais->type9.accuracy),
			   ais->type9.lon,
			   ais->type9.lat,
			   ais->type9.course,
			   ais->type9.second,
			   ais->type9.regional,
			   ais->type9.dte,
			   JSON_BOOL(ais->type9.raim), ais->type9.radio);
	}
	break;
    case 10:			/* UTC/Date Inquiry */
	str_appendf(buf, buflen,
		       "\"dest_mmsi\":%u}\r\n", ais->type10.dest_mmsi);
	break;
    case 12:			/* Safety Related Message */
	str_appendf(buf, buflen,
		       "\"seqno\":%u,\"dest_mmsi\":%u,\"retransmit\":%s,\"text\":\"%s\"}\r\n",
		       ais->type12.seqno,
		       ais->type12.dest_mmsi,
		       JSON_BOOL(ais->type12.retransmit),
		       json_stringify(buf1, sizeof(buf1), ais->type12.text));
	break;
    case 14:			/* Safety Related Broadcast Message */
	str_appendf(buf, buflen,
		       "\"text\":\"%s\"}\r\n",
		       json_stringify(buf1, sizeof(buf1), ais->type14.text));
	break;
    case 15:			/* Interrogation */
	str_appendf(buf, buflen,
		       "\"mmsi1\":%u,\"type1_1\":%u,\"offset1_1\":%u,"
		       "\"type1_2\":%u,\"offset1_2\":%u,\"mmsi2\":%u,"
		       "\"type2_1\":%u,\"offset2_1\":%u}\r\n",
		       ais->type15.mmsi1,
		       ais->type15.type1_1,
		       ais->type15.offset1_1,
		       ais->type15.type1_2,
		       ais->type15.offset1_2,
		       ais->type15.mmsi2,
		       ais->type15.type2_1, ais->type15.offset2_1);
	break;
    case 16:
	str_appendf(buf, buflen,
		       "\"mmsi1\":%u,\"offset1\":%u,\"increment1\":%u,"
		       "\"mmsi2\":%u,\"offset2\":%u,\"increment2\":%u}\r\n",
		       ais->type16.mmsi1,
		       ais->type16.offset1,
		       ais->type16.increment1,
		       ais->type16.mmsi2,
		       ais->type16.offset2, ais->type16.increment2);
	break;
    case 17:
	if (scaled) {
	    str_appendf(buf, buflen,
			   "\"lon\":%.1f,\"lat\":%.1f,\"data\":\"%zd:%s\"}\r\n",
			   ais->type17.lon / AIS_GNSS_LATLON_DIV,
			   ais->type17.lat / AIS_GNSS_LATLON_DIV,
			   ais->type17.bitcount,
			   gpsd_hexdump(scratchbuf, sizeof(scratchbuf),
					(char *)ais->type17.bitdata,
					BITS_TO_BYTES(ais->type17.bitcount)));
	} else {
	    str_appendf(buf, buflen,
			   "\"lon\":%d,\"lat\":%d,\"data\":\"%zd:%s\"}\r\n",
			   ais->type17.lon,
			   ais->type17.lat,
			   ais->type17.bitcount,
			   gpsd_hexdump(scratchbuf, sizeof(scratchbuf),
					(char *)ais->type17.bitdata,
					BITS_TO_BYTES(ais->type17.bitcount)));
	}
	break;
    case 18:
	if (scaled) {
	    str_appendf(buf, buflen,
			   "\"reserved\":%u,\"speed\":%.1f,\"accuracy\":%s,"
			   "\"lon\":%.4f,\"lat\":%.4f,\"course\":%.1f,"
			   "\"heading\":%u,\"second\":%u,\"regional\":%u,"
			   "\"cs\":%s,\"display\":%s,\"dsc\":%s,\"band\":%s,"
			   "\"msg22\":%s,\"raim\":%s,\"radio\":%u}\r\n",
			   ais->type18.reserved,
			   ais->type18.speed / 10.0,
			   JSON_BOOL(ais->type18.accuracy),
			   ais->type18.lon / AIS_LATLON_DIV,
			   ais->type18.lat / AIS_LATLON_DIV,
			   ais->type18.course / 10.0,
			   ais->type18.heading,
			   ais->type18.second,
			   ais->type18.regional,
			   JSON_BOOL(ais->type18.cs),
			   JSON_BOOL(ais->type18.display),
			   JSON_BOOL(ais->type18.dsc),
			   JSON_BOOL(ais->type18.band),
			   JSON_BOOL(ais->type18.msg22),
			   JSON_BOOL(ais->type18.raim), ais->type18.radio);
	} else {
	    str_appendf(buf, buflen,
			   "\"reserved\":%u,\"speed\":%u,\"accuracy\":%s,"
			   "\"lon\":%d,\"lat\":%d,\"course\":%u,"
			   "\"heading\":%u,\"second\":%u,\"regional\":%u,"
			   "\"cs\":%s,\"display\":%s,\"dsc\":%s,\"band\":%s,"
			   "\"msg22\":%s,\"raim\":%s,\"radio\":%u}\r\n",
			   ais->type18.reserved,
			   ais->type18.speed,
			   JSON_BOOL(ais->type18.accuracy),
			   ais->type18.lon,
			   ais->type18.lat,
			   ais->type18.course,
			   ais->type18.heading,
			   ais->type18.second,
			   ais->type18.regional,
			   JSON_BOOL(ais->type18.cs),
			   JSON_BOOL(ais->type18.display),
			   JSON_BOOL(ais->type18.dsc),
			   JSON_BOOL(ais->type18.band),
			   JSON_BOOL(ais->type18.msg22),
			   JSON_BOOL(ais->type18.raim), ais->type18.radio);
	}
	break;
    case 19:
	if (scaled) {
	    str_appendf(buf, buflen,
			   "\"reserved\":%u,\"speed\":%.1f,\"accuracy\":%s,"
			   "\"lon\":%.4f,\"lat\":%.4f,\"course\":%.1f,"
			   "\"heading\":%u,\"second\":%u,\"regional\":%u,"
			   "\"shipname\":\"%s\","
			   "\"shiptype\":%u,\"shiptype_text\":\"%s\","
			   "\"to_bow\":%u,\"to_stern\":%u,\"to_port\":%u,"
			   "\"to_starboard\":%u,"
			   "\"epfd\":%u,\"epfd_text\":\"%s\","
			   "\"raim\":%s,\"dte\":%u,\"assigned\":%s}\r\n",
			   ais->type19.reserved,
			   ais->type19.speed / 10.0,
			   JSON_BOOL(ais->type19.accuracy),
			   ais->type19.lon / AIS_LATLON_DIV,
			   ais->type19.lat / AIS_LATLON_DIV,
			   ais->type19.course / 10.0,
			   ais->type19.heading,
			   ais->type19.second,
			   ais->type19.regional,
			   json_stringify(buf1, sizeof(buf1),
					  ais->type19.shipname),
			   ais->type19.shiptype,
			   SHIPTYPE_DISPLAY(ais->type19.shiptype),
			   ais->type19.to_bow,
			   ais->type19.to_stern,
			   ais->type19.to_port,
			   ais->type19.to_starboard,
			   ais->type19.epfd,
			   EPFD_DISPLAY(ais->type19.epfd),
			   JSON_BOOL(ais->type19.raim),
			   ais->type19.dte,
			   JSON_BOOL(ais->type19.assigned));
	} else {
	    str_appendf(buf, buflen,
			   "\"reserved\":%u,\"speed\":%u,\"accuracy\":%s,"
			   "\"lon\":%d,\"lat\":%d,\"course\":%u,"
			   "\"heading\":%u,\"second\":%u,\"regional\":%u,"
			   "\"shipname\":\"%s\","
			   "\"shiptype\":%u,\"shiptype_text\":\"%s\","
			   "\"to_bow\":%u,\"to_stern\":%u,\"to_port\":%u,"
			   "\"to_starboard\":%u,"
			   "\"epfd\":%u,\"epfd_text\":\"%s\","
			   "\"raim\":%s,\"dte\":%u,\"assigned\":%s}\r\n",
			   ais->type19.reserved,
			   ais->type19.speed,
			   JSON_BOOL(ais->type19.accuracy),
			   ais->type19.lon,
			   ais->type19.lat,
			   ais->type19.course,
			   ais->type19.heading,
			   ais->type19.second,
			   ais->type19.regional,
			   json_stringify(buf1, sizeof(buf1),
					  ais->type19.shipname),
			   ais->type19.shiptype,
			   SHIPTYPE_DISPLAY(ais->type19.shiptype),
			   ais->type19.to_bow,
			   ais->type19.to_stern,
			   ais->type19.to_port,
			   ais->type19.to_starboard,
			   ais->type19.epfd,
			   EPFD_DISPLAY(ais->type19.epfd),
			   JSON_BOOL(ais->type19.raim),
			   ais->type19.dte,
			   JSON_BOOL(ais->type19.assigned));
	}
	break;
    case 20:			/* Data Link Management Message */
	str_appendf(buf, buflen,
		       "\"offset1\":%u,\"number1\":%u,"
		       "\"timeout1\":%u,\"increment1\":%u,"
		       "\"offset2\":%u,\"number2\":%u,"
		       "\"timeout2\":%u,\"increment2\":%u,"
		       "\"offset3\":%u,\"number3\":%u,"
		       "\"timeout3\":%u,\"increment3\":%u,"
		       "\"offset4\":%u,\"number4\":%u,"
		       "\"timeout4\":%u,\"increment4\":%u}\r\n",
		       ais->type20.offset1,
		       ais->type20.number1,
		       ais->type20.timeout1,
		       ais->type20.increment1,
		       ais->type20.offset2,
		       ais->type20.number2,
		       ais->type20.timeout2,
		       ais->type20.increment2,
		       ais->type20.offset3,
		       ais->type20.number3,
		       ais->type20.timeout3,
		       ais->type20.increment3,
		       ais->type20.offset4,
		       ais->type20.number4,
		       ais->type20.timeout4, ais->type20.increment4);
	break;
    case 21:			/* Aid to Navigation */
	if (scaled) {
	    str_appendf(buf, buflen,
			   "\"aid_type\":%u,\"aid_type_text\":\"%s\","
			   "\"name\":\"%s\",\"lon\":%.4f,"
			   "\"lat\":%.4f,\"accuracy\":%s,\"to_bow\":%u,"
			   "\"to_stern\":%u,\"to_port\":%u,\"to_starboard\":%u,"
			   "\"epfd\":%u,\"epfd_text\":\"%s\","
			   "\"second\":%u,\"regional\":%u,"
			   "\"off_position\":%s,\"raim\":%s,"
			   "\"virtual_aid\":%s}\r\n",
			   ais->type21.aid_type,
			   NAVAIDTYPE_DISPLAY(ais->type21.aid_type),
			   json_stringify(buf1, sizeof(buf1),
					  ais->type21.name),
			   ais->type21.lon / AIS_LATLON_DIV,
			   ais->type21.lat / AIS_LATLON_DIV,
			   JSON_BOOL(ais->type21.accuracy),
			   ais->type21.to_bow, ais->type21.to_stern,
			   ais->type21.to_port, ais->type21.to_starboard,
			   ais->type21.epfd,
			   EPFD_DISPLAY(ais->type21.epfd),
			   ais->type21.second,
			   ais->type21.regional,
			   JSON_BOOL(ais->type21.off_position),
			   JSON_BOOL(ais->type21.raim),
			   JSON_BOOL(ais->type21.virtual_aid));
	} else {
	    str_appendf(buf, buflen,
			   "\"aid_type\":%u,\"aid_type_text\":\"%s\","
			   "\"name\":\"%s\",\"accuracy\":%s,"
			   "\"lon\":%d,\"lat\":%d,\"to_bow\":%u,"
			   "\"to_stern\":%u,\"to_port\":%u,\"to_starboard\":%u,"
			   "\"epfd\":%u,\"epfd_text\":\"%s\","
			   "\"second\":%u,\"regional\":%u,"
			   "\"off_position\":%s,\"raim\":%s,"
			   "\"virtual_aid\":%s}\r\n",
			   ais->type21.aid_type,
			   NAVAIDTYPE_DISPLAY(ais->type21.aid_type),
			   json_stringify(buf1, sizeof(buf1),
					  ais->type21.name),
			   JSON_BOOL(ais->type21.accuracy),
			   ais->type21.lon,
			   ais->type21.lat,
			   ais->type21.to_bow,
			   ais->type21.to_stern,
			   ais->type21.to_port,
			   ais->type21.to_starboard,
			   ais->type21.epfd,
			   EPFD_DISPLAY(ais->type21.epfd),
			   ais->type21.second,
			   ais->type21.regional,
			   JSON_BOOL(ais->type21.off_position),
			   JSON_BOOL(ais->type21.raim),
			   JSON_BOOL(ais->type21.virtual_aid));
	}
	break;
    case 22:			/* Channel Management */
	str_appendf(buf, buflen,
		       "\"channel_a\":%u,\"channel_b\":%u,"
		       "\"txrx\":%u,\"power\":%s,",
		       ais->type22.channel_a,
		       ais->type22.channel_b,
		       ais->type22.txrx, JSON_BOOL(ais->type22.power));
	if (ais->type22.addressed) {
	    str_appendf(buf, buflen,
			   "\"dest1\":%u,\"dest2\":%u,",
			   ais->type22.mmsi.dest1, ais->type22.mmsi.dest2);
	} else if (scaled) {
	    str_appendf(buf, buflen,
			   "\"ne_lon\":\"%f\",\"ne_lat\":\"%f\","
			   "\"sw_lon\":\"%f\",\"sw_lat\":\"%f\",",
			   ais->type22.area.ne_lon / AIS_CHANNEL_LATLON_DIV,
			   ais->type22.area.ne_lat / AIS_CHANNEL_LATLON_DIV,
			   ais->type22.area.sw_lon / AIS_CHANNEL_LATLON_DIV,
			   ais->type22.area.sw_lat /
			   AIS_CHANNEL_LATLON_DIV);
	} else {
	    str_appendf(buf, buflen,
			   "\"ne_lon\":%d,\"ne_lat\":%d,"
			   "\"sw_lon\":%d,\"sw_lat\":%d,",
			   ais->type22.area.ne_lon,
			   ais->type22.area.ne_lat,
			   ais->type22.area.sw_lon, ais->type22.area.sw_lat);
	}
	str_appendf(buf, buflen,
		       "\"addressed\":%s,\"band_a\":%s,"
		       "\"band_b\":%s,\"zonesize\":%u}\r\n",
		       JSON_BOOL(ais->type22.addressed),
		       JSON_BOOL(ais->type22.band_a),
		       JSON_BOOL(ais->type22.band_b), ais->type22.zonesize);
	break;
    case 23:			/* Group Assignment Command */
	if (scaled) {
	    str_appendf(buf, buflen,
			   "\"ne_lon\":\"%f\",\"ne_lat\":\"%f\","
			   "\"sw_lon\":\"%f\",\"sw_lat\":\"%f\","
			   "\"stationtype\":%u,\"stationtype_text\":\"%s\","
			   "\"shiptype\":%u,\"shiptype_text\":\"%s\","
			   "\"interval\":%u,\"quiet\":%u}\r\n",
			   ais->type23.ne_lon / AIS_CHANNEL_LATLON_DIV,
			   ais->type23.ne_lat / AIS_CHANNEL_LATLON_DIV,
			   ais->type23.sw_lon / AIS_CHANNEL_LATLON_DIV,
			   ais->type23.sw_lat / AIS_CHANNEL_LATLON_DIV,
			   ais->type23.stationtype,
			   STATIONTYPE_DISPLAY(ais->type23.stationtype),
			   ais->type23.shiptype,
			   SHIPTYPE_DISPLAY(ais->type23.shiptype),
			   ais->type23.interval, ais->type23.quiet);
	} else {
	    str_appendf(buf, buflen,
			   "\"ne_lon\":%d,\"ne_lat\":%d,"
			   "\"sw_lon\":%d,\"sw_lat\":%d,"
			   "\"stationtype\":%u,\"stationtype_text\":\"%s\","
			   "\"shiptype\":%u,\"shiptype_text\":\"%s\","
			   "\"interval\":%u,\"quiet\":%u}\r\n",
			   ais->type23.ne_lon,
			   ais->type23.ne_lat,
			   ais->type23.sw_lon,
			   ais->type23.sw_lat,
			   ais->type23.stationtype,
			   STATIONTYPE_DISPLAY(ais->type23.stationtype),
			   ais->type23.shiptype,
			   SHIPTYPE_DISPLAY(ais->type23.shiptype),
			   ais->type23.interval, ais->type23.quiet);
	}
	break;
    case 24:			/* Class B CS Static Data Report */
	if (ais->type24.part != both) {
	    static char *partnames[] = {"AB", "A", "B"};
	    str_appendf(buf, buflen,
			   "\"part\":\"%s\",",
			   json_stringify(buf1, sizeof(buf1),
					  partnames[ais->type24.part]));
	}
	if (ais->type24.part != part_b)
	    str_appendf(buf, buflen,
			   "\"shipname\":\"%s\",",
			   json_stringify(buf1, sizeof(buf1),
				      ais->type24.shipname));
	if (ais->type24.part != part_a) {
	    str_appendf(buf, buflen,
			   "\"shiptype\":%u,\"shiptype_text\":\"%s\","
			   "\"vendorid\":\"%s\",\"model\":%u,\"serial\":%u,"
			   "\"callsign\":\"%s\",",
			   ais->type24.shiptype,
			   SHIPTYPE_DISPLAY(ais->type24.shiptype),
			   json_stringify(buf1, sizeof(buf1),
					  ais->type24.vendorid),
			   ais->type24.model,
			   ais->type24.serial,
			   json_stringify(buf2, sizeof(buf2),
					  ais->type24.callsign));
	    if (AIS_AUXILIARY_MMSI(ais->mmsi)) {
		str_appendf(buf, buflen,
			       "\"mothership_mmsi\":%u}\r\n",
			       ais->type24.mothership_mmsi);
	    } else {
		str_appendf(buf, buflen,
			       "\"to_bow\":%u,\"to_stern\":%u,"
			       "\"to_port\":%u,\"to_starboard\":%u",
			       ais->type24.dim.to_bow,
			       ais->type24.dim.to_stern,
			       ais->type24.dim.to_port,
			       ais->type24.dim.to_starboard);
	    }
	}
	str_rstrip_char(buf, ',');
	(void)strlcat(buf, "}\r\n", buflen);
	break;
    case 25:			/* Binary Message, Single Slot */
	str_appendf(buf, buflen,
		       "\"addressed\":%s,\"structured\":%s,\"dest_mmsi\":%u,"
		       "\"app_id\":%u,\"data\":\"%zd:%s\"}\r\n",
		       JSON_BOOL(ais->type25.addressed),
		       JSON_BOOL(ais->type25.structured),
		       ais->type25.dest_mmsi,
		       ais->type25.app_id,
		       ais->type25.bitcount,
		       gpsd_hexdump(scratchbuf, sizeof(scratchbuf),
				    (char *)ais->type25.bitdata,
				    BITS_TO_BYTES(ais->type25.bitcount)));
	break;
    case 26:			/* Binary Message, Multiple Slot */
	str_appendf(buf, buflen,
		       "\"addressed\":%s,\"structured\":%s,\"dest_mmsi\":%u,"
		       "\"app_id\":%u,\"data\":\"%zd:%s\",\"radio\":%u}\r\n",
		       JSON_BOOL(ais->type26.addressed),
		       JSON_BOOL(ais->type26.structured),
		       ais->type26.dest_mmsi,
		       ais->type26.app_id,
		       ais->type26.bitcount,
		       gpsd_hexdump(scratchbuf, sizeof(scratchbuf),
				    (char *)ais->type26.bitdata,
				    BITS_TO_BYTES(ais->type26.bitcount)),
		       ais->type26.radio);
	break;
    case 27:			/* Long Range AIS Broadcast message */
	if (scaled)
	    str_appendf(buf, buflen,
			   "\"status\":\"%s\","
			   "\"accuracy\":%s,\"lon\":%.1f,\"lat\":%.1f,"
			   "\"speed\":%u,\"course\":%u,\"raim\":%s,\"gnss\":%s}\r\n",
			   nav_legends[ais->type27.status],
			   JSON_BOOL(ais->type27.accuracy),
			   ais->type27.lon / AIS_LONGRANGE_LATLON_DIV,
			   ais->type27.lat / AIS_LONGRANGE_LATLON_DIV,
			   ais->type27.speed,
			   ais->type27.course,
			   JSON_BOOL(ais->type27.raim),
			   JSON_BOOL(ais->type27.gnss));
	else
	    str_appendf(buf, buflen,
			   "\"status\":%u,"
			   "\"accuracy\":%s,\"lon\":%d,\"lat\":%d,"
			   "\"speed\":%u,\"course\":%u,\"raim\":%s,\"gnss\":%s}\r\n",
			   ais->type27.status,
			   JSON_BOOL(ais->type27.accuracy),
			   ais->type27.lon,
			   ais->type27.lat,
			   ais->type27.speed,
			   ais->type27.course,
			   JSON_BOOL(ais->type27.raim),
			   JSON_BOOL(ais->type27.gnss));
	break;
    default:
	str_rstrip_char(buf, ',');
	(void)strlcat(buf, "}\r\n", buflen);
	break;
    }
    /*@ +formatcode +mustfreefresh @*/
}
#endif /* defined(AIVDM_ENABLE) */

#ifdef COMPASS_ENABLE
void json_att_dump(const struct gps_data_t *gpsdata,
		   /*@out@*/ char *reply, size_t replylen)
/* dump the contents of an attitude_t structure as JSON */
{
    assert(replylen > sizeof(char *));
    (void)strlcpy(reply, "{\"class\":\"ATT\",", replylen);
    str_appendf(reply, replylen, "\"device\":\"%s\",", gpsdata->dev.path);
    if (isnan(gpsdata->attitude.heading) == 0) {
	str_appendf(reply, replylen,
		       "\"heading\":%.2f,", gpsdata->attitude.heading);
	if (gpsdata->attitude.mag_st != '\0')
	    str_appendf(reply, replylen,
			   "\"mag_st\":\"%c\",", gpsdata->attitude.mag_st);

    }
    if (isnan(gpsdata->attitude.pitch) == 0) {
	str_appendf(reply, replylen,
		       "\"pitch\":%.2f,", gpsdata->attitude.pitch);
	if (gpsdata->attitude.pitch_st != '\0')
	    str_appendf(reply, replylen,
			   "\"pitch_st\":\"%c\",",
			   gpsdata->attitude.pitch_st);

    }
    if (isnan(gpsdata->attitude.yaw) == 0) {
	str_appendf(reply, replylen,
		       "\"yaw\":%.2f,", gpsdata->attitude.yaw);
	if (gpsdata->attitude.yaw_st != '\0')
	    str_appendf(reply, replylen,
			   "\"yaw_st\":\"%c\",", gpsdata->attitude.yaw_st);

    }
    if (isnan(gpsdata->attitude.roll) == 0) {
	str_appendf(reply, replylen,
		       "\"roll\":%.2f,", gpsdata->attitude.roll);
	if (gpsdata->attitude.roll_st != '\0')
	    str_appendf(reply, replylen,
			   "\"roll_st\":\"%c\",", gpsdata->attitude.roll_st);

    }

    if (isnan(gpsdata->attitude.dip) == 0)
	str_appendf(reply, replylen,
		       "\"dip\":%.3f,", gpsdata->attitude.dip);

    if (isnan(gpsdata->attitude.mag_len) == 0)
	str_appendf(reply, replylen,
		       "\"mag_len\":%.3f,", gpsdata->attitude.mag_len);
    if (isnan(gpsdata->attitude.mag_x) == 0)
	str_appendf(reply, replylen,
		       "\"mag_x\":%.3f,", gpsdata->attitude.mag_x);
    if (isnan(gpsdata->attitude.mag_y) == 0)
	str_appendf(reply, replylen,
		       "\"mag_y\":%.3f,", gpsdata->attitude.mag_y);
    if (isnan(gpsdata->attitude.mag_z) == 0)
	str_appendf(reply, replylen,
		       "\"mag_z\":%.3f,", gpsdata->attitude.mag_z);

    if (isnan(gpsdata->attitude.acc_len) == 0)
	str_appendf(reply, replylen,
		       "\"acc_len\":%.3f,", gpsdata->attitude.acc_len);
    if (isnan(gpsdata->attitude.acc_x) == 0)
	str_appendf(reply, replylen,
		       "\"acc_x\":%.3f,", gpsdata->attitude.acc_x);
    if (isnan(gpsdata->attitude.acc_y) == 0)
	str_appendf(reply, replylen,
		       "\"acc_y\":%.3f,", gpsdata->attitude.acc_y);
    if (isnan(gpsdata->attitude.acc_z) == 0)
	str_appendf(reply, replylen,
		       "\"acc_z\":%.3f,", gpsdata->attitude.acc_z);

    if (isnan(gpsdata->attitude.gyro_x) == 0)
	str_appendf(reply, replylen,
		       "\"gyro_x\":%.3f,", gpsdata->attitude.gyro_x);
    if (isnan(gpsdata->attitude.gyro_y) == 0)
	str_appendf(reply, replylen,
		       "\"gyro_y\":%.3f,", gpsdata->attitude.gyro_y);

    if (isnan(gpsdata->attitude.temp) == 0)
	str_appendf(reply, replylen,
		       "\"temp\":%.3f,", gpsdata->attitude.temp);
    if (isnan(gpsdata->attitude.depth) == 0)
	str_appendf(reply, replylen,
		       "\"depth\":%.3f,", gpsdata->attitude.depth);

    str_rstrip_char(reply, ',');
    (void)strlcat(reply, "}\r\n", replylen);
}
#endif /* COMPASS_ENABLE */

void json_data_report(const gps_mask_t changed,
		 const struct gps_device_t *session,
		 const struct policy_t *policy,
		 /*@out@*/char *buf, size_t buflen)
/* report a session state in JSON */
{
    const struct gps_data_t *datap = &session->gpsdata;
    buf[0] = '\0';

    if ((changed & REPORT_IS) != 0) {
	json_tpv_dump(session, policy, buf+strlen(buf), buflen-strlen(buf));
    }

    if ((changed & GST_SET) != 0) {
	json_noise_dump(datap, buf+strlen(buf), buflen-strlen(buf));
    }

    if ((changed & SATELLITE_SET) != 0) {
	json_sky_dump(datap, buf+strlen(buf), buflen-strlen(buf));
    }

    if ((changed & SUBFRAME_SET) != 0) {
	json_subframe_dump(datap, buf+strlen(buf), buflen-strlen(buf));
    }

#ifdef COMPASS_ENABLE
    if ((changed & ATTITUDE_SET) != 0) {
	json_att_dump(datap, buf+strlen(buf), buflen-strlen(buf));
    }
#endif /* COMPASS_ENABLE */

#ifdef RTCM104V2_ENABLE
    if ((changed & RTCM2_SET) != 0) {
	json_rtcm2_dump(&datap->rtcm2, datap->dev.path,
			buf+strlen(buf), buflen-strlen(buf));
    }
#endif /* RTCM104V2_ENABLE */

#ifdef RTCM104V3_ENABLE
    if ((changed & RTCM3_SET) != 0) {
	json_rtcm3_dump(&datap->rtcm3, datap->dev.path,
			buf+strlen(buf), buflen-strlen(buf));
    }
#endif /* RTCM104V3_ENABLE */

#ifdef AIVDM_ENABLE
    if ((changed & AIS_SET) != 0) {
	json_aivdm_dump(&datap->ais, datap->dev.path,
			policy->scaled,
			buf+strlen(buf), buflen-strlen(buf));
    }
#endif /* AIVDM_ENABLE */
}

#undef JSON_BOOL
#endif /* SOCKET_EXPORT_ENABLE */

/* gpsd_json.c ends here */
