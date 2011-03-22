# Automake description for gpsd
#
# This file is Copyright (c) 2010 by the GPSD project
# BSD terms apply: see the file COPYING in the distribution root for details.
#

.PHONY: print_libgps_VERSION_CURRENT \
        print_libgps_VERSION_REVISION \
        print_libgps_VERSION_AGE \
        print_libgps_SONAME \
        print_libgps_VERSION \
	pydoc \
	check_programs

# The deheader production uses this to pass in stop-on-error flags
CFLAGS += $(MORECFLAGS) -DSYSCONFDIR=\"$(sysconfdir)\"

ACLOCAL_AMDFLAGS = -I m4

CLEANFILES =

#SUBDIRS = contrib

XMLTO = xmlto

#
# Conditionally add programs depending on libraries that may or may not be present.
#
if HAVE_NCURSES
CURSESPROGS = cgps gpsmon
endif

# Conditional includes.
INCLUDES = $(LIBUSB_CFLAGS)
if HAVE_DBUS
INCLUDES += $(DBUS_CFLAGS) $(DBUS_GLIB_CFLAGS) -DDBUS_API_SUBJECT_TO_CHANGE=1
endif
if HAVE_BLUEZ
INCLUDES += $(BLUEZ_CFLAGS)
endif

bin_PROGRAMS = $(CURSESPROGS) gpsdecode gpsctl gpspipe gpxlogger lcdgps
sbin_PROGRAMS = gpsd

# to-do: Add programs to TESTS if not already used.
check_PROGRAMS = test_float test_trig test_bits test_packet test_mkgmtime test_geoid test_json
if LIBGPSMM_ENABLE
check_PROGRAMS += test_gpsmm
endif
if LIB_Q_GPSMM_ENABLE
check_PROGRAMS += test_qgpsmm
endif

check_programs: $(check_PROGRAMS)
# nothing to do here - this is just a target which allows
# to ask make to build our check_programs without running them.

# List of Python scripts and modules, which are handled by setup.py.
# Required to ensure that the extensions/modules/scripts are rebuilt via
# setup.py in case they changed.
# Also used to specify which files to include during make dist.
PYTHONSCRIPTS_DIST = gpscat gpsfake gpsprof xgps xgpsspeed
PYTHONMODULES_DIST = gps/__init__.py gps/misc.py gps/fake.py gps/gps.py gps/client.py

PYTHONPAGES_DIST = gpsprof.1 gpsfake.1 gpscat.1 xgpsspeed.1 xgps.1
if HAVE_PYTHON
pyexec_PYTHON = gpscap.py
PYTHONPAGES = $(PYTHONPAGES_DIST)
endif


#
# Build cgps
#
cgps_SOURCES = cgps.c
cgps_LDADD = $(LIBC) $(LIBNSL) $(LIBSOCKET) $(NCURSES_LIBS) libgps.la $(LIBM) $(LIBPTHREAD)

#
# Build gpxlogger
#
gpxlogger_SOURCES = gpxlogger.c
gpxlogger_LDADD = $(DBUS_GLIB_LIBS) libgps.la $(LIBM)

#
# Build gpsd
#
gpsd_c_sources = gpsd.c gpsd_dbus.c
gpsd_SOURCES = $(gpsd_c_sources) gpsd_dbus.h
gpsd_LDADD = $(DBUS_LIBS) libgpsd.la libgps.la $(LIBM) $(LIBPTHREAD) $(LIBUSB_LIBS)

#
# Build gpsctl
#
gpsctl_SOURCES = gpsctl.c
gpsctl_LDADD = libgpsd.la libgps.la $(LIBM) $(LIBPTHREAD) $(LIBUSB_LIBS)

#
# Build gpspipe
#
gpspipe_SOURCES = gpspipe.c
gpspipe_LDADD = $(DBUS_LIBS) libgps.la $(LIBM)

#
# Build lcdgps
#
lcdgps_SOURCES = lcdgps.c
lcdgps_LDADD = libgps.la $(LIBM)

#
# Build gpsmon
#
gpsmon_SOURCES = gpsmon.c monitor_nmea.c monitor_sirf.c \
	monitor_italk.c monitor_ubx.c monitor_superstar2.c \
	monitor_oncore.c monitor_tnt.c
gpsmon_LDADD = $(NCURSES_LIBS) libgpsd.la libgps.la $(LIBM) $(LIBPTHREAD)  $(LIBUSB_LIBS) $(BLUEZ_LIBS)


#
# Build gpsdecode
#
gpsdecode_SOURCES = gpsdecode.c
gpsdecode_LDADD = libgpsd.la libgps.la $(LIBM) $(LIBPTHREAD)  $(LIBUSB_LIBS)

#
# Build shared libraries
#
# As we need to retrieve the version from qmake to build the Qt library,
# we provide targets to print the necessary informations.
libgps_VERSION_CURRENT = 20
libgps_VERSION_REVISION = 0
libgps_VERSION_AGE = 0
libgps_VERSION_NUMBER = $(libgps_VERSION_CURRENT):$(libgps_VERSION_REVISION):$(libgps_VERSION_AGE)
libgps_la_LDFLAGS = -version-number $(libgps_VERSION_NUMBER)
lib_LTLIBRARIES = libgps.la libgpsd.la

libgps_SONAME = $(shell expr $(libgps_VERSION_CURRENT) - $(libgps_VERSION_AGE))
libgps_VERSION = $(libgps_SONAME).$(libgps_VERSION_AGE).$(libgps_VERSION_REVISION)
print_libgps_VERSION_CURRENT:
	echo $(libgps_VERSION_CURRENT)
print_libgps_VERSION_REVISION:
	echo $(libgps_VERSION_REVISION)
print_libgps_VERSION_AGE:
	echo $(libgps_VERSION_AGE)
print_libgps_SONAME:
	echo $(libgps_SONAME)
print_libgps_VERSION:
	echo $(libgps_VERSION)

libgps_c_sources = \
	ais_json.c \
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


if NO_DAEMON3
libgps_c_sources += daemon.c
endif

libgpsd_c_sources = \
	bits.c \
	bsd-base64.c \
	crc24q.c \
	gpsd_json.c \
	isgps.c \
	gpsd_maskdump.c \
	timebase.c \
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
	driver_geostar.c \
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
	bits.h \
	crc24q.h

libgpsd_la_LDFLAGS = $(LIBUSB_LIBS) $(BLUEZ_LIBS)

BUILT_SOURCES = packet_names.h gpsd.h revision.h ais_json.i gps_maskdump.c gpsd_maskdump.c timebase.h

$(srcdir)/gpsutils.c $(srcdir)/gpsd.c: $(BUILT_SOURCES)
	touch $@

packet_names.h: packet_states.h
	rm -f packet_names.h && \
	sed -e '/^ *\([A-Z][A-Z0-9_]*\),/s//   "\1",/' -e '/_states/s//_names/' < `test -f 'packet_states.h' || echo '$(srcdir)/'`packet_states.h > packet_names.h && \
	chmod a-w packet_names.h

gpsd.h: gpsd.h-head gpsd.h-tail gpsd_config.h
	rm -f gpsd.h && \
	echo "/* This file is generated.  Do not hand-hack it! */" >gpsd.h && \
	cat $(srcdir)/gpsd.h-head >>gpsd.h && \
	cat gpsd_config.h >>gpsd.h && \
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
	$(PYTHON) maskaudit.py -c $(srcdir) >gps_maskdump.c && \
	chmod a-w gps_maskdump.c

gpsd_maskdump.c: gpsd.h maskaudit.py
	rm -f gpsd_maskdump.c && \
	$(PYTHON) maskaudit.py -d >gpsd_maskdump.c && \
	chmod a-w gpsd_maskdump.c

timebase.h: $(srcdir)/leapsecond.py $(srcdir)/leapseconds.cache
	$(PYTHON) $(srcdir)/leapsecond.py -h $(srcdir)/leapseconds.cache >timebase.h

