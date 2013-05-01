/****************************************************************************

NAME
   rtcm3_json.c - deserialize RTCM3 JSON

DESCRIPTION
   This module uses the generic JSON parser to get data from RTCM3
representations to libgps structures.

PERMISSIONS
   This file is Copyright (c) 2013 by the GPSD project
   BSD terms apply: see the file COPYING in the distribution root for details.

***************************************************************************/

#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stddef.h>

#include "gpsd.h"

#ifdef SOCKET_EXPORT_ENABLE
#include "gps_json.h"

int json_rtcm3_read(const char *buf,
		    char *path, size_t pathlen, struct rtcm3_t *rtcm3,
		    /*@null@*/ const char **endptr)
{

    static char *stringptrs[NITEMS(rtcm3->rtcmtypes.data)];
    static char stringstore[sizeof(rtcm3->rtcmtypes.data) * 2];
    static int stringcount;

/* *INDENT-OFF* */
#define RTCM3_HEADER \
	{"class",          t_check,    .dflt.check = "RTCM3"}, \
	{"type",           t_uinteger, .addr.uinteger = &rtcm3->type}, \
	{"device",         t_string,   .addr.string = path, \
	                                  .len = pathlen}, \
	{"length",         t_uinteger, .addr.uinteger = &rtcm3->length},

    int status = 0, satcount = 0;

    /*@ -fullinitblock @*/
    const struct json_attr_t rtcm1001_satellite[] = {
	{"ident",     t_uinteger, STRUCTOBJECT(struct rtcm3_1001_t, ident)},
	{"ind",       t_uinteger, STRUCTOBJECT(struct rtcm3_1001_t, L1.indicator)},
	{"prange",    t_real,     STRUCTOBJECT(struct rtcm3_1001_t, L1.pseudorange)},
	{"delta",     t_real,     STRUCTOBJECT(struct rtcm3_1001_t, L1.rangediff)},

	{"lockt",     t_uinteger, STRUCTOBJECT(struct rtcm3_1001_t, L1.locktime)},
	{NULL},
    };
    /*@-type@*//* STRUCTARRAY confuses splint */
#define R1001	&rtcm3->rtcmtypes.rtcm3_1001.header
    const struct json_attr_t json_rtcm1001[] = {
	RTCM3_HEADER
	{"station_id",     t_uinteger, .addr.uinteger = R1001.station_id},
	//{"tow",            t_uinteger,     .addr.time = R1001.tow},
        {"sync",           t_boolean,  .addr.boolean = R1001.sync},
        {"smoothing",      t_boolean,  .addr.boolean = R1001.smoothing},
	//{"interval",       t_uinteger, .addr.uinteger = R1001.interval},
        {"satellites",     t_array,	STRUCTARRAY(rtcm3->rtcmtypes.rtcm3_1001.rtk_data,
					    rtcm1001_satellite, &satcount)},
	{NULL},
    };
#undef R1001
    /*@+type@*/

    /*@-type@*//* complex union array initislizations confuses splint */
    const struct json_attr_t json_rtcm3_fallback[] = {
	RTCM3_HEADER
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

#undef RTCM3_HEADER
/* *INDENT-ON* */

    memset(rtcm3, '\0', sizeof(struct rtcm3_t));

    if (strstr(buf, "\"type\":1001,") != NULL) {
	status = json_read_object(buf, json_rtcm1001, endptr);
	if (status == 0)
	    rtcm3->rtcmtypes.rtcm3_1003.header.satcount = (unsigned short)satcount;
    } else {
	int n;
	status = json_read_object(buf, json_rtcm3_fallback, endptr);
	for (n = 0; n < NITEMS(rtcm3->rtcmtypes.data); n++) {
	    if (n >= stringcount) {
		rtcm3->rtcmtypes.data[n] = '\0';
	    } else {
		unsigned int u;
		int fldcount = sscanf(stringptrs[n], "0x%02x\n", &u);
		if (fldcount != 1)
		    return JSON_ERR_MISC;
		else
		    rtcm3->rtcmtypes.data[n] = (char)u;
	    }
	}
    }
    return status;
}
#endif /* SOCKET_EXPORT_ENABLE */

/* rtcm3_json.c ends here */
