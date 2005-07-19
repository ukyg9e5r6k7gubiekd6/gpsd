# Automake description for gpsd

SUBDIRS = contrib

#
# Build stuff depending on Motif
#
if HAVE_MOTIF
MOTIF_PROGS = xgps xgpsspeed
endif

XMLTO = xmlto

if HAVE_DBUS
INCLUDES = $(DBUS_CFLAGS)
endif

bin_PROGRAMS = $(MOTIF_PROGS) sirfmon rtcmdecode gpsflash
bin_SCRIPTS = gpsprof gpsfake 
sbin_PROGRAMS = gpsd
check_PROGRAMS = bits_test gpsmm_test packet_test

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
gpsflash_SOURCES = gpsflash.c gpsflash.h sirfflash.c
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
	srecord.c \
	bits.h \
	timebase.h

libgps_la_SOURCES = $(libgps_c_sources) libgpsmm.cpp
libgps_la_LIBADD = $(LIBM) $(LIBC) $(LIBNSL) $(LIBSOCKET) $(LIBPTHREAD)

#
# Build gpsmm_test
#
gpsmm_test_SOURCES = gpsmm_test.cpp
gpsmm_test_LDADD = $(LIBC) libgps.la -lm

#
# Build bits_test tester
#
bits_test_SOURCES = bits_test.c
bits_test_LDADD = $(LIBC) libgps.la

#
# Build packets tester
#
packet_test_SOURCES = packet_test.c
packet_test_LDADD = $(LIBC) libgps.la

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

gpsflash.1: gpsflash.xml
	-$(XMLTO) man gpsflash.xml

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
	gpsflash.xml \
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
	@echo "Running splint on rtcmdecode..."
	-splint -exportlocal rtcmdecode.c
	@echo "Running splint on gpsflash..."
	-splint $(gpsflash_SOURCES)
	@echo "Running splint on bits_test test harness..."
	-splint $(bits_test_SOURCES)
	@echo "Running splint on packet_test test harness..."
	-splint $(packet_test_SOURCES)

#
# Regression tests begin here
#

# Regression-test the daemon
# This requires gcc4; the use of the math coprocessor's on-board trig functions
# apparently increases the accuracy of computation in a way that
# that affects the low-order digits of the track field in the O response.
gps-regress: gpsd
	@echo "Testing the daemon..."
	@for f in test/*.log; do gpsfake -b -p $${f} | grep -v "^GPSD,X" >test/test.chk; diff -ub $${f}.chk test/test.chk; done; rm test/test.chk

# Build the regression tests for the daeomon
gps-makeregress: gpsd
	@for f in test/*.log; do gpsfake -b -p $${f} | grep -v "^GPSD,X" >$${f}.chk; done

# Regression-test the RTCM decoder
rtcm-regress: rtcmdecode
	@echo "Testing RTCM decoding..."
	@for f in test/*.rtcm; do rtcmdecode <$${f} >test/test.chk; diff -ub $${f}.chk test/test.chk; done; rm test/test.chk

# Rebuild the RTCM regression tests
rtcm-makeregress: rtcmdecode
	@for f in test/*.rtcm; do rtcmdecode <$${f} >$${f}.chk; done

# Regression-test the packet getter
packet-regress: packet_test
	@echo "Testing detection of invalid packets..."
	@packet_test | diff -u test/packet.test.chk -

# Rebuild the packet-getter regression test
packet-makeregress: packet_test
	packet_test >test/packet.test.chk

testregress: gps-regress rtcm-regress packet-regress
	@echo "Rgressions complete."
