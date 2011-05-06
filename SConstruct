### SCons build recipe for the GPSD project

# Important targets:
#
# build     - build the software (default)
# dist      - make distribution tarball
# install   - install programs, libraties, and manual pages
# uninstall - undo an install
#
# check     - run regression and unit tests.
# splint    - run the splint static tester on the code
# cppcheck  - run the cppcheck static tester on the code
# xmllint   - run xmllint on the documentation
# testbuild - test-build the code from a tarball

# Unfinished items:
# * Qt binding (needs to build .pc, .prl files)
# * Allow building for multiple python versions)
# * Out-of-directory builds: see http://www.scons.org/wiki/UsingBuildDir
#
# Setting the DESTDIR environment variable will prefix the install destinations
# without changing the --prefix prefix.

# Release identification begins here
gpsd_version = "3.0~dev"
libgps_major = 20
libgps_minor = 0
libgps_age   = 0
# Release identification ends here

EnsureSConsVersion(1,2,0)

import copy, os, sys, commands, glob, re
from distutils import sysconfig
from distutils.util import get_platform
from distutils import sysconfig
import SCons

#
# Build-control options
#

# Start by reading configuration variables from the cache
opts = Variables('.scons-option-cache')

systemd = os.path.exists("/usr/share/systemd/system")

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
    ("socket_export", True,  "data export over sockets"),
    ("dbus_export",   False,  "enable DBUS export support"),
    ("shm_export",    True,  "export via shared memory"),
    # Communication
    ('usb',           True,  "libusb support for USB devices"),
    ("bluez",         True,  "BlueZ support for Bluetooth devices"),
    ("ipv6",          True,  "build IPv6 support"),
    # Other daemon options
    ("timing",        True,  "latency timing support"),
    ("control_socket",True,  "control socket for hotplug notifications"),
    ("systemd",       systemd, "systemd socket activation"),
    # Client-side options
    ("clientdebug",   True,  "client debugging support"),
    ("oldstyle",      True,  "oldstyle (pre-JSON) protocol support"),
    ("libgpsmm",      True,  "build C++ bindings"),
    ("libQgpsmm",     True,  "build QT bindings"),
    ("reconfigure",   True,  "allow gpsd to change device settings"),
    ("controlsend",   True,  "allow gpsctl/gpsmon to change device settings"),
    ("cheapfloats",   True,  "float ops are cheap, compute error estimates"),
    ("squelch",       False, "squelch gpsd_report/gpsd_hexdump to save cpu"),
    ("ncurses",       True,  "build with ncurses"),
    # Build control
    ("shared",        True,  "build shared libraries, not static"),
    ("debug",         False, "include debug information in build"),
    ("profiling",     False, "build with profiling enabled"),
    )
for (name, default, help) in boolopts:
    opts.Add(BoolVariable(name, help, default))

nonboolopts = (
    ("gpsd_user",           "privilege revocation user",     ""),
    ("gpsd_group",          "privilege revocation group",    ""),
    ("prefix",              "installation directory prefix", "/usr/local/"),
    ("limited_max_clients", "maximum allowed clients",       0),
    ("limited_max_devices", "maximum allowed devices",       0),
    ("fixed_port_speed",    "fixed serial port speed",       0),
    ("target",              "cross-development target",      ""),
    )
for (name, help, default) in nonboolopts:
    opts.Add(name, help, default)

pathopts = (
    ("sysconfdir",  "system configuration directory",        "/etc"),
    ("bindir",      "application binaries directory",        "/bin"),
    ("includedir",  "header file directory",                 "/include"),
    ("libdir",      "system libraries",                      "/lib"),
    ("sbindir",     "system binaries directory",             "/sbin"),
    ("mandir",      "manual pages directory",                "/share/man"),
    ("docdir",      "documents directory",                   "/share/doc"),
    )
for (name, help, default) in pathopts:
    opts.Add(PathVariable(name, help, default, PathVariable.PathAccept))


#
# Environment creation
#

env = Environment(tools=["default", "tar", "textfile"], options=opts)
opts.Save('.scons-option-cache', env)
env.SConsignFile(".sconsign.dblite")

env['VERSION'] = gpsd_version
env['PYTHON'] = sys.executable

# set defaults from environment
env['STRIP'] = "strip"
env['CHRPATH'] = 'chrpath'
for i in ["AR", "ARFLAGS", "CCFLAGS", "CFLAGS", "CC", "CXX", "CXXFLAGS", "STRIP", "CHRPATH", "LD", "TAR"]:
    if os.environ.has_key(i):
        j = i
        if i == "LD":
            i = "SHLINK"
        env[j]=os.getenv(i)
for flags in ["LDFLAGS", "CPPFLAGS"]:
    if os.environ.has_key(flags):
        env.MergeFlags([os.getenv(flags)])


# Placeholder so we can kluge together something like VPATH builds.
# $SRCDIR replaces occurrences for $(srcdir) in the autotools build.
env['SRCDIR'] = '.'


# define a helper function for pkg-config - we need to pass
# --static for static linking, too.
if env["shared"]:
    pkg_config = lambda pkg: ['!pkg-config --cflags --libs %s' %(pkg, )]
else:
    pkg_config = lambda pkg: ['!pkg-config --cflags --libs --static %s' %(pkg, )]




# DESTDIR environment variable means user wants to prefix the installation root.
DESTDIR = os.environ.get('DESTDIR', '')

if env['CC'] == 'gcc':
    # Enable all GCC warnings except uninitialized and
    # missing-field-initializers, which we can't help triggering because
    # of the way some of the JSON code is generated.
    # Also not including -Wcast-qual
    env.Append(CFLAGS=Split('''-Wextra -Wall -Wno-uninitialized
                            -Wno-missing-field-initializers -Wcast-align
                            -Wmissing-declarations -Wmissing-prototypes
                            -Wstrict-prototypes -Wpointer-arith -Wreturn-type
                            -D_GNU_SOURCE'''))

# Honor the specified installation prefix in link paths.
env.Prepend(LIBPATH=[os.path.join(env['prefix'], 'lib')])
env.Prepend(RPATH=[os.path.join(env['prefix'], 'lib')])

