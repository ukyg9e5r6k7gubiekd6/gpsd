#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
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

#include "config.h"
#include "gps.h"
#include "display.h"

static Widget lxbApp, form, left, right, quitbutton;
static Widget satellite_list, satellite_diagram, status;
static Widget rowColumn_11, rowColumn_12, rowColumn_13, rowColumn_14;
static Widget rowColumn_15, rowColumn_16, rowColumn_17, rowColumn_18;
static Widget text_1, text_2, text_3, text_4, text_5, text_6, text_7;
static Widget label_1, label_2, label_3, label_4, label_5, label_6, label_7;
static GC gc;

static void quit_cb(void)
{
    exit(0);	/* closes the GPS along with other fds */
}

static Pixel get_pixel(Widget w, char *resource_value)
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

static void build_gui(Widget lxbApp)
{
    Arg args[100];
    XGCValues gcv;
    Atom delw;
    int i;
    XmString string;

    /* the root application window */
    XtSetArg(args[0], XmNgeometry, "620x470");
    XtSetArg(args[1], XmNresizePolicy, XmRESIZE_NONE);
    XtSetArg(args[2], XmNallowShellResize, False);
    XtSetArg(args[3], XmNdeleteResponse, XmDO_NOTHING);
    XtSetArg(args[4], XmNmwmFunctions,
	     MWM_FUNC_RESIZE | MWM_FUNC_MOVE | MWM_FUNC_MINIMIZE | MWM_FUNC_MAXIMIZE);
    XtSetValues(lxbApp, args, 5);

    /* a form to assist with geometry negotiation */
    form = XtVaCreateManagedWidget("form", xmFormWidgetClass, lxbApp, NULL);
    /* the left half of the screen */
    left = XtVaCreateManagedWidget("left", xmRowColumnWidgetClass, form,
				   XmNleftAttachment, XmATTACH_FORM,
				   XmNtopAttachment, XmATTACH_FORM,
				   NULL);
    /* the right half of the screen */
    right = XtVaCreateManagedWidget("right", xmRowColumnWidgetClass, form,
				    XmNleftAttachment, XmATTACH_WIDGET,
				    XmNleftWidget, left,
				    XmNtopAttachment, XmATTACH_FORM,
				    NULL);
    /* the application status bar */
    status = XtVaCreateManagedWidget("status", xmTextFieldWidgetClass, form,
				     XmNcursorPositionVisible, False,
				     XmNeditable, False,
				     XmNmarginHeight, 1,
				     XmNhighlightThickness, 0,
				     XmNshadowThickness, 1,
				     XmNleftAttachment, XmATTACH_FORM,
				     XmNrightAttachment, XmATTACH_FORM,
				     XmNtopAttachment, XmATTACH_WIDGET,
				     XmNtopWidget, left,
				     NULL);
    /* satellite location and SNR data panel */
#define FRAMEHEIGHT	220
#define LEFTSIDE_WIDTH	205
    satellite_list =
      XtVaCreateManagedWidget("satellite_list", xmListWidgetClass, left,
			      XmNbackground, get_pixel(lxbApp, "snow"),
			      XmNheight, FRAMEHEIGHT,
			      XmNwidth, LEFTSIDE_WIDTH,
			      XmNlistSizePolicy, XmCONSTANT,
			      XmNhighlightThickness, 0,
			      XmNlistSpacing, 4,
			      NULL);
    /* the satellite diagram */
#define SATDIAG_SIZE	400
    satellite_diagram = 
      XtVaCreateManagedWidget("satellite_diagram",
			      xmDrawingAreaWidgetClass, right, 
			      XmNbackground, get_pixel(lxbApp, "snow"),
			      XmNheight, SATDIAG_SIZE, XmNwidth, SATDIAG_SIZE,
			      NULL);
    gcv.foreground = BlackPixelOfScreen(XtScreen(satellite_diagram));
    gc = XCreateGC(XtDisplay(satellite_diagram),
	RootWindowOfScreen(XtScreen(satellite_diagram)), GCForeground, &gcv);
    register_canvas(satellite_diagram, gc);
    XtAddCallback(satellite_diagram, XmNexposeCallback, (XtPointer)redraw, NULL);
    /* the data display */
    XtSetArg(args[0], XmNorientation, XmHORIZONTAL);
    rowColumn_11 = XtCreateManagedWidget("time", xmRowColumnWidgetClass, left, args, 1);

    rowColumn_12 = XtCreateManagedWidget("latitude", xmRowColumnWidgetClass, left, args, 1);
    rowColumn_13 = XtCreateManagedWidget("longitude", xmRowColumnWidgetClass, left, args, 1);
    rowColumn_14 = XtCreateManagedWidget("altitude", xmRowColumnWidgetClass, left, args, 1);
    rowColumn_15 = XtCreateManagedWidget("speed", xmRowColumnWidgetClass, left, args, 1);
    rowColumn_16 = XtCreateManagedWidget("track", xmRowColumnWidgetClass, left, args, 1);
    rowColumn_17 = XtCreateManagedWidget("fix_status", xmRowColumnWidgetClass, left, args, 1);
    rowColumn_18 = XtCreateManagedWidget("quit", xmRowColumnWidgetClass, left, args, 1);

    label_1 = XtCreateManagedWidget("Time     ", xmLabelWidgetClass, rowColumn_11, args, 0);
    label_2 = XtCreateManagedWidget("Latitide ", xmLabelWidgetClass, rowColumn_12, args, 0);
    label_3 = XtCreateManagedWidget("Longitude", xmLabelWidgetClass, rowColumn_13, args, 0);
    label_4 = XtCreateManagedWidget("Altitude ", xmLabelWidgetClass, rowColumn_14, args, 0);
    label_5 = XtCreateManagedWidget("Speed    ", xmLabelWidgetClass, rowColumn_15, args, 0);
    label_6 = XtCreateManagedWidget("Course   ", xmLabelWidgetClass, rowColumn_16, args, 0);
    label_7 = XtCreateManagedWidget("Status   ", xmLabelWidgetClass, rowColumn_17, args, 0);

    XtSetArg(args[0], XmNcursorPositionVisible, False);
    XtSetArg(args[1], XmNeditable, False);
    XtSetArg(args[2], XmNmarginHeight, 2);
    XtSetArg(args[3], XmNhighlightThickness, 0);
    XtSetArg(args[4], XmNshadowThickness, 1);
    XtSetArg(args[5], XmNcolumns, 23);
    text_1 = XtCreateManagedWidget("text_1", xmTextFieldWidgetClass,
				   rowColumn_11, args, 6);
    text_2 = XtCreateManagedWidget("text_2", xmTextFieldWidgetClass,
				   rowColumn_12, args, 6);
    text_3 = XtCreateManagedWidget("text_3", xmTextFieldWidgetClass,
				   rowColumn_13, args, 6);
    text_4 = XtCreateManagedWidget("text_4", xmTextFieldWidgetClass,
				   rowColumn_14, args, 6);
    text_5 = XtCreateManagedWidget("text_5", xmTextFieldWidgetClass,
				   rowColumn_15, args, 6);
    text_6 = XtCreateManagedWidget("text_6", xmTextFieldWidgetClass,
				   rowColumn_16, args, 6);
    text_7 = XtCreateManagedWidget("text_7", xmTextFieldWidgetClass,
				   rowColumn_17, args, 6);

    quitbutton = XtCreateManagedWidget("Quit",
			 xmPushButtonWidgetClass, rowColumn_18, args, 0);
    XtAddCallback(quitbutton, XmNactivateCallback, (XtPointer)quit_cb, NULL);

    XtRealizeWidget(lxbApp);
    delw = XmInternAtom(XtDisplay(lxbApp), "WM_DELETE_WINDOW", False);
    XmAddWMProtocolCallback(lxbApp, delw,
			    (XtCallbackProc)quit_cb, (XtPointer)NULL);

    /* create empty list items to be replaced on update */
    string = XmStringCreateSimple(" ");
    for (i = 0; i < MAXCHANNELS; i++)
	XmListAddItem(satellite_list, string, i+1);
    XmStringFree(string);
}

