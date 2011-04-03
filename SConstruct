### SCons build recipe for the GPSD project

# Unfinished items:
# * Check for Python development libraries
# * Python module build
# * Qt binding
# * PYTHONPATH adjustment for Gentoo
# * Utility and test productions
# * Installation and uninstallation
# * Out-of-directory builds: see http://www.scons.org/wiki/UsingBuildDir

# Release identification begins here
gpsd_version = "3.0~dev"
libgps_major = 20
libgps_minor = 0
libgps_age   = 0
# Release identification ends here

EnsureSConsVersion(1,2,0)

import os, sys, commands, glob


#
# Build-control options
#

def internalize(s):
    return s.replace('-', '_')

boolopts = (
    # GPS protocols
    ("nmea",          True,  "NMEA support"),
    ("ashtech",       True,  "Ashtech support"),
    ("earthmate",     True,  "DeLorme EarthMate Zodiac support"),
    ("evermore",      True,  "EverMore binary support"),
    ("fv18",          True,  "San Jose Navigation FV-18 support"),
    ("garmin",        True,  "Garmin kernel driver support"),
    ("garmintxt",     True,  "Garmin Simple Text support"),
    ("geostar",       True,  "Geostar Protocol support"),
    ("itrax",         True,  "iTrax hardware support"),
    ("mtk3301",       True,  "MTK-3301 support"),
    ("navcom",        True,  "Navcom support"),
    ("oncore",        True,  "Motorola OnCore chipset support"),
    ("sirf",          True,  "SiRF chipset support"),
    ("superstar2",    True,  "Novatel SuperStarII chipset support"),
    ("tnt",           True,  "True North Technologies support"),
    ("tripmate",      True,  "DeLorme TripMate support"),
    ("tsip",          True,  "Trimble TSIP support"),
    ("ubx",           True,  "UBX Protocol support"),
    # Non-GPS protocols
    ("aivdm",         True,  "AIVDM support"),
    ("gpsclock",      True,  "GPSClock support"),
    ("ntrip",         True,  "NTRIP support"),
    ("oceanserver",   True,  "OceanServer support"),
    ("rtcm104v2",     True,  "rtcm104v2 support"),
    ("rtcm104v3",     True,  "rtcm104v3 support"),
    # Time service
    ("ntpshm",        True,  "NTP time hinting support"),
    ("pps",           True,  "PPS time syncing support"),
    ("pps_on_cts",    False, "PPS pulse on CTS rather than DCD"),
    # Export methods
    ("socket-export", True,  "data export over sockets"),
    ("dbus-export",   True,  "enable DBUS export support"),
    ("shm-export",    True,  "export via shared memory"),
    # Communication
    ("bluez",         False, "BlueZ support for Bluetooth devices"),
    ("ipv6",          True,  "build IPv6 support"),
    # Client-side options
    ("clientdebug",   True,  "client debugging support"),
    ("oldstyle",      True,  "oldstyle (pre-JSON) protocol support"),
    ("libgpsmm",      True,  "build C++ bindings"),
    ("libQgpsmm",     False, "build QT bindings"),
    ("reconfigure",   True,  "allow gpsd to change device settings"),
    ("controlsend",   True,  "allow gpsctl/gpsmon to change device settings"),
    ("cheapfloats",   True,  "float ops are cheap, compute error estimates"),
    ("squelch",       False, "squelch gpsd_report/gpsd_hexdump to save cpu"),
    # Miscellaneous
    ("profiling",     False, "Build with profiling enabled"),
    ("timing",        True,  "latency timing support"),
    ("control-socket",True,  "control socket for hotplug notifications")
    )
for (name, default, help) in boolopts:
    internal_name = internalize(name)
    if default:
        AddOption('--disable-'+ name,
                  dest=internal_name,
                  default=True,
                  action="store_false",
                  help=help)
    else:
        AddOption('--enable-'+ name,
                  dest=internal_name,
                  default=False,
                  action="store_true",
                  help=help)

nonboolopts = (
    ("gpsd-user",           "USER",     "privilege revocation user",     ""),
    ("gpsd-group",          "GROUP",    "privilege revocation group",    ""),
    ("prefix",              "PREFIX",   "installation path prefix",      "/usr/local/"),
    ("sysconfdir",          "SYCONFDIR","system configuration directory","/etc"),
    ("limited-max-clients", "CLIENTS",  "maximum allowed clients",       0),
    ("limited-max-devices", "DEVICES",  "maximum allowed devices",       0),
    ("fixed-port-speed",    "SPEED",    "fixed serial port speed",       0),
    )
