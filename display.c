#include <Xm/Xm.h>
#include <math.h>

#include "gps.h"

#define XCENTER         (double)(width/2)
#define YCENTER         (double)(height/2)
#define SCALE           (double)(diameter/2)
#define DEG2RAD         (3.1415926535897931160E0/180.0)
#define RM		20

#undef min
#define min(a,b) ((a) < (b) ? (a) : (b))

static Widget draww;
static GC drawGC;
static Dimension width, height;
static int diameter;
static Pixmap pixmap;

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

static void pol2cart(double azimuth, double elevation, double *xout, double *yout)
{
    azimuth *= DEG2RAD;
#ifdef PCORRECT
    elevation = sin((90.0 - elevation) * DEG2RAD);
#else
    elevation = ((90.0 - elevation) / 90.0);
#endif
    *xout = XCENTER + sin(azimuth) * elevation * SCALE;
    *yout = YCENTER - cos(azimuth) * elevation * SCALE;
}

static void draw_arc(int x, int y, int diam)
{
    XDrawArc(XtDisplay(draww), pixmap, drawGC,
	     x - diam / 2, y - diam / 2,	/* x,y */
	     diam, diam,	/* width, height */
	     0, 360 * 64);	/* angle1, angle2 */
}

static int get_status(struct gps_data_t *gpsdata, int satellite)
{
    int i;

    for (i = 0; i < MAXCHANNELS; i++)
	if (satellite == gpsdata->PRN[i]) {
	    if (gpsdata->ss[i]<20) return 1;
	    if (gpsdata->ss[i]<40) return 2;
	    return 7;
	}
    return 0;
}

void draw_graphics(struct gps_data_t *gpsdata)
{
    int i;
    double x, y;
    char buf[20];

    if (SEEN(gpsdata->satellite_stamp)) {
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
	XDrawString(XtDisplay(draww),pixmap, drawGC, (int)x, (int)y, "N", 1);
	pol2cart(90, 0, &x, &y);
	set_color("Black");
	XDrawString(XtDisplay(draww),pixmap, drawGC, (int)x+2, (int)y, "E", 1);
	pol2cart(180, 0, &x, &y);
	set_color("Black");
	XDrawString(XtDisplay(draww),pixmap, drawGC, (int)x, (int)y+10, "S", 1);
	pol2cart(270, 0, &x, &y);
	set_color("Black");
	XDrawString(XtDisplay(draww),pixmap, drawGC, (int) x-5,(int)y, "W", 1);

	/* Now draw the satellites... */
	for (i = 0; i < gpsdata->satellites; i++) {
	    pol2cart(gpsdata->azimuth[i], gpsdata->elevation[i], &x, &y);

	    switch (get_status(gpsdata, gpsdata->PRN[i]) & 7) {
	    case 0: case 1:
		set_color("Grey");
		break;
	    case 2: case 3:
		set_color("Yellow");
		break;
	    case 4: case 5: case 6:
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
	    sprintf(buf, "%02d", gpsdata->PRN[i]);
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
