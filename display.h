/* $gpsd: display.h 3486 2006-09-21 00:58:22Z ckuethe $ */

void register_shell(Widget w);
void register_canvas(Widget w, GC gc);
void set_title(char *title);
void draw_graphics(struct gps_data_t *gpsdata);
void redraw(Widget w, XtPointer client_data, XtPointer call_data);
void resize(Widget w, XtPointer client_data, XtPointer call_data);
