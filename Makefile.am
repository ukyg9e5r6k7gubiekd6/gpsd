# Automake description for gpsd
# $Id$

#SUBDIRS = contrib

XMLTO = xmlto

#
# Conditionally add programs depending on libraries that may or may not be present.
#
if HAVE_NCURSES
CURSESPROGS = cgps sirfmon
endif
if HAVE_MOTIF
MOTIF_PROGS = xgps
endif
if HAVE_XAW
XAW_PROGS = xgpsspeed
endif

if HAVE_DBUS
INCLUDES = $(DBUS_CFLAGS) $(DBUS_GLIB_CFLAGS) -DDBUS_API_SUBJECT_TO_CHANGE=1
DBUSPROGS = gpxlogger
gpxlogger_LDADD = $(DBUS_GLIB_LIBS) libgps.la  -lm
endif

if HAVE_RTCM104V2
RTCM104PROGS = rtcmdecode
RTCM104PAGES = rtcmdecode.1
endif

bin_PROGRAMS = $(DBUSPROGS) $(MOTIF_PROGS) $(XAW_PROGS) $(RTCM104PROGS) $(CURSESPROGS) gpsctl gpspipe gpsflash cgpxlogger gpsdlcdd
sbin_PROGRAMS = gpsd
check_PROGRAMS = bits_test gpsmm_test packet_test mkgmtime_test geoid_test
if HAVE_PYTHON
dist_bin_SCRIPTS = gpsprof gpsfake gpscat
python_PYTHON = gps.py gpsfake.py gpscap.py
nodist_python_PYTHON = gpspacket.so gpslib.so
PYTHONPAGES = gpsprof.1 gpsfake.1 gpscat.1
endif

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
xgpsspeed_LDADD = $(LIBM) $(LIBC) $(LIBNSL) $(LIBSOCKET) $(XAW_LIBS) $(X_LIBS) $(X_PRE_LIBS) libgps.la -lm $(LIBPTHREAD)

#
# Build cgps
#
cgps_SOURCES = cgps.c
cgps_LDADD = $(LIBM) $(LIBC) $(LIBNSL) $(LIBSOCKET) $(NCURSES_LIBS) libgps.la -lm $(LIBPTHREAD)

#
# Build cgpxlogger
#
cgpxlogger_SOURCES = cgpxlogger.c
cgpxlogger_LDADD = $(LIBC) $(LIBNSL) $(LIBSOCKET) libgps.la -lm $(LIBPTHREAD)

#
# Build gpsd
#
gpsd_c_sources = gpsd_dbus.c gpsd.c
gpsd_SOURCES = $(gpsd_c_sources) gpsd_dbus.h
gpsd_LDADD = $(DBUS_LIBS) $(LIBM) libgps.la -lm $(LIBPTHREAD)

#
# Build gpsctl
#
gpsctl_SOURCES = gpsctl.c
gpsctl_LDADD = $(LIBM) libgps.la -lm $(LIBPTHREAD)

#
# Build gpspipe
#
gpspipe_SOURCES = gpspipe.c
gpspipe_LDADD = $(DBUS_LIBS) $(LIBM) libgps.la -lm

#
# Build gpsdlcdd
#
gpsdlcdd_SOURCES = gpsdlcdd.c
gpsdlcdd_LDADD = $(LIBM) libgps.la -lm

#
# Build sirfmon
#
sirfmon_SOURCES = sirfmon.c
sirfmon_LDADD = $(LIBM) $(NCURSES_LIBS) libgps.la -lm $(LIBPTHREAD)

#
# Build rtcmdecode
#
rtcmdecode_SOURCES = rtcmdecode.c
rtcmdecode_LDADD = $(LIBM) libgps.la -lm $(LIBPTHREAD)

#
# Build gpsflash
#
gpsflash_SOURCES = gpsflash.c gpsflash.h sirfflash.c
gpsflash_LDADD = $(LIBM) libgps.la -lm

#
# Build shared libraries
#
libgps_la_LDFLAGS = -version-number 17:0:0
lib_LTLIBRARIES = libgps.la

