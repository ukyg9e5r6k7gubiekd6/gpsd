#!/usr/bin/env python
# $Id$
#
# gps.py -- Python interface to GPSD.
#
import time, calendar, math, socket, sys, select, json

api_major_version = 3   # bumped on incompatible changes
api_minor_version = 1   # bumped on compatible changes

NaN = float('nan')
def isnan(x): return str(x) == 'nan'

# Don't hand-hack this list, it's generated.
ONLINE_SET     	= 0x00000001
TIME_SET       	= 0x00000002
TIMERR_SET     	= 0x00000004
LATLON_SET     	= 0x00000008
ALTITUDE_SET   	= 0x00000010
SPEED_SET      	= 0x00000020
TRACK_SET      	= 0x00000040
CLIMB_SET      	= 0x00000080
STATUS_SET     	= 0x00000100
MODE_SET       	= 0x00000200
DOP_SET        	= 0x00000400
VERSION_SET    	= 0x00000800
HERR_SET       	= 0x00001000
VERR_SET       	= 0x00002000
PERR_SET       	= 0x00004000
POLICY_SET     	= 0x00020000
ERR_SET        	= (HERR_SET|VERR_SET|PERR_SET)
SATELLITE_SET  	= 0x00040000
RAW_SET        	= 0x00080000
USED_SET       	= 0x00100000
SPEEDERR_SET   	= 0x00200000
TRACKERR_SET   	= 0x00400000
CLIMBERR_SET   	= 0x00800000
DEVICE_SET     	= 0x01000000
DEVICELIST_SET 	= 0x02000000
DEVICEID_SET   	= 0x04000000
ERROR_SET      	= 0x08000000
RTCM2_SET      	= 0x10000000
RTCM3_SET      	= 0x20000000
AIS_SET        	= 0x40000000
PACKET_SET     	= 0x80000000
DATA_SET       	= ~(ONLINE_SET|PACKET_SET)

STATUS_NO_FIX = 0
STATUS_FIX = 1
STATUS_DGPS_FIX = 2
MODE_NO_FIX = 1
MODE_2D = 2
MODE_3D = 3
MAXCHANNELS = 12
SIGNAL_STRENGTH_UNKNOWN = NaN

WATCH_DISABLE	= 0x00
WATCH_ENABLE	= 0x01
WATCH_RAW	= 0x02
WATCH_SCALED	= 0x08
WATCH_NEWSTYLE	= 0x10
WATCH_OLDSTYLE	= 0x20

GPSD_PORT = 2947

class gpsfix:
    def __init__(self):
        self.mode = MODE_NO_FIX
        self.time = NaN
        self.ept = NaN
        self.latitude = self.longitude = 0.0
        self.epx = NaN
        self.epy = NaN
        self.altitude = NaN         # Meters
        self.epv = NaN
        self.track = NaN            # Degrees from true north
        self.speed = NaN            # Knots
        self.climb = NaN            # Meters per second
        self.epd = NaN
        self.eps = NaN
        self.epc = NaN

