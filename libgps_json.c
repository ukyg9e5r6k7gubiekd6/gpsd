/****************************************************************************

NAME
   libgps_json.c - deserialize gpsd data coming from the server

DESCRIPTION
   This module uses the generic JSON parser to get data from JSON
representations to libgps strctures.

***************************************************************************/

#include <math.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>

#include "gpsd_config.h"
#include "gpsd.h"
#include "gps_json.h"

static int json_tpv_read(const char *buf, 
			 struct gps_data_t *gpsdata, const char **endptr)
{
    const struct json_attr_t json_attrs_1[] = {
	{"device", string,  .addr.string.ptr = gpsdata->gps_device,
			    .addr.string.len = sizeof(gpsdata->gps_device)},
	{"tag",    string,  .addr.string.ptr = gpsdata->tag,
			    .addr.string.len = sizeof(gpsdata->tag)},
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
	{"eph",    real,    .addr.real = &gpsdata->fix.eph,
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

    return json_read_object(buf, json_attrs_1, 0, NULL);
}

static int json_sky_read(const char *buf, 
			 struct gps_data_t *gpsdata, const char **endptr)
{
    bool usedflags[MAXCHANNELS];
    const struct json_attr_t json_attrs_2_1[] = {
	{"PRN",	   integer, .addr.integer = gpsdata->PRN},
	{"el",	   integer, .addr.integer = gpsdata->elevation},
	{"az",	   integer, .addr.integer = gpsdata->azimuth},
	{"ss",	   real,    .addr.real = gpsdata->ss},
	{"used",   boolean, .addr.boolean = usedflags},
	{NULL},
    };
    const struct json_attr_t json_attrs_2[] = {
	{"device",     string,  .addr.string.ptr  = gpsdata->gps_device,
				.addr.string.len = PATH_MAX},
	{"tag",	       string,  .addr.string.ptr  = gpsdata->tag,
				.addr.string.len = MAXTAGLEN},
	{"time",       real,    .addr.real    = &gpsdata->fix.time},
	{"reported",   integer, .addr.integer = &gpsdata->satellites_used},
	{"satellites", array,   .addr.array.element_type = object,
				.addr.array.arr.subtype = json_attrs_2_1,
				.addr.array.maxlen = MAXCHANNELS},
	{NULL},
    };
    int status, i, j;

    for (i = 0; i < MAXCHANNELS; i++)
	usedflags[i] = false;

    status = json_read_object(buf, json_attrs_2, 0, NULL);
    if (status != 0)
	return status;

    for (i = j = 0; i < MAXCHANNELS; i++) {
	if (usedflags[i]) {
	    gpsdata->used[j++] = gpsdata->PRN[i];
	}
    }

    return 0;
}

void libgps_json_unpack(char *buf, struct gps_data_t *gpsdata)
/* the only entry point - punpack a JSON object into C structs */
{
    if (strstr(buf, "\"class\":\"TPV\"") == 0) {
	json_tpv_read(buf, gpsdata, NULL);
    } else if (strstr(buf, "\"class\":\"SKY\"") == 0) {
	json_sky_read(buf, gpsdata, NULL);
    } 
}

/* libgps_json.c ends here */
