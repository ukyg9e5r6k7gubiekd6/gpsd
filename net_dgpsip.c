/* net_dgpsip.c -- gather and dispatch DGPS data from DGPSIP servers
 *
 * This file is Copyright (c) 2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 */

#include "gpsd_config.h"

#include <sys/types.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#ifndef S_SPLINT_S
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif /* HAVE_NETDB_H */
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif /* HAVE_SYS_SOCKET_H */
#include <unistd.h>
#endif /* S_SPLINT_S */

#include "gpsd.h"

/*@ -branchstate */
int dgpsip_open(struct gps_device_t *device, const char *dgpsserver)
/* open a connection to a DGPSIP server */
{
    char hn[256], buf[BUFSIZ];
    char *colon, *dgpsport = "rtcm-sc104";

    device->dgpsip.reported = false;
    if ((colon = strchr(dgpsserver, ':')) != NULL) {
	dgpsport = colon + 1;
	*colon = '\0';
    }
    if (!getservbyname(dgpsport, "tcp"))
	dgpsport = DEFAULT_RTCM_PORT;

    device->gpsdata.gps_fd =
	netlib_connectsock(AF_UNSPEC, dgpsserver, dgpsport, "tcp");
    if (!BADSOCK(device->gpsdata.gps_fd)) {
	gpsd_report(LOG_PROG, "connection to DGPS server %s established.\n",
		    dgpsserver);
	(void)gethostname(hn, sizeof(hn));
	/* greeting required by some RTCM104 servers; others will ignore it */
	(void)snprintf(buf, sizeof(buf), "HELO %s gpsd %s\r\nR\r\n", hn,
		       VERSION);
	if (write(device->gpsdata.gps_fd, buf, strlen(buf)) != (ssize_t) strlen(buf))
	    gpsd_report(LOG_ERROR, "hello to DGPS server %s failed\n",
			dgpsserver);
    } else
	gpsd_report(LOG_ERROR,
		    "can't connect to DGPS server %s, netlib error %d.\n",
		    dgpsserver, device->gpsdata.gps_fd);
    nonblock_enable(device->gpsdata.gps_fd);
    device->servicetype = service_dgpsip;
    return device->gpsdata.gps_fd;
}

/*@ +branchstate */

void dgpsip_report(struct gps_context_t *context,
		   struct gps_device_t *gps,
		   struct gps_device_t *dgpsip)
/* may be time to ship a usage report to the DGPSIP server */
{
    /*
     * 10 is an arbitrary number, the point is to have gotten several good
     * fixes before reporting usage to our DGPSIP server.
     */
    if (context->fixcnt > 10 && !dgpsip->dgpsip.reported) {
	dgpsip->dgpsip.reported = true;
	if (!BADSOCK(dgpsip->gpsdata.gps_fd)) {
	    char buf[BUFSIZ];
	    (void)snprintf(buf, sizeof(buf), "R %0.8f %0.8f %0.2f\r\n",
			   gps->gpsdata.fix.latitude,
			   gps->gpsdata.fix.longitude,
			   gps->gpsdata.fix.altitude);
	    if (write(dgpsip->gpsdata.gps_fd, buf, strlen(buf)) ==
		(ssize_t) strlen(buf))
		gpsd_report(LOG_IO, "=> dgps %s\n", buf);
	    else
		gpsd_report(LOG_IO, "write to dgps FAILED\n");
	}
    }
}

