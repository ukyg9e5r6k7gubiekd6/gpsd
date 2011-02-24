/* net_dgpsip.c -- gather and dispatch DGPS data from DGPSIP servers
 *
 * This file is Copyright (c) 2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 */
#include <sys/types.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#ifndef S_SPLINT_S
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif /* S_SPLINT_S */

#include "gpsd.h"

/*@ -branchstate */
int dgpsip_open(struct gps_device_t *device, const char *dgpsserver)
/* open a connection to a DGPSIP server */
{
    char hn[256], buf[BUFSIZ];
    char *colon, *dgpsport = "rtcm-sc104";
    int opts;

    device->dgpsip.reported = false;
    if ((colon = strchr(dgpsserver, ':')) != NULL) {
	dgpsport = colon + 1;
	*colon = '\0';
    }
    if (!getservbyname(dgpsport, "tcp"))
	dgpsport = DEFAULT_RTCM_PORT;

    device->gpsdata.gps_fd =
	netlib_connectsock(AF_UNSPEC, dgpsserver, dgpsport, "tcp");
    if (device->gpsdata.gps_fd >= 0) {
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
    opts = fcntl(device->gpsdata.gps_fd, F_GETFL);

    if (opts >= 0)
	(void)fcntl(device->gpsdata.gps_fd, F_SETFL, opts | O_NONBLOCK);
    device->servicetype = service_dgpsip;
    return device->gpsdata.gps_fd;
}

/*@ +branchstate */

void dgpsip_report(struct gps_device_t *session)
/* may be time to ship a usage report to the DGPSIP server */
{
    /*
     * 10 is an arbitrary number, the point is to have gotten several good
     * fixes before reporting usage to our DGPSIP server.
     */
    if (session->context->fixcnt > 10 && !session->dgpsip.reported) {
	session->dgpsip.reported = true;
	if (session->gpsdata.gps_fd > -1) {
	    char buf[BUFSIZ];
	    (void)snprintf(buf, sizeof(buf), "R %0.8f %0.8f %0.2f\r\n",
			   session->gpsdata.fix.latitude,
			   session->gpsdata.fix.longitude,
			   session->gpsdata.fix.altitude);
	    if (write(session->gpsdata.gps_fd, buf, strlen(buf)) ==
		(ssize_t) strlen(buf))
		gpsd_report(LOG_IO, "=> dgps %s\n", buf);
	    else
		gpsd_report(LOG_IO, "write to dgps FAILED\n");
	}
    }
}

