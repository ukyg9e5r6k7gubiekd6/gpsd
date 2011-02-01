/****************************************************************************

NAME
   libgps_json.c - deserialize gpsd data coming from the server

DESCRIPTION
   This module uses the generic JSON parser to get data from JSON
representations to libgps structures.

PERMISSIONS
   Written by Eric S. Raymond, 2009
   This file is Copyright (c) 2010 by the GPSD project
   BSD terms apply: see the file COPYING in the distribution root for details.

***************************************************************************/

#include <stdbool.h>
#include <math.h>
#include <string.h>
#include <stddef.h>

#include "gpsd.h"
#include "gps_json.h"

/*
 * There's a splint limitation that parameters can be declared
 * @out@ or @null@ but not, apparently, both.  This collides with
 * the (admittedly tricky) way we use endptr. The workaround is to
 * declare it @null@ and use -compdef around the JSON reader calls.
 */
/*@-compdef@*/

static int json_tpv_read(const char *buf, struct gps_data_t *gpsdata,
			 /*@null@*/ const char **endptr)
{
    int status;
    char tbuf[JSON_DATE_MAX];
    /*@ -fullinitblock @*/
    const struct json_attr_t json_attrs_1[] = {
	/* *INDENT-OFF* */
	{"class",  t_check,   .dflt.check = "TPV"},
	{"device", t_string,  .addr.string = gpsdata->dev.path,
			         .len = sizeof(gpsdata->dev.path)},
	{"tag",    t_string,  .addr.string = gpsdata->tag,
			         .len = sizeof(gpsdata->tag)},
	{"time",   t_string,  .addr.string = tbuf,
			         .len = sizeof(tbuf)},
	{"ept",    t_real,    .addr.real = &gpsdata->fix.ept,
			         .dflt.real = NAN},
	{"lon",    t_real,    .addr.real = &gpsdata->fix.longitude,
			         .dflt.real = NAN},
	{"lat",    t_real,    .addr.real = &gpsdata->fix.latitude,
			         .dflt.real = NAN},
	{"alt",    t_real,    .addr.real = &gpsdata->fix.altitude,
			         .dflt.real = NAN},
	{"epx",    t_real,    .addr.real = &gpsdata->fix.epx,
			         .dflt.real = NAN},
	{"epy",    t_real,    .addr.real = &gpsdata->fix.epy,
			         .dflt.real = NAN},
	{"epv",    t_real,    .addr.real = &gpsdata->fix.epv,
			         .dflt.real = NAN},
	{"track",   t_real,   .addr.real = &gpsdata->fix.track,
			         .dflt.real = NAN},
	{"speed",   t_real,   .addr.real = &gpsdata->fix.speed,
			         .dflt.real = NAN},
	{"climb",   t_real,   .addr.real = &gpsdata->fix.climb,
			         .dflt.real = NAN},
	{"epd",    t_real,    .addr.real = &gpsdata->fix.epd,
			         .dflt.real = NAN},
	{"eps",    t_real,    .addr.real = &gpsdata->fix.eps,
			         .dflt.real = NAN},
	{"epc",    t_real,    .addr.real = &gpsdata->fix.epc,
			         .dflt.real = NAN},
	{"mode",   t_integer, .addr.integer = &gpsdata->fix.mode,
			         .dflt.integer = MODE_NOT_SEEN},
	{NULL},
	/* *INDENT-ON* */
    };
    /*@ +fullinitblock @*/

    status = json_read_object(buf, json_attrs_1, endptr);

    if (status == 0) {
	gpsdata->status = STATUS_FIX;
	gpsdata->set = STATUS_SET;
	/*@-usedef@*/
	if (tbuf[0] == '\0')
	    gpsdata->fix.time = NAN;
	else
	    gpsdata->fix.time = iso8601_to_unix(tbuf);
	/*@+usedef@*/
	if (isnan(gpsdata->fix.time) == 0)
	    gpsdata->set |= TIME_SET;
	if (isnan(gpsdata->fix.ept) == 0)
	    gpsdata->set |= TIMERR_SET;
	if (isnan(gpsdata->fix.longitude) == 0)
	    gpsdata->set |= LATLON_SET;
	if (isnan(gpsdata->fix.altitude) == 0)
	    gpsdata->set |= ALTITUDE_SET;
	if (isnan(gpsdata->fix.epx) == 0 && isnan(gpsdata->fix.epy) == 0)
	    gpsdata->set |= HERR_SET;
	if (isnan(gpsdata->fix.epv) == 0)
	    gpsdata->set |= VERR_SET;
	if (isnan(gpsdata->fix.track) == 0)
	    gpsdata->set |= TRACK_SET;
	if (isnan(gpsdata->fix.speed) == 0)
	    gpsdata->set |= SPEED_SET;
	if (isnan(gpsdata->fix.climb) == 0)
	    gpsdata->set |= CLIMB_SET;
	if (isnan(gpsdata->fix.epd) == 0)
	    gpsdata->set |= TRACKERR_SET;
	if (isnan(gpsdata->fix.eps) == 0)
	    gpsdata->set |= SPEEDERR_SET;
	if (isnan(gpsdata->fix.epc) == 0)
	    gpsdata->set |= CLIMBERR_SET;
	if (isnan(gpsdata->fix.epc) == 0)
	    gpsdata->set |= CLIMBERR_SET;
	if (gpsdata->fix.mode != MODE_NOT_SEEN)
	    gpsdata->set |= MODE_SET;
    }
    return status;
}