# Tell generated binaries to look in the current directory for
# shared libraries. Should be handled sanely by scons on all systems.
# Not good to use '.' or a relative path here; it's a security risk.
# At install time we should use chrpath to edit this out of RPATH.
env.Prepend(LIBPATH=[os.path.realpath(os.curdir)])
env.Prepend(RPATH=[os.path.realpath(os.curdir)])

# Give deheader a way to set compiler flags
if 'MORECFLAGS' in os.environ:
    env.Append(CFLAGS=Split(os.environ['MORECFLAGS']))

# Should we build with profiling?
if env['profiling']:
    env.Append(CCFLAGS=['-pg'])
    env.Append(LDFLAGS=['-pg'])

# Get a slight speedup by not doing automatic RCS and SCCS fetches.
env.SourceCode('.', None)

# Should we build with debug symbols?
if env['debug']:
    env.Append(CCFLAGS=['-g'])
    env.Append(CCFLAGS=['-O0'])
else:
    env.Append(CCFLAGS=['-O2'])

## Cross-development

devenv = (("ADDR2LINE", "addr2line"),
          ("AR","ar"),
          ("AS","as"),
          ("CXX","c++"),
          ("CXXFILT","c++filt"),
          ("CPP","cpp"),
          ("GXX","g++"),
          ("CC","gcc"),
          ("GCCBUG","gccbug"),
          ("GCOV","gcov"),
          ("GPROF","gprof"),
          ("LD", "ld"),
          ("NM", "nm"),
          ("OBJCOPY","objcopy"),
          ("OBJDUMP","objdump"),
          ("RANLIB", "ranlib"),
          ("READELF","readelf"),
          ("SIZE", "size"),
          ("STRINGS", "strings"),
          ("STRIP", "strip"))

if env['target']:
    for (name, toolname) in devenv:
        env[name] = env['target'] + '-' + toolname

## Build help

Help("""Arguments may be a mixture of switches and targets in any order.
Switches apply to the entire build regardless of where they are in the order.
Important switches include:

    prefix=/usr     probably what you want for production tools

Options are cached in a file named .scons-option-cache and persist to later
invocations.  The file is editable.  Delete it to start fresh.  Current option
values can be listed with 'scons -h'.

""" + opts.GenerateHelpText(env, sort=cmp))

if "help" in ARGLIST:
    Return()

## Configuration

def CheckPKG(context, name):
    context.Message( 'Checking for %s... ' % name )
    ret = context.TryAction('pkg-config --exists \'%s\'' % name)[0]
    context.Result( ret )
    return ret

def CheckExecutable(context, testprogram, check_for):
    context.Message( 'Checking for %s... ' %(check_for,))
    ret = context.TryAction(testprogram)[0]
    context.Result( ret )
    return ret

# Stylesheet URLs for making HTML and man pages from DocBook XML.
docbook_url_stem = 'http://docbook.sourceforge.net/release/xsl/current/'
docbook_man_uri = docbook_url_stem + 'manpages/docbook.xsl'
docbook_html_uri = docbook_url_stem + 'html/docbook.xsl'

def CheckXsltproc(context):
    context.Message('Checking that xsltproc can make man pages... ')
    with open("xmltest.xml", "w") as ofp:
        ofp.write('''
       <refentry id="foo.1">
      <refmeta>
        <refentrytitle>foo</refentrytitle>
        <manvolnum>1</manvolnum>
        <refmiscinfo class='date'>9 Aug 2004</refmiscinfo>
      </refmeta>
      <refnamediv id='name'>
        <refname>foo</refname>
        <refpurpose>check man page generation from docbook source</refpurpose>
      </refnamediv>
    </refentry>
''')
    probe = "xsltproc --nonet --noout '%s' xmltest.xml" % (docbook_man_uri,)
    ret = context.TryAction(probe)[0]
    os.remove("xmltest.xml")
    if os.path.exists("foo.1"):
        os.remove("foo.1")
    context.Result( ret )
    return ret

config = Configure(env, custom_tests = { 'CheckPKG' : CheckPKG,
                                         'CheckExecutable' : CheckExecutable,
                                         'CheckXsltproc' : CheckXsltproc})

confdefs = ["/* gpsd_config.h.  Generated by scons, do not hand-hack.  */\n\n"]

confdefs.append('#define VERSION "%s"\n\n' % gpsd_version)

cxx = config.CheckCXX()

for f in ("daemon", "strlcpy", "strlcat"):
    if config.CheckFunc(f):
        confdefs.append("#define HAVE_%s 1\n\n" % f.upper())
    else:
        confdefs.append("/* #undef HAVE_%s */\n\n" % f.upper())

if env['ncurses']:
    if config.CheckPKG('ncurses'):
        ncurseslibs = pkg_config('ncurses')
    elif config.CheckExecutable('ncurses5-config --version', 'ncurses5-config'):
        ncurseslibs = ['!ncurses5-config --libs --cflags']
    else:
        ncurseslibs= []
else:
    ncurseslibs= []

if env['usb'] and config.CheckPKG('libusb-1.0'):
    confdefs.append("#define HAVE_LIBUSB 1\n\n")
    usblibs = pkg_config('libusb-1.0')
else:
    confdefs.append("/* #undef HAVE_LIBUSB */\n\n")
    usblibs = []

if config.CheckLib('librt'):
    confdefs.append("#define HAVE_LIBRT 1\n\n")
    # System library - no special flags
    rtlibs = ["-lrt"]
else:
    confdefs.append("/* #undef HAVE_LIBRT */\n\n")
    rtlibs = []

if env['dbus_export'] and config.CheckPKG('dbus-1') and config.CheckPKG('dbus-glib-1'):
    confdefs.append("#define HAVE_DBUS 1\n\n")
    dbus_xmit_libs = pkg_config('dbus-1')
    dbus_recv_libs = pkg_config('dbus-glib-1')
else:
    confdefs.append("/* #undef HAVE_DBUS */\n\n")
    dbus_xmit_libs = []
    dbus_recv_libs = []

if env['bluez'] and config.CheckPKG('bluez'):
    confdefs.append("#define HAVE_BLUEZ 1\n\n")
    bluezlibs = pkg_config('bluez')
