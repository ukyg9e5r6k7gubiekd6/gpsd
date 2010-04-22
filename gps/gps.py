#!/usr/bin/env python
# -*- coding: utf-8 -*-
#
# This file is Copyright (c) 2010 by the GPSD project
# BSD terms apply: see the file COPYING in the distribution root for details.
#
# gps.py -- Python interface to GPSD.
#
# This interface has a lot of historical cruft in it related to old
# protocol, and was modeled on the C interface. It won't be thrown
# away, but it's likely to be deprecated in favor of something more
# Pythonic.
#
import time, socket, sys, select

if sys.hexversion >= 0x2060000:
    import json			# For Python 2.6
else:
    import simplejson as json	# For Python 2.4 and 2.5

GPSD_PORT="2947"

class gpscommon:
    "Isolate socket handling and buffering from the protcol interpretation."
    def __init__(self, host="127.0.0.1", port=GPSD_PORT, verbose=0):
        self.sock = None        # in case we blow up in connect
        self.linebuffer = ""
        self.verbose = verbose
        self.connect(host, port)

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
        #if self.verbose > 0:
        #    print 'connect:', (host, port)
        msg = "getaddrinfo returns an empty list"
        self.sock = None
        for res in socket.getaddrinfo(host, port, 0, socket.SOCK_STREAM):
            af, socktype, proto, canonname, sa = res
            try:
                self.sock = socket.socket(af, socktype, proto)
                #if self.debuglevel > 0: print 'connect:', (host, port)
                self.sock.connect(sa)
            except socket.error, msg:
                #if self.debuglevel > 0: print 'connect fail:', (host, port)
                self.close()
                continue
            break
        if not self.sock:
            raise socket.error, msg

    def close(self):
        if self.sock:
            self.sock.close()
        self.sock = None

    def __del__(self):
        self.close()

    def waiting(self):
        "Return True if data is ready for the client."
        if self.linebuffer:
            return True
        (winput, woutput, wexceptions) = select.select((self.sock,), (), (), 0)
        return winput != []

    def read(self):
        "Wait for and read data being streamed from the daemon."
        if self.verbose > 1:
            sys.stderr.write("poll: reading from daemon...\n")
        eol = self.linebuffer.find('\n')
        if eol == -1:
            frag = self.sock.recv(4096)
            self.linebuffer += frag
            if self.verbose > 1:
                sys.stderr.write("poll: read complete.\n")
            if not self.linebuffer:
                if self.verbose > 1:
                    sys.stderr.write("poll: returning -1.\n")
                # Read failed
                return -1
            eol = self.linebuffer.find('\n')
            if eol == -1:
                if self.verbose > 1:
                    sys.stderr.write("poll: returning 0.\n")
                # Read succeeded, but only got a fragment
                return 0
        else:
            if self.verbose > 1:
                sys.stderr.write("poll: fetching from buffer.\n")

        # We got a line
        eol += 1
        self.response = self.linebuffer[:eol]
        self.linebuffer = self.linebuffer[eol:]

        # Can happen if daemon terminates while we're reading.
        if not self.response:
            return -1
        if self.verbose:
            sys.stderr.write("poll: data is %s\n" % repr(self.response))
        self.received = time.time()
        # We got a \n-terminated line
        return len(self.response)

    def send(self, commands):
        "Ship commands to the daemon."
        if not commands.endswith("\n"):
            commands += "\n"
        self.sock.send(commands)

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
    def __contains__(self, key):
        return key in self.__dict__
    def __str__(self):
        return "<dictwrapper: " + str(self.__dict__) + ">"
    __repr__ = __str__


#
# Stuff below this line is specific to the old interface 
#

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
ATTITUDE_SET   	= 0x00004000
POLICY_SET     	= 0x00008000
SATELLITE_SET  	= 0x00010000
RAW_SET        	= 0x00020000
USED_SET       	= 0x00040000
SPEEDERR_SET   	= 0x00080000
TRACKERR_SET   	= 0x00100000
CLIMBERR_SET   	= 0x00200000
DEVICE_SET     	= 0x00400000
DEVICELIST_SET 	= 0x00800000
DEVICEID_SET   	= 0x01000000
ERROR_SET      	= 0x02000000
RTCM2_SET      	= 0x04000000
RTCM3_SET      	= 0x08000000
AIS_SET        	= 0x10000000
PACKET_SET     	= 0x20000000
AUXDATA_SET    	= 0x80000000
UNION_SET      	= (RTCM2_SET|RTCM3_SET|AIS_SET|VERSION_SET|DEVICELIST_SET|ERROR_SET)

