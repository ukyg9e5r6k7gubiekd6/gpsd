#ifndef _display_h
#define _display_h

#include <X11/Intrinsic.h>
#include <Xm/Xm.h>

void register_canvas(Widget w, GC gc);
void draw_graphics(struct gps_data_t *gpsdata);
void redraw(Widget w, XtPointer client_data, XmDrawingAreaCallbackStruct *cbs);

#endif /* _display_h */
