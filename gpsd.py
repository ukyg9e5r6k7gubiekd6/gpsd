#!/usr/bin/env python
#
# gpsd.py -- low-level interface to an NMEA GPS
#
# Like libgpsd in C, but handles only straight NMEA devices (not Zodiac).

import sys, termios, os, fcntl, copy, time, math, struct
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
        if not words[0] or not words[1] or not words[2] or not words[3]:
            return False
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
        self.data.latlon_stamp.refresh()
        self.data.latlon_stamp.changed = ((lat, lon) != (self.data.latitude, self.data.longitude))
        self.data.latitude = lat
        self.data.longitude = lon
        return True

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
            yyyymmdd = time.strftime("%C") + "%s-%s-%s" % (ddmmyy[4:6], ddmmyy[2:4], ddmmyy[0:2])
        if not hhmmss:
            hhmmss = time.strftime("%H:%M:%S")
        else:
            hhmmss = hhmmss[0:2] + ":" + hhmmss[2:4] + ":" + hhmmss[4:]
        self.data.utc = yyyymmdd + "T" + hhmmss + "Z"

    def processGPRMC(self, words):
        if words[1] == "A":
            self.update_timestamp(words[8], words[0])
            if self.__do_lat_lon(words[2:]):
                if words[6]:
                    newspeed = float(words[6])
                    self.data.speed_stamp.changed = (self.data.speed != newspeed) 
                    self.data.speed = newspeed
                    self.data.speed_stamp.refresh()
                if words[7]:
                    newtrack = float(words[7])
                    self.data.track_stamp.changed = (self.data.track != newtrack) 
                    self.data.track = newtrack
                    self.data.track_stamp.refresh()

    def processGPGLL(self, words):
        if words[1] == "A":
            self.__do_lat_lon(words)
            self.update_timestamp(None, words[4])
            if words[5] == 'N':
                newstatus = gps.STATUS_NO_FIX
            elif words[5] == 'D':
                newstatus = gps.STATUS_DGPS_FIX
            else:
                newstatus = gps.STATUS_FIX;
            self.data.status_stamp.changed = (self.data.status != newstatus)
            self.data.status = newstatus
            self.logger(3, "GPGLL sets status %d\n", self.data.status);
 
    def processGPGGA(self,words):
        self.update_timestamp(None, words[0])
        self.__do_lat_lon(words[1:])
        if words[8]:
            newaltitude        = float(words[8])
            self.data.altitude_stamp.changed = (self.data.altitude != newaltitude)
            self.data.altitude = newaltitude
            self.data.altitude_stamp.refresh()
        if words[5]:
            newstatus          = int(words[5])
            self.data.status_stamp.changed = (self.data.status != newstatus)
            self.data.status = newstatus
            self.data.status_stamp.refresh()
            self.logger(3, "GPGGA sets status %d\n" % self.data.status);

    def processGPGSA(self,words):
        newmode = int(words[1])
        self.data.mode_stamp.changed = (self.data.mode != newmode)
        self.data.mode = newmode
        self.data.mode_stamp.refresh()
        self.data.satellites_used = map(int, filter(lambda x: x, words[2:14]))
        (newpdop, newhdop, newvdop) = (self.data.pdop, self.data.hdop, self.data.vdop)
        if words[14]:
            newpdop = float(words[14])
        if words[15]:
            newhdop = float(words[15])
        if words[16]:
            newvdop = float(words[16])
        if words[14] and words[15] and words[16]:
            self.data.fix_quality_stamp.refresh()
            self.data.fix_quality_stamp.changed = (newpdop, newhdop, newvdop) != (self.data.pdop, self.data.hdop, self.data.vdop)
            (self.data.pdop, self.data.hdop, self.data.vdop) = (newpdop, newhdop, newvdop)
        self.logger(3, "GPGGA sets mode %d\n" % self.data.mode)

    def processGPGVTG(self, words):
        if words[0]:
            newtrack = float(words[0])
            self.data.track_stamp.changed = (self.data.track != newtrack) 
            self.data.track = newtrack
            self.data.track_stamp.refresh()
        if words[1] == 'T':
            newspeed = words[4]
        else:
            newspeed = words[2]
        if newspeed:
            newspeed = float(newspeed)
            self.data.speed_stamp.changed = (self.data.speed != newspeed) 
            self.data.speed = newspeed

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
        if line and line[0] == '$':
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

    def handler(self, linebuf, raw_hook):
        self.handle_line(linebuf[:-2])
        if raw_hook:
            raw_hook(linebuf)

