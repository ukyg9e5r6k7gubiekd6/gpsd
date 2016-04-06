/* net_ntrip.c -- gather and dispatch DGNSS data from Ntrip broadcasters
 *
 * This file is Copyright (c) 2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include "gpsd.h"
#include "strfuncs.h"

#define NTRIP_SOURCETABLE	"SOURCETABLE 200 OK\r\n"
#define NTRIP_ENDSOURCETABLE	"ENDSOURCETABLE"
#define NTRIP_CAS		"CAS;"
#define NTRIP_NET		"NET;"
#define NTRIP_STR		"STR;"
#define NTRIP_BR		"\r\n"
#define NTRIP_QSC		"\";\""
#define NTRIP_ICY		"ICY 200 OK"
#define NTRIP_UNAUTH		"401 Unauthorized"

static char *ntrip_field_iterate(char *start,
				 char *prev,
				 const char *eol,
				 const struct gpsd_errout_t *errout)
{
    char *s, *t, *u;

    if (start)
	s = start;
    else {
	if (!prev)
	    return NULL;
	s = prev + strlen(prev) + 1;
	if (s >= eol)
	    return NULL;
    }

    /* ignore any quoted ; chars as they are part of the field content */
    t = s;
    while ((u = strstr(t, NTRIP_QSC)))
	t = u + strlen(NTRIP_QSC);

    if ((t = strstr(t, ";")))
	*t = '\0';

    gpsd_log(errout, LOG_RAW, "Next Ntrip source table field %s\n", s);

    return s;
}


static void ntrip_str_parse(char *str, size_t len,
			    struct ntrip_stream_t *hold,
			    const struct gpsd_errout_t *errout)
{
    char *s, *eol = str + len;

    memset(hold, 0, sizeof(*hold));

    /* <mountpoint> */
    if ((s = ntrip_field_iterate(str, NULL, eol, errout)))
	(void)strlcpy(hold->mountpoint, s, sizeof(hold->mountpoint));
    /* <identifier> */
    s = ntrip_field_iterate(NULL, s, eol, errout);
    /* <format> */
    if ((s = ntrip_field_iterate(NULL, s, eol, errout))) {
	if (strcasecmp("RTCM 2", s) == 0)
	    hold->format = fmt_rtcm2;
	else if (strcasecmp("RTCM 2.0", s) == 0)
	    hold->format = fmt_rtcm2_0;
	else if (strcasecmp("RTCM 2.1", s) == 0)
	    hold->format = fmt_rtcm2_1;
	else if (strcasecmp("RTCM 2.2", s) == 0)
	    hold->format = fmt_rtcm2_2;
	else if (strcasecmp("RTCM 2.3", s) == 0)
	    hold->format = fmt_rtcm2_3;
	/* required for the SAPOS derver in Gemany, confirmed as RTCM2.3 */
	else if (strcasecmp("RTCM1_", s) == 0)
	    hold->format = fmt_rtcm2_3;
	else if (strcasecmp("RTCM 3.0", s) == 0)
	    hold->format = fmt_rtcm3_0;
	else if (strcasecmp("RTCM 3.1", s) == 0)
	    hold->format = fmt_rtcm3_1;
	else if (strcasecmp("RTCM 3.2", s) == 0)
	    hold->format = fmt_rtcm3_2;
	else
	    hold->format = fmt_unknown;
    }
    /* <format-details> */
    s = ntrip_field_iterate(NULL, s, eol, errout);
    /* <carrier> */
    if ((s = ntrip_field_iterate(NULL, s, eol, errout)))
	hold->carrier = atoi(s);
    /* <nav-system> */
    s = ntrip_field_iterate(NULL, s, eol, errout);
    /* <network> */
    s = ntrip_field_iterate(NULL, s, eol, errout);
    /* <country> */
    s = ntrip_field_iterate(NULL, s, eol, errout);
    /* <latitude> */
    hold->latitude = NAN;
    if ((s = ntrip_field_iterate(NULL, s, eol, errout)))
	hold->latitude = safe_atof(s);
    /* <longitude> */
    hold->longitude = NAN;
    if ((s = ntrip_field_iterate(NULL, s, eol, errout)))
	hold->longitude = safe_atof(s);
    /* <nmea> */
    if ((s = ntrip_field_iterate(NULL, s, eol, errout))) {
	hold->nmea = atoi(s);
    }
    /* <solution> */
    s = ntrip_field_iterate(NULL, s, eol, errout);
    /* <generator> */
    s = ntrip_field_iterate(NULL, s, eol, errout);
    /* <compr-encryp> */
    if ((s = ntrip_field_iterate(NULL, s, eol, errout))) {
	if (strcasecmp("none", s) == 0)
	    hold->compr_encryp = cmp_enc_none;
	else
	    hold->compr_encryp = cmp_enc_unknown;
    }
    /* <authentication> */
    if ((s = ntrip_field_iterate(NULL, s, eol, errout))) {
	if (strcasecmp("N", s) == 0)
	    hold->authentication = auth_none;
	else if (strcasecmp("B", s) == 0)
	    hold->authentication = auth_basic;
	else if (strcasecmp("D", s) == 0)
	    hold->authentication = auth_digest;
	else
	    hold->authentication = auth_unknown;
    }
    /* <fee> */
    if ((s = ntrip_field_iterate(NULL, s, eol, errout))) {
	hold->fee = atoi(s);
    }
    /* <bitrate> */
    if ((s = ntrip_field_iterate(NULL, s, eol, errout))) {
	hold->bitrate = atoi(s);
    }
    /* ...<misc> */
    while ((s = ntrip_field_iterate(NULL, s, eol, errout)));
}

