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

#if defined(DBUS_EXPORT_ENABLE) && !defined(S_SPLINT_S)

struct privdata_t
{
    void (*handler)(struct gps_data_t *);
};
#define PRIVATE(gpsdata) ((struct privdata_t *)(gpsdata)->privdata)

#include <glib.h>
#include <dbus/dbus.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <dbus/dbus-glib.h>

#include <glib/gprintf.h>

/*
 * Unpleasant that we have to declare a static context pointer here - means
 * you can't have multiple DBUS sessions open (not that this matters 
 * much in practice). The problem is the DBUS API lacks some hook
 * arguments that it ought to have.
 */
static struct gps_data_t *share_gpsdata;
static DBusConnection *connection;
static char gpsd_devname[BUFSIZ];

static DBusHandlerResult handle_gps_fix(DBusMessage * message)
{
    DBusError error;
    /* this packet format was designed before we split eph */
    double eph;

    dbus_error_init(&error);

    dbus_message_get_args(message,
			  &error,
			  DBUS_TYPE_DOUBLE, &share_gpsdata->fix.time,
			  DBUS_TYPE_INT32, &share_gpsdata->fix.mode,
			  DBUS_TYPE_DOUBLE, &share_gpsdata->fix.ept,
			  DBUS_TYPE_DOUBLE, &share_gpsdata->fix.latitude,
			  DBUS_TYPE_DOUBLE, &share_gpsdata->fix.longitude,
			  DBUS_TYPE_DOUBLE, &eph,
			  DBUS_TYPE_DOUBLE, &share_gpsdata->fix.altitude,
			  DBUS_TYPE_DOUBLE, &share_gpsdata->fix.epv,
			  DBUS_TYPE_DOUBLE, &share_gpsdata->fix.track,
			  DBUS_TYPE_DOUBLE, &share_gpsdata->fix.epd,
			  DBUS_TYPE_DOUBLE, &share_gpsdata->fix.speed,
			  DBUS_TYPE_DOUBLE, &share_gpsdata->fix.eps,
			  DBUS_TYPE_DOUBLE, &share_gpsdata->fix.climb,
			  DBUS_TYPE_DOUBLE, &share_gpsdata->fix.epc,
			  DBUS_TYPE_STRING, &gpsd_devname, DBUS_TYPE_INVALID);

    if (share_gpsdata->fix.mode > MODE_NO_FIX )
	share_gpsdata->status = STATUS_FIX;
    else
	share_gpsdata->status = STATUS_NO_FIX;

    PRIVATE(share_gpsdata)->handler(share_gpsdata);
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

int gps_dbus_open(struct gps_data_t *gpsdata)
{
    DBusError error;

    gpsdata->privdata = (void *)malloc(sizeof(struct privdata_t));
    if (gpsdata->privdata == NULL)
	return -1;

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

    share_gpsdata = gpsdata;
    return 0;
}

int gps_dbus_mainloop(struct gps_data_t *gpsdata,
		       int timeout UNUSED,
		       void (*hook)(struct gps_data_t *))
/* run a DBUS main loop with a specified handler */
{
    GMainLoop *mainloop;

    share_gpsdata = gpsdata;
    PRIVATE(share_gpsdata)->handler = (void (*)(struct gps_data_t *))hook;
    mainloop = g_main_loop_new(NULL, FALSE);
    dbus_connection_setup_with_g_main(connection, NULL);
    g_main_loop_run(mainloop);
    return 0;
}

#endif /* defined(DBUS_EXPORT_ENABLE) && !defined(S_SPLINT_S) */