class gpsdata:
    "Position, track, velocity and status information returned by a GPS."

    class satellite:
        def __init__(self, PRN, elevation, azimuth, ss, used=None):
            self.PRN = PRN
            self.elevation = elevation
            self.azimuth = azimuth
            self.ss = ss
            self.used = used
        def __repr__(self):
            return "PRN: %3d  E: %3d  Az: %3d  Ss: %3d  Used: %s" % (
                self.PRN, self.elevation, self.azimuth, self.ss, "ny"[self.used]
            )

    def __init__(self):
        # Initialize all data members 
        self.online = 0                 # NZ if GPS on, zero if not

        self.valid = 0
        self.fix = gpsfix()

        self.status = STATUS_NO_FIX
        self.utc = ""

        self.satellites_used = 0        # Satellites used in last fix
        self.xdop = self.ydop = self.vdop = self.tdop = 0
        self.pdop = self.hdop = self.gdop = 0.0

        self.epe = 0.0

        self.satellites = []            # satellite objects in view

        self.gps_id = None
        self.driver_mode = 0
        self.baudrate = 0
        self.stopbits = 0
        self.cycle = 0
        self.mincycle = 0
        self.device = None
        self.devices = []

        self.version = None
        self.timings = None

    def __repr__(self):
        st = "Time:     %s (%s)\n" % (self.utc, self.fix.time)
        st += "Lat/Lon:  %f %f\n" % (self.fix.latitude, self.fix.longitude)
        if isnan(self.fix.altitude):
            st += "Altitude: ?\n"
        else:
            st += "Altitude: %f\n" % (self.fix.altitude)
        if isnan(self.fix.speed):
            st += "Speed:    ?\n"
        else:
            st += "Speed:    %f\n" % (self.fix.speed)
        if isnan(self.fix.track):
            st += "Track:    ?\n"
        else:
            st += "Track:    %f\n" % (self.fix.track)
        st += "Status:   STATUS_%s\n" % ("NO_FIX", "FIX", "DGPS_FIX")[self.status]
        st += "Mode:     MODE_%s\n" % ("ZERO", "NO_FIX", "2D", "3D")[self.fix.mode]
        st += "Quality:  %d p=%2.2f h=%2.2f v=%2.2f t=%2.2f g=%2.2f\n" % \
              (self.satellites_used, self.pdop, self.hdop, self.vdop, self.tdop, self.gdop)
        st += "Y: %s satellites in view:\n" % len(self.satellites)
        for sat in self.satellites:
          st += "    %r\n" % sat
        return st

class dictwrapper:
    "Wrapper that yields both class and dictionary behavior,"
    def __init__(self, **ddict):
        self.__dict__ = ddict
    def get(self, k, d=None):
        return self.__dict__.get(k, d)
    def keys(self):
        return self.__dict__.keys()
    def __getitem__(self, key):
        "Emulate dictionary, for new-style interface."
        return self.__dict__[key]
    def __setitem__(self, key, val):
        "Emulate dictionary, for new-style interface."
        self.__dict__[key] = val
    def __str__(self):
        return "<dictwrapper: " + str(self.__dict__) + ">"
    __repr__ = __str__

