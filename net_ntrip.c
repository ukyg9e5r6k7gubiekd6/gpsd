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
#ifndef S_SPLINT_S
#include <sys/types.h>
#include <sys/stat.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif /* S_SPLINT_S */

#include "gpsd.h"
#include "bsd-base64.h"

struct ntrip_stream_t
{
    char mountpoint[101];
    char credentials[128];
    char authStr[128];
    char url[256];
    char port[32]; /* in my /etc/services 16 was the longest */
    bool set; /* found and set */
    enum
    {
	fmt_rtcm2,
	fmt_rtcm2_0,
	fmt_rtcm2_1,
	fmt_rtcm2_2,
	fmt_rtcm2_3,
	fmt_rtcm3,
	fmt_unknown
    } format;
    int carrier;
    double latitude;
    double longitude;
    int nmea;
    enum
    { cmp_enc_none, cmp_enc_unknown } compr_encryp;
    enum
    { auth_none, auth_basic, auth_digest, auth_unknown } authentication;
    int fee;
    int bitrate;
};

#define NTRIP_SOURCETABLE	"SOURCETABLE 200 OK\r\n"
#define NTRIP_ENDSOURCETABLE	"ENDSOURCETABLE"
#define NTRIP_CAS		"CAS;"
#define NTRIP_NET		"NET;"
#define NTRIP_STR		"STR;"
#define NTRIP_BR		"\r\n"
#define NTRIP_QSC		"\";\""
#define NTRIP_ICY		"ICY 200 OK"
#define NTRIP_UNAUTH		"401 Unauthorized"

static struct ntrip_stream_t ntrip_stream;

/*@ -temptrans -mustfreefresh @*/
static /*@null@*/ char *ntrip_field_iterate( /*@null@ */ char *start,
					     /*@null@*/ char *prev,
					     const char *eol)
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

    gpsd_report(LOG_RAW, "Next Ntrip source table field %s\n", s);

    return s;
}

/*@ +temptrans +mustfreefresh @*/