libgpsd_c_sources = \
	netlib.c \
	nmea_parse.c \
	serial.c \
	libgpsd_core.c \
	subframe.c \
	ntpshm.c \
	libgps.c \
	packet.c \
	gpsutils.c \
	geoid.c \
	dgnss.c \
	dgpsip.c \
	ntrip.c \
	sirf.c \
	superstar2.c \
	report.c \
	isgps.c \
	rtcm2.c \
	rtcm3.c \
	srecord.c \
	bsd-base64.c \
	strl.c \
	hex.c \
	drivers.c \
	zodiac.c \
	ubx.c \
	garmin.c \
	garmin_txt.c \
	tsip.c \
	evermore.c \
	italk.c \
	navcom.c \
	bits.c \
	crc24q.c

libgpsd_h_sources = \
	italk.h \
	bsd-base64.h \
	timebase.h \
	bits.h \
	rtcm2.h \
	crc24q.h

BUILT_SOURCES = packet_names.h gpsd.h

packet_names.h: packet_states.h
	sed -e '/^ *\([A-Z][A-Z0-9_]*\),/s//   "\1",/' -e '/_states/s//_names/' < `test -f 'packet_states.h' || echo '$(srcdir)/'`packet_states.h > packet_names.h

gpsd.h: gpsd.h-head gpsd.h-tail gpsd_config.h
	cat $(srcdir)/gpsd.h-head >gpsd.h
	grep VERSION  gpsd_config.h >>gpsd.h
	grep ENABLE gpsd_config.h >>gpsd.h
	cat $(srcdir)/gpsd.h-tail >>gpsd.h

libgps_la_SOURCES = $(libgpsd_c_sources) $(libgpsd_h_sources) \
				rtcm2.h libgpsmm.cpp packet_states.h
nodist_libgps_la_SOURCES = packet_names.h
libgps_la_LIBADD = $(LIBM) $(LIBC) $(LIBNSL) $(LIBSOCKET) $(LIBPTHREAD)

#
# Build Python binding
#
# install may require --root depending on what you're doing.
# python setup.py install --root=$DESTIR --prefix=$PREFIX
if HAVE_PYTHON
PYEXTENSIONS = gpspacket.so gpslib.so
noinst_SCRIPTS = gpspacket.so gpslib.so setup.py

gpspacket.so gpslib.so: gpspacket.c gpslib.c libgps.la
	(pwd="`pwd`"; cd $(srcdir) && $(PYTHON) setup.py build_ext --build-lib "$$pwd" --build-temp "$$pwd/build" --include-dirs "$$pwd")
endif
# Clean up after Python
clean-local:
	rm -rf build

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
packet_test_LDADD = $(LIBC) libgps.la -lm

#
# Build geoid model tester
#
geoid_test_SOURCES = geoid_test.c
geoid_test_LDADD = $(LIBC) libgps.la -lm

#
# Build time functions tester
#
mkgmtime_test_SOURCES = mkgmtime_test.c
mkgmtime_test_LDADD = $(LIBC) libgps.la -lm

if HAVE_XSLT_PROCESSOR
#
# Create Manpages
#
man_MANS = \
	gpsd.8 \
	gps.1 \
	xgps.1 \
	xgpsspeed.1 \
	cgps.1 \
	cgpxlogger.1 \
	libgps.3 \
	libgpsmm.3 \
	libgpsd.3 \
	sirfmon.1 \
	$(RTCM104PAGES) \
	$(PYTHONPAGES) \
	gpsctl.1 \
	gpspipe.1 \
	gpsflash.1 \
	rtcm-104.5 \
	srec.5

.xml.1:
	$(MANGENERATOR) $(MANFLAGS) $(MANTARGET) $<

.xml.3:
	$(MANGENERATOR) $(MANFLAGS) $(MANTARGET) $<

.xml.5:
	$(MANGENERATOR) $(MANFLAGS) $(MANTARGET) $<

.xml.8:
	$(MANGENERATOR) $(MANFLAGS) $(MANTARGET) $<

gpsd.8: gpsd.xml

gps.1 xgps.1 xgpsspeed.1 cgps.1 cgpxlogger.1: gps.xml

libgps.3: libgps.xml

libgpsmm.3: libgpsmm.xml

libgpsd.3: libgpsd.xml

gpsctl.1: gpsctl.xml

gpscat.1: gpsctl.xml

gpsprof.1: gpsprof.xml

gpsfake.1: gpsfake.xml

sirfmon.1: sirfmon.xml

gpspipe.1: gpspipe.xml

rtcmdecode.1: rtcmdecode.xml

rtcm-104.5: rtcm-104.xml

srec.5: srec.xml

gpsflash.1: gpsflash.xml

endif

noinst_HEADERS = ubx.h
nodist_include_HEADERS = gpsd.h
include_HEADERS = gps.h libgpsmm.h

EXTRA_DIST = \
	autogen.sh \
	README \
	INSTALL \
	COPYING \
	TODO \
	NEWS \
	AUTHORS \
	dgpsip-servers \
	floattest.c \
	gpsd.php \
	gpsd.xml \
	gpsd.h-head \
	gpsd.h-tail \
	gps.xml \
	libgpsd.xml \
	libgps.xml \
	libgpsmm.xml \
	gpsprof.xml \
	gpsfake.xml \
	gpsctl.xml \
	sirfmon.xml \
	gpspipe.xml \
	rtcmdecode.xml \
	rtcm-104.xml \
	srec.xml \
	gpsflash.xml \
	$(man_MANS) \
	xgps.ad \
	xgpsspeed.ad \
	gpsd.spec.in \
	gpsd.spec \
	gpsd.hotplug \
	gpsd.usermap \
	valgrind-audit \
	valgrind-suppressions \
	gpspacket.c \
	gpslib.c \
	driver_proto.c \
	logextract \
	striplog \
	setup.py \
	packet_states.h \
	libgps.pc.in \
	libgpsd.pc.in \
	gpscap.ini

CLEANFILES = $(BUILT_SOURCES) floattest *.core $(PYEXTENSIONS)

pkgconfig_DATA = libgps.pc libgpsd.pc
pkgconfigdir = $(libdir)/pkgconfig

# These are not distributed
libgps: libgps.c .libs/libgps.a
	$(CC) $(CFLAGS) -o libgps -lm -DTESTMAIN $(LIBPTHREAD) -g libgps.c .libs/libgps.a

# Report splint warnings
SPLINTOPTS = -I/usr/include/dbus-1.0/ +quiet
splint: gpsd.h packet_names.h
	@echo "Running splint on daemon and libraries..."
	-splint $(SPLINTOPTS) -exportlocal -redef $(gpsd_c_sources) $(libgpsd_c_sources)
	@echo "Running splint on xgps..."
	-splint $(SPLINTOPTS) -exportlocal $(xgps_SOURCES)
	@echo "Running splint on cgps..."
	-splint $(SPLINTOPTS) -exportlocal $(cgps_SOURCES)
	@echo "Running splint on xgpsspeed..."
	-splint $(SPLINTOPTS) -exportlocal $(xgpsspeed_c_sources)
	@echo "Running splint on gpsctl..."
	-splint $(SPLINTOPTS) $(gpsctl_SOURCES)
	@echo "Running splint on sirfmon..."
	-splint $(SPLINTOPTS) $(sirfmon_SOURCES)
	@echo "Running splint on gpspipe..."
	-splint $(SPLINTOPTS) $(gpspipe_SOURCES)
	@echo "Running splint on rtcmdecode..."
	-splint $(SPLINTOPTS) -exportlocal rtcmdecode.c
	@echo "Running splint on gpsflash..."
	-splint $(SPLINTOPTS) $(gpsflash_SOURCES)
	@echo "Running splint on bits_test test harness..."
	-splint $(SPLINTOPTS) $(bits_test_SOURCES)
	@echo "Running splint on packet_test test harness..."
	-splint $(SPLINTOPTS) $(packet_test_SOURCES)
	@echo "Running splint on geoid_test test harness..."
	-splint $(SPLINTOPTS) $(geoid_test_SOURCES)

# Bogon-checking the Python tools
pychecker:
	for f in gps.py gpsfake.py leapsecond.py; do pychecker $$f; done

version:
	@echo $(VERSION)

