
/* include files */
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>
#include <errno.h>

#if defined (HAVE_SYS_TERMIOS_H)
#include <sys/termios.h>
#else
#if defined (HAVE_TERMIOS_H)
#include <termios.h>
#endif
#endif

#include <Xm/Xm.h>
#include <Xm/MwmUtil.h>
#include <Xm/ScrolledW.h>
#include <Xm/ScrollBar.h>
#include <Xm/PushB.h>
#include <Xm/ToggleB.h>
#include <Xm/ArrowB.h>
#include <Xm/CascadeB.h>
#include <Xm/Separator.h>
#include <Xm/DrawnB.h>
#include <Xm/Scale.h>
#include <Xm/Frame.h>
#include <Xm/Form.h>
#include <Xm/RowColumn.h>
#include <Xm/Label.h>
#include <Xm/TextF.h>
#include <Xm/Text.h>
#include <Xm/List.h>
#include <Xm/DrawingA.h>
#include <Xm/MenuShell.h>
#include <Xm/Protocols.h>
#include <X11/Shell.h>
#include <sys/ioctl.h>
#ifdef HAVE_SYS_FILIO_H
#include <sys/filio.h>
#endif

#include "gps.h"
#include "gpsd.h"

extern void register_canvas(Widget w, GC gc);
extern void draw_graphics(struct gps_data *gpsdata);

struct gpsd_t session;

void update_display(char *message);

/* global variables */
static Widget lxbApp;
static Widget form_6;
static Widget list_7;
#ifdef PROCESS_PRWIZCH
static Widget list_8;
#endif /* PROCESS_PRWIZCH */
static Widget drawingArea_8;
static Widget rowColumn_10;
static Widget rowColumn_11;
static Widget rowColumn_12;
static Widget rowColumn_13;
static Widget rowColumn_14;
static Widget rowColumn_15;
static Widget rowColumn_16;
static Widget rowColumn_17;
static Widget rowColumn_18;
static Widget pushButton_11;
static Widget text_1, text_2, text_3, text_4, text_5, text_6, text_7;
static Widget label_1, label_2, label_3, label_4, label_5, label_6, label_7;
static Widget status;

static int device_speed = 4800;
static char *device_name = 0;
static char *default_device_name = "localhost:2947";

String fallback_resources[] =
{
    "*gps_data.time.label.labelString: Time  ",
    "*gps_data.latitude.label.labelString: Lat.  ",
    "*gps_data.longitude.label.labelString: Long. ",
    "*gps_data.altitude.label.labelString: Alt.  ",
    "*gps_data.speed.label.labelString: Speed ",
    "*gps_data.track.label.labelString: Track ",
    "*gps_data.fix_status.label.labelString: Status",
    "*gps_data.quit.label.labelString: Quit",
    NULL
};


GC gc;
static Atom delw;

extern void redraw();

void quit_cb()
{
    gps_close();
    exit(0);
}

