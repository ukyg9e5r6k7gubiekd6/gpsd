"""
$Id$

gpsfake.py -- classes for creating a controlled test environment around gpsd.

The gpsfake(1) regression tester shipped with gpsd is a trivial wrapper
around this code.  For a more interesting usage example, see the
valgrind-audit script shipped with the gpsd code.

To use this code, start by instantiating a TestSession class.  Use the
prefix argument if you want to run the daemon under some kind of run-time
monitor like valgrind or gdb.  Here are some particularly useful possibilities:

valgrind --tool=memcheck --gen-suppressions=yes --leak-check=yes
    Run under Valgrind, checking for malloc errors and memory leaks.

xterm -e gdb -tui --args
    Run under gdb, controlled from a new xterm.

You can use the options argument to pass in daemon options; normally you will
use this to set the debug-logging level.

On initialization, the test object spawns an instance of gpsd with no
devices or clients attached, connected to a control socket.

TestSession has methods to attach and detch fake GPSes. The
TestSession class simulates GPS devices for you with objects composed
from a pty and a thread that cycles sentences into the master side
from some specified logfile; gpsd reads the slave side.  A fake GPS is
identified by the string naming its slave device.

TestSession also has methods to start and end client sessions.  Daemon
responses to a client are fed to a hook function which, by default,
discards them.  You can change the hook to sys.stdout.write() to dump
responses to standard output (this is what the gpsfake executable
does) or do something more exotic. A client session is identified by a
small integer that counts the number of client session starts.

There are a couple of convenience methods.  TestSession.wait() does nothing,
allowing a specified number of seconds to elapse.  TestSession.client_query()
ships commands to an open client session.

TestSession does not currently capture the daemon's log output.  It is
run with -N, so the output will go to stderr (along with, for example,
Valgrind notifications).

Your test code should be wrapped in a try/finally block that calls the
TestSession cleanup() method; this will ensure that any stray threads
are properly terminated.  If you do anything with the SIGINT, SIGQUIT,
or SIGTERM signals, ensure that they call the TestSession.killall()
method; otherwise your code will fail to clean up after itself when
interrupted.

Each FakeGPS instance tries to packetize the data from the logfile it
is initialized with.  It looks for packet headers asociated with common
packet types such as NMEA, SiRF, and Zodiac.  Additioonally, the Type
header in a logfile can be used to force the packet type, notably to RTCM
which is fed to the daemon character by character,

There are some limitations. Due to indeterminacy in thread timings, it
is not guaranteed that runs with identical options will present
exactly the same sentences to the daemon at the same times from start.
"""
import sys, os, time, signal, pty, termios
import string, exceptions, threading, socket
import gps

### System-dependent code begins here
#

def proc_has_file_open(pid, file):
    "Does the given process have the specified file open?"
    d = "/proc/%s/fd/" % pid
    try:
        for fd in os.listdir(d):
            if os.readlink(d+fd) == file:
                return True
    except OSError:
        pass
    return False

def proc_fd_set(pid):
    "Return the set of file descriptors currently opened by the process."
    fds = map(int, os.listdir("/proc/%d/fd" % self.pid))
    # I wish I knew what the entries above 1000 in Linux /proc/*/fd mean...
    return filter(lambda x: x < 1000, fds)

#
### System-dependent code ends here

class PacketError(exceptions.Exception):
    def __init__(self, msg):
        self.msg = msg