for (name, metavar, help, default) in nonboolopts:
        internal_name = internalize(name)
        AddOption('--enable-'+ name,
                  dest=internal_name,
                  metavar=metavar,
                  default=default,
                  nargs=1, type='string',
                  help=help)

#
# Environment creation
#

env = Environment(tools=["default", "tar"], toolpath = ["scons"])
env.SConsignFile(".sconsign.dblite")

env['VERSION'] = gpsd_version

env.Append(LIBPATH=['.'])

# Placeholder so we can kluge together something like VPATH builds
env['SRCDIR'] = '.'

# Enable all GCC warnings except uninitialized and
# missing-field-initializers, which we can't help triggering because
# of the way some of the JSON code is generated.
# Also not including -Wcast-qual
if env['CC'] == 'gcc':
    env.Append(CFLAGS=Split('''-Wextra -Wall -Wno-uninitialized
                            -Wno-missing-field-initializers -Wcast-align
                            -Wmissing-declarations -Wmissing-prototypes
                            -Wstrict-prototypes -Wpointer-arith -Wreturn-type
                            -D_GNU_SOURCE'''))

# Should we build with profiling?
if GetOption('profiling'):
    env.Append(CCFLAGS=['-pg'])
    env.Append(LDFLAGS=['-pg'])

## Build help

if GetOption("help"):
    Return()

## Configuration

config = Configure(env)

confdefs = ["/* gpsd_config.h.  Generated by scons, do not hand-hack.  */\n\n"]

confdefs.append('#define VERSION "%s"\n\n' % gpsd_version)

cxx = config.CheckCXX()

for f in ("daemon", "strlcpy", "strlcat"):
    if config.CheckFunc(f):
        confdefs.append("#define HAVE_%s 1\n\n" % f.upper())
    else:
        confdefs.append("/* #undef HAVE_%s */\n\n" % f.upper())

if config.CheckLib('libncurses'):
    ncurseslibs = ["ncurses"]
else:
    ncurseslibs = []

if config.CheckLib('libusb-1.0'):
    confdefs.append("#define HAVE_LIBUSB 1\n\n")
    env.MergeFlags(['!pkg-config libusb-1.0 --cflags'])
    flags = env.ParseFlags('!pkg-config libusb-1.0 --libs')
    usblibs = flags['LIBS']
else:
    confdefs.append("/* #undef HAVE_LIBUSB */\n\n")
    usblibs = []

if config.CheckLib('libpthread'):
    confdefs.append("#define HAVE_LIBPTHREAD 1\n\n")
    # System library - no special flags
    pthreadlibs = ["pthread"]
else:
    confdefs.append("/* #undef HAVE_LIBPTHREAD */\n\n")
    pthreadlibs = []

if config.CheckLib('librt'):
    confdefs.append("#define HAVE_LIBRT 1\n\n")
    # System library - no special flags
    rtlibs = ["rt"]
else:
    confdefs.append("/* #undef HAVE_LIBRT */\n\n")
    rtlibs = []

if config.CheckLib('dbus-1'):
    env.MergeFlags(['!pkg-config --cflags dbus-glib-1'])
    dbus_xmit = env.ParseFlags('!pkg-config --libs dbus-1')
    dbus_xmit_libs = dbus_xmit['LIBS']
    dbus_recv = env.ParseFlags('!pkg-config --libs dbus-glib-1')
    dbus_recv_libs = dbus_recv['LIBS']
else:
    confdefs.append("/* #undef HAVE_LIBDBUS */\n\n")
    dbuslibs = []

if config.CheckLib('libbluez'):
    confdefs.append("#define HAVE_LIBBLUEZ 1\n\n")
    env.MergeFlags(['!pkg-config bluez --cflags'])
    flags = env.ParseFlags('!pkg-config bluez --libs')
    bluezlibs = flags['LIBS']
else:
    confdefs.append("/* #undef HAVE_LIBBLUEZ */\n\n")
    bluezlibs = []

if config.CheckHeader("pps.h"):
    confdefs.append("#define HAVE_PPS_H 1\n\n")
else:
    confdefs.append("/* #undef HAVE_PPS_H */\n\n")

if config.CheckHeader("sys/timepps.h"):
    confdefs.append("#define HAVE_SYS_TIMEPPS_H 1\n\n")
else:
    confdefs.append("/* #undef HAVE_SYS_TIMEPPS_H */\n\n")

# Map options to libraries required to support them that might be absent. 
optionrequires = {
    "bluez": ["libbluez"],
    "pps" : ["librt"],
    "dbus_export" : ["libdbus", "libdbus-glib"],
    }

keys = map(lambda x: x[0], boolopts) + map(lambda x: x[0], nonboolopts)
keys.sort()
for key in keys:
    key = internalize(key)
    value = GetOption(key)

    if value and key in optionrequires:
        for required in optionrequires[key]:
            if not config.CheckLib(required):
                print "%s not found, %s cannot be enabled." % (required, key)
                value = False
                break

    if type(value) == type(True):
        if value:
            confdefs.append("#define %s_ENABLE 1\n\n" % key.upper())
        else:
            confdefs.append("/* #undef %s_ENABLE */\n\n" % key.upper())
    elif value in (0, ""):
        confdefs.append("/* #undef %s */\n\n" % key.upper())
    else:
        if type(value) == type(-1):
            confdefs.append("#define %d %s\n\n" % (key.upper(), value))
        elif type(value) == type(""):
            confdefs.append("#define %s \"%s\"\n\n" % (key.upper(), value))
        else:
            raise ValueError

confdefs.append('''
/* will not handle pre-Intel Apples that can run big-endian */
#if defined __BIG_ENDIAN__
#define WORDS_BIGENDIAN 1
#else
#undef WORDS_BIGENDIAN
#endif

/* Some libcs do not have strlcat/strlcpy. Local copies are provided */
#ifndef HAVE_STRLCAT
# ifdef __cplusplus
extern "C" {
# endif
size_t strlcat(/*@out@*/char *dst, /*@in@*/const char *src, size_t size);
# ifdef __cplusplus
}
# endif
#endif
#ifndef HAVE_STRLCPY
# ifdef __cplusplus
extern "C" {
# endif
size_t strlcpy(/*@out@*/char *dst, /*@in@*/const char *src, size_t size);
# ifdef __cplusplus
}
# endif
#endif

#define GPSD_CONFIG_H
''')

with open("gpsd_config.h", "w") as ofp:
    ofp.writelines(confdefs)

if WhereIs("xsltproc"):
    docbook_url_stem = 'http://docbook.sourceforge.net/release/xsl/current/' 
    docbook_man_uri = docbook_url_stem + 'manpages/docbook.xsl'
    docbook_html_uri = docbook_url_stem + 'html/docbook.xsl'
    testpage = 'libgpsmm.xml'
    if not os.path.exists(testpage):
        print "What!? Test page is missing!"
        sys.exit(1)
    probe = "xsltproc --nonet --noout '%s' %s" % (docbook_man_uri, testpage)
    if commands.getstatusoutput(probe)[0] == 0:
        build = "xsltproc --nonet %s $SOURCE"
        htmlbuilder = build % docbook_html_uri
        manbuilder = build % docbook_man_uri
    elif WhereIs("xmlto"):
        print "xmlto is available"
        htmlbuilder = "xmlto html-nochunks $SOURCE"
        manbuilder = "xmlto man $SOURCE"
    else:
        print "Neither xsltproc nor xmlto found, documentation cannot be built."
        sys.exit(0)
env['BUILDERS']["Man"] = Builder(action=manbuilder)
env['BUILDERS']["HTML"] = Builder(action=htmlbuilder,
                                  src_suffix=".xml", suffix=".html")

env = config.Finish()

## Two shared libraries provide most of the code for the C programs

libgps_sources = [
	"ais_json.c",
	"daemon.c",
	"gpsutils.c",
	"geoid.c",
	"gpsdclient.c",
	"gps_maskdump.c",
	"hex.c",
	"json.c",
	"libgps_core.c",
	"libgps_json.c",
	"libgps_shm.c",
        "libgps_sock.c",
	"netlib.c",
	"rtcm2_json.c",
	"shared_json.c",
	"strl.c",
]
if cxx:
    libgps_sources.append("libgpsmm.cpp")

