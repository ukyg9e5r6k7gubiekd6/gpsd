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
 * The sup[port for it is conditioned out in order to simplify moving 
 * to the new JSON-based oprotocol and reduce startup time.
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
#define DATAWIN_GPS_FIELDS 9

/* This is how many display fields are output in the 'datawin' window
   when in COMPASS mode.  Change this value if you add or remove fields
   from the 'datawin' window for the COMPASS mode. */
#define DATAWIN_COMPASS_FIELDS 6

/* This is how far over in the 'datawin' window to indent the field
   descriptions. */
#define DATAWIN_DESC_OFFSET 5

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

/* This is the minimum size we'll accept for the 'datawin' window in
   GPS mode. */
#define MIN_GPS_DATAWIN_SIZE (DATAWIN_GPS_FIELDS + DATAWIN_OVERHEAD)

/* This is the minimum size we'll accept for the 'datawin' window in
   COMPASS mode. */
#define MIN_COMPASS_DATAWIN_SIZE (DATAWIN_COMPASS_FIELDS + DATAWIN_OVERHEAD)

/* This is the maximum number of satellites gpsd can track. */
#define MAX_POSSIBLE_SATS (MAXCHANNELS - 2)

/* This is the maximum size we need for the 'satellites' window. */
#define MAX_SATWIN_SIZE (MAX_POSSIBLE_SATS + SATWIN_OVERHEAD)

#include <sys/types.h>
#include <sys/select.h>
#ifndef S_SPLINT_S
#include <sys/socket.h>
#include <unistd.h>
#endif /* S_SPLINT_S */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <errno.h>
#include <assert.h>
#include <signal.h>
#include <stdbool.h>

#include "gpsd_config.h"
#ifdef HAVE_NCURSES_H
#include <ncurses.h>
#else
#include <curses.h>
#endif /* HAVE_NCURSES_H */

#include "gps.h"
#include "gpsdclient.h"
#include "revision.h"

/*
 * FIXME: use here is a minor bug, should report epx and epy separately.
 * How to mix together epx and epy to get a horizontal circular error.
 */
#define EMIX(x, y)	(((x) > (y)) ? (x) : (y))

static struct gps_data_t *gpsdata;
static time_t status_timer;    /* Time of last state change. */
static int state = 0;   /* or MODE_NO_FIX=1, MODE_2D=2, MODE_3D=3 */
static float altfactor = METERS_TO_FEET;
static float speedfactor = MPS_TO_MPH;
static char *altunits = "ft";
static char *speedunits = "mph";
static struct fixsource_t source;
#ifdef CLIENTDEBUG_ENABLE
static int debug;
#endif /* CLIENTDEBUG_ENABLE */

static WINDOW *datawin, *satellites, *messages;

static bool raw_flag=false;
static bool silent_flag=false;
static bool magnetic_flag=false;
static int window_length;
static int display_sats;
#ifdef TRUENORTH
static bool compass_flag=false;
#endif /* TRUENORTH */

/* pseudo-signals indicating reason for termination */
#define CGPS_QUIT	0	/* voluntary yterminastion */
#define GPS_GONE	-1	/* GPS device went away */
#define GPS_ERROR	-2	/* low-level failure in GPS read */

/* Convert true heading to magnetic.  Taken from the Aviation
   Formulary v1.43.  Valid to within two degrees within the
   continiental USA except for the following airports: MO49 MO86 MO50
   3K6 02K and KOOA.  AK correct to better than one degree.  Western
   Europe correct to within 0.2 deg.

   If you're not in one of these areas, I apologize, I don't have the
   math to compute your varation.  This is obviously extremely
   floating-point heavy, so embedded people, beware of using.

   Note that there are issues with using magnetic heading.  This code
   does not account for the possibility of travelling into or out of
   an area of valid calculation beyond forcing the magnetic conversion
   off.  A better way to communicate this to the user is probably
   desirable (in case the don't notice the subtle change from "(mag)"
   to "(true)" on their display).
 */
static float true2magnetic(double lat, double lon, double heading)
{
    /* Western Europe */
    /*@ -evalorder +relaxtypes @*/
    if((lat > 36.0) && (lat < 68.0) &&
       (lon > -10.0) && (lon < 28.0)) {
	heading = ( 10.4768771667158 - (0.507385322418858 * lon) + (0.00753170031703826 * pow(lon, 2))
		    - (1.40596203924748e-05 * pow(lon, 3)) - (0.535560699962353 * lat)
		    + (0.0154348808069955 * lat * lon) - (8.07756425110592e-05 * lat * pow(lon, 2))
		    + (0.00976887198864442 * pow(lat, 2)) - (0.000259163929798334 * lon * pow(lat, 2))
		    - (3.69056939266123e-05 * pow(lat, 3)) + heading);
    }
    /* USA */
    else if((lat > 24.0) && (lat < 50.0) &&
	    (lon > 66.0) && (lon < 125.0)) {
	lon=0.0-lon;
	heading = ( (-65.6811) + (0.99 * lat) + (0.0128899 * pow(lat, 2)) - (0.0000905928 * pow(lat, 3)) + (2.87622 * lon)
		    - (0.0116268 * lat * lon) - (0.00000603925 * lon * pow(lat, 2)) - (0.0389806 * pow(lon, 2))
		    - (0.0000403488 * lat * pow(lon, 2)) + (0.000168556 * pow(lon, 3)) + heading);
    }
    /* AK */
    else if((lat > 54.0) &&
	    (lon > 130.0) && (lon < 172.0)) {
	lon=0.0-lon;
	heading = ( 618.854 + (2.76049 * lat) - (0.556206 * pow(lat, 2)) + (0.00251582 * pow(lat, 3)) - (12.7974 * lon)
		    + (0.408161 * lat * lon) + (0.000434097 * lon * pow(lat, 2)) - (0.00602173 * pow(lon, 2))
		    - (0.00144712 * lat * pow(lon, 2)) + (0.000222521 * pow(lon, 3)) + heading);
    } else {
	/* We don't know how to compute magnetic heading for this
	   location. */
	magnetic_flag=false;
    }

    /* No negative headings. */
    if (heading < 0.0)
	heading += 360.0;

    return(heading);
    /*@ +evalorder -relaxtypes @*/
}

/* Function to call when we're all done.  Does a bit of clean-up. */
static void die(int sig)
{
    /* Ignore signals. */
    (void)signal(SIGINT,SIG_IGN);
    (void)signal(SIGHUP,SIG_IGN);

    /* Move the cursor to the bottom left corner. */
    (void)mvcur(0,COLS-1,LINES-1,0);

    /* Put input attributes back the way they were. */
    (void)echo();

    /* Done with curses. */
    (void)endwin();

    /* We're done talking to gpsd. */
    (void)gps_close(gpsdata);

    switch (sig) {
    case CGPS_QUIT:
	break;
    case GPS_GONE:
	(void)fprintf(stderr, "cgps: GPS hung up.\n");
	break;
    case GPS_ERROR:
	(void)fprintf(stderr, "cgps: GPS read returned error\n");
	break;
    default:
	(void)fprintf(stderr, "cgps: caught signal %d\n", sig);
    }

    /* Bye! */
    exit(0);
}


static enum deg_str_type deg_type = deg_dd;

/*@ -globstate @*/
static void windowsetup(void) 
{
    /* Set the window sizes per the following criteria:

       1.  Set the window size to display the maximum number of
       satellites possible, but not more than the size required to
       display the maximum number of satellites gpsd is capable of
       tracking (MAXCHANNELS - 2).

       2.  If the screen size will not allow for the full complement of
       satellites to be displayed, set the windows sizes smaller, but
       not smaller than the number of lines necessary to display all of
       the fields in the 'datawin'.  The list of displayed satellites
       will be truncated to fit the available window size.  (TODO: If
       the satellite list is truncated, omit the satellites not used to
       obtain the current fix.)

       3.  If the screen is large enough to display all possible
       satellites (MAXCHANNELS - 2) with space still left at the bottom,
       add a window at the bottom in which to scroll raw gpsd data.
    */
    int xsize, ysize;

    getmaxyx(stdscr,ysize,xsize);

#ifdef TRUENORTH
    if(compass_flag) {
	if(ysize == MIN_COMPASS_DATAWIN_SIZE) {
	    raw_flag = false;
	    window_length = MIN_COMPASS_DATAWIN_SIZE;
	} else if(ysize > MIN_COMPASS_DATAWIN_SIZE) {
	    raw_flag = true;
	    window_length = MIN_COMPASS_DATAWIN_SIZE;
	} else {
	    (void)mvprintw(0, 0, "Your screen must be at least 80x%d to run cgps.",MIN_COMPASS_DATAWIN_SIZE);
	    /*@ -nullpass @*/
	    (void)refresh();
	    /*@ +nullpass @*/
	    (void)sleep(5);
	    die(0);
	}
    } else
#endif /* TRUENORTH */
    {
	if(ysize == MAX_SATWIN_SIZE) {
	    raw_flag = false;
	    window_length = MAX_SATWIN_SIZE;
	    display_sats = MAX_POSSIBLE_SATS;
	} else if(ysize == MAX_SATWIN_SIZE + 1) {
	    raw_flag = true;
	    window_length = MAX_SATWIN_SIZE;
	    display_sats = MAX_POSSIBLE_SATS;
	} else if(ysize > MAX_SATWIN_SIZE + 2) {
	    raw_flag = true;
	    window_length = MAX_SATWIN_SIZE;
	    display_sats = MAX_POSSIBLE_SATS;
	} else if(ysize > MIN_GPS_DATAWIN_SIZE) {
	    raw_flag = false;
	    window_length = ysize - (int)raw_flag;
	    display_sats = window_length - SATWIN_OVERHEAD - (int)raw_flag;
	} else if(ysize == MIN_GPS_DATAWIN_SIZE) {
	    raw_flag = false;
	    window_length = MIN_GPS_DATAWIN_SIZE;
	    display_sats = window_length - SATWIN_OVERHEAD - 1;
	} else {
	    (void)mvprintw(0, 0, "Your screen must be at least 80x%d to run cgps.",MIN_GPS_DATAWIN_SIZE);
	    /*@ -nullpass @*/
	    (void)refresh();
	    /*@ +nullpass @*/
	    (void)sleep(5);
	    die(0);
	}
    }

#ifdef TRUENORTH
    /* Set up the screen for either a compass or a gps receiver. */
    if(compass_flag) {
	/* We're a compass, set up accordingly. */

	/*@ -onlytrans @*/
	datawin    = newwin(window_length, DATAWIN_WIDTH, 0, 0);
	(void)nodelay(datawin,(bool)TRUE);
	if(raw_flag) {
	    messages   = newwin(0, 0, window_length, 0);

	    /*@ +onlytrans @*/
	    (void)scrollok(messages, true);
	    (void)wsetscrreg(messages, 0, ysize - (window_length));
	}

	/*@ -nullpass @*/
	(void)refresh();
	/*@ +nullpass @*/

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

	/*@ -onlytrans @*/
	datawin    = newwin(window_length, DATAWIN_WIDTH, 0, 0);
	satellites = newwin(window_length, SATELLITES_WIDTH, 0, DATAWIN_WIDTH);
	(void)nodelay(datawin,(bool)TRUE);
	if(raw_flag) {
	    messages   = newwin(ysize - (window_length), xsize, window_length, 0);

	    /*@ +onlytrans @*/
	    (void)scrollok(messages, true);
	    (void)wsetscrreg(messages, 0, ysize - (window_length));
	}

	/*@ -nullpass @*/
	(void)refresh();
	/*@ +nullpass @*/

	/* Do the initial field label setup. */
	(void)mvwprintw(datawin, 1, DATAWIN_DESC_OFFSET, "Time:");
	(void)mvwprintw(datawin, 2, DATAWIN_DESC_OFFSET, "Latitude:");
	(void)mvwprintw(datawin, 3, DATAWIN_DESC_OFFSET, "Longitude:");
	(void)mvwprintw(datawin, 4, DATAWIN_DESC_OFFSET, "Altitude:");
	(void)mvwprintw(datawin, 5, DATAWIN_DESC_OFFSET, "Speed:");
	(void)mvwprintw(datawin, 6, DATAWIN_DESC_OFFSET, "Heading:");
	(void)mvwprintw(datawin, 7, DATAWIN_DESC_OFFSET, "Climb:");
	(void)mvwprintw(datawin, 8, DATAWIN_DESC_OFFSET, "Status:");
	(void)mvwprintw(datawin, 9, DATAWIN_DESC_OFFSET, "GPS Type:");

	/* Note that the following four fields are exceptions to the
	   sizing rule.  The minimum window size does not include these
	   fields, if the window is too small, they get excluded.  This
	   may or may not change if/when the output for these fields is
	   fixed and/or people request their permanance.  They're only
	   there in the first place because I arbitrarily thought they
	   sounded interesting. ;^) */

	if(window_length >= (MIN_GPS_DATAWIN_SIZE + 4)) {
	    (void)mvwprintw(datawin, 10, DATAWIN_DESC_OFFSET, "Horizontal Err:");
	    (void)mvwprintw(datawin, 11, DATAWIN_DESC_OFFSET, "Vertical Err:");
	    (void)mvwprintw(datawin, 12, DATAWIN_DESC_OFFSET, "Course Err:");
	    (void)mvwprintw(datawin, 13, DATAWIN_DESC_OFFSET, "Speed Err:");
	}

	(void)wborder(datawin, 0, 0, 0, 0, 0, 0, 0, 0);
	(void)mvwprintw(satellites, 1,1, "PRN:   Elev:  Azim:  SNR:  Used:");
	(void)wborder(satellites, 0, 0, 0, 0, 0, 0, 0, 0);
    }
}
/*@ +globstate @*/

#ifdef TRUENORTH
/* This gets called once for each new compass sentence. */
static void update_compass_panel(struct gps_data_t *gpsdata,
			char *message,
			size_t len UNUSED)
{
    char scr[128];
    /* Print time/date. */
    if (isnan(gpsdata->fix.time)==0) {
	(void)unix_to_iso8601(gpsdata->fix.time, scr, sizeof(scr));
    } else
	(void)snprintf(scr, sizeof(scr), "n/a");
    (void)mvwprintw(datawin, 1, DATAWIN_VALUE_OFFSET, "%-*s", 27, scr);

    /* Fill in the heading. */
    if (isnan(gpsdata->fix.track)==0) {
	(void)snprintf(scr, sizeof(scr), "%.1f degrees", gpsdata->fix.track);
    } else
	(void)snprintf(scr, sizeof(scr), "n/a");
    (void)mvwprintw(datawin, 2, DATAWIN_VALUE_OFFSET, "%-*s", 27, scr);

    /* Fill in the pitch. */
    if (isnan(gpsdata->fix.climb)==0) {
	(void)snprintf(scr, sizeof(scr), "%.1f", gpsdata->fix.climb);
    } else
	(void)snprintf(scr, sizeof(scr), "n/a");
    (void)mvwprintw(datawin, 3, DATAWIN_VALUE_OFFSET, "%-*s", 27, scr);

    /* Fill in the roll. */
    if (isnan(gpsdata->fix.speed)==0)
	(void)snprintf(scr, sizeof(scr), "%.1f",gpsdata->fix.speed);
    else
	(void)snprintf(scr, sizeof(scr), "n/a");
    (void)mvwprintw(datawin, 4, DATAWIN_VALUE_OFFSET, "%-*s", 27, scr);

    /* Fill in the speed. */
    if (isnan(gpsdata->fix.altitude)==0)
	(void)snprintf(scr, sizeof(scr), "%.1f", gpsdata->fix.altitude);
    else
	(void)snprintf(scr, sizeof(scr), "n/a");
    (void)mvwprintw(datawin, 5, DATAWIN_VALUE_OFFSET, "%-*s", 27, scr);

    /* When we need to fill in receiver type again, do it here. */
    (void)mvwprintw(datawin, 6, DATAWIN_VALUE_OFFSET, "%-*s", 27, scr);

    /* Be quiet if the user requests silence. */
    if(!silent_flag && raw_flag) {
	(void)waddstr(messages, message);
    }

    (void)wrefresh(datawin);
    if(raw_flag) {
	(void)wrefresh(messages);
    }
}
#endif /* TRUENORTH */

/* This gets called once for each new GPS sentence. */
static void update_gps_panel(struct gps_data_t *gpsdata,
			char *message,
			size_t len UNUSED)
{
    int i,j, n;
    int newstate;
    char scr[128];
    bool usedflags[MAXCHANNELS];

    /* this is where we implement source-device filtering */
    if (gpsdata->dev.path[0]!='\0' && source.device!=NULL && strcmp(source.device, gpsdata->dev.path) != 0)
	return;

    /* must build bit vector of which statellites are used from list */
    for (i = 0; i < MAXCHANNELS; i++) {
	usedflags [i] = false;
	for (j = 0; j < gpsdata->satellites_used; j++)
	    if (gpsdata->used[j] == gpsdata->PRN[i])
		usedflags[i] = true;
    }

    /* This is for the satellite status display.  Originally lifted from
       xgps.c.  Note that the satellite list may be truncated based on
       available screen size, or may only show satellites used for the
       fix.  */
    if (gpsdata->satellites_visible!=0) {
	if (display_sats >= MAX_POSSIBLE_SATS) {
	    for (i = 0; i < MAX_POSSIBLE_SATS; i++) {
		if (i < gpsdata->satellites_visible) {
		    (void)snprintf(scr, sizeof(scr),
				   " %3d    %02d    %03d    %02d      %c",
				   gpsdata->PRN[i],
				   gpsdata->elevation[i], gpsdata->azimuth[i],
				   (int)gpsdata->ss[i], usedflags[i] ? 'Y' : 'N');
		} else {
		    (void)strlcpy(scr, "", sizeof(scr));
		}
		(void)mvwprintw(satellites, i+2, 1, "%-*s", SATELLITES_WIDTH - 3, scr);
	    }
	} else {
	    n=0;
	    for (i = 0; i < MAX_POSSIBLE_SATS; i++) {
		if (n < display_sats) {
		    if ((i < gpsdata->satellites_visible) && ((gpsdata->used[i]!=0) || (gpsdata->satellites_visible <= display_sats))) {
			(void)snprintf(scr, sizeof(scr),
				       " %3d    %02d    %03d    %02d      %c",
				       gpsdata->PRN[i],
				       gpsdata->elevation[i], gpsdata->azimuth[i],
				       (int)gpsdata->ss[i],gpsdata->used[i] ? 'Y' : 'N');
			(void)mvwprintw(satellites, n+2, 1, "%-*s", SATELLITES_WIDTH - 3, scr);
			n++;
		    }
		}
	    }

	    if(n < display_sats) {
		for (i = n; i <= display_sats; i++) {
		    (void)mvwprintw(satellites, i+2, 1, "%-*s", SATELLITES_WIDTH - 3, "");
		}
	    }

	}
    }

    /* Print time/date. */
    if (isnan(gpsdata->fix.time)==0) {
	(void)unix_to_iso8601(gpsdata->fix.time, scr, sizeof(scr));
    } else
	(void)snprintf(scr, sizeof(scr), "n/a");
    (void)mvwprintw(datawin, 1, DATAWIN_VALUE_OFFSET, "%-*s", 27, scr);


    /* Fill in the latitude. */
    if (gpsdata->fix.mode >= MODE_2D && isnan(gpsdata->fix.latitude)==0) {
	(void)snprintf(scr, sizeof(scr), "%s %c",
		       deg_to_str(deg_type,  fabs(gpsdata->fix.latitude)),
		       (gpsdata->fix.latitude < 0) ? 'S' : 'N');
    } else
	(void)snprintf(scr, sizeof(scr), "n/a");
    (void)mvwprintw(datawin, 2, DATAWIN_VALUE_OFFSET, "%-*s", 27, scr);

    /* Fill in the longitude. */
    if (gpsdata->fix.mode >= MODE_2D && isnan(gpsdata->fix.longitude)==0) {
	(void)snprintf(scr, sizeof(scr), "%s %c",
		       deg_to_str(deg_type,  fabs(gpsdata->fix.longitude)),
		       (gpsdata->fix.longitude < 0) ? 'W' : 'E');
    } else
	(void)snprintf(scr, sizeof(scr), "n/a");
    (void)mvwprintw(datawin, 3, DATAWIN_VALUE_OFFSET, "%-*s", 27, scr);

    /* Fill in the altitude. */
    if (gpsdata->fix.mode == MODE_3D && isnan(gpsdata->fix.altitude)==0)
	(void)snprintf(scr, sizeof(scr), "%.1f %s",gpsdata->fix.altitude*altfactor, altunits);
    else
	(void)snprintf(scr, sizeof(scr), "n/a");
    (void)mvwprintw(datawin, 4, DATAWIN_VALUE_OFFSET, "%-*s", 27, scr);

    /* Fill in the speed. */
    if (gpsdata->fix.mode >= MODE_2D && isnan(gpsdata->fix.track)==0)
	(void)snprintf(scr, sizeof(scr), "%.1f %s", gpsdata->fix.speed*speedfactor, speedunits);
    else
	(void)snprintf(scr, sizeof(scr), "n/a");
    (void)mvwprintw(datawin, 5, DATAWIN_VALUE_OFFSET, "%-*s", 27, scr);

    /* Fill in the heading. */
    if (gpsdata->fix.mode >= MODE_2D && isnan(gpsdata->fix.track)==0)
	if(!magnetic_flag) {
	    (void)snprintf(scr, sizeof(scr), "%.1f deg (true)", gpsdata->fix.track);
	} else {
	    (void)snprintf(scr, sizeof(scr), "%.1f deg (mag) ", true2magnetic(gpsdata->fix.latitude, gpsdata->fix.longitude, gpsdata->fix.track));
	}
    else
	(void)snprintf(scr, sizeof(scr), "n/a");
    (void)mvwprintw(datawin, 6, DATAWIN_VALUE_OFFSET, "%-*s", 27, scr);

    /* Fill in the rate of climb. */
    if (gpsdata->fix.mode == MODE_3D && isnan(gpsdata->fix.climb)==0)
	(void)snprintf(scr, sizeof(scr), "%.1f %s/min",
		       gpsdata->fix.climb * altfactor * 60, altunits);
    else
	(void)snprintf(scr, sizeof(scr), "n/a");
    (void)mvwprintw(datawin, 7, DATAWIN_VALUE_OFFSET, "%-*s", 27, scr);

    /* Fill in the GPS status and the time since the last state
       change. */
    if (gpsdata->online == 0) {
	newstate = 0;
	(void)snprintf(scr, sizeof(scr), "OFFLINE");
    } else {
	newstate = gpsdata->fix.mode;
	switch (gpsdata->fix.mode) {
	case MODE_2D:
	    (void)snprintf(scr, sizeof(scr), "2D %sFIX (%d secs)",(gpsdata->status==STATUS_DGPS_FIX)?"DIFF ":"", (int) (time(NULL) - status_timer));
	    break;
	case MODE_3D:
	    (void)snprintf(scr, sizeof(scr), "3D %sFIX (%d secs)",(gpsdata->status==STATUS_DGPS_FIX)?"DIFF ":"", (int) (time(NULL) - status_timer));
	    break;
	default:
	    (void)snprintf(scr, sizeof(scr), "NO FIX (%d secs)", (int) (time(NULL) - status_timer));
	    break;
	}
    }
    (void)mvwprintw(datawin, 8, DATAWIN_VALUE_OFFSET, "%-*s", 27, scr);

    /* Fill in receiver type. */
    if (gpsdata->set & (DEVICE_SET | DEVICELIST_SET)) {
#ifdef CLIENTDEBUG_ENABLE
	if (debug > 0)
	    (void)fprintf(stderr, "Device ID or list set.\n");
#endif
	if (gpsdata->set & DEVICE_SET) {
	    (void)snprintf(scr, sizeof(scr), "%s", gpsdata->dev.driver);
	} else if (gpsdata->devices.ndevices == 1) {
	    (void)snprintf(scr, sizeof(scr), "%s", gpsdata->devices.list[0].driver);
	} else {
	    (void)snprintf(scr, sizeof(scr), "%d devices", 
			   gpsdata->devices.ndevices);
	}      
	(void)mvwprintw(datawin, 9, DATAWIN_VALUE_OFFSET, "%-*s", 27, scr);
    }
    /* Note that the following four fields are exceptions to the
       sizing rule.  The minimum window size does not include these
       fields, if the window is too small, they get excluded.  This
       may or may not change if/when the output for these fields is
       fixed and/or people request their permanance.  They're only
       there in the first place because I arbitrarily thought they
       sounded interesting. ;^) */

    if(window_length >= (MIN_GPS_DATAWIN_SIZE + 4)) {

	// FIXME: Report both epx and epy!
	/* Fill in the estimated horizontal position error. */
	if (isnan(gpsdata->fix.epx)==0 && isnan(gpsdata->fix.epx)==0)
	    (void)snprintf(scr, sizeof(scr), "+/- %d %s", (int) (EMIX(gpsdata->fix.epx,gpsdata->fix.epy) * altfactor), altunits);
	else
	    (void)snprintf(scr, sizeof(scr), "n/a");
	(void)mvwprintw(datawin, 10, DATAWIN_VALUE_OFFSET + 5, "%-*s", 22, scr);

	/* Fill in the estimated vertical position error. */
	if (isnan(gpsdata->fix.epv)==0)
	    (void)snprintf(scr, sizeof(scr), "+/- %d %s", (int)(gpsdata->fix.epv * altfactor), altunits);
	else
	    (void)snprintf(scr, sizeof(scr), "n/a");
	(void)mvwprintw(datawin, 11, DATAWIN_VALUE_OFFSET + 5, "%-*s", 22, scr);

	/* Fill in the estimated track error. */
	if (isnan(gpsdata->fix.epd)==0)
	    (void)snprintf(scr, sizeof(scr), "+/- %d deg", (int)(gpsdata->fix.epd));
	else
	    (void)snprintf(scr, sizeof(scr), "n/a");
	(void)mvwprintw(datawin, 12, DATAWIN_VALUE_OFFSET + 5, "%-*s", 22, scr);

	/* Fill in the estimated speed error. */
	if (isnan(gpsdata->fix.eps)==0)
	    (void)snprintf(scr, sizeof(scr), "+/- %d %s", (int)(gpsdata->fix.eps * speedfactor), speedunits);
	else
	    (void)snprintf(scr, sizeof(scr), "n/a");
	(void)mvwprintw(datawin, 13, DATAWIN_VALUE_OFFSET + 5, "%-*s", 22, scr);
    }

    /* Be quiet if the user requests silence. */
    if(!silent_flag && raw_flag) {
	(void)waddstr(messages, message);
    }

    /* Reset the status_timer if the state has changed. */
    if (newstate != state) {
	status_timer = time(NULL);
	state = newstate;
    }

    (void)wrefresh(datawin);
    (void)wrefresh(satellites);
    if(raw_flag) {
	(void)wrefresh(messages);
    }
}

static void usage( char *prog)
{
    (void)fprintf(stderr,
		  "Usage: %s [-h] [-V] [-l {d|m|s}] [server[:port:[device]]]\n\n"
		  "  -h	  Show this help, then exit\n"
		  "  -V	  Show version, then exit\n"
		  "  -s	  Be silent (don't print raw gpsd data)\n"
		  "  -l {d|m|s}  Select lat/lon format\n"
		  "		d = DD.dddddd\n"
		  "		m = DD MM.mmmm'\n"
		  "		s = DD MM' SS.sss\"\n"
		  " -m      Display heading as the estimated magnetic heading\n"
		  "         Valid only for USA (Lower 48 + AK) and Western Europe.\n"
		  , prog);

    exit(1);
}

/*
 * No protocol dependencies above this line
 */

int main(int argc, char *argv[])
{
    int option;
    int c;

    struct timeval timeout;
    fd_set rfds;
    int data;

    /*@ -observertrans @*/
    switch (gpsd_units())
    {
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
    /*@ +observertrans @*/

    /* Process the options.  Print help if requested. */
    while ((option = getopt(argc, argv, "hVl:smuD:")) != -1) {
	switch (option) {
#ifdef CLIENTDEBUG_ENABLE
	case 'D':
	    debug = atoi(optarg);
	    gps_enable_debug(debug, stderr);
	    break;
#endif /* CLIENTDEBUG_ENABLE */
	case 'm':
	    magnetic_flag=true;
	    break;
	case 's':
	    silent_flag=true;
	    break;
	case 'u':
	    /*@ -observertrans @*/
	    switch ( optarg[0] ) {
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
	    /*@ +observertrans @*/
	case 'V':
	    (void)fprintf(stderr, "xgps: %s (revision %s)\n", 
			  VERSION, REVISION);
	    exit(0);
	case 'l':
	    switch ( optarg[0] ) {
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
		/*@ -casebreak @*/
	    }
	    break;
	case 'h': default:
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
    /*@i@*/gpsdata = gps_open(source.server, source.port);
    if (!gpsdata) {
	(void)fprintf( stderr,
		       "cgps: no gpsd running or network error: %d, %s\n",
		       errno, gps_errstr(errno));
	exit(2);
    }

    /* Fire up curses. */
    (void)initscr();
    (void)noecho();
    (void)signal(SIGINT,die);
    (void)signal(SIGHUP,die);

    windowsetup();

    /* Here's where updates go now that things are established. */
#ifdef TRUENORTH
    if(compass_flag) {
	gps_set_raw_hook(gpsdata, update_compass_panel);
    } else
#endif /* TRUENORTH */
    {
	gps_set_raw_hook(gpsdata, update_gps_panel);
    }

    status_timer = time(NULL);

    (void)gps_stream(gpsdata, WATCH_ENABLE|WATCH_NEWSTYLE, NULL);

    /* heart of the client */
    for (;;) {

	/* watch to see when it has input */
	FD_ZERO(&rfds);
	FD_SET(gpsdata->gps_fd, &rfds);

	/* wait up to five seconds. */
	timeout.tv_sec = 5;
	timeout.tv_usec = 0;

	/* check if we have new information */
	data = select(gpsdata->gps_fd + 1, &rfds, NULL, NULL, &timeout);

	if (data == -1) {
	    fprintf( stderr, "cgps: socket error 3\n");
	    exit(2);
	} else if( data ) {
	    errno = 0;
	    if (gps_poll(gpsdata) != 0) {
		fprintf( stderr, "cgps: socket error 4\n");
		die(errno == 0 ? GPS_GONE : GPS_ERROR);
	    }
	}

	/* Check for user input. */
	c=wgetch(datawin);

	switch ( c ) {
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