class TestLoad:
    "Digest a logfile into a list of sentences we can cycle through."
    def __init__(self, logfp):
        self.sentences = []	# This and .packtype are the interesting bits
        self.logfp = logfp
        self.logfile = logfp.name
        self.type = None
        # Skip the comment header
        while True:
            first = logfp.read(1)
            self.first = first;
            if first == "#":
                line = logfp.readline()
                if line.strip().startswith("Type:"):
                    if line.find("RTCM") > -1:
                        self.type = "RTCM"
            else:
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
            self.packtype = "SiRF binary"
            self.legend = "gpsfake: packet %d"
            self.textual = False
        elif self.sentences[0][0] == '\x10':
            self.packtype = "TSIP binary"
            self.legend = "gpsfake: packet %d"
            self.textual = False
        elif self.type == "RTCM":
            self.packtype = "RTCM"
            self.legend = None
            self.textual = False
        else:
            sys.stderr.write("gpsfake: unknown log type (not NMEA or SiRF) can't handle it!\n")
            self.sentences = None
    def packet_get(self):
        "Grab a packet.  Unlike the daemon's state machine, this assumes no noise."
        if self.first == '':
            first = self.logfp.read(1)
        else:
            first=self.first
            self.first=''
        if not first:
            return None
        elif self.type == "RTCM":
            return first
        elif first == '$':					# NMEA packet
            return "$" + self.logfp.readline()
        second = self.logfp.read(1)
        if first == '\xa0' and second == '\xa2':		# SiRF packet
            third = self.logfp.read(1)
            fourth = self.logfp.read(1)
            length = (ord(third) << 8) | ord(fourth)
            return "\xa0\xa2" + third + fourth + self.logfp.read(length+4)
        elif first == '\xff' and second == '\x81':		# Zodiac
            third = self.logfp.read(1)
            fourth = self.logfp.read(1)
            fifth = self.logfp.read(1)
            sixth = self.logfp.read(1)
            id = ord(third) | (ord(fourth) << 8)
            ndata = ord(fifth) | (ord(sixth) << 8)
            return "\xff\x81" + third + fourth + fifth + sixth + self.logfp.read(2*ndata+6)
        elif first == '\x10':					# TSIP
            packet = first + second
            delcnt = 0
            while True:
                next = self.logfp.read(1)
                if not next:
                    return ''
                packet += next
                if next == '\x10':
                    delcnt += 1
                elif next == '\x03':
                    if delcnt % 2:
                        break
                else:
                    delcnt = 0
            return packet
        else:
            raise PacketError("unknown packet type, leader %s, (0x%x)" % (first, ord(first)))

class FakeGPS:
    "A fake GPS is a pty with a test log ready to be cycled to it."
    def __init__(self, logfp, speed=4800, verbose=False):
        self.verbose = verbose
        self.go_predicate = lambda: True
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
        speed = baudrates[speed]	# Throw an error if the speed isn't legal
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
        raw[4] = raw[5] = speed
        termios.tcsetattr(ttyfp.fileno(), termios.TCSANOW, raw)
    def slave_is_open(self, pid):
        "Is the slave device of this pty opened by the specified process?"
        if self.verbose:
            sys.stderr.write("slave_is_open() begins")
        isopen = proc_has_file_open(pid, self.slave)
        if self.verbose:
            sys.stderr.write("slave_is_open() ends")
        return isopen
    def __feed(self):
        "Feed the contents of the GPS log to the daemon."
        while self.readers and \
                  self.daemon.is_alive() and \
                  self.go_predicate(self.index, self):
            os.write(self.master_fd, self.testload.sentences[self.index % len(self.testload.sentences)])
            self.index += 1
    def start(self, daemon, thread=False):
        "Increment pseudodevice's reader count, starting it if necessary."
        self.daemon = daemon
        self.readers += 1
        if self.readers == 1:
            self.thread = threading.Thread(target=self.__feed)
            while not self.slave_is_open(daemon.pid):
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

class DaemonError(exceptions.Exception):
    def __init__(self, msg):
        self.msg = msg

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
    def spawn(self, options, port, background=False, prefix=""):
        "Spawn a daemon instance."
        self.spawncmd = "gpsd -N -S %s -F %s -P %s %s" % (port, self.control_socket, self.pidfile, options)
        if prefix:
            self.spawncmd = prefix + " " + self.spawncmd.strip()
        if background:
            self.spawncmd += " &"
        status = os.system(self.spawncmd)
        if os.WIFSIGNALED(status) or os.WEXITSTATUS(status):
            raise DaemonError("daemon exited with status %d" % status)
    def wait_pid(self):
        "Wait for the daemon, get its PID and a control-socket connection."
        while True:
            try:
                fp = open(self.pidfile)
            except IOError:
                time.sleep(1)
                continue
            try:
                pidstr = fp.read()
                self.pid = int(pidstr)
            except ValueError:
                fp.close()
                raise DaemonError("bad pid file, contents %s" % `pidstr`)
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
    def kill(self):
        "Kill the daemon instance."
        if self.pid:
            try:
                os.kill(self.pid, signal.SIGTERM)
            except OSError:
                pass
            self.pid = None
            time.sleep(1)	# Give signal time to land

class TestSessionError(exceptions.Exception):
    def __init__(self, msg):
        self.msg = msg

