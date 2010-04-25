# Automake description for gpsd
#
# This file is Copyright (c) 2010 by the GPSD project
# BSD terms apply: see the file COPYING in the distribution root for details.
#

# Exclude version-control state from the tarball.  Needed because we 
# grab the test directory wholesale. Hack appropriately when and if we 
# change version control systems.
# \bug This is non-portable and depends on GNU tar beyond-POSIX extensions.
am__tar += --exclude '*git*'

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
if HAVE_XAW
XAW_PROGS = xgpsspeed
endif

# Conditional includes.
INCLUDES = $(INCUSB)
if HAVE_DBUS
INCLUDES += $(DBUS_CFLAGS) $(DBUS_GLIB_CFLAGS) -DDBUS_API_SUBJECT_TO_CHANGE=1
endif

RTCM104PAGES_DIST = gpsdecode.1
if HAVE_RTCM104V2
if HAVE_RTCM104V3
if HAVE_AIVDM
RTCM104PROGS = gpsdecode
RTCM104PAGES = $(RTCM104PAGES_DIST)
endif
endif
endif

bin_PROGRAMS = $(XAW_PROGS) $(RTCM104PROGS) $(CURSESPROGS) gpsctl gpspipe gpxlogger lcdgps
sbin_PROGRAMS = gpsd

# \todo Add programs to TESTS if not already. used.
check_PROGRAMS = test_float test_trig test_bits test_packet test_mkgmtime test_geoid test_json
if LIBGPSMM_ENABLE
check_PROGRAMS += test_gpsmm
endif

# List of Python scripts and modules, which are handled by setup.py.
# Required to ensure that the extensions/modules/scripts are rebuilt via
# setup.py in case they changed.
# Also used to specify which files to include during make dist.
PYTHONSCRIPTS_DIST = gpscat gpsfake gpsprof xgps
PYTHONMODULES_DIST = gps/__init__.py gps/misc.py gps/fake.py gps/gps.py gps/client.py

PYTHONPAGES_DIST = gpsprof.1 gpsfake.1 gpscat.1
if HAVE_PYTHON
python_PYTHON = gpscap.py
PYTHONPAGES = $(PYTHONPAGES_DIST)
endif

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
xgpsspeed_LDADD = $(LIBM) $(LIBC) $(LIBNSL) $(LIBSOCKET) $(XAW_LIBS) $(XT_LIBS) $(X_LIBS) $(X_PRE_LIBS) libgps.la -lm $(LIBPTHREAD)

#
# Build cgps
#
cgps_SOURCES = cgps.c
cgps_LDADD = $(LIBM) $(LIBC) $(LIBNSL) $(LIBSOCKET) $(NCURSES_LIBS) libgps.la -lm $(LIBPTHREAD)

#
# Build gpxlogger
#
gpxlogger_SOURCES = gpxlogger.c
gpxlogger_LDADD = $(DBUS_GLIB_LIBS) libgps.la -lm

#
# Build gpsd
#
gpsd_c_sources = gpsd_dbus.c gpsd.c
gpsd_SOURCES = $(gpsd_c_sources) gpsd_dbus.h
gpsd_LDADD = $(DBUS_LIBS) $(LIBM) libgpsd.la libgps.la -lm $(LIBPTHREAD) $(LIBUSB)

#
# Build gpsctl
#
gpsctl_SOURCES = gpsctl.c
gpsctl_LDADD = $(LIBM) libgpsd.la libgps.la -lm $(LIBPTHREAD) $(LIBUSB)

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
	monitor_oncore.c monitor_tnt.c
gpsmon_LDADD = $(LIBM) $(NCURSES_LIBS) libgpsd.la libgps.la -lm $(LIBPTHREAD)  $(LIBUSB)

#
# Build gpsdecode
#
gpsdecode_SOURCES = gpsdecode.c
gpsdecode_LDADD = $(LIBM) libgpsd.la libgps.la -lm $(LIBPTHREAD)  $(LIBUSB)

#
# Build shared libraries
#
libgps_la_LDFLAGS = -version-number 19:0:0
lib_LTLIBRARIES = libgps.la libgpsd.la

libgps_c_sources = \
	ais_json.c \
	gpsd_report.c \
	gpsutils.c \
	geoid.c \
	gpsdclient.c \
	gps_maskdump.c \
	hex.c \
	json.c \
	libgps_core.c \
	libgps_json.c \
	netlib.c \
	rtcm2_json.c \
	shared_json.c \
	strl.c

libgpsd_c_sources = \
	bits.c \
	bsd-base64.c \
	crc24q.c \
	gpsd_json.c \
	isgps.c \
	gpsd_maskdump.c \
	libgpsd_core.c \
	net_dgpsip.c \
	net_gnss_dispatch.c \
	net_ntrip.c \
	ntpshm.c \
	packet.c \
	pseudonmea.c \
	serial.c \
	srecord.c \
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
	sockaddr.h \
	bsd-base64.h \
	timebase.h \
	bits.h \
	crc24q.h

BUILT_SOURCES = packet_names.h gpsd.h revision.h ais_json.i gps_maskdump.c gpsd_maskdump.c

packet_names.h: packet_states.h
	rm -f packet_names.h && \
	sed -e '/^ *\([A-Z][A-Z0-9_]*\),/s//   "\1",/' -e '/_states/s//_names/' < `test -f 'packet_states.h' || echo '$(srcdir)/'`packet_states.h > packet_names.h && \
	chmod a-w packet_names.h

gpsd.h: gpsd.h-head gpsd.h-tail gpsd_config.h
	rm -f gpsd.h && \
	echo "/* This file is generated.  Do not hand-hack it! */" >gpsd.h && \
	cat $(srcdir)/gpsd.h-head >>gpsd.h && \
	cat $(srcdir)/gpsd_config.h >>gpsd.h && \
	cat $(srcdir)/gpsd.h-tail >>gpsd.h && \
	chmod a-w gpsd.h

ais_json.i: jsongen.py
	rm -f ais_json.i && \
	$(PYTHON) jsongen.py --ais --target=parser >ais_json.i && \
	chmod a-w ais_json.i

revision.h: Makefile
	@rm -f revision.h && \
	echo '#define' REVISION '"'`date -u +%Y-%m-%dT%H:%M:%S`'"' >revision.h && \
	chmod a-w revision.h

gps_maskdump.c: gps.h maskaudit.py
	rm -f gps_maskdump.c && \
	$(PYTHON) maskaudit.py -c >gps_maskdump.c && \
	chmod a-w gps_maskdump.c

gpsd_maskdump.c: gpsd.h-tail maskaudit.py
	rm -f gpsd_maskdump.c && \
	$(PYTHON) maskaudit.py -d >gpsd_maskdump.c && \
	chmod a-w gpsd_maskdump.c

libgps_la_SOURCES = $(libgps_c_sources)

libgpsd_la_SOURCES = $(libgpsd_c_sources) $(libgpsd_h_sources) \
				driver_rtcm2.h packet_states.h

# Warning: This overrides autoconf's normal link-line generation process
if LIBGPSMM_ENABLE
libgps_la_SOURCES += libgpsmm.cpp
libgps_la_LINK = /bin/sh ./libtool --tag=CXX --mode=link g++ $(libgps_la_LDFLAGS) -o $@
else
libgps_la_LINK = /bin/sh ./libtool --tag=CC --mode=link gcc $(libgps_la_LDFLAGS) -o $@
endif

nodist_libgpsd_la_SOURCES = packet_names.h ais_json.i
libgps_la_LIBADD = $(LIBM) $(LIBC) $(LIBNSL) $(LIBSOCKET) $(LIBPTHREAD)
libgpsd_la_LIBADD = $(LIBM) $(LIBC) $(LIBNSL) $(LIBSOCKET) $(LIBPTHREAD) libgps.la

#
# Build Python binding
#
if HAVE_PYTHON
PYEXTENSIONS = gpspacket.so gpslib.so
noinst_SCRIPTS = stamp-python setup.py

