/*
 * This is the main sequence of the gpsd daemon. The IO dispatcher, main 
 * select loop, and user command handling lives here. 
 *
 * This file is Copyright (c) 2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 */
#include <stdlib.h>
#include "gpsd_config.h"
#include <sys/types.h>
#ifndef S_SPLINT_S
#include <unistd.h>
#endif /* S_SPLINT_S */
#ifdef HAVE_SYSLOG_H
#include <syslog.h>
#endif /* HAVE_SYSLOG_H */
#include <signal.h>
#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <string.h>
#ifndef S_SPLINT_S
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#else
#define AF_UNSPEC 0
#endif /* HAVE_SYS_SOCKET_H */
#ifdef HAVE_SYS_UN_H
#include <sys/un.h>
#endif /* HAVE_SYS_UN_H */
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif /* HAVE_NETINET_IN_H */
#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif /* HAVE_ARPA_INET_H */
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif /* HAVE_NETDB_H */
#endif /* S_SPLINT_S */
#include <stdarg.h>
#include <setjmp.h>
#include <stdio.h>
#include <assert.h>
#ifdef HAVE_PWD_H
#include <pwd.h>
#endif /* HAVE_PWD_H */
#ifdef HAVE_GRP_H
#include <grp.h>
#endif /* HAVE_GRP_H */
#include <stdbool.h>
#include <math.h>

#if defined (HAVE_PATH_H)
#include <paths.h>
#else
#if !defined (_PATH_DEVNULL)
#define _PATH_DEVNULL    "/dev/null"
#endif
#endif
#if defined (HAVE_SYS_SELECT_H)
#include <sys/select.h>
#endif
#if defined (HAVE_SYS_STAT_H)
#include <sys/stat.h>
#endif
#if defined(HAVE_SYS_TIME_H)
#include <sys/time.h>
#endif
#ifdef HAVE_SETLOCALE
#include <locale.h>
#endif

#ifdef DBUS_ENABLE
#include <gpsd_dbus.h>
#endif

#include "gpsd.h"
#include "sockaddr.h"
#include "gps_json.h"
#include "timebase.h"
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
 * Finally, RELEASE_TIMEOUT sets the amount of time we hold a device
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
 */
#define COMMAND_TIMEOUT		60*15
#define NOREAD_TIMEOUT		60*3
#define RELEASE_TIMEOUT		60

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

/* *INDENT-OFF* */
/*@ -initallelements -nullassign -nullderef @*/
struct gps_context_t context = {
    .valid	    = 0,
    .readonly	    = false,
    .sentdgps	    = false,
    .netgnss_service  = netgnss_none,
    .fixcnt	    = 0,
    .dsock	    = -1,
    .netgnss_privdata = NULL,
    .rtcmbytes	    = 0,
    .rtcmbuf	    = {'\0'},
    .rtcmtime	    = 0,
    .leap_seconds   = LEAP_SECONDS,
    .century	    = CENTURY_BASE,
#ifdef NTPSHM_ENABLE
    .enable_ntpshm  = false,
    .shmTime	    = {0},
    .shmTimeInuse   = {0},
# ifdef PPS_ENABLE
    .shmTimePPS	    = false,
# endif /* PPS_ENABLE */
#endif /* NTPSHM_ENABLE */
};
/*@ +initallelements +nullassign +nullderef @*/
/* *INDENT-ON* */

static volatile sig_atomic_t signalled;

static void onsig(int sig)
{
    /* just set a variable, and deal with it in the main loop */
    signalled = (sig_atomic_t) sig;
}

