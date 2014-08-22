/* libgpsd_core.c -- manage access to sensors
 *
 * Access to the driver layer goes through the entry points in this file.
 * The idea is to present a session as an abstraction from which you get
 * fixes (and possibly other data updates) by calling gpsd_multipoll(). The
 * rest is setup and teardown. (For backward compatibility the older gpsd_poll()
 * entry point has been retained.)
 *
 * This file is Copyright (c) 2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 */
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
#ifndef S_SPLINT_S
#include <sys/wait.h>
#include <sys/socket.h>
#include <unistd.h>
#endif /* S_SPLINT_S */

#include "gpsd.h"
#if defined(NMEA2000_ENABLE)
#include "driver_nmea2000.h"
#endif /* defined(NMEA2000_ENABLE) */

#if defined(PPS_ENABLE)
static pthread_mutex_t report_mutex;

void gpsd_acquire_reporting_lock(void)
{
    /*@ -unrecog  (splint has no pthread declarations as yet) @*/
    (void)pthread_mutex_lock(&report_mutex);
    /*@ +unrecog @*/
}

void gpsd_release_reporting_lock(void)
{
    /*@ -unrecog (splint has no pthread declarations as yet) @*/
    (void)pthread_mutex_unlock(&report_mutex);
    /*@ +unrecog @*/
}
#endif /* PPS_ENABLE */

static void visibilize(/*@out@*/char *buf2, size_t len, const char *buf)
{
    const char *sp;

    buf2[0] = '\0';
    for (sp = buf; *sp != '\0' && strlen(buf2)+4 < len; sp++)
	if (isprint(*sp) || (sp[0] == '\n' && sp[1] == '\0')
	  || (sp[0] == '\r' && sp[2] == '\0'))
	    (void)snprintf(buf2 + strlen(buf2), 2, "%c", *sp);
	else
	    (void)snprintf(buf2 + strlen(buf2), 6, "\\x%02x",
			   0x00ff & (unsigned)*sp);
}

const char *gpsd_prettydump(struct gps_device_t *session)
/* dump the current packet in a form optimised for eyeballs */
{
    return gpsd_packetdump(session->msgbuf, sizeof(session->msgbuf),
			   (char *)session->packet.outbuffer, 
			   session->packet.outbuflen);
}


void gpsd_labeled_report(const int debuglevel, const int errlevel,
			 const char *label, const char *fmt, va_list ap)
/* assemble command in printf(3) style, use stderr or syslog */
{
#ifndef SQUELCH_ENABLE
    if (errlevel <= debuglevel) {
	char buf[BUFSIZ], buf2[BUFSIZ];
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

	(void)strlcpy(buf, label, sizeof(buf));
	(void)strncat(buf, err_str, sizeof(buf) - 1 - strlen(buf));
	(void)vsnprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), fmt, ap);

	visibilize(buf2, sizeof(buf2), buf);

	if (getpid() == getsid(getpid()))
	    syslog((errlevel == 0) ? LOG_ERR : LOG_NOTICE, "%s", buf2);
	else
	    (void)fputs(buf2, stderr);
#if defined(PPS_ENABLE)
	gpsd_release_reporting_lock();
#endif /* PPS_ENABLE */
    }
#endif /* !SQUELCH_ENABLE */
}

static void gpsd_run_device_hook(const int debuglevel, 
				 char *device_name, char *hook)
{
    struct stat statbuf;
    if (stat(DEVICEHOOKPATH, &statbuf) == -1)
	gpsd_report(debuglevel, LOG_PROG,
		    "no %s present, skipped running %s hook\n",
		    DEVICEHOOKPATH, hook);
    else {
	/*
	 * We make an exception to the no-malloc rule here because
	 * the pointer will never persist outside this small scope
	 * and can thus never cause a leak or stale-pointer problem.
	 */
	size_t bufsize = strlen(DEVICEHOOKPATH) + 1 + strlen(device_name) + 1 + strlen(hook) + 1;
	char *buf = malloc(bufsize);
	if (buf == NULL)
	    gpsd_report(debuglevel, LOG_ERROR,
			"error allocating run-hook buffer\n");
	else
	{
	    int status;
	    (void)snprintf(buf, bufsize, "%s %s %s",
			   DEVICEHOOKPATH, device_name, hook);
	    gpsd_report(debuglevel, LOG_INF, "running %s\n", buf);
	    status = system(buf);
	    if (status == -1)
		gpsd_report(debuglevel, LOG_ERROR, "error running %s\n", buf);
	    else
		gpsd_report(debuglevel, LOG_INF,
			    "%s returned %d\n", DEVICEHOOKPATH,
			    WEXITSTATUS(status));
	    free(buf);
	}
    }
}