libgps_soname = "%d:%d:%d" % (libgps_major, libgps_minor, libgps_age)
compiled_gpslib = env.SharedLibrary(target="gps",
                                    LDFLAGS = '--version ' + libgps_soname,
                                    source=libgps_sources)

compiled_gpsdlib = env.SharedLibrary(target="gpsd", source=[
	"bits.c",
	"bsd-base64.c",
	"crc24q.c",
	"gpsd_json.c",
	"isgps.c",
	"libgpsd_core.c",
	"net_dgpsip.c",
	"net_gnss_dispatch.c",
	"net_ntrip.c",
	"packet.c",
	"pseudonmea.c",
	"serial.c",
	"srecord.c",
	"subframe.c",
	"timebase.c",
	"drivers.c",
	"driver_aivdm.c",
	"driver_evermore.c",
	"driver_garmin.c",
	"driver_garmin_txt.c",
	"driver_geostar.c",
	"driver_italk.c",
	"driver_navcom.c",
	"driver_nmea.c",
	"driver_oncore.c",
	"driver_rtcm2.c",
	"driver_rtcm3.c",
	"driver_sirf.c",
	"driver_superstar2.c",
	"driver_tsip.c",
	"driver_ubx.c",
	"driver_zodiac.c",
])

env.Default(compiled_gpsdlib, compiled_gpslib)

# The libraries have dependencies on system libraries

gpslibs = ["gps", "m"]
gpsdlibs = ["gpsd"] + usblibs + bluezlibs + gpslibs

## Production programs
gpsd = env.Program('gpsd', ['gpsd.c','ntpshm.c','shmexport.c','dbusexport.c'],
                   LIBS = gpsdlibs + pthreadlibs + rtlibs + dbus_xmit_libs)
gpsdecode = env.Program('gpsdecode', ['gpsdecode.c'], LIBS=gpsdlibs+pthreadlibs+rtlibs)
gpsctl = env.Program('gpsctl', ['gpsctl.c'], LIBS=gpsdlibs+pthreadlibs+rtlibs)
gpsmon = env.Program('gpsmon', [
                     'gpsmon.c',
                     'monitor_italk.c',
                     'monitor_nmea.c',
                     'monitor_oncore.c',
                     'monitor_sirf.c',
                     'monitor_superstar2.c',
                     'monitor_tnt.c',
                     'monitor_ubx.c',
                     ], LIBS=gpsdlibs)
gpspipe = env.Program('gpspipe', ['gpspipe.c'], LIBS=gpslibs)
gpxlogger = env.Program('gpxlogger', ['gpxlogger.c'], LIBS=gpslibs+dbus_recv_libs)
lcdgps = env.Program('lcdgps', ['lcdgps.c'], LIBS=gpslibs)
cgps = env.Program('cgps', ['cgps.c'], LIBS=gpslibs + ncurseslibs)

default_targets = [gpsd, gpsdecode, gpsctl, gpspipe, gpxlogger, lcdgps]
if ncurseslibs:
    default_targets += [cgps, gpsmon]
env.Default(*default_targets)

# Test programs
# TODO: conditionally add test_gpsmm and test_qgpsmm
test_float = env.Program('test_float', ['test_float.c'])
test_geoid = env.Program('test_geoid', ['test_geoid.c'], LIBS=gpslibs)
test_json = env.Program('test_json', ['test_json.c'], LIBS=gpslibs)
test_mkgmtime = env.Program('test_mkgmtime', ['test_mkgmtime.c'], LIBS=gpslibs)
test_trig = env.Program('test_trig', ['test_trig.c'], LIBS=["m"])
test_packet = env.Program('test_packet', ['test_packet.c'], LIBS=gpsdlibs)
test_bits = env.Program('test_bits', ['test_bits.c', "bits.c"])
test_gpsmm = env.Program('test_gpsmm', ['test_gpsmm.cpp'], LIBS=gpslibs)
testprogs = [test_float, test_trig, test_bits, test_packet,
             test_mkgmtime, test_geoid, test_json]
if cxx:
    testprogs.append(test_gpsmm)

env.Alias("buildtest",testprogs)

env.Default(testprogs)

# Python programs
python_progs = ["gpscat", "gpsfake", "gpsprof", "xgps", "xgpsspeed"]
python_modules = ["__init__.py", "misc.py", "fake.py", "gps.py", "client.py"]

#
# Special dependencies to make generated files
# TO-DO: Inline the Python scripts.
#

env.Command(target = "packet_names.h", source="packet_states.h", action="""
	rm -f $TARGET &&\
	sed -e '/^ *\([A-Z][A-Z0-9_]*\),/s//   \"\1\",/' <$SOURCE >$TARGET &&\
	chmod a-w $TARGET""")

env.Command(target="timebase.h", source="leapseconds.cache",
            action='python leapsecond.py -h $SOURCE >$TARGET')

env.Command(target="gpsd.h", source="gpsd_config.h", action="""\
	rm -f $TARGET &&\
	echo \"/* This file is generated.  Do not hand-hack it! */\" >$TARGET &&\
	cat $TARGET-head >>$TARGET &&\
	cat gpsd_config.h >>$TARGET &&\
	cat $TARGET-tail >>$TARGET &&\
	chmod a-w $TARGET""")
Depends(target="gpsd.h", dependency="gpsd.h-head")
Depends(target="gpsd.h", dependency="gpsd.h-tail")

env.Command(target="gps_maskdump.c", source="maskaudit.py", action='''
	rm -f $TARGET &&\
        python $SOURCE -c $SRCDIR >$TARGET &&\
        chmod a-w $TARGET''')
Depends(target="gps_maskdump.c", dependency="gps.h")
Depends(target="gps_maskdump.c", dependency="gpsd.h")

env.Command(target="ais_json.i", source="jsongen.py", action='''\
	rm -f $TARGET &&\
	python $SOURCE --ais --target=parser >$TARGET &&\
	chmod a-w $TARGET''')

generated_sources = ['packet_names.h', 'timebase.h', 'gpsd.h',
                     'gps_maskdump.c', 'ais_json.c']
#env.Clean(generated_sources)

# Under autotools this depended on Makefile. We need it to depend
# on the state of the build-system variables.
env.Command(target="revision.h", source="gpsd_config.h", action='''
	rm -f $TARGET &&\
	python -c \'from datetime import datetime; print "#define REVISION \\"%s\\"\\n" % (datetime.now().isoformat()[:-4])\' >$TARGET &&\
	chmod a-w revision.h''')

# leapseconds.cache is a local cache for information on leapseconds issued
# by the U.S. Naval observatory. It gets kept in the repository so we can
# build without Internet access.
env.Command(target="leapseconds.cache", source="leapsecond.py",
            action='python $SOURCE -f $TARGET')

# Instantiate some file templates.  We'd like to use the Substfile builtin
# but it doesn't seem to work in scons 1.20
def substituter(target, source, env):
    substmap = (
        ('@VERSION@', gpsd_version),
        ('@prefix@',  GetOption('prefix')),
        ('@PYTHON@',  "python"),
        )
    with open(str(source[0])) as sfp:
        content = sfp.read()
    for (s, t) in substmap:
        content = content.replace(s, t)
    with open(str(target[0]), "w") as tfp:
        tfp.write(content)
for fn in ("packaging/rpm/gpsd.spec.in", "libgps.pc.in", "libgpsd.pc.in",
       "jsongen.py.in", "maskaudit.py.in", "valgrind-audit.in"):
    builder = env.Command(source=fn, target=fn[:-3], action=substituter)
    post1 = env.AddPostAction(builder, 'chmod -w $TARGET')
    if fn.endswith(".py.in"):
        env.AddPostAction(post1, 'chmod +x $TARGET')

# Documentation

base_manpages = {
    "gpsd.8" : "gpsd.xml",
    "gpsd_json.5" : "gpsd_json.xml",
    "gps.1" : "gps.xml",
    "cgps.1" : "gps.xml",
    "lcdgps.1" : "gps.xml",
    "libgps.3" : "libgps.xml",
    "libgpsmm.3" : "libgpsmm.xml",
    "libgpsd.3" : "libgpsd.xml",
    "gpsmon.1": "gpsmon.xml",
    "gpsctl.1" : "gpsctl.xml",
    "gpspipe.1" : "gpspipe.xml",
    "gpsdecode.1" : "gpsdecode.xml",
    "srec.5" : "srec.xml",
    }
python_manpages = {
    "gpsprof.1" : "gpsprof.xml",
    "gpsfake.1" : "gpsfake.xml",
    "gpscat.1" : "gpscat.xml",
    "xgpsspeed.1" : "gps.xml",
    "xgps.1" : "gps.xml",
    }
qt_manpages = {
    "libQgpsmm.3" : "libQgpsmm.xml",
    }

manpage_targets = []
for (man, xml) in base_manpages.items():
    manpage_targets.append(env.Man(source=xml, target=man))
env.Default(*manpage_targets)

# Utility productions

def Utility(target, source, action):
    target = env.Command(target=target, source=source, action=action)
    env.AlwaysBuild(target)
    env.Precious(target)
    return target

Utility("cppcheck", ["gpsd.h", "packet_names.h"],
        "cppcheck --template gcc --all --force $SRCDIR")

# Check the documentation for bogons, too
Utility("xmllint", glob.glob("*.xml"),
	"for xml in $SOURCES; do xmllint --nonet --noout --valid $$xml; done")

## MORE GOES HERE

#
# Regression tests begin here
#
# Note that the *-makeregress targets re-create the *.log.chk source
# files from the *.log source files.

# Regression-test the daemon
Utility("gps-regress", [gpsd],
        '$SRCDIR/regress-driver test/daemon/*.log')

# Test that super-raw mode works. Compare each logfile against itself 
# dumped through the daemon running in R=2 mode.  (This test is not
# included in the normal regressions.)
Utility("raw-regress", [gpsd],
	'$SRCDIR/regress-driver test/daemon/*.log')

# Build the regression tests for the daemon.
Utility('gps-makeregress', [gpsd],
	'$SRCDIR/regress-driver -b test/daemon/*.log')

# To build an individual test for a load named foo.log, put it in
# test/daemon and do this:
#	regress-driver -b test/daemon/foo.log

## MORE GOES HERE

# Productions for setting up and performing udev tests.
#
# Requires root. Do "udev-install", then "tail -f /var/log/syslog" in
# another window, then run 'make udev-test', then plug and unplug the
# GPS ad libitum.  All is well when you get fix reports each time a GPS
# is plugged in.

Utility('udev-install', '', [
	'cp $(srcdir)/gpsd.rules /lib/udev/rules.d/25-gpsd.rules',
	'cp $(srcdir)/gpsd.hotplug $(srcdir)/gpsd.hotplug.wrapper /lib/udev/',
	'chmod a+x /lib/udev/gpsd.hotplug /lib/udev/gpsd.hotplug.wrapper',
        ])

Utility('udev-uninstall', '', [
	'rm -f /lib/udev/{gpsd.hotplug,gpsd.hotplug.wrapper}',
	'rm -f /lib/udev/rules.d/25-gpsd.rules',
        ])

Utility('udev-test', '', [
	'$(srcdir)/gpsd -N -n -F /var/run/gpsd.sock -D 5',
        ])
        
# Release machinery begins here
#

distfiles = commands.getoutput(r"git ls-files |egrep -v '^(www|devtools|packaging|repo)'")
distfiles = distfiles.split()
distfiles.remove(".gitignore")
distfiles += generated_sources
distfiles += base_manpages.keys() + python_manpages.keys() + qt_manpages.keys()

tarball = env.Command('tarball', distfiles, [
    '@tar -czf gpsd-${VERSION}.tar.gz $SOURCES',
    '@ls -l gpsd-${VERSION}.tar.gz',
    ])
#env.Clean("gpsd-${VERSION}.tar.gz")

# Make RPM from the specfile in packaging
Utility('dist-rpm', tarball, 'rpm -ta $SOURCE')

# This is how to ship a release to Berlios incoming.
# It requires developer access verified via ssh.
#
Utility('upload-ftp', tarball, [
	'shasum gpsd-${VERSION}.tar.gz >gpsd-${VERSION}.sum',
	'lftp -c "open ftp://ftp.berlios.de/incoming; mput $SOURCE gpsd-${VERSION}.sum"',
        ])

#
# This is how to tag a release.
# It requires developer access verified via ssh.
#
Utility("release-tag", '', [
	'git tag -s -m "Tagged for external release $VERSION" release-$VERSION',
	'git push --tags'
        ])

#
# Ship a release, providing all regression tests pass.
# The clean is necessary so that dist will remake revision.h
# with the current revision level in it.
#
#Utility('ship', '', [testregress, tarball, upload-ftp, release-tag])


# The following sets edit modes for GNU EMACS
# Local Variables:
# mode:python
# End:
