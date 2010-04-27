# This file is Copyright (c) 2010 by the GPSD project
# BSD terms apply: see the file COPYING in the distribution root for details.
"""
A GPS simulator.

This is proof-of-concept code, not production ready; some functions are stubs.
"""
import sys, math, random, exceptions
import gps, gpslib

# First, the mathematics.  We simulate a moving viewpoint on the Earth
# and a satellite with specified orbital elements in the sky.

class ksv:
    "Kinematic state vector."
    def __init__(self, time=0, lat=0, lon=0, alt=0, course=0,
                 speed=0, climb=0, h_acc=0, v_acc=0):
        self.time = time	# Seconds from epoch
        self.lat = lat		# Decimal degrees
        self.lon = lon		# Decimal degrees
        self.alt = alt		# Meters
        self.course = course	# Degrees from true North 
        self.speed = speed	# Meters per second
        self.climb = climb	# Meters per second
        self.h_acc = h_acc	# Meters per second per second
        self.v_acc = v_acc	# Meters per second per second
    def next(self, quantum=1):
        "State after quantum."
        self.time += quantum
        avspeed = (2*self.speed + self.h_acc*quantum)/2
        avclimb = (2*self.climb + self.v_acc*quantum)/2
        self.alt += avclimb * quantum
        self.speed += self.h_acc * quantum
        self.climb += self.v_acc * quantum
        distance = avspeed * quantum
        # Formula from <http://williams.best.vwh.net/avform.htm#Rhumb>
        # Initial point cannot be a pole, but GPS doesn't work at high.
        # latitudes anyway so it would be OK to fail there.
        # Seems to assume a spherical Earth, which means it's going
        # to have a slight inaccuracy rising towards the poles.
        # The if/then avoids 0/0 indeterminacies on E-W courses.
        tc = gps.Deg2Rad(self.course)
        lat = gps.Deg2Rad(self.lat)
        lon = gps.Deg2Rad(self.lon)
        lat += distance * math.cos(tc)
        dphi = math.log(tan(lat/2+math.pi/4)/math.tan(self.lat/2+math.pi/4))
        if abs(lat-self.lat) < sqrt(1e-15):
            q = cos(self.lat)
        else:
            q = (lat-self.lat)/dphi
        dlon = -distance * sin(tc) / q
        self.lon = gp.Rad2Deg(math.mod(self.lon + dlon + pi, 2 * math.pi) - math.pi)
        self.lat = gp.Rad2Deg(lat)

# Satellite orbital elements are available at:
# <http://www.ngs.noaa.gov/orbits/>
# Orbital theory at:
# <http://www.wolffdata.se/gps/gpshtml/anomalies.html>

class satellite:
    "Orbital elements of one satellite. PRESENTLY A STUB"
    def __init__(self, prn):
        self.prn = prn
    def position(self, time):
        "Return right ascension and declination of satellite,"
        pass

# Next, the command interpreter.  This is an object that takes an
# input source in the track description language, interprets it into
# sammples that might be reported by a GPS, and calls a reporting
# class to generate output.

class gpssimException(exceptions.Exception):
    def __init__(self, message, filename, lineno):
        self.message = message
        self.filename = filename
        self.lineno = lineno
    def __str__(self):
        return '"%s", %d:' % (self.filename, self.lineno)

class gpssim:
    "Simulate a moving sensor, with skyview."
    active_PRNs = range(1, 24+1) + [134,] 
    def __init__(self, gpstype):
        self.ksv = ksv()
        self.ephemeris = {}
        # This sets up satellites at random.  Not really what we want.
        for PRN in simulator.active_PRNs:
            for (prn, satellite) in self.ephemeris.items():
                self.skyview[prn] = (random.randint(-60, +61),
                                     random.randint(0, 359))
        self.have_ephemeris = False
        self.channels = {}
        self.outfmt = outfmt
        self.status = gps.STATUS_NO_FIX
        self.mode = gps.MODE_NO_FIX
        self.validity = "V"
        self.satellites_used = 0
        self.filename = None
        self.lineno = 0
    def parse_tdl(self, line):
        "Interpret one TDL directive."
        line = line.strip()
        if "#" in line:
            line = line[:line.find("#")]
        if line == '':
            return
        fields = line.split()
        command = fields[0]
        if command == "time":
            self.ksv.time = gps.isotime(fields[1])
        elif command == "location":
            (self.lat, self.lon, self.alt) = map(float, fiels[1:])
        elif command == "course":
            self.ksv.time = float(fields[1])
        elif command == "speed":
            self.ksv.speed = float(fields[1])
        elif command == "climb":
            self.ksv.climb = float(fields[1])
        elif command == "acceleration":
            (self.ksv.h_acc, self.ksv.h_acc) = map(float, fields[1:])
        elif command == "snr":
            self.channels[int(fields[1])] = float(fields[2])
        elif command == "go":
            self.go(int(fields[1]))
        elif command == "status":
            try:
                code = fields[1]
                self.status = {"no_fix":0, "fix":1, "dgps_fix":2}[code.lower()]
            except KeyError:
                raise gpssimException("invalid status code '%s'" % code,
                                      self.filename, self.lineno)
        elif command == "mode":
            try:
                code = fields[1]
                self.status = {"no_fix":1, "2d":2, "3d":3}[code.lower()]
            except KeyError:
                raise gpssimException("invalid mode code '%s'" % code,
                                      self.filename, self.lineno)
        elif command == "satellites":
            self.satellites_used = int(fields[1])
        elif command == "validity":
            self.validity = fields[1]
        else:
            raise gpssimException("unknown command '%s'" % fields[1],
                                  self.filename, self.lineno)
        # FIX-ME: add syntax for ephemeris elements
        self.lineno += 1
    def filter(self, input, output):
        "Make this a filter for file-like objects."
        self.filename = input.name
        self.lineno = 1
        self.output = output
        for line in input:
            self.execute(line)
    def go(self, seconds):
        "Run the simulation for a specified number of seconds."
        for i in range(seconds):
            self.ksv.next()
            if self.have_ephemeris:
                self.skyview = {}
                for (prn, satellite) in self.ephemeris.items():
                    self.skyview[prn] = satellite.position(time)
            self.output.write(self.gpstype.report(self))