else:
    confdefs.append("/* #undef HAVE_BLUEZ */\n\n")
    bluezlibs = []


if config.CheckHeader("sys/timepps.h"):
    confdefs.append("#define HAVE_SYS_TIMEPPS_H 1\n\n")
else:
    confdefs.append("/* #undef HAVE_SYS_TIMEPPS_H */\n\n")

if config.CheckExecutable('$CHRPATH -v', 'chrpath'):
    have_chrpath = True
else:
    have_chrpath = False

# Map options to libraries required to support them that might be absent.
optionrequires = {
    "bluez": ["libbluetooth"],
    "dbus_export" : ["libdbus-1", "libdbus-glib-1"],
    }

keys = map(lambda x: (x[0],x[2]), boolopts) + map(lambda x: (x[0],x[2]), nonboolopts) + map(lambda x: (x[0],x[2]), pathopts)
keys.sort()
for (key,help) in keys:
    value = env[key]
    if value and key in optionrequires:
        for required in optionrequires[key]:
            if not config.CheckLib(required):
                print "%s not found, %s cannot be enabled." % (required, key)
                value = False
                break

    confdefs.append("/* %s */\n"%help)
    if type(value) == type(True):
        if value:
            confdefs.append("#define %s_ENABLE 1\n\n" % key.upper())
        else:
            confdefs.append("/* #undef %s_ENABLE */\n\n" % key.upper())
    elif value in (0, ""):
        confdefs.append("/* #undef %s */\n\n" % key.upper())
    else:
        if value.isdigit():
            confdefs.append("#define %s %s\n\n" % (key.upper(), value))
        else:
            confdefs.append("#define %s \"%s\"\n\n" % (key.upper(), value))


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


manbuilder = mangenerator = htmlbuilder = None
if config.CheckXsltproc():
    mangenerator = 'xsltproc'
    build = "xsltproc --nonet %s $SOURCE >$TARGET"
    htmlbuilder = build % docbook_html_uri
    manbuilder = build % docbook_man_uri
elif WhereIs("xmlto"):
    mangenerator = 'xmlto'
    htmlbuilder = "xmlto html-nochunks $SOURCE; mv `basename $TARGET` $TARGET"
    manbuilder = "xmlto man $SOURCE; mv `basename $TARGET` $TARGET"
else:
    print "Neither xsltproc nor xmlto found, documentation cannot be built."
if manbuilder:
    env['BUILDERS']["Man"] = Builder(action=manbuilder)
    env['BUILDERS']["HTML"] = Builder(action=htmlbuilder,
                                      src_suffix=".xml", suffix=".html")

qt_network = config.CheckPKG('QtNetwork')

env = config.Finish()

# Be explicit about what we're doing.
changelatch = False 
for (name, default, help) in boolopts:
    if env[name] != default:
        if not changelatch:
            print "Altered configuration variables:"
            changelatch = True
        print "%s = %s (default %s): %s" % (name, env[name], default, help)
for (name, help, default) in nonboolopts + pathopts:
    if env[name] != default:
        if not changelatch:
            print "Altered configuration variables:"
            changelatch = True
        print "%s = %s (default %s): %s" % (name, env[name], default, help)
if not changelatch:
    print "All configuration flags are defaulted."

# Gentoo systems can have a problem with the Python path
if os.path.exists("/etc/gentoo-release"):
    print "This is a Gentoo system."
    print "Adjust your PYTHONPATH to see library directories under /usr/local/lib"

# Should we build the Qt binding?
if cxx and env['libQgpsmm'] and qt_network:
    qt_env = env.Clone()
    qt_env.MergeFlags('-DUSE_QT')
    qt_env.MergeFlags(pkg_config('QtNetwork'))
else:
    qt_env = None

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

if cxx and env['libgpsmm']:
    libgps_sources.append("libgpsmm.cpp")

libgpsd_sources = [
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
]

# Cope with scons's failure to set SONAME in its builtins.
# Inspired by Richard Levitte's (slightly buggy) code at
# http://markmail.org/message/spttz3o4xrsftofr

def VersionedSharedLibrary(env, libname, libversion, lib_objs=[], parse_flags=[]):
    platform = env.subst('$PLATFORM')
    shlib_pre_action = None
    shlib_suffix = env.subst('$SHLIBSUFFIX')
    shlib_post_action = None
    shlink_flags = SCons.Util.CLVar(env.subst('$SHLINKFLAGS'))

    if platform == 'posix':
        shlib_post_action = ['rm -f $TARGET','ln -s ${SOURCE.file} $TARGET']
        shlib_post_action_output_re = [
            '%s\\.[0-9\\.]*$' % re.escape(shlib_suffix),
            shlib_suffix ]
        shlib_suffix += '.' + libversion
        shlink_flags += [ '-Wl,-Bsymbolic', '-Wl,-soname=${TARGET}' ]
    elif platform == 'aix':
        shlib_pre_action = [
            "nm -Pg $SOURCES &gt; ${TARGET}.tmp1",
            "grep ' [BDT] ' &lt; ${TARGET}.tmp1 &gt; ${TARGET}.tmp2",
            "cut -f1 -d' ' &lt; ${TARGET}.tmp2 &gt; ${TARGET}",
            "rm -f ${TARGET}.tmp[12]" ]
        shlib_pre_action_output_re = [ '$', '.exp' ]
        shlib_post_action = [ 'rm -f $TARGET', 'ln -s $SOURCE $TARGET' ]
        shlib_post_action_output_re = [
            '%s\\.[0-9\\.]*' % re.escape(shlib_suffix),
            shlib_suffix ]
        shlib_suffix += '.' + libversion
        shlink_flags += ['-G', '-bE:${TARGET}.exp', '-bM:SRE']
    elif platform == 'cygwin':
        shlink_flags += [ '-Wl,-Bsymbolic',
                          '-Wl,--out-implib,${TARGET.base}.a' ]
    elif platform == 'darwin':
        shlib_suffix = '.' + libversion + shlib_suffix
        shlink_flags += [ '-current_version', '%s' % libversion,
                          '-undefined', 'dynamic_lookup' ]

    lib = env.SharedLibrary(libname,lib_objs,
                            SHLIBSUFFIX=shlib_suffix,
                            SHLINKFLAGS=shlink_flags, parse_flags=parse_flags)

    if shlib_pre_action:
        shlib_pre_action_output = re.sub(shlib_pre_action_output_re[0],
                                         shlib_pre_action_output_re[1],
                                         str(lib[0]))
        env.Command(shlib_pre_action_output, [ lib_objs ],
                     shlib_pre_action)
        env.Depends(lib, shlib_pre_action_output)
    if shlib_post_action:
        shlib_post_action_output = re.sub(shlib_post_action_output_re[0],
                                          shlib_post_action_output_re[1],
                                          str(lib[0]))
        env.Command(shlib_post_action_output, lib, shlib_post_action)
    return lib

