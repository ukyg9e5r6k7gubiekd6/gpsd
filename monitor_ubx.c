/* $Id$ */
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

#ifdef UBX_ENABLE
#include "driver_ubx.h"
extern const struct gps_type_t ubx_binary;
static WINDOW *satwin;

static bool ubx_initialize(void)
{
	int i;

	/* "heavily inspired" by monitor_nmea.c */
	if ((satwin  = derwin(devicewin, 19, 28, 0, 0)) == NULL)
		return false;
	(void)wborder(satwin, 0, 0, 0, 0, 0, 0, 0, 0),
	(void)syncok(satwin, true);
	(void)wattrset(satwin, A_BOLD);
	(void)mvwprintw(satwin, 1, 1, "Ch PRN  Az  El S/N Flag U");
	for (i = 0; i < 16; i++)
		(void)mvwprintw(satwin, (int)(i+2), 1, "%2d",i);
	(void)mvwprintw(satwin, 18, 7, " NAV_SVINFO ");
	(void)wattrset(satwin, A_NORMAL);

    return true;
}

static void display_nav_svinfo(unsigned char *buf, size_t data_len)
{
	unsigned int i, nchan;

	if (data_len < 152 )
		return;

	nchan = getub(buf, 4);
	if (nchan > 16)
		nchan = 16;

	for (i = 0; i < nchan; i++) {
		unsigned int off = 8 + 12 * i;
		unsigned char ss, prn;
		char el;
		short az;
		unsigned short fl;

		prn = getub(buf, off+1);
		fl = getleuw(buf, off+2);
		ss = getub(buf, off+4);
		el = getsb(buf, off+5);
		az = getlesw(buf, off+6);
		wmove(satwin, (int)(i+2), 4);
		wprintw(satwin, "%3d %3d %3d  %2d %04x %c",
			prn, az, el, ss, fl,
			(fl & UBX_SAT_USED)? 'Y' : ' ');
	}
	wnoutrefresh(satwin);
	return;
}

static void ubx_update(void)
{
	unsigned char *buf;
	size_t data_len;
	unsigned short msgid;

	buf = session.packet.outbuffer;
	msgid = (unsigned short)((buf[2] << 8) | buf[3]);
	data_len = (size_t)getlesw(buf, 4);
	switch (msgid) {
		case UBX_NAV_SVINFO:
			display_nav_svinfo(&buf[6], data_len);
			break;
		default:
			break;
	}

}

static int ubx_command(char line[] UNUSED)
{
	return COMMAND_UNKNOWN;
}

static void ubx_wrap(void)
{
	delwin(satwin);
	return;
}

const struct monitor_object_t ubx_mmt = {
    .initialize = ubx_initialize,
    .update = ubx_update,
    .command = ubx_command,
    .wrap = ubx_wrap,
    .min_y = 23, .min_x = 80,	/* size of the device window */
    .driver = &ubx_binary,
};
#endif
