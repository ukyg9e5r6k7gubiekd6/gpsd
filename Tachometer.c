/*
 * Tachometer Widget Implementation
 *
 * Author: Kazuhiko Shutoh, 1989.
 * Revised by Shinji Sumimoto, 1989/9 (xtachos)
 *
 * Permission to use, copy, modify and distribute without charge this software,
 * documentation, images, etc. is granted, provided that this comment and the
 * author's name is retained.  The author assumes no responsibility for lost
 * sleep as a consequence of use of this software.
 *
 * Send any comments, bug reports, etc. to shutoh@isl.yamaha.JUNET
 *
 *
 * Modifications : ilham@mit.edu   (July 10 '90)
 */


#define XtStrlen(s)		((s) ? strlen(s) : 0)

#include <X11/IntrinsicP.h>
#include <X11/StringDefs.h>
#include <TachometerP.h>
#include <math.h>

/****************************************************************
 *
 * Full class record constant
 *
 ****************************************************************/

/* Private Data */

#define		PI		3.1415927

typedef struct {
		unsigned char	digit[7];
		} DigitRec;

typedef struct {
		int	nofline;
		XPoint	point_list[5];
		} StringRec;

/*	Number's character database - like as "LED" */

DigitRec num_segment[] = {
		{1,1,1,1,1,1,0},
		{0,1,1,0,0,0,0},
		{1,1,0,1,1,0,1},
		{1,1,1,1,0,0,1},
		{0,1,1,0,0,1,1},
		{1,0,1,1,0,1,1},
		{1,0,1,1,1,1,1},
		{1,1,1,0,0,0,0},
		{1,1,1,1,1,1,1},
		{1,1,1,1,0,1,1}};

XSegment	offset[] = {
		{-10,-10, 10,-10},
		{ 10,-10, 10,  0},
		{ 10,  0, 10, 10},
		{ 10, 10,-10, 10},
		{-10, 10,-10,  0},
		{-10,  0,-10,-10},
		{-10,  0, 10,  0}};


/*	" X 10 %" character database	*/

StringRec	char_data[] = {
		{ 2,				/* "X" */
			{{-17, -5},	
		  	{-7, 5}}},
		{ 2,
			{{-7, -5},
			{-17, 5}}},
		{ 2,				/* "1" */
			{{-2, -5},
			{-2,  5}}},
		{ 5,				/* "0" */
			{{2, -5},
			{12, -5},
			{12, 5},
			{2, 5},
			 {2, -5}}}};
#if 0
{{{{			{2, -5}}},
		{ 5, 				/* "%" */
			{{17, -5},
			{20, -5},
			{20, -2},
			{17, -2},
			{17, -5}}},
		{ 2, 
			{{27, -5},
			{17, 5}}},
		{5, 	
			{{24, 2},
			{27, 2},
			{27, 5},
			{24, 5},
			{24, 2}}}};
#endif



#define offst(field) XtOffset(TachometerWidget, field)
static XtResource resources[] = {
    {XtNforeground, XtCForeground, XtRPixel, sizeof(Pixel),
	  offst(tachometer.scale), XtRString, "XtDefaultForeground"},
    {XtNtachometerCircleColor, XtCBorderColor, XtRPixel, sizeof(Pixel),
	  offst(tachometer.circle), XtRString, "XtDefaultForeground"},
    {XtNtachometerNeedleColor, XtCBorderColor, XtRPixel, sizeof(Pixel),
	  offst(tachometer.needle), XtRString, "XtDefaultForeground"},
    {XtNtachometerNeedleSpeed, XtCtachometerNeedleSpeed, XtRInt,
	  sizeof(int), offst(tachometer.speed), XtRImmediate, (caddr_t) 1},
    {XtNvalue, XtCValue, XtRInt, sizeof(int),
	  offst(tachometer.value), XtRImmediate, (caddr_t) 0},
    {XtNheight, XtCHeight, XtRDimension, sizeof(Dimension),
	  offst(core.height), XtRImmediate, (caddr_t) 100},
    {XtNwidth, XtCWidth, XtRDimension, sizeof(Dimension),
	  offst(core.width), XtRImmediate, (caddr_t) 100},
    {XtNborderWidth, XtCBorderWidth, XtRDimension, sizeof(Dimension),
	  offst(core.border_width), XtRImmediate, (caddr_t) 0},
    {XtNinternalBorderWidth, XtCBorderWidth, XtRDimension, sizeof(Dimension),
	  offst(tachometer.internal_border), XtRImmediate, (caddr_t) 0},
};

static void Initialize();
static void Realize();
static void Resize();
static void Redisplay();
static Boolean SetValues();
static void Destroy();

TachometerClassRec tachometerClassRec = {
  {
/* core_class fields */	
#define superclass		(&simpleClassRec)
    /* superclass	  	*/	(WidgetClass) superclass,
    /* class_name	  	*/	"Tachometer",
    /* widget_size	  	*/	sizeof(TachometerRec),
    /* class_initialize   	*/	NULL,
    /* class_part_initialize	*/	NULL,
    /* class_inited       	*/	FALSE,
    /* initialize	  	*/	Initialize,
    /* initialize_hook		*/	NULL,
    /* realize		  	*/	Realize,
    /* actions		  	*/	NULL,
    /* num_actions	  	*/	0,
    /* resources	  	*/	resources,
    /* num_resources	  	*/	XtNumber(resources),
    /* xrm_class	  	*/	NULLQUARK,
    /* compress_motion	  	*/	TRUE,
    /* compress_exposure  	*/	TRUE,
    /* compress_enterleave	*/	TRUE,
    /* visible_interest	  	*/	FALSE,
    /* destroy		  	*/	Destroy,
    /* resize		  	*/	Resize,
    /* expose		  	*/	Redisplay,
    /* set_values	  	*/	SetValues,
    /* set_values_hook		*/	NULL,
    /* set_values_almost	*/	XtInheritSetValuesAlmost,
    /* get_values_hook		*/	NULL,
    /* accept_focus	 	*/	NULL,
    /* version			*/	XtVersion,
    /* callback_private   	*/	NULL,
    /* tm_table		   	*/	NULL,
    /* query_geometry		*/	NULL,
    /* display_accelerator	*/	XtInheritDisplayAccelerator,
    /* extension		*/	NULL
  },
/* Simple class fields initialization */
  {
	    /* change_sensitive         */      XtInheritChangeSensitive
  }

};
WidgetClass tachometerWidgetClass = (WidgetClass)&tachometerClassRec;
/****************************************************************
 *
 * Private Procedures
 *
 ****************************************************************/


static void DrawTachometer();
static void FastFillCircle();
static void GetneedleGC();
static void GetscaleGC();
static void GetcSingleircleGC();
static void GetbackgroundGC();
static void DrawGauge();
static void DrawNeedle();
static void DrawNumbers();
static void DrawSingleNumber();
static void DrawLabelString();
static void MoveNeedle();



static void DrawTachometer(w)
TachometerWidget w;
{
     Cardinal center_x, center_y;
     Cardinal radius_x, radius_y;
     
     center_x = w->core.width / 2;
     center_y = w->core.height / 2;

     radius_x = center_x - w->tachometer.internal_border;
     radius_y = center_y - w->tachometer.internal_border;

     if ((center_x == 0) || (center_y == 0) ||
	 (radius_x <= 0) || (radius_y <= 0))
	  /* Can't draw anything -- no room */
	  return;
     
     /* Draw meter shape */

     /* Big circle */
     
     FastFillCircle(XtDisplay(w), XtWindow(w), w->tachometer.circle_GC,
		    center_x, center_y, radius_x, radius_y);

     /* Inner circle same color as the background */
     
     FastFillCircle(XtDisplay(w), XtWindow(w), w->tachometer.background_GC,
		    center_x, center_y, (Cardinal) (radius_x * 0.95),
		    (Cardinal) (radius_y * 0.95));

     /* Small circle */
	  
     FastFillCircle(XtDisplay(w), XtWindow(w), w->tachometer.circle_GC,
		    center_x, center_y, (Cardinal) (radius_x * 0.1),
		    (Cardinal) (radius_y * 0.1));
	  
     /* Draw the details */

     DrawGauge(w);
     DrawNeedle(w, w->tachometer.value);
}



static void FastFillCircle(d, w, gc, center_x, center_y, radius_x, radius_y)
Display		*d;
Drawable	w;
GC              gc;
Cardinal        center_x;
Cardinal        center_y;
Cardinal        radius_x;
Cardinal        radius_y;
{
     XPoint          points[360];
     Cardinal        angle;

     for (angle = 0; angle < 360; angle++) {
	  points[angle].x = (short) (sin((double) angle * PI / 180.0) *
				     (double) radius_x + (double) center_x);
	  points[angle].y = (short) (cos((double) angle * PI / 180.0) *
				     (double) radius_y + (double) center_y);
     }

     XFillPolygon(d, w, gc, points, 360, Complex,
		  CoordModeOrigin);

}




static void DrawGauge(w)
TachometerWidget w;
{
     XPoint	points[4];
     double	step;
     Cardinal	in_gauge_x, in_gauge_y;
     Cardinal	out_gauge_x, out_gauge_y;
     Cardinal	number_x, number_y;
     Cardinal	center_x, center_y;
     Cardinal	radius_x, radius_y;
     GC		gc;
     double     jump = 1.0;

     center_x = w->core.width / 2;
     center_y = w->core.height / 2;

     radius_x = center_x - w->tachometer.internal_border;
     radius_y = center_y - w->tachometer.internal_border;
     
     if ((center_x == 0) || (center_y == 0) || (radius_x <= 0) ||
	 (radius_y <= 0))
	  /* Can't draw anything */
	  return;

     gc = w->tachometer.scale_GC;
     
     for (step = 330.0; step >= 30.0; step -= jump) {
	  if ((Cardinal) (step) % 30 == 0) {
	       points[0].x = sin((step + 1.0) * PI / 180.0) * radius_x * 0.75
		    + center_x;
	       points[0].y = cos((step + 1.0) * PI / 180.0) * radius_y * 0.75
		    + center_y;
	       points[1].x = sin((step - 1.0) * PI / 180.0) * radius_x * 0.75
		    + center_x;
	       points[1].y = cos((step - 1.0) * PI / 180.0) * radius_y * 0.75
		    + center_y;
	       points[2].x = sin((step - 1.0) * PI / 180.0) * radius_x * 0.85
		    + center_x;
	       points[2].y = cos((step - 1.0) * PI / 180.0) * radius_y * 0.85
		    + center_y;
	       points[3].x = sin((step + 1.0) * PI / 180.0) * radius_x * 0.85
		    + center_x;
	       points[3].y = cos((step + 1.0) * PI / 180.0) * radius_y * 0.85
		    + center_y;

	       XFillPolygon(XtDisplay(w), XtWindow(w), gc, points, 4,
			    Complex, CoordModeOrigin);

	       number_x = sin((step + 1.0) * PI / 180.0) * radius_x * 0.65
		    + center_x;
	       number_y = cos((step + 1.0) * PI / 180.0) * radius_y * 0.65
		    + center_y;

	       if ((int)((330.0 - step) / 30.0) == 1)
		   jump = 3.0;

	       DrawNumbers(w, (unsigned char) ((330.0 - step) / 30.0),
			   number_x, number_y);

	  } else {
	       in_gauge_x = sin(step * PI / 180.0) * radius_x * 0.8 + center_x;
	       in_gauge_y = cos(step * PI / 180.0) * radius_y * 0.8 + center_y;
	       out_gauge_x = sin(step * PI / 180.0) * radius_x * 0.85
		    + center_x;
	       out_gauge_y = cos(step * PI / 180.0) * radius_y * 0.85
		    + center_y;
	       
	       XDrawLine(XtDisplay(w), XtWindow(w), gc, in_gauge_x,
			 in_gauge_y, out_gauge_x, out_gauge_y);
	  }
     }

     DrawLabelString(w);
}

static void DrawNeedle(w, load)
TachometerWidget w;
int load;
{
     XPoint	points[6];
     double	cur_theta1, cur_theta2, cur_theta3;
     double	cur_theta4, cur_theta5;
     Cardinal	center_x, center_y;
     Cardinal	radius_x, radius_y;
     GC		gc;
     
     center_x = w->core.width / 2;
     center_y = w->core.height / 2;
     
     radius_x = center_x - w->tachometer.internal_border;
     radius_y = center_y - w->tachometer.internal_border;

     if ((center_x == 0) || (center_y == 0) || (radius_x <= 0) ||
	 (radius_y <= 0))
	  /* can't draw anything */
	  return;

     gc = w->tachometer.needle_GC;
     
     cur_theta1 = (double) (330 - (load * 3)) * PI / 180.0;
     cur_theta2 = (double) (330 - (load * 3) + 1) * PI / 180.0;
     cur_theta3 = (double) (330 - (load * 3) - 1) * PI / 180.0;
     cur_theta4 = (330.0 - ((double) load * 3.0) + 7.0) * PI / 180.0;
     cur_theta5 = (330.0 - ((double) load * 3.0) - 7.0) * PI / 180.0;

     points[0].x = sin(cur_theta1) * radius_x * 0.75 + center_x;
     points[0].y = cos(cur_theta1) * radius_y * 0.75 + center_y;
     points[1].x = sin(cur_theta2) * radius_x * 0.7 + center_x;
     points[1].y = cos(cur_theta2) * radius_y * 0.7 + center_y;
     points[2].x = sin(cur_theta4) * radius_x * 0.1 + center_x;
     points[2].y = cos(cur_theta4) * radius_y * 0.1 + center_y;
     points[3].x = sin(cur_theta5) * radius_x * 0.1 + center_x;
     points[3].y = cos(cur_theta5) * radius_y * 0.1 + center_y;
     points[4].x = sin(cur_theta3) * radius_x * 0.7 + center_x;
     points[4].y = cos(cur_theta3) * radius_y * 0.7 + center_y;
     points[5].x = points[0].x;
     points[5].y = points[0].y;

     XDrawLines(XtDisplay(w), XtWindow(w), gc, points, 6, CoordModeOrigin);
}


static void DrawNumbers(w, which, x, y)
TachometerWidget          w;
unsigned char   which;
Cardinal        x, y;
{
     /* Draw Numbers	 */

     if (which == 10) {
	  DrawSingleNumber(w, 1, (Cardinal) ((double) x * 0.9), y);
	  DrawSingleNumber(w, 0, x, y);
     } else
	  DrawSingleNumber(w, which, x, y);
}



static void DrawSingleNumber(w, which, x, y)
TachometerWidget          w;
Cardinal        x, y;
{

     XSegment        segments[7];
     Cardinal        nsegments;
     unsigned char   count;
     Cardinal		width, height;
     GC 	gc;
     
     width = (w->core.width / 2) - w->tachometer.internal_border;
     height = (w->core.height / 2) - w->tachometer.internal_border;

     if ((width <= 0) || (height <= 0))
	  return;

     gc = w->tachometer.scale_GC;
     
     for (count = 0, nsegments = 0; count < 7; count++) {
	  if (num_segment[which].digit[count] == 1) {
	       segments[nsegments].x1 = (short)
		    (x + ((double) offset[count].x1 *
			  ((double) width / 200.0)));
	       segments[nsegments].y1 = (short)
		    (y + ((double) offset[count].y1 *
			  ((double) height / 200.0)));
	       segments[nsegments].x2 = (short)
		    (x + ((double) offset[count].x2 *
			  ((double) width / 200.0)));
	       segments[nsegments].y2 = (short)
		    (y + ((double) offset[count].y2 *
			  ((double) height / 200.0)));
	       nsegments++;
	  }
     }

     XDrawSegments(XtDisplay(w), XtWindow(w), gc, segments, nsegments);

}



static void DrawLabelString(w)
TachometerWidget          w;
{
     XPoint          points[5];
     Cardinal        ry;
     unsigned char   char_count;
     unsigned char   data_count;
     Cardinal	center_x, center_y;
     Cardinal	radius_x, radius_y;
     GC gc;

     gc = w->tachometer.scale_GC;

     center_x = w->core.width / 2;
     center_y = w->core.height / 2;
     radius_x = center_x - w->tachometer.internal_border;
     radius_y = center_y - w->tachometer.internal_border;
     
     if (! (center_x && center_y && (radius_x > 0) && (radius_y > 0)))
	  return;
     
     ry = (double)  radius_y * 0.35 + center_y;

     for (char_count = 0; char_count < 4; char_count++) {
	  for (data_count = 0; data_count < char_data[char_count].nofline
	       ; data_count++) {
	       points[data_count].x = (double)
		    (char_data[char_count].point_list[data_count].x) *
			 (double) radius_x * 0.01 + center_x;
	       points[data_count].y = (double)
		    (char_data[char_count].point_list[data_count].y) *
			 (double) radius_y * 0.01 + ry;
	  }
	  XDrawLines(XtDisplay(w), XtWindow(w), gc, points,
		     char_data[char_count].nofline, CoordModeOrigin);
     }
}



static void MoveNeedle(w, new)
TachometerWidget w;
int new;
{
     int step;
     int old, loop;

     old = w->tachometer.value;
     if (new > 100)
	  new = 100;
     
     if (old == new)
	  return;
     else if (old < new)
	  step = (w->tachometer.speed ? w->tachometer.speed : new - old);
     else
	  step = (w->tachometer.speed ? - w->tachometer.speed : new - old);

     if (old < new) {
	  for (loop = old; loop < new; loop += step)
	    DrawNeedle(w, loop);
	  for (loop = old + step; loop <= new; loop += step)
	    DrawNeedle(w, loop);
     }
     else {
	  for (loop = old; loop > new; loop += step)
	    DrawNeedle(w, loop);
	  for (loop = old + step; loop >= new; loop += step)
	    DrawNeedle(w, loop);
     }
	  
     if (loop != new + step) /* The final needle wasn't printed */
	  DrawNeedle(w, new);
     
     w->tachometer.value = new;
}

static void GetneedleGC(ta)
TachometerWidget ta;
{
    XGCValues	values;

    values.background	= ta->core.background_pixel;
    values.foreground	= ta->tachometer.needle ^ ta->core.background_pixel;
    values.function = GXxor;

    ta->tachometer.needle_GC = XtGetGC(
	(Widget)ta,
	(unsigned) GCFunction | GCBackground | GCForeground,
	&values);
}

static void GetscaleGC(ta)
TachometerWidget ta;
{
    XGCValues	values;

    values.foreground	= ta->tachometer.scale;
    values.background	= ta->core.background_pixel;

    ta->tachometer.scale_GC = XtGetGC(
	(Widget)ta,
	(unsigned) GCForeground | GCBackground,
	&values);
}

static void GetcircleGC(ta)
TachometerWidget ta;
{
    XGCValues	values;

    values.foreground	= ta->tachometer.circle;
    values.background	= ta->core.background_pixel;

    ta->tachometer.circle_GC = XtGetGC(
	(Widget)ta,
	(unsigned) GCForeground | GCBackground,
	&values);
}


static void GetbackgroundGC(ta)
TachometerWidget ta;
{
    XGCValues	values;

    values.foreground	= ta->core.background_pixel;
    values.background	= ta->core.background_pixel;

    ta->tachometer.background_GC = XtGetGC(
	(Widget)ta,
	(unsigned) GCForeground | GCBackground,
	&values);
}

/* ARGSUSED */
static void Initialize(request, new)
 Widget request, new;
{
    TachometerWidget ta = (TachometerWidget) new;
    
    GetneedleGC(ta);
    GetcircleGC(ta);
    GetscaleGC(ta);
    GetbackgroundGC(ta);
    ta->tachometer.width = ta->tachometer.height = 0;
} /* Initialize */


static void Realize(w, valueMask, attributes)
    register Widget w;
    Mask *valueMask;
    XSetWindowAttributes *attributes;
{
    *valueMask |= CWBitGravity;
    attributes->bit_gravity = NorthWestGravity;
    (*superclass->core_class.realize) (w, valueMask, attributes);

} /* Realize */



/*
 * Repaint the widget window
 */

/* ARGSUSED */
static void Redisplay(w, event, region)
    Widget w;
    XEvent *event;
    Region region;
{
     TachometerWidget ta = (TachometerWidget) w;

     if (event->xexpose.count == 0)
	  DrawTachometer(ta);
}

static void Resize(w)
    Widget w;
{
     TachometerWidget ta = (TachometerWidget) w;

     if ((ta->core.width == ta->tachometer.width) &&
	 (ta->core.height == ta->tachometer.height))
	  /* What resize?  We don't see a resize! */
	  return;

     XClearWindow(XtDisplay(w), XtWindow(w));
     
     if ((ta->core.width <= ta->tachometer.width) &&
	 (ta->core.height <= ta->tachometer.height))
	  /* Only redraw here if no expose events are going to be */
	  /* generated, i.e. if the window has not grown horizontally */
	  /* or vertically. */
	  DrawTachometer(ta);

     ta->tachometer.width = ta->core.width;
     ta->tachometer.height = ta->core.height;
}

/*
 * Set specified arguments into widget
 */

/* ARGSUSED */
static Boolean SetValues(current, request, new)
    Widget current, request, new;
{
     Boolean back;
     Boolean changed = False;
     
     TachometerWidget curta = (TachometerWidget) current;
     TachometerWidget newta = (TachometerWidget) new;

     back = (curta->core.background_pixel != newta->core.background_pixel);
     
     if (back || (curta->tachometer.needle != newta->tachometer.needle)) {
	  XtReleaseGC(new, newta->tachometer.needle_GC);
	  GetneedleGC(newta);
	  changed = True;
     }
     if (back || (curta->tachometer.scale != newta->tachometer.scale)) {
	  XtReleaseGC(new, newta->tachometer.scale_GC);
	  GetscaleGC(newta);
	  changed = True;
     }
     if (back || (curta->tachometer.circle != newta->tachometer.circle)) {
	  XtReleaseGC(new, newta->tachometer.circle_GC);
	  GetcircleGC(newta);
	  changed = True;
     }
     if (back) {
	  XtReleaseGC(new, newta->tachometer.background_GC);
	  GetbackgroundGC(newta);
	  changed = True;
     }

     if (curta->tachometer.value != newta->tachometer.value) {
	  MoveNeedle(newta, newta->tachometer.value);
	  changed = True;
     }

     return(changed);
}

static void Destroy(w)
    Widget w;
{
     TachometerWidget ta = (TachometerWidget) w;
     
    XtReleaseGC( w, ta->tachometer.needle_GC );
    XtReleaseGC( w, ta->tachometer.circle_GC );
    XtReleaseGC( w, ta->tachometer.scale_GC );
    XtReleaseGC( w, ta->tachometer.background_GC );
}



/***************************************************************
 *
 * Exported Procedures
 * 
 ***************************************************************/

int TachometerGetValue(w)
Widget w;
{
     TachometerWidget ta = (TachometerWidget) w;

     return(ta->tachometer.value);
}


int TachometerSetValue(w, i)
Widget w;
int i;
{
     int old;
     TachometerWidget ta = (TachometerWidget) w;

     old = ta->tachometer.value;

     MoveNeedle(ta, i);
     
     return(old);
}
