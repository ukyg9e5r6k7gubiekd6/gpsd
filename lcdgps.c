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

  A client that passes gpsd data to lcdproc, turning your car computer
  into a very expensive feature-free GPS receiver ;^).  Currently
  assumes a 4x40 LCD and writes data formatted to fit that size
  screen.  Also displays Maidenhead grid square output for the hams among us.

  This program assumes that LCDd (lcdproc) is running locally on the
  default (13666) port.  The #defines LCDDHOST and LCDDPORT can be
  changed to talk to a different host and TCP port.
*/

#define LCDDHOST "localhost"
#define LCDDPORT 13666

#define CLIMB 3

#include <sys/types.h>
#include <sys/stat.h>
#ifndef S_SPLINT_S
#include <netdb.h>
#ifndef AF_UNSPEC
#include <sys/socket.h>
#endif /* AF_UNSPEC */
#endif /* S_SPLINT_S */
#ifndef INADDR_ANY
#include <netinet/in.h>
#endif /* INADDR_ANY */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <stdio.h>
#ifndef S_SPLINT_S
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif /* S_SPLINT_S */

#include "gps.h"
#include "gpsdclient.h"
#include "revision.h"

/* Prototypes. */
ssize_t sockreadline(int sockd,void *vptr,size_t maxlen);
ssize_t sockwriteline(int sockd,const void *vptr,size_t n);
int send_lcd(char *buf);

static struct fixsource_t source;
static struct gps_data_t gpsdata;
static float altfactor = METERS_TO_FEET;
static float speedfactor = MPS_TO_MPH;
static char *altunits = "ft";
static char *speedunits = "mph";
double avgclimb, climb[CLIMB];

/* Global socket descriptor for LCDd. */
int sd;

/*  Read a line from a socket  */
ssize_t sockreadline(int sockd,void *vptr,size_t maxlen) {
  ssize_t n,rc;
  char    c,*buffer;

  buffer=vptr;

  for (n = 1; n < (ssize_t)maxlen; n++) {

    if((rc=read(sockd,&c,1))==1) {
      *buffer++=c;
      if(c=='\n')
        break;
    }
    else if(rc==0) {
      if(n==1)
        return(0);
      else
        break;
    }
    else {
      if(errno==EINTR)
        continue;
      return(-1);
    }
  }

  *buffer=0;
  return(n);
}

/*  Write a line to a socket  */
ssize_t sockwriteline(int sockd,const void *vptr,size_t n) {
  size_t      nleft;
  ssize_t     nwritten;
  const char *buffer;

  buffer=vptr;
  nleft=n;

  while(nleft>0) {
    if((nwritten= write(sockd,buffer,nleft))<=0) {
      if(errno==EINTR)
        nwritten=0;
      else
        return(-1);
    }
    nleft-=nwritten;
    buffer+=nwritten;
  }

  return(n);
}

/* send a command to the LCD */
int send_lcd(char *buf) {

  int res;
  char rcvbuf[256];
  size_t outlen;

  /* Limit the size of outgoing strings. */
  outlen = strlen(buf);
  if(outlen > 255) {
    outlen = 256;
  }

  /* send the command */
  (void)sockwriteline(sd,buf,outlen);

  /* TODO:  check return status */

  /* read the data */
  res=sockreadline(sd,rcvbuf,sizeof(rcvbuf)-1);

  /* null-terminate the string before printing */
  /* rcvbuf[res-1]=0; FIX-ME: not using this at the moment... */

  /* return the result */
  return(res);
}

/* reset the LCD */
static void reset_lcd(void) {

  /* Initialize.  In theory, we should look at what's returned, as it
     tells us info on the attached LCD module.  TODO. */
  send_lcd("hello\n");

  /* Set up the screen */
  send_lcd("client_set name {GPSD test}\n");
  send_lcd("screen_add gpsd\n");
  send_lcd("widget_add gpsd one string\n");
  send_lcd("widget_add gpsd two string\n");
  send_lcd("widget_add gpsd three string\n");
  send_lcd("widget_add gpsd four string\n");
}

static enum deg_str_type deg_type = deg_dd;

/* This gets called once for each new sentence. */
static void update_lcd(struct gps_data_t *gpsdata)
{
  char tmpbuf[255];
  char *s, *gridsquare;

  /* Get our location in Maidenhead. */
  gridsquare = maidenhead(gpsdata->fix.latitude,gpsdata->fix.longitude);

  /* Fill in the latitude and longitude. */
  if (gpsdata->fix.mode >= MODE_2D) {
    int track;

    s = deg_to_str(deg_type,  fabs(gpsdata->fix.latitude));
    snprintf(tmpbuf, 254, "widget_set gpsd one 1 1 {Lat: %s %c}\n", s, (gpsdata->fix.latitude < 0) ? 'S' : 'N');
    send_lcd(tmpbuf);

    s = deg_to_str(deg_type,  fabs(gpsdata->fix.longitude));
    snprintf(tmpbuf, 254, "widget_set gpsd two 1 2 {Lon: %s %c}\n", s, (gpsdata->fix.longitude < 0) ? 'W' : 'E');
    send_lcd(tmpbuf);

    /* As a pilot, a heading of "0" gives me the heebie-jeebies (ie, 0
       == "invalid heading data", 360 == "North"). */
    track=(int)(gpsdata->fix.track);
    if(track == 0) track = 360;

    snprintf(tmpbuf, 254, "widget_set gpsd three 1 3 {%.1f %s %d deg}\n",
             gpsdata->fix.speed*speedfactor, speedunits,
             track);
    send_lcd(tmpbuf);

  } else {

    send_lcd("widget_set gpsd one 1 1 {Lat: n/a}\n");
    send_lcd("widget_set gpsd two 1 2 {Lon: n/a}\n");
    send_lcd("widget_set gpsd three 1 3 {n/a}\n");
  }

  /* Fill in the altitude and fix status. */
  if (gpsdata->fix.mode == MODE_3D) {
    int n;
    for(n=0;n<CLIMB-2;n++) climb[n]=climb[n+1];
    climb[CLIMB-1]=gpsdata->fix.climb;
    avgclimb=0.0;
    for(n=0;n<CLIMB;n++) avgclimb+=climb[n];
    avgclimb/=CLIMB;
    snprintf(tmpbuf, 254, "widget_set gpsd four 1 4 {%d %s %s %d fpm       }\n",
            (int)(gpsdata->fix.altitude*altfactor), altunits, gridsquare, (int)(avgclimb * METERS_TO_FEET * 60));
  } else {
    snprintf(tmpbuf, 254, "widget_set gpsd four 1 4 {n/a}\n");
  }
  send_lcd(tmpbuf);
}

static void usage( char *prog)
{
  (void)fprintf(stderr,
                "Usage: %s [-h] [-v] [-V] [-s] [-l {d|m|s}] [-u {i|m|n}] [server[:port:[device]]]\n\n"
                "  -h          Show this help, then exit\n"
                "  -V          Show version, then exit\n"
                "  -s          Sleep for 10 seconds before starting\n"
                "  -j          Turn on anti-jitter buffering\n"
                "  -l {d|m|s}  Select lat/lon format\n"
                "                d = DD.dddddd (default)\n"
                "                m = DD MM.mmmm'\n"
                "                s = DD MM' SS.sss\"\n"
                "  -u {i|m|n}  Select Units\n"
                "                i = Imperial (default)\n"
                "                m = Metric'\n"
                "                n = Nautical\"\n"
                , prog);

  exit(1);
}

int main(int argc, char *argv[]) 
{
    int option, rc;
    struct sockaddr_in localAddr, servAddr;
    struct hostent *h;

    int n;
    for(n=0;n<CLIMB;n++) climb[n]=0.0;

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
    while ((option = getopt(argc, argv, "Vhl:su:")) != -1) {
	switch (option) {
	case 'V':
	    (void)fprintf(stderr, "lcdgs revision " REVISION "\n");
	    exit(0);
	case 'h':
	default:
	    usage(argv[0]);
	    break;
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
	case 's':
	    sleep(10);
	    continue;
	case 'u':
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
		/*@ -casebreak @*/
	    }
	}
    }

    /* Grok the server, port, and device. */
  if (optind < argc) {
      gpsd_source_spec(argv[optind], &source);
  } else
      gpsd_source_spec(NULL, &source);

    /* Daemonize... */
  if (daemon(0, 0) != 0)
      (void)fprintf(stderr,
		    "lcdgps: demonization failed: %s\n",
		    strerror(errno));

    /* Open the stream to gpsd. */
    if (gps_open(source.server, source.port, &gpsdata) != 0) {
	(void)fprintf( stderr,
		       "lcdgps: no gpsd running or network error: %d, %s\n",
		       errno, gps_errstr(errno));
	exit(2);
    }

    /* Connect to LCDd */
    h = gethostbyname(LCDDHOST);
    if(h==NULL) {
	printf("%s: unknown host '%s'\n",argv[0],LCDDHOST);
	exit(1);
    }

    servAddr.sin_family = h->h_addrtype;
    memcpy((char *) &servAddr.sin_addr.s_addr, h->h_addr_list[0], h->h_length);
    servAddr.sin_port = htons(LCDDPORT);

    /* create socket */
    sd = socket(AF_INET, SOCK_STREAM, 0);
    if(sd == -1) {
	perror("cannot open socket ");
	exit(1);
    }

    /* bind any port number */
    localAddr.sin_family = AF_INET;
    localAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    localAddr.sin_port = htons(0);

    rc = bind(sd, (struct sockaddr *) &localAddr, sizeof(localAddr));
    if(rc == -1) {
	printf("%s: cannot bind port TCP %u\n",argv[0],LCDDPORT);
	perror("error ");
	exit(1);
    }

    /* connect to server */
    rc = connect(sd, (struct sockaddr *) &servAddr, sizeof(servAddr));
    if(rc == -1) {
	perror("cannot connect ");
	exit(1);
    }

    /* Do the initial field label setup. */
    reset_lcd();

    /* Here's where updates go. */
    unsigned int flags = WATCH_ENABLE;
    if (source.device != NULL)
	flags |= WATCH_DEVICE;
    (void)gps_stream(&gpsdata, flags, source.device);

    for (;;) { /* heart of the client */
	if (!gps_waiting(&gpsdata, 50000000)) {
	    fprintf( stderr, "lcdgps: error while waiting\n");
	    exit(2);
	} else {
	    (void)gps_read(&gpsdata);
	    update_lcd(&gpsdata);
	}

    }
}
