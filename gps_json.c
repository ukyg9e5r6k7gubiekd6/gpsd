/****************************************************************************

NAME
   gps_json.c - move data between in-core and JSON structures

DESCRIPTION
   This module uses the generic JSON parser to get data from JSON
representations to gpsd core strctures, and vice_versa.

***************************************************************************/

#include <math.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>

#include "gpsd_config.h"
#include "gpsd.h"
#include "gps_json.h"

void json_tpv_dump(struct gps_data_t *gpsdata, struct gps_fix_t *fixp, 
		   char *reply, size_t replylen)
{
    assert(replylen > 2);
    (void)strlcpy(reply, "{\"class\":\"TPV\",", replylen);
    (void)snprintf(reply+strlen(reply),
		   replylen-strlen(reply),
		   "\"tag\":\"%s\",",
		   gpsdata->tag[0]!='\0' ? gpsdata->tag : "-");
    (void)snprintf(reply+strlen(reply),
		   replylen-strlen(reply),
		   "\"device\":\"%s\",",
		   gpsdata->gps_device);
    if (isnan(fixp->time)==0)
	(void)snprintf(reply+strlen(reply),
		       replylen-strlen(reply),
		       "\"time\":%.3f,",
		       fixp->time);
    if (isnan(fixp->ept)==0)
	(void)snprintf(reply+strlen(reply),
		       replylen-strlen(reply),
		       "\"ept\":%.3f,",
		       fixp->ept);
    if (isnan(fixp->latitude)==0)
	(void)snprintf(reply+strlen(reply),
		       replylen-strlen(reply),
		       "\"lat\":%.9f,",
		       fixp->latitude);
    if (isnan(fixp->longitude)==0)
	(void)snprintf(reply+strlen(reply),
		       replylen-strlen(reply),
		       "\"lon\":%.9f,",
		       fixp->longitude);
    if (isnan(fixp->altitude)==0)
	(void)snprintf(reply+strlen(reply),
		       replylen-strlen(reply),
		       "\"alt\":%.3f,",
		       fixp->altitude);
    if (isnan(fixp->eph)==0)
	(void)snprintf(reply+strlen(reply),
		       replylen-strlen(reply),
		       "\"eph\":%.3f,",
		       fixp->eph);
    if (isnan(fixp->epv)==0)
	(void)snprintf(reply+strlen(reply),
		       replylen-strlen(reply),
		       "\"epv\":%.3f,",
		       fixp->epv);
    if (isnan(fixp->track)==0)
	(void)snprintf(reply+strlen(reply),
		       replylen-strlen(reply),
		       "\"track\":%.4f,",
		       fixp->track);
    if (isnan(fixp->speed)==0)
	(void)snprintf(reply+strlen(reply),
		       replylen-strlen(reply),
		       "\"speed\":%.3f,",
		       fixp->speed);
    if (isnan(fixp->climb)==0)
	(void)snprintf(reply+strlen(reply),
		       replylen-strlen(reply),
		       "\"climb\":%.3f,",
		       fixp->climb);
    if (isnan(fixp->epd)==0)
	(void)snprintf(reply+strlen(reply),
		       replylen-strlen(reply),
		       "\"epd\":%.4f,",
		       fixp->epd);
    if (isnan(fixp->eps)==0)
	(void)snprintf(reply+strlen(reply),
		       replylen-strlen(reply),
		       "\"eps\":%.2f,", fixp->eps);
    if (isnan(fixp->epc)==0)
	(void)snprintf(reply+strlen(reply),
		       replylen-strlen(reply),
		       "\"epc\":%.2f,", fixp->epc);
    if (fixp->mode > 0)
	(void)snprintf(reply+strlen(reply),
		       replylen-strlen(reply),
		       "\"mode\":%d,", fixp->mode);
    if (reply[strlen(reply)-1] == ',')
	reply[strlen(reply)-1] = '\0';	/* trim trailing comma */
    (void)strlcat(reply, "}", sizeof(reply)-strlen(reply));
}

