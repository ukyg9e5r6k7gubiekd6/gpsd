#!/usr/bin/env python
#
# gps.py -- Python interface to GPSD.
#
import time, socket, sys
from math import *

ONLINE_SET =	0x000001
TIME_SET =	0x000002
TIMERR_SET =	0x000004
LATLON_SET =	0x000008
ALTITUDE_SET =	0x000010
SPEED_SET =	0x000020
TRACK_SET =	0x000040
CLIMB_SET =	0x000080
STATUS_SET =	0x000100
MODE_SET =	0x000200
HDOP_SET =  	0x000400
VDOP_SET =  	0x000800
PDOP_SET =  	0x001000
HERR_SET =	0x002000
VERR_SET =	0x004000
PERR_SET =	0x008000
SATELLITE_SET =	0x010000
SPEEDERR_SET =	0x020000
TRACKERR_SET =	0x040000
CLIMBERR_SET =	0x080000

STATUS_NO_FIX = 0
STATUS_FIX = 1
STATUS_DGPS_FIX = 2
MODE_NO_FIX = 1
MODE_2D = 2
MODE_3D = 3
MAXCHANNELS = 12
SIGNAL_STRENGTH_UNKNOWN = -1
ALTITUDE_NOT_VALID = -999
TRACK_NOT_VALID = -1

NMEA_MAX = 82

GPSD_PORT = 2947

class gpstimings:
    def __init__(self):
        self.tag = ""
        self.length = 0
        self.gps_time = 0.0
        self.d_recv_time = 0
        self.d_decode_time = 0
        self.emit_time = 0
        self.poll_time = 0
        self.c_recv_time = 0
        self.c_decode_time = 0
    def collect(self, tag, length, gps_time, xmit_time, recv_time, decode_time, poll_time, emit_time):
        self.tag = tag
        self.length = int(length)
        self.gps_time = float(gps_time)
        self.d_xmit_time = float(xmit_time)
        self.d_recv_time = float(recv_time)
        self.d_decode_time = float(decode_time)
        self.poll_time = float(poll_time)
        self.emit_time = float(emit_time)

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
	    return "PRN: %3d  E: %3d  Az: %3d  Ss: %d Used: %s" % (self.PRN,self.elevation,self.azimuth,self.ss,"ny"[self.used])

    def __init__(self):
	# Initialize all data members 
	self.online = 0			# NZ if GPS on, zero if not

	self.mode = MODE_NO_FIX
	self.latitude = self.longitude = 0.0
        self.eph = 0.0
	self.altitude = ALTITUDE_NOT_VALID	# Meters
        self.epv = 0.0
	self.track = TRACK_NOT_VALID		# Degrees from true north
	self.speed = 0.0			# Knots
	self.climb = 0.0			# Meters per second

        self.valid = 0
	self.status = STATUS_NO_FIX
        self.utc = ""

	self.satellites_used = 0		# Satellites used in last fix
	self.pdop = self.hdop = self.vdop = 0.0

	self.epe = 0.0

	self.satellites = []			# satellite objects in view
        self.await = self.parts = 0

        self.gps_id = None
        self.driver_mode = 0
        self.profiling = False
        self.timings = gpstimings()
        self.baudrate = 0
        self.stopbits = 0
        self.cycle = 0

    def __repr__(self):
	st = ""
	st += "Lat/lon:  %f %f\n" % (self.latitude, self.longitude)
        if self.altitude == ALTITUDE_NOT_VALID:
            st += "Altitude: ALTITUDE_NOT_VALID\n"
        else:
            st += "Altitude: %f\n" % (self.altitude)
	st += "Speed:    %f\n" % (self.speed)
        if self.track == TRACK_NOT_VALID:
            st += "Track:    TRACK_NOT_VALID\n"
        else:
            st += "Track:    %f\n" % (self.track)
	st += "Status:   STATUS_%s\n" %("NO_FIX","FIX","DGPS_FIX")[self.status]
	st += "Mode:     MODE_"+("ZERO", "NO_FIX", "2D","3D")[self.mode]+"\n"
	st += "Quality:  %d p=%2.2f h=%2.2f v=%2.2f\n" % \
              (self.satellites_used, self.pdop, self.hdop, self.vdop)
	st += "Y: %s satellites in view:\n" % len(self.satellites)
	for sat in self.satellites:
	  st += "    " + repr(sat) + "\n"
	return st

