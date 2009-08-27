/****************************************************************************

NAME
   rtcm2_json.c - deserialize RTCM2 JSON

DESCRIPTION
   This module uses the generic JSON parser to get data from RTCM2
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

/* common fields in every RTCM2 message */

int json_rtcm2_read(const char *buf, 
		    char *path, size_t pathlen,
		    struct rtcm2_t *rtcm2, 
		    const char **endptr)
{

#define RTCM2_HEADER \
	{"class",          check,    .dflt.check = "RTCM2"}, \
	{"type",           uinteger, .addr.uinteger = &rtcm2->type}, \
	{"device",         string,   .addr.string = path, \
	                             .len = pathlen}, \
	{"station_id",     uinteger, .addr.uinteger = &rtcm2->refstaid}, \
	{"zcount",         real,     .addr.real = &rtcm2->zcount, \
			             .dflt.real = NAN}, \
	{"seqnum",         uinteger, .addr.uinteger = &rtcm2->seqnum}, \
	{"length",         uinteger, .addr.uinteger = &rtcm2->length}, \
	{"station_health", uinteger, .addr.uinteger = &rtcm2->stathlth},

#define STRUCTOBJECT(s, f)	.addr.offset=offsetof(s, f)

    int status, satcount;
    const struct json_attr_t json_rtcm1_satellite[] = {
	{"ident",     uinteger, STRUCTOBJECT(struct rangesat_t, ident)},
	{"udre",      uinteger, STRUCTOBJECT(struct rangesat_t, udre)},
	{"issuedata", real,     STRUCTOBJECT(struct rangesat_t, issuedata)},
	{"rangerr",   real,     STRUCTOBJECT(struct rangesat_t, rangerr)},
	{"rangerate", real,     STRUCTOBJECT(struct rangesat_t, rangerate)},
	{NULL},
    };
    const struct json_attr_t json_rtcm1[] = {
	RTCM2_HEADER
        {"satellites", array,  .addr.array.element_type = structobject,
	            .addr.array.arr.objects.base = (char*)rtcm2->ranges.sat,
                    .addr.array.arr.objects.stride = sizeof(rtcm2->ranges.sat[0]),
                    .addr.array.arr.objects.subtype = json_rtcm1_satellite,
	            .addr.array.count = &satcount,
	            .addr.array.maxlen = NITEMS(rtcm2->ranges.sat)},
	{NULL},
    };

    const struct json_attr_t json_rtcm3[] = {
	RTCM2_HEADER
        {"valid",          boolean, .addr.boolean = &rtcm2->reference.valid},
	{"x",              real,    .addr.real = &rtcm2->ecef.x,
			            .dflt.real = NAN},
	{"y",              real,    .addr.real = &rtcm2->ecef.y,
			            .dflt.real = NAN},
	{"z",              real,    .addr.real = &rtcm2->ecef.z,
			            .dflt.real = NAN},
	{NULL},
    };

    const struct json_attr_t json_rtcm4[] = {
	RTCM2_HEADER
        {"valid",          boolean, .addr.boolean = &rtcm2->reference.valid},
	{"system",         integer, .addr.integer = &rtcm2->reference.system},
	{"sense",          integer, .addr.integer = &rtcm2->reference.sense},
	{"datum",          string,  .addr.string = rtcm2->reference.datum,
	                            .len = sizeof(rtcm2->reference.datum)},
	{"dx",             real,    .addr.real = &rtcm2->reference.dx,
			            .dflt.real = NAN},
	{"dy",             real,    .addr.real = &rtcm2->reference.dy,
			            .dflt.real = NAN},
	{"dz",             real,    .addr.real = &rtcm2->reference.dz,
			            .dflt.real = NAN},
	{NULL},
    };

    const struct json_attr_t json_rtcm5[] = {
	// FIXME
	RTCM2_HEADER
	{NULL},
    };

    const struct json_attr_t json_rtcm6[] = {
	RTCM2_HEADER
	// No-op or keepalive message
	{NULL},
    };

    const struct json_attr_t json_rtcm7[] = {
	// FIXME
	RTCM2_HEADER
	{NULL},
    };

    const struct json_attr_t json_rtcm16[] = {
	RTCM2_HEADER
	{"message",        string,  .addr.string = rtcm2->message,
	                            .len = sizeof(rtcm2->message)},
	{NULL},
    };

    const struct json_attr_t json_rtcm_fallback[] = {
	// FIXME
	RTCM2_HEADER
	{NULL},
    };

#undef STRUCTOBJECT
#undef RTCM2_HEADER

    memset(rtcm2, '\0', sizeof(struct rtcm2_t));

    if (strstr(buf, "\"type\":1")!=NULL || strstr(buf, "\"type\":9")!=NULL) {
	status = json_read_object(buf, json_rtcm1, endptr);
	if (status == 0)
	    rtcm2->ranges.nentries = (unsigned)satcount;
    } else if (strstr(buf, "\"type\":3") != NULL)
	status = json_read_object(buf, json_rtcm3, endptr);
    else if (strstr(buf, "\"type\":4") != NULL) {
	status = json_read_object(buf, json_rtcm4, endptr);
    } else if (strstr(buf, "\"type\":5") != NULL)
	status = json_read_object(buf, json_rtcm5, endptr);
    else if (strstr(buf, "\"type\":6") != NULL)
	status = json_read_object(buf, json_rtcm6, endptr);
    else if (strstr(buf, "\"type\":7") != NULL)
	status = json_read_object(buf, json_rtcm7, endptr);
    else if (strstr(buf, "\"type\":16") != NULL)
	status = json_read_object(buf, json_rtcm16, endptr);
    else
	status = json_read_object(buf, json_rtcm_fallback, endptr);
    if (status != 0)
	return status;
    return 0;
}

/* rtcm2_json.c ends here */
