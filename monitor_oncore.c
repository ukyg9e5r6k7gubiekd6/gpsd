/*
 * OnCore object for the GPS packet monitor.
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

#if defined(ONCORE_ENABLE) && defined(BINARY_ENABLE)
extern const struct gps_type_t oncore_binary;

static WINDOW *Ea1win, *Eawin, *Bbwin, *Enwin, *Bowin, *Aywin, *Aswin, *Atwin;
static unsigned char EaSVlines[8];

static const char *antenna[] = {
    "OK (conn)",
    "OC (short)",
    "UC (open)",
    "OU (short)"
};

static const char *sv_mode[] = {
    "srch",
    "acq",
    "AGCs",
    "pacq",
    "bits",
    "msgs",
    "satT",
    "epha",
    "avl"
};

static const char *pps_ctrl[] = {
    "off",
    "on",
    "on if >= 1 SV",
    "on if TRAIM ok"
};

static const char *pps_sync[] = {
    "UTC",
    "GPS"
};

static const char *traim_sol[] = {
    "OK",
    "ALARM",
    "UNKNOWN"
};

static const char *traim_status[] = {
    "detect & isolate",
    "detect",
    "insufficient"
};

static const char *pos_hold_mode[] = {
    "off",
    "on",
    "survey"
};

#define ONCTYPE(id2,id3) ((((unsigned int)id2)<<8)|(id3))

#define MAXTRACKSATS 	8	/* the most satellites being tracked */
#define MAXVISSATS 	12	/* the most satellites with known az/el */

static bool oncore_initialize(void)
{
    unsigned int i;

    /*@ -onlytrans @*/
    Ea1win = subwin(devicewin, 5, 80, 1, 0);
    Eawin = subwin(devicewin, MAXTRACKSATS + 3, 27, 6, 0);
    Bbwin = subwin(devicewin, MAXVISSATS + 3, 22, 6, 28);
    Enwin = subwin(devicewin, 10, 29, 6, 51);
    Bowin = subwin(devicewin, 4, 11, 17, 0);
    Aywin = subwin(devicewin, 4, 15, 17, 12);
    Atwin = subwin(devicewin, 5, 9, 16, 51);
    Aswin = subwin(devicewin, 5, 19, 16, 61);
    /*@ +onlytrans @*/

    if (Ea1win == NULL || Eawin == NULL || Bbwin == NULL || Enwin == NULL
	|| Bowin == NULL || Aswin == NULL || Atwin == NULL)
	return false;

    (void)syncok(Ea1win, true);
    (void)syncok(Eawin, true);
    (void)syncok(Bbwin, true);
    (void)syncok(Enwin, true);
    (void)syncok(Bowin, true);
    (void)syncok(Aywin, true);
    (void)syncok(Aswin, true);
    (void)syncok(Atwin, true);

    (void)wborder(Ea1win, 0, 0, 0, 0, 0, 0, 0, 0),
	(void)wattrset(Ea1win, A_BOLD);
    (void)mvwaddstr(Ea1win, 1, 1,
		    "Time:                                     Lat:              Lon:");
    (void)mvwaddstr(Ea1win, 2, 1,
		    "Antenna:             DOP:                 Speed:            Course:");
    (void)mvwaddstr(Ea1win, 3, 1,
		    "SV/vis:        Status:                                      Alt:");
    (void)mvwprintw(Ea1win, 4, 4, " @@Ea (pos) ");
    (void)wattrset(Ea1win, A_NORMAL);

    (void)wborder(Eawin, 0, 0, 0, 0, 0, 0, 0, 0),
	(void)wattrset(Eawin, A_BOLD);
    (void)mvwprintw(Eawin, 1, 1, "Ch PRN mode S/N ????????");
    (void)mvwprintw(Eawin, 10, 4, " @@Ea (sat) ");
    for (i = 0; i < 8; i++) {
	(void)mvwprintw(Eawin, (int)(i + 2), 1, "%2d", i);
    }
    (void)wattrset(Eawin, A_NORMAL);

    (void)wborder(Bbwin, 0, 0, 0, 0, 0, 0, 0, 0),
	(void)wattrset(Bbwin, A_BOLD);
    (void)mvwprintw(Bbwin, 1, 1, "PRN  Az El doppl ??");
    (void)mvwprintw(Bbwin, 14, 4, " @@Bb ");
    (void)wattrset(Bbwin, A_NORMAL);

    (void)wborder(Enwin, 0, 0, 0, 0, 0, 0, 0, 0),
	(void)wattrset(Enwin, A_BOLD);
    (void)mvwprintw(Enwin, 1, 1, "Time RAIM: ");
    (void)mvwprintw(Enwin, 2, 1, "Alarm limit:");
    (void)mvwprintw(Enwin, 3, 1, "PPS ctrl:");
    (void)mvwprintw(Enwin, 4, 1, "Pulse:");
    (void)mvwprintw(Enwin, 5, 1, "PPS sync:");
    (void)mvwprintw(Enwin, 6, 1, "TRAIM sol stat:");
    (void)mvwprintw(Enwin, 7, 1, "Status:");
    (void)mvwprintw(Enwin, 8, 1, "Time sol sigma:");
    (void)mvwprintw(Enwin, 9, 4, " @@En ");
    (void)wattrset(Enwin, A_NORMAL);

    (void)wborder(Bowin, 0, 0, 0, 0, 0, 0, 0, 0),
	(void)wattrset(Bowin, A_BOLD);
    (void)mvwprintw(Bowin, 1, 1, "UTC:");
    (void)mvwprintw(Bowin, 3, 2, " @@Bo ");
    (void)wattrset(Bowin, A_NORMAL);

    (void)wborder(Aywin, 0, 0, 0, 0, 0, 0, 0, 0),
	(void)wattrset(Aywin, A_BOLD);
    (void)mvwprintw(Aywin, 1, 1, "PPS delay:");
    (void)mvwprintw(Aywin, 3, 4, " @@Ay ");
    (void)wattrset(Aywin, A_NORMAL);

    (void)wborder(Atwin, 0, 0, 0, 0, 0, 0, 0, 0),
	(void)wattrset(Atwin, A_BOLD);
    (void)mvwprintw(Atwin, 1, 1, "PHold:");
    (void)mvwprintw(Atwin, 4, 1, " @@At ");
    (void)wattrset(Atwin, A_NORMAL);

    (void)wborder(Aswin, 0, 0, 0, 0, 0, 0, 0, 0),
	(void)wattrset(Aswin, A_BOLD);
    (void)mvwprintw(Aswin, 1, 1, "Lat:");
    (void)mvwprintw(Aswin, 2, 1, "Lon:");
    (void)mvwprintw(Aswin, 3, 1, "Alt:");
    (void)mvwprintw(Aswin, 4, 4, " @@As ");
    (void)wattrset(Aswin, A_NORMAL);

    memset(EaSVlines, 0, sizeof(EaSVlines));

    return true;
}

