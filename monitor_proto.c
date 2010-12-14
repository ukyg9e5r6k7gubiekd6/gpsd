/*
 * Prototype file for a gpsmon monitor object.  
 *
 * This file is Copyright (c) 2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <assert.h>
#ifndef S_SPLINT_S
#include <unistd.h>
#endif /* S_SPLINT_S */

#include "gpsd.h"
#include "bits.h"
#include "gpsmon.h"

/*
 * Replace PROTO everywhere with the name of the GPSD driver describing
 * the device you want to support.
 *
 * gpsmon basically sits in a loop reading packets, using the same layer
 * as gpsd to dispatch on packet type to select an active device driver.
 * Your monitor object will become the handler for incoming packets whenever
 * the driver your object points at is selected.
 *
 * A comment following the method descriptions explains some available
 * helper functions.
 */

extern const struct gps_type_t PROTO_binary;

static bool PROTO_initialize(void)
{
    /*
     * This function is called when your monitor object is activated.
     *
     * When you enter it, two windows will be accessible to you; (1)
     * devicewin, just below the status and command line at top of
     * screen, and (2) packetwin, taking up the rest of the screen below
     * it; packetwin will be enabled for scrolling. Note, however,
     * that you cannot necessarily update packetwin safely, as it may be NULL
     * if the screen has no lines left over after allocating devicewin;
     * you'll need to check this in your code.
     *
     * Use this method to paint windowframes and legends on the
     * freshly initialized device window.  You can also use this
     * method to send probes to the device, e.g. to elicit a response
     * telling you firmware rev levels or whatever.
     */

    /* return false if the window allocation failed; gpsmon will abort */
    return true;
}

static void PROTO_update(void)
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

static int PROTO_command(char line[])
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

static void PROTO_wrap(void)
{
    /* 
     * Deinitialize any windows you created in PROTO_initialize.
     * This will be called when gpsmon switches drivers due to seeing
     * a new packet type.
     */
}

/*
 * Use mmt = monitor method table as a suffix for naming these things
 * Yours will need to be added to the monitor_objects table in gpsmon.c,
 * then of course you need to link your module into gpsmon.
 */
const struct monitor_object_t PROTO_mmt = {
    .initialize = PROTO_initialize,
    .update = PROTO_update,
    .command = PROTO_command,
    .wrap = PROTO_wrap,
    .min_y = 23, .min_x = 80,	/* size of the device window */
    /*
     * The gpsd driver type for your device.  gpsmon will use the mode_switcher
     * method for 'n', the speed_switcher for 's', and the control_send method
     * for 'c'.  Additionally, the driver type name will be displayed before
     * the '>' command prompt in the top line of the display.
     */
    .driver = &PROTO_binary,
};

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
