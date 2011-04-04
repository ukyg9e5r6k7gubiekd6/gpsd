/*
 * This file is Copyright (c) 2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 */
#ifndef _GPSD_DBUS_H_
#define _GPSD_DBUS_H_

#if defined(DBUS_EXPORT_ENABLE) && !defined(S_SPLINT_S)

#include <dbus/dbus.h>

#include "gpsd.h"

int initialize_dbus_connection (void);
void send_dbus_fix (struct gps_device_t* channel);

#endif /* defined(DBUS_EXPORT_ENABLE) && !defined(S_SPLINT_S) */

#endif /* _GPSD_DBUS_H_ */
