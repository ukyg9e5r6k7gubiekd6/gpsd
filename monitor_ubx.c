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

#ifdef UBX_ENABLE
#include "driver_ubx.h"
extern const struct gps_type_t ubx_binary;
static WINDOW *satwin, *navsolwin, *dopwin;

#define display	(void)mvwprintw
static bool ubx_initialize(void)
{
	int i;

	/*@ -onlytrans @*/
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
	/*@ -onlytrans @*/

	/* "heavily inspired" by monitor_nmea.c */
	if ((navsolwin = derwin(devicewin, 13,  51,  0, 28)) == NULL)
		return false;
	(void)wborder(navsolwin, 0, 0, 0, 0, 0, 0, 0, 0),
	(void)wattrset(navsolwin, A_BOLD);
	(void)wmove(navsolwin, 1,1);
	(void)wprintw(navsolwin, "ECEF Pos:");
	(void)wmove(navsolwin, 2,1);
	(void)wprintw(navsolwin, "ECEF Vel:");

	(void)wmove(navsolwin, 4,1);
	(void)wprintw(navsolwin, "LTP Pos:");
	(void)wmove(navsolwin, 5,1);
	(void)wprintw(navsolwin, "LTP Vel:");

	(void)wmove(navsolwin, 7,1);
	(void)wprintw(navsolwin, "Time UTC:");
	(void)wmove(navsolwin, 8,1);
	(void)wprintw(navsolwin, "Time GPS:                     Day:");

	(void)wmove(navsolwin, 10,1);
	(void)wprintw(navsolwin, "Est Pos Err       m Est Vel Err       m/s");
	(void)wmove(navsolwin, 11,1);
	(void)wprintw(navsolwin, "PRNs: ## PDOP: xx.x Fix 0x.. Flags 0x..");

	display(navsolwin, 12, 20, " NAV_SOL ");
	(void)wattrset(navsolwin, A_NORMAL);

	if ((dopwin = derwin(devicewin, 3,  51,  13, 28)) == NULL)
		return false;
	(void)wborder(dopwin, 0, 0, 0, 0, 0, 0, 0, 0),
	(void)wattrset(dopwin, A_BOLD);
	(void)wmove(dopwin, 1,1);
	(void)wprintw(dopwin, "DOP [H]      [V]      [P]      [T]      [G]");
	display(dopwin, 2, 20, " NAV_DOP ");
	(void)wattrset(dopwin, A_NORMAL);

	return true;
}

static void display_nav_svinfo(unsigned char *buf, size_t data_len)
{
	int i, nchan;

	if (data_len < 152 )
		return;

	nchan = (int)getub(buf, 4);
	if (nchan > 16)
		nchan = 16;

	for (i = 0; i < nchan; i++) {
		int off = 8 + 12 * i;
		unsigned char ss, prn;
		char el;
		short az;
		unsigned short fl;

		prn = (unsigned char)getub(buf, off+1);
		fl = (unsigned short)getleuw(buf, off+2);
		ss = (unsigned char)getub(buf, off+4);
		el = getsb(buf, off+5);
		az = getlesw(buf, off+6);
		(void)wmove(satwin, (int)(i+2), 4);
		(void)wprintw(satwin, "%3d %3d %3d  %2d %04x %c",
			prn, az, el, ss, fl,
			(fl & UBX_SAT_USED)? 'Y' : ' ');
	}
	(void)wnoutrefresh(satwin);
	return;
}

