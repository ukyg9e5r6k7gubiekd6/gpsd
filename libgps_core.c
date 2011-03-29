/* libgps.c -- client interface library for the gpsd daemon
 *
 * This file is Copyright (c) 2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>

#include "gpsd.h"
#include "gps_json.h"

#ifdef LIBGPS_DEBUG
int libgps_debuglevel = 0;

static FILE *debugfp;

void gps_enable_debug(int level, FILE * fp)
/* control the level and destination of debug trace messages */
{
    libgps_debuglevel = level;
    debugfp = fp;
#ifdef CLIENTDEBUG_ENABLE
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

    libgps_debug_trace((DEBUG_CALLS, "gps_read()\n"));

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

    return status;
}

extern const char /*@observer@*/ *gps_errstr(const int err)
{
    /* 
     * We might add our own error codes in the future, e.g for
     * protocol compatibility checks
     */
#ifndef USE_QT
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
    /* will fail on a 32-bit macine */
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

#ifdef TESTMAIN
/*
 * A simple command-line exerciser for the library.
 * Not really useful for anything but debugging.
 */

#ifndef S_SPLINT_S
#include <unistd.h>
#endif /* S_SPLINT_S */
#include <getopt.h>
#include <signal.h>

static void onsig(int sig)
{
    (void)fprintf(stderr, "libgps: died with signal %d\n", sig);
    exit(1);
}

/* must start zeroed, otherwise the unit test will try to chase garbage pointer fields. */
static struct gps_data_t gpsdata;

int main(int argc, char *argv[])
{
    struct gps_data_t collect;
    char buf[BUFSIZ];
    int option;
    bool batchmode = false;
    int debug = 0;

    (void)signal(SIGSEGV, onsig);
    (void)signal(SIGBUS, onsig);

    while ((option = getopt(argc, argv, "bhsD:?")) != -1) {
	switch (option) {
	case 'b':
	    batchmode = true;
	    break;
	case 's':
	    (void)
		printf
		("Sizes: fix=%zd gpsdata=%zd rtcm2=%zd rtcm3=%zd ais=%zd compass=%zd raw=%zd devices=%zd policy=%zd version=%zd, noise=%zd\n",
		 sizeof(struct gps_fix_t),
		 sizeof(struct gps_data_t), sizeof(struct rtcm2_t),
		 sizeof(struct rtcm3_t), sizeof(struct ais_t),
		 sizeof(struct attitude_t), sizeof(struct rawdata_t),
		 sizeof(collect.devices), sizeof(struct policy_t),
		 sizeof(struct version_t), sizeof(struct gst_t));
	    exit(0);
	case 'D':
	    debug = atoi(optarg);
	    break;
	case '?':
	case 'h':
	default:
	    (void)fputs("usage: libgps [-b] [-d lvl] [-s]\n", stderr);
	    exit(1);
	}
    }

    gps_enable_debug(debug, stdout);
    if (batchmode) {
	while (fgets(buf, sizeof(buf), stdin) != NULL) {
	    if (buf[0] == '{' || isalpha(buf[0])) {
		gps_unpack(buf, &gpsdata);
		libgps_dump_state(&gpsdata);
	    }
	}
    } else if (gps_open(NULL, 0, &collect) <= 0) {
	(void)fputs("Daemon is not running.\n", stdout);
	exit(1);
    } else if (optind < argc) {
	(void)strlcpy(buf, argv[optind], BUFSIZ);
	(void)strlcat(buf, "\n", BUFSIZ);
	(void)gps_send(&collect, buf);
	(void)gps_read(&collect);
	libgps_dump_state(&collect);
	(void)gps_close(&collect);
    } else {
	int tty = isatty(0);

	if (tty)
	    (void)fputs("This is the gpsd exerciser.\n", stdout);
	for (;;) {
	    if (tty)
		(void)fputs("> ", stdout);
	    if (fgets(buf, sizeof(buf), stdin) == NULL) {
		if (tty)
		    putchar('\n');
		break;
	    }
	    collect.set = 0;
	    (void)gps_send(&collect, buf);
	    (void)gps_read(&collect);
	    libgps_dump_state(&collect);
	}
	(void)gps_close(&collect);
    }

    return 0;
}

/*@-nullderef@*/

#endif /* TESTMAIN */
