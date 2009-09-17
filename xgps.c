/* $Id$ */
/* $gpsd: xgps.c 3871 2006-11-13 00:40:00Z esr $ */

/*
 * Copyright (c) 2007 Marc Balmer <marc@msys.ch>
 * Copyright (c) 2006 Eric S. Raymond
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

#include <sys/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#ifndef S_SPLINT_S
#include <unistd.h>
#endif /* S_SPLINT_S */
#include <math.h>
#include <errno.h>
#include <stdbool.h>

#include <Xm/Xm.h>
#include <Xm/MwmUtil.h>
#include <Xm/PushB.h>
#include <Xm/Form.h>
#include <Xm/RowColumn.h>
#include <Xm/Label.h>
#include <Xm/TextF.h>
#include <Xm/List.h>
#include <Xm/DrawingA.h>
#include <Xm/Protocols.h>
#include <Xm/MainW.h>
#include <Xm/Frame.h>
#include <Xm/LabelG.h>
#include <Xm/ScrolledW.h>
#include <Xm/MessageB.h>
#include <Xm/Text.h>
#include <X11/Shell.h>

#include "gpsd_config.h"
#include "gps.h"
#include "gpsdclient.h"

/*
 * FIXME: use here is a minor bug, should report epx and epy separately.
 * How to mix together epx and epy to get a horizontal circular error.
 */
#define EMIX(x, y)	(((x) > (y)) ? (x) : (y))

/* This code used to live in display.c */

#define RM		20
#define IDIAM		5	/* satellite icon diameter */

#undef min
#define min(a, b) ((a) < (b) ? (a) : (b))

static Widget draww, appshell;
static GC drawGC;
static Dimension width, height, diameter;
static Pixmap pixmap;

/*@ -usedef -compdef -mustfreefresh @*/
static void
set_color(String color)
{
	Display *dpy = XtDisplay(draww);
	Colormap cmap = DefaultColormapOfScreen(XtScreen(draww));
	XColor col, unused;

	if (XAllocNamedColor(dpy, cmap, color, &col, &unused)==0) {
		char buf[32];

		(void)snprintf(buf, sizeof(buf), "Can't alloc %s", color);
		XtWarning(buf);
		return;
	}
	(void)XSetForeground(dpy, drawGC, col.pixel);
}
/*@ +usedef @*/

static void
register_shell(Widget w)
{
	appshell = w;
}

static void
register_canvas(Widget w, GC gc)
{
	Display *dpy = XtDisplay(w);
	draww = w;
	drawGC = gc;

	XtVaGetValues(w, XmNwidth, &width, XmNheight, &height, NULL);

	if (pixmap)
	    (void)XFreePixmap(dpy, pixmap);
	pixmap = XCreatePixmap(dpy, RootWindowOfScreen(XtScreen(w)),
	    width, height, (unsigned int)DefaultDepthOfScreen(XtScreen(w)));
	set_color("White");
	(void)XFillRectangle(XtDisplay(draww), pixmap, drawGC, 0,0, width, height);
	diameter = min(width, height) - RM;
}

static void
set_title(char *title)
{
	/*@ -usedef @*/
	XTextProperty windowProp;
	if (XStringListToTextProperty(&title, 1, &windowProp )!=0)
	{
		/* Not working. */
	    	/* Do we need to traverse up to the root window somehow? */
		XSetWMName(XtDisplay(appshell), XtWindow(appshell), &windowProp);
		(void)XFree(windowProp.value);
	}
	/*@ +usedef @*/
}

static void
pol2cart(double azimuth, double elevation,
	 /*@out@*/int *xout, /*@out@*/int *yout)
{
	azimuth *= DEG_2_RAD;
#ifdef PCORRECT
	elevation = sin((90.0 - elevation) * DEG_2_RAD);
#else
	elevation = ((90.0 - elevation) / 90.0);
#endif
	*xout = (int)((width / 2) + sin(azimuth) * elevation * (diameter / 2));
	*yout = (int)((height / 2) - cos(azimuth) * elevation * (diameter / 2));
}

static void
draw_arc(int x, int y, unsigned int diam)
{
    (void)XDrawArc(XtDisplay(draww), pixmap, drawGC,
		   x - diam / 2, y - diam / 2,        /* x,y */
		   diam, diam,        /* width, height */
		   0, 360 * 64);      /* angle1, angle2 */
}
/*@ +compdef @*/

static void
draw_graphics(struct gps_data_t *gpsdata)
{
	Display *dpy = XtDisplay(draww);
	int i, x, y;
	char buf[20];

	if (gpsdata->satellites != 0) {
		i = (int)min(width, height);

		set_color("White");
		(void)XFillRectangle(dpy, pixmap, drawGC, 0, 0, width, height);

		/* draw something in the center */
		set_color("Grey");
		draw_arc((int)(width / 2), (int)(height / 2), 6);

		/* draw the 45 degree circle */
#ifdef PCORRECT
#define FF	0.7 /* sin(45) ~ 0.7 */
#else
#define FF	0.5
#endif
		draw_arc((int)(width / 2), (int)(height / 2), (unsigned)((i - RM) * FF));
#undef FF

		set_color("Black");
		draw_arc((int)(width / 2), (int)(height / 2), (unsigned)(i - RM));

		pol2cart(0, 0, &x, &y);
		set_color("Black");
		(void)XDrawString(dpy, pixmap, drawGC, x, y, "N", 1);
		pol2cart(90, 0, &x, &y);
		set_color("Black");
		(void)XDrawString(dpy, pixmap, drawGC, x + 2, y, "E", 1);
		pol2cart(180, 0, &x, &y);
		set_color("Black");
		(void)XDrawString(dpy, pixmap, drawGC, x, y + 10, "S", 1);
		pol2cart(270, 0, &x, &y);
		set_color("Black");
		(void)XDrawString(dpy, pixmap, drawGC, x - 5, y, "W", 1);

		/* Now draw the satellites... */
		for (i = 0; i < gpsdata->satellites; i++) {
			pol2cart((double)gpsdata->azimuth[i],
			    (double)gpsdata->elevation[i], &x, &y);
			if (gpsdata->ss[i] < 10)
				set_color("Black");
			else if (gpsdata->ss[i] < 30)
				set_color("Red");
			else if (gpsdata->ss[i] < 35)
				set_color("Yellow");
			else if (gpsdata->ss[i] < 40)
				set_color("Green3");
			else
				set_color("Green1");
			if (gpsdata->PRN[i] > GPS_PRNMAX) {
				/* SBAS satellites */
				XPoint vertices[5];
				/*@ -type @*/

				vertices[0].x = x;
				vertices[0].y = y-IDIAM;
				vertices[1].x = x+IDIAM;
				vertices[1].y = y;
				vertices[2].x = x;
				vertices[2].y = y+IDIAM;
				vertices[3].x = x-IDIAM;
				vertices[3].y = y;
				vertices[4].x = x;
				vertices[4].y = y-IDIAM;
				/*@ +type -compdef @*/

				if (gpsdata->used[i])
				    (void)XFillPolygon(dpy, pixmap, drawGC,
					    vertices, 5, Convex,
					    CoordModeOrigin);
				else
				    (void)XDrawLines(dpy, pixmap, drawGC,
					    vertices, 5, CoordModeOrigin);
			} else {
				/* ordinary GPS satellites */
				if (gpsdata->used[i])
				    (void)XFillArc(dpy, pixmap, drawGC,
					    x - IDIAM,
					    y - IDIAM, 2 * IDIAM + 1,
					    2 * IDIAM + 1, 0, 360 * 64);
				else
				    (void)XDrawArc(dpy, pixmap, drawGC,
					    x - IDIAM,
					    y - IDIAM, 2 * IDIAM + 1,
					    2 * IDIAM + 1, 0, 360 * 64);
			}
			(void)snprintf(buf, sizeof(buf),
				       "%-3d", gpsdata->PRN[i]);
			set_color("Black");
			(void)XDrawString(dpy, pixmap, drawGC, x, y+17, buf, 3);

		}
		(void)XCopyArea(dpy, pixmap, XtWindow(draww), drawGC,
		    0, 0, width, height, 0, 0);
	}
}

static void
redraw(Widget widget UNUSED, XtPointer client_data UNUSED, XtPointer call_data)
{
	XmDrawingAreaCallbackStruct *cbs =
	    (XmDrawingAreaCallbackStruct *)call_data;
	XEvent *event = cbs->event;
	Display *dpy = event->xany.display;

	(void)XCopyArea(dpy, pixmap, XtWindow(draww), drawGC,
			cbs->event->xexpose.x, cbs->event->xexpose.y,
			(unsigned int)cbs->event->xexpose.width,
			(unsigned int)cbs->event->xexpose.height,
			cbs->event->xexpose.x, cbs->event->xexpose.y);
}

/*@ -usedef @*/
static void
resize(Widget widget, XtPointer client_data UNUSED, XtPointer call_data UNUSED)
{
	GC gc;
	XtVaGetValues(widget,
	    XmNuserData, 	&gc,
	    NULL);
	register_canvas(widget, gc);
}
/*@ +usedef +mustfreefresh @*/

/* From here down is the original xgps code */

/* Widget and window sizes. */
#define MAX_FONTSIZE	18		/* maximum fontsize we handle*/

/* height of satellite-data display */
#define SATDATA_HEIGHT	MAX_FONTSIZE*(MAXCHANNELS+1)
#define LEFTSIDE_WIDTH	205		/* width of data-display side */
#define SATDIAG_SIZE	400		/* size of satellite diagram */

static Widget toplevel, form, left, right;
static Widget satellite_list, satellite_diagram;
static Widget status_form, status_frame, status;
static Widget text_1, text_2, text_3, text_4, text_5;
static Widget text_6, text_7, text_8, text_9, text_10;
static GC gc;

static struct gps_data_t *gpsdata;
static time_t timer;	/* time of last state change */
static int state = 0;	/* or MODE_NO_FIX=1, MODE_2D=2, MODE_3D=3 */
static XtAppContext app;
static XtIntervalId timeout, gps_timeout;
static XtInputId gps_input;
static enum deg_str_type deg_type = deg_dd;
static struct fixsource_t source;

bool gps_lost;

/*@ -nullassign @*/
static XrmOptionDescRec options[] = {
	{ "-altunits",  "*altunits",	XrmoptionSepArg,	NULL },
	{ "-speedunits","*speedunits",	XrmoptionSepArg,	NULL },
};
String fallback_resources[] = { NULL} ;
/*@ +nullassign @*/

struct unit_t {
	char *legend;
	double factor;
};
static struct unit_t speedtable[] = {
	{ "knots",	MPS_TO_KNOTS },
	{ "mph",	MPS_TO_MPH },
	{ "kmh",	MPS_TO_KPH },
}, *speedunits = speedtable;
static struct unit_t alttable[] = {
	{ "feet",	METERS_TO_FEET },
	{ "meters",	1},
}, *altunits = alttable;

void dlg_callback(Widget dialog, XtPointer client_data, XtPointer call_data);
void help_cb(Widget widget, XtPointer client_data, XtPointer call_data);
void file_cb(Widget widget, XtPointer client_data, XtPointer call_data);
Widget err_dialog(Widget widget, char *s);
void handle_gps(XtPointer client_data, XtIntervalId *ignored);

static void
quit_cb(void)
{
    exit(0);
}

/*@ -mustfreefresh -compdef +ignoresigns @*/
static Pixel
get_pixel(Widget w, char *resource_value)
{
	Colormap colormap;
	Boolean cstatus;
	XColor exact, color;

	colormap = DefaultColormapOfScreen(
	    DefaultScreenOfDisplay(XtDisplay(w)));
	/*@i@*/cstatus = XAllocNamedColor(XtDisplay(w), colormap, resource_value,
	    &color, &exact);
	if (cstatus == (Boolean)False) {
		(void)fprintf(stderr, "Unknown color: %s", resource_value);
		color.pixel = BlackPixelOfScreen(
		    DefaultScreenOfDisplay(XtDisplay(w)));
	};
	/*@i1@*/return (color.pixel);
}

static void
build_gui(Widget toplevel)
{
	Widget main_w, menubar, widget, sat_frame, sky_frame, gps_frame;
	Widget gps_form, gps_data, sw;

	Arg args[100];
	XGCValues gcv;
	Atom delw;
	int i;
	XmString string;
	XmString file, help, about, quit;

	/*@ -immediatetrans -usedef @*/
	/* the root application window */
	XtSetArg(args[0], XmNwidth, LEFTSIDE_WIDTH + SATDIAG_SIZE + 26);
	XtSetArg(args[1], XmNheight, SATDATA_HEIGHT + 14 * MAX_FONTSIZE + 12);
	/*@ +immediatetrans +usedef @*/
	XtSetValues(toplevel, args, 2);

	/*@ -onlytrans @*/
	main_w = XtVaCreateManagedWidget("main_window",
	    xmMainWindowWidgetClass,	toplevel,
	    NULL);

	/* Construct the menubar */
	file = XmStringCreateLocalized("File");
	help = XmStringCreateLocalized("Help");
	menubar = XmVaCreateSimpleMenuBar(main_w, "menubar",
	    XmVaCASCADEBUTTON,	file,	'F',
	    XmVaCASCADEBUTTON,	help,	'H',
	    NULL);
	XmStringFree(file);

	if ((widget = XtNameToWidget(menubar, "button_1")))
		XtVaSetValues(menubar, XmNmenuHelpWidget, widget, NULL);

	quit = XmStringCreateLocalized("Quit");
	(void)XmVaCreateSimplePulldownMenu(menubar, "file_menu", 0, file_cb,
	    XmVaPUSHBUTTON, quit, 'Q', NULL, NULL,
	    NULL);
	XmStringFree(quit);

	about = XmStringCreateLocalized("About");
	(void)XmVaCreateSimplePulldownMenu(menubar, "help_menu", 1, help_cb,
	    XmVaPUSHBUTTON, help,  'H', NULL, NULL,
	    XmVaSEPARATOR,
	    XmVaPUSHBUTTON, about, 'A', NULL, NULL,
	    NULL);
	XmStringFree(help);
	XmStringFree(about);

	XtManageChild(menubar);


	/* a form to assist with geometry negotiation */
	form = XtVaCreateManagedWidget("form",
	    xmFormWidgetClass,		main_w,
	    XmNfractionBase,		3,
	    NULL);

	/* satellite frame */
	sat_frame = XtVaCreateWidget("satellite_frame",
	    xmFrameWidgetClass,		form,
	    XmNshadowType,		XmSHADOW_ETCHED_IN,
	    XmNtopAttachment,		XmATTACH_FORM,
	    XmNrightAttachment,		XmATTACH_POSITION,
	    XmNrightPosition,		1,
	    XmNbottomAttachment,	XmATTACH_POSITION,
	    XmNbottomPosition,		2,
	    XmNleftAttachment,		XmATTACH_FORM,
	    NULL);
	(void)XtVaCreateManagedWidget("Satellite List",
	    xmLabelGadgetClass,		sat_frame,
	    XmNchildType,		XmFRAME_TITLE_CHILD,
	    XmNchildVerticalAlignment,	XmALIGNMENT_CENTER,
	    NULL);

	/* the left half of the screen */
	left = XtVaCreateManagedWidget("left",
	    xmFormWidgetClass, 	sat_frame,
	    NULL);

	/* skyview frame */
	sky_frame = XtVaCreateWidget("skyview_frame",
	    xmFrameWidgetClass,		form,
	    XmNshadowType,		XmSHADOW_ETCHED_IN,
	    XmNtopAttachment,		XmATTACH_FORM,
	    XmNrightAttachment,		XmATTACH_FORM,
	    XmNbottomAttachment,	XmATTACH_POSITION,
	    XmNbottomPosition,		2,
	    XmNleftAttachment,		XmATTACH_POSITION,
	    XmNleftPosition,		1,
	    NULL);
	(void)XtVaCreateManagedWidget("Skyview",
	    xmLabelGadgetClass,		sky_frame,
	    XmNchildType,		XmFRAME_TITLE_CHILD,
	    XmNchildVerticalAlignment,	XmALIGNMENT_CENTER,
	    NULL);

	/* the right half of the screen */
	right = XtVaCreateManagedWidget("right",
	    xmFormWidgetClass, 		sky_frame,
	    NULL);

	/* the application status bar */
	status_form = XtVaCreateManagedWidget("status_form",
	    xmFormWidgetClass,		form,
	    XmNtopAttachment,		XmATTACH_POSITION,
	    XmNtopPosition,		2,
	    XmNleftAttachment,		XmATTACH_FORM,
	    XmNrightAttachment,		XmATTACH_FORM,
	    XmNtopAttachment,           XmATTACH_WIDGET,
	    XmNtopWidget,               left,
	    XmNfractionBase,		3,
	    NULL);
	status_frame = XtVaCreateWidget("status_frame",
	    xmFrameWidgetClass,		status_form,
	    XmNshadowType,		XmSHADOW_ETCHED_IN,
	    XmNtopAttachment,		XmATTACH_FORM,
	    XmNleftAttachment,		XmATTACH_FORM,
	    XmNrightAttachment,		XmATTACH_FORM,
	    XmNbottomAttachment,	XmATTACH_FORM,
	    NULL);
	(void)XtVaCreateManagedWidget("Message Data",
	    xmLabelGadgetClass,		status_frame,
	    XmNchildType,		XmFRAME_TITLE_CHILD,
	    XmNchildVerticalAlignment,	XmALIGNMENT_CENTER,
	    NULL);
	status = XtVaCreateManagedWidget("status",
					 xmTextFieldWidgetClass, status_form,
					 XmNcursorPositionVisible, False,
					 XmNeditable, False,
					 XmNmarginHeight, 1,
					 XmNhighlightThickness, 0,
					 XmNshadowThickness, 2,
					 XmNleftAttachment, XmATTACH_FORM,
					 XmNrightAttachment, XmATTACH_FORM,
					 XmNtopAttachment, XmATTACH_FORM,
					 XmNbottomAttachment, XmATTACH_FORM,
					 NULL);

	/* gps information frame */
	gps_form = XtVaCreateManagedWidget("gps_form",
	    xmFormWidgetClass,		form,
	    XmNtopAttachment,		XmATTACH_POSITION,
	    XmNtopPosition,		2,
	    XmNleftAttachment,		XmATTACH_FORM,
	    XmNrightAttachment,		XmATTACH_FORM,
	    XmNbottomAttachment,	XmATTACH_FORM,
	    XmNtopAttachment,           XmATTACH_WIDGET,
	    XmNtopWidget,               status_form,
	    XmNfractionBase,		3,
	    NULL);
	gps_frame = XtVaCreateWidget("gps_frame",
	    xmFrameWidgetClass,		gps_form,
	    XmNshadowType,		XmSHADOW_ETCHED_IN,
	    XmNtopAttachment,		XmATTACH_FORM,
	    XmNleftAttachment,		XmATTACH_FORM,
	    XmNrightAttachment,		XmATTACH_FORM,
	    XmNbottomAttachment,	XmATTACH_FORM,
	    NULL);
	(void)XtVaCreateManagedWidget("GPS Data",
	    xmLabelGadgetClass,		gps_frame,
	    XmNchildType,		XmFRAME_TITLE_CHILD,
	    XmNchildVerticalAlignment,	XmALIGNMENT_CENTER,
	    NULL);
	sw = XtVaCreateManagedWidget("scrolled_w",
	    xmScrolledWindowWidgetClass,	gps_frame,
	    XmNscrollingPolicy,			XmAUTOMATIC,
	    NULL);
	gps_data = XtVaCreateWidget("gps_data",
	    xmFormWidgetClass,		sw,
	    XmNfractionBase,		30,
	    NULL);

	/* satellite location and SNR data panel */
	satellite_list = XtVaCreateManagedWidget("satellite_list",
	    xmListWidgetClass,		left,
	    XmNbackground,		get_pixel(toplevel, "snow"),
	    XmNlistSizePolicy,		XmCONSTANT,
	    XmNhighlightThickness,	0,
	    XmNlistSpacing,		4,
	    XmNtopAttachment,		XmATTACH_FORM,
	    XmNrightAttachment,		XmATTACH_FORM,
	    XmNbottomAttachment,	XmATTACH_FORM,
	    XmNleftAttachment,		XmATTACH_FORM,
	    NULL);

	/* the satellite diagram */
	satellite_diagram = XtVaCreateManagedWidget("satellite_diagram",
	    xmDrawingAreaWidgetClass,	right,
	    XmNbackground,		get_pixel(toplevel, "snow"),
	    XmNheight,			SATDIAG_SIZE + 24,
	    XmNwidth,			SATDIAG_SIZE,
	    XmNtopAttachment,		XmATTACH_FORM,
	    XmNrightAttachment,		XmATTACH_FORM,
	    XmNbottomAttachment,	XmATTACH_FORM,
	    XmNleftAttachment,		XmATTACH_FORM,
	    NULL);

	gcv.foreground = BlackPixelOfScreen(XtScreen(satellite_diagram));
	gc = XCreateGC(XtDisplay(satellite_diagram),
	RootWindowOfScreen(XtScreen(satellite_diagram)), GCForeground, &gcv);
	register_canvas(satellite_diagram, gc);
	XtVaSetValues(satellite_diagram, XmNuserData, gc, NULL);
	/*@i@*/XtAddCallback(satellite_diagram, XmNexposeCallback, redraw, NULL);
	/*@i@*/XtAddCallback(satellite_diagram, XmNresizeCallback, resize, NULL);

	/* the data display */
	(void)XtVaCreateManagedWidget("Time", xmLabelGadgetClass, gps_data,
	    XmNalignment,		XmALIGNMENT_END,
	    XmNtopAttachment,		XmATTACH_POSITION,
	    XmNtopPosition,		0,
	    XmNrightAttachment,		XmATTACH_POSITION,
	    XmNrightPosition,		5,
	    XmNbottomAttachment,	XmATTACH_POSITION,
	    XmNbottomPosition,		6,
	    XmNleftAttachment,		XmATTACH_POSITION,
	    XmNleftPosition,		0,
	    NULL);
	(void)XtVaCreateManagedWidget("Latitude", xmLabelGadgetClass, gps_data,
	    XmNalignment,		XmALIGNMENT_END,
	    XmNtopAttachment,		XmATTACH_POSITION,
	    XmNtopPosition,		6,
	    XmNrightAttachment,		XmATTACH_POSITION,
	    XmNrightPosition,		5,
	    XmNbottomAttachment,	XmATTACH_POSITION,
	    XmNbottomPosition,		12,
	    XmNleftAttachment,		XmATTACH_POSITION,
	    XmNleftPosition,		0,
	    NULL);
	(void)XtVaCreateManagedWidget("Longitude", xmLabelGadgetClass, gps_data,
	    XmNalignment,		XmALIGNMENT_END,
	    XmNtopAttachment,		XmATTACH_POSITION,
	    XmNtopPosition,		12,
	    XmNrightAttachment,		XmATTACH_POSITION,
	    XmNrightPosition,		5,
	    XmNbottomAttachment,	XmATTACH_POSITION,
	    XmNbottomPosition,		18,
	    XmNleftAttachment,		XmATTACH_POSITION,
	    XmNleftPosition,		0,
	    NULL);
	(void)XtVaCreateManagedWidget("Altitude", xmLabelGadgetClass, gps_data,
	    XmNalignment,		XmALIGNMENT_END,
	    XmNtopAttachment,		XmATTACH_POSITION,
	    XmNtopPosition,		18,
	    XmNrightAttachment,		XmATTACH_POSITION,
	    XmNrightPosition,		5,
	    XmNbottomAttachment,	XmATTACH_POSITION,
	    XmNbottomPosition,		24,
	    XmNleftAttachment,		XmATTACH_POSITION,
	    XmNleftPosition,		0,
	    NULL);
	(void)XtVaCreateManagedWidget("Speed", xmLabelGadgetClass, gps_data,
	    XmNalignment,		XmALIGNMENT_END,
	    XmNtopAttachment,		XmATTACH_POSITION,
	    XmNtopPosition,		24,
	    XmNrightAttachment,		XmATTACH_POSITION,
	    XmNrightPosition,		5,
	    XmNbottomAttachment,	XmATTACH_POSITION,
	    XmNbottomPosition,		30,
	    XmNleftAttachment,		XmATTACH_POSITION,
	    XmNleftPosition,		0,
	    NULL);

	text_1 = XtVaCreateManagedWidget("time",
	    xmTextFieldWidgetClass,	gps_data,
	    XmNeditable,		False,
	    XmNcursorPositionVisible,	False,
	    XmNtopAttachment,		XmATTACH_POSITION,
	    XmNtopPosition,		0,
	    XmNrightAttachment,		XmATTACH_POSITION,
	    XmNrightPosition,		15,
	    XmNbottomAttachment,	XmATTACH_POSITION,
	    XmNbottomPosition,		6,
	    XmNleftAttachment,		XmATTACH_POSITION,
	    XmNleftPosition,		5,
	    NULL);
	text_2 = XtVaCreateManagedWidget("latitude",
	    xmTextFieldWidgetClass,	gps_data,
	    XmNeditable,		False,
	    XmNcursorPositionVisible,	False,
	    XmNtopAttachment,		XmATTACH_POSITION,
	    XmNtopPosition,		6,
	    XmNrightAttachment,		XmATTACH_POSITION,
	    XmNrightPosition,		15,
	    XmNbottomAttachment,	XmATTACH_POSITION,
	    XmNbottomPosition,		12,
	    XmNleftAttachment,		XmATTACH_POSITION,
	    XmNleftPosition,		5,
	    NULL);
	text_3 = XtVaCreateManagedWidget("longitude",
	    xmTextFieldWidgetClass,	gps_data,
	    XmNeditable,		False,
	    XmNcursorPositionVisible,	False,
	    XmNtopAttachment,		XmATTACH_POSITION,
	    XmNtopPosition,		12,
	    XmNrightAttachment,		XmATTACH_POSITION,
	    XmNrightPosition,		15,
	    XmNbottomAttachment,	XmATTACH_POSITION,
	    XmNbottomPosition,		18,
	    XmNleftAttachment,		XmATTACH_POSITION,
	    XmNleftPosition,		5,
	    NULL);
	text_4 = XtVaCreateManagedWidget("altitude",
	    xmTextFieldWidgetClass,	gps_data,
	    XmNeditable,		False,
	    XmNcursorPositionVisible,	False,
	    XmNtopAttachment,		XmATTACH_POSITION,
	    XmNtopPosition,		18,
	    XmNrightAttachment,		XmATTACH_POSITION,
	    XmNrightPosition,		15,
	    XmNbottomAttachment,	XmATTACH_POSITION,
	    XmNbottomPosition,		24,
	    XmNleftAttachment,		XmATTACH_POSITION,
	    XmNleftPosition,		5,
	    NULL);
	text_5 = XtVaCreateManagedWidget("speed",
	    xmTextFieldWidgetClass,	gps_data,
	    XmNeditable,		False,
	    XmNcursorPositionVisible,	False,
	    XmNtopAttachment,		XmATTACH_POSITION,
	    XmNtopPosition,		24,
	    XmNrightAttachment,		XmATTACH_POSITION,
	    XmNrightPosition,		15,
	    XmNbottomAttachment,	XmATTACH_POSITION,
	    XmNbottomPosition,		30,
	    XmNleftAttachment,		XmATTACH_POSITION,
	    XmNleftPosition,		5,
	    NULL);

	(void)XtVaCreateManagedWidget("EPH", xmLabelGadgetClass, gps_data,
	    XmNalignment,		XmALIGNMENT_END,
	    XmNtopAttachment,		XmATTACH_POSITION,
	    XmNtopPosition,		0,
	    XmNrightAttachment,		XmATTACH_POSITION,
	    XmNrightPosition,		20,
	    XmNbottomAttachment,	XmATTACH_POSITION,
	    XmNbottomPosition,		6,
	    XmNleftAttachment,		XmATTACH_POSITION,
	    XmNleftPosition,		15,
	    NULL);
	(void)XtVaCreateManagedWidget("EPV", xmLabelGadgetClass, gps_data,
	    XmNalignment,		XmALIGNMENT_END,
	    XmNtopAttachment,		XmATTACH_POSITION,
	    XmNtopPosition,		6,
	    XmNrightAttachment,		XmATTACH_POSITION,
	    XmNrightPosition,		20,
	    XmNbottomAttachment,	XmATTACH_POSITION,
	    XmNbottomPosition,		12,
	    XmNleftAttachment,		XmATTACH_POSITION,
	    XmNleftPosition,		15,
	    NULL);
	(void)XtVaCreateManagedWidget("Climb", xmLabelGadgetClass, gps_data,
	    XmNalignment,		XmALIGNMENT_END,
	    XmNtopAttachment,		XmATTACH_POSITION,
	    XmNtopPosition,		12,
	    XmNrightAttachment,		XmATTACH_POSITION,
	    XmNrightPosition,		20,
	    XmNbottomAttachment,	XmATTACH_POSITION,
	    XmNbottomPosition,		18,
	    XmNleftAttachment,		XmATTACH_POSITION,
	    XmNleftPosition,		15,
	    NULL);
	(void)XtVaCreateManagedWidget("Track", xmLabelGadgetClass, gps_data,
	    XmNalignment,		XmALIGNMENT_END,
	    XmNtopAttachment,		XmATTACH_POSITION,
	    XmNtopPosition,		18,
	    XmNrightAttachment,		XmATTACH_POSITION,
	    XmNrightPosition,		20,
	    XmNbottomAttachment,	XmATTACH_POSITION,
	    XmNbottomPosition,		24,
	    XmNleftAttachment,		XmATTACH_POSITION,
	    XmNleftPosition,		15,
	    NULL);
	(void)XtVaCreateManagedWidget("Status", xmLabelGadgetClass, gps_data,
	    XmNalignment,		XmALIGNMENT_END,
	    XmNtopAttachment,		XmATTACH_POSITION,
	    XmNtopPosition,		24,
	    XmNrightAttachment,		XmATTACH_POSITION,
	    XmNrightPosition,		20,
	    XmNbottomAttachment,	XmATTACH_POSITION,
	    XmNbottomPosition,		30,
	    XmNleftAttachment,		XmATTACH_POSITION,
	    XmNleftPosition,		15,
	    NULL);

	text_7 = XtVaCreateManagedWidget("eph",
	    xmTextFieldWidgetClass,	gps_data,
	    XmNeditable,		False,
	    XmNcursorPositionVisible,	False,
	    XmNtopAttachment,		XmATTACH_POSITION,
	    XmNtopPosition,		0,
	    XmNrightAttachment,		XmATTACH_POSITION,
	    XmNrightPosition,		30,
	    XmNbottomAttachment,	XmATTACH_POSITION,
	    XmNbottomPosition,		6,
	    XmNleftAttachment,		XmATTACH_POSITION,
	    XmNleftPosition,		20,
	    NULL);
	text_8 = XtVaCreateManagedWidget("epv",
	    xmTextFieldWidgetClass,	gps_data,
	    XmNeditable,		False,
	    XmNcursorPositionVisible,	False,
	    XmNtopAttachment,		XmATTACH_POSITION,
	    XmNtopPosition,		6,
	    XmNrightAttachment,		XmATTACH_POSITION,
	    XmNrightPosition,		30,
	    XmNbottomAttachment,	XmATTACH_POSITION,
	    XmNbottomPosition,		12,
	    XmNleftAttachment,		XmATTACH_POSITION,
	    XmNleftPosition,		20,
	    NULL);
	text_9 = XtVaCreateManagedWidget("climb",
	    xmTextFieldWidgetClass,	gps_data,
	    XmNeditable,		False,
	    XmNcursorPositionVisible,	False,
	    XmNtopAttachment,		XmATTACH_POSITION,
	    XmNtopPosition,		12,
	    XmNrightAttachment,		XmATTACH_POSITION,
	    XmNrightPosition,		30,
	    XmNbottomAttachment,	XmATTACH_POSITION,
	    XmNbottomPosition,		18,
	    XmNleftAttachment,		XmATTACH_POSITION,
	    XmNleftPosition,		20,
	    NULL);
	text_6 = XtVaCreateManagedWidget("track",
	    xmTextFieldWidgetClass,	gps_data,
	    XmNeditable,		False,
	    XmNcursorPositionVisible,	False,
	    XmNtopAttachment,		XmATTACH_POSITION,
	    XmNtopPosition,		18,
	    XmNrightAttachment,		XmATTACH_POSITION,
	    XmNrightPosition,		30,
	    XmNbottomAttachment,	XmATTACH_POSITION,
	    XmNbottomPosition,		24,
	    XmNleftAttachment,		XmATTACH_POSITION,
	    XmNleftPosition,		20,
	    NULL);
	text_10 = XtVaCreateManagedWidget("status",
	    xmTextFieldWidgetClass,	gps_data,
	    XmNeditable,		False,
	    XmNcursorPositionVisible,	False,
	    XmNtopAttachment,		XmATTACH_POSITION,
	    XmNtopPosition,		24,
	    XmNrightAttachment,		XmATTACH_POSITION,
	    XmNrightPosition,		30,
	    XmNbottomAttachment,	XmATTACH_POSITION,
	    XmNbottomPosition,		30,
	    XmNleftAttachment,		XmATTACH_POSITION,
	    XmNleftPosition,		20,
	    NULL);

	XtManageChild(gps_data);
	XtManageChild(sat_frame);
	XtManageChild(sky_frame);
	XtManageChild(gps_frame);

	XtVaSetValues(main_w,
	    XmNmenuBar,		menubar,
	    XmNworkWindow,	form,
	    NULL);

	XtRealizeWidget(toplevel);
	/*@ -type -nullpass @*/
	delw = XmInternAtom(XtDisplay(toplevel), "WM_DELETE_WINDOW",
	    (Boolean)False);
	(void)XmAddWMProtocolCallback(toplevel, delw,
		(XtCallbackProc)quit_cb, NULL);
	/*@ +type +onlytrans @*/

	/* create empty list items to be replaced on update */
	string = XmStringCreateSimple(" ");
	for (i = 0; i <= MAXCHANNELS; i++)
		XmListAddItem(satellite_list, string, 0);
	XmStringFree(string);
}
/*@ +mustfreefresh -ignoresigns +immediatetrans @*/

/* runs when there is no data for a while */
static void
handle_time_out(XtPointer client_data UNUSED, XtIntervalId *ignored UNUSED)
{
	XmTextFieldSetString(text_10, "UNKNOWN");
}

static void
handle_input(XtPointer client_data UNUSED, int *source UNUSED, XtInputId *id UNUSED)
{
	if (gps_poll(gpsdata) < 0) {
		XtRemoveInput(gps_input);
		(void)gps_close(gpsdata);
		XtRemoveTimeOut(timeout);
		XmTextFieldSetString(text_10, "No GPS data available");
		(void)err_dialog(toplevel, "No GPS data available.\n\n"
		    "Check the connection to gpsd and if gpsd is running");
		gps_lost = true;
		gps_timeout = XtAppAddTimeOut(app, 3000, handle_gps, app);
	}
}

/* runs on each sentence */
static void
update_panel(struct gps_data_t *gpsdata, char *message,	size_t len UNUSED)
{
	unsigned int i;
	int newstate;
	XmString string[MAXCHANNELS + 1];
	char s[128], *latlon, *sp;

	/* this is where we implement source-device filtering */
	if (gpsdata->dev.path[0]!='\0' && source.device!=NULL && strcmp(source.device, gpsdata->dev.path) != 0)
	    return;

	/* the raw data sisplay */
	if (message[0] != '\0')
		while (isspace(*(sp = message + strlen(message) - 1)))
			*sp = '\0';
	XmTextFieldSetString(status, message);

	/* This is for the satellite status display */
	if (gpsdata->satellites) {
		string[0] = XmStringCreateSimple(
		    "PRN:   Elev:  Azim:  SNR:  Used:");
		for (i = 0; i < MAXCHANNELS; i++) {
			if (i < (unsigned int)gpsdata->satellites) {
				(void)snprintf(s, sizeof(s),
				    " %3d    %2d    %3d    %2.0f      %c",
				    gpsdata->PRN[i], gpsdata->elevation[i],
				    gpsdata->azimuth[i], gpsdata->ss[i],
				    gpsdata->used[i] ? 'Y' : 'N');
			} else
			    (void)strlcpy(s, "                  ", sizeof(s));
			string[i + 1] = XmStringCreateSimple(s);
		}
		XmListReplaceItemsPos(satellite_list, string,
		    (int)sizeof(string), 1);
#ifndef S_SPLINT_S
		for (i = 0; i < (sizeof(string)/sizeof(string[0])); i++)
			XmStringFree(string[i]);
#endif /* S_SPLINT_S */
	}

	/* here are the value fields */
	if (isnan(gpsdata->fix.time)==0) {
	    (void)unix_to_iso8601(gpsdata->fix.time, s, sizeof(s));
		XmTextFieldSetString(text_1, s);
	} else
		XmTextFieldSetString(text_1, "n/a");
	if (gpsdata->fix.mode >= MODE_2D) {
		latlon = deg_to_str(deg_type,
		    fabs(gpsdata->fix.latitude));
		(void)snprintf(s, sizeof(s), "%s %c", latlon,
		    (gpsdata->fix.latitude < 0) ? 'S' : 'N');
		XmTextFieldSetString(text_2, s);
	} else
		XmTextFieldSetString(text_2, "n/a");
	if (gpsdata->fix.mode >= MODE_2D) {
		latlon = deg_to_str(deg_type,
		    fabs(gpsdata->fix.longitude));
		(void)snprintf(s, sizeof(s), "%s %c", latlon,
		    (gpsdata->fix.longitude < 0) ? 'W' : 'E');
		XmTextFieldSetString(text_3, s);
	} else
		XmTextFieldSetString(text_3, "n/a");
	if (gpsdata->fix.mode == MODE_3D) {
		(void)snprintf(s, sizeof(s), "%f %s",
		    gpsdata->fix.altitude * altunits->factor,
		    altunits->legend);
		XmTextFieldSetString(text_4, s);
	} else
		XmTextFieldSetString(text_4, "n/a");
	if (gpsdata->fix.mode >= MODE_2D && isnan(gpsdata->fix.track)==0) {
		(void)snprintf(s, sizeof(s), "%f %s",
		    gpsdata->fix.speed * speedunits->factor,
		    speedunits->legend);
		XmTextFieldSetString(text_5, s);
	} else
		XmTextFieldSetString(text_5, "n/a");
	if (gpsdata->fix.mode >= MODE_2D && isnan(gpsdata->fix.track)==0) {
		(void)snprintf(s, sizeof(s), "%f degrees",
		    gpsdata->fix.track);
		XmTextFieldSetString(text_6, s);
	} else
		XmTextFieldSetString(text_6, "n/a");
	/* FIXME: Someday report epx and epy */
	if (isnan(gpsdata->fix.epx)==0) {
		(void)snprintf(s, sizeof(s), "%f %s",
		    EMIX(gpsdata->fix.epx, gpsdata->fix.epy) * altunits->factor,
		    altunits->legend);
		XmTextFieldSetString(text_7, s);
	} else
		XmTextFieldSetString(text_7, "n/a");
	if (isnan(gpsdata->fix.epv)==0) {
		(void)snprintf(s, sizeof(s), "%f %s",
		    gpsdata->fix.epv * altunits->factor,
		    altunits->legend);
		XmTextFieldSetString(text_8, s);
	} else
		XmTextFieldSetString(text_8, "n/a");
	if (gpsdata->fix.mode == MODE_3D && isnan(gpsdata->fix.climb)==0) {
		(void)snprintf(s, sizeof(s), "%f %s/sec",
		    gpsdata->fix.climb * altunits->factor,
		    altunits->legend);
		XmTextFieldSetString(text_9, s);
	} else
		XmTextFieldSetString(text_9, "n/a");
	if (gpsdata->set & DEVICEID_SET) {
		(void)strlcpy(s, "xgps: ", sizeof(s));
		(void)strlcat(s, gpsdata->dev.driver, sizeof(s));
		(void)strlcat(s, " ", sizeof(s));
		(void)strlcat(s, gpsdata->dev.subtype, sizeof(s));
		set_title(s);
	}
	if (gpsdata->online == 0) {
		newstate = 0;
		(void)strlcpy(s, "OFFLINE", sizeof(s));
	} else {
		newstate = gpsdata->fix.mode;

		switch (gpsdata->fix.mode) {
		case MODE_2D:
			(void)snprintf(s, sizeof(s), "2D %sFIX",
			    (gpsdata->status == STATUS_DGPS_FIX) ? "DIFF " :
			    "");
			break;
		case MODE_3D:
			(void)snprintf(s, sizeof(s), "3D %sFIX",
			    (gpsdata->status == STATUS_DGPS_FIX) ? "DIFF " :
			    "");
			break;
		default:
		    (void)strlcpy(s, "NO FIX", sizeof(s));
			break;
		}
	}
	if (newstate != state) {
		timer = time(NULL);
		state = newstate;
	}
	(void)snprintf(s + strlen(s), sizeof(s) - strlen(s), " (%d secs)",
	    (int) (time(NULL) - timer));
	XmTextFieldSetString(text_10, s);
	draw_graphics(gpsdata);

	XtRemoveTimeOut(timeout);
	timeout = XtAppAddTimeOut(app, 2000, handle_time_out, NULL);
}

static char *
get_resource(Widget w, char *name, char *default_value)
{
	XtResource xtr;
	char *value = NULL;

	/*@ -observertrans -statictrans -immediatetrans -compdestroy @*/
	xtr.resource_name = name;
	xtr.resource_class = "AnyClass";
	xtr.resource_type = XmRString;
	xtr.resource_size = (Cardinal)sizeof(String);
	xtr.resource_offset = 0;
	xtr.default_type = XmRImmediate;
	xtr.default_addr = default_value;

	XtGetApplicationResources(w, &value, &xtr, 1, NULL, 0);
	/*@ +observertrans +statictrans +immediatetrans +compdestroy @*/
	/*@i@*/return value ? value: default_value;
}

/* runs when gps needs attention */
/*@ -globstate -branchstate @*/
void
handle_gps(XtPointer client_data UNUSED, XtIntervalId *ignored UNUSED)
{
	char error[128];
	static bool dialog_posted = false;

	/*@i@*/gpsdata = gps_open(source.server, source.port);
	if (!gpsdata) {
		if (!gps_lost && !dialog_posted) {
			(void)snprintf(error, sizeof(error),
			    "No GPS data available.\n\n%s\n\n"
			    "Check the connection to gpsd and if "
			    "gpsd is running.", gps_errstr(errno));
			(void)err_dialog(toplevel, error);
			dialog_posted = true;
		}
		gps_timeout = XtAppAddTimeOut(app, 1000, handle_gps, app);
	} else {
	        gps_mask_t mask;
		timeout = XtAppAddTimeOut(app, 2000, handle_time_out, app);
		timer = time(NULL);

		gps_set_raw_hook(gpsdata, update_panel);

		// WATCH_NEWSTYLE forces new protocol, for test purposes 
		mask = WATCH_ENABLE|WATCH_RAW|WATCH_NEWSTYLE;
		(void)gps_stream(gpsdata, mask);

		gps_input = XtAppAddInput(app, gpsdata->gps_fd,
		    (XtPointer)XtInputReadMask, handle_input, NULL);
		if (gps_lost || dialog_posted)
		    (void)err_dialog(toplevel, "GPS data is available.");
		dialog_posted = gps_lost = false;
	}
}
/*@ +globstate +branchstate @*/

Widget
err_dialog(Widget widget, char *s)
{
	static Widget dialog;
	XmString t;

	/*@ -mustfreefresh +charint -usedef -statictrans -immediatetrans -onlytrans @*/
	if (!dialog) {
		Arg args[5];
		int n = 0;
		XmString ok = XmStringCreateLocalized("OK");
		XtSetArg(args[n], XmNautoUnmanage, False); n++;
		XtSetArg(args[n], XmNcancelLabelString, ok); n++;
		dialog = XmCreateInformationDialog(widget, "notice",
						   args, (Cardinal)n);
		XtAddCallback(dialog, XmNcancelCallback, dlg_callback, NULL);
		XtUnmanageChild(XmMessageBoxGetChild(dialog,
		    XmDIALOG_OK_BUTTON));
		XtUnmanageChild(XmMessageBoxGetChild(dialog,
		    XmDIALOG_HELP_BUTTON));
	}
	t = XmStringCreateLocalized(s);
	XtVaSetValues(dialog,
	    XmNmessageString,	t,
	    XmNdialogStyle,	XmDIALOG_FULL_APPLICATION_MODAL,
	    NULL);
	XmStringFree(t);
	XtManageChild(dialog);
	XtPopup(XtParent(dialog), XtGrabNone);
	return dialog;
	/*@ +mustfreefresh -charint +usedef +statictrans +immediatetrans  +onlytrans @*/
}

