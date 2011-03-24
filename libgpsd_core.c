/* libgpsd_core.c -- direct access to GPSes on serial or USB devices.
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
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifndef S_SPLINT_S
#include <sys/wait.h>
#include <sys/socket.h>
#include <unistd.h>
#endif /* S_SPLINT_S */

#include "gpsd.h"

static void gpsd_run_device_hook(char *device_name, char *hook)
{
    struct stat statbuf;
    if (stat(DEVICEHOOKPATH, &statbuf) == -1)
	gpsd_report(LOG_PROG, "no %s present, skipped running hook\n",
	    DEVICEHOOKPATH); 
    else {
	char buf[PATH_MAX];
	int status;
	(void)snprintf(buf, sizeof(buf), "%s %s %s",
	    DEVICEHOOKPATH, device_name, hook);
	gpsd_report(LOG_INF, "running %s\n", buf);
	status = system(buf);
	if (status == -1)
	    gpsd_report(LOG_ERROR, "error running %s\n", buf);
	else
	    gpsd_report(LOG_INF, "%s returned %d\n", DEVICEHOOKPATH,
		WEXITSTATUS(status));
    }
}

int gpsd_switch_driver(struct gps_device_t *session, char *type_name)
{
    const struct gps_type_t **dp;
    bool identified = (session->device_type != NULL);

    gpsd_report(LOG_PROG, "switch_driver(%s) called...\n", type_name);
    if (identified && strcmp(session->device_type->type_name, type_name) == 0)
	return 0;

    /*@ -compmempass @*/
    for (dp = gpsd_drivers; *dp; dp++)
	if (strcmp((*dp)->type_name, type_name) == 0) {
	    gpsd_report(LOG_PROG, "selecting %s driver...\n",
			(*dp)->type_name);
	    gpsd_assert_sync(session);
	    /*@i@*/ session->device_type = *dp;
#ifdef ALLOW_RECONFIGURE
	    session->gpsdata.dev.mincycle = session->device_type->min_cycle;
#endif /* ALLOW_RECONFIGURE */
	    /* reconfiguration might be required */
	    if (identified && session->device_type->event_hook != NULL)
		session->device_type->event_hook(session,
						 event_driver_switch);
	    /* clients should be notified */
	    session->notify_clients = true;
	    return 1;
	}
    gpsd_report(LOG_ERROR, "invalid GPS type \"%s\".\n", type_name);
    return 0;
    /*@ +compmempass @*/
}

/*@-compdestroy@*/
void gps_context_init(struct gps_context_t *context)
{
    /* *INDENT-OFF* */
    /*@ -initallelements -nullassign -nullderef @*/
    struct gps_context_t nullcontext = {
	.valid	    = 0,
	.readonly	    = false,
	.fixcnt	    = 0,
	.rtcmbytes	    = 0,
	.rtcmbuf	    = {'\0'},
	.rtcmtime	    = 0,
	.start_time     = 0,
	.leap_seconds   = 0,
	.gps_week	    = 0,
	.gps_tow        = 0,
	.century	    = 0,
	.rollovers      = 0,
#ifdef NTPSHM_ENABLE
	.enable_ntpshm  = false,
	.shmTime	    = {0},
	.shmTimeInuse   = {0},
# ifdef PPS_ENABLE
	.shmTimePPS	    = false,
# endif /* PPS_ENABLE */
#endif /* NTPSHM_ENABLE */
    };
    /*@ +initallelements +nullassign +nullderef @*/
    /* *INDENT-ON* */
    (void)memcpy(context, &nullcontext, sizeof(struct gps_context_t));
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
    session->rtcmtime = (timestamp_t)0;
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
    session->gpsdata.dop.hdop = NAN;
    session->gpsdata.dop.vdop = NAN;
    session->gpsdata.dop.pdop = NAN;
    session->gpsdata.dop.tdop = NAN;
    session->gpsdata.dop.gdop = NAN;
    session->gpsdata.epe = NAN;
    session->mag_var = NAN;
    session->gpsdata.dev.cycle = session->gpsdata.dev.mincycle = 1;

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
    ntpd_link_deactivate(session);
#ifdef ALLOW_RECONFIGURE
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
#endif /* ALLOW_RECONFIGURE */
    gpsd_report(LOG_INF, "closing GPS=%s (%d)\n",
		session->gpsdata.dev.path, session->gpsdata.gps_fd);
    (void)gpsd_close(session);
    gpsd_run_device_hook(session->gpsdata.dev.path, "DEACTIVATE");
}

