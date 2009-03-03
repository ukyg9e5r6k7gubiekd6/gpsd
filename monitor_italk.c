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

#ifdef ITRAX_ENABLE
#include "driver_italk.h"

extern const struct gps_type_t italk_binary;
static WINDOW *satwin;

static bool italk_initialize(void)
{
	int i;

	/* "heavily inspired" by monitor_nmea.c */
	if ((satwin  = derwin(devicewin, 15, 27, 6, 0)) == NULL)
		return false;
	(void)wborder(satwin, 0, 0, 0, 0, 0, 0, 0, 0),
	(void)syncok(satwin, true);
	(void)wattrset(satwin, A_BOLD);
	(void)mvwprintw(satwin, 1, 1, "Ch  SV  Az El S/N Flag U");
	for (i = 0; i < SIRF_CHANNELS; i++)
		(void)mvwprintw(satwin, (int)(i+2), 1, "%2d",i);
	(void)mvwprintw(satwin, 14, 7, " PRN_STATUS ");
	(void)wattrset(satwin, A_NORMAL);

	return true;
}

static void display_itk_prnstatus(unsigned char *buf, size_t len)
{
	unsigned int i, nchan;
	if (len < 62)
		return;

	nchan = (unsigned int)((len - 10 - 52) / 20);
	for (i = 0; i < nchan; i++) {
		unsigned int off = 7+ 52 + 20 * i;
		unsigned short fl;
		unsigned char ss, prn, el, az;

		fl  = getleuw(buf, off);
		ss  = (unsigned char)getleuw(buf, off+2)&0xff;
		prn = (unsigned char)getleuw(buf, off+4)&0xff;
		el  = (unsigned char)getlesw(buf, off+6)&0xff;
		az  = (unsigned char)getlesw(buf, off+8)&0xff;
		wmove(satwin, i+2, 4);
		wprintw(satwin, "%3d %3d %2d  %02d %04x %c",
			prn, az, el, ss, fl,
			(fl & PRN_FLAG_USE_IN_NAV)? 'Y' : ' ');
	}
	wnoutrefresh(satwin);
	return;
}

static void italk_update(void)
{
	unsigned char *buf;
	size_t len;
	unsigned char type;

	buf = session.packet.outbuffer;
	len = session.packet.outbuflen;
	type = getub(buf, 4);
	switch (type) {
		case ITALK_PRN_STATUS:
			display_itk_prnstatus(buf, len);
			break;
		default:
			break;
	}
}

static int italk_command(char line[] UNUSED)
{
	return COMMAND_UNKNOWN;
}

static void italk_wrap(void)
{
	delwin(satwin);
	return;
}

const struct monitor_object_t italk_mmt = {
	.initialize = italk_initialize,
	.update = italk_update,
	.command = italk_command,
	.wrap = italk_wrap,
	.min_y = 23, .min_x = 80, /* size of the device window */
	.driver = &italk_binary,
};
#endif
