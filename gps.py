#!/usr/bin/env python
#
# gps.py -- Python interface to GPSD.
#
import time, socket

STATUS_NO_FIX = 0
STATUS_FIX = 1
STATUS_DGPS_FIX = 2
MODE_NO_FIX = 1
MODE_2D = 2
MODE_3D = 3
MAXCHANNELS = 12
SIGNAL_STRENGTH_UNKNOWN = -1

class gpsdata:
    "Position, track, velocity and status information returned by a GPS."

    class timestamp:
	def __init__(self, now):
	    self.last_refresh = now
	    self.changed = False
	def refresh(self):
	    self.last_refresh = time.time()
	def seen(self):
	    return self.refreshes
	def __repr__(self):
	    return "{lr=%d, changed=%s}" % (self.last_refresh, self.changed)
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
	now = time.time()

	self.online = False			# True if GPS on, False if not
	self.online_stamp = gps.timestamp(now)

	self.utc = ""

	self.latitude = self.longitude = 0.0
	self.latlon_stamp = gps.timestamp(now)

	self.altitude = 0.0			# Meters
	self.altitude_stamp = gps.timestamp(now)

	self.speed = 0.0			# Knots
	self.speed_stamp = gps.timestamp(now)

	self.track = 0.0			# Degrees from true north
	self.track_stamp = gps.timestamp(now)

	self.status = STATUS_NO_FIX
	self.status_stamp = gps.timestamp(now)

	self.mode = MODE_NO_FIX
	self.mode_stamp = gps.timestamp(now)

	self.satellites_used = []		# Satellites used in last fix
	self.pdop = self.hdop = self.vdop = 0.0
	self.fix_quality_stamp = gps.timestamp(now)

	self.satellites = []			# satellite objects in view
	self.satellite_stamp = gps.timestamp(now)
        self.await = self.parts = 0

        __setattr__ = setattr

    def setattr(self, name, value):
        # Make sure the change stamp for each field is kept up to date
        group = {'online':'online_stamp',
	         #'latitude':'latlon_stamp'),
                 #'longitude':'latlon_stamp',
                 'altitude':'altitude_stamp',
                 'speed':'speed_stamp',
                 'track':'track_stamp',
                 'status':'status_stamp',
                 'mode':'mode_stamp',
                 #'pdop':'fix_quality_stamp',
                 #'hdop':'fix_quality_stamp',
                 #'vdop':'fix_quality_stamp',
                 'satellites':'satellite_stamp',
                 }
        if field in group:
            stamp = getattr(self, group[field])
            stamp.changed = (getattr(self, field) != value)
            stamp.refresh()
        self.__dict__[name] = value

    def __repr__(self):
	st = ""
	st += "Lat/lon:  %f %f " % (self.latitude, self.longitude)
	st += repr(self.latlon_stamp) + "\n"
	st += "Altitude: %f " % (self.altitude)
	st += repr(self.altitude_stamp) + "\n"
	st += "Speed:    %f " % (self.speed)
	st += repr(self.speed_stamp) + "\n"
	st += "Track:    %f " % (self.track)
	st += repr(self.track_stamp) + "\n"
	st += "Status:   STATUS_%s" % ("NO_FIX", "FIX","DGPS_FIX")[self.status]
	st += " " +repr(self.status_stamp) + "\n"
	st += "Mode:     MODE_" + ("ZERO", "NO_FIX", "2D","3D")[self.mode]
	st += " " + repr(self.mode_stamp) + "\n"
	st += "Quality:  %d p=%2.2f h=%2.2f v=%2.2f " % \
              (len(self.satellites_used),
               self.pdop, self.hdop, self.vdop)
	st += repr(self.fix_quality_stamp) + "\n"
	st += "Y: %s satellites in view:\n" % len(self.satellites)
	for sat in self.satellites:
	  st += "    " + repr(sat) + "\n"
	st += "    " + repr(self.satellite_stamp) + "\n"
	return st

