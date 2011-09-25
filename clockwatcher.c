/*
 * This file is Copyright (c) 2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <syslog.h>
#include <math.h>
#include <time.h>
#include <errno.h>
#include <libgen.h>
#include <signal.h>
#ifndef S_SPLINT_S
#include <unistd.h>
#endif /* S_SPLINT_S */

#include "gps.h"
#include "gpsd_config.h"
#include "gpsdclient.h"
#include "revision.h"

#define NITEMS(x) (int)(sizeof(x)/sizeof(x[0])) /* from gpsd.h-tail */

#ifdef S_SPLINT_S
extern struct tm *gmtime_r(const time_t *, /*@out@*/ struct tm *tp);
#endif /* S_SPLINT_S */

static char *progname;
static struct fixsource_t source;

/**************************************************************************
 *
 * Transport-layer-independent functions
 *
 **************************************************************************/

static struct gps_data_t gpsdata;
static FILE *logfile;
static time_t timeout;
#ifdef CLIENTDEBUG_ENABLE
static int debug;
#endif /* CLIENTDEBUG_ENABLE */

static void conditionally_log_fix(struct gps_data_t *gpsdata UNUSED)
{
    /* time logging goes here */
}

static void quit_handler(int signum)
{
    /* don't clutter the logs on Ctrl-C */
    if (signum != SIGINT)
	syslog(LOG_INFO, "exiting, signal %d received", signum);
    (void)gps_close(&gpsdata);
    exit(0);
}

#if defined(DBUS_EXPORT_ENABLE) && !defined(S_SPLINT_S)
/**************************************************************************
 *
 * Doing it with D-Bus
 *
 **************************************************************************/

#include <glib.h>
#include <dbus/dbus.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <dbus/dbus-glib.h>

#include <glib/gprintf.h>

DBusConnection *connection;

static char gpsd_devname[BUFSIZ];

static DBusHandlerResult handle_gps_fix(DBusMessage * message)
{
    DBusError error;
    /* this packet format was designed before we split eph */
    double eph;

    dbus_error_init(&error);

    dbus_message_get_args(message,
			  &error,
			  DBUS_TYPE_DOUBLE, &gpsdata.fix.time,
			  DBUS_TYPE_INT32, &gpsdata.fix.mode,
			  DBUS_TYPE_DOUBLE, &gpsdata.fix.ept,
			  DBUS_TYPE_DOUBLE, &gpsdata.fix.latitude,
			  DBUS_TYPE_DOUBLE, &gpsdata.fix.longitude,
			  DBUS_TYPE_DOUBLE, &eph,
			  DBUS_TYPE_DOUBLE, &gpsdata.fix.altitude,
			  DBUS_TYPE_DOUBLE, &gpsdata.fix.epv,
			  DBUS_TYPE_DOUBLE, &gpsdata.fix.track,
			  DBUS_TYPE_DOUBLE, &gpsdata.fix.epd,
			  DBUS_TYPE_DOUBLE, &gpsdata.fix.speed,
			  DBUS_TYPE_DOUBLE, &gpsdata.fix.eps,
			  DBUS_TYPE_DOUBLE, &gpsdata.fix.climb,
			  DBUS_TYPE_DOUBLE, &gpsdata.fix.epc,
			  DBUS_TYPE_STRING, &gpsd_devname, DBUS_TYPE_INVALID);

    if (gpsdata.fix.mode > MODE_NO_FIX )
	gpsdata.status = STATUS_FIX;
    else
	gpsdata.status = STATUS_NO_FIX;

    conditionally_log_fix(&gpsdata);
    return DBUS_HANDLER_RESULT_HANDLED;
}

/*
 * Message dispatching function
 *
 */