static void oncore_update(void)
{
    unsigned int i, j, off;
    unsigned char *buf;
    unsigned int type;

    assert(Eawin != NULL);
    buf = session.packet.outbuffer;
    type = ONCTYPE(buf[2], buf[3]);
    switch (type) {
    case ONCTYPE('E', 'a'):
    {
	double lat, lon, alt;
	float speed, track;
	float dop;
	unsigned short year;
	unsigned char mon, day, hour, min, sec;
	unsigned int nsec;
	unsigned char dopt, nvis, nsat, status;
	char statusbuf[64];	/* 6+9+3+3+10+5+7+12+1=56 */

	mon = (unsigned char)getub(buf, 4);
	day = (unsigned char)getub(buf, 5);
	year = (unsigned short)getbeuw(buf, 6);
	hour = (unsigned char)getub(buf, 8);
	min = (unsigned char)getub(buf, 9);
	sec = (unsigned char)getub(buf, 10);
	nsec = (unsigned int)getbeul(buf, 11);

	lat = getbesl(buf, 15) / 3600000.0;
	lon = getbesl(buf, 19) / 3600000.0;
	alt = getbesl(buf, 23) / 100.0;
	speed = (float)(getbeuw(buf, 31) / 100.0);
	track = (float)(getbeuw(buf, 33) / 10.0);
	dop = (float)(getbeuw(buf, 35) / 10.0);
	dopt = (unsigned char)getub(buf, 37);
	nvis = (unsigned char)getub(buf, 38);
	nsat = (unsigned char)getub(buf, 39);
	status = (unsigned char)getub(buf, 72);

	(void)mvwprintw(Ea1win, 1, 7, "%04d-%02d-%02d %02d:%02d:%02d.%09d",
			year, mon, day, hour, min, sec, nsec);
	(void)mvwprintw(Ea1win, 1, 47, "%10.6lf %c",
			fabs(lat), lat < 0 ? 'S' : lat > 0 ? 'N' : ' ');
	(void)mvwprintw(Ea1win, 1, 66, "%10.6lf %c",
			fabs(lon), lon < 0 ? 'W' : lon > 0 ? 'E' : ' ');

	(void)mvwprintw(Ea1win, 2, 50, "%6.2f m/s", speed);
	(void)mvwprintw(Ea1win, 2, 70, "%5.1f", track);
	(void)mvwprintw(Ea1win, 3, 68, "%8.2f m", alt);

	/*@ -predboolothers @*/
	(void)snprintf(statusbuf, sizeof(statusbuf), "%s%s%s%s%s%s%s%s%s",
		       status & 0x80 ? "PProp " : "",
		       status & 0x40 ? "PoorGeom " : "",
		       status & 0x20 ? "3D " : "",
		       status & 0x10 ? "2D " : "",
		       status & 0x08 ? "Acq/PHold " : "",
		       status & 0x04 ? "Diff " : "",
		       status & 0x02 ? "Ins (<3 SV) " : "",
		       status & 0x01 ? "BadAlm " : "",
		       dopt   & 0x20 ? "survey " : "");
	/*@ +predboolothers @*/

	(void)mvwprintw(Ea1win, 3, 24, "%-37s", statusbuf);

	(void)mvwprintw(Ea1win, 2, 10, "%-10s", antenna[dopt >> 6]);

	/*@ -predboolothers @*/
	(void)mvwprintw(Ea1win, 2, 27, "%s %4.1f",
			dopt & 1 ? "hdop" : "pdop", dop);
	/*@ +predboolothers @*/

	(void)mvwprintw(Ea1win, 3, 10, "%d/%d ", nsat, nvis);
    }

	for (i = 0; i < 8; i++) {
	    unsigned char sv, mode, sn, status;

	    off = 40 + 4 * i;
	    sv = (unsigned char)getub(buf, off);
	    mode = (unsigned char)getub(buf, off + 1);
	    sn = (unsigned char)getub(buf, off + 2);
	    status = (unsigned char)getub(buf, off + 3);
	    (void)wmove(Eawin, (int)(i + 2), 3);
	    (void)wprintw(Eawin, " %3d", sv);
	    EaSVlines[i] = sv;
	    if (mode <= (unsigned char)8)
		(void)wprintw(Eawin, " %4s", sv_mode[mode]);
	    else
		(void)wprintw(Eawin, "    -");
	    (void)wprintw(Eawin, " %3d", sn);
	    /*@ -predboolothers @*/
	    (void)wprintw(Eawin, " %c%c%c%c%c%c%c%c", (status & 0x80) ? 'p' : ' ',	/* used for pos fix  */
			  (status & 0x40) ? 'M' : ' ',	/* momentum alert    */
			  (status & 0x20) ? 's' : ' ',	/* anti-spoof   */
			  (status & 0x10) ? 'U' : ' ',	/* unhealthy     */
			  (status & 0x08) ? 'I' : ' ',	/* inaccurate   */
			  (status & 0x04) ? 'S' : ' ',	/* spare             */
			  (status & 0x02) ? 't' : ' ',	/* used for time sol */
			  (status & 0x01) ? 'P' : ' ');	/* parity error      */
	    /*@ +predboolothers @*/
	}

	monitor_log("Ea =");
	break;

    case ONCTYPE('B', 'b'):
    {
	unsigned int Bblines[12];
	unsigned int Bblines_mask;
	unsigned int next_line;
	unsigned char sv;
	unsigned int ch;

	ch = (unsigned int)getub(buf, 4);
	if (ch > 12)
	    ch = 12;
	/* Try to align the entries for each SV of the Bb message at
	 * the same lines as in the Ea message.
	 */
	memset(Bblines, 0, sizeof(Bblines));
	Bblines_mask = 0;
	for (i = 0; i < ch; i++) {
	    off = 5 + 7 * i;
	    sv = (unsigned char)getub(buf, off);
	    /*@ -boolops @*/
	    for (j = 0; j < 8; j++)
		if (EaSVlines[j] == sv && !(Bblines_mask & (1 << (j + 2)))) {
		    Bblines[i] = j + 2;
		    Bblines_mask |= 1 << Bblines[i];
		}
	    /*@ +boolops @*/
	}
	/* SVs not seen in Ea fill lines left over. */
	next_line = 2;
	for (i = 0; i < ch; i++) {
	    if (Bblines[i] == 0) {
		while (Bblines_mask & (1 << next_line))
		    next_line++;
		Bblines[i] = next_line++;
		Bblines_mask |= 1 << Bblines[i];
	    }
	}
	/* Ready to print on precalculated lines. */
	for (i = 0; i < ch; i++) {
	    int doppl, el, az, health;

	    off = 5 + 7 * i;
	    sv = (unsigned char)getub(buf, off);
	    doppl = (int)getbesw(buf, off + 1);
	    el = (int)getub(buf, off + 3);
	    az = (int)getbeuw(buf, off + 4);
	    health = (int)getub(buf, off + 5);

	    (void)wmove(Bbwin, (int)Bblines[i], 1);
	    (void)wprintw(Bbwin, "%3d %3d %2d %5d %c%c", sv, az, el, doppl, (health & 0x02) ? 'U' : ' ',	/* unhealthy */
			  (health & 0x01) ? 'R' : ' ');	/* removed   */
	}

	for (i = 2; i < 14; i++)
	    /*@ -boolops @*/
	    if (!(Bblines_mask & (1 << i))) {
		(void)wmove(Bbwin, (int)i, 1);
		(void)wprintw(Bbwin, "                   ");
	    }
	/*@ +boolops @*/
    }

	monitor_log("Bb =");
	break;

    case ONCTYPE('E', 'n'):
    {
	unsigned char traim, ctrl, pulse, sync, sol_stat, status;
	float alarm, sigma;

	traim = (unsigned char)getub(buf, 5);
	alarm = (float)(getbeuw(buf, 6) / 10.);
	ctrl = (unsigned char)getub(buf, 8);
	pulse = (unsigned char)getub(buf, 19);
	sync = (unsigned char)getub(buf, 20);
	sol_stat = (unsigned char)getub(buf, 21);
	status = (unsigned char)getub(buf, 22);
	sigma = (float)(getbeuw(buf, 23));

	/*@ -predboolothers @*/
	(void)mvwprintw(Enwin, 1, 24, "%3s", traim ? "on" : "off");
	(void)mvwprintw(Enwin, 2, 18, "%6.1f us", alarm);
	(void)mvwprintw(Enwin, 3, 13, "%14s", pps_ctrl[ctrl]);
	(void)mvwprintw(Enwin, 4, 24, "%3s", pulse ? "on" : "off");
	(void)mvwprintw(Enwin, 5, 24, "%3s", pps_sync[sync]);
	(void)mvwprintw(Enwin, 6, 20, "%7s", traim_sol[sol_stat]);
	(void)mvwprintw(Enwin, 7, 11, "%16s", traim_status[status]);
	(void)mvwprintw(Enwin, 8, 18, "%6.3f us", sigma * 0.001);
	/*@ +predboolothers @*/
    }

	monitor_log("En =");
	break;

    case ONCTYPE('B', 'o'):
    {
	unsigned char utc_offset;

	utc_offset = (unsigned char)getub(buf, 4);

	if (utc_offset != (unsigned char)0)
	    (void)mvwprintw(Bowin, 2, 2, "GPS%+4d", utc_offset);
	else
	    (void)mvwprintw(Bowin, 2, 2, "unknown", utc_offset);
    }

	monitor_log("Bo =");
	break;

    case ONCTYPE('A', 'y'):
    {
	double pps_delay;

	pps_delay = getbesl(buf, 4) / 1000000.0;

	(void)mvwprintw(Aywin, 2, 2, " %7.3f ms", pps_delay);
    }

	monitor_log("Ay =");
	break;

    case ONCTYPE('A', 't'):
    {
	unsigned char mode;

	mode = (unsigned char)getub(buf, 4);

	(void)mvwprintw(Atwin, 2, 1, "%6s", pos_hold_mode[mode]);
    }

	monitor_log("At =");
	break;

    case ONCTYPE('A', 's'):
    {
	double lat, lon, alt;

	lat = getbesl(buf, 4) / 3600000.0;
	lon = getbesl(buf, 8) / 3600000.0;
	alt = getbesl(buf, 12) / 100.0;

	(void)mvwprintw(Aswin, 1, 5, "%10.6lf %c",
			fabs(lat), lat < 0 ? 'S' : lat > 0 ? 'N' : ' ');
	(void)mvwprintw(Aswin, 2, 5, "%10.6lf %c",
			fabs(lon), lon < 0 ? 'W' : lon > 0 ? 'E' : ' ');
	(void)mvwprintw(Aswin, 3, 7, "%8.2f m", alt);
    }

	monitor_log("As =");
	break;

    default:
	monitor_log("%c%c =", buf[2], buf[3]);
	break;
    }
}

static int oncore_command(char line[]UNUSED)
{
    return COMMAND_UNKNOWN;
}

static void oncore_wrap(void)
{
    (void)delwin(Ea1win);
    (void)delwin(Eawin);
    (void)delwin(Bbwin);
    (void)delwin(Enwin);
    (void)delwin(Bowin);
    (void)delwin(Aywin);
    (void)delwin(Atwin);
    (void)delwin(Aswin);
}

const struct monitor_object_t oncore_mmt = {
    .initialize = oncore_initialize,
    .update = oncore_update,
    .command = oncore_command,
    .wrap = oncore_wrap,
    .min_y = 20,.min_x = 80,	/* size of the device window */
    .driver = &oncore_binary,
};

#endif /* defined(ONCORE_ENABLE) && defined(BINARY_ENABLE) */
