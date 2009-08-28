/****************************************************************************

NAME
   ais_json.c - deserialize AIS JSON

DESCRIPTION
   This module uses the generic JSON parser to get data from AIS
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

/* common fields in every AIS message */

int json_ais_read(const char *buf, 
		    char *path, size_t pathlen,
		    struct ais_t *ais, 
		    const char **endptr)
{

#define AIS_HEADER \
	{"class",          check,    .dflt.check = "AIS"}, \
	{"type",           uinteger, .addr.uinteger = &ais->type}, \
	{"device",         string,   .addr.string = path, \
	                             .len = pathlen}, \
	{"repeat",         uinteger, .addr.uinteger = &ais->repeat}, \
	{"mmsi",           uinteger, .addr.uinteger = &ais->mmsi},

    int status;

    const struct json_attr_t json_ais1[] = {
	AIS_HEADER
	{"status",         uinteger, .addr.uinteger = &ais->type123.status},
	{"turn",           integer,  .addr.integer = &ais->type123.turn},
	{"speed",          uinteger, .addr.uinteger = &ais->type123.speed},
	{"accuracy",       boolean,  .addr.boolean = &ais->type123.accuracy},
	{"lon",            integer,  .addr.integer = &ais->type123.lon},
	{"lat",            integer,  .addr.integer = &ais->type123.lat},
	{"course",         uinteger, .addr.uinteger = &ais->type123.course},
	{"heading",        uinteger, .addr.uinteger = &ais->type123.heading},
	{"second",         uinteger, .addr.uinteger = &ais->type123.second},
	{"maneuver",       uinteger, .addr.uinteger = &ais->type123.maneuver},
	{"raim",           boolean,  .addr.boolean = &ais->type123.raim},
	{"radio",          uinteger, .addr.uinteger = &ais->type123.radio},
	{NULL},
    };

    const struct json_attr_t json_ais4[] = {
	AIS_HEADER
	{NULL},
    };

    const struct json_attr_t json_ais5[] = {
	AIS_HEADER
	{NULL},
    };

    const struct json_attr_t json_ais6[] = {
	AIS_HEADER
	{NULL},
    };

    const struct json_attr_t json_ais7[] = {
	AIS_HEADER
	{NULL},
    };

    const struct json_attr_t json_ais8[] = {
	AIS_HEADER
	{NULL},
    };

    const struct json_attr_t json_ais16[] = {
	AIS_HEADER
	{NULL},
    };

#undef AIS_HEADER

    memset(ais, '\0', sizeof(struct ais_t));

    if (strstr(buf, "\"type\":1,")!=NULL || strstr(buf, "\"type\":2,")!=NULL || strstr(buf, "\"type\":3,")!=NULL) {
	status = json_read_object(buf, json_ais1, endptr);
    } else if (strstr(buf, "\"type\":4,") != NULL || strstr(buf, "\"type\":11,")!=NULL) {
	status = json_read_object(buf, json_ais4, endptr);
    } else if (strstr(buf, "\"type\":5,") != NULL) {
	status = json_read_object(buf, json_ais5, endptr);
    } else if (strstr(buf, "\"type\":6,") != NULL) {
	status = json_read_object(buf, json_ais6, endptr);
    } else if (strstr(buf, "\"type\":7,") != NULL) {
	status = json_read_object(buf, json_ais7, endptr);
    } else if (strstr(buf, "\"type\":8,") != NULL) {
	status = json_read_object(buf, json_ais8, endptr);
    } else if (strstr(buf, "\"type\":16,") != NULL) {
	status = json_read_object(buf, json_ais16, endptr);
    } else {
	return JSON_ERR_MISC;
    }
    return status;
}

/* ais_json.c ends here */
