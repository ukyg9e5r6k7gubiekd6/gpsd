"""
gpsfake.py -- classes for creating a controlled test environment around gpsd

"""
import sys, os, time, signal, pty, termios
import string, exceptions, threading, socket
import gps

class PacketError(exceptions.Exception):
    def __init__(self, msg):
        self.msg = msg

class TestLoad:
    "Digest a logfile into a list of sentences we can cycle through."
    def __init__(self, logfp):
        self.sentences = []	# This and .packtype are the interesting bits
        self.logfp = logfp
        self.logfile = logfp.name 
        # Skip the comment header
        while True:
            first = logfp.read(1)
            if first == "#":
                logfp.readline()
            else:
                logfp.seek(-1, 1)	# Must be a real file, not stdin
                break
        # Grab the packets
        while True:
            packet = self.packet_get()
            #print "I see: %s, length %d" % (`packet`, len(packet))
            if not packet:
                break
            else:
                self.sentences.append(packet)
        # Look at the first packet to grok the GPS type
        if self.sentences[0][0] == '$':
            self.packtype = "NMEA"
            self.legend = "gpsfake: line %d "
            self.textual = True
        elif self.sentences[0][0] == '\xff':
            self.packtype = "Zodiac binary"
            self.legend = "gpsfake: packet %d"
            self.textual = True
        elif self.sentences[0][0] == '\xa0':
            self.packtype = "SiRF-II binary"
            self.legend = "gpsfake: packet %d"
            self.textual = False
        else:
            print "gpsfake: unknown log type (not NMEA or SiRF) can't handle it!"
            self.sentences = None
    def packet_get(self):
        "Grab a packet.  Unlike the daemon's state machine, this assumes no noise."
        first = self.logfp.read(1)
        if not first:
            return None
        elif first == '$':					# NMEA packet
            return "$" + self.logfp.readline()
        second = self.logfp.read(1)
        if first == '\xa0' and second == '\xa2':		# SiRF packet
            third = self.logfp.read(1)
            fourth = self.logfp.read(1)
            length = (ord(third) << 8) | ord(fourth)
            return "\xa0\xa2" + third + fourth + self.logfp.read(length+4)
        elif first == '\xff' and second == '\x81':
            third = self.logfp.read(1)
            fourth = self.logfp.read(1)
            fifth = self.logfp.read(1)
            sixth = self.logfp.read(1)
            id = ord(third) | (ord(fourth) << 8)
            ndata = ord(fifth) | (ord(sixth) << 8)
            return "\xff\x81" + third + fourth + fifth + sixth + self.logfp.read(2*ndata+6)
        else:
            raise PacketError("unknown packet type, leader %s, (0x%x)" % (first, ord(first)))

class FakeGPS:
    "A fake GPS is a pty with a test log ready to be cycled to it."
    def __init__(self, logfp, rate=4800):
        self.go_predicate = lambda i, s: True
        self.readers = 0
        self.thread = None
        self.index = 0
        baudrates = {
            0: termios.B0,
            50: termios.B50,
            75: termios.B75,
            110: termios.B110,
            134: termios.B134,
            150: termios.B150,
            200: termios.B200,
            300: termios.B300,
            600: termios.B600,
            1200: termios.B1200,
            1800: termios.B1800,
            2400: termios.B2400,
            4800: termios.B4800,
            9600: termios.B9600,
            19200: termios.B19200,
            38400: termios.B38400,
            57600: termios.B57600,
            115200: termios.B115200,
            230400: termios.B230400,
        }
        rate = baudrates[rate]	# Throw an error if the rate isn't legal
        if type(logfp) == type(""):
            logfp = open(logfp, "r");            
        self.testload = TestLoad(logfp)
        (self.master_fd, self.slave_fd) = pty.openpty()
        self.slave = os.ttyname(self.slave_fd)
        ttyfp = open(self.slave, "rw")
        raw = termios.tcgetattr(ttyfp.fileno())
        raw[0] = 0					# iflag
        raw[1] = termios.ONLCR				# oflag
        raw[2] &= ~(termios.PARENB | termios.CRTSCTS)	# cflag
        raw[2] |= (termios.CSIZE & termios.CS8)		# cflag
        raw[2] |= termios.CREAD | termios.CLOCAL	# cflag
        raw[3] = 0					# lflag
        raw[4] = raw[5] = rate
        termios.tcsetattr(ttyfp.fileno(), termios.TCSANOW, raw)
    def slave_is_open(self):
        "Is the slave device of this pty opened?"
        return os.system("fuser -s " + self.slave) == 0
    def enable_capture(self):
        "Enable capture of the responses from the daemon."
        self.responses = []
        try:
            session = gps.gps()
        except socket.error:
            return False
        session.query("w+r+")
        session.set_thread_hook(lambda x: self.responses.append(x))
        return True
    def __feed(self):
        "Feed the contents of the GPS log to the daemon."
        while self.readers and self.go_predicate(self.index, self):
            os.write(self.master_fd, self.testload.sentences[self.index % len(self.testload.sentences)])
            self.index += 1
    def start(self, thread=False):
        "Increment pseudodevice's reader count, starting it if necessary."
        self.readers += 1
        if self.readers == 1:
            self.thread = threading.Thread(self.__feed())
            while not self.slave_is_open():
                time.sleep(0.01);
            if thread:
                self.thread.start()	# Run asynchronously
            else:
                self.thread.run()	# Run synchronously
    def release(self):
        "Decrement pseudodevice's reader count; it will stop when count==0."
        if self.readers > 0:
            self.readers -= 1
    def stop(self):
        "Zero pseudodevice's reader count; it will stop."
        self.readers = 0

