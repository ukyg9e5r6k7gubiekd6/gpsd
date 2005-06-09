# Automake description for gpsd

#
# Build stuff depending on Motif
#
if HAVE_MOTIF
BUILD_PROGS = xgps xgpsspeed
endif

XMLTO = xmlto

if HAVE_DBUS
INCLUDES = $(DBUS_CFLAGS)
endif

bin_PROGRAMS = $(BUILD_PROGS) sirfmon 
check_PROGRAMS = gpsmm_test bits
bin_SCRIPTS = gpsprof gpsfake 

#
# Build xgps
#
xgps_SOURCES = display.c display.h xgps.c
xgps_LDADD = $(LIBM) $(LIBC) $(LIBNSL) $(LIBSOCKET) $(XM_LIBS) $(XT_LIBS) $(X_LIBS) $(X_PRE_LIBS) libgps.la -lm $(LIBPTHREAD)

#
# Build xgpsspeed
#
xgpsspeed_c_sources = \
	xgpsspeed.c \
	Tachometer.c \
	TachometerP.h \
	Tachometer.h
xgpsspeed_SOURCES = \
	$(xgpsspeed_c_sources) \
	xgpsspeed.icon
xgpsspeed_LDADD = $(LIBM) $(LIBC) $(LIBNSL) $(LIBSOCKET) $(XAW_LIBS) $(XM_LIBS) $(X_LIBS) $(X_PRE_LIBS) libgps.la -lm $(LIBPTHREAD)

#
# Build gpsmm_test
#
gpsmm_test_SOURCES = \
	gpsmm_test.cpp
gpsmm_test_LDADD = $(LIBM) $(LIBC) $(LIBNSL) $(LIBSOCKET) $(XAW_LIBS) $(XM_LIBS) $(X_LIBS) $(X_PRE_LIBS) libgps.la -lm

#
# Build gpsd
#
sbin_PROGRAMS = gpsd
gpsd_SOURCES = \
	gpsd_dbus.h \
	gpsd_dbus.c \
	gpsd.c
gpsd_LDADD = $(DBUS_LIBS) $(LIBM) libgps.la -lm $(LIBPTHREAD)


#
# Build sirfmon
#
sirfmon_SOURCES = sirfmon.c
sirfmon_LDADD = $(LIBM) -lncurses libgps.la -lm $(LIBPTHREAD)

#
# Build shared libraries
#
libgps_la_LDFLAGS = -version-number 13:0:0
lib_LTLIBRARIES = libgps.la

libgps_c_sources = \
	netlib.c \
	nmea_parse.c \
	serial.c \
	drivers.c \
	zodiac.c \
	garmin.c \
	tsip.c \
	libgpsd_core.c \
	ntpshm.c \
	libgps.c \
	packet.c \
	gpsutils.c \
	geoid.c \
	sirf.c \
	report.c \
	gpsutils.h \
	timebase.h

libgps_la_SOURCES = $(libgps_c_sources) libgpsmm.cpp
libgps_la_LIBADD = $(LIBM) $(LIBC) $(LIBNSL) $(LIBSOCKET) $(LIBPTHREAD)

#
# Create Manpages
#
man_MANS = \
	gpsd.8 \
	xgps.1 \
	libgps.3 \
	libgpsmm.3 \
	libgpsd.3 \
	gpsprof.1 \
	gpsfake.1 \
	sirfmon.1

gpsd.8: gpsd.xml
	-$(XMLTO) man gpsd.xml

xgps.1 xgpsspeed.1: xgps.xml
	-$(XMLTO) man xgps.xml

libgps.3: libgps.xml
	-$(XMLTO) man libgps.xml

libgpsmm.3: libgpsmm.xml
	-$(XMLTO) man libgpsmm.xml

libgpsd.3: libgpsd.xml
	-$(XMLTO) man libgpsd.xml

gpsprof.1: gpsprof.xml
	-$(XMLTO) man gpsprof.xml

gpsfake.1: gpsfake.xml
	-$(XMLTO) man gpsfake.xml

sirfmon.1: sirfmon.xml
	-$(XMLTO) man sirfmon.xml

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

include_HEADERS = gps.h gpsd.h libgpsmm.h 

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
	xgps.xml \
	libgpsd.xml \
	libgps.xml \
	libgpsmm.xml \
	gpsprof.xml \
	gpsfake.xml \
	sirfmon.xml \
	$(man_MANS) \
	xgps.ad \
	xgpsspeed.ad \
	gpsd.spec.in \
	gpsd.spec \
	gps.py \
	gpsprof \
	gpsfake \
	gpsd.hotplug \
	gpsd.usermap \
	logextract

# This is not distributed
libgps: libgps.c .libs/libgps.a
	$(CC) -o libgps -lm -DTESTMAIN -g libgps.c .libs/libgps.a

# Regression-test the daemon
testregress:
	@for f in test/*.log; do gpsfake -p $${f} | grep -v "^GPSD,X" >test/test.chk; diff -c $${f}.chk test/test.chk; done; rm test/test.chk

# Build the regression tests
makeregress:
	@for f in test/*.log; do gpsfake -p $${f} | grep -v "^GPSD,X" >$${f}.chk; done

# Report splint warnings
splint:
	@echo "Running splint on daemon and libraries..."
	-splint -redef $(gpsd_SOURCES) $(libgps_c_sources)
	@echo "Running splint on xgps..."
	-splint -exportlocal $(xgps_SOURCES)
	@echo "Running splint on xgpsspeed..."
	-splint -exportlocal $(xgpsspeed_c_sources)
	@echo "Running splint on sirfmon..."
	-splint $(sirfmon_SOURCES)
