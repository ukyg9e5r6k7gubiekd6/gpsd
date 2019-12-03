# SCons build recipe for the GPSD project

# Important targets:
#
# build      - build the software (default)
# dist       - make distribution tarball (requires GNU tar)
# install    - install programs, libraries, and manual pages
# uninstall  - undo an install
#
# check      - run regression and unit tests.
# audit      - run code-auditing tools
# testbuild  - test-build the code from a tarball
# website    - refresh the website
# release    - ship a release
#
# --clean    - clean all normal build targets
# sconsclean - clean up scons dotfiles (but not the database: .sconsign.*.dblite)
#
# Setting the DESTDIR environment variable will prefix the install destinations
# without changing the --prefix prefix.

# Unfinished items:
# * Out-of-directory builds: see http://www.scons.org/wiki/UsingBuildDir
# * Coveraging mode: gcc "-coverage" flag requires a hack
#   for building the python bindings
# * Python 3 compatibility in this recipe

# Since SCons 3.0.0 forces print_function on us, it needs to be unconditional.
# This is recognized to be a bug in SCons, but we need to live with it for now,
# and we'll need this for eventual Python 3 compatibility, anyway.
# Python requires this to precede any non-comment code.
from __future__ import print_function

import ast
import functools
import glob
import imp         # for imp.find_module('gps'), imp deprecated in 3.4
# for conditional_leapsecond_fetch(), make_leapsecond_include()
import leapsecond
import operator
import os
import pickle
import re
# replacement for functions from the commands module, which is deprecated.
import subprocess
import sys
import time
from distutils import sysconfig
import SCons


# Release identification begins here.
#
# Actual releases follow the normal X.Y or X.Y.Z scheme.  The version
# number in git between releases has the form X.Y~dev, when it is
# expected that X.Y will be the next actual release.  As an example,
# when 3.18.1 is the last release, and 3.19 is the expected next
# release, the version in git will be 3.19~dev.  Note that ~ is used,
# because there is some precedent, ~ is an allowed version number in
# the Debian version rules, and it does not cause confusion with
# whether - separates components of the package name, separates the
# name from the version, or separates version componnents.
#
# Keep in sync with gps/__init__.py
# There are about 16 files with copies of the version number; make
# sure to update all of them.
#
# package version
gpsd_version = "3.19.1~dev"
# client library version
libgps_version_current = 25
libgps_version_revision = 0
libgps_version_age = 0
libgps_version = "%d.%d.%d" % (libgps_version_current, libgps_version_age,
                               libgps_version_revision)
#
# Release identification ends here

# Hosting information (mainly used for templating web pages) begins here
# Each variable foo has a corresponding @FOO@ expanded in .in files.
# There are no project-dependent URLs or references to the hosting site
# anywhere else in the distribution; preserve this property!
annmail = "gpsd-announce@nongnu.org"
bugtracker = "https://gitlab.com/gpsd/gpsd/issues"
cgiupload = "root@thyrsus.com:/var/www/cgi-bin/"
clonerepo = "git@gitlab.com:gpsd/gpsd.git"
devmail = "gpsd-dev@lists.nongnu.org"
download = "http://download-mirror.savannah.gnu.org/releases/gpsd/"
formserver = "www@thyrsus.com"
gitrepo = "git@gitlab.com:gpsd/gpsd.git"
ircchan = "irc://chat.freenode.net/#gpsd"
mailman = "https://lists.nongnu.org/mailman/listinfo/"
mainpage = "https://gpsd.io"
projectpage = "https://gitlab.com/gpsd/gpsd"
scpupload = "garyemiller@dl.sv.nongnu.org:/releases/gpsd/"
sitename = "GPSD"
sitesearch = "gpsd.io"
tiplink = "<a href='https://www.patreon.com/esr'>" \
          "leave a remittance at Patreon</a>"
tipwidget = '<p><a href="https://www.patreon.com/esr">' \
            'Donate here to support continuing development.</a></p>'
usermail = "gpsd-users@lists.nongnu.org"
webform = "http://www.thyrsus.com/cgi-bin/gps_report.cgi"
website = "https://gpsd.io/"
# Hosting information ends here

# gpsd needs Scons version at least 2.3
EnsureSConsVersion(2, 3, 0)
# gpsd needs Python version at least 2.6
EnsurePythonVersion(2, 6)


PYTHON_SYSCONFIG_IMPORT = 'from distutils import sysconfig'

# Utility productions


def Utility(target, source, action, **kwargs):
    target = env.Command(target=target, source=source, action=action, **kwargs)
    env.AlwaysBuild(target)
    env.Precious(target)
    return target


def UtilityWithHerald(herald, target, source, action, **kwargs):
    if not env.GetOption('silent'):
        action = ['@echo "%s"' % herald] + action
    return Utility(target=target, source=source, action=action, **kwargs)


