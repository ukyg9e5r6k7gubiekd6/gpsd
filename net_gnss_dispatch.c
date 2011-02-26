/* net_gnss_dispatch.c -- common interface to a number of Network GNSS services
 *
 * This file is Copyright (c) 2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 */

#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifndef S_SPLINT_S
#include <sys/socket.h>
#include <unistd.h>
#endif /* S_SPLINT_S */

#include "gpsd.h"

#define NETGNSS_DGPSIP	"dgpsip://"
#define NETGNSS_NTRIP	"ntrip://"

bool netgnss_uri_check(char *name)
/* is given string a valid URI for GNSS/DGPS service? */
{
    return
	strncmp(name, NETGNSS_NTRIP, strlen(NETGNSS_NTRIP)) == 0
	|| strncmp(name, NETGNSS_DGPSIP, strlen(NETGNSS_DGPSIP)) == 0;
}


/*@ -branchstate */
int netgnss_uri_open(struct gps_device_t *dev, char *netgnss_service)
/* open a connection to a DGNSS service */
{
#ifdef NTRIP_ENABLE
    if (strncmp(netgnss_service, NETGNSS_NTRIP, strlen(NETGNSS_NTRIP)) == 0)
	return ntrip_open(dev, netgnss_service + strlen(NETGNSS_NTRIP));
#endif

    if (strncmp(netgnss_service, NETGNSS_DGPSIP, strlen(NETGNSS_DGPSIP)) == 0)
	return dgpsip_open(dev, netgnss_service + strlen(NETGNSS_DGPSIP));

#ifndef REQUIRE_DGNSS_PROTO
    return dgpsip_open(dev, netgnss_service);
#else
    gpsd_report(LOG_ERROR,
		"Unknown or unspecified DGNSS protocol for service %s\n",
		netgnss_service);
    return -1;
#endif
}

/*@ +branchstate */

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

/* *INDENT-OFF* */
void rtcm_relay(struct gps_device_t *session)
/* pass a DGNSS connection report to a session */
{
    if (session->gpsdata.gps_fd != -1
	&& session->context->rtcmbytes > 0
	&& session->rtcmtime < session->context->rtcmtime
	&& session->device_type->rtcm_writer != NULL) {
	if (session->device_type->rtcm_writer(session,
					      session->context->rtcmbuf,
					      session->context->rtcmbytes) ==
	    0)
	    gpsd_report(LOG_ERROR, "Write to RTCM sink failed\n");
	else {
	    session->rtcmtime = timestamp();
	    gpsd_report(LOG_IO, "<= DGPS: %zd bytes of RTCM relayed.\n",
			session->context->rtcmbytes);
	}
    }
}
/* *INDENT-ON* */

/* end */
