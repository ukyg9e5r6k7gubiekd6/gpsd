#!/usr/bin/env python
#
# gpsd.py -- low-level interface to an NMEA GPS
#
# Like libgpsd in C, but handles only straight NMEA devices
# with a send cycle of one second.

import termios, os, fcntl, copy, time, math, struct
import gps

class NMEA:
    def __init__(self, data, logger=None):
        self.data = data
        if logger:
            self.logger = logger
        else:
            self.logger = lambda *args: None

    def add_checksum(self,sentence):
        csum = 0
        for c in sentence:
            csum = csum ^ ord(c)
        return sentence + "%02X" % csum + "\r\n"

    def checksum(self,sentence, cksum):
        csum = 0
        for c in sentence:
            csum = csum ^ ord(c)
        return "%02X" % csum == cksum

    def __do_lat_lon(self, words):
        # The Navman sleeve's GPS firmware sometimes puts the direction in
        # the wrong order.
        if words[0][-1] == 'N':
            words[0] = words[0][:-1]
            words[1] = 'N'
        if words[0][-1] == 'S':
            words[0] = words[0][:-1]
            words[1] = 'S'
        if words[2][-1] == 'E':
            words[2] = words[2][:-1]
            words[3] = 'E'
        if words[2][-1] == 'W':
            words[2] = words[2][:-1]
            words[3] = 'W'
        if len(words[0]):
            lat = float(words[0])
            frac, intpart = math.modf(lat / 100.0)
            lat = intpart + frac * 100.0 / 60.0
            if words[1] == 'S':
                lat = -lat
        if len(words[2]):
            lon = float(words[2])
            frac, intpart = math.modf(lon / 100.0)
            lon = intpart + frac * 100.0 / 60.0
            if words[3] == 'W':
                lon = -lon
        self.data.latlon_stamp.changed = ((lat, lon) != (self.data.latitude, self.data.longitude))
        self.data.latitude = lat
        self.data.longitude = lon

    # Three sentences, GGA and GGL and RMC, contain timestamps.
    # Timestamps always look like hhmmss.ss, with the trailing .ss
    # part optional.  RMC alone has a date field, in the format
    # ddmmyy.
    #
    # We want the output to be in ISO 8601 format:
    #
    # yyyy-mm-ddThh:mm:ss.sssZ
    # 012345678901234567890123
    #
    # (where part or all of the decimal second suffix may be omitted).
    # This means that for GPRMC we must supply a century and for GGA and
    # GGL we must supply a century, year, and day.
    #
    # We get the missing data from the host machine's clock time.  That
    # is, the machine where this *daemon* is running -- which is probably
    # connected to the GPS by a link short enough that it doesn't cross
    # the International Date Line.  Even if it does, this hack could only
    # screw the year number up for two hours around the first midnight of
    # a new century.
    def update_timestamp(self, ddmmyy=None, hhmmss=None):
        if not ddmmyy:
            yyyymmdd = time.strftime("%Y-%m-%d")
        else:
            yyyymmdd = time.strftime("%C") + "%s-%s-%sT" % (ddmmyy[4:6], ddmmyy[2:4], ddmmyy[8:10])
        if not hhmmss:
            hhmmss = time.strftime("%H:%M:%S")
        else:
            hhmmss = hhmmss[0:2] + ":" + hhmmss[2:4] + ":" + hhmmss[4:]
        self.data.utc = yyyymmdd + "T" + hhmmss + "Z"

    def processGPRMC(self, words):
        if words[1] == "A":
            self.update_timestamp(words[8], words[0])
            self.__do_lat_lon(words[2:])
            if words[6]: self.data.speed = float(words[6])
            if words[7]: self.data.track = float(words[7])

    def processGPGLL(self, words):
        if words[1] == "A":
            self.__do_lat_lon(words)
            self.update_timestamp(None, words[4])
            if words[5] == 'N':
                self.data.status = gps.STATUS_NO_FIX
            elif words[5] == 'D':
                self.data.status = gps.STATUS_DGPS_FIX
            else:
                self.data.status = gps.STATUS_FIX;
            self.logger(3, "GPGLL sets status %d\n", self.data.status);
 
    def processGPGGA(self,words):
        self.update_timestamp(None, words[0])
        self.__do_lat_lon(words[1:])
        self.data.status          = int(words[5])
        self.data.altitude        = float(words[8])
        self.logger(3, "GPGGA sets status %d\n" % self.data.status);

    def processGPGSA(self,words):
        self.data.mode = int(words[1])
        self.data.satellites_used = map(int, filter(lambda x: x, words[2:14]))
        self.data.pdop = float(words[14])
        self.data.hdop = float(words[15])
        self.data.vdop = float(words[16])
        self.logger(3, "GPGGA sets mode %d\n" % self.data.mode)

    def processGPGVTG(self, words):
        self.data.track = float(words[0])
        if words[1] == 'T':
            self.data.speed = float(words[4])
        else:
            self.data.speed = float(words[2])

    def nmea_sane_satellites(self):
        # data may be incomplete *
        if self.data.part < self.data.await:
            return False;
        # This sanity check catches an odd behavior of the BU-303, and thus
        # possibly of other SiRF-II based GPSes.  When they can't see any
        # satellites at all (like, inside a building) they sometimes cough
        # up a hairball in the form of a GSV packet with all the azimuth 
        # and entries 0 (but nonzero elevations).  This
        # was observed under SiRF firmware revision 231.000.000_A2.
        for sat in self.data.satellites:
            if sat.azimuth[n]:
                return True;
        return False;

    def processGPGSV(self, words):
        self.data.await = int(words.pop(0))
        self.data.part = int(words.pop(0))
        inview = int(words.pop(0))	# Total satellites in view
        lower = (self.data.part - 1) * 4
        upper = lower + 4
        fldnum = 0
        newsats = []
        while lower < inview and lower < upper:
            prn = int(words[fldnum]); fldnum += 1
            elevation = int(words[fldnum]); fldnum += 1
            azimuth = int(words[fldnum]); fldnum += 1
            if words[fldnum]:
                ss = int(words[fldnum])
            else:
                ss = gps.SIGNAL_STRENGTH_UNKNOWN
            fldnum += 1
            newsats.append(gps.gpsdata.satellite(prn, elevation, azimuth, ss))
            lower += 1
        # not valid data until we've seen a complete set of parts
        if self.data.part < self.data.await:
            self.logger(3, "Partial satellite data (%d of %d).\n" % (self.data.part, self.data.await));
        else:
            # trim off PRNs with spurious data attached
            while newsats \
                        and not newsats[-1].elevation \
                        and not newsats[-1].azimuth \
                        and not newsats[-1].ss:
                newsats.pop()
            if self.nmea_sane_satellites():
                self.data.satellites = newsats
                self.logger(3, "Satellite data OK.\n")
            else:
                self.logger(3, "Satellite data no good.\n");

    def handle_line(self, line):
        if line[0] == '$':
            line = line[1:].split('*')
            if len(line) != 2: return
            if not self.checksum(line[0], line[1]):
                self.logger(0, "Bad checksum\n")
                return
            words = line[0].split(',')
            if NMEA.__dict__.has_key('process'+words[0]):
                NMEA.__dict__['process'+words[0]](self, words[1:])
            else:
                self.logger(0, "Unknown sentence\n")
        else:
            return self.logger(0, "Not NMEA\n")

    def handler(self, fd, raw_hook):
        linebuf = ""
        try:
            while True:
                linebuf += os.read(fd, 1)
                if linebuf.find("\r\n") > -1:
                    self.handle_line(linebuf[:-2])
                    if raw_hook:
                        raw_hook(linebuf)
                    break
        except OSError:
            print "Line buffer on error : " + `linebuf`
            pass

