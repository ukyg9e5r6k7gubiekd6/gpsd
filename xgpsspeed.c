/* GPS speedometer as a wrapper around an Athena widget Tachometer
 * - Derrick J Brashear <shadow@dementia.org>
 * Tachometer widget from Kerberometer (xklife)
 */
#include "config.h"
#include "xgpsspeed.h"
#include "xgpsspeed.icon"
#include "nmea.h"
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#define BUFSIZE          4096
#define DEFAULTPORT "2947"
char latd, lond;
double latitude, longitude;
int device_type;
int debug = 0;

#if defined(ultrix) || defined(SOLARIS) 
extern double rint();
#endif

Widget toplevel, base;
Widget tacho, label, title;

static XrmOptionDescRec options[] = {
{"-rv",		"*reverseVideo",	XrmoptionNoArg,		"TRUE"},
{"-nc",         "*needleColor",         XrmoptionSepArg,        NULL},
{"-needlecolor","*needleColor",         XrmoptionSepArg,        NULL},
};

/*
 * Definition of the Application resources structure.
 */

typedef struct _XGpsResources {
} XGpsResources;

XGpsResources resources;

#define Offset(field) (XtOffset(XGpsResources *, field))

static XtResource my_resources[] = {
};

String fallback_resources[] =
{
};

static void open_input(XtAppContext app);

#undef Offset

int errexit(char *s)
{
    perror(s);
    /* serial_close(); */
    exit(1);
}

int
main(int argc, char **argv)
{
    Arg             args[10];
    XtAppContext app;
    Cardinal        i;
    char *cp;
    int ret;


    toplevel = XtVaAppInitialize(&app, "XGpsSpeed", options, XtNumber(options),
			    &argc, argv, fallback_resources, NULL);

    XtGetApplicationResources( toplevel, (caddr_t) &resources, 
			      my_resources, XtNumber(my_resources),
			      NULL, (Cardinal) 0);

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
}



Usage()
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
  int new = rint(gNMEAdata.speed * 6076.12 / 5280);
#if 0
  fprintf(stderr, "gNMEAspeed %f scaled %f %d\n", gNMEAdata.speed, rint(gNMEAdata.speed * 5208/6706.12), (int)rint(gNMEAdata.speed * 5208/6706.12));
#endif
  if (new > 100)
    new = 100;

  TachometerSetValue(tacho, new);
}

static void handle_input(XtPointer client_data, int *source, XtInputId * id)
{
  double speed;
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
      handle_message(buf);
      update_display();
      offset = 0;
      return;
    }
    offset++;
  }
}

int my_serial_open()
{
    char *temp;
    char *p;
    char *port = DEFAULTPORT;
    char *device_name="localhost";
    int one = 1;
    int ttyfd;

    temp = malloc(strlen(device_name) + 1);
    strcpy(temp, device_name);

    /* temp now holds the HOSTNAME portion and port the port number. */
    ttyfd = connectTCP(temp, port);
    free(temp);
    port = 0;

    setsockopt(ttyfd, SOL_SOCKET, SO_REUSEADDR, (char *) &one, sizeof(one));

    if (write(ttyfd, "r\n", 2) != 2)
      errexit("Can't write to socket");
    return ttyfd;
}

static void open_input(XtAppContext app)
{
    int input = 0;
    XtInputId input_id;

    input = my_serial_open();

    input_id = XtAppAddInput(app, input, (XtPointer) XtInputReadMask,
                             handle_input, NULL);
}
