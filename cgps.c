/*
 * Copyright (c) 2005 Jeff Francis <jeff@gritch.org>
 * BSD terms apply: see the filr COPYING in the distribution root for details.
 */

/*
  Jeff Francis
  jeff@gritch.org

  Kind of a curses version of xgps for use with gpsd.
*/

/*
 * The True North compass fails with current gpsd versions for reasons
 * the dev team has been unable to diagnose due to not having test hardware.
 * The support for it is conditioned out in order to simplify moving
 * to the new JSON-based protocol and reduce startup time.
 */
#undef TRUENORTH

/* ==================================================================
   These #defines should be modified if changing the number of fields
   to be displayed.
   ================================================================== */

/* This defines how much overhead is contained in the 'datawin' window
   (eg, box around the window takes two lines). */
#define DATAWIN_OVERHEAD 2

/* This defines how much overhead is contained in the 'satellites'
   window (eg, box around the window takes two lines, plus the column
   headers take another line). */
#define SATWIN_OVERHEAD 3

/* This is how many display fields are output in the 'datawin' window
   when in GPS mode.  Change this value if you add or remove fields
   from the 'datawin' window for the GPS mode. */
#define DATAWIN_GPS_FIELDS 8

/* Count of optional fields that we'll display if we have the room. */
#define DATAWIN_OPTIONAL_FIELDS 7

/* This is how many display fields are output in the 'datawin' window
   when in COMPASS mode.  Change this value if you add or remove fields
   from the 'datawin' window for the COMPASS mode. */
#define DATAWIN_COMPASS_FIELDS 6

/* This is how far over in the 'datawin' window to indent the field
   descriptions. */
#define DATAWIN_DESC_OFFSET 2

/* This is how far over in the 'datawin' window to indent the field
   values. */
#define DATAWIN_VALUE_OFFSET 17

/* This is the width of the 'datawin' window.  It's recommended to
   keep DATAWIN_WIDTH + SATELLITES_WIDTH <= 80 so it'll fit on a
   "standard" 80x24 screen. */
#define DATAWIN_WIDTH 45

/* This is the width of the 'satellites' window.  It's recommended to
   keep DATAWIN_WIDTH + SATELLITES_WIDTH <= 80 so it'll fit on a
   "standard" 80x24 screen. */
#define SATELLITES_WIDTH 35

/* ================================================================
   You shouldn't have to modify any #define values below this line.
   ================================================================ */

/* This is the minimum ysize we'll accept for the 'datawin' window in
   GPS mode. */
#define MIN_GPS_DATAWIN_YSIZE (DATAWIN_GPS_FIELDS + DATAWIN_OVERHEAD)

/* And the maximum ysize we'll try to use */
#define MAX_GPS_DATAWIN_YSIZE (DATAWIN_GPS_FIELDS + DATAWIN_OPTIONAL_FIELDS + DATAWIN_OVERHEAD)

/* This is the minimum ysize we'll accept for the 'datawin' window in
   COMPASS mode. */
#define MIN_COMPASS_DATAWIN_YSIZE (DATAWIN_COMPASS_FIELDS + DATAWIN_OVERHEAD)

/* This is the maximum number of satellites gpsd can track. */
#define MAX_POSSIBLE_SATS (MAXCHANNELS - 2)

/* This is the maximum ysize we need for the 'satellites' window. */
#define MAX_SATWIN_SIZE (MAX_POSSIBLE_SATS + SATWIN_OVERHEAD)

/* Minimum xsize to display 3rd window with ECEF's, etc. */
#define MIN_ERRWIN_SIZE 100

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <curses.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <ctype.h>

#include "gpsd_config.h"
#include "gps.h"
#include "compiler.h"	/* for UNUSED */
#include "gpsdclient.h"
#include "revision.h"
#include "os_compat.h"

static struct gps_data_t gpsdata;
static time_t status_timer;	/* Time of last state change. */
static int state = 0;		/* or MODE_NO_FIX=1, MODE_2D=2, MODE_3D=3 */
static float altfactor = METERS_TO_FEET;
static float speedfactor = MPS_TO_MPH;
static char *altunits = "ft";
static char *speedunits = "mph";
static struct fixsource_t source;
#ifdef CLIENTDEBUG_ENABLE
static int debug;
#endif /* CLIENTDEBUG_ENABLE */

static WINDOW *datawin, *satellites, *messages;

static bool raw_flag = false;
static bool ecef_flag = false;      /* tall screen, show ECEF */
static bool silent_flag = false;
static bool magnetic_flag = false;
static int window_ysize = 0;
static int display_sats = 0;
#ifdef TRUENORTH
static bool compass_flag = false;
#endif /* TRUENORTH */

