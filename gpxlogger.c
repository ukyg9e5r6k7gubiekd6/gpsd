/* $Id$ */
#include <sys/types.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <syslog.h>
#include <math.h>
#include <time.h>
#include <signal.h>
#include <getopt.h>
#include <errno.h>
#ifndef S_SPLINT_S
#include <unistd.h>
#endif /* S_SPLINT_S */

#include "gpsd_config.h"
#include "gps.h"
#include "gpsdclient.h"

/**************************************************************************
 *
 * Transport-layer-independent functions
 *
 **************************************************************************/

static char *author = "Amaury Jacquot, Chris Kuethe";
static char *license = "BSD";
static char *progname;

static time_t int_time, old_int_time;
static bool intrack = false;
static bool first = true;
static time_t timeout = 5; /* seconds */

static void print_gpx_header(void) 
{
    (void)printf("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
    (void)printf("<gpx version=\"1.1\" creator=\"navsys logger\"\n");
    (void)printf("        xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"\n");
    (void)printf("        xmlns=\"http://www.topografix.com/GPX/1.1\"\n");
    (void)printf("        xsi:schemaLocation=\"http://www.topografix.com/GPS/1/1\n");
    (void)printf("        http://www.topografix.com/GPX/1/1/gpx.xsd\">\n");
    (void)printf(" <metadata>\n");
    (void)printf("  <name>NavSys GPS logger dump</name>\n");
    (void)printf("  <author>%s</author>\n", author);
    (void)printf("  <copyright>%s</copyright>\n", license);
    (void)printf(" </metadata>\n");
    (void)fflush(stdout);
}

static void print_gpx_trk_end (void) 
{
    (void)printf("  </trkseg>\n");
    (void)printf(" </trk>\n");
    (void)fflush(stdout);
}

static void print_gpx_footer (void) 
{
    if (intrack)
	print_gpx_trk_end();
    (void)printf("</gpx>\n");
    (void)fclose(stdout);
}

static void print_gpx_trk_start (void)
{
    (void)printf(" <trk>\n");
    (void)printf("  <trkseg>\n");
    (void)fflush(stdout);
}

static void print_fix(struct gps_fix_t *fix, struct tm *time)
{
    (void)printf("   <trkpt lat=\"%f\" lon=\"%f\">\n", 
		 fix->latitude, fix->longitude);
    (void)printf("    <ele>%f</ele>\n",
		 fix->altitude);
    (void)printf("    <time>%04d-%02d-%02dT%02d:%02d:%02dZ</time>\n",
		 time->tm_year+1900, time->tm_mon+1, time->tm_mday,
		 time->tm_hour, time->tm_min, time->tm_sec);
    if (fix->mode==MODE_NO_FIX)
	(void)fprintf (stdout, "    <fix>none</fix>\n");
    else
	(void)fprintf (stdout, "    <fix>%dd</fix>\n", fix->mode);
#if 0
    /*
     * Can't print this more detailed report because in D-Bus mode
     * we don't necessarily have access to some of the stuff in gsdata.
     * Might mean some of this stuff should be promoted.
     */
    if ((gpsdata->status >= 2) && (gpsdata->fix.mode >= MODE_3D)){
	/* dgps or pps */
	if (gpsdata->fix.mode == 4) { /* military pps */
	    (void)printf("        <fix>pps</fix>\n");
	} else { /* civilian dgps or sbas */
	    (void)printf("        <fix>dgps</fix>\n");
	}
    } else { /* no dgps or pps */
	if (gpsdata->fix.mode == MODE_3D) {
	    (void)printf("        <fix>3d</fix>\n");
	} else if (gpsdata->fix.mode == MODE_2D) {
	    (void)printf("        <fix>2d</fix>\n");
	} else if (gpsdata->fix.mode == MODE_NOFIX) {
	    (void)printf("        <fix>none</fix>\n");
	} /* don't print anything if no fix indicator */
    }

    /* print # satellites used in fix, if reasonable to do so */
    if (gpsdata->fix.mode >= MODE_2D) {
	(void)printf("        <hdop>%.1f</hdop>\n", gpsdata->hdop);
	(void)printf("        <sat>%d</sat>\n", gpsdata->satellites_used);
    }
#endif

    (void)printf("   </trkpt>\n");
    (void)fflush (stdout);
}

static void conditionally_log_fix(struct gps_fix_t *gpsfix)
{
    int_time = floor(gpsfix->time);
    if ((int_time != old_int_time) && gpsfix->mode >= MODE_2D) {
	struct tm 	time;
	/* 
	 * Make new track if the jump in time is above
	 * timeout.  Handle jumps both forward and
	 * backwards in time.  The clock sometimes jumps
	 * backward when gpsd is submitting junk on the
	 * dbus.
	 */
	if (fabs(int_time - old_int_time) > timeout && !first) {
	    print_gpx_trk_end();
	    intrack = false;
	}

	if (!intrack) {
	    print_gpx_trk_start();
	    intrack = true;
	    if (first)
		first = false;
	}
		
	old_int_time = int_time;
	gmtime_r(&(int_time), &time);
	print_fix(gpsfix, &time);
    }
}

static void quit_handler (int signum) 
{
    /* don't clutter the logs on Ctrl-C */
    if (signum != SIGINT)
	syslog(LOG_INFO, "exiting, signal %d received", signum);
    print_gpx_footer ();
    exit(0);
}

static struct gps_fix_t gpsfix;

#ifdef DBUS_ENABLE
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

DBusConnection* connection;

static char gpsd_devname[BUFSIZ];

static DBusHandlerResult handle_gps_fix (DBusMessage* message)
{
    DBusError	error;

    dbus_error_init (&error);

    dbus_message_get_args (message,
			   &error,
			   DBUS_TYPE_DOUBLE, &gpsfix.time,
			   DBUS_TYPE_INT32,  &gpsfix.mode,
			   DBUS_TYPE_DOUBLE, &gpsfix.ept,
			   DBUS_TYPE_DOUBLE, &gpsfix.latitude,
			   DBUS_TYPE_DOUBLE, &gpsfix.longitude,
			   DBUS_TYPE_DOUBLE, &gpsfix.eph,
			   DBUS_TYPE_DOUBLE, &gpsfix.altitude,
			   DBUS_TYPE_DOUBLE, &gpsfix.epv,
			   DBUS_TYPE_DOUBLE, &gpsfix.track,
			   DBUS_TYPE_DOUBLE, &gpsfix.epd,
			   DBUS_TYPE_DOUBLE, &gpsfix.speed,
			   DBUS_TYPE_DOUBLE, &gpsfix.eps,
			   DBUS_TYPE_DOUBLE, &gpsfix.climb,
			   DBUS_TYPE_DOUBLE, &gpsfix.epc,
			   DBUS_TYPE_STRING, &gpsd_devname,
			   DBUS_TYPE_INVALID);

    conditionally_log_fix(&gpsfix);
    return DBUS_HANDLER_RESULT_HANDLED;
}

/*
 * Message dispatching function
 *
 */
static DBusHandlerResult signal_handler (
		DBusConnection* connection, DBusMessage* message)
{
    /* dummy, need to use the variable for some reason */
    connection = NULL;
	
    if (dbus_message_is_signal (message, "org.gpsd", "fix")) 
	return handle_gps_fix (message);
    /*
     * ignore all other messages
     */
	
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static int dbus_mainloop(void)
{
    GMainLoop* mainloop;
    DBusError error;

    mainloop = g_main_loop_new (NULL, FALSE);

    dbus_error_init (&error);
    connection = dbus_bus_get (DBUS_BUS_SYSTEM, &error);
    if (dbus_error_is_set (&error)) {
	syslog (LOG_CRIT, "%s: %s", error.name, error.message);
	return 3;
    }
	
    dbus_bus_add_match (connection, "type='signal'", &error);
    if (dbus_error_is_set (&error)) {
	syslog (LOG_CRIT, "unable to add match for signals %s: %s", error.name, error.message);
	return 4;
    }

    if (!dbus_connection_add_filter (connection, (DBusHandleMessageFunction)signal_handler, NULL, NULL)) {
	syslog (LOG_CRIT, "unable to register filter with the connection");
	return 5;
    }
	
    dbus_connection_setup_with_g_main (connection, NULL);

    g_main_loop_run (mainloop);
    return 0;
}

#endif /* DBUS_ENABLE */

/**************************************************************************
 *
 * Doing it with sockets
 *
 **************************************************************************/

struct fixsource_t source;

static void process(struct gps_data_t *gpsdata,
	     char *buf UNUSED, size_t len UNUSED)
{
    /* this is where we implement source-device filtering */
    if (gpsdata->dev.path[0] && source.device!=NULL && strcmp(source.device, gpsdata->dev.path) != 0)
	return;

    conditionally_log_fix(&gpsdata->fix);
}

static int socket_mainloop(void)
{
    fd_set fds;
    struct gps_data_t *gpsdata;

    gpsdata = gps_open(source.server, source.port);
    if (!gpsdata) {
	fprintf(stderr,
		"%s: no gpsd running or network error: %d, %s\n",
		progname, errno, gps_errstr(errno));
	exit(1);
    }

    gps_set_raw_hook(gpsdata, process);
    gps_stream(gpsdata, WATCH_ENABLE);

    for(;;){
	int data;
	struct timeval tv;

	FD_ZERO(&fds);
	FD_SET(gpsdata->gps_fd, &fds);

	tv.tv_usec = 250000;
	tv.tv_sec = 0;
	data = select(gpsdata->gps_fd + 1, &fds, NULL, NULL, &tv);

	if (data == -1) {
	    (void)fprintf(stderr,"%s\n", strerror(errno));
	    break;
	}
	else if (data)
	    gps_poll(gpsdata);
    }
    return 0;
}

/**************************************************************************
 *
 * Main sequence
 *
 **************************************************************************/

static void usage(void) 
{
    fprintf(stderr,
	    "Usage: %s [-V] [-h] [-i timeout] [-j casoc] [server[:port:[device]]]\n",
	    progname);
    fprintf(stderr,
	    "\tdefaults to '%s -i 5 -j 0 127.0.0.1:2947'\n",
	    progname);
    exit(1);
}

int main (int argc, char** argv) 
{
    int ch;

    progname = argv[0];
    while ((ch = getopt(argc, argv, "hi:V")) != -1){
	switch (ch) {
	case 'i':		/* set polling interfal */
	    timeout = (unsigned int)atoi(optarg);
	    if (timeout < 1)
		timeout = 1;
	    if (timeout >= 3600)
		fprintf(stderr,
			"WARNING: track timeout is an hour or more!\n");
	    break;
	case 'V':
	    (void)fprintf(stderr, "SVN ID: $Id$ \n");
	    exit(0);
	default:
 	    usage();
	    /* NOTREACHED */
	}
    }

    if (optind < argc) {
	gpsd_source_spec(argv[optind], &source);
    } else
	gpsd_source_spec(NULL, &source);
#if 0
    (void)printf("<!-- server: %s port: %s  device: %s -->\n",
		 source.server, source.port, source.device);
#endif

    /* initializes the gpsfix data structure */
    gps_clear_fix(&gpsfix);

    /* catch all interesting signals */
    signal (SIGTERM, quit_handler);
    signal (SIGQUIT, quit_handler);
    signal (SIGINT, quit_handler);

    //openlog ("gpxlogger", LOG_PID | LOG_NDELAY , LOG_DAEMON);
    //syslog (LOG_INFO, "---------- STARTED ----------");
	
    print_gpx_header ();

#ifdef DBUS_ENABLE
    /* To force socket use in the default way just give a '127.0.0.1' arg */  
    if (optind < argc)
	return socket_mainloop();
    else
	return dbus_mainloop();
#else
    return socket_mainloop();
#endif
}