static DBusHandlerResult signal_handler(DBusConnection * connection,
					DBusMessage * message)
{
    /* dummy, need to use the variable for some reason */
    connection = NULL;

    if (dbus_message_is_signal(message, "org.gpsd", "fix"))
	return handle_gps_fix(message);
    /*
     * ignore all other messages
     */

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static int dbus_mainloop(void)
{
    GMainLoop *mainloop;
    DBusError error;

    mainloop = g_main_loop_new(NULL, FALSE);

    dbus_error_init(&error);
    connection = dbus_bus_get(DBUS_BUS_SYSTEM, &error);
    if (dbus_error_is_set(&error)) {
	syslog(LOG_CRIT, "%s: %s", error.name, error.message);
	return 3;
    }

    dbus_bus_add_match(connection, "type='signal'", &error);
    if (dbus_error_is_set(&error)) {
	syslog(LOG_CRIT, "unable to add match for signals %s: %s", error.name,
	       error.message);
	return 4;
    }

    if (!dbus_connection_add_filter
	(connection, (DBusHandleMessageFunction) signal_handler, NULL,
	 NULL)) {
	syslog(LOG_CRIT, "unable to register filter with the connection");
	return 5;
    }

    dbus_connection_setup_with_g_main(connection, NULL);

    g_main_loop_run(mainloop);
    return 0;
}

#endif /* defined(DBUS_EXPORT_ENABLE) && !defined(S_SPLINT_S) */

#ifdef SOCKET_EXPORT_ENABLE
/**************************************************************************
 *
 * Doing it with sockets
 *
 **************************************************************************/

/*@-mustfreefresh -compdestroy@*/
static int socket_mainloop(void)
{
    unsigned int flags = WATCH_ENABLE;

    if (gps_open(source.server, source.port, &gpsdata) != 0) {
	(void)fprintf(stderr,
		      "%s: no gpsd running or network error: %d, %s\n",
		      progname, errno, gps_errstr(errno));
	exit(1);
    }

    if (source.device != NULL)
	flags |= WATCH_DEVICE;
    (void)gps_stream(&gpsdata, flags, source.device);

    for (;;) {
	if (!gps_waiting(&gpsdata, 5000000)) {
	    (void)fprintf(stderr, "%s: error while waiting\n", progname);
	    break;
	} else {
	    (void)gps_read(&gpsdata);
	    conditionally_log_fix(&gpsdata);
	}
    }
    (void)gps_close(&gpsdata);
    return 0;
}
/*@+mustfreefresh +compdestroy@*/
#endif /* SOCKET_EXPORT_ENABLE */

#ifdef SHM_EXPORT_ENABLE
/**************************************************************************
 *
 * Doing it with shared memory
 *
 **************************************************************************/

/*@-mustfreefresh -compdestroy@*/
static int shm_mainloop(void)
{
    int status;
    if ((status = gps_open(GPSD_SHARED_MEMORY, NULL, &gpsdata)) != 0) {
	(void)fprintf(stderr,
		      "%s: shm open failed with status %d.\n",
		      progname, status);
	return 1;
    }

    for (;;) {
	status = gps_read(&gpsdata);

	if (status == -1)
	    break;
	if (status > 0)
	    conditionally_log_fix(&gpsdata);
    }
    (void)gps_close(&gpsdata);
    return 0;
}

/*@+mustfreefresh +compdestroy@*/
#endif /* SHM_EXPORT_ENABLE */

/**************************************************************************
 *
 * Main sequence
 *
 **************************************************************************/

struct method_t
{
    const char *name;
    int (*method)(void);
    const char *description;
};

static struct method_t methods[] = {
#if defined(DBUS_EXPORT_ENABLE) && !defined(S_SPLINT_S)
    {"dbus", dbus_mainloop, "DBUS broadcast"},
#endif /* defined(DBUS_EXPORT_ENABLE) && !defined(S_SPLINT_S) */
#ifdef SHM_EXPORT_ENABLE
    {"shm", shm_mainloop, "shared memory"},
#endif /* SOCKET_EXPORT_ENABLE */
#ifdef SOCKET_EXPORT_ENABLE
    {"sockets", socket_mainloop, "JSON via sockets"},
#endif /* SOCKET_EXPORT_ENABLE */
};

static void usage(void)
{
    fprintf(stderr,
	    "Usage: %s [-V] [-h] [-d] [-i timeout] [-f filename]\n"
	    "\t[-e exportmethod] [server[:port:[device]]]\n\n"
	    "defaults to '%s -i 5 -e %s localhost:2947'\n",
	    progname, progname, (NITEMS(methods) > 0) ? methods[0].name : "(none)");
    exit(1);
}

/*@-mustfreefresh -globstate@*/
int main(int argc, char **argv)
{
    int ch;
    bool daemonize = false;
    struct method_t *mp, *method = NULL;

    progname = argv[0];

    logfile = stdout;
    while ((ch = getopt(argc, argv, "dD:e:f:hi:lV")) != -1) {
	switch (ch) {
	case 'd':
	    openlog(basename(progname), LOG_PID | LOG_PERROR, LOG_DAEMON);
	    daemonize = true;
	    break;
#ifdef CLIENTDEBUG_ENABLE
	case 'D':
	    debug = atoi(optarg);
	    gps_enable_debug(debug, logfile);
	    break;
#endif /* CLIENTDEBUG_ENABLE */
	case 'e':
	    for (mp = methods;
		 mp < methods + NITEMS(methods);
		 mp++)
		if (strcmp(mp->name, optarg) == 0)
		    method = mp;
	    if (method == NULL) {
		(void)fprintf(stderr,
			      "%s: %s is not a known export method.\n",
			      progname, optarg);
		exit(1);
	    }
	    break;
       case 'f':       /* Output file name. */
            {
                char    fname[PATH_MAX];
                time_t  t;
                size_t  s;

                t = time(NULL);
                s = strftime(fname, sizeof(fname), optarg, localtime(&t));
                if (s == 0) {
                        syslog(LOG_ERR,
                            "Bad template \"%s\", logging to stdout.", optarg);
                        break;
                }
                logfile = fopen(fname, "w");
                if (logfile == NULL)
                        syslog(LOG_ERR,
                            "Failed to open %s: %s, logging to stdout.",
                            fname, strerror(errno));
                break;
            }
	case 'i':		/* set polling interfal */
	    timeout = (time_t) atoi(optarg);
	    if (timeout < 1)
		timeout = 1;
	    if (timeout >= 3600)
		fprintf(stderr,
			"WARNING: track timeout is an hour or more!\n");
	    break;
	case 'l':
	    for (method = methods;
		 method < methods + NITEMS(methods);
		 method++)
		(void)printf("%s: %s\n", method->name, method->description);
	    exit(0);
	case 'V':
	    (void)fprintf(stderr, "%s revision " REVISION "\n", progname);
	    exit(0);
	default:
	    usage();
	    /* NOTREACHED */
	}
    }

    if (daemonize && logfile == stdout) {
	syslog(LOG_ERR, "Daemon mode with no valid logfile name - exiting.");
	exit(1);
    }

    if (optind < argc) {
	gpsd_source_spec(argv[optind], &source);
    } else
	gpsd_source_spec(NULL, &source);
#if 0
    (void)fprintf(logfile,"<!-- server: %s port: %s  device: %s -->\n",
		 source.server, source.port, source.device);
#endif

    /* initializes the some gpsdata data structure */
    gpsdata.status = STATUS_NO_FIX;
    gpsdata.satellites_used = 0;
    gps_clear_fix(&(gpsdata.fix));
    gps_clear_dop(&(gpsdata.dop));

    /* catch all interesting signals */
    (void)signal(SIGTERM, quit_handler);
    (void)signal(SIGQUIT, quit_handler);
    (void)signal(SIGINT, quit_handler);

    /*@-unrecog@*/
    /* might be time to daemonize */
    if (daemonize) {
	/* not SuS/POSIX portable, but we have our own fallback version */
	if (daemon(0, 0) != 0)
	    (void) fprintf(stderr,"demonization failed: %s\n", strerror(errno));
    }
    /*@+unrecog@*/

    //syslog (LOG_INFO, "---------- STARTED ----------");

    if (method != NULL) {
	exit((*method->method)());
    } else if (NITEMS(methods)) {
	exit((methods[0].method)());
    } else {
	(void)fprintf(stderr, "%s: no export methods.\n", progname);
	exit(1);
    }
}
/*@+mustfreefresh +globstate@*/