/*@ -mustfreefresh @*/
static void ntrip_str_parse(char *str, size_t len,
			    /*@out@*/ struct ntrip_stream_t *hold)
{
    char *s, *eol = str + len;

    memset(hold, 0, sizeof(*hold));

    /* <mountpoint> */
    if ((s = ntrip_field_iterate(str, NULL, eol)))
	strncpy(hold->mountpoint, s, sizeof(hold->mountpoint) - 1);
    /* <identifier> */
    s = ntrip_field_iterate(NULL, s, eol);
    /* <format> */
    if ((s = ntrip_field_iterate(NULL, s, eol))) {
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
	    hold->format = fmt_rtcm3;
	else
	    hold->format = fmt_unknown;
    }
    /* <format-details> */
    s = ntrip_field_iterate(NULL, s, eol);
    /* <carrier> */
    if ((s = ntrip_field_iterate(NULL, s, eol)))
	(void)sscanf(s, "%d", &hold->carrier);
    /* <nav-system> */
    s = ntrip_field_iterate(NULL, s, eol);
    /* <network> */
    s = ntrip_field_iterate(NULL, s, eol);
    /* <country> */
    s = ntrip_field_iterate(NULL, s, eol);
    /* <latitude> */
    hold->latitude = NAN;
    if ((s = ntrip_field_iterate(NULL, s, eol)))
	(void)sscanf(s, "%lf", &hold->latitude);
    /* <longitude> */
    hold->longitude = NAN;
    if ((s = ntrip_field_iterate(NULL, s, eol)))
	(void)sscanf(s, "%lf", &hold->longitude);
    /* <nmea> */
    if ((s = ntrip_field_iterate(NULL, s, eol))) {
	(void)sscanf(s, "%d", &hold->nmea);
    }
    /* <solution> */
    s = ntrip_field_iterate(NULL, s, eol);
    /* <generator> */
    s = ntrip_field_iterate(NULL, s, eol);
    /* <compr-encryp> */
    if ((s = ntrip_field_iterate(NULL, s, eol))) {
	if (strcasecmp("none", s) == 0)
	    hold->compr_encryp = cmp_enc_none;
	else
	    hold->compr_encryp = cmp_enc_unknown;
    }
    /* <authentication> */
    if ((s = ntrip_field_iterate(NULL, s, eol))) {
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
    if ((s = ntrip_field_iterate(NULL, s, eol))) {
	(void)sscanf(s, "%d", &hold->fee);
    }
    /* <bitrate> */
    if ((s = ntrip_field_iterate(NULL, s, eol))) {
	(void)sscanf(s, "%d", &hold->bitrate);
    }
    /* ...<misc> */
    while ((s = ntrip_field_iterate(NULL, s, eol)));
}

static int ntrip_sourcetable_parse(struct gps_device_t *dev)
{
    struct ntrip_stream_t hold;
    ssize_t llen, len = 0;
    char *line;
    bool sourcetable = false;
    bool match = false;
    char buf[BUFSIZ];
    size_t blen = BUFSIZ;
    int fd = dev->gpsdata.gps_fd;
    struct gps_context_t *context = dev->context;

    while (1) {
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
	    gpsd_report(LOG_ERROR, "ntrip stream read error %d on fd %d\n",
		    errno, fd);
	    return -1;
	} else if (rlen == 0) { // server closed the connection
	    gpsd_report(LOG_ERROR, "ntrip stream unexpected close %d on fd %d during sourcetable read\n",
		    errno, fd);
	    return -1;
	}

	line = buf;
	rlen = len += rlen;

	gpsd_report(LOG_RAW, "Ntrip source table buffer %s\n", buf);

	sourcetable = context->ntrip_sourcetable_parse;
	if (!sourcetable) {
	    /* parse SOURCETABLE */
	    if (strncmp(line, NTRIP_SOURCETABLE, strlen(NTRIP_SOURCETABLE)) ==
		    0) {
		sourcetable = true;
		context->ntrip_sourcetable_parse = true;
		llen = (ssize_t) strlen(NTRIP_SOURCETABLE);
		line += llen;
		len -= llen;
	    } else {
		gpsd_report(LOG_WARN, "Received unexpexted Ntrip reply %s.\n",
			buf);
		return -1;
	    }
	}

	while (len > 0) {
	    /* parse ENDSOURCETABLE */
	    if (strncmp
		    (line, NTRIP_ENDSOURCETABLE,
		     strlen(NTRIP_ENDSOURCETABLE)) == 0)
		goto done;

	    if (!(eol = strstr(line, NTRIP_BR)))
		break;

	    gpsd_report(LOG_IO, "next Ntrip source table line %s\n", line);

	    *eol = '\0';
	    llen = (ssize_t) (eol - line);

	    /* todo: parse headers */

	    /* parse STR */
	    if (strncmp(line, NTRIP_STR, strlen(NTRIP_STR)) == 0) {
		ntrip_str_parse(line + strlen(NTRIP_STR),
			(size_t) (llen - strlen(NTRIP_STR)), &hold);
		if (strcmp(ntrip_stream.mountpoint, hold.mountpoint) == 0) {
		    /* todo: support for RTCM 3.0, SBAS (WAAS, EGNOS), ... */
		    if (hold.format == fmt_unknown) {
			gpsd_report(LOG_ERROR,
				"Ntrip stream %s format not supported\n",
				line);
			return -1;
		    }
		    /* todo: support encryption and compression algorithms */
		    if (hold.compr_encryp != cmp_enc_none) {
			gpsd_report(LOG_ERROR,
				"Ntrip stream %s compression/encryption algorithm not supported\n",
				line);
			return -1;
		    }
		    /* todo: support digest authentication */
		    if (hold.authentication != auth_none
			    && hold.authentication != auth_basic) {
			gpsd_report(LOG_ERROR,
				"Ntrip stream %s authentication method not supported\n",
				line);
			return -1;
		    }
		    /* no memcpy, so we can keep the other infos */
		    ntrip_stream.format = hold.format;
		    ntrip_stream.carrier = hold.carrier;
		    ntrip_stream.latitude = hold.latitude;
		    ntrip_stream.longitude = hold.longitude;
		    ntrip_stream.nmea = hold.nmea;
		    ntrip_stream.compr_encryp = hold.compr_encryp;
		    ntrip_stream.authentication = hold.authentication;
		    ntrip_stream.fee = hold.fee;
		    ntrip_stream.bitrate = hold.bitrate;
		    ntrip_stream.set = true;
		    match = true;
		}
		/* todo: compare stream location to own location to
		 * find nearest stream if user hasn't provided one */
	    }
	    /* todo: parse CAS */
	    /* else if (strncmp(line, NTRIP_CAS, strlen(NTRIP_CAS))==0); */

	    /* todo: parse NET */
	    /* else if (strncmp(line, NTRIP_NET, strlen(NTRIP_NET))==0); */

	    llen += strlen(NTRIP_BR);
	    line += llen;
	    len -= llen;
	    gpsd_report(LOG_RAW,
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

static int ntrip_stream_req_probe(void)
{
    int dsock;
    ssize_t r;
    char buf[BUFSIZ];

    dsock = netlib_connectsock(AF_UNSPEC, ntrip_stream.url, ntrip_stream.port, "tcp");
    if (dsock < 0) {
	gpsd_report(LOG_ERROR, "ntrip stream connect error %d in req probe\n", dsock);
	return -1;
    }
    gpsd_report(LOG_SPIN, "ntrip stream for req probe connected on fd %d\n", dsock);
    (void)snprintf(buf, sizeof(buf),
	    "GET / HTTP/1.1\r\n"
	    "User-Agent: NTRIP gpsd/%s\r\n"
	    "Connection: close\r\n"
	    "\r\n", VERSION);
    r = write(dsock, buf, strlen(buf));
    if (r != (ssize_t)strlen(buf)) {
	gpsd_report(LOG_ERROR, "ntrip stream write error %d on fd %d during probe request %zd\n",
		errno, dsock, r);
	return -1;
    }
    return dsock;
}

static int ntrip_auth_encode(const struct ntrip_stream_t *stream,
	const char *auth,
	/*@out@*/ char buf[],
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

static int ntrip_stream_get_req(void)
{
    int dsock;
    char buf[BUFSIZ];

    dsock = netlib_connectsock(AF_UNSPEC, ntrip_stream.url, ntrip_stream.port, "tcp");
    if (dsock == -1) {
	gpsd_report(LOG_ERROR, "ntrip stream connect error %d\n", dsock);
	return -1;
    }

    gpsd_report(LOG_SPIN, "netlib_connectsock() returns socket on fd %d\n",
	    dsock);

    (void)snprintf(buf, sizeof(buf),
	    "GET /%s HTTP/1.1\r\n"
	    "User-Agent: NTRIP gpsd/%s\r\n"
	    "Accept: rtk/rtcm, dgps/rtcm\r\n"
	    "%s"
	    "Connection: close\r\n"
	    "\r\n", ntrip_stream.mountpoint, VERSION, ntrip_stream.authStr);
    if (write(dsock, buf, strlen(buf)) != (ssize_t) strlen(buf)) {
	gpsd_report(LOG_ERROR, "ntrip stream write error %d on fd %d during get request\n", errno,
		dsock);
	(void)close(dsock);
	return -1;
    }
    return dsock;
}

static int ntrip_stream_get_parse (int dsock)
{
/*@-nullpass@*/
    char buf[BUFSIZ];
    int opts;
    memset(buf, 0, sizeof(buf));
    while (read(dsock, buf, sizeof(buf) - 1) == -1) {
	if (errno == EINTR)
	    continue;
	gpsd_report(LOG_ERROR, "ntrip stream read error %d on fd %d during get rsp\n", errno,
		dsock);
	goto close;
    }

    /* parse 401 Unauthorized */
    if (strstr(buf, NTRIP_UNAUTH)!=NULL) {
	gpsd_report(LOG_ERROR,
		"not authorized for Ntrip stream %s/%s\n", ntrip_stream.url,
		ntrip_stream.mountpoint);
	goto close;
    }
    /* parse SOURCETABLE */
    if (strstr(buf, NTRIP_SOURCETABLE)!=NULL) {
	gpsd_report(LOG_ERROR,
		"Broadcaster doesn't recognize Ntrip stream %s:%s/%s\n",
		ntrip_stream.url, ntrip_stream.port, ntrip_stream.mountpoint);
	goto close;
    }
    /* parse ICY 200 OK */
    if (strstr(buf, NTRIP_ICY)==NULL) {
	gpsd_report(LOG_ERROR,
		"Unknown reply %s from Ntrip service %s:%s/%s\n", buf,
		ntrip_stream.url, ntrip_stream.port, ntrip_stream.mountpoint);
	goto close;
    }
    opts = fcntl(dsock, F_GETFL);

    if (opts >= 0)
	(void)fcntl(dsock, F_SETFL, opts | O_NONBLOCK);

    return dsock;
close:
    (void)close(dsock);
    return -1;
/*@+nullpass@*/
}

/*@ -branchstate -nullpass @*/
int ntrip_open(struct gps_device_t *dev, char *caster)
    /* open a connection to a Ntrip broadcaster */
{
    char *amp, *colon, *slash;
    char *auth = NULL;
    char *port = NULL;
    char *stream = NULL;
    char *url = NULL;
    int ret;
    char t[strlen(caster + 1)];
    char *tmp = t;
    struct gps_context_t *context = dev->context;

    switch (context->ntrip_conn_state) {
	case ntrip_conn_init:
	    ntrip_stream.set = false;
	    strcpy(tmp, caster);

	    /*@ -boolops @*/
	    if ((amp = strchr(tmp, '@')) != NULL) {
		if (((colon = strchr(tmp, ':')) != NULL) && colon < amp) {
		    auth = tmp;
		    *amp = '\0';
		    tmp = amp + 1;
		    url = tmp;
		} else {
		    gpsd_report(LOG_ERROR,
			    "can't extract user-ID and password from %s\n",
			    caster);
		    context->ntrip_conn_state = ntrip_conn_err;
		    return -1;
		}
	    }
	    /*@ +boolops @*/
	    if ((slash = strchr(tmp, '/')) != NULL) {
		*slash = '\0';
		stream = slash + 1;
	    } else {
		/* todo: add autoconnect like in dgpsip.c */
		gpsd_report(LOG_ERROR, "can't extract Ntrip stream from %s\n",
			caster);
		context->ntrip_conn_state = ntrip_conn_err;
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

	    /* this has to be done here, because it is needed for multi-stage connection */
	    context->netgnss_service = netgnss_ntrip;
	    strncpy(ntrip_stream.mountpoint, stream, 101); /* magic numbers from struct definitions */
	    strncpy(ntrip_stream.credentials, auth, 128); /* magic numbers from struct definitions */
	    strncpy(ntrip_stream.url, url, 256);
	    strncpy(ntrip_stream.port, port, 32);

	    ret = ntrip_stream_req_probe();
	    if (ret == -1) {
		context->ntrip_conn_state = ntrip_conn_err;
		return -1;
	    }
	    dev->gpsdata.gps_fd = ret;
	    context->ntrip_conn_state = ntrip_conn_sent_probe;
	    return ret;
	case ntrip_conn_sent_probe:
	    ret = ntrip_sourcetable_parse(dev);
	    if (ret == -1) {
		context->ntrip_conn_state = ntrip_conn_err;
		return -1;
	    }
	    if (ret == 0 && ntrip_stream.set == false) {
		return ret;
	    }
	    (void)close(dev->gpsdata.gps_fd);
	    if (ntrip_auth_encode(&ntrip_stream, ntrip_stream.credentials, ntrip_stream.authStr, 128) != 0) {
		context->ntrip_conn_state = ntrip_conn_err;
		return -1;
	    }
	    ret = ntrip_stream_get_req();
	    if (ret == -1) {
		context->ntrip_conn_state = ntrip_conn_err;
		return -1;
	    }
	    dev->gpsdata.gps_fd = ret;
	    context->ntrip_conn_state = ntrip_conn_sent_get;
	    break;
	case ntrip_conn_sent_get:
	    ret = ntrip_stream_get_parse(dev->gpsdata.gps_fd);
	    if (ret == -1) {
		context->ntrip_conn_state = ntrip_conn_err;
		return -1;
	    }
	    context->ntrip_conn_state = ntrip_conn_established;
	    context->ntrip_works = true; // we know, this worked.
	    break;
	case ntrip_conn_established:
	case ntrip_conn_err:
	    return -1;
    }
    return ret;
}
/*@ +branchstate +nullpass @*/

void ntrip_report(struct gps_device_t *session)
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
    if (ntrip_stream.nmea != 0
	    && session->context->fixcnt > 10 && (count % 5)==0) {
	if (session->context->dsock > -1) {
	    char buf[BUFSIZ];
	    gpsd_position_fix_dump(session, buf, sizeof(buf));
	    if (write(session->context->dsock, buf, strlen(buf)) ==
		    (ssize_t) strlen(buf)) {
		gpsd_report(LOG_IO, "=> dgps %s\n", buf);
	    } else {
		gpsd_report(LOG_IO, "ntrip report write failed\n");
	    }
	}
    }
}
