/****************************************************************************

NAME
   gpsd_json.c - move data between in-core and JSON structures

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

void json_version_dump(char *reply, size_t replylen)
{
    (void)snprintf(reply, replylen,
		   "{\"class\":\"VERSION\",\"release\":\"" VERSION "\",\"rev\":\"$Id: gpsd.c 5957 2009-08-23 15:45:54Z esr $\",\"api_major\":%d,\"api_minor\":%d}", 
		   GPSD_API_MAJOR_VERSION, GPSD_API_MINOR_VERSION);
}

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
		   gpsdata->dev.path);
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
		   datap->dev.path);
    if (isnan(datap->sentence_time)==0)
	(void)snprintf(reply+strlen(reply),
		       replylen-strlen(reply),
		       "\"time\":%.3f,",
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

int json_device_read(const char *buf, 
			     struct devconfig_t *dev, const char **endptr)
{
    char serialmode[4];
    const struct json_attr_t json_attrs_device[] = {
	{"class",      check,      .dflt.check = "DEVICE"},
	
        {"path",       string,     .addr.string  = dev->path,
	                           .len = sizeof(dev->path)},
	{"activated",  real,       .addr.real = &dev->activated},
	{"flags",      integer,    .addr.integer = &dev->flags},
	{"driver",     string,     .addr.string  = dev->driver,
	                           .len = sizeof(dev->driver)},
	{"subtype",    string,     .addr.string  = dev->subtype,
	                           .len = sizeof(dev->subtype)},
	{"native",     integer,    .addr.integer = &dev->driver_mode,
				   .dflt.integer = -1},
	{"bps",	       integer,    .addr.integer = &dev->baudrate,
				   .dflt.integer = -1},
	{"parity",     string,	   .addr.string=serialmode,
				   .len=sizeof(serialmode)},
	{"stopbits",   integer,    .addr.integer = &dev->stopbits,
				   .dflt.integer = -1},
	{"cycle",      real,       .addr.real = &dev->cycle,
				   .dflt.real = NAN},
	{"mincycle",   real,       .addr.real = &dev->mincycle,
				   .dflt.real = NAN},
	{NULL},
    };
    int status;

    status = json_read_object(buf, json_attrs_device, endptr);
    if (status != 0)
	return status;

    return 0;
}

void json_device_dump(struct gps_device_t *device,
		     char *reply, size_t replylen)
{
    struct classmap_t *cmp;
    (void)strlcpy(reply, "{\"class\":\"DEVICE\",\"path\":\"", replylen);
    (void)strlcat(reply, device->gpsdata.dev.path, replylen);
    (void)strlcat(reply, "\",", replylen);
    if (device->gpsdata.online > 0) {
	(void)snprintf(reply+strlen(reply), replylen-strlen(reply),
		       "\"activated\":%2.2f,", device->gpsdata.online);
	if (device->observed != 0) {
	    int mask = 0;
	    for (cmp = classmap; cmp < classmap+NITEMS(classmap); cmp++)
		if ((device->observed & cmp->packetmask) != 0) 
		    mask |= cmp->typemask;
	    if (mask != 0)
		(void)snprintf(reply+strlen(reply), replylen-strlen(reply),
			       "\"flags\":%d,", mask);
	}
	if (device->device_type != NULL) {
	    (void)strlcat(reply, "\"driver\":\"", replylen);
	    (void)strlcat(reply, 
			  device->device_type->type_name,
			  replylen);
	    (void)strlcat(reply, "\",", replylen);
	}
	if (device->subtype[0] != '\0') {
	    (void)strlcat(reply, "\"subtype\":\",", replylen);
	    (void)strlcat(reply, 
			  device->subtype,
			  replylen);
	    (void)strlcat(reply, "\",", replylen);
	}
	(void)snprintf(reply+strlen(reply), replylen-strlen(reply),
		       "\"native\":%d,\"bps\":%d,\"parity\":\"%c\",\"stopbits\":%u,\"cycle\":%2.2f",
		       device->gpsdata.dev.driver_mode,
		       (int)gpsd_get_speed(&device->ttyset),
		       (int)device->gpsdata.dev.parity,
		       device->gpsdata.dev.stopbits,
		       device->gpsdata.dev.cycle);
	if (device->device_type != NULL && device->device_type->rate_switcher != NULL)
	    (void)snprintf(reply+strlen(reply), replylen-strlen(reply),
			   ",\"mincycle\":%2.2f",
			   device->device_type->min_cycle);
    }
    if (reply[strlen(reply)-1] == ',')
	reply[strlen(reply)-1] = '\0';	/* trim trailing comma */
    (void)strlcat(reply, "}", replylen);
}

int json_watch_read(const char *buf, 
		    struct policy_t *ccp,
		    const char **endptr)
{
    int intcasoc;
    struct json_attr_t chanconfig_attrs[] = {
	{"enable",         boolean,  .addr.boolean = &ccp->watcher,
                                     .dflt.boolean = true},
	{"raw",	           integer,  .addr.integer = &ccp->raw,
	                             .nodefault = true},
	{"buffer_policy",  integer,  .addr.integer = &intcasoc,
				     .dflt.integer = -1},
	{"scaled",         boolean,  .addr.boolean = &ccp->scaled},
	{NULL},
    };
    int status;

    status = json_read_object(buf, chanconfig_attrs, endptr);
    if (status == 0) {
	if (intcasoc != -1)
	    ccp->buffer_policy = intcasoc;
    }
    return status;
}

void json_watch_dump(struct policy_t *ccp, char *reply, size_t replylen)
{
    (void)snprintf(reply+strlen(reply), replylen-strlen(reply),
		   "{\"class\":\"WATCH\",\"enable\":%s,\"raw\":%d,\"buffer_policy\":%d,\"scaled\":%s}",
		   ccp->watcher ? "true" : "false",
		   ccp->raw, 
		   ccp->buffer_policy,
		   ccp->scaled ? "true" : "false");
}

/* gpsd_json.c ends here */