class gps(gpsdata):
    "Client interface to a running gpsd instance."
    def __init__(self, host="localhost", port="2947", verbose=0):
	gpsdata.__init__(self)
	self.sock = None	# in case we blow up in connect
	self.sockfile = None
	self.connect(host, port)
        self.verbose = verbose
	self.raw_hook = None

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
                if self.sock:
                    self.sock.close()
                self.sock = None
                self.sockfile = None
                continue
            break
        if not self.sock:
            raise socket.error, msg

    def set_raw_hook(self, hook):
        self.raw_hook = hook

    def __del__(self):
	if self.sock:
	    self.sock.close()
        self.sock = None
        self.sockfile = None

    def __unpack(self, buf):
	# unpack a daemon response into the instance members
        self.gps_time = 0.0
	fields = buf.strip().split(",")
	if fields[0] == "GPSD":
	  for field in fields[1:]:
	    if not field or field[1] != '=':
	      continue
	    cmd = field[0]
	    data = field[2:]
	    if data[0] == "?":
		continue
	    if cmd in ('A', 'a'):
	      self.altitude = float(data)
              self.valid |= ALTITUDE_SET
	    elif cmd in ('B', 'b'):
              (f1, f2, f3, f4) = data.split()
              self.baudrate = int(f1)
              self.stopbits = int(f4)
	    elif cmd in ('C', 'c'):
	      self.cycle = int(data)
	    elif cmd in ('D', 'd'):
	      self.utc = data
              self.gps_time = isotime(self.utc)
              self.valid |= TIME_SET
	    elif cmd in ('E', 'e'):
	      parts = data.split()
	      (self.epe, self.eph, self.epv) = map(float, parts)
              self.valid |= HERR_SET | VERR_SET | PERR_SET
	    elif cmd in ('I', 'i'):
	      self.gps_id = data
	    elif cmd in ('M', 'm'):
	      self.mode = int(data)
              self.valid |= MODE_SET
	    elif cmd in ('N', 'n'):
	      self.driver_mode = int(data)
	    elif cmd in ('O', 'o'):
                fields = data.split()
                if fields[0] == '?':
                    self.mode = MODE_NO_FIX
                else:
                    self.gps_time = float(fields[0])
                    self.ept = float(fields[1])
                    self.latitude = float(fields[2])
                    self.longitude = float(fields[3])
                    def default(i, d):
                        if fields[i] == '?':
                            return d
                        else:
                            return float(fields[i])
                    self.altitude = default(4, ALTITUDE_NOT_VALID)
                    if self.altitude == ALTITUDE_NOT_VALID:
                        self.mode = MODE_2D
                    else:
                        self.mode = MODE_3D
                    self.eph = default(5, 0.0)
                    self.epv = default(6, 0.0)
                    self.track = default(7, TRACK_NOT_VALID)
                    self.speed = default(8, 0.0)
                    self.climb = default(9, 0.0)
                    self.epd = default(10, 0.0)
                    self.eps = default(11, 0.0)
                    self.epc = default(12, 0.0)
                    self.valid |= TIME_SET|TIMERR_SET|LATLON_SET|MODE_SET
                    if self.mode == MODE_3D:
                        self.valid |= ALTITUDE_SET | CLIMB_SET
                    if self.eph:
                        self.valid |= HERR_SET
                    if self.epv:
                        self.valid |= VERR_SET
                    if self.track != TRACK_NOT_VALID:
                        self.valid |= TRACK_SET | SPEED_SET
                    if self.eps:
                        self.valid |= SPEEDERR_SET
                    if self.epc:
                        self.valid |= CLIMBERR_SET
	    elif cmd in ('P', 'p'):
	      (self.latitude, self.longitude) = map(float, data.split())
              self.valid |= LATLON_SET
	    elif cmd in ('Q', 'q'):
	      parts = data.split()
	      self.satellites_used = int(parts[0])
	      (self.pdop, self.hdop, self.vdop) = map(float, parts[1:])
              self.valid |= HDOP_SET | VDOP_SET | PDOP_SET
	    elif cmd in ('S', 's'):
	      self.status = int(data)
              self.valid |= STATUS_SET
	    elif cmd in ('T', 't'):
	      self.track = float(data)
              self.valid |= TRACK_SET
	    elif cmd in ('U', 'u'):
	      self.climb = float(data)
              self.valid |= CLIMB_SET
	    elif cmd in ('V', 'v'):
	      self.speed = float(data)
              self.valid |= SPEED_SET
	    elif cmd in ('X', 'x'):
	      self.online = (data[0] == '1')
              self.valid |= ONLINE_SET
	    elif cmd in ('Y', 'y'):
	      satellites = data.split(":")
	      d1 = int(satellites.pop(0))
	      newsats = []
	      for i in range(d1):
		newsats.append(gps.satellite(*map(int, satellites[i].split())))
	      self.satellites = newsats
              self.valid |= SATELLITE_SET
	    elif cmd in ('Z', 'z'):
              self.profiling = (data[0] == '1')
            elif cmd == '$':
                self.timings.collect(*data.split())
	if self.raw_hook:
	    self.raw_hook(buf);

    def poll(self):
	"Wait for and read data being streamed from gpsd."
        data = self.sockfile.readline()
        if not data:
            return -1
        if self.verbose:
            sys.stderr.write("GPS DATA %s\n" % repr(data))
        self.timings.c_recv_time = time.time()
	self.__unpack(data)
        if self.gps_time:
            basetime = self.gps_time - tzoffset()
            self.timings.c_decode_time = time.time() - basetime
            self.timings.c_recv_time -= basetime
        return 0

    def query(self, commands):
	"Send a command, get back a response."
 	self.sockfile.write(commands)
 	self.sockfile.flush()
	return self.poll()

