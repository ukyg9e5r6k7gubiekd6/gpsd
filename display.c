#include <stdio.h>
#include <math.h>

#include "gps.h"
#include "config.h"
#include "display.h"

#define RM		20

#undef min
#define min(a,b) ((a) < (b) ? (a) : (b))

static Widget draww;
static GC drawGC;
static Dimension width, height;
static int diameter;
static Pixmap pixmap;

static void set_color(String color)
{
    Display *dpy = XtDisplay(draww);
    Colormap cmap = DefaultColormapOfScreen(XtScreen(draww));
    XColor col, unused;

    if (!XAllocNamedColor(dpy, cmap, color, &col, &unused)) {
	char buf[32];

	snprintf(buf, sizeof(buf), "Can't alloc %s", color);
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

static void pol2cart(double azimuth, double elevation, int *xout, int *yout)
{
    azimuth *= DEG_2_RAD;
#ifdef PCORRECT
    elevation = sin((90.0 - elevation) * DEG_2_RAD);
#else
    elevation = ((90.0 - elevation) / 90.0);
#endif
    *xout = (int)((width/2) + sin(azimuth) * elevation * (diameter/2));
    *yout = (int)((height/2) - cos(azimuth) * elevation * (diameter/2));
}

static void draw_arc(int x, int y, int diam)
{
    XDrawArc(XtDisplay(draww), pixmap, drawGC,
	     x - diam / 2, y - diam / 2,	/* x,y */
	     diam, diam,	/* width, height */
	     0, 360 * 64);	/* angle1, angle2 */
}

void draw_graphics(struct gps_data_t *gpsdata)
{
    int i, x, y;
    char buf[20];

    if (gpsdata->satellites) {
	i = min(width, height);

	set_color("White");
	XFillRectangle(XtDisplay(draww), pixmap, drawGC, 0, 0, width, height);

	/* draw something in the center */
	set_color("Grey");
	draw_arc(width / 2, height / 2, 6);

	/* draw the 45 degree circle */
#ifdef PCORRECT
	draw_arc(width / 2, height / 2, (i - RM) * 0.7); /* sin(45) ~ 0.7 */
#else
	draw_arc(width / 2, height / 2, (i - RM) * 0.5);
#endif

	set_color("Black");
	draw_arc(width / 2, height / 2, i - RM);

	pol2cart(0, 0, &x, &y);
	set_color("Black");
	XDrawString(XtDisplay(draww),pixmap, drawGC, x, y, "N", 1);
	pol2cart(90, 0, &x, &y);
	set_color("Black");
	XDrawString(XtDisplay(draww),pixmap, drawGC, x+2, y, "E", 1);
	pol2cart(180, 0, &x, &y);
	set_color("Black");
	XDrawString(XtDisplay(draww),pixmap, drawGC, x, y+10, "S", 1);
	pol2cart(270, 0, &x, &y);
	set_color("Black");
	XDrawString(XtDisplay(draww),pixmap, drawGC, x-5,y, "W", 1);

	/* Now draw the satellites... */
	for (i = 0; i < gpsdata->satellites; i++) {
	    pol2cart(gpsdata->azimuth[i], gpsdata->elevation[i], &x, &y);

	    if (gpsdata->ss[i] < 20) 
		set_color("Grey");
	    else if (gpsdata->ss[i] < 40)
		set_color("Yellow");
	    else
		set_color("Green");
	    XFillArc(XtDisplay(draww), pixmap, drawGC,
		     x - 5, y - 5,	/* x,y */
		     11, 11,		/* width, height */
		     0, 360 * 64	/* angle1, angle2 */
		);
	    snprintf(buf, sizeof(buf), "%02d", gpsdata->PRN[i]);
	    set_color("Blue");
	    XDrawString(XtDisplay(draww), pixmap, drawGC, x, y + 17, buf, 2);
	    if (gpsdata->ss[i]) {
		set_color("Black");
		XDrawPoint(XtDisplay(draww), pixmap, drawGC, x, y);
	    }
		
	}
	XCopyArea(XtDisplay(draww), pixmap, XtWindow(draww), drawGC,
		  0, 0, width, height, 0, 0);
    }
}

void redraw(Widget w UNUSED, XtPointer client_data UNUSED,
	    XmDrawingAreaCallbackStruct * cbs)
{
    XCopyArea(XtDisplay(draww), pixmap, XtWindow(draww), drawGC,
	      cbs->event->xexpose.x, cbs->event->xexpose.y,
	      cbs->event->xexpose.width, cbs->event->xexpose.height,
	      cbs->event->xexpose.x, cbs->event->xexpose.y);
}