def InstallVersionedSharedLibrary(env, destination, lib):
    platform = env.subst('$PLATFORM')
    shlib_suffix = env.subst('$SHLIBSUFFIX')
    shlib_install_pre_action = None
    shlib_install_post_action = None

    if platform == 'posix':
        shlib_post_action = [ 'rm -f $TARGET',
                              'ln -s ${SOURCE.file} $TARGET' ]
        shlib_post_action_output_re = ['%s\\.[0-9\\.]*$' % re.escape(shlib_suffix),
                                       shlib_suffix ]
        shlib_install_post_action = shlib_post_action
        shlib_install_post_action_output_re = shlib_post_action_output_re

    ilib = env.Install(destination, lib)

    if shlib_install_pre_action:
        shlib_install_pre_action_output = re.sub(shlib_install_pre_action_output_re[0],
                                                 shlib_install_pre_action_output_re[1],
                                                 str(ilib[0]))
        env.Command(shlib_install_pre_action_output, ilib,
                    shlib_install_pre_action)
        env.Depends(shlib_install_pre_action_output, ilib)

    if shlib_install_post_action:
        shlib_install_post_action_output = re.sub(shlib_install_post_action_output_re[0],
                                                  shlib_install_post_action_output_re[1],
                                                  str(ilib[0]))
        env.Command(shlib_install_post_action_output, ilib,
                    shlib_install_post_action)
    return ilib

if not env["shared"]:
    def Library(env, target, sources, version, parse_flags=[]):
        return env.StaticLibrary(target, sources, parse_flags=parse_flags)
    LibraryInstall = lambda env, libdir, sources: env.Install(libdir, sources)
else:
    def Library(env, target, sources, version, parse_flags=[]):
        return VersionedSharedLibrary(env=env,
                                     libname=target,
                                     libversion=version,
                                     lib_objs=sources,
                                     parse_flags=parse_flags)
    LibraryInstall = lambda env, libdir, sources: \
                     InstallVersionedSharedLibrary(env, libdir, sources)

# Klugery to handle sonames ends

libversion = "%d.%d.%d" % (libgps_major, libgps_minor, libgps_age)

compiled_gpslib = Library(env=env,
                          target="gps",
                          sources=libgps_sources,
                          version=libversion)

compiled_gpsdlib = Library(env=env,
                           target="gpsd",
                           sources=libgpsd_sources,
                           version=libversion, parse_flags=usblibs + rtlibs + bluezlibs)

if qt_env:
    qtobjects = []
    qt_flags = qt_env['CFLAGS']
    for c_only in ('-Wmissing-prototypes', '-Wstrict-prototypes'):
        if c_only in qt_flags:
            qt_flags.remove(c_only)
    # Qt binding object files have to be renamed as they're built to avoid
    # name clashes with the plain non-Qt object files. This prevents the
    # infamous "Two environments with different actions were specified
    # for the same target" error.
    for src in libgps_sources:
        if src in ("gpsutils.c", "libgps_sock.c"):
            compile_with = qt_env['CXX']
            compile_flags = qt_flags
        else:
            compile_with = qt_env['CC']
            compile_flags = qt_env['CFLAGS']
        qtobjects.append(qt_env.SharedObject(src.split(".")[0] + '-qt', src,
                                       CC=compile_with, CFLAGS=compile_flags))
    compiled_qgpsmmlib = Library(qt_env, "Qgpsmm", qtobjects, libversion)

# The libraries have dependencies on system libraries

gpslibs = ["-lgps", "-lm"]
gpsdlibs = ["-lgpsd"] + usblibs + bluezlibs + gpslibs

# Source groups

gpsd_sources = ['gpsd.c','ntpshm.c','shmexport.c','dbusexport.c']

if env['systemd']:
    gpsd_sources.append("sd_socket.c")

gpsmon_sources = [
    'gpsmon.c',
    'monitor_italk.c',
    'monitor_nmea.c',
    'monitor_oncore.c',
    'monitor_sirf.c',
    'monitor_superstar2.c',
    'monitor_tnt.c',
    'monitor_ubx.c',
    ]

## Production programs
# Don't lose the RPATH when building gpsd
gpsd_env = env.Clone()
gpsd_env.MergeFlags("-pthread")
gpsd = gpsd_env.Program('gpsd', gpsd_sources,
                        parse_flags = gpsdlibs + rtlibs + dbus_xmit_libs)
gpsdecode = env.Program('gpsdecode', ['gpsdecode.c'], parse_flags=gpsdlibs+rtlibs)
gpsctl = env.Program('gpsctl', ['gpsctl.c'], parse_flags=gpsdlibs+rtlibs)
gpsmon = env.Program('gpsmon', gpsmon_sources, parse_flags=gpsdlibs + ncurseslibs)
gpspipe = env.Program('gpspipe', ['gpspipe.c'], parse_flags=gpslibs)
gpxlogger = env.Program('gpxlogger', ['gpxlogger.c'], parse_flags=gpslibs+dbus_recv_libs)
lcdgps = env.Program('lcdgps', ['lcdgps.c'], parse_flags=gpslibs)
cgps = env.Program('cgps', ['cgps.c'], parse_flags=gpslibs + ncurseslibs)

binaries = [gpsd, gpsdecode, gpsctl, gpspipe, gpxlogger, lcdgps]
if ncurseslibs:
    binaries += [cgps, gpsmon]

# Test programs
test_float = env.Program('test_float', ['test_float.c'])
test_geoid = env.Program('test_geoid', ['test_geoid.c'], parse_flags=gpslibs)
test_json = env.Program('test_json', ['test_json.c'], parse_flags=gpslibs)
test_mkgmtime = env.Program('test_mkgmtime', ['test_mkgmtime.c'], parse_flags=gpslibs)
test_trig = env.Program('test_trig', ['test_trig.c'], parse_flags=["-lm"])
test_packet = env.Program('test_packet', ['test_packet.c'], parse_flags=gpsdlibs)
test_bits = env.Program('test_bits', ['test_bits.c', "bits.c"])
test_gpsmm = env.Program('test_gpsmm', ['test_gpsmm.cpp'], parse_flags=gpslibs)
test_libgps = env.Program('test_libgps', ['test_libgps.c'], parse_flags=gpslibs)
testprogs = [test_float, test_trig, test_bits, test_packet,
             test_mkgmtime, test_geoid, test_json, test_libgps]
if cxx and env["libgpsmm"]:
    testprogs.append(test_gpsmm)

# Python programs
python_progs = ["gpscat", "gpsfake", "gpsprof", "xgps", "xgpsspeed"]
python_modules = Glob('gps/*.py') 

# Build Python binding
#
python_extensions = {
    "gps" + os.sep + "packet" : ["gpspacket.c", "packet.c", "isgps.c",
                                    "driver_rtcm2.c", "strl.c", "hex.c", "crc24q.c"],
    "gps" + os.sep + "clienthelpers" : ["gpsclient.c", "geoid.c", "gpsdclient.c", "strl.c"]
}
 
python_env = env.Clone()
vars = sysconfig.get_config_vars('CC', 'CXX', 'OPT', 'BASECFLAGS', 'CCSHARED', 'LDSHARED', 'SO', 'INCLUDEPY')
for i in range(len(vars)):
    if vars[i] is None:
        vars[i] = ""
(cc, cxx, opt, basecflags, ccshared, ldshared, so_ext, includepy) = vars
# in case CC/CXX was set to the scan-build wrapper,
# ensure that we build the python modules with scan-build, too
if env['CC'] is None or env['CC'].find('scan-build') < 0:
    python_env['CC'] = cc
else:
    python_env['CC'] = ' '.join([env['CC']] + cc.split()[1:])
if env['CXX'] is None or env['CXX'].find('scan-build') < 0:
    python_env['CXX'] = cxx
else:
    python_env['CXX'] = ' '.join([env['CXX']] + cxx.split()[1:])

python_env['SHLINKFLAGS'] = []
python_env['SHLINK'] = ldshared
python_env['SHLIBPREFIX']=""
python_env['SHLIBSUFFIX']=so_ext
python_env['CPPPATH'] =[includepy]
python_env['CPPFLAGS']=basecflags + " " + opt
python_objects={}
python_compiled_libs = {}
for ext, sources in python_extensions.iteritems():
    python_objects[ext] = []
    for src in sources:
        python_objects[ext].append(python_env.SharedObject(src.split(".")[0] + '-py', src))
    python_compiled_libs[ext] = python_env.SharedLibrary(ext, python_objects[ext])
python_built_extensions = python_compiled_libs.values()

python_egg_info_source = """Metadata-Version: 1.0
Name: gps
Version: %s
Summary: Python libraries for the gpsd service daemon
Home-page: http://gpsd.berlios.de/
Author: the GPSD project
Author-email: gpsd-dev@lists.berlios.de
License: BSD
Description: The gpsd service daemon can monitor one or more GPS devices connected to a host computer, making all data on the location and movements of the sensors available to be queried on TCP port 2947.
Platform: UNKNOWN
""" %(gpsd_version, )
python_egg_info = python_env.Textfile(target="gps-%s.egg-info" %(gpsd_version, ), source=python_egg_info_source)

env.Command(target = "packet_names.h", source="packet_states.h", action="""
    rm -f $TARGET &&\
    sed -e '/^ *\([A-Z][A-Z0-9_]*\),/s//   \"\1\",/' <$SOURCE >$TARGET &&\
    chmod a-w $TARGET""")

# build timebase.h
def timebase_h(target, source, env):
    from leapsecond import make_leapsecond_include
    with open(target[0].abspath, 'w') as f:
        f.write(make_leapsecond_include(source[0].abspath))
env.Command(target="timebase.h", source="leapseconds.cache",
            action=timebase_h)

env.Textfile(target="gpsd_config.h", source=confdefs)
env.Textfile(target="gpsd.h", source=[File("gpsd.h-head"), File("gpsd_config.h"), File("gpsd.h-tail")])

env.Command(target="gps_maskdump.c", source=["maskaudit.py", "gps.h", "gpsd.h"], action='''
    rm -f $TARGET &&\
        $PYTHON $SOURCE -c $SRCDIR >$TARGET &&\
        chmod a-w $TARGET''')

env.Command(target="ais_json.i", source="jsongen.py", action='''\
    rm -f $TARGET &&\
    $PYTHON $SOURCE --ais --target=parser >$TARGET &&\
    chmod a-w $TARGET''')

generated_sources = ['packet_names.h', 'timebase.h', 'gpsd.h',
                     'gps_maskdump.c', 'ais_json.c']


# generate revision.h
(st, rev) = commands.getstatusoutput('git describe')
if st != 0:
    from datetime import datetime
    rev = datetime.now().isoformat()[:-4]
revision='#define REVISION "%s"' %(rev.strip(),)
env.Textfile(target="revision.h", source=[revision])

# leapseconds.cache is a local cache for information on leapseconds issued
# by the U.S. Naval observatory. It gets kept in the repository so we can
# build without Internet access.
from leapsecond import save_leapseconds
leapseconds_cache = lambda target, source, env: save_leapseconds(target[0].abspath)
env.NoClean(env.Command(target="leapseconds.cache", source="leapsecond.py",
            action=leapseconds_cache))

