/* libgpsd_core.c -- manage access to sensors
 *
 * Access to the driver layer goes through the entry points in this file.
 * The idea is to present a session as an abstraction from which you get
 * fixes (and possibly other data updates) by calling gpsd_multipoll(). The
 * rest is setup and teardown. (For backward compatibility the older gpsd_poll()
 * entry point has been retained.)
 *
 * This file is Copyright (c) 2010 by the GPSD project
 * SPDX-License-Identifier: BSD-2-clause
 */

#ifdef __linux__
/* FreeBSD chokes on this */
/* getsid() needs _XOPEN_SOURCE, 500 means X/Open 1995 */
#define _XOPEN_SOURCE 500
/* isfinite() and pselect() needs  _POSIX_C_SOURCE >= 200112L */
#define  _POSIX_C_SOURCE 200112L
#endif /* __linux__ */

/* strlcpy() needs _DARWIN_C_SOURCE */
#define _DARWIN_C_SOURCE

#include <time.h>
#include <stdio.h>
#include <stdlib.h>

#include <stdbool.h>
#include <libgen.h>
#include <math.h>
#include <string.h>
#include <stdarg.h>
#include <fcntl.h>
#include <ctype.h>
#include <syslog.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <assert.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <unistd.h>

#include "gpsd.h"
#include "matrix.h"
#include "strfuncs.h"
#if defined(NMEA2000_ENABLE)
#include "driver_nmea2000.h"
#endif /* defined(NMEA2000_ENABLE) */

ssize_t gpsd_write(struct gps_device_t *session,
		   const char *buf,
		   const size_t len)
/* pass low-level data to devices straight through */
{
    return session->context->serial_write(session, buf, len);
}

static void basic_report(const char *buf)
{
    (void)fputs(buf, stderr);
}

void errout_reset(struct gpsd_errout_t *errout)
{
    errout->debug = LOG_SHOUT;
    errout->report = basic_report;
}

#if defined(PPS_ENABLE)
static pthread_mutex_t report_mutex;

void gpsd_acquire_reporting_lock(void)
{
    int err;
    err = pthread_mutex_lock(&report_mutex);
    if ( 0 != err ) {
        /* POSIX says pthread_mutex_lock() should only fail if the
        thread holding the lock has died.  Best for gppsd to just die
        because things are FUBAR. */

	(void) fprintf(stderr,"pthread_mutex_lock() failed: %s\n",
            strerror(errno));
	exit(EXIT_FAILURE);
    }
}

void gpsd_release_reporting_lock(void)
{
    int err;
    err = pthread_mutex_unlock(&report_mutex);
    if ( 0 != err ) {
        /* POSIX says pthread_mutex_unlock() should only fail when
        trying to unlock a lock that does not exist, or is not owned by
        this thread.  This should never happen, so best for gpsd to die
        because things are FUBAR. */

	(void) fprintf(stderr,"pthread_mutex_unlock() failed: %s\n",
            strerror(errno));
	exit(EXIT_FAILURE);
    }
}
#endif /* PPS_ENABLE */

#ifndef SQUELCH_ENABLE
static void visibilize(char *outbuf, size_t outlen,
		       const char *inbuf, size_t inlen)
{
    const char *sp;

    outbuf[0] = '\0';
    for (sp = inbuf; sp < inbuf + inlen && strlen(outbuf)+6 < outlen; sp++)
	if (isprint((unsigned char) *sp) || (sp[0] == '\n' && sp[1] == '\0')
	  || (sp[0] == '\r' && sp[2] == '\0'))
	    (void)snprintf(outbuf + strlen(outbuf), 2, "%c", *sp);
	else
	    (void)snprintf(outbuf + strlen(outbuf), 6, "\\x%02x",
			   0x00ff & (unsigned)*sp);
}
#endif /* !SQUELCH_ENABLE */


void gpsd_vlog(const struct gpsd_errout_t *errout,
			 const int errlevel,
			 char *outbuf, size_t outlen,
			 const char *fmt, va_list ap)
/* assemble msg in vprintf(3) style, use errout hook or syslog for delivery */
{
#ifdef SQUELCH_ENABLE
    (void)errout;
    (void)errlevel;
    (void)fmt;
#else
    if (errout->debug >= errlevel) {
	char buf[BUFSIZ];
	char *err_str;

#if defined(PPS_ENABLE)
	gpsd_acquire_reporting_lock();
#endif /* PPS_ENABLE */
	switch ( errlevel ) {
	case LOG_ERROR:
		err_str = "ERROR: ";
		break;
	case LOG_SHOUT:
		err_str = "SHOUT: ";
		break;
	case LOG_WARN:
		err_str = "WARN: ";
		break;
	case LOG_CLIENT:
		err_str = "CLIENT: ";
		break;
	case LOG_INF:
		err_str = "INFO: ";
		break;
	case LOG_DATA:
		err_str = "DATA: ";
		break;
	case LOG_PROG:
		err_str = "PROG: ";
		break;
	case LOG_IO:
		err_str = "IO: ";
		break;
	case LOG_SPIN:
		err_str = "SPIN: ";
		break;
	case LOG_RAW:
		err_str = "RAW: ";
		break;
	default:
		err_str = "UNK: ";
	}

	assert(errout->label != NULL);
	(void)strlcpy(buf, errout->label, sizeof(buf));
	(void)strlcat(buf, ":", sizeof(buf));
	(void)strlcat(buf, err_str, sizeof(buf));
	str_vappendf(buf, sizeof(buf), fmt, ap);

	visibilize(outbuf, outlen, buf, strlen(buf));

	if (getpid() == getsid(getpid()))
	    syslog((errlevel <= LOG_SHOUT) ? LOG_ERR : LOG_NOTICE, "%s", outbuf);
	else if (errout->report != NULL)
	    errout->report(outbuf);
	else
	    (void)fputs(outbuf, stderr);
#if defined(PPS_ENABLE)
	gpsd_release_reporting_lock();
#endif /* PPS_ENABLE */
    }
#endif /* !SQUELCH_ENABLE */
}

void gpsd_log(const struct gpsd_errout_t *errout,
		 const int errlevel,
		 const char *fmt, ...)
/* assemble msg in printf(3) style, use errout hook or syslog for delivery */
{
    char buf[BUFSIZ];
    va_list ap;

    buf[0] = '\0';
    va_start(ap, fmt);
    gpsd_vlog(errout, errlevel, buf, sizeof(buf), fmt, ap);
    va_end(ap);
}

const char *gpsd_prettydump(struct gps_device_t *session)
/* dump the current packet in a form optimised for eyeballs */
{
    return gpsd_packetdump(session->msgbuf, sizeof(session->msgbuf),
			   (char *)session->lexer.outbuffer,
			   session->lexer.outbuflen);
}



/* Define the possible hook strings here so we can get the length */
#define HOOK_ACTIVATE "ACTIVATE"
#define HOOK_DEACTIVATE "DEACTIVATE"

#define HOOK_CMD_MAX (sizeof(DEVICEHOOKPATH) + GPS_PATH_MAX \
                      + sizeof(HOOK_DEACTIVATE))

static void gpsd_run_device_hook(struct gpsd_errout_t *errout,
				 char *device_name, char *hook)
{
    struct stat statbuf;

    if (stat(DEVICEHOOKPATH, &statbuf) == -1)
	gpsd_log(errout, LOG_PROG,
		 "no %s present, skipped running %s hook\n",
		 DEVICEHOOKPATH, hook);
    else {
	int status;
	char buf[HOOK_CMD_MAX];
	(void)snprintf(buf, sizeof(buf), "%s %s %s",
		       DEVICEHOOKPATH, device_name, hook);
	gpsd_log(errout, LOG_INF, "running %s\n", buf);
	status = system(buf);
	if (status == -1)
	    gpsd_log(errout, LOG_ERROR, "error running %s\n", buf);
	else
	    gpsd_log(errout, LOG_INF,
		     "%s returned %d\n", DEVICEHOOKPATH,
		     WEXITSTATUS(status));
    }
}

