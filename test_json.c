/* $Id$ */
/* json.c - unit test for JSON partsing into fixed-extent structures */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include "gpsd_config.h"
#include "gpsd.h"
#include "gps_json.h"

#include "strl.c"

static void ASSERT_CASE(int num, int status)
{
    if (status != 0)
    { 
	(void)fprintf(stderr, "case %d FAILED, status %d (%s).\n", num, status, json_error_string(status));
	exit(1);
    }
}

static void ASSERT_STRING(char *attr, char *fld, char *val)
{
    if (strcmp(fld, val))
    {
	(void)fprintf(stderr, "'%s' string attribute eval failed, value = %s.\n", attr, fld);
	exit(1);
    }
}

static void ASSERT_INTEGER(char *attr, int fld, int val)
{
    if (fld != val)
    {
	(void)fprintf(stderr, "'%s' integer attribute eval failed, value = %d.\n", attr, fld);
	exit(1);
    }
}

static void ASSERT_BOOLEAN(char *attr, bool fld, bool val)
{
    if (fld != val)
    {
	(void)fprintf(stderr, "'%s' boolean attribute eval failed, value = %s.\n", attr, fld ? "true" : "false");
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
	(void)fprintf(stderr, "'%s' real attribute eval failed, value = %f.\n", attr, fld);
	exit(1);
    }
}

static struct gps_data_t gpsdata;

/* Case 1: TPV report */

const char *json_str1 = "{\"class\":\"TPV\",\
    \"device\":\"GPS#1\",\"tag\":\"MID2\",				\
    \"time\":1119197561.890,\"lon\":46.498203637,\"lat\":7.568074350,\
    \"alt\":1327.780,\"eph\":21.000,\"epv\":124.484,\"mode\":3}";

/* Case 2: SKY report */

const char *json_str2 = "{\"class\":\"SKY\",\
         \"tag\":\"MID4\",\"time\":1119197562.890,   \
         \"reported\":7,\
         \"satellites\":[\
         {\"PRN\":10,\"el\":45,\"az\":196,\"ss\":34,\"used\":true},\
         {\"PRN\":29,\"el\":67,\"az\":310,\"ss\":40,\"used\":true},\
         {\"PRN\":28,\"el\":59,\"az\":108,\"ss\":42,\"used\":true},\
         {\"PRN\":26,\"el\":51,\"az\":304,\"ss\":43,\"used\":true},\
         {\"PRN\":8,\"el\":44,\"az\":58,\"ss\":41,\"used\":true},\
         {\"PRN\":27,\"el\":16,\"az\":66,\"ss\":39,\"used\":true},\
         {\"PRN\":21,\"el\":10,\"az\":301,\"ss\":0,\"used\":false}]}";

/* Case 3: String list syntax */

const char *json_str3 = "[\"foo\",\"bar\",\"baz\"]";

static char *stringptrs[3];
static char stringstore[256];
static int stringcount;

const struct json_array_t json_array_3 = {
    .element_type = string,
    .arr.strings.ptrs = stringptrs,
    .arr.strings.store = stringstore,
    .arr.strings.storelen = sizeof(stringstore),
    .count = &stringcount,
    .maxlen = sizeof(stringptrs)/sizeof(stringptrs[0]),
};

/* Case 4: test defaulting of unspecified attributes */

const char *json_str4 = "{\"flag1\":true,\"flag2\":false}";

static bool flag1, flag2;
static double dftreal;
static int dftinteger;

const struct json_attr_t json_attrs_4[] = {
    {"dftint", integer, .addr.integer = &dftinteger, .dflt.integer = 5},
    {"dftreal",real,    .addr.real = &dftreal,       .dflt.real = 23.17},
    {"flag1",  boolean, .addr.boolean = &flag1,},
    {"flag2",  boolean, .addr.boolean = &flag2,},
    {NULL},
};

/* Case 5: test DEVICE parsing */

const char *json_str5 = "{\"class\":\"DEVICE\",\
           \"path\":\"/dev/ttyUSB0\",\
           \"flags\":5,\
           \"driver\":\"Foonly\",\"subtype\":\"Foonly Frob\"\
           }";

/* Case 6: test parsing of subobject list into array of structures */

const char *json_str6 = "{\"parts\":[\
           {\"name\":\"Urgle\", \"flag\":true, \"count\":3},\
           {\"name\":\"Burgle\",\"flag\":false,\"count\":1},\
           {\"name\":\"Witter\",\"flag\":true, \"count\":4},\
           {\"name\":\"Thud\",  \"flag\":false,\"count\":1}]}";

struct dumbstruct_t {
    char name[64];
    bool flag;
    int count;
};
static struct dumbstruct_t dumbstruck[5];
static int dumbcount;

const struct json_attr_t json_attrs_6_subtype[] = {
    {"name",  string,  .addr.offset = offsetof(struct dumbstruct_t, name),
                       .addr.string.len = 64},
    {"flag",  boolean, .addr.offset = offsetof(struct dumbstruct_t, flag),},
    {"count", integer, .addr.offset = offsetof(struct dumbstruct_t, count),},
    {NULL},
};

const struct json_attr_t json_attrs_6[] = {
    {"parts", array,  .addr.array.element_type = structobject,
                      .addr.array.arr.objects.base = (char*)&dumbstruck,
                      .addr.array.arr.objects.stride = sizeof(struct dumbstruct_t),
                      .addr.array.arr.objects.subtype = json_attrs_6_subtype,
                      .addr.array.count = &dumbcount,
                      .addr.array.maxlen = sizeof(dumbstruck)/sizeof(dumbstruck[0])},
    {NULL},
};

/* Case 7: test parsing of version response */

const char *json_str7 = "{\"class\":\"VERSION\",\
           \"release\":\"2.40dev\",\"rev\":\"dummy-revision\",\
           \"api_major\":3,\"api_minor\":1}";

int main(int argc UNUSED, char *argv[] UNUSED)
{
    int status;

    (void)fprintf(stderr, "JSON unit test ");

    status = libgps_json_unpack(json_str1, &gpsdata);
    ASSERT_CASE(1, status);
    ASSERT_STRING("device", gpsdata.gps_device, "GPS#1");
    ASSERT_STRING("tag", gpsdata.tag, "MID2");
    ASSERT_INTEGER("mode", gpsdata.fix.mode, 3);
    ASSERT_REAL("time", gpsdata.fix.time, 1119197561.890);
    ASSERT_REAL("lon", gpsdata.fix.longitude, 46.498203637);
    ASSERT_REAL("lat", gpsdata.fix.latitude, 7.568074350);

    status = libgps_json_unpack(json_str2, &gpsdata);
    ASSERT_CASE(2, status);
    ASSERT_STRING("tag", gpsdata.tag, "MID4");
    ASSERT_INTEGER("reported", gpsdata.satellites_used, 7);
    ASSERT_INTEGER("PRN[0]", gpsdata.PRN[0], 10);
    ASSERT_INTEGER("el[0]", gpsdata.elevation[0], 45);
    ASSERT_INTEGER("az[0]", gpsdata.azimuth[0], 196);
    ASSERT_REAL("ss[0]", gpsdata.ss[0], 34);
    ASSERT_INTEGER("used[0]", gpsdata.used[0], 10);
    ASSERT_INTEGER("used[5]", gpsdata.used[5], 27);
    ASSERT_INTEGER("PRN[6]", gpsdata.PRN[6], 21);
    ASSERT_INTEGER("el[6]", gpsdata.elevation[6], 10);
    ASSERT_INTEGER("az[6]", gpsdata.azimuth[6], 301);
    ASSERT_REAL("ss[6]", gpsdata.ss[6], 0);

    status = json_read_array(json_str3, &json_array_3, NULL);
    ASSERT_CASE(3, status);
    assert(stringcount == 3);
    assert(strcmp(stringptrs[0], "foo") == 0);
    assert(strcmp(stringptrs[1], "bar") == 0);
    assert(strcmp(stringptrs[2], "baz") == 0);

    status = json_read_object(json_str4, json_attrs_4, NULL);
    ASSERT_CASE(4, status);
    ASSERT_INTEGER("dftint", dftinteger, 5);	/* did the default work? */
    ASSERT_REAL("dftreal", dftreal, 23.17);	/* did the default work? */
    ASSERT_BOOLEAN("flag1", flag1, true);
    ASSERT_BOOLEAN("flag2", flag2, false);

    status = libgps_json_unpack(json_str5, &gpsdata);
    ASSERT_CASE(5, status);
    ASSERT_STRING("path", gpsdata.devices.list[0].path, "/dev/ttyUSB0");
    ASSERT_INTEGER("flags",gpsdata.devices.list[0].flags, 5);
    ASSERT_STRING("driver", gpsdata.devices.list[0].driver, "Foonly");

    status = json_read_object(json_str6, json_attrs_6, NULL);
    ASSERT_CASE(6, status);
    ASSERT_STRING("dumbstruck[0].name", dumbstruck[0].name, "Urgle");
    ASSERT_STRING("dumbstruck[1].name", dumbstruck[1].name, "Burgle");
    ASSERT_STRING("dumbstruck[2].name", dumbstruck[2].name, "Witter");
    ASSERT_STRING("dumbstruck[3].name", dumbstruck[3].name, "Thud");
    ASSERT_BOOLEAN("dumbstruck[0].flag", dumbstruck[0].flag, true);
    ASSERT_BOOLEAN("dumbstruck[1].flag", dumbstruck[1].flag, false);
    ASSERT_BOOLEAN("dumbstruck[2].flag", dumbstruck[2].flag, true);
    ASSERT_BOOLEAN("dumbstruck[3].flag", dumbstruck[3].flag, false);
    ASSERT_INTEGER("dumbstruck[0].count", dumbstruck[0].count, 3);
    ASSERT_INTEGER("dumbstruck[1].count", dumbstruck[1].count, 1);
    ASSERT_INTEGER("dumbstruck[2].count", dumbstruck[2].count, 4);
    ASSERT_INTEGER("dumbstruck[3].count", dumbstruck[3].count, 1);

    status = libgps_json_unpack(json_str7, &gpsdata);
    ASSERT_CASE(7, status);
    ASSERT_STRING("release", gpsdata.version.release, "2.40dev");
    ASSERT_STRING("rev", gpsdata.version.rev, "dummy-revision");
    ASSERT_INTEGER("api_major", gpsdata.version.api_major, 3);
    ASSERT_INTEGER("api_minor", gpsdata.version.api_minor, 1);

    (void)fprintf(stderr, "succeeded.\n");

    exit(0);
}
