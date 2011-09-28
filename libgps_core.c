/* libgps_core.c -- client interface library for the gpsd daemon
 *
 * Core portion of client library.  Cals helpers to handle different eports.
 *
 * This file is Copyright (c) 2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>

#include "gpsd.h"
#include "libgps.h"
#include "gps_json.h"

/*
 * All privdata structures have export as a first member, 
 * and can have others that the individual method libraries
 * know about but this one doesn't.
 */
struct privdata_t
{
    enum export_t export;
};
#define PRIVATE(gpsdata) ((struct privdata_t *)gpsdata->privdata)

#ifdef LIBGPS_DEBUG
int libgps_debuglevel = 0;

static FILE *debugfp;

void gps_enable_debug(int level, FILE * fp)
/* control the level and destination of debug trace messages */
{
    libgps_debuglevel = level;
    debugfp = fp;
#if defined(CLIENTDEBUG_ENABLE) && defined(SOCKET_EXPORT_ENABLE)
    json_enable_debug(level - DEBUG_JSON, fp);
#endif
}

void libgps_trace(int errlevel, const char *fmt, ...)
/* assemble command in printf(3) style */
{
    if (errlevel <= libgps_debuglevel) {
	char buf[BUFSIZ];
	va_list ap;

	(void)strlcpy(buf, "libgps: ", BUFSIZ);
	va_start(ap, fmt);
	(void)vsnprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), fmt,
			ap);
	va_end(ap);

	(void)fputs(buf, debugfp);
    }
}
#endif /* LIBGPS_DEBUG */

#ifdef SOCKET_EXPORT_ENABLE
#define CONDITIONALLY_UNUSED
#else
#define CONDITIONALLY_UNUSED UNUSED
#endif /* SOCKET_EXPORT_ENABLE */

int gps_open(/*@null@*/const char *host, 
	     /*@null@*/const char *port CONDITIONALLY_UNUSED,
	     /*@out@*/ struct gps_data_t *gpsdata)
{
    int status = -1;

    /*@ -branchstate @*/
    if (!gpsdata)
	return -1;

#ifdef SHM_EXPORT_ENABLE
    if (host != NULL && strcmp(host, GPSD_SHARED_MEMORY) == 0) {
	status = gps_shm_open(gpsdata);
	if (status == -1)
	    status = SHM_NOSHARED;
	else if (status == -2)
	    status = SHM_NOATTACH;
    }
#endif /* SHM_EXPORT_ENABLE */

#ifdef SOCKET_EXPORT_ENABLE
    if (status == -1) {
        status = gps_sock_open(host, port, gpsdata);
    }
#endif /* SOCKET_EXPORT_ENABLE */

    gpsdata->set = 0;
    gpsdata->status = STATUS_NO_FIX;
    gps_clear_fix(&gpsdata->fix);

    return status;
    /*@ +branchstate @*/
}

int gps_close(struct gps_data_t *gpsdata)
/* close a gpsd connection */
{
    int status = -1;

    libgps_debug_trace((DEBUG_CALLS, "gps_close()\n"));

#ifdef SHM_EXPORT_ENABLE
    if ((intptr_t)(gpsdata->gps_fd) == -1) {
	gps_shm_close(gpsdata);
	status = 0;
    }
#endif /* SHM_EXPORT_ENABLE */

#ifdef SOCKET_EXPORT_ENABLE
    if (status == -1) {
        status = gps_sock_close(gpsdata);
    }
#endif /* SOCKET_EXPORT_ENABLE */

	return status;
}