# Multiple-outputs hack.  See
# http://www.gnu.org/software/automake/manual/automake.html#Multiple-Outputs
$(PYEXTENSIONS): stamp-python
	+@WITNESS=stamp-python; $(MULTIOUT_RECOVER_DELETED)
# TODO:  Should the dependency on libgps.la be enforced inside
# setup.py?  (See the variable 'needed_files' in setup.py.)
stamp-python: gpspacket.c gpsclient.c libgps.la setup.py $(PYTHONMODULES_DIST) $(PYTHONSCRIPTS_DIST)
# Build Python modules and scripts using distutils via setup.py.
# We define build-lib and build-scripts as distutils might have been
# configured to use different directories, but we want to use the
# produced files within the regress-driver script - therefore we
# need to build them in directories we know about.
# See configure.ac for the definition of PYTHON_DISTUTILS_LIBDIR
# and PYTHON_DISTUTILS_SCRIPTDIR.
	@rm -f '$@' '$@.tmp'
	@echo 'timestamp for $@' > '$@.tmp'
	(cd '$(srcdir)' && \
	env abs_builddir='$(abs_builddir)' \
	    MAKE='$(MAKE)' \
		$(PYTHON) setup.py build \
			--build-lib '$(srcdir)/$(PYTHON_DISTUTILS_LIBDIR)' \
			--build-scripts '$(srcdir)/$(PYTHON_DISTUTILS_SCRIPTDIR)' \
			--mangenerator '$(MANGENERATOR)') && \
	(cd '$(srcdir)/gps' && \
	    rm -f *.so && \
	    ln -s ../$(PYTHON_DISTUTILS_LIBDIR)/gps/*.so . ) && \
	mv -f '$@.tmp' '$@'
CLEANFILES += stamp-python stamp-python.tmp

# Clean up after Python
clean-local:
	rm -rf build gps/*.so

# Install Python modules
install-exec-local:
# Make sure we do not use --root as option to setup.py install
# when DESTDIR is not defined as distutils would use the current
# working directory as root directory and not install to ${prefix}.
	if test -z "$(DESTDIR)"; then \
		$(PYTHON) setup.py install --prefix=${prefix} ;\
	else \
		$(PYTHON) setup.py install --prefix=${prefix} --root=$(DESTDIR) ;\
	fi
endif

#
# Build test_float
#
test_float_SOURCES = test_float.c
test_float_LDADD = $(LIBC) -lm

#
# Build test_trig
#
test_trig_SOURCES = test_trig.c
test_trig_LDADD = $(LIBC) -lm

if LIBGPSMM_ENABLE
#
# Build test_gpsmm
#
test_gpsmm_SOURCES = test_gpsmm.cpp
test_gpsmm_LDADD = $(LIBC) libgps.la -lm  $(LIBUSB)
endif

#
# Build test_bits tester
#
test_bits_SOURCES = test_bits.c
test_bits_LDADD = $(LIBC) libgpsd.la libgps.la  $(LIBUSB)

#
# Build packets tester
#
test_packet_SOURCES = test_packet.c
test_packet_LDADD = $(LIBC) libgpsd.la libgps.la -lm  $(LIBUSB)

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
	libgps.3 \
	libgpsmm.3 \
	libgpsd.3 \
	gpsmon.1 \
	gpsctl.1 \
	gpspipe.1 \
	rtcm-104.5 \
	srec.5

if HAVE_XSLT_PROCESSOR
MANPAGES_DIST = \
	$(MANPAGES_BASE) \
	$(RTCM104PAGES_DIST) \
	$(PYTHONPAGES_DIST)

man_MANS = \
	$(MANPAGES_BASE) \
	$(RTCM104PAGES) \
	$(PYTHONPAGES)

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
gps.1 xgps.1 xgpsspeed.1 cgps.1 lcdgps.1: stamp-gps-manpages
	+@WITNESS=stamp-gps-manpages; $(MULTIOUT_RECOVER_DELETED)
stamp-gps-manpages: gps.xml
	@rm -f '$@' '$@.tmp'
	echo 'timestamp for $@' > '$@.tmp' && \
	$(MANGENERATOR) $(MANFLAGS) $(MANTARGET) '$(srcdir)/gps.xml' && \
	mv -f '$@.tmp' '$@'
CLEANFILES += stamp-gps-manpages stamp-gps-manpages.tmp
endif

noinst_HEADERS = gpsd_config.h \
	driver_italk.h driver_rtcm2.h driver_superstar2.h \
	driver_ubx.h gpsmon.h gpsdclient.h json.h gps_json.h \
	revision.h

nodist_include_HEADERS = gpsd.h

if LIBGPSMM_ENABLE
include_HEADERS = gps.h libgpsmm.h
else
include_HEADERS = gps.h
endif

XML = \
	gpsd.xml \
	gps.xml \
	libgps.xml \
	libgpsmm.xml \
	libgpsd.xml \
	gpsmon.xml \
	gpsdecode.xml \
	gpsprof.xml \
	gpsfake.xml \
	gpsctl.xml \
	gpscat.xml \
	gpspipe.xml \
	rtcm-104.xml \
	srec.xml

# Note: packaging/gpsd.spec is generated, but it needs to be in the
# tarball in order for 'make diat-rpm' to work.  The BUILT_SOURCES
# files are here in order to minimize build dependencies for package
# builders who never alter anything, especially the Python dependency.
# Also note that the test and gps directories are here rather than
# being the contents of a SUBDIRS variable so that autconf won't 
# go looking for makefiles in them.
EXTRA_DIST = \
	revision.h \
	autogen.sh \
	README \
	INSTALL \
	COPYING \
	TODO \
	NEWS \
	AUTHORS \
	jsongen.py.in \
	maskaudit.py.in \
	dgpsip-servers \
	test_float.c \
	test_trig.c \
	gpsd.php \
	gpsd.xml \
	gpsd.h-head \
	gpsd.h-tail \
	$(XML) \
	$(BUILT_SOURCES) \
	$(MANPAGES_DIST) \
	xgpsspeed.ad \
	gpsd.rules \
	gpsd.hotplug \
	gpsd.hotplug.wrapper \
	gpsd.usermap \
	valgrind-audit \
	valgrind-suppressions \
	gpspacket.c \
	gpsclient.c \
	driver_proto.c \
	monitor_proto.c \
	setup.py \
	packet_states.h \
	libgps.pc.in \
	libgpsd.pc.in \
	gpscap.ini \
	packaging/gpsd.spec \
	packaging/etc_default_gpsd \
	packaging/etc_init.d_gpsd \
	packaging/etc_init.d_gpsd_rpm \
	do-tests \
	regress-driver \
	$(PYTHONSCRIPTS_DIST) \
	$(PYTHONMODULES_DIST) \
	test

#dist-hook:
#	$(MKDIR_P) '$(distdir)/contrib' && \
#	cp -p '$(srcdir)/contrib/'* '$(distdir)/contrib'
#distclean-local:
#	rm -rf '$(distdir)/contrib'

CLEANFILES += $(BUILT_SOURCES) *.core $(PYEXTENSIONS) $(BUILT_MANPAGES)

pkgconfig_DATA = libgps.pc libgpsd.pc
pkgconfigdir = $(libdir)/pkgconfig

# These are not distributed
libgps: libgps_core.c gps.h .libs/libgps.a
	$(CC) $(CFLAGS) -o libgps -lm -DTESTMAIN $(LIBPTHREAD) -g libgps_core.c .libs/libgps.a

# Report splint warnings
SPLINTOPTS = -I/usr/include/dbus-1.0/ $(INCUSB) +quiet
splint: gpsd.h packet_names.h
	@echo "Running splint on daemon and libraries..."
	-splint $(SPLINTOPTS) -exportlocal -redef $(gpsd_c_sources) $(libgpsd_c_sources) $(libgps_c_sources)
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
	-splint $(SPLINTOPTS) $(gpsdecode_SOURCES)
	@echo "Running splint on gpxlogger..."
	-splint $(SPLINTOPTS) $(gpxlogger_SOURCES)
	@echo "Running splint on test_bits test harness..."
	-splint $(SPLINTOPTS) $(test_bits_SOURCES)
	@echo "Running splint on test_packet test harness..."
	-splint $(SPLINTOPTS) $(test_packet_SOURCES)
	@echo "Running splint on test_mkgmtime test harness..."
	-splint $(SPLINTOPTS) $(test_mkgmtime_SOURCES)
	@echo "Running splint on test_geoid test harness..."
	-splint $(SPLINTOPTS) $(test_geoid_SOURCES)
	@echo "Running splint on test_json test harness..."
	-splint $(SPLINTOPTS) $(test_json_SOURCES)

# Report cppcheck warnings.  Requires 1.40 or later.
cppcheck: gpsd.h packet_names.h
	cppcheck --template gcc --all --force .

# Re-indent the codebase in aa uniform style for readability.
# FIXME: Except for xgpsspeed - Tachometer.c makes GNU indent lose it.
INDENT_FILES = $(gpsd_c_sources) $(libgpsd_c_sources) $(libgps_c_sources) \
	$(cgps_SOURCES) $(gpsmon_SOURCES) $(gpspipe_SOURCES) \
	$(gpxlogger_SOURCES) $(gpsdecode_SOURCES) \
	$(test_bits_SOURCES) $(test_packet_SOURCES) \
	$(test_mkgmtime_SOURCES)  $(test_geoid_SOURCES) $(test_json_SOURCES)
INDENT_OPTIONS = --indent-level4 \
		--honour-newlines \
		--dont-break-procedure-type \
		--cuddle-else \
		--braces-on-if-line \
		--case-brace-indentation0 \
		--brace-indent0 \
		--no-space-after-casts \
		--no-space-after-function-call-names \
		--start-left-side-of-comments \
		--dont-format-comments
indent:
	chmod u+w *maskdump.c
	indent $(INDENT_OPTIONS) $(INDENT_FILES)
	for f in $(INDENT_FILES); \
		do \
			sed <$${f} >/tmp/reindent$$$$ -e 's/@ \*/@*/' ; \
			mv /tmp/reindent$$$$ $${f} ; \
		done
	chmod u-w *maskdump.c
	@echo "Diff lines:" `git diff | wc -l`

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

# Our regression tests are make targets, while automake expects
# programs.  Thus, our approach is to construct the test
# infrastructure our way, with make targets, and to have one TEST from
# automake's viewpoint: a trivial shell script to invoke make with our
# top-level regression target.

# One might think that using TESTS_ENVIRONMENT=$(MAKE) would work
# around this, but because the generated rule (check-TESTS) both
# depends on each TEST as well as invokes it (with TESTS_ENVIRONMENT)
# the entire list of tests is run twice.

run_regress_driver = PYTHON=$(PYTHON) $(srcdir)/regress-driver

# Regression-test the daemon
gps-regress: gpsd stamp-python
	$(run_regress_driver) $(srcdir)/test/daemon/*.log

# Test that super-raw mode works. Compare each logfile against itself 
# dumped through the daemon running in R=2 mode.  (This test is not
# included in the normal regressions.)
raw-regress: stamp-python
	$(run_regress_driver) -r  $(srcdir)/test/daemon/*.log

# Build the regression tests for the daemon.
gps-makeregress: gpsd stamp-python
	$(run_regress_driver) -b $(srcdir)/test/daemon/*.log

# To build an individual test for a load named foo.log, put it in
# test/daemon and do this:
#	regress-driver -b test/daemon/foo.log

# Regression-test the RTCM decoder.
rtcm-regress: gpsdecode
	@echo "Testing RTCM decoding..."
	@mkdir -p test
	@for f in $(srcdir)/test/*.rtcm2; do \
		echo "Testing $${f}..."; \
		$(srcdir)/gpsdecode <$${f} >/tmp/test-$$$$.chk; \
		diff -ub $${f}.chk /tmp/test-$$$$.chk; \
	done;
	@echo "Testing idempotency of JSON dump/decode for RTCM2"
	@$(srcdir)/gpsdecode -e -j <test/synthetic-rtcm2.json >/tmp/test-$$$$.chk; \
		grep -v '^#' test/synthetic-rtcm2.json | diff -ub - /tmp/test-$$$$.chk; \
		rm /tmp/test-$$$$.chk

# Rebuild the RTCM regression tests.
rtcm-makeregress: gpsdecode
	@for f in $(srcdir)/test/*.rtcm2; do \
		$(srcdir)/gpsdecode -j < $${f} > $${f}.chk; \
	done

# Regression-test the AIVDM decoder.
aivdm-regress: gpsdecode
	echo "Testing AIVDM decoding..."
	@mkdir -p $(srcdir)/test
	@for f in $(srcdir)/test/*.aivdm; do \
		echo "Testing $${f}..."; \
		$(srcdir)/gpsdecode -u -c <$${f} >/tmp/test-$$$$.chk; \
		diff -ub $${f}.chk /tmp/test-$$$$.chk; \
	done;
	@echo "Testing idempotency of JSON dump/decode for AIS"
	@$(srcdir)/gpsdecode -e -j <$(srcdir)/test/synthetic-ais.json >/tmp/test-$$$$.chk; \
		grep -v '^#' $(srcdir)/test/synthetic-ais.json | diff -ub - /tmp/test-$$$$.chk; \
		rm /tmp/test-$$$$.chk

# Rebuild the AIVDM regression tests.
aivdm-makeregress: gpsdecode
	@for f in $(srcdir)/test/*.aivdm; do \
		$(srcdir)/gpsdecode -u -c <$${f} > $${f}.chk; \
	done

# Regression-test the packet getter.
packet-regress: test_packet
	@echo "Testing detection of invalid packets..."
	@$(srcdir)/test_packet | diff -u $(srcdir)/test/packet.test.chk -

# Rebuild the packet-getter regression test
packet-makeregress: test_packet
	@mkdir -p $(srcdir)/test
	$(srcdir)/test_packet >$(srcdir)/test/packet.test.chk

# Regression-test the geoid tester.
geoid-regress: test_geoid
	@echo "Testing the geoid model..."
	@$(srcdir)/test_geoid 37.371192 122.014965 | diff -u $(srcdir)/test/geoid.test.chk -

# Rebuild the packet-getter regression test
geoid-makeregress: test_geoid
	@mkdir -p $(srcdir)/test
	$(srcdir)/test_geoid 37.371192 122.014965 >$(srcdir)/test/geoid.test.chk

# Regression-test the calendar functions
time-regress: test_mkgmtime
	$(srcdir)/test_mkgmtime

# Regression test the unpacking code in libgps
unpack-regress: libgps
	@echo "Testing the client-library sentence decoder..."
	$(run_regress_driver) -c $(srcdir)/test/clientlib/*.log

# Build the regression test for the sentence unpacker
unpack-makeregress: libgps
	@echo "Rebuilding the client sentence-unpacker tests..."
	$(run_regress_driver) -c -b $(srcdir)/test/clientlib/*.log

# Unit-test the JSON parsing
json-regress: test_json
	$(srcdir)/test_json

# Unit-test the bitfield extractor - not in normal tests
bits-regress: test_bits
	$(srcdir)/test_bits

# Do all normal regression tests.
testregress: gps-regress rtcm-regress aivdm-regress packet-regress time-regress unpack-regress json-regress
	@echo "Regressions complete."

# do-tests is a shell script that invokes make with target testregress.
# This works around automake's lack of support for make targets as tests.
TESTS_ENVIRONMENT = MAKE=$(MAKE) PYTHON=$(PYTHON)
TESTS = do-tests

# The website directory
#
# None of these productions are fired by 'make all'.

# FIXME: Needs to be replaced by a $(MANGENERATOR) rule
%.html: %.xml
	xmlto xhtml-nochunks $<

website: $(XML:.xml=.html) \
		www/AIVDM.html www/NMEA.html \
		www/protocol-evolution.html www/protocol-transition.html \
		www/client-howto.html www/writing-a-driver.html \
		www/index.html www/hardware.html \
		www/performance/performance.html \
		www/internals.html
	@cp $(XML:.xml=.html) www

www/AIVDM.html: www/AIVDM.txt
	asciidoc -a toc -o www/AIVDM.html www/AIVDM.txt

www/NMEA.html: www/NMEA.txt
	asciidoc -a toc -o www/NMEA.html www/NMEA.txt

www/protocol-evolution.html: www/protocol-evolution.txt
	asciidoc -a toc -o www/protocol-evolution.html www/protocol-evolution.txt

www/protocol-transition.html: www/protocol-transition.txt
	asciidoc -a toc -o www/protocol-transition.html www/protocol-transition.txt

www/client-howto.html: www/client-howto.txt
	asciidoc -a toc -o www/client-howto.html www/client-howto.txt

www/writing-a-driver.html: www/writing-a-driver.xml
	xmlto xhtml-nochunks www/writing-a-driver.xml; mv writing-a-driver.html www

www/index.html: www/index.html.in
	sed -e "/@DATE@/s//`date '+%B %d, %Y'`/" <www/index.html.in >www/index.html

www/hardware.html: www/hardware-head.html gpscap.ini www/hardware-tail.html
	(cat www/hardware-head.html; python gpscap.py; cat www/hardware-tail.html) >www/hardware.html

www/performance/performance.html: www/performance/performance.xml
	(cd www/performance; xmlto xhtml-nochunks performance.xml)

www/internals.html: $(shell ls doc/*.xml)
	cd doc; xmlto xhtml-nochunks explanation.xml; cp explanation.html ../www/internals.html

if HAVE_PYTHON
# Experimenting with pydoc.  Not yet fired by any other productions.

pydoc: www/pydoc/index.html

# We need to run epydoc with the Python version we built the modules for.
# So we define our on epydoc instead of using /usr/bin/epydoc
EPYDOC = $(PYTHON) -c 'from epydoc.cli import cli; cli()'

# We have pre-compiled python scripts in the script directory, so we exclude
# all files ending on c here. Needs a better solution as soon as we have a
# script ending with c.
EPYDOCSCRIPTS = $(shell find $(PYTHON_DISTUTILS_SCRIPTDIR) -name '*c' -o -type f -print)
EPYDOCMODULES = $(PYTHON_DISTUTILS_LIBDIR)/gps

www/pydoc/index.html: gps gpsfake gpscat gpsprof stamp-python
	mkdir -p www/pydoc
	$(EPYDOC) -v --html --graph all -n GPSD $(EPYDOCSCRIPTS) $(EPYDOCMODULES) -o www/pydoc

.PHONY: pydoc
endif

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

# Release machinery begins here
#

# Make RPM from the specfile in packaging
dist-rpm: distdir
	tardir=$(distdir) && $(am__tar) | GZIP=$(GZIP_ENV) gzip -c >$(distdir).tar.gz
	rpmbuild -ta $(distdir).tar.gz
	$(am__remove_distdir)

# This is how to ship a release to Berlios incoming.
# It requires developer access verified via ssh.
#
upload-ftp: dist
	shasum gpsd-$(VERSION).tar.gz >gpsd.sum
	lftp -c "open ftp://ftp.berlios.de/incoming; mput gpsd-$(VERSION).tar.gz gpsd.sum"

#
# This is how to tag a release.
# It requires developer access verified via ssh.
#
release-tag:
	git tag -s -m "Tagged for external release $(VERSION)" release-$(VERSION)
	git push --tags

#
# Ship a release, providing all regression tests pass.
# The clean is necessary so that dist will remake revision.h
# with the current revision level in it.
#
ship: testregress clean dist upload-ftp release-tag

