
/* include files */
#include "config.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>

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
#ifdef HAVE_SYS_FILIO_H
#include <sys/filio.h>
#endif

#include "gps.h"

extern void register_canvas(Widget w, GC gc);
extern void draw_graphics(struct gps_data_t *gpsdata);
extern void redraw();

static Widget lxbApp, data_panel, satellite_list, satellite_diagram, status;
static Widget rowColumn_10, rowColumn_11, rowColumn_12, rowColumn_13;
static Widget rowColumn_14, rowColumn_15, rowColumn_16, rowColumn_17;
static Widget rowColumn_18, pushButton_11;
static Widget text_1, text_2, text_3, text_4, text_5, text_6, text_7;
static Widget label_1, label_2, label_3, label_4, label_5, label_6, label_7;

String fallback_resources[] =
{
    "*gpsdata.time.label.labelString: Time  ",
    "*gpsdata.latitude.label.labelString: Lat.  ",
    "*gpsdata.longitude.label.labelString: Long. ",
    "*gpsdata.altitude.label.labelString: Alt.  ",
    "*gpsdata.speed.label.labelString: Speed ",
    "*gpsdata.track.label.labelString: Track ",
    "*gpsdata.fix_status.label.labelString: Status",
    "*gpsdata.quit.label.labelString: Quit",
    NULL
};

static GC gc;
static Atom delw;

static void quit_cb()
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
    int n;
    Arg args[100];
    XGCValues gcv;

    /* the root application window */
    XtSetArg(args[0], XmNgeometry, "630x460");
    XtSetArg(args[1], XmNresizePolicy, XmRESIZE_NONE);
    XtSetArg(args[2], XmNallowShellResize, False);
    XtSetArg(args[3], XmNdeleteResponse, XmDO_NOTHING);
    XtSetArg(args[4], XmNmwmFunctions,
	     MWM_FUNC_RESIZE | MWM_FUNC_MOVE | MWM_FUNC_MINIMIZE | MWM_FUNC_MAXIMIZE);
    XtSetValues(lxbApp, args, 5);

#define LEFTSIDE_WIDTH	190
    /* the data panel */
    XtSetArg(args[0], XmNrubberPositioning, False);
    XtSetArg(args[1], XmNresizePolicy, XmRESIZE_NONE);
    XtSetArg(args[2], XmNwidth, LEFTSIDE_WIDTH);
    data_panel = XtCreateManagedWidget("gpsdata", xmFormWidgetClass, lxbApp, args, 3);

    /* satellite location and SNR data panel */