static int ntrip_sourcetable_parse(struct gps_device_t *device)
{
    struct ntrip_stream_t hold;
    ssize_t llen, len = 0;
    char *line;
    bool sourcetable = false;
    bool match = false;
    char buf[BUFSIZ];
    size_t blen = sizeof(buf);
    int fd = device->gpsdata.gps_fd;

    for (;;) {
	char *eol;
	ssize_t rlen;

	memset(&buf[len], 0, (size_t) (blen - len));
	rlen = read(fd, &buf[len], (size_t) (blen - 1 - len));
	if (rlen == -1) {
	    if (errno == EINTR) {
		continue;
	    }
	    if (sourcetable && !match && errno == EAGAIN) { // we have not yet found a match, but there currently is no more data
		return 0;
	    }
	    if (match) {
		return 1;
	    }
	    gpsd_log(&device->context->errout, LOG_ERROR,
		     "ntrip stream read error %d on fd %d\n",
		     errno, fd);
	    return -1;
	} else if (rlen == 0) { // server closed the connection
	    gpsd_log(&device->context->errout, LOG_ERROR,
		     "ntrip stream unexpected close %d on fd %d during sourcetable read\n",
		     errno, fd);
	    return -1;
	}

	line = buf;
	rlen = len += rlen;

	gpsd_log(&device->context->errout, LOG_RAW,
		 "Ntrip source table buffer %s\n", buf);

	sourcetable = device->ntrip.sourcetable_parse;
	if (!sourcetable) {
	    /* parse SOURCETABLE */
	    if (str_starts_with(line, NTRIP_SOURCETABLE)) {
		sourcetable = true;
		device->ntrip.sourcetable_parse = true;
		llen = (ssize_t) strlen(NTRIP_SOURCETABLE);
		line += llen;
		len -= llen;
	    } else {
		gpsd_log(&device->context->errout, LOG_WARN,
			 "Received unexpexted Ntrip reply %s.\n",
			 buf);
		return -1;
	    }
	}

	while (len > 0) {
	    /* parse ENDSOURCETABLE */
	    if (str_starts_with(line, NTRIP_ENDSOURCETABLE))
		goto done;

	    /* coverity[string_null] - nul-terminated by previous memset */
	    if (!(eol = strstr(line, NTRIP_BR)))
		break;

	    gpsd_log(&device->context->errout, LOG_DATA,
		     "next Ntrip source table line %s\n", line);

	    *eol = '\0';
	    llen = (ssize_t) (eol - line);

	    /* todo: parse headers */

	    /* parse STR */
	    if (str_starts_with(line, NTRIP_STR)) {
		ntrip_str_parse(line + strlen(NTRIP_STR),
				(size_t) (llen - strlen(NTRIP_STR)),
				&hold, &device->context->errout);
		if (strcmp(device->ntrip.stream.mountpoint, hold.mountpoint) == 0) {
		    /* todo: support for RTCM 3.0, SBAS (WAAS, EGNOS), ... */
		    if (hold.format == fmt_unknown) {
			gpsd_log(&device->context->errout, LOG_ERROR,
				 "Ntrip stream %s format not supported\n",
				 line);
			return -1;
		    }
		    /* todo: support encryption and compression algorithms */
		    if (hold.compr_encryp != cmp_enc_none) {
			gpsd_log(&device->context->errout, LOG_ERROR,
				 "Ntrip stream %s compression/encryption algorithm not supported\n",
				 line);
			return -1;
		    }
		    /* todo: support digest authentication */
		    if (hold.authentication != auth_none
			    && hold.authentication != auth_basic) {
			gpsd_log(&device->context->errout, LOG_ERROR,
				 "Ntrip stream %s authentication method not supported\n",
				line);
			return -1;
		    }
		    /* no memcpy, so we can keep the other infos */
		    device->ntrip.stream.format = hold.format;
		    device->ntrip.stream.carrier = hold.carrier;
		    device->ntrip.stream.latitude = hold.latitude;
		    device->ntrip.stream.longitude = hold.longitude;
		    device->ntrip.stream.nmea = hold.nmea;
		    device->ntrip.stream.compr_encryp = hold.compr_encryp;
		    device->ntrip.stream.authentication = hold.authentication;
		    device->ntrip.stream.fee = hold.fee;
		    device->ntrip.stream.bitrate = hold.bitrate;
		    device->ntrip.stream.set = true;
		    match = true;
		}
		/* todo: compare stream location to own location to
		 * find nearest stream if user hasn't provided one */
	    }
	    /* todo: parse CAS */
	    /* else if (str_starts_with(line, NTRIP_CAS)); */

	    /* todo: parse NET */
	    /* else if (str_starts_with(line, NTRIP_NET)); */

	    llen += strlen(NTRIP_BR);
	    line += llen;
	    len -= llen;
	    gpsd_log(&device->context->errout, LOG_RAW,
		     "Remaining Ntrip source table buffer %zd %s\n", len,
		     line);
	}
	/* message too big to fit into buffer */
	if ((size_t)len == blen - 1)
	    return -1;

	if (len > 0)
	    memmove(buf, &buf[rlen - len], (size_t) len);
    }

done:
    return match ? 1 : -1;
}