static int json_noise_read(const char *buf, struct gps_data_t *gpsdata,
                           /*@null@*/ const char **endptr)
{
    int status;
    /*@ -fullinitblock @*/
    const struct json_attr_t json_attrs_1[] = {
	/* *INDENT-OFF* */
	{"class",  t_check,   .dflt.check = "NOISE"},
	{"device", t_string,  .addr.string = gpsdata->dev.path,
			         .len = sizeof(gpsdata->dev.path)},
	{"tag",    t_string,  .addr.string = gpsdata->tag,
			         .len = sizeof(gpsdata->tag)},
	{"time",   t_real,    .addr.real = &gpsdata->noise_stats.utctime,
			         .dflt.real = NAN},
	{"rms",    t_real,    .addr.real = &gpsdata->noise_stats.rms_deviation,
			         .dflt.real = NAN},
	{"major",  t_real,    .addr.real = &gpsdata->noise_stats.smajor_deviation,
			         .dflt.real = NAN},
	{"minor",  t_real,    .addr.real = &gpsdata->noise_stats.sminor_deviation,
			         .dflt.real = NAN},
	{"orient", t_real,    .addr.real = &gpsdata->noise_stats.smajor_orientation,
			         .dflt.real = NAN},
	{"lat",    t_real,    .addr.real = &gpsdata->noise_stats.lat_err_deviation,
			         .dflt.real = NAN},
	{"longt",  t_real,    .addr.real = &gpsdata->noise_stats.longt_err_deviation,
			         .dflt.real = NAN},
	{"alt",    t_real,    .addr.real = &gpsdata->noise_stats.alt_err_deviation,
			         .dflt.real = NAN},
	{NULL},
	/* *INDENT-ON* */
    };
    /*@ +fullinitblock @*/

    status = json_read_object(buf, json_attrs_1, endptr);
    if (status != 0)
	return status;

    gpsdata->set |= NOISE_SET;
    return 0;
}

