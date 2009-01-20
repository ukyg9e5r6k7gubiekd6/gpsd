/* $gpsd: display.c 4106 2006-12-07 23:55:10Z esr $ */

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

#include <math.h>
#include <stdio.h>

#include <X11/Intrinsic.h>
#include <Xm/Xm.h>

#include <gpsd_config.h>
#include <gps.h>

#include "display.h"

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

void 
register_shell(Widget w)
{
	appshell = w;
}

void
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

void
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

void
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

void
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
void
resize(Widget widget, XtPointer client_data UNUSED, XtPointer call_data UNUSED)
{
	GC gc;
	XtVaGetValues(widget,
	    XmNuserData, 	&gc,
	    NULL);
	register_canvas(widget, gc);
}
/*@ +usedef +mustfreefresh @*/