static int ntrip_stream_req_probe(const struct ntrip_stream_t *stream,
				  struct gpsd_errout_t *errout)
{
    int dsock;
    ssize_t r;
    char buf[BUFSIZ];

    dsock = netlib_connectsock(AF_UNSPEC, stream->url, stream->port, "tcp");
    if (dsock < 0) {
	gpsd_log(errout, LOG_ERROR,
		 "ntrip stream connect error %d in req probe\n", dsock);
	return -1;
    }
    gpsd_log(errout, LOG_SPIN,
	     "ntrip stream for req probe connected on fd %d\n", dsock);
    (void)snprintf(buf, sizeof(buf),
	    "GET / HTTP/1.1\r\n"
	    "User-Agent: NTRIP gpsd/%s\r\n"
	    "Host: %s\r\n"
	    "Connection: close\r\n"
	    "\r\n", VERSION, stream->url);
    r = write(dsock, buf, strlen(buf));
    if (r != (ssize_t)strlen(buf)) {
	gpsd_log(errout, LOG_ERROR,
		 "ntrip stream write error %d on fd %d during probe request %zd\n",
		 errno, dsock, r);
	(void)close(dsock);
	return -1;
    }
    /* coverity[leaked_handle] This is an intentional allocation */
    return dsock;
}

static int ntrip_auth_encode(const struct ntrip_stream_t *stream,
			     const char *auth,
			     char buf[],
			     size_t size)
{
    memset(buf, 0, size);
    if (stream->authentication == auth_none)
	return 0;
    else if (stream->authentication == auth_basic) {
	char authenc[64];
	if (!auth)
	    return -1;
	memset(authenc, 0, sizeof(authenc));
	if (b64_ntop
		((unsigned char *)auth, strlen(auth), authenc,
		 sizeof(authenc) - 1) < 0)
	    return -1;
	(void)snprintf(buf, size - 1, "Authorization: Basic %s\r\n", authenc);
    } else {
	/* todo: support digest authentication */
    }
    return 0;
}

/* *INDENT-ON* */