void gpsd_clear(struct gps_device_t *session)
{
    session->gpsdata.online = timestamp();
#ifdef SIRF_ENABLE
    session->driver.sirf.satcounter = 0;
#endif /* SIRF_ENABLE */
    packet_init(&session->packet);
    // session->gpsdata.online = 0;
    session->gpsdata.fix.mode = MODE_NOT_SEEN;
    session->gpsdata.status = STATUS_NO_FIX;
    session->gpsdata.fix.track = NAN;
    session->gpsdata.separation = NAN;
    session->mag_var = NAN;
    session->releasetime = (timestamp_t)0;
    session->getcount = 0;

    /* clear the private data union */
    memset(&session->driver, '\0', sizeof(session->driver));

    session->opentime = timestamp();
}

int gpsd_open(struct gps_device_t *session)
/* open a device for access to its data */
{
    /* special case: source may be a URI to a remote GNSS or DGPS service */
    if (netgnss_uri_check(session->gpsdata.dev.path)) {
	session->gpsdata.gps_fd = netgnss_uri_open(session,
						   session->gpsdata.dev.path);
	session->sourcetype = source_tcp;
	gpsd_report(LOG_SPIN,
		    "netgnss_uri_open(%s) returns socket on fd %d\n",
		    session->gpsdata.dev.path, session->gpsdata.gps_fd);
	return session->gpsdata.gps_fd;
    /* otherwise, could be an TCP data feed */
    } else if (strncmp(session->gpsdata.dev.path, "tcp://", 6) == 0) {
	char server[GPS_PATH_MAX], *port;
	socket_t dsock;
	(void)strlcpy(server, session->gpsdata.dev.path + 6, sizeof(server));
	session->gpsdata.gps_fd = -1;
	port = strchr(server, ':');
	if (port == NULL) {
	    gpsd_report(LOG_ERROR, "Missing colon in TCP feed spec.\n");
	    return -1;
	}
	*port++ = '\0';
	gpsd_report(LOG_INF, "opening TCP feed at %s, port %s.\n", server,
		    port);
	if ((dsock = netlib_connectsock(AF_UNSPEC, server, port, "tcp")) < 0) {
	    gpsd_report(LOG_ERROR, "TCP device open error %s.\n",
			netlib_errstr(dsock));
	    return -1;
	} else
	    gpsd_report(LOG_SPIN, "TCP device opened on fd %d\n", dsock);
	session->gpsdata.gps_fd = dsock;
	session->sourcetype = source_tcp;
	return session->gpsdata.gps_fd;
    /* or could be UDP */
    } else if (strncmp(session->gpsdata.dev.path, "udp://", 6) == 0) {
	char server[GPS_PATH_MAX], *port;
	socket_t dsock;
	(void)strlcpy(server, session->gpsdata.dev.path + 6, sizeof(server));
	session->gpsdata.gps_fd = -1;
	port = strchr(server, ':');
	if (port == NULL) {
	    gpsd_report(LOG_ERROR, "Missing colon in UDP feed spec.\n");
	    return -1;
	}
	*port++ = '\0';
	gpsd_report(LOG_INF, "opening UDP feed at %s, port %s.\n", server,
		    port);
	if ((dsock = netlib_connectsock(AF_UNSPEC, server, port, "udp")) < 0) {
	    gpsd_report(LOG_ERROR, "UDP device open error %s.\n",
			netlib_errstr(dsock));
	    return -1;
	} else
	    gpsd_report(LOG_SPIN, "UDP device opened on fd %d\n", dsock);
	session->gpsdata.gps_fd = dsock;
	session->sourcetype = source_udp;
	return session->gpsdata.gps_fd;
    }

    /* fall through to plain serial open */
    return gpsd_serial_open(session);
}