class gps(gpsdata):
    "Client interface to a running gpsd instance."
    def __init__(self, host="127.0.0.1", port="2947", verbose=0, mode=0):
        gpsdata.__init__(self)
        self.sock = None        # in case we blow up in connect
        self.sockfile = None
        self.connect(host, port)
        self.verbose = verbose
        self.raw_hook = None
        self.newstyle = False
        if mode:
            self.stream(mode)

    def __iter__(self):
        return self

    def connect(self, host, port):
        """Connect to a host on a given port.

        If the hostname ends with a colon (`:') followed by a number, and
        there is no port specified, that suffix will be stripped off and the
        number interpreted as the port number to use.
        """
        if not port and (host.find(':') == host.rfind(':')):
            i = host.rfind(':')
            if i >= 0:
                host, port = host[:i], host[i+1:]
            try: port = int(port)
            except ValueError:
                raise socket.error, "nonnumeric port"
        if not port: port = GPSD_PORT
        #if self.debuglevel > 0: print 'connect:', (host, port)
        msg = "getaddrinfo returns an empty list"
        self.sock = None
        self.sockfile = None
        for res in socket.getaddrinfo(host, port, 0, socket.SOCK_STREAM):
            af, socktype, proto, canonname, sa = res
            try:
                self.sock = socket.socket(af, socktype, proto)
                #if self.debuglevel > 0: print 'connect:', (host, port)
                self.sock.connect(sa)
                self.sockfile = self.sock.makefile()
            except socket.error, msg:
                #if self.debuglevel > 0: print 'connect fail:', (host, port)
                self.close()
                continue
            break
        if not self.sock:
            raise socket.error, msg

    def set_raw_hook(self, hook):
        self.raw_hook = hook

    def close(self):
        if self.sockfile:
            self.sockfile.close()
        if self.sock:
            self.sock.close()
        self.sock = None
        self.sockfile = None

    def __del__(self):
        self.close()

    def __oldstyle_unpack(self, buf):
        # unpack a daemon response into the gps instance members
        self.fix.time = 0.0
        fields = buf.strip().split(",")
        if fields[0] == "GPSD":
            for field in fields[1:]:
                if not field or field[1] != '=':
                    continue
                cmd = field[0].upper()
                data = field[2:]
                if data[0] == "?":
                    continue
                if cmd == 'A':
                    self.fix.altitude = float(data)
                    self.valid |= ALTITUDE_SET
                elif cmd == 'B':
                    parts = data.split()
                    self.baudrate = int(parts[0])
                    self.stopbits = int(parts[3])
                elif cmd == 'C':
                    parts = data.split()
                    if len(parts) == 2:
                        (self.cycle, self.mincycle) = map(float, parts)
                    else:
                        self.mincycle = self.cycle = float(data)
                elif cmd == 'D':
                    self.utc = data
                    self.fix.time = isotime(self.utc)
                    self.valid |= TIME_SET
                elif cmd == 'E':
                    parts = data.split()
                    (self.epe, eph, self.fix.epv) = map(float, parts)
                    self.epx = self.epy = eph
                    self.valid |= HERR_SET | VERR_SET | PERR_SET
                elif cmd == 'F':
                    self.device = data
                elif cmd == 'I':
                    self.gps_id = data
                elif cmd == 'K':
                    self.devices = data[1:].split()
                elif cmd == 'M':
                    self.fix.mode = int(data)
                    self.valid |= MODE_SET
                elif cmd == 'N':
                    self.driver_mode = int(data)
                elif cmd == 'O':
                    fields = data.split()
                    if fields[0] == '?':
                        self.fix.mode = MODE_NO_FIX
                    else:
                        def default(i, vbit=0, cnv=float):
                            if fields[i] == '?':
                                return NaN
                            else:
                                try:
                                    value = cnv(fields[i])
                                except ValueError:
                                    return NaN
                                self.valid |= vbit
                                return value
                        # clear all valid bits that might be set again below
                        self.valid &= ~(
                            TIME_SET | TIMERR_SET | LATLON_SET | ALTITUDE_SET |
                            HERR_SET | VERR_SET | TRACK_SET | SPEED_SET |
                            CLIMB_SET | SPEEDERR_SET | CLIMBERR_SET | MODE_SET
                        )
                        self.utc = fields[1]
                        self.fix.time = default(1, TIME_SET)
                        if not isnan(self.fix.time):
                            self.utc = isotime(self.fix.time)
                        self.fix.ept = default(2, TIMERR_SET)
                        self.fix.latitude = default(3, LATLON_SET)
                        self.fix.longitude = default(4)
                        self.fix.altitude = default(5, ALTITUDE_SET)
                        self.fix.epx = self.epy = default(6, HERR_SET)
                        self.fix.epv = default(7, VERR_SET)
                        self.fix.track = default(8, TRACK_SET)
                        self.fix.speed = default(9, SPEED_SET)
                        self.fix.climb = default(10, CLIMB_SET)
                        self.fix.epd = default(11)
                        self.fix.eps = default(12, SPEEDERR_SET)
                        self.fix.epc = default(13, CLIMBERR_SET)
                        if len(fields) > 14:
                            self.fix.mode = default(14, MODE_SET, int)
                        else:
                            if self.valid & ALTITUDE_SET:
                                self.fix.mode = MODE_2D
                            else:
                                self.fix.mode = MODE_3D
                            self.valid |= MODE_SET
                elif cmd == 'P':
                    (self.fix.latitude, self.fix.longitude) = map(float, data.split())
                    self.valid |= LATLON_SET
                elif cmd == 'Q':
                    parts = data.split()
                    self.satellites_used = int(parts[0])
                    (self.pdop, self.hdop, self.vdop, self.tdop, self.gdop) = map(float, parts[1:])
                    self.valid |= HDOP_SET | VDOP_SET | PDOP_SET | TDOP_SET | GDOP_SET
                elif cmd == 'S':
                    self.status = int(data)
                    self.valid |= STATUS_SET
                elif cmd == 'T':
                    self.fix.track = float(data)
                    self.valid |= TRACK_SET
                elif cmd == 'U':
                    self.fix.climb = float(data)
                    self.valid |= CLIMB_SET
                elif cmd == 'V':
                    self.fix.speed = float(data)
                    self.valid |= SPEED_SET
                elif cmd == 'X':
                    self.online = float(data)
                    self.valid |= ONLINE_SET
                elif cmd == 'Y':
                    satellites = data.split(":")
                    prefix = satellites.pop(0).split()
                    d1 = int(prefix.pop())
                    newsats = []
                    for i in range(d1):
                        newsats.append(gps.satellite(*map(int, satellites[i].split())))
                    self.satellites = newsats
                    self.valid |= SATELLITE_SET

    def __json_unpack(self, buf):
        self.newstyle = True
        def asciify(d):
            "De-Unicodify everything so we can copy dicts into Python objects."
            t = {}
            for (k, v) in d.items():
                ka = k.encode("ascii")
                if type(v) == type(u"x"):
                    va = v.encode("ascii")
                elif type(v) == type({}):
                    va = asciify(v)
                elif type(v) == type([]):
                    va = map(asciify, v)
                else:
                    va = v
                t[ka] = va
            return t
        self.data = dictwrapper(**asciify(json.loads(buf, encoding="ascii")))
        # The rest is backwards compatibility for the old interface
        def default(k, dflt, vbit=0):
            if k not in self.data.keys():
                return dflt
            else:
                self.valid |= vbit
                return self.data[k]
        if self.data.get("class") == "VERSION":
            self.version = self.data
        elif self.data.get("class") == "DEVICE":
            self.valid = ONLINE_SET | DEVICE_SET
            self.path        = self.data["path"]
            self.activated   = default("activated", None)
            driver = default("driver", None, DEVICEID_SET) 
            subtype = default("subtype", None, DEVICEID_SET) 
            self.gps_id      = driver
            if subtype:
                self.gps_id += " " + subtype
            self.driver_mode = default("native", 0)
            self.baudrate    = default("bps", 0)
            self.serialmode  = default("serialmode", "8N1")
            self.cycle       = default("cycle",    NaN)
            self.mincycle    = default("mincycle", NaN)
        elif self.data.get("class") == "TPV":
            self.valid = ONLINE_SET
            self.fix.time = default("time", NaN, TIME_SET)
            self.fix.ept =       default("ept",   NaN, TIMERR_SET)
            self.fix.latitude =  default("lat",   NaN, LATLON_SET)
            self.fix.longitude = default("lon",   NaN)
            self.fix.altitude =  default("alt",   NaN, ALTITUDE_SET)
            self.fix.epx =       default("epx",   NaN, HERR_SET)
            self.fix.epy =       default("epy",   NaN, HERR_SET)
            self.fix.epv =       default("epv",   NaN, VERR_SET)
            self.fix.track =     default("track", NaN, TRACK_SET)
            self.fix.speed =     default("speed", NaN, SPEED_SET)
            self.fix.climb =     default("climb", NaN, CLIMB_SET)
            self.fix.epd =       default("epd",   NaN)
            self.fix.eps =       default("eps",   NaN, SPEEDERR_SET)
            self.fix.epc =       default("epc",   NaN, CLIMBERR_SET)
            self.fix.mode =      default("mode",  0,   MODE_SET)
        elif self.data.get("class") == "SKY":
            for attrp in "xyvhpg":
                setattr(self, attrp+"dop", default(attrp+"dop", NaN, DOP_SET))
            if "satellites" in self.data.keys():
                for sat in self.data['satellites']:
                    self.satellites.append(gps.satellite(PRN=sat['PRN'], elevation=sat['el'], azimuth=sat['az'], ss=sat['ss'], used=sat['used']))
            self.satellites_used = 0
            for sat in self.satellites:
                if sat.used:
                    self.satellites_used += 1
            self.valid = ONLINE_SET | SATELLITE_SET
        elif self.data.get("class") == "TIMING":
            self.data["c_recv"] = self.received
            self.data["c_decode"] = time.time()
            self.timings = self.data

    def waiting(self):
        "Return True if data is ready for the client."
        # WARNING! When we're testing here is whether there's data
        # left in sockfile.readline()'s read buffer before we look to
        # see if there's input waiting at the socket level. The Python
        # sockfile API doesn't expose a way to do this, so we have to
        # rely on knowing that the read buffer is the _rbuf member and
        # that it's a StringIO object. Without this test, we go back
        # to having flaky regression errors at the end of check files,
        # but with it the tests hang on some BSD-derived systems.  The
        # former outcome () but not the latteris OK for production
        # use, because dropping some data on device close isn't
        # actually a problem.
        broken = ('openbsd4')
        if sys.platform not in broken and len(self.sockfile._rbuf.getvalue()) > 0:
            return True
        (winput, woutput, wexceptions) = select.select((self.sock,), (), (), 0)
        return winput != []

    def poll(self):
        "Wait for and read data being streamed from gpsd."
        self.response = self.sockfile.readline()
        # This code can go away when we remove oldstyle protocol
        if self.response.startswith("H") and "=" not in self.response:
            while True:
                frag = self.sockfile.readline()
                self.response += frag
                if frag.startswith("."):
                    break
        # Can happen if daemon terminates while we're reading.
        if not self.response:
            return -1
        if self.verbose:
            sys.stderr.write("GPS-DATA %s\n" % repr(self.response))
        self.received = time.time()
        if self.raw_hook:
            self.raw_hook(self.response);
        # This code can go away when we remove oldstyle protocol
        if self.response.startswith("{"):
            self.__json_unpack(self.response)
        else:
            self.__oldstyle_unpack(self.response)
        return 0

    def next(self):
        "Get next object (new-style interface)."
        if self.poll() == -1:
            raise StopIteration
        # There are a few things we need to stash away for later use
        payload = dictwrapper(**self.data)
        if self.data["class"] == "VERSION":
            self.version = payload
        elif self.data["class"] == "DEVICE":
            if "driver" in self.data:
                if "driver" in data:
                    self.driver = self.data["driver"]
                if "subtype" in data:
                    self.subtype = self.data["subtype"]
                if self.driver:
                    self.gps_id = self.driver
                    if self.subtype:
                        self.gps_id += self.subtype
        elif self.data["class"] == "TIMING":
            payload.c_recv = self.received
            payload.c_decode = time.time()
        return payload

    def send(self, commands):
        "Ship commands to the daemon."
        if not commands.endswith("\n"):
            commands += "\n"
        self.sock.send(commands)

    def stream(self, flags=0):
        "Ask gpsd to stream reports at your client."
        if (flags & (WATCH_NEWSTYLE|WATCH_OLDSTYLE)) == 0:
            # If we're looking at a daemon that speakds JSON, this
            # should have been set when we saw the initial VERSION
            # response.  Note, however, that this requires at
            # least one poll() before stream() is called
            if self.newstyle:
                flags |= WATCH_NEWSTYLE
            else:
                flags |= WATCH_OLDSTYLE
        if flags & WATCH_NEWSTYLE:
            if flags & WATCH_ENABLE:
                arg = '?WATCH={"enable":true'
                if self.raw_hook or (flags & WATCH_NMEA):
                    arg += ',"nmea":true'
            elif flags & WATCH_DISABLE:
                arg = '?WATCH={"enable":false'
                if self.raw_hook or (flags & WATCH_NMEA):
                    arg += ',"nmea":false'
            return self.send(arg + "}")
        elif flags & WATCH_OLDSTYLE:
            if flags & WATCH_ENABLE:
                arg = 'w+'
                if self.raw_hook or (flags & WATCH_NMEA):
                    arg += 'r+'
                    return self.send(arg)
            elif flags & WATCH_DISABLE:
                arg = "w-"
                if self.raw_hook or (flags & WATCH_NMEA):
                    arg += 'r-'
                    return self.send(arg)