class gpsd(gps.gpsdata):
    "Device interface to a GPS."
    class gps_driver:
        def __init__(self, name,
                     parser=NMEA,
                     cycle=1, stopbits=1,
                     trigger=None, initializer=None, rtcm=None, wrapup=None):
            self.name = name
            self.parser = parser
            self.cycle = cycle
            self.stopbits = stopbits
            self.trigger = trigger
            self.initializer = initializer
            self.rtcm = rtcm
            self.wrap = wrapup
    def __init__(self, device="/dev/gps", dgps=None, logger=None):
        self.ttyfd = -1
        self.device = device
        self.bps = 0
        self.devtype = gpsd.gps_driver("NMEA")
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

    def send(self, buf):
        os.write(self.ttyfd, self.parser.add_checksum(buf))

    def readline(self):
        buf = ""
        while len(buf) < gps.NMEA_MAX:
            buf += os.read(self.ttyfd, gps.NMEA_MAX-len(buf))
            if buf.endswith("\r\n"):
                break
        return buf

    def set_speed(self, speed):
        self.raw[4] = self.raw[5] = eval("termios.B" + `speed`)
        termios.tcflush(self.ttyfd, termios.TCIOFLUSH)
        termios.tcsetattr(self.ttyfd, termios.TCSANOW, self.raw)
        termios.tcflush(self.ttyfd, termios.TCIOFLUSH)
        time.sleep(3)
        if self.readline().find("$GP") > -1:
            self.bps = speed
            return 1
        else:
            return 0;

    def activate(self):
        self.ttyfd = os.open(self.device, os.O_RDWR|os.O_SYNC|os.O_NOCTTY)
        if self.ttyfd == -1:
            return None
	self.normal = termios.tcgetattr(self.ttyfd)
        self.raw = termios.tcgetattr(self.ttyfd)
        self.raw[0] = 0						# iflag
        self.raw[1] = termios.ONLCR				# oflag
	# Tip from Chris Kuethe: the FIDI chip used in the Trip-Nav
	# 200 (and possibly other USB GPSes) gets completely hosed
	# in the presence of flow control.  Thus, turn off CRTSCTS.
        self.raw[2] &= ~(termios.PARENB | termios.CRTSCTS)	# cflag
        for stopbits in range(1, 3):
            self.raw[2] &= ~termios.CSIZE			# cflag
            if stopbits == 2:
                self.raw[2] |= (termios.CSIZE & termios.CS7)	# cflag
            else:
                self.raw[2] |= (termios.CSIZE & termios.CS8)	# cflag
            self.raw[2] |= termios.CREAD | termios.CLOCAL	# cflag
            self.raw[3] = 0					# lflag
            for speed in (4800, 9600, 19200, 38400, 57600):
                if self.set_speed(speed):
                    if self.devtype.initializer:
                        self.devtype.initializer(self)
                    self.online = True;
                    return self.ttyfd
        self.ttyfd = -1
        return self.ttyfd

    def deactivate(self):
        if hasattr(self, 'normal'):
            termios.tcsetattr(self.ttyfd, termios.TCSANOW, self.normal)
        self.online = False;
        self.mode = gps.MODE_NO_FIX;
        self.status = gps.STATUS_NO_FIX;

    def set_raw_hook(self, hook=None):
        self.raw_hook = hook

    def waiting(self):
        "How much input is waiting?"
        if self.ttyfd == -1:
            return -1
        st = fcntl.ioctl(self.ttyfd, termios.FIONREAD, " "*struct.calcsize('i'))
        if st == -1:
            return -1
        st = struct.unpack('i', st)[0]
        return st

    def read(self):
        return os.read(self.ttyfd, gps.NMEA_MAX*2)

    def write(self, buf):
        return os.write(self.ttyfd, buf)

    def poll(self):
        if self.dsock > -1:
            os.write(self.ttyfd, session.dsock.recv(1024))
        waiting = self.waiting()
        if waiting < 0:
            return waiting
        elif waiting == 0:
            if time.time() < self.online_stamp.last_refresh + self.devtype.cycle + 1:
                return 0
            else:
                self.online = False
                self.online_stamp.refresh()
                return -1
        else:
            self.online = True
            self.online_stamp.refresh()
            self.devtype.parser.handler(self.readline(), self.raw_hook)

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

# SirF-II control code

class SiRF:
    def transport(payload):
        msg = '\xa0'
        msg += '\xa2'
        msg += chr(len(payload) >> 8)
        msg += chr(len(payload) & 0xff)
        msg += payload

        checksum = 0
        for ch in payload:
          checksum += ord(ch);
        checksum &= 0x7fff

        msg += chr((checksum >> 8) & 0xff00)
        msg += chr(checksum & 0x00ff)
        msg += '\xb0'
        msg += '\xb3'
        return msg
    transport = staticmethod(transport)

    def to_NMEA(baudrate):
        "Generate a SiRF binary protocol command to switch back to NMEA."
        switcher = [
            '\x81',	# Byte  0 = 0x81: Switch to NMEA command
            '\x02',	# Byte  1 = 0x02: Leave debug-message switch as it is.
            '\x01',	# Byte  2 = 0x01: Enable GPGGA at 1-second interval
            '\x01',	# Byte  3 = 0x01: GPGGA checksum enable
            '\x01',	# Byte  4 = 0x01: Enable GPGLL at 1-second interval
            '\x01',	# Byte  5 = 0x01: GPGLL checksum enable
            '\x01',	# Byte  6 = 0x05: Enable GPGSA at 5-second interval
            '\x01',	# Byte  7 = 0x01: GPGSA checksum enable
            '\x05',	# Byte  8 = 0x05: Enable GPGSV at 5-second interval
            '\x01',	# Byte  9 = 0x01: GPGSV checksum enable
            '\x00',	# Byte 10 = 0x05: Disable GPMSS
            '\x00',	# Byte 11 = 0x01: GPMSS checksum disable
            '\x01',	# Byte 12 = 0x01: Enable GPRMC at 1-second interval
            '\x01',	# Byte 13 = 0x01: GPRMC checksum enable
            '\x01',	# Byte 14 = 0x01: Enable GPVTG at 1-second interval
            '\x01',	# Byte 15 = 0x01: GPVTG checksum enable
            '\x00',	# Byte 16 = 0x00: Unused
            '\x00',	# Byte 17 = 0x00: Unused
            '\x00',	# Byte 18 = 0x00: Unused
            '\x00',	# Byte 19 = 0x00: Unused
            '\x00',	# Byte 20 = 0x00: Unused
            '\x00',	# Byte 21 = 0x00: Unused
            ]
        switcher += [chr(baudrate >> 8), chr(baudrate & 0x0ff)]
        return SiRF.transport("".join(switcher))
    to_NMEA = staticmethod(to_NMEA)

    def reset():
        "Generate a GPS reset command."
        return SiRF.transport("\x85\x00\x00\x00\x00\x00\x00\x00\x00")
    reset = staticmethod(reset)

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
    del dev
