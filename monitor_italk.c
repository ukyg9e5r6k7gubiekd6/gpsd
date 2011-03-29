/*
 * This file is Copyright (c) 2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 */

#include <stdio.h>

#include "gpsd.h"
#include "bits.h"
#include "gpsmon.h"

#ifdef ITRAX_ENABLE
#include "driver_italk.h"

#ifdef HAVE_STRLCAT
#include <string.h>
#endif

extern const struct gps_type_t italk_binary;
static WINDOW *satwin, *navfixwin;

#define display	(void)mvwprintw
static bool italk_initialize(void)
{
    int i;

    /*@ -onlytrans @*/
    /* "heavily inspired" by monitor_nmea.c */
    if ((satwin =
	 derwin(devicewin, MAX_NR_VISIBLE_PRNS + 3, 27, 0, 0)) == NULL)
	return false;
    (void)wborder(satwin, 0, 0, 0, 0, 0, 0, 0, 0), (void)syncok(satwin, true);
    (void)wattrset(satwin, A_BOLD);
    display(satwin, 1, 1, "Ch PRN  Az El S/N Flag U");
    for (i = 0; i < MAX_NR_VISIBLE_PRNS; i++)
	display(satwin, (int)(i + 2), 1, "%2d", i);
    display(satwin, MAX_NR_VISIBLE_PRNS + 2, 7, " PRN_STATUS ");
    (void)wattrset(satwin, A_NORMAL);

    /* "heavily inspired" by monitor_nmea.c */
    if ((navfixwin = derwin(devicewin, 13, 52, 0, 27)) == NULL)
	return false;
    (void)wborder(navfixwin, 0, 0, 0, 0, 0, 0, 0, 0),
	(void)wattrset(navfixwin, A_BOLD);
    (void)wmove(navfixwin, 1, 1);
    (void)wprintw(navfixwin, "ECEF Pos:");
    (void)wmove(navfixwin, 2, 1);
    (void)wprintw(navfixwin, "ECEF Vel:");

    (void)wmove(navfixwin, 4, 1);
    (void)wprintw(navfixwin, "LTP Pos:");
    (void)wmove(navfixwin, 5, 1);
    (void)wprintw(navfixwin, "LTP Vel:");

    (void)wmove(navfixwin, 7, 1);
    (void)wprintw(navfixwin, "Time UTC:");
    (void)wmove(navfixwin, 8, 1);
    (void)wprintw(navfixwin, "Time GPS:                  Day:");

    (void)wmove(navfixwin, 10, 1);
    (void)wprintw(navfixwin, "DOP [H]      [V]      [P]      [T]      [G]");
    (void)wmove(navfixwin, 11, 1);
    (void)wprintw(navfixwin, "Fix:");

    display(navfixwin, 12, 20, " NAV_FIX ");
    (void)wattrset(navfixwin, A_NORMAL);
    return true;
    /*@ +onlytrans @*/
}

