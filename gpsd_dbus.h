/* $Id$ */
#ifndef _GPSD_DBUS_H_
#define _GPSD_DBUS_H_

#ifdef DBUS_ENABLE

#include <dbus/dbus.h>

#include "gpsd.h"

int initialize_dbus_connection (void);
void send_dbus_fix (struct gps_device_t* channel);

#endif

#endif /* _GPSD_DBUS_H_ */
