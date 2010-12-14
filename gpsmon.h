/* gpsmon.h -- what monitor capabuilities look like
 *
 * By Eric S. Raymond, 2009
 * This file is Copyright (c) 2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 */

#ifndef _GPSD_GPSMON_H_
#define _GPSD_GPSMON_H_

#include "gpsd_config.h"
#ifdef HAVE_NCURSES_H
#include <ncurses.h>
#else
#include <curses.h>
#endif /* HAVE_NCURSES_H */

#define COMMAND_TERMINATE	-1
#define COMMAND_MATCH		1
#define COMMAND_UNKNOWN		0

struct monitor_object_t {
    /* a device-specific capability table for the monitor */
    bool (*initialize)(void);		/* paint legends on windows */
    void (*update)(void);		/* now paint the data */
    int (*command)(char[]);		/* interpret device-specific commands */
    void (*wrap)(void);			/* deallocate storage */
    int min_y, min_x;			/* space required for device info */
    const struct gps_type_t *driver;	/* device driver table */
};

// Device-specific may need these.
extern bool monitor_control_send(unsigned char *buf, size_t len);
extern void monitor_fixframe(WINDOW *win);
extern void monitor_log(const char *fmt, ...);
extern void monitor_complain(const char *fmt, ...);

#define BUFLEN		2048

extern WINDOW *devicewin;
extern struct gps_device_t	session;
extern int gmt_offset;

#endif /* _GPSD_GPSMON_H_ */
/* gpsmon.h ends here */