int gpsd_switch_driver(struct gps_device_t *session, char *type_name)
{
    const struct gps_type_t **dp;
    bool first_sync = (session->device_type != NULL);
    unsigned int i;

    if (first_sync && strcmp(session->device_type->type_name, type_name) == 0)
	return 0;

    gpsd_log(&session->context->errout, LOG_PROG,
	     "switch_driver(%s) called...\n", type_name);
    for (dp = gpsd_drivers, i = 0; *dp; dp++, i++)
	if (strcmp((*dp)->type_name, type_name) == 0) {
	    gpsd_log(&session->context->errout, LOG_PROG,
		     "selecting %s driver...\n",
		     (*dp)->type_name);
	    gpsd_assert_sync(session);
	    session->device_type = *dp;
	    session->driver_index = i;
#ifdef RECONFIGURE_ENABLE
	    session->gpsdata.dev.mincycle = session->device_type->min_cycle;
#endif /* RECONFIGURE_ENABLE */
	    /* reconfiguration might be required */
	    if (first_sync && session->device_type->event_hook != NULL)
		session->device_type->event_hook(session,
						 event_driver_switch);
#ifdef RECONFIGURE_ENABLE
	    if (STICKY(*dp))
		session->last_controller = *dp;
#endif /* RECONFIGURE_ENABLE */
	    return 1;
	}
    gpsd_log(&session->context->errout, LOG_ERROR,
	     "invalid GPS type \"%s\".\n", type_name);
    return 0;
}

void gps_context_init(struct gps_context_t *context,
		      const char *label)
{
    (void)memset(context, '\0', sizeof(struct gps_context_t));
    //context.readonly = false;
#ifdef TIMEHINT_ENABLE
    context->leap_notify    = LEAP_NOWARNING;
#endif /* TIMEHINT_ENABLE */
    context->serial_write = gpsd_serial_write;

    errout_reset(&context->errout);
    context->errout.label = (char *)label;

#if defined(PPS_ENABLE)
    (void)pthread_mutex_init(&report_mutex, NULL);
#endif /* defined(PPS_ENABLE) */
}

void gpsd_init(struct gps_device_t *session, struct gps_context_t *context,
	       const char *device)
/* initialize GPS polling */
{
    if (device != NULL)
	(void)strlcpy(session->gpsdata.dev.path, device,
		      sizeof(session->gpsdata.dev.path));
    session->device_type = NULL;	/* start by hunting packets */
#ifdef RECONFIGURE_ENABLE
    session->last_controller = NULL;
#endif /* RECONFIGURE_ENABLE */
    session->observed = 0;
    session->sourcetype = source_unknown;	/* gpsd_open() sets this */
    session->servicetype = service_unknown;	/* gpsd_open() sets this */
    session->context = context;
    memset( session->subtype, 0, sizeof( session->subtype));
    gps_clear_fix(&session->gpsdata.fix);
    gps_clear_fix(&session->newdata);
    gps_clear_fix(&session->oldfix);
    session->gpsdata.set = 0;
    gps_clear_att(&session->gpsdata.attitude);
    gps_clear_dop(&session->gpsdata.dop);
    session->gpsdata.epe = NAN;
    session->mag_var = NAN;
    session->gpsdata.dev.cycle = session->gpsdata.dev.mincycle = 1;
#ifdef TIMING_ENABLE
    session->sor = 0.0;
    session->chars = 0;
#endif /* TIMING_ENABLE */
    /* tty-level initialization */
    gpsd_tty_init(session);
    /* necessary in case we start reading in the middle of a GPGSV sequence */
    gpsd_zero_satellites(&session->gpsdata);

    /* initialize things for the packet parser */
    packet_reset(&session->lexer);
}

void gpsd_deactivate(struct gps_device_t *session)
/* temporarily release the GPS device */
{
#ifdef RECONFIGURE_ENABLE
    if (!session->context->readonly
	&& session->device_type != NULL
	&& session->device_type->event_hook != NULL) {
	session->device_type->event_hook(session, event_deactivate);
    }
    if (session->device_type != NULL) {
	if (session->back_to_nmea
	    && session->device_type->mode_switcher != NULL)
	    session->device_type->mode_switcher(session, 0);
    }
#endif /* RECONFIGURE_ENABLE */
    gpsd_log(&session->context->errout, LOG_INF,
	     "closing GPS=%s (%d)\n",
	     session->gpsdata.dev.path, session->gpsdata.gps_fd);
#if defined(NMEA2000_ENABLE)
    if (session->sourcetype == source_can)
        (void)nmea2000_close(session);
    else
#endif /* of defined(NMEA2000_ENABLE) */
        (void)gpsd_close(session);
    if (session->mode == O_OPTIMIZE)
	gpsd_run_device_hook(&session->context->errout,
			     session->gpsdata.dev.path,
			     HOOK_DEACTIVATE);
#ifdef PPS_ENABLE
    session->pps_thread.report_hook = NULL; /* tell any PPS-watcher thread to die */
#endif /* PPS_ENABLE */
    /* mark it inactivated */
    session->gpsdata.online = (timestamp_t)0;
}

#ifdef PPS_ENABLE
static void ppsthread_log(volatile struct pps_thread_t *pps_thread,
			  int loglevel, const char *fmt, ...)
/* shim function to decouple PPS monitor code from the session structure */
{
    struct gps_device_t *device = (struct gps_device_t *)pps_thread->context;
    char buf[BUFSIZ];
    va_list ap;

    switch (loglevel) {
    case THREAD_ERROR:
	loglevel = LOG_ERROR;
	break;
    case THREAD_WARN:
	loglevel = LOG_WARN;
	break;
    case THREAD_INF:
	loglevel = LOG_INF;
	break;
    case THREAD_PROG:
	loglevel = LOG_PROG;
	break;
    case THREAD_RAW:
	loglevel = LOG_RAW;
	break;
    }

    buf[0] = '\0';
    va_start(ap, fmt);
    gpsd_vlog(&device->context->errout, loglevel, buf, sizeof(buf), fmt, ap);
    va_end(ap);
}
#endif /* PPS_ENABLE */


void gpsd_clear(struct gps_device_t *session)
/* device has been opened - clear its storage for use */
{
    session->gpsdata.online = timestamp();
    lexer_init(&session->lexer);
    session->lexer.errout = session->context->errout;
    // session->gpsdata.online = 0;
    gps_clear_att(&session->gpsdata.attitude);
    gps_clear_dop(&session->gpsdata.dop);
    gps_clear_fix(&session->gpsdata.fix);
    session->gpsdata.status = STATUS_NO_FIX;
    session->gpsdata.separation = NAN;
    session->mag_var = NAN;
    session->releasetime = (time_t)0;
    session->badcount = 0;

    /* clear the private data union */
    memset( (void *)&session->driver, '\0', sizeof(session->driver));
#ifdef PPS_ENABLE
    /* set up the context structure for the PPS thread monitor */
    memset((void *)&session->pps_thread, 0, sizeof(session->pps_thread));
    session->pps_thread.devicefd = session->gpsdata.gps_fd;
    session->pps_thread.devicename = session->gpsdata.dev.path;
    session->pps_thread.log_hook = ppsthread_log;
    session->pps_thread.context = (void *)session;
#endif /* PPS_ENABLE */

    session->opentime = time(NULL);
}