/*@-kepttrans@*/
int gpsd_switch_driver(struct gps_device_t *session, char *type_name)
{
    /*@-mustfreeonly@*/
    const struct gps_type_t **dp;
    bool first_sync = (session->device_type != NULL);
    unsigned int i;

    if (first_sync && strcmp(session->device_type->type_name, type_name) == 0)
	return 0;

    gpsd_report(session->context->debug, LOG_PROG,
		"switch_driver(%s) called...\n", type_name);
    /*@ -compmempass @*/
    for (dp = gpsd_drivers, i = 0; *dp; dp++, i++)
	if (strcmp((*dp)->type_name, type_name) == 0) {
	    gpsd_report(session->context->debug, LOG_PROG,
			"selecting %s driver...\n",
			(*dp)->type_name);
	    gpsd_assert_sync(session);
	    /*@i@*/ session->device_type = *dp;
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
    gpsd_report(session->context->debug, LOG_ERROR, "invalid GPS type \"%s\".\n", type_name);
    return 0;
    /*@ +compmempass @*/
    /*@+mustfreeonly@*/
}
/*@+kepttrans@*/

/*@-compdestroy@*/
void gps_context_init(struct gps_context_t *context)
{
    /* *INDENT-OFF* */
    /*@ -initallelements -nullassign -nullderef @*/
    struct gps_context_t nullcontext = {
	.valid	        = 0,
	.debug	        = 0,
	.readonly	= false,
	.fixcnt	        = 0,
	.start_time     = 0,
	.leap_seconds   = 0,
	.gps_week	= 0,
	.gps_tow        = 0,
	.century	= 0,
	.rollovers      = 0,
#ifdef TIMEHINT_ENABLE
	.leap_notify    = LEAP_NOWARNING,
#endif /* TIMEHINT_ENABLE */
#ifdef NTPSHM_ENABLE
	.shmTime	= {0},
	.shmTimeInuse   = {0},
#endif /* NTPSHM_ENABLE */
#ifdef PPS_ENABLE
	.pps_hook       = NULL,
#endif /* PPS_ENABLE */
#ifdef SHM_EXPORT_ENABLE
	.shmexport      = NULL,
#endif /* SHM_EXPORT_ENABLE */
    };
    /*@ +initallelements +nullassign +nullderef @*/
    /* *INDENT-ON* */
    (void)memcpy(context, &nullcontext, sizeof(struct gps_context_t));

#if !defined(S_SPLINT_S) && defined(PPS_ENABLE)
    /*@-nullpass@*/
    (void)pthread_mutex_init(&report_mutex, NULL);
    /*@+nullpass@*/
#endif /* defined(S_SPLINT_S) defined(PPS_ENABLE) */
}
/*@+compdestroy@*/

void gpsd_init(struct gps_device_t *session, struct gps_context_t *context,
	       const char *device)
/* initialize GPS polling */
{
    /*@ -mayaliasunique @*/
    if (device != NULL)
	(void)strlcpy(session->gpsdata.dev.path, device,
		      sizeof(session->gpsdata.dev.path));
    /*@ -mustfreeonly @*/
    session->device_type = NULL;	/* start by hunting packets */
    session->observed = 0;
    session->sourcetype = source_unknown;	/* gpsd_open() sets this */
    session->servicetype = service_unknown;	/* gpsd_open() sets this */
    /*@ -temptrans @*/
    session->context = context;
    /*@ +temptrans @*/
    /*@ +mayaliasunique @*/
    /*@ +mustfreeonly @*/
    gps_clear_fix(&session->gpsdata.fix);
    gps_clear_fix(&session->newdata);
    gps_clear_fix(&session->oldfix);
    session->gpsdata.set = 0;
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
    packet_reset(&session->packet);
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
    gpsd_report(session->context->debug, LOG_INF, "closing GPS=%s (%d)\n",
		session->gpsdata.dev.path, session->gpsdata.gps_fd);
#if defined(NMEA2000_ENABLE)
    if (session->sourcetype == source_can)
        (void)nmea2000_close(session);
    else
#endif /* of defined(NMEA2000_ENABLE) */
        (void)gpsd_close(session);
    if (session->mode == O_OPTIMIZE)
	gpsd_run_device_hook(session->context->debug, 
			     session->gpsdata.dev.path,
			     "DEACTIVATE");
#ifdef PPS_ENABLE
    /*@-mustfreeonly@*/
    session->thread_report_hook = NULL;	/* tell any PPS-watcher thread to die */
#endif /* PPS_ENABLE */
    /*@-mustfreeonly@*/
    /* mark it inactivated */
    session->gpsdata.online = (timestamp_t)0;
}

void gpsd_clear(struct gps_device_t *session)
{
    session->gpsdata.online = timestamp();
#ifdef SIRF_ENABLE
    session->driver.sirf.satcounter = 0;
#endif /* SIRF_ENABLE */
    packet_init(&session->packet);
    session->packet.debug = session->context->debug;
    // session->gpsdata.online = 0;
    gps_clear_fix(&session->gpsdata.fix);
    session->gpsdata.status = STATUS_NO_FIX;
    session->gpsdata.separation = NAN;
    session->mag_var = NAN;
    session->releasetime = (timestamp_t)0;
    session->badcount = 0;

    /* clear the private data union */
    memset(&session->driver, '\0', sizeof(session->driver));

    session->opentime = timestamp();
}

int gpsd_open(struct gps_device_t *session)
/* open a device for access to its data */
{
#ifdef NETFEED_ENABLE
    /* special case: source may be a URI to a remote GNSS or DGPS service */
    if (netgnss_uri_check(session->gpsdata.dev.path)) {
	session->gpsdata.gps_fd = netgnss_uri_open(session,
						   session->gpsdata.dev.path);
	session->sourcetype = source_tcp;
	gpsd_report(session->context->debug, LOG_SPIN,
		    "netgnss_uri_open(%s) returns socket on fd %d\n",
		    session->gpsdata.dev.path, session->gpsdata.gps_fd);
	return session->gpsdata.gps_fd;
    /* otherwise, could be an TCP data feed */
    } else if (strncmp(session->gpsdata.dev.path, "tcp://", 6) == 0) {
	char server[strlen(session->gpsdata.dev.path)+1], *port;
	socket_t dsock;
	(void)strlcpy(server, session->gpsdata.dev.path + 6, sizeof(server));
	INVALIDATE_SOCKET(session->gpsdata.gps_fd);
	port = strchr(server, ':');
	if (port == NULL) {
	    gpsd_report(session->context->debug, LOG_ERROR,
			"Missing colon in TCP feed spec.\n");
	    return -1;
	}
	*port++ = '\0';
	gpsd_report(session->context->debug, LOG_INF,
		    "opening TCP feed at %s, port %s.\n", server,
		    port);
	if ((dsock = netlib_connectsock(AF_UNSPEC, server, port, "tcp")) < 0) {
	    gpsd_report(session->context->debug, LOG_ERROR,
			"TCP device open error %s.\n",
			netlib_errstr(dsock));
	    return -1;
	} else
	    gpsd_report(session->context->debug, LOG_SPIN, 
			"TCP device opened on fd %d\n", dsock);
	session->gpsdata.gps_fd = dsock;
	session->sourcetype = source_tcp;
	return session->gpsdata.gps_fd;
    /* or could be UDP */
    } else if (strncmp(session->gpsdata.dev.path, "udp://", 6) == 0) {
	char server[strlen(session->gpsdata.dev.path)+1], *port;
	socket_t dsock;
	(void)strlcpy(server, session->gpsdata.dev.path + 6, sizeof(server));
	INVALIDATE_SOCKET(session->gpsdata.gps_fd);
	port = strchr(server, ':');
	if (port == NULL) {
	    gpsd_report(session->context->debug, LOG_ERROR,
			"Missing colon in UDP feed spec.\n");
	    return -1;
	}
	*port++ = '\0';
	gpsd_report(session->context->debug, LOG_INF,
		    "opening UDP feed at %s, port %s.\n", server,
		    port);
	if ((dsock = netlib_connectsock(AF_UNSPEC, server, port, "udp")) < 0) {
	    gpsd_report(session->context->debug, LOG_ERROR,
			"UDP device open error %s.\n",
			netlib_errstr(dsock));
	    return -1;
	} else
	    gpsd_report(session->context->debug, LOG_SPIN,
			"UDP device opened on fd %d\n", dsock);
	session->gpsdata.gps_fd = dsock;
	session->sourcetype = source_udp;
	return session->gpsdata.gps_fd;
    }
#endif /* NETFEED_ENABLE */
#ifdef PASSTHROUGH_ENABLE
    if (strncmp(session->gpsdata.dev.path, "gpsd://", 7) == 0) {
	/*@-branchstate -nullpass@*/
	char server[strlen(session->gpsdata.dev.path)+1], *port;
	socket_t dsock;
	(void)strlcpy(server, session->gpsdata.dev.path + 7, sizeof(server));
	INVALIDATE_SOCKET(session->gpsdata.gps_fd);
	if ((port = strchr(server, ':')) == NULL) {
	    port = DEFAULT_GPSD_PORT;
	} else
	    *port++ = '\0';
	gpsd_report(session->context->debug, LOG_INF,
		    "opening remote gpsd feed at %s, port %s.\n",
		    server, port);
	if ((dsock = netlib_connectsock(AF_UNSPEC, server, port, "tcp")) < 0) {
	    gpsd_report(session->context->debug, LOG_ERROR,
			"remote gpsd device open error %s.\n",
			netlib_errstr(dsock));
	    return -1;
	} else
	    gpsd_report(session->context->debug, LOG_SPIN,
			"remote gpsd feed opened on fd %d\n", dsock);
	/*@+branchstate +nullpass@*/
	/* watch to remote is issued when WATCH is */
	session->gpsdata.gps_fd = dsock;
	session->sourcetype = source_gpsd;
	return session->gpsdata.gps_fd;
    }
#endif /* PASSTHROUGH_ENABLE */
#if defined(NMEA2000_ENABLE) && !defined(S_SPLINT_S)
    if (strncmp(session->gpsdata.dev.path, "nmea2000://", 11) == 0) {
        return nmea2000_open(session);
    }
#endif /* defined(NMEA2000_ENABLE) && !defined(S_SPLINT_S) */
    /* fall through to plain serial open */
    return gpsd_serial_open(session);
}

/*@ -branchstate @*/
int gpsd_activate(struct gps_device_t *session, const int mode)
/* acquire a connection to the GPS device */
{
    if (session->mode == O_OPTIMIZE)
	gpsd_run_device_hook(session->context->debug,
			     session->gpsdata.dev.path, "ACTIVATE");
    session->gpsdata.gps_fd = gpsd_open(session);
    if (mode != O_CONTINUE)
	session->mode = mode;

    // cppcheck-suppress pointerLessThanZero
    if (session->gpsdata.gps_fd < 0)
	return -1;
    else {
#ifdef NON_NMEA_ENABLE
	/* if it's a sensor, it must be probed */
        if ((session->servicetype == service_sensor) && 
	    (session->sourcetype != source_can)) {
	    const struct gps_type_t **dp;

	    /*@ -mustfreeonly @*/
	    for (dp = gpsd_drivers; *dp; dp++) {
		if ((*dp)->probe_detect != NULL) {
		    gpsd_report(session->context->debug, LOG_PROG,
				"Probing \"%s\" driver...\n",
				 (*dp)->type_name);
		    /* toss stale data */
		    (void)tcflush(session->gpsdata.gps_fd, TCIOFLUSH);
		    if ((*dp)->probe_detect(session) != 0) {
			gpsd_report(session->context->debug, LOG_PROG,
				    "Probe found \"%s\" driver...\n",
				     (*dp)->type_name);
			session->device_type = *dp;
			gpsd_assert_sync(session);
			goto foundit;
		    } else
			gpsd_report(session->context->debug, LOG_PROG,
				    "Probe not found \"%s\" driver...\n",
			             (*dp)->type_name);
		}
	    }
	    /*@ +mustfreeonly @*/
	    gpsd_report(session->context->debug, LOG_PROG,
			"no probe matched...\n");
	}
      foundit:
#endif /* NON_NMEA_ENABLE */
	gpsd_clear(session);
	gpsd_report(session->context->debug, LOG_INF,
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
    }
    return session->gpsdata.gps_fd;
}

/*@ +branchstate @*/

#ifdef CHEAPFLOATS_ENABLE
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

/*@ -fixedformalarray -mustdefine @*/
static bool invert(double mat[4][4], /*@out@*/ double inverse[4][4])
{
    // Find all NECESSARY 2x2 subdeterminants
    double Det2_12_01 = mat[1][0] * mat[2][1] - mat[1][1] * mat[2][0];
    double Det2_12_02 = mat[1][0] * mat[2][2] - mat[1][2] * mat[2][0];
    //double Det2_12_03 = mat[1][0]*mat[2][3] - mat[1][3]*mat[2][0];
    double Det2_12_12 = mat[1][1] * mat[2][2] - mat[1][2] * mat[2][1];
    //double Det2_12_13 = mat[1][1]*mat[2][3] - mat[1][3]*mat[2][1];
    //double Det2_12_23 = mat[1][2]*mat[2][3] - mat[1][3]*mat[2][2];
    double Det2_13_01 = mat[1][0] * mat[3][1] - mat[1][1] * mat[3][0];
    //double Det2_13_02 = mat[1][0]*mat[3][2] - mat[1][2]*mat[3][0];
    double Det2_13_03 = mat[1][0] * mat[3][3] - mat[1][3] * mat[3][0];
    //double Det2_13_12 = mat[1][1]*mat[3][2] - mat[1][2]*mat[3][1];
    double Det2_13_13 = mat[1][1] * mat[3][3] - mat[1][3] * mat[3][1];
    //double Det2_13_23 = mat[1][2]*mat[3][3] - mat[1][3]*mat[3][2];
    double Det2_23_01 = mat[2][0] * mat[3][1] - mat[2][1] * mat[3][0];
    double Det2_23_02 = mat[2][0] * mat[3][2] - mat[2][2] * mat[3][0];
    double Det2_23_03 = mat[2][0] * mat[3][3] - mat[2][3] * mat[3][0];
    double Det2_23_12 = mat[2][1] * mat[3][2] - mat[2][2] * mat[3][1];
    double Det2_23_13 = mat[2][1] * mat[3][3] - mat[2][3] * mat[3][1];
    double Det2_23_23 = mat[2][2] * mat[3][3] - mat[2][3] * mat[3][2];

    // Find all NECESSARY 3x3 subdeterminants
    double Det3_012_012 = mat[0][0] * Det2_12_12 - mat[0][1] * Det2_12_02
	+ mat[0][2] * Det2_12_01;
    //double Det3_012_013 = mat[0][0]*Det2_12_13 - mat[0][1]*Det2_12_03
    //                            + mat[0][3]*Det2_12_01;
    //double Det3_012_023 = mat[0][0]*Det2_12_23 - mat[0][2]*Det2_12_03
    //                            + mat[0][3]*Det2_12_02;
    //double Det3_012_123 = mat[0][1]*Det2_12_23 - mat[0][2]*Det2_12_13
    //                            + mat[0][3]*Det2_12_12;
    //double Det3_013_012 = mat[0][0]*Det2_13_12 - mat[0][1]*Det2_13_02
    //                            + mat[0][2]*Det2_13_01;
    double Det3_013_013 = mat[0][0] * Det2_13_13 - mat[0][1] * Det2_13_03
	+ mat[0][3] * Det2_13_01;
    //double Det3_013_023 = mat[0][0]*Det2_13_23 - mat[0][2]*Det2_13_03
    //                            + mat[0][3]*Det2_13_02;
    //double Det3_013_123 = mat[0][1]*Det2_13_23 - mat[0][2]*Det2_13_13
    //                            + mat[0][3]*Det2_13_12;
    //double Det3_023_012 = mat[0][0]*Det2_23_12 - mat[0][1]*Det2_23_02
    //                            + mat[0][2]*Det2_23_01;
    //double Det3_023_013 = mat[0][0]*Det2_23_13 - mat[0][1]*Det2_23_03
    //                            + mat[0][3]*Det2_23_01;
    double Det3_023_023 = mat[0][0] * Det2_23_23 - mat[0][2] * Det2_23_03
	+ mat[0][3] * Det2_23_02;
    //double Det3_023_123 = mat[0][1]*Det2_23_23 - mat[0][2]*Det2_23_13
    //                            + mat[0][3]*Det2_23_12;
    double Det3_123_012 = mat[1][0] * Det2_23_12 - mat[1][1] * Det2_23_02
	+ mat[1][2] * Det2_23_01;
    double Det3_123_013 = mat[1][0] * Det2_23_13 - mat[1][1] * Det2_23_03
	+ mat[1][3] * Det2_23_01;
    double Det3_123_023 = mat[1][0] * Det2_23_23 - mat[1][2] * Det2_23_03
	+ mat[1][3] * Det2_23_02;
    double Det3_123_123 = mat[1][1] * Det2_23_23 - mat[1][2] * Det2_23_13
	+ mat[1][3] * Det2_23_12;

    // Find the 4x4 determinant
    static double det;
    det = mat[0][0] * Det3_123_123
	- mat[0][1] * Det3_123_023
	+ mat[0][2] * Det3_123_013 - mat[0][3] * Det3_123_012;

    // Very small determinants probably reflect floating-point fuzz near zero
    if (fabs(det) < 0.0001)
	return false;

    inverse[0][0] = Det3_123_123 / det;
    //inverse[0][1] = -Det3_023_123 / det;
    //inverse[0][2] =  Det3_013_123 / det;
    //inverse[0][3] = -Det3_012_123 / det;

    //inverse[1][0] = -Det3_123_023 / det;
    inverse[1][1] = Det3_023_023 / det;
    //inverse[1][2] = -Det3_013_023 / det;
    //inverse[1][3] =  Det3_012_023 / det;

    //inverse[2][0] =  Det3_123_013 / det;
    //inverse[2][1] = -Det3_023_013 / det;
    inverse[2][2] = Det3_013_013 / det;
    //inverse[2][3] = -Det3_012_013 / det;

    //inverse[3][0] = -Det3_123_012 / det;
    //inverse[3][1] =  Det3_023_012 / det;
    //inverse[3][2] = -Det3_013_012 / det;
    inverse[3][3] = Det3_012_012 / det;

    return true;
}

/*@ +fixedformalarray +mustdefine @*/

static gps_mask_t fill_dop(const struct gps_data_t * gpsdata, struct dop_t * dop, const int debug)
{
    double prod[4][4];
    double inv[4][4];
    double satpos[MAXCHANNELS][4];
    double xdop, ydop, hdop, vdop, pdop, tdop, gdop;
    int i, j, k, n;

    memset(satpos, 0, sizeof(satpos));

    for (n = k = 0; k < gpsdata->satellites_used; k++) {
	if (gpsdata->used[k] == 0)
	    continue;
	satpos[n][0] = sin(gpsdata->azimuth[k] * DEG_2_RAD)
	    * cos(gpsdata->elevation[k] * DEG_2_RAD);
	satpos[n][1] = cos(gpsdata->azimuth[k] * DEG_2_RAD)
	    * cos(gpsdata->elevation[k] * DEG_2_RAD);
	satpos[n][2] = sin(gpsdata->elevation[k] * DEG_2_RAD);
	satpos[n][3] = 1;
	n++;
    }

    /* If we don't have 4 satellites then we don't have enough information to calculate DOPS */
    if (n < 4) {
#ifdef __UNUSED__
	gpsd_report(session->context->debug, LOG_DATA + 2,
		    "Not enough satellites available %d < 4:\n",
		    n);
#endif /* __UNUSED__ */
	return 0;		/* Is this correct return code here? or should it be ERROR_SET */
    }

    memset(prod, 0, sizeof(prod));
    memset(inv, 0, sizeof(inv));

#ifdef __UNUSED__
    gpsd_report(session->context->debug, LOG_INF, "Line-of-sight matrix:\n");
    for (k = 0; k < n; k++) {
	gpsd_report(debug, LOG_INF, "%f %f %f %f\n",
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
    gpsd_report(debug, LOG_INF, "product:\n");
    for (k = 0; k < 4; k++) {
	gpsd_report(session->context->debug, LOG_INF, "%f %f %f %f\n",
		    prod[k][0], prod[k][1], prod[k][2], prod[k][3]);
    }
#endif /* __UNUSED__ */

    if (invert(prod, inv)) {
#ifdef __UNUSED__
	/*
	 * Note: this will print garbage unless all the subdeterminants
	 * are computed in the invert() function.
	 */
	gpsd_report(debug, LOG_RAW, "inverse:\n");
	for (k = 0; k < 4; k++) {
	    gpsd_report(session->context->debug, LOG_RAW,
			"%f %f %f %f\n",
			inv[k][0], inv[k][1], inv[k][2], inv[k][3]);
	}
#endif /* __UNUSED__ */
    } else {
#ifndef USE_QT
	gpsd_report(debug, LOG_DATA,
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
    gpsd_report(debug, LOG_DATA,
		"DOPS computed/reported: X=%f/%f, Y=%f/%f, H=%f/%f, V=%f/%f, P=%f/%f, T=%f/%f, G=%f/%f\n",
		xdop, dop->xdop, ydop, dop->ydop, hdop, dop->hdop, vdop,
		dop->vdop, pdop, dop->pdop, tdop, dop->tdop, gdop, dop->gdop);
#endif

    /*@ -usedef @*/
    if (isnan(dop->xdop) != 0) {
	dop->xdop = xdop;
    }
    if (isnan(dop->ydop) != 0) {
	dop->ydop = ydop;
    }
    if (isnan(dop->hdop) != 0) {
	dop->hdop = hdop;
    }
    if (isnan(dop->vdop) != 0) {
	dop->vdop = vdop;
    }
    if (isnan(dop->pdop) != 0) {
	dop->pdop = pdop;
    }
    if (isnan(dop->tdop) != 0) {
	dop->tdop = tdop;
    }
    if (isnan(dop->gdop) != 0) {
	dop->gdop = gdop;
    }
    /*@ +usedef @*/

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

    /*
     * OK, this is not an error computation, but we're at the right
     * place in the architecture for it.  Compute speed over ground
     * and climb/sink in the simplest possible way.
     */
    if (fix->mode >= MODE_2D && oldfix->mode >= MODE_2D
	&& isnan(fix->speed) != 0) {
	if (fix->time == oldfix->time)
	    fix->speed = 0;
	else
	    fix->speed =
		earth_distance(fix->latitude, fix->longitude,
			       oldfix->latitude, oldfix->longitude)
		/ (fix->time - oldfix->time);
    }
    if (fix->mode >= MODE_3D && oldfix->mode >= MODE_3D
	&& isnan(fix->climb) != 0) {
	if (fix->time == oldfix->time)
	    fix->climb = 0;
	else if (isnan(fix->altitude) == 0 && isnan(oldfix->altitude) == 0) {
	    fix->climb =
		(fix->altitude - oldfix->altitude) / (fix->time -
						      oldfix->time);
	}
    }

    /*
     * Field reports match the theoretical prediction that
     * expected time error should be half the resolution of
     * the GPS clock, so we put the bound of the error
     * in as a constant pending getting it from each driver.
     * FIXME: increase this if no keap-second has been seen
     * and it's less than 750s (one almanac load cycle)
     * from device powerup.
     */
    if (isnan(fix->time) == 0 && isnan(fix->ept) != 0)
	fix->ept = 0.005;
    /* Other error computations depend on having a valid fix */
    if (fix->mode >= MODE_2D) {
	if (isnan(fix->epx) != 0 && isfinite(session->gpsdata.dop.hdop) != 0)
	    fix->epx = session->gpsdata.dop.xdop * h_uere;

	if (isnan(fix->epy) != 0 && isfinite(session->gpsdata.dop.hdop) != 0)
	    fix->epy = session->gpsdata.dop.ydop * h_uere;

	if ((fix->mode >= MODE_3D)
	    && isnan(fix->epv) != 0 && isfinite(session->gpsdata.dop.vdop) != 0)
	    fix->epv = session->gpsdata.dop.vdop * v_uere;

	if (isnan(session->gpsdata.epe) != 0
	    && isfinite(session->gpsdata.dop.pdop) != 0)
	    session->gpsdata.epe = session->gpsdata.dop.pdop * p_uere;
	else
	    session->gpsdata.epe = NAN;

	/*
	 * If we have a current fix and an old fix, and the packet handler
	 * didn't set the speed error and climb error members itself,
	 * try to compute them now.
	 */
	if (isnan(fix->eps) != 0) {
	    if (oldfix->mode > MODE_NO_FIX && fix->mode > MODE_NO_FIX
		&& isnan(oldfix->epx) == 0 && isnan(oldfix->epy) == 0
		&& isnan(oldfix->time) == 0 && isnan(oldfix->time) == 0
		&& fix->time > oldfix->time) {
		timestamp_t t = fix->time - oldfix->time;
		double e =
		    EMIX(oldfix->epx, oldfix->epy) + EMIX(fix->epx, fix->epy);
		fix->eps = e / t;
	    } else
		fix->eps = NAN;
	}
	if ((fix->mode >= MODE_3D)
	    && isnan(fix->epc) != 0 && fix->time > oldfix->time) {
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
		if (isnan(adj) == 0 && adj > EMIX(fix->epx, fix->epy)) {
		    double opp = EMIX(fix->epx, fix->epy);
		    double hyp = sqrt(adj * adj + opp * opp);
		    fix->epd = RAD_2_DEG * 2 * asin(opp / hyp);
		}
	    }
	}
    }

    /* save old fix for later error computations */
    /*@ -mayaliasunique @*/
    if (fix->mode >= MODE_2D)
	(void)memcpy(oldfix, fix, sizeof(struct gps_fix_t));
    /*@ +mayaliasunique @*/
}
#endif /* CHEAPFLOATS_ENABLE */

/*@ -mustdefine -compdef @*/
int gpsd_await_data(/*@out@*/fd_set *rfds, 
		     const int maxfd,
		     /*@in@*/fd_set *all_fds, 
		     const int debug)
/* await data from any socket in the all_fds set */
{
    int status;
#ifdef COMPAT_SELECT
    struct timeval tv;
#endif /* COMPAT_SELECT */

#ifdef EFDS
    FD_ZERO(efds);
#endif /* EFDS */
    (void)memcpy((char *)rfds, (char *)all_fds, sizeof(fd_set));
    gpsd_report(debug, LOG_RAW + 2, "select waits\n");
    /*
     * Poll for user commands or GPS data.  The timeout doesn't
     * actually matter here since select returns whenever one of
     * the file descriptors in the set goes ready.  The point
     * of tracking maxfd is to keep the set of descriptors that
     * select(2) has to poll here as small as possible (for
     * low-clock-rate SBCs and the like).
     *
     * pselect() is preferable, when we can have it, to eliminate
     * the once-per-second wakeup when no sensors are attached.
     * This cuts power consumption.
     */
    /*@ -usedef -nullpass @*/
    errno = 0;

#ifdef COMPAT_SELECT
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    status = select(maxfd + 1, rfds, NULL, NULL, &tv);
#else
    status = pselect(maxfd + 1, rfds, NULL, NULL, NULL, NULL);
#endif
    if (status == -1) {
	if (errno == EINTR)
	    return AWAIT_NOT_READY;
	else if (errno == EBADF) {
	    int fd;
	    for (fd = 0; fd < FD_SETSIZE; fd++)
		/*
		 * All we care about here is a cheap, fast, uninterruptible
		 * way to check if a file descriptor is valid.
		 * FIXME: pass out error fds when we can do a library bump.
		 */
		if (FD_ISSET(fd, all_fds) && fcntl(fd, F_GETFL, 0) == -1) {
		    FD_CLR(fd, all_fds);
#ifdef EFDS
		    FD_SET(fd, efds);
#endif /* EFDS */
		}
	    return AWAIT_NOT_READY;
	} else {
	    gpsd_report(debug, LOG_ERROR, "select: %s\n", strerror(errno));
	    return AWAIT_FAILED;
	}
    }
    /*@ +usedef +nullpass @*/

    if (debug >= LOG_SPIN) {
	int i;
	char dbuf[BUFSIZ];
	dbuf[0] = '\0';
	for (i = 0; i < FD_SETSIZE; i++)
	    if (FD_ISSET(i, all_fds))
		(void)snprintf(dbuf + strlen(dbuf),
			       sizeof(dbuf) - strlen(dbuf), "%d ", i);
	if (strlen(dbuf) > 0)
	    dbuf[strlen(dbuf) - 1] = '\0';
	(void)strlcat(dbuf, "} -> {", BUFSIZ);
	for (i = 0; i < FD_SETSIZE; i++)
	    if (FD_ISSET(i, rfds))
		(void)snprintf(dbuf + strlen(dbuf),
			       sizeof(dbuf) - strlen(dbuf), " %d ", i);
	gpsd_report(debug, LOG_SPIN,
		    "select() {%s} at %f (errno %d)\n",
		    dbuf, timestamp(), errno);
    }

    return AWAIT_GOT_INPUT;
}
/*@ +mustdefine +compdef @*/

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
     * The second was session->badcount++>1 && session->packet.state==0.
     * Fail hunt only if we get a second consecutive bad packet
     * and the lexer is in ground state.  We don't want to fail on
     * a first bad packet because the source might have a burst of
     * leading garbage after open.  We don't want to fail if the
     * lexer is not in ground state, because that means the read
     * might have picked up a valid partial packet - better to go
     * back around the loop and pick up more data.
     *
     * The "&& session->packet.state==0" guard causes an intermittent
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
    if (session->packet.outbuflen == 0)
    {
	/* beginning of a new packet */
	timestamp_t now = timestamp();
	if (session->device_type != NULL && session->packet.start_time > 0) {
#ifdef RECONFIGURE_ENABLE
	    const double min_cycle = session->device_type->min_cycle;
#else
	    const double min_cycle = 1;
#endif /* RECONFIGURE_ENABLE */
	    double quiet_time = (MINIMUM_QUIET_TIME * min_cycle);
	    double gap = now - session->packet.start_time;

	    if (gap > min_cycle)
		gpsd_report(session->context->debug, LOG_WARN,
			    "cycle-start detector failed.\n");
	    else if (gap > quiet_time) {
		gpsd_report(session->context->debug, LOG_PROG,
			    "transmission pause of %f\n", gap);
		session->sor = now;
		session->packet.start_char = session->packet.char_counter;
	    }
	}
	session->packet.start_time = now;
    }
#endif /* TIMING_ENABLE */

    if (session->packet.type >= COMMENT_PACKET) {
	/*@-shiftnegative@*/
	session->observed |= PACKET_TYPEMASK(session->packet.type);
	/*@+shiftnegative@*/
    }

    /* can we get a full packet from the device? */
    if (session->device_type != NULL) {
	newlen = session->device_type->get_packet(session);
	/* coverity[deref_ptr] */
	gpsd_report(session->context->debug, LOG_RAW,
		    "%s is known to be %s\n",
		    session->gpsdata.dev.path,
		    session->device_type->type_name);
    } else {
	newlen = generic_get(session);
    }

    /* update the scoreboard structure from the GPS */
    gpsd_report(session->context->debug, LOG_RAW + 2,
		"%s sent %zd new characters\n",
		session->gpsdata.dev.path, newlen);
    if (newlen < 0) {		/* read error */
	gpsd_report(session->context->debug, LOG_INF,
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
	    gpsd_report(session->context->debug, LOG_INF,
			"GPS on %s is offline (%lf sec since data)\n",
			session->gpsdata.dev.path,
			timestamp() - session->gpsdata.online);
	    session->gpsdata.online = (timestamp_t)0;
	}
	return NODATA_IS;
    } else /* (newlen > 0) */ {
	gpsd_report(session->context->debug, LOG_RAW,
		    "packet sniff on %s finds type %d\n",
		    session->gpsdata.dev.path, session->packet.type);
	if (session->packet.type == COMMENT_PACKET) {
	    if (strcmp((const char *)session->packet.outbuffer, "# EOF\n") == 0) {
		gpsd_report(session->context->debug, LOG_PROG,
			    "synthetic EOF\n");
		return EOF_SET;
	    }
	    else
		gpsd_report(session->context->debug, LOG_PROG,
			    "comment, sync lock deferred\n");
	    /* FALL THROUGH */
	} else if (session->packet.type > COMMENT_PACKET) {
	    if (session->device_type == NULL)
		driver_change = true;
	    else {
		int newtype = session->packet.type;
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
	    /*@-nullderef@*/
	    if (driver_change) {
		const struct gps_type_t **dp;

		for (dp = gpsd_drivers; *dp; dp++)
		    if (session->packet.type == (*dp)->packet_type) {
			gpsd_report(session->context->debug, LOG_PROG,
				    "switching to match packet type %d: %s\n",
				    session->packet.type, gpsd_prettydump(session));
			(void)gpsd_switch_driver(session, (*dp)->type_name);
			break;
		    }
	    }
	    /*@+nullderef@*/
	    session->badcount = 0;
	    session->gpsdata.dev.driver_mode = (session->packet.type > NMEA_PACKET) ? MODE_BINARY : MODE_NMEA;
	    /* FALL THROUGH */
	} else if (hunt_failure(session) && !gpsd_next_hunt_setting(session)) {
	    gpsd_report(session->context->debug, LOG_INF,
			"hunt on %s failed (%lf sec since data)\n",
			session->gpsdata.dev.path,
			timestamp() - session->gpsdata.online);
	    return ERROR_SET;
	}
    }

    if (session->packet.outbuflen == 0) {	/* got new data, but no packet */
	gpsd_report(session->context->debug, LOG_RAW + 3,
		    "New data on %s, not yet a packet\n",
		    session->gpsdata.dev.path);
	return ONLINE_SET;
    } else {			/* we have recognized a packet */
	gps_mask_t received = PACKET_SET;
	session->gpsdata.online = timestamp();

	gpsd_report(session->context->debug, LOG_RAW + 3,
		    "Accepted packet on %s.\n",
		    session->gpsdata.dev.path);

	/* track the packet count since achieving sync on the device */
	if (driver_change 
		&& (session->drivers_identified & (1 << session->driver_index)) == 0) {
	    speed_t speed = gpsd_get_speed(session);

	    /*@-nullderef@*/
	    /* coverity[var_deref_op] */
	    gpsd_report(session->context->debug, LOG_INF,
			"%s identified as type %s, %f sec @ %ubps\n",
			session->gpsdata.dev.path,
			session->device_type->type_name,
			timestamp() - session->opentime,
			(unsigned int)speed);
	    /*@+nullderef@*/
	    /* fire the identified hook */
	    if (session->device_type != NULL
		&& session->device_type->event_hook != NULL)
		session->device_type->event_hook(session, event_identified);
	    session->packet.counter = 0;

	    /* let clients know about this. */
	    received |= DRIVER_IS;

	    /* mark the fact that this driver has been seen */
	    session->drivers_identified |= (1 << session->driver_index);
	} else
	    session->packet.counter++;

	/* fire the configure hook */
	if (session->device_type != NULL
	    && session->device_type->event_hook != NULL)
	    session->device_type->event_hook(session, event_configure);

	/*
	 * The guard looks superfluous, but it keeps the rather expensive
	 * gpsd_packetdump() function from being called even when the debug
	 * level does not actually require it.
	 */
	if (session->context->debug >= LOG_RAW)
	    gpsd_report(session->context->debug, LOG_RAW,
			"raw packet of type %d, %zd:%s\n",
			session->packet.type,
			session->packet.outbuflen,
			gpsd_prettydump(session));

	/* Get data from current packet into the fix structure */
	if (session->packet.type != COMMENT_PACKET)
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
	/*@-mustfreeonly@*/
	if (!STICKY(session->device_type)
	    && session->last_controller != NULL)
	{
	    session->device_type = session->last_controller;
	    gpsd_report(session->context->debug, LOG_PROG,
			"reverted to %s driver...\n",
			session->device_type->type_name);
	}
	/*@+mustfreeonly@*/
#endif /* RECONFIGURE_ENABLE */

#ifdef TIMING_ENABLE
	/* are we going to generate a report? if so, count characters */
	if ((received & REPORT_IS) != 0) {
	    session->chars = session->packet.char_counter - session->packet.start_char;
	}
#endif /* TIMING_ENABLE */


	session->gpsdata.set = ONLINE_SET | received;

#ifdef CHEAPFLOATS_ENABLE
	/*
	 * Compute fix-quality data from the satellite positions.
	 * These will not overwrite any DOPs reported from the packet
	 * we just got.
	 */
	if ((received & SATELLITE_SET) != 0
	    && session->gpsdata.satellites_visible > 0) {
	    session->gpsdata.set |= fill_dop(&session->gpsdata, &session->gpsdata.dop, session->context->debug);
	    session->gpsdata.epe = NAN;
	}
#endif /* CHEAPFLOATS_ENABLE */

	/* copy/merge device data into staging buffers */
	/*@-nullderef -nullpass@*/
	if ((session->gpsdata.set & CLEAR_IS) != 0)
	    gps_clear_fix(&session->gpsdata.fix);
	/* don't downgrade mode if holding previous fix */
	if (session->gpsdata.fix.mode > session->newdata.mode)
	    session->gpsdata.set &= ~MODE_SET;
	//gpsd_report(session->context->debug, LOG_PROG,
	//              "transfer mask on %s: %02x\n", session->gpsdata.tag, session->gpsdata.set);
	gps_merge_fix(&session->gpsdata.fix,
		      session->gpsdata.set, &session->newdata);
#ifdef CHEAPFLOATS_ENABLE
	gpsd_error_model(session, &session->gpsdata.fix, &session->oldfix);
#endif /* CHEAPFLOATS_ENABLE */

	/*@+nullderef -nullpass@*/

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
	/*@+relaxtypes +longunsignedintegral@*/
	if ((session->gpsdata.set & TIME_SET) != 0) {
	    if (session->newdata.time > time(NULL) + (60 * 60 * 24 * 365))
		gpsd_report(session->context->debug, LOG_WARN,
			    "date more than a year in the future!\n");
	    else if (session->newdata.time < 0)
		gpsd_report(session->context->debug, LOG_ERROR,
			    "date in %s is negative!\n", session->gpsdata.tag);
	}
	/*@-relaxtypes -longunsignedintegral@*/

	return session->gpsdata.set;
    }
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

	gpsd_report(device->context->debug, LOG_RAW + 1, 
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
		gpsd_report(device->context->debug, LOG_WARN,
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

	    if (changed == EOF_SET) {
		gpsd_report(device->context->debug, LOG_WARN,
			    "device signed off %s\n",
			    device->gpsdata.dev.path);
		return DEVICE_EOF;
	    } else if (changed == ERROR_SET) {
		gpsd_report(device->context->debug, LOG_WARN,
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
		    gpsd_report(device->context->debug, LOG_DATA,
				"%s returned zero bytes\n",
				device->gpsdata.dev.path);
		    if (device->zerokill) {
			/* failed timeout-and-reawake, kill it */
			gpsd_deactivate(device);
			if (device->ntrip.works) {
			    device->ntrip.works = false; // reset so we try this once only
			    if (gpsd_activate(device, O_CONTINUE) < 0) {
				gpsd_report(device->context->debug, LOG_WARN,
					    "reconnect to ntrip server failed\n");
				return DEVICE_ERROR;
			    } else {
				gpsd_report(device->context->debug, LOG_INFO,
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
			gpsd_report(device->context->debug, LOG_DATA,
				    "%s will be repolled in %f seconds\n",
				    device->gpsdata.dev.path, reawake_time);
			device->reawake = timestamp() + reawake_time;
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
	    device->reawake = (timestamp_t)0;

	    /* must have a full packet to continue */
	    if ((changed & PACKET_SET) == 0)
		break;

	    /* conditional prevents mask dumper from eating CPU */
	    if (device->context->debug >= LOG_DATA) {
		if (device->packet.type == BAD_PACKET)
		    gpsd_report(device->context->debug, LOG_DATA,
				"packet with bad checksum from %s\n",
				device->gpsdata.dev.path);
		else
		    gpsd_report(device->context->debug, LOG_DATA,
				"packet type %d from %s with %s\n",
				device->packet.type,
				device->gpsdata.dev.path,
				gps_maskdump(device->gpsdata.set));
	    }


	    /* handle data contained in this packet */
	    if (device->packet.type != BAD_PACKET)
		/*@i1@*/handler(device, changed);

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
    else if (device->reawake>0 && timestamp()>device->reawake) {
	/* device may have had a zero-length read */
	gpsd_report(device->context->debug, LOG_DATA,
		    "%s reawakened after zero-length read\n",
		    device->gpsdata.dev.path);
	device->reawake = (timestamp_t)0;
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

void gpsd_zero_satellites( /*@out@*/ struct gps_data_t *out)
{
    (void)memset(out->PRN, 0, sizeof(out->PRN));
    (void)memset(out->elevation, 0, sizeof(out->elevation));
    (void)memset(out->azimuth, 0, sizeof(out->azimuth));
    (void)memset(out->ss, 0, sizeof(out->ss));
    out->satellites_visible = 0;
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

void ntpshm_latch(struct gps_device_t *device, struct timedrift_t /*@out@*/*td)
/* latch the fact that we've saved a fix */
{
    double fix_time, integral, fractional;

#ifdef HAVE_CLOCK_GETTIME
    /*@i2@*/(void)clock_gettime(CLOCK_REALTIME, &td->clock);
#else
    struct timeval clock_tv;
    (void)gettimeofday(&clock_tv, NULL);
    TVTOTS(&td->clock, &clock_tv);
#endif /* HAVE_CLOCK_GETTIME */
    fix_time = device->newdata.time;
    /* assume zero when there's no offset method */
    if (device->device_type == NULL
	|| device->device_type->time_offset == NULL)
	fix_time += 0.0;
    else
	fix_time += device->device_type->time_offset(device);
    /* it's ugly but timestamp_t is double */
    fractional = modf(fix_time, &integral);
    /*@-type@*/ /* splint is confused about struct timespec */
    td->real.tv_sec = (time_t)integral;
    td->real.tv_nsec = (long)(fractional * 1e+9);
    /*@+type@*/
    device->last_fixtime.real = device->newdata.time;
#ifndef S_SPLINT_S
    device->last_fixtime.clock = td->clock.tv_sec + td->clock.tv_nsec / 1e9;
#endif /* S_SPLINT_S */
}

/* end */
