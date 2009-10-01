/****************************************************************************

NAME
   libgps_json.c - deserialize gpsd data coming from the server

DESCRIPTION
   This module uses the generic JSON parser to get data from JSON
representations to libgps structures.

***************************************************************************/

#include <math.h>
#include <assert.h>
#include <string.h>
#include <stddef.h>
#include <stdio.h>

#include "gpsd_config.h"
#include "gpsd.h"
#include "gps_json.h"

/*
 * There's a splint limitation that parameters can be declared
 * @out@ or @null@ but not, apparently, both.  This collides with
 * the (admittedly tricky) way we use endptr. The workaround is to
 * declare it @null@ and use -compdef around the JSON reader calls.
 */
/*@-compdef@*/

static int json_tpv_read(const char *buf, 
			 struct gps_data_t *gpsdata, 
			 /*@null@*/const char **endptr)
{
    int status;
    /*@ -fullinitblock @*/
    const struct json_attr_t json_attrs_1[] = {
	{"class",  check,   .dflt.check = "TPV"},
	{"device", string,  .addr.string = gpsdata->dev.path,
			    .len = sizeof(gpsdata->dev.path)},
	{"tag",    string,  .addr.string = gpsdata->tag,
			    .len = sizeof(gpsdata->tag)},
	{"time",   real,    .addr.real = &gpsdata->fix.time,
			    .dflt.real = NAN},
	{"ept",    real,    .addr.real = &gpsdata->fix.ept,
			    .dflt.real = NAN},
	{"lon",    real,    .addr.real = &gpsdata->fix.longitude,
			    .dflt.real = NAN},
	{"lat",    real,    .addr.real = &gpsdata->fix.latitude,
			    .dflt.real = NAN},
	{"alt",    real,    .addr.real = &gpsdata->fix.altitude,
			    .dflt.real = NAN},
	{"epx",    real,    .addr.real = &gpsdata->fix.epx,
			    .dflt.real = NAN},
	{"epy",    real,    .addr.real = &gpsdata->fix.epy,
			    .dflt.real = NAN},
	{"epv",    real,    .addr.real = &gpsdata->fix.epv,
			    .dflt.real = NAN},
	{"track",   real,   .addr.real = &gpsdata->fix.track,
			    .dflt.real = NAN},
	{"speed",   real,   .addr.real = &gpsdata->fix.speed,
			    .dflt.real = NAN},
	{"climb",   real,   .addr.real = &gpsdata->fix.climb,
			    .dflt.real = NAN},
	{"epd",    real,    .addr.real = &gpsdata->fix.epd,
			    .dflt.real = NAN},
	{"eps",    real,    .addr.real = &gpsdata->fix.eps,
			    .dflt.real = NAN},
	{"epc",    real,    .addr.real = &gpsdata->fix.epc,
			    .dflt.real = NAN},
	{"mode",   integer, .addr.integer = &gpsdata->fix.mode,
			    .dflt.integer = MODE_NOT_SEEN},
	{NULL},
    };
    /*@ +fullinitblock @*/

    status = json_read_object(buf, json_attrs_1, endptr);

    if (status == 0) {
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
	if (isnan(gpsdata->fix.epx)==0 && isnan(gpsdata->fix.epy)==0)
	    gpsdata->set |= HERR_SET;
	if (isnan(gpsdata->fix.epv)==0)
	    gpsdata->set |= VERR_SET;
	if (isnan(gpsdata->fix.track)==0)
	    gpsdata->set |= TRACK_SET;
	if (isnan(gpsdata->fix.speed)==0)
	    gpsdata->set |= SPEED_SET;
	if (isnan(gpsdata->fix.climb)==0)
	    gpsdata->set |= CLIMB_SET;
	if (isnan(gpsdata->fix.epd)==0)
	    gpsdata->set |= TRACKERR_SET;
	if (isnan(gpsdata->fix.eps)==0)
	    gpsdata->set |= SPEEDERR_SET;
	if (isnan(gpsdata->fix.epc)==0)
	    gpsdata->set |= CLIMBERR_SET;
	if (isnan(gpsdata->fix.epc)==0)
	    gpsdata->set |= CLIMBERR_SET;
	if (gpsdata->fix.mode != MODE_NOT_SEEN)
	    gpsdata->set |= MODE_SET;
    }
    return status;
}