/* pseudo-signals indicating reason for termination */
#define CGPS_QUIT	0	/* voluntary termination */
#define GPS_GONE	-1	/* GPS device went away */
#define GPS_ERROR	-2	/* low-level failure in GPS read */
#define GPS_TIMEOUT	-3	/* low-level failure in GPS waiting */

/* Function to call when we're all done.  Does a bit of clean-up. */
static void die(int sig)
{
    if (!isendwin())
    {
	/* Move the cursor to the bottom left corner. */
	(void)mvcur(0, COLS - 1, LINES - 1, 0);

	/* Put input attributes back the way they were. */
	(void)echo();

	/* Done with curses. */
	(void)endwin();
    }

    /* We're done talking to gpsd. */
    (void)gps_close(&gpsdata);

    switch (sig) {
    case CGPS_QUIT:
	break;
    case GPS_GONE:
	(void)fprintf(stderr, "cgps: GPS hung up.\n");
	break;
    case GPS_ERROR:
	(void)fprintf(stderr, "cgps: GPS read returned error\n");
	break;
    case GPS_TIMEOUT:
	(void)fprintf(stderr, "cgps: GPS timeout\n");
	break;
    default:
	(void)fprintf(stderr, "cgps: caught signal %d\n", sig);
	break;
    }

    /* Bye! */
    exit(EXIT_SUCCESS);
}

/* format an ECEF p and v into a string, handle NAN, INFINITE */
static char *ecef_to_str(double pos, double vel, double factor, char *units)
{
    static char buf[128];

    if (isfinite(pos) == 0) {
	if (isfinite(vel) == 0) {
	    return "  n/a    n/a ";
        } else {
	    (void)snprintf(buf, sizeof(buf), "  n/a %8.2f%.4s/s",
                           vel * factor, units);
        }
    } else {
	(void)snprintf(buf, sizeof(buf), "% 14.4f%.4s %8.2f%.4s/s",
                       pos * factor, units,  vel * factor, units);
    }
    return buf;
}

static enum deg_str_type deg_type = deg_dd;

