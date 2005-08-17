#if HAVE_DBUS
DBUS_PROGS = gpxlogger
#endif

check_PROGRAMS = $(DBUS_PROGS)

INCLUDES = \
       $(DBUS_GLIB_CFLAGS) -DDBUS_API_SUBJECT_TO_CHANGE     \
       $(GLIB_CFLAGS)

gpxlogger_LDADD = \
       $(DBUS_GLIB_LIBS) \
       $(GLIB_LIBS)

gpxlogger_SOURCES = \
       gpxlogger.c