# leapseconds.cache is a local cache for information on leapseconds issued
# by the U.S. Naval observatory. It gets kept in the repository so we can
# build without Internet access.
leapseconds.cache: $(srcdir)/leapsecond.py
	$(PYTHON) $(srcdir)/leapsecond.py -f leapseconds.cache

libgps_la_SOURCES = $(libgps_c_sources)

libgpsd_la_SOURCES = $(libgpsd_c_sources) $(libgpsd_h_sources) \
				packet_states.h timebase.h

# Warning: This overrides autoconf's normal link-line generation process
if LIBGPSMM_ENABLE
libgps_la_SOURCES += libgpsmm.cpp
libgps_la_LINK = $(LIBTOOL) --tag=CXX --mode=link g++ $(libgps_la_LDFLAGS) -o $@
else
libgps_la_LINK = $(LIBTOOL) --tag=CC --mode=link gcc $(libgps_la_LDFLAGS) -o $@
endif

nodist_libgpsd_la_SOURCES = packet_names.h ais_json.i
libgps_la_LIBADD = $(LIBM) $(LIBC) $(LIBNSL) $(LIBSOCKET) $(LIBPTHREAD)
libgpsd_la_LIBADD = $(LIBM) $(LIBC) $(LIBNSL) $(LIBSOCKET) $(LIBPTHREAD) $(BLUEZ_LIBS) $(LIBRT) libgps.la

noinst_SCRIPTS =

# Build Python binding
#
if HAVE_PYTHON
PYEXTENSIONS = gpspacket.so gpslib.so
noinst_SCRIPTS += stamp-python setup.py

# TODO:  Should the dependency on libgps.la be enforced inside
# setup.py?  (See the variable 'needed_files' in setup.py.)
stamp-python: gpspacket.c gpsclient.c libgps.la setup.py $(PYTHONMODULES_DIST) $(PYTHONSCRIPTS_DIST) $(BUILT_SOURCES)
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
	env version='$(PACKAGE_VERSION)' abs_builddir='$(abs_builddir)' \
	    MAKE='$(MAKE)' \
		$(PYTHON) setup.py build \
			--build-lib '$(srcdir)/$(PYTHON_DISTUTILS_LIBDIR)' \
			--build-scripts '$(srcdir)/$(PYTHON_DISTUTILS_SCRIPTDIR)' \
			--mangenerator '$(XMLPROC)') && \
	(cd '$(srcdir)/gps' && \
	    rm -f *.so && \
	    ln -s ../$(PYTHON_DISTUTILS_LIBDIR)/gps/*.so . ) && \
	mv -f '$@.tmp' '$@'
CLEANFILES += stamp-python stamp-python.tmp
endif

QTLIB_DIST = libQgpsmm/libQgpsmm.pro \
             libQgpsmm/gpsutils.cpp \
             libQgpsmm/libgps_core.cpp \
             libQgpsmm/libQgpsmm_global.h

QTLIB_DIST_MINGW = libQgpsmm/mingw/gpsd_config.h \
		   libQgpsmm/mingw/test_qgpsmm.pro

if LIB_Q_GPSMM_ENABLE
noinst_SCRIPTS += stamp-qt
QTLIBS = libQgpsmm/binaries/libQgpsmm.so \
	 libQgpsmm/binaries/libQgpsmm.so.$(libgps_SONAME) \
	 libQgpsmm/binaries/libQgpsmm.so.$(libgps_SONAME).$(libgps_VERSION_AGE) \
	 libQgpsmm/binaries/libQgpsmm.so.$(libgps_VERSION)

QTLIB_sources = gpsutils.c \
		libgps_core.c \
		libgpsmm.cpp \
		libgps_json.c \
		hex.c \
		strl.c \
		shared_json.c \
		rtcm2_json.c \
		ais_json.c \
		json.c \
		gps.h \
		libgpsmm.h \
		gps_json.h \
		json.h \
		ais_json.i \
		gpsd.h \
		$(QTLIB_DIST)

QMAKE_OPTS = "PREFIX=${prefix}" \
	     "MAKE=$(MAKE)" \
             "ABS_BUILDDIR=`cd $(abs_builddir); pwd`" \
	     "QMAKE_CXX=$(CXX)" \
	     "QMAKE_CC=$(CC)" \
	     "QMAKE_CFLAGS+=$(CFLAGS)" \
	     "QMAKE_LFLAGS+=$(LDFLAGS)" \
	     "VERSION=$(libgps_VERSION)" \
	     "TARGET_LIBDIR=${libdir}" \
	     "TARGET_INCLUDEDIR=${includedir}"

libQgpsmm/Makefile: libQgpsmm/libQgpsmm.pro gpsd.h ais_json.i
	cd $(srcdir)/libQgpsmm && $(QMAKE) $(QMAKE_OPTS)
stamp-qt: $(QTLIB_sources) libQgpsmm/Makefile
	$(MAKE) -C $(srcdir)/libQgpsmm
	touch $@
CLEANFILES += stamp-qt
endif

clean-local:
# remove libgps test binary
	rm -f libgps
# Clean up after Python and QT
if HAVE_PYTHON
	(cd '$(srcdir)' && \
	env version='$(PACKAGE_VERSION)' abs_builddir='$(abs_builddir)' \
	    MAKE='$(MAKE)' \
	        $(PYTHON) setup.py clean \
	                --build-lib '$(srcdir)/$(PYTHON_DISTUTILS_LIBDIR)' \
	                --build-scripts '$(srcdir)/$(PYTHON_DISTUTILS_SCRIPTDIR)' \
	                --mangenerator '$(XMLPROC)')
	rm -rf $(srcdir)/build
	rm -f gps/*.so gps/*.pyc stamp-python*.tmp
	for i in $(PYTHONSCRIPTS_DIST); do rm -f $${i}c; done
endif
if LIB_Q_GPSMM_ENABLE
	if test -r $(srcdir)/libQgpsmm/Makefile; then \
		$(MAKE) -C $(srcdir)/libQgpsmm distclean || true;  \
	fi
	rm -rf $(srcdir)/libQgpsmm/binaries
	rm -f $(srcdir)/libQgpsmm/*.o $(srcdir)/libQgpsmm/Makefile
endif

# Install Python modules and QT library
install-exec-local:
if HAVE_PYTHON
# Make sure we do not use --root as option to setup.py install
# when DESTDIR is not defined as distutils would use the current
# working directory as root directory and not install to ${prefix}.
	if test -z "$(DESTDIR)"; then \
		env version='$(PACKAGE_VERSION)' MAKE=$(MAKE) $(PYTHON) setup.py install --prefix=${prefix} ;\
	else \
		env version='$(PACKAGE_VERSION)' MAKE=$(MAKE) $(PYTHON) setup.py install --prefix=${prefix} --root=$(DESTDIR) ;\
	fi
endif
if LIB_Q_GPSMM_ENABLE
	$(MAKE) -C libQgpsmm install INSTALL_ROOT=$(DESTDIR)
endif

#
# Build test_float
#
test_float_SOURCES = test_float.c
test_float_LDADD = $(LIBC) $(LIBM)

#
# Build test_trig
#
test_trig_SOURCES = test_trig.c
test_trig_LDADD = $(LIBC) $(LIBM)

if LIBGPSMM_ENABLE
#
# Build test_gpsmm
#
test_gpsmm_SOURCES = test_gpsmm.cpp
test_gpsmm_LDADD = $(LIBC) libgps.la $(LIBM)  $(LIBUSB_LIBS)
endif

if LIB_Q_GPSMM_ENABLE
#
# Build test_qgpsmm
#
test_qgpsmm_SOURCES = test_gpsmm.cpp
test_qgpsmm_LDFLAGS = -Wl,-rpath,$(srcdir)/libQgpsmm/binaries
test_qgpsmm_LDADD = $(LIBC) $(LIBUSB) $(QtNetwork_LIBS) libgps.la -LlibQgpsmm/binaries -lQgpsmm
test_qgpsmm_DEPENDENCIES = libQgpsmm/binaries/libQgpsmm.so
endif

#
# Build test_bits tester
#
test_bits_SOURCES = test_bits.c
test_bits_LDADD = $(LIBC) bits.o libgps.la $(LIBUSB_LIBS)

#
# Build packets tester
#
test_packet_SOURCES = test_packet.c
test_packet_LDADD = $(LIBC) libgpsd.la libgps.la $(LIBM)  $(LIBUSB_LIBS)

#
# Build geoid model tester
#
test_geoid_SOURCES = test_geoid.c
test_geoid_LDADD = $(LIBC) libgps.la $(LIBM)

#
# Build time functions tester
#
test_mkgmtime_SOURCES = test_mkgmtime.c
test_mkgmtime_LDADD = $(LIBC) libgps.la $(LIBM)

#
# Build JSON parse tester
test_json_SOURCES = test_json.c
test_json_LDADD = $(LIBC) libgps.la $(LIBM)

MANPAGES_BASE = \
	gpsd.8 \
	gps.1 \
	cgps.1 \
	lcdgps.1 \
	libgps.3 \
	libgpsmm.3 \
	libgpsd.3 \
	gpsmon.1 \
	gpsctl.1 \
	gpspipe.1 \
	gpsdecode.1 \
	srec.5

if HAVE_XSLT_PROCESSOR
MANPAGES_DIST = \
	$(MANPAGES_BASE) \
	$(PYTHONPAGES_DIST)

man_MANS = \
	$(MANPAGES_BASE) \
	$(PYTHONPAGES)

#
# Create Manpages
#
BUILT_MANPAGES = $(MANPAGES_DIST)

.xml.1:
	$(XMLPROC) $(XMLPROCFLAGS) $(MANTARGET) $<

.xml.3:
	$(XMLPROC) $(XMLPROCFLAGS) $(MANTARGET) $<

.xml.5:
	$(XMLPROC) $(XMLPROCFLAGS) $(MANTARGET) $<

.xml.8:
	$(XMLPROC) $(XMLPROCFLAGS) $(MANTARGET) $<

# Another instance of the multiple-outputs hack.
gps.1 xgps.1 xgpsspeed.1 cgps.1 lcdgps.1: stamp-gps-manpages
	+@WITNESS=stamp-gps-manpages; $(MULTIOUT_RECOVER_DELETED)
stamp-gps-manpages: gps.xml
	@rm -f '$@' '$@.tmp'
	echo 'timestamp for $@' > '$@.tmp' && \
	$(XMLPROC) $(XMLPROCFLAGS) $(MANTARGET) '$(srcdir)/gps.xml' && \
	mv -f '$@.tmp' '$@'
CLEANFILES += stamp-gps-manpages stamp-gps-manpages.tmp
endif

noinst_HEADERS = gpsd_config.h \
	driver_italk.h driver_superstar2.h \
	driver_ubx.h gpsmon.h gpsdclient.h json.h gps_json.h \
	revision.h TachometerP.h Tachometer.h
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
	srec.xml

# Note: packaging/rpm/gpsd.spec is generated, but it needs to be in the
# tarball in order for 'make dist-rpm' to work.  The BUILT_SOURCES
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
	leapsecond.py \
	packet_states.h \
	libgps.pc.in \
	libgpsd.pc.in \
	gpscap.ini \
	packaging/deb/etc_default_gpsd \
	packaging/deb/etc_init.d_gpsd \
	packaging/rpm/gpsd.spec \
	packaging/rpm/gpsd.init \
	packaging/rpm/gpsd.sysconfig \
	packaging/X11/xgps.desktop \
	packaging/X11/xgpsspeed.desktop \
	packaging/X11/gpsd-logo.png \
	do-tests \
	regress-driver \
	leapseconds.cache \
	$(PYTHONSCRIPTS_DIST) \
	$(PYTHONMODULES_DIST) \
	$(QTLIB_DIST) \
	$(QTLIB_DIST_MINGW) \
	test


# Prepare necessary files to build the mingw-port of libQgpsmm
# while creating the dist tarball.
dist-hook: ais_json.i gpsd_config.h
	$(MKDIR_P) '$(distdir)/libQgpsmm/mingw'
	cp -p ais_json.i $(distdir)/libQgpsmm/mingw
	grep "#define PACKAGE_VERSION" gpsd_config.h > $(distdir)/libQgpsmm/mingw/version.h
	echo "VERSION=$(libgps_VERSION)" > $(distdir)/libQgpsmm/mingw/version.pri
distclean-local:
	rm -f $(distdir)/libQgpsmm/mingw/version.* $(distdir)/libQgpsmm/mingw/ais_json.i


CLEANFILES += $(BUILT_SOURCES) *.core $(PYEXTENSIONS) $(BUILT_MANPAGES)

pkgconfig_DATA = libgps.pc libgpsd.pc
pkgconfigdir = $(libdir)/pkgconfig

# These are not distributed
libgps: libgps_core.c gps.h .libs/libgps.a
	$(CC) $(CFLAGS) -o libgps $(LIBM) -DTESTMAIN $(LIBPTHREAD) -g libgps_core.c .libs/libgps.a


# Report splint warnings
# Note: test_bits.c is unsplintable because of the PRI64 macros.
SPLINTOPTS = -I/usr/include/dbus-1.0/ -DSYSCONFDIR=\"$(sysconfdir)\" $(LIBUSB_CFLAGS) +quiet
splint: gpsd.h packet_names.h
	@echo "Running splint on daemon..."
	-splint $(SPLINTOPTS) -exportlocal -redef $(gpsd_c_sources)
	@echo "Running splint on libgpsd..."
	-splint $(SPLINTOPTS) -exportlocal -redef $(libgpsd_c_sources)
	@echo "Running splint on user-side libraries..."
	-splint $(SPLINTOPTS) -exportlocal -redef $(libgps_c_sources)
	@echo "Running splint on cgps..."
	-splint $(SPLINTOPTS) -exportlocal $(cgps_SOURCES)
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

# Check the documentation for bogons, too
xmllint: $(XML)
	for xml in $(XML); do xmllint --nonet --noout --valid $$xml; done

# Use deheader to remove headers not required.  If the statistics line
# ends with other than '0 removed' there's work to be done.
ENABLES = -DDEBUG -DPPS_ENABLE
NOHEADCHECK = -x 'cpp' -x 'contrib' -x 'gpspacket.c' -x 'gpsclient.c' -x 'monitor_proto.c'
deheader:
	make --quiet clean $(BUILT_SOURCES)
	deheader $(NOHEADCHECK) -i gpsd_config.h -i gpsd.h -m "MORECFLAGS='-Werror -Wfatal-errors $(ENABLES)' make -e"
	make --quiet clean

# Re-indent the codebase in a uniform style for readability.
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
	@echo $(PACKAGE_VERSION)

cflags:
	@echo $(CFLAGS)

#
# Regression tests begin here
#
# Note that the *-makeregress targets re-create the *.log.chk source
# files from the *.log source files.

# Our regression tests are make targets, while automake expects
# programs.  Thus, our approach is to construct the test
# infrastructure our way, with make targets, and to have one TEST from
# automake's viewpoint: a trivial shell script to invoke make with our
# top-level regression target.

# One might think that using TESTS_ENVIRONMENT=$(MAKE) would work
# around this, but because the generated rule (check-TESTS) both
# depends on each TEST as well as invokes it (with TESTS_ENVIRONMENT)
# the entire list of tests is run twice.

# Use make REGRESSOPTS=-u to force running with UDP rather than pty devices

run_regress_driver = PYTHON=$(PYTHON) $(srcdir)/regress-driver $(REGRESSOPTS)

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
		$(srcdir)/gpsdecode -j <$${f} >/tmp/test-$$$$.chk; \
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
	@echo "Testing AIVDM decoding..."
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

python-indentation-regress:
	set -e; for pyfile in $(PYTHONSCRIPTS_DIST) $(PYTHONMODULES_DIST); do \
		$(PYTHON) -tt -m py_compile $$pyfile ;\
	done

# Do all normal regression tests.
testregress: python-indentation-regress gps-regress rtcm-regress aivdm-regress packet-regress geoid-regress time-regress unpack-regress json-regress
	@echo "Regressions complete."

# do-tests is a shell script that invokes make with target testregress.
# This works around automake's lack of support for make targets as tests.
TESTS_ENVIRONMENT = MAKE=$(MAKE) PYTHON=$(PYTHON)
TESTS = do-tests

# The website directory
#
# None of these productions are fired by 'make all'.

if XMLTOSTDOUT
www/%.html: %.xml
	$(XMLPROC) $(XMLPROCFLAGS) $(HTMLTARGET) $< >$(<:.xml=.html) ; cp $(<:.xml=.html) $@
else
www/%.html: %.xml
	$(XMLPROC) $(XMLPROCFLAGS) $(HTMLTARGET) $<; cp $(<:.xml=.html) $@
endif

website: www/gpscat.html www/gpsctl.html www/gpsdecode.html \
		www/gpsd.html www/gpsfake.html www/gpsmon.html \
		www/gpspipe.html www/gpsprof.html www/gps.html \
		www/libgpsd.html www/libgpsmm.html www/libgps.html \
		www/srec.html \
		www/AIVDM.html www/NMEA.html \
		www/protocol-evolution.html www/protocol-transition.html \
		www/client-howto.html www/writing-a-driver.html \
		www/index.html www/hardware.html \
		www/performance/performance.html \
		www/internals.html

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

www/hardware.html: gpscap.py www/hardware-head.html gpscap.ini www/hardware-tail.html
	(cat www/hardware-head.html; python gpscap.py; cat www/hardware-tail.html) >www/hardware.html

www/performance/performance.html: www/performance/performance.xml
	(cd www/performance; xmlto xhtml-nochunks performance.xml)

www/internals.html: $(shell ls $(srcdir)/doc/*.xml)
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

endif

# Productions for setting up and performing udev tests.
#
# Requires root. Do "udev-install", then "tail -f /var/log/syslog" in
# another window, then run 'make udev-test', then plug and unplug the
# GPS ad libitum.  All is well when you get fix reports each time a GPS
# is plugged in.

udev-install:
	cp $(srcdir)/gpsd.rules /lib/udev/rules.d/25-gpsd.rules
	cp $(srcdir)/gpsd.hotplug $(srcdir)/gpsd.hotplug.wrapper /lib/udev/
	chmod a+x /lib/udev/gpsd.hotplug /lib/udev/gpsd.hotplug.wrapper

udev-uninstall:
	rm -f /lib/udev/{gpsd.hotplug,gpsd.hotplug.wrapper}
	rm -f /lib/udev/rules.d/25-gpsd.rules

udev-test:
	$(srcdir)/gpsd -N -n -F /var/run/gpsd.sock -D 5

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
	shasum gpsd-$(PACKAGE_VERSION).tar.gz >gpsd-$(PACKAGE_VERSION).sum
	lftp -c "open ftp://ftp.berlios.de/incoming; mput gpsd-$(PACKAGE_VERSION).tar.gz gpsd-$(PACKAGE_VERSION).sum"

#
# This is how to tag a release.
# It requires developer access verified via ssh.
#
release-tag:
	git tag -s -m "Tagged for external release $(PACKAGE_VERSION)" release-$(PACKAGE_VERSION)
	git push --tags

#
# Ship a release, providing all regression tests pass.
# The clean is necessary so that dist will remake revision.h
# with the current revision level in it.
#
ship: testregress clean dist upload-ftp release-tag

# End