static int daemonize(void)
{
    int fd;
    pid_t pid;

    /*@ -type @*//* weirdly, splint 3.1.2 is confused by fork() */
    switch (pid = fork()) {
    case -1:
	return -1;
    case 0:			/* child side */
	break;
    default:			/* parent side */
	exit(0);
    }
    /*@ +type @*/

    if (setsid() == -1)
	return -1;
    if (chdir("/") == -1)
	return -1;
    /*@ -nullpass @*/
    if ((fd = open(_PATH_DEVNULL, O_RDWR, 0)) != -1) {
	(void)dup2(fd, STDIN_FILENO);
	(void)dup2(fd, STDOUT_FILENO);
	(void)dup2(fd, STDERR_FILENO);
	if (fd > 2)
	    (void)close(fd);
    }
    /*@ +nullpass @*/
    in_background = true;
    return 0;
}

#if defined(PPS_ENABLE)
static pthread_mutex_t report_mutex;
#endif /* PPS_ENABLE */

void gpsd_report(int errlevel, const char *fmt, ...)
/* assemble command in printf(3) style, use stderr or syslog */
{
#ifndef SQUELCH_ENABLE
    if (errlevel <= debuglevel) {
	char buf[BUFSIZ], buf2[BUFSIZ], *sp;
	va_list ap;

#if defined(PPS_ENABLE)
	/*@ -unrecog  (splint has no pthread declarations as yet) @*/
	(void)pthread_mutex_lock(&report_mutex);
	/* +unrecog */
#endif /* PPS_ENABLE */
	(void)strlcpy(buf, "gpsd: ", BUFSIZ);
	va_start(ap, fmt);
	(void)vsnprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), fmt,
			ap);
	va_end(ap);

	buf2[0] = '\0';
	for (sp = buf; *sp != '\0'; sp++)
	    if (isprint(*sp)
		|| (isspace(*sp) && (sp[1] == '\0' || sp[2] == '\0')))
		(void)snprintf(buf2 + strlen(buf2), 2, "%c", *sp);
	    else
		(void)snprintf(buf2 + strlen(buf2), 6, "\\x%02x",
			       (unsigned)*sp);

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