static void handle_time_out(XtPointer client_data UNUSED,
			    XtIntervalId *ignored UNUSED)
/* runs when there is no data for a while */
{
    XmTextFieldSetString(status, "no data arriving");
    XmTextFieldSetString(text_7, "UNKNOWN");
}

/*
 * No dependencies on the session structure above this point.
 */

static struct gps_data_t *gpsdata;
static time_t timer;	/* time of last state change */
static int state = 0;	/* or MODE_NO_FIX=1, MODE_2D=2, MODE_3D=3 */
XtAppContext app;
XtIntervalId timeout;

static void handle_input(XtPointer client_data UNUSED, int *source UNUSED,
			 XtInputId *id UNUSED)
{
    gps_poll(gpsdata);
}

static void update_panel(char *message)
/* runs on each sentence */
{
    int i, newstate;
    XmString string[12];
    char s[128], *sp;

    if (message[0])
	while (isspace(*(sp = message + strlen(message) - 1)))
	    *sp = '\0';
    XmTextFieldSetString(status, message);
    string[0] = XmStringCreateSimple("PRN:  Elev:  Azim:  SNR:  Used:");
    /* This is for the satellite status display */
    if (SEEN(gpsdata->satellite_stamp)) {
	for (i = 0; i < MAXCHANNELS; i++) {
	    if (i < gpsdata->satellites) {
		sprintf(s, " %2d    %02d    %03d    %02d      %c", 
			gpsdata->PRN[i],
			gpsdata->elevation[i], gpsdata->azimuth[i], 
			gpsdata->ss[i],	gpsdata->used[i] ? 'Y' : 'N'
		    );
	    } else
		sprintf(s, "                  ");
	    string[i+1] = XmStringCreateSimple(s);
	}
	XmListReplaceItemsPos(satellite_list, string, sizeof(string), 1);
	for (i = 0; i < MAXCHANNELS; i++)
	    XmStringFree(string[i]);
    }
    /* here are the value fields */
    XmTextFieldSetString(text_1, gpsdata->utc);
    sprintf(s, "%f %c", fabsf(gpsdata->latitude), (gpsdata->latitude < 0) ? 'S' : 'N');
    XmTextFieldSetString(text_2, s);
    sprintf(s, "%f %c", fabsf(gpsdata->longitude), (gpsdata->longitude < 0) ? 'W' : 'E');
    XmTextFieldSetString(text_3, s);
    sprintf(s, "%f meters", gpsdata->altitude);
    XmTextFieldSetString(text_4, s);
    sprintf(s, "%f knots", gpsdata->speed);
    XmTextFieldSetString(text_5, s);
    sprintf(s, "%f degrees", gpsdata->track);
    XmTextFieldSetString(text_6, s);

    if (!gpsdata->online) {
	newstate = 0;
	sprintf(s, "OFFLINE");
    } else {
	newstate = gpsdata->mode;
	switch (gpsdata->mode) {
	case 2:
	    sprintf(s, "2D %sFIX",(gpsdata->status==STATUS_DGPS_FIX)?"DIFF ":"");
	    break;
	case 3:
	    sprintf(s, "3D %sFIX",(gpsdata->status==STATUS_DGPS_FIX)?"DIFF ":"");
	    break;
	default:
	    sprintf(s, "NO FIX");
	    break;
	}
    }
    if (newstate != state) {
	timer = time(NULL);
	state = newstate;
    }
    sprintf(s + strlen(s), " (%d secs)", (int) (time(NULL) - timer));
    XmTextFieldSetString(text_7, s);
    draw_graphics(gpsdata);

    XtRemoveTimeOut(timeout);
    timeout = XtAppAddTimeOut(app, 2000, handle_time_out, NULL);
}

