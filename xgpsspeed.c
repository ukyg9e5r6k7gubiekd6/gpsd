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
#include "outdata.h"
#include "nmea.h"
#include "gpsd.h"

#define BUFSIZE          4096
#define DEFAULTPORT "2947"

#if defined(ultrix) || defined(SOLARIS) 
extern double rint();
#endif

struct session_t session;

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

void gps_gpscli_errexit(char *s)
{
    perror(s);
    exit(1);
}

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

int
main(int argc, char **argv)
{
    Arg             args[10];
    XtAppContext app;
    Cardinal        i;

    session.debug = 1;

    toplevel = XtVaAppInitialize(&app, "XGpsSpeed", options, XtNumber(options),
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

void update_display()
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
  static unsigned char buf[BUFSIZE];  /* that is more than a sentence */
  static int offset = 0;
  int count;
  int flags;

  ioctl(*source, FIONREAD, &count);

  /* Make the port NON-BLOCKING so reads will not wait if no data */
  if ((flags = fcntl(*source, F_GETFL)) < 0) return;
  if (fcntl(*source, F_SETFL, flags | O_NDELAY) < 0) return;

  while (offset < BUFSIZE && count--) {
    if (read(*source, buf + offset, 1) != 1)
      return;

    if (buf[offset] == '\n') {
      if (buf[offset - 1] == '\r')
	buf[offset - 1] = '\0';
      gps_NMEA_handle_message(buf);
      update_display();
      offset = 0;
      return;
    }
    offset++;
  }
}

int my_gps_open()
{
    char *temp;
    char *port = DEFAULTPORT;
    char *device_name="localhost";
    int ttyfd;

    temp = malloc(strlen(device_name) + 1);
    strcpy(temp, device_name);

    /* temp now holds the HOSTNAME portion and port the port number. */
    ttyfd = netlib_connectTCP(temp, port);
    free(temp);
    port = 0;

    if (write(ttyfd, "r\n", 2) != 2)
      gps_gpscli_errexit("Can't write to socket");
    return ttyfd;
}

static void open_input(XtAppContext app)
{
    int input = 0;
    XtInputId input_id;

    input = my_gps_open();

    input_id = XtAppAddInput(app, input, (XtPointer) XtInputReadMask,
                             handle_input, NULL);
}
