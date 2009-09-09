/* $Id$ */
/* net_remotegpsd.c -- gather and dispatch GPS data from other GPSD servers */
#include <sys/types.h>
#ifndef S_SPLINT_S
#include <sys/socket.h>
#endif /* S_SPLINT_S */
#include <sys/time.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <netdb.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include "gpsd_config.h"

#include "gpsd.h"

struct remotegpsd_stream_t {
    char *protocol;
    char *devpath;
};

static struct remotegpsd_stream_t remotegpsd_stream;

static int remotegpsd_device_probe(const char *host,
			      const char *port,
			      const char *devpath)
{
    int ret;
    int dsock;
    char buf[BUFSIZ];

    if ((dsock = netlib_connectsock(host, port, "tcp")) == -1) {
	    gpsd_report(LOG_WARN, "remotegpsd device probe connect error %d\n", dsock);
	    return -1;
    }
    if (write(dsock, "K\n", 2) != 2) {
	    gpsd_report(LOG_WARN, "remotegpsd device probe write error %d\n", dsock);
	    return -1;
    }
    ret = (int)read(dsock, buf, sizeof(buf));
    (void)close(dsock);
    if (strlen(devpath) > 0 && (NULL == strstr(buf, devpath)))
	return 1;
    else
	return 0;
}

static int remotegpsd_stream_open(const char *host,
			     const char *port,
			     struct gps_context_t *context,
			     struct remotegpsd_stream_t *stream)
{
    char buf[BUFSIZ], mode = 'R', level = 1;

    if (0 == strcmp(stream->protocol, "raw")) {
	    mode = 'R';
	    level = 2;
    } else if (0 == strcmp(stream->protocol, "nmea")) {
	    mode = 'R';
	    level = 1;
    } else if (0 == strcmp(stream->protocol, "gpsd")) {
	    mode = 'w';
	    level = 1;
    }

    if ((context->dsock = netlib_connectsock(host, port, "tcp")) == -1){
	gpsd_report(LOG_WARN, "failed to connect to %s:%s\n", host, port);
	return -1;
    }

    if (strlen(stream->devpath)) { /* select device if specified */
	(void)snprintf(buf, sizeof(buf), "F=%s\n", stream->devpath);
	if (write(context->dsock, buf, strlen(buf)) != (ssize_t)strlen(buf)) {
	    gpsd_report(LOG_WARN, "remotegpsd stream write devpath error on %d\n", context->dsock);
	    return -1;
	}

	if (read(context->dsock, buf, sizeof(buf) - 1) == -1)
	    goto close;
    }

    memset(buf, 0, sizeof(buf));
    (void)snprintf(buf, sizeof(buf), "%c=%d\n", mode, level);
    if (write(context->dsock, buf, strlen(buf)) != (ssize_t)strlen(buf)) {
	gpsd_report(LOG_WARN, "remotegpsd stream write protocol error on %d\n", context->dsock);
	return -1;
    }

    if (read(context->dsock, buf, sizeof(buf) - 1) == -1)
	goto close;

    context->netgnss_service = netgnss_remotegpsd;
#ifndef S_SPLINT_S
    context->netgnss_privdata = stream;
#endif
    return context->dsock;
close:
    (void)close(context->dsock);
    return -1;
}

int remotegpsd_open(struct gps_context_t *context, char *uri)
/* open a connection to another gpsd */
{
    char *colon, *quo, *slash;
    char *port = NULL;
    char *protocol = NULL;
    char *devpath = NULL;
    int ret;

    if ((quo = strchr(uri, '?')) != NULL) {
	*quo = '\0';
	protocol = quo + 1;
    } else {
	protocol = "nmea";
    }

    if ((strcmp(protocol, "nmea") == 0)
	&& (strcmp(protocol, "raw") == 0)
	&& (strcmp(protocol, "gpsd") == 0)){
	    gpsd_report(LOG_ERROR, "remotegpsd_open: invalid protocol '%s'\n", protocol);
	    return -1;
    }

    if ((slash = strchr(uri, '/')) != NULL) {
	if ((devpath = strdup(slash)) == NULL)
	    return -1;
	*slash = '\0';
    } else {
	devpath = "";
    }

    if ((colon = strchr(uri, ':')) != NULL) {
	port = colon + 1;
	*colon = '\0';
    }
    if (!port) {
	port = "gpsd";
	if (!getservbyname(port, "tcp"))
	    port = DEFAULT_GPSD_PORT;
    }

    if ((ret = remotegpsd_device_probe(uri, port, devpath))) {
	gpsd_report(LOG_ERROR, "unable to probe for data about device %s:%s%s - %s\n",
		    uri, port, devpath,
		    ret > 0 ? "no such device" : "network error");
	free(devpath);
	return -1;
    }
    gpsd_report(LOG_WARN, "device probe ok\n");
    remotegpsd_stream.devpath = devpath;
    remotegpsd_stream.protocol = protocol;
    ret = remotegpsd_stream_open(uri, port, context, &remotegpsd_stream);
    if (ret >= 0)
	gpsd_report(LOG_PROG,"connection to gpsd %s:%s established.\n",
		    uri, port);
    else
	gpsd_report(LOG_ERROR, "can't connect to gpsd %s:%s\n", uri, port);
    return ret;
}
