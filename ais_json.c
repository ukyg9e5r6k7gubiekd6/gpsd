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
	{"mmsi",           uinteger, .addr.uinteger = &ais->mmsi}

    int status;


#include "ais_json.i"	/* JSON parser template structures */

#undef AIS_HEADER

    memset(ais, '\0', sizeof(struct ais_t));

    if (strstr(buf, "\"type\":1,")!=NULL || strstr(buf, "\"type\":2,")!=NULL || strstr(buf, "\"type\":3,")!=NULL) {
	status = json_read_object(buf, json_ais1, endptr);
    } else if (strstr(buf, "\"type\":4,") != NULL || strstr(buf, "\"type\":11,")!=NULL) {
	// FIXME: Needs processing to handle timestamp
	status = json_read_object(buf, json_ais4, endptr);
	// FIXME: Needs processing to handle eta
    } else if (strstr(buf, "\"type\":5,") != NULL) {
	status = json_read_object(buf, json_ais5, endptr);
    } else if (strstr(buf, "\"type\":6,") != NULL) {
	status = json_read_object(buf, json_ais6, endptr);
    } else if (strstr(buf, "\"type\":7,") != NULL || strstr(buf, "\"type\":13,") != NULL) {
	status = json_read_object(buf, json_ais7, endptr);
    } else if (strstr(buf, "\"type\":8,") != NULL) {
	// FIXME: requires postprocessing of data field
	status = json_read_object(buf, json_ais8, endptr);
    } else if (strstr(buf, "\"type\":9,") != NULL) {
	status = json_read_object(buf, json_ais9, endptr);
    } else if (strstr(buf, "\"type\":10,") != NULL) {
	status = json_read_object(buf, json_ais10, endptr);
    } else if (strstr(buf, "\"type\":12,") != NULL) {
	status = json_read_object(buf, json_ais12, endptr);
    } else if (strstr(buf, "\"type\":14,") != NULL) {
	status = json_read_object(buf, json_ais14, endptr);
    } else if (strstr(buf, "\"type\":15,") != NULL) {
	status = json_read_object(buf, json_ais15, endptr);
    } else if (strstr(buf, "\"type\":16,") != NULL) {
	status = json_read_object(buf, json_ais16, endptr);
    } else if (strstr(buf, "\"type\":17,") != NULL) {
	// FIXME: requires postprocessing of data field
	status = json_read_object(buf, json_ais17, endptr);
    } else if (strstr(buf, "\"type\":18,") != NULL) {
	status = json_read_object(buf, json_ais18, endptr);
    } else if (strstr(buf, "\"type\":18,") != NULL) {
	status = json_read_object(buf, json_ais17, endptr);
    } else if (strstr(buf, "\"type\":19,") != NULL) {
	status = json_read_object(buf, json_ais19, endptr);
    } else if (strstr(buf, "\"type\":20,") != NULL) {
	status = json_read_object(buf, json_ais20, endptr);
    } else if (strstr(buf, "\"type\":21,") != NULL) {
	status = json_read_object(buf, json_ais21, endptr);
    } else if (strstr(buf, "\"type\":22,") != NULL) {
	status = json_read_object(buf, json_ais22, endptr);
    } else if (strstr(buf, "\"type\":24,") != NULL) {
	status = json_read_object(buf, json_ais24, endptr);
    } else {
	return JSON_ERR_MISC;
    }
    return status;
}

/* ais_json.c ends here */