void
dlg_callback(Widget dialog, XtPointer client_data UNUSED, XtPointer call_data UNUSED)
{
    /*@i1@*/XtPopdown(XtParent(dialog));
}

void
file_cb(Widget widget UNUSED, XtPointer client_data, XtPointer call_data UNUSED)
{
	uintptr_t item_no = (uintptr_t)client_data;

	if (item_no == 0)
		exit(0);
}

void
help_cb(Widget widget UNUSED, XtPointer client_data, XtPointer call_data UNUSED)
{
	static Widget help, about;
	Widget *dialog;
	uintptr_t item_no = (uintptr_t)client_data;

	/*@ -usedef -immediatetrans -onlytrans -mustfreefresh -type +charint -ptrcompare @*/
	if (item_no == 0 && !help) {
		Arg args[5];
		int n = 0;
		XmString msg = XmStringCreateLtoR(
		    "XGps displays live data from a GPS unit controlled by\n"
		    "a running gpsd daemon.\n\n"
		    "The list of satellites and their position on the sky\n"
		    "are displayed and the most important live data is\n"
		    "shown in text fields below the skyview.\n",
		    XmFONTLIST_DEFAULT_TAG);
		XtSetArg(args[n], XmNmessageString, msg); n++;
		help = XmCreateInformationDialog(toplevel, "help_dialog",
						 args, (Cardinal)n);
		XtUnmanageChild(XmMessageBoxGetChild(help,
		    XmDIALOG_CANCEL_BUTTON));
		XtUnmanageChild(XmMessageBoxGetChild(help,
		    XmDIALOG_HELP_BUTTON));
	}

	if (item_no == 1 && !about) {
		Arg args[5];
		int n = 0;
		XmString msg = XmStringCreateLtoR(
		    "XGps 3.1.2\n\n"
		    "Copyright (c) 2007 by Marc Balmer <marc@msys.ch>\n"
		    "Copyright (c) 2006 by Eric S. Raymond\n"
		    "\nUse at your own risk.\n\n",
		    XmFONTLIST_DEFAULT_TAG);
		XtSetArg(args[n], XmNmessageString, msg);
		n++;
		about = XmCreateInformationDialog(toplevel, "about_dialog",
						  args, (Cardinal)n);
		XtUnmanageChild(XmMessageBoxGetChild(about,
		    XmDIALOG_CANCEL_BUTTON));
		XtUnmanageChild(XmMessageBoxGetChild(about,
		    XmDIALOG_HELP_BUTTON));
	}
	/*@ +usedef +immediatetrans +onlytrans +type -charint +ptrcompare @*/

	if (item_no == 0)
		dialog = &help;
	else
		dialog = &about;

	XtManageChild(*dialog);
	XtPopup(XtParent(*dialog), XtGrabNone);
	/*@ +mustfreefresh @*/
}

