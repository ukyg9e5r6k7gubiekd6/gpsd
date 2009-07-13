/* json.c - unit test for JSON partsing into fixed-extent structures */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gpsd_config.h"
#include "json.h"
#include "gps.h"

#include "strl.c"

static void ASSERT_CASE(int num, int status)
{
    if (status < 0)
    { 
	(void)fprintf(stderr, "case %d FAILED, status %d.\n", num, status);
	exit(1);
    }
}

static void ASSERT_STRING(char *attr, char *fld, char *val)
{
    if (strcmp(fld, val))
    {
	(void)fprintf(stderr, "'%s' attribute eval failed, value = %s.\n", attr, fld);
	exit(1);
    }
}

static void ASSERT_INTEGER(char *attr, int fld, int val)
{
    if (fld != val)
    {
	(void)fprintf(stderr, "'%s' attribute eval failed, value = %d.\n", attr, fld);
	exit(1);
    }
}

static void ASSERT_BOOLEAN(char *attr, bool fld, bool val)
{
    if (fld != val)
    {
	(void)fprintf(stderr, "'%s' attribute eval failed, value = %s.\n", attr, fld ? "true" : "false");
	exit(1);
    }
}

/*
 * Floating point comparisons are iffy, but at least if any of these fail
 * the output will make it clear whether it was a precision issue
 */
static void ASSERT_REAL(char *attr, double fld, double val)
{
    if (fld != val)
    {
	(void)fprintf(stderr, "'%s' attribute eval failed, value = %f.\n", attr, fld);
	exit(1);
    }
}

const char *json_str1 = "{\"device\":\"GPS#1\",\"tag\":\"MID2\",\
    \"time\":1119197561.890,\"lon\":46.498203637,\"lat\":7.568074350,\
    \"alt\":1327.780,\"eph\":21.000,\"epv\":124.484,\"mode\":3,\
    \"flag1\":true,\"flag2\":false}";

static struct gps_data_t gpsdata;
static bool flag1, flag2;
static double dftreal;
static int dftinteger;

const struct json_attr_t json_attrs_1[] = {
    {"device", string,  .addr.string.ptr = gpsdata.gps_device, 
			.addr.string.len = PATH_MAX},
    {"tag",    string,  .addr.string.ptr = gpsdata.tag,
			.addr.string.len = MAXTAGLEN},
    {"time",   real,    .addr.real = &gpsdata.fix.time,      .dflt.real = 0},
    {"lon",    real,    .addr.real = &gpsdata.fix.longitude, .dflt.real = 0},
    {"lat",    real,    .addr.real = &gpsdata.fix.latitude,  .dflt.real = 0},
    {"alt",    real,    .addr.real = &gpsdata.fix.altitude,  .dflt.real = 0},
    {"eph",    real,    .addr.real = &gpsdata.fix.eph,       .dflt.real = 0},
    {"epv",    real,    .addr.real = &gpsdata.fix.epv,       .dflt.real = 0},
    {"dftint", integer, .addr.integer = &dftinteger, .dflt.integer = 5},
    {"dftreal",real,    .addr.real = &dftreal,       .dflt.real = 23.17},
    {"mode",   integer, .addr.integer = &gpsdata.fix.mode,   .dflt.integer = -1},
    {"flag1",  boolean, .addr.boolean = &flag1,},
    {"flag2",  boolean, .addr.boolean = &flag2,},
    {NULL},
};

static bool usedflags[MAXCHANNELS];

const char *json_str2 = "{\"tag\":\"MID4\",\"time\":1119197562.890,\
         \"reported\":7,\
         \"satellites\":[\
         {\"PRN\":10,\"el\":45,\"az\":196,\"ss\":34,\"used\":true},\
         {\"PRN\":29,\"el\":67,\"az\":310,\"ss\":40,\"used\":true},\
         {\"PRN\":28,\"el\":59,\"az\":108,\"ss\":42,\"used\":true},\
         {\"PRN\":26,\"el\":51,\"az\":304,\"ss\":43,\"used\":true},\
         {\"PRN\":8,\"el\":44,\"az\":58,\"ss\":41,\"used\":true},\
         {\"PRN\":27,\"el\":16,\"az\":66,\"ss\":39,\"used\":true},\
         {\"PRN\":21,\"el\":10,\"az\":301,\"ss\":0,\"used\":false}]}";

const struct json_attr_t json_attrs_2_1[] = {
    {"PRN",	   integer, .addr.integer = gpsdata.PRN},
    {"el",	   integer, .addr.integer = gpsdata.elevation},
    {"az",	   integer, .addr.integer = gpsdata.azimuth},
    {"ss",	   real,    .addr.real = gpsdata.ss},
    {"used",	   boolean, .addr.boolean = usedflags},
    {NULL},
};

const struct json_attr_t json_attrs_2[] = {
    {"device",     string,  .addr.string.ptr  = gpsdata.gps_device,
    			    .addr.string.len = PATH_MAX},
    {"tag",        string,  .addr.string.ptr  = gpsdata.tag,
    			    .addr.string.len = MAXTAGLEN},
    {"time",       real,    .addr.real    = &gpsdata.fix.time},
    {"reported",   integer, .addr.integer = &gpsdata.satellites_used},
    {"satellites", array,   .addr.array.subtype = json_attrs_2_1,
                            .addr.array.maxlen = MAXCHANNELS},
    {NULL},
};

int main(int argc UNUSED, char *argv[] UNUSED)
{
    int status;

    (void)fprintf(stderr, "JSON unit test ");

    status = json_read_object(json_str1, json_attrs_1, 0, NULL);
    ASSERT_CASE(1, status);
    ASSERT_STRING("device", gpsdata.gps_device, "GPS#1");
    ASSERT_STRING("tag", gpsdata.tag, "MID2");
    ASSERT_INTEGER("mode", gpsdata.fix.mode, 3);
    ASSERT_REAL("time", gpsdata.fix.time, 1119197561.890);
    ASSERT_REAL("lon", gpsdata.fix.longitude, 46.498203637);
    ASSERT_REAL("lat", gpsdata.fix.latitude, 7.568074350);
    ASSERT_INTEGER("dftint", dftinteger, 5);	/* did the default work? */
    ASSERT_REAL("dftreal", dftreal, 23.17);	/* did the default work? */
    ASSERT_BOOLEAN("flag1", flag1, true);
    ASSERT_BOOLEAN("flag2", flag2, false);

    status = json_read_object(json_str2, json_attrs_2, 0, NULL);
    ASSERT_CASE(2, status);
    ASSERT_STRING("tag", gpsdata.tag, "MID4");
    ASSERT_INTEGER("reported", gpsdata.satellites_used, 7);
    ASSERT_INTEGER("PRN[0]", gpsdata.PRN[0], 10);
    ASSERT_INTEGER("el[0]", gpsdata.elevation[0], 45);
    ASSERT_INTEGER("az[0]", gpsdata.azimuth[0], 196);
    ASSERT_REAL("ss[0]", gpsdata.ss[0], 34);
    ASSERT_BOOLEAN("used[0]", usedflags[0], true);
    ASSERT_INTEGER("PRN[6]", gpsdata.PRN[6], 21);
    ASSERT_INTEGER("el[6]", gpsdata.elevation[6], 10);
    ASSERT_INTEGER("az[6]", gpsdata.azimuth[6], 301);
    ASSERT_REAL("ss[6]", gpsdata.ss[6], 0);
    ASSERT_BOOLEAN("used[6]", usedflags[6], false);

    (void)fprintf(stderr, "succeeded.\n");
    exit(0);
}