void json_sky_dump(struct gps_data_t *datap, char *reply, size_t replylen)
{
    int i, j, used, reported = 0;
    assert(replylen > 2);
    (void)strlcpy(reply, "{\"class\":\"SKY\",", replylen);
    (void)snprintf(reply+strlen(reply),
		   replylen- strlen(reply),
		   "\"tag\":\"%s\",",
		   datap->tag[0]!='\0' ? datap->tag : "-");
    (void)snprintf(reply+strlen(reply),
		   replylen-strlen(reply),
		   "\"device\":\"%s\",",
		   datap->gps_device);
    if (isnan(datap->sentence_time)==0)
	(void)snprintf(reply+strlen(reply),
		       replylen-strlen(reply),
		       "\"time\":%.3f ",
		       datap->sentence_time);
    /* insurance against flaky drivers */
    for (i = 0; i < datap->satellites; i++)
	if (datap->PRN[i])
	    reported++;
    (void)snprintf(reply+strlen(reply),
		   replylen-strlen(reply),
		   "\"reported\":%d,", reported);
    if (reported) {
	(void)strlcat(reply, "\"satellites\":[", replylen);
	for (i = 0; i < reported; i++) {
	    used = 0;
	    for (j = 0; j < datap->satellites_used; j++)
		if (datap->used[j] == datap->PRN[i]) {
		    used = 1;
		    break;
		}
	    if (datap->PRN[i]) {
		(void)snprintf(reply+strlen(reply),
			       replylen-strlen(reply),
			       "{\"PRN\":%d,\"el\":%d,\"az\":%d,\"ss\":%.0f,\"used\":%s},",
			       datap->PRN[i],
			       datap->elevation[i],datap->azimuth[i],
			       datap->ss[i],
			       used ? "true" : "false");
	    }
	}
	reply[strlen(reply)-1] = '\0';	/* trim trailing comma */
	(void)strlcat(reply, "]", replylen-strlen(reply));
    }
    (void)strlcat(reply, "}", replylen-strlen(reply));
    if (datap->satellites != reported)
	gpsd_report(LOG_WARN,"Satellite count %d != PRN count %d\n",
		    datap->satellites, reported);
}

int json_tpv_read(const char *buf, struct gps_data_t *gpsdata, const char **endptr)
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

int json_sky_read(const char *buf, struct gps_data_t *gpsdata, const char **endptr)
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

int json_watch_read(struct policy_t *ccp,
			 const char *buf, const char **endptr)
{
    int intcasoc;
    struct json_attr_t chanconfig_attrs[] = {
	{"raw",	           integer,  .addr.integer = &ccp->raw,
				     .dflt.integer = -1},
	{"buffer_policy",  integer,  .addr.integer = &intcasoc,
				     .dflt.integer = -1},
	{"scaled",         boolean,  .addr.boolean = &ccp->scaled},
	{NULL},
    };
    int status;

    status = json_read_object(buf, chanconfig_attrs, 0, endptr);
    if (status == 0) {
	if (intcasoc != -1)
	    ccp->buffer_policy = intcasoc;
    }
    return status;
}

void json_watch_dump(struct policy_t *ccp, char *reply, size_t replylen)
{
    (void)snprintf(reply+strlen(reply), replylen-strlen(reply),
		   "{\"class\":\"WATCH\",\"raw\":%d,\"buffer_policy\":%d,\"scaled\":%s}",
		   ccp->raw, 
		   ccp->buffer_policy,
		   ccp->scaled ? "true" : "false");
}

int json_configdev_read(struct devconfig_t *cdp, const char *buf, const char **endptr)
{
    struct json_attr_t devconfig_attrs[] = {
	{"device",	string,   .addr.string.ptr=cdp->device,
				  .addr.string.len=sizeof(cdp->device)},
	{"native",	integer,  .addr.integer = &cdp->native,
				  .dflt.integer = -1},
	{"bps",		integer,  .addr.integer = &cdp->bps,
				  .dflt.integer = -1},
	{"serialmode",	string,	  .addr.string.ptr=cdp->serialmode,
				  .addr.string.len=sizeof(cdp->serialmode)},
	{"cycle",	real,     .addr.real = &cdp->cycle,
				  .dflt.real = NAN},
	{"mincycle",	real,     .addr.real = &cdp->mincycle,
				  .dflt.real = NAN},
	{NULL},
    };

    return json_read_object(buf, devconfig_attrs, 0, endptr);
}

void json_configdev_dump(struct gps_device_t *devp, char *reply, size_t replylen)
{
    (void)snprintf(reply, replylen,
		   "{\"class\":\"CONFIGDEV\",\"device\":\"%s\",\"native\":%d,\"bps\":%d,\"serialmode\":\"%u%c%u\",\"cycle\":%2.2f",
		   devp->gpsdata.gps_device,
		   devp->gpsdata.driver_mode,
		   (int)gpsd_get_speed(&devp->ttyset),
		   9 - devp->gpsdata.stopbits,
		   (int)devp->gpsdata.parity,
		   devp->gpsdata.stopbits,
		   devp->gpsdata.cycle);
    if (devp->device_type->rate_switcher != NULL)
	(void)snprintf(reply+strlen(reply), replylen-strlen(reply),
		       ",\"mincycle\":%2.2f",
		       devp->device_type->min_cycle);
    (void)strlcat(reply, "}", replylen);
}

/* gpsd_json.c ends here */