STATUS_NO_FIX = 0
STATUS_FIX = 1
STATUS_DGPS_FIX = 2
MODE_NO_FIX = 1
MODE_2D = 2
MODE_3D = 3
MAXCHANNELS = 20
SIGNAL_STRENGTH_UNKNOWN = NaN

WATCH_DISABLE	= 0x0000
WATCH_ENABLE	= 0x0001
WATCH_JSON	= 0x0002
WATCH_NMEA	= 0x0004
WATCH_RARE	= 0x0008
WATCH_RAW	= 0x0010
WATCH_SCALED	= 0x0020
WATCH_NEWSTYLE	= 0x0040
WATCH_OLDSTYLE	= 0x0080
WATCH_DEVICE	= 0x0100

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

class gps(gpsdata, gpscommon):
    "Client interface to a running gpsd instance."
    def __init__(self, host="127.0.0.1", port=GPSD_PORT, verbose=0, mode=0):
        gpscommon.__init__(self, host, port, verbose)
        gpsdata.__init__(self)
        self.raw_hook = None
        self.newstyle = False
        if mode:
            self.stream(mode)

    def __iter__(self):
        return self

    def set_raw_hook(self, hook):
        self.raw_hook = hook

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
                if cmd == 'F':
                    self.device = data
                elif cmd == 'I':
                    self.gps_id = data
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
        self.data = dictwrapper(**asciify(json.loads(buf.strip(), encoding="ascii")))
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
                self.satellites = [] 
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

    def poll(self):
        "Read and interpret data from the daemon."
        status = gpscommon.read(self)
        if status <= 0:
            return status
        if self.raw_hook:
            self.raw_hook(self.response);
        if self.response.startswith("{"):
            self.__json_unpack(self.response)
        else:
            self.__oldstyle_unpack(self.response)
        self.valid |= PACKET_SET
        return 0

    def next(self):
        if self.poll() == -1:
            raise StopIteration
        if hasattr(self, "data"):
            return self.data
        else:
            return self.response

    def stream(self, flags=0, outfile=None):
        "Ask gpsd to stream reports at your client."
        if (flags & (WATCH_JSON|WATCH_OLDSTYLE|WATCH_NMEA|WATCH_RAW)) == 0:
            # If we're looking at a daemon that speaks JSON, this
            # should have been set when we saw the initial VERSION
            # response.  Note, however, that this requires at
            # least one poll() before stream() is called
            if self.newstyle or flags & WATCH_NEWSTYLE:
                flags |= WATCH_JSON
            else:
                flags |= WATCH_OLDSTYLE
        if flags & WATCH_OLDSTYLE:
            if flags & WATCH_DISABLE:
                arg = "w-"
                if flags & WATCH_NMEA:
                    arg += 'r-'
                    return self.send(arg)
            else: # flags & WATCH_ENABLE:
                arg = 'w+'
                if self.raw_hook or (flags & WATCH_NMEA):
                    arg += 'r+'
                    return self.send(arg)
        else: # flags & WATCH_NEWSTYLE:
            if flags & WATCH_DISABLE:
                arg = '?WATCH={"enable":false'
                if flags & WATCH_JSON:
                    arg += ',"json":false'
                if flags & WATCH_NMEA:
                    arg += ',"nmea":false'
                if flags & WATCH_RARE:
                    arg += ',"raw":1'
                if flags & WATCH_RAW:
                    arg += ',"raw":2'
                if flags & WATCH_SCALED:
                    arg += ',"scaled":false'
            else: # flags & WATCH_ENABLE:
                arg = '?WATCH={"enable":true'
                if flags & WATCH_JSON:
                    arg += ',"json":true'
                if flags & WATCH_NMEA:
                    arg += ',"nmea":true'
                if flags & WATCH_RAW:
                    arg += ',"raw":1'
                if flags & WATCH_RARE:
                    arg += ',"raw":0'
                if flags & WATCH_SCALED:
                    arg += ',"scaled":true'
                if flags & WATCH_DEVICE:
                    arg += ',"device":"%s"' % outfile
            return self.send(arg + "}")

if __name__ == '__main__':
    import readline, getopt
    (options, arguments) = getopt.getopt(sys.argv[1:], "v")
    streaming = False
    verbose = False
    for (switch, val) in options:
        if switch == '-v':
            verbose = True
    if len(arguments) > 2:
        print 'Usage: gps.py [-v] [host [port]]'
        sys.exit(1)

    opts = { "verbose" : verbose }
    if len(arguments) > 0:
        opts["host"] = arguments[0]
    if len(arguments) > 1:
        opts["port"] = arguments[1]

    session = gps(**opts)
    session.set_raw_hook(lambda s: sys.stdout.write(s.strip() + "\n"))
    session.stream(WATCH_ENABLE|WATCH_NEWSTYLE)
    for report in session:
        print report

# gps.py ends here