void gpscli_report(int errlevel, const char *fmt, ... )
/* assemble command in printf(3) style, use stderr or syslog */
{
    char buf[BUFSIZ];
    va_list ap;

    strcpy(buf, "gpsd: ");
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

/**************************************************
* Function: get_pixel
**************************************************/
Pixel
get_pixel(Widget w, char *resource_value)
{
    Colormap colormap;
    Boolean status;
    XColor exact, color;

    colormap = DefaultColormapOfScreen(DefaultScreenOfDisplay(XtDisplay(w)));

    status = XAllocNamedColor(XtDisplay(w), colormap, resource_value, &color, &exact);

    if (status == 0) {
	fprintf(stderr, "Unknown color: %s", resource_value);
	color.pixel = BlackPixelOfScreen(DefaultScreenOfDisplay(XtDisplay(w)));
    };

    return (color.pixel);
}

/**************************************************
* Function: build_gui
**************************************************/
static void build_gui(Widget lxbApp)
{
    int n;
    Arg args[100];
    XGCValues gcv;

    n = 0;
    XtSetArg(args[n], XmNrubberPositioning, False);
    n++;
    XtSetArg(args[n], XmNresizePolicy, XmRESIZE_NONE);
    n++;

    form_6 = XtCreateManagedWidget("gps_data", xmFormWidgetClass, lxbApp, args, n);

#define FRAMEHEIGHT	220
    /* satellite location and SNR display */
    XtSetArg(args[0], XmNbackground, get_pixel(lxbApp, "snow"));
    XtSetArg(args[1], XmNleftOffset, 10);
    XtSetArg(args[2], XmNtopOffset, 10);
    XtSetArg(args[3], XmNbottomAttachment, XmATTACH_NONE);
    XtSetArg(args[4], XmNleftAttachment, XmATTACH_FORM);
    XtSetArg(args[5], XmNtopAttachment, XmATTACH_FORM);
    XtSetArg(args[6], XmNheight, FRAMEHEIGHT);
#ifdef PROCESS_PRWIZCH
    XtSetArg(args[7], XmNwidth, 100);
#else
    XtSetArg(args[7], XmNwidth, 180);
#endif /* PROCESS_PRWIZCH */
    XtSetArg(args[8], XmNlistSizePolicy, XmCONSTANT);
    XtSetArg(args[9], XmNhighlightThickness, 0);
    XtSetArg(args[10], XmNlistSpacing, 4);
    list_7 = XtCreateManagedWidget("list_7", xmListWidgetClass, form_6, args, 11);

#ifdef PROCESS_PRWIZCH
    /* signal quality display */
    XtSetArg(args[0], XmNbackground, get_pixel(lxbApp, "snow"));
    XtSetArg(args[1], XmNleftOffset, 10);
    XtSetArg(args[2], XmNtopOffset, 10);
    XtSetArg(args[3], XmNbottomAttachment, XmATTACH_NONE);
    XtSetArg(args[4], XmNleftAttachment, XmATTACH_WIDGET);
    XtSetArg(args[5], XmNtopAttachment, XmATTACH_FORM);
    XtSetArg(args[6], XmNheight, FRAMEHEIGHT);
    XtSetArg(args[7], XmNwidth, 80);
    XtSetArg(args[8], XmNlistSizePolicy, XmCONSTANT);
    XtSetArg(args[9], XmNhighlightThickness, 0);
    XtSetArg(args[10], XmNlistSpacing, 4);
    XtSetArg(args[11], XmNleftWidget, list_7);
    list_8 = XtCreateManagedWidget("list_8", xmListWidgetClass, form_6, args, 12);
#endif /* PROCESS_PRWIZCH */

    /* the satellite diagram */
    XtSetArg(args[0], XmNbottomAttachment, XmATTACH_NONE);
    XtSetArg(args[1], XmNleftOffset, 10);
    XtSetArg(args[2], XmNrightOffset, 10);
    XtSetArg(args[3], XmNbackground, get_pixel(lxbApp, "snow"));
    XtSetArg(args[4], XmNy, 10);
    XtSetArg(args[5], XmNx, 80);
    XtSetArg(args[6], XmNrightAttachment, XmATTACH_NONE);
#ifdef PROCESS_PRWIZCH
    XtSetArg(args[7], XmNleftWidget, list_8);
#else
    XtSetArg(args[7], XmNleftWidget, list_7);
#endif /* PROCESS_PRWIZCH */
    XtSetArg(args[8], XmNtopOffset, 10);
    XtSetArg(args[9], XmNleftAttachment, XmATTACH_WIDGET);
    XtSetArg(args[10], XmNtopAttachment, XmATTACH_FORM);
    XtSetArg(args[11], XmNresizePolicy, XmRESIZE_NONE);
    XtSetArg(args[12], XmNheight, 402);
    XtSetArg(args[13], XmNwidth, 402);
    drawingArea_8 = XtCreateManagedWidget("drawingArea_8",
			     xmDrawingAreaWidgetClass, form_6, args, 14);
    gcv.foreground = BlackPixelOfScreen(XtScreen(drawingArea_8));
    gc = XCreateGC(XtDisplay(drawingArea_8),
	RootWindowOfScreen(XtScreen(drawingArea_8)), GCForeground, &gcv);

    register_canvas(drawingArea_8, gc);
    XtAddCallback(drawingArea_8, XmNexposeCallback, redraw, NULL);

    XtSetArg(args[0], XmNtopOffset, 10);
    XtSetArg(args[1], XmNbottomOffset, 10);
    XtSetArg(args[2], XmNrightOffset, 10);
    XtSetArg(args[3], XmNleftOffset, 10);
    XtSetArg(args[4], XmNorientation, XmVERTICAL);
    XtSetArg(args[5], XmNrightAttachment, XmATTACH_WIDGET);
    XtSetArg(args[6], XmNrightWidget, drawingArea_8);
    XtSetArg(args[7], XmNbottomAttachment, XmATTACH_NONE);	/* XXX */

    XtSetArg(args[8], XmNy, 352);
    XtSetArg(args[9], XmNx, 0);
    XtSetArg(args[10], XmNleftAttachment, XmATTACH_FORM);
    XtSetArg(args[11], XmNtopAttachment, XmATTACH_WIDGET);
    XtSetArg(args[12], XmNtopWidget, list_7);
    XtSetArg(args[13], XmNheight, 12);
    rowColumn_10 = XtCreateManagedWidget("rowColumn_10", xmRowColumnWidgetClass, form_6, args, 14);

    XtSetArg(args[0], XmNorientation, XmHORIZONTAL);
    XtSetArg(args[1], XmNleftAttachment, XmATTACH_FORM);
    XtSetArg(args[2], XmNrightAttachment, XmATTACH_NONE);
    XtSetArg(args[3], XmNtopAttachment, XmATTACH_WIDGET);
    XtSetArg(args[4], XmNbottomAttachment, XmATTACH_NONE);
    XtSetArg(args[5], XmNrightWidget, drawingArea_8);
    XtSetArg(args[6], XmNtopWidget, rowColumn_10);
    rowColumn_11 = XtCreateManagedWidget("time", xmRowColumnWidgetClass, form_6, args, 7);

    XtSetArg(args[6], XmNtopWidget, rowColumn_11);
    rowColumn_12 = XtCreateManagedWidget("latitude", xmRowColumnWidgetClass, form_6, args, 7);

    XtSetArg(args[6], XmNtopWidget, rowColumn_12);
    rowColumn_13 = XtCreateManagedWidget("longitude", xmRowColumnWidgetClass, form_6, args, 7);

    XtSetArg(args[6], XmNtopWidget, rowColumn_13);
    rowColumn_14 = XtCreateManagedWidget("altitude", xmRowColumnWidgetClass, form_6, args, 7);

    XtSetArg(args[6], XmNtopWidget, rowColumn_14);
    rowColumn_15 = XtCreateManagedWidget("speed", xmRowColumnWidgetClass, form_6, args, 7);

    XtSetArg(args[6], XmNtopWidget, rowColumn_15);
    rowColumn_16 = XtCreateManagedWidget("track", xmRowColumnWidgetClass, form_6, args, 7);

    XtSetArg(args[6], XmNtopWidget, rowColumn_16);
    rowColumn_17 = XtCreateManagedWidget("fix_status", xmRowColumnWidgetClass, form_6, args, 7);

    XtSetArg(args[6], XmNtopWidget, rowColumn_17);
    rowColumn_18 = XtCreateManagedWidget("quit", xmRowColumnWidgetClass, form_6, args, 7);


    n = 0;
    label_1 = XtCreateManagedWidget("label", xmLabelWidgetClass, rowColumn_11, args, n);
    label_2 = XtCreateManagedWidget("label", xmLabelWidgetClass, rowColumn_12, args, n);
    label_3 = XtCreateManagedWidget("label", xmLabelWidgetClass, rowColumn_13, args, n);
    label_4 = XtCreateManagedWidget("label", xmLabelWidgetClass, rowColumn_14, args, n);
    label_5 = XtCreateManagedWidget("label", xmLabelWidgetClass, rowColumn_15, args, n);
    label_6 = XtCreateManagedWidget("label", xmLabelWidgetClass, rowColumn_16, args, n);
    label_7 = XtCreateManagedWidget("label", xmLabelWidgetClass, rowColumn_17, args, n);

    n = 0;
    XtSetArg(args[n], XmNcursorPositionVisible, False);
    n++;
    XtSetArg(args[n], XmNeditable, False);
    n++;
    XtSetArg(args[n], XmNmarginHeight, 2);
    n++;
    XtSetArg(args[n], XmNhighlightThickness, 0);
    n++;
    XtSetArg(args[n], XmNshadowThickness, 1);
    n++;
    text_1 = XtCreateManagedWidget("text_1", xmTextFieldWidgetClass,
				   rowColumn_11, args, n);
    text_2 = XtCreateManagedWidget("text_2", xmTextFieldWidgetClass,
				   rowColumn_12, args, n);
    text_3 = XtCreateManagedWidget("text_3", xmTextFieldWidgetClass,
				   rowColumn_13, args, n);
    text_4 = XtCreateManagedWidget("text_4", xmTextFieldWidgetClass,
				   rowColumn_14, args, n);
    text_5 = XtCreateManagedWidget("text_5", xmTextFieldWidgetClass,
				   rowColumn_15, args, n);
    text_6 = XtCreateManagedWidget("text_6", xmTextFieldWidgetClass,
				   rowColumn_16, args, n);
    text_7 = XtCreateManagedWidget("text_7", xmTextFieldWidgetClass,
				   rowColumn_17, args, n);


    pushButton_11 = XtCreateManagedWidget("label",
			 xmPushButtonWidgetClass, rowColumn_18, args, 0);
    XtAddCallback(pushButton_11, XmNactivateCallback, quit_cb, NULL);

    status = XtVaCreateManagedWidget("status", xmTextFieldWidgetClass, form_6,
				     XmNcursorPositionVisible, False,
				     XmNeditable, False,
				     XmNmarginHeight, 1,
				     XmNhighlightThickness, 0,
				     XmNshadowThickness, 1,
				     XmNleftAttachment, XmATTACH_FORM,
				     XmNrightAttachment, XmATTACH_FORM,
				     XmNbottomAttachment, XmATTACH_FORM,
				     NULL);

}

/**************************************************
* Function: init_list
**************************************************/
void init_list()
{
    int i;
    XmString string;

    for (i = 1; i < 13; i++) {
	string = XmStringCreateSimple(" ");
	XmListAddItem(list_7, string, i);
#ifdef PROCESS_PRWIZCH
	XmListAddItem(list_8, string, i);
#endif /* PROCESS_PRWIZCH */
	XmStringFree(string);
    }
}

/**************************************************
* Function: handle_input
**************************************************/
static void handle_input(XtPointer client_data, int *source, XtInputId * id)
{
    gps_poll(&session);
}

/**************************************************
* Function: update_display
**************************************************/
void update_display(char *message)
{
    int i;
    XmString string[12];
    char s[128], *sp;

    while (isspace(*(sp = message + strlen(message) - 1)))
	*sp = '\0';
    XmTextFieldSetString(status, message);

    /* This is for the satellite status display */
    if (SEEN(session.gNMEAdata.satellite_stamp)) {
	for (i = 0; i < MAXCHANNELS; i++) {
	    if (i < session.gNMEAdata.satellites) {
		sprintf(s, "%2d %02d %03d %02d", session.gNMEAdata.PRN[i],
			session.gNMEAdata.elevation[i],
			session.gNMEAdata.azimuth[i], session.gNMEAdata.ss[i]);
	    } else
		sprintf(s, " ");
	    string[i] = XmStringCreateSimple(s);
	}
	XmListReplaceItemsPos(list_7, string, sizeof(string), 1);
	for (i = 0; i < MAXCHANNELS; i++)
	    XmStringFree(string[i]);
    }
#ifdef PROCESS_PRWIZCH
    if (SEEN(session.gNMEAdata.signal_quality_stamp)) {
	for (i = 0; i < MAXCHANNELS; i++) {
	    sprintf(s, "%2d %02x", session.gNMEAdata.Zs[i], session.gNMEAdata.Zv[i]);
	    string[i] = XmStringCreateSimple(s);
	}
	XmListReplaceItemsPos(list_8, string, sizeof(string), 1);
	for (i = 0; i < MAXCHANNELS; i++)
	    XmStringFree(string[i]);
    }
#endif /* PROCESS_PRWIZCH */
    /* here are the value fields */
    XmTextFieldSetString(text_1, session.gNMEAdata.utc);
    sprintf(s, "%f", session.gNMEAdata.latitude);
    XmTextFieldSetString(text_2, s);
    sprintf(s, "%f", session.gNMEAdata.longitude);
    XmTextFieldSetString(text_3, s);
    sprintf(s, "%f", session.gNMEAdata.altitude);
    XmTextFieldSetString(text_4, s);
    sprintf(s, "%f", session.gNMEAdata.speed);
    XmTextFieldSetString(text_5, s);
    sprintf(s, "%f", session.gNMEAdata.track);
    XmTextFieldSetString(text_6, s);

    switch (session.gNMEAdata.mode) {
    case 2:
	sprintf(s, "2D %sFIX", (session.gNMEAdata.status==2) ? "DIFF ": "");
	break;
    case 3:
	sprintf(s, "3D %sFIX", (session.gNMEAdata.status==2) ? "DIFF ": "");
	break;
    default:
	sprintf(s, "NO FIX");
	break;
    }
    XmTextFieldSetString(text_7, s);

    draw_graphics(&session.gNMEAdata);
}

/**************************************************
* Function: main
**************************************************/
int main(int argc, char *argv[])
{
    XtAppContext app;
    Arg args[100];
    int n;
    extern char *optarg;
    int option;
    double baud;
    char devtype = 'n';

    while ((option = getopt(argc, argv, "D:T:hp:s:")) != -1) {
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
	case 's':
	    baud = strtod(optarg, 0);
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
    if (!device_name)
	device_name = default_device_name;

    if (session.debug > 0) {
	fprintf(stderr, "command line options:\n");
	fprintf(stderr, "  debug level:        %d\n", session.debug);
	fprintf(stderr, "  gps device name:    %s\n", device_name);
	fprintf(stderr, "  gps device speed:   %d\n", device_speed);
    }
    lxbApp = XtVaAppInitialize(&app, "gps.ad", NULL, 0, &argc, argv, fallback_resources, NULL);

    n = 0;
    XtSetArg(args[n], XmNgeometry, "620x460");
    n++;
    XtSetArg(args[n], XmNresizePolicy, XmRESIZE_NONE);
    n++;
    XtSetArg(args[n], XmNallowShellResize, False);
    n++;
    XtSetArg(args[n], XmNdeleteResponse, XmDO_NOTHING);
    n++;
    XtSetArg(args[n], XmNmwmFunctions,
	     MWM_FUNC_RESIZE | MWM_FUNC_MOVE | MWM_FUNC_MINIMIZE | MWM_FUNC_MAXIMIZE);
    n++;
    XtSetValues(lxbApp, args, n);

    build_gui(lxbApp);

    XtRealizeWidget(lxbApp);


    delw = XmInternAtom(XtDisplay(lxbApp), "WM_DELETE_WINDOW", False);
    XmAddWMProtocolCallback(lxbApp, delw,
			    (XtCallbackProc) quit_cb, (XtPointer) NULL);

    gps_init(&session, GPS_TIMEOUT, devtype, NULL);
    session.gps_device = device_name;
    session.gNMEAdata.raw_hook = update_display;
    if (gps_activate(&session) == -1)
	exit(1);

    XtAppAddInput(app, session.fdin, (XtPointer) XtInputReadMask,
			     handle_input, NULL);

    init_list();

    XtAppMainLoop(app);

    gps_wrap(&session);
    return 0;
}