static int json_sky_read(const char *buf, 
			 struct gps_data_t *gpsdata, 
			 /*@null@*/const char **endptr)
{
    bool usedflags[MAXCHANNELS];
    /*@ -fullinitblock @*/
    const struct json_attr_t json_attrs_2_1[] = {
	{"PRN",	   integer, .addr.integer = gpsdata->PRN},
	{"el",	   integer, .addr.integer = gpsdata->elevation},
	{"az",	   integer, .addr.integer = gpsdata->azimuth},
	{"ss",	   real,    .addr.real = gpsdata->ss},
	{"used",   boolean, .addr.boolean = usedflags},
	{NULL},
    };
    const struct json_attr_t json_attrs_2[] = {
	{"class",      check,   .dflt.check = "SKY"},
	{"device",     string,  .addr.string  = gpsdata->dev.path,
				.len = PATH_MAX},
	{"tag",	       string,  .addr.string  = gpsdata->tag,
				.len = MAXTAGLEN},
	{"time",       real,    .addr.real    = &gpsdata->fix.time},
	{"hdop",       real,    .addr.real    = &gpsdata->dop.hdop,
	                        .dflt.real = NAN},
	{"xdop",       real,    .addr.real    = &gpsdata->dop.xdop,
	                        .dflt.real = NAN},
	{"ydop",       real,    .addr.real    = &gpsdata->dop.ydop,
	                        .dflt.real = NAN},
	{"vdop",       real,    .addr.real    = &gpsdata->dop.vdop,
	                        .dflt.real = NAN},
	{"tdop",       real,    .addr.real    = &gpsdata->dop.tdop,
	                        .dflt.real = NAN},
	{"pdop",       real,    .addr.real    = &gpsdata->dop.pdop,
	                        .dflt.real = NAN},
	{"gdop",       real,    .addr.real    = &gpsdata->dop.gdop,
	                        .dflt.real = NAN},
	{"satellites", array,   .addr.array.element_type = object,
				.addr.array.arr.objects.subtype=json_attrs_2_1,
	                        .addr.array.maxlen = MAXCHANNELS,
	                        .addr.array.count = &gpsdata->satellites_visible},
	{NULL},
    };
    /*@ +fullinitblock @*/
    int status, i, j;

    for (i = 0; i < MAXCHANNELS; i++)
	usedflags[i] = false;

    status = json_read_object(buf, json_attrs_2, endptr);
    if (status != 0)
	return status;

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

static int json_devicelist_read(const char *buf, 
				struct gps_data_t *gpsdata, 
				/*@null@*/const char **endptr)
{
    /*@ -fullinitblock @*/
    const struct json_attr_t json_attrs_subdevices[] = {
	{"class",      check,      .dflt.check = "DEVICE"},
	{"path",       string,     STRUCTOBJECT(struct devconfig_t, path),
	                           .len = sizeof(gpsdata->devices.list[0].path)},
	{"activated",  real,       STRUCTOBJECT(struct devconfig_t, activated)},
	{"flags",      integer,	   STRUCTOBJECT(struct devconfig_t, flags)},
	{"driver",     string,     STRUCTOBJECT(struct devconfig_t, driver),
	                           .len = sizeof(gpsdata->devices.list[0].driver)},
	{"subtype",    string,     STRUCTOBJECT(struct devconfig_t, subtype),
	                           .len = sizeof(gpsdata->devices.list[0].subtype)},
	{"native",     integer,    STRUCTOBJECT(struct devconfig_t, driver_mode),
				   .dflt.integer = -1},
	{"bps",	       integer,    STRUCTOBJECT(struct devconfig_t, baudrate),
				   .dflt.integer = -1},
	{"parity",     character,  STRUCTOBJECT(struct devconfig_t, parity),
	                           .dflt.character = 'N'},
	{"stopbits",   integer,    STRUCTOBJECT(struct devconfig_t, stopbits),
				   .dflt.integer = -1},
	{"cycle",      real,       STRUCTOBJECT(struct devconfig_t, cycle),
				   .dflt.real = NAN},
	{"mincycle",   real,       STRUCTOBJECT(struct devconfig_t, mincycle),
				   .dflt.real = NAN},
	{NULL},
    };
    /*@-type@*//* STRUCTARRAY confuses splint */
    const struct json_attr_t json_attrs_devices[] = {
        {"class",   check,  .dflt.check = "DEVICES"},
        {"devices", array,  STRUCTARRAY(gpsdata->devices.list,
					json_attrs_subdevices,
					&gpsdata->devices.ndevices)},
	{NULL},
    };
    /*@+type@*/
    /*@ +fullinitblock @*/
    int status;

    memset(&gpsdata->devices, '\0', sizeof(gpsdata->devices));
    status = json_read_object(buf, json_attrs_devices, endptr);
    if (status != 0)
	return status;

    gpsdata->devices.time = timestamp();
    gpsdata->set |= DEVICELIST_SET;
    return 0;
}

static int json_version_read(const char *buf, 
			     struct gps_data_t *gpsdata, 
			     /*@null@*/const char **endptr)
{
    /*@ -fullinitblock @*/
    const struct json_attr_t json_attrs_version[] = {
        {"class",     check,   .dflt.check = "VERSION"},
	{"release",   string,  .addr.string  = gpsdata->version.release,
	                       .len = sizeof(gpsdata->version.release)},
	{"rev",       string,  .addr.string  = gpsdata->version.rev,
	                       .len = sizeof(gpsdata->version.rev)},
	{"proto_major", integer, .addr.integer = &gpsdata->version.proto_major},
	{"proto_minor", integer, .addr.integer = &gpsdata->version.proto_minor},
	{NULL},
    };
    /*@ +fullinitblock @*/
    int status;

    memset(&gpsdata->version, '\0', sizeof(gpsdata->version));
    status = json_read_object(buf, json_attrs_version, endptr);
    if (status != 0)
	return status;

    gpsdata->set |= VERSION_SET;
    return 0;
}

static int json_error_read(const char *buf, 
			   struct gps_data_t *gpsdata, 
			   /*@null@*/const char **endptr)
{
    /*@ -fullinitblock @*/
    const struct json_attr_t json_attrs_error[] = {
        {"class",     check,   .dflt.check = "ERROR"},
	{"message",   string,  .addr.string  = gpsdata->error,
	                       .len = sizeof(gpsdata->error)},
	{NULL},
    };
    /*@ +fullinitblock @*/
    int status;

    memset(&gpsdata->error, '\0', sizeof(gpsdata->error));
    status = json_read_object(buf, json_attrs_error, endptr);
    if (status != 0)
	return status;

    gpsdata->set |= ERR_SET;
    return 0;
}

int libgps_json_unpack(const char *buf, struct gps_data_t *gpsdata)
/* the only entry point - unpack a JSON object into gpsdata_t substructures */
{
    int status;
    if (strstr(buf, "\"class\":\"TPV\"") != 0) {
	return json_tpv_read(buf, gpsdata, NULL);
    } else if (strstr(buf, "\"class\":\"SKY\"") != 0) {
	return json_sky_read(buf, gpsdata, NULL);
    } else if (strstr(buf, "\"class\":\"DEVICES\"") != 0) {
	return json_devicelist_read(buf, gpsdata, NULL);
    } else if (strstr(buf, "\"class\":\"DEVICE\"") != 0) {
	status = json_device_read(buf, &gpsdata->devices.list[0], NULL);
	if (status == 0)
	    gpsdata->set |= DEVICE_SET;
	return status;
    } else if (strstr(buf, "\"class\":\"WATCH\"") != 0) {
	status = json_watch_read(buf, &gpsdata->policy, NULL);
	if (status == 0)
	    gpsdata->set |= POLICY_SET;
	return status;
    } else if (strstr(buf, "\"class\":\"VERSION\"") != 0) {
	return json_version_read(buf, gpsdata, NULL);
#ifdef RTCM104V2_ENABLE
    } else if (strstr(buf, "\"class\":\"RTCM2\"") != 0) {
	status = json_rtcm2_read(buf, 
				 gpsdata->dev.path, sizeof(gpsdata->dev.path), 
				 &gpsdata->rtcm2, NULL);
	if (status == 0)
	    gpsdata->set |= RTCM2_SET;
	return status;
#endif /* RTCM104V2_ENABLE */
#ifdef AIVDM_ENABLE
    } else if (strstr(buf, "\"class\":\"AIS\"") != 0) {
	status = json_ais_read(buf, 
				 gpsdata->dev.path, sizeof(gpsdata->dev.path), 
				 &gpsdata->ais, NULL);
	if (status == 0)
	    gpsdata->set |= AIS_SET;
	return status;
#endif /* AIVDM_ENABLE */
    } else if (strstr(buf, "\"class\":\"ERROR\"") != 0) {
	return json_error_read(buf, gpsdata, NULL);
    } else
	return -1;
}
/*@+compdef@*/

/* libgps_json.c ends here */
