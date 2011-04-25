/****************************************************************************

NAME
   ais_json.c - deserialize AIS JSON

DESCRIPTION
   This module uses the generic JSON parser to get data from AIS
representations to libgps structures.

***************************************************************************/

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stddef.h>

#include "gpsd.h"
#ifdef SOCKET_EXPORT_ENABLE
#include "gps_json.h"

/*@ -mustdefine @*/
static void lenhex_unpack(const char *from,
			  size_t * plen, /*@out@*/ char *to, size_t maxlen)
{
    char *colon = strchr(from, ':');

    *plen = (size_t) atoi(from);
    if (colon != NULL)
	(void)gpsd_hexpack(colon + 1, to, maxlen);
}

/*@ +mustdefine @*/

int json_ais_read(const char *buf,
		  char *path, size_t pathlen, struct ais_t *ais,
		  /*@null@*/ const char **endptr)
{
    /* collected but not actually used yet */
    bool scaled;
    /*@-compdef@*//* splint is confused by storage declared in the .i file */
    /*@-nullstate@*/

#define AIS_HEADER \
	{"class",          t_check,    .dflt.check = "AIS"}, \
	{"type",           t_uinteger, .addr.uinteger = &ais->type}, \
	{"device",         t_string,   .addr.string = path, \
	                                  .len = pathlen}, \
	{"repeat",         t_uinteger, .addr.uinteger = &ais->repeat}, \
	{"scaled",         t_boolean,  .addr.boolean = &scaled, \
		                          .dflt.boolean = false}, \
	{"mmsi",           t_uinteger, .addr.uinteger = &ais->mmsi},

#define AIS_TYPE6 \
	{"seqno",         t_uinteger,  .addr.uinteger = &ais->type6.seqno,\
                                       .dflt.uinteger = 0},\
	{"dest_mmsi",     t_uinteger,  .addr.uinteger = &ais->type6.dest_mmsi,\
                                       .dflt.uinteger = 0},\
	{"retransmit",    t_boolean,   .addr.boolean = &ais->type6.retransmit,\
                                       .dflt.boolean = false},\
	{"dac",           t_uinteger,  .addr.uinteger = &ais->type6.dac,\
                                       .dflt.uinteger = 0},\
	{"fid",           t_uinteger,  .addr.uinteger = &ais->type6.fid,\
                                       .dflt.uinteger = 0},

    int status;

#include "ais_json.i"		/* JSON parser template structures */

#undef AIS_HEADER

    memset(ais, '\0', sizeof(struct ais_t));

    /*@-usedef@*/
    if (strstr(buf, "\"type\":1,") != NULL
	|| strstr(buf, "\"type\":2,") != NULL
	|| strstr(buf, "\"type\":3,") != NULL) {
	status = json_read_object(buf, json_ais1, endptr);
    } else if (strstr(buf, "\"type\":4,") != NULL
	       || strstr(buf, "\"type\":11,") != NULL) {
	status = json_read_object(buf, json_ais4, endptr);
	if (status == 0) {
	    ais->type4.year = AIS_YEAR_NOT_AVAILABLE;
	    ais->type4.month = AIS_MONTH_NOT_AVAILABLE;
	    ais->type4.day = AIS_DAY_NOT_AVAILABLE;
	    ais->type4.hour = AIS_HOUR_NOT_AVAILABLE;
	    ais->type4.minute = AIS_MINUTE_NOT_AVAILABLE;
	    ais->type4.second = AIS_SECOND_NOT_AVAILABLE;
	    (void)sscanf(timestamp, "%4u-%02u-%02uT%02u:%02u:%02uZ",
			 &ais->type4.year,
			 &ais->type4.month,
			 &ais->type4.day,
			 &ais->type4.hour,
			 &ais->type4.minute, 
			 &ais->type4.second);
	}
    } else if (strstr(buf, "\"type\":5,") != NULL) {
	status = json_read_object(buf, json_ais5, endptr);
	if (status == 0) {
	    ais->type5.month = AIS_MONTH_NOT_AVAILABLE;
	    ais->type5.day = AIS_DAY_NOT_AVAILABLE;
	    ais->type5.hour = AIS_HOUR_NOT_AVAILABLE;
	    ais->type5.minute = AIS_MINUTE_NOT_AVAILABLE;
	    (void)sscanf(eta, "%02u-%02uT%02u:%02uZ",
			 &ais->type5.month,
			 &ais->type5.day,
			 &ais->type5.hour, 
			 &ais->type5.minute);
	}
    } else if (strstr(buf, "\"type\":6,") != NULL) {
	bool imo = false;
	if (strstr(buf, "\"fid\":12,") != NULL) {
	    status = json_read_object(buf, json_ais6_fid12, endptr);
	    if (status == 0) {
		ais->type6.dac1fid12.lmonth = AIS_MONTH_NOT_AVAILABLE;
		ais->type6.dac1fid12.lday = AIS_DAY_NOT_AVAILABLE;
		ais->type6.dac1fid12.lhour = AIS_HOUR_NOT_AVAILABLE;
		ais->type6.dac1fid12.lminute = AIS_MINUTE_NOT_AVAILABLE;
		(void)sscanf(departure, "%02u-%02uT%02u:%02uZ",
			     &ais->type6.dac1fid12.lmonth,
			     &ais->type6.dac1fid12.lday,
			     &ais->type6.dac1fid12.lhour, 
			     &ais->type6.dac1fid12.lminute);
		ais->type6.dac1fid12.nmonth = AIS_MONTH_NOT_AVAILABLE;
		ais->type6.dac1fid12.nday = AIS_DAY_NOT_AVAILABLE;
		ais->type6.dac1fid12.nhour = AIS_HOUR_NOT_AVAILABLE;
		ais->type6.dac1fid12.nminute = AIS_MINUTE_NOT_AVAILABLE;
		(void)sscanf(eta, "%02u-%02uT%02u:%02uZ",
			     &ais->type6.dac1fid12.nmonth,
			     &ais->type6.dac1fid12.nday,
			     &ais->type6.dac1fid12.nhour, 
			     &ais->type6.dac1fid12.nminute);
	    }
	    imo = true;
	}
	else if (strstr(buf, "\"fid\":15,") != NULL) {
	    status = json_read_object(buf, json_ais6_fid15, endptr);
	    imo = true;
	}
	else if (strstr(buf, "\"fid\":16,") != NULL) {
	    status = json_read_object(buf, json_ais6_fid16, endptr);
	    imo = true;
	}
	else if (strstr(buf, "\"fid\":25,") != NULL) {
	    status = json_read_object(buf, json_ais6_fid25, endptr);
	    imo = true;
	}
	else if (strstr(buf, "\"fid\":32,") != NULL || strstr(buf, "\"fid\":14,") != NULL) {
	    status = json_read_object(buf, json_ais6_fid32, endptr);
	    imo = true;
	}
	else if (strstr(buf, "\"fid\":30,") != NULL) {
	    status = json_read_object(buf, json_ais6_fid30, endptr);
	    imo = true;
	}
	if (!imo) {
	    status = json_read_object(buf, json_ais6, endptr);
	    if (status == 0)
		lenhex_unpack(data, &ais->type6.bitcount,
			      ais->type6.bitdata, sizeof(ais->type6.bitdata));
	}
    } else if (strstr(buf, "\"type\":7,") != NULL
	       || strstr(buf, "\"type\":13,") != NULL) {
	status = json_read_object(buf, json_ais7, endptr);
    } else if (strstr(buf, "\"type\":8,") != NULL) {
	bool imo = false;
	if (strstr(buf, "\"fid\":29,") != NULL) {
	    status = json_read_object(buf, json_ais8_fid29, endptr);
	    imo = true;
	}
	else if (strstr(buf, "\"fid\":31,") != NULL || strstr(buf, "\"fid\":11,") != NULL) {
	    status = json_read_object(buf, json_ais8_fid31, endptr);
	    if (status == 0) {
		ais->type5.day = AIS_DAY_NOT_AVAILABLE;
		ais->type5.hour = AIS_HOUR_NOT_AVAILABLE;
		ais->type5.minute = AIS_MINUTE_NOT_AVAILABLE;
		(void)sscanf(eta, "%02uT%02u:%02uZ",
			     &ais->type5.day,
			     &ais->type5.hour, 
			     &ais->type5.minute);
	    }
	    imo = true;
	}
	if (!imo) {
	    status = json_read_object(buf, json_ais8, endptr);
	    if (status == 0)
		lenhex_unpack(data, &ais->type8.bitcount,
			      ais->type8.bitdata, sizeof(ais->type8.bitdata));
	}
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
	status = json_read_object(buf, json_ais17, endptr);
	if (status == 0)
	    lenhex_unpack(data, &ais->type17.bitcount,
			  ais->type17.bitdata, sizeof(ais->type17.bitdata));
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
    } else if (strstr(buf, "\"type\":23,") != NULL) {
	status = json_read_object(buf, json_ais23, endptr);
    } else if (strstr(buf, "\"type\":24,") != NULL) {
	status = json_read_object(buf, json_ais24, endptr);
    } else if (strstr(buf, "\"type\":25,") != NULL) {
	status = json_read_object(buf, json_ais25, endptr);
    } else if (strstr(buf, "\"type\":26,") != NULL) {
	status = json_read_object(buf, json_ais26, endptr);
    } else if (strstr(buf, "\"type\":27,") != NULL) {
	status = json_read_object(buf, json_ais27, endptr);
    } else {
	if (endptr != NULL)
	    *endptr = NULL;
	return JSON_ERR_MISC;
    }
    return status;
    /*@+compdef +usedef +nullstate@*/
}
#endif /* SOCKET_EXPORT_ENABLE */

/* ais_json.c ends here */