static int json_sky_read(const char *buf, struct gps_data_t *gpsdata,
			 /*@null@*/ const char **endptr)
{
    bool usedflags[MAXCHANNELS];
    char tbuf[JSON_DATE_MAX];
    /*@ -fullinitblock @*/
    const struct json_attr_t json_attrs_2_1[] = {
	/* *INDENT-OFF* */
	{"PRN",	   t_integer, .addr.integer = gpsdata->PRN},
	{"el",	   t_integer, .addr.integer = gpsdata->elevation},
	{"az",	   t_integer, .addr.integer = gpsdata->azimuth},
	{"ss",	   t_real,    .addr.real = gpsdata->ss},
	{"used",   t_boolean, .addr.boolean = usedflags},
	/* *INDENT-ON* */
	{NULL},
    };
    const struct json_attr_t json_attrs_2[] = {
	/* *INDENT-OFF* */
	{"class",      t_check,   .dflt.check = "SKY"},
	{"device",     t_string,  .addr.string  = gpsdata->dev.path,
	                             .len = sizeof(gpsdata->dev.path)},
	{"tag",	       t_string,  .addr.string  = gpsdata->tag,
	                             .len = sizeof(gpsdata->tag)},
	{"time",       t_string,  .addr.string = tbuf,
			             .len = sizeof(tbuf)},
	{"hdop",       t_real,    .addr.real    = &gpsdata->dop.hdop,
	                             .dflt.real = NAN},
	{"xdop",       t_real,    .addr.real    = &gpsdata->dop.xdop,
	                             .dflt.real = NAN},
	{"ydop",       t_real,    .addr.real    = &gpsdata->dop.ydop,
	                             .dflt.real = NAN},
	{"vdop",       t_real,    .addr.real    = &gpsdata->dop.vdop,
	                             .dflt.real = NAN},
	{"tdop",       t_real,    .addr.real    = &gpsdata->dop.tdop,
	                             .dflt.real = NAN},
	{"pdop",       t_real,    .addr.real    = &gpsdata->dop.pdop,
	                             .dflt.real = NAN},
	{"gdop",       t_real,    .addr.real    = &gpsdata->dop.gdop,
	                             .dflt.real = NAN},
	{"satellites", t_array,   .addr.array.element_type = t_object,
				     .addr.array.arr.objects.subtype=json_attrs_2_1,
	                             .addr.array.maxlen = MAXCHANNELS,
	                             .addr.array.count = &gpsdata->satellites_visible},
	{NULL},
	/* *INDENT-ON* */
    };
    /*@ +fullinitblock @*/
    int status, i, j;

    for (i = 0; i < MAXCHANNELS; i++)
	usedflags[i] = false;

    status = json_read_object(buf, json_attrs_2, endptr);
    if (status != 0)
	return status;

	/*@-usedef@*/
	if (tbuf[0] == '\0')
	    gpsdata->skyview_time = NAN;
	else
	    gpsdata->skyview_time = iso8601_to_unix(tbuf);
	/*@+usedef@*/
    gpsdata->satellites_used = 0;
    for (i = j = 0; i < MAXCHANNELS; i++) {
	if (usedflags[i]) {
	    gpsdata->used[j++] = gpsdata->PRN[i];
	    gpsdata->satellites_used++;
	}
    }

    gpsdata->set |= SATELLITE_SET;
    return 0;
}

static int json_att_read(const char *buf, struct gps_data_t *gpsdata,
			 /*@null@*/ const char **endptr)
{
    /*@ -fullinitblock @*/
    const struct json_attr_t json_attrs_1[] = {
	/* *INDENT-OFF* */
	{"class",    t_check,     .dflt.check = "ATT"},
	{"device",   t_string,    .addr.string = gpsdata->dev.path,
			             .len = sizeof(gpsdata->dev.path)},
	{"tag",      t_string,    .addr.string = gpsdata->tag,
			             .len = sizeof(gpsdata->tag)},
	{"heading",  t_real,      .addr.real = &gpsdata->attitude.heading,
			             .dflt.real = NAN},
	{"mag_st",   t_character, .addr.character = &gpsdata->attitude.mag_st},
	{"pitch",    t_real,      .addr.real = &gpsdata->attitude.pitch,
			             .dflt.real = NAN},
	{"pitch_st", t_character, .addr.character = &gpsdata->attitude.pitch_st},
	{"roll",     t_real,      .addr.real = &gpsdata->attitude.roll,
			             .dflt.real = NAN},
	{"roll_st",  t_character, .addr.character = &gpsdata->attitude.roll_st},
	{"yaw",      t_real,      .addr.real = &gpsdata->attitude.yaw,
			             .dflt.real = NAN},
	{"yaw_st",   t_character, .addr.character = &gpsdata->attitude.yaw_st},

	{"mag_len",  t_real,      .addr.real = &gpsdata->attitude.mag_len,
			             .dflt.real = NAN},
	{"mag_x",    t_real,      .addr.real = &gpsdata->attitude.mag_x,
			              .dflt.real = NAN},
	{"mag_y",    t_real,      .addr.real = &gpsdata->attitude.mag_y,
			              .dflt.real = NAN},
	{"mag_z",    t_real,      .addr.real = &gpsdata->attitude.mag_z,
			              .dflt.real = NAN},
	{"acc_len",  t_real,      .addr.real = &gpsdata->attitude.acc_len,
			             .dflt.real = NAN},
	{"acc_x",    t_real,      .addr.real = &gpsdata->attitude.acc_x,
			              .dflt.real = NAN},
	{"acc_y",    t_real,      .addr.real = &gpsdata->attitude.acc_y,
			              .dflt.real = NAN},
	{"acc_z",    t_real,      .addr.real = &gpsdata->attitude.acc_z,
			              .dflt.real = NAN},
	{"gyro_x",    t_real,      .addr.real = &gpsdata->attitude.gyro_x,
			              .dflt.real = NAN},
	{"gyro_y",    t_real,      .addr.real = &gpsdata->attitude.gyro_y,
			              .dflt.real = NAN},

	{"temp", t_real, .addr.real = &gpsdata->attitude.temp,
			         .dflt.real = NAN},
	{"depth",    t_real,    .addr.real = &gpsdata->attitude.depth,
			         .dflt.real = NAN},
	{NULL},
	/* *INDENT-ON* */
    };
    /*@ +fullinitblock @*/

    return json_read_object(buf, json_attrs_1, endptr);
}

static int json_devicelist_read(const char *buf, struct gps_data_t *gpsdata,
				/*@null@*/ const char **endptr)
{
    /*@ -fullinitblock @*/
    const struct json_attr_t json_attrs_subdevices[] = {
	/* *INDENT-OFF* */
	{"class",      t_check,      .dflt.check = "DEVICE"},
	{"path",       t_string,     STRUCTOBJECT(struct devconfig_t, path),
	                                .len = sizeof(gpsdata->devices.list[0].path)},
	{"activated",  t_real,       STRUCTOBJECT(struct devconfig_t, activated)},
	{"flags",      t_integer,	   STRUCTOBJECT(struct devconfig_t, flags)},
	{"driver",     t_string,     STRUCTOBJECT(struct devconfig_t, driver),
	                                .len = sizeof(gpsdata->devices.list[0].driver)},
	{"subtype",    t_string,     STRUCTOBJECT(struct devconfig_t, subtype),
	                                .len = sizeof(gpsdata->devices.list[0].subtype)},
	{"native",     t_integer,    STRUCTOBJECT(struct devconfig_t, driver_mode),
				        .dflt.integer = -1},
	{"bps",	       t_integer,    STRUCTOBJECT(struct devconfig_t, baudrate),
				        .dflt.integer = -1},
	{"parity",     t_character,  STRUCTOBJECT(struct devconfig_t, parity),
	                                .dflt.character = 'N'},
	{"stopbits",   t_integer,    STRUCTOBJECT(struct devconfig_t, stopbits),
				        .dflt.integer = -1},
	{"cycle",      t_real,       STRUCTOBJECT(struct devconfig_t, cycle),
				        .dflt.real = NAN},
	{"mincycle",   t_real,       STRUCTOBJECT(struct devconfig_t, mincycle),
				        .dflt.real = NAN},
	{NULL},
	/* *INDENT-ON* */
    };
    /*@-type@*//* STRUCTARRAY confuses splint */
    const struct json_attr_t json_attrs_devices[] = {
	{"class", t_check,.dflt.check = "DEVICES"},
	{"devices", t_array, STRUCTARRAY(gpsdata->devices.list,
					 json_attrs_subdevices,
					 &gpsdata->devices.ndevices)},
	{NULL},
    };
    /*@+type@*/
    /*@ +fullinitblock @*/
    int status;

    memset(&gpsdata->devices, '\0', sizeof(gpsdata->devices));
    status = json_read_object(buf, json_attrs_devices, endptr);
    if (status != 0) {
	return status;
    }

    gpsdata->devices.time = timestamp();
    gpsdata->set &= ~UNION_SET;
    gpsdata->set |= DEVICELIST_SET;
    return 0;
}

static int json_version_read(const char *buf, struct gps_data_t *gpsdata,
			     /*@null@*/ const char **endptr)
{
    /*@ -fullinitblock @*/
    const struct json_attr_t json_attrs_version[] = {
	/* *INDENT-OFF* */
        {"class",     t_check,   .dflt.check = "VERSION"},
	{"release",   t_string,  .addr.string  = gpsdata->version.release,
	                            .len = sizeof(gpsdata->version.release)},
	{"rev",       t_string,  .addr.string  = gpsdata->version.rev,
	                            .len = sizeof(gpsdata->version.rev)},
	{"proto_major", t_integer, .addr.integer = &gpsdata->version.proto_major},
	{"proto_minor", t_integer, .addr.integer = &gpsdata->version.proto_minor},
	{NULL},
	/* *INDENT-ON* */
    };
    /*@ +fullinitblock @*/
    int status;

    memset(&gpsdata->version, '\0', sizeof(gpsdata->version));
    status = json_read_object(buf, json_attrs_version, endptr);
    if (status != 0)
	return status;

    gpsdata->set &= ~UNION_SET;
    gpsdata->set |= VERSION_SET;
    return 0;
}