int main(int argc, char *argv[])
{
    int option;
    char *colon, *server = NULL, *port = DEFAULT_GPSD_PORT;

    while ((option = getopt(argc, argv, "?hv")) != -1) {
	switch (option) {
	case 'v':
	    printf("gps %s\n", VERSION);
	    exit(0);
	case 'h': case '?': default:
	    fputs("usage:  gps [-?hv] [server[:port]]\n", stderr);
	    exit(1);
	}
    }
    if (optind < argc) {
	server = strdup(argv[optind]);
	colon = strchr(server, ':');
	if (colon != NULL) {
	    server[colon - server] = '\0';
	    port = colon + 1;
	}
    }

    gpsdata = gps_open(server, port);
    if (!gpsdata) {
	fprintf(stderr,"gps: no gpsd running or network error (%d).\n", errno);
	exit(2);
    }

    lxbApp = XtVaAppInitialize(&app, "gps.ad", NULL, 0, &argc,argv, NULL,NULL);
    build_gui(lxbApp);

    timeout = XtAppAddTimeOut(app, 2000, handle_time_out, app);

    gps_set_raw_hook(gpsdata, update_panel);
    gps_query(gpsdata, "w+x\n");

    XtAppAddInput(app, gpsdata->gps_fd, (XtPointer) XtInputReadMask,
			     handle_input, NULL);
    XtAppMainLoop(app);

    gps_close(gpsdata);
    return 0;
}
