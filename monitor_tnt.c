/*
 * monitor_tnt.c - gpsmon support for True North Revolution devices.
 *
 * This file is Copyright (c) 2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 */
#ifndef S_SPLINT_S
#include <unistd.h>
#endif /* S_SPLINT_S */

#include "gpsd.h"
#include "gpsmon.h"

#ifdef TNT_ENABLE
extern const struct gps_type_t trueNorth;

static WINDOW *thtmwin;

static bool tnt_initialize(void)
{
    /*@ -onlytrans @*/
    thtmwin = derwin(devicewin, 6, 80, 0, 0);
    (void)wborder(thtmwin, 0, 0, 0, 0, 0, 0, 0, 0),
	(void)syncok(thtmwin, true);
    (void)wattrset(thtmwin, A_BOLD);
    (void)mvwaddstr(thtmwin, 0, 35, " PTNTHTM ");
    (void)mvwaddstr(thtmwin, 1, 1, "Heading:          ");
    (void)mvwaddstr(thtmwin, 2, 1, "Pitch:            ");
    (void)mvwaddstr(thtmwin, 3, 1, "Roll:             ");
    (void)mvwaddstr(thtmwin, 4, 1, "Dip:              ");

    (void)mvwaddstr(thtmwin, 1, 40, "Magnetometer Status: ");
    (void)mvwaddstr(thtmwin, 2, 40, "Pitch Status:        ");
    (void)mvwaddstr(thtmwin, 3, 40, "Roll Status          ");
    (void)mvwaddstr(thtmwin, 4, 40, "Horizontal Field:    ");
    (void)wattrset(thtmwin, A_NORMAL);
    /*@ +onlytrans @*/
    return true;
}

static void tnt_update(void)
{
    /* 
     * We have to do our own field parsing because the way this
     * gets valled, nmea_parse() is never called on the sentence.
     */
    (void)nmea_parse((char *)session.packet.outbuffer, &session);

    (void)mvwaddstr(thtmwin, 1, 19, session.driver.nmea.field[1]);
    (void)mvwaddstr(thtmwin, 2, 19, session.driver.nmea.field[3]);
    (void)mvwaddstr(thtmwin, 3, 19, session.driver.nmea.field[5]);
    (void)mvwaddstr(thtmwin, 4, 19, session.driver.nmea.field[7]);

    (void)mvwaddstr(thtmwin, 1, 61, session.driver.nmea.field[2]);
    (void)mvwaddstr(thtmwin, 2, 61, session.driver.nmea.field[4]);
    (void)mvwaddstr(thtmwin, 3, 61, session.driver.nmea.field[6]);
    (void)mvwaddstr(thtmwin, 4, 61, session.driver.nmea.field[8]);
}

static int tnt_command(char line[] UNUSED)
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
    .min_y = 6,.min_x = 80,	/* size of the device window */
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