static int json_error_read(const char *buf, struct gps_data_t *gpsdata,
			   /*@null@*/ const char **endptr)
{
    /*@ -fullinitblock @*/
    const struct json_attr_t json_attrs_error[] = {
	/* *INDENT-OFF* */
        {"class",     t_check,   .dflt.check = "ERROR"},
	{"message",   t_string,  .addr.string  = gpsdata->error,
	                            .len = sizeof(gpsdata->error)},
	{NULL},
	/* *INDENT-ON* */
    };
    /*@ +fullinitblock @*/
    int status;

    memset(&gpsdata->error, '\0', sizeof(gpsdata->error));
    status = json_read_object(buf, json_attrs_error, endptr);
    if (status != 0)
	return status;

    gpsdata->set &= ~UNION_SET;
    gpsdata->set |= ERROR_SET;
    return 0;
}

int libgps_json_unpack(const char *buf,
		       struct gps_data_t *gpsdata, const char **end)
/* the only entry point - unpack a JSON object into gpsdata_t substructures */
{
    int status;
    char *classtag = strstr(buf, "\"class\":");

    if (classtag == NULL)
	return -1;
#define STARTSWITH(str, prefix)	strncmp(str, prefix, sizeof(prefix)-1)==0
    if (STARTSWITH(classtag, "\"class\":\"TPV\"")) {
	return json_tpv_read(buf, gpsdata, end);
    } else if (STARTSWITH(classtag, "\"class\":\"NOISE\"")) {
	return json_noise_read(buf, gpsdata, end);
    } else if (STARTSWITH(classtag, "\"class\":\"SKY\"")) {
	return json_sky_read(buf, gpsdata, end);
    } else if (STARTSWITH(classtag, "\"class\":\"ATT\"")) {
	return json_att_read(buf, gpsdata, end);
    } else if (STARTSWITH(classtag, "\"class\":\"DEVICES\"")) {
	return json_devicelist_read(buf, gpsdata, end);
    } else if (STARTSWITH(classtag, "\"class\":\"DEVICE\"")) {
	status = json_device_read(buf, &gpsdata->dev, end);
	if (status == 0)
	    gpsdata->set |= DEVICE_SET;
	return status;
    } else if (STARTSWITH(classtag, "\"class\":\"WATCH\"")) {
	status = json_watch_read(buf, &gpsdata->policy, end);
	if (status == 0)
	    gpsdata->set |= POLICY_SET;
	return status;
    } else if (STARTSWITH(classtag, "\"class\":\"VERSION\"")) {
	return json_version_read(buf, gpsdata, end);
#ifdef RTCM104V2_ENABLE
    } else if (STARTSWITH(classtag, "\"class\":\"RTCM2\"")) {
	status = json_rtcm2_read(buf,
				 gpsdata->dev.path, sizeof(gpsdata->dev.path),
				 &gpsdata->rtcm2, end);
	if (status == 0) {
	    gpsdata->set &= ~UNION_SET;
	    gpsdata->set |= RTCM2_SET;
	}
	return status;
#endif /* RTCM104V2_ENABLE */
#ifdef AIVDM_ENABLE
    } else if (STARTSWITH(classtag, "\"class\":\"AIS\"")) {
	status = json_ais_read(buf,
			       gpsdata->dev.path, sizeof(gpsdata->dev.path),
			       &gpsdata->ais, end);
	if (status == 0) {
	    gpsdata->set &= ~UNION_SET;
	    gpsdata->set |= AIS_SET;
	}
	return status;
#endif /* AIVDM_ENABLE */
    } else if (STARTSWITH(classtag, "\"class\":\"ERROR\"")) {
	return json_error_read(buf, gpsdata, end);
    } else
	return -1;
#undef STARTSWITH
}

/*@+compdef@*/

/* libgps_json.c ends here */
