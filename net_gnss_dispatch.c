/* net_gnss_dispatch.c -- common interface to a number of Network GNSS services
 *
 * This file is Copyright (c) 2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 */

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifndef S_SPLINT_S
#include <unistd.h>
#endif /* S_SPLINT_S */

#include "gpsd_config.h"
#ifndef S_SPLINT_S
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#endif /* S_SPLINT_S */

#include "gpsd.h"

#define NETGNSS_DGPSIP	"dgpsip://"
#define NETGNSS_NTRIP	"ntrip://"

/* Where to find the list of DGPSIP correction servers, if there is one */
#define DGPSIP_SERVER_LIST	"/usr/share/gpsd/dgpsip-servers"

bool netgnss_uri_check(char *name)
/* is given string a valid URI for GNSS/DGPS service? */
{
    return
	strncmp(name, NETGNSS_NTRIP, strlen(NETGNSS_NTRIP)) == 0
	|| strncmp(name, NETGNSS_DGPSIP, strlen(NETGNSS_DGPSIP)) == 0;
}


/*@ -branchstate */
int netgnss_uri_open(struct gps_context_t *context, char *netgnss_service)
/* open a connection to a DGNSS service */
{
#ifdef NTRIP_ENABLE
    if (strncmp(netgnss_service, NETGNSS_NTRIP, strlen(NETGNSS_NTRIP)) == 0)
	return ntrip_open(context, netgnss_service + strlen(NETGNSS_NTRIP));
#endif

    if (strncmp(netgnss_service, NETGNSS_DGPSIP, strlen(NETGNSS_DGPSIP)) == 0)
	return dgpsip_open(context, netgnss_service + strlen(NETGNSS_DGPSIP));

#ifndef REQUIRE_DGNSS_PROTO
    return dgpsip_open(context, netgnss_service);
#else
    gpsd_report(LOG_ERROR,
		"Unknown or unspecified DGNSS protocol for service %s\n",
		netgnss_service);
    return -1;
#endif
}

/*@ +branchstate */

int netgnss_poll(struct gps_context_t *context)
/* poll the DGNSS service for a correction report */
{
    if (context->dsock > -1) {
	ssize_t rtcmbytes =
	    read(context->dsock, context->rtcmbuf, sizeof(context->rtcmbuf));
	if ((rtcmbytes == -1 && errno != EAGAIN) || (rtcmbytes == 0)) {
	    (void)shutdown(context->dsock, SHUT_RDWR);
	    (void)close(context->dsock);
	    context->rtcmbytes = 0;
	    return -1;
	} else {
	    context->rtcmbytes = (size_t) rtcmbytes;
	    context->rtcmtime = timestamp();
	}
    }
    return 0;
}

void netgnss_report(struct gps_device_t *session)
/* may be time to ship a usage report to the DGNSS service */
{
    if (session->context->netgnss_service == netgnss_dgpsip)
	dgpsip_report(session);
#ifdef NTRIP_ENABLE
    else if (session->context->netgnss_service == netgnss_ntrip)
	ntrip_report(session);
#endif
}

void netgnss_autoconnect(struct gps_context_t *context, double lat,
			 double lon)
{
    if (context->netgnss_service == netgnss_dgpsip) {
	dgpsip_autoconnect(context, lat, lon, DGPSIP_SERVER_LIST);
    }
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
