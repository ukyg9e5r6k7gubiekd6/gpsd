/* net_gnss_dispatch.c -- common interface to a number of Network GNSS services
 *
 * This file is Copyright (c) 2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 */

#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <unistd.h>

#include "gpsd.h"
#include "strfuncs.h"

#define NETGNSS_DGPSIP	"dgpsip://"
#define NETGNSS_NTRIP	"ntrip://"

bool netgnss_uri_check(char *name)
/* is given string a valid URI for GNSS/DGPS service? */
{
    return
	str_starts_with(name, NETGNSS_NTRIP)
	|| str_starts_with(name, NETGNSS_DGPSIP);
}


int netgnss_uri_open(struct gps_device_t *dev, char *netgnss_service)
/* open a connection to a DGNSS service */
{
#ifdef NTRIP_ENABLE
    if (str_starts_with(netgnss_service, NETGNSS_NTRIP)) {
	dev->ntrip.conn_state = ntrip_conn_init;
	return ntrip_open(dev, netgnss_service + strlen(NETGNSS_NTRIP));
    }
#endif

    if (str_starts_with(netgnss_service, NETGNSS_DGPSIP))
	return dgpsip_open(dev, netgnss_service + strlen(NETGNSS_DGPSIP));

#ifndef REQUIRE_DGNSS_PROTO
    return dgpsip_open(dev, netgnss_service);
#else
    gpsd_log(&dev->context.errout, LOG_ERROR,
	     "Unknown or unspecified DGNSS protocol for service %s\n",
	     netgnss_service);
    return -1;
#endif
}

void netgnss_report(struct gps_context_t *context,
		    struct gps_device_t *gps, struct gps_device_t *dgnss)
/* may be time to ship a usage report to the DGNSS service */
{
    if (dgnss->servicetype == service_dgpsip)
	dgpsip_report(context, gps, dgnss);
#ifdef NTRIP_ENABLE
    else if (dgnss->servicetype == service_ntrip)
	ntrip_report(context, gps, dgnss);
#endif
}

/* end */