int gps_read(struct gps_data_t *gpsdata)
/* read from a gpsd connection */
{
    int status = -1;

    libgps_debug_trace((DEBUG_CALLS, "gps_read() begins\n"));

    /*@ -usedef -compdef -uniondef @*/
#ifdef SHM_EXPORT_ENABLE
    if ((intptr_t)(gpsdata->gps_fd) == -1) {
	status = gps_shm_read(gpsdata);
    }
#endif /* SHM_EXPORT_ENABLE */

#ifdef SOCKET_EXPORT_ENABLE
    if (status == -1) {
        status = gps_sock_read(gpsdata);
    }
#endif /* SOCKET_EXPORT_ENABLE */
    /*@ +usedef +compdef +uniondef @*/

    libgps_debug_trace((DEBUG_CALLS, "gps_read() -> %d (%s)\n", 
			status, gps_maskdump(gpsdata->set)));

    return status;
}

int gps_send(struct gps_data_t *gpsdata CONDITIONALLY_UNUSED, const char *fmt CONDITIONALLY_UNUSED, ...)
/* send a command to the gpsd instance */
{
    int status = -1;
    char buf[BUFSIZ];
    va_list ap;

    va_start(ap, fmt);
    (void)vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
    va_end(ap);
    if (buf[strlen(buf) - 1] != '\n')
	(void)strlcat(buf, "\n", BUFSIZ);

#ifdef SOCKET_EXPORT_ENABLE
    status = gps_sock_send(gpsdata, buf);
#endif /* SOCKET_EXPORT_ENABLE */

    return status;
}

int gps_stream(struct gps_data_t *gpsdata CONDITIONALLY_UNUSED, 
	unsigned int flags CONDITIONALLY_UNUSED,
	/*@null@*/ void *d CONDITIONALLY_UNUSED)
{
    int status = -1;

#ifdef SOCKET_EXPORT_ENABLE
    status = gps_sock_stream(gpsdata, flags, d);
#endif /* SOCKET_EXPORT_ENABLE */

    return status;
}

const char /*@observer@*/ *gps_data(const struct gps_data_t *gpsdata CONDITIONALLY_UNUSED)
/* return the contents of the client data buffer */
{
    const char *bufp = NULL;

#ifdef SOCKET_EXPORT_ENABLE
    bufp = gps_sock_data(gpsdata);
#endif /* SOCKET_EXPORT_ENABLE */

    return bufp;
}

bool gps_waiting(const struct gps_data_t *gpsdata CONDITIONALLY_UNUSED, int timeout CONDITIONALLY_UNUSED)
/* is there input waiting from the GPS? */
{
    /* this is bogus, but I can't think of a better solution yet */
    bool waiting = true;

#ifdef SOCKET_EXPORT_ENABLE
    waiting = gps_sock_waiting(gpsdata, timeout);
#endif /* SOCKET_EXPORT_ENABLE */

    return waiting;
}

extern const char /*@observer@*/ *gps_errstr(const int err)
{
    /* 
     * We might add our own error codes in the future, e.g for
     * protocol compatibility checks
     */
#ifndef USE_QT
#ifdef SHM_EXPORT_ENABLE
    if (err == SHM_NOSHARED)
	return "no shared-memory segment or daemon not running";
    else if (err == SHM_NOATTACH)
	return "attach failed for unknown reason";
#endif /* SHM_EXPORT_ENABLE */
    return netlib_errstr(err);
#else
    static char buf[32];
    (void)snprintf(buf, sizeof(buf), "Qt error %d", err);
    return buf;
#endif
}