# some multipliers for interpreting GPS output
METERS_TO_FEET  = 3.2808399
METERS_TO_MILES = 0.00062137119
KNOTS_TO_MPH    = 1.1507794

# EarthDistance code swiped from Kismet and corrected
# (As yet, this stuff is not in the libgps C library.)

def Deg2Rad(x):
    "Degrees to radians."
    return x * (math.pi/180)

def Rad2Deg(x):
    "Radians to degress."
    return x * (180/math.pi)

def CalcRad(lat):
    "Radius of curvature in meters at specified latitude."
    a = 6378.137
    e2 = 0.081082 * 0.081082
    # the radius of curvature of an ellipsoidal Earth in the plane of a
    # meridian of latitude is given by
    #
    # R' = a * (1 - e^2) / (1 - e^2 * (sin(lat))^2)^(3/2)
    #
    # where a is the equatorial radius,
    # b is the polar radius, and
    # e is the eccentricity of the ellipsoid = sqrt(1 - b^2/a^2)
    #
    # a = 6378 km (3963 mi) Equatorial radius (surface to center distance)
    # b = 6356.752 km (3950 mi) Polar radius (surface to center distance)
    # e = 0.081082 Eccentricity
    sc = math.sin(Deg2Rad(lat))
    x = a * (1.0 - e2)
    z = 1.0 - e2 * sc * sc
    y = pow(z, 1.5)
    r = x / y

    r = r * 1000.0      # Convert to meters
    return r