#
# Regression tests begin here
#
# Note that the *-makeregress targets re-create the *.log.chk source
# files from the *.log source files.
#
# These require gcc4; use of the math coprocessor's on-board trig functions
# apparently increases the accuracy of computation in a way that affects
# the low-order digits of the track field in the O response.

# Regression-test the daemon
gps-regress: gpsd 
	$(srcdir)/regress-driver -t $(srcdir)/test/stable/*.log

# Test that super-raw mode works. Compare each logfile against itself 
# dumped through the daemon running in R=2 mode.  (This test is not
# included in the normal regressions.)
raw-regress:
	$(srcdir)/regress-driver -r  $(srcdir)/test/stable/*.log

# Build the regression tests for the daemon.
gps-makeregress: gpsd
	$(srcdir)/regress-driver -b $(srcdir)/test/stable/*.log

# To build an individual test for a load named foo.log, put it in
# test/stable and do this:
#	regress-driver -b test/stable/foo.log

# Regression-test the RTCM decoder.
rtcm-regress: rtcmdecode
	@echo "Testing RTCM decoding..."
	@mkdir -p test
	@for f in $(srcdir)/test/*.rtcm2; do \
		echo "Testing $${f}..."; \
		$(srcdir)/rtcmdecode <$${f} >test/test.chk; \
		diff -ub $${f}.chk test/test.chk; \
	done; \
	rm test/test.chk

# Rebuild the RTCM regression tests.
rtcm-makeregress: rtcmdecode
	@for f in $(srcdir)/test/*.rtcm2; do \
		$(srcdir)/rtcmdecode < $${f} > $${f}.chk; \
	done

# Regression-test the packet getter.
packet-regress: packet_test
	@echo "Testing detection of invalid packets..."
	@$(srcdir)/packet_test | diff -u $(srcdir)/test/packet.test.chk -

# Rebuild the packet-getter regression test
packet-makeregress: packet_test
	@mkdir -p test
	$(srcdir)/packet_test >test/packet.test.chk

# Regression-test the geoid tester.
geoid-regress: geoid_test
	@echo "Testing the geoid model..."
	@$(srcdir)/geoid_test 37.371192 122.014965 | diff -u $(srcdir)/test/geoid.test.chk -

# Rebuild the packet-getter regression test
geoid-makeregress: geoid_test
	@mkdir -p test
	$(srcdir)/geoid_test 37.371192 122.014965 >test/geoid.test.chk

# Test RTCM encoding using passthrough mode.
undump-regress: rtcmdecode
	@echo "Testing RTCM encoding..."
	@mkdir -p test
	@$(srcdir)/rtcmdecode -p < $(srcdir)/test/undump.sample > test/undump.test
	@diff -u $(srcdir)/test/undump.sample test/undump.test
	@rm test/undump.test

# Regression-test the calendar functions
time_regress: mkgmtime_test
	./mkgmtime_test

# Do all normal regression tests
testregress: gps-regress rtcm-regress packet-regress undump-regress time_regress
	@echo "Regressions complete."

# We would like "${MAKE} check" to run the regression tests.  automake
# appears to have support for running programs, but not make targets.
# Perhaps the tests should be moved into scripts, but then it's not
# clear how to deal with $(srcdir).
# TESTS=testregress

#
# Productions for testing unstable drivers.
#

unstable-regress: gpsd 
	./regress-driver -t $(srcdir)/test/unstable/*.log

unstable-makeregress: gpsd
	./regress-driver -b $(srcdir)/test/unstable/*.log


# Release machinery begins here
#
# This is how to ship a release to Berlios incomoing.
# It requires developer access verified via ssh.
#
upload-ftp: dist
	shasum gpsd-$(VERSION).tar.gz >gpsd.sum
	lftp -c "open ftp://ftp.berlios.de/incoming; mput gpsd-$(VERSION).tar.gz gpsd.sum"

#
# This is how to tag a release for the SVN repository.
# It requires developer access verified via ssh.
#
REPO=https://svn.berlios.de/svnroot/repos/gpsd
svn-tag:
	svn copy $(REPO)/trunk $(REPO)/tags/release-$(VERSION) \
		-m "Tagged for external release $(VERSION)"
#
# Ship a release, providing all regression tests pass.
#
ship: testregress dist upload-ftp svn-tag