class gpsd(gps.gpsdata):
    "Device interface to a GPS."
    class gps_driver:
        def __init__(self, name,
                     parser=NMEA,
                     cycle=1, bps=4800,
                     trigger=None, initializer=None, rtcm=None, wrapup=None):
            self.name = name
            self.parser = parser
            self.cycle = cycle
            self.bps = bps
            self.trigger = trigger
            self.initializer = initializer
            self.rtcm = rtcm
            self.wrap = wrapup
    def __init__(self, device="/dev/gps", bps=4800,
                 devtype='n', dgps=None, logger=None):
        self.ttyfd = -1
        self.device = device
        self.bps = bps
        self.drivers = {
            'n' : gpsd.gps_driver("NMEA"),
            # Someday, other drivers go here
            }
        self.devtype = self.drivers[devtype]
        if not logger:
            logger = lambda level, message: None
        self.devtype.parser = self.devtype.parser(self, logger=logger)
        self.logger = logger
        self.dsock = -1
        self.fixcnt = 0
        self.sentdgps = 0
        gps.gpsdata.__init__(self)
        if dgps:
            dgpsport = "2101"
            if ":" in dgps:
                (dgps, dgpsport) = dgps(":")
            self.dsock = gps.gps.connect(self, dgps, dgpsport)
        self.raw_hook = None

    def __del__(self):
        self.deactivate()
        if self.dsock >= 0:
            os.close(self.dsock);
    close = __del__

    def activate(self):
        self.ttyfd = os.open(self.device, os.O_RDWR);  
	self.normal = termios.tcgetattr(self.ttyfd)
        self.raw = termios.tcgetattr(self.ttyfd)
        self.raw[0] = 0						# iflag
        self.raw[1] = termios.ONLCR				# oflag
        self.raw[2] &= ~(termios.PARENB | termios.CRTSCTS)	# cflag
        self.raw[2] |= (termios.CSIZE & termios.CS8)		# cflag
        self.raw[2] |= termios.CREAD | termios.CLOCAL		# cflag
        self.raw[3] = 0						# lflag
        self.raw[4] = self.raw[5] = eval("termios.B" + `self.bps`)
        termios.tcsetattr(self.ttyfd, termios.TCSANOW, self.raw)
	self.online = True;

    def deactivate(self):
        if hasattr(self, 'normal'):
            termios.tcsetattr(self.ttyfd, termios.TCSANOW, self.normal)
        self.online = False;
        self.mode = gps.MODE_NO_FIX;
        self.status = gps.STATUS_NO_FIX;

    def set_raw_hook(self, hook=None):
        self.raw_hook = hook

    def is_input_waiting(self):
        if self.ttyfd < 0:
            return -1
        st = fcntl.ioctl(self.ttyfd, termios.FIONREAD, " "*struct.calcsize('i'))
        if st == -1:
            return -1
        st = struct.unpack('i', st)[0]
        return st

    def poll(self):
        if self.dsock > -1:
            os.write(self.ttyfd, session.dsock.recv(1024))
        waiting = self.is_input_waiting()
        if waiting < 0:
            return waiting
        elif waiting == 0:
            if time.time() < self.online_stamp.last_refresh + self.devtype.cycle + 1:
                return 0
            else:
                self.online = False
                session.online_stamp.refresh()
                return -1
        else:
            self.online = True
            self.online_stamp.refresh()
            self.devtype.parser.handler(self.ttyfd, self.raw_hook)

            # count the good fixes
            if self.status > gps.STATUS_NO_FIX: 
	    	self.fixcnt += 1;

            # may be time to ship a DGPS correction to the GPS
            if self.fixcnt > 10:
                if not self.sentdgps:
                    self.sentdgps += 1;
                    if self.dsock > -1:
                        self.dsock.send(self.dsock, \
                              "R %0.8f %0.8f %0.2f\r\n" % \
                              (self.latitude, self.longitude, self.altitude))
	return waiting;

if __name__ == '__main__':
    import sys
    def logger(level, message):
        sys.stdout.write(message)
    def dumpline(message):
        sys.stdout.write("Raw line: " + `message`+ "\n")
    dev = gpsd(logger=logger)
    dev.set_raw_hook(dumpline)
    dev.activate()
    while True:
        status = dev.poll()
        if status > 0:
            print dev
            print "=" * 75
    del wrapup()
