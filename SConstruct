### SCons build recipe for the GPSD project

EnsureSConsVersion(1,1,0)

import os, sys, shutil, re, commands
from glob import glob
from subprocess import Popen, PIPE, call
from os import access, F_OK

# Warn user of current set of build options.
if os.path.exists('.scons-option-cache'):
    optfile = file('.scons-option-cache')
    print "Saved options:", optfile.read().replace("\n", ", ")[:-2]
    optfile.close()

#
# Build-control options
#

opts = Variables('.scons-option-cache')
opts.AddVariables(
    #
    # Feature options
    #
    # NMEA variants
    BoolVariable("nmea",          "NMEA support", True),
    BoolVariable("oncore",        "Motorola OnCore chipset support", True),
    BoolVariable("sirf",          "SiRF chipset support", True),
    BoolVariable("superstar2",    "SuperStarII chipset support", True),
    BoolVariable("tsip",          "Trimble TSIP support", True),
    BoolVariable("fv18",          "San Jose Navigation FV-18 support", True),
    BoolVariable("tripmate",      "DeLorme TripMate support", True),
    BoolVariable("earthmate",     "DeLorme EarthMate Zodiac support", True),
    BoolVariable("itrax",         "iTrax hardware support", True),
    BoolVariable("ashtech",       "Ashtech support", True),
    BoolVariable("navcom",        "Navcom support", True),
    BoolVariable("garmin",        "Garmin kernel driver support", True),
    BoolVariable("garmintxt",     "Garmin Simple Text support", True),
    BoolVariable("tnt",           "True North Technologies support", True),
    BoolVariable("oceanserver",   "OceanServer support", True),
    BoolVariable("ubx",           "UBX Protocol support", True),
    BoolVariable("geostar",       "Geostar Protocol support", True),
    BoolVariable("evermore",      "EverMore binary support", True),
    BoolVariable("mtk3301",       "MTK-3301 support", True),
    BoolVariable("gpsclock",      "GPSClock support", True),
    BoolVariable("rtcm104v2",     "rtcm104v2 support", True),
    BoolVariable("rtcm104v3",     "rtcm104v3 support", False),
    BoolVariable("ntrip",         "NTRIP support", True),
    BoolVariable("aivdm",         "AIVDM support", True),
    BoolVariable("timing",        "latency timing support", True),
    BoolVariable("clientdebug",   "client debugging support", True),
    BoolVariable("oldstyle",      "oldstyle (pre-JSON) protocol support", True),
    BoolVariable("control_socket","control socket for hotplug notifications", True),
    BoolVariable("profiling",     "profiling support", False),
    BoolVariable("ntpshm",        "NTP time hinting support", True),
    BoolVariable("pps",           "PPS time syncing support", True),
    BoolVariable("pps_on_cts",    "Enable PPS pulse on CTS rather than DCD", False),
    ("gpsd_user",   "privilege revocation user", "nobody"),
    ("gpsd_group",  "privilege revocation group if /dev/ttyS0 not found.", "nogroup"),
    ("fixed_port_speed",          "compile with fixed serial port speed", None),
    BoolVariable("bluetooth",     "Enable BlueZ support for Bluetooth devices", False),
    BoolVariable("socket_export", "data export over sockets", True),
    BoolVariable("dbus_export",   "enable DBUS export support", True),
    BoolVariable("shm_export",    "disable export via shared memory", True),
    ("max_clients",               "compile with limited maximum clients", None),
    ("max_devices",               "compile with maximum allowed devices", None),
    BoolVariable("reconfigure",   "allow gpsd to change device settings", True),
    BoolVariable("controlsend",   "allow gpsctl/gpsmon to change device settings", True),
    BoolVariable("squelch",       "squelch gpsd_report/gpsd_hexdump to save cpu", False),
    BoolVariable("libgpsmm",      "build C++ bindings", True),
    BoolVariable("libQgpsmm",     "build QT bindings", False),
    BoolVariable("ipv6",          "build IPv6 support", True),
    )

env = Environment(tools=["tar"], options=opts)

opts.Save('.scons-option-cache', env)
env.SConsignFile(".sconsign.dblite")

## Build help

Help("""Arguments may be a mixture of switches and targets in any order.
Switches apply to the entire build regardless of where they are in the order.

Options are cached in a file named .scons-option-cache and persist to later
invocations.  The file is editable.  Delete it to start fresh.  Current option
values can be listed with 'scons -h'.
""" + opts.GenerateHelpText(env, sort=cmp))

if GetOption("help"):
    Return()

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

# TODO: conditionalize these properly
usblibs = ["usb"]
bluezlibs = ["bluez"]
pthreadlibs = ["pthreads"]
dbuslibs = ["dbus"]

gpslibs = ["gps", "m"]
gpsdlibs = ["gpsd"] + usblibs + bluezlibs + gpslibs

## Programs to be built

gpsd = env.Program('gpsd', ['gpsd.c', 'gpsd_dbus.c'],
                   LIBS = gpsdlibs + pthreadlibs + dbuslibs)
gpsdecode = env.Program('gpsdecode', ['gpsdecode.c'], LIBS=gpsdlibs)
gpsctl = env.Program('gpsctl', ['gpsctl.c'], LIBS=gpsdlibs)
gpsmon = env.Program('gpsmon', ['gpsmon.c'], LIBS=gpsdlibs)

gpspipe = env.Program('gpspipe', ['gpspipe.c'], LIBS=gpslibs)
gpxlogger = env.Program('gpxlogger', ['gpxlogger.c'], LIBS=gpslibs+dbuslibs)
lcdgps = env.Program('lcdgps', ['lcdgps.c'], LIBS=gpslibs)
cgps = env.Program('cgps', ['cgps.c'], LIBS=gpslibs)

env.Default(gpsd, gpsdecode, gpsctl, gpsmon, gpspipe, gpxlogger, lcdgps, cgps)

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
