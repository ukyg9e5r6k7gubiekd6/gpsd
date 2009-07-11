/* json.c - unit test for JSON partsing into fixed-extent structures */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "json.h"
#include "gps.h"

const char *json_str1 = "{\"device\":\"GPS#1\",\"tag\":\"MID2\",\
    \"time\":1119197561.890,\"lon\":46.498203637,\"lat\":7.568074350,\
    \"alt\":1327.780,\"eph\":21.000,\"epv\":124.484,\"mode\":3}";

static char buf1[JSON_VAL_MAX+1];
static char buf2[JSON_VAL_MAX+1];
static struct gps_fix_t fix;

const struct json_attr_t json_attrs_1[] = {
    {"device", string,  .addr.string = buf1},
    {"tag",    string,  .addr.string = buf2},
    {"time",   real,    .addr.real = &fix.time,      .dflt.real = 0},
    {"lon",    real,    .addr.real = &fix.longitude, .dflt.real = 0},
    {"lat",    real,    .addr.real = &fix.latitude,  .dflt.real = 0},
    {"alt",    real,    .addr.real = &fix.altitude,  .dflt.real = 0},
    {"eph",    real,    .addr.real = &fix.eph,       .dflt.real = 0},
    {"epv",    real,    .addr.real = &fix.epv,       .dflt.real = 0},
    {"mode",   integer, .addr.integer = &fix.mode,   .dflt.integer = -1},
    {NULL},
};

int main(int argc, char *argv[])
{
    int status;

    status = parse_json(json_str1, json_attrs_1);

    (void)fprintf(stderr, "JSON unit test ");
    if (status < 0)
    {
	(void)fprintf(stderr, "case 1 FAILED, status %d.\n", status);
	exit(1);
    }
    if (strcmp(buf1, "GPS#1"))
    {
	(void)fprintf(stderr, "string attribute eval failed, value = %s.\n", buf1);
	exit(1);
    }
    if (strcmp(buf2, "MID2"))
    {
	(void)fprintf(stderr, "string attribute eval failed, value = %s.\n", buf1);
	exit(1);
    }
    if (fix.mode != 3)
    {
	(void)fprintf(stderr, "integer attribute eval failed, value = %d.\n", fix.mode);
	exit(1);
    }


    (void)fprintf(stderr, "succeeded.\n");
    exit(0);
}