def _getstatusoutput(cmd, nput=None, shell=True, cwd=None, env=None):
    pipe = subprocess.Popen(cmd, shell=shell, cwd=cwd, env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (output, errout) = pipe.communicate(input=nput)
    status = pipe.returncode
    return (status, output)


def _getoutput(cmd, nput=None, shell=True, cwd=None, env=None):
    return _getstatusoutput(cmd, nput, shell, cwd, env)[1]


# Spawn replacement that suppresses non-error stderr
def filtered_spawn(sh, escape, cmd, args, env):
    proc = subprocess.Popen([sh, '-c', ' '.join(args)],
                            env=env, close_fds=True, stderr=subprocess.PIPE)
    _, stderr = proc.communicate()
    if proc.returncode:
        sys.stderr.write(stderr)
    return proc.returncode

#
# Build-control options
#


# without this, scons will not rebuild an existing target when the
# source changes.
Decider('timestamp-match')

# support building with various Python versions.
sconsign_file = '.sconsign.{}.dblite'.format(pickle.HIGHEST_PROTOCOL)
SConsignFile(sconsign_file)

# Start by reading configuration variables from the cache
opts = Variables('.scons-option-cache')

systemd_dir = '/lib/systemd/system'
systemd = os.path.exists(systemd_dir)

# Set distribution-specific defaults here
imloads = True

boolopts = (
    # GPS protocols
    ("ashtech",       True,  "Ashtech support"),
    ("earthmate",     True,  "DeLorme EarthMate Zodiac support"),
    ("evermore",      True,  "EverMore binary support"),
    ("fury",          True,  "Jackson Labs Fury and Firefly support"),
    ("fv18",          True,  "San Jose Navigation FV-18 support"),
    ("garmin",        True,  "Garmin kernel driver support"),
    ("garmintxt",     True,  "Garmin Simple Text support"),
    ("geostar",       True,  "Geostar Protocol support"),
    ("greis",         True,  "Javad GREIS support"),
    ("itrax",         True,  "iTrax hardware support"),
    ("mtk3301",       True,  "MTK-3301 support"),
    ("navcom",        True,  "Navcom NCT support"),
    ("nmea0183",      True,  "NMEA0183 support"),
    ("nmea2000",      True,  "NMEA2000/CAN support"),
    ("oncore",        True,  "Motorola OnCore chipset support"),
    ("sirf",          True,  "SiRF chipset support"),
    ("skytraq",       True,  "Skytraq chipset support"),
    ("superstar2",    True,  "Novatel SuperStarII chipset support"),
    ("tnt",           True,  "True North Technologies support"),
    ("tripmate",      True,  "DeLorme TripMate support"),
    ("tsip",          True,  "Trimble TSIP support"),
    ("ublox",         True,  "u-blox Protocol support"),
    # Non-GPS protocols
    ("aivdm",         True,  "AIVDM support"),
    ("gpsclock",      True,  "GPSClock support"),
    ("isync",         True,  "Spectratime iSync LNRClok/GRCLOK support"),
    ("ntrip",         True,  "NTRIP support"),
    ("oceanserver",   True,  "OceanServer support"),
    ("passthrough",   True,  "build support for passing through JSON"),
    ("rtcm104v2",     True,  "rtcm104v2 support"),
    ("rtcm104v3",     True,  "rtcm104v3 support"),
    # Time service
    ("oscillator",    True,  "Disciplined oscillator support"),
    # Export methods
    ("dbus_export",   True,  "enable DBUS export support"),
    ("shm_export",    True,  "export via shared memory"),
    ("socket_export", True,  "data export over sockets"),
    # Communication
    ("bluez",         True,  "BlueZ support for Bluetooth devices"),
    ("netfeed",       True,  "build support for handling TCP/IP data sources"),
    ('usb',           True,  "libusb support for USB devices"),
    # Other daemon options
    ("control_socket", True,  "control socket for hotplug notifications"),
    ("force_global",  False, "force daemon to listen on all addressses"),
    ("systemd",       systemd, "systemd socket activation"),
    # Client-side options
    ("clientdebug",   True,  "client debugging support"),
    ("libgpsmm",      True,  "build C++ bindings"),
    ("ncurses",       True,  "build with ncurses"),
    ("qt",            True,  "build Qt bindings"),
    # Daemon options
    ("controlsend",   True,  "allow gpsctl/gpsmon to change device settings"),
    ("reconfigure",   True,  "allow gpsd to change device settings"),
    ("squelch",       False, "squelch gpsd_log/gpsd_hexdump to save cpu"),
    # Build control
    ("coveraging",    False, "build with code coveraging enabled"),
    ("debug",         False, "include debug information in build"),
    ("gpsdclients",   True,  "gspd client programs"),
    ("gpsd",          True,  "gpsd itself"),
    ("implicit_link", imloads, "implicit linkage is supported in shared libs"),
    ("leapfetch",     True,  "fetch up-to-date data on leap seconds."),
    ("magic_hat", sys.platform.startswith('linux'),
     "special Linux PPS hack for Raspberry Pi et al"),
    ("manbuild",      True,  "build help in man and HTML formats"),
    ("minimal", False, "turn off every option not set on the command line"),
    ("nostrip",       False, "don't symbol-strip binaries at link time"),
    ("profiling",     False, "build with profiling enabled"),
    ("python",        True,  "build Python support and modules."),
    ("shared",        True,  "build shared libraries, not static"),
    ("timeservice",   False, "time-service configuration"),
    ("xgps",          True,  "include xgps and xgpsspeed."),
    # Test control
    ("slow",          False, "run tests with realistic (slow) delays"),
)

# now step on the boolopts just read from '.scons-option-cache'
for (name, default, helpd) in boolopts:
    opts.Add(BoolVariable(name, helpd, default))

# Gentoo, Fedora, opensuse systems use uucp for ttyS* and ttyUSB*
if os.path.exists("/etc/gentoo-release"):
    def_group = "uucp"
else:
    def_group = "dialout"

nonboolopts = (
    ("gpsd_group",       def_group,     "privilege revocation group"),
    ("gpsd_user",        "nobody",      "privilege revocation user",),
    ("max_clients",      '64',          "maximum allowed clients"),
    ("max_devices",      '4',           "maximum allowed devices"),
    ("prefix",           "/usr/local",  "installation directory prefix"),
    ("python_coverage",  "coverage run", "coverage command for Python progs"),
    ("python_libdir",    "",            "Python module directory prefix"),
    ("qt_versioned",     "",            "version for versioned Qt"),
    ("sysroot",          "",            "cross-development system root"),
    ("target",           "",            "cross-development target"),
    ("target_python",    "python",      "target Python version as command"),
)

# now step on the non boolopts just read from '.scons-option-cache'
for (name, default, helpd) in nonboolopts:
    opts.Add(name, helpd, default)

pathopts = (
    ("bindir",              "bin",           "application binaries directory"),
    ("docdir",              "share/doc",     "documents directory"),
    ("includedir",          "include",       "header file directory"),
    ("libdir",              "lib",           "system libraries"),
    ("mandir",              "share/man",     "manual pages directory"),
    ("pkgconfig",           "$libdir/pkgconfig", "pkgconfig file directory"),
    ("sbindir",             "sbin",          "system binaries directory"),
    ("sysconfdir",          "etc",           "system configuration directory"),
    ("udevdir",             "/lib/udev",     "udev rules directory"),
)

# now step on the path options just read from '.scons-option-cache'
for (name, default, helpd) in pathopts:
    opts.Add(PathVariable(name, helpd, default, PathVariable.PathAccept))

#
# Environment creation
#
import_env = (
    # Variables used by programs invoked during the build
    "DISPLAY",         # Required for dia to run under scons
    "GROUPS",          # Required by gpg
    "HOME",            # Required by gpg
    "LOGNAME",         # LOGNAME is required for the flocktest production.
    'PATH',            # Required for ccache and Coverity scan-build
    'CCACHE_DIR',      # Required for ccache
    'CCACHE_RECACHE',  # Required for ccache (probably there are more)
    # pkg-config (required for crossbuilds at least, and probably pkgsrc)
    'PKG_CONFIG_LIBDIR',
    'PKG_CONFIG_PATH',
    'PKG_CONFIG_SYSROOT_DIR',
    # Variables for specific packaging/build systems
    "MACOSX_DEPLOYMENT_TARGET",  # MacOSX 10.4 (and probably earlier)
    'STAGING_DIR',               # OpenWRT and CeroWrt
    'STAGING_PREFIX',            # OpenWRT and CeroWrt
    'CWRAPPERS_CONFIG_DIR',      # pkgsrc
    # Variables used in testing
    'WRITE_PAD',       # So we can test WRITE_PAD values on the fly.
)

envs = {}
for var in import_env:
    if var in os.environ:
        envs[var] = os.environ[var]
envs["GPSD_HOME"] = os.getcwd()

env = Environment(tools=["default", "tar", "textfile"], options=opts, ENV=envs)

#  Minimal build turns off every option not set on the command line,
if ARGUMENTS.get('minimal'):
    for (name, default, helpd) in boolopts:
        # Ensure gpsd and gpsdclients are always enabled unless explicitly
        # turned off.
        if ((default is True and
             not ARGUMENTS.get(name) and
             name not in ("gpsd", "gpsdclients"))):
            env[name] = False

# Time-service build = stripped-down with some diagnostic tools
if ARGUMENTS.get('timeservice'):
    timerelated = ("gpsd",
                   "ipv6",
                   "magic_hat",
                   "mtk3301",    # For the Adafruit HAT
                   "ncurses",
                   "nmea0183",   # For generic hats of unknown type.
                   "oscillator",
                   "socket_export",
                   "ublox",      # For the Uputronics board
                   )
    for (name, default, helpd) in boolopts:
        if ((default is True and
             not ARGUMENTS.get(name) and
             name not in timerelated)):
            env[name] = False

# Many drivers require NMEA0183 - in case we select timeserver/minimal
# followed by one of these.
for driver in ('ashtech',
               'earthmate',
               'fury',
               'fv18',
               'gpsclock',
               'mtk3301',
               'oceanserver',
               'skytraq',
               'tnt',
               'tripmate', ):
    if env[driver]:
        env['nmea0183'] = True
        break


# iSync uses ublox underneath, so we force to enable it
if env['isync']:
    env['ublox'] = True

opts.Save('.scons-option-cache', env)

for (name, default, helpd) in pathopts:
    env[name] = env.subst(env[name])

env['VERSION'] = gpsd_version
env['SC_PYTHON'] = sys.executable  # Path to SCons Python

# Set defaults from environment.  Note that scons doesn't cope well
# with multi-word CPPFLAGS/LDFLAGS/SHLINKFLAGS values; you'll have to
# explicitly quote them or (better yet) use the "=" form of GNU option
# settings.
# Scons also uses different internal names than most other build-systems.
# So we rely on MergeFlags/ParseFlags to do the right thing for us.
env['STRIP'] = "strip"
env['PKG_CONFIG'] = "pkg-config"
for i in ["AR", "CC", "CXX", "LD",
          "PKG_CONFIG", "STRIP", "TAR"]:
    if i in os.environ:
        j = i
        if i == "LD":
            i = "SHLINK"
        env[i] = os.getenv(j)
for i in ["ARFLAGS", "CFLAGS", "CXXFLAGS", "LDFLAGS", "SHLINKFLAGS",
          "CPPFLAGS", "CCFLAGS", "LINKFLAGS"]:
    if i in os.environ:
        env.MergeFlags(Split(os.getenv(i)))


# Keep scan-build options in the environment
for key, value in os.environ.items():
    if key.startswith('CCC_'):
        env.Append(ENV={key: value})

# Placeholder so we can kluge together something like VPATH builds.
# $SRCDIR replaces occurrences for $(srcdir) in the autotools build.
# scons can get confused if this is not a full path
env['SRCDIR'] = os.getcwd()

# We may need to force slow regression tests to get around race
# conditions in the pty layer, especially on a loaded machine.
if env["slow"]:
    env['REGRESSOPTS'] = "-S"
else:
    env['REGRESSOPTS'] = ""

if env.GetOption("silent"):
    env['REGRESSOPTS'] += " -Q"


def announce(msg):
    if not env.GetOption("silent"):
        print(msg)


# DESTDIR environment variable means user prefix the installation root.
DESTDIR = os.environ.get('DESTDIR', '')


def installdir(idir, add_destdir=True):
    # use os.path.join to handle absolute paths properly.
    wrapped = os.path.join(env['prefix'], env[idir])
    if add_destdir:
        wrapped = os.path.normpath(DESTDIR + os.path.sep + wrapped)
    wrapped.replace("/usr/etc", "/etc")
    wrapped.replace("/usr/lib/systemd", "/lib/systemd")
    return wrapped


# Honor the specified installation prefix in link paths.
if env["sysroot"]:
    env.Prepend(LIBPATH=[env["sysroot"] + installdir('libdir',
                add_destdir=False)])

# Give deheader a way to set compiler flags
if 'MORECFLAGS' in os.environ:
    env.Append(CFLAGS=Split(os.environ['MORECFLAGS']))

# Don't change CCFLAGS if already set by environment.
if 'CCFLAGS' in os.environ:
    announce('Warning: CCFLAGS from environment overriding scons settings')
else:
    # Should we build with profiling?
    if env['profiling']:
        env.Append(CCFLAGS=['-pg'])
        env.Append(LDFLAGS=['-pg'])
    # Should we build with coveraging?
    if env['coveraging']:
        env.Append(CFLAGS=['-coverage'])
        env.Append(LDFLAGS=['-coverage'])
        env.Append(LINKFLAGS=['-coverage'])
    # Should we build with debug symbols?
    if env['debug']:
        env.Append(CCFLAGS=['-g3'])
    # Should we build with optimisation?
    if env['debug'] or env['coveraging']:
        env.Append(CCFLAGS=['-O0'])
    else:
        env.Append(CCFLAGS=['-O2'])

# Cross-development

devenv = (("ADDR2LINE", "addr2line"),
          ("AR", "ar"),
          ("AS", "as"),
          ("CXX", "c++"),
          ("CXXFILT", "c++filt"),
          ("CPP", "cpp"),
          ("GXX", "g++"),
          ("CC", "gcc"),
          ("GCCBUG", "gccbug"),
          ("GCOV", "gcov"),
          ("GPROF", "gprof"),
          ("LD", "ld"),
          ("NM", "nm"),
          ("OBJCOPY", "objcopy"),
          ("OBJDUMP", "objdump"),
          ("RANLIB", "ranlib"),
          ("READELF", "readelf"),
          ("SIZE", "size"),
          ("STRINGS", "strings"),
          ("STRIP", "strip"))

if env['target']:
    for (name, toolname) in devenv:
        env[name] = env['target'] + '-' + toolname

if env['sysroot']:
    env.MergeFlags({"CFLAGS": ["--sysroot=%s" % env['sysroot']]})
    env.MergeFlags({"LINKFLAGS": ["--sysroot=%s" % env['sysroot']]})


# Build help
def cmp(a, b):
    return (a > b) - (a < b)


Help("""Arguments may be a mixture of switches and targets in any order.
Switches apply to the entire build regardless of where they are in the order.
Important switches include:

    prefix=/usr     probably what packagers want

Options are cached in a file named .scons-option-cache and persist to later
invocations.  The file is editable.  Delete it to start fresh.  Current option
values can be listed with 'scons -h'.
""" + opts.GenerateHelpText(env, sort=cmp))

# Configuration


def CheckPKG(context, name):
    context.Message('Checking pkg-config for %s... ' % name)
    ret = context.TryAction('%s --exists \'%s\''
                            % (env['PKG_CONFIG'], name))[0]
    context.Result(ret)
    return ret


# Stylesheet URLs for making HTML and man pages from DocBook XML.
docbook_url_stem = 'http://docbook.sourceforge.net/release/xsl/current/'
docbook_man_uri = docbook_url_stem + 'manpages/docbook.xsl'
docbook_html_uri = docbook_url_stem + 'html/docbook.xsl'


def CheckXsltproc(context):
    context.Message('Checking that xsltproc can make man pages... ')
    ofp = open("man/xmltest.xml", "w")
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
    ofp.close()
    probe = ("xsltproc --encoding UTF-8 --output man/foo.1 --nonet "
             "--noout '%s' man/xmltest.xml" % (docbook_man_uri,))
    ret = context.TryAction(probe)[0]
    os.remove("man/xmltest.xml")
    if os.path.exists("man/foo.1"):
        os.remove("man/foo.1")
    else:
        # failed to create output
        ret = False
    context.Result(ret)
    return ret


def CheckCompilerOption(context, option):
    context.Message('Checking if compiler accepts %s... ' % (option,))
    old_CFLAGS = context.env['CFLAGS'][:]  # Get a *copy* of the old list
    context.env.Append(CFLAGS=option)
    ret = context.TryLink("""
        int main(int argc, char **argv) {
            (void) argc; (void) argv;
            return 0;
        }
    """, '.c')
    if not ret:
        context.env.Replace(CFLAGS=old_CFLAGS)
    context.Result(ret)
    return ret


def CheckHeaderDefines(context, file, define):
    context.Message('Checking if %s supplies %s... ' % (file, define))
    ret = context.TryLink("""
        #include <%s>
        #ifndef %s
        #error %s is not defined
        #endif
        int main(int argc, char **argv) {
            (void) argc; (void) argv;
            return 0;
        }
    """ % (file, define, define), '.c')
    context.Result(ret)
    return ret


def CheckCompilerDefines(context, define):
    context.Message('Checking if compiler supplies %s... ' % (define,))
    ret = context.TryLink("""
        #ifndef %s
        #error %s is not defined
        #endif
        int main(int argc, char **argv) {
            (void) argc; (void) argv;
            return 0;
        }
    """ % (define, define), '.c')
    context.Result(ret)
    return ret

# Check if this compiler is C11 or better


def CheckC11(context):
    context.Message('Checking if compiler is C11... ')
    ret = context.TryLink("""
        #if (__STDC_VERSION__ < 201112L)
        #error Not C11
        #endif
        int main(int argc, char **argv) {
            (void) argc; (void) argv;
            return 0;
        }
    """, '.c')
    context.Result(ret)
    return ret


def GetPythonValue(context, name, imp, expr, brief=False):
    context.Message('Obtaining Python %s... ' % name)
    context.sconf.cached = 0  # Avoid bogus "(cached)"
    if not env['target_python']:
        status, value = 0, str(eval(expr))
    else:
        command = [target_python_path, '-c', '%s; print(%s)' % (imp, expr)]
        try:
            status, value = _getstatusoutput(command, shell=False)
        except OSError:
            status = -1
        if status == 0:
            value = value.strip()
        else:
            value = ''
            announce('Python command "%s" failed - disabling Python.\n'
                     'Python components will NOT be installed' %
                     command[2])
            env['python'] = False
    context.Result('failed' if status else 'ok' if brief else value)
    return value


def GetLoadPath(context):
    context.Message("Getting system load path... ")


cleaning = env.GetOption('clean')
helping = env.GetOption('help')

# Always set up LIBPATH so that cleaning works properly.
env.Prepend(LIBPATH=[os.path.realpath(os.curdir)])

# from scons 3.0.5, any changes to env after this, until after
# config.Finish(), will be lost.  Use config.env until then.

# CheckXsltproc works, but result is incorrectly saved as "no"
config = Configure(env, custom_tests={
    'CheckPKG': CheckPKG,
    'CheckXsltproc': CheckXsltproc,
    'CheckCompilerOption': CheckCompilerOption,
    'CheckCompilerDefines': CheckCompilerDefines,
    'CheckC11': CheckC11,
    'CheckHeaderDefines': CheckHeaderDefines,
    'GetPythonValue': GetPythonValue})

# Use print, rather than announce, so we see it in -s mode.
print("This system is: %s" % sys.platform)

libgps_flags = []
if cleaning or helping:
    bluezflags = []
    confdefs = []
    dbusflags = []
    htmlbuilder = False
    manbuilder = False
    ncurseslibs = []
    rtlibs = []
    mathlibs = []
    tiocmiwait = True  # For cleaning, which works on any OS
    usbflags = []
else:

    # OS X aliases gcc to clang
    # clang accepts -pthread, then warns it is unused.
    if not config.CheckCC():
        announce("ERROR: CC doesn't work")

    if ((config.CheckCompilerOption("-pthread") and
         not sys.platform.startswith('darwin'))):
        config.env.MergeFlags("-pthread")

    confdefs = ["/* gpsd_config.h generated by scons, do not hand-hack. */\n"]

    confdefs.append('#ifndef GPSD_CONFIG_H\n')

    confdefs.append('#define VERSION "%s"\n' % gpsd_version)

    confdefs.append('#define GPSD_URL "%s"\n' % website)

    # TODO: Move these into an if block only on systems with glibc.
    # needed for isfinite(), pselect(), etc.
    # for strnlen() before glibc 2.10
    # glibc 2.10+ needs 200908L (or XOPEN 700+) for strnlen()
    # on newer glibc _DEFAULT_SOURCE resets _POSIX_C_SOURCE
    # we set it just in case
    confdefs.append('#if !defined(_POSIX_C_SOURCE)')
    confdefs.append('#define _POSIX_C_SOURCE 200809L')
    confdefs.append('#endif\n')
    # for daemon(), cfmakeraw(), strsep() and setgroups()
    # on glibc 2.19+
    # may also be added by pkg_config
    # on linux this eventually sets _USE_XOPEN
    confdefs.append('#if !defined(_DEFAULT_SOURCE)')
    confdefs.append('#define _DEFAULT_SOURCE')
    confdefs.append('#endif\n')

    # sys/un.h, and more, needs __USE_MISC with glibc and osX
    # __USE_MISC is set by _DEFAULT_SOURCE or _BSD_SOURCE

    # TODO: Many of these are now specified by POSIX.  Check if
    # defining _XOPEN_SOURCE is necessary, and limit to systems where
    # it is.
    # 500 means X/Open 1995
    # getsid(), isascii(), nice(), putenv(), strdup(), sys/ipc.h need 500
    # 600 means X/Open 2004
    # Ubuntu and OpenBSD isfinite() needs 600
    # 700 means X/Open 2008
    # glibc 2.10+ needs 700+ for strnlen()
    # Python.h wants 600 or 700

    # removed 2 Jul 2019 to see if anything breaks...
    # confdefs.append('#if !defined(_XOPEN_SOURCE)')
    # confdefs.append('#define _XOPEN_SOURCE 700')
    # confdefs.append('#endif\n')
    # Reinstated for FreeBSD (below) 16-Aug-2019

    if sys.platform.startswith('linux'):
        # for cfmakeraw(), strsep(), etc. on CentOS 7
        # glibc 2.19 and before
        # sets __USE_MISC
        confdefs.append('#if !defined(_BSD_SOURCE)')
        confdefs.append('#define _BSD_SOURCE')
        confdefs.append('#endif\n')
        # for strnlen() and struct ifreq
        # glibc before 2.10, deprecated in 2.10+
        confdefs.append('#if !defined(_GNU_SOURCE)')
        confdefs.append('#define _GNU_SOURCE 1')
        confdefs.append('#endif\n')
    elif sys.platform.startswith('darwin'):
        # strlcpy() and SIGWINCH need _DARWIN_C_SOURCE
        confdefs.append('#if !defined(_DARWIN_C_SOURCE)')
        confdefs.append('#define _DARWIN_C_SOURCE 1\n')
        confdefs.append('#endif\n')
        # vsnprintf() needs __DARWIN_C_LEVEL >= 200112L
        # snprintf() needs __DARWIN_C_LEVEL >= 200112L
        # _DARWIN_C_SOURCE forces __DARWIN_C_LEVEL to 900000L
        # see <sys/cdefs.h>

        # set internal lib versions at link time.
        libgps_flags = ["-Wl,-current_version,%s" % libgps_version,
                        "-Wl,-compatibility_version,%s" % libgps_version,
                        "-Wl,-install_name,%s/$TARGET" %
                        installdir('libdir', add_destdir=False)]
    elif sys.platform.startswith('freebsd'):
        # for isascii(), putenv(), nice(), strptime()
        confdefs.append('#if !defined(_XOPEN_SOURCE)')
        confdefs.append('#define _XOPEN_SOURCE 700')
        confdefs.append('#endif\n')
        # required to define u_int in sys/time.h
        confdefs.append('#if !defined(_BSD_SOURCE)')
        confdefs.append("#define _BSD_SOURCE 1\n")
        confdefs.append('#endif\n')
        # required to get strlcpy(), and more, from string.h
        confdefs.append('#if !defined(__BSD_VISIBLE)')
        confdefs.append("#define __BSD_VISIBLE 1\n")
        confdefs.append('#endif\n')
    elif sys.platform.startswith('openbsd'):
        # required to define u_int in sys/time.h
        confdefs.append('#if !defined(_BSD_SOURCE)')
        confdefs.append("#define _BSD_SOURCE 1\n")
        confdefs.append('#endif\n')
        # required to get strlcpy(), and more, from string.h
        confdefs.append('#if !defined(__BSD_VISIBLE)')
        confdefs.append("#define __BSD_VISIBLE 1\n")
        confdefs.append('#endif\n')
    elif sys.platform.startswith('netbsd'):
        # required to get strlcpy(), and more, from string.h
        confdefs.append('#if !defined(_NETBSD_SOURCE)')
        confdefs.append("#define _NETBSD_SOURCE 1\n")
        confdefs.append('#endif\n')

    cxx = config.CheckCXX()
    if not cxx and config.env["libgpsmm"]:
        announce("C++ doesn't work, suppressing libgpsmm build.")
        config.env["libgpsmm"] = False

    # define a helper function for pkg-config - we need to pass
    # --static for static linking, too.
    #
    # Using "--libs-only-L --libs-only-l" instead of "--libs" avoids
    # a superfluous "-rpath" option in some FreeBSD cases, and the resulting
    # scons crash.
    # However, it produces incorrect results for Qt5Network in OSX, so
    # it can't be used unconditionally.
    def pkg_config(pkg, shared=env['shared'], rpath_hack=False):
        libs = '--libs-only-L --libs-only-l' if rpath_hack else '--libs'
        if not shared:
            libs += ' --static'
        return ['!%s --cflags %s %s' % (env['PKG_CONFIG'], libs, pkg)]

    # The actual distinction here is whether the platform has ncurses in the
    # base system or not. If it does, pkg-config is not likely to tell us
    # anything useful. FreeBSD does, Linux doesn't. Most likely other BSDs
    # are like FreeBSD.
    ncurseslibs = []
    if config.env['ncurses']:
        if not config.CheckHeader(["curses.h"]):
            announce('Turning off ncurses support, curses.h not found.')
            config.env['ncurses'] = False
        elif config.CheckPKG('ncurses'):
            ncurseslibs = pkg_config('ncurses', rpath_hack=True)
            if config.CheckPKG('tinfo'):
                ncurseslibs += pkg_config('tinfo', rpath_hack=True)
        # It's not yet known whether rpath_hack is appropriate for
        # ncurses5-config.
        elif WhereIs('ncurses5-config'):
            ncurseslibs = ['!ncurses5-config --libs --cflags']
        elif WhereIs('ncursesw5-config'):
            ncurseslibs = ['!ncursesw5-config --libs --cflags']
        elif sys.platform.startswith('freebsd'):
            ncurseslibs = ['-lncurses']
        elif sys.platform.startswith('openbsd'):
            ncurseslibs = ['-lcurses']
        elif sys.platform.startswith('darwin'):
            ncurseslibs = ['-lcurses']
        else:
            announce('Turning off ncurses support, library not found.')
            config.env['ncurses'] = False

    if config.env['usb']:
        # In FreeBSD except version 7, USB libraries are in the base system
        if config.CheckPKG('libusb-1.0'):
            confdefs.append("#define HAVE_LIBUSB 1\n")
            try:
                usbflags = pkg_config('libusb-1.0')
            except OSError:
                announce("pkg_config is confused about the state "
                         "of libusb-1.0.")
                usbflags = []
        elif sys.platform.startswith("freebsd"):
            confdefs.append("#define HAVE_LIBUSB 1\n")
            usbflags = ["-lusb"]
        else:
            confdefs.append("/* #undef HAVE_LIBUSB */\n")
            usbflags = []
    else:
        confdefs.append("/* #undef HAVE_LIBUSB */\n")
        usbflags = []
        config.env["usb"] = False

    if config.CheckLib('librt'):
        confdefs.append("#define HAVE_LIBRT 1\n")
        # System library - no special flags
        rtlibs = ["-lrt"]
    else:
        confdefs.append("/* #undef HAVE_LIBRT */\n")
        rtlibs = []

    # The main reason we check for libm explicitly is to set up the config
    # environment for CheckFunc for sincos().  But it doesn't hurt to omit
    # the '-lm' when it isn't appropriate.
    if config.CheckLib('libm'):
        mathlibs = ['-lm']
    else:
        mathlibs = []

    # FreeBSD uses -lthr for pthreads
    if config.CheckLib('libthr'):
        confdefs.append("#define HAVE_LIBTHR 1\n")
        # System library - no special flags
        rtlibs += ["-lthr"]
    else:
        confdefs.append("/* #undef HAVE_LIBTHR */\n")

    if config.env['dbus_export'] and config.CheckPKG('dbus-1'):
        confdefs.append("#define HAVE_DBUS 1\n")
        dbusflags = pkg_config("dbus-1")
        config.env.MergeFlags(dbusflags)
    else:
        confdefs.append("/* #undef HAVE_DBUS */\n")
        dbusflags = []
        if config.env["dbus_export"]:
            announce("Turning off dbus-export support, library not found.")
        config.env["dbus_export"] = False

    if config.env['bluez'] and config.CheckPKG('bluez'):
        confdefs.append("#define ENABLE_BLUEZ 1\n")
        bluezflags = pkg_config('bluez')
    else:
        confdefs.append("/* #undef ENABLE_BLUEZ */\n")
        bluezflags = []
        if config.env["bluez"]:
            announce("Turning off Bluetooth support, library not found.")
        config.env["bluez"] = False

    # in_port_t is not defined on Android
    if not config.CheckType("in_port_t", "#include <netinet/in.h>"):
        announce("Did not find in_port_t typedef, assuming unsigned short int")
        confdefs.append("typedef unsigned short int in_port_t;\n")

    # SUN_LEN is not defined on Android
    if ((not config.CheckDeclaration("SUN_LEN", "#include <sys/un.h>") and
         not config.CheckDeclaration("SUN_LEN", "#include <linux/un.h>"))):
        announce("SUN_LEN is not system-defined, using local definition")
        confdefs.append("#ifndef SUN_LEN\n")
        confdefs.append("#define SUN_LEN(ptr) "
                        "((size_t) (((struct sockaddr_un *) 0)->sun_path) "
                        "+ strlen((ptr)->sun_path))\n")
        confdefs.append("#endif /* SUN_LEN */\n")

    if config.CheckHeader(["linux/can.h"]):
        confdefs.append("#define HAVE_LINUX_CAN_H 1\n")
        announce("You have kernel CANbus available.")
    else:
        confdefs.append("/* #undef HAVE_LINUX_CAN_H */\n")
        announce("You do not have kernel CANbus available.")
        config.env["nmea2000"] = False

    # check for C11 or better, and __STDC__NO_ATOMICS__ is not defined
    # before looking for stdatomic.h
    if ((config.CheckC11() and
         not config.CheckCompilerDefines("__STDC_NO_ATOMICS__") and
         config.CheckHeader("stdatomic.h"))):
        confdefs.append("#define HAVE_STDATOMIC_H 1\n")
    else:
        confdefs.append("/* #undef HAVE_STDATOMIC_H */\n")
        if config.CheckHeader("libkern/OSAtomic.h"):
            confdefs.append("#define HAVE_OSATOMIC_H 1\n")
        else:
            confdefs.append("/* #undef HAVE_OSATOMIC_H */\n")
            announce("No memory barriers - SHM export and time hinting "
                     "may not be reliable.")

    # endian.h is required for rtcm104v2 unless the compiler defines
    # __ORDER_BIG_ENDIAN__, __ORDER_LITTLE_ENDIAN__ and __BYTE_ORDER__
    if config.CheckCompilerDefines("__ORDER_BIG_ENDIAN__") \
       and config.CheckCompilerDefines("__ORDER_LITTLE_ENDIAN__") \
       and config.CheckCompilerDefines("__BYTE_ORDER__"):
        confdefs.append("#define HAVE_BUILTIN_ENDIANNESS 1\n")
        confdefs.append("/* #undef HAVE_ENDIAN_H */\n")
        confdefs.append("/* #undef HAVE_SYS_ENDIAN_H */\n")
        announce("Your compiler has built-in endianness support.")
    else:
        confdefs.append("/* #undef HAVE_BUILTIN_ENDIANNESS\n */")
        if config.CheckHeader("endian.h"):
            confdefs.append("#define HAVE_ENDIAN_H 1\n")
            confdefs.append("/* #undef HAVE_SYS_ENDIAN_H */\n")
            confdefs.append("/* #undef HAVE_MACHINE_ENDIAN_H */\n")
        elif config.CheckHeader("sys/endian.h"):
            confdefs.append("/* #undef HAVE_ENDIAN_H */\n")
            confdefs.append("#define HAVE_SYS_ENDIAN_H 1\n")
            confdefs.append("/* #undef HAVE_MACHINE_ENDIAN_H */\n")
        elif config.CheckHeader("machine/endian.h"):
            confdefs.append("/* #undef HAVE_ENDIAN_H */\n")
            confdefs.append("/* #undef HAVE_SYS_ENDIAN_H */\n")
            confdefs.append("#define HAVE_MACHINE_ENDIAN_H 1\n")
        else:
            confdefs.append("/* #undef HAVE_ENDIAN_H */\n")
            confdefs.append("/* #undef HAVE_SYS_ENDIAN_H */\n")
            confdefs.append("/* #undef HAVE_MACHINE_ENDIAN_H */\n")
            announce("You do not have the endian.h header file. "
                     "RTCM V2 support disabled.")
            config.env["rtcm104v2"] = False

    for hdr in ("arpa/inet",
                "netdb",
                "netinet/in",
                "netinet/ip",
                "sys/sysmacros",   # for major(), on linux
                "sys/socket",
                "sys/un",
                "syslog",
                "termios",
                "winsock2"
                ):
        if config.CheckHeader(hdr + ".h"):
            confdefs.append("#define HAVE_%s_H 1\n"
                            % hdr.replace("/", "_").upper())
        elif "termios" == hdr:
            announce("ERROR: %s.h not found" % hdr)
        else:
            confdefs.append("/* #undef HAVE_%s_H */\n"
                            % hdr.replace("/", "_").upper())

    # check function after libraries, because some function require libraries
    # for example clock_gettime() require librt on Linux glibc < 2.17
    for f in ("cfmakeraw", "clock_gettime", "daemon", "fcntl", "fork",
              "gmtime_r", "inet_ntop", "strlcat", "strlcpy", "strptime"):
        if config.CheckFunc(f):
            confdefs.append("#define HAVE_%s 1\n" % f.upper())
        else:
            confdefs.append("/* #undef HAVE_%s */\n" % f.upper())

    # Apple may supply sincos() as __sincos(), or not at all
    if config.CheckFunc('sincos'):
        confdefs.append('#define HAVE_SINCOS\n')
    elif config.CheckFunc('__sincos'):
        confdefs.append('#define sincos __sincos\n#define HAVE_SINCOS\n')
    else:
        confdefs.append('/* #undef HAVE_SINCOS */\n')

    if config.CheckHeader(["sys/types.h", "sys/time.h", "sys/timepps.h"]):
        confdefs.append("#define HAVE_SYS_TIMEPPS_H 1\n")
        kpps = True
    else:
        kpps = False
        if config.env["magic_hat"]:
            announce("Forcing magic_hat=no since RFC2783 API is unavailable")
            config.env["magic_hat"] = False
    tiocmiwait = config.CheckHeaderDefines("sys/ioctl.h", "TIOCMIWAIT")
    if not tiocmiwait and not kpps:
        announce("Neither TIOCMIWAIT nor RFC2783 API is available)")
        if config.env["timeservice"]:
            announce("ERROR: timeservice specified, but no PPS available")
            Exit(1)

    # Map options to libraries required to support them that might be absent.
    optionrequires = {
        "bluez": ["libbluetooth"],
        "dbus_export": ["libdbus-1"],
    }

    keys = list(map(lambda x: (x[0], x[2]), boolopts))  \
        + list(map(lambda x: (x[0], x[2]), nonboolopts)) \
        + list(map(lambda x: (x[0], x[2]), pathopts))
    keys.sort()
    for (key, helpd) in keys:
        value = config.env[key]
        if value and key in optionrequires:
            for required in optionrequires[key]:
                if not config.CheckLib(required):
                    announce("%s not found, %s cannot be enabled."
                             % (required, key))
                    value = False
                    break

        confdefs.append("/* %s */" % helpd)
        if isinstance(value, bool):
            if value:
                confdefs.append("#define %s_ENABLE 1\n" % key.upper())
            else:
                confdefs.append("/* #undef %s_ENABLE */\n" % key.upper())
        elif value in (0, "", "(undefined)"):
            confdefs.append("/* #undef %s */\n" % key.upper())
        else:
            if value.isdigit():
                confdefs.append("#define %s %s\n" % (key.upper(), value))
            else:
                confdefs.append("#define %s \"%s\"\n" % (key.upper(), value))

    # Simplifies life on hackerboards like the Raspberry Pi
    if config.env['magic_hat']:
        confdefs.append('''\
/* Magic device which, if present, means to grab a static /dev/pps0 for KPPS */
#define MAGIC_HAT_GPS   "/dev/ttyAMA0"
/* Generic device which, if present, means: */
/* to grab a static /dev/pps0 for KPPS */
#define MAGIC_LINK_GPS  "/dev/gpsd0"
''')

    confdefs.append('''\

#define GPSD_CONFIG_H
#endif /* GPSD_CONFIG_H */
''')

    manbuilder = htmlbuilder = None
    if config.env['manbuild']:
        if config.CheckXsltproc():
            build = ("xsltproc --encoding UTF-8 --output $TARGET"
                     " --nonet %s $SOURCE")
            htmlbuilder = build % docbook_html_uri
            manbuilder = build % docbook_man_uri
        elif WhereIs("xmlto"):
            xmlto = "xmlto -o `dirname $TARGET` %s $SOURCE"
            htmlbuilder = xmlto % "html-nochunks"
            manbuilder = xmlto % "man"
        else:
            announce("Neither xsltproc nor xmlto found, documentation "
                     "cannot be built.")
    else:
        announce("Build of man and HTML documentation is disabled.")
    if manbuilder:
        # 18.2. Attaching a Builder to a Construction Environment
        config.env.Append(BUILDERS={"Man": Builder(action=manbuilder,
                                                   src_suffix=".xml")})
        config.env.Append(BUILDERS={"HTML": Builder(action=htmlbuilder,
                                                    src_suffix=".xml",
                                                    suffix=".html")})

    # Determine if Qt network libraries are present, and
    # if not, force qt to off
    if config.env["qt"]:
        qt_net_name = 'Qt%sNetwork' % config.env["qt_versioned"]
        qt_network = config.CheckPKG(qt_net_name)
        if not qt_network:
            config.env["qt"] = False
            announce('Turning off Qt support, library not found.')

    # If supported by the compiler, enable all warnings except uninitialized
    # and missing-field-initializers, which we can't help triggering because
    # of the way some of the JSON-parsing code is generated.
    # Also not including -Wcast-qual and -Wimplicit-function-declaration,
    # because we can't seem to keep scons from passing these to g++.
    #
    # Do this after the other config checks, to keep warnings out of them.
    for option in ('-Wall',
                   '-Wcast-align',
                   '-Wextra',
                   # -Wimplicit-fallthrough same as
                   # -Wimplicit-fallthrough=3, except osX hates the
                   # second flavor
                   '-Wimplicit-fallthrough',
                   '-Wmissing-declarations',
                   '-Wmissing-prototypes',
                   '-Wno-missing-field-initializers',
                   '-Wno-uninitialized',
                   '-Wpointer-arith',
                   '-Wreturn-type',
                   '-Wstrict-prototypes',
                   '-Wvla',
                   ):
        if option not in config.env['CFLAGS']:
            config.CheckCompilerOption(option)

# OSX needs to set the ID for installed shared libraries.  See if this is OSX
# and whether we have the tool.

# Set up configuration for target Python

PYTHON_LIBDIR_CALL = 'sysconfig.get_python_lib()'

PYTHON_CONFIG_NAMES = ['CC', 'CXX', 'OPT', 'BASECFLAGS',
                       'CCSHARED', 'LDSHARED', 'SO', 'INCLUDEPY', 'LDFLAGS']
PYTHON_CONFIG_QUOTED = ["'%s'" % s for s in PYTHON_CONFIG_NAMES]
PYTHON_CONFIG_CALL = ('sysconfig.get_config_vars(%s)'
                      % ', '.join(PYTHON_CONFIG_QUOTED))


# ugly hack from http://www.catb.org/esr/faqs/practical-python-porting/
# handle python2/3 strings
def polystr(o):
    if isinstance(o, str):
        return o
    if isinstance(o, bytes):
        return str(o, encoding='latin-1')
    raise ValueError


if helping:

    # If helping just get usable config info from the local Python
    target_python_path = ''
    py_config_text = str(eval(PYTHON_CONFIG_CALL))
    python_libdir = str(eval(PYTHON_LIBDIR_CALL))

else:

    if config.env['python'] and config.env['target_python']:
        try:
            config.CheckProg
        except AttributeError:  # Older scons versions don't have CheckProg
            target_python_path = config.env['target_python']
        else:
            target_python_path = config.CheckProg(config.env['target_python'])
        if not target_python_path:
            announce("Target Python doesn't exist - disabling Python.")
            config.env['python'] = False
    if config.env['python']:
        # Maximize consistency by using the reported sys.executable
        target_python_path = config.GetPythonValue('exe path',
                                                   'import sys',
                                                   'sys.executable',
                                                   brief=cleaning)
        if config.env['python_libdir']:
            python_libdir = config.env['python_libdir']
        else:
            python_libdir = config.GetPythonValue('lib dir',
                                                  PYTHON_SYSCONFIG_IMPORT,
                                                  PYTHON_LIBDIR_CALL,
                                                  brief=cleaning)
            # follow FHS, put in /usr/local/libXX, not /usr/libXX
            # may be lib, lib32 or lib64
            python_libdir = polystr(python_libdir)
            python_libdir = python_libdir.replace("/usr/lib",
                                                  "/usr/local/lib")

        py_config_text = config.GetPythonValue('config vars',
                                               PYTHON_SYSCONFIG_IMPORT,
                                               PYTHON_CONFIG_CALL,
                                               brief=True)

if config.env['python']:  # May have been turned off by error
    config.env['PYTHON'] = polystr(target_python_path)
    # For regress-driver
    config.env['ENV']['PYTHON'] = polystr(target_python_path)
    py_config_vars = ast.literal_eval(py_config_text.decode())
    py_config_vars = [[] if x is None else x for x in py_config_vars]
    python_config = dict(zip(PYTHON_CONFIG_NAMES, py_config_vars))
    announce(python_config)


env = config.Finish()
# All configuration should be finished.  env can now be modified.

if not (cleaning or helping):

    # Be explicit about what we're doing.
    changelatch = False
    for (name, default, helpd) in boolopts + nonboolopts + pathopts:
        if env[name] != env.subst(default):
            if not changelatch:
                announce("Altered configuration variables:")
                changelatch = True
            announce("%s = %s (default %s): %s"
                     % (name, env[name], env.subst(default), helpd))
    if not changelatch:
        announce("All configuration flags are defaulted.")

    # Gentoo systems can have a problem with the Python path
    if os.path.exists("/etc/gentoo-release"):
        announce("This is a Gentoo system.")
        announce("Adjust your PYTHONPATH to see library directories "
                 "under /usr/local/lib")

# Should we build the Qt binding?
if env["qt"] and env["shared"]:
    qt_env = env.Clone()
    qt_env.MergeFlags('-DUSE_QT')
    qt_env.Append(OBJPREFIX='qt-')
    if not (cleaning or helping):
        try:
            qt_env.MergeFlags(pkg_config(qt_net_name))
        except OSError:
            announce("pkg_config is confused about the state of %s."
                     % qt_net_name)
            qt_env = None
else:
    qt_env = None

# Set up for Python coveraging if needed
if env['coveraging'] and env['python_coverage'] and not (cleaning or helping):
    pycov_default = opts.options[opts.keys().index('python_coverage')].default
    pycov_current = env['python_coverage']
    pycov_list = pycov_current.split()
    if env.GetOption('num_jobs') > 1 and pycov_current == pycov_default:
        pycov_list.append('--parallel-mode')
    # May need absolute path to coveraging tool if 'PythonXX' is prefixed
    pycov_path = env.WhereIs(pycov_list[0])
    if pycov_path:
        pycov_list[0] = pycov_path
        env['PYTHON_COVERAGE'] = ' '.join(pycov_list)
        env['ENV']['PYTHON_COVERAGE'] = ' '.join(pycov_list)
    else:
        announce('Python coverage tool not found - disabling Python coverage.')
        env['python_coverage'] = ''  # So we see it in the options

# Two shared libraries provide most of the code for the C programs

# gpsd client library
libgps_sources = [
    "ais_json.c",
    "bits.c",
    "gpsdclient.c",
    "gps_maskdump.c",
    "gpsutils.c",
    "hex.c",
    "json.c",
    "libgps_core.c",
    "libgps_dbus.c",
    "libgps_json.c",
    "libgps_shm.c",
    "libgps_sock.c",
    "netlib.c",
    "os_compat.c",
    "rtcm2_json.c",
    "rtcm3_json.c",
    "shared_json.c",
    "timespec_str.c",
]

if env['libgpsmm']:
    libgps_sources.append("libgpsmm.cpp")

# gpsd server library
libgpsd_sources = [
    "bsd_base64.c",
    "crc24q.c",
    "driver_ais.c",
    "driver_evermore.c",
    "driver_garmin.c",
    "driver_garmin_txt.c",
    "driver_geostar.c",
    "driver_greis.c",
    "driver_greis_checksum.c",
    "driver_italk.c",
    "driver_navcom.c",
    "driver_nmea0183.c",
    "driver_nmea2000.c",
    "driver_oncore.c",
    "driver_rtcm2.c",
    "driver_rtcm3.c",
    "drivers.c",
    "driver_sirf.c",
    "driver_skytraq.c",
    "driver_superstar2.c",
    "driver_tsip.c",
    "driver_ubx.c",
    "driver_zodiac.c",
    "geoid.c",
    "gpsd_json.c",
    "isgps.c",
    "libgpsd_core.c",
    "matrix.c",
    "net_dgpsip.c",
    "net_gnss_dispatch.c",
    "net_ntrip.c",
    "ntpshmread.c",
    "ntpshmwrite.c",
    "packet.c",
    "ppsthread.c",
    "pseudoais.c",
    "pseudonmea.c",
    "serial.c",
    "subframe.c",
    "timebase.c",
]

if not env["shared"]:
    def Library(env, target, sources, version, parse_flags=None):
        return env.StaticLibrary(target,
                                 [env.StaticObject(s) for s in sources],
                                 parse_flags=parse_flags)

    def LibraryInstall(env, libdir, sources, version):
        return env.Install(libdir, sources)
else:
    def Library(env, target, sources, version, parse_flags=None):
        # Note: We have a possibility of getting either Object or file
        # list for sources, so we run through the sources and try to make
        # them into SharedObject instances.
        obj_list = []
        for s in Flatten(sources):
            if isinstance(s, str):
                obj_list.append(env.SharedObject(s))
            else:
                obj_list.append(s)
        return env.SharedLibrary(target=target,
                                 source=obj_list,
                                 parse_flags=parse_flags,
                                 SHLIBVERSION=version)

    def LibraryInstall(env, libdir, sources, version):
        # note: osX lib name s/b libgps.VV.dylib
        # where VV is libgps_version_current
        inst = env.InstallVersionedLib(libdir, sources, SHLIBVERSION=version)
        return inst

libgps_shared = Library(env=env,
                        target="gps",
                        sources=libgps_sources,
                        version=libgps_version,
                        parse_flags=rtlibs + libgps_flags)
env.Clean(libgps_shared, "gps_maskdump.c")

libgps_static = env.StaticLibrary("gps_static",
                                  [env.StaticObject(s)
                                   for s in libgps_sources], rtlibs)

static_gpsdlib = env.StaticLibrary(
    target="gpsd",
    source=[env.StaticObject(s, parse_flags=usbflags + bluezflags)
            for s in libgpsd_sources],
    parse_flags=usbflags + bluezflags)

libraries = [libgps_shared]

# Only attempt to create the qt library if we have shared turned on
# otherwise we have a mismash of objects in library
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
        if src not in ('ais_json.c', 'json.c', 'libgps_json.c',
                       'rtcm2_json.c', 'rtcm3_json.c', 'shared_json.c'):
            compile_with = qt_env['CXX']
            compile_flags = qt_flags
        else:
            compile_with = qt_env['CC']
            compile_flags = qt_env['CFLAGS']
        qtobjects.append(qt_env.SharedObject(src,
                                             CC=compile_with,
                                             CFLAGS=compile_flags))
    compiled_qgpsmmlib = Library(qt_env, "Qgpsmm", qtobjects, libgps_version)
    libraries.append(compiled_qgpsmmlib)

# The libraries have dependencies on system libraries
# libdbus appears multiple times because the linker only does one pass.

gpsflags = mathlibs + rtlibs + dbusflags
gpsdflags = usbflags + bluezflags + gpsflags

# Source groups

gpsd_sources = [
    'dbusexport.c',
    'gpsd.c',
    'shmexport.c',
    'timehint.c'
]

if env['systemd']:
    gpsd_sources.append("sd_socket.c")

gpsmon_sources = [
    'gpsmon.c',
    'monitor_garmin.c',
    'monitor_italk.c',
    'monitor_nmea0183.c',
    'monitor_oncore.c',
    'monitor_sirf.c',
    'monitor_superstar2.c',
    'monitor_tnt.c',
    'monitor_ubx.c',
]

# Production programs

gpsd = env.Program('gpsd', gpsd_sources,
                   LIBS=['gpsd', 'gps_static'],
                   parse_flags=gpsdflags + gpsflags)
gpsdecode = env.Program('gpsdecode', ['gpsdecode.c'],
                        LIBS=['gpsd', 'gps_static'],
                        parse_flags=gpsdflags + gpsflags)
gpsctl = env.Program('gpsctl', ['gpsctl.c'],
                     LIBS=['gpsd', 'gps_static'],
                     parse_flags=gpsdflags + gpsflags)
gpsmon = env.Program('gpsmon', gpsmon_sources,
                     LIBS=['gpsd', 'gps_static'],
                     parse_flags=gpsdflags + gpsflags + ncurseslibs)
gpsdctl = env.Program('gpsdctl', ['gpsdctl.c'],
                      LIBS=['gps_static'],
                      parse_flags=gpsflags)
gpspipe = env.Program('gpspipe', ['gpspipe.c'],
                      LIBS=['gps_static'],
                      parse_flags=gpsflags)
gpsrinex = env.Program('gpsrinex', ['gpsrinex.c'],
                       LIBS=['gps_static'],
                       parse_flags=gpsflags)
gps2udp = env.Program('gps2udp', ['gps2udp.c'],
                      LIBS=['gps_static'],
                      parse_flags=gpsflags)
gpxlogger = env.Program('gpxlogger', ['gpxlogger.c'],
                        LIBS=['gps_static'],
                        parse_flags=gpsflags)
lcdgps = env.Program('lcdgps', ['lcdgps.c'],
                     LIBS=['gps_static'],
                     parse_flags=gpsflags)
cgps = env.Program('cgps', ['cgps.c'],
                   LIBS=['gps_static'],
                   parse_flags=gpsflags + ncurseslibs)
ntpshmmon = env.Program('ntpshmmon', ['ntpshmmon.c'],
                        LIBS=['gpsd', 'gps_static'],
                        parse_flags=gpsflags)
ppscheck = env.Program('ppscheck', ['ppscheck.c'],
                       LIBS=['gps_static'],
                       parse_flags=gpsflags)

bin_binaries = []
sbin_binaries = []
if env["gpsd"]:
    sbin_binaries += [gpsd]

if env["gpsdclients"]:
    sbin_binaries += [gpsdctl]
    bin_binaries += [
        gps2udp,
        gpsctl,
        gpsdecode,
        gpspipe,
        gpsrinex,
        gpxlogger,
        lcdgps
    ]

if env["timeservice"] or env["gpsdclients"]:
    bin_binaries += [ntpshmmon]
    if tiocmiwait:
        bin_binaries += [ppscheck]

if env["ncurses"] and (env["timeservice"] or env["gpsdclients"]):
    bin_binaries += [cgps, gpsmon]
else:
    announce("WARNING: not building cgps or gpsmon")

# Test programs - always link locally and statically
test_bits = env.Program('tests/test_bits', ['tests/test_bits.c'],
                        LIBS=['gps_static'])
test_float = env.Program('tests/test_float', ['tests/test_float.c'])
test_geoid = env.Program('tests/test_geoid', ['tests/test_geoid.c'],
                         LIBS=['gpsd', 'gps_static'],
                         parse_flags=gpsdflags)
test_gpsdclient = env.Program('tests/test_gpsdclient',
                              ['tests/test_gpsdclient.c'],
                              LIBS=['gps_static', 'm'])
test_matrix = env.Program('tests/test_matrix', ['tests/test_matrix.c'],
                          LIBS=['gpsd', 'gps_static'],
                          parse_flags=gpsdflags)
test_mktime = env.Program('tests/test_mktime', ['tests/test_mktime.c'],
                          LIBS=['gps_static'], parse_flags=mathlibs + rtlibs)
test_packet = env.Program('tests/test_packet', ['tests/test_packet.c'],
                          LIBS=['gpsd', 'gps_static'],
                          parse_flags=gpsdflags)
test_timespec = env.Program('tests/test_timespec', ['tests/test_timespec.c'],
                            LIBS=['gpsd', 'gps_static'],
                            parse_flags=gpsdflags)
test_trig = env.Program('tests/test_trig', ['tests/test_trig.c'],
                        parse_flags=mathlibs)
# test_libgps for glibc older than 2.17
test_libgps = env.Program('tests/test_libgps', ['tests/test_libgps.c'],
                          LIBS=['gps_static'],
                          parse_flags=mathlibs + rtlibs + dbusflags)

if not env['socket_export']:
    announce("test_json not building because socket_export is disabled")
    test_json = None
else:
    test_json = env.Program(
        'tests/test_json', ['tests/test_json.c'],
        LIBS=['gps_static'],
        parse_flags=mathlibs + rtlibs + usbflags + dbusflags)

# duplicate below?
test_gpsmm = env.Program('tests/test_gpsmm', ['tests/test_gpsmm.cpp'],
                         LIBS=['gps_static', 'gps'],
                         parse_flags=mathlibs + rtlibs + dbusflags)
testprogs = [test_bits,
             test_float,
             test_geoid,
             test_gpsdclient,
             test_libgps,
             test_matrix,
             test_mktime,
             test_packet,
             test_timespec,
             test_trig]
if env['socket_export']:
    testprogs.append(test_json)
if env["libgpsmm"]:
    testprogs.append(test_gpsmm)

# Python programs
if not env['python']:
    python_built_extensions = []
    python_manpages = []
    python_misc = []
    python_progs = []
    python_targets = []
else:
    # installed python programs
    python_progs = ["gegps", "gpscat", "gpsfake", "gpsprof", "ubxtool", "zerk"]
    python_deps = {'gpscat': 'packet'}

    # python misc helpers and stuff
    python_misc = [
        "gpscap.py",
        "gpssim.py",
        "jsongen.py",
        "leapsecond.py",
        "maskaudit.py",
        "test_clienthelpers.py",
        "test_misc.py",
        "test_xgps_deps.py",
        "valgrind-audit.py"
    ]

    python_manpages = {
        "man/gegps.1": "man/gps.xml",
        "man/gpscat.1": "man/gpscat.xml",
        "man/gpsfake.1": "man/gpsfake.xml",
        "man/gpsprof.1": "man/gpsprof.xml",
        "man/ubxtool.1": "man/ubxtool.xml",
        "man/zerk.1": "man/zerk.xml",
    }

    # aiogps is only available on Python >= 3.6
    # FIXME check target_python, not current python
    if sys.version_info < (3, 6):
        env['aiogps'] = False
        announce("WARNING: Python too old: "
                 "gps/aiogps.py will not be installed\n")
    else:
        env['aiogps'] = True
        python_misc.extend(["example_aiogps.py", "example_aiogps_run"])

    if env['xgps']:
        # check for pycairo
        try:
            imp.find_module('cairo')
        except ImportError:
            # no pycairo, don't build xgps, xgpsspeed
            announce("WARNING: Python module pycairo not found.")
            env['xgps'] = False

        # check for pygobject
        try:
            imp.find_module('gi')
        except ImportError:
            # no pygobject, don't build xgps, xgpsspeed
            announce("WARNING: Python module pygobject not found.")
            env['xgps'] = False

    if env['xgps']:
        python_progs.extend(["xgps", "xgpsspeed"])
        python_manpages.update({
            "man/xgps.1": "man/gps.xml",
            "man/xgpsspeed.1": "man/gps.xml",
        })
    else:
        announce("WARNING: xgps and xgpsspeed will not be installed")

    # Glob() has to be run after all buildable objects defined
    if env['aiogps']:
        python_modules = Glob('gps/*.py', strings=True)
    else:
        python_modules = Glob('gps/*.py',strings=True, exclude="gps/aiogps.py")

    # Build Python binding
    #
    python_extensions = {
        "gps" + os.sep + "packet": ["crc24q.c",
                                    "driver_greis_checksum.c",
                                    "driver_rtcm2.c",
                                    "gpspacket.c",
                                    "hex.c",
                                    "isgps.c",
                                    "os_compat.c",
                                    "packet.c",
                                    ]
    }

    python_env = env.Clone()
    # FIXME: build of python wrappers doesn't pickup flags set for coveraging,
    # manually add them here
    if env['coveraging']:
        python_config['BASECFLAGS'] += ' -coverage'
        python_config['LDFLAGS'] += ' -coverage'
        python_config['LDSHARED'] += ' -coverage'
    # in case CC/CXX was set to the scan-build wrapper,
    # ensure that we build the python modules with scan-build, too
    if env['CC'] is None or env['CC'].find('scan-build') < 0:
        python_env['CC'] = python_config['CC']
        # As we seem to be changing compilers we must assume that the
        # CCFLAGS are incompatible with the new compiler. If we should
        # use other flags, the variable or the variable for this
        # should be predefined.
        if python_config['CC'].split()[0] != env['CC']:
            python_env['CCFLAGS'] = ''
    else:
        python_env['CC'] = (' '.join([env['CC']] +
                            python_config['CC'].split()[1:]))
    if env['CXX'] is None or env['CXX'].find('scan-build') < 0:
        python_env['CXX'] = python_config['CXX']
        # As we seem to be changing compilers we must assume that the
        # CCFLAGS or CXXFLAGS are incompatible with the new
        # compiler. If we should use other flags, the variable or the
        # variable for this should be predefined.
        if python_config['CXX'].split()[0] != env['CXX']:
            python_env['CCFLAGS'] = ''
            python_env['CXXFLAGS'] = ''
    else:
        python_env['CXX'] = (' '.join([env['CXX']] +
                             python_config['CXX'].split()[1:]))

    ldshared = python_config['LDSHARED']
    ldshared = ldshared.replace('-fPIE', '')
    ldshared = ldshared.replace('-pie', '')
    python_env.Replace(SHLINKFLAGS=[],
                       LDFLAGS=python_config['LDFLAGS'],
                       LINK=ldshared,
                       SHLIBPREFIX="",
                       SHLIBSUFFIX=python_config['SO'],
                       CPPPATH=[python_config['INCLUDEPY']],
                       CPPFLAGS=python_config['OPT'],
                       CFLAGS=python_config['BASECFLAGS'],
                       CXXFLAGS=python_config['BASECFLAGS'])

    python_objects = {}
    python_compiled_libs = {}
    for ext, sources in python_extensions.items():
        python_objects[ext] = []
        for src in sources:
            python_objects[ext].append(
                python_env.NoCache(
                    python_env.SharedObject(
                        src.split(".")[0] + '-py_' +
                        '_'.join(['%s' % (x) for x in sys.version_info]) +
                        python_config['SO'], src
                    )
                )
            )
        python_compiled_libs[ext] = python_env.SharedLibrary(
            ext, python_objects[ext])

    # Make sure we know about compiled dependencies
    for prog, dep in python_deps.items():
        env.Depends(prog, python_compiled_libs['gps' + os.sep + dep])

    # Make PEP 241 Metadata 1.0.
    # Why not PEP 314 (V1.1) or PEP 345 (V1.2)?
    # V1.2 and V1.2 require a Download-URL to an installable binary
    python_egg_info_source = """Metadata-Version: 1.0
Name: gps
Version: %s
Summary: Python libraries for the gpsd service daemon
Home-page: %s
Author: the GPSD project
Author-email: %s
License: BSD
Keywords: GPS
Description: The gpsd service daemon can monitor one or more GPS devices \
connected to a host computer, making all data on the location and movements \
of the sensors available to be queried on TCP port 2947.
Platform: UNKNOWN
""" % (gpsd_version, website, devmail)
    python_egg_info = python_env.Textfile(target="gps-%s.egg-info"
                                          % (gpsd_version, ),
                                          source=python_egg_info_source)
    python_built_extensions = list(python_compiled_libs.values())
    python_targets = python_built_extensions + [python_egg_info]


env.Command(target="packet_names.h", source="packet_states.h",
            action="""
    rm -f $TARGET &&\
    sed -e '/^ *\\([A-Z][A-Z0-9_]*\\),/s//   \"\\1\",/' <$SOURCE >$TARGET &&\
    chmod a-w $TARGET""")

# timebase.h - always built in order to include current GPS week


def timebase_h(target, source, env):
    f = open(target[0].abspath, 'w')
    f.write(leapsecond.make_leapsecond_include(source[0].abspath))
    f.close()


timebase = env.Command(target="timebase.h",
                       source=["leapseconds.cache"], action=timebase_h)
env.AlwaysBuild(timebase)

env.Textfile(target="gpsd_config.h", source=confdefs)

env.Command(target="gps_maskdump.c",
            source=["maskaudit.py", "gps.h", "gpsd.h"],
            action='''
    rm -f $TARGET &&\
        $SC_PYTHON $SOURCE -c $SRCDIR >$TARGET &&\
        chmod a-w $TARGET''')

env.Command(target="ais_json.i", source="jsongen.py", action='''\
    rm -f $TARGET &&\
    $SC_PYTHON $SOURCE --ais --target=parser >$TARGET &&\
    chmod a-w $TARGET''')

generated_sources = ['packet_names.h', 'timebase.h', "ais_json.i",
                     'gps_maskdump.c', 'revision.h', 'gpsd.php',
                     'gpsd_config.h']

# Helper functions for revision hackery


def GetMtime(file):
    """Get mtime of given file, or 0."""
    try:
        return os.stat(file).st_mtime
    except OSError:
        return 0


def FileList(patterns, exclusions=None):
    """Get list of files based on patterns, minus excluded files."""
    files = functools.reduce(operator.add, map(glob.glob, patterns), [])
    for file in exclusions:
        try:
            files.remove(file)
        except ValueError:
            pass
    return files


# generate revision.h
if 'dev' in gpsd_version:
    (st, rev) = _getstatusoutput('git describe --tags')
    if st != 0:
        # Use timestamp from latest relevant file
        files = FileList(['*.c', '*.cpp', '*.h', '*.in', 'SConstruct'],
                         generated_sources)
        timestamps = map(GetMtime, files)
        if timestamps:
            from datetime import datetime
            latest = datetime.fromtimestamp(sorted(timestamps)[-1])
            rev = '%s-%s' % (gpsd_version, latest.isoformat())
        else:
            rev = gpsd_version  # Paranoia
else:
    rev = gpsd_version
revision = '''/* Automatically generated file, do not edit */
#define REVISION "%s"
''' % (polystr(rev.strip()),)
env.Textfile(target="revision.h", source=[revision])

# leapseconds.cache is a local cache for information on leapseconds issued
# by the U.S. Naval observatory. It gets kept in the repository so we can
# build without Internet access.


def leapseconds_cache_rebuild(target, source, env):
    if not env["leapfetch"]:
        sys.stdout.write("Leapsecond fetch suppressed by leapfetch=no.\n")
    elif not leapsecond.conditional_leapsecond_fetch(target[0].abspath,
                                                     timeout=15):
        sys.stdout.write("try building with leapfetch=no.\n")


if 'dev' in gpsd_version or not os.path.exists('leapseconds.cache'):
    leapseconds_cache = env.Command(target="leapseconds.cache",
                                    source="leapsecond.py",
                                    action=leapseconds_cache_rebuild)
    env.Clean(leapseconds_cache, "leapsecond.pyc")
    env.NoClean(leapseconds_cache)
    env.Precious(leapseconds_cache)
    env.AlwaysBuild(leapseconds_cache)

if env['systemd']:
    udevcommand = 'TAG+="systemd", ENV{SYSTEMD_WANTS}="gpsdctl@%k.service"'
else:
    udevcommand = 'RUN+="%s/gpsd.hotplug"' % (env['udevdir'], )


# Instantiate some file templates.  We'd like to use the Substfile builtin
# but it doesn't seem to work in scons 1.20
def substituter(target, source, env):
    substmap = (
        ('@ANNOUNCE@',   annmail),
        ('@BUGTRACKER@', bugtracker),
        ('@CGIUPLOAD@',  cgiupload),
        ('@CLONEREPO@',  clonerepo),
        ('@DATE@',       time.asctime()),
        ('@DEVMAIL@',    devmail),
        ('@DOWNLOAD@',   download),
        ('@FORMSERVER@', formserver),
        ('@GITREPO@',    gitrepo),
        ('@includedir@',     installdir('includedir', add_destdir=False)),
        ('@IRCCHAN@',    ircchan),
        ('@libdir@',     installdir('libdir', add_destdir=False)),
        ('@LIBGPSVERSION@', libgps_version),
        ('@MAILMAN@',    mailman),
        ('@MAINPAGE@',   mainpage),
        ('@MASTER@',     'DO NOT HAND_HACK! THIS FILE IS GENERATED'),
        ('@prefix@',     env['prefix']),
        ('@PROJECTPAGE@', projectpage),
        ('@QTVERSIONED@', env['qt_versioned']),
        ('@SCPUPLOAD@',  scpupload),
        ('@SITENAME@',   sitename),
        ('@SITESEARCH@', sitesearch),
        ('@TIPLINK@',    tiplink),
        ('@TIPWIDGET@',  tipwidget),
        ('@udevcommand@',    udevcommand),
        ('@USERMAIL@',   usermail),
        ('@VERSION@',    gpsd_version),
        ('@WEBFORM@',    webform),
        ('@WEBSITE@',    website),
    )

    sfp = open(str(source[0]))
    content = sfp.read()
    sfp.close()
    for (s, t) in substmap:
        content = content.replace(s, t)
    m = re.search("@[A-Z]+@", content)
    if m and m.group(0) not in map(lambda x: x[0], substmap):
        print("Unknown subst token %s in %s." % (m.group(0), sfp.name),
              file=sys.stderr)
    tfp = open(str(target[0]), "w")
    tfp.write(content)
    tfp.close()


templated = glob.glob("*.in") + glob.glob("*/*.in") + glob.glob("*/*/*.in")

# ignore files in subfolder called 'debian' - the Debian packaging
# tools will handle them.
templated = [x for x in templated if not x.startswith('debian/')]


for fn in templated:
    builder = env.Command(source=fn, target=fn[:-3], action=substituter)
    env.AddPostAction(builder, 'chmod -w $TARGET')
    if fn.endswith(".py.in"):
        env.AddPostAction(builder, 'chmod +x $TARGET')

# Documentation

base_manpages = {
    "man/cgps.1": "man/gps.xml",
    "man/gps.1": "man/gps.xml",
    "man/gps2udp.1": "man/gps2udp.xml",
    "man/gpsctl.1": "man/gpsctl.xml",
    "man/gpsd.8": "man/gpsd.xml",
    "man/gpsdctl.8": "man/gpsdctl.xml",
    "man/gpsdecode.1": "man/gpsdecode.xml",
    "man/gpsd_json.5": "man/gpsd_json.xml",
    "man/gpsinit.8": "man/gpsinit.xml",
    "man/gpsmon.1": "man/gpsmon.xml",
    "man/gpspipe.1": "man/gpspipe.xml",
    "man/gpsrinex.1": "man/gpsrinex.xml",
    "man/gpxlogger.1": "man/gpxlogger.xml",
    "man/lcdgps.1": "man/gps.xml",
    "man/libgps.3": "man/libgps.xml",
    "man/libgpsmm.3": "man/libgpsmm.xml",
    "man/libQgpsmm.3": "man/libgpsmm.xml",
    "man/srec.5": "man/srec.xml",
}

if env["timeservice"] or env["gpsdclients"]:
    base_manpages.update({
        "man/ntpshmmon.1": "man/ntpshmmon.xml",
    })

if tiocmiwait:
    base_manpages.update({
        "man/ppscheck.8": "man/ppscheck.xml",
    })

all_manpages = list(base_manpages.keys())
other_manpages = [
                  "man/gegps.1",
                  "man/xgps.1",
                  "man/xgpsspeed.1",
                  ]

if python_manpages:
    all_manpages += list(python_manpages.keys())

man_env = env.Clone()
if man_env.GetOption('silent'):
    man_env['SPAWN'] = filtered_spawn  # Suppress stderr chatter
manpage_targets = []
if manbuilder:
    items = list(base_manpages.items())
    if python_manpages:
        items += list(python_manpages.items())

    for (man, xml) in items:
        manpage_targets.append(man_env.Man(source=xml, target=man))

# Where it all comes together

build = env.Alias('build',
                  [libraries, sbin_binaries, bin_binaries, python_targets,
                   "gpsd.php", manpage_targets,
                   "libgps.pc", "gpsd.rules"])

if qt_env:
    # duplicate above?
    test_qgpsmm = env.Program('tests/test_qgpsmm', ['tests/test_gpsmm.cpp'],
                              LIBPATH=['.'],
                              OBJPREFIX='qt-',
                              LIBS=['Qgpsmm', 'gps'])
    build_qt = qt_env.Alias('build', [compiled_qgpsmmlib, test_qgpsmm])
    qt_env.Default(*build_qt)
    testprogs.append(test_qgpsmm)

if env['python']:
    build_python = python_env.Alias('build', python_targets)
    python_env.Default(*build_python)

# Installation and deinstallation

# Not here because too distro-specific: udev rules, desktop files, init scripts

# It's deliberate that we don't install gpsd.h. It's full of internals that
# third-party client programs should not see.
headerinstall = [env.Install(installdir('includedir'), x)
                 for x in ("libgpsmm.h", "gps.h")]

binaryinstall = []
binaryinstall.append(env.Install(installdir('sbindir'), sbin_binaries))
binaryinstall.append(env.Install(installdir('bindir'), bin_binaries))
binaryinstall.append(LibraryInstall(env, installdir('libdir'), libgps_shared,
                                    libgps_version))
# Work around a minor bug in InstallSharedLib() link handling
env.AddPreAction(binaryinstall, 'rm -f %s/libgps.*' % (installdir('libdir'), ))

if qt_env:
    binaryinstall.append(LibraryInstall(qt_env, installdir('libdir'),
                         compiled_qgpsmmlib, libgps_version))

if ((not env['debug'] and not env['profiling'] and not env['nostrip'] and
     not sys.platform.startswith('darwin'))):
    env.AddPostAction(binaryinstall, '$STRIP $TARGET')

if env['python']:
    python_module_dir = str(python_libdir) + os.sep + 'gps'
    python_extensions_install = python_env.Install(DESTDIR + python_module_dir,
                                                   python_built_extensions)
    if ((not env['debug'] and not env['profiling'] and
         not env['nostrip'] and not sys.platform.startswith('darwin'))):
        python_env.AddPostAction(python_extensions_install, '$STRIP $TARGET')

    python_modules_install = python_env.Install(DESTDIR + python_module_dir,
                                                python_modules)

    python_progs_install = python_env.Install(installdir('bindir'),
                                              python_progs)

    python_egg_info_install = python_env.Install(DESTDIR + str(python_libdir),
                                                 python_egg_info)
    python_install = [python_extensions_install,
                      python_modules_install,
                      python_progs_install,
                      python_egg_info_install,
                      # We don't need the directory explicitly for the
                      # install, but we do need it for the uninstall
                      Dir(DESTDIR + python_module_dir)]

    # Check that Python modules compile properly
    python_all = python_misc + python_modules + python_progs + ['SConstruct']
    check_compile = []
    for p in python_all:
        # split in two lines for readability
        check_compile.append('cp %s tmp.py; %s -tt -m py_compile tmp.py;' %
                             (p, sys.executable))
        check_compile.append('rm tmp.py*')

    python_compilation_regress = Utility('python-compilation-regress',
                                         python_all, check_compile)

    # Sanity-check Python code.
    # Bletch.  We don't really want to suppress W0231 E0602 E0611 E1123,
    # but Python 3 syntax confuses a pylint running under Python 2.
    # There's an internal error in astroid that requires we disable some
    # auditing. This is irritating as hell but there's no help for it short
    # of an upstream fix.
    python_lint = python_misc + python_modules + python_progs + ['SConstruct']

    pylint = Utility(
        "pylint", python_lint,
        ['''pylint --rcfile=/dev/null --dummy-variables-rgx='^_' '''
         '''--msg-template='''
         '''"{path}:{line}: [{msg_id}({symbol}), {obj}] {msg}" '''
         '''--reports=n --disable=F0001,C0103,C0111,C1001,C0301,C0122,C0302,'''
         '''C0322,C0324,C0323,C0321,C0330,C0411,C0413,E1136,R0201,R0204,'''
         '''R0801,'''
         '''R0902,R0903,R0904,R0911,R0912,R0913,R0914,R0915,W0110,W0201,'''
         '''W0121,W0123,W0231,W0232,W0234,W0401,W0403,W0141,W0142,W0603,'''
         '''W0614,W0640,W0621,W1504,E0602,E0611,E1101,E1102,E1103,E1123,'''
         '''F0401,I0011 ''' + " ".join(python_lint)])

    # Additional Python readability style checks
    pep8 = Utility("pep8", python_lint,
                   ['pycodestyle --ignore=W602,E122,E241 ' +
                    " ".join(python_lint)])

    flake8 = Utility("flake8", python_lint,
                     ['flake8 --ignore=E501,W602,E122,E241,E401 ' +
                      " ".join(python_lint)])

    # get version from each python prog
    # this ensures they can run and gps_versions match
    vchk = ''
    verenv = env['ENV'].copy()
    verenv['DISPLAY'] = ''  # Avoid launching X11 in X11 progs
    pp = []
    for p in python_progs:
        pp.append("$PYTHON $SRCDIR/%s -V" % p)
    python_versions = Utility('python-versions', python_progs, pp, ENV=verenv)

else:
    python_install = []
    python_compilation_regress = None
    python_versions = None

pc_install = [env.Install(installdir('pkgconfig'), 'libgps.pc')]
if qt_env:
    pc_install.append(qt_env.Install(installdir('pkgconfig'), 'Qgpsmm.pc'))
    pc_install.append(qt_env.Install(installdir('libdir'), 'libQgpsmm.prl'))


maninstall = []
for manpage in all_manpages:
    if not manbuilder and not os.path.exists(manpage):
        continue
    section = manpage.split(".")[1]
    dest = os.path.join(installdir('mandir'), "man" + section,
                        os.path.basename(manpage))
    maninstall.append(env.InstallAs(source=manpage, target=dest))
install = env.Alias('install', binaryinstall + maninstall + python_install +
                    pc_install + headerinstall)


def Uninstall(nodes):
    deletes = []
    for node in nodes:
        if node.__class__ == install[0].__class__:
            deletes.append(Uninstall(node.sources))
        else:
            deletes.append(Delete(str(node)))
    return deletes


uninstall = env.Command('uninstall', '',
                        Flatten(Uninstall(Alias("install"))) or "")
env.AlwaysBuild(uninstall)
env.Precious(uninstall)

# Target selection for '.' is badly broken. This is a general scons problem,
# not a glitch in this particular recipe. Avoid triggering the bug.


def error_action(target, source, env):
    raise SCons.Error.UserError("Target selection for '.' is broken.")


AlwaysBuild(Alias(".", [], error_action))


# Putting in all these -U flags speeds up cppcheck and allows it to look
# at configurations we actually care about.
Utility("cppcheck", ["gpsd.h", "packet_names.h"],
        "cppcheck -U__UNUSED__ -UUSE_QT -U__COVERITY__ -U__future__ "
        "-ULIMITED_MAX_CLIENTS -ULIMITED_MAX_DEVICES -UAF_UNSPEC -UINADDR_ANY "
        "-U_WIN32 -U__CYGWIN__ "
        "-UPATH_MAX -UHAVE_STRLCAT -UHAVE_STRLCPY -UIPTOS_LOWDELAY "
        "-UIPV6_TCLASS -UTCP_NODELAY -UTIOCMIWAIT --template gcc "
        "--enable=all --inline-suppr --suppress='*:driver_proto.c' "
        "--force $SRCDIR")

# Check with clang analyzer
Utility("scan-build", ["gpsd.h", "packet_names.h"],
        "scan-build scons")


# Check the documentation for bogons, too
Utility("xmllint", glob.glob("man/*.xml"),
        "for xml in $SOURCES; do xmllint --nonet --noout --valid $$xml; done")

# Use deheader to remove headers not required.  If the statistics line
# ends with other than '0 removed' there's work to be done.
Utility("deheader", generated_sources, [
    'deheader -x cpp -x contrib -x gpspacket.c '
    '-x monitor_proto.c -i gpsd_config.h -i gpsd.h '
    '-m "MORECFLAGS=\'-Werror -Wfatal-errors -DDEBUG \' scons -Q"',
])

# Perform all local code-sanity checks (but not the Coverity scan).
audit = env.Alias('audit',
                  ['cppcheck',
                   'pylint',
                   'scan-build',
                   'valgrind-audit',
                   'xmllint',
                   ])

#
# Regression tests begin here
#
# Note that the *-makeregress targets re-create the *.log.chk source
# files from the *.log source files.

# Unit-test the bitfield extractor
bits_regress = Utility('bits-regress', [test_bits], [
    '$SRCDIR/tests/test_bits --quiet'
])

# Unit-test the deg_to_str() converter
bits_regress = Utility('deg-regress', [test_gpsdclient], [
    '$SRCDIR/tests/test_gpsdclient'
])

# Unit-test the bitfield extractor
matrix_regress = Utility('matrix-regress', [test_matrix], [
    '$SRCDIR/tests/test_matrix --quiet'
])

# using regress-drivers requires socket_export being enabled.
if not env['socket_export'] or not env['python']:
    announce("GPS regression tests suppressed because socket_export "
             "or python is off.")
    gps_regress = None
    gpsfake_tests = None
else:
    # Regression-test the daemon.
    # But first dump the platform and its delay parameters.
    # The ":;" in this production and the later one forestalls an attempt by
    # SCons to install up to date versions of gpsfake and gpsctl if it can
    # find older versions of them in a directory on your $PATH.
    gps_herald = Utility('gps-herald', [gpsd, gpsctl, python_built_extensions],
                         ':; $PYTHON $PYTHON_COVERAGE $SRCDIR/gpsfake -T')
    gps_log_pattern = os.path.join('test', 'daemon', '*.log')
    gps_logs = glob.glob(gps_log_pattern)
    gps_names = [os.path.split(x)[-1][:-4] for x in gps_logs]
    gps_tests = []
    for gps_name, gps_log in zip(gps_names, gps_logs):
        gps_tests.append(Utility(
            'gps-regress-' + gps_name, gps_herald,
            '$SRCDIR/regress-driver -q -o -t $REGRESSOPTS ' + gps_log))
    if GetOption('num_jobs') <= 1:
        gps_regress = Utility('gps-regress', gps_herald,
                              '$SRCDIR/regress-driver $REGRESSOPTS %s'
                              % gps_log_pattern)
    else:
        gps_regress = env.Alias('gps-regress', gps_tests)

    # Run the passthrough log in all transport modes for better coverage
    gpsfake_log = os.path.join('test', 'daemon', 'passthrough.log')
    gpsfake_tests = []
    for name, opts in [['pty', ''], ['udp', '-u'], ['tcp', '-o -t']]:
        gpsfake_tests.append(Utility('gpsfake-' + name, gps_herald,
                                     '$SRCDIR/regress-driver'
                                     ' $REGRESSOPTS -q %s %s'
                                     % (opts, gpsfake_log)))
    env.Alias('gpsfake-tests', gpsfake_tests)

    # Build the regression tests for the daemon.
    # Note: You'll have to do this whenever the default leap second
    # changes in timebase.h.  The problem is in the SiRF tests;
    # that driver relies on the default until it gets the current
    # offset from subframe data.
    gps_rebuilds = []
    for gps_name, gps_log in zip(gps_names, gps_logs):
        gps_rebuilds.append(Utility('gps-makeregress-' + gps_name, gps_herald,
                                    '$SRCDIR/regress-driver -bq -o -t '
                                    '$REGRESSOPTS ' + gps_log))
    if GetOption('num_jobs') <= 1:
        Utility('gps-makeregress', gps_herald,
                '$SRCDIR/regress-driver -b $REGRESSOPTS %s' % gps_log_pattern)
    else:
        env.Alias('gps-makeregress', gps_rebuilds)

# To build an individual test for a load named foo.log, put it in
# test/daemon and do this:
#    regress-driver -b test/daemon/foo.log

# Regression-test the RTCM decoder.
if not env["rtcm104v2"]:
    announce("RTCM2 regression tests suppressed because rtcm104v2 is off.")
    rtcm_regress = None
else:
    rtcm_regress = Utility('rtcm-regress', [gpsdecode], [
        '@echo "Testing RTCM decoding..."',
        '@for f in $SRCDIR/test/*.rtcm2; do '
        '    echo "\tTesting $${f}..."; '
        '    TMPFILE=`mktemp -t gpsd-test.chk-XXXXXXXXXXXXXX`; '
        '    $SRCDIR/gpsdecode -u -j <$${f} >$${TMPFILE}; '
        '    diff -ub $${f}.chk $${TMPFILE} || echo "Test FAILED!"; '
        '    rm -f $${TMPFILE}; '
        'done;',
        '@echo "Testing idempotency of JSON dump/decode for RTCM2"',
        '@TMPFILE=`mktemp -t gpsd-test.chk-XXXXXXXXXXXXXX`; '
        '$SRCDIR/gpsdecode -u -e -j <test/synthetic-rtcm2.json >$${TMPFILE}; '
        '    grep -v "^#" test/synthetic-rtcm2.json | diff -ub - $${TMPFILE} '
        '    || echo "Test FAILED!"; '
        '    rm -f $${TMPFILE}; ',
    ])

# Rebuild the RTCM regression tests.
Utility('rtcm-makeregress', [gpsdecode], [
    'for f in $SRCDIR/test/*.rtcm2; do '
    '    $SRCDIR/gpsdecode -j <$${f} >$${f}.chk; '
    'done'
])

# Regression-test the AIVDM decoder.
if not env["aivdm"]:
    announce("AIVDM regression tests suppressed because aivdm is off.")
    aivdm_regress = None
else:
    # FIXME! Does not return a proper fail code
    aivdm_regress = Utility('aivdm-regress', [gpsdecode], [
        '@echo "Testing AIVDM decoding w/ CSV format..."',
        '@for f in $SRCDIR/test/*.aivdm; do '
        '    echo "\tTesting $${f}..."; '
        '    TMPFILE=`mktemp -t gpsd-test.chk-XXXXXXXXXXXXXX`; '
        '    $SRCDIR/gpsdecode -u -c <$${f} >$${TMPFILE}; '
        '    diff -ub $${f}.chk $${TMPFILE} || echo "Test FAILED!"; '
        '    rm -f $${TMPFILE}; '
        'done;',
        '@echo "Testing AIVDM decoding w/ JSON unscaled format..."',
        '@for f in $SRCDIR/test/*.aivdm; do '
        '    echo "\tTesting $${f}..."; '
        '    TMPFILE=`mktemp -t gpsd-test.chk-XXXXXXXXXXXXXX`; '
        '    $SRCDIR/gpsdecode -u -j <$${f} >$${TMPFILE}; '
        '    diff -ub $${f}.ju.chk $${TMPFILE} || echo "Test FAILED!"; '
        '    rm -f $${TMPFILE}; '
        'done;',
        '@echo "Testing AIVDM decoding w/ JSON scaled format..."',
        '@for f in $SRCDIR/test/*.aivdm; do '
        '    echo "\tTesting $${f}..."; '
        '    TMPFILE=`mktemp -t gpsd-test.chk-XXXXXXXXXXXXXX`; '
        '    $SRCDIR/gpsdecode -j <$${f} >$${TMPFILE}; '
        '    diff -ub $${f}.js.chk $${TMPFILE} || echo "Test FAILED!"; '
        '    rm -f $${TMPFILE}; '
        'done;',
        '@echo "Testing idempotency of unscaled JSON dump/decode for AIS"',
        '@TMPFILE=`mktemp -t gpsd-test.chk-XXXXXXXXXXXXXX`; '
        '$SRCDIR/gpsdecode -u -e -j <$SRCDIR/test/sample.aivdm.ju.chk '
        ' >$${TMPFILE}; '
        '    grep -v "^#" $SRCDIR/test/sample.aivdm.ju.chk '
        '    | diff -ub - $${TMPFILE} || echo "Test FAILED!"; '
        '    rm -f $${TMPFILE}; ',
        # Parse the unscaled json reference, dump it as scaled json,
        # and finally compare it with the scaled json reference
        '@echo "Testing idempotency of scaled JSON dump/decode for AIS"',
        '@TMPFILE=`mktemp -t gpsd-test.chk-XXXXXXXXXXXXXX`; '
        '$SRCDIR/gpsdecode -e -j <$SRCDIR/test/sample.aivdm.ju.chk '
        ' >$${TMPFILE};'
        '    grep -v "^#" $SRCDIR/test/sample.aivdm.js.chk '
        '    | diff -ub - $${TMPFILE} || echo "Test FAILED!"; '
        '    rm -f $${TMPFILE}; ',
    ])

# Rebuild the AIVDM regression tests.
Utility('aivdm-makeregress', [gpsdecode], [
    'for f in $SRCDIR/test/*.aivdm; do '
    '    $SRCDIR/gpsdecode -u -c <$${f} > $${f}.chk; '
    '    $SRCDIR/gpsdecode -u -j <$${f} > $${f}.ju.chk; '
    '    $SRCDIR/gpsdecode -j  <$${f} > $${f}.js.chk; '
    'done', ])

# Regression-test the packet getter.
packet_regress = UtilityWithHerald(
    'Testing detection of invalid packets...',
    'packet-regress', [test_packet], [
        '$SRCDIR/tests/test_packet | '
        ' diff -u $SRCDIR/test/packet.test.chk -', ])

# Rebuild the packet-getter regression test
Utility('packet-makeregress', [test_packet], [
    '$SRCDIR/tests/test_packet >$SRCDIR/test/packet.test.chk', ])

# Regression-test the geoid and variation tester.
geoid_regress = UtilityWithHerald(
    'Testing the geoid and variation models...',
    'geoid-regress', [test_geoid], ['$SRCDIR/tests/test_geoid'])

# Regression-test the calendar functions
time_regress = Utility('time-regress', [test_mktime], [
    '$SRCDIR/tests/test_mktime'
])

if not env['python']:
    unpack_regress = None
    misc_regress = None
else:
    # Regression test the unpacking code in libgps
    unpack_regress = UtilityWithHerald(
        'Testing the client-library sentence decoder...',
        'unpack-regress', [test_libgps], [
            '$SRCDIR/regress-driver $REGRESSOPTS -c'
            ' $SRCDIR/test/clientlib/*.log', ])
    # Unit-test the bitfield extractor
    misc_regress = Utility('misc-regress', [], [
        '$SRCDIR/test_clienthelpers.py',
        '$SRCDIR/test_misc.py'
    ])


# Build the regression test for the sentence unpacker
Utility('unpack-makeregress', [test_libgps], [
    '@echo "Rebuilding the client sentence-unpacker tests..."',
    '$SRCDIR/regress-driver $REGRESSOPTS -c -b $SRCDIR/test/clientlib/*.log'
])

# Unit-test the JSON parsing
if not env['socket_export']:
    json_regress = None
else:
    json_regress = Utility('json-regress', [test_json],
                           ['$SRCDIR/tests/test_json'])

# Unit-test timespec math
timespec_regress = Utility('timespec-regress', [test_timespec], [
    '$SRCDIR/tests/test_timespec'
])

# Unit-test float math
float_regress = Utility('float-regress', [test_float], [
    '$SRCDIR/tests/test_float'
])

# Unit-test trig math
trig_regress = Utility('trig-regress', [test_trig], [
    '$SRCDIR/tests/test_trig'
])

# consistency-check the driver methods
method_regress = UtilityWithHerald(
    'Consistency-checking driver methods...',
    'method-regress', [test_packet], [
        '$SRCDIR/tests/test_packet -c >/dev/null', ])

# Test the xgps/xgpsspeed dependencies
if not env['python'] or not env['xgps']:
    test_xgps_deps = None
else:
    test_xgps_deps = UtilityWithHerald(
        'Testing xgps/xgpsspeed dependencies (since xgps=yes)...',
        'test-xgps-deps', [], [
            '$PYTHON $SRCDIR/test_xgps_deps.py'])

# Run a valgrind audit on the daemon  - not in normal tests
valgrind_audit = Utility('valgrind-audit', [
    '$SRCDIR/valgrind-audit.py', python_built_extensions, gpsd],
    '$PYTHON $SRCDIR/valgrind-audit.py'
)

# Run test builds on remote machines
flocktest = Utility("flocktest", [], "cd devtools; ./flocktest " + gitrepo)


# Run all normal regression tests
describe = UtilityWithHerald(
    'Run normal regression tests for %s...' % rev.strip(),
    'describe', [], [])

# Delete all test programs
test_exes = [str(p) for p in Flatten(testprogs)]
test_objs = [p + '.o' for p in test_exes]
testclean = Utility('testclean', [],
                    'rm -f %s' % ' '.join(test_exes + test_objs))

test_nondaemon = [
    aivdm_regress,
    bits_regress,
    describe,
    float_regress,
    geoid_regress,
    json_regress,
    matrix_regress,
    method_regress,
    misc_regress,
    packet_regress,
    python_compilation_regress,
    python_versions,
    rtcm_regress,
    test_xgps_deps,
    time_regress,
    timespec_regress,
    # trig_regress,  # not ready
    unpack_regress,
]

test_quick = test_nondaemon + [gpsfake_tests]
test_noclean = test_quick + [gps_regress]

env.Alias('test-nondaemon', test_nondaemon)
env.Alias('test-quick', test_quick)
check = env.Alias('check', test_noclean)
env.Alias('testregress', check)
env.Alias('build-tests', testprogs)
build_all = env.Alias('build-all', build + testprogs)

# Remove all shared-memory segments.  Normally only needs to be run
# when a segment size changes.
Utility('shmclean', [], ["ipcrm  -M 0x4e545030;"
                         "ipcrm  -M 0x4e545031;"
                         "ipcrm  -M 0x4e545032;"
                         "ipcrm  -M 0x4e545033;"
                         "ipcrm  -M 0x4e545034;"
                         "ipcrm  -M 0x4e545035;"
                         "ipcrm  -M 0x4e545036;"
                         "ipcrm  -M 0x47505345;"
                         ])

# The website directory
#
# None of these productions are fired by default.
# The content they handle is the GPSD website, not included in
# release tarballs.

# asciidoc documents
if env.WhereIs('asciidoc'):
    adocfiles = ['AIVDM',
                 'client-howto',
                 'gpsd-time-service-howto',
                 'NMEA',
                 'ppp-howto',
                 'protocol-evolution',
                 'protocol-transition',
                 'time-service-intro',
                 ]
    asciidocs = ["www/" + stem + ".html" for stem in adocfiles] \
        + ["www/installation.html"] + ["www/README.html"]
    for stem in adocfiles:
        env.Command('www/%s.html' % stem, 'www/%s.adoc' % stem,
                    ['asciidoc -b html5 -a toc -o www/%s.html www/%s.adoc'
                     % (stem, stem)])
    env.Command("www/installation.html",
                "INSTALL.adoc",
                ["asciidoc -o www/installation.html INSTALL.adoc"])
    env.Command("www/README.html",
                "README.adoc",
                ["asciidoc -o www/README.html README.adoc"])
else:
    announce("Part of the website build requires asciidoc, not installed.")
    asciidocs = []

# Non-asciidoc webpages only
htmlpages = Split('''
    www/gps2udp.html
    www/gpscat.html
    www/gpsctl.html
    www/gpsdctl.html
    www/gpsdecode.html
    www/gpsd.html
    www/gpsd_json.html
    www/gpsfake.html
    www/gps.html
    www/gpsinit.html
    www/gpsmon.html
    www/gpspipe.html
    www/gpsprof.html
    www/gpsrinex.html
    www/gpxlogger.html
    www/hardware.html
    www/internals.html
    www/libgps.html
    www/libgpsmm.html
    www/ntpshmmon.html
    www/performance/performance.html
    www/ppscheck.html
    www/replacing-nmea.html
    www/srec.html
    www/ubxtool.html
    www/writing-a-driver.html
    www/zerk.html
    ''')

webpages = htmlpages + asciidocs + list(map(lambda f: f[:-3],
                                            glob.glob("www/*.in")))

www = env.Alias('www', webpages)

# Paste 'scons --quiet validation-list' to a batch validator such as
# http://htmlhelp.com/tools/validator/batch.html.en


def validation_list(target, source, env):
    for page in glob.glob("www/*.html"):
        if '-head' not in page:
            fp = open(page)
            if "Valid HTML" in fp.read():
                print(os.path.join(website, os.path.basename(page)))
            fp.close()


Utility("validation-list", [www], validation_list)

# How to update the website.  Assumes a logal GitLab pages setup.
# See .gitlab-ci.yml
upload_web = Utility("website", [www],
                     ['rsync --exclude="*.in" -avz www/ ' +
                      os.environ.get('WEBSITE', '.public'),
                      'cp TODO NEWS ' +
                      os.environ.get('WEBSITE', '.public')])

# When the URL declarations change, so must the generated web pages
for fn in glob.glob("www/*.in"):
    env.Depends(fn[:-3], "SConstruct")

if htmlbuilder:
    # Manual pages
    for xml in glob.glob("man/*.xml"):
        env.HTML('www/%s.html' % os.path.basename(xml[:-4]), xml)

    # DocBook documents
    for stem in ['writing-a-driver', 'performance/performance',
                 'replacing-nmea']:
        env.HTML('www/%s.html' % stem, 'www/%s.xml' % stem)

    # The internals manual.
    # Doesn't capture dependencies on the subpages
    env.HTML('www/internals.html', '$SRCDIR/doc/internals.xml')

# The hardware page
env.Command('www/hardware.html', ['gpscap.py',
                                  'www/hardware-head.html',
                                  'gpscap.ini',
                                  'www/hardware-tail.html'],
            ['(cat www/hardware-head.html && PYTHONIOENCODING=utf-8 '
             '$SC_PYTHON gpscap.py && cat www/hardware-tail.html) '
             '>www/hardware.html'])

# The diagram editor dia is required in order to edit the diagram masters
Utility("www/cycle.svg", ["www/cycle.dia"],
        ["dia -e www/cycle.svg www/cycle.dia"])

# Experimenting with pydoc.  Not yet fired by any other productions.
# scons www/ dies with this

# # if env['python']:
# #     env.Alias('pydoc', "www/pydoc/index.html")
# #
# #     # We need to run epydoc with the Python version the modules built for.
# #     # So we define our own epydoc instead of using /usr/bin/epydoc
# #     EPYDOC = "python -c 'from epydoc.cli import cli; cli()'"
# #     env.Command('www/pydoc/index.html', python_progs + glob.glob("*.py")
# #                 + glob.glob("gps/*.py"), [
# #         'mkdir -p www/pydoc',
# #         EPYDOC + " -v --html --graph all -n GPSD $SOURCES -o www/pydoc",
# #             ])

# Productions for setting up and performing udev tests.
#
# Requires root. Do "udev-install", then "tail -f /var/log/syslog" in
# another window, then run 'scons udev-test', then plug and unplug the
# GPS ad libitum.  All is well when you get fix reports each time a GPS
# is plugged in.
#
# In case you are a systemd user you might also need to watch the
# journalctl output. Instead of the hotplug script the gpsdctl@.service
# unit will handle hotplugging together with the udev rules.
#
# Note that a udev event can be triggered with an invocation like:
# udevadm trigger --sysname-match=ttyUSB0 --action add

if env['systemd']:
    systemdinstall_target = [env.Install(DESTDIR + systemd_dir,
                             "systemd/%s" % (x,)) for x in
                             ("gpsdctl@.service", "gpsd.service",
                              "gpsd.socket")]
    systemd_install = env.Alias('systemd_install', systemdinstall_target)
    systemd_uninstall = env.Command(
        'systemd_uninstall', '',
        Flatten(Uninstall(Alias("systemd_install"))) or "")

    env.AlwaysBuild(systemd_uninstall)
    env.Precious(systemd_uninstall)
    hotplug_wrapper_install = []
else:
    hotplug_wrapper_install = [
        'cp $SRCDIR/gpsd.hotplug ' + DESTDIR + env['udevdir'],
        'chmod a+x ' + DESTDIR + env['udevdir'] + '/gpsd.hotplug'
    ]

udev_install = Utility('udev-install', 'install', [
    'mkdir -p ' + DESTDIR + env['udevdir'] + '/rules.d',
    'cp $SRCDIR/gpsd.rules ' + DESTDIR + env['udevdir'] +
    '/rules.d/25-gpsd.rules', ] + hotplug_wrapper_install)

if env['systemd']:
    env.Requires(udev_install, systemd_install)

if env['systemd'] and not env["sysroot"]:
    systemctl_daemon_reload = Utility('systemctl-daemon-reload', '',
                                      ['systemctl daemon-reload || true'])
    env.AlwaysBuild(systemctl_daemon_reload)
    env.Precious(systemctl_daemon_reload)
    env.Requires(systemctl_daemon_reload, systemd_install)
    env.Requires(udev_install, systemctl_daemon_reload)


Utility('udev-uninstall', '', [
    'rm -f %s/gpsd.hotplug' % env['udevdir'],
    'rm -f %s/rules.d/25-gpsd.rules' % env['udevdir'],
])

Utility('udev-test', '', ['$SRCDIR/gpsd -N -n -F /var/run/gpsd.sock -D 5', ])

# Cleanup

# Dummy target for cleaning misc files
clean_misc = env.Alias('clean-misc')
# Since manpage targets are disabled in clean mode, we cover them here
env.Clean(clean_misc, all_manpages + other_manpages)
# Clean compiled Python
env.Clean(clean_misc,
          glob.glob('*.pyc') + glob.glob('gps/*.pyc') +
          glob.glob('gps/*.so') + ['gps/__pycache__'])
# Clean coverage and profiling files
env.Clean(clean_misc, glob.glob('*.gcno') + glob.glob('*.gcda'))
# Clean Python coverage files
env.Clean(clean_misc, glob.glob('.coverage*') + ['htmlcov/'])
# Clean Qt stuff
env.Clean(clean_misc, ['libQgpsmm.prl', 'Qgpsmm.pc'])
# Clean shared library files
env.Clean(clean_misc, glob.glob('*.so') + glob.glob('*.so.*'))
# Clean old location man page files
env.Clean(clean_misc, glob.glob('*.[0-8]'))
# Other misc items
env.Clean(clean_misc, ['config.log', 'contrib/ppscheck', 'contrib/clock_test',
                       'TAGS'])
# Clean scons state files
env.Clean(clean_misc, ['.sconf_temp', '.scons-option-cache', 'config.log'])

# Nuke scons state files
sconsclean = Utility("sconsclean", '',
                     ["rm -fr .sconf_temp .scons-option-cache config.log"])

# Default targets

if cleaning:
    env.Default(build_all, audit, clean_misc)
    announce("You must manually remove {}".format(sconsign_file))
else:
    env.Default(build)

# Tags for Emacs and vi
misc_sources = ['cgps.c',
                'gps2udp.c',
                'gpsctl.c',
                'gpsdctl.c',
                'gpsdecode.c',
                'gpspipe.c',
                'gpxlogger.c',
                'ntpshmmon.c',
                'ppscheck.c',
                ]
sources = libgpsd_sources + libgps_sources + gpsd_sources + gpsmon_sources + \
    misc_sources
env.Command('TAGS', sources, ['etags ' + " ".join(sources)])

# Release machinery begins here
#
# We need to be in the actual project repo (i.e. not doing a -Y build)
# for these productions to work.

if os.path.exists("gpsd.c") and os.path.exists(".gitignore"):
    distfiles = _getoutput(r"git ls-files | grep -v '^www/'").split()
    # for some reason distfiles is now a mix of byte strings and char strings
    distfiles = [polystr(d) for d in distfiles]

    if ".gitignore" in distfiles:
        distfiles.remove(".gitignore")
    distfiles += generated_sources
    distfiles += all_manpages
    if "packaging/rpm/gpsd.spec" not in distfiles:
        distfiles.append("packaging/rpm/gpsd.spec")

    # How to build a zip file.
    # Perversely, if the zip exists, it is modified, not replaced.
    # So delete it first.
    dozip = env.Command('zip', distfiles, [
        'rm -f gpsd-${VERSION}.zip',
        '@zip -ry gpsd-${VERSION}.zip $SOURCES -x contrib/ais-samples/\\*',
        '@ls -l gpsd-${VERSION}.zip',
    ])
    env.Clean(dozip, ["gpsd-${VERSION}.zip", "packaging/rpm/gpsd.spec"])

    # How to build a tarball.
    # The command assume the non-portable GNU tar extension
    # "--transform", and will thus fail if ${TAR} is not GNU tar.
    # scons in theory has code to cope with this, but in practice this
    # is not working.  On BSD-derived systems, install GNU tar and
    # pass TAR=gtar in the environment.
    # make a .tar.gz and a .tar.xz
    dist = env.Command('dist', distfiles, [
        '@${TAR} --transform "s:^:gpsd-${VERSION}/:S" '
        ' -czf gpsd-${VERSION}.tar.gz --exclude contrib/ais-samples $SOURCES',
        '@${TAR} --transform "s:^:gpsd-${VERSION}/:S" '
        ' -cJf gpsd-${VERSION}.tar.xz --exclude contrib/ais-samples $SOURCES',
        '@ls -l gpsd-${VERSION}.tar.gz',
    ])
    env.Clean(dist, ["gpsd-${VERSION}.tar.gz", "packaging/rpm/gpsd.spec"])

    # Make RPM from the specfile in packaging
    Utility('dist-rpm', dist, 'rpmbuild -ta gpsd-${VERSION}.tar.gz')

    # Make sure build-from-tarball works.
    testbuild = Utility('testbuild', [dist], [
        '${TAR} -xzvf gpsd-${VERSION}.tar.gz',
        'cd gpsd-${VERSION}; scons',
        'rm -fr gpsd-${VERSION}',
    ])

    releasecheck = env.Alias('releasecheck', [
        testbuild,
        check,
        audit,
        flocktest,
    ])

    # This is how to ship a release to the hosting site.
    # The chmod copes with the fact that scp will give a
    # replacement the permissions of the *original*...
    upload_release = Utility('upload-release', [dist], [
        'rm -f gpsd-*tar*sig',
        'gpg -b gpsd-${VERSION}.tar.gz',
        'gpg -b gpsd-${VERSION}.tar.xz',
        'chmod ug=rw,o=r gpsd-${VERSION}.tar.*',
        'scp gpsd-${VERSION}.tar.* ' + scpupload,
    ])

    # How to tag a release
    tag_release = Utility('tag-release', [], [
        'git tag -s -m "Tagged for external release ${VERSION}" \
         release-${VERSION}'])
    upload_tags = Utility('upload-tags', [], ['git push --tags'])

    # Local release preparation. This production will require Internet access,
    # but it doesn't do any uploads or public repo mods.
    #
    # Note that tag_release has to fire early, otherwise the value of REVISION
    # won't be right when revision.h is generated for the tarball.
    releaseprep = env.Alias("releaseprep",
                            [Utility("distclean", [], ["rm -f revision.h"]),
                             tag_release,
                             dist])
    # Undo local release preparation
    Utility("undoprep", [], ['rm -f gpsd-${VERSION}.tar.gz;',
                             'git tag -d release-${VERSION};'])

    # All a buildup to this.
    env.Alias("release", [releaseprep,
                          upload_release,
                          upload_tags,
                          upload_web])

    # Experimental release mechanics using shipper
    # This will ship a freecode metadata update
    Utility("ship", [dist, "control"],
            ['shipper version=%s | sh -e -x' % gpsd_version])

# The following sets edit modes for GNU EMACS
# Local Variables:
# mode:python
# End:
# vim: set expandtab shiftwidth=4