static void windowsetup(void)
/* initialize curses and set up screen windows */
{
    /* Set the window sizes per the following criteria:
     *
     * 1.  Set the window size to display the maximum number of
     * satellites possible, but not more than can be fit in a
     * window the size of the GPS report window. We have to set
     * the limit that way because MAXCHANNELS has been made large
     * in order to prepare for survey-grade receivers..
     *
     * 2.  If the screen size will not allow for the full complement of
     * satellites to be displayed, set the windows sizes smaller, but
     * not smaller than the number of lines necessary to display all of
     * the fields in the 'datawin'.  The list of displayed satellites
     * will be truncated to fit the available window size.  (TODO: If
     * the satellite list is truncated, omit the satellites not used to
     * obtain the current fix.)
     *
     * 3.  If the screen is tall enough to display all possible
     * satellites (MAXCHANNELS - 2) with space still left at the bottom,
     * add a window at the bottom in which to scroll raw gpsd data.
     *
     * 4.  If the screen is wide enough to display extra data, add a
     * window on the right for PDOP, etc.
     */
    int xsize, ysize;

    /* Fire up curses. */
    (void)initscr();
    (void)noecho();
    getmaxyx(stdscr, ysize, xsize);

#ifdef TRUENORTH
    if (compass_flag) {
	if (ysize == MIN_COMPASS_DATAWIN_YSIZE) {
	    raw_flag = false;
	    window_ysize = MIN_COMPASS_DATAWIN_YSIZE;
	} else if (ysize > MIN_COMPASS_DATAWIN_YSIZE) {
	    raw_flag = true;
	    window_ysize = MIN_COMPASS_DATAWIN_YSIZE;
	} else {
	    (void)mvprintw(0, 0,
			   "Your screen must be at least 80x%d to run cgps.",
			   MIN_COMPASS_DATAWIN_YSIZE);
	    (void)refresh();
	    (void)sleep(5);
	    die(0);
	}
    } else
#endif /* TRUENORTH */
    {
	if (ysize > MAX_GPS_DATAWIN_YSIZE + 7) {
	    raw_flag = true;
	    ecef_flag = true;
	    window_ysize = MAX_GPS_DATAWIN_YSIZE + 3;
	} else if (ysize > MAX_GPS_DATAWIN_YSIZE) {
	    raw_flag = true;
	    ecef_flag = false;
	    window_ysize = MAX_GPS_DATAWIN_YSIZE;
	} else if (ysize == MAX_GPS_DATAWIN_YSIZE) {
	    raw_flag = false;
	    ecef_flag = false;
	    window_ysize = MAX_GPS_DATAWIN_YSIZE;
	} else if (ysize > MIN_GPS_DATAWIN_YSIZE) {
	    raw_flag = true;
	    ecef_flag = false;
	    window_ysize = MIN_GPS_DATAWIN_YSIZE;
	} else if (ysize == MIN_GPS_DATAWIN_YSIZE) {
	    raw_flag = false;
	    ecef_flag = false;
	    window_ysize = MIN_GPS_DATAWIN_YSIZE;
	} else {
	    (void)mvprintw(0, 0,
			   "Your screen must be at least 80x%d to run cgps.",
			   MIN_GPS_DATAWIN_YSIZE);
	    (void)refresh();
	    (void)sleep(5);
	    die(0);
	}
	display_sats = window_ysize - SATWIN_OVERHEAD - (int)raw_flag;
    }

#ifdef TRUENORTH
    /* Set up the screen for either a compass or a gps receiver. */
    if (compass_flag) {
	/* We're a compass, set up accordingly. */

	datawin = newwin(window_ysize, DATAWIN_WIDTH, 0, 0);
	(void)nodelay(datawin, (bool) TRUE);
	if (raw_flag) {
	    messages = newwin(0, 0, window_ysize, 0);

	    (void)scrollok(messages, true);
	    (void)wsetscrreg(messages, 0, ysize - (window_ysize));
	}

	(void)refresh();

	/* Do the initial field label setup. */
	(void)mvwprintw(datawin, 1, DATAWIN_DESC_OFFSET, "Time:");
	(void)mvwprintw(datawin, 2, DATAWIN_DESC_OFFSET, "Heading:");
	(void)mvwprintw(datawin, 3, DATAWIN_DESC_OFFSET, "Pitch:");
	(void)mvwprintw(datawin, 4, DATAWIN_DESC_OFFSET, "Roll:");
	(void)mvwprintw(datawin, 5, DATAWIN_DESC_OFFSET, "Dip:");
	(void)mvwprintw(datawin, 6, DATAWIN_DESC_OFFSET, "Rcvr Type:");
	(void)wborder(datawin, 0, 0, 0, 0, 0, 0, 0, 0);

    } else
#endif /* TRUENORTH */
    {
	/* We're a GPS, set up accordingly. */

	datawin = newwin(window_ysize, DATAWIN_WIDTH, 0, 0);
	satellites =
	    newwin(window_ysize, SATELLITES_WIDTH, 0, DATAWIN_WIDTH);
	(void)nodelay(datawin, (bool) TRUE);
	if (raw_flag) {
	    messages =
		newwin(ysize - (window_ysize), xsize, window_ysize, 0);

	    (void)scrollok(messages, true);
	    (void)wsetscrreg(messages, 0, ysize - (window_ysize));
	}

	(void)refresh();

	/* Do the initial field label setup. */
	(void)mvwprintw(datawin, 1, DATAWIN_DESC_OFFSET, "Time:");
	(void)mvwprintw(datawin, 2, DATAWIN_DESC_OFFSET, "Latitude:");
	(void)mvwprintw(datawin, 3, DATAWIN_DESC_OFFSET, "Longitude:");
	(void)mvwprintw(datawin, 4, DATAWIN_DESC_OFFSET, "Altitude:");
	(void)mvwprintw(datawin, 5, DATAWIN_DESC_OFFSET, "Speed:");
	(void)mvwprintw(datawin, 6, DATAWIN_DESC_OFFSET, "Heading:");
	(void)mvwprintw(datawin, 7, DATAWIN_DESC_OFFSET, "Climb:");
	(void)mvwprintw(datawin, 8, DATAWIN_DESC_OFFSET, "Status:");

	/* Note that the following fields are exceptions to the
	 * sizing rule.  The minimum window size does not include these
	 * fields, if the window is too small, they get excluded.  This
	 * may or may not change if/when the output for these fields is
	 * fixed and/or people request their permanance.  They're only
	 * there in the first place because I arbitrarily thought they
	 * sounded interesting. ;^) */

	if (window_ysize >= MAX_GPS_DATAWIN_YSIZE) {
	    int row = 9;

            (void)mvwprintw(datawin, row++, DATAWIN_DESC_OFFSET,
                            "Longitude Err:");
            (void)mvwprintw(datawin, row++, DATAWIN_DESC_OFFSET,
                            "Latitude Err:");
            (void)mvwprintw(datawin, row++, DATAWIN_DESC_OFFSET,
                            "Altitude Err:");

	    if (ecef_flag){
		(void)mvwprintw(datawin, row++, DATAWIN_DESC_OFFSET,
				"ECEF X, VX:");
		(void)mvwprintw(datawin, row++, DATAWIN_DESC_OFFSET,
				"ECEF Y, VY:");
		(void)mvwprintw(datawin, row++, DATAWIN_DESC_OFFSET,
				"ECEF Z, VZ:");
	    }
            (void)mvwprintw(datawin, row++, DATAWIN_DESC_OFFSET, "Course Err:");
            (void)mvwprintw(datawin, row++, DATAWIN_DESC_OFFSET, "Speed Err:");

	    /* it's actually esr that thought *these* were interesting */
	    (void)mvwprintw(datawin, row++, DATAWIN_DESC_OFFSET,
			    "Time offset:");
	    (void)mvwprintw(datawin, row++, DATAWIN_DESC_OFFSET,
			    "Grid Square:");
	}

	(void)wborder(datawin, 0, 0, 0, 0, 0, 0, 0, 0);
	/* PRN is not unique, starting at 1, for GPS, GAILEO, etc. */
	(void)mvwprintw(satellites, 1, 1, " PRN  Elev   Azim   SNR   Used  ");
	(void)wborder(satellites, 0, 0, 0, 0, 0, 0, 0, 0);
    }
}


