#ifndef _display_h
#define _display_h

#include <X11/Intrinsic.h>
#include <Xm/Xm.h>
#include "gps.h"

void register_canvas(Widget w, GC gc);
void draw_graphics(struct gps_data_t *gpsdata);
void redraw(Widget w, XtPointer client_data, XmDrawingAreaCallbackStruct *cbs);

#if defined(__GNUC__)
#  define UNUSED __attribute__((unused)) /* Flag variable as unused */
#else /* not __GNUC__ */
#  define UNUSED
#endif

#endif /* _display_h */