int gpsd_open(struct gps_device_t *session)
/* open a device for access to its data *
 * return: the opened file descriptor
 *         PLACEHOLDING_FD - for /dev/ppsX
 *         UNALLOCATED_FD - for open failure
 *         -1 - for open failure
 */
{
#ifdef NETFEED_ENABLE
    /* special case: source may be a URI to a remote GNSS or DGPS service */
    if (netgnss_uri_check(session->gpsdata.dev.path)) {
	session->gpsdata.gps_fd = netgnss_uri_open(session,
						   session->gpsdata.dev.path);
	session->sourcetype = source_tcp;
	gpsd_log(&session->context->errout, LOG_SPIN,
		 "netgnss_uri_open(%s) returns socket on fd %d\n",
		 session->gpsdata.dev.path, session->gpsdata.gps_fd);
	return session->gpsdata.gps_fd;
    /* otherwise, could be an TCP data feed */
    } else if (str_starts_with(session->gpsdata.dev.path, "tcp://")) {
	char server[strlen(session->gpsdata.dev.path)+1], *port;
	socket_t dsock;
	(void)strlcpy(server, session->gpsdata.dev.path + 6, sizeof(server));
	INVALIDATE_SOCKET(session->gpsdata.gps_fd);
	port = strchr(server, ':');
	if (port == NULL) {
	    gpsd_log(&session->context->errout, LOG_ERROR,
		     "Missing colon in TCP feed spec.\n");
	    return -1;
	}
	*port++ = '\0';
	gpsd_log(&session->context->errout, LOG_INF,
		 "opening TCP feed at %s, port %s.\n", server,
		 port);
	if ((dsock = netlib_connectsock(AF_UNSPEC, server, port, "tcp")) < 0) {
	    gpsd_log(&session->context->errout, LOG_ERROR,
		     "TCP device open error %s.\n",
		     netlib_errstr(dsock));
	    return -1;
	} else
	    gpsd_log(&session->context->errout, LOG_SPIN,
		     "TCP device opened on fd %d\n", dsock);
	session->gpsdata.gps_fd = dsock;
	session->sourcetype = source_tcp;
	return session->gpsdata.gps_fd;
    /* or could be UDP */
    } else if (str_starts_with(session->gpsdata.dev.path, "udp://")) {
	char server[strlen(session->gpsdata.dev.path)+1], *port;
	socket_t dsock;
	(void)strlcpy(server, session->gpsdata.dev.path + 6, sizeof(server));
	INVALIDATE_SOCKET(session->gpsdata.gps_fd);
	port = strchr(server, ':');
	if (port == NULL) {
	    gpsd_log(&session->context->errout, LOG_ERROR,
		     "Missing colon in UDP feed spec.\n");
	    return -1;
	}
	*port++ = '\0';
	gpsd_log(&session->context->errout, LOG_INF,
		 "opening UDP feed at %s, port %s.\n", server,
		 port);
	if ((dsock = netlib_connectsock(AF_UNSPEC, server, port, "udp")) < 0) {
	    gpsd_log(&session->context->errout, LOG_ERROR,
		     "UDP device open error %s.\n",
		     netlib_errstr(dsock));
	    return -1;
	} else
	    gpsd_log(&session->context->errout, LOG_SPIN,
		     "UDP device opened on fd %d\n", dsock);
	session->gpsdata.gps_fd = dsock;
	session->sourcetype = source_udp;
	return session->gpsdata.gps_fd;
    }
#endif /* NETFEED_ENABLE */
#ifdef PASSTHROUGH_ENABLE
    if (str_starts_with(session->gpsdata.dev.path, "gpsd://")) {
	char server[strlen(session->gpsdata.dev.path)+1], *port;
	socket_t dsock;
	(void)strlcpy(server, session->gpsdata.dev.path + 7, sizeof(server));
	INVALIDATE_SOCKET(session->gpsdata.gps_fd);
	if ((port = strchr(server, ':')) == NULL) {
	    port = DEFAULT_GPSD_PORT;
	} else
	    *port++ = '\0';
	gpsd_log(&session->context->errout, LOG_INF,
		 "opening remote gpsd feed at %s, port %s.\n",
		 server, port);
	if ((dsock = netlib_connectsock(AF_UNSPEC, server, port, "tcp")) < 0) {
	    gpsd_log(&session->context->errout, LOG_ERROR,
		     "remote gpsd device open error %s.\n",
		     netlib_errstr(dsock));
	    return -1;
	} else
	    gpsd_log(&session->context->errout, LOG_SPIN,
		     "remote gpsd feed opened on fd %d\n", dsock);
	/* watch to remote is issued when WATCH is */
	session->gpsdata.gps_fd = dsock;
	session->sourcetype = source_gpsd;
	return session->gpsdata.gps_fd;
    }
#endif /* PASSTHROUGH_ENABLE */
#if defined(NMEA2000_ENABLE)
    if (str_starts_with(session->gpsdata.dev.path, "nmea2000://")) {
        return nmea2000_open(session);
    }
#endif /* defined(NMEA2000_ENABLE) */
    /* fall through to plain serial open */
    /* could be a naked /dev/ppsX */
    return gpsd_serial_open(session);
}

int gpsd_activate(struct gps_device_t *session, const int mode)
/* acquire a connection to the GPS device */
{
    if (mode == O_OPTIMIZE)
	gpsd_run_device_hook(&session->context->errout,
			     session->gpsdata.dev.path, HOOK_ACTIVATE);
    session->gpsdata.gps_fd = gpsd_open(session);
    if (mode != O_CONTINUE)
	session->mode = mode;

    // cppcheck-suppress pointerLessThanZero
    if (session->gpsdata.gps_fd < 0) {
        /* return could be -1, PLACEHOLDING_FD, of UNALLOCATED_FD */
        if ( PLACEHOLDING_FD == session->gpsdata.gps_fd ) {
            /* it is /dev/ppsX, need to set devicename, etc. */
	    gpsd_clear(session);
        }
	return session->gpsdata.gps_fd;
    }

#ifdef NON_NMEA0183_ENABLE
    /* if it's a sensor, it must be probed */
    if ((session->servicetype == service_sensor) &&
	(session->sourcetype != source_can)) {
	const struct gps_type_t **dp;

	for (dp = gpsd_drivers; *dp; dp++) {
	    if ((*dp)->probe_detect != NULL) {
		gpsd_log(&session->context->errout, LOG_PROG,
			 "Probing \"%s\" driver...\n",
			 (*dp)->type_name);
		/* toss stale data */
		(void)tcflush(session->gpsdata.gps_fd, TCIOFLUSH);
		if ((*dp)->probe_detect(session) != 0) {
		    gpsd_log(&session->context->errout, LOG_PROG,
			     "Probe found \"%s\" driver...\n",
			     (*dp)->type_name);
		    session->device_type = *dp;
		    gpsd_assert_sync(session);
		    goto foundit;
		} else
		    gpsd_log(&session->context->errout, LOG_PROG,
			     "Probe not found \"%s\" driver...\n",
			     (*dp)->type_name);
	    }
	}
	gpsd_log(&session->context->errout, LOG_PROG,
		 "no probe matched...\n");
    }
foundit:
#endif /* NON_NMEA0183_ENABLE */

    gpsd_clear(session);
    gpsd_log(&session->context->errout, LOG_INF,
	     "gpsd_activate(%d): activated GPS (fd %d)\n",
	     session->mode, session->gpsdata.gps_fd);
    /*
     * We might know the device's type, but we shouldn't assume it has
     * retained its settings.  A revert hook might well have undone
     * them on the previous close.  Fire a reactivate event so drivers
     * can do something about this if they choose.
     */
    if (session->device_type != NULL
	&& session->device_type->event_hook != NULL)
	session->device_type->event_hook(session, event_reactivate);
    return session->gpsdata.gps_fd;
}


