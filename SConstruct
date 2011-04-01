### SCons build recipe for the GPSD project

EnsureSConsVersion(1,1,0)

import os, sys, shutil, re, commands

gpsd_version="3.0~dev"

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
    ("rtcm104v3",     False, "rtcm104v3 support"),
    # Time service
    ("ntpshm",        True,  "NTP time hinting support"),
    ("pps",           True,  "PPS time syncing support"),
    ("pps_on_cts",    False, "PPS pulse on CTS rather than DCD"),
    # Export methods
    ("socket-export", True,  "data export over sockets"),
    ("dbus-export",   True,  "enable DBUS export support"),
    ("shm-export",    True,  "export via shared memory"),
    # Communication
    ("bluetooth",     False, "BlueZ support for Bluetooth devices"),
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
    ("gpsd-user",           "USER",    "privilege revocation user",   ""),
    ("gpsd-group",          "GROUP",   "privilege revocation group",  ""),
    ("limited-max-clients", "CLIENTS", "maximum allowed clients",     0),
    ("limited-max-devices", "DEVICES", "maximum allowed devices",     0),
    ("fixed-port-speed",    "SPEED",   "fixed serial port speed",     0),
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

env = Environment(tools=["default", "tar"])
env.SConsignFile(".sconsign.dblite")

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
    
if not config.CheckLib('libncurses'):
    ncurseslibs = []
else:
    ncurseslibs = ["ncurses"]

# TODO: Check that this is libusb 1.x.x, not 0.1 
if not config.CheckLib('libusb'):
    confdefs.append("/* #undef HAVE_LIBUSB */\n\n")
    usblibs = []
else:
    confdefs.append("#define HAVE_LIBUSB 1\n\n")
    usblibs = ["usb"]
    
if not config.CheckLib('libpthread'):
    confdefs.append("/* #undef HAVE_LIBPTHREAD */\n\n")
    pthreadlibs = []
else:
    confdefs.append("#define HAVE_LIBPTHREAD 1\n\n")
    pthreadlibs = ["pthread"]
    
if not config.CheckLib('librt'):
    confdefs.append("/* #undef HAVE_LIBRT */\n\n")
    rtlibs = []
else:
    confdefs.append("#define HAVE_LIBRT 1\n\n")
    rtlibs = ["rt"]
    
if not config.CheckLib('libdbus'):
    confdefs.append("/* #undef HAVE_LIBDBUS */\n\n")
    dbuslibs = []
else:
    confdefs.append("#define HAVE_LIBDBUS 1\n\n")
    dbuslibs = ["dbus"]
    
if not config.CheckLib('libbluez'):
    confdefs.append("/* #undef HAVE_LIBBLUEZ */\n\n")
    bluezlibs = []
else:
    confdefs.append("#define HAVE_LIBBLUEZ 1\n\n")
    bluezlibs = ["bluez"]

keys = map(lambda x: x[0], boolopts) + map(lambda x: x[0], nonboolopts)
keys.sort()
for key in keys:
    key = internalize(key)
    value = GetOption(key)
    if type(value) == type(True):
        if value:
            confdefs.append("#define %s_ENABLE 1\n\n" % key.upper())
        else:
            confdefs.append("/* #undef %s_ENABLE */\n\n" % key.upper())
    elif value in (0, ""):
        confdefs.append("/* #undef %s */\n\n" % key.upper())
    else:
        if type(value) == type(-1):
            confdefs.append("#define %d %s\n\n" % value)
        elif type(value) == type(""):
            confdefs.append("#define %d \"%s\"\n\n" % value)
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

if not os.path.exists('leapseconds.cache'):
    try:
        print "retrieving leapseconds.cache"
        save_leapseconds('leapseconds.cache')
    except:
        print "Failed to retrieve leapseconds"

env = config.Finish()

## Two shared libraries provide most of the code for the C programs

compiled_gpslib = env.Library(target="gps", source=[
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
	"netlib.c",
	"rtcm2_json.c",
	"shared_json.c",
	"strl.c",
])

compiled_gpsdlib = env.Library(target="gpsd", source=[
	"bits.c",
	"bsd-base64.c",
	"crc24q.c",
	"gpsd_json.c",
	"isgps.c",
	"timebase.c",
	"libgpsd_core.c",
	"net_dgpsip.c",
	"net_gnss_dispatch.c",
	"net_ntrip.c",
	"ntpshm.c",
	"packet.c",
	"pseudonmea.c",
	"serial.c",
	"srecord.c",
	"subframe.c",
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
gpsd = env.Program('gpsd', ['gpsd.c', 'gpsd_dbus.c'],
                   LIBS = gpsdlibs + pthreadlibs + rtlibs + dbuslibs)
gpsdecode = env.Program('gpsdecode', ['gpsdecode.c'], LIBS=gpsdlibs)
gpsctl = env.Program('gpsctl', ['gpsctl.c'], LIBS=gpsdlibs)
gpsmon = env.Program('gpsmon', ['gpsmon.c'], LIBS=gpsdlibs)
gpspipe = env.Program('gpspipe', ['gpspipe.c'], LIBS=gpslibs)
gpxlogger = env.Program('gpxlogger', ['gpxlogger.c'], LIBS=gpslibs+dbuslibs)
lcdgps = env.Program('lcdgps', ['lcdgps.c'], LIBS=gpslibs)
cgps = env.Program('cgps', ['cgps.c'], LIBS=gpslibs + ncurseslibs)

default_targets = [gpsd, gpsdecode, gpsctl, gpspipe, gpxlogger, lcdgps]
if ncurseslibs:
    default_targets + [cgps, gpsmon]
env.Default(*default_targets)

# Test programs
# TODO: conditionally add test_gpsmm and test_qgpsmm
test_float = env.Program('test_float', ['test_float.c'])
test_geoid = env.Program('test_geoid', ['test_geoid.c'], LIBS=gpslibs)
test_json = env.Program('test_json', ['test_json.c'], LIBS=gpslibs)
test_mkgmtime = env.Program('test_mkgmtime', ['test_mkgmtime.c'], LIBS=gpslibs)
test_trig = env.Program('test_trig', ['test_trig.c'], LIBS=["m"])
test_packet = env.Program('test_packet', ['test_packet.c'], LIBS=gpsdlibs)
test_bits = env.Program('test_bits', ['test_bits.c','bits.c'], LIBS=gpslibs)
testprogs = [test_float, test_trig, test_bits, test_packet,
             test_mkgmtime, test_geoid, test_json]

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
Depends(target="gpd.hc", dependency="gpsd.h-tail") 

# TO-DO: The '.' in the command may break out-of-directory builds.
env.Command(target="gps_maskdump.c", source="maskaudit.py", action='''
	rm -f $TARGET &&\
        python $SOURCE -c . >$TARGET &&\
        chmod a-w $TARGET''')
Depends(target="gps_maskdump.c", dependency="gps.h") 
Depends(target="gps_maskdump.c", dependency="gpsd.h") 

env.Command(target="ais_json.i", source="jsongen.py", action='''\
	rm -f $TARGET &&\
	python $SOURCE --ais --target=parser >$TARGET &&\
	chmod a-w $TARGET''')

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

# The following sets edit modes for GNU EMACS
# Local Variables:
# mode:python
# End:
