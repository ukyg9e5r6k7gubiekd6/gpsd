/* dgpsip.c -- gather and dispatch DGPS data from DGPSIP servers */
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include "gpsd.h"

/*@ -branchstate */
int dgpsip_open(struct gps_context_t *context, const char *dgpsserver)
/* open a connection to a DGPSIP server */
{
    char hn[256], buf[BUFSIZ];
    char *colon, *dgpsport = "rtcm-sc104";
    int opts;

    if ((colon = strchr(dgpsserver, ':'))) {
	dgpsport = colon+1;
	*colon = '\0';
    }
    if (!getservbyname(dgpsport, "tcp"))
	dgpsport = DEFAULT_RTCM_PORT;

    context->dsock = netlib_connectsock(dgpsserver, dgpsport, "tcp");
    if (context->dsock >= 0) {
	gpsd_report(1,"connection to DGPS server %s established.\n",dgpsserver);
	(void)gethostname(hn, sizeof(hn));
	/* greeting required by some RTCM104 servers; others will ignore it */
	(void)snprintf(buf,sizeof(buf), "HELO %s gpsd %s\r\nR\r\n",hn,VERSION);
	(void)write(context->dsock, buf, strlen(buf));
    } else
	gpsd_report(1, "can't connect to DGPS server %s, netlib error %d.\n", dgpsserver, context->dsock);
    opts = fcntl(context->dsock, F_GETFL);

    if (opts >= 0)
	(void)fcntl(context->dsock, F_SETFL, opts | O_NONBLOCK);
    return context->dsock;
}
/*@ +branchstate */

void dgpsip_poll(struct gps_context_t *context)
/* poll the DGPSIP server for a correction report */
{
    if (context->dsock > -1) {
	context->rtcmbytes = read(context->dsock, context->rtcmbuf, sizeof(context->rtcmbuf));
	if (context->rtcmbytes < 0 && errno != EAGAIN)
	    gpsd_report(1, "Read from rtcm source failed\n");
	else
	    context->rtcmtime = timestamp();
    }
}

void dgpsip_relay(struct gps_device_t *session)
/* pass a DGPSIP connection report to a session */
{
    if (session->gpsdata.gps_fd !=-1 
	&& session->context->rtcmbytes > -1
	&& session->rtcmtime < session->context->rtcmtime
	&& session->device_type->rtcm_writer) {
	if (session->device_type->rtcm_writer(session, 
					      session->context->rtcmbuf, 
					      (size_t)session->context->rtcmbytes) == 0)
	    gpsd_report(1, "Write to rtcm sink failed\n");
	else { 
	    session->rtcmtime = timestamp();
	    gpsd_report(2, "<= DGPS: %d bytes of RTCM relayed.\n", session->context->rtcmbytes);
	}
    }
}

void dgpsip_report(struct gps_device_t *session)
/* may be time to ship a usage report to the DGPSIP server */
{
    /*
     * 10 is an arbitrary number, the point is to have gotten several good
     * fixes before reporting usage to our DGPSIP server.
     */
    if (session->context->fixcnt > 10 && !session->context->sentdgps) {
	session->context->sentdgps = true;
	if (session->context->dsock > -1) {
	    char buf[BUFSIZ];
	    (void)snprintf(buf, sizeof(buf), "R %0.8f %0.8f %0.2f\r\n", 
			   session->gpsdata.fix.latitude, 
			   session->gpsdata.fix.longitude, 
			   session->gpsdata.fix.altitude);
	    (void)write(session->context->dsock, buf, strlen(buf));
	    gpsd_report(2, "=> dgps %s", buf);
	}
    }
}

#define DGPS_THRESHOLD	1600000	/* max. useful dist. from DGPS server (m) */
#define SERVER_SAMPLE	12	/* # of servers within threshold to check */

struct dgps_server_t {
    double lat, lon;
    char server[257];
    double dist;
};

static int srvcmp(const void *s, const void *t)
{
    return (int)(((struct dgps_server_t *)s)->dist - ((struct dgps_server_t *)t)->dist);
}

void dgpsip_autoconnect(struct gps_context_t *context,
			double lat, double lon,
			const char *serverlist)
/* tell the library to talk to the nearest DGPSIP server */
{
    struct dgps_server_t keep[SERVER_SAMPLE], hold, *sp, *tp;
    char buf[BUFSIZ];
    FILE *sfp = fopen(serverlist, "r");

    if (sfp == NULL) {
	gpsd_report(1, "no DGPS server list found.\n");
	context->dsock = -2;	/* don't try this again */
	return;
    }

    for (sp = keep; sp < keep + SERVER_SAMPLE; sp++) {
	sp->dist = DGPS_THRESHOLD;
	sp->server[0] = '\0';
    }
    /*@ -usedef @*/
    while (fgets(buf, (int)sizeof(buf), sfp)) {
	char *cp = strchr(buf, '#');
	if (cp)
	    *cp = '\0';
	if (sscanf(buf,"%lf %lf %256s",&hold.lat, &hold.lon, hold.server)==3) {
	    hold.dist = earth_distance(lat, lon, hold.lat, hold.lon);
	    tp = NULL;
	    /*
	     * The idea here is to look for a server in the sample array
	     * that is (a) closer than the one we're checking, and (b)
	     * furtherest away of all those that are closer.  Replace it.
	     * In this way we end up with the closest possible set.
	     */
	    for (sp = keep; sp < keep + SERVER_SAMPLE; sp++)
		if (hold.dist < sp->dist && (tp==NULL || hold.dist > tp->dist))
		    tp = sp;
	    if (tp != NULL)
		memcpy(tp, &hold, sizeof(struct dgps_server_t));
	}
    }
    (void)fclose(sfp);

    if (keep[0].server[0] == '\0') {
	gpsd_report(1, "no DGPS servers within %dm.\n", (int)(DGPS_THRESHOLD/1000));
	context->dsock = -2;	/* don't try this again */
	return;
    }
    /*@ +usedef @*/

    /* sort them and try the closest first */
    qsort((void *)keep, SERVER_SAMPLE, sizeof(struct dgps_server_t), srvcmp);
    for (sp = keep; sp < keep + SERVER_SAMPLE; sp++) {
	if (sp->server[0] != '\0') {
	    gpsd_report(2,"%s is %dkm away.\n",sp->server,(int)(sp->dist/1000));
	    if (dgpsip_open(context, sp->server) >= 0)
		break;
	}
    }
}