#ifndef NOFLOATS_ENABLE
/*****************************************************************************

Carl Carter of SiRF supplied this algorithm for computing DOPs from
a list of visible satellites (some typos corrected)...

For satellite n, let az(n) = azimuth angle from North and el(n) be elevation.
Let:

    a(k, 1) = sin az(k) * cos el(k)
    a(k, 2) = cos az(k) * cos el(k)
    a(k, 3) = sin el(k)

Then form the line-of-sight matrix A for satellites used in the solution:

    | a(1,1) a(1,2) a(1,3) 1 |
    | a(2,1) a(2,2) a(2,3) 1 |
    |   :       :      :   : |
    | a(n,1) a(n,2) a(n,3) 1 |

And its transpose A~:

    |a(1, 1) a(2, 1) .  .  .  a(n, 1) |
    |a(1, 2) a(2, 2) .  .  .  a(n, 2) |
    |a(1, 3) a(2, 3) .  .  .  a(n, 3) |
    |    1       1   .  .  .     1    |

Compute the covariance matrix (A~*A)^-1, which is guaranteed symmetric:

    | s(x)^2    s(x)*s(y)  s(x)*s(z)  s(x)*s(t) |
    | s(y)*s(x) s(y)^2     s(y)*s(z)  s(y)*s(t) |
    | s(z)*s(x) s(z)*s(y)  s(z)^2     s(z)*s(t) |
    | s(t)*s(x) s(t)*s(y)  s(t)*s(z)  s(t)^2    |

Then:

GDOP = sqrt(s(x)^2 + s(y)^2 + s(z)^2 + s(t)^2)
TDOP = sqrt(s(t)^2)
PDOP = sqrt(s(x)^2 + s(y)^2 + s(z)^2)
HDOP = sqrt(s(x)^2 + s(y)^2)
VDOP = sqrt(s(z)^2)

Here's how we implement it...

First, each compute element P(i,j) of the 4x4 product A~*A.
If S(k=1,k=n): f(...) is the sum of f(...) as k varies from 1 to n, then
applying the definition of matrix product tells us:

P(i,j) = S(k=1,k=n): B(i, k) * A(k, j)

But because B is the transpose of A, this reduces to

P(i,j) = S(k=1,k=n): A(k, i) * A(k, j)

This is not, however, the entire algorithm that SiRF uses.  Carl writes:

> As you note, with rounding accounted for, most values agree exactly, and
> those that don't agree our number is higher.  That is because we
> deweight some satellites and account for that in the DOP calculation.
> If a satellite is not used in a solution at the same weight as others,
> it should not contribute to DOP calculation at the same weight.  So our
> internal algorithm does a compensation for that which you would have no
> way to duplicate on the outside since we don't output the weighting
> factors.  In fact those are not even available to API users.

Queried about the deweighting, Carl says:

> In the SiRF tracking engine, each satellite track is assigned a quality
> value based on the tracker's estimate of that signal.  It includes C/No
> estimate, ability to hold onto the phase, stability of the I vs. Q phase
> angle, etc.  The navigation algorithm then ranks all the tracks into
> quality order and selects which ones to use in the solution and what
> weight to give those used in the solution.  The process is actually a
> bit of a "trial and error" method -- we initially use all available
> tracks in the solution, then we sequentially remove the lowest quality
> ones until the solution stabilizes.  The weighting is inherent in the
> Kalman filter algorithm.  Once the solution is stable, the DOP is
> computed from those SVs used, and there is an algorithm that looks at
> the quality ratings and determines if we need to deweight any.
> Likewise, if we use altitude hold mode for a 3-SV solution, we deweight
> the phantom satellite at the center of the Earth.

So we cannot exactly duplicate what SiRF does internally.  We'll leave
HDOP alone and use our computed values for VDOP and PDOP.  Note, this
may have to change in the future if this code is used by a non-SiRF
driver.

******************************************************************************/


static gps_mask_t fill_dop(const struct gpsd_errout_t *errout,
			   const struct gps_data_t * gpsdata,
			   struct dop_t * dop)
{
    double prod[4][4];
    double inv[4][4];
    double satpos[MAXCHANNELS][4];
    double xdop, ydop, hdop, vdop, pdop, tdop, gdop;
    int i, j, k, n;

    memset(satpos, 0, sizeof(satpos));

    for (n = k = 0; k < gpsdata->satellites_visible; k++) {
        if (!gpsdata->skyview[k].used) {
             /* skip unused sats */
             continue;
        }
        if (1 > gpsdata->skyview[k].PRN) {
             /* skip bad PRN */
             continue;
        }
        if (0 > gpsdata->skyview[k].azimuth ||
             359 < gpsdata->skyview[k].azimuth) {
             /* skip bad azimuth */
             continue;
        }
        if (-90 > gpsdata->skyview[k].elevation ||
             90 < gpsdata->skyview[k].elevation) {
             /* skip bad elevation */
             continue;
        }
        const struct satellite_t *sp = &gpsdata->skyview[k];
        satpos[n][0] = sin(sp->azimuth * DEG_2_RAD)
            * cos(sp->elevation * DEG_2_RAD);
        satpos[n][1] = cos(sp->azimuth * DEG_2_RAD)
            * cos(sp->elevation * DEG_2_RAD);
        satpos[n][2] = sin(sp->elevation * DEG_2_RAD);
        satpos[n][3] = 1;
        gpsd_log(errout, LOG_INF, "PRN=%3d az=%3d el=%2d (%f, %f, %f)\n",
                 gpsdata->skyview[k].PRN,
                 gpsdata->skyview[k].azimuth,
                 gpsdata->skyview[k].elevation,
                 satpos[n][0], satpos[n][1], satpos[n][2]);
        n++;
    }
    /* can't use gpsdata->satellites_used as that is a counter for xxGSA,
     * and gets cleared at odd times */
    gpsd_log(errout, LOG_INF, "Sats used (%d):\n", n);

    /* If we don't have 4 satellites then we don't have enough information to calculate DOPS */
    if (n < 4) {
#ifdef __UNUSED__
	gpsd_log(errout, LOG_DATA + 2,
		 "Not enough satellites available %d < 4:\n",
		 n);
#endif /* __UNUSED__ */
	return 0;		/* Is this correct return code here? or should it be ERROR_SET */
    }

    memset(prod, 0, sizeof(prod));
    memset(inv, 0, sizeof(inv));

#ifdef __UNUSED__
    gpsd_log(errout, LOG_INF, "Line-of-sight matrix:\n");
    for (k = 0; k < n; k++) {
	gpsd_log(errout, LOG_INF, "%f %f %f %f\n",
		 satpos[k][0], satpos[k][1], satpos[k][2], satpos[k][3]);
    }
#endif /* __UNUSED__ */

    for (i = 0; i < 4; ++i) {	//< rows
	for (j = 0; j < 4; ++j) {	//< cols
	    prod[i][j] = 0.0;
	    for (k = 0; k < n; ++k) {
		prod[i][j] += satpos[k][i] * satpos[k][j];
	    }
	}
    }

#ifdef __UNUSED__
    gpsd_log(errout, LOG_INF, "product:\n");
    for (k = 0; k < 4; k++) {
	gpsd_log(errout, LOG_INF, "%f %f %f %f\n",
		 prod[k][0], prod[k][1], prod[k][2], prod[k][3]);
    }
#endif /* __UNUSED__ */

    if (matrix_invert(prod, inv)) {
#ifdef __UNUSED__
	/*
	 * Note: this will print garbage unless all the subdeterminants
	 * are computed in the invert() function.
	 */
	gpsd_log(errout, LOG_RAW, "inverse:\n");
	for (k = 0; k < 4; k++) {
	    gpsd_log(errout, LOG_RAW,
		     "%f %f %f %f\n",
		     inv[k][0], inv[k][1], inv[k][2], inv[k][3]);
	}
#endif /* __UNUSED__ */
    } else {
#ifndef USE_QT
	gpsd_log(errout, LOG_DATA,
		 "LOS matrix is singular, can't calculate DOPs - source '%s'\n",
		 gpsdata->dev.path);
#endif
	return 0;
    }

    xdop = sqrt(inv[0][0]);
    ydop = sqrt(inv[1][1]);
    hdop = sqrt(inv[0][0] + inv[1][1]);
    vdop = sqrt(inv[2][2]);
    pdop = sqrt(inv[0][0] + inv[1][1] + inv[2][2]);
    tdop = sqrt(inv[3][3]);
    gdop = sqrt(inv[0][0] + inv[1][1] + inv[2][2] + inv[3][3]);

#ifndef USE_QT
    gpsd_log(errout, LOG_DATA,
	     "DOPS computed/reported: X=%f/%f, Y=%f/%f, H=%f/%f, V=%f/%f, "
	     "P=%f/%f, T=%f/%f, G=%f/%f\n",
	     xdop, dop->xdop, ydop, dop->ydop, hdop, dop->hdop, vdop,
	     dop->vdop, pdop, dop->pdop, tdop, dop->tdop, gdop, dop->gdop);
#endif

    /* Check to see which DOPs we already have.  Save values if no value
     * from the GPS.  Do not overwrite values which came from the GPS */
    if (isfinite(dop->xdop) == 0) {
	dop->xdop = xdop;
    }
    if (isfinite(dop->ydop) == 0) {
	dop->ydop = ydop;
    }
    if (isfinite(dop->hdop) == 0) {
	dop->hdop = hdop;
    }
    if (isfinite(dop->vdop) == 0) {
	dop->vdop = vdop;
    }
    if (isfinite(dop->pdop) == 0) {
	dop->pdop = pdop;
    }
    if (isfinite(dop->tdop) == 0) {
	dop->tdop = tdop;
    }
    if (isfinite(dop->gdop) == 0) {
	dop->gdop = gdop;
    }

    return DOP_SET;
}

static void gpsd_error_model(struct gps_device_t *session,
			     struct gps_fix_t *fix, struct gps_fix_t *oldfix)
/* compute errors and derived quantities */
{
    /*
     * Now we compute derived quantities.  This is where the tricky error-
     * modeling stuff goes. Presently we don't know how to derive
     * time error.
     *
     * Some drivers set the position-error fields.  Only the Zodiacs
     * report speed error.  Nobody reports track error or climb error.
     *
     * The UERE constants are our assumption about the base error of
     * GPS fixes in different directions.
     */
#define H_UERE_NO_DGPS		15.0	/* meters, 95% confidence */
#define H_UERE_WITH_DGPS	3.75	/* meters, 95% confidence */
#define V_UERE_NO_DGPS		23.0	/* meters, 95% confidence */
#define V_UERE_WITH_DGPS	5.75	/* meters, 95% confidence */
#define P_UERE_NO_DGPS		19.0	/* meters, 95% confidence */
#define P_UERE_WITH_DGPS	4.75	/* meters, 95% confidence */
    double h_uere, v_uere, p_uere;

    if (NULL == session)
	return;

    h_uere =
	(session->gpsdata.status ==
	 STATUS_DGPS_FIX ? H_UERE_WITH_DGPS : H_UERE_NO_DGPS);
    v_uere =
	(session->gpsdata.status ==
	 STATUS_DGPS_FIX ? V_UERE_WITH_DGPS : V_UERE_NO_DGPS);
    p_uere =
	(session->gpsdata.status ==
	 STATUS_DGPS_FIX ? P_UERE_WITH_DGPS : P_UERE_NO_DGPS);

    /* sanity check the speed, 10,000 m/s should be a nice max */
    if ( 9999.9 < fix->speed )
	fix->speed = NAN;
    else if ( -9999.9 > fix->speed )
	fix->speed = NAN;

    /* sanity check the climb, 10,000 m/s should be a nice max */
    if ( 9999.9 < fix->climb )
	fix->climb = NAN;
    else if ( -9999.9 > fix->climb )
	fix->climb = NAN;


    /*
     * OK, this is not an error computation, but we're at the right
     * place in the architecture for it.  Compute speed over ground
     * and climb/sink in the simplest possible way.
     */
    if (fix->mode >= MODE_2D && oldfix->mode >= MODE_2D
	&& isfinite(fix->speed) == 0) {
	if (fix->time == oldfix->time)
	    fix->speed = 0;
	else
	    fix->speed =
		earth_distance(fix->latitude, fix->longitude,
			       oldfix->latitude, oldfix->longitude)
		/ (fix->time - oldfix->time);
    }
    if (fix->mode >= MODE_3D && oldfix->mode >= MODE_3D
	&& isfinite(fix->climb) == 0) {
	if (fix->time == oldfix->time)
	    fix->climb = 0;
	else if ((isfinite(fix->altitude) != 0 &&
	         isfinite(oldfix->altitude) != 0)) {
	    fix->climb = ((fix->altitude - oldfix->altitude) /
	                  (fix->time - oldfix->time));
	}
    }

    /*
     * Field reports match the theoretical prediction that
     * expected time error should be half the resolution of
     * the GPS clock, so we put the bound of the error
     * in as a constant pending getting it from each driver.
     *
     * In an ideal world, we'd increase this if no leap-second has
     * been seen and it's less than 750s (one almanac load cycle) from
     * device powerup. Alas, we have no way to know when device
     * powerup occurred - depending on the receiver design it could be
     * when the hardware was first powered up or when it was first
     * opened.  Also, some devices (notably plain NMEA0183 receivers)
     * never ship an indication of when they have valid leap second.
     */
    if (isfinite(fix->time) != 0 && isfinite(fix->ept) == 0)
	fix->ept = 0.005;
    /* Other error computations depend on having a valid fix */
    if (fix->mode >= MODE_2D) {
	if (isfinite(fix->epx) == 0 && isfinite(session->gpsdata.dop.hdop) != 0)
	    fix->epx = session->gpsdata.dop.xdop * h_uere;

	if (isfinite(fix->epy) == 0 && isfinite(session->gpsdata.dop.hdop) != 0)
	    fix->epy = session->gpsdata.dop.ydop * h_uere;

	if ((fix->mode >= MODE_3D)
	    && isfinite(fix->epv) == 0
	    && isfinite(session->gpsdata.dop.vdop) != 0)
	    fix->epv = session->gpsdata.dop.vdop * v_uere;

	if (isfinite(session->gpsdata.epe) == 0
	    && isfinite(session->gpsdata.dop.pdop) != 0)
	    session->gpsdata.epe = session->gpsdata.dop.pdop * p_uere;
	else
	    session->gpsdata.epe = NAN;

	/*
	 * If we have a current fix and an old fix, and the packet handler
	 * didn't set the speed error and climb error members itself,
	 * try to compute them now.
	 */
	if (isfinite(fix->eps) == 0) {
	    if (oldfix->mode > MODE_NO_FIX && fix->mode > MODE_NO_FIX
		&& isfinite(oldfix->epx) != 0 && isfinite(oldfix->epy) != 0
		&& isfinite(oldfix->time) != 0 && isfinite(fix->time) != 0
		&& fix->time > oldfix->time) {
		timestamp_t t = fix->time - oldfix->time;
		double e =
		    EMIX(oldfix->epx, oldfix->epy) + EMIX(fix->epx, fix->epy);
		fix->eps = e / t;
	    } else
		fix->eps = NAN;
	}
	if ((fix->mode >= MODE_3D)
	    && isfinite(fix->epc) == 0 && fix->time > oldfix->time) {
	    if (oldfix->mode >= MODE_3D && fix->mode >= MODE_3D) {
		timestamp_t t = fix->time - oldfix->time;
		double e = oldfix->epv + fix->epv;
		/* if vertical uncertainties are zero this will be too */
		fix->epc = e / t;
	    }
	    /*
	     * We compute a track error estimate solely from the
	     * position of this fix and the last one.  The maximum
	     * track error, as seen from the position of last fix, is
	     * the angle subtended by the two most extreme possible
	     * error positions of the current fix; the expected track
	     * error is half that.  Let the position of the old fix be
	     * A and of the new fix B.  We model the view from A as
	     * two right triangles ABC and ABD with BC and BD both
	     * having the length of the new fix's estimated error.
	     * adj = len(AB), opp = len(BC) = len(BD), hyp = len(AC) =
	     * len(AD). This leads to spurious uncertainties
	     * near 180 when we're moving slowly; to avoid reporting
	     * garbage, throw back NaN if the distance from the previous
	     * fix is less than the error estimate.
	     */
	    fix->epd = NAN;
	    if (oldfix->mode >= MODE_2D) {
		double adj =
		    earth_distance(oldfix->latitude, oldfix->longitude,
				   fix->latitude, fix->longitude);
		if (isfinite(adj) != 0 && adj > EMIX(fix->epx, fix->epy)) {
		    double opp = EMIX(fix->epx, fix->epy);
		    double hyp = sqrt(adj * adj + opp * opp);
		    fix->epd = RAD_2_DEG * 2 * asin(opp / hyp);
		}
	    }
	}
    }

    /* save old fix for later error computations */
    if (fix->mode >= MODE_2D)
	*oldfix = *fix;
}
#endif /* NOFLOATS_ENABLE */

int gpsd_await_data(fd_set *rfds,
		    fd_set *efds,
		     const int maxfd,
		     fd_set *all_fds,
		     struct gpsd_errout_t *errout)
/* await data from any socket in the all_fds set */
{
    int status;

    FD_ZERO(efds);
    *rfds = *all_fds;
    gpsd_log(errout, LOG_RAW + 2, "select waits\n");
    /*
     * Poll for user commands or GPS data.  The timeout doesn't
     * actually matter here since select returns whenever one of
     * the file descriptors in the set goes ready.  The point
     * of tracking maxfd is to keep the set of descriptors that
     * select(2) has to poll here as small as possible (for
     * low-clock-rate SBCs and the like).
     *
     * pselect() is preferable to vanilla select, to eliminate
     * the once-per-second wakeup when no sensors are attached.
     * This cuts power consumption.
     */
    errno = 0;

    status = pselect(maxfd + 1, rfds, NULL, NULL, NULL, NULL);
    if (status == -1) {
	if (errno == EINTR)
	    return AWAIT_NOT_READY;
	else if (errno == EBADF) {
	    int fd;
	    for (fd = 0; fd < (int)FD_SETSIZE; fd++)
		/*
		 * All we care about here is a cheap, fast, uninterruptible
		 * way to check if a file descriptor is valid.
		 */
		if (FD_ISSET(fd, all_fds) && fcntl(fd, F_GETFL, 0) == -1) {
		    FD_CLR(fd, all_fds);
		    FD_SET(fd, efds);
		}
	    return AWAIT_NOT_READY;
	} else {
	    gpsd_log(errout, LOG_ERROR, "select: %s\n", strerror(errno));
	    return AWAIT_FAILED;
	}
    }

    if (errout->debug >= LOG_SPIN) {
	int i;
	char dbuf[BUFSIZ];
	dbuf[0] = '\0';
	for (i = 0; i < (int)FD_SETSIZE; i++)
	    if (FD_ISSET(i, all_fds))
		str_appendf(dbuf, sizeof(dbuf), "%d ", i);
	str_rstrip_char(dbuf, ' ');
	(void)strlcat(dbuf, "} -> {", sizeof(dbuf));
	for (i = 0; i < (int)FD_SETSIZE; i++)
	    if (FD_ISSET(i, rfds))
		str_appendf(dbuf, sizeof(dbuf), " %d ", i);
	gpsd_log(errout, LOG_SPIN,
		 "select() {%s} at %f (errno %d)\n",
		 dbuf, timestamp(), errno);
    }

    return AWAIT_GOT_INPUT;
}

static bool hunt_failure(struct gps_device_t *session)
/* after a bad packet, what should cue us to go to next autobaud setting? */
{
    /*
     * We have tried three different tests here.
     *
     * The first was session->badcount++>1.  This worked very well on
     * ttys for years and years, but caused failure to sync on TCP/IP
     * sources, which have I/O boundaries in mid-packet more often
     * than RS232 ones.  There's a test for this at
     * test/daemon/tcp-torture.log.
     *
     * The second was session->badcount++>1 && session->lexer.state==0.
     * Fail hunt only if we get a second consecutive bad packet
     * and the lexer is in ground state.  We don't want to fail on
     * a first bad packet because the source might have a burst of
     * leading garbage after open.  We don't want to fail if the
     * lexer is not in ground state, because that means the read
     * might have picked up a valid partial packet - better to go
     * back around the loop and pick up more data.
     *
     * The "&& session->lexer.state==0" guard causes an intermittent
     * hang while autobauding on SiRF IIIs (but not on SiRF-IIs, oddly
     * enough).  Removing this conjunct resurrected the failure
     * of test/daemon/tcp-torture.log.
     *
     * Our third attempt, isatty(session->gpsdata.gps_fd) != 0
     * && session->badcount++>1, reverts to the old test that worked
     * well on ttys for ttys and prevents non-tty devices from *ever*
     * having hunt failures. This has the cost that non-tty devices
     * will never get kicked off for presenting bad packets.
     *
     * This test may need further revision.
     */
    return isatty(session->gpsdata.gps_fd) != 0 && session->badcount++>1;
}

