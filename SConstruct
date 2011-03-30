### SCons build recipe for the GPSD project

env = Environment()

## Two shared libraries provide most of the code for the C programs

# TODO: Conditionallyinclude daemon.c whereit's not present
env.SharedLibrary(target="gps", source=[
	"ais_json.c",
        #"daemon.c",
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

env.SharedLibrary(target="gpsd", source=[
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

# The following sets edit modes for GNU EMACS
# Local Variables:
# mode:python
# End:
