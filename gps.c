
/* include files */
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

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

#include "nmea.h"
#include "gps.h"

void update_display(char *message);

/* global variables */
Widget lxbApp;
Widget form_6;
Widget list_7;
Widget list_8;
Widget drawingArea_8;
Widget rowColumn_10;
Widget rowColumn_11;
Widget rowColumn_12;
Widget rowColumn_13;
Widget rowColumn_14;
Widget rowColumn_15;
Widget rowColumn_16;
Widget pushButton_11;
Widget text_1, text_2, text_3, text_4, text_5;
Widget label_1, label_2, label_3, label_4, label_5;
Widget status;

				/* command line options */
int debug = 0;
int device_type;
int device_speed = B4800;
char *device_name = 0;
char *latitude = 0;
char *longitude = 0;
char latd = 'N';
char lond = 'W';
				/* command line option defaults */
char *default_device_name = "localhost:2947";
char *default_latitude = "3600.000";
char *default_longitude = "12300.000";

String fallback_resources[] =
{
    "*gps_data.time.label.labelString: Time",
    "*gps_data.latitude.label.labelString: Lat.",
    "*gps_data.longitude.label.labelString: Lon.",
    "*gps_data.altitude.label.labelString: Alt.",
    "*gps_data.fix_status.label.labelString: Stat",
    "*gps_data.quit.label.labelString: Quit",
};


GC gc;
static Atom delw;

#define BUFSIZE 4096

extern void redraw();

void quit_cb()
{
    serial_close();
    exit(0);
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

    XtSetArg(args[0], XmNbackground, get_pixel(lxbApp, "snow"));
    XtSetArg(args[1], XmNleftOffset, 10);
    XtSetArg(args[2], XmNtopOffset, 10);
    XtSetArg(args[3], XmNbottomAttachment, XmATTACH_NONE);
    XtSetArg(args[4], XmNleftAttachment, XmATTACH_FORM);
    XtSetArg(args[5], XmNtopAttachment, XmATTACH_FORM);
    XtSetArg(args[6], XmNheight, 204);
    XtSetArg(args[7], XmNwidth, 100);
    XtSetArg(args[8], XmNlistSizePolicy, XmCONSTANT);
    XtSetArg(args[9], XmNhighlightThickness, 0);
    XtSetArg(args[10], XmNlistSpacing, 4);
    list_7 = XtCreateManagedWidget("list_7", xmListWidgetClass, form_6, args, 11);

    XtSetArg(args[0], XmNbackground, get_pixel(lxbApp, "snow"));
    XtSetArg(args[1], XmNleftOffset, 10);
    XtSetArg(args[2], XmNtopOffset, 10);
    XtSetArg(args[3], XmNbottomAttachment, XmATTACH_NONE);
    XtSetArg(args[4], XmNleftAttachment, XmATTACH_WIDGET);
    XtSetArg(args[5], XmNtopAttachment, XmATTACH_FORM);
    XtSetArg(args[6], XmNheight, 204);
    XtSetArg(args[7], XmNwidth, 80);
    XtSetArg(args[8], XmNlistSizePolicy, XmCONSTANT);
    XtSetArg(args[9], XmNhighlightThickness, 0);
    XtSetArg(args[10], XmNlistSpacing, 4);
    XtSetArg(args[11], XmNleftWidget, list_7);
    list_8 = XtCreateManagedWidget("list_8", xmListWidgetClass, form_6, args, 12);

    XtSetArg(args[0], XmNbottomAttachment, XmATTACH_NONE);
    XtSetArg(args[1], XmNleftOffset, 10);
    XtSetArg(args[2], XmNrightOffset, 10);
    XtSetArg(args[3], XmNbackground, get_pixel(lxbApp, "snow"));
    XtSetArg(args[4], XmNy, 10);
    XtSetArg(args[5], XmNx, 80);
    XtSetArg(args[6], XmNrightAttachment, XmATTACH_NONE);
    XtSetArg(args[7], XmNleftWidget, list_8);
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
    XtSetArg(args[13], XmNheight, 30);
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
    rowColumn_15 = XtCreateManagedWidget("fix_status", xmRowColumnWidgetClass, form_6, args, 7);

    XtSetArg(args[6], XmNtopWidget, rowColumn_15);
    rowColumn_16 = XtCreateManagedWidget("quit", xmRowColumnWidgetClass, form_6, args, 7);


    n = 0;
    label_1 = XtCreateManagedWidget("label", xmLabelWidgetClass, rowColumn_11, args, n);
    label_2 = XtCreateManagedWidget("label", xmLabelWidgetClass, rowColumn_12, args, n);
    label_3 = XtCreateManagedWidget("label", xmLabelWidgetClass, rowColumn_13, args, n);
    label_4 = XtCreateManagedWidget("label", xmLabelWidgetClass, rowColumn_14, args, n);
    label_5 = XtCreateManagedWidget("label", xmLabelWidgetClass, rowColumn_15, args, n);

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


    pushButton_11 = XtCreateManagedWidget("label",
			 xmPushButtonWidgetClass, rowColumn_16, args, 0);
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
* Function: handle_input
**************************************************/
static void handle_input(XtPointer client_data, int *source, XtInputId * id)
{
    static unsigned char buf[BUFSIZE];	/* that is more than a sentence */
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
	    update_display(buf);
	    offset = 0;
	    return;
	}
	offset++;
    }
}