/*@ -mustfreefresh @*/
int
main(int argc, char *argv[])
{
	int option;
	char *su, *au;

	/*@ -globstate -onlytrans @*/
	toplevel = XtVaAppInitialize(&app, "XGps", options, XtNumber(options),
	    &argc, argv, fallback_resources, NULL);

	su = get_resource(toplevel, "speedunits", "kmh");
	for (speedunits = speedtable;
	    speedunits < speedtable + sizeof(speedtable)/sizeof(speedtable[0]);
	    speedunits++)
		if (strcmp(speedunits->legend, su)==0)
			goto speedunits_ok;
	speedunits = speedtable;
	fprintf(stderr, "xgps: unknown speed unit, defaulting to %s\n",
	    speedunits->legend);

speedunits_ok:

	au = get_resource(toplevel, "altunits", "meters");
	for (altunits = alttable;
	    altunits < alttable + sizeof(alttable)/sizeof(alttable[0]);
	    altunits++)
		if (strcmp(altunits->legend, au)==0)
			goto altunits_ok;
	altunits = alttable;
	fprintf(stderr, "xgps: unknown altitude unit, defaulting to %s\n",
	    altunits->legend);

altunits_ok:

	while ((option = getopt(argc, argv, "Vhl:")) != -1) {
		switch (option) {
		case 'V':
		    (void)fprintf(stderr, "SVN ID: $Id$ \n");
		    exit(0);
		case 'l':
		    switch (optarg[0]) {
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
			fprintf(stderr, "Unknown -l argument: %s\n",
				optarg);
			/*@ -casebreak @*/
		    }
		case 'h':
		default:
		    (void)fputs("usage:  xgps [-Vhj] [-speedunits "
				"{mph,kmh,knots}] [-altunits {ft,meters}] "
				"[-l {d|m|s}] [server[:port:[device]]]\n", stderr);
		    exit(1);
		}
	}

	if (optind < argc) {
	    gpsd_source_spec(argv[optind], &source);
	} else
	    gpsd_source_spec(NULL, &source);

	register_shell(toplevel);
	build_gui(toplevel);

	gps_timeout = XtAppAddTimeOut(app, 200, handle_gps, app);
	XtAppMainLoop(app);

	return 0;
	/*@ +globstate +onlytrans @*/
}
/*@ +mustfreefresh @*/
