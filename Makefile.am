# Copyright (C) 2004 Jens Oberender <j.obi at troja.net>

#
# Build gps
#
bin_PROGRAMS = gps xgpsspeed

gps_SOURCES = \
	display.c \
	gps.c \
	version.h

gps_CFLAGS = $(X_CFLAGS)
gps_LDADD = $(LIBM) $(LIBC) $(LIBNSL) $(LIBSOCKET) $(XM_LIBS) $(X_LIBS) $(X_PRE_LIBS) libgps.a


#
# Build xgpsspeed
#
xgpsspeed_SOURCES = \
	xgpsspeed.c \
	Tachometer.c \
	TachometerP.h \
	Tachometer.h \
	xgpsspeed.icon

xgpsspeed_CFLAGS = $(X_CFLAGS)
xgpsspeed_LDADD = $(LIBM) $(LIBC) $(LIBNSL) $(LIBSOCKET) $(XAW_LIBS) $(XM_LIBS) $(X_LIBS) $(X_PRE_LIBS) libgps.a


#
# Build gpsd
#
sbin_PROGRAMS = gpsd

gpsd_SOURCES = \
	gpsd.c \
	version.h

gpsd_LDADD = $(LIBM) libgps.a


#
# Build shared libgps
#
lib_LIBRARIES = libgps.a

libgps_a_SOURCES = \
	netlib.c \
	nmea_parse.c \
	serial.c \
	drivers.c \
	earthmate.c \
	libgpsd_core.c \
	libgps.c

libgps_a_LIBADD = $(LIBM) $(LIBC) $(LIBNSL) $(LIBSOCKET)

#
# Create Manpages
#
man_MANS = \
	gpsd.1 \
	libgps.3 \
	libgpsd.3

gpsd.1 gps.1 xgpsspeed.1: gpsd.xml
	xmlto man gpsd.xml

libgps.3: libgps.xml
	xmlto man libgps.xml

libgpsd.3: libgpsd.xml
	xmlto man libgpsd.xml

#
# Do a release with a tar.gz and a tar.bz2
#
release: distdir
	-chmod -R a+r $(distdir)
	GZIP=$(GZIP_ENV) $(AMTAR)$(TAR) chozf $(distdir).tar.gz $(distdir)
	rm -f $(distdir)/gpsd.spec
	sed '/^Source:/s/\.tar\.gz$$/\.tar\.bz2/' gpsd.spec \
		> $(distdir)/gpsd.spec
	BZIP2=$(BZIP2_ENV) $(AMTAR)$(TAR) --bzip2 -chof $(distdir).tar.bz2 $(distdir)
	-rm -rf $(distdir)

include_HEADERS = \
	gps.h \
	gpsd.h

EXTRA_DIST = \
	README \
	INSTALL \
	COPYING \
	HACKING \
	TODO \
	gpsd.xml \
	libgpsd.xml \
	libgps.xml \
	$(man_MANS) \
	gps.ad \
	xgpsspeed.ad \
	gpsd.init \
	gpsd.spec \
	gpsd.lsm
