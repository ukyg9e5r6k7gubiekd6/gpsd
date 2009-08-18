# Automake description for gpsd
# $Id$

CLEANFILES =

# For a detailed explanation of what this ugly code is doing, see
# http://www.gnu.org/software/automake/manual/automake.html#Multiple-Outputs
MULTIOUT_RECOVER_DELETED = \
	if test -f '$@'; then :; else \
	  trap "rm -rf '$$WITNESS.lock' '$$WITNESS'" HUP INT PIPE TERM; \
	  if mkdir "$$WITNESS.lock" 2>/dev/null; then \
	    rm -f "$$WITNESS"; \
	    $(MAKE) $(AM_MAKEFLAGS) "$$WITNESS"; \
	    result=$$?; rm -rf "$$WITNESS.lock"; exit $$result; \
	  else \
	    while test -d "$$WITNESS.lock"; do sleep 1; done; \
	    test -f "$$WITNESS"; \
	  fi; \
	fi

#SUBDIRS = contrib

XMLTO = xmlto

#
# Conditionally add programs depending on libraries that may or may not be present.
#
if HAVE_NCURSES
CURSESPROGS = cgps gpsmon
endif
if HAVE_MOTIF
MOTIF_PROGS = xgps
endif
if HAVE_XAW
XAW_PROGS = xgpsspeed
endif

if HAVE_DBUS
INCLUDES = $(DBUS_CFLAGS) $(DBUS_GLIB_CFLAGS) -DDBUS_API_SUBJECT_TO_CHANGE=1
endif

RTCM104PAGES_DIST = gpsdecode.1
if HAVE_RTCM104V2
RTCM104PROGS = gpsdecode
RTCM104PAGES = $(RTCM104PAGES_DIST)
endif

bin_PROGRAMS = $(MOTIF_PROGS) $(XAW_PROGS) $(RTCM104PROGS) $(CURSESPROGS) gpsctl gpspipe gpxlogger lcdgps
sbin_PROGRAMS = gpsd
check_PROGRAMS = test_float test_bits test_gpsmm test_packet test_mkgmtime test_geoid test_json

PYTHONPAGES_DIST = gpsprof.1 gpsfake.1 gpscat.1
if HAVE_PYTHON
dist_bin_SCRIPTS = gpsprof gpsfake gpscat
python_PYTHON = gps.py gpsfake.py gpscap.py
nodist_python_PYTHON = gpspacket.so gpslib.so
PYTHONPAGES = $(PYTHONPAGES_DIST)
endif

#
# Build xgps
#
xgps_SOURCES = xgps.c
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
# Build gpxlogger
#
gpxlogger_SOURCES = gpxlogger.c
gpxlogger_LDADD = $(DBUS_GLIB_LIBS) libgps.la  -lm

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
# Build lcdgps
#
lcdgps_SOURCES = lcdgps.c
lcdgps_LDADD = $(LIBM) libgps.la -lm

#
# Build gpsmon
#
gpsmon_SOURCES = gpsmon.c monitor_nmea.c monitor_sirf.c \
	monitor_italk.c monitor_ubx.c monitor_superstar2.c \
	monitor_oncore.c
gpsmon_LDADD = $(LIBM) $(NCURSES_LIBS) libgps.la -lm $(LIBPTHREAD)

#
# Build gpsdecode
#
gpsdecode_SOURCES = gpsdecode.c
gpsdecode_LDADD = $(LIBM) libgps.la -lm $(LIBPTHREAD)

#
# Build shared libraries
#
libgps_la_LDFLAGS = -version-number 19:0:0
lib_LTLIBRARIES = libgps.la