static void display_itk_navfix(unsigned char *buf, size_t len)
{

    unsigned int tow, tod, d, svlist;
    unsigned short gps_week, nsv;
    unsigned short year, mon, day, hour, min, sec;
    double epx, epy, epz, evx, evy, evz;
    double latitude, longitude;
    float altitude, speed, track, climb;
    float hdop, gdop, pdop, vdop, tdop;

    if (len != 296)
	return;

#ifdef __UNUSED__
    flags = (ushort) getleu16(buf, 7 + 4); */
    cflags = (ushort) getleu16(buf, 7 + 6);
    pflags = (ushort) getleu16(buf, 7 + 8);
#endif /* __UNUSED__ */

#define MAX(a,b) (((a) > (b)) ? (a) : (b))
    nsv = (ushort) MAX(getleu16(buf, 7 + 12), getleu16(buf, 7 + 14));
    svlist = (ushort) getleu32(buf, 7 + 16) | getleu32(buf, 7 + 24);

    hour = (ushort) getleu16(buf, 7 + 66);
    min = (ushort) getleu16(buf, 7 + 68);
    sec = (ushort) getleu16(buf, 7 + 70);
    //nsec = (ushort) getleu32(buf, 7 + 72);
    year = (ushort) getleu16(buf, 7 + 76);
    mon = (ushort) getleu16(buf, 7 + 78);
    day = (ushort) getleu16(buf, 7 + 80);
    gps_week = (ushort) getles16(buf, 7 + 82);
    tow = (ushort) getleu32(buf, 7 + 84);

    epx = (double)(getles32(buf, 7 + 96) / 100.0);
    epy = (double)(getles32(buf, 7 + 100) / 100.0);
    epz = (double)(getles32(buf, 7 + 104) / 100.0);
    evx = (double)(getles32(buf, 7 + 186) / 1000.0);
    evy = (double)(getles32(buf, 7 + 190) / 1000.0);
    evz = (double)(getles32(buf, 7 + 194) / 1000.0);

    latitude = (double)(getles32(buf, 7 + 144) / 1e7);
    longitude = (double)(getles32(buf, 7 + 148) / 1e7);
    altitude = (float)(getles32(buf, 7 + 152) / 1e3);
    climb = (float)(getles32(buf, 7 + 206) / 1e3);
    speed = (float)(getleu32(buf, 7 + 210) / 1e3);
    track = (float)(getleu16(buf, 7 + 214) / 1e2);

    hdop = (float)(getleu16(buf, 7 + 56) / 100.0);
    gdop = (float)(getleu16(buf, 7 + 58) / 100.0);
    pdop = (float)(getleu16(buf, 7 + 60) / 100.0);
    vdop = (float)(getleu16(buf, 7 + 62) / 100.0);
    tdop = (float)(getleu16(buf, 7 + 64) / 100.0);

    (void)wmove(navfixwin, 1, 11);
    (void)wprintw(navfixwin, "%12.2lf %12.2lf %12.2lfm", epx, epy, epz);
    (void)wmove(navfixwin, 2, 11);
    (void)wprintw(navfixwin, "%11.2lf %11.2lf %11.2lfm/s", evx, evy, evz);

    (void)wmove(navfixwin, 4, 11);
    (void)wprintw(navfixwin, "%11.8lf   %13.8lf %8.1lfm",
		  latitude, longitude, altitude);
    (void)mvwaddch(navfixwin, 4, 22, ACS_DEGREE);
    (void)mvwaddch(navfixwin, 4, 38, ACS_DEGREE);
    (void)wmove(navfixwin, 5, 11);
    (void)wprintw(navfixwin, "%6.2lfm/s  %5.1lf  %6.2lfm/s climb",
		  speed, track, climb);
    (void)mvwaddch(navfixwin, 5, 27, ACS_DEGREE);

    (void)wmove(navfixwin, 7, 11);
    (void)wprintw(navfixwin, "%04u-%02u-%02u %02u:%02u:%02u",
		  year, mon, day, hour, min, sec);
    (void)wmove(navfixwin, 8, 11);
    (void)wprintw(navfixwin, "%04u+%010.3lf", gps_week, tow / 1000.0);
    (void)wmove(navfixwin, 8, 33);
    d = (tow / 1000) / 86400;
    tod = (tow / 1000) - (d * 86400);
    sec = (unsigned short)tod % 60;
    min = (unsigned short)(tod / 60) % 60;
    hour = (unsigned short)tod / 3600;
    (void)wprintw(navfixwin, "%1d %02d:%02d:%02d", d, hour, min, sec);

    (void)wmove(navfixwin, 10, 9);
    (void)wprintw(navfixwin, "%-5.1f", hdop);
    (void)wmove(navfixwin, 10, 18);
    (void)wprintw(navfixwin, "%-5.1f", vdop);
    (void)wmove(navfixwin, 10, 27);
    (void)wprintw(navfixwin, "%-5.1f", pdop);
    (void)wmove(navfixwin, 10, 36);
    (void)wprintw(navfixwin, "%-5.1f", tdop);
    (void)wmove(navfixwin, 10, 45);
    (void)wprintw(navfixwin, "%-5.1f", gdop);

    (void)wmove(navfixwin, 11, 6);
    {
	char prn[4], satlist[38];
	unsigned int i;
	satlist[0] = '\0';
	for (i = 0; i < 32; i++) {
	    if (svlist & (1 << i)) {
		(void)snprintf(prn, 4, "%u ", i + 1);
		(void)strlcat(satlist, prn, 38);
	    }
	}
	(void)wprintw(navfixwin, "%02d = %-38s", nsv, satlist);
    }
    (void)wnoutrefresh(navfixwin);

}

static void display_itk_prnstatus(unsigned char *buf, size_t len)
{
    int i, nchan;
    if (len < 62)
	return;

    nchan = (int)getleu16(buf, 7 + 50);
    if (nchan > MAX_NR_VISIBLE_PRNS)
	nchan = MAX_NR_VISIBLE_PRNS;
    for (i = 0; i < nchan; i++) {
	int off = 7 + 52 + 10 * i;
	unsigned short fl;
	unsigned char ss, prn, el, az;

	fl = (unsigned short)getleu16(buf, off);
	ss = (unsigned char)getleu16(buf, off + 2) & 0xff;
	prn = (unsigned char)getleu16(buf, off + 4) & 0xff;
	el = (unsigned char)getles16(buf, off + 6) & 0xff;
	az = (unsigned char)getles16(buf, off + 8) & 0xff;
	(void)wmove(satwin, i + 2, 4);
	(void)wprintw(satwin, "%3d %3d %2d  %02d %04x %c",
		      prn, az, el, ss, fl,
		      (fl & PRN_FLAG_USE_IN_NAV) ? 'Y' : ' ');
    }
    for (; i < MAX_NR_VISIBLE_PRNS; i++) {
	(void)wmove(satwin, (int)i + 2, 4);
	(void)wprintw(satwin, "                      ");
    }
    (void)wnoutrefresh(satwin);
    return;
}

static void italk_update(void)
{
    unsigned char *buf;
    size_t len;
    unsigned char type;

    buf = session.packet.outbuffer;
    len = session.packet.outbuflen;
    type = (unsigned char)getub(buf, 4);
    switch (type) {
    case ITALK_NAV_FIX:
	display_itk_navfix(buf, len);
	break;
    case ITALK_PRN_STATUS:
	display_itk_prnstatus(buf, len);
	break;
    default:
	break;
    }
}

static int italk_command(char line[]UNUSED)
{
    return COMMAND_UNKNOWN;
}

static void italk_wrap(void)
{
    (void)delwin(satwin);
    return;
}

const struct monitor_object_t italk_mmt = {
    .initialize = italk_initialize,
    .update = italk_update,
    .command = italk_command,
    .wrap = italk_wrap,
    .min_y = 23,.min_x = 80,	/* size of the device window */
    .driver = &italk_binary,
};
#endif
