/* GPS speedometer as a wrapper around an Athena widget Tachometer
 * - Derrick J Brashear <shadow@dementia.org>
 */
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <math.h>
#include <X11/Intrinsic.h>
#include <X11/Shell.h>
#include <X11/Xaw/Label.h>
#include <X11/Xaw/Paned.h>
#include <Xm/XmStrDefs.h>
#include <Tachometer.h>

#include "config.h"
#include "xgpsspeed.icon"
#include "gps.h"

static XrmOptionDescRec options[] = {
{"-rv",		"*reverseVideo",	XrmoptionNoArg,		"TRUE"},
{"-nc",         "*needleColor",         XrmoptionSepArg,        NULL},
{"-needlecolor","*needleColor",         XrmoptionSepArg,        NULL},
{"-speedunits", "*speedunits",          XrmoptionSepArg,        NULL},
};
String fallback_resources[] = {NULL};

static struct gps_data_t *gpsdata;
static Widget tacho;
static double speedfactor;
static Widget toplevel;

static void update_display(struct gps_data_t *gpsdata, 
			   char *buf UNUSED, int level UNUSED)
{
    TachometerSetValue(tacho, rint(gpsdata->fix.speed * speedfactor));
}

static void handle_input(XtPointer client_data UNUSED,
			 int *source UNUSED, XtInputId * id UNUSED)
{
    gps_poll(gpsdata);
}

static char *get_resource(char *name, char *default_value)
{
  XtResource xtr;
  char *value = NULL;

  xtr.resource_name = name;
  xtr.resource_class = "AnyClass";
  xtr.resource_type = XmRString;
  xtr.resource_size = sizeof(String);
  xtr.resource_offset = 0;
  xtr.default_type = XmRImmediate;
  xtr.default_addr = default_value;
  XtGetApplicationResources(toplevel, &value, &xtr, 1, NULL, 0);
  if (value) return value;
  return default_value;
}

int main(int argc, char **argv)
{
    Arg             args[10];
    XtAppContext app;
    int option;
    char *arg, *colon1, *colon2, *device = NULL, *server = NULL, *port = DEFAULT_GPSD_PORT;
    char *speedunits;
    Widget base;

    toplevel = XtVaAppInitialize(&app, "xgpsspeed", 
				 options, XtNumber(options),
				 &argc, argv, fallback_resources, NULL);

    speedfactor = MPS_TO_MPH;		/* Software maintained in US */
    speedunits = get_resource("speedunits", "mph");
    if (!strcmp(speedunits, "kph")) 
	speedfactor = MPS_TO_KPH;
    else if (!strcmp(speedunits, "knots"))
	speedfactor = 1/MPS_TO_KNOTS;

    while ((option = getopt(argc, argv, "hv")) != -1) {
	switch (option) {
	case 'v':
	    printf("xgpsspeed %s\n", VERSION);
	    exit(0);
	case 'h': default:
	    fputs("usage: gps [-h] [-v] [-rv] [-nc] [-needlecolor] [-speedunits {mph,kph,knots}] [server[:port]]\n", stderr);
	    exit(1);
	}
    }
    if (optind < argc) {
	arg = strdup(argv[optind]);
	colon1 = strchr(arg, ':');
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
    }

   /**** Shell Widget ****/
    XtSetArg(args[0], XtNiconPixmap,
	     XCreateBitmapFromData(XtDisplay(toplevel),
				   XtScreen(toplevel)->root, (char*)xgps_bits,
				   xgps_width, xgps_height));
    XtSetValues(toplevel, args, 1);
    
    /**** Form widget ****/
    base = XtCreateManagedWidget("pane", panedWidgetClass, toplevel, NULL, 0);

    /**** Label widget (Title) ****/
    XtSetArg(args[0], XtNlabel, "GPS Speedometer");
    XtCreateManagedWidget("title", labelWidgetClass, base, args, 1);

    /**** Label widget ****/
    if (speedfactor == KNOTS_TO_MPH)
        XtSetArg(args[0], XtNlabel, "Miles per Hour");
    else
        XtSetArg(args[0], XtNlabel, "Km per Hour");
    XtCreateManagedWidget("name", labelWidgetClass, base, args, 1);
    
    /**** Tachometer widget ****/
    tacho = XtCreateManagedWidget("meter", tachometerWidgetClass,base,NULL,0);
    XtRealizeWidget(toplevel);

    if (!(gpsdata = gps_open(server, DEFAULT_GPSD_PORT))) {
	fputs("xgpsspeed: no gpsd running or network error\n", stderr);
	exit(2);
    }

    XtAppAddInput(app, gpsdata->gps_fd, (XtPointer) XtInputReadMask,
		  handle_input, NULL);
    
    gps_set_raw_hook(gpsdata, update_display);

    if (device) {
	char *channelcmd = (char *)malloc(strlen(device)+3);

	strcpy(channelcmd, "F=");
	strcpy(channelcmd+2, device);
	gps_query(gpsdata, channelcmd);
    }
	
    gps_query(gpsdata, "w+x\n");

    XtAppMainLoop(app);

    gps_close(gpsdata);
    return 0;
}