#define FRAMEHEIGHT	220
    XtSetArg(args[0], XmNbackground, get_pixel(lxbApp, "snow"));
    XtSetArg(args[1], XmNleftOffset, 10);
    XtSetArg(args[2], XmNtopOffset, 10);
    XtSetArg(args[3], XmNbottomAttachment, XmATTACH_NONE);
    XtSetArg(args[4], XmNleftAttachment, XmATTACH_FORM);
    XtSetArg(args[5], XmNtopAttachment, XmATTACH_FORM);
    XtSetArg(args[6], XmNheight, FRAMEHEIGHT);
    XtSetArg(args[7], XmNwidth, LEFTSIDE_WIDTH);
    XtSetArg(args[8], XmNlistSizePolicy, XmCONSTANT);
    XtSetArg(args[9], XmNhighlightThickness, 0);
    XtSetArg(args[10], XmNlistSpacing, 4);
    satellite_list = XtCreateManagedWidget("satellite_list", xmListWidgetClass, data_panel, args, 11);

    /* the satellite diagram */
    XtSetArg(args[0], XmNbottomAttachment, XmATTACH_NONE);
    XtSetArg(args[1], XmNleftOffset, 10);
    XtSetArg(args[2], XmNrightOffset, 10);
    XtSetArg(args[3], XmNbackground, get_pixel(lxbApp, "snow"));
    XtSetArg(args[4], XmNy, 10);
    XtSetArg(args[5], XmNx, 80);
    XtSetArg(args[6], XmNrightAttachment, XmATTACH_NONE);
    XtSetArg(args[7], XmNtopOffset, 10);
    XtSetArg(args[8], XmNrightAttachment, XmATTACH_FORM);
    XtSetArg(args[9], XmNtopAttachment, XmATTACH_FORM);
    XtSetArg(args[10], XmNresizePolicy, XmRESIZE_NONE);
    XtSetArg(args[11], XmNheight, 402);
    XtSetArg(args[12], XmNwidth, 402);
    satellite_diagram = XtCreateManagedWidget("satellite_diagram",
			     xmDrawingAreaWidgetClass, data_panel, args, 13);
    gcv.foreground = BlackPixelOfScreen(XtScreen(satellite_diagram));
    gc = XCreateGC(XtDisplay(satellite_diagram),
	RootWindowOfScreen(XtScreen(satellite_diagram)), GCForeground, &gcv);
    register_canvas(satellite_diagram, gc);
    XtAddCallback(satellite_diagram, XmNexposeCallback, redraw, NULL);

    /* the data display */
    XtSetArg(args[0], XmNtopOffset, 10);
    XtSetArg(args[1], XmNbottomOffset, 10);
    XtSetArg(args[2], XmNrightOffset, 10);
    XtSetArg(args[3], XmNleftOffset, 10);
    XtSetArg(args[4], XmNorientation, XmVERTICAL);
    XtSetArg(args[5], XmNrightAttachment, XmATTACH_WIDGET);
    XtSetArg(args[6], XmNrightWidget, satellite_diagram);
    XtSetArg(args[7], XmNbottomAttachment, XmATTACH_NONE);	/* XXX */
    XtSetArg(args[8], XmNleftAttachment, XmATTACH_FORM);
    XtSetArg(args[9], XmNtopAttachment, XmATTACH_WIDGET);
    XtSetArg(args[10], XmNtopWidget, satellite_list);
    XtSetArg(args[11], XmNheight, 12);
    rowColumn_10 = XtCreateManagedWidget("rowColumn_10", xmRowColumnWidgetClass, data_panel, args, 12);

    XtSetArg(args[0], XmNorientation, XmHORIZONTAL);
    XtSetArg(args[1], XmNleftAttachment, XmATTACH_FORM);
    XtSetArg(args[2], XmNrightAttachment, XmATTACH_NONE);
    XtSetArg(args[3], XmNtopAttachment, XmATTACH_WIDGET);
    XtSetArg(args[4], XmNbottomAttachment, XmATTACH_NONE);
    XtSetArg(args[5], XmNrightWidget, satellite_diagram);
    XtSetArg(args[6], XmNtopWidget, rowColumn_10);
    rowColumn_11 = XtCreateManagedWidget("time", xmRowColumnWidgetClass, data_panel, args, 7);

    XtSetArg(args[6], XmNtopWidget, rowColumn_11);
    rowColumn_12 = XtCreateManagedWidget("latitude", xmRowColumnWidgetClass, data_panel, args, 7);

    XtSetArg(args[6], XmNtopWidget, rowColumn_12);
    rowColumn_13 = XtCreateManagedWidget("longitude", xmRowColumnWidgetClass, data_panel, args, 7);

    XtSetArg(args[6], XmNtopWidget, rowColumn_13);
    rowColumn_14 = XtCreateManagedWidget("altitude", xmRowColumnWidgetClass, data_panel, args, 7);

    XtSetArg(args[6], XmNtopWidget, rowColumn_14);
    rowColumn_15 = XtCreateManagedWidget("speed", xmRowColumnWidgetClass, data_panel, args, 7);

    XtSetArg(args[6], XmNtopWidget, rowColumn_15);
    rowColumn_16 = XtCreateManagedWidget("track", xmRowColumnWidgetClass, data_panel, args, 7);

    XtSetArg(args[6], XmNtopWidget, rowColumn_16);
    rowColumn_17 = XtCreateManagedWidget("fix_status", xmRowColumnWidgetClass, data_panel, args, 7);

    XtSetArg(args[6], XmNtopWidget, rowColumn_17);
    rowColumn_18 = XtCreateManagedWidget("quit", xmRowColumnWidgetClass, data_panel, args, 7);


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
    XtSetArg(args[n], XmNcolumns, 23);
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

    status = XtVaCreateManagedWidget("status", xmTextFieldWidgetClass, data_panel,
				     XmNcursorPositionVisible, False,
				     XmNeditable, False,
				     XmNmarginHeight, 1,
				     XmNhighlightThickness, 0,
				     XmNshadowThickness, 1,
				     XmNleftAttachment, XmATTACH_FORM,
				     XmNrightAttachment, XmATTACH_FORM,
				     XmNbottomAttachment, XmATTACH_FORM,
				     NULL);

    XtRealizeWidget(lxbApp);

    delw = XmInternAtom(XtDisplay(lxbApp), "WM_DELETE_WINDOW", False);
    XmAddWMProtocolCallback(lxbApp, delw,
			    (XtCallbackProc)quit_cb, (XtPointer)NULL);
}