static void resize(int sig UNUSED)
/* cope with terminal resize */
{
    if (!isendwin())
    {
	(void)endwin();
	windowsetup();
    }
}

#ifdef TRUENORTH
/* This gets called once for each new compass sentence. */
static void update_compass_panel(struct gps_data_t *gpsdata)
{
    char scr[128];
    /* Print time/date. */
    if (isnan(gpsdata->fix.time) == 0) {
	(void)unix_to_iso8601(gpsdata->fix.time, scr, sizeof(scr));
    } else
	(void)snprintf(scr, sizeof(scr), "    n/a");
    (void)mvwprintw(datawin, 1, DATAWIN_VALUE_OFFSET, "%-*s", 27, scr);

    /* Fill in the heading. */
    if (isnan(gpsdata->fix.track) == 0) {
	(void)snprintf(scr, sizeof(scr), "%.1f degrees", gpsdata->fix.track);
    } else
	(void)snprintf(scr, sizeof(scr), "    n/a");
    (void)mvwprintw(datawin, 2, DATAWIN_VALUE_OFFSET, "%-*s", 27, scr);

    /* Fill in the climb. */
    if (isnan(gpsdata->fix.climb) == 0) {
	(void)snprintf(scr, sizeof(scr), "%.2f", gpsdata->fix.climb);
    } else
	(void)snprintf(scr, sizeof(scr), "    n/a");
    (void)mvwprintw(datawin, 3, DATAWIN_VALUE_OFFSET, "%-*s", 27, scr);

    /* Fill in the speed. */
    if (isnan(gpsdata->fix.speed) == 0)
	(void)snprintf(scr, sizeof(scr), "%.2f", gpsdata->fix.speed);
    else
	(void)snprintf(scr, sizeof(scr), "    n/a");
    (void)mvwprintw(datawin, 4, DATAWIN_VALUE_OFFSET, "%-*s", 27, scr);

    /* Fill in the altitude. */
    if (isnan(gpsdata->fix.altitude) == 0)
	(void)snprintf(scr, sizeof(scr), "%.3f", gpsdata->fix.altitude);
    else
	(void)snprintf(scr, sizeof(scr), "    n/a");
    (void)mvwprintw(datawin, 5, DATAWIN_VALUE_OFFSET, "%-*s", 27, scr);

    /* When we need to fill in receiver type again, do it here. */
    (void)mvwprintw(datawin, 6, DATAWIN_VALUE_OFFSET, "%-*s", 27, scr);

    /* Be quiet if the user requests silence. */
    if (!silent_flag && raw_flag) {
	(void)waddstr(messages, message);
    }

    (void)wrefresh(datawin);
    if (raw_flag) {
	(void)wrefresh(messages);
    }
}
#endif /* TRUENORTH */

/* sort the skyviews
 * Used = Y first, then used = N
 * then sort by PRN
 */
static int sat_cmp(const void *p1, const void *p2)
{

   if ( ((struct satellite_t*)p2)->used - ((struct satellite_t*)p1)->used ) {
	return ((struct satellite_t*)p2)->used - ((struct satellite_t*)p1)->used;
   }
   return ((struct satellite_t*)p1)->PRN - ((struct satellite_t*)p2)->PRN;
}


static void update_gps_panel(struct gps_data_t *gpsdata)
/* This gets called once for each new GPS sentence. */
{
    int newstate;
    int row = 9;
    char scr[128], *s;

    /* This is for the satellite status display.  Originally lifted from
     * xgps.c.  Note that the satellite list may be truncated based on
     * available screen size, or may only show satellites used for the
     * fix.  */
    (void)mvwprintw(satellites, 0, 27, "Seen %2d", gpsdata->satellites_visible);
    (void)mvwprintw(satellites, 1, 32, "%2d", gpsdata->satellites_used);

    if (gpsdata->satellites_visible != 0) {
	int i;
	qsort( gpsdata->skyview, gpsdata->satellites_visible,
		sizeof( struct satellite_t), sat_cmp);
	if (display_sats >= MAX_POSSIBLE_SATS) {
	    for (i = 0; i < MAX_POSSIBLE_SATS; i++) {
		if (i < gpsdata->satellites_visible) {
                    /* allow for 3 digit elevation,
                     * ublox uses -91 to signal something undocumented */
		    (void)snprintf(scr, sizeof(scr),
				   " %3d   %3d    %3d    %2d      %c",
				   gpsdata->skyview[i].PRN,
				   gpsdata->skyview[i].elevation,
				   gpsdata->skyview[i].azimuth,
				   (int)gpsdata->skyview[i].ss,
				   gpsdata->skyview[i].used ? 'Y' : 'N');
		} else {
		    (void)strlcpy(scr, "", sizeof(scr));
		}
		(void)mvwprintw(satellites, i + 2, 1, "%-*s",
				SATELLITES_WIDTH - 3, scr);
	    }
	} else {
	    int n = 0;
	    for (i = 0; i < MAX_POSSIBLE_SATS; i++) {
		if (n < display_sats) {
		    if ((i < gpsdata->satellites_visible)
			&& (gpsdata->skyview[i].used
			    || (gpsdata->satellites_visible <= display_sats))) {
			(void)snprintf(scr, sizeof(scr),
				       " %3d    %2d    %3d    %2d      %c",
				       gpsdata->skyview[i].PRN,
				       gpsdata->skyview[i].elevation,
				       gpsdata->skyview[i].azimuth,
				       (int)gpsdata->skyview[i].ss,
				       gpsdata->skyview[i].used ? 'Y' : 'N');
			(void)mvwprintw(satellites, n + 2, 1, "%-*s",
					SATELLITES_WIDTH - 3, scr);
			n++;
		    }
		}
	    }

	    if (n < display_sats) {
		for (i = n; i <= display_sats; i++) {
		    (void)mvwprintw(satellites, i + 2, 1, "%-*s",
				    SATELLITES_WIDTH - 3, "");
		}
	    }

	}
    }

    row = 1;
    /* Print time/date. */
    if (isnan(gpsdata->fix.time) == 0) {
	(void)unix_to_iso8601(gpsdata->fix.time, scr, sizeof(scr));
    } else
	(void)snprintf(scr, sizeof(scr), "    n/a");
    (void)mvwprintw(datawin, row++, DATAWIN_VALUE_OFFSET, "%-*s", 27, scr);


    /* Fill in the latitude. */
    if (gpsdata->fix.mode >= MODE_2D && isnan(gpsdata->fix.latitude) == 0) {
	(void)snprintf(scr, sizeof(scr), "  %s %c",
		       deg_to_str(deg_type, fabs(gpsdata->fix.latitude)),
		       (gpsdata->fix.latitude < 0) ? 'S' : 'N');
    } else
	(void)snprintf(scr, sizeof(scr), "    n/a");
    (void)mvwprintw(datawin, row++, DATAWIN_VALUE_OFFSET, "%-*s", 27, scr);

    /* Fill in the longitude. */
    if (gpsdata->fix.mode >= MODE_2D && isfinite(gpsdata->fix.longitude) != 0) {
	(void)snprintf(scr, sizeof(scr), "  %s %c",
		       deg_to_str(deg_type, fabs(gpsdata->fix.longitude)),
		       (gpsdata->fix.longitude < 0) ? 'W' : 'E');
    } else
	(void)snprintf(scr, sizeof(scr), "    n/a");
    (void)mvwprintw(datawin, row++, DATAWIN_VALUE_OFFSET, "%-*s", 27, scr);

    /* Fill in the altitude. */
    if (gpsdata->fix.mode >= MODE_3D && isfinite(gpsdata->fix.altitude) != 0)
	(void)snprintf(scr, sizeof(scr), "%9.3f %s",
		       gpsdata->fix.altitude * altfactor, altunits);
    else
	(void)snprintf(scr, sizeof(scr), "    n/a");
    (void)mvwprintw(datawin, row++, DATAWIN_VALUE_OFFSET, "%-*s", 27, scr);

    /* Fill in the speed. */
    if (gpsdata->fix.mode >= MODE_2D && isfinite(gpsdata->fix.track) != 0)
	(void)snprintf(scr, sizeof(scr), "%8.2f %s",
		       gpsdata->fix.speed * speedfactor, speedunits);
    else
	(void)snprintf(scr, sizeof(scr), "    n/a");
    (void)mvwprintw(datawin, row++, DATAWIN_VALUE_OFFSET, "%-*s", 27, scr);

    /* Fill in the heading. */
    if (gpsdata->fix.mode >= MODE_2D && isfinite(gpsdata->fix.track) != 0) {
	double magheading = true2magnetic(gpsdata->fix.latitude,
					 gpsdata->fix.longitude,
					  gpsdata->fix.track);
	if (!magnetic_flag || isnan(magheading) != 0) {
	    (void)snprintf(scr, sizeof(scr), "%5.1f deg (true)",
			   gpsdata->fix.track);
	} else {
	    (void)snprintf(scr, sizeof(scr), "%5.1f deg (mag) ",
		magheading);
	}
    } else
	(void)snprintf(scr, sizeof(scr), "    n/a");
    (void)mvwprintw(datawin, row++, DATAWIN_VALUE_OFFSET, "  %-*s", 25, scr);

    /* Fill in the rate of climb. */
    if (gpsdata->fix.mode >= MODE_3D && isnan(gpsdata->fix.climb) == 0)
	(void)snprintf(scr, sizeof(scr), "%8.2f %s/min",
		       gpsdata->fix.climb * altfactor * 60, altunits);
    else
	(void)snprintf(scr, sizeof(scr), "    n/a");
    (void)mvwprintw(datawin, row++, DATAWIN_VALUE_OFFSET, "%-*s", 27, scr);

    /* Fill in the GPS status and the time since the last state
     * change. */
    if (gpsdata->online == 0) {
	newstate = 0;
	(void)snprintf(scr, sizeof(scr), "OFFLINE");
    } else {
	newstate = gpsdata->fix.mode;
	switch (gpsdata->fix.mode) {
	case MODE_2D:
	    (void)snprintf(scr, sizeof(scr), "2D %sFIX (%d secs)",
			   (gpsdata->status ==
			    STATUS_DGPS_FIX) ? "DIFF " : "",
			   (int)(time(NULL) - status_timer));
	    break;
	case MODE_3D:
	    (void)snprintf(scr, sizeof(scr), "3D %sFIX (%d secs)",
			   (gpsdata->status ==
			    STATUS_DGPS_FIX) ? "DIFF " : "",
			   (int)(time(NULL) - status_timer));
	    break;
	default:
	    (void)snprintf(scr, sizeof(scr), "NO FIX (%d secs)",
			   (int)(time(NULL) - status_timer));
	    break;
	}
    }
    (void)mvwprintw(datawin, row++, DATAWIN_VALUE_OFFSET + 1, "%-*s", 26, scr);

    /* Note that the following fields are exceptions to the
     * sizing rule.  The minimum window size does not include these
     * fields, if the window is too small, they get excluded.  This
     * may or may not change if/when the output for these fields is
     * fixed and/or people request their permanence.  They're only
     * there in the first place because I arbitrarily thought they
     * sounded interesting. ;^) */

    if (window_ysize >= (MIN_GPS_DATAWIN_YSIZE + 5)) {
	/* Fill in the estimated horizontal position error. */
	if (isnan(gpsdata->fix.epx) == 0)
	    (void)snprintf(scr, sizeof(scr), "+/- %d %s",
			   (int)(gpsdata->fix.epx * altfactor), altunits);
	else
	    (void)snprintf(scr, sizeof(scr), "  n/a");
	(void)mvwprintw(datawin, row++, DATAWIN_VALUE_OFFSET + 5, "%-*s", 22,
			scr);

	if (isnan(gpsdata->fix.epy) == 0)
	    (void)snprintf(scr, sizeof(scr), "+/- %d %s",
			   (int)(gpsdata->fix.epy * altfactor), altunits);
	else
	    (void)snprintf(scr, sizeof(scr), "  n/a");
	(void)mvwprintw(datawin, row++, DATAWIN_VALUE_OFFSET + 5, "%-*s", 22,
			scr);

	/* Fill in the estimated vertical position error. */
	if (isnan(gpsdata->fix.epv) == 0)
	    (void)snprintf(scr, sizeof(scr), "+/- %d %s",
			   (int)(gpsdata->fix.epv * altfactor), altunits);
	else
	    (void)snprintf(scr, sizeof(scr), "  n/a");
	(void)mvwprintw(datawin, row++, DATAWIN_VALUE_OFFSET + 5, "%-*s", 22,
			scr);


	/* extra large screen, show more ECEF */
	if (ecef_flag) {
            char *estr;

	    /* Fill in the ECEF's. */
	    estr = ecef_to_str(gpsdata->fix.ecef.x, gpsdata->fix.ecef.vx,
                               1, " m");
	    (void)mvwprintw(datawin, row++, DATAWIN_VALUE_OFFSET - 3, "%-*s",
                            27, estr);

	    estr = ecef_to_str(gpsdata->fix.ecef.y, gpsdata->fix.ecef.vy,
                               1, " m");
	    (void)mvwprintw(datawin, row++, DATAWIN_VALUE_OFFSET - 3, "%-*s",
                            27, estr);

	    estr = ecef_to_str(gpsdata->fix.ecef.z, gpsdata->fix.ecef.vz,
                               1, " m");
	    (void)mvwprintw(datawin, row++, DATAWIN_VALUE_OFFSET - 3, "%-*s",
                            27, estr);
	}

	/* Fill in the estimated track error. */
	if (isnan(gpsdata->fix.epd) == 0)
	    (void)snprintf(scr, sizeof(scr), "+/- %d deg",
			   (int)(gpsdata->fix.epd));
	else
	    (void)snprintf(scr, sizeof(scr), "  n/a");
	(void)mvwprintw(datawin, row++, DATAWIN_VALUE_OFFSET + 5, "%-*s", 22,
			scr);

 	/* Fill in the estimated speed error. */
 	if (isnan(gpsdata->fix.eps) == 0)
 	    (void)snprintf(scr, sizeof(scr), "+/- %d %s",
 			   (int)(gpsdata->fix.eps * speedfactor), speedunits);
 	else
 	    (void)snprintf(scr, sizeof(scr), "  n/a");
 	(void)mvwprintw(datawin, row++, DATAWIN_VALUE_OFFSET + 5, "%-*s", 22,
			scr);

	/* Fill in the time offset, milli seconds. */
	if (isfinite(gpsdata->fix.time) != 0)
	    (void)snprintf(scr, sizeof(scr), "%6.3f sec",
			   (double)(timestamp()-gpsdata->fix.time));
	else
	    (void)snprintf(scr, sizeof(scr), "  n/a");
	(void)mvwprintw(datawin, row++, DATAWIN_VALUE_OFFSET + 8, "%-*s", 18,
			scr);
	/* Fill in the grid square (esr thought *this* one was interesting). */
	if (isnan(gpsdata->fix.longitude)==0 && isnan(gpsdata->fix.latitude)==0)
	    s = maidenhead(gpsdata->fix.latitude,gpsdata->fix.longitude);
	else
	    s = "  n/a";
	(void)mvwprintw(datawin, row++, DATAWIN_VALUE_OFFSET + 9, "%-*s",
			18, s);

    }

    if ( raw_flag) {
        /* Be quiet if the user requests silence. */
        const char *sr;
        if (!silent_flag && (sr = gps_data(gpsdata)) != NULL) {
            char *p, *pe;

            /* make a copy of the const char *, then trim trailing spaces */
            p = strdup(sr);
            if ( NULL != p ) {
		pe = p + strlen(p);
		for ( ; --pe > p && isspace((int) *pe); *pe = '\0')
		    ;
		(void)wprintw(messages, "%s\n", p);
		free(p);
            }
        }
        (void)wrefresh(messages);
    }

    /* Reset the status_timer if the state has changed. */
    if (newstate != state) {
	status_timer = time(NULL);
	state = newstate;
    }

    (void)wrefresh(datawin);
    (void)wrefresh(satellites);
}

