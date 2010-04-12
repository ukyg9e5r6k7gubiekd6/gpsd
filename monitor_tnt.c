/*
 * monitor_tnt.c - gpsmon support for True North Revolution devices.
 *
 * This file is Copyright (c) 2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#ifndef S_SPLINT_S
#include <unistd.h>
#endif /* S_SPLINT_S */
#include <stdarg.h>
#include <stdbool.h>
#include <assert.h>

#include "gpsd_config.h"

#ifdef HAVE_NCURSES_H
#include <ncurses.h>
#else
#include <curses.h>
#endif /* HAVE_NCURSES_H */
#include "gpsd.h"

#include "bits.h"
#include "gpsmon.h"

#ifdef TNT_ENABLE
extern const struct gps_type_t trueNorth;

static WINDOW *thtmwin;

static bool tnt_initialize(void)
{
    /*@ -onlytrans @*/
    thtmwin  = derwin(devicewin, 3, 40, 0, 0);
    (void)wborder(thtmwin, 0, 0, 0, 0, 0, 0, 0, 0),
    (void)syncok(thtmwin, true);
    (void)wattrset(thtmwin, A_BOLD);
    (void)mvwprintw(thtmwin, 1,  1, "Heading:          ");
    (void)mvwprintw(thtmwin, 2,  1, "Pitch:            ");
    (void)mvwprintw(thtmwin, 3,  1, "Roll:             ");
    (void)mvwprintw(thtmwin, 4,  1, "Dip:              ");
    (void)mvwprintw(thtmwin, 5,  1, "Mag Field Len:    ");
    (void)mvwprintw(thtmwin, 6,  1, "Mag Field X:      ");
    (void)mvwprintw(thtmwin, 7,  1, "Mag Field Y:      ");
    (void)mvwprintw(thtmwin, 8,  1, "Mag Field Z:      ");
    (void)mvwprintw(thtmwin, 9,  1, "Acceleration Len: ");
    (void)mvwprintw(thtmwin, 10, 1, "Acceleration X:   ");
    (void)mvwprintw(thtmwin, 11, 1, "Acceleration Y:   ");
    (void)mvwprintw(thtmwin, 12, 1, "Acceleration Z:   ");
    (void)mvwprintw(thtmwin, 13, 1, "Depth:         ");
    (void)mvwprintw(thtmwin, 14, 1, "Temperature:   ");
    (void)wattrset(thtmwin, A_NORMAL);
    /*@ +onlytrans @*/
    return true;
}

static void tnt_update(void)
{
    /*
     * Called on each packet received.  The packet will be accessible in 
     * session.packet.outbuffer and the length in session.packet.outbuflen.
     * If the device is NMEA, session.driver.nmea.fields[] will contain the
     * array of unconverted field strings, including the tag in slot zero
     * but not including the checksum or trailing \r\n.
     *
     * Use this function to update devicewin.  The packet will be echoed to
     * packetwin immediately after this function is called; you can use this
     * function to write a prefix on the line.
     */
}

static int tnt_command(char line[])
{
    /*
     * Interpret a command line.  Whatever characters the user types will
     * be echoed in the command buffer at the top right of the display. When
     * he/she presses enter the command line will be passed to this function  
     * for interpretation.  Note: packet receipt is suspended while this
     * function is executing.
     *
     * This method is optional.  If you set the command method pointer to
     * NULL, gpsmon will behave sanely, accepting no device-specific commands. 
     *
     * It is a useful convention to use uppercase letters for
     * driver-specific commands and leave lowercase ones for the
     * generic gpsmon ones.
     */

    /* 
     * Return COMMAND_UNKNOWN to tell gpsmon you can't interpret the line, and
     * it will be passed to the generic command interpreter to be handled there.
     * You can alse return COMMAND_MATCH to tell it you handled the command,
     * or COMMAND_TERMINATE to tell gpsmon you handled it and gpsmon should
     * terminate.
     */
    return COMMAND_UNKNOWN;
}

static void tnt_wrap(void)
{
    (void)delwin(thtmwin);
}

/*
 * Use mmt = monitor method table as a suffix for naming these things
 * Yours will need to be added to the monitor_objects table in gpsmon.c,
 * then of course you need to link your module into gpsmon.
 */
const struct monitor_object_t tnt_mmt = {
    .initialize = tnt_initialize,
    .update = tnt_update,
    .command = tnt_command,
    .wrap = tnt_wrap,
    .min_y = 16, .min_x = 80,	/* size of the device window */
    .driver = &trueNorth,
};
#endif /* TNT_ENABLE */

/*
 * Helpers:
 *
 * bool monitor_control_send(unsigned char *buf, size_t len)
 *    Ship a packet payload to the device.  Calls the driver send_control()
 *    method to add headers/trailers/checksum; also dumps the sent
 *    packet to the packet window, if the send_control() is playing
 *    nice by using session.msgbuf to assemble the message.
 *
 * void monitor_log(const char *fmt, ...)
 *    Write a message to the packet window.  Safe if the packet window
 *    is not on screen.
 *
 * void monitor_complain(const char *fmt, ...)
 *    Post an error message to the command window, wait till user presses a key.
 *    You get to make sure the message will fit.
 *
 * void monitor_fixframe(WINDOW *win)
 *    Fix the frame of win to the right of the current location by redrawing 
 *    ACS_VLINE there.  Useful after doing wclrtoeol() and writing on the
 *    line.
 *
 * The libgpsd session object is accessible as the global variable 'session'.
 */