def EarthDistance((lat1, lon1), (lat2, lon2)):
    "Distance in meters between two points specified in degrees."
    x1 = CalcRad(lat1) * math.cos(Deg2Rad(lon1)) * math.sin(Deg2Rad(90-lat1))
    x2 = CalcRad(lat2) * math.cos(Deg2Rad(lon2)) * math.sin(Deg2Rad(90-lat2))
    y1 = CalcRad(lat1) * math.sin(Deg2Rad(lon1)) * math.sin(Deg2Rad(90-lat1))
    y2 = CalcRad(lat2) * math.sin(Deg2Rad(lon2)) * math.sin(Deg2Rad(90-lat2))
    z1 = CalcRad(lat1) * math.cos(Deg2Rad(90-lat1))
    z2 = CalcRad(lat2) * math.cos(Deg2Rad(90-lat2))
    a = (x1*x2 + y1*y2 + z1*z2)/pow(CalcRad((lat1+lat2)/2), 2)
    # a should be in [1, -1] but can sometimes fall outside it by
    # a very small amount due to rounding errors in the preceding
    # calculations (this is prone to happen when the argument points
    # are very close together).  Thus we constrain it here.
    if abs(a) > 1: a = 1
    elif a < -1: a = -1
    return CalcRad((lat1+lat2) / 2) * math.acos(a)