/*@ -branchstate @*/
int gpsd_activate(struct gps_device_t *session)
/* acquire a connection to the GPS device */
{
    gpsd_run_device_hook(session->gpsdata.dev.path, "ACTIVATE");
    session->gpsdata.gps_fd = gpsd_open(session);

    if (session->gpsdata.gps_fd < 0)
	return -1;
    else {
#ifdef NON_NMEA_ENABLE
	/* if it's a sensor, it must be probed */
	if (session->servicetype == service_sensor)  {
	    const struct gps_type_t **dp;

	    /*@ -mustfreeonly @*/
	    for (dp = gpsd_drivers; *dp; dp++) {
		(void)tcflush(session->gpsdata.gps_fd, TCIOFLUSH);  /* toss stale data */
		if ((*dp)->probe_detect != NULL
			&& (*dp)->probe_detect(session) != 0) {
		    gpsd_report(LOG_PROG, "probe found %s driver...\n",
			    (*dp)->type_name);
		    session->device_type = *dp;
		    gpsd_assert_sync(session);
		    goto foundit;
		}
	    }
	    /*@ +mustfreeonly @*/
	    gpsd_report(LOG_PROG, "no probe matched...\n");
	}
      foundit:
#endif /* NON_NMEA_ENABLE */
	gpsd_clear(session);
	gpsd_report(LOG_INF,
		    "gpsd_activate(): activated GPS (fd %d)\n",
		    session->gpsdata.gps_fd);
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

char /*@observer@*/ *gpsd_id( /*@in@ */ struct gps_device_t *session)
/* full ID of the device for reports, including subtype */
{
    static char buf[128];
    if ((session == NULL) || (session->device_type == NULL) ||
	(session->device_type->type_name == NULL))
	return "unknown,";
    (void)strlcpy(buf, session->device_type->type_name, sizeof(buf));
    if (session->subtype[0] != '\0') {
	(void)strlcat(buf, " ", sizeof(buf));
	(void)strlcat(buf, session->subtype, sizeof(buf));
    }
    return (buf);
}

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

void clear_dop( /*@out@*/ struct dop_t *dop)
{
    dop->xdop = dop->ydop = dop->vdop = dop->tdop = dop->hdop = dop->pdop =
	dop->gdop = NAN;
}

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

static gps_mask_t fill_dop(const struct gps_data_t * gpsdata, struct dop_t * dop)
{
    double prod[4][4];
    double inv[4][4];
    double satpos[MAXCHANNELS][4];
    double xdop, ydop, hdop, vdop, pdop, tdop, gdop;
    int i, j, k, n;

    memset(satpos, 0, sizeof(satpos));

#ifdef __UNUSED__
    gpsd_report(LOG_INF, "Satellite picture:\n");
    for (k = 0; k < MAXCHANNELS; k++) {
	if (gpsdata->used[k])
	    gpsd_report(LOG_INF, "az: %d el: %d  SV: %d\n",
			gpsdata->azimuth[k], gpsdata->elevation[k],
			gpsdata->used[k]);
    }
#endif /* __UNUSED__ */

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
	gpsd_report(LOG_DATA + 2, "Not enough Satellites available %d < 4:\n",
		    n);
	return 0;		/* Is this correct return code here? or should it be ERROR_IS */
    }

    memset(prod, 0, sizeof(prod));
    memset(inv, 0, sizeof(inv));

#ifdef __UNUSED__
    gpsd_report(LOG_INF, "Line-of-sight matrix:\n");
    for (k = 0; k < n; k++) {
	gpsd_report(LOG_INF, "%f %f %f %f\n",
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
    gpsd_report(LOG_INF, "product:\n");
    for (k = 0; k < 4; k++) {
	gpsd_report(LOG_INF, "%f %f %f %f\n",
		    prod[k][0], prod[k][1], prod[k][2], prod[k][3]);
    }
#endif /* __UNUSED__ */

    if (invert(prod, inv)) {
#ifdef __UNUSED__
	/*
	 * Note: this will print garbage unless all the subdeterminants
	 * are computed in the invert() function.
	 */
	gpsd_report(LOG_RAW, "inverse:\n");
	for (k = 0; k < 4; k++) {
	    gpsd_report(LOG_RAW, "%f %f %f %f\n",
			inv[k][0], inv[k][1], inv[k][2], inv[k][3]);
	}
#endif /* __UNUSED__ */
    } else {
#ifndef USE_QT
	gpsd_report(LOG_DATA,
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
    gpsd_report(LOG_DATA,
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

    return DOP_IS;
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
     */
    if (isnan(fix->time) == 0 && isnan(fix->ept) != 0)
	fix->ept = 0.005;
    /* Other error computations depend on having a valid fix */
    gpsd_report(LOG_DATA, "modeling errors: mode=%d, masks=%s\n",
		fix->mode, gpsd_maskdump(session->gpsdata.set));
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
	    if (oldfix->mode > MODE_3D && fix->mode > MODE_3D) {
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

gps_mask_t gpsd_poll(struct gps_device_t *session)
/* update the stuff in the scoreboard structure */
{
    ssize_t newlen;
    bool first_sync = false;

    gps_clear_fix(&session->newdata);

#ifdef TIMING_ENABLE
    if (session->packet.outbuflen == 0)
	session->d_xmit_time = timestamp();
#endif /* TIMING_ENABLE */

    if (session->packet.type >= COMMENT_PACKET) {
	/*@-shiftnegative@*/
	session->observed |= PACKET_TYPEMASK(session->packet.type);
	/*@+shiftnegative@*/
    }

    /* can we get a full packet from the device? */
    if (session->device_type) {
	newlen = session->device_type->get_packet(session);
	gpsd_report(LOG_RAW,
		    "%s is known to be %s\n",
		    session->gpsdata.dev.path,
		    session->device_type->type_name);
    } else {
	const struct gps_type_t **dp;

	newlen = generic_get(session);
	gpsd_report(LOG_RAW,
		    "packet sniff on %s finds type %d\n",
		    session->gpsdata.dev.path, session->packet.type);
	if (session->packet.type == COMMENT_PACKET) {
	    gpsd_report (LOG_PROG, "comment, sync lock deferred\n");
	} else if (session->packet.type > COMMENT_PACKET) {
	    first_sync = (session->device_type == NULL);
	    for (dp = gpsd_drivers; *dp; dp++)
		if (session->packet.type == (*dp)->packet_type) {
		    (void)gpsd_switch_driver(session, (*dp)->type_name);
		    break;
		}
	} else if (session->getcount++ > 1 && !gpsd_next_hunt_setting(session)) {
	    gpsd_run_device_hook(session->gpsdata.dev.path, "DEACTIVATE");
	    return ERROR_IS;
	}
    }

    /* update the scoreboard structure from the GPS */
    gpsd_report(LOG_RAW + 2, "%s sent %zd new characters\n",
		session->gpsdata.dev.path, newlen);
    if (newlen < 0) {		/* read error */
	gpsd_report(LOG_INF, "GPS on %s returned error %zd (%lf sec since data)\n",
		    session->gpsdata.dev.path, newlen,
		    timestamp() - session->gpsdata.online);
	session->gpsdata.online = (timestamp_t)0;
	return ERROR_IS;
    } else if (newlen == 0) {		/* zero length read, possible EOF */
	/*
	 * Multiplier is 2 to avoid edge effects due to sampling at the exact
	 * wrong time...
	 */
	if (session->gpsdata.online > 0 && timestamp() - session->gpsdata.online >= session->gpsdata.dev.cycle * 2) {
	    gpsd_report(LOG_INF, "GPS on %s is offline (%lf sec since data)\n",
			session->gpsdata.dev.path,
			timestamp() - session->gpsdata.online);
	    session->gpsdata.online = (timestamp_t)0;
	}
	return NODATA_IS;
    } else if (session->packet.outbuflen == 0) {	/* got new data, but no packet */
	gpsd_report(LOG_RAW + 3, "New data on %s, not yet a packet\n",
		    session->gpsdata.dev.path);
	return ONLINE_IS;
    } else {			/* we have recognized a packet */
	gps_mask_t received = PACKET_IS, dopmask = 0;
	session->gpsdata.online = timestamp();

	gpsd_report(LOG_RAW + 3, "Accepted packet on %s.\n",
		    session->gpsdata.dev.path);

#ifdef TIMING_ENABLE
	session->d_recv_time = timestamp();
#endif /* TIMING_ENABLE */

	/* track the packet count since achieving sync on the device */
	if (first_sync) {
	    speed_t speed = gpsd_get_speed(&session->ttyset);

	    /*@-nullderef@*/
	    gpsd_report(LOG_INF,
			"%s identified as type %s (%f sec @ %dbps)\n",
			session->gpsdata.dev.path,
			session->device_type->type_name,
			timestamp() - session->opentime,
			speed);
	    /*@+nullderef@*/
	    /* fire the identified hook */
	    if (session->device_type != NULL
		&& session->device_type->event_hook != NULL)
		session->device_type->event_hook(session, event_identified);
	    session->packet.counter = 0;
	} else
	    session->packet.counter++;

	/* fire the configure hook */
	if (session->device_type != NULL
	    && session->device_type->event_hook != NULL)
	    session->device_type->event_hook(session, event_configure);

	/*
	 * If this is the first time we've achieved sync on this
	 * device, or the driver type has changed for any other
	 * reason, that's a significant event that the caller needs to
	 * know about.
	 */
	if (first_sync || session->notify_clients) {
	    session->notify_clients = false;
	    received |= DRIVER_IS;
	}

	/* Get data from current packet into the fix structure */
	if (session->packet.type != COMMENT_PACKET)
	    if (session->device_type != NULL
		&& session->device_type->parse_packet != NULL)
		received |= session->device_type->parse_packet(session);

#ifdef NTPSHM_ENABLE
	/*
	 * Time is eligible for shipping to NTPD if the driver has
	 * asserted PPSTIME_IS at any point in the current cycle.
	 */
	if ((received & CLEAR_IS)!=0)
	    session->ship_to_ntpd = false;
	else if ((received & PPSTIME_IS)!=0)
	    session->ship_to_ntpd = true;
	/*
	 * Only update the NTP time if we've seen the leap-seconds data.
	 * Else we may be providing GPS time.
	 */
	if (session->context->enable_ntpshm == 0) {
	    //gpsd_report(LOG_PROG, "NTP: off\n");
	} else if ((received & TIME_IS) == 0) {
	    //gpsd_report(LOG_PROG, "NTP: No time this packet\n");
	} else if (isnan(session->newdata.time)) {
	    //gpsd_report(LOG_PROG, "NTP: bad new time\n");
	} else if (session->newdata.time == session->last_fixtime) {
	    //gpsd_report(LOG_PROG, "NTP: Not a new time\n");
	} else if (!session->ship_to_ntpd) {
	    //gpsd_report(LOG_PROG, "NTP: No precision time report\n");
	} else {
	    double offset;
	    //gpsd_report(LOG_PROG, "NTP: Got one\n");
	    /* assume zero when there's no offset method */
	    if (session->device_type == NULL
		|| session->device_type->ntp_offset == NULL)
		offset = 0.0;
	    else
		offset = session->device_type->ntp_offset(session);
	    (void)ntpshm_put(session, session->newdata.time, offset);
	    session->last_fixtime = session->newdata.time;
	}
#endif /* NTPSHM_ENABLE */

	/*
	 * Compute fix-quality data from the satellite positions.
	 * These will not overwrite any DOPs reported from the packet
	 * we just got.
	 */
	if ((received & SATELLITE_IS) != 0
	    && session->gpsdata.satellites_visible > 0) {
	    dopmask = fill_dop(&session->gpsdata, &session->gpsdata.dop);
	    session->gpsdata.epe = NAN;
	}
	session->gpsdata.set = ONLINE_IS | dopmask | received;

	/* copy/merge device data into staging buffers */
	/*@-nullderef -nullpass@*/
	if ((session->gpsdata.set & CLEAR_IS) != 0)
	    gps_clear_fix(&session->gpsdata.fix);
	/* don't downgrade mode if holding previous fix */
	if (session->gpsdata.fix.mode > session->newdata.mode)
	    session->gpsdata.set &= ~MODE_IS;
	//gpsd_report(LOG_PROG,
	//              "transfer mask on %s: %02x\n", session->gpsdata.tag, session->gpsdata.set);
	gps_merge_fix(&session->gpsdata.fix,
		      session->gpsdata.set, &session->newdata);
	gpsd_error_model(session, &session->gpsdata.fix, &session->oldfix);
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
	if ((session->gpsdata.set & LATLON_IS) != 0
	    && session->gpsdata.status > STATUS_NO_FIX) {
	    session->context->fixcnt++;
	    session->fixcnt++;
	}

#ifdef TIMING_ENABLE
	session->d_decode_time = timestamp();
#endif /* TIMING_ENABLE */

	/*
	 * Sanity check.  This catches a surprising number of port and
	 * driver errors, including 32-vs.-64-bit problems.
	 */
	/*@+relaxtypes +longunsignedintegral@*/
	if ((session->gpsdata.set & TIME_IS) != 0) {
	    if (session->newdata.time > time(NULL) + (60 * 60 * 24 * 365))
		gpsd_report(LOG_WARN,
			    "date more than a year in the future!\n");
	    else if (session->newdata.time < 0)
		gpsd_report(LOG_ERROR, "date is negative!\n");
	}
	/*@-relaxtypes -longunsignedintegral@*/

	return session->gpsdata.set;
    }
}

void gpsd_wrap(struct gps_device_t *session)
/* end-of-session wrapup */
{
    if (session->gpsdata.gps_fd != -1)
	gpsd_deactivate(session);
}

void gpsd_zero_satellites( /*@out@*/ struct gps_data_t *out)
{
    (void)memset(out->PRN, 0, sizeof(out->PRN));
    (void)memset(out->elevation, 0, sizeof(out->elevation));
    (void)memset(out->azimuth, 0, sizeof(out->azimuth));
    (void)memset(out->ss, 0, sizeof(out->ss));
    out->satellites_visible = 0;
    clear_dop(&out->dop);
}
