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
#include "strfuncs.h"
#ifdef SOCKET_EXPORT_ENABLE
#include "gps_json.h"

static int json_tpv_read(const char *buf, struct gps_data_t *gpsdata,
			 const char **endptr)
{
    const struct json_attr_t json_attrs_1[] = {
	/* *INDENT-OFF* */
	{"class",  t_check,   .dflt.check = "TPV"},
	{"device", t_string,  .addr.string = gpsdata->dev.path,
			         .len = sizeof(gpsdata->dev.path)},
	{"time",   t_time,    .addr.real = &gpsdata->fix.time,
			         .dflt.real = NAN},
	{"time",   t_real,    .addr.real = &gpsdata->fix.time,
			         .dflt.real = NAN},
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

    return json_read_object(buf, json_attrs_1, endptr);
}

static int json_noise_read(const char *buf, struct gps_data_t *gpsdata,
                           const char **endptr)
{
    const struct json_attr_t json_attrs_1[] = {
	/* *INDENT-OFF* */
	{"class",  t_check,   .dflt.check = "GST"},
	{"device", t_string,  .addr.string = gpsdata->dev.path,
			         .len = sizeof(gpsdata->dev.path)},
	{"time",   t_time,    .addr.real = &gpsdata->gst.utctime,
			         .dflt.real = NAN},
	{"time",   t_real,    .addr.real = &gpsdata->gst.utctime,
			         .dflt.real = NAN},
	{"rms",    t_real,    .addr.real = &gpsdata->gst.rms_deviation,
			         .dflt.real = NAN},
	{"major",  t_real,    .addr.real = &gpsdata->gst.smajor_deviation,
			         .dflt.real = NAN},
	{"minor",  t_real,    .addr.real = &gpsdata->gst.sminor_deviation,
			         .dflt.real = NAN},
	{"orient", t_real,    .addr.real = &gpsdata->gst.smajor_orientation,
			         .dflt.real = NAN},
	{"lat",    t_real,    .addr.real = &gpsdata->gst.lat_err_deviation,
			         .dflt.real = NAN},
	{"lon",    t_real,    .addr.real = &gpsdata->gst.lon_err_deviation,
			         .dflt.real = NAN},
	{"alt",    t_real,    .addr.real = &gpsdata->gst.alt_err_deviation,
			         .dflt.real = NAN},
	{NULL},
	/* *INDENT-ON* */
    };

    return json_read_object(buf, json_attrs_1, endptr);
}

static int json_sky_read(const char *buf, struct gps_data_t *gpsdata,
			 const char **endptr)
{
    const struct json_attr_t json_attrs_satellites[] = {
	/* *INDENT-OFF* */
	{"PRN",	   t_short, STRUCTOBJECT(struct satellite_t, PRN)},
	{"el",	   t_short, STRUCTOBJECT(struct satellite_t, elevation)},
	{"az",	   t_short, STRUCTOBJECT(struct satellite_t, azimuth)},
	{"ss",	   t_real,    STRUCTOBJECT(struct satellite_t, ss)},
	{"used",   t_boolean, STRUCTOBJECT(struct satellite_t, used)},
	/* *INDENT-ON* */
	{NULL},
    };
    const struct json_attr_t json_attrs_2[] = {
	/* *INDENT-OFF* */
	{"class",      t_check,   .dflt.check = "SKY"},
	{"device",     t_string,  .addr.string  = gpsdata->dev.path,
	                             .len = sizeof(gpsdata->dev.path)},
	{"time",       t_time,    .addr.real = &gpsdata->skyview_time,
	      	                     .dflt.real = NAN},
	{"time",       t_real,    .addr.real = &gpsdata->skyview_time,
	      	                     .dflt.real = NAN},
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
	{"satellites", t_array,
	                           STRUCTARRAY(gpsdata->skyview,
					 json_attrs_satellites,
					 &gpsdata->satellites_visible)},
	{NULL},
	/* *INDENT-ON* */
    };
    int status, i, j;

    for (i = 0; i < MAXCHANNELS; i++) {
	gpsdata->skyview[i].PRN = 0;
	gpsdata->skyview[i].used = false;
    }

    status = json_read_object(buf, json_attrs_2, endptr);
    if (status != 0)
	return status;

    gpsdata->satellites_used = 0;
    gpsdata->satellites_visible = 0;
    for (i = j = 0; i < MAXCHANNELS; i++) {
	if(gpsdata->skyview[i].PRN > 0)
	    gpsdata->satellites_visible++;
	if (gpsdata->skyview[i].used) {
	    gpsdata->satellites_used++;
	}
    }

    return 0;
}

static int json_att_read(const char *buf, struct gps_data_t *gpsdata,
			 const char **endptr)
{
    const struct json_attr_t json_attrs_1[] = {
	/* *INDENT-OFF* */
	{"class",    t_check,     .dflt.check = "ATT"},
	{"device",   t_string,    .addr.string = gpsdata->dev.path,
			             .len = sizeof(gpsdata->dev.path)},
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

	{"dip",      t_real,      .addr.real = &gpsdata->attitude.dip,
			             .dflt.real = NAN},
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

    return json_read_object(buf, json_attrs_1, endptr);
}

static int json_devicelist_read(const char *buf, struct gps_data_t *gpsdata,
				const char **endptr)
{
    const struct json_attr_t json_attrs_subdevices[] = {
	/* *INDENT-OFF* */
	{"class",      t_check,      .dflt.check = "DEVICE"},
	{"path",       t_string,     STRUCTOBJECT(struct devconfig_t, path),
	                                .len = sizeof(gpsdata->devices.list[0].path)},
	{"activated",  t_time,       STRUCTOBJECT(struct devconfig_t, activated)},
	{"activated",  t_real,       STRUCTOBJECT(struct devconfig_t, activated)},
	{"flags",      t_integer,    STRUCTOBJECT(struct devconfig_t, flags)},
	{"driver",     t_string,     STRUCTOBJECT(struct devconfig_t, driver),
	                                .len = sizeof(gpsdata->devices.list[0].driver)},
	{"subtype",    t_string,     STRUCTOBJECT(struct devconfig_t, subtype),
	                                .len = sizeof(gpsdata->devices.list[0].subtype)},
	{"native",     t_integer,    STRUCTOBJECT(struct devconfig_t, driver_mode),
				        .dflt.integer = -1},
	{"bps",	       t_uinteger,   STRUCTOBJECT(struct devconfig_t, baudrate),
				        .dflt.uinteger = DEVDEFAULT_BPS},
	{"parity",     t_character,  STRUCTOBJECT(struct devconfig_t, parity),
	                                .dflt.character = DEVDEFAULT_PARITY},
	{"stopbits",   t_uinteger,   STRUCTOBJECT(struct devconfig_t, stopbits),
				        .dflt.integer = DEVDEFAULT_STOPBITS},
	{"cycle",      t_real,       STRUCTOBJECT(struct devconfig_t, cycle),
				        .dflt.real = NAN},
	{"mincycle",   t_real,       STRUCTOBJECT(struct devconfig_t, mincycle),
				        .dflt.real = NAN},
	{NULL},
	/* *INDENT-ON* */
    };
    const struct json_attr_t json_attrs_devices[] = {
	{"class", t_check,.dflt.check = "DEVICES"},
	{"devices", t_array, STRUCTARRAY(gpsdata->devices.list,
					 json_attrs_subdevices,
					 &gpsdata->devices.ndevices)},
	{NULL},
    };
    int status;

    memset(&gpsdata->devices, '\0', sizeof(gpsdata->devices));
    status = json_read_object(buf, json_attrs_devices, endptr);
    if (status != 0) {
	return status;
    }

    gpsdata->devices.time = timestamp();
    return 0;
}

static int json_version_read(const char *buf, struct gps_data_t *gpsdata,
			     const char **endptr)
{
    const struct json_attr_t json_attrs_version[] = {
	/* *INDENT-OFF* */
        {"class",     t_check,   .dflt.check = "VERSION"},
	{"release",   t_string,  .addr.string  = gpsdata->version.release,
	                            .len = sizeof(gpsdata->version.release)},
	{"rev",       t_string,  .addr.string  = gpsdata->version.rev,
	                            .len = sizeof(gpsdata->version.rev)},
	{"proto_major", t_integer, .addr.integer = &gpsdata->version.proto_major},
	{"proto_minor", t_integer, .addr.integer = &gpsdata->version.proto_minor},
	{"remote",    t_string,  .addr.string  = gpsdata->version.remote,
	                            .len = sizeof(gpsdata->version.remote)},
	{NULL},
	/* *INDENT-ON* */
    };
    int status;

    memset(&gpsdata->version, '\0', sizeof(gpsdata->version));
    status = json_read_object(buf, json_attrs_version, endptr);

    return status;
}

static int json_error_read(const char *buf, struct gps_data_t *gpsdata,
			   const char **endptr)
{
    const struct json_attr_t json_attrs_error[] = {
	/* *INDENT-OFF* */
        {"class",     t_check,   .dflt.check = "ERROR"},
	{"message",   t_string,  .addr.string  = gpsdata->error,
	                            .len = sizeof(gpsdata->error)},
	{NULL},
	/* *INDENT-ON* */
    };
    int status;

    memset(&gpsdata->error, '\0', sizeof(gpsdata->error));
    status = json_read_object(buf, json_attrs_error, endptr);
    if (status != 0)
	return status;

    return status;
}

int json_toff_read(const char *buf, struct gps_data_t *gpsdata,
			   const char **endptr)
{
    int real_sec = 0, real_nsec = 0, clock_sec = 0, clock_nsec = 0;
    const struct json_attr_t json_attrs_toff[] = {
	/* *INDENT-OFF* */
        {"class",     t_check,   .dflt.check = "TOFF"},
	{"device",    t_string,  .addr.string = gpsdata->dev.path,
			         .len = sizeof(gpsdata->dev.path)},
	{"real_sec",  t_integer, .addr.integer = &real_sec,
			         .dflt.integer = 0},
	{"real_nsec", t_integer, .addr.integer = &real_nsec,
			         .dflt.integer = 0},
	{"clock_sec", t_integer, .addr.integer = &clock_sec,
			         .dflt.integer = 0},
	{"clock_nsec",t_integer, .addr.integer = &clock_nsec,
			         .dflt.integer = 0},
	{NULL},
	/* *INDENT-ON* */
    };
    int status;

    memset(&gpsdata->toff, '\0', sizeof(gpsdata->toff));
    status = json_read_object(buf, json_attrs_toff, endptr);
    gpsdata->toff.real.tv_sec = (time_t)real_sec;
    gpsdata->toff.real.tv_nsec = (long)real_nsec;
    gpsdata->toff.clock.tv_sec = (time_t)clock_sec;
    gpsdata->toff.clock.tv_nsec = (long)clock_nsec;
    if (status != 0)
	return status;

    return status;
}

int json_pps_read(const char *buf, struct gps_data_t *gpsdata,
		  const char **endptr)
{
    int real_sec = 0, real_nsec = 0, clock_sec = 0, clock_nsec = 0, precision=0;
    const struct json_attr_t json_attrs_pps[] = {
	/* *INDENT-OFF* */
        {"class",     t_check,   .dflt.check = "PPS"},
	{"device",    t_string,  .addr.string = gpsdata->dev.path,
			         .len = sizeof(gpsdata->dev.path)},
	{"real_sec",  t_integer, .addr.integer = &real_sec,
			         .dflt.integer = 0},
	{"real_nsec", t_integer, .addr.integer = &real_nsec,
			         .dflt.integer = 0},
	{"clock_sec", t_integer, .addr.integer = &clock_sec,
			         .dflt.integer = 0},
	{"clock_nsec",t_integer, .addr.integer = &clock_nsec,
			         .dflt.integer = 0},
	{"precision", t_integer, .addr.integer = &precision,
			         .dflt.integer = 0},
	{NULL},
	/* *INDENT-ON* */
    };
    int status;

    memset(&gpsdata->pps, '\0', sizeof(gpsdata->pps));
    status = json_read_object(buf, json_attrs_pps, endptr);

    /* This is good until GPS are more than nanosec accurate */
    gpsdata->pps.real.tv_sec = (time_t)real_sec;
    gpsdata->pps.real.tv_nsec = (long)real_nsec;
    gpsdata->pps.clock.tv_sec = (time_t)clock_sec;
    gpsdata->pps.clock.tv_nsec = (long)clock_nsec;
    /* FIXME! find a place to stash precision */
    if (status != 0)
	return status;

    /* FIXME: precision is currently parsed but discarded */
    return status;
}

int json_oscillator_read(const char *buf, struct gps_data_t *gpsdata,
			 const char **endptr)
{
    bool running = false, reference = false, disciplined = false;
    int delta = 0;
    const struct json_attr_t json_attrs_osc[] = {
	/* *INDENT-OFF* */
        {"class",	t_check,   .dflt.check = "OSC"},
	{"device",	t_string,  .addr.string = gpsdata->dev.path,
				   .len = sizeof(gpsdata->dev.path)},
	{"running",	t_boolean, .addr.boolean = &running,
				   .dflt.boolean = false},
	{"reference",	t_boolean, .addr.boolean = &reference,
				   .dflt.boolean = false},
	{"disciplined",	t_boolean, .addr.boolean = &disciplined,
				   .dflt.boolean = false},
	{"delta",	t_integer, .addr.integer = &delta,
				   .dflt.integer = 0},
	{NULL},
	/* *INDENT-ON* */
    };
    int status;

    memset(&gpsdata->osc, '\0', sizeof(gpsdata->osc));
    status = json_read_object(buf, json_attrs_osc, endptr);

    gpsdata->osc.running = running;
    gpsdata->osc.reference = reference;
    gpsdata->osc.disciplined = disciplined;
    gpsdata->osc.delta = delta;

    return status;
}

int libgps_json_unpack(const char *buf,
		       struct gps_data_t *gpsdata, const char **end)
/* the only entry point - unpack a JSON object into gpsdata_t substructures */
{
    int status;
    char *classtag = strstr(buf, "\"class\":");

    if (classtag == NULL)
	return -1;
    if (str_starts_with(classtag, "\"class\":\"TPV\"")) {
	status = json_tpv_read(buf, gpsdata, end);
	gpsdata->status = STATUS_FIX;
	gpsdata->set = STATUS_SET;
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
	return status;
    } else if (str_starts_with(classtag, "\"class\":\"GST\"")) {
	status = json_noise_read(buf, gpsdata, end);
	if (status == 0) {
	    gpsdata->set &= ~UNION_SET;
	    gpsdata->set |= GST_SET;
	}
	return status;
    } else if (str_starts_with(classtag, "\"class\":\"SKY\"")) {
	status = json_sky_read(buf, gpsdata, end);
	if (status == 0)
	    gpsdata->set |= SATELLITE_SET;
	return status;
    } else if (str_starts_with(classtag, "\"class\":\"ATT\"")) {
	status = json_att_read(buf, gpsdata, end);
	if (status == 0) {
	    gpsdata->set &= ~UNION_SET;
	    gpsdata->set |= ATTITUDE_SET;
	}
	return status;
    } else if (str_starts_with(classtag, "\"class\":\"DEVICES\"")) {
	status = json_devicelist_read(buf, gpsdata, end);
	if (status == 0) {
	    gpsdata->set &= ~UNION_SET;
	    gpsdata->set |= DEVICELIST_SET;
	}
	return status;
    } else if (str_starts_with(classtag, "\"class\":\"DEVICE\"")) {
	status = json_device_read(buf, &gpsdata->dev, end);
	if (status == 0)
	    gpsdata->set |= DEVICE_SET;
	return status;
    } else if (str_starts_with(classtag, "\"class\":\"WATCH\"")) {
	status = json_watch_read(buf, &gpsdata->policy, end);
	if (status == 0) {
	    gpsdata->set &= ~UNION_SET;
	    gpsdata->set |= POLICY_SET;
	}
	return status;
    } else if (str_starts_with(classtag, "\"class\":\"VERSION\"")) {
	status = json_version_read(buf, gpsdata, end);
	if (status ==  0) {
	    gpsdata->set &= ~UNION_SET;
	    gpsdata->set |= VERSION_SET;
	}
	return status;
#ifdef RTCM104V2_ENABLE
    } else if (str_starts_with(classtag, "\"class\":\"RTCM2\"")) {
	status = json_rtcm2_read(buf,
				 gpsdata->dev.path, sizeof(gpsdata->dev.path),
				 &gpsdata->rtcm2, end);
	if (status == 0) {
	    gpsdata->set &= ~UNION_SET;
	    gpsdata->set |= RTCM2_SET;
	}
	return status;
#endif /* RTCM104V2_ENABLE */
#ifdef RTCM104V3_ENABLE
    } else if (str_starts_with(classtag, "\"class\":\"RTCM3\"")) {
	status = json_rtcm3_read(buf,
				 gpsdata->dev.path, sizeof(gpsdata->dev.path),
				 &gpsdata->rtcm3, end);
	if (status == 0) {
	    gpsdata->set &= ~UNION_SET;
	    gpsdata->set |= RTCM3_SET;
	}
	return status;
#endif /* RTCM104V3_ENABLE */
#ifdef AIVDM_ENABLE
    } else if (str_starts_with(classtag, "\"class\":\"AIS\"")) {
	status = json_ais_read(buf,
			       gpsdata->dev.path, sizeof(gpsdata->dev.path),
			       &gpsdata->ais, end);
	if (status == 0) {
	    gpsdata->set &= ~UNION_SET;
	    gpsdata->set |= AIS_SET;
	}
	return status;
#endif /* AIVDM_ENABLE */
    } else if (str_starts_with(classtag, "\"class\":\"ERROR\"")) {
	status = json_error_read(buf, gpsdata, end);
	if (status == 0) {
	    gpsdata->set &= ~UNION_SET;
	    gpsdata->set |= ERROR_SET;
	}
	return status;
    } else if (str_starts_with(classtag, "\"class\":\"TOFF\"")) {
	status = json_pps_read(buf, gpsdata, end);
	if (status == 0) {
	    gpsdata->set &= ~UNION_SET;
	    gpsdata->set |= TOFF_SET;
	}
	return status;
    } else if (str_starts_with(classtag, "\"class\":\"PPS\"")) {
	status = json_pps_read(buf, gpsdata, end);
	if (status == 0) {
	    gpsdata->set &= ~UNION_SET;
	    gpsdata->set |= PPS_SET;
	}
	return status;
    } else if (str_starts_with(classtag, "\"class\":\"OSC\"")) {
	status = json_oscillator_read(buf, gpsdata, end);
	if (status == 0) {
	    gpsdata->set &= ~UNION_SET;
	    gpsdata->set |= OSCILLATOR_SET;
	}
	return status;
    } else
	return -1;
}


#endif /* SOCKET_EXPORT_ENABLE */

/* libgps_json.c ends here */
