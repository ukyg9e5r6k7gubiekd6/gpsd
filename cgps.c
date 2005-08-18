/*
 * Copyright (c) 2005 Jeff Francis <jeff@gritch.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
  Jeff Francis
  jeff@gritch.org
  $Id$

  Kind of a curses version of xgps for use with gpsd.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <math.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/socket.h>

#include <ncurses.h>                                                         
#include <signal.h>

#include "gps.h"

/* Macro for declaring function arguments unused. */
#if defined(__GNUC__)
#  define UNUSED __attribute__((unused)) /* Flag variable as unused */
#else /* not __GNUC__ */
#  define UNUSED
#endif

static struct gps_data_t *gpsdata;
static time_t timer;	/* time of last state change */
static int state = 0;	/* or MODE_NO_FIX=1, MODE_2D=2, MODE_3D=3 */
static float altfactor = METERS_TO_FEET;
static float speedfactor = MPS_TO_MPH;
static char *altunits = "ft";
static char *speedunits = "mph";

/* Function to call when we're all done.  Does a bit of clean-up. */
static void die(int sig UNUSED) 
{
    /* Ignore signals. */
    (void)signal(SIGINT,SIG_IGN);

    /* Move the cursor to the bottom left corner. */
    (void)mvcur(0,COLS-1,LINES-1,0);

    /* Put input attributes back the way they were. */
    (void)echo();

    /* Done with curses. */
    (void)endwin();

    /* We're done talking to gpsd. */
    (void)gps_close(gpsdata);

    /* Bye! */
    exit(0);
}

/* This gets called once for each new sentence. */
static void update_panel(struct gps_data_t *gpsdata, 
			 char *message,
			 size_t len UNUSED, 
			 int level UNUSED)
{
    int i;
    int newstate;
    char s[128];

    /* Do the initial field label setup. */
    (void)move(0,5);
    (void)printw("Time:");
    (void)move(1,5);
    (void)printw("Latitude:");
    (void)move(2,5);
    (void)printw("Longitude:");
    (void)move(3,5);
    (void)printw("Altitude:");
    (void)move(4,5);
    (void)printw("Speed:");
    (void)move(5,5);
    (void)printw("Heading:");
    (void)move(6,5);
    (void)printw("HPE:");
    (void)move(7,5);
    (void)printw("VPE:");
    (void)move(8,5);
    (void)printw("Climb:");
    (void)move(9,5);
    (void)printw("Status:");
    (void)move(10,5);
    (void)printw("Change:");
    (void)move(0,45);
    (void)printw("PRN:   Elev:  Azim:  SNR:  Used:");


    /* This is for the satellite status display.  Lifted almost verbatim
       from xgps.c. */
    if (gpsdata->satellites) {
	for (i = 0; i < MAXCHANNELS; i++) {
	    if (i < gpsdata->satellites) {
		(void)move(i+1,45);
		(void)printw(" %3d    %02d    %03d    %02d      %c    ",
		       gpsdata->PRN[i],
		       gpsdata->elevation[i], gpsdata->azimuth[i], 
		       gpsdata->ss[i],	gpsdata->used[i] ? 'Y' : 'N');
	    } else {
		(void)move(i+1,45);
		(void)printw("                                          ");
	    }
	}
    }
  
/* TODO: Make this work. */
    if (isnan(gpsdata->fix.time)==0) {
	(void)move(0,17);
	(void)printw("%s",unix_to_iso8601(gpsdata->fix.time, s, (int)sizeof(s)));
    } else {
	(void)move(0,17);
	(void)printw("n/a         ");
    }

    /* Fill in the latitude. */
    if (gpsdata->fix.mode >= MODE_2D) {
	(void)move(1,17);
	(void)printw("%lf %c     ", fabs(gpsdata->fix.latitude), (gpsdata->fix.latitude < 0) ? 'S' : 'N');
    } else {
	(void)move(1,17);
	(void)printw("n/a         ");
    }

    /* Fill in the longitude. */
    if (gpsdata->fix.mode >= MODE_2D) {
	(void)move(2,17);
	(void)printw("%lf %c     ", fabs(gpsdata->fix.longitude), (gpsdata->fix.longitude < 0) ? 'W' : 'E');
    } else {
	(void)move(2,17);
	(void)printw("n/a         ");
    }

    /* Fill in the altitude. */
    if (gpsdata->fix.mode == MODE_3D) {
	(void)move(3,17);
	(void)printw("%.1f %s     ",gpsdata->fix.altitude*altfactor, altunits);
    } else {
	(void)move(3,17);
	(void)printw("n/a         ");
    }

    /* Fill in the speed */
    if (gpsdata->fix.mode >= MODE_2D && isnan(gpsdata->fix.track)==0) {
	(void)move(4,17);
	(void)printw("%.1f %s     ", gpsdata->fix.speed*speedfactor, speedunits);
    } else {
	(void)move(4,17);
	(void)printw("n/a         ");
    }

    /* Fill in the heading. */
    if (gpsdata->fix.mode >= MODE_2D && isnan(gpsdata->fix.track)==0) {
	(void)move(5,17);
	(void)printw("%.1f degrees     ", gpsdata->fix.track);
    } else {
	(void)move(5,17);
	(void)printw("n/a         ");
    }

    /* Fill in the estimated horizontal position error. */
    if (isnan(gpsdata->fix.eph)==0) {
	(void)move(6,17);
	(void)printw("%d ft     ", (int) (gpsdata->fix.eph * altfactor));
    } else {
	(void)move(6,17);
	(void)printw("n/a         ");
    }

    /* Fill in the estimated vertical position error. */
    if (isnan(gpsdata->fix.epv)==0) {
	(void)move(7,17);
	(void)printw("%d ft     ", (int)(gpsdata->fix.epv * altfactor));
    } else {
	(void)move(7,17);
	(void)printw("n/a         ");
    }

    /* Fill in the rate of climb. */
    /* TODO: Factor are probably wrong. */
    if (gpsdata->fix.mode == MODE_3D && isnan(gpsdata->fix.climb)==0) {
	(void)move(8,17);
	(void)printw("%.1f ft/min     ", gpsdata->fix.climb * METERS_TO_FEET * 60);
    } else {
	(void)move(8,17);
	(void)printw("n/a         ");
    }
  
    /* Fill in the GPS status */
    if (gpsdata->online == 0) {
	newstate = 0;
	(void)move(9,17);
	(void)printw("OFFLINE          ");
    } else {
	newstate = gpsdata->fix.mode;
	switch (gpsdata->fix.mode) {
	case MODE_2D:
	    (void)move(9,17);
	    (void)printw("2D %sFIX     ",(gpsdata->status==STATUS_DGPS_FIX)?"DIFF ":"");
	    break;
	case MODE_3D:
	    (void)move(9,17);
	    (void)printw("3D %sFIX     ",(gpsdata->status==STATUS_DGPS_FIX)?"DIFF ":"");
	    break;
	default:
	    (void)move(9,17);
	    (void)printw("NO FIX               ");
	    break;
	}
    }

    /* Fill in the time since the last state change. */
    if (newstate != state) {
	timer = time(NULL);
	state = newstate;
    }
    (void)move(10,17);
    (void)printw("(%d secs)          ", (int) (time(NULL) - timer));

    /* Update the screen. */
    (void)refresh();
}

int main(int argc, char *argv[])
{
    int option;
    char *arg = NULL, *colon1, *colon2, *device = NULL, *server = NULL, *port = DEFAULT_GPSD_PORT;
    char *err_str = NULL;
    char c;

    struct timeval timeout;
    fd_set rfds;
    int data;

		

    /* Process the options.  Print help if requested. */
    while ((option = getopt(argc, argv, "hv")) != -1) {
	switch (option) {
	case 'v':
	    (void)fprintf(stderr, "SVN ID: $Id$ \n");
	    exit(0);
	case 'h': default:
	    (void)fprintf(stderr, "Usage: %s [-h] [-v] [server[:port:[device]]]\n", argv[0]);
	    exit(1);
	}
    }

    /* Grok the server, port, and device. */
    /*@ -branchstate @*/
    if (optind < argc) {
	arg = strdup(argv[optind]);
	/*@i@*/colon1 = strchr(arg, ':');
	server = arg;
	if (colon1 != NULL) {
	    if (colon1 == arg)
		server = NULL;
	    else
		*colon1 = '\0';
	    port = colon1 + 1;
	    colon2 = strchr(port, ':');
	    if (colon2 != NULL) {
		if (colon2 == port)
		    port = NULL;
		else
		    *colon2 = '\0';
		device = colon2 + 1;
	    }
	}
	colon1 = colon2 = NULL;
    }
    /*@ +branchstate @*/

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

    /* Open the stream to gpsd. */
    /*@i@*/gpsdata = gps_open(server, port);
    if (!gpsdata) {
	switch ( errno ) {
	case NL_NOSERVICE: 	err_str = "can't get service entry"; break;
	case NL_NOHOST: 	err_str = "can't get host entry"; break;
	case NL_NOPROTO: 	err_str = "can't get protocol entry"; break;
	case NL_NOSOCK: 	err_str = "can't create socket"; break;
	case NL_NOSOCKOPT: 	err_str = "error SETSOCKOPT SO_REUSEADDR"; break;
	case NL_NOCONNECT: 	err_str = "can't connect to host"; break;
	default:             	err_str = "Unknown"; break;
	}
	(void)fprintf( stderr, 
		       "cgps: no gpsd running or network error: %d, %s\n", 
		       errno, err_str);
	exit(2);
    }

    /* Update the timestamp (used to keep track of time since last state
       change). */
    timer = time(NULL);

    /* Set up the curses screen (if using curses). */
    (void)initscr();
    (void)noecho();
    (void)nodelay(stdscr,TRUE);
    (void)signal(SIGINT,die);

    /* Here's where updates go. */
    gps_set_raw_hook(gpsdata, update_panel);

    /* If the user requested a specific device, try to change to it. */
    if (device) {
	char *channelcmd = (char *)malloc(strlen(device)+3);

	if (channelcmd) {
	    /*@i@*/(void)strcpy(channelcmd, "F=");
	    (void)strcpy(channelcmd+2, device);
	    (void)gps_query(gpsdata, channelcmd);
	    (void)free(channelcmd);
	}
    }

    /* Request "w+x" data from gpsd. */
    (void)gps_query(gpsdata, "w+x\n");

    /* accept connections */
    (void)listen(gpsdata->gps_fd, 5);

    for (;;) { /* heart of the client */
    
        /* watch to see when it has input */
        FD_ZERO(&rfds);
        FD_SET(gpsdata->gps_fd, &rfds);

	/* wait up to five seconds. */
	timeout.tv_sec = 5;
	timeout.tv_usec = 0;
																
	/* check if we have new information */
	data = select(gpsdata->gps_fd + 1, &rfds, NULL, NULL, &timeout);
	
	if (data == -1) {
	    fprintf( stderr, "cgps: Socket error\n");
	    exit(2);
	}
	else if( data ) {
	    /* code that calls gps_poll(gpsdata) */
	    (void)gps_poll(gpsdata);
	}
	else {
	    fprintf(stderr, "cgps: No data\n");
	}
    }
 
    /* Check for user input. */
    c=getch();

    /* Quit if 'q'. */
    if(c=='q') {
      die(NULL);
    }
 
}