class TestSession:
    "Manage a session including a daemon with fake GPS and client threads."
    def __init__(self, prefix=None, port=None, options=None, verbose=False):
        "Initialize the test session by launching the daemon."
        self.verbose = verbose
        self.daemon = DaemonInstance()
        self.fakegpslist = {}
        self.clients = []
        self.client_id = 0
        if port:
            self.port = port
        else:
            self.port = gps.GPSD_PORT
        self.reporter = lambda x: None
        self.progress = lambda x: None
        for sig in (signal.SIGQUIT, signal.SIGINT, signal.SIGTERM):
            signal.signal(sig, lambda signal, frame: self.killall())
        self.daemon.spawn(background=True, prefix=prefix, port=self.port, options=options)
        self.daemon.wait_pid()
        self.default_predicate = None
        self.fd_set = []
        self.sanity_check()
    def set_predicate(self, pred):
        "Set a default go predicate for the session."
        self.default_predicate = pred
    def sanity_check(self):
        try:
            now = proc_fd_set(self.daemon.pid)
            if now != self.fd_set:
                self.progress("File descriptors: %s\n" % now)
                self.fd_set = now
        except:
            self.progress("Sanity check not working -- port fd_set()\n")
    def gps_add(self, logfile, speed=4800, pred=None):
        "Add a simulated GPS being fed by the specified logfile."
        self.progress("gpsfake: gps_add(%s, %d)\n" % (logfile, speed))
        if logfile not in self.fakegpslist:
            newgps = FakeGPS(logfile, speed=speed, verbose=self.verbose)
            if pred:
                newgps.go_predicate = pred
            elif self.default_predicate:
                newgps.go_predicate = self.default_predicate
            self.fakegpslist[newgps.slave] = newgps
        self.daemon.add_device(newgps.slave)
        self.fakegpslist[newgps.slave].start(self.daemon, thread=True)
        self.sanity_check()
        return newgps.slave
    def gps_remove(self, name):
        "Remove a simulated GPS from the daeon's search list."
        self.progress("gpsfake: gps_remove(%s)\n" % name)
        self.fakegpslist[name].stop()
        self.daemon.remove_device(name)
        self.sanity_check()
    def client_add(self, commands):
        "Initiate a client session and force connection to a fake GPS."
        self.progress("gpsfake: client_add()\n")
        newclient = gps.gps(port=self.port)
        self.clients.append(newclient)
        newclient.id = self.client_id + 1 
        self.client_id += 1
        self.progress("gpsfake: client %d has %s\n" % (self.client_id,newclient.device))
        newclient.set_thread_hook(lambda x: self.reporter(x))
        if commands:
            newclient.query(commands)
        self.sanity_check()
        return newclient.id
    def client_query(self, id, commands):
        "Ship a command down a client channel, accept a response."
        self.progress("gpsfake: client_query(%d, %s)\n" % (id, `commands`))
        for client in self.clients:
            if client.id == id:
                client.query(commands)
                return client.response
        self.sanity_check()
        return None
    def client_remove(self, id):
        "Terminate a client session."
        self.progress("gpsfake: client_remove(%d)\n" % id)
        for client in self.clients:
            if client.id == id:
                self.fakegpslist[client.device].release()
                self.clients.remove(client)
                del client
                self.sanity_check()
                return True
        else:
            self.sanity_check()
            return False
    def wait(self, seconds):
        "Wait, doing nothing."
        self.progress("gpsfake: wait(%d)\n" % seconds)
        time.sleep(seconds)
        self.sanity_check()
    def gather(self, seconds):
        "Wait, doing nothing but watching for sentences."
        self.progress("gpsfake: gather(%d)\n" % seconds)
        #mark = time.time()
        time.sleep(seconds)
        #if self.timings.c_recv_time <= mark:
        #    TestSessionError("no sentences received\n")
        self.sanity_check()
    def gps_count(self):
        "Return the number of GPSes active in this session"
        tc = 0
        for fakegps in self.fakegpslist.values():
            if fakegps.thread and fakegps.thread.isAlive():
                tc += 1
        return tc
    def cleanup(self):
        "Wait for all threads to end and kill the daemon."
        self.progress("gpsfake: cleanup()\n")
        while self.gps_count():
            time.sleep(0.1)
        self.daemon.kill()
    def killall(self):
        "Kill all fake-GPS threads and the daemon."
        self.progress("gpsfake: killall()\n")
        for fakegps in self.fakegpslist.values():
            if fakegps.thread and fakegps.thread.isAlive():
                fakegps.stop()
        self.daemon.kill()

# End