libgpsd_c_sources = \
	bits.c \
	bsd-base64.c \
	crc24q.c \
	gpsd_report.c \
	gpsutils.c \
	geoid.c \
	gpsd_json.c \
	hex.c \
	isgps.c \
	json.c \
	libgps.c \
	libgps_json.c \
	gpsdclient.c \
	libgpsd_core.c \
	net_dgpsip.c \
	net_gnss_dispatch.c \
	net_ntrip.c \
	net_remotegpsd.c \
	netlib.c \
	ntpshm.c \
	packet.c \
	serial.c \
	srecord.c \
	strl.c \
	subframe.c \
	drivers.c \
	driver_aivdm.c \
	driver_evermore.c \
	driver_garmin.c \
	driver_garmin_txt.c \
	driver_italk.c \
	driver_navcom.c \
	driver_nmea.c \
	driver_oncore.c \
	driver_rtcm2.c \
	driver_rtcm3.c \
	driver_sirf.c \
	driver_superstar2.c \
	driver_tsip.c \
	driver_ubx.c \
	driver_zodiac.c

libgpsd_h_sources = \
	bsd-base64.h \
	timebase.h \
	bits.h \
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
				driver_rtcm2.h libgpsmm.cpp packet_states.h
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

# Multiple-outputs hack.  See
# http://www.gnu.org/software/automake/manual/automake.html#Multiple-Outputs
$(PYEXTENSIONS): stamp-python-modules
	+@WITNESS=stamp-python-modules; $(MULTIOUT_RECOVER_DELETED)
# TODO:  Should the dependency on libgps.la be enforced inside
# setup.py?  (See the variable 'needed_files' in setup.py.)
stamp-python-modules: gpspacket.c gpslib.c libgps.la setup.py
	@rm -f '$@' '$@.tmp'
	@echo 'timestamp for $@' > '$@.tmp'
	cd '$(srcdir)' && \
	env abs_builddir='$(abs_builddir)' \
	    MAKE='$(MAKE)' \
		$(PYTHON) setup.py build_ext \
			--build-lib '$(abs_builddir)' \
			--build-temp '$(abs_builddir)/build' \
			--include-dirs '$(abs_builddir):.'
	@mv -f '$@.tmp' '$@'
CLEANFILES += stamp-python-modules stamp-python-modules.tmp
endif
# Clean up after Python
clean-local:
	rm -rf build

TESTBINS = test_float test_gpsmm test_bits test_packet test_geoid \
	test_mkgmtime test_json

#
# Build test_float
#
test_float_SOURCES = test_float.c
test_float_LDADD = $(LIBC) libgps.la -lm

#
# Build test_gpsmm
#
test_gpsmm_SOURCES = test_gpsmm.cpp
test_gpsmm_LDADD = $(LIBC) libgps.la -lm

#
# Build test_bits tester
#
test_bits_SOURCES = test_bits.c
test_bits_LDADD = $(LIBC) libgps.la

#
# Build packets tester
#
test_packet_SOURCES = test_packet.c
test_packet_LDADD = $(LIBC) libgps.la -lm

#
# Build geoid model tester
#
test_geoid_SOURCES = test_geoid.c
test_geoid_LDADD = $(LIBC) libgps.la -lm

#
# Build time functions tester
#
test_mkgmtime_SOURCES = test_mkgmtime.c
test_mkgmtime_LDADD = $(LIBC) libgps.la -lm

#
# Build JSON parse tester
test_json_SOURCES = test_json.c
test_json_LDADD = $(LIBC) libgps.la -lm

MANPAGES_BASE = \
	gpsd.8 \
	gps.1 \
	xgps.1 \
	xgpsspeed.1 \
	cgps.1 \
	lcdgps.1 \
	cgpxlogger.1 \
	libgps.3 \
	libgpsmm.3 \
	libgpsd.3 \
	gpsmon.1 \
	gpsctl.1 \
	gpspipe.1 \
	rtcm-104.5 \
	srec.5

MANPAGES_DIST = \
	$(MANPAGES_BASE) \
	$(RTCM104PAGES_DIST) \
	$(PYTHONPAGES_DIST)

man_MANS = \
	$(MANPAGES_BASE) \
	$(RTCM104PAGES) \
	$(PYTHONPAGES)

if HAVE_XSLT_PROCESSOR
#
# Create Manpages
#
BUILT_MANPAGES = $(MANPAGES_DIST)

.xml.1:
	$(MANGENERATOR) $(MANFLAGS) $(MANTARGET) $<

.xml.3:
	$(MANGENERATOR) $(MANFLAGS) $(MANTARGET) $<

.xml.5:
	$(MANGENERATOR) $(MANFLAGS) $(MANTARGET) $<

.xml.8:
	$(MANGENERATOR) $(MANFLAGS) $(MANTARGET) $<

# Another instance of the multiple-outputs hack.
gps.1 xgps.1 xgpsspeed.1 cgps.1 lcdgps.1 cgpxlogger.1: stamp-gps-manpages
	+@WITNESS=stamp-gps-manpages; $(MULTIOUT_RECOVER_DELETED)
stamp-gps-manpages: gps.xml
	@rm -f '$@' '$@.tmp'
	@echo 'timestamp for $@' > '$@.tmp'
	$(MANGENERATOR) $(MANFLAGS) $(MANTARGET) '$(srcdir)/gps.xml'
	@mv -f '$@.tmp' '$@'
CLEANFILES += stamp-gps-manpages stamp-gps-manpages.tmp
endif

noinst_HEADERS = driver_italk.h driver_rtcm2.h driver_superstar2.h \
	driver_ubx.h gpsmon.h gpsdclient.h json.h gps_json.h

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
	test_float.c \
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
	gpscat.xml \
	gpsctl.xml \
	gpsmon.xml \
	gpspipe.xml \
	gpsdecode.xml \
	rtcm-104.xml \
	srec.xml \
	$(MANPAGES_DIST) \
	xgps.ad \
	xgpsspeed.ad \
	gpsd.rules \
	gpsd.hotplug \
	gpsd.hotplug.wrapper \
	gpsd.usermap \
	valgrind-audit \
	valgrind-suppressions \
	gpspacket.c \
	gpslib.c \
	driver_proto.c \
	monitor_proto.c \
	logextract \
	striplog \
	setup.py \
	packet_states.h \
	libgps.pc.in \
	libgpsd.pc.in \
	gpscap.ini \
	packaging/etc_default_gpsd \
	packaging/etc_init.d_gpsd \
	packaging/etc_init.d_gpsd_rpm

#dist-hook:
#	$(MKDIR_P) '$(distdir)/contrib' && \
#	cp -p '$(srcdir)/contrib/'* '$(distdir)/contrib'
#distclean-local:
#	rm -rf '$(distdir)/contrib'

CLEANFILES += $(BUILT_SOURCES) $(TESTBINS) *.core $(PYEXTENSIONS) $(BUILT_MANPAGES)

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
	@echo "Running splint on gpsmon..."
	-splint $(SPLINTOPTS) -exportlocal $(gpsmon_SOURCES)
	@echo "Running splint on gpspipe..."
	-splint $(SPLINTOPTS) $(gpspipe_SOURCES)
	@echo "Running splint on gpsdecode..."
	-splint $(SPLINTOPTS) -exportlocal gpsdecode.c
	@echo "Running splint on test_bits test harness..."
	-splint $(SPLINTOPTS) $(test_bits_SOURCES)
	@echo "Running splint on test_packet test harness..."
	-splint $(SPLINTOPTS) $(test_packet_SOURCES)
	@echo "Running splint on test_geoid test harness..."
	-splint $(SPLINTOPTS) $(test_geoid_SOURCES)

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
rtcm-regress: gpsdecode
	@echo "Testing RTCM decoding..."
	@mkdir -p test
	@for f in $(srcdir)/test/*.rtcm2; do \
		echo "Testing $${f}..."; \
		$(srcdir)/gpsdecode <$${f} >test/test.chk; \
		diff -ub $${f}.chk test/test.chk; \
	done; \
	rm test/test.chk

# Rebuild the RTCM regression tests.
rtcm-makeregress: gpsdecode
	@for f in $(srcdir)/test/*.rtcm2; do \
		$(srcdir)/gpsdecode < $${f} > $${f}.chk; \
	done


# Regression-test the AIVDM decoder.
aivdm-regress: gpsdecode
	@echo "Testing AIVDM decoding..."
	@mkdir -p test
	@for f in $(srcdir)/test/*.aivdm; do \
		echo "Testing $${f}..."; \
		$(srcdir)/gpsdecode <$${f} >test/test.chk; \
		diff -ub $${f}.chk test/test.chk; \
	done; \
	rm test/test.chk

# Rebuild the AIVDM regression tests.
aivdm-makeregress: gpsdecode
	@for f in $(srcdir)/test/*.aivdm; do \
		$(srcdir)/gpsdecode < $${f} > $${f}.chk; \
	done


# Regression-test the packet getter.
packet-regress: test_packet
	@echo "Testing detection of invalid packets..."
	@$(srcdir)/test_packet | diff -u $(srcdir)/test/packet.test.chk -

# Rebuild the packet-getter regression test
packet-makeregress: test_packet
	@mkdir -p test
	$(srcdir)/test_packet >test/packet.test.chk

# Regression-test the geoid tester.
geoid-regress: test_geoid
	@echo "Testing the geoid model..."
	@$(srcdir)/test_geoid 37.371192 122.014965 | diff -u $(srcdir)/test/geoid.test.chk -

# Rebuild the packet-getter regression test
geoid-makeregress: test_geoid
	@mkdir -p test
	$(srcdir)/test_geoid 37.371192 122.014965 >test/geoid.test.chk

# Test RTCM encoding using passthrough mode.
undump-regress: gpsdecode
	@echo "Testing RTCM encoding..."
	@mkdir -p test
	@$(srcdir)/gpsdecode -p < $(srcdir)/test/undump.sample > test/undump.test
	@diff -u $(srcdir)/test/undump.sample test/undump.test
	@rm test/undump.test

# Regression-test the calendar functions
time-regress: test_mkgmtime
	./test_mkgmtime

# Regression test the unpacking code in libgps
unpack-regress: libgps
	@echo "Testing the client-library sentence decoder..."
	@$(srcdir)/libgps -u >test/test.chk
	@diff -u test/unpack.chk test/test.chk

# Build the regression test for the sentence unpacker
unpack-makeregress: libgps
	@echo "Rebuilding the clent sentence-unpacker test..."
	@$(srcdir)/libgps -u >test/unpack.chk

# Unit-test the JSON parsing
json-regress: test_json
	test_json

# Unit-test the bitfield extractor - not in normal tests
bits-regress: test_bits
	test_bits

# Do all normal regression tests
testregress: gps-regress rtcm-regress aivdm-regress packet-regress undump-regress time-regress unpack-regress
	@echo "Regressions complete."

# We would like "${MAKE} check" to run the regression tests.  automake
# appears to have support for running programs, but not make targets.
# Perhaps the tests should be moved into scripts, but then it's not
# clear how to deal with $(srcdir).
# TESTS=testregress

# Productions for setting up and performing udev tests.
#
# Requires root. Do "udev-install", then "tail -f /var/run/syslog" in
# another window, then run 'make udev-test', then plug and unplug the
# GPS ad libitum.  All is well when you get fix reports each time a GPS
# is plugged in.

udev-install:
	cp $(srcdir)/gpsd.rules /etc/udev/
	ln -sf /etc/udev/gpsd.rules /etc/udev/rules.d/025_gpsd.rules
	cp $(srcdir)/gpsd.hotplug /lib/udev/
	cp $(srcdir)/gpsd.hotplug.wrapper /lib/udev/
	chmod a+x /lib/udev/gpsd.hotplug /lib/udev/gpsd.hotplug.wrapper

udev-uninstall:
	rm -f /etc/udev/{gpsd.rules,gpsd.hotplug,gpsd.hotplug.wrapper}
	rm -f /etc/udev/rules.d/025_gpsd.rules

udev-test:
	$(srcdir)/gpsd -N -F /var/run/gpsd.sock -D 4

#
# Productions for testing unstable drivers.
#

unstable-regress: gpsd 
	./regress-driver -t $(srcdir)/test/unstable/*.log

unstable-makeregress: gpsd
	./regress-driver -b $(srcdir)/test/unstable/*.log


# Release machinery begins here
#

# Make RPM from the specfile in packaging
dist-rpm: distdir
	tardir=$(distdir) && $(am__tar) | GZIP=$(GZIP_ENV) gzip -c >$(distdir).tar.gz
	rpmbuild -ta $(distdir).tar.gz
	$(am__remove_distdir)

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