static int filesock(char *filename)
{
    struct sockaddr_un addr;
    int sock;

    /*@ -mayaliasunique -usedef @*/
    if ((sock = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
	gpsd_report(LOG_ERROR, "Can't create device-control socket\n");
	return -1;
    }
    (void)strlcpy(addr.sun_path, filename, 104);	/* from sys/un.h */
    addr.sun_family = AF_UNIX;
    (void)bind(sock, (struct sockaddr *)&addr, (int)sizeof(addr));
    if (listen(sock, QLEN) == -1) {
	gpsd_report(LOG_ERROR, "can't listen on local socket %s\n", filename);
	return -1;
    }
    /*@ +mayaliasunique +usedef @*/
    return sock;
}

struct subscriber_t
{
    int fd;			/* client file descriptor. -1 if unused */
    double active;		/* when subscriber last polled for data */
    struct policy_t policy;	/* configurable bits */
};

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

#ifdef LIMITED_MAX_CLIENTS
#define MAXSUBSCRIBERS LIMITED_MAX_CLIENTS
#else
/* subscriber structure is small enough that there's no need to limit this */
#define MAXSUBSCRIBERS	FD_SETSIZE
#endif
#define sub_index(s) (int)((s) - subscribers)
#define allocated_device(devp)	 ((devp)->gpsdata.dev.path[0] != '\0')
#define free_device(devp)	 (devp)->gpsdata.dev.path[0] = '\0'
#define initialized_device(devp) ((devp)->context != NULL)
#define subscribed(sub, devp)    (sub->policy.devpath[0]=='\0' || strcmp(sub->policy.devpath, devp->gpsdata.dev.path)==0)

struct gps_device_t devices[MAXDEVICES];
struct subscriber_t subscribers[MAXSUBSCRIBERS];	/* indexed by client file descriptor */

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
    sub->active = 0;
    sub->policy.watcher = false;
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
	    gpsd_report(LOG_IO, "=> client(%d): %s", sub_index(sub), buf);
	else {
	    char *cp, buf2[MAX_PACKET_LENGTH * 3];
	    buf2[0] = '\0';
	    for (cp = buf; cp < buf + len; cp++)
		(void)snprintf(buf2 + strlen(buf2),
			       sizeof(buf2) - strlen(buf2),
			       "%02x", (unsigned int)(*cp & 0xff));
	    gpsd_report(LOG_IO, "=> client(%d): =%s\r\n", sub_index(sub),
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

static void notify_watchers(struct gps_device_t *device, char *sentence, ...)
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

static void deactivate_device(struct gps_device_t *device)
/* deactivate device, but leave it in the pool (do not free it) */
{
    notify_watchers(device,
		    "{\"class\":\"DEVICE\",\"path\":\"%s\",\"activated\":0}\r\n",
		    device->gpsdata.dev.path);
    if (device->gpsdata.gps_fd != -1) {
	FD_CLR(device->gpsdata.gps_fd, &all_fds);
	adjust_max_fd(device->gpsdata.gps_fd, false);
	gpsd_deactivate(device);
	device->gpsdata.gps_fd = -1;	/* device is already disconnected */
    }
}

/* *INDENT-OFF* */
/*@ -globstate @*/
/*@null@*//*@observer@*/ static struct gps_device_t *find_device(char
								 *device_name)
/* find the device block for an existing device name */
{
    struct gps_device_t *devp;

    for (devp = devices; devp < devices + MAXDEVICES; devp++)
	if (allocated_device(devp)
	    && strcmp(devp->gpsdata.dev.path, device_name) == 0)
	    return devp;
    return NULL;
}
/* *INDENT-ON* */

/*@ -nullret @*/
/*@ -statictrans @*/
static bool open_device(char *device_name)
/* open and initialize a new device block */
{
    struct gps_device_t *devp;

    for (devp = devices; devp < devices + MAXDEVICES; devp++)
	if (!allocated_device(devp)
	    || (strcmp(devp->gpsdata.dev.path, device_name) == 0
		&& !initialized_device(devp))) {
	    goto found;
	}
    return false;
  found:
    gpsd_init(devp, &context, device_name);
    if (gpsd_activate(devp) < 0)
	return false;
    FD_SET(devp->gpsdata.gps_fd, &all_fds);
    adjust_max_fd(devp->gpsdata.gps_fd, true);
    return true;
}

static bool add_device(char *device_name)
/* add a device to the pool; open it right away if in nowait mode */
{
    if (nowait)
	return open_device(device_name);
    else {
	struct gps_device_t *devp;
	/* stash devicename away for probing when the first client connects */
	for (devp = devices; devp < devices + MAXDEVICES; devp++)
	    if (!allocated_device(devp)) {
		gpsd_init(devp, &context, device_name);
		gpsd_report(LOG_INF, "stashing device %s at slot %d\n",
			    device_name, (int)(devp - devices));
		devp->gpsdata.gps_fd = -1;
		notify_watchers(devp,
				"{\"class\":\"DEVICE\",\"path\":\"%s\",\"activated\":%ld}\r\n",
				devp->gpsdata.dev.path, timestamp());
		return true;
	    }
	return false;
    }
}

/*@ +nullret @*/
/*@ +statictrans @*/
/*@ +globstate @*/

static bool awaken(struct subscriber_t *user, struct gps_device_t *device)
/* awaken a device and notify all watchers*/
{
    /* open that device */
    if (!initialized_device(device)) {
	if (!open_device(device->gpsdata.dev.path)) {
	    gpsd_report(LOG_PROG, "client(%d): open failed\n",
			sub_index(user));
	    free_device(device);
	    return false;
	}
    }

    if (device->gpsdata.gps_fd != -1) {
	gpsd_report(LOG_PROG,
		    "client(%d): device %d (fd=%d, path %s) already active.\n",
		    sub_index(user), (int)(device - devices),
		    device->gpsdata.gps_fd, device->gpsdata.dev.path);
	return true;
    } else {
	if (gpsd_activate(device) < 0) {
	    gpsd_report(LOG_ERROR, "client(%d): device activation failed.\n",
			sub_index(user));
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
#endif /* ALLOW_CONFIGURE */

static void handle_control(int sfd, char *buf)
/* handle privileged commands coming through the control socket */
{
    char *p, *stash, *eq;
    struct gps_device_t *devp;

    /*@ -sefparams @*/
    if (buf[0] == '-') {
	p = snarfline(buf + 1, &stash);
	gpsd_report(LOG_INF, "<= control(%d): removing %s\n", sfd, stash);
	if ((devp = find_device(stash))) {
	    deactivate_device(devp);
	    free_device(devp);
	    ignore_return(write(sfd, "OK\n", 3));
	} else
	    ignore_return(write(sfd, "ERROR\n", 6));
    } else if (buf[0] == '+') {
	p = snarfline(buf + 1, &stash);
	if (find_device(stash)) {
	    gpsd_report(LOG_INF, "<= control(%d): %s already active \n", sfd,
			stash);
	    ignore_return(write(sfd, "ERROR\n", 6));
	} else {
	    gpsd_report(LOG_INF, "<= control(%d): adding %s \n", sfd, stash);
	    if (add_device(stash))
		ignore_return(write(sfd, "OK\n", 3));
	    else
		ignore_return(write(sfd, "ERROR\n", 6));
	}
    } else if (buf[0] == '!') {
	p = snarfline(buf + 1, &stash);
	eq = strchr(stash, '=');
	if (eq == NULL) {
	    gpsd_report(LOG_WARN, "<= control(%d): ill-formed command \n",
			sfd);
	    ignore_return(write(sfd, "ERROR\n", 6));
	} else {
	    *eq++ = '\0';
	    if ((devp = find_device(stash))) {
		gpsd_report(LOG_INF, "<= control(%d): writing to %s \n", sfd,
			    stash);
		ignore_return(write(devp->gpsdata.gps_fd, eq, strlen(eq)));
		ignore_return(write(sfd, "OK\n", 3));
	    } else {
		gpsd_report(LOG_INF, "<= control(%d): %s not active \n", sfd,
			    stash);
		ignore_return(write(sfd, "ERROR\n", 6));
	    }
	}
    } else if (buf[0] == '&') {
	p = snarfline(buf + 1, &stash);
	eq = strchr(stash, '=');
	if (eq == NULL) {
	    gpsd_report(LOG_WARN, "<= control(%d): ill-formed command \n",
			sfd);
	    ignore_return(write(sfd, "ERROR\n", 6));
	} else {
	    size_t len;
	    int st;
	    *eq++ = '\0';
	    len = strlen(eq) + 5;
	    if ((devp = find_device(stash)) != NULL) {
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
		    ignore_return(write
				  (devp->gpsdata.gps_fd, eq, (size_t) st));
		    ignore_return(write(sfd, "OK\n", 3));
		}
	    } else {
		gpsd_report(LOG_INF, "<= control(%d): %s not active\n", sfd,
			    stash);
		ignore_return(write(sfd, "ERROR\n", 6));
	    }
	}
    }
    /*@ +sefparams @*/
}

#ifdef ALLOW_RECONFIGURE
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
    /* *INDENT-ON* */
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
		gpsd_report(LOG_ERROR, "ERROR response: %s", reply);
	    } else if (sub->policy.watcher) {
		if (sub->policy.devpath[0] == '\0') {
		    /* awaken all devices */
		    for (devp = devices; devp < devices + MAXDEVICES; devp++)
			if (allocated_device(devp))
			    (void)awaken(sub, devp);
		} else {
		    devp = find_device(sub->policy.devpath);
		    if (devp == NULL) {
			(void)snprintf(reply, replylen,
				       "{\"class\":\"ERROR\",\"message\":\"Do nuch device as %s\"}\r\n",
				       sub->policy.devpath);
			gpsd_report(LOG_ERROR, "ERROR response: %s", reply);
			goto bailout;
		    } else if (!awaken(sub, devp))
			(void)snprintf(reply, replylen,
				       "{\"class\":\"ERROR\",\"message\":\"Can't assign %s\"}\r\n",
				       sub->policy.devpath);
		    gpsd_report(LOG_ERROR, "ERROR response: %s", reply);
		    goto bailout;
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
			       "{\"class\":\"ERROR\",\"message\":\"Invalid DEVICE: %s\"}\r\n",
			       json_error_string(status));
		gpsd_report(LOG_ERROR, "ERROR response: %s", reply);
		goto bailout;
	    } else {
		if (devconf.path[0] != '\0') {
		    /* user specified a path, try to assign it */
		    if (!awaken(sub, find_device(devconf.path))) {
			(void)snprintf(reply, replylen,
				       "{\"class\":\"ERROR\",\"message\":\"Can't open %s.\"}\r\n",
				       devconf.path);
			gpsd_report(LOG_ERROR, "ERROR response: %s", reply);
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
			gpsd_report(LOG_ERROR, "ERROR response: %s", reply);
			goto bailout;
		    } else if (devcount > 1) {
			(void)snprintf(reply + strlen(reply),
				       replylen - strlen(reply),
				       "{\"class\":\"ERROR\",\"message\":\"No path specified in DEVICE, but multiple devices are attached.\"}\r\n");
			gpsd_report(LOG_ERROR, "ERROR response: %s", reply);
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
	int active = 0;
	buf += 5;
	for (devp = devices; devp < devices + MAXDEVICES; devp++)
	    if (allocated_device(devp) && subscribed(sub, devp))
		if ((devp->observed & GPS_TYPEMASK) != 0)
		    active++;
	(void)snprintf(reply, replylen,
		       "{\"class\":\"POLL\",\"timestamp\":%.3f,\"active\":%d,\"fixes\":[",
		       timestamp(), active);
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
	(void)strlcat(reply, "],\"skyviews\":[", replylen);
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
	(void)strlcat(reply, "]}]}\r\n", replylen);
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
	gpsd_report(LOG_ERROR, "ERROR response: %s", reply);
	buf += strlen(buf);
    }
  bailout:
    *after = buf;
    /*@+nullderef +nullpass@*/
    /*@+compdef@*/
}

static int handle_gpsd_request(struct subscriber_t *sub, const char *buf)
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

/*@ -mustfreefresh @*/
int main(int argc, char *argv[])
{
    char *pid_file = NULL;
    int st, csock = -1;
    gps_mask_t changed;
    static char *gpsd_service = NULL;	/* static pacifies splint */
    char *control_socket = NULL;
    struct gps_device_t *device;
    sockaddr_t fsin;
    fd_set rfds, control_fds;
    int i, option, msocks[2], cfd, dfd;
    bool go_background = true;
    struct timeval tv;
    struct subscriber_t *sub;
    const struct gps_type_t **dp;

#ifdef PPS_ENABLE
    pthread_mutex_init(&report_mutex, NULL);
#endif /* PPS_ENABLE */

#ifdef HAVE_SETLOCALE
    (void)setlocale(LC_NUMERIC, "C");
#endif
    debuglevel = 0;
    gpsd_hexdump_level = 0;
    while ((option = getopt(argc, argv, "F:D:S:bGhlNnP:V")) != -1) {
	switch (option) {
	case 'D':
	    debuglevel = (int)strtol(optarg, 0, 0);
	    gpsd_hexdump_level = debuglevel;
#ifdef CLIENTDEBUG_ENABLE
	    gps_enable_debug(debuglevel, stderr);
#endif /* CLIENTDEBUG_ENABLE */
	    break;
	case 'F':
	    control_socket = optarg;
	    break;
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
	    gpsd_service = optarg;
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

#ifdef FIXED_PORT_SPEED
    /* Assume that if we're running with FIXED_PORT_SPEED we're some sort
     * of embedded configuration where we don't want to wait for connect */
    nowait = true;
#endif

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

    if (go_background)
	(void)daemonize();

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

#ifdef NTPSHM_ENABLE
    if (getuid() == 0) {
	errno = 0;
	// nice() can ONLY succeed when run as root!
	// do not even bother as non-root
	if (nice(NICEVAL) == -1 && errno != 0)
	    gpsd_report(2, "NTPD Priority setting failed.\n");
    }
    (void)ntpshm_init(&context, nowait);
#endif /* NTPSHM_ENABLE */

#ifdef DBUS_ENABLE
    /* we need to connect to dbus as root */
    if (initialize_dbus_connection()) {
	/* the connection could not be started */
	gpsd_report(LOG_ERROR, "unable to connect to the DBUS system bus\n");
    } else
	gpsd_report(LOG_PROG,
		    "successfully connected to the DBUS system bus\n");
#endif /* DBUS_ENABLE */

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
	    (void)seteuid(pw->pw_uid);
	/*@+type@*/
    }
    gpsd_report(LOG_INF, "running with effective group ID %d\n", getegid());
    gpsd_report(LOG_INF, "running with effective user ID %d\n", geteuid());

    for (i = 0; i < NITEMS(subscribers); i++)
	subscribers[i].fd = UNALLOCATED_FD;

    /* daemon got termination or interrupt signal */
    if ((st = setjmp(restartbuf)) > 0) {
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
    FD_ZERO(&control_fds);

    /* optimization hack to defer having to read subframe data */
    if (time(NULL) < START_SUBFRAME)
	context.valid |= LEAP_SECOND_VALID;

    for (i = optind; i < argc; i++) {
	if (!add_device(argv[i])) {
	    gpsd_report(LOG_ERROR,
			"GPS device %s nonexistent or can't be read\n",
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
	    gpsd_report(LOG_SPIN, "select() {%s} at %f\n", dbuf, timestamp());
	}

	/* always be open to new client connections */
	for (i = 0; i < AFCOUNT; i++) {
	    if (msocks[i] >= 0 && FD_ISSET(msocks[i], &rfds)) {
		socklen_t alen = (socklen_t) sizeof(fsin);
		char *c_ip;
		/*@+matchanyintegral@*/
		int ssock =  accept(msocks[i], (struct sockaddr *)&fsin, &alen);
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

	/* also be open to new control-socket connections */
	if (csock > -1 && FD_ISSET(csock, &rfds)) {
	    socklen_t alen = (socklen_t) sizeof(fsin);
	    /*@+matchanyintegral@*/ 
	    int ssock =	accept(csock, (struct sockaddr *)&fsin, &alen);
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

	if (context.dsock >= 0 && FD_ISSET(context.dsock, &rfds)) {
	    /* be ready for DGPS reports */
	    if (netgnss_poll(&context) == -1) {
		FD_CLR(context.dsock, &all_fds);
		FD_CLR(context.dsock, &rfds);
		context.dsock = -1;
	    }
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

	/* poll all active devices */
	for (device = devices; device < devices + MAXDEVICES; device++) {
	    if (!allocated_device(device))
		continue;

	    /* pass the current RTCM correction to the GPS if new */
	    if (device->device_type != NULL)
		rtcm_relay(device);

	    /* get data from the device */
	    changed = 0;
	    if (device->gpsdata.gps_fd >= 0
		&& FD_ISSET(device->gpsdata.gps_fd, &rfds)) {
		gpsd_report(LOG_RAW + 1, "polling %d\n",
			    device->gpsdata.gps_fd);
		changed = gpsd_poll(device);

		if (changed == ERROR_IS) {
		    gpsd_report(LOG_WARN,
				"packet sniffer failed sync with %s\n",
				device->gpsdata.dev.path);
		    deactivate_device(device);
		    continue;
		} else if ((changed & ONLINE_IS) == 0) {
		    gpsd_report(LOG_WARN,
				"%s returned error or went offline\n",
				device->gpsdata.dev.path);
		    deactivate_device(device);
		    continue;
		}

		/* must have a full packet to continue */
		if ((changed & PACKET_IS) == 0)
		    continue;

		/* raw hook and relaying functions */
		for (sub = subscribers; sub < subscribers + MAXSUBSCRIBERS;
		     sub++) {
		    if (sub->active == 0)
			continue;

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
			continue;
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
			continue;
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
		    /* *INDENT-ON* */
		}

		if (device->gpsdata.fix.mode == MODE_3D)
		    netgnss_report(device);

		else {
		    /* we may need to add device to new-style watcher lists */
		    if ((changed & DEVICE_IS) != 0) {
			for (sub = subscribers;
			     sub < subscribers + MAXSUBSCRIBERS; sub++)
			    if (sub->policy.watcher
				&& sub->policy.devpath[0] == '\0')
				(void)awaken(sub, device);
		    }
		    /* handle laggy response to a firmware version query */
		    if ((changed & (DEVICEID_IS | DEVICE_IS)) != 0) {
			assert(device->device_type != NULL);
			{
			    char id2[GPS_JSON_RESPONSE_MAX];
			    json_device_dump(device, id2, sizeof(id2));
			    notify_watchers(device, id2);
			}
		    }
		}
		/* *INDENT-OFF* */
		/* copy each RTCM-104 correction to all GPSes */
		if ((changed & RTCM2_IS) != 0 || (changed & RTCM3_IS) != 0) {
		    struct gps_device_t *gps;
		    for (gps = devices; gps < devices + MAXDEVICES; gps++)
			if (gps->device_type != NULL
			    && gps->device_type->rtcm_writer != NULL)
			    (void)gps->device_type->rtcm_writer(gps,
								(char *)gps->
								packet.
								outbuffer,
								gps->packet.
								outbuflen);
		}
		/* *INDENT-ON* */
	    }

	    /* watch all channels associated with this device */
	    for (sub = subscribers; sub < subscribers + MAXSUBSCRIBERS; sub++) {
		if (sub->active == 0)
		    continue;
		/* some listeners may be in watcher mode */
		/*@-nullderef@*/
		if (sub != NULL && sub->policy.watcher) {
		    char buf2[GPS_JSON_RESPONSE_MAX * 4];
		    if (changed & DATA_IS) {
			bool report_fix = false;
			gpsd_report(LOG_PROG,
				    "Changed mask: %s with %sreliable cycle detection\n",
				    gpsd_maskdump(changed),
				    device->cycle_end_reliable ? "" : "un");
			if (device->cycle_end_reliable) {
			    /*
			     * Driver returns reliable end of cycle, 
			     * report only when that is signaled.
			     */
			    if ((changed & REPORT_IS) != 0)
				report_fix = true;
			} else if (changed & (LATLON_IS | MODE_IS))
			    /*
			     * No reliable end of cycle.  Must report
			     * every time a sentence changes position
			     * or mode. Likely to cause display jitter.
			     */
			    report_fix = true;
			if (report_fix)
			    gpsd_report(LOG_PROG, "time to report a fix\n");
#ifdef DBUS_ENABLE
			if (report_fix)
			    send_dbus_fix(device);
#endif /* DBUS_ENABLE */

			/* binary GPS packet, pseudo-NMEA dumping enabled */
			if (sub->policy.nmea
			    && GPS_PACKET_TYPE(device->packet.type)
			    && !TEXTUAL_PACKET_TYPE(device->packet.type)) {
			    char buf3[MAX_PACKET_LENGTH * 3 + 2];

			    gpsd_report(LOG_PROG, "data mask is %s\n",
					gpsd_maskdump(device->gpsdata.set));
			    if (report_fix) {
				nmea_tpv_dump(device, buf3, sizeof(buf3));
				gpsd_report(LOG_IO, "<= GPS (binary1) %s: %s",
					    device->gpsdata.dev.path, buf3);
				(void)throttled_write(sub, buf3,
						      strlen(buf3));
			    } else if ((changed & SATELLITE_IS) != 0) {
				nmea_sky_dump(device, buf3, sizeof(buf3));
				gpsd_report(LOG_IO, "<= GPS (binary2) %s: %s",
					    device->gpsdata.dev.path, buf3);
				(void)throttled_write(sub, buf3,
						      strlen(buf3));
			    }
			}

			if (sub->policy.json) {
			    buf2[0] = '\0';
			    if (report_fix) {
				json_tpv_dump(&device->gpsdata,
					      buf2, sizeof(buf2));
				(void)throttled_write(sub, buf2,
						      strlen(buf2));
			    }
			    if ((changed & SATELLITE_IS) != 0) {
				json_sky_dump(&device->gpsdata,
					      buf2, sizeof(buf2));
				(void)throttled_write(sub, buf2,
						      strlen(buf2));
			    }
#ifdef COMPASS_ENABLE
			    if ((changed & ATT_IS) != 0) {
				json_att_dump(&device->gpsdata,
					      buf2, sizeof(buf2));
				(void)throttled_write(sub, buf2,
						      strlen(buf2));
			    }
#endif /* COMPASS_ENABLE */
#ifdef RTCM104V2_ENABLE
			    if ((changed & RTCM2_IS) != 0) {
				rtcm2_json_dump(&device->gpsdata.rtcm2, buf2,
						sizeof(buf2));
				(void)throttled_write(sub, buf2,
						      strlen(buf2));
			    }
#endif /* RTCM104V2_ENABLE */
#ifdef AIVDM_ENABLE
			    if ((changed & AIS_IS) != 0) {
				aivdm_json_dump(&device->gpsdata.ais,
						sub->policy.scaled,
						buf2, sizeof(buf2));
				(void)throttled_write(sub, buf2,
						      strlen(buf2));
			    }
#endif /* AIVDM_ENABLE */

#ifdef TIMING_ENABLE
			    if (buf2[0] != '\0' && sub->policy.timing) {
				(void)snprintf(buf2, sizeof(buf2),
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
				(void)throttled_write(sub, buf2,
						      strlen(buf2));
			    }
#endif /* TIMING_ENABLE */
			}
		    }
		    //gpsd_report(LOG_PROG, "reporting finished\n");
		}
		/*@-nullderef@*/
	    }
	}

#ifdef NOT_FIXED
	if (context.fixcnt > 0 && context.dsock == -1) {
	    for (device = devices; device < devices + MAXDEVICES; device++) {
		if (device->gpsdata.fix.mode > MODE_NO_FIX) {
		    netgnss_autoconnect(&context,
					device->gpsdata.fix.latitude,
					device->gpsdata.fix.longitude);
		    break;
		}
	    }
	}
#endif

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
				"<= client(%d): %s", sub_index(sub), buf);

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
				cfd);
		    detach_client(sub);
		}
	    }
	}

	/*
	 * Mark devices with an identified packet type but no
	 * remaining subscribers to be closed in RELEASE_TIME seconds.
	 * See the explanation of RELEASE_TIME for the reasoning.
	 */
	if (!nowait)
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

    /* if we make it here, we got a signal... deal with it */
    /* restart on SIGHUP, clean up and exit otherwise */
    if (SIGHUP == (int)signalled)
	longjmp(restartbuf, 1);

    gpsd_report(LOG_WARN, "Received terminating signal %d. Exiting...\n",
		signalled);

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

    /* try to undo all device configurations */
    for (dfd = 0; dfd < MAXDEVICES; dfd++) {
	if (allocated_device(&devices[dfd]))
	    (void)gpsd_wrap(&devices[dfd]);
    }

    if (control_socket)
	(void)unlink(control_socket);
    if (pid_file)
	(void)unlink(pid_file);
    return 0;
}

/*@ +mustfreefresh @*/
