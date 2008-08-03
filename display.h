/* $gpsd: display.h 3486 2006-09-21 00:58:22Z ckuethe $ */
#ifndef _GPSD_DISPLAY_H_
#define _GPSD_DISPLAY_H_

void register_shell(Widget w);
void register_canvas(Widget w, GC gc);
void set_title(char *title);
void draw_graphics(struct gps_data_t *gpsdata);
void redraw(Widget w, XtPointer client_data, XtPointer call_data);
void resize(Widget w, XtPointer client_data, XtPointer call_data);

#endif /* _GPSD_DISPLAY_H_ */