/*@ -mustfreeonly -compdestroy @*/
static void display_nav_sol(unsigned char *buf, size_t data_len)
{
	unsigned short gw = 0;
	unsigned int tow = 0, flags;
	double epx, epy, epz, evx, evy, evz;
	unsigned char navmode;
	double t;
	time_t tt;
	struct gps_data_t g;
	double separation;

	if (data_len != 52)
		return;

	navmode = (unsigned char)getub(buf, 10);
	flags = (unsigned int)getub(buf, 11);

	if ((flags & (UBX_SOL_VALID_WEEK |UBX_SOL_VALID_TIME)) != 0) {
	    tow = (unsigned int)getleul(buf, 0);
		gw = (unsigned short)getlesw(buf, 8);
		t = gpstime_to_unix((int)gw, tow/1000.0);
		tt = (time_t)trunc(t);
	}

	epx = (double)(getlesl(buf, 12)/100.0);
	epy = (double)(getlesl(buf, 16)/100.0);
	epz = (double)(getlesl(buf, 20)/100.0);
	evx = (double)(getlesl(buf, 28)/100.0);
	evy = (double)(getlesl(buf, 32)/100.0);
	evz = (double)(getlesl(buf, 36)/100.0);
	ecef_to_wgs84fix(&g.fix, &separation,
			 epx, epy, epz, evx, evy, evz);
	g.fix.epx = g.fix.epy = (double)(getlesl(buf, 24)/100.0);
	g.fix.eps = (double)(getlesl(buf, 40)/100.0);
	g.dop.pdop = (double)(getleuw(buf, 44)/100.0);
	g.satellites_used = (int)getub(buf, 47);

	(void)wmove(navsolwin, 1, 11);
	(void)wprintw(navsolwin, "%+10.2fm %+10.2fm %+10.2fm", epx, epy, epz);
	(void)wmove(navsolwin, 2, 11);
	(void)wprintw(navsolwin, "%+9.2fm/s %+9.2fm/s %+9.2fm/s", evx, evy, evz);

	(void)wmove(navsolwin, 4, 11);
	(void)wprintw(navsolwin, "%12.9f  %13.9f  %8.2fm",
		g.fix.latitude, g.fix.longitude, g.fix.altitude);
	(void)mvwaddch(navsolwin, 4, 23, ACS_DEGREE);
	(void)mvwaddch(navsolwin, 4, 39, ACS_DEGREE);
	(void)wmove(navsolwin, 5, 11);
	(void)wprintw(navsolwin, "%6.2fm/s %5.1fo %6.2fm/s",
		g.fix.speed, g.fix.track, g.fix.climb);
	(void)mvwaddch(navsolwin, 5, 26, ACS_DEGREE);

	(void)wmove(navsolwin, 7, 11);
	/*@ -compdef @*/
	(void)wprintw(navsolwin, "%s", ctime(&tt));
	/*@ +compdef @*/
	(void)wmove(navsolwin, 8, 11);
	if ((flags & (UBX_SOL_VALID_WEEK |UBX_SOL_VALID_TIME)) != 0) {
	    (void)wprintw(navsolwin, "%d+%10.3lf", gw, (double)(tow/1000.0));
	    (void)wmove(navsolwin, 8, 36);
	    (void)wprintw(navsolwin, "%d", (tow/86400000) );
	}

	/* relies on the fact that expx and epy are aet to same value */
	(void)wmove(navsolwin, 10, 12);
	(void)wprintw(navsolwin, "%7.2f", g.fix.epx);
	(void)wmove(navsolwin, 10, 33);
	(void)wprintw(navsolwin, "%6.2f", g.fix.epv);
	(void)wmove(navsolwin, 11, 7);
	(void)wprintw(navsolwin, "%2d", g.satellites_used);
	(void)wmove(navsolwin, 11, 15);
	(void)wprintw(navsolwin, "%5.1f", g.dop.pdop);
	(void)wmove(navsolwin, 11, 25);
	(void)wprintw(navsolwin, "0x%02x", navmode);
	(void)wmove(navsolwin, 11, 36);
	(void)wprintw(navsolwin, "0x%02x", flags);
	(void)wnoutrefresh(navsolwin);
}
/*@ +mustfreeonly +compdestroy @*/

static void display_nav_dop(unsigned char *buf, size_t data_len)
{
	if (data_len != 18)
		return;
	(void)wmove(dopwin, 1,9);
	(void)wprintw(dopwin, "%4.1f", getleuw(buf, 12)/100.0);
	(void)wmove(dopwin, 1,18);
	(void)wprintw(dopwin, "%4.1f", getleuw(buf, 10)/100.0);
	(void)wmove(dopwin, 1,27);
	(void)wprintw(dopwin, "%4.1f", getleuw(buf, 6)/100.0);
	(void)wmove(dopwin, 1,36);
	(void)wprintw(dopwin, "%4.1f", getleuw(buf, 8)/100.0);
	(void)wmove(dopwin, 1,45);
	(void)wprintw(dopwin, "%4.1f", getleuw(buf, 4)/100.0);
	(void)wnoutrefresh(dopwin);
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
		case UBX_NAV_DOP:
			display_nav_dop(&buf[6], data_len);
			break;
		case UBX_NAV_SOL:
			display_nav_sol(&buf[6], data_len);
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
	(void)delwin(satwin);
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
