/*
 * This is the main sequence of the gpsd daemon. The IO dispatcher, main
 * select loop, and user command handling lives here.
 *
 * This file is Copyright (c) 2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 */

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>		/* for select() */
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <ctype.h>
#include <setjmp.h>
#include <assert.h>
#include <pwd.h>
#include <grp.h>
#include <math.h>
#include <syslog.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#ifndef S_SPLINT_S
#include <netdb.h>
#ifndef AF_UNSPEC
#include <sys/socket.h>
#endif /* AF_UNSPEC */
#ifndef INADDR_ANY
#include <netinet/in.h>
#endif /* INADDR_ANY */
#include <sys/un.h>
#include <arpa/inet.h>     /* for htons() and friends */
#endif /* S_SPLINT_S */
#ifndef S_SPLINT_S
#include <unistd.h>
#endif /* S_SPLINT_S */

#include "gpsd_config.h"

#ifdef DBUS_EXPORT_ENABLE
#include "gpsd_dbus.h"
#endif

#include "gpsd.h"
#include "sockaddr.h"
#include "gps_json.h"
#include "revision.h"

/*
 * The name of a tty device from which to pick up whatever the local
 * owning group for tty devices is.  Used when we drop privileges.
 */
#if defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
#define PROTO_TTY "/dev/tty00"	/* correct for *BSD */
#else
#define PROTO_TTY "/dev/ttyS0"	/* correct for Linux */
#endif

/* Name of (unprivileged) user to change to when we drop privileges. */
#ifndef GPSD_USER
#define GPSD_USER	"nobody"
#endif

/*
 * Timeout policy.  We can't rely on clients closing connections
 * correctly, so we need timeouts to tell us when it's OK to
 * reclaim client fds.  COMMAND_TIMEOUT fends off programs
 * that open connections and just sit there, not issuing a WATCH or
 * doing anything else that triggers a device assignment.  Clients
 * in watcher or raw mode that don't read their data will get dropped
 * when throttled_write() fills up the outbound buffers and the
 * NOREAD_TIMEOUT expires.
 *
 * RELEASE_TIMEOUT sets the amount of time we hold a device
 * open after the last subscriber closes it; this is nonzero so a
 * client that does open/query/close will have time to come back and
 * do another single-shot query, if it wants to, before the device is
 * actually closed.  The reason this matters is because some Bluetooth
 * GPSes not only shut down the GPS receiver on close to save battery
 * power, they actually shut down the Bluetooth RF stage as well and
 * only re-wake it periodically to see if an attempt to raise the
 * device is in progress.  The result is that if you close the device
 * when it's powered up, a re-open can fail with EIO and needs to be
 * tried repeatedly.  Better to avoid this...
 *
 * DEVICE_REAWAKE says how long to wait before repolling after a zero-length
 * read. It's there so we avoid spinning forever on an EOF condition.
 */
#define COMMAND_TIMEOUT		60*15
#define NOREAD_TIMEOUT		60*3
#define RELEASE_TIMEOUT		60
#define DEVICE_REAWAKE		0.01

#define QLEN			5

/*
 * If ntpshm is enabled, we renice the process to this priority level.
 * For precise timekeeping increase priority.
 */
#define NICEVAL	-10

/* Needed because 4.x versions of GCC are really annoying */
#define ignore_return(funcall)	assert(funcall != -23)

/* IP version used by the program */
/* AF_UNSPEC: all
 * AF_INET: IPv4 only
 * AF_INET6: IPv6 only
 */
#ifdef IPV6_ENABLE
static const int af = AF_UNSPEC;
#else
static const int af = AF_INET;
#endif

#define AFCOUNT 2

static fd_set all_fds;
static int maxfd;
static int debuglevel;
static bool in_background = false;
static bool listen_global = false;
static bool nowait = false;
static jmp_buf restartbuf;
static struct gps_context_t context;

static volatile sig_atomic_t signalled;

static void onsig(int sig)
{
    /* just set a variable, and deal with it in the main loop */
    signalled = (sig_atomic_t) sig;
}

#if defined(PPS_ENABLE)
static pthread_mutex_t report_mutex;
#endif /* PPS_ENABLE */

static void visibilize(/*@out@*/char *buf2, size_t len, const char *buf)
{
    const char *sp;

    buf2[0] = '\0';
    for (sp = buf; *sp != '\0' && strlen(buf2)+4 < len; sp++)
	if (isprint(*sp) || (sp[0] == '\n' && sp[1] == '\0')
	  || (sp[0] == '\r' && sp[2] == '\0'))
	    (void)snprintf(buf2 + strlen(buf2), 2, "%c", *sp);
	else
	    (void)snprintf(buf2 + strlen(buf2), 6, "\\x%02x",
			   0x00ff & (unsigned)*sp);
}

void gpsd_report(int errlevel, const char *fmt, ...)
/* assemble command in printf(3) style, use stderr or syslog */
{
#ifndef SQUELCH_ENABLE
    if (errlevel <= debuglevel) {
	char buf[BUFSIZ], buf2[BUFSIZ];
	char *err_str;
	va_list ap;

#if defined(PPS_ENABLE)
	/*@ -unrecog  (splint has no pthread declarations as yet) @*/
	(void)pthread_mutex_lock(&report_mutex);
	/* +unrecog */
#endif /* PPS_ENABLE */
	switch ( errlevel ) {
	case LOG_ERROR:
		err_str = "ERROR: ";
		break;
	case LOG_SHOUT:
		err_str = "SHOUT: ";
		break;
	case LOG_WARN:
		err_str = "WARN: ";
		break;
	case LOG_INF:
		err_str = "INFO: ";
		break;
	case LOG_DATA:
		err_str = "DATA: ";
		break;
	case LOG_PROG:
		err_str = "PROG: ";
		break;
	case LOG_IO:
		err_str = "IO: ";
		break;
	case LOG_SPIN:
		err_str = "SPIN: ";
		break;
	case LOG_RAW:
		err_str = "RAW: ";
		break;
	default:
		err_str = "UNK: ";
	}

	(void)strlcpy(buf, "gpsd:", BUFSIZ);
	(void)strncat(buf, err_str, BUFSIZ - strlen(buf) );
	va_start(ap, fmt);
	(void)vsnprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), fmt,
			ap);
	va_end(ap);

	visibilize(buf2, sizeof(buf2), buf);

	if (in_background)
	    syslog((errlevel == 0) ? LOG_ERR : LOG_NOTICE, "%s", buf2);
	else
	    (void)fputs(buf2, stderr);
#if defined(PPS_ENABLE)
	/*@ -unrecog (splint has no pthread declarations as yet) @*/
	(void)pthread_mutex_unlock(&report_mutex);
	/* +unrecog */
#endif /* PPS_ENABLE */
    }
#endif /* !SQUELCH_ENABLE */
}

static void usage(void)
{
    const struct gps_type_t **dp;

    (void)printf("usage: gpsd [-b] [-n] [-N] [-D n] [-F sockfile] [-G] [-P pidfile] [-S port] [-h] device...\n\
  Options include: \n\
  -b		     	    = bluetooth-safe: open data sources read-only\n\
  -n			    = don't wait for client connects to poll GPS\n\
  -N			    = don't go into background\n\
  -F sockfile		    = specify control socket location\n\
  -G         		    = make gpsd listen on INADDR_ANY\n\
  -P pidfile	      	    = set file to record process ID \n\
  -D integer (default 0)    = set debug level \n\
  -S integer (default %s) = set port for daemon \n\
  -h		     	    = help message \n\
  -V			    = emit version and exit.\n\
A device may be a local serial device for GPS input, or a URL of the form:\n\
     {dgpsip|ntrip}://[user:passwd@]host[:port][/stream]\n\
     gpsd://host[:port][/device][?protocol]\n\
in which case it specifies an input source for GPSD, DGPS or ntrip data.\n\
\n\
The following driver types are compiled into this gpsd instance:\n",
		 DEFAULT_GPSD_PORT);
    for (dp = gpsd_drivers; *dp; dp++) {
	(void)printf("    %s\n", (*dp)->type_name);
    }
}

