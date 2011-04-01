### SCons build recipe for the GPSD project

EnsureSConsVersion(1,1,0)

import os, sys, shutil, re, commands
from glob import glob
from subprocess import Popen, PIPE, call
from os import access, F_OK

# Warn user of current set of build options.
if os.path.exists('.scons-option-cache'):
    optfile = file('.scons-option-cache')
    optxt = optfile.read().replace("\n", ", ")
    optfile.close()
    if optxt:
        print "Saved options:", optxt[:-2]

#
# Build-control options
#

opts = Variables('.scons-option-cache')
opts.AddVariables(
    #
    # Feature options
    #
    # GPS types
    BoolVariable("ashtech",       "Ashtech support", True),
    BoolVariable("earthmate",     "DeLorme EarthMate Zodiac support", True),
    BoolVariable("evermore",      "EverMore binary support", True),
    BoolVariable("fv18",          "San Jose Navigation FV-18 support", True),
    BoolVariable("garmin",        "Garmin kernel driver support", True),
    BoolVariable("garmintxt",     "Garmin Simple Text support", True),
    BoolVariable("geostar",       "Geostar Protocol support", True),
    BoolVariable("itrax",         "iTrax hardware support", True),
    BoolVariable("mtk3301",       "MTK-3301 support", True),
    BoolVariable("navcom",        "Navcom support", True),
    BoolVariable("nmea",          "NMEA support", True),
    BoolVariable("oncore",        "Motorola OnCore chipset support", True),
    BoolVariable("sirf",          "SiRF chipset support", True),
    BoolVariable("superstar2",    "Novatel SuperStarII chipset support", True),
    BoolVariable("tnt",           "True North Technologies support", True),
    BoolVariable("tripmate",      "DeLorme TripMate support", True),
    BoolVariable("tsip",          "Trimble TSIP support", True),
    BoolVariable("ubx",           "UBX Protocol support", True),
    # Non-GPS protcols
    BoolVariable("aivdm",         "AIVDM support", True),
    BoolVariable("gpsclock",      "GPSClock support", True),
    BoolVariable("ntrip",         "NTRIP support", True),
    BoolVariable("oceanserver",   "OceanServer support", True),
    BoolVariable("rtcm104v2",     "rtcm104v2 support", True),
    BoolVariable("rtcm104v3",     "rtcm104v3 support", False),
    # Time service
    BoolVariable("ntpshm",        "NTP time hinting support", True),
    BoolVariable("pps",           "PPS time syncing support", True),
    BoolVariable("pps_on_cts",    "PPS pulse on CTS rather than DCD", False),
    # Export methods
    BoolVariable("socket_export", "data export over sockets", True),
    BoolVariable("dbus_export",   "enable DBUS export support", True),
    BoolVariable("shm_export",    "export via shared memory", True),
    # Communication
    BoolVariable("bluetooth",     "BlueZ support for Bluetooth devices", False),
    BoolVariable("ipv6",          "build IPv6 support", True),
    # Client-side options
    BoolVariable("clientdebug",   "client debugging support", True),
    BoolVariable("oldstyle",      "oldstyle (pre-JSON) protocol support", True),
    BoolVariable("libgpsmm",      "build C++ bindings", True),
    BoolVariable("libQgpsmm",     "build QT bindings", False),
    # Performance-tuning
    ("max_clients",               "compile with limited maximum clients", None),
    ("max_devices",               "compile with maximum allowed devices", None),
    BoolVariable("reconfigure",   "allow gpsd to change device settings", True),
    BoolVariable("controlsend",   "allow gpsctl/gpsmon to change device settings", True),
    BoolVariable("cheapfloats",   "float ops are cheap, compute all error estimates", True),
    BoolVariable("squelch",       "squelch gpsd_report/gpsd_hexdump to save cpu", False),
    ("fixed_port_speed",          "compile with fixed serial port speed", None),
    # Miscellaneous
    BoolVariable("profiling",     "Build with profiling enabled", True),
    BoolVariable("timing",        "latency timing support", True),
    BoolVariable("control_socket","control socket for hotplug notifications", True),
    ("gpsd_user",   "privilege revocation user", "nobody"),
    ("gpsd_group",  "privilege revocation group if /dev/ttyS0 not found.", "nogroup"),
    )

env = Environment(tools=["default", "tar"], options=opts)

opts.Save('.scons-option-cache', env)
env.SConsignFile(".sconsign.dblite")

# Should we build with profiling?
if ARGUMENTS.get('profiling'):
    env.Append(CCFLAGS=['-pg'])
    env.Append(LDFLAGS=['-pg'])

## Build help

Help("""Arguments may be a mixture of switches and targets in any order.
Switches apply to the entire build regardless of where they are in the order.

Options are cached in a file named .scons-option-cache and persist to later
invocations.  The file is editable.  Delete it to start fresh.  Current option
values can be listed with 'scons -h'.
""" + opts.GenerateHelpText(env, sort=cmp))

if GetOption("help"):
    Return()

## Configuration

config = Configure(env)

confdefs = []

if not config.CheckCXX():
    print('!! Your compiler and/or environment is not correctly configured.')
    Exit(0)

if not config.CheckLib('libncurses'):
    ncurseslibs = []
else:
    ncurseslibs = ["ncurses"]

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
    confdefs.append("#define HAVE_LIBPTHREAD\n\n")
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
    confdefs.append("/* #undef HAVE_BLUEZ */\n\n")
    bluezlibs = []
else:
    confdefs.append("#define HAVE_BLUEZ 1\n\n")
    bluezlibs = ["bluez"]

sys.stdout.writelines(confdefs)

env = config.Finish()

## Two shared libraries provide most of the code for the C programs

env.Library(target="gps", source=[
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

env.Library(target="gpsd", source=[
	"bits.c",
	"bsd-base64.c",
	"crc24q.c",
	"gpsd_json.c",
	"isgps.c",
	"gpsd_maskdump.c",
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

# The libraries have dependencies on system libraries 

gpslibs = ["gps", "m"]
gpsdlibs = ["gpsd"] + usblibs + bluezlibs + gpslibs

## Programs to be built

gpsd = env.Program('gpsd', ['gpsd.c', 'gpsd_dbus.c'],
                   LIBS = gpsdlibs + pthreadlibs + rtlibs + dbuslibs)
gpsdecode = env.Program('gpsdecode', ['gpsdecode.c'], LIBS=gpsdlibs)
gpsctl = env.Program('gpsctl', ['gpsctl.c'], LIBS=gpsdlibs)
gpsmon = env.Program('gpsmon', ['gpsmon.c'], LIBS=gpsdlibs)
gpspipe = env.Program('gpspipe', ['gpspipe.c'], LIBS=gpslibs)
gpxlogger = env.Program('gpxlogger', ['gpxlogger.c'], LIBS=gpslibs+dbuslibs)
lcdgps = env.Program('lcdgps', ['lcdgps.c'], LIBS=gpslibs)
cgps = env.Program('cgps', ['cgps.c'], LIBS=gpslibs + ncurseslibs)

default_targets = [gpsd, gpsdecode, gpsctl, gpsmon, gpspipe, gpxlogger, lcdgps]
if ncurseslibs:
    default_targets.append(cgps)
env.Default(*default_targets)

# Test programs
testprogs = ["test_float", "test_trig", "test_bits", "test_packet",
             "test_mkgmtime", "test_geoid", "test_json"]
# TODO: conditionally add test_gpsmm and test_qgpsmm

# Python programs
python_progs = ["gpscat", "gpsfake", "gpsprof", "xgps", "xgpsspeed"]
python_modules = ["__init__.py", "misc.py", "fake.py", "gps.py", "client.py"]

# The following sets edit modes for GNU EMACS
# Local Variables:
# mode:python
# End:
