/* $Id$ */
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

  Kind of a curses version of xgps for use with gpsd.
*/

#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <math.h>
#include <errno.h>
#include <assert.h>

#include <ncurses.h>                                                         
#include <signal.h>

#include "gpsd_config.h"
#include "gps.h"

static struct gps_data_t *gpsdata;
static time_t status_timer;    /* Time of last state change. */
static time_t misc_timer;    /* Misc use timer. */
static int state = 0;   /* or MODE_NO_FIX=1, MODE_2D=2, MODE_3D=3 */
static float altfactor = METERS_TO_FEET;
static float speedfactor = MPS_TO_MPH;
static char *altunits = "ft";
static char *speedunits = "mph";

static WINDOW *datawin, *satellites, *messages, *command, *status;

static int silent_flag=0;
static int fixclear_flag=0;
static int bigger;

static int compass_flag=0;
static int got_gps_type=0;
static char gps_type[20];

/* Function to call when we're all done.  Does a bit of clean-up. */
static void die(int sig UNUSED) 
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

  /* Bye! */
  exit(0);
}


static enum deg_str_type deg_type = deg_dd;

/* This gets called once for each new sentence until we figure out
   what's going on and switch to either update_gps_panel() or
   update_compass_panel(). */
static void update_probe(struct gps_data_t *gpsdata, 
		       char *message,
		       size_t len UNUSED , 
		       int level UNUSED)
{
  /* Send an 'i' once per second until we figure out what the GPS
     device is. */
  if(time(NULL)-misc_timer > 2) {
    (void)gps_query(gpsdata, "i\n");
    (void)fprintf(stderr,"Probing...\n");
    misc_timer=time(NULL);
  }

  assert(message != NULL);
  if(strncmp(message,"GPSD,I=",6)==0) {
    message+=7;
    (void)strlcpy(gps_type, message, 20);
    got_gps_type=1;
    /* If we're hooked to a compass, we display an entirely different
       screen and label the data much differently. */
    if(strstr(message,"True North")) {
      compass_flag=1;
    }
  }
}


/* This gets called once for each new compass sentence. */
static void update_compass_panel(struct gps_data_t *gpsdata, 
                         char *message,
                         size_t len UNUSED , 
                         int level UNUSED)
{
  char *s;

  /* Print time/date. */
  (void)wmove(datawin, 1,17);
  if (isnan(gpsdata->fix.time)==0) {
    char scr[128];
    (void)wprintw(datawin,"%s",unix_to_iso8601(gpsdata->fix.time, scr, (int)sizeof(s)));
  } else
    (void)wprintw(datawin,"n/a                    ");

  /* Fill in the heading. */
  (void)wmove(datawin, 2,17);
  if (isnan(gpsdata->fix.track)==0) {
    (void)wprintw(datawin,"%.1f     ", gpsdata->fix.track);
  } else
    (void)wprintw(datawin,"n/a         ");

  /* Fill in the pitch. */
  (void)wmove(datawin, 3,17);
  if (isnan(gpsdata->fix.climb)==0) {
    (void)wprintw(datawin,"%.1f     ", gpsdata->fix.climb);
  } else
    (void)wprintw(datawin,"n/a         ");

  /* Fill in the roll. */
  (void)wmove(datawin, 4,17);
  if (isnan(gpsdata->fix.speed)==0)
    (void)wprintw(datawin,"%.1f     ",gpsdata->fix.speed);
  else
    (void)wprintw(datawin,"n/a         ");

  /* Fill in the speed. */
  (void)wmove(datawin, 5,17);
  if (isnan(gpsdata->fix.altitude)==0)
    (void)wprintw(datawin,"%.1f     ", gpsdata->fix.altitude);
  else
    (void)wprintw(datawin,"n/a         ");

  /* Fill in GPS type. */
  (void)wmove(datawin, 6,17);
  if(got_gps_type==1) {
    (void)wprintw(datawin,"%s",gps_type);
  } else {
    (void)wprintw(datawin,"unknown     ");
  }

  /* Be quiet if the user requests silence. */
  if(silent_flag==0) {
    (void)wprintw(messages, "%s\n", message);
  }

  (void)wrefresh(datawin);
  (void)wrefresh(messages);
  (void)wrefresh(command);
}


/* This gets called once for each new GPS sentence. */
static void update_gps_panel(struct gps_data_t *gpsdata, 
                         char *message,
                         size_t len UNUSED , 
                         int level UNUSED)
{
  int i;
  int newstate;
  char *s;

  /* This is for the satellite status display.  Lifted almost
     verbatim from xgps.c.  Note that the satellite list may be
     truncated based on available screen size.  */
  if (gpsdata->satellites!=0) {
    for (i = 0; i < (bigger + 12); i++) {
      (void)wmove(satellites, i+2, 1);
      if (i < gpsdata->satellites) {
	(void)wprintw(satellites," %3d    %02d    %03d    %02d      %c  ",
		      gpsdata->PRN[i],
		      gpsdata->elevation[i], gpsdata->azimuth[i], 
		      gpsdata->ss[i],   gpsdata->used[i] ? 'Y' : 'N');
      } else {
	(void)wprintw(satellites,"                              ");
      }
    }
  }

  /* Print time/date. */
  (void)wmove(datawin, 1,17);
  if (isnan(gpsdata->fix.time)==0) {
    char scr[128];
    (void)wprintw(datawin,"%s",unix_to_iso8601(gpsdata->fix.time, scr, (int)sizeof(s)));
  } else
    (void)wprintw(datawin,"n/a                    ");

  /* Fill in the latitude. */
  (void)wmove(datawin, 2,17);
  if (gpsdata->fix.mode >= MODE_2D && isnan(gpsdata->fix.latitude)==0) {
    s = deg_to_str(deg_type,  fabs(gpsdata->fix.latitude));
    (void)wprintw(datawin,"%s %c     ", s, (gpsdata->fix.latitude < 0) ? 'S' : 'N');
  } else
    (void)wprintw(datawin,"n/a         ");

  /* Fill in the longitude. */
  (void)wmove(datawin, 3,17);
  if (gpsdata->fix.mode >= MODE_2D && isnan(gpsdata->fix.longitude)==0) {
    s = deg_to_str(deg_type,  fabs(gpsdata->fix.longitude));
    (void)wprintw(datawin,"%s %c     ", s, (gpsdata->fix.longitude < 0) ? 'W' : 'E');
  } else
    (void)wprintw(datawin,"n/a         ");

  /* Fill in the altitude. */
  (void)wmove(datawin, 4,17);
  if (gpsdata->fix.mode == MODE_3D && isnan(gpsdata->fix.altitude)==0)
    (void)wprintw(datawin,"%.1f %s     ",gpsdata->fix.altitude*altfactor, altunits);
  else
    (void)wprintw(datawin,"n/a         ");

  /* Fill in the speed. */
  (void)wmove(datawin, 5,17);
  if (gpsdata->fix.mode >= MODE_2D && isnan(gpsdata->fix.track)==0)
    (void)wprintw(datawin,"%.1f %s     ", gpsdata->fix.speed*speedfactor, speedunits);
  else
    (void)wprintw(datawin,"n/a         ");

  /* Fill in the heading. */
  (void)wmove(datawin, 6,17);
  if (gpsdata->fix.mode >= MODE_2D && isnan(gpsdata->fix.track)==0)
    (void)wprintw(datawin,"%.1f degrees     ", gpsdata->fix.track);
  else
    (void)wprintw(datawin,"n/a          ");

  /* Fill in the rate of climb. */
  (void)wmove(datawin, 7,17);
  if (gpsdata->fix.mode == MODE_3D && isnan(gpsdata->fix.climb)==0)
    (void)wprintw(datawin,"%.1f %s/min     "
                  , gpsdata->fix.climb * altfactor * 60, altunits);
  else
    (void)wprintw(datawin,"n/a         ");
  
  /* Fill in the estimated horizontal position error. */
  (void)wmove(datawin, 9,22);
  if (isnan(gpsdata->fix.eph)==0)
    (void)wprintw(datawin,"+/- %d %s     ", (int) (gpsdata->fix.eph * altfactor), altunits);
  else
    (void)wprintw(datawin,"n/a         ");

  /* Fill in the estimated vertical position error. */
  (void)wmove(datawin, 10,22);
  if (isnan(gpsdata->fix.epv)==0)
    (void)wprintw(datawin,"+/- %d %s     ", (int)(gpsdata->fix.epv * altfactor), altunits);
  else
    (void)wprintw(datawin,"n/a         ");

  /* Fill in the estimated track error. */
  (void)wmove(datawin, 11,22);
  if (isnan(gpsdata->fix.epd)==0)
    (void)wprintw(datawin,"+/- %.1f deg     ", (gpsdata->fix.epd));
  else
    (void)wprintw(datawin,"n/a          ");
  
  /* Fill in the estimated speed error. */
  (void)wmove(datawin, 12,22);
  if (isnan(gpsdata->fix.eps)==0)
    (void)wprintw(datawin,"+/- %d %s     ", (int)(gpsdata->fix.eps * speedfactor), speedunits);
  else
    (void)wprintw(datawin,"n/a            ");

  /* Fill in GPS type. */
  (void)wmove(datawin, 8,17);
  if(got_gps_type==1) {
    (void)wprintw(datawin,"%s",gps_type);
  } else {
    (void)wprintw(datawin,"unknown     ");
  }

  /* Fill in the GPS status and the time since the last state change. */
  (void)wmove(status, 1,10);
  if (gpsdata->online == 0) {
    newstate = 0;
    (void)wprintw(status,"OFFLINE          ");
  } else {
    newstate = gpsdata->fix.mode;
    switch (gpsdata->fix.mode) {
    case MODE_2D:
      (void)wprintw(status,"2D %sFIX (%d secs)   ",(gpsdata->status==STATUS_DGPS_FIX)?"DIFF ":"", (int) (time(NULL) - status_timer));
      break;
    case MODE_3D:
      (void)wprintw(status,"3D %sFIX (%d secs)   ",(gpsdata->status==STATUS_DGPS_FIX)?"DIFF ":"", (int) (time(NULL) - status_timer));
      break;
    default:
      (void)wprintw(status,"NO FIX (%d secs)     ", (int) (time(NULL) - status_timer));
      break;
    }
  }

  /* Be quiet if the user requests silence. */
  if(silent_flag==0) {
    (void)wprintw(messages, "%s\n", message);
  }

  /* Reset the status_timer if the state has changed. */
  if (newstate != state) {
    status_timer = time(NULL);
    state = newstate;
  }

  (void)wrefresh(datawin);
  (void)wrefresh(status);
  (void)wrefresh(satellites);
  (void)wrefresh(messages);
  (void)wrefresh(command);
}

static void usage( char *prog) 
{
  (void)fprintf(stderr, 
                "Usage: %s [-h] [-V] [-l {d|m|s}] [server[:port:[device]]]\n\n"
                "  -h          Show this help, then exit\n"
                "  -V          Show version, then exit\n"
                "  -s          Be silent (don't print raw dgps data)\n"
                "  -l {d|m|s}  Select lat/lon format\n"
                "                d = DD.dddddd\n"
                "                m = DD MM.mmmm'\n"
                "                s = DD MM' SS.sss\"\n"
                , prog);

  exit(1);
}

int main(int argc, char *argv[])
{
  int option;
  char *arg = NULL, *colon1, *colon2, *device = NULL, *server = NULL, *port = DEFAULT_GPSD_PORT;
  char *err_str = NULL;
  int c;

  int xsize, ysize;

  struct timeval timeout;
  fd_set rfds;
  int data;

  /* Process the options.  Print help if requested. */
  while ((option = getopt(argc, argv, "hVl:sj")) != -1) {
    switch (option) {
    case 's':
      silent_flag=1;
      break;
    case 'j':
      fixclear_flag=1;
      break;
    case 'V':
      (void)fprintf(stderr, "SVN ID: $Id$ \n");
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
    case 'h': default:
      usage(argv[0]);
      break;
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
    case NL_NOSERVICE:  err_str = "can't get service entry"; break;
    case NL_NOHOST:     err_str = "can't get host entry"; break;
    case NL_NOPROTO:    err_str = "can't get protocol entry"; break;
    case NL_NOSOCK:     err_str = "can't create socket"; break;
    case NL_NOSOCKOPT:  err_str = "error SETSOCKOPT SO_REUSEADDR"; break;
    case NL_NOCONNECT:  err_str = "can't connect to host"; break;
    default:                    err_str = "Unknown"; break;
    }
    (void)fprintf( stderr, 
                   "cgps: no gpsd running or network error: %d, %s\n", 
                   errno, err_str);
    exit(2);
  }

  /* Set both timers to now. */
  status_timer = time(NULL);
  misc_timer = status_timer;

  /* If the user requested a specific device, try to change to it. */
  if (device)
      (void)gps_query(gpsdata, "F=%s\n", device);

  /* Here's where updates go until we figure out what we're dealing
     with. */
  gps_set_raw_hook(gpsdata, update_probe);

  /* Tell me what you are... */
  (void)gps_query(gpsdata, "i\n");

  /* Loop for ten seconds looking for a device.  If none found, give
     up and assume "unknown" device type. */
  while(got_gps_type==0) {

    /* Sleep for one second. */
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    (int)select(0,NULL,NULL,NULL,&timeout);

    /* Give up after ten seconds. */
    if(time(NULL)-status_timer >= 10) {
      (void)strlcpy(gps_type, "unknown", 20);
      got_gps_type=1;
    }
  }

  /* If the user has requested the 'j' option (buffering), make the
     request of gpsd before we continue. */
  if(fixclear_flag==1 && compass_flag==0) {
    (void)gps_query(gpsdata, "j=1\n");
  }

  /* Fire up curses. */
  (void)initscr();
  (void)noecho();
  (void)signal(SIGINT,die);
  (void)signal(SIGHUP,die);

  /* See if the screen is big enough to show more than 12 sats. */
  (void)getmaxyx(stdscr,ysize,xsize);

  /* If the user's screen is too small, exit gracefully.  We do run as
     small as 80x20, but we force the raw gpsd data scrolling flag
     off.

     TODO: Allow sizes smaller than 80x20 by dropping less useful data
     fields and showing only certain satellites (maybe the ones
     actually used to compute the fix?)  

     TODO: Allow smaller than 80x20 for a compass display, as there
     are far fewer fields required. */
  if((ysize >= 20) && (ysize < 24) && (xsize >= 80)) {
    silent_flag=1;
  } else if((ysize < 24) || (xsize < 80)) {
    (void)mvprintw(0, 0, "Your screen must be at least 80x20 to run cgps.");
    /*@ -nullpass @*/
    (void)refresh();
    /*@ +nullpass @*/
    (void)sleep(5);
    die(0);
  }

  /* Set up the screen for either a compass or a gps receiver. */
  if(compass_flag==1) {
    /* We're a compass, set up accordingly. */

    /*@ -onlytrans @*/
    datawin    = newwin(8, 45, 1, 0);
    command    = newwin(3, 45, 9, 0);
    messages   = newwin(0, 0, 12, 0);

    /*@ +onlytrans @*/
    (void)scrollok(messages, true);
    (void)wsetscrreg(messages, 0, LINES-13);
    (void)nodelay(messages,(bool)TRUE);
    
    (void)mvprintw(0, 31, "CGPS Test Client");

    /*@ -nullpass @*/
    (void)refresh();
    /*@ +nullpass @*/
    
    /* Do the initial field label setup. */
    (void)mvwprintw(datawin, 1,5, "Time:");
    (void)mvwprintw(datawin, 2,5, "Heading:");
    (void)mvwprintw(datawin, 3,5, "Pitch:");
    (void)mvwprintw(datawin, 4,5, "Roll:");
    (void)mvwprintw(datawin, 5,5, "Dip:");
    (void)mvwprintw(datawin, 6,5, "GPS Type:");
    (void)wborder(datawin, 0, 0, 0, 0, 0, 0, 0, 0);
    (void)mvwprintw(command, 1,1, "Command:  ");
    (void)wborder(command, 0, 0, 0, 0, 0, 0, 0, 0);

  } else {
    /* We're a GPS, set up accordingly. */

    /* Grow the windows based on screen size, but not more than the
       maximum allowed number of sats.  Always leave at least four
       lines at the bottom of the screen to scroll gpsd raw data. */
    bigger=ysize-24;
    
    if(bigger<0) {
      bigger=0;
    }
    
    if(bigger > (MAXCHANNELS - 2 - 12)) {
      bigger = MAXCHANNELS - 2 - 12;
    }
    
    /*@ -onlytrans @*/
    datawin    = newwin(15+bigger, 45, 1, 0);
    satellites = newwin(15+bigger, 35, 1, 45);
    command    = newwin(3, 45, 16+bigger, 0);
    status     = newwin(3, 35, 16+bigger, 45);
    messages   = newwin(0, 0, 19+bigger, 0);

    /*@ +onlytrans @*/
    (void)scrollok(messages, true);
    (void)wsetscrreg(messages, 0, LINES-13);
    (void)nodelay(messages,(bool)TRUE);
    
    (void)mvprintw(0, 31, "CGPS Test Client");
    /*@ -nullpass @*/
    (void)refresh();
    /*@ +nullpass @*/
    
    /* Do the initial field label setup. */
    (void)mvwprintw(datawin, 1,5, "Time:");
    (void)mvwprintw(datawin, 2,5, "Latitude:");
    (void)mvwprintw(datawin, 3,5, "Longitude:");
    (void)mvwprintw(datawin, 4,5, "Altitude:");
    (void)mvwprintw(datawin, 5,5, "Speed:");
    (void)mvwprintw(datawin, 6,5, "Heading:");
    (void)mvwprintw(datawin, 7,5, "Climb:");
    (void)mvwprintw(datawin, 8,5, "GPS Type:");
    (void)mvwprintw(datawin, 9,5, "Horizontal Err:");
    (void)mvwprintw(datawin, 10,5, "Vertical Err:");
    (void)mvwprintw(datawin, 11,5, "Course Err:");
    (void)mvwprintw(datawin, 12,5, "Speed Err:");
    (void)mvwprintw(status, 1,1, "Status:");
    (void)wborder(datawin, 0, 0, 0, 0, 0, 0, 0, 0);
    (void)mvwprintw(satellites, 1,1, "PRN:   Elev:  Azim:  SNR:  Used:");
    (void)wborder(satellites, 0, 0, 0, 0, 0, 0, 0, 0);
    (void)mvwprintw(command, 1,1, "Command:  ");
    (void)wborder(command, 0, 0, 0, 0, 0, 0, 0, 0);
    (void)wborder(status, 0, 0, 0, 0, 0, 0, 0, 0);
  }

  /* Here's where updates go now that things are established. */
  if(compass_flag==1) {
    gps_set_raw_hook(gpsdata, update_compass_panel);
  } else {
    gps_set_raw_hook(gpsdata, update_gps_panel);
  }

  /* Request "w+x" data from gpsd. */
  (void)gps_query(gpsdata, "w+x\n");

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
      fprintf( stderr, "cgps: socket error\n");
      exit(2);
    }
    else if( data ) {
      /* code that calls gps_poll(gpsdata) */
      if (gps_poll(gpsdata) != 0)
        die(1);
    }

    /* Check for user input.

     TODO: Restrict the allowed keys based on device type (ie, 'j'
     makes no sense for a True North compass).  No doubt there will be
     other examples as cgps grows more bells and whistles. */
    c=wgetch(messages);
        
    switch ( c ) {
      /* Quit */
    case 'q':
      die(0);
      break;

      /* Toggle spewage of raw gpsd data. */
    case 's':
      if(silent_flag==0) {
        silent_flag=1;
      } else {
        silent_flag=0;
      }
      break;

      /* Toggle fix clear. */
    case 'j':
      if(fixclear_flag==0) {
        fixclear_flag=1;
        (void)gps_query(gpsdata, "j=1\n");
      } else {
        fixclear_flag=0;
        (void)gps_query(gpsdata, "j=0\n");
      }
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
