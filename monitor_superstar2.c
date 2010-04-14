/*
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

#ifdef SUPERSTAR2_ENABLE
#include "driver_superstar2.h"
extern const struct gps_type_t superstar2_binary;
static WINDOW *satwin;

static bool superstar2_initialize(void)
{
    int i;

    /*@ -onlytrans @*/
    /* "heavily inspired" by monitor_nmea.c */
    if ((satwin = derwin(devicewin, 15, 27, 7, 0)) == NULL)
	return false;
    (void)wborder(satwin, 0, 0, 0, 0, 0, 0, 0, 0), (void)syncok(satwin, true);
    (void)wattrset(satwin, A_BOLD);
    (void)mvwprintw(satwin, 1, 1, "Ch PRN  Az El S/N Fl U");
    for (i = 0; i < 12; i++)
	(void)mvwprintw(satwin, (int)(i + 2), 1, "%2d", i);
    (void)mvwprintw(satwin, 14, 1, " Satellite Data & Status ");
    (void)wattrset(satwin, A_NORMAL);
    /*@ +onlytrans @*/

    return true;
}

static void display_superstar2_svinfo(unsigned char *buf, size_t data_len)
{
    int i;

    if (data_len != 67)
	return;

    for (i = 0; i < 12; i++) {
	/* get info for one channel/satellite */
	int off = i * 5 + 5;
	unsigned char fl, porn, ss;
	char el;
	unsigned short az;

	/*@ +charint */
	if ((porn = (unsigned char)getub(buf, off) & 0x1f) == 0)
	    porn = ((unsigned char)getub(buf, off + 3) >> 1) + 87;
	/*@ -charint */

	ss = (unsigned char)getub(buf, off + 4);
	el = getsb(buf, off + 1);
	az = (unsigned short)(getub(buf, off + 2) +
			      ((getub(buf, off + 3) & 0x1) << 1));
	fl = (unsigned char)getub(buf, off) & 0xe0;
	(void)wmove(satwin, i + 2, 4);
	/*@ +charint */
	(void)wprintw(satwin, "%3u %3d %2d  %02d %02x %c",
		      porn, az, el, ss, fl,
		      ((fl & 0x60) == 0x60) ? 'Y' : ' ');
	/*@ -charint */
    }
    (void)wnoutrefresh(satwin);
    return;
}

static void superstar2_update(void)
{
    unsigned char *buf;
    size_t len;
    unsigned char type;

    buf = session.packet.outbuffer;
    len = session.packet.outbuflen;
    type = buf[SUPERSTAR2_TYPE_OFFSET];
    switch (type) {
    case SUPERSTAR2_SVINFO:
	display_superstar2_svinfo(buf, len - 3);
	break;
    default:
	break;
    }
}

static int superstar2_command(char line[]UNUSED)
{
    return COMMAND_UNKNOWN;
}

static void superstar2_wrap(void)
{
}

const struct monitor_object_t superstar2_mmt = {
    .initialize = superstar2_initialize,
    .update = superstar2_update,
    .command = superstar2_command,
    .wrap = superstar2_wrap,
    .min_y = 23,.min_x = 80,	/* size of the device window */
    .driver = &superstar2_binary,
};
#endif