class DaemonInstance:
    "Control a gpsd instance."
    def __init__(self, control_socket=None):
        self.sockfile = None
        self.pid = None
        if control_socket:
            self.control_socket = control_socket
        else:
            self.control_socket = "/tmp/gpsfake-%d.sock" % os.getpid()
        self.pidfile  = "/tmp/gpsfake_pid-%s" % os.getpid()
    def spawn(self, options, background=False, prefix=""):
        "Spawn a daemon instance."
        self.spawncmd = "gpsd -N -F %s -P %s %s" % (self.control_socket, self.pidfile, options)
        if prefix:
            self.spawncmd = prefix + " " + self.spawncmd.strip()
        if background:
            self.spawncmd += " &"
        os.system(self.spawncmd)
    def wait_pid(self):
        "Wait for the daemon, get its PID and a control-socket connection."
        while True:
            try:
                fp = open(self.pidfile)
            except IOError:
                time.sleep(1)
                continue
            self.pid = int(fp.read())
            fp.close()
            break
    def __get_control_socket(self):
        # Now we know it's running, get a connection to the control socket.
        if not os.path.exists(self.control_socket):
            return None
        try:
            self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM, 0)
            self.sock.connect(self.control_socket)
        except socket.error:
            if self.sock:
                self.sock.close()
            self.sock = None
        return self.sock
    def is_alive(self):
        "Is the daemon still alive?"
        try:
            st = os.kill(self.pid, 0)
            return True
        except OSError:
            return False
    def add_device(self, path):
        "Add a device to the daemon's internal search list."
        if self.__get_control_socket():
            self.sock.sendall("+%s\r\n" % path)
            self.sock.recv(12)
            self.sock.close()
    def remove_device(self, path):
        "Remove a device from the daemon's internal search list."
        if self.__get_control_socket():
            self.sock.sendall("-%s\r\n" % path)
            self.sock.recv(12)
            self.sock.close()
    def __del__(self):
        "Kill the daemon instance."
        import signal	# Avoids some strange condition on Python exit
        if self.pid:
            os.kill(self.pid, signal.SIGTERM)
            self.pid = None
            time.sleep(1)	# Give signal time to land

class TestSession:
    "Manage a session including a daemon with fake GPS and client threads."
    def __init__(self, prefix=None, options=None):
        "Initialize the test session by launching the daemon."
        self.daemon = DaemonInstance()
        self.fakegpslist = {}
        self.clients = []
        self.client_id = 0
        self.reporter = sys.stdout.write
        self.daemon.spawn(background=True, prefix=prefix, options=options)
        self.daemon.wait_pid()
    def gps_add(self, name):
        "Add a simulated GPS being fed by the specified logfile."
        if name not in self.fakegpslist:
            if not name.endswith(".log"):
                logfile = name + ".log"
            else:
                logfile = name
            newgps = FakeGPS(logfile)
            self.fakegpslist[newgps.slave] = newgps
        self.daemon.add_device(newgps.slave)
    def gps_remove(self, name):
        "Remove a simulated GPS from the daeon's search list."
        self.devices[name].stop()
        self.daemon.remove_device(newgps.slave)
    def client_add(self, commands):
        "Initiate a client session and force connection to a fake GPS."
        newclient = gps.gps()
        self.client_id += 1
        newclient.id = self.client_id 
        self.clients.append(newclient)
        newclient.query("of\n")
        self.fakegpslist[newclient.device].start(thread=True)
        session.set_thread_hook(self.reporter)
        if commands:
            newclient.query(commands)
        return newclient
    def client_order(self, id, commands):
        "Ship a command down a client channel, accept a response."
        for client in self.clients:
            if client.id == id:
                client.query(commands)
    def client_remove(self, id):
        "Terminate a client session."
        for client in self.clients:
            if client.id == id:
                self.fakegpslist[client.device].release()
                self.clients.remove(client)
                del client
                break

if __name__ == "__main__":
    #prefix="valgrind --tool=memcheck", 
    test = TestSession(options="-D 4")
    test.gps_add("FUCKUP")
    test.client_add("w+\n")
    time.sleep(5)

# End
