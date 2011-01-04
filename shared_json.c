/****************************************************************************

NAME
   shared_json.c - move data between in-core and JSON structures

DESCRIPTION 
   This module uses the generic JSON parser to get data from JSON
representations to gps.h structures. These functions are used in both
the daemon and the client library.

PERMISSIONS
  Written by Eric S. Raymond, 2009
  This file is Copyright (c) 2010 by the GPSD project
  BSD terms apply: see the file COPYING in the distribution root for details.

***************************************************************************/

#include <math.h>

#include "gpsd.h"
#include "gps_json.h"

int json_device_read(const char *buf,
		     /*@out@*/ struct devconfig_t *dev,
		     /*@null@*/ const char **endptr)
{
    /*@ -fullinitblock @*/
    /* *INDENT-OFF* */
    const struct json_attr_t json_attrs_device[] = {
	{"class",      t_check,      .dflt.check = "DEVICE"},
	
        {"path",       t_string,     .addr.string  = dev->path,
	                                .len = sizeof(dev->path)},
	{"activated",  t_real,       .addr.real = &dev->activated},
	{"flags",      t_integer,    .addr.integer = &dev->flags},
	{"driver",     t_string,     .addr.string  = dev->driver,
	                                .len = sizeof(dev->driver)},
	{"subtype",    t_string,     .addr.string  = dev->subtype,
	                                .len = sizeof(dev->subtype)},
	{"native",     t_integer,    .addr.integer = &dev->driver_mode,
				        .dflt.integer = DEVDEFAULT_NATIVE},
	{"bps",	       t_uinteger,   .addr.uinteger = &dev->baudrate,
				        .dflt.uinteger = DEVDEFAULT_BPS},
	{"parity",     t_character,  .addr.character = &dev->parity,
                                        .dflt.character = DEVDEFAULT_PARITY},
	{"stopbits",   t_uinteger,   .addr.uinteger = &dev->stopbits,
				        .dflt.uinteger = DEVDEFAULT_STOPBITS},
	{"cycle",      t_real,       .addr.real = &dev->cycle,
				        .dflt.real = NAN},
	{"mincycle",   t_real,       .addr.real = &dev->mincycle,
				        .dflt.real = NAN},
	{NULL},
    };
    /* *INDENT-ON* */
    /*@ +fullinitblock @*/
    int status;

    status = json_read_object(buf, json_attrs_device, endptr);
    if (status != 0)
	return status;

    return 0;
}

int json_watch_read(const char *buf,
		    /*@out@*/ struct policy_t *ccp,
		    /*@null@*/ const char **endptr)
{
    /*@ -fullinitblock @*/
    /* *INDENT-OFF* */
    struct json_attr_t chanconfig_attrs[] = {
	{"class",          t_check,    .dflt.check = "WATCH"},
	
	{"enable",         t_boolean,  .addr.boolean = &ccp->watcher,
                                          .dflt.boolean = true},
	{"json",           t_boolean,  .addr.boolean = &ccp->json,
                                          .nodefault = true},
	{"raw",	           t_integer,  .addr.integer = &ccp->raw,
	                                  .nodefault = true},
	{"nmea",	   t_boolean,  .addr.boolean = &ccp->nmea,
	                                  .nodefault = true},
	{"subframe",	   t_boolean,  .addr.boolean = &ccp->subframe,
	                                  .nodefault = true},
	{"scaled",         t_boolean,  .addr.boolean = &ccp->scaled},
	{"timing",         t_boolean,  .addr.boolean = &ccp->timing},
	{"device",         t_string,   .addr.string = ccp->devpath,
	                                  .len = sizeof(ccp->devpath)},
	{NULL},
    };
    /* *INDENT-ON* */
    /*@ +fullinitblock @*/
    int status;

    status = json_read_object(buf, chanconfig_attrs, endptr);
    return status;
}

/* shared_json.c ends here */
