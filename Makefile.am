# Automake descrotion for gpsd

#
# Build stuff depending on Motif
#
if HAVE_MOTIF
BUILD_PROGS = xgps xgpsspeed
endif

XMLTO = xmlto

bin_PROGRAMS = $(BUILD_PROGS)
bin_SCRIPTS = gpsprobe gpsprof gpsfake

#
# Build xgps
#
xgps_SOURCES = display.c display.h xgps.c
xgps_CFLAGS = $(X_CFLAGS)
xgps_LDADD = $(LIBM) $(LIBC) $(LIBNSL) $(LIBSOCKET) $(XM_LIBS) $(XT_LIBS) $(X_LIBS) $(X_PRE_LIBS) libgps.la -lm

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
xgpsspeed_LDADD = $(LIBM) $(LIBC) $(LIBNSL) $(LIBSOCKET) $(XAW_LIBS) $(XM_LIBS) $(X_LIBS) $(X_PRE_LIBS) libgps.la -lm


#
# Build gpsd
#
sbin_PROGRAMS = gpsd
gpsd_SOURCES = gpsd.c
gpsd_LDADD = $(LIBM) libgps.la -lm


#
# Build shared library
#
libgps_la_LDFLAGS = -version-number 8:0:0
lib_LTLIBRARIES = libgps.la

libgps_la_SOURCES = \
	netlib.c \
	nmea_parse.c \
	serial.c \
	drivers.c \
	zodiac.c \
	garmin.c \
	libgpsd_core.c \
	ntpshm.c \
	libgps.c \
	packet.c \
	gpsutils.c \
	sirf.c \
	report.c

libgps_la_LIBADD = $(LIBM) $(LIBC) $(LIBNSL) $(LIBSOCKET)

#
# Create Manpages
#
man_MANS = \
	gpsd.8 \
	xgps.1 \
	libgps.3 \
	libgpsd.3 \
	gpsprobe.1 \
	gpsprof.1 \
	gpsfake.1

gpsd.8: gpsd.xml
	-$(XMLTO) man gpsd.xml

xgps.1 xgpsspeed.1: gpsd.xml
	-$(XMLTO) man xgps.xml

libgps.3: libgps.xml
	-$(XMLTO) man libgps.xml

libgpsd.3: libgpsd.xml
	-$(XMLTO) man libgpsd.xml

gpsprobe.1: gpsprobe.xml
	-$(XMLTO) man gpsprobe.xml

gpsprof.1: gpsprof.xml
	-$(XMLTO) man gpsprof.xml

gpsfake.1: gpsfake.xml
	-$(XMLTO) man gpsfake.xml

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

include_HEADERS = gps.h gpsd.h

# Automake is buggy and doesn't automatically include bin_SCRIPTS in dist
EXTRA_DIST = \
	autogen.sh \
	README \
	INSTALL \
	COPYING \
	HACKING \
	TODO \
	AUTHORS \
	gpsd.xml \
	libgpsd.xml \
	libgps.xml \
	gpsprobe.xml \
	gpsprof.xml \
	gpsfake.xml \
	$(man_MANS) \
	xgps.ad \
	xgpsspeed.ad \
	gpsd.init \
	gpsd.spec \
	gps.py \
	gpsd.py \
	gpsprobe \
	gpsprof \
	gpsfake \
	sirfmon.c \
	gpsd.hotplug \
	gpsd.usermap \
	logextract

# These are not distributed
libgps: libgps.c .libs/libgps.a
	$(CC) -o libgps -DTESTMAIN libgps.c .libs/libgps.a
sirfmon: sirfmon.c
	$(CC) -g -O sirfmon.c -lm -lncurses -o sirfmon