#ifdef CONTROL_SOCKET_ENABLE
static int filesock(char *filename)
{
    struct sockaddr_un addr;
    int sock;

    /*@ -mayaliasunique -usedef @*/
    if ((sock = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
	gpsd_report(LOG_ERROR, "Can't create device-control socket\n");
	return -1;
    }
    (void)strlcpy(addr.sun_path, filename, sizeof(addr.sun_path));
    addr.sun_family = (sa_family_t)AF_UNIX;
    (void)bind(sock, (struct sockaddr *)&addr, (int)sizeof(addr));
    if (listen(sock, QLEN) == -1) {
	gpsd_report(LOG_ERROR, "can't listen on local socket %s\n", filename);
	return -1;
    }
    /*@ +mayaliasunique +usedef @*/
    return sock;
}
#endif /* CONTROL_SOCKET_ENABLE */

/*
 * This hackery is intended to support SBCs that are resource-limited
 * and only need to support one or a few devices each.  It avoids the
 * space overhead of allocating thousands of unused device structures.
 * This array fills from the bottom, so as an extreme case you could
 * reduce LIMITED_MAX_DEVICES to 1.
 */
#ifdef LIMITED_MAX_DEVICES
#define MAXDEVICES	LIMITED_MAX_DEVICES
#else
/* we used to make this FD_SETSIZE, but that cost 14MB of wasted core! */
#define MAXDEVICES	4
#endif

#define sub_index(s) (int)((s) - subscribers)
#define allocated_device(devp)	 ((devp)->gpsdata.dev.path[0] != '\0')
#define free_device(devp)	 (devp)->gpsdata.dev.path[0] = '\0'
#define initialized_device(devp) ((devp)->context != NULL)

static struct gps_device_t devices[MAXDEVICES];

static void adjust_max_fd(int fd, bool on)
/* track the largest fd currently in use */
{
    if (on) {
	if (fd > maxfd)
	    maxfd = fd;
    }
#if !defined(LIMITED_MAX_DEVICES) && !defined(LIMITED_MAX_CLIENT_FD)
    /*
     * I suspect there could be some weird interactions here if
     * either of these were set lower than FD_SETSIZE.  We'll avoid
     * potential bugs by not scavenging in this case at all -- should
     * be OK, as the use case for limiting is SBCs where the limits
     * will be very low (typically 1) and the maximum size of fd
     * set to scan through correspondingly small.
     */
    else {
	if (fd == maxfd) {
	    int tfd;

	    for (maxfd = tfd = 0; tfd < FD_SETSIZE; tfd++)
		if (FD_ISSET(tfd, &all_fds))
		    maxfd = tfd;
	}
    }
#endif /* !defined(LIMITED_MAX_DEVICES) && !defined(LIMITED_MAX_CLIENT_FD) */
}

#ifdef SOCKET_EXPORT_ENABLE
static int passivesock_af(int af, char *service, char *tcp_or_udp, int qlen)
/* bind a passive command socket for the daemon */
{
    /*
     * af = address family,
     * service = IANA protocol name or number.
     * tcp_or_udp = TCP or UDP
     * qlen = maximum wait-queue length for connections
     */
    struct servent *pse;
    struct protoent *ppe;	/* splint has a bug here */
    sockaddr_t sat;
    int sin_len = 0;
    int s = -1, type, proto, one = 1;
    in_port_t port;
    char *af_str = "";

    if ((pse = getservbyname(service, tcp_or_udp)))
	port = ntohs((in_port_t) pse->s_port);
    else if ((port = (in_port_t) atoi(service)) == 0) {
	gpsd_report(LOG_ERROR, "can't get \"%s\" service entry.\n", service);
	return -1;
    }
    ppe = getprotobyname(tcp_or_udp);
    if (strcmp(tcp_or_udp, "udp") == 0) {
	type = SOCK_DGRAM;
	/*@i@*/ proto = (ppe) ? ppe->p_proto : IPPROTO_UDP;
    } else {
	type = SOCK_STREAM;
	/*@i@*/ proto = (ppe) ? ppe->p_proto : IPPROTO_TCP;
    }

    /*@ -mustfreefresh +matchanyintegral @*/
    switch (af) {
    case AF_INET:
	sin_len = sizeof(sat.sa_in);

	memset((char *)&sat.sa_in, 0, sin_len);
	sat.sa_in.sin_family = (sa_family_t) AF_INET;
	if (listen_global)
	    sat.sa_in.sin_addr.s_addr = htonl(INADDR_ANY);
	else
	    sat.sa_in.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	sat.sa_in.sin_port = htons(port);

	af_str = "IPv4";
	/* see PF_INET6 case below */
	s = socket(PF_INET, type, proto);
	break;
#ifdef IPV6_ENABLE
    case AF_INET6:
	sin_len = sizeof(sat.sa_in6);

	memset((char *)&sat.sa_in6, 0, sin_len);
	sat.sa_in6.sin6_family = (sa_family_t) AF_INET6;
	if (listen_global) {
	    /* BAD:  sat.sa_in6.sin6_addr = in6addr_any;
	     * the simple assignment will not work (except as an initializer)
	     * because sin6_addr is an array not a simple type
	     * we could do something like this:
	     * memcpy(sat.sa_in6.sin6_addr, in6addr_any, sizeof(sin6_addr));
	     * BUT, all zeros is IPv6 wildcard, and we just zeroed the array
	     * so really nothing to do here
	     */
	} else
	    sat.sa_in6.sin6_addr = in6addr_loopback;
	sat.sa_in6.sin6_port = htons(port);

	/*
	 * Traditionally BSD uses "communication domains", named by
	 * constants starting with PF_ as the first argument for
	 * select.  In practice PF_INET has the same value as AF_INET
	 * (on BSD and Linux, and probably everywhere else).  POSIX
	 * leaves much of this unspecified, but requires that AF_INET
	 * be recognized.  We follow tradition here.
	 */
	af_str = "IPv6";
	s = socket(PF_INET6, type, proto);
	break;
#endif
    default:
	gpsd_report(LOG_ERROR, "unhandled address family %d\n", af);
	return -1;
    }
    gpsd_report(LOG_IO, "opening %s socket\n", af_str);

    if (s == -1) {
	gpsd_report(LOG_ERROR, "can't create %s socket\n", af_str);
	return -1;
    }
    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *)&one,
		   (int)sizeof(one)) == -1) {
	gpsd_report(LOG_ERROR, "Error: SETSOCKOPT SO_REUSEADDR\n");
	return -1;
    }
    if (bind(s, &sat.sa, sin_len) < 0) {
	gpsd_report(LOG_ERROR, "can't bind to %s port %s, %s\n", af_str,
		    service, strerror(errno));
	if (errno == EADDRINUSE) {
	    gpsd_report(LOG_ERROR, "maybe gpsd is already running!\n");
	}
	return -1;
    }
    if (type == SOCK_STREAM && listen(s, qlen) == -1) {
	gpsd_report(LOG_ERROR, "can't listen on port %s\n", service);
	return -1;
    }

    gpsd_report(LOG_SPIN, "passivesock_af() -> %d\n", s);
    return s;
    /*@ +mustfreefresh -matchanyintegral @*/
}

/* *INDENT-OFF* */
static int passivesocks(char *service, char *tcp_or_udp,
			int qlen, /*@out@*/int socks[])
{
    int numsocks = AFCOUNT;
    int i;

    for (i = 0; i < AFCOUNT; i++)
	socks[i] = -1;

    if (AF_UNSPEC == af || (AF_INET == af))
	socks[0] = passivesock_af(AF_INET, service, tcp_or_udp, qlen);

    if (AF_UNSPEC == af || (AF_INET6 == af))
	socks[1] = passivesock_af(AF_INET6, service, tcp_or_udp, qlen);

    for (i = 0; i < AFCOUNT; i++)
	if (socks[i] < 0)
	    numsocks--;

    /* Return the number of succesfully opened sockets
     * The failed ones are identified by negative values */
    return numsocks;
}
/* *INDENT-ON* */

struct subscriber_t
{
    int fd;			/* client file descriptor. -1 if unused */
    timestamp_t active;		/* when subscriber last polled for data */
    struct policy_t policy;	/* configurable bits */
};

#ifdef LIMITED_MAX_CLIENTS
#define MAXSUBSCRIBERS LIMITED_MAX_CLIENTS
#else
/* subscriber structure is small enough that there's no need to limit this */
#define MAXSUBSCRIBERS	FD_SETSIZE
#endif

#define subscribed(sub, devp)    (sub->policy.devpath[0]=='\0' || strcmp(sub->policy.devpath, devp->gpsdata.dev.path)==0)

static struct subscriber_t subscribers[MAXSUBSCRIBERS];	/* indexed by client file descriptor */

#define UNALLOCATED_FD	-1

static /*@null@*//*@observer@ */ struct subscriber_t *allocate_client(void)
/* return the address of a subscriber structure allocated for a new session */
{
    int si;

#if UNALLOCATED_FD == 0
#error client allocation code will fail horribly
#endif
    for (si = 0; si < NITEMS(subscribers); si++) {
	if (subscribers[si].fd == UNALLOCATED_FD) {
	    subscribers[si].fd = 0;	/* mark subscriber as allocated */
	    return &subscribers[si];
	}
    }
    return NULL;
}

static void detach_client(struct subscriber_t *sub)
/* detach a client and terminate the session */
{
    char *c_ip;
    if (sub->fd == UNALLOCATED_FD)
	return;
    c_ip = netlib_sock2ip(sub->fd);
    (void)shutdown(sub->fd, SHUT_RDWR);
    gpsd_report(LOG_SPIN, "close(%d) in detach_client()\n", sub->fd);
    (void)close(sub->fd);
    gpsd_report(LOG_INF, "detaching %s (sub %d, fd %d) in detach_client\n",
		c_ip, sub_index(sub), sub->fd);
    FD_CLR(sub->fd, &all_fds);
    adjust_max_fd(sub->fd, false);
    sub->active = (timestamp_t)0;
    sub->policy.watcher = false;
    sub->policy.json = false;
    sub->policy.nmea = false;
    sub->policy.raw = 0;
    sub->policy.scaled = false;
    sub->policy.timing = false;
    sub->policy.devpath[0] = '\0';
    sub->fd = UNALLOCATED_FD;
    /*@+mustfreeonly@*/
}

static ssize_t throttled_write(struct subscriber_t *sub, char *buf,
			       size_t len)
/* write to client -- throttle if it's gone or we're close to buffer overrun */
{
    ssize_t status;

    if (debuglevel >= 3) {
	if (isprint(buf[0]))
	    gpsd_report(LOG_IO, "=> client(%d): %s\n", sub_index(sub), buf);
	else {
	    char *cp, buf2[MAX_PACKET_LENGTH * 3];
	    buf2[0] = '\0';
	    for (cp = buf; cp < buf + len; cp++)
		(void)snprintf(buf2 + strlen(buf2),
			       sizeof(buf2) - strlen(buf2),
			       "%02x", (unsigned int)(*cp & 0xff));
	    gpsd_report(LOG_IO, "=> client(%d): =%s\n", sub_index(sub),
			buf2);
	}
    }

    status = send(sub->fd, buf, len, 0);
    if (status == (ssize_t) len)
	return status;
    else if (status > -1) {
	gpsd_report(LOG_INF, "short write disconnecting client(%d)\n",
		    sub_index(sub));
	detach_client(sub);
	return 0;
    } else if (errno == EAGAIN || errno == EINTR)
	return 0;		/* no data written, and errno says to retry */
    else if (errno == EBADF)
	gpsd_report(LOG_WARN, "client(%d) has vanished.\n", sub_index(sub));
    else if (errno == EWOULDBLOCK
	     && timestamp() - sub->active > NOREAD_TIMEOUT)
	gpsd_report(LOG_INF, "client(%d) timed out.\n", sub_index(sub));
    else
	gpsd_report(LOG_INF, "client(%d) write: %s\n", sub_index(sub),
		    strerror(errno));
    detach_client(sub);
    return status;
}

static void notify_watchers(struct gps_device_t *device, const char *sentence, ...)
/* notify all clients watching a given device of an event */
{
    va_list ap;
    char buf[BUFSIZ];
    struct subscriber_t *sub;

    va_start(ap, sentence);
    (void)vsnprintf(buf, sizeof(buf), sentence, ap);
    va_end(ap);

    for (sub = subscribers; sub < subscribers + MAXSUBSCRIBERS; sub++)
	if (sub->active != 0 && subscribed(sub, device))
	    (void)throttled_write(sub, buf, strlen(buf));
}
#endif /* SOCKET_EXPORT_ENABLE */

static void deactivate_device(struct gps_device_t *device)
/* deactivate device, but leave it in the pool (do not free it) */
{
#ifdef SOCKET_EXPORT_ENABLE
    notify_watchers(device,
		    "{\"class\":\"DEVICE\",\"path\":\"%s\",\"activated\":0}\r\n",
		    device->gpsdata.dev.path);
#endif /* SOCKET_EXPORT_ENABLE */
    if (device->gpsdata.gps_fd != -1) {
	FD_CLR(device->gpsdata.gps_fd, &all_fds);
	adjust_max_fd(device->gpsdata.gps_fd, false);
#ifdef NTPSHM_ENABLE
	ntpd_link_deactivate(device);
#endif /* NTPSHM_ENABLE */
	gpsd_deactivate(device);
    }
}

/* *INDENT-OFF* */
/*@null@*//*@observer@*/ static struct gps_device_t *find_device(/*@null@*/const char
								 *device_name)
/* find the device block for an existing device name */
{
    struct gps_device_t *devp;

    for (devp = devices; devp < devices + MAXDEVICES; devp++)
    {
        if (allocated_device(devp) && NULL != device_name &&
            strcmp(devp->gpsdata.dev.path, device_name) == 0)
            return devp;
    }
    return NULL;
}
/* *INDENT-ON* */

static bool open_device( /*@null@*/struct gps_device_t *device)
{
    if (NULL == device || gpsd_activate(device) < 0) {
	return false;
    }
    gpsd_report(LOG_INF, "device %s activated\n",
		device->gpsdata.dev.path);
    FD_SET(device->gpsdata.gps_fd, &all_fds);
    adjust_max_fd(device->gpsdata.gps_fd, true);
    return true;
}

static bool add_device(const char *device_name)
/* add a device to the pool; open it right away if in nowait mode */
{
    struct gps_device_t *devp;
    bool ret = false;
    /* stash devicename away for probing when the first client connects */
    for (devp = devices; devp < devices + MAXDEVICES; devp++)
	if (!allocated_device(devp)) {
	    gpsd_init(devp, &context, device_name);
#ifdef NTPSHM_ENABLE
	    /*
	     * Now is the right time to grab the shared memory segment(s)
	     * to communicate the navigation message derived and (possibly)
	     * 1pps derived time data to ntpd.
	     */

	    /* do not start more than one ntp thread */
	    if (!(devp->shmindex >= 0))
		ntpd_link_activate(devp);

	    gpsd_report(LOG_INF, "NTPD ntpd_link_activate: %d\n",
			(int)devp->shmindex >= 0);

#endif /* NTPSHM_ENABLE */
	    gpsd_report(LOG_INF, "stashing device %s at slot %d\n",
			device_name, (int)(devp - devices));
	    if (nowait) {
		ret = open_device(devp);
	    } else {
		devp->gpsdata.gps_fd = -1;
		ret = true;
	    }
#ifdef SOCKET_EXPORT_ENABLE
	    notify_watchers(devp,
			    "{\"class\":\"DEVICE\",\"path\":\"%s\",\"activated\":%lf}\r\n",
			    devp->gpsdata.dev.path, timestamp());
#endif /* SOCKET_EXPORT_ENABLE */
	    break;
	}
    return ret;
}

#ifdef CONTROL_SOCKET_ENABLE
/*@ observer @*/ static char *snarfline(char *p, /*@out@*/ char **out)
/* copy the rest of the command line, before CR-LF */
{
    char *q;
    static char stash[BUFSIZ];

    /*@ -temptrans -mayaliasunique @*/
    for (q = p; isprint(*p) && !isspace(*p) && /*@i@*/ (p - q < BUFSIZ - 1);
	 p++)
	continue;
    (void)memcpy(stash, q, (size_t) (p - q));
    stash[p - q] = '\0';
    *out = stash;
    return p;
    /*@ +temptrans +mayaliasunique @*/
}

static void handle_control(int sfd, char *buf)
/* handle privileged commands coming through the control socket */
{
    char *stash, *eq;
    struct gps_device_t *devp;

    /*@ -sefparams @*/
    if (buf[0] == '-') {
	(void)snarfline(buf + 1, &stash);
	gpsd_report(LOG_INF, "<= control(%d): removing %s\n", sfd, stash);
	if ((devp = find_device(stash))) {
	    deactivate_device(devp);
	    free_device(devp);
	    ignore_return(write(sfd, "OK\n", 3));
	} else
	    ignore_return(write(sfd, "ERROR\n", 6));
    } else if (buf[0] == '+') {
	(void)snarfline(buf + 1, &stash);
	if (find_device(stash)) {
	    gpsd_report(LOG_INF, "<= control(%d): %s already active \n", sfd,
			stash);
	    ignore_return(write(sfd, "ERROR\n", 6));
	} else {
	    gpsd_report(LOG_INF, "<= control(%d): adding %s\n", sfd, stash);
	    if (add_device(stash))
		ignore_return(write(sfd, "OK\n", 3));
	    else
		ignore_return(write(sfd, "ERROR\n", 6));
	}
    } else if (buf[0] == '!') {
	(void)snarfline(buf + 1, &stash);
	eq = strchr(stash, '=');
	if (eq == NULL) {
	    gpsd_report(LOG_WARN, "<= control(%d): ill-formed command \n",
			sfd);
	    ignore_return(write(sfd, "ERROR\n", 6));
	} else {
	    *eq++ = '\0';
	    if ((devp = find_device(stash))) {
		if (devp->context->readonly || (devp->sourcetype <= source_blockdev)) {
		    gpsd_report(LOG_WARN, "<= control(%d): attempted to write to a read-only device\n",
				sfd);
		    ignore_return(write(sfd, "ERROR\n", 6));
		} else {
		    gpsd_report(LOG_INF, "<= control(%d): writing to %s \n", sfd,
				stash);
		    if (write(devp->gpsdata.gps_fd, eq, strlen(eq)) <= 0) {
			gpsd_report(LOG_WARN, "<= control(%d): write to device failed\n",
				    sfd);
			ignore_return(write(sfd, "ERROR\n", 6));
		    } else {
			ignore_return(write(sfd, "OK\n", 3));
		    }
		}
	    } else {
		gpsd_report(LOG_INF, "<= control(%d): %s not active \n", sfd,
			    stash);
		ignore_return(write(sfd, "ERROR\n", 6));
	    }
	}
    } else if (buf[0] == '&') {
	(void)snarfline(buf + 1, &stash);
	eq = strchr(stash, '=');
	if (eq == NULL) {
	    gpsd_report(LOG_WARN, "<= control(%d): ill-formed command\n",
			sfd);
	    ignore_return(write(sfd, "ERROR\n", 6));
	} else {
	    size_t len;
	    int st;
	    *eq++ = '\0';
	    len = strlen(eq) + 5;
	    if ((devp = find_device(stash)) != NULL) {
		if (devp->context->readonly || (devp->sourcetype <= source_blockdev)) {
		    gpsd_report(LOG_WARN, "<= control(%d): attempted to write to a read-only device\n",
				sfd);
		    ignore_return(write(sfd, "ERROR\n", 6));
                } else {
                    /* NOTE: this destroys the original buffer contents */
                    st = gpsd_hexpack(eq, eq, len);
                    if (st <= 0) {
                        gpsd_report(LOG_INF,
                                    "<= control(%d): invalid hex string (error %d).\n",
                                    sfd, st);
                        ignore_return(write(sfd, "ERROR\n", 6));
                    } else {
                        gpsd_report(LOG_INF,
                                    "<= control(%d): writing %d bytes fromhex(%s) to %s\n",
                                    sfd, st, eq, stash);
                        if (write(devp->gpsdata.gps_fd, eq, (size_t) st) <= 0) {
                            gpsd_report(LOG_WARN, "<= control(%d): write to device failed\n",
                                        sfd);
                            ignore_return(write(sfd, "ERROR\n", 6));
                        } else {
                            ignore_return(write(sfd, "OK\n", 3));
                        }
                    }
		}
	    } else {
		gpsd_report(LOG_INF, "<= control(%d): %s not active\n", sfd,
			    stash);
		ignore_return(write(sfd, "ERROR\n", 6));
	    }
	}
    } else if (strcmp(buf, "?devices")==0) {
	for (devp = devices; devp < devices + MAXDEVICES; devp++) {
	    char *path = devp->gpsdata.dev.path;
	    ignore_return(write(sfd, path, strlen(path)));
	    ignore_return(write(sfd, "\n", 1));
	}
	ignore_return(write(sfd, "OK\n", 6));
    } else {
	/* unknown command */
	ignore_return(write(sfd, "ERROR\n", 6));
    }
    /*@ +sefparams @*/
}
#endif /* CONTROL_SOCKET_ENABLE */

#ifdef SOCKET_EXPORT_ENABLE
static bool awaken(struct gps_device_t *device)
/* awaken a device and notify all watchers*/
{
    /* open that device */
    if (!initialized_device(device)) {
	if (!open_device(device)) {
	    gpsd_report(LOG_PROG, "%s: open failed\n",
			device->gpsdata.dev.path);
	    free_device(device);
	    return false;
	}
    }

    if (device->gpsdata.gps_fd != -1) {
	gpsd_report(LOG_PROG,
		    "device %d (fd=%d, path %s) already active.\n",
		    (int)(device - devices),
		    device->gpsdata.gps_fd, device->gpsdata.dev.path);
	return true;
    } else {
	if (gpsd_activate(device) < 0) {
	    gpsd_report(LOG_ERROR, "%s: device activation failed.\n",
			device->gpsdata.dev.path);
	    return false;
	} else {
	    gpsd_report(LOG_RAW,
			"flagging descriptor %d in assign_channel()\n",
			device->gpsdata.gps_fd);
	    FD_SET(device->gpsdata.gps_fd, &all_fds);
	    adjust_max_fd(device->gpsdata.gps_fd, true);
	    return true;
	}
    }
}

#ifdef ALLOW_RECONFIGURE
static bool privileged_user(struct gps_device_t *device)
/* is this channel privileged to change a device's behavior? */
{
    /* grant user privilege if he's the only one listening to the device */
    struct subscriber_t *sub;
    int subcount = 0;
    for (sub = subscribers; sub < subscribers + MAXSUBSCRIBERS; sub++) {
	if (sub->active == 0)
	    continue;
	else if (subscribed(sub, device))
	    subcount++;
    }
    return subcount == 1;
}

static void set_serial(struct gps_device_t *device,
		       speed_t speed, char *modestring)
/* set serial parameters for a device from a speed and modestring */
{
    unsigned int stopbits = device->gpsdata.dev.stopbits;
    char parity = device->gpsdata.dev.parity;
    int wordsize = 8;

    if (strchr("78", *modestring) != NULL) {
	while (isspace(*modestring))
	    modestring++;
	wordsize = (int)(*modestring++ - '0');
	if (strchr("NOE", *modestring) != NULL) {
	    parity = *modestring++;
	    while (isspace(*modestring))
		modestring++;
	    if (strchr("12", *modestring) != NULL)
		stopbits = (unsigned int)(*modestring - '0');
	}
    }

    gpsd_report(LOG_PROG, "set_serial(,%d,%s) %c%d\n", speed, modestring,
		parity, stopbits);
    /* no support for other word sizes yet */
    /* *INDENT-OFF* */
    if (wordsize == (int)(9 - stopbits)
	&& device->device_type->speed_switcher != NULL) {
	if (device->device_type->speed_switcher(device, speed, parity, (int)stopbits)) {
	    /*
	     * Deep black magic is required here. We have to
	     * allow the control string time to register at the
	     * GPS before we do the baud rate switch, which
	     * effectively trashes the UART's buffer.
	     *
	     * This definitely fails below 40 milliseconds on a
	     * BU-303b. 50ms is also verified by Chris Kuethe on
	     *  Pharos iGPS360 + GSW 2.3.1ES + prolific
	     *  Rayming TN-200 + GSW 2.3.1 + ftdi
	     *  Rayming TN-200 + GSW 2.3.2 + ftdi
	     * so it looks pretty solid.
	     *
	     * The minimum delay time is probably constant
	     * across any given type of UART.
	     */
	    (void)tcdrain(device->gpsdata.gps_fd);
	    (void)usleep(50000);
	    gpsd_set_speed(device, speed, parity, stopbits);
	}
    }
    /* *INDENT-ON* */
}
#endif /* ALLOW_RECONFIGURE */

static void json_devicelist_dump(char *reply, size_t replylen)
{
    struct gps_device_t *devp;
    (void)strlcpy(reply, "{\"class\":\"DEVICES\",\"devices\":[", replylen);
    for (devp = devices; devp < devices + MAXDEVICES; devp++)
	if (allocated_device(devp)
	    && strlen(reply) + strlen(devp->gpsdata.dev.path) + 3 <
	    replylen - 1) {
	    char *cp;
	    json_device_dump(devp,
			     reply + strlen(reply), replylen - strlen(reply));
	    cp = reply + strlen(reply);
	    *--cp = '\0';
	    *--cp = '\0';
	    (void)strlcat(reply, ",", replylen);
	}

    if (reply[strlen(reply) - 1] == ',')
	reply[strlen(reply) - 1] = '\0';
    (void)strlcat(reply, "]}\r\n", replylen);
}

static void rstrip(char *str)
/* strip trailing \r\n\t\SP from a string */
{
    char *strend;
    strend = str + strlen(str) - 1;
    while (isspace(*strend)) {
	*strend = '\0';
	--strend;
    }
}

static void handle_request(struct subscriber_t *sub,
			   const char *buf, const char **after,
			   char *reply, size_t replylen)
{
    struct gps_device_t *devp;
    const char *end = NULL;

    /*
     * There's a splint limitation that parameters can be declared
     * @out@ or @null@ but not, apparently, both.  This collides with
     * the (admittedly tricky) way we use endptr. The workaround is to
     * declare it @null@ and use -compdef around the JSON reader calls.
     */
    /*@-compdef@*/
    /*
     * See above...
     */
    /*@-nullderef -nullpass@*/

    if (strncmp(buf, "DEVICES;", 8) == 0) {
	buf += 8;
	json_devicelist_dump(reply, replylen);
    } else if (strncmp(buf, "WATCH", 5) == 0
	       && (buf[5] == ';' || buf[5] == '=')) {
	buf += 5;
	if (*buf == ';') {
	    ++buf;
	} else {
	    int status = json_watch_read(buf + 1, &sub->policy, &end);
	    if (end == NULL)
		buf += strlen(buf);
	    else {
		if (*end == ';')
		    ++end;
		buf = end;
	    }
	    if (status != 0) {
		(void)snprintf(reply, replylen,
			       "{\"class\":\"ERROR\",\"message\":\"Invalid WATCH: %s\"}\r\n",
			       json_error_string(status));
		gpsd_report(LOG_ERROR, "response: %s\n", reply);
	    } else if (sub->policy.watcher) {
		if (sub->policy.devpath[0] == '\0') {
		    /* awaken all devices */
		    for (devp = devices; devp < devices + MAXDEVICES; devp++)
			if (allocated_device(devp))
			    (void)awaken(devp);
		} else {
		    devp = find_device(sub->policy.devpath);
		    if (devp == NULL) {
			(void)snprintf(reply, replylen,
				       "{\"class\":\"ERROR\",\"message\":\"No such device as %s\"}\r\n",
				       sub->policy.devpath);
			gpsd_report(LOG_ERROR, "response: %s\n", reply);
			goto bailout;
		    } else if (!awaken(devp)) {
			(void)snprintf(reply, replylen,
				       "{\"class\":\"ERROR\",\"message\":\"Can't assign %s\"}\r\n",
				       sub->policy.devpath);
			gpsd_report(LOG_ERROR, "response: %s\n", reply);
			goto bailout;
		    }
		}
	    }
	}
	/* display a device list and the user's policy */
	json_devicelist_dump(reply + strlen(reply), replylen - strlen(reply));
	json_watch_dump(&sub->policy,
			reply + strlen(reply), replylen - strlen(reply));
    } else if (strncmp(buf, "DEVICE", 6) == 0
	       && (buf[6] == ';' || buf[6] == '=')) {
	struct devconfig_t devconf;
	struct gps_device_t *device;
	buf += 6;
	devconf.path[0] = '\0';	/* initially, no device selection */
	if (*buf == ';') {
	    ++buf;
	} else {
#ifdef ALLOW_RECONFIGURE
	    /* first, select a device to operate on */
	    int status = json_device_read(buf + 1, &devconf, &end);
	    if (end == NULL)
		buf += strlen(buf);
	    else {
		if (*end == ';')
		    ++end;
		buf = end;
	    }
	    device = NULL;
	    /*@-branchstate@*/
	    if (status != 0) {
		(void)snprintf(reply, replylen,
			       "{\"class\":\"ERROR\",\"message\":\"Invalid DEVICE: \"%s\"}\r\n",
			       json_error_string(status));
		gpsd_report(LOG_ERROR, "response: %s\n", reply);
		goto bailout;
	    } else {
		if (devconf.path[0] != '\0') {
		    /* user specified a path, try to assign it */
		    device = find_device(devconf.path);
		    /* do not optimize away, we need 'device' later on! */
		    if (!awaken(device)) {
			(void)snprintf(reply, replylen,
				       "{\"class\":\"ERROR\",\"message\":\"Can't open %s.\"}\r\n",
				       devconf.path);
			gpsd_report(LOG_ERROR, "response: %s\n", reply);
			goto bailout;
		    }
		} else {
		    /* no path specified */
		    int devcount = 0;
		    for (devp = devices; devp < devices + MAXDEVICES; devp++)
			if (allocated_device(devp)) {
			    device = devp;
			    devcount++;
			}
		    if (devcount == 0) {
			(void)strlcat(reply,
				      "{\"class\":\"ERROR\",\"message\":\"Can't perform DEVICE configuration, no devices attached.\"}\r\n",
				      replylen);
			gpsd_report(LOG_ERROR, "response: %s\n", reply);
			goto bailout;
		    } else if (devcount > 1) {
			(void)snprintf(reply + strlen(reply),
				       replylen - strlen(reply),
				       "{\"class\":\"ERROR\",\"message\":\"No path specified in DEVICE, but multiple devices are attached.\"}\r\n");
			gpsd_report(LOG_ERROR, "response: %s\n", reply);
			goto bailout;
		    }
		    /* we should have exactly one device now */
		}
		if (device == NULL)
		    (void)snprintf(reply + strlen(reply),
				   replylen - strlen(reply),
				   "{\"class\":\"ERROR\",\"message\":\"Channel has no device (possible internal error).\"}\r\n");
		else if (!privileged_user(device))
		    (void)snprintf(reply + strlen(reply),
				   replylen - strlen(reply),
				   "{\"class\":\"ERROR\",\"message\":\"Multiple subscribers, cannot change control bits on %s.\"}\r\n",
				   device->gpsdata.dev.path);
		else if (device->device_type == NULL)
		    (void)snprintf(reply + strlen(reply),
				   replylen - strlen(reply),
				   "{\"class\":\"ERROR\",\"message\":\"Type of %s is unknown.\"}\r\n",
				   device->gpsdata.dev.path);
		else {
		    char serialmode[3];
		    const struct gps_type_t *dt = device->device_type;
		    /* interpret defaults */
		    if (devconf.baudrate == DEVDEFAULT_BPS)
			devconf.baudrate =
			    (uint) gpsd_get_speed(&device->ttyset);
		    if (devconf.parity == DEVDEFAULT_PARITY)
			devconf.stopbits = device->gpsdata.dev.stopbits;
		    if (devconf.stopbits == DEVDEFAULT_STOPBITS)
			devconf.stopbits = device->gpsdata.dev.stopbits;
		    if (isnan(devconf.cycle))
			devconf.cycle = device->gpsdata.dev.cycle;

		    /* now that channel is selected, apply changes */
		    if (devconf.driver_mode != device->gpsdata.dev.driver_mode
			&& devconf.driver_mode != DEVDEFAULT_NATIVE
			&& dt->mode_switcher != NULL)
			dt->mode_switcher(device, devconf.driver_mode);

		    serialmode[0] = devconf.parity;
		    serialmode[1] = '0' + devconf.stopbits;
		    serialmode[2] = '\0';
		    set_serial(device,
			       (speed_t) devconf.baudrate, serialmode);
		    if (dt->rate_switcher != NULL
			&& isnan(devconf.cycle) == 0
			&& devconf.cycle >= dt->min_cycle)
			if (dt->rate_switcher(device, devconf.cycle))
			    device->gpsdata.dev.cycle = devconf.cycle;
		}
	    }
	    /*@+branchstate@*/
#else /* ALLOW_RECONFIGURE */
	    (void)snprintf(reply + strlen(reply), replylen - strlen(reply),
			   "{\"class\":\"ERROR\",\"message\":\"Device configuration support not compiled.\"}\r\n");
#endif /* ALLOW_RECONFIGURE */
	}
	/* dump a response for each selected channel */
	for (devp = devices; devp < devices + MAXDEVICES; devp++)
	    if (!allocated_device(devp))
		continue;
	    else if (devconf.path[0] != '\0' && devp != NULL
		     && strcmp(devp->gpsdata.dev.path, devconf.path) != 0)
		continue;
	    else {
		json_device_dump(devp,
				 reply + strlen(reply),
				 replylen - strlen(reply));
	    }
    } else if (strncmp(buf, "POLL;", 5) == 0) {
	char tbuf[JSON_DATE_MAX+1];
	int active = 0;
	buf += 5;
	for (devp = devices; devp < devices + MAXDEVICES; devp++)
	    if (allocated_device(devp) && subscribed(sub, devp))
		if ((devp->observed & GPS_TYPEMASK) != 0)
		    active++;
	(void)snprintf(reply, replylen,
		       "{\"class\":\"POLL\",\"time\":\"%s\",\"active\":%d,\"tpv\":[",
		       unix_to_iso8601(timestamp(), tbuf, sizeof(tbuf)), active);
	for (devp = devices; devp < devices + MAXDEVICES; devp++) {
	    if (allocated_device(devp) && subscribed(sub, devp)) {
		if ((devp->observed & GPS_TYPEMASK) != 0) {
		    json_tpv_dump(&devp->gpsdata,
				  reply + strlen(reply),
				  replylen - strlen(reply));
		    rstrip(reply);
		    (void)strlcat(reply, ",", replylen);
		}
	    }
	}
	if (reply[strlen(reply) - 1] == ',')
	    reply[strlen(reply) - 1] = '\0';	/* trim trailing comma */
	(void)strlcat(reply, "],\"gst\":[", replylen);
	for (devp = devices; devp < devices + MAXDEVICES; devp++) {
	    if (allocated_device(devp) && subscribed(sub, devp)) {
		if ((devp->observed & GPS_TYPEMASK) != 0) {
		    json_noise_dump(&devp->gpsdata,
				  reply + strlen(reply),
				  replylen - strlen(reply));
		    rstrip(reply);
		    (void)strlcat(reply, ",", replylen);
		}
	    }
	}
	if (reply[strlen(reply) - 1] == ',')
	    reply[strlen(reply) - 1] = '\0';	/* trim trailing comma */
	(void)strlcat(reply, "],\"sky\":[", replylen);
	for (devp = devices; devp < devices + MAXDEVICES; devp++) {
	    if (allocated_device(devp) && subscribed(sub, devp)) {
		if ((devp->observed & GPS_TYPEMASK) != 0) {
		    json_sky_dump(&devp->gpsdata,
				  reply + strlen(reply),
				  replylen - strlen(reply));
		    rstrip(reply);
		    (void)strlcat(reply, ",", replylen);
		}
	    }
	}
	if (reply[strlen(reply) - 1] == ',')
	    reply[strlen(reply) - 1] = '\0';	/* trim trailing comma */
	(void)strlcat(reply, "]}\r\n", replylen);
    } else if (strncmp(buf, "VERSION;", 8) == 0) {
	buf += 8;
	json_version_dump(reply, replylen);
    } else {
	const char *errend;
	errend = buf + strlen(buf) - 1;
	while (isspace(*errend) && errend > buf)
	    --errend;
	(void)snprintf(reply, replylen,
		       "{\"class\":\"ERROR\",\"message\":\"Unrecognized request '%.*s'\"}\r\n",
		       (int)(errend - buf), buf);
	gpsd_report(LOG_ERROR, "ERROR response: %s\n", reply);
	buf += strlen(buf);
    }
  bailout:
    *after = buf;
    /*@+nullderef +nullpass@*/
    /*@+compdef@*/
}

static void raw_report(struct subscriber_t *sub, struct gps_device_t *device)
/* report a raw packet to a subscriber */
{
    /* *INDENT-OFF* */
    /*
     * NMEA and other textual sentences are simply
     * copied to all clients that are in raw or nmea
     * mode.
     */
    if (TEXTUAL_PACKET_TYPE(device->packet.type)
	&& (sub->policy.raw > 0 || sub->policy.nmea)) {
	(void)throttled_write(sub,
			      (char *)device->packet.
			      outbuffer,
			      device->packet.outbuflen);
	return;
    }

    /*
     * Also, simply copy if user has specified
     * super-raw mode.
     */
    if (sub->policy.raw > 1) {
	(void)throttled_write(sub,
			      (char *)device->packet.
			      outbuffer,
			      device->packet.outbuflen);
	return;
    }
#ifdef BINARY_ENABLE
    /*
     * Maybe the user wants a binary packet hexdumped.
     */
    if (sub->policy.raw == 1) {
	char *hd =
	    gpsd_hexdump((char *)device->packet.outbuffer,
			 device->packet.outbuflen);
	/*
	 * Ugh...depends on knowing the length of gpsd_hexdump's
	 * buffer.
	 */
	(void)strlcat(hd, "\r\n", MAX_PACKET_LENGTH * 2 + 1);
	(void)throttled_write(sub, hd, strlen(hd));
    }
#endif /* BINARY_ENABLE */
}

static void pseudonmea_report(struct subscriber_t *sub,
			  gps_mask_t changed,
			  struct gps_device_t *device)
/* report pseudo-NMEA in appropriate circumstances */
{
    if (GPS_PACKET_TYPE(device->packet.type)
	&& !TEXTUAL_PACKET_TYPE(device->packet.type)) {
	char buf[MAX_PACKET_LENGTH * 3 + 2];

	gpsd_report(LOG_PROG, "data mask is %s\n",
		    gps_maskdump(device->gpsdata.set));

	if ((changed & REPORT_IS) != 0) {
	    nmea_tpv_dump(device, buf, sizeof(buf));
	    gpsd_report(LOG_IO, "<= GPS (binary tpv) %s: %s\n",
			device->gpsdata.dev.path, buf);
	    (void)throttled_write(sub, buf, strlen(buf));
	}

	if ((changed & SATELLITE_SET) != 0) {
	    nmea_sky_dump(device, buf, sizeof(buf));
	    gpsd_report(LOG_IO, "<= GPS (binary sky) %s: %s\n",
			device->gpsdata.dev.path, buf);
	    (void)throttled_write(sub, buf, strlen(buf));
	}

	if ((changed & SUBFRAME_SET) != 0) {
	    nmea_subframe_dump(device, buf, sizeof(buf));
	    gpsd_report(LOG_IO, "<= GPS (binary subframe) %s: %s\n",
			device->gpsdata.dev.path, buf);
	    (void)throttled_write(sub, buf, strlen(buf));
	}
    }
}
#endif /* SOCKET_EXPORT_ENABLE */

static void consume_packets(struct gps_device_t *device)
/* consume and report packets from a specified device */
{
    gps_mask_t changed;
    int fragments;
#ifdef SOCKET_EXPORT_ENABLE
    struct subscriber_t *sub;
#endif /* SOCKET_EXPORT_ENABLE */

    gpsd_report(LOG_RAW + 1, "polling %d\n",
	    device->gpsdata.gps_fd);

    /*
     * Strange special case - the opening transaction on an NTRIP connection
     * may not yet be completed.  Try to ratchet things forward.
     */
    if (device->servicetype == service_ntrip
	    && device->ntrip.conn_state != ntrip_conn_established) {

	/* the socket descriptor might change during connection */
	if (device->gpsdata.gps_fd != -1) {
	    FD_CLR(device->gpsdata.gps_fd, &all_fds);
	}
	(void)ntrip_open(device, "");
	if (device->ntrip.conn_state == ntrip_conn_err) {
	    gpsd_report(LOG_WARN,
		    "connection to ntrip server failed\n");
	    device->ntrip.conn_state = ntrip_conn_init;
	    deactivate_device(device);
	} else {
	    FD_SET(device->gpsdata.gps_fd, &all_fds);
	}
	return;
    }

    for (fragments = 0; ; fragments++) {
	changed = gpsd_poll(device);

	if (changed == ERROR_SET) {
	    gpsd_report(LOG_WARN,
			"device read of %s returned error or packet sniffer failed sync (flags %s)\n",
			device->gpsdata.dev.path,
			gps_maskdump(changed));
	    deactivate_device(device);
	    break;
	} else if (changed == NODATA_IS) {
	    /*
	     * No data on the first fragment read means the device
	     * fd may have been in an end-of-file condition on select.
	     */
	    if (fragments == 0) {
		gpsd_report(LOG_DATA,
			    "%s returned zero bytes\n",
			    device->gpsdata.dev.path);
		if (device->zerokill) {
		    /* failed timeout-and-reawake, kill it */
		    deactivate_device(device);
		    if (device->ntrip.works) {
			device->ntrip.works = false; // reset so we try this once only
			if (gpsd_activate(device) < 0) {
			    gpsd_report(LOG_WARN, "reconnect to ntrip server failed\n");
			} else {
			    gpsd_report(LOG_INFO, "reconnecting to ntrip server\n");
			    FD_SET(device->gpsdata.gps_fd, &all_fds);
			}
		    }
		} else {
		    /*
		     * Disable listening to this fd for long enough
		     * that the buffer can fill up again.
		     */
		    gpsd_report(LOG_DATA,
				"%s will be repolled in %f seconds\n",
				device->gpsdata.dev.path, DEVICE_REAWAKE);
		    device->reawake = timestamp() + DEVICE_REAWAKE;
		    FD_CLR(device->gpsdata.gps_fd, &all_fds);
		    adjust_max_fd(device->gpsdata.gps_fd, false);
		}
	    }
	    /*
	     * No data on later fragment reads just means the
	     * input buffer is empty.  In this case break out
	     * of the packet-processing loop but don't drop
	     * the device.
	     */
	    break;
	}

	/* we got actual data, head off the reawake special case */
	device->zerokill = false;
	device->reawake = (timestamp_t)0;

	/* must have a full packet to continue */
	if ((changed & PACKET_SET) == 0)
	    break;

	gpsd_report(LOG_DATA,
		    "packet from %s with %s\n",
		    device->gpsdata.dev.path,
		    gps_maskdump(device->gpsdata.set));

#ifdef SOCKET_EXPORT_ENABLE
	/* add any just-identified device to watcher lists */
	if ((changed & DRIVER_IS) != 0) {
	    bool listeners = false;
	    for (sub = subscribers;
		 sub < subscribers + MAXSUBSCRIBERS; sub++)
		if (sub->active != 0
		    && sub->policy.watcher
		    && (sub->policy.devpath[0] == '\0'
			|| strcmp(sub->policy.devpath,
				  device->gpsdata.dev.path) == 0))
		    listeners = true;
	    if (listeners)
		(void)awaken(device);
	}

	/* handle laggy response to a firmware version query */
	if ((changed & (DEVICEID_SET | DRIVER_IS)) != 0) {
	    assert(device->device_type != NULL);
	    {
		char id2[GPS_JSON_RESPONSE_MAX];
		json_device_dump(device, id2, sizeof(id2));
		notify_watchers(device, id2);
	    }
	}
#endif /* SOCKET_EXPORT_ENABLE */

	/*
	 * If the device provided an RTCM packet, stash it
	 * in the context structure for use as a future correction.
	 */
	if ((changed & RTCM2_SET) != 0 || (changed & RTCM3_SET) != 0) {
	    if (device->packet.outbuflen > RTCM_MAX) {
		gpsd_report(LOG_ERROR,
			    "overlong RTCM packet (%zd bytes)\n",
			    device->packet.outbuflen);
	    } else {
		context.rtcmbytes = device->packet.outbuflen;
		memcpy(context.rtcmbuf,
		       device->packet.outbuffer,
		       context.rtcmbytes);
		context.rtcmtime = timestamp();
	    }
	}

#ifdef NTPSHM_ENABLE
	/*
	 * Time is eligible for shipping to NTPD if the driver has
	 * asserted PPSTIME_IS at any point in the current cycle.
	 */
	if ((changed & CLEAR_IS)!=0)
	    device->ship_to_ntpd = false;
	if ((changed & PPSTIME_IS)!=0)
	    device->ship_to_ntpd = true;
	/*
	 * Only update the NTP time if we've seen the leap-seconds data.
	 * Else we may be providing GPS time.
	 */
	if (device->context->enable_ntpshm == 0) {
	    //gpsd_report(LOG_PROG, "NTP: off\n");
	} else if ((changed & TIME_SET) == 0) {
	    //gpsd_report(LOG_PROG, "NTP: No time this packet\n");
	} else if (isnan(device->newdata.time)) {
	    //gpsd_report(LOG_PROG, "NTP: bad new time\n");
	} else if (device->newdata.time == device->last_fixtime) {
	    //gpsd_report(LOG_PROG, "NTP: Not a new time\n");
	} else if (!device->ship_to_ntpd) {
	    //gpsd_report(LOG_PROG, "NTP: No precision time report\n");
	} else {
	    double offset;
	    //gpsd_report(LOG_PROG, "NTP: Got one\n");
	    /* assume zero when there's no offset method */
	    if (device->device_type == NULL
		|| device->device_type->ntp_offset == NULL)
		offset = 0.0;
	    else
		offset = device->device_type->ntp_offset(device);
	    (void)ntpshm_put(device, device->newdata.time, offset);
	    //device->last_fixtime = device->newdata.time;
	}
#endif /* NTPSHM_ENABLE */

	/*
	 * If no reliable end of cycle, must report every time
	 * a sentence changes position or mode. Likely to
	 * cause display jitter.
	 */
	if (!device->cycle_end_reliable && (changed & (LATLON_SET | MODE_SET))!=0)
	    changed |= REPORT_IS;

	/* a few things are not per-subscriber reports */
	if ((changed & REPORT_IS) != 0) {
	    if (device->gpsdata.fix.mode == MODE_3D) {
		struct gps_device_t *dgnss;
		/*
		 * Pass the fix to every potential caster, here.
		 * netgnss_report() individual caster types get to
		 * make filtering decisiona.
		 */
		for (dgnss = devices; dgnss < devices + MAXDEVICES; dgnss++)
		    if (dgnss != device)
			netgnss_report(&context, device, dgnss);
	    }
#ifdef DBUS_EXPORT_ENABLE
	    if (device->gpsdata.fix.mode > MODE_NO_FIX)
		send_dbus_fix(device);
#endif /* DBUS_EXPORT_ENABLE */
	}

#ifdef SHM_EXPORT_ENABLE
	if ((changed & (REPORT_IS|GST_SET|SATELLITE_SET|SUBFRAME_SET|
			ATTITUDE_SET|RTCM2_SET|RTCM3_SET|AIS_SET)) != 0)
	    shm_update(&context, &device->gpsdata);
#endif /* DBUS_EXPORT_ENABLE */

#ifdef SOCKET_EXPORT_ENABLE
	/* update all subscribers associated with this device */
	for (sub = subscribers; sub < subscribers + MAXSUBSCRIBERS; sub++) {
	    /*@-nullderef@*/
	    if (sub == NULL || sub->active == 0 || !subscribed(sub, device))
		continue;

	    /* report raw packets to users subscribed to those */
	    raw_report(sub, device);

	    /* some listeners may be in watcher mode */
	    if (sub->policy.watcher) {
		if (changed & DATA_IS) {
		    gpsd_report(LOG_PROG,
				"Changed mask: %s with %sreliable cycle detection\n",
				gps_maskdump(changed),
				device->cycle_end_reliable ? "" : "un");
		    if ((changed & REPORT_IS) != 0)
			gpsd_report(LOG_PROG, "time to report a fix\n");

		    if (sub->policy.nmea)
			pseudonmea_report(sub, changed, device);

		    if (sub->policy.json)
		    {
			char buf[GPS_JSON_RESPONSE_MAX * 4];

			json_data_report(changed,
					 &device->gpsdata, &sub->policy,
					 buf, sizeof(buf));
			if (buf[0] != '\0')
			    (void)throttled_write(sub, buf, strlen(buf));

#ifdef TIMING_ENABLE
			if (buf[0] != '\0' && sub->policy.timing) {
			    (void)snprintf(buf, sizeof(buf),
					   "{\"class\":\"TIMING\","
					   "\"tag\":\"%s\",\"len\":%d,"
					   "\"xmit\":%lf,\"recv\":%lf,"
					   "\"decode\":%lf,"
					   "\"emit\":%lf}\r\n",
					   device->gpsdata.tag,
					   (int)device->packet.outbuflen,
					   device->d_xmit_time,
					   device->d_recv_time,
					   device->d_decode_time,
					   timestamp());
			    (void)throttled_write(sub, buf, strlen(buf));
			}
#endif /* TIMING_ENABLE */
		    }
		}
	    }
	    /*@+nullderef@*/
	} /* subscribers */
#endif /* SOCKET_EXPORT_ENABLE */
    }
}

#ifdef SOCKET_EXPORT_ENABLE
static int handle_gpsd_request(struct subscriber_t *sub, const char *buf)
/* execute GPSD requests from a buffer */
{
    char reply[GPS_JSON_RESPONSE_MAX + 1];

    reply[0] = '\0';
    if (buf[0] == '?') {
	const char *end;
	for (end = ++buf; *buf != '\0'; buf = end)
	    if (isspace(*buf))
		end = buf + 1;
	    else
		handle_request(sub, buf, &end,
			       reply + strlen(reply),
			       sizeof(reply) - strlen(reply));
    }
    return (int)throttled_write(sub, reply, strlen(reply));
}
#endif /* SOCKET_EXPORT_ENABLE */

#ifdef __UNUSED_AUTOCONNECT__
#define DGPS_THRESHOLD	1600000	/* max. useful dist. from DGPS server (m) */
#define SERVER_SAMPLE	12	/* # of servers within threshold to check */

struct dgps_server_t
{
    double lat, lon;
    char server[257];
    double dist;
};

static int srvcmp(const void *s, const void *t)
{
    return (int)(((const struct dgps_server_t *)s)->dist - ((const struct dgps_server_t *)t)->dist);	/* fixes: warning: cast discards qualifiers from pointer target type */
}

static void netgnss_autoconnect(struct gps_context_t *context,
			double lat, double lon, const char *serverlist)
/* tell the library to talk to the nearest DGPSIP server */
{
    struct dgps_server_t keep[SERVER_SAMPLE], hold, *sp, *tp;
    char buf[BUFSIZ];
    FILE *sfp = fopen(serverlist, "r");

    if (sfp == NULL) {
	gpsd_report(LOG_ERROR, "no DGPS server list found.\n");
	return;
    }

    for (sp = keep; sp < keep + SERVER_SAMPLE; sp++) {
	sp->dist = DGPS_THRESHOLD;
	sp->server[0] = '\0';
    }
    /*@ -usedef @*/
    while (fgets(buf, (int)sizeof(buf), sfp)) {
	char *cp = strchr(buf, '#');
	if (cp != NULL)
	    *cp = '\0';
	if (sscanf(buf, "%lf %lf %256s", &hold.lat, &hold.lon, hold.server) ==
	    3) {
	    hold.dist = earth_distance(lat, lon, hold.lat, hold.lon);
	    tp = NULL;
	    /*
	     * The idea here is to look for a server in the sample array
	     * that is (a) closer than the one we're checking, and (b)
	     * furtherest away of all those that are closer.  Replace it.
	     * In this way we end up with the closest possible set.
	     */
	    for (sp = keep; sp < keep + SERVER_SAMPLE; sp++)
		if (hold.dist < sp->dist
		    && (tp == NULL || hold.dist > tp->dist))
		    tp = sp;
	    if (tp != NULL)
		memcpy(tp, &hold, sizeof(struct dgps_server_t));
	}
    }
    (void)fclose(sfp);

    if (keep[0].server[0] == '\0') {
	gpsd_report(LOG_ERROR, "no DGPS servers within %dm.\n",
		    (int)(DGPS_THRESHOLD / 1000));
	return;
    }
    /*@ +usedef @*/

    /* sort them and try the closest first */
    qsort((void *)keep, SERVER_SAMPLE, sizeof(struct dgps_server_t), srvcmp);
    for (sp = keep; sp < keep + SERVER_SAMPLE; sp++) {
	if (sp->server[0] != '\0') {
	    gpsd_report(LOG_INF, "%s is %dkm away.\n", sp->server,
			(int)(sp->dist / 1000));
	    if (dgpsip_open(context, sp->server) >= 0)
		break;
	}
    }
}
#endif /* __UNUSED_AUTOCONNECT__ */

/*@ -mustfreefresh @*/
int main(int argc, char *argv[])
{
    /* some of these statics suppress -W warnings due to longjmp() */
    static char *pid_file = NULL;
#ifdef SOCKET_EXPORT_ENABLE
    static char *gpsd_service = NULL;	/* this static pacifies splint */
    struct subscriber_t *sub;
#endif /* SOCKET_EXPORT_ENABLE */
    struct gps_device_t *device;
    sockaddr_t fsin;
    fd_set rfds;
    int i, option, msocks[2], dfd;
#ifdef CONTROL_SOCKET_ENABLE
    static int csock = -1;
    fd_set control_fds;
    socket_t cfd;
    static char *control_socket = NULL;
#endif /* CONTROL_SOCKET_ENABLE */
    bool go_background = true;
    struct timeval tv;
    const struct gps_type_t **dp;

#ifdef PPS_ENABLE
    pthread_mutex_init(&report_mutex, NULL);
#endif /* PPS_ENABLE */

    (void)setlocale(LC_NUMERIC, "C");
    debuglevel = 0;
    gpsd_hexdump_level = 0;
    gps_context_init(&context);
    while ((option = getopt(argc, argv, "F:D:S:bGhlNnP:V")) != -1) {
	switch (option) {
	case 'D':
	    debuglevel = (int)strtol(optarg, 0, 0);
	    gpsd_hexdump_level = debuglevel;
#ifdef CLIENTDEBUG_ENABLE
	    gps_enable_debug(debuglevel, stderr);
#endif /* CLIENTDEBUG_ENABLE */
	    break;
#ifdef CONTROL_SOCKET_ENABLE
	case 'F':
	    control_socket = optarg;
	    break;
#endif /* CONTROL_SOCKET_ENABLE */
	case 'N':
	    go_background = false;
	    break;
	case 'b':
	    context.readonly = true;
	    break;
	case 'G':
	    listen_global = true;
	    break;
	case 'l':		/* list known device types and exit */
	    for (dp = gpsd_drivers; *dp; dp++) {
#ifdef ALLOW_RECONFIGURE
		if ((*dp)->mode_switcher != NULL)
		    (void)fputs("n\t", stdout);
		else
		    (void)fputc('\t', stdout);
		if ((*dp)->speed_switcher != NULL)
		    (void)fputs("b\t", stdout);
		else
		    (void)fputc('\t', stdout);
		if ((*dp)->rate_switcher != NULL)
		    (void)fputs("c\t", stdout);
		else
		    (void)fputc('\t', stdout);
#endif /* ALLOW_RECONFIGURE */
		(void)puts((*dp)->type_name);
	    }
	    exit(0);
	case 'S':
#ifdef SOCKET_EXPORT_ENABLE
	    gpsd_service = optarg;
#endif /* SOCKET_EXPORT_ENABLE */
	    break;
	case 'n':
	    nowait = true;
	    break;
	case 'P':
	    pid_file = optarg;
	    break;
	case 'V':
	    (void)printf("gpsd: %s (revision %s)\n", VERSION, REVISION);
	    exit(0);
	case 'h':
	case '?':
	default:
	    usage();
	    exit(0);
	}
    }

#if defined(FIXED_PORT_SPEED) || !defined(SOCKET_EXPORT_ENABLE)
    /*
     * Force nowait in two circumstances:
     *
     * (1) If we're running with FIXED_PORT_SPEED we're some sort
     * of embedded configuration where we don't want to wait for connect
     *
     * (2) Socket export has been disabled.  In this case we have no 
     * way to know when client apps are watching the export channels,
     * so we need to be running all the time.
     */
    nowait = true;
#endif

#ifdef CONTROL_SOCKET_ENABLE
    if (!control_socket && optind >= argc) {
	gpsd_report(LOG_ERROR,
		    "can't run with neither control socket nor devices\n");
	exit(1);
    }

    /*
     * Control socket has to be created before we go background in order to
     * avoid a race condition in which hotplug scripts can try opening
     * the socket before it's created.
     */
    if (control_socket) {
	(void)unlink(control_socket);
	if ((csock = filesock(control_socket)) == -1) {
	    gpsd_report(LOG_ERROR,
			"control socket create failed, netlib error %d\n",
			csock);
	    exit(2);
	} else
	    gpsd_report(LOG_SPIN, "control socket %s is fd %d\n",
			control_socket, csock);
	FD_SET(csock, &all_fds);
	adjust_max_fd(csock, true);
	gpsd_report(LOG_PROG, "control socket opened at %s\n",
		    control_socket);
    }
#else
    if (optind >= argc) {
	gpsd_report(LOG_ERROR,
		    "can't run with no devices specified\n");
	exit(1);
    }
#endif /* CONTROL_SOCKET_ENABLE */

    /* might be time to daemonize */
    if (go_background) {
	/* not SuS/POSIX portable, but we have our own fallback version */
	if (daemon(0, 0) == 0)
	    in_background = true;
        else
	    gpsd_report(LOG_ERROR,"demonization failed: %s\n",strerror(errno));
    }

    if (pid_file) {
	FILE *fp;

	if ((fp = fopen(pid_file, "w")) != NULL) {
	    (void)fprintf(fp, "%u\n", (unsigned int)getpid());
	    (void)fclose(fp);
	} else {
	    gpsd_report(LOG_ERROR, "Cannot create PID file: %s.\n", pid_file);
	}
    }

    openlog("gpsd", LOG_PID, LOG_USER);
    gpsd_report(LOG_INF, "launching (Version %s)\n", VERSION);

#ifdef SOCKET_EXPORT_ENABLE
    /*@ -observertrans @*/
    if (!gpsd_service)
	gpsd_service =
	    getservbyname("gpsd", "tcp") ? "gpsd" : DEFAULT_GPSD_PORT;
    /*@ +observertrans @*/
    if (passivesocks(gpsd_service, "tcp", QLEN, msocks) < 1) {
	gpsd_report(LOG_ERR,
		    "command sockets creation failed, netlib errors %d, %d\n",
		    msocks[0], msocks[1]);
	exit(2);
    }
    gpsd_report(LOG_INF, "listening on port %s\n", gpsd_service);
#endif /* SOCKET_EXPORT_ENABLE */

#ifdef NTPSHM_ENABLE
    if (getuid() == 0) {
	errno = 0;
	// nice() can ONLY succeed when run as root!
	// do not even bother as non-root
	if (nice(NICEVAL) == -1 && errno != 0)
	    gpsd_report(LOG_INF, "NTPD Priority setting failed.\n");
    }
    (void)ntpshm_init(&context, nowait);
#endif /* NTPSHM_ENABLE */

#ifdef DBUS_EXPORT_ENABLE
    /* we need to connect to dbus as root */
    if (initialize_dbus_connection()) {
	/* the connection could not be started */
	gpsd_report(LOG_ERROR, "unable to connect to the DBUS system bus\n");
    } else
	gpsd_report(LOG_PROG,
		    "successfully connected to the DBUS system bus\n");
#endif /* DBUS_EXPORT_ENABLE */

#ifdef SHM_EXPORT_ENABLE
    /* create the shared segment as root so readers can't mess with it */
    if (!shm_acquire(&context)) {
	gpsd_report(LOG_ERROR, "shared-segment creation failed,\n");
    } else
	gpsd_report(LOG_PROG, "shared-segment creation succeeded,\n");
#endif /* DBUS_EXPORT_ENABLE */

    if (getuid() == 0 && go_background) {
	struct passwd *pw;
	struct stat stb;

	/* make default devices accessible even after we drop privileges */
	for (i = optind; i < argc; i++)
	    if (stat(argv[i], &stb) == 0)
		(void)chmod(argv[i], stb.st_mode | S_IRGRP | S_IWGRP);
	/*
	 * Drop privileges.  Up to now we've been running as root.  Instead,
	 * set the user ID to 'nobody' (or whatever the --enable-gpsd-user
	 * is) and the group ID to the owning group of a prototypical TTY
	 * device. This limits the scope of any compromises in the code.
	 * It requires that all GPS devices have their group read/write
	 * permissions set.
	 */
	/*@-type@*/
	if ((optind < argc && stat(argv[optind], &stb) == 0)
	    || stat(PROTO_TTY, &stb) == 0) {
	    gpsd_report(LOG_PROG, "changing to group %d\n", stb.st_gid);
	    if (setgid(stb.st_gid) != 0)
		gpsd_report(LOG_ERROR, "setgid() failed, errno %s\n",
			    strerror(errno));
	}
#ifdef GPSD_GROUP
	else {
	    struct group *grp = getgrnam(GPSD_GROUP);
	    if (grp)
		(void)setgid(grp->gr_gid);
	}
#endif
	pw = getpwnam(GPSD_USER);
	if (pw)
	    (void)setuid(pw->pw_uid);
	/*@+type@*/
    }
    gpsd_report(LOG_INF, "running with effective group ID %d\n", getegid());
    gpsd_report(LOG_INF, "running with effective user ID %d\n", geteuid());

#ifdef SOCKET_EXPORT_ENABLE
    for (i = 0; i < NITEMS(subscribers); i++)
	subscribers[i].fd = UNALLOCATED_FD;
#endif /* SOCKET_EXPORT_ENABLE*/

    /* daemon got termination or interrupt signal */
    if (setjmp(restartbuf) > 0) {
	/* try to undo all device configurations */
	for (dfd = 0; dfd < MAXDEVICES; dfd++) {
	    if (allocated_device(&devices[dfd]))
		(void)gpsd_wrap(&devices[dfd]);
	}
	gpsd_report(LOG_WARN, "gpsd restarted by SIGHUP\n");
    }

    /* Handle some signals */
    signalled = 0;
    (void)signal(SIGHUP, onsig);
    (void)signal(SIGINT, onsig);
    (void)signal(SIGTERM, onsig);
    (void)signal(SIGQUIT, onsig);
    (void)signal(SIGPIPE, SIG_IGN);

    for (i = 0; i < AFCOUNT; i++)
	if (msocks[i] >= 0) {
	    FD_SET(msocks[i], &all_fds);
	    adjust_max_fd(msocks[i], true);
	}
#ifdef CONTROL_SOCKET_ENABLE
    FD_ZERO(&control_fds);
#endif /* CONTROL_SOCKET_ENABLE */

    /* initialize the GPS context's time fields */
    gpsd_time_init(&context, time(NULL));

    for (i = optind; i < argc; i++) {
	if (!add_device(argv[i])) {
	    gpsd_report(LOG_ERROR,
			"GPS device %s open failed\n",
			argv[i]);
	}
    }

    while (0 == signalled) {
	(void)memcpy((char *)&rfds, (char *)&all_fds, sizeof(rfds));

	gpsd_report(LOG_RAW + 2, "select waits\n");
	/*
	 * Poll for user commands or GPS data.  The timeout doesn't
	 * actually matter here since select returns whenever one of
	 * the file descriptors in the set goes ready.  The point
	 * of tracking maxfd is to keep the set of descriptors that
	 * select(2) has to poll here as small as possible (for
	 * low-clock-rate SBCs and the like).
	 */
	/*@ -usedef @*/
	tv.tv_sec = 1;
	tv.tv_usec = 0;
	errno = 0;
	if (select(maxfd + 1, &rfds, NULL, NULL, &tv) == -1) {
	    if (errno == EINTR)
		continue;
	    gpsd_report(LOG_ERROR, "select: %s\n", strerror(errno));
	    exit(2);
	}
	/*@ +usedef @*/

	if (debuglevel >= LOG_SPIN) {
	    char dbuf[BUFSIZ];
	    dbuf[0] = '\0';
	    for (i = 0; i < FD_SETSIZE; i++)
		if (FD_ISSET(i, &all_fds))
		    (void)snprintf(dbuf + strlen(dbuf),
				   sizeof(dbuf) - strlen(dbuf), "%d ", i);
	    if (strlen(dbuf) > 0)
		dbuf[strlen(dbuf) - 1] = '\0';
	    (void)strlcat(dbuf, "} -> {", BUFSIZ);
	    for (i = 0; i < FD_SETSIZE; i++)
		if (FD_ISSET(i, &rfds))
		    (void)snprintf(dbuf + strlen(dbuf),
				   sizeof(dbuf) - strlen(dbuf), " %d ", i);
	    gpsd_report(LOG_SPIN, "select() {%s} at %f (errno %d)\n",
			dbuf, timestamp(), errno);
	}

#ifdef SOCKET_EXPORT_ENABLE
	/* always be open to new client connections */
	for (i = 0; i < AFCOUNT; i++) {
	    if (msocks[i] >= 0 && FD_ISSET(msocks[i], &rfds)) {
		socklen_t alen = (socklen_t) sizeof(fsin);
		char *c_ip;
		/*@+matchanyintegral@*/
		int ssock =
		    accept(msocks[i], (struct sockaddr *)&fsin, &alen);
		/*@+matchanyintegral@*/

		if (ssock == -1)
		    gpsd_report(LOG_ERROR, "accept: %s\n", strerror(errno));
		else {
		    struct subscriber_t *client = NULL;
		    int opts = fcntl(ssock, F_GETFL);
		    static struct linger linger = { 1, RELEASE_TIMEOUT };

		    if (opts >= 0)
			(void)fcntl(ssock, F_SETFL, opts | O_NONBLOCK);

		    c_ip = netlib_sock2ip(ssock);
		    client = allocate_client();
		    if (client == NULL) {
			gpsd_report(LOG_ERROR, "Client %s connect on fd %d -"
				    "no subscriber slots available\n", c_ip,
				    ssock);
			(void)close(ssock);
		    } else
			if (setsockopt
			    (ssock, SOL_SOCKET, SO_LINGER, (char *)&linger,
			     (int)sizeof(struct linger)) == -1) {
			gpsd_report(LOG_ERROR,
				    "Error: SETSOCKOPT SO_LINGER\n");
			(void)close(ssock);
		    } else {
			char announce[GPS_JSON_RESPONSE_MAX];
			FD_SET(ssock, &all_fds);
			adjust_max_fd(ssock, true);
			client->fd = ssock;
			client->active = timestamp();
			gpsd_report(LOG_SPIN,
				    "client %s (%d) connect on fd %d\n", c_ip,
				    sub_index(client), ssock);
			json_version_dump(announce, sizeof(announce));
			(void)throttled_write(client, announce,
					      strlen(announce));
		    }
		}
		FD_CLR(msocks[i], &rfds);
	    }
	}
#endif /* SOCKET_EXPORT_ENABLE */

#ifdef CONTROL_SOCKET_ENABLE
	/* also be open to new control-socket connections */
	if (csock > -1 && FD_ISSET(csock, &rfds)) {
	    socklen_t alen = (socklen_t) sizeof(fsin);
	    /*@+matchanyintegral@*/
	    int ssock = accept(csock, (struct sockaddr *)&fsin, &alen);
	    /*@-matchanyintegral@*/

	    if (ssock == -1)
		gpsd_report(LOG_ERROR, "accept: %s\n", strerror(errno));
	    else {
		gpsd_report(LOG_INF, "control socket connect on fd %d\n",
			    ssock);
		FD_SET(ssock, &all_fds);
		FD_SET(ssock, &control_fds);
		adjust_max_fd(ssock, true);
	    }
	    FD_CLR(csock, &rfds);
	}

	/* read any commands that came in over control sockets */
	for (cfd = 0; cfd < FD_SETSIZE; cfd++)
	    if (FD_ISSET(cfd, &control_fds)) {
		char buf[BUFSIZ];

		while (read(cfd, buf, sizeof(buf) - 1) > 0) {
		    gpsd_report(LOG_IO, "<= control(%d): %s\n", cfd, buf);
		    handle_control(cfd, buf);
		}
		gpsd_report(LOG_SPIN, "close(%d) of control socket\n", cfd);
		(void)close(cfd);
		FD_CLR(cfd, &all_fds);
		FD_CLR(cfd, &control_fds);
		adjust_max_fd(cfd, false);
	    }
#endif /* CONTROL_SOCKET_ENABLE */

	/* poll all active devices */
	for (device = devices; device < devices + MAXDEVICES; device++) {
	    if (!allocated_device(device))
		continue;

	    /* pass the current RTCM correction to the GPS if new */
	    if (device->device_type != NULL)
		rtcm_relay(device);

	    if (device->gpsdata.gps_fd >= 0) {
		if (FD_ISSET(device->gpsdata.gps_fd, &rfds))
		    /* get data from the device */
		    consume_packets(device);
	        else if (device->reawake>0 && timestamp()>device->reawake) {
		    /* device may have had a zero-length read */
		    gpsd_report(LOG_DATA,
				"%s reawakened after zero-length read\n",
				device->gpsdata.dev.path);
		    device->reawake = (timestamp_t)0;
		    device->zerokill = true;
		    FD_SET(device->gpsdata.gps_fd, &all_fds);
		    adjust_max_fd(device->gpsdata.gps_fd, true);
		}
		    }
	} /* devices */

#ifdef __UNUSED_AUTOCONNECT__
	if (context.fixcnt > 0 && !context.autconnect) {
	    for (device = devices; device < devices + MAXDEVICES; device++) {
		if (device->gpsdata.fix.mode > MODE_NO_FIX) {
		    netgnss_autoconnect(&context,
					device->gpsdata.fix.latitude,
					device->gpsdata.fix.longitude);
		    context.autconnect = True;
		    break;
		}
	    }
	}
#endif /* __UNUSED_AUTOCONNECT__ */

#ifdef SOCKET_EXPORT_ENABLE
	/* accept and execute commands for all clients */
	for (sub = subscribers; sub < subscribers + MAXSUBSCRIBERS; sub++) {
	    if (sub->active == 0)
		continue;

	    if (FD_ISSET(sub->fd, &rfds)) {
		char buf[BUFSIZ];
		int buflen;

		gpsd_report(LOG_PROG, "checking client(%d)\n",
			    sub_index(sub));
		if ((buflen =
		     (int)recv(sub->fd, buf, sizeof(buf) - 1, 0)) <= 0) {
		    detach_client(sub);
		} else {
		    if (buf[buflen - 1] != '\n')
			buf[buflen++] = '\n';
		    buf[buflen] = '\0';
		    gpsd_report(LOG_IO,
				"<= client(%d): %s\n", sub_index(sub), buf);

		    /*
		     * When a command comes in, update subscriber.active to
		     * timestamp() so we don't close the connection
		     * after COMMAND_TIMEOUT seconds. This makes
		     * COMMAND_TIMEOUT useful.
		     */
		    sub->active = timestamp();
		    if (handle_gpsd_request(sub, buf) < 0)
			detach_client(sub);
		}
	    } else {
		if (!sub->policy.watcher
		    && timestamp() - sub->active > COMMAND_TIMEOUT) {
		    gpsd_report(LOG_WARN,
				"client(%d) timed out on command wait.\n",
				sub_index(sub));
		    detach_client(sub);
		}
	    }
	}

	/*
	 * Mark devices with an identified packet type but no
	 * remaining subscribers to be closed in RELEASE_TIME seconds.
	 * See the explanation of RELEASE_TIME for the reasoning.
	 */
	if (!nowait) {
	    for (device = devices; device < devices + MAXDEVICES; device++) {
		if (allocated_device(device)) {
		    if (device->packet.type != BAD_PACKET) {
			bool device_needed = false;

			for (sub = subscribers;
			     sub < subscribers + MAXSUBSCRIBERS; sub++) {
			    if (sub->active == 0)
				continue;
			    else if (subscribed(sub, device)) {
				device_needed = true;
				break;
			    }
			}

			if (!device_needed && device->gpsdata.gps_fd > -1) {
			    if (device->releasetime == 0) {
				device->releasetime = timestamp();
				gpsd_report(LOG_PROG,
					    "device %d (fd %d) released\n",
					    (int)(device - devices),
					    device->gpsdata.gps_fd);
			    } else if (timestamp() - device->releasetime >
				       RELEASE_TIMEOUT) {
				gpsd_report(LOG_PROG, "device %d closed\n",
					    (int)(device - devices));
				gpsd_report(LOG_RAW,
					    "unflagging descriptor %d\n",
					    device->gpsdata.gps_fd);
				deactivate_device(device);
			    }
			}
		    }
		}
	    }
	}

	/* nowait */
#endif /* SOCKET_EXPORT_ENABLE */
    }

    /* if we make it here, we got a signal... deal with it */
    /* restart on SIGHUP, clean up and exit otherwise */
    if (SIGHUP == (int)signalled)
	longjmp(restartbuf, 1);

    gpsd_report(LOG_WARN, "received terminating signal %d.\n", signalled);

    /* try to undo all device configurations */
    for (dfd = 0; dfd < MAXDEVICES; dfd++) {
	if (allocated_device(&devices[dfd]))
	    (void)gpsd_wrap(&devices[dfd]);
    }

    gpsd_report(LOG_WARN, "exiting.\n");

#ifdef SOCKET_EXPORT_ENABLE
    /*
     * A linger option was set on each client socket when it was
     * created.  Now, shut them down gracefully, letting I/O drain.
     * This is an attempt to avoid the sporadic race errors at the ends
     * of our regression tests.
     */
    for (sub = subscribers; sub < subscribers + MAXSUBSCRIBERS; sub++) {
	if (sub->active != 0)
	    detach_client(sub);
    }
#endif /* SOCKET_EXPORT_ENABLE */

#ifdef SHM_EXPORT_ENABLE
    shm_release(&context);
#endif /* SHM_EXPORT_ENABLE */

#ifdef CONTROL_SOCKET_ENABLE
    if (control_socket)
	(void)unlink(control_socket);
#endif /* CONTROL_SOCKET_ENABLE */
    if (pid_file)
	(void)unlink(pid_file);
    return 0;
}

/*@ +mustfreefresh @*/