# Reporting classes need to have a report() method returning a string
# that is a sentence (or possibly several sentences) reporting the
# state of the simulation.  Presently we have only one, for NMEA
# devices, but the point of the architecture is so that we could simulate
# others - SirF, Evermore, whatever.

MPS_TO_KNOTS = 1.9438445	# Meters per second to knots

class NMEA:
    "NMEA output generator."
    def __init__(self):
        self.sentences = ("RMC", "GGA",)
        self.counter = 0
    def add_checksum(self, str):
        "Concatenate NMEA checksum and trailer to a string"
        sum = 0
        for (i, c) in enumerate(str):
            if i == 0 and c == "$":
                continue
            sum ^= ord(c)
        str += "*%02X\r\n" % sum
        return str
    def degtodm(self, angle):
        "Decimal degrees to GPS-style, degrees first followed by minutes."
        (fraction, integer) = math.modf(angle)
        return math.floor(angle) * 100 + fraction * 60;
    def GGA(self, sim):
        "Emit GGA sentence describing the simulation state."
        tm = time.gmtime(sim.ksv.time)
        gga = \
            "$GPGGA,%02d%02d%02d,%09.4f,%c,%010.4f,%c,%d,%02d," % (
            tm.tm_hour,
            tm.tm_min,
            tm.tm_sec,
            self.degtodm(abs(sim.ksv.lat)), "SN"[sim.ksv.lat > 0],
            self.degtodm(abs(sim.ksv.lon)), "WE"[sim.ksv.lon > 0],
            sim.status,
            sim.satellites_used);
        # HDOP calculation goes here
        gga += ","
        if sim.mode == gps.MODE_3D:
            gga += "%.1f,M" % self.ksv.lat
        gga += ","
        gga += "%.3f,M," % gpslib.wg84_separation(sim.ksv.lat, sim.ksv.lon)
        # Magnetic variation goes here
        # gga += "%3.2f,M," % mag_var
        gga += ",,"
        # Time in seconds since last DGPS update goes here
        gga += ","
        # DGPS station ID goes here
        return self.add_checksum(gga);
    def GLL(self, sim):
        "Emit GLL sentence describing the simulation state."
        tm = time.gmtime(sim.ksv.time)
        gll = \
            "$GPLL,%09.4f,%c,%010.4f,%c,%02d%02d%02d,%s," % (
            self.degtodm(abs(sim.ksv.lat)), "SN"[sim.ksv.lat > 0],
            self.degtodm(abs(sim.ksv.lon)), "WE"[sim.ksv.lon > 0],
            tm.tm_hour,
            tm.tm_min,
            tm.tm_sec,
            sim.validity,
            )
            # FAA mode indicator could go after these fields.
        return self.add_checksum(gll);
    def RMC(self, sim):
        "Emit RMC sentence describing the simulation state."
        tm = time.gmtime(sim.ksv.time)
        rmc = \
            "GPRMC,%02d%02d%02d,%s,%09.4f,%c,%010.4f,%c,%.1f,%02d%02d%02d," % (
            tm.tm_hour,
            tm.tm_min,
            tm.tm_sec,
            sim.validity,
            self.degtodm(abs(sim.ksv.lat)), "SN"[sim.ksv.lat > 0],
            self.degtodm(abs(sim.ksv.lon)), "WE"[sim.ksv.lon > 0],
            sim.course * MPS_TO_KNOTS,
            tm.tm_mday,
            tm.tm_mon,
            tm.tm_year % 100)
        # Magnetic variation goes here
        # rmc += "%3.2f,M," % mag_var
        rmc += ",,"
        # FAA mode goes here
        return self.add_checksum(rmc);        
    def ZDA(self, sim):
        "Emit ZDA sentence describing the simulation state."
        tm = time.gmtime(sim.ksv.time)
        zda = "$GPZDA,%02d%2d%02d,%02d,%02d,%04d" % (
            tm.tm_hour,
            tm.tm_min,
            tm.tm_sec,
            tm.tm_mday,
            tm.tm_mon,
            tm.tm_year,
            )
        # Local zone description, 00 to +- 13 hours, goes here
        zda += ","
        # Local zone minutes description goes here
        zda += ","
        return self.add_checksum(zda);        
    def report(self, sim):
        "Report the simulation state."
        out = ""
        for sentence in self.sentences:
            if type(sentence) == type(()):
                (interval, sentence) = sentence
                if self.counter % interval:
                    continue
            out += apply(getattr(self, sentence), [sim])
        self.counter += 1
        return out

# The very simple main line.

if __name__ == "__main__":
    try:
        gpssim(NMEA).filter(sys.stdin, sys.stdout)
    except gpssimException, e:
        print >>sys.stderr, e

# gpssim.py ends here.
