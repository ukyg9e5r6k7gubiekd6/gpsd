# Automake description for gpsd

SUBDIRS = contrib

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

bin_PROGRAMS = $(BUILD_PROGS) sirfmon rtcmdecode
bin_SCRIPTS = gpsprof gpsfake 
sbin_PROGRAMS = gpsd
check_PROGRAMS = gpsmm_test bits gpsflash

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
# Build gpsd
#
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
# Build rtcmdecode
#
rtcmdecode_SOURCES = rtcmdecode.c
rtcmdecode_LDADD = $(LIBM) -lncurses libgps.la -lm $(LIBPTHREAD)

#
# Build gpsflash
#
gpsflash_SOURCES = gpsflash.c gpsflash.h utils.c
gpsflash_LDADD = $(LIBM) libgps.la -lm

#
# Build shared libraries
#
libgps_la_LDFLAGS = -version-number 15:0:0
lib_LTLIBRARIES = libgps.la

libgps_c_sources = \
	netlib.c \
	nmea_parse.c \
	serial.c \
	drivers.c \
	zodiac.c \
	garmin.c \
	tsip.c \
	evermore.c \
	italk.c \
	libgpsd_core.c \
	ntpshm.c \
	libgps.c \
	packet.c \
	gpsutils.c \
	geoid.c \
	dgpsip.c \
	sirf.c \
	report.c \
	rtcm.c \
	rtcm.h \
	srecord.c \
	bits.h \
	timebase.h

libgps_la_SOURCES = $(libgps_c_sources) libgpsmm.cpp
libgps_la_LIBADD = $(LIBM) $(LIBC) $(LIBNSL) $(LIBSOCKET) $(LIBPTHREAD)

#
# Build gpsmm_test
#
gpsmm_test_SOURCES = \
	gpsmm_test.cpp
gpsmm_test_LDADD = $(LIBM) $(LIBC) $(LIBNSL) $(LIBSOCKET) $(XAW_LIBS) $(XM_LIBS) $(X_LIBS) $(X_PRE_LIBS) libgps.la -lm

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
	sirfmon.1 \
	rtcmdecode.1 \
	rtcm.5 \
	srec.5

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

rtcmdecode.1: rtcmdecode.xml
	-$(XMLTO) man rtcmdecode.xml

rtcm.5: rtcm.xml
	-$(XMLTO) man rtcm.xml

srec.5: srec.xml
	-$(XMLTO) man srec.xml

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

include_HEADERS = gps.h gpsd.h rtcm.h libgpsmm.h 

# Automake is buggy and doesn't automatically include bin_SCRIPTS in dist
EXTRA_DIST = \
	autogen.sh \
	README \
	INSTALL \
	COPYING \
	HACKING \
	TODO \
	AUTHORS \
	dgpsip-servers \
	gpsd.xml \
	xgps.xml \
	libgpsd.xml \
	libgps.xml \
	libgpsmm.xml \
	gpsprof.xml \
	gpsfake.xml \
	sirfmon.xml \
	rtcmdecode.xml \
	rtcm.xml \
	srec.xml \
	$(man_MANS) \
	xgps.ad \
	xgpsspeed.ad \
	gpsd.spec.in \
	gpsd.spec \
	gps.py \
	gpsfake.py \
	gpsprof \
	gpsfake \
	gpsd.hotplug \
	gpsd.usermap \
	valgrind-audit \
	valgrind-suppressions \
	driver_proto.c \
	logextract \
	statetable.el

# This is not distributed
libgps: libgps.c .libs/libgps.a
	$(CC) -o libgps -lm -DTESTMAIN -g libgps.c .libs/libgps.a

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
	@echo "Running splint on bits test harness..."
	-splint bits.c @echo "Running splint on rtcmdecode..."
	-splint rtcmdecode.c

#
# Regression tests begin here
#

# Regression-test the daemon
# This requires gcc4; there's been some change in the math libraries since
# gcc3 that affects the low-order digits of the track field in the O response.
gps-regress:
	@for f in test/*.log; do gpsfake -b -p $${f} | grep -v "^GPSD,X" >test/test.chk; diff -ub $${f}.chk test/test.chk; done; rm test/test.chk

# Build the regression tests for the daeomon
gps-makeregress:
	@for f in test/*.log; do gpsfake -b -p $${f} | grep -v "^GPSD,X" >$${f}.chk; done

# Regression-test the RTCM decoder
rtcm-regress:
	@for f in test/*.rtcm; do rtcmdecode <$${f} >test/test.chk; diff -ub $${f}.chk test/test.chk; done; rm test/test.chk

# Rebuild the RTCM regression tests
rtcm-makeregress:
	@for f in test/*.rtcm; do rtcmdecode <$${f} >$${f}.chk; done

testregress: gps-regress rtcm-regress