# some multipliers for interpreting GPS output
METERS_TO_FEET	= 3.2808399
METERS_TO_MILES	= 0.00062137119
KNOTS_TO_MPH	= 1.1507794

# EarthDistance code swiped from Kismet and corrected
# (As yet, this stuff is not in the libgps C library.)

def Deg2Rad(x):
    "Degrees to radians."
    return x * (pi/180)

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
    sc = sin(Deg2Rad(lat))
    x = a * (1.0 - e2)
    z = 1.0 - e2 * sc * sc
    y = pow(z, 1.5)
    r = x / y

    r = r * 1000.0	# Convert to meters
    return r

def EarthDistance((lat1, lon1), (lat2, lon2)):
    "Distance in meters between two points specified in degrees."
    x1 = CalcRad(lat1) * cos(Deg2Rad(lon1)) * sin(Deg2Rad(90-lat1))
    x2 = CalcRad(lat2) * cos(Deg2Rad(lon2)) * sin(Deg2Rad(90-lat2))
    y1 = CalcRad(lat1) * sin(Deg2Rad(lon1)) * sin(Deg2Rad(90-lat1))
    y2 = CalcRad(lat2) * sin(Deg2Rad(lon2)) * sin(Deg2Rad(90-lat2))
    z1 = CalcRad(lat1) * cos(Deg2Rad(90-lat1))
    z2 = CalcRad(lat2) * cos(Deg2Rad(90-lat2))
    a = (x1*x2 + y1*y2 + z1*z2)/pow(CalcRad((lat1+lat2)/2),2)
    # a should be in [1, -1] but can sometimes fall outside it by
    # a very small amount due to rounding errors in the preceding
    # calculations (this is prone to happen when the argument points
    # are very close together).  Thus we constrain it here.
    if abs(a) > 1: a = 1
    elif a < -1: a = -1
    return CalcRad((lat1+lat2) / 2) * acos(a)

def MeterOffset((lat1, lon1), (lat2, lon2)):
    "Return offset in meters of second arg from first."
    dx = EarthDistance((lat1, lon1), (lat1, lon2))
    dy = EarthDistance((lat1, lon1), (lat2, lon1))
    if lat1 < lat2: dy *= -1
    if lon1 < lon2: dx *= -1
    return (dx, dy)

def tzoffset():
    if time.daylight and time.localtime().tm_isdst:
        return time.altzone
    else:
        return time.timezone

def isotime(s):
    "Convert timestamps in ISO8661 format to and from Unix local time."
    if type(s) == type(1):
        return time.strftime(time.localtime(s), "%Y-%m-%dT%H:%M:%S")
    elif type(s) == type(1.0):
        date = int(s)
        msec = s - date
        date = time.strftime("%Y-%m-%dT%H:%M:%S", time.localtime(s))
        return date + "." + `msec`[2:]
    elif type(s) == type(""):
        gmt = s[-1] == "Z"
        if gmt:
            s = s[:-1]
        if "." in s:
            (date, msec) = s.split(".")
        else:
            date = s
            msec = "0"
        unpacked = time.strptime(date, "%Y-%m-%dT%H:%M:%S")
        seconds = time.mktime(unpacked)
        if gmt:
            seconds -= tzoffset()
        return seconds + float("0." + msec)
    else:
        raise TypeError

if __name__ == '__main__':
    import sys,readline
    print "This is the exerciser for the Python gps interface."
    session = gps()
    session.set_raw_hook(lambda s: sys.stdout.write(s + "\n"))
    try:
        while True:
            commands = raw_input("> ")
            session.query(commands)
            print session
    except EOFError:
        print "Goodbye!"
    del session

# gps.py ends here
