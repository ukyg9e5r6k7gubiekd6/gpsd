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

#include "gpsd.h"

#if defined(ultrix) || defined(SOLARIS) 
extern double rint();
#endif

struct gpsd_t session;
static char *device_name = NULL;

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

#if 0
typedef struct _XGpsResources {
} XGpsResources;

XGpsResources resources;
#endif

#define Offset(field) (XtOffset(XGpsResources *, field))

#if 0
static XtResource my_resources[] = {
};
#endif

String fallback_resources[] =
{
  NULL
};

static void open_input(XtAppContext app);

#undef Offset

void gpscli_report(int errlevel, const char *fmt, ... )
/* assemble command in printf(3) style, use stderr or syslog */
{
    char buf[BUFSIZ];
    va_list ap;

    strcpy(buf, "xgpsspeed: ");
    va_start(ap, fmt) ;
#ifdef HAVE_VSNPRINTF
    vsnprintf(buf + strlen(buf), sizeof(buf)-strlen(buf), fmt, ap);
#else
    vsprintf(buf + strlen(buf), fmt, ap);
#endif
    va_end(ap);

    if (errlevel > session.debug)
	return;

    fputs(buf, stderr);
}

static void update_display(char *);

int
main(int argc, char **argv)
{
    Arg             args[10];
    XtAppContext app;
    Cardinal        i;
    extern char *optarg;
    char devtype = 'n';
    int option;

    session.debug = 1;
    while ((option = getopt(argc, argv, "D:T:hp:")) != -1) {
	switch (option) {
        case 'T':
	    devtype = *optarg;
            break;
	case 'D':
	    session.debug = (int) strtol(optarg, 0, 0);
	    break;
	case 'p':
	    if (device_name)
		free(device_name);
	    device_name = malloc(strlen(optarg) + 1);
	    strcpy(device_name, optarg);
	    break;
	case 'h':
	case '?':
	default:
	    fputs("usage:  gps [options] \n\
  options include: \n\
  -p string    = set GPS device name \n\
  -T {e|t}     = set GPS device type \n\
  -s baud_rate = set baud rate on GPS device \n\
  -D integer   = set debug level \n\
  -h           = help message \n\
", stderr);
	    exit(1);
	}
    }


    toplevel = XtVaAppInitialize(&app, "xpsspeed.ad", options, XtNumber(options),
			    &argc, argv, fallback_resources, NULL);

#if 0
    XtGetApplicationResources( toplevel, (caddr_t) &resources, 
			      my_resources, XtNumber(my_resources),
			      NULL, (Cardinal) 0);
#endif

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
    gps_init(&session,device_name, 5, devtype, NULL, update_display);
    open_input(app);
    
    XtAppMainLoop(app);

    return 0;
}



void Usage()
{
    fprintf(stderr, 
	    "xgpsspeed <Toolkit Options> [-rv] [-nc needlecolor]\n");
    exit(-1);
}


#if 0
#if defined(i386) || defined(__hpux)
#define rint (int)
#endif	
#endif

void update_display(char *buf)
{
  int new = rint(session.gNMEAdata.speed * 6076.12 / 5280);
#if 0
  fprintf(stderr, "gNMEAspeed %f scaled %f %d\n", session.gNMEAdata.speed, rint(session.gNMEAdata.speed * 5208/6706.12), (int)rint(session.gNMEAdata.speed * 5208/6706.12));
#endif
  if (new > 100)
    new = 100;

  TachometerSetValue(tacho, new);
}

static void handle_input(XtPointer client_data, int *source, XtInputId * id)
{
    gps_poll(&session);
}

static void open_input(XtAppContext app)
{
    XtInputId input_id;

    gps_activate(&session);

    input_id = XtAppAddInput(app, session.fdin, (XtPointer) XtInputReadMask,
                             handle_input, NULL);
}