/**************************************************
* Function: update_display
**************************************************/
void update_display(char *message)
{
    int i;
    XmString string[12];
    char s[128];

    XmTextFieldSetString(status, message);

    /* This is for the satellite status display */
    if (gNMEAdata.cmask & C_SAT) {
	for (i = 0; i < 12; i++) {
	    if (i < gNMEAdata.in_view) {
		sprintf(s, "%2d %02d %03d %02d", gNMEAdata.PRN[i],
			gNMEAdata.elevation[i],
			gNMEAdata.azimuth[i], gNMEAdata.ss[i]);
	    } else
		sprintf(s, " ");
	    string[i] = XmStringCreateSimple(s);
	}
	XmListReplaceItemsPos(list_7, string, 12, 1);
	for (i = 0; i < 12; i++)
	    XmStringFree(string[i]);
    }
    if (gNMEAdata.cmask & C_ZCH) {
	for (i = 0; i < 12; i++) {
	    sprintf(s, "%2d %02x", gNMEAdata.Zs[i], gNMEAdata.Zv[i]);
	    string[i] = XmStringCreateSimple(s);
	}
	XmListReplaceItemsPos(list_8, string, 12, 1);
	for (i = 0; i < 12; i++)
	    XmStringFree(string[i]);
    }
    /* here now the value fields */

    XmTextFieldSetString(text_1, gNMEAdata.utc);
    sprintf(s, "%f", gNMEAdata.latitude);
    XmTextFieldSetString(text_2, s);
    sprintf(s, "%f", gNMEAdata.longitude);
    XmTextFieldSetString(text_3, s);
    sprintf(s, "%f", gNMEAdata.altitude);
    XmTextFieldSetString(text_4, s);

    switch (gNMEAdata.mode) {
    case 2:
	sprintf(s, "2D %sFIX", (gNMEAdata.status==2) ? "DIFF ": "");
	break;
    case 3:
	sprintf(s, "3D %sFIX", (gNMEAdata.status==2) ? "DIFF ": "");
	break;
    default:
	sprintf(s, "NO FIX");
	break;
    }
    XmTextFieldSetString(text_5, s);

    draw_graphics();
    gNMEAdata.cmask = 0;
}

/**************************************************
* Function: open_input
**************************************************/
static void open_input(XtAppContext app)
{
    int input = 0;
    XtInputId input_id;

    input = serial_open();

    gNMEAdata.fdin = input;
    gNMEAdata.fdout = input;

    input_id = XtAppAddInput(app, input, (XtPointer) XtInputReadMask,
			     handle_input, NULL);
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
	XmListAddItem(list_8, string, i);
	XmStringFree(string);
    }
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

    while ((option = getopt(argc, argv, "D:T:hp:s:")) != -1) {
	switch (option) {
        case 'T':
            switch (*optarg) {
                case 't':
                    device_type = DEVICE_TRIPMATE;
                    break;
                case 'e':
                    device_type = DEVICE_EARTHMATE;
                    break;
                default:
                    fprintf(stderr,"Invalide device type \"%s\"\n"
                                   "Using GENERIC instead\n", device_type);
                    break;
            }
            break;
	case 'D':
	    debug = (int) strtol(optarg, 0, 0);
	    break;
	case 'p':
	    if (device_name)
		free(device_name);
	    device_name = malloc(strlen(optarg) + 1);
	    strcpy(device_name, optarg);
	    break;
	case 's':
	    baud = strtod(optarg, 0);
	    if (baud < 200)
		baud *= 1000;
	    if (baud < 2400)
		device_speed = B1200;
	    else if (baud < 4800)
		device_speed = B2400;
	    else if (baud < 9600)
		device_speed = B4800;
	    else if (baud < 19200)
		device_speed = B9600;
	    else if (baud < 38400)
		device_speed = B19200;
	    else
		device_speed = B38400;
	    break;
	case 'h':
	case '?':
	default:
	    fputs("usage:  gps [options] \n\
  options include: \n\
  -D integer   [ set debug level ] \n\
  -h           [ help message ] \n\
  -p string    [ set gps device name ] \n\
  -s baud_rate [ set baud rate on gps device ] \n\
", stderr);
	    exit(1);
	}
    }
    if (!device_name)
	device_name = default_device_name;
    if (!latitude)
	latitude = default_latitude;
    if (!longitude)
	longitude = default_longitude;

    if (debug > 0) {
	fprintf(stderr, "command line options:\n");
	fprintf(stderr, "  debug level:        %d\n", debug);
	fprintf(stderr, "  gps device name:    %s\n", device_name);
	fprintf(stderr, "  gps device speed:   %d\n", device_speed);
    }
    lxbApp = XtVaAppInitialize(&app, "Gps", NULL, 0, &argc, argv, fallback_resources, NULL);

    n = 0;
    XtSetArg(args[n], XmNgeometry, "620x434");
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

    open_input(app);

    init_list();

    XtAppMainLoop(app);

    return 0;
}


int errexit(char *s)
{
    perror(s);
    serial_close();
    exit(1);
}