# Instantiate some file templates.  We'd like to use the Substfile builtin
# but it doesn't seem to work in scons 1.20
def substituter(target, source, env):
    substmap = (
        ('@VERSION@', gpsd_version),
        ('@prefix@',  env['prefix']),
        ('@PYTHON@',  sys.executable),
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
    "libQgpsmm.3" : "libgpsmm.xml",
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

manpage_targets = []
if manbuilder:
    for (man, xml) in base_manpages.items() + python_manpages.items():
        manpage_targets.append(env.Man(source=xml, target=man))

## Where it all comes together

build = env.Alias('build', [binaries, python_built_extensions, manpage_targets])
env.Clean(build, glob.glob("*.o") + glob.glob("*.os"))
env.Default(*build)

if qt_env:
    build_qt = qt_env.Alias('build', [compiled_qgpsmmlib])
    qt_env.Default(*build_qt)

build_python = python_env.Alias('build', [python_built_extensions])
python_env.Default(*build_python)

## Installation and deinstallation

# Not here because too distro-specific: udev rules, desktop files, init scripts

for (name, help, default) in pathopts:
    exec name + " = DESTDIR + env['prefix'] + env['%s']" % name

headerinstall = [ env.Install(includedir, x) for x in ("libgpsmm.h", "gps.h")]

binaryinstall = []
binaryinstall.append(env.Install(sbindir, gpsd))
binaryinstall.append(env.Install(bindir,  [gpsdecode, gpsctl, gpspipe, gpxlogger, lcdgps]))
if ncurseslibs:
    binaryinstall.append(env.Install(bindir, [cgps, gpsmon]))
binaryinstall.append(LibraryInstall(env, libdir, compiled_gpslib))
binaryinstall.append(LibraryInstall(env, libdir, compiled_gpsdlib))

if qt_env:
    binaryinstall.append(LibraryInstall(qt_env, libdir, compiled_qgpsmmlib))

if have_chrpath:
    env.AddPostAction(binaryinstall, '$CHRPATH -r $LIBDIR $TARGET')
if not env['debug'] or env['profiling']:
    env.AddPostAction(binaryinstall, '$STRIP $TARGET')

python_lib_dir = sysconfig.get_python_lib(
                plat_specific=1,
                standard_lib=0,
                prefix=env['prefix']
            )
python_module_dir = python_lib_dir + os.sep + 'gps'
python_extensions_install = python_env.Install( DESTDIR + python_module_dir,
                                                python_built_extensions)
if not env['debug'] or env['profiling']:
    python_env.AddPostAction(python_extensions_install, '$STRIP $TARGET')

python_modules_install = python_env.Install( DESTDIR + python_module_dir,
                                            python_modules)

python_progs_install = python_env.Install(bindir, python_progs)

python_egg_info_install = python_env.Install(DESTDIR + python_lib_dir, python_egg_info)
python_install = [  python_extensions_install,
                    python_modules_install,
                    python_progs_install,
                    python_egg_info_install]

pkgconfigdir = libdir + os.sep + 'pkgconfig'
pc_install = [ env.Install(pkgconfigdir, "libgps.pc") ]

maninstall = []
for manpage in base_manpages:
    section = manpage.split(".")[1]
    dest = os.path.join(mandir, "man"+section, manpage)
    maninstall.append(env.InstallAs(source=manpage, target=dest))
install = env.Alias('install', binaryinstall + maninstall + python_install + pc_install + headerinstall)

def Uninstall(nodes):
    deletes = []
    for node in nodes:
        if node.__class__ == install[0].__class__:
            deletes.append(Uninstall(node.sources))
        else:
            deletes.append(Delete(str(node)))
    return deletes
uninstall = env.Command('uninstall', '', Flatten(Uninstall(Alias("install"))) or "")
env.AlwaysBuild(uninstall)
env.Precious(uninstall)

# Utility productions

def Utility(target, source, action):
    target = env.Command(target=target, source=source, action=action)
    env.AlwaysBuild(target)
    env.Precious(target)
    return target

# Report splint warnings
# Note: test_bits.c is unsplintable because of the PRI64 macros.
env['SPLINTOPTS'] = "-I/usr/include/libusb-1.0 +quiet"

def Splint(target,sources, description, params):
    return Utility(target,sources,[
            '@echo "Running splint on %s..."'%description,
            '-splint $SPLINTOPTS %s %s'%(" ".join(params)," ".join(sources)),
            ])

splint_table = [
    ('splint-daemon',gpsd_sources,'daemon', ['-exportlocal', '-redef']),
    ('splint-libgpsd',libgpsd_sources,'libgpsd', ['-exportlocal', '-redef']),
    ('splint-libgps',libgps_sources,'user-side libraries', ['-exportlocal',
                                                            '-fileextensions',
                                                            '-redef']),
    ('splint-cgps',['cgps.c'],'cgps', ['-exportlocal']),
    ('splint-gpsctl',['gpsctl.c'],'gpsctl', ['']),
    ('splint-gpsmon',gpsmon_sources,'gpsmon', ['-exportlocal']),
    ('splint-gpspipe',['gpspipe.c'],'gpspipe', ['']),
    ('splint-gpsdecode',['gpsdecode.c'],'gpsdecode', ['']),
    ('splint-gpxlogger',['gpxlogger.c'],'gpxlogger', ['']),
    ('splint-test_packet',['test_packet.c'],'test_packet test harness', ['']),
    ('splint-test_mkgmtime',['test_mkgmtime.c'],'test_mkgmtime test harness', ['']),
    ('splint-test_geoid',['test_geoid.c'],'test_geoid test harness', ['']),
    ('splint-test_json',['test_json.c'],'test_json test harness', ['']),
    ]

for (target,sources,description,params) in splint_table:
    env.Alias('splint',Splint(target,sources,description,params))


Utility("cppcheck", ["gpsd.h", "packet_names.h"],
        "cppcheck --template gcc --all --force $SRCDIR")

# Check the documentation for bogons, too
Utility("xmllint", glob.glob("*.xml"),
    "for xml in $SOURCES; do xmllint --nonet --noout --valid $$xml; done")

# Use deheader to remove headers not required.  If the statistics line
# ends with other than '0 removed' there's work to be done.
Utility("deheader", generated_sources, [
    'deheader -x cpp -x contrib -x gpspacket.c -x gpsclient.c -x monitor_proto.c -i gpsd_config.h -i gpsd.h -m "MORECFLAGS=\'-Werror -Wfatal-errors -DDEBUG -DPPS_ENABLE\' scons -Q"',
        ])

env.Alias('checkall', ['cppcheck','xmllint','splint'])

#
# Regression tests begin here
#
# Note that the *-makeregress targets re-create the *.log.chk source
# files from the *.log source files.

# Check that all Python modules compile properly 
def check_compile(target, source, env):
    for pyfile in source:
        'cp %s tmp.py'%(pyfile)
        '%s -tt -m py_compile tmp.py' %(sys.executable, )
        'rm -f tmp.py tmp.pyc'
python_compilation_regress = Utility('python-comilation-regress',
        Glob('*.py') + python_modules + python_progs + ['SConstruct'], check_compile)

# Regression-test the daemon
gps_regress = Utility("gps-regress", [gpsd, python_built_extensions],
        '$SRCDIR/regress-driver test/daemon/*.log')

# Test that super-raw mode works. Compare each logfile against itself
# dumped through the daemon running in R=2 mode.  (This test is not
# included in the normal regressions.)
Utility("raw-regress", [gpsd, python_built_extensions],
    '$SRCDIR/regress-driver test/daemon/*.log')

# Build the regression tests for the daemon.
Utility('gps-makeregress', [gpsd, python_built_extensions],
    '$SRCDIR/regress-driver -b test/daemon/*.log')

# To build an individual test for a load named foo.log, put it in
# test/daemon and do this:
#    regress-driver -b test/daemon/foo.log

# Regression-test the RTCM decoder.
rtcm_regress = Utility('rtcm-regress', [gpsdecode], [
    '@echo "Testing RTCM decoding..."',
    'for f in $SRCDIR/test/*.rtcm2; do '
        'echo "Testing $${f}..."; '
        '$SRCDIR/gpsdecode -j <$${f} >/tmp/test-$$$$.chk; '
        'diff -ub $${f}.chk /tmp/test-$$$$.chk; '
    'done;',
    '@echo "Testing idempotency of JSON dump/decode for RTCM2"',
    '$SRCDIR/gpsdecode -e -j <test/synthetic-rtcm2.json >/tmp/test-$$$$.chk; '
        'grep -v "^#" test/synthetic-rtcm2.json | diff -ub - /tmp/test-$$$$.chk; '
        'rm /tmp/test-$$$$.chk',
        ])

# Rebuild the RTCM regression tests.
Utility('rtcm-makeregress', [gpsdecode], [
    'for f in $SRCDIR/test/*.rtcm2; do '
        '$SRCDIR/gpsdecode -j < ${f} > ${f}.chk; '
    'done'
        ])

# Regression-test the AIVDM decoder.
aivdm_regress = Utility('aivdm-regress', [gpsdecode], [
    '@echo "Testing AIVDM decoding..."',
    'for f in $SRCDIR/test/*.aivdm; do '
        'echo "Testing $${f}..."; '
        '$SRCDIR/gpsdecode -u -c <$${f} >/tmp/test-$$$$.chk; '
        'diff -ub $${f}.chk /tmp/test-$$$$.chk; '
    'done;',
    '@echo "Testing idempotency of JSON dump/decode for AIS"',
    '$SRCDIR/gpsdecode -e -j <$SRCDIR/test/synthetic-ais.json >/tmp/test-$$$$.chk; '
        'grep -v "^#" $SRCDIR/test/synthetic-ais.json | diff -ub - /tmp/test-$$$$.chk; '
        'rm /tmp/test-$$$$.chk',
        ])

# Rebuild the AIVDM regression tests.
Utility('aivdm-makeregress', [gpsdecode], [
    'for f in $SRCDIR/test/*.aivdm; do '
        '$SRCDIR/gpsdecode -u -c <$${f} > $${f}.chk; '
    'done',
        ])

# Regression-test the packet getter.
packet_regress = Utility('packet-regress', [test_packet], [
    '@echo "Testing detection of invalid packets..."',
    '$SRCDIR/test_packet | diff -u $SRCDIR/test/packet.test.chk -',
    ])

# Rebuild the packet-getter regression test
Utility('packet-makeregress', [test_packet], [
    '$SRCDIR/test_packet >$SRCDIR/test/packet.test.chk',
    ])

# Rebuild the geoid test
Utility('geoid-makeregress', [test_geoid], [
    '$SRCDIR/test_geoid 37.371192 122.014965 >$SRCDIR/test/geoid.test.chk'])

# Regression-test the geoid tester.
geoid_regress = Utility('geoid-regress', [test_geoid], [
    '@echo "Testing the geoid model..."',
    '$SRCDIR/test_geoid 37.371192 122.014965 | diff -u $SRCDIR/test/geoid.test.chk -',
    ])

# Regression-test the Maidenhead Locator
maidenhead_locator_regress = Utility('maidenhead-locator-regress', [], [
    '@echo "Testing the Maidenhead Locator conversion..."',
    '$SRCDIR/test_maidenhead.py >/dev/null',
    ])

# Regression-test the calendar functions
time_regress = Utility('time-regress', [test_mkgmtime], [
    '$SRCDIR/test_mkgmtime'
    ])

# Regression test the unpacking code in libgps
unpack_regress = Utility('unpack-regress', [test_libgps], [
    '@echo "Testing the client-library sentence decoder..."',
    '$SRCDIR/regress-driver -c $SRCDIR/test/clientlib/*.log',
    ])

# Build the regression test for the sentence unpacker
Utility('unpack-makeregress', [test_libgps], [
    '@echo "Rebuilding the client sentence-unpacker tests..."',
    '$SRCDIR/regress-driver -c -b $SRCDIR/test/clientlib/*.log'
    ])

# Unit-test the JSON parsing
json_regress = Utility('json-regress', [test_json], [
    '$SRCDIR/test_json'
    ])

# Unit-test the bitfield extractor - not in normal tests
bits_regress = Utility('bits-regress', [test_bits], [
    '$SRCDIR/test_bits'
    ])

# Run all normal regression tests
check = env.Alias('check', [
    python_compilation_regress,
    gps_regress,
    rtcm_regress,
    aivdm_regress,
    packet_regress,
    geoid_regress,
    maidenhead_locator_regress,
    time_regress,
    unpack_regress,
    json_regress])

env.Alias('testregress', check)

# The website directory
#
# None of these productions are fired by default.
# The content they handle is the GPSD website, not included in release tarballs.

env.Alias('website', Split('''
    www/gpscat.html www/gpsctl.html www/gpsdecode.html 
    www/gpsd.html www/gpsfake.html www/gpsmon.html 
    www/gpspipe.html www/gpsprof.html www/gps.html 
    www/libgpsd.html www/libgpsmm.html www/libgps.html
    www/srec.html
    www/AIVDM.html www/NMEA.html
    www/protocol-evolution.html www/protocol-transition.html
    www/client-howto.html www/writing-a-driver.html
    www/index.html www/hardware.html
    www/performance/performance.html
    www/internals.html
    '''))

# asciidoc documents
if env.WhereIs('asciidoc'):
    for stem in ['AIVDM', 'NMEA',
                 'protocol-evolution', 'protocol-transition'
                 'client-howto']:
        env.Command('www/%s.html' % stem, 'www/%s.txt' % stem,    
                ['asciidoc -a toc -o www/%s.html www/%s.txt' % (stem, stem)])

if htmlbuilder:
    # DocBook documents
    for stem in ['writing-a-driver', 'performance/performance']:
        env.HTML('www/%s.html' % stem, 'www/%s.xml' % stem)

    # The internals manual.
    # Doesn't capture dependencies on the subpages
    env.HTML('www/internals.html', '$SRCDIR/doc/explanation.xml')

# The index page
env.Command('www/index.html', 'www/index.html.in',
            ['sed -e "/@DATE@/s//`date \'+%B %d, %Y\'`/" <$SOURCE >$TARGET'])

# The hardware page
env.Command('www/hardware.html', ['gpscap.py',
                                  'www/hardware-head.html',
                                  'gpscap.ini',
                                  'www/hardware-tail.html'],
            ['(cat www/hardware-head.html; $PYTHON gpscap.py; cat www/hardware-tail.html) >www/hardware.html'])


# Experimenting with pydoc.  Not yet fired by any other productions.

env.Alias('pydoc', "www/pydoc/index.html")

# We need to run epydoc with the Python version we built the modules for.
# So we define our own epydoc instead of using /usr/bin/epydoc
EPYDOC = "python -c 'from epydoc.cli import cli; cli()'"
env.Command('www/pydoc/index.html', python_progs + glob.glob("*.py")  + glob.glob("gps/*.py"), [
    'mkdir -p www/pydoc',
    EPYDOC + " -v --html --graph all -n GPSD $SOURCES -o www/pydoc",
        ])

# Productions for setting up and performing udev tests.
#
# Requires root. Do "udev-install", then "tail -f /var/log/syslog" in
# another window, then run 'make udev-test', then plug and unplug the
# GPS ad libitum.  All is well when you get fix reports each time a GPS
# is plugged in.

Utility('udev-install', '', [
    'cp $SRCDIR/gpsd.rules /lib/udev/rules.d/25-gpsd.rules',
    'cp $SRCDIR/gpsd.hotplug $SRCDIR/gpsd.hotplug.wrapper /lib/udev/',
    'chmod a+x /lib/udev/gpsd.hotplug /lib/udev/gpsd.hotplug.wrapper',
        ])

Utility('udev-uninstall', '', [
    'rm -f /lib/udev/{gpsd.hotplug,gpsd.hotplug.wrapper}',
    'rm -f /lib/udev/rules.d/25-gpsd.rules',
        ])

Utility('udev-test', '', [
    '$SRCDIR/gpsd -N -n -F /var/run/gpsd.sock -D 5',
        ])

# Release machinery begins here
#
# We need to be in the actual project repo (i.e. not doing a -Y build)
# for these productions to work.

if os.path.exists("gpsd.c") and os.path.exists(".gitignore"):
    distfiles = commands.getoutput(r"git ls-files").split()
    distfiles.remove(".gitignore")
    distfiles += generated_sources
    distfiles += base_manpages.keys() + python_manpages.keys()
    if "packaging/rpm/gpsd.spec" not in distfiles:
        distfiles.append("packaging/rpm/gpsd.spec")

    dist = env.Command('dist', distfiles, [
        '@tar --transform "s:^:gpsd-${VERSION}/:" -czf gpsd-${VERSION}.tar.gz $SOURCES',
        '@ls -l gpsd-${VERSION}.tar.gz',
        ])
    env.Clean(dist, "gpsd-${VERSION}.tar.gz")

    # Make RPM from the specfile in packaging
    Utility('dist-rpm', dist, 'rpmbuild -ta $SOURCE')

    # Make sure build-from-tarball works.
    Utility('testbuild', [dist], [
        'tar -xzvf gpsd-${VERSION}.tar.gz',
        'cd gpsd-${VERSION}; ./configure; make',
        'rm -fr gpsd-${VERSION}',
        ])

    # This is how to ship a release to Berlios incoming.
    # It requires developer access verified via ssh.
    #
    upload_ftp = Utility('upload-ftp', dist, [
            'shasum gpsd-${VERSION}.tar.gz >gpsd-${VERSION}.sum',
            'lftp -c "open ftp://ftp.berlios.de/incoming; mput $SOURCE gpsd-${VERSION}.sum"',
            ])

    #
    # This is how to tag a release.
    # It requires developer access verified via ssh.
    #
    release_tag = Utility("release-tag", '', [
            'git tag -s -m "Tagged for external release $VERSION" release-$VERSION',
            'git push --tags'
            ])

    #
    # Ship a release, providing all regression tests pass.
    # The clean is necessary so that dist will remake revision.h
    # with the current revision level in it.
    #
    Utility('ship', '', [check, dist, upload_ftp, release_tag])

# The following sets edit modes for GNU EMACS
# Local Variables:
# mode:python
# End:
