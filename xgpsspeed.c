/* GPS speedometer as a wrapper around an Athena widget Tachometer
 * - Derrick J Brashear <shadow@dementia.org>
 * Tachometer widget from Kerberometer (xklife)
 */
#include "config.h"
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdarg.h>
#include <getopt.h>
#include <errno.h>

#include <stdio.h>
#include <math.h>
#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <X11/Shell.h>
#include <X11/Xaw/Box.h>
#include <X11/Xaw/Label.h>
#include <X11/Xaw/Command.h>
#include <X11/Xaw/Paned.h>
#include <Tachometer.h>

#include "xgpsspeed.icon"

#include "gps.h"

#if defined(ultrix) || defined(SOLARIS) 
extern double rint();
#endif

struct gps_data_t *gpsdata;

static Widget toplevel, base;
static Widget tacho, label;

static XrmOptionDescRec options[] = {
{"-rv",		"*reverseVideo",	XrmoptionNoArg,		"TRUE"},
{"-nc",         "*needleColor",         XrmoptionSepArg,        NULL},
{"-needlecolor","*needleColor",         XrmoptionSepArg,        NULL},
};

/*
 * Definition of the Application resources structure.
 */

String fallback_resources[] =
{
  NULL
};

static void update_display(char *buf)
{
  int new = rint(gpsdata->speed * KNOTS_TO_MPH);
  if (new > 100)
    new = 100;

  TachometerSetValue(tacho, new);
}

static void handle_input(XtPointer client_data, int *source, XtInputId * id)
{
    gps_poll(gpsdata);
}

int main(int argc, char **argv)
{
    Arg             args[10];
    XtAppContext app;
    Cardinal        i;
    extern char *optarg;
    int option;
    char *server = NULL;

    while ((option = getopt(argc, argv, "hp:")) != -1) {
	switch (option) {
	case 'p':
	    server = strdup(optarg);
	    break;
	case 'h':
	case '?':
	default:
	    fputs("usage:  gps [options] \n\
  options include: \n\
  -p string    = set GPS device name \n\
  -h           = help message \n\
", stderr);
	    exit(1);
	}
    }

    toplevel = XtVaAppInitialize(&app, "xpsspeed.ad", 
				 options, XtNumber(options),
				 &argc, argv, fallback_resources, NULL);

   /**** Shell Widget ****/
    i = 0;
    XtSetArg(args[0], XtNiconPixmap,
	     XCreateBitmapFromData(XtDisplay(toplevel),
				   XtScreen(toplevel)->root, xgps_bits,
				   xgps_width, xgps_height)); i++;
    XtSetValues(toplevel, args, i);
    
    /**** Form widget ****/
    base = XtCreateManagedWidget("pane", panedWidgetClass, toplevel, NULL, 0);

    /**** Label widget (Title) ****/
    i = 0;
    XtSetArg(args[i], XtNlabel, "GPS Speedometer"); i++;
    label = XtCreateManagedWidget("title", labelWidgetClass,
				  base, args, i);

    /**** Label widget ****/
    i = 0;
    XtSetArg(args[i], XtNlabel, "Miles per Hour"); i++;
    label = XtCreateManagedWidget("name", labelWidgetClass,
				  base, args, i);
    
    /**** Tachometer widget ****/
    tacho = XtCreateManagedWidget("meter",
				  tachometerWidgetClass,
				  base, NULL, 0);
    
    XtRealizeWidget(toplevel);


    /*
     * Essentially all the interface to libgps happens below here
     */
    gpsdata = gps_open(server, DEFAULT_GPSD_PORT);
    if (!gpsdata)
    {
	fprintf(stderr, "xgpsspeed: no gpsd running or network error (%d).\n", errno);
	exit(2);
    }

    XtAppAddInput(app, gpsdata->gps_fd, (XtPointer) XtInputReadMask,
		  handle_input, NULL);
    
    gps_set_raw_hook(gpsdata, update_display);
    gps_query(gpsdata, "w+x\n");

    XtAppMainLoop(app);

    gps_close(gpsdata);
    return 0;
}

void Usage()
{
    fprintf(stderr, 
	    "xgpsspeed <Toolkit Options> [-rv] [-nc needlecolor]\n");
    exit(-1);
}