gps_mask_t gpsd_poll(struct gps_device_t *session)
/* update the stuff in the scoreboard structure */
{
    ssize_t newlen;
    bool driver_change = false;

    gps_clear_fix(&session->newdata);

#ifdef TIMING_ENABLE
    /*
     * Input just became available from a sensor, but no read from the
     * device has yet been done.
     *
     * What we actually do here is trickier.  For latency-timing
     * purposes, we want to know the time at the start of the current
     * recording cycle. We rely on the fact that even at 4800bps
     * there's a quiet time perceptible to the human eye in gpsmon
     * between when the last character of the last packet in a
     * 1-second cycle ships and when the next reporting cycle
     * ships. Because the cycle time is fixed, higher baud rates will
     * make this gap larger.
     *
     * Thus, we look for an inter-character delay much larger than an
     * average 4800bps sentence time.  How should this delay be set?  Well,
     * counting framing bits and erring on the side of caution, it's
     * about 480 characters per second or 2083 microeconds per character;
     * that's almost exactly 0.125 seconds per average 60-char sentence.
     * Doubling this to avoid false positives, we look for an inter-character
     * delay of greater than 0.250s.
     *
     * The above assumes a cycle time of 1 second.  To get the minimum size of
     * the quiet period, we multiply by the device cycle time.
     *
     * We can sanity-check these calculation by watching logs. If we have set
     * MINIMUM_QUIET_TIME correctly, the "transmission pause" message below
     * will consistently be emitted just before the sentence that shows up
     * as start-of-cycle in gpsmon, and never emitted at any other point
     * in the cycle.
     *
     * In practice, it seems that edge detection succeeds at 9600bps but
     * fails at 4800bps.  This is not surprising, as previous profiling has
     * indicated that at 4800bps some devices overrun a 1-second cycle time
     * with the data they transmit.
     */
#define MINIMUM_QUIET_TIME	0.25
    if (session->lexer.outbuflen == 0)
    {
	/* beginning of a new packet */
	timestamp_t now = timestamp();
	if (session->device_type != NULL && session->lexer.start_time > 0) {
#ifdef RECONFIGURE_ENABLE
	    const double min_cycle = session->device_type->min_cycle;
#else
	    const double min_cycle = 1;
#endif /* RECONFIGURE_ENABLE */
	    double quiet_time = (MINIMUM_QUIET_TIME * min_cycle);
	    double gap = now - session->lexer.start_time;

	    if (gap > min_cycle)
		gpsd_log(&session->context->errout, LOG_WARN,
			 "cycle-start detector failed.\n");
	    else if (gap > quiet_time) {
		gpsd_log(&session->context->errout, LOG_PROG,
			 "transmission pause of %f\n", gap);
		session->sor = now;
		session->lexer.start_char = session->lexer.char_counter;
	    }
	}
	session->lexer.start_time = now;
    }
#endif /* TIMING_ENABLE */

    if (session->lexer.type >= COMMENT_PACKET) {
	session->observed |= PACKET_TYPEMASK(session->lexer.type);
    }

    /* can we get a full packet from the device? */
    if (session->device_type != NULL) {
	newlen = session->device_type->get_packet(session);
	/* coverity[deref_ptr] */
	gpsd_log(&session->context->errout, LOG_RAW,
		 "%s is known to be %s\n",
		 session->gpsdata.dev.path,
		 session->device_type->type_name);
    } else {
	newlen = generic_get(session);
    }

    /* update the scoreboard structure from the GPS */
    gpsd_log(&session->context->errout, LOG_RAW + 2,
	     "%s sent %zd new characters\n",
	     session->gpsdata.dev.path, newlen);
    if (newlen < 0) {		/* read error */
	gpsd_log(&session->context->errout, LOG_INF,
		 "GPS on %s returned error %zd (%lf sec since data)\n",
		 session->gpsdata.dev.path, newlen,
		 timestamp() - session->gpsdata.online);
	session->gpsdata.online = (timestamp_t)0;
	return ERROR_SET;
    } else if (newlen == 0) {		/* zero length read, possible EOF */
	/*
	 * Multiplier is 2 to avoid edge effects due to sampling at the exact
	 * wrong time...
	 */
	if (session->gpsdata.online > 0 && timestamp() - session->gpsdata.online >= session->gpsdata.dev.cycle * 2) {
	    gpsd_log(&session->context->errout, LOG_INF,
		     "GPS on %s is offline (%lf sec since data)\n",
		     session->gpsdata.dev.path,
		     timestamp() - session->gpsdata.online);
	    session->gpsdata.online = (timestamp_t)0;
	}
	return NODATA_IS;
    } else /* (newlen > 0) */ {
	gpsd_log(&session->context->errout, LOG_RAW,
		 "packet sniff on %s finds type %d\n",
		 session->gpsdata.dev.path, session->lexer.type);
	if (session->lexer.type == COMMENT_PACKET) {
	    if (strcmp((const char *)session->lexer.outbuffer, "# EOF\n") == 0) {
		gpsd_log(&session->context->errout, LOG_PROG,
			 "synthetic EOF\n");
		return EOF_IS;
	    }
	    else
		gpsd_log(&session->context->errout, LOG_PROG,
			 "comment, sync lock deferred\n");
	    /* FALL THROUGH */
	} else if (session->lexer.type > COMMENT_PACKET) {
	    if (session->device_type == NULL)
		driver_change = true;
	    else {
		int newtype = session->lexer.type;
		/*
		 * Are we seeing a new packet type? Then we probably
		 * want to change drivers.
		 */
		bool new_packet_type =
		    (newtype != session->device_type->packet_type);
		/*
		 * Possibly the old driver has a mode-switcher method, in
		 * which case we know it can handle NMEA itself and may
		 * want to do special things (like tracking whether a
		 * previous mode switch to binary succeeded in suppressing
		 * NMEA).
		 */
#ifdef RECONFIGURE_ENABLE
		bool dependent_nmea = (newtype == NMEA_PACKET
				       && session->device_type->mode_switcher!=NULL);
#else
		bool dependent_nmea = false;
#endif /* RECONFIGURE_ENABLE */

		/*
		 * Compute whether to switch drivers.
		 * If the previous driver type was sticky and this one
		 * isn't, we'll revert after processing the packet.
		 */
		driver_change = new_packet_type && !dependent_nmea;
	    }
	    if (driver_change) {
		const struct gps_type_t **dp;

		for (dp = gpsd_drivers; *dp; dp++)
		    if (session->lexer.type == (*dp)->packet_type) {
			gpsd_log(&session->context->errout, LOG_PROG,
				 "switching to match packet type %d: %s\n",
				 session->lexer.type, gpsd_prettydump(session));
			(void)gpsd_switch_driver(session, (*dp)->type_name);
			break;
		    }
	    }
	    session->badcount = 0;
	    session->gpsdata.dev.driver_mode = (session->lexer.type > NMEA_PACKET) ? MODE_BINARY : MODE_NMEA;
	    /* FALL THROUGH */
	} else if (hunt_failure(session) && !gpsd_next_hunt_setting(session)) {
	    gpsd_log(&session->context->errout, LOG_INF,
		     "hunt on %s failed (%lf sec since data)\n",
		     session->gpsdata.dev.path,
		     timestamp() - session->gpsdata.online);
	    return ERROR_SET;
	}
    }

    if (session->lexer.outbuflen == 0) {	/* got new data, but no packet */
	gpsd_log(&session->context->errout, LOG_RAW + 3,
		 "New data on %s, not yet a packet\n",
		 session->gpsdata.dev.path);
	return ONLINE_SET;
    } else {			/* we have recognized a packet */
	gps_mask_t received = PACKET_SET;
	session->gpsdata.online = timestamp();

	gpsd_log(&session->context->errout, LOG_RAW + 3,
		 "Accepted packet on %s.\n",
		 session->gpsdata.dev.path);

	/* track the packet count since achieving sync on the device */
	if (driver_change
		&& (session->drivers_identified & (1 << session->driver_index)) == 0) {
	    speed_t speed = gpsd_get_speed(session);

	    /* coverity[var_deref_op] */
	    gpsd_log(&session->context->errout, LOG_INF,
		     "%s identified as type %s, %ld sec @ %ubps\n",
		     session->gpsdata.dev.path,
		     session->device_type->type_name,
		     (long)(time(NULL) - session->opentime),
		     (unsigned int)speed);

	    /* fire the init_query method */
	    if (session->device_type != NULL
		&& session->device_type->init_query != NULL) {
		/*
		 * We can force readonly off knowing this method does
		 * not alter device state.
		 */
		bool saved = session->context->readonly;
		session->context->readonly = false;
		session->device_type->init_query(session);
		session->context->readonly = saved;
	    }

	    /* fire the identified hook */
	    if (session->device_type != NULL
		&& session->device_type->event_hook != NULL)
		session->device_type->event_hook(session, event_identified);
	    session->lexer.counter = 0;

	    /* let clients know about this. */
	    received |= DRIVER_IS;

	    /* mark the fact that this driver has been seen */
	    session->drivers_identified |= (1 << session->driver_index);
	} else
	    session->lexer.counter++;

	/* fire the configure hook */
	if (session->device_type != NULL
	    && session->device_type->event_hook != NULL)
	    session->device_type->event_hook(session, event_configure);

	/*
	 * The guard looks superfluous, but it keeps the rather expensive
	 * gpsd_packetdump() function from being called even when the debug
	 * level does not actually require it.
	 */
	if (session->context->errout.debug >= LOG_RAW)
	    gpsd_log(&session->context->errout, LOG_RAW,
		     "raw packet of type %d, %zd:%s\n",
		     session->lexer.type,
		     session->lexer.outbuflen,
		     gpsd_prettydump(session));

	/* Get data from current packet into the fix structure */
	if (session->lexer.type != COMMENT_PACKET)
	    if (session->device_type != NULL
		&& session->device_type->parse_packet != NULL)
		received |= session->device_type->parse_packet(session);

#ifdef RECONFIGURE_ENABLE
	/*
	 * We may want to revert to the last driver that was marked
	 * sticky.  What this accomplishes is that if we've just
	 * processed something like AIVDM, but a driver with control
	 * methods or an event hook had been active before that, we
	 * keep the information about those capabilities.
	 */
	if (!STICKY(session->device_type)
	    && session->last_controller != NULL
	    && STICKY(session->last_controller))
	{
	    session->device_type = session->last_controller;
	    gpsd_log(&session->context->errout, LOG_PROG,
		     "reverted to %s driver...\n",
		     session->device_type->type_name);
	}
#endif /* RECONFIGURE_ENABLE */

#ifdef TIMING_ENABLE
	/* are we going to generate a report? if so, count characters */
	if ((received & REPORT_IS) != 0) {
	    session->chars = session->lexer.char_counter - session->lexer.start_char;
	}
#endif /* TIMING_ENABLE */


	session->gpsdata.set = ONLINE_SET | received;

#ifndef NOFLOATS_ENABLE
	/*
	 * Compute fix-quality data from the satellite positions.
	 * These will not overwrite any DOPs reported from the packet
	 * we just got.
	 */
	if ((received & SATELLITE_SET) != 0
	    && session->gpsdata.satellites_visible > 0) {
	    session->gpsdata.set |= fill_dop(&session->context->errout,
					     &session->gpsdata,
					     &session->gpsdata.dop);
	    session->gpsdata.epe = NAN;
	}
#endif /* NOFLOATS_ENABLE */

	/* copy/merge device data into staging buffers */
	if ((session->gpsdata.set & CLEAR_IS) != 0) {
	    gps_clear_fix(&session->gpsdata.fix);
            gps_clear_att(&session->gpsdata.attitude);
        }
	/* don't downgrade mode if holding previous fix */
	if (session->gpsdata.fix.mode > session->newdata.mode)
	    session->gpsdata.set &= ~MODE_SET;
	/* gpsd_log(&session->context->errout, LOG_PROG,
	                 "transfer mask: %s\n",
                         gps_maskdump(session->gpsdata.set)); */
	gps_merge_fix(&session->gpsdata.fix,
		      session->gpsdata.set, &session->newdata);
#ifndef NOFLOATS_ENABLE
	gpsd_error_model(session, &session->gpsdata.fix, &session->oldfix);
#endif /* NOFLOATS_ENABLE */


	/*
	 * Count good fixes. We used to check
	 *      session->gpsdata.status > STATUS_NO_FIX
	 * here, but that wasn't quite right.  That tells us whether
	 * we think we have a valid fix for the current cycle, but remains
	 * true while following non-fix packets are received.  What we
	 * really want to know is whether the last packet received was a
	 * fix packet AND held a valid fix. We must ignore non-fix packets
	 * AND packets which have fix data but are flagged as invalid. Some
	 * devices output fix packets on a regular basis, even when unable
	 * to derive a good fix. Such packets should set STATUS_NO_FIX.
	 */
	if ( 0 != (session->gpsdata.set & LATLON_SET)) {
	    if ( session->gpsdata.status > STATUS_NO_FIX) {
		session->context->fixcnt++;
		session->fixcnt++;
            } else {
		session->context->fixcnt = 0;
		session->fixcnt = 0;
            }
	}

	/*
	 * Sanity check.  This catches a surprising number of port and
	 * driver errors, including 32-vs.-64-bit problems.
	 */
	if ((session->gpsdata.set & TIME_SET) != 0) {
	    if (session->newdata.time > time(NULL) + (60 * 60 * 24 * 365))
		gpsd_log(&session->context->errout, LOG_WARN,
			 "date more than a year in the future!\n");
	    else if (session->newdata.time < 0)
		gpsd_log(&session->context->errout, LOG_ERROR,
			 "date is negative!\n");
	}

	return session->gpsdata.set;
    }
    /* Should never get here */
    gpsd_log(&session->context->errout, LOG_EMERG,
             "fell out of gps_poll()!\n");
    return 0;
}

