
/* include files */
#include "config.h"
#include <stdio.h>

#include <Xm/Xm.h>
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
#include <X11/Shell.h>
#include <math.h>

#include "nmea.h"
#include "gps.h"

#define XCENTER         (double)(width/2)
#define YCENTER         (double)(height/2)
#define SCALE           (double)(diameter/2)
#define DEG2RAD         (3.1415926535897931160E0/180.0)
#define RM		20

#undef min
#define min(a,b) ((a) < (b) ? (a) : (b))

Widget draww;
GC drawGC;
Dimension width, height;
int diameter;
Pixmap pixmap;

void set_color(String color)
{
    Display *dpy = XtDisplay(draww);
    Colormap cmap = DefaultColormapOfScreen(XtScreen(draww));
    XColor col, unused;

    if (!XAllocNamedColor(dpy, cmap, color, &col, &unused)) {
	char buf[32];

	sprintf(buf, "Can't alloc %s", color);
	XtWarning(buf);
	return;
    }
    XSetForeground(dpy, drawGC, col.pixel);
}

void register_canvas(Widget w, GC gc)
{
    draww = w;
    drawGC = gc;

    XtVaGetValues(w, XmNwidth, &width, XmNheight, &height, NULL);
    pixmap = XCreatePixmap(XtDisplay(w),
			   RootWindowOfScreen(XtScreen(w)), width, height,
			   DefaultDepthOfScreen(XtScreen(w)));
    set_color("White");
    XFillRectangle(XtDisplay(draww), pixmap, drawGC, 0, 0, width, height);
    diameter = min(width, height) - RM;
}

/* #define PCORRECT */

static void pol2cart(double azimuth, double elevation, double *xout, double *yout)
{
    double sinelev;

    azimuth *= DEG2RAD;
    elevation = 90.0 - elevation;

#ifdef PCORRECT
    elevation *= DEG2RAD;
    sinelev = sin(elevation) * SCALE;
#else
    sinelev = (elevation / 90.0) * SCALE;
#endif
    *xout = XCENTER + sin(azimuth) * sinelev;
    *yout = YCENTER - cos(azimuth) * sinelev;
}


void draw_arc(int x, int y, int diam)
{
    XDrawArc(XtDisplay(draww), pixmap, drawGC,
	     x - diam / 2, y - diam / 2,	/* x,y */
	     diam, diam,	/* width, height */
	     0, 360 * 64	/* angle1, angle2 */
	);
}


int get_status(int satellite)
{
    int i;
    int s;

    if (gNMEAdata.ZCHseen) {
	for (i = 0; i < 12; i++)
	    if (satellite == gNMEAdata.Zs[i])
		return gNMEAdata.Zv[i];
	return 0;
    } else {
	for (i = 0; i < 12; i++)
	    if (satellite == gNMEAdata.PRN[i]) {
		s = gNMEAdata.ss[i] / 6;
		return s > 7 ? 7 : s;
	    }
	return 0;
    }
}


void draw_graphics()
{
    int i;
    double x, y;
    char buf[20];

    if (gNMEAdata.cmask & (C_SAT | C_ZCH)) {

	i = min(width, height);

	set_color("White");
	XFillRectangle(XtDisplay(draww), pixmap, drawGC, 0, 0, width, height);

	/* draw something in the center */
	set_color("Grey");
	draw_arc(width / 2, height / 2, 6);

	/* draw the 45 degree circle */
#ifdef PCORRECT
	draw_arc(width / 2, height / 2, ((i - RM) * 7) / 10);	/* sin(45) ~ 0.7 */
#else
	draw_arc(width / 2, height / 2, (i - RM) / 2);
#endif

	set_color("Black");
	draw_arc(width / 2, height / 2, i - RM);

	/* Now draw the satellites... */
	for (i = 0; i < gNMEAdata.in_view; i++) {
	    pol2cart(gNMEAdata.azimuth[i], gNMEAdata.elevation[i], &x, &y);

	    switch (get_status(gNMEAdata.PRN[i]) & 7) {
	    case 0:
	    case 1:
		set_color("Grey");
		break;
	    case 2:
	    case 3:
		set_color("Yellow");
		break;
	    case 4:
	    case 5:
	    case 6:
		set_color("Red");
		break;
	    case 7:
		set_color("Green");
		break;
	    }
	    XFillArc(XtDisplay(draww), pixmap, drawGC,
		     (int) x - 5, (int) y - 5,	/* x,y */
		     11, 11,	/* width, height */
		     0, 360 * 64	/* angle1, angle2 */
		);
	    sprintf(buf, "%02d", gNMEAdata.PRN[i]);
	    set_color("Blue");
	    XDrawString(XtDisplay(draww), pixmap, drawGC,
			(int) x + 0, (int) y + 17, buf, 2);
	}
	XCopyArea(XtDisplay(draww), pixmap, XtWindow(draww), drawGC,
		  0, 0, width, height, 0, 0);
    }
}

void redraw(Widget w, XtPointer client_data, XmDrawingAreaCallbackStruct * cbs)
{
    XCopyArea(XtDisplay(draww), pixmap, XtWindow(draww), drawGC,
	      cbs->event->xexpose.x, cbs->event->xexpose.y,
	      cbs->event->xexpose.width, cbs->event->xexpose.height,
	      cbs->event->xexpose.x, cbs->event->xexpose.y);
}