def MeterOffset((lat1, lon1), (lat2, lon2)):
    "Return offset in meters of second arg from first."
    dx = EarthDistance((lat1, lon1), (lat1, lon2))
    dy = EarthDistance((lat1, lon1), (lat2, lon1))
    if lat1 < lat2: dy *= -1
    if lon1 < lon2: dx *= -1
    return (dx, dy)

def isotime(s):
    "Convert timestamps in ISO8661 format to and from Unix time."
    if type(s) == type(1):
        return time.strftime("%Y-%m-%dT%H:%M:%S", time.gmtime(s))
    elif type(s) == type(1.0):
        date = int(s)
        msec = s - date
        date = time.strftime("%Y-%m-%dT%H:%M:%S", time.gmtime(s))
        return date + "." + `msec`[2:]
    elif type(s) == type(""):
        if s[-1] == "Z":
            s = s[:-1]
        if "." in s:
            (date, msec) = s.split(".")
        else:
            date = s
            msec = "0"
        # Note: no leap-second correction! 
        return calendar.timegm(time.strptime(date, "%Y-%m-%dT%H:%M:%S")) + float("0." + msec)
    else:
        raise TypeError

if __name__ == '__main__':
    import readline, getopt
    (options, arguments) = getopt.getopt(sys.argv[1:], "w")
    streaming = False
    for (switch, val) in options:
        if (switch == '-w'):
            streaming = True    
    if len(arguments) > 2:
        print 'Usage: gps.py [host [port]]'
        sys.exit(1)

    if streaming:
        session = gps(*arguments)
        session.set_raw_hook(lambda s: sys.stdout.write(s.strip() + "\n"))
        session.stream(WATCH_ENABLE|WATCH_NEWSTYLE)
        for report in session:
            print report
    else:
        print "This is the exerciser for the Python gps interface."
        session = gps(*arguments)
        session.set_raw_hook(lambda s: sys.stdout.write(s.strip() + "\n"))
        try:
            while True:
                session.query(raw_input("> "))
                print session
        except EOFError:
            print "Goodbye!"
        del session

# gps.py ends here
