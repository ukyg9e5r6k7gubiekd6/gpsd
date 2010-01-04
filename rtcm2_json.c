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

#include "gpsd.h"
#include "gps_json.h"

/* common fields in every RTCM2 message */

int json_rtcm2_read(const char *buf, 
		    char *path, size_t pathlen,
		    struct rtcm2_t *rtcm2, 
		    /*@null@*/const char **endptr)
{

    static char *stringptrs[NITEMS(rtcm2->words)];
    static char stringstore[sizeof(rtcm2->words)*2];
    static int stringcount;

#define RTCM2_HEADER \
	{"class",          t_check,    .dflt.check = "RTCM2"}, \
	{"type",           t_uinteger, .addr.uinteger = &rtcm2->type}, \
	{"device",         t_string,   .addr.string = path, \
	                                  .len = pathlen}, \
	{"station_id",     t_uinteger, .addr.uinteger = &rtcm2->refstaid}, \
	{"zcount",         t_real,     .addr.real = &rtcm2->zcount, \
			                  .dflt.real = NAN}, \
	{"seqnum",         t_uinteger, .addr.uinteger = &rtcm2->seqnum}, \
	{"length",         t_uinteger, .addr.uinteger = &rtcm2->length}, \
	{"station_health", t_uinteger, .addr.uinteger = &rtcm2->stathlth},

    int status = 0, satcount = 0;

    /*@ -fullinitblock @*/
    const struct json_attr_t rtcm1_satellite[] = {
	{"ident",     t_uinteger, STRUCTOBJECT(struct rangesat_t, ident)},
	{"udre",      t_uinteger, STRUCTOBJECT(struct rangesat_t, udre)},
	{"issuedata", t_uinteger, STRUCTOBJECT(struct rangesat_t, issuedata)},
	{"rangerr",   t_real,     STRUCTOBJECT(struct rangesat_t, rangerr)},
	{"rangerate", t_real,     STRUCTOBJECT(struct rangesat_t, rangerate)},
	{NULL},
    };
    /*@-type@*//* STRUCTARRAY confuses splint */
    const struct json_attr_t json_rtcm1[] = {
	RTCM2_HEADER
        {"satellites", t_array,	STRUCTARRAY(rtcm2->ranges.sat, 
					    rtcm1_satellite, &satcount)},
	{NULL},
    };
    /*@+type@*/

    const struct json_attr_t json_rtcm3[] = {
	RTCM2_HEADER
	{"x",              t_real,    .addr.real = &rtcm2->ecef.x,
			                 .dflt.real = NAN},
	{"y",              t_real,    .addr.real = &rtcm2->ecef.y,
			                 .dflt.real = NAN},
	{"z",              t_real,    .addr.real = &rtcm2->ecef.z,
			                 .dflt.real = NAN},
	{NULL},
    };

    /*
     * Beware! Needs to stay synchronized with a corresponding
     * nam,e array in the RTCM2 JSON dump code. This interpretation of
     * NAVSYSTEM_GALILEO is assumed from RTCM3, it's not actually
     * documented in RTCM 2.1.
     */
    const struct json_enum_t system_table[] = {
	{"GPS", 0}, {"GLONASS", 1}, {"GALILEO", 2}, {"UNKNOWN", 2}, {NULL}
    };
    const struct json_attr_t json_rtcm4[] = {
	RTCM2_HEADER
        {"valid",          t_boolean, .addr.boolean = &rtcm2->reference.valid},
	{"system",         t_integer, .addr.integer = &rtcm2->reference.system,
	                                 .map=system_table},
	{"sense",          t_integer, .addr.integer = &rtcm2->reference.sense},
	{"datum",          t_string,  .addr.string = rtcm2->reference.datum,
	                                 .len = sizeof(rtcm2->reference.datum)},
	{"dx",             t_real,    .addr.real = &rtcm2->reference.dx,
			                 .dflt.real = NAN},
	{"dy",             t_real,    .addr.real = &rtcm2->reference.dy,
			                 .dflt.real = NAN},
	{"dz",             t_real,    .addr.real = &rtcm2->reference.dz,
			                 .dflt.real = NAN},
	{NULL},
    };

    const struct json_attr_t rtcm5_satellite[] = {
	{"ident",       t_uinteger, STRUCTOBJECT(struct consat_t, ident)},
	{"iodl",        t_boolean,  STRUCTOBJECT(struct consat_t, iodl)},
	{"health",      t_uinteger, STRUCTOBJECT(struct consat_t, health)},
	{"snr",         t_integer,  STRUCTOBJECT(struct consat_t, snr)},
	{"health_en",   t_boolean,  STRUCTOBJECT(struct consat_t, health_en)},
	{"new_data",    t_boolean,  STRUCTOBJECT(struct consat_t, new_data)},
	{"los_warning", t_boolean,  STRUCTOBJECT(struct consat_t, los_warning)},
	{"tou",         t_uinteger, STRUCTOBJECT(struct consat_t, tou)},
	{NULL},
    };
    /*@-type@*//* STRUCTARRAY confuses splint */
    const struct json_attr_t json_rtcm5[] = {
	RTCM2_HEADER
        {"satellites", t_array,	STRUCTARRAY(rtcm2->conhealth.sat, 
					    rtcm5_satellite, &satcount)},
	{NULL},
    };
    /*@+type@*/

    const struct json_attr_t json_rtcm6[] = {
	RTCM2_HEADER
	// No-op or keepalive message
	{NULL},
    };

    const struct json_attr_t rtcm7_satellite[] = {
	{"lat",         t_real,     STRUCTOBJECT(struct station_t, latitude)},
	{"lon",         t_real,     STRUCTOBJECT(struct station_t, longitude)},
	{"range",       t_uinteger, STRUCTOBJECT(struct station_t, range)},
	{"frequency",   t_real,     STRUCTOBJECT(struct station_t, frequency)},
	{"health",      t_uinteger, STRUCTOBJECT(struct station_t, health)},
	{"station_id",  t_uinteger, STRUCTOBJECT(struct station_t, station_id)},
	{"bitrate",     t_uinteger, STRUCTOBJECT(struct station_t, bitrate)},
	{NULL},
    };
    /*@-type@*//* STRUCTARRAY confuses splint */
    const struct json_attr_t json_rtcm7[] = {
	RTCM2_HEADER
        {"satellites", t_array,	STRUCTARRAY(rtcm2->almanac.station, 
					    rtcm7_satellite, &satcount)},
	{NULL},
    };
    /*@+type@*/

    const struct json_attr_t json_rtcm16[] = {
	RTCM2_HEADER
	{"message",        t_string,  .addr.string = rtcm2->message,
	                                 .len = sizeof(rtcm2->message)},
	{NULL},
    };

    /*@-type@*//* complex union array initislizations confuses splint */
    const struct json_attr_t json_rtcm2_fallback[] = {
	RTCM2_HEADER
	{"data",         t_array, .addr.array.element_type = t_string,
	                             .addr.array.arr.strings.ptrs = stringptrs,
	                             .addr.array.arr.strings.store = stringstore,
	                             .addr.array.arr.strings.storelen = sizeof(stringstore),
	                             .addr.array.count = &stringcount,
	                             .addr.array.maxlen = NITEMS(stringptrs)},
	{NULL},
    };
    /*@+type@*/
    /*@ +fullinitblock @*/

#undef RTCM2_HEADER

    memset(rtcm2, '\0', sizeof(struct rtcm2_t));

    if (strstr(buf, "\"type\":1,")!=NULL || strstr(buf, "\"type\":9,")!=NULL) {
	status = json_read_object(buf, json_rtcm1, endptr);
	if (status == 0)
	    rtcm2->ranges.nentries = (unsigned)satcount;
    } else if (strstr(buf, "\"type\":3,") != NULL) {
	status = json_read_object(buf, json_rtcm3, endptr);
	if (status == 0) {
	    rtcm2->ecef.valid = (isnan(rtcm2->ecef.x)==0)&&(isnan(rtcm2->ecef.y)==0)&&(isnan(rtcm2->ecef.z)==0);
	}
    } else if (strstr(buf, "\"type\":4,") != NULL) {
	status = json_read_object(buf, json_rtcm4, endptr);
	if (status == 0)
	    rtcm2->reference.valid = (isnan(rtcm2->reference.dx)==0)&&(isnan(rtcm2->reference.dy)==0)&&(isnan(rtcm2->reference.dz)==0);
    } else if (strstr(buf, "\"type\":5,") != NULL) {
	status = json_read_object(buf, json_rtcm5, endptr);
	if (status == 0)
	    rtcm2->conhealth.nentries = (unsigned)satcount;
    } else if (strstr(buf, "\"type\":6,") != NULL) {
	status = json_read_object(buf, json_rtcm6, endptr);
    } else if (strstr(buf, "\"type\":7,") != NULL) {
	status = json_read_object(buf, json_rtcm7, endptr);
	if (status == 0)
	    rtcm2->almanac.nentries = (unsigned)satcount;
    } else if (strstr(buf, "\"type\":16,") != NULL) {
	status = json_read_object(buf, json_rtcm16, endptr);
    } else {
	int n;
	status = json_read_object(buf, json_rtcm2_fallback, endptr);
	for (n = 0; n < NITEMS(rtcm2->words); n++)
	    if (n >= stringcount) {
		rtcm2->words[n] = 0;
	    } else {
		unsigned int u;
		int fldcount = sscanf(stringptrs[n], "0x%08x\n", &u);
		if (fldcount != 1)
		return JSON_ERR_MISC;
	    else
		rtcm2->words[n] = (isgps30bits_t)u;
	}
	
    }
    return status;
}

/* rtcm2_json.c ends here */