#ifdef LIBGPS_DEBUG
void libgps_dump_state(struct gps_data_t *collect)
{
    const char *status_values[] = { "NO_FIX", "FIX", "DGPS_FIX" };
    const char *mode_values[] = { "", "NO_FIX", "MODE_2D", "MODE_3D" };

    /* no need to dump the entire state, this is a sanity check */
#ifndef USE_QT
    /* will fail on a 32-bit machine */
    (void)fprintf(debugfp, "flags: (0x%04x) %s\n",
		  (unsigned int)collect->set, gps_maskdump(collect->set));
#endif
    if (collect->set & ONLINE_SET)
	(void)fprintf(debugfp, "ONLINE: %lf\n", collect->online);
    if (collect->set & TIME_SET)
	(void)fprintf(debugfp, "TIME: %lf\n", collect->fix.time);
    if (collect->set & LATLON_SET)
	(void)fprintf(debugfp, "LATLON: lat/lon: %lf %lf\n",
		      collect->fix.latitude, collect->fix.longitude);
    if (collect->set & ALTITUDE_SET)
	(void)fprintf(debugfp, "ALTITUDE: altitude: %lf  U: climb: %lf\n",
		      collect->fix.altitude, collect->fix.climb);
    if (collect->set & SPEED_SET)
	(void)fprintf(debugfp, "SPEED: %lf\n", collect->fix.speed);
    if (collect->set & TRACK_SET)
	(void)fprintf(debugfp, "TRACK: track: %lf\n", collect->fix.track);
    if (collect->set & CLIMB_SET)
	(void)fprintf(debugfp, "CLIMB: climb: %lf\n", collect->fix.climb);
    if (collect->set & STATUS_SET)
	(void)fprintf(debugfp, "STATUS: status: %d (%s)\n",
		      collect->status, status_values[collect->status]);
    if (collect->set & MODE_SET)
	(void)fprintf(debugfp, "MODE: mode: %d (%s)\n",
		      collect->fix.mode, mode_values[collect->fix.mode]);
    if (collect->set & DOP_SET)
	(void)fprintf(debugfp,
		      "DOP: satellites %d, pdop=%lf, hdop=%lf, vdop=%lf\n",
		      collect->satellites_used, collect->dop.pdop,
		      collect->dop.hdop, collect->dop.vdop);
    if (collect->set & VERSION_SET)
	(void)fprintf(debugfp, "VERSION: release=%s rev=%s proto=%d.%d\n",
		      collect->version.release,
		      collect->version.rev,
		      collect->version.proto_major,
		      collect->version.proto_minor);
    if (collect->set & POLICY_SET)
	(void)fprintf(debugfp,
		      "POLICY: watcher=%s nmea=%s raw=%d scaled=%s timing=%s, devpath=%s\n",
		      collect->policy.watcher ? "true" : "false",
		      collect->policy.nmea ? "true" : "false",
		      collect->policy.raw,
		      collect->policy.scaled ? "true" : "false",
		      collect->policy.timing ? "true" : "false",
		      collect->policy.devpath);
    if (collect->set & SATELLITE_SET) {
	int i;

	(void)fprintf(debugfp, "SKY: satellites in view: %d\n",
		      collect->satellites_visible);
	for (i = 0; i < collect->satellites_visible; i++) {
	    (void)fprintf(debugfp, "    %2.2d: %2.2d %3.3d %3.0f %c\n",
			  collect->PRN[i], collect->elevation[i],
			  collect->azimuth[i], collect->ss[i],
			  collect->used[i] ? 'Y' : 'N');
	}
    }
    if (collect->set & DEVICE_SET)
	(void)fprintf(debugfp, "DEVICE: Device is '%s', driver is '%s'\n",
		      collect->dev.path, collect->dev.driver);
#ifdef OLDSTYLE_ENABLE
    if (collect->set & DEVICEID_SET)
	(void)fprintf(debugfp, "GPSD ID is %s\n", collect->dev.subtype);
#endif /* OLDSTYLE_ENABLE */
    if (collect->set & DEVICELIST_SET) {
	int i;
	(void)fprintf(debugfp, "DEVICELIST:%d devices:\n",
		      collect->devices.ndevices);
	for (i = 0; i < collect->devices.ndevices; i++) {
	    (void)fprintf(debugfp, "%d: path='%s' driver='%s'\n",
			  collect->devices.ndevices,
			  collect->devices.list[i].path,
			  collect->devices.list[i].driver);
	}
    }

}
#endif /* LIBGPS_DEBUG */

// end