static int ntrip_stream_get_req(const struct ntrip_stream_t *stream,
				const struct gpsd_errout_t *errout)
{
    int dsock;
    char buf[BUFSIZ];

    dsock = netlib_connectsock(AF_UNSPEC, stream->url, stream->port, "tcp");
    if (BAD_SOCKET(dsock)) {
	gpsd_log(errout, LOG_ERROR,
		 "ntrip stream connect error %d\n", dsock);
	return -1;
    }

    gpsd_log(errout, LOG_SPIN,
	     "netlib_connectsock() returns socket on fd %d\n",
	     dsock);

    (void)snprintf(buf, sizeof(buf),
	    "GET /%s HTTP/1.1\r\n"
	    "User-Agent: NTRIP gpsd/%s\r\n"
	    "Host: %s\r\n"
	    "Accept: rtk/rtcm, dgps/rtcm\r\n"
	    "%s"
	    "Connection: close\r\n"
	    "\r\n", stream->mountpoint, VERSION, stream->url, stream->authStr);
    if (write(dsock, buf, strlen(buf)) != (ssize_t) strlen(buf)) {
	gpsd_log(errout, LOG_ERROR,
		 "ntrip stream write error %d on fd %d during get request\n", errno,
		 dsock);
	(void)close(dsock);
	return -1;
    }
    return dsock;
}

static int ntrip_stream_get_parse(const struct ntrip_stream_t *stream,
				  const int dsock,
				  const struct gpsd_errout_t *errout)
{
    char buf[BUFSIZ];
    int opts;
    memset(buf, 0, sizeof(buf));
    while (read(dsock, buf, sizeof(buf) - 1) == -1) {
	if (errno == EINTR)
	    continue;
	gpsd_log(errout, LOG_ERROR,
		 "ntrip stream read error %d on fd %d during get rsp\n", errno,
		 dsock);
	goto close;
    }

    /* parse 401 Unauthorized */
    /* coverity[string_null] - guaranteed terminated by the memset above */
    if (strstr(buf, NTRIP_UNAUTH)!=NULL) {
	gpsd_log(errout, LOG_ERROR,
		 "not authorized for Ntrip stream %s/%s\n", stream->url,
		 stream->mountpoint);
	goto close;
    }
    /* parse SOURCETABLE */
    if (strstr(buf, NTRIP_SOURCETABLE)!=NULL) {
	gpsd_log(errout, LOG_ERROR,
		 "Broadcaster doesn't recognize Ntrip stream %s:%s/%s\n",
		 stream->url, stream->port, stream->mountpoint);
	goto close;
    }
    /* parse ICY 200 OK */
    if (strstr(buf, NTRIP_ICY)==NULL) {
	gpsd_log(errout, LOG_ERROR,
		 "Unknown reply %s from Ntrip service %s:%s/%s\n", buf,
		 stream->url, stream->port, stream->mountpoint);
	goto close;
    }
    opts = fcntl(dsock, F_GETFL);

    if (opts >= 0)
	(void)fcntl(dsock, F_SETFL, opts | O_NONBLOCK);

    return dsock;
close:
    (void)close(dsock);
    return -1;
}

int ntrip_open(struct gps_device_t *device, char *caster)
    /* open a connection to a Ntrip broadcaster */
{
    char *amp, *colon, *slash;
    char *auth = NULL;
    char *port = NULL;
    char *stream = NULL;
    char *url = NULL;
    int ret = -1;
    char t[strlen(caster) + 1];
    char *tmp = t;

    switch (device->ntrip.conn_state) {
	case ntrip_conn_init:
	    /* this has to be done here, because it is needed for multi-stage connection */
	    device->servicetype = service_ntrip;
	    device->ntrip.works = false;
	    device->ntrip.sourcetable_parse = false;
	    device->ntrip.stream.set = false;
	    (void)strlcpy(tmp, caster, sizeof(t));

	    if ((amp = strchr(tmp, '@')) != NULL) {
		if (((colon = strchr(tmp, ':')) != NULL) && colon < amp) {
		    auth = tmp;
		    *amp = '\0';
		    tmp = amp + 1;
		    url = tmp;
		} else {
		    gpsd_log(&device->context->errout, LOG_ERROR,
			     "can't extract user-ID and password from %s\n",
			     caster);
		    device->ntrip.conn_state = ntrip_conn_err;
		    return -1;
		}
	    }
	    if ((slash = strchr(tmp, '/')) != NULL) {
		*slash = '\0';
		stream = slash + 1;
	    } else {
		/* todo: add autoconnect like in dgpsip.c */
		gpsd_log(&device->context->errout, LOG_ERROR,
			 "can't extract Ntrip stream from %s\n",
			 caster);
		device->ntrip.conn_state = ntrip_conn_err;
		return -1;
	    }
	    if ((colon = strchr(tmp, ':')) != NULL) {
		port = colon + 1;
		*colon = '\0';
	    }
	    if (!port) {
		port = "rtcm-sc104";
		if (!getservbyname(port, "tcp"))
		    port = DEFAULT_RTCM_PORT;
	    }

	    (void)strlcpy(device->ntrip.stream.mountpoint,
		    stream,
		    sizeof(device->ntrip.stream.mountpoint));
	    if (auth != NULL)
		(void)strlcpy(device->ntrip.stream.credentials,
			      auth,
			      sizeof(device->ntrip.stream.credentials));
	    /*
	     * Semantically url and port ought to be non-NULL by now,
	     * but just in case...this code appeases Coverity.
	     */
	    if (url != NULL)
		(void)strlcpy(device->ntrip.stream.url,
			      url,
			      sizeof(device->ntrip.stream.url));
	    if (port != NULL)
		(void)strlcpy(device->ntrip.stream.port,
			      port,
			      sizeof(device->ntrip.stream.port));

	    ret = ntrip_stream_req_probe(&device->ntrip.stream,
					 &device->context->errout);
	    if (ret == -1) {
		device->ntrip.conn_state = ntrip_conn_err;
		return -1;
	    }
	    device->gpsdata.gps_fd = ret;
	    device->ntrip.conn_state = ntrip_conn_sent_probe;
	    return ret;
	case ntrip_conn_sent_probe:
	    ret = ntrip_sourcetable_parse(device);
	    if (ret == -1) {
		device->ntrip.conn_state = ntrip_conn_err;
		return -1;
	    }
	    if (ret == 0 && device->ntrip.stream.set == false) {
		return ret;
	    }
	    (void)close(device->gpsdata.gps_fd);
	    if (ntrip_auth_encode(&device->ntrip.stream, device->ntrip.stream.credentials, device->ntrip.stream.authStr, sizeof(device->ntrip.stream.authStr)) != 0) {
		device->ntrip.conn_state = ntrip_conn_err;
		return -1;
	    }
	    ret = ntrip_stream_get_req(&device->ntrip.stream,
				       &device->context->errout);
	    if (ret == -1) {
		device->ntrip.conn_state = ntrip_conn_err;
		return -1;
	    }
	    device->gpsdata.gps_fd = ret;
	    device->ntrip.conn_state = ntrip_conn_sent_get;
	    break;
	case ntrip_conn_sent_get:
	    ret = ntrip_stream_get_parse(&device->ntrip.stream,
					 device->gpsdata.gps_fd,
					 &device->context->errout);
	    if (ret == -1) {
		device->ntrip.conn_state = ntrip_conn_err;
		return -1;
	    }
	    device->ntrip.conn_state = ntrip_conn_established;
	    device->ntrip.works = true; // we know, this worked.
	    break;
	case ntrip_conn_established:
	case ntrip_conn_err:
	    return -1;
    }
    return ret;
}

void ntrip_report(struct gps_context_t *context,
		  struct gps_device_t *gps,
		  struct gps_device_t *caster)
    /* may be time to ship a usage report to the Ntrip caster */
{
    static int count;
    /*
     * 10 is an arbitrary number, the point is to have gotten several good
     * fixes before reporting usage to our Ntrip caster.
     *
     * count % 5 is as arbitrary a number as the fixcnt. But some delay
     * was needed here
     */
    count ++;
    if (caster->ntrip.stream.nmea != 0 && context->fixcnt > 10 && (count % 5)==0) {
	if (caster->gpsdata.gps_fd > -1) {
	    char buf[BUFSIZ];
	    gpsd_position_fix_dump(gps, buf, sizeof(buf));
	    if (write(caster->gpsdata.gps_fd, buf, strlen(buf)) ==
		    (ssize_t) strlen(buf)) {
		gpsd_log(&context->errout, LOG_IO, "=> dgps %s\n", buf);
	    } else {
		gpsd_log(&context->errout, LOG_IO, "ntrip report write failed\n");
	    }
	}
    }
}