int gpsd_multipoll(const bool data_ready,
		   struct gps_device_t *device,
		   void (*handler)(struct gps_device_t *, gps_mask_t),
		   float reawake_time)
/* consume and handle packets from a specified device */
{
    if (data_ready)
    {
	int fragments;

	gpsd_log(&device->context->errout, LOG_RAW + 1,
		 "polling %d\n", device->gpsdata.gps_fd);

#ifdef NETFEED_ENABLE
	/*
	 * Strange special case - the opening transaction on an NTRIP connection
	 * may not yet be completed.  Try to ratchet things forward.
	 */
	if (device->servicetype == service_ntrip
	    && device->ntrip.conn_state != ntrip_conn_established) {

	    (void)ntrip_open(device, "");
	    if (device->ntrip.conn_state == ntrip_conn_err) {
		gpsd_log(&device->context->errout, LOG_WARN,
			 "connection to ntrip server failed\n");
		device->ntrip.conn_state = ntrip_conn_init;
		return DEVICE_ERROR;
	    } else {
		return DEVICE_READY;
	    }
	}
#endif /* NETFEED_ENABLE */

	for (fragments = 0; ; fragments++) {
	    gps_mask_t changed = gpsd_poll(device);

	    if (changed == EOF_IS) {
		gpsd_log(&device->context->errout, LOG_WARN,
			 "device signed off %s\n",
			 device->gpsdata.dev.path);
		return DEVICE_EOF;
	    } else if (changed == ERROR_SET) {
		gpsd_log(&device->context->errout, LOG_WARN,
			 "device read of %s returned error or packet sniffer failed sync (flags %s)\n",
			 device->gpsdata.dev.path,
			 gps_maskdump(changed));
		return DEVICE_ERROR;
	    } else if (changed == NODATA_IS) {
		/*
		 * No data on the first fragment read means the device
		 * fd may have been in an end-of-file condition on select.
		 */
		if (fragments == 0) {
		    gpsd_log(&device->context->errout, LOG_DATA,
			     "%s returned zero bytes\n",
			     device->gpsdata.dev.path);
		    if (device->zerokill) {
			/* failed timeout-and-reawake, kill it */
			gpsd_deactivate(device);
			if (device->ntrip.works) {
			    device->ntrip.works = false; // reset so we try this once only
			    if (gpsd_activate(device, O_CONTINUE) < 0) {
				gpsd_log(&device->context->errout, LOG_WARN,
					 "reconnect to ntrip server failed\n");
				return DEVICE_ERROR;
			    } else {
				gpsd_log(&device->context->errout, LOG_INFO,
					 "reconnecting to ntrip server\n");
				return DEVICE_READY;
			    }
			}
		    } else if (reawake_time == 0) {
			return DEVICE_ERROR;
		    } else {
			/*
			 * Disable listening to this fd for long enough
			 * that the buffer can fill up again.
			 */
			gpsd_log(&device->context->errout, LOG_DATA,
				 "%s will be repolled in %f seconds\n",
				 device->gpsdata.dev.path, reawake_time);
			device->reawake = time(NULL) + reawake_time;
			return DEVICE_UNREADY;
		    }
		}
		/*
		 * No data on later fragment reads just means the
		 * input buffer is empty.  In this case break out
		 * of the fragment-processing loop but consider
		 * the device still good.
		 */
		break;
	    }

	    /* we got actual data, head off the reawake special case */
	    device->zerokill = false;
	    device->reawake = (time_t)0;

	    /* must have a full packet to continue */
	    if ((changed & PACKET_SET) == 0)
		break;

	    /* conditional prevents mask dumper from eating CPU */
	    if (device->context->errout.debug >= LOG_DATA) {
		if (device->lexer.type == BAD_PACKET)
		    gpsd_log(&device->context->errout, LOG_DATA,
			     "packet with bad checksum from %s\n",
			     device->gpsdata.dev.path);
		else
		    gpsd_log(&device->context->errout, LOG_DATA,
			     "packet type %d from %s with %s\n",
			     device->lexer.type,
			     device->gpsdata.dev.path,
			     gps_maskdump(device->gpsdata.set));
	    }


	    /* handle data contained in this packet */
	    if (device->lexer.type != BAD_PACKET)
		handler(device, changed);

#ifdef __future__
	    /*
	     * Bernd Ocklin suggests:
	     * Exit when a full packet was received and parsed.
	     * This allows other devices to be serviced even if
	     * this device delivers a full packet at every single
	     * read.
	     * Otherwise we can sit here for a long time without
	     * any for-loop exit condition being met.
	     */
	    if ((changed & PACKET_SET) != 0)
               break;
#endif /* __future__ */
	}
    }
    else if (device->reawake>0 && time(NULL) >device->reawake) {
	/* device may have had a zero-length read */
	gpsd_log(&device->context->errout, LOG_DATA,
		 "%s reawakened after zero-length read\n",
		 device->gpsdata.dev.path);
	device->reawake = (time_t)0;
	device->zerokill = true;
	return DEVICE_READY;
    }

    /* no change in device descriptor state */
    return DEVICE_UNCHANGED;
}

void gpsd_wrap(struct gps_device_t *session)
/* end-of-session wrapup */
{
    if (!BAD_SOCKET(session->gpsdata.gps_fd))
	gpsd_deactivate(session);
}

void gpsd_zero_satellites( struct gps_data_t *out)
{
    int sat;

    (void)memset(out->skyview, '\0', sizeof(out->skyview));
    out->satellites_visible = 0;
    /* zero is good inbound data for ss, elevation, and azimuth.  */
    /* we need to set them to invalid values */
    for ( sat = 0; sat < MAXCHANNELS; sat++ ) {
        out->skyview[sat].azimuth = -1;
        out->skyview[sat].elevation = -91;
        out->skyview[sat].ss = -1.0;
    }
#if 0
    /*
     * We used to clear DOPs here, but this causes misbehavior on some
     * combined GPS/GLONASS/QZSS receivers like the Telit SL869; the
     * symptom is that the "satellites_used" field in a struct gps_data_t
     * filled in by gps_read() is always zero.
     */
    gps_clear_dop(&out->dop);
#endif
}

#ifdef NTP_ENABLE
void ntp_latch(struct gps_device_t *device, struct timedelta_t *td)
/* latch the fact that we've saved a fix */
{
    double fix_time, integral, fractional;

    /* this should be an invariant of the way this function is called */
    assert(isfinite(device->newdata.time)!=0);

    (void)clock_gettime(CLOCK_REALTIME, &td->clock);
    fix_time = device->newdata.time;

#ifdef TIMEHINT_ENABLE
    /* assume zero when there's no offset method */
    if (device->device_type == NULL
	|| device->device_type->time_offset == NULL)
	fix_time += 0.0;
    else
	fix_time += device->device_type->time_offset(device);
#endif /* TIMEHINT_ENABLE */
    /* it's ugly but timestamp_t is double */
    /* note loss of precision here
     * td->clock is in nanoSec
     * fix_time is in microSec
     * OK since GPS timestamps are millSec or worse */
    fractional = modf(fix_time, &integral);
    td->real.tv_sec = (time_t)integral;
    td->real.tv_nsec = (long)(fractional * 1e+9);

#ifdef PPS_ENABLE
    /* thread-safe update */
    pps_thread_fixin(&device->pps_thread, td);
#endif /* PPS_ENABLE */
}
#endif /* NTP_ENABLE */

/* end */