static void usage(char *prog)
{
    (void)fprintf(stderr,
		  "Usage: %s [-h] [-V] [-l {d|m|s}] [server[:port:[device]]]\n\n"
		  "  -h	  Show this help, then exit\n"
		  "  -V	  Show version, then exit\n"
		  "  -s	  Be silent (don't print raw gpsd data)\n"
		  "  -l {d|m|s}  Select lat/lon format\n"
		  "		d = DD.ddddddd\n"
		  "		m = DD MM.mmmmmm'\n"
		  "		s = DD MM' SS.sssss\"\n"
		  " -m      Display heading as the estimated magnetic heading\n"
		  "         Valid only for USA (Lower 48 + AK) and Western Europe.\n",
		  prog);

    exit(EXIT_FAILURE);
}

/*
 * No protocol dependencies above this line
 */

int main(int argc, char *argv[])
{
    int option;
    unsigned int flags = WATCH_ENABLE;
    int wait_clicks = 0;  /* cycles to wait before gpsd timeout */

    switch (gpsd_units()) {
    case imperial:
	altfactor = METERS_TO_FEET;
	altunits = "ft";
	speedfactor = MPS_TO_MPH;
	speedunits = "mph";
	break;
    case nautical:
	altfactor = METERS_TO_FEET;
	altunits = "ft";
	speedfactor = MPS_TO_KNOTS;
	speedunits = "knots";
	break;
    case metric:
	altfactor = 1;
	altunits = "m";
	speedfactor = MPS_TO_KPH;
	speedunits = "kph";
	break;
    default:
	/* leave the default alone */
	break;
    }

    /* Process the options.  Print help if requested. */
    while ((option = getopt(argc, argv, "hVl:smu:D:")) != -1) {
	switch (option) {
#ifdef CLIENTDEBUG_ENABLE
	case 'D':
	    debug = atoi(optarg);
	    gps_enable_debug(debug, stderr);
	    break;
#endif /* CLIENTDEBUG_ENABLE */
	case 'm':
	    magnetic_flag = true;
	    break;
	case 's':
	    silent_flag = true;
	    break;
	case 'u':
	    switch (optarg[0]) {
	    case 'i':
		altfactor = METERS_TO_FEET;
		altunits = "ft";
		speedfactor = MPS_TO_MPH;
		speedunits = "mph";
		continue;
	    case 'n':
		altfactor = METERS_TO_FEET;
		altunits = "ft";
		speedfactor = MPS_TO_KNOTS;
		speedunits = "knots";
		continue;
	    case 'm':
		altfactor = 1;
		altunits = "m";
		speedfactor = MPS_TO_KPH;
		speedunits = "kph";
		continue;
	    default:
		(void)fprintf(stderr, "Unknown -u argument: %s\n", optarg);
	    }
	    break;
	case 'V':
	    (void)fprintf(stderr, "%s: %s (revision %s)\n",
			  argv[0], VERSION, REVISION);
	    exit(EXIT_SUCCESS);
	case 'l':
	    switch (optarg[0]) {
	    case 'd':
		deg_type = deg_dd;
		continue;
	    case 'm':
		deg_type = deg_ddmm;
		continue;
	    case 's':
		deg_type = deg_ddmmss;
		continue;
	    default:
		(void)fprintf(stderr, "Unknown -l argument: %s\n", optarg);
	    }
	    break;
	case 'h':
	default:
	    usage(argv[0]);
	    break;
	}
    }

    /* Grok the server, port, and device. */
    if (optind < argc) {
	gpsd_source_spec(argv[optind], &source);
    } else
	gpsd_source_spec(NULL, &source);

    /* Open the stream to gpsd. */
    if (gps_open(source.server, source.port, &gpsdata) != 0) {
	(void)fprintf(stderr,
		      "cgps: no gpsd running or network error: %d, %s\n",
		      errno, gps_errstr(errno));
	exit(EXIT_FAILURE);
    }

    /* note: we're assuming BSD-style reliable signals here */
    (void)signal(SIGINT, die);
    (void)signal(SIGHUP, die);
    (void)signal(SIGWINCH, resize);

    windowsetup();

    status_timer = time(NULL);

    if (source.device != NULL)
	flags |= WATCH_DEVICE;
    (void)gps_stream(&gpsdata, flags, source.device);

    /* heart of the client */
    for (;;) {
	int c;

	/* wait 1/2 second for gpsd */
	if (!gps_waiting(&gpsdata, 500000)) {
	    /* 240 tries at .5 Sec a try is a 2 minute timeout */
	    if ( 240 < wait_clicks++ )
		die(GPS_TIMEOUT);
	} else {
	    wait_clicks = 0;
	    errno = 0;
	    if (gps_read(&gpsdata) == -1) {
		(void)fprintf(stderr, "cgps: socket error 4\n");
		die(errno == 0 ? GPS_GONE : GPS_ERROR);
	    } else {
		/* Here's where updates go now that things are established. */
#ifdef TRUENORTH
		if (compass_flag)
		    update_compass_panel(&gpsdata);
		else
#endif /* TRUENORTH */
		    update_gps_panel(&gpsdata);
	    }
	}

	/* Check for user input. */
	c = wgetch(datawin);

	switch (c) {
	    /* Quit */
	case 'q':
	    die(CGPS_QUIT);
	    break;

	    /* Toggle spewage of raw gpsd data. */
	case 's':
	    silent_flag = !silent_flag;
	    break;

	    /* Clear the spewage area. */
	case 'c':
	    (void)werase(messages);
	    break;

	default:
	    break;
	}
    }
}