void init_list()
{
    int i;
    XmString string;

    for (i = 0; i < MAXCHANNELS; i++) {
	string = XmStringCreateSimple(" ");
	XmListAddItem(satellite_list, string, i+1);
	XmStringFree(string);
    }
}

/*
 * No dependencies on the session structure above this point.
 */

static struct gps_data_t *gpsdata;
static int timer;	/* time since last state change in seconds*/
static int state = 0;	/* or MODE_NO_FIX=1, MODE_2D=2, MODE_3D= 3 */

static void handle_input(XtPointer client_data, int *source, XtInputId * id)
{
    gps_poll(gpsdata);
}

static void update_panel(char *message)
/* gets done both on alarm ticks and on each sentence */
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
			gpsdata->elevation[i],
			gpsdata->azimuth[i], 
			gpsdata->ss[i],
			gpsdata->used[i] ? 'Y' : 'N'
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
    sprintf(s, "%f", gpsdata->latitude);
    XmTextFieldSetString(text_2, s);
    sprintf(s, "%f", gpsdata->longitude);
    XmTextFieldSetString(text_3, s);
    sprintf(s, "%f", gpsdata->altitude);
    XmTextFieldSetString(text_4, s);
    sprintf(s, "%f", gpsdata->speed);
    XmTextFieldSetString(text_5, s);
    sprintf(s, "%f", gpsdata->track);
    XmTextFieldSetString(text_6, s);

    if (!gpsdata->online)
    {
	newstate = 0;
	sprintf(s, "OFFLINE");
    }
    else
    {
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
    if (newstate != state)
    {
	timer = 0;
	state = newstate;
    }
    sprintf(s + strlen(s), " (%d secs)", timer);
    XmTextFieldSetString(text_7, s);

    draw_graphics(gpsdata);
}

static void update_display(char *message)
/* only gets done on sentence receipt, not alarm ticks */
{
    sigset_t	allsigs;

    /*
     * The Motif canvas widget seems to react badly to incoming alarm signals.
     * The symptom is that the satellite-display background will sometimes 
     * flash odd colors when SIGALRM comes in.  Prevent this.  We'll get the
     * alarm when the redraw is done.
     */
    sigfillset(&allsigs);
    sigprocmask(SIG_BLOCK, &allsigs, NULL);
    update_panel(message);
    draw_graphics(gpsdata);
    sigprocmask(SIG_UNBLOCK, &allsigs, NULL);
}

static void handle_alarm(int sig)
{
    timer++;
    update_display("");
    alarm(1);
}

int main(int argc, char *argv[])
{
    XtAppContext app;
    extern char *optarg;
    int option;
    char *colon, *server = NULL;
    char *port = DEFAULT_GPSD_PORT;

    while ((option = getopt(argc, argv, "hp:")) != -1) {
	switch (option) {
	case 'p':
	    server = strdup(optarg);
	    colon = strchr(server, ':');
	    if (colon != NULL)
	    {
		server[colon - server] = '\0';
		port = colon + 1;
	    }
	    break;
	case 'h':
	case '?':
	default:
	    fputs("usage:  gps [options] \n\
  options include: \n\
  -p string    = set server:port to query \n\
  -h           = help message \n\
", stderr);
	    exit(1);
	}
    }

    /*
     * Essentially all the interface to libgps happens below here
     */
    gpsdata = gps_open(server, port);
    if (!gpsdata)
    {
	fprintf(stderr, "gps: no gpsd running or network error (%d).\n", errno);
	exit(2);
    }

    lxbApp = XtVaAppInitialize(&app, "gps.ad", NULL, 0, &argc, argv, fallback_resources, NULL);

    build_gui(lxbApp);
    init_list();

    gps_set_raw_hook(gpsdata, update_display);
    gps_query(gpsdata, "w+x\n");

    XtAppAddInput(app, gpsdata->gps_fd, (XtPointer) XtInputReadMask,
			     handle_input, NULL);

    signal(SIGALRM, handle_alarm);
    alarm(1);
    XtAppMainLoop(app);

    gps_close(gpsdata);
    return 0;
}