class gps(gpsdata):
    "Client interface to a running gpsd instance."
    def __init__(self, host="localhost", port="2947"):
	gpsdata.__init__(self)
	self.sock = None	# in case we blow up in connect
	self.connect(host, port)

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
        if not port: port = SMTP_PORT
        #if self.debuglevel > 0: print 'connect:', (host, port)
        msg = "getaddrinfo returns an empty list"
        self.sock = None
        for res in socket.getaddrinfo(host, port, 0, socket.SOCK_STREAM):
            af, socktype, proto, canonname, sa = res
            try:
                self.sock = socket.socket(af, socktype, proto)
                #if self.debuglevel > 0: print 'connect:', (host, port)
                self.sock.connect(sa)
            except socket.error, msg:
                if self.debuglevel > 0: print 'connect fail:', (host, port)
                if self.sock:
                    self.sock.close()
                self.sock = None
                continue
            break
        if not self.sock:
            raise socket.error, msg

    def set_raw_hook(self, hook):
        self.raw_hook = hook

    def __del__(self):
	if self.sock:
	    self.sock.close()
    close = __del__

    def __unpack(self, buf):
	# unpack a daemon response into the instance members
        print "Data: %s" % `buf`
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
	      d1 = float(data)
	      self.altitude_stamp.changed = (self.altitude != d1)
	      self.altitude = d1
	      self.altitude_stamp.refresh()
	    elif cmd in ('D', 'd'):
	      self.utc = data
	    elif cmd in ('M', 'm'):
	      i1 = int(data)
	      self.mode_stamp.changed = (self.mode != i1)
	      self.mode = i1
	      self.mode_stamp.refresh()
	    elif cmd in ('P', 'p'):
	      (f1, f2) = map(float, data.split())
	      self.latlon_stamp.changed = (self.latitude != f1 or self.longitude != f2)
	      self.latitude = f1
	      self.longitude = f2
	      self.latlon_stamp.refresh()
	    elif cmd in ('Q', 'q'):
	      parts = data.split()
	      i1 = int(parts[0])
	      (f1, f2, f3) = map(float, parts[1:])
	      self.fix_quality_stamp.changed = (self.pdop != f1 or self.hdop != f2 or self.vdop != f3)
	      self.satellites_used = i1
	      self.pdop = f1
	      self.hdop = f2
	      self.vdop = f3
	      self.fix_quality_stamp.refresh()
	    elif cmd in ('S', 's'):
	      i1 = int(data)
	      self.status_stamp.changed = (self.status != i1)
	      self.status = i1
	      self.status_stamp.refresh()
	    elif cmd in ('T', 't'):
	      d1 = float(data)
	      self.track_stamp.changed = (self.track != d1)
	      self.track = d1
	      self.track_stamp.refresh()
	    elif cmd in ('V', 'v'):
	      d1 = float(data)
	      self.speed_stamp.changed = (self.speed != d1)
	      self.speed = d1
	      self.speed_stamp.refresh()
	    elif cmd in ('X', 'x'):
	      b1 = data[0] == '1'
	      self.online_stamp.changed = (b1 != self.online)
	      self.online = b1
	      self.online_stamp.refresh()
	    elif cmd in ('Y', 'y'):
	      satellites = data.split(":")
	      d1 = int(satellites.pop(0))
	      newsats = []
	      for i in range(d1):
		newsats.append(gps.satellite(*map(int, satellites[i].split())))
	      self.satellite_stamp.changed = (self.satellites) != newsats
	      self.satellites = newsats
	      self.satellite_stamp.refresh() 
	if self.raw_hook:
	    self.raw_hook(buf);
	return self.online_stamp.changed \
	    or self.latlon_stamp.changed \
	    or self.altitude_stamp.changed \
	    or self.speed_stamp.changed \
	    or self.track_stamp.changed \
	    or self.fix_quality_stamp.changed \
	    or self.fix_quality_stamp.changed \
	    or self.status_stamp.changed \
	    or self.mode_stamp.changed \
	    or self.satellite_stamp.changed 

    def poll(self):
	"Wait for and read data being streamed from gpsd."
	return self.__unpack(self.sock.recv(1024))

    def query(self, commands):
	"Send a command, get back a response."
	self.sock.send(commands)
	return self.poll()

if __name__ == '__main__':
    import sys,readline
    print "This is the exerciser for the Python gps interface."
    session = gps()
    session.set_raw_hook(lambda s: sys.stdout.write(s + "\n"))
    while True:
	commands = raw_input("> ")
	session.query(commands)
	print session
    session.close()



