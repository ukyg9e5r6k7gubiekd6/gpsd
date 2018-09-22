/*
 * This is the main sequence of the gpsd daemon. The IO dispatcher, main
 * select loop, and user command handling lives here.
 *
 * This file is Copyright (c) 2010 by the GPSD project
 * SPDX-License-Identifier: BSD-2-clause
 */

#ifdef __linux__
/* FreeBSD chokes on this */
/* nice() needs _XOPEN_SOURCE, 500 means X/Open 1995 */
/* Ubuntu isfinite() needs _XOPEN_SOURCE, 600 means X/Open 2004 */
#define _XOPEN_SOURCE 600
/* setgroups() needs _DEFAULT_SOURCE or _BSD_SOURCE (glibc-dependent) */
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#endif /* __linux__ */

/* vsnprintf() needs __DARWIN_C_LEVEL >= 200112L */
#define __DARWIN_C_LEVEL 200112L
/* strlcpy() needs _DARWIN_C_SOURCE */
#define _DARWIN_C_SOURCE

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>		/* for select() */
#include <sys/select.h>
#include <stdio.h>
#include <stdint.h>		/* for uint32_t, etc. */
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <setjmp.h>
#include <assert.h>
#include <math.h>
#include <syslog.h>
#include <errno.h>
#include <signal.h>
#include <ctype.h>
#include <pwd.h>
#include <grp.h>
#include <fcntl.h>
#include <pthread.h>
#include <netdb.h>
#ifndef AF_UNSPEC
#include <sys/socket.h>
#endif /* AF_UNSPEC */
#ifndef INADDR_ANY
#include <netinet/in.h>
#endif /* INADDR_ANY */
#include <sys/un.h>
#include <arpa/inet.h>     /* for htons() and friends */
#include <unistd.h>

#include "gpsd_config.h"

#include "gpsd.h"
#include "sockaddr.h"
#include "gps_json.h"
#include "revision.h"
#include "strfuncs.h"

#if defined(SYSTEMD_ENABLE)
#include "sd_socket.h"
#endif

/*
 * The name of a tty device from which to pick up whatever the local
 * owning group for tty devices is.  Used when we drop privileges.
 */
#if defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
#define PROTO_TTY "/dev/tty00"	/* correct for *BSD */
#else
#define PROTO_TTY "/dev/ttyS0"	/* correct for Linux */
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
 *
 * DEVICE_RECONNECT sets interval on retries when (re)connecting to
 * a device.
 */
#define COMMAND_TIMEOUT		60*15
#define NOREAD_TIMEOUT		60*3
#define RELEASE_TIMEOUT		60
#define DEVICE_REAWAKE		0.01
#define DEVICE_RECONNECT	2

#define QLEN			5

/*
 * If ntpshm is enabled, we renice the process to this priority level.
 * For precise timekeeping increase priority.
 */
#define NICEVAL	-10

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
#define FORCE_NOWAIT
#endif /* defined(FIXED_PORT_SPEED) || !defined(SOCKET_EXPORT_ENABLE) */

#ifdef SOCKET_EXPORT_ENABLE
/* IP version used by the program */
/* AF_UNSPEC: all
 * AF_INET: IPv4 only
 * AF_INET6: IPv6 only
 */
#ifdef IPV6_ENABLE
static const int af_allowed = AF_UNSPEC;
#else
static const int af_allowed = AF_INET;
#endif
#endif /* SOCKET_EXPORT_ENABLE */

#define AFCOUNT 2

static fd_set all_fds;
static int maxfd;
static int highwater;
#ifndef FORCE_GLOBAL_ENABLE
static bool listen_global = false;
#endif /* FORCE_GLOBAL_ENABLE */
#ifndef FORCE_NOWAIT
#define NOWAIT nowait
static bool nowait = false;
#else /* FORCE_NOWAIT */
#define NOWAIT true
#endif /* FORCE_NOWAIT */
static bool batteryRTC = false;
static jmp_buf restartbuf;
static struct gps_context_t context;
#if defined(SYSTEMD_ENABLE)
static int sd_socket_count = 0;
#endif

/* work around the unfinished ipv6 implementation on hurd and OSX <10.6 */
#ifndef IPV6_TCLASS
# if defined(__GNU__)
#  define IPV6_TCLASS 61
# elif defined(__APPLE__)
#  define IPV6_TCLASS 36
# endif
#endif

static volatile sig_atomic_t signalled;

static void onsig(int sig)
{
    /* just set a variable, and deal with it in the main loop */
    signalled = (sig_atomic_t) sig;
}

static void typelist(void)
/* list installed drivers and enabled features */
{
    const struct gps_type_t **dp;

    for (dp = gpsd_drivers; *dp; dp++) {
	if ((*dp)->packet_type == COMMENT_PACKET)
	    continue;
#ifdef RECONFIGURE_ENABLE
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
	if ((*dp)->packet_type > NMEA_PACKET)
	    (void)fputs("*\t", stdout);
	else
	    (void)fputc('\t', stdout);
#endif /* RECONFIGURE_ENABLE */
	(void)puts((*dp)->type_name);
    }
    (void)printf("# n: mode switch, b: speed switch, "
	"c: rate switch, *: non-NMEA packet type.\n");
#if defined(SOCKET_EXPORT_ENABLE)
    (void)printf("# Socket export enabled.\n");
#endif
#if defined(SHM_EXPORT_ENABLE)
    (void)printf("# Shared memory export enabled.\n");
#endif
#if defined(DBUS_EXPORT_ENABLE)
    (void)printf("# DBUS export enabled\n");
#endif
#if defined(TIMEHINT_ENABLE)
    (void)printf("# Time service features enabled.\n");
#endif
#if defined(PPS_ENABLE)
    (void)printf("# PPS enabled.\n");
#endif
    exit(EXIT_SUCCESS);
}

static void usage(void)
{
    (void)printf("usage: gpsd [-b] [-D n] [-F sockfile] [-G] [-h] [-n] [-N] [-P pidfile] [-S port] device...\n\
  Options include: \n\
  -b		     	    = bluetooth-safe: open data sources read-only\n\
  -D integer (default 0)    = set debug level \n\
  -F sockfile		    = specify control socket location\n"
#ifndef FORCE_GLOBAL_ENABLE
"  -G         		    = make gpsd listen on INADDR_ANY\n"
#endif /* FORCE_GLOBAL_ENABLE */
"  -h		     	    = help message \n"
#ifndef FORCE_NOWAIT
"  -n			    = don't wait for client connects to poll GPS\n"
#endif /* FORCE_NOWAIT */
"  -N			    = don't go into background\n\
  -P pidfile	      	    = set file to record process ID\n\
  -r               	    = use GPS time even if no fix\n\
  -S integer (default %s) = set port for daemon \n\
  -V			    = emit version and exit.\n"
#ifdef NETFEED_ENABLE
"A device may be a local serial device for GPS input, or a URL in one \n\
of the following forms:\n\
     tcp://host[:port]\n\
     udp://host[:port]\n\
     {dgpsip|ntrip}://[user:passwd@]host[:port][/stream]\n\
     gpsd://host[:port][/device][?protocol]\n\
in which case it specifies an input source for device, DGPS or ntrip data.\n"
#endif /* NETFEED_ENABLE */
"\n\
The following driver types are compiled into this gpsd instance:\n",
		 DEFAULT_GPSD_PORT);
    typelist();
}

#ifdef CONTROL_SOCKET_ENABLE
static socket_t filesock(char *filename)
{
    struct sockaddr_un addr;
    socket_t sock;

    if (BAD_SOCKET(sock = socket(AF_UNIX, SOCK_STREAM, 0))) {
	gpsd_log(&context.errout, LOG_ERROR,
		 "Can't create device-control socket\n");
	return -1;
    }
    (void)strlcpy(addr.sun_path, filename, sizeof(addr.sun_path));
    addr.sun_family = (sa_family_t)AF_UNIX;
    if (bind(sock, (struct sockaddr *)&addr, (int)sizeof(addr)) < 0) {
	gpsd_log(&context.errout, LOG_ERROR,
		 "can't bind to local socket %s\n", filename);
	(void)close(sock);
	return -1;
    }
    if (listen(sock, QLEN) == -1) {
	gpsd_log(&context.errout, LOG_ERROR,
		 "can't listen on local socket %s\n", filename);
	(void)close(sock);
	return -1;
    }

    /* coverity[leaked_handle] This is an intentional allocation */
    return sock;
}
#endif /* CONTROL_SOCKET_ENABLE */

#define sub_index(s) (int)((s) - subscribers)
#define allocated_device(devp)	 ((devp)->gpsdata.dev.path[0] != '\0')
#define free_device(devp)	 (devp)->gpsdata.dev.path[0] = '\0'
#define initialized_device(devp) ((devp)->context != NULL)

/*
 * This array fills from the bottom, so as an extreme case you can
 * reduce MAX_DEVICES to 1 in the build recipe.
 */
static struct gps_device_t devices[MAX_DEVICES];

static void adjust_max_fd(int fd, bool on)
/* track the largest fd currently in use */
{
    if (on) {
	if (fd > maxfd)
	    maxfd = fd;
    }
    else {
	if (fd == maxfd) {
	    int tfd;

	    for (maxfd = tfd = 0; tfd < (int)FD_SETSIZE; tfd++)
		if (FD_ISSET(tfd, &all_fds))
		    maxfd = tfd;
	}
    }
}

#ifdef SOCKET_EXPORT_ENABLE
#ifndef IPTOS_LOWDELAY
#define IPTOS_LOWDELAY 0x10
#endif

static socket_t passivesock_af(int af, char *service, char *tcp_or_udp, int qlen)
/* bind a passive command socket for the daemon */
{
    volatile socket_t s;
    /*
     * af = address family,
     * service = IANA protocol name or number.
     * tcp_or_udp = TCP or UDP
     * qlen = maximum wait-queue length for connections
     */
    struct servent *pse;
    struct protoent *ppe;
    sockaddr_t sat;
    int sin_len = 0;
    int type, proto, one = 1;
    in_port_t port;
    char *af_str = "";
    const int dscp = IPTOS_LOWDELAY; /* Prioritize packet */
    INVALIDATE_SOCKET(s);
    if ((pse = getservbyname(service, tcp_or_udp)))
	port = ntohs((in_port_t) pse->s_port);
    // cppcheck-suppress unreadVariable
    else if ((port = (in_port_t) atoi(service)) == 0) {
	gpsd_log(&context.errout, LOG_ERROR,
		 "can't get \"%s\" service entry.\n", service);
	return -1;
    }
    ppe = getprotobyname(tcp_or_udp);
    if (strcmp(tcp_or_udp, "udp") == 0) {
	type = SOCK_DGRAM;
	proto = (ppe) ? ppe->p_proto : IPPROTO_UDP;
    } else {
	type = SOCK_STREAM;
	proto = (ppe) ? ppe->p_proto : IPPROTO_TCP;
    }

    switch (af) {
    case AF_INET:
	sin_len = sizeof(sat.sa_in);

	memset((char *)&sat.sa_in, 0, sin_len);
	sat.sa_in.sin_family = (sa_family_t) AF_INET;
#ifndef FORCE_GLOBAL_ENABLE
	if (!listen_global)
	    sat.sa_in.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	else
#endif /* FORCE_GLOBAL_ENABLE */
	    sat.sa_in.sin_addr.s_addr = htonl(INADDR_ANY);
	sat.sa_in.sin_port = htons(port);

	af_str = "IPv4";
	/* see PF_INET6 case below */
	s = socket(PF_INET, type, proto);
	if (s > -1 ) {
	/* Set packet priority */
	if (setsockopt(s, IPPROTO_IP, IP_TOS, &dscp, sizeof(dscp)) == -1)
	    gpsd_log(&context.errout, LOG_WARN,
		     "Warning: SETSOCKOPT TOS failed\n");
	}

	break;
#ifdef IPV6_ENABLE
    case AF_INET6:
	sin_len = sizeof(sat.sa_in6);

	memset((char *)&sat.sa_in6, 0, sin_len);
	sat.sa_in6.sin6_family = (sa_family_t) AF_INET6;
#ifndef FORCE_GLOBAL_ENABLE
	if (!listen_global)
	    sat.sa_in6.sin6_addr = in6addr_loopback;
	else
#endif /* FORCE_GLOBAL_ENABLE */
	    sat.sa_in6.sin6_addr = in6addr_any;
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

	/*
	 * On some network stacks, including Linux's, an IPv6 socket
	 * defaults to listening on IPv4 as well. Unless we disable
	 * this, trying to listen on in6addr_any will fail with the
	 * address-in-use error condition.
	 */
	if (s > -1) {
	    int on = 1;
	    if (setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, &on, sizeof(on)) == -1) {
		gpsd_log(&context.errout, LOG_ERROR,
			 "Error: SETSOCKOPT IPV6_V6ONLY\n");
		(void)close(s);
		return -1;
	    }
#ifdef IPV6_TCLASS
	    /* Set packet priority */
	    if (setsockopt(s, IPPROTO_IPV6, IPV6_TCLASS, &dscp, sizeof(dscp)) == -1)
		gpsd_log(&context.errout, LOG_WARN,
			 "Warning: SETSOCKOPT TOS failed\n");
#endif /* IPV6_TCLASS */
	}
	break;
#endif  /* IPV6_ENABLE */
    default:
	gpsd_log(&context.errout, LOG_ERROR,
		 "unhandled address family %d\n", af);
	return -1;
    }
    gpsd_log(&context.errout, LOG_IO,
	     "opening %s socket\n", af_str);

    if (BAD_SOCKET(s)) {
	gpsd_log(&context.errout, LOG_ERROR,
		 "can't create %s socket\n", af_str);
	return -1;
    }
    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *)&one,
		   (int)sizeof(one)) == -1) {
	gpsd_log(&context.errout, LOG_ERROR,
		 "Error: SETSOCKOPT SO_REUSEADDR\n");
	(void)close(s);
	return -1;
    }
    if (bind(s, &sat.sa, sin_len) < 0) {
	gpsd_log(&context.errout, LOG_ERROR,
		 "can't bind to %s port %s, %s\n", af_str,
		 service, strerror(errno));
	if (errno == EADDRINUSE) {
	    gpsd_log(&context.errout, LOG_ERROR,
		     "maybe gpsd is already running!\n");
	}
	(void)close(s);
	return -1;
    }
    if (type == SOCK_STREAM && listen(s, qlen) == -1) {
	gpsd_log(&context.errout, LOG_ERROR,
		 "can't listen on port %s\n", service);
	(void)close(s);
	return -1;
    }

    gpsd_log(&context.errout, LOG_SPIN, "passivesock_af() -> %d\n", s);
    return s;
}

/* *INDENT-OFF* */
static int passivesocks(char *service, char *tcp_or_udp,
			int qlen, socket_t socks[])
{
    int numsocks = AFCOUNT;
    int i;

    for (i = 0; i < AFCOUNT; i++)
	INVALIDATE_SOCKET(socks[i]);

#if defined(SYSTEMD_ENABLE)
    if (sd_socket_count > 0) {
        for (i = 0; i < AFCOUNT && i < sd_socket_count - 1; i++) {
            socks[i] = SD_SOCKET_FDS_START + i + 1;
        }
        return sd_socket_count - 1;
    }
#endif

    if (AF_UNSPEC == af_allowed || (AF_INET == af_allowed))
	socks[0] = passivesock_af(AF_INET, service, tcp_or_udp, qlen);

    if (AF_UNSPEC == af_allowed || (AF_INET6 == af_allowed))
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
    int fd;			  /* client file descriptor. -1 if unused */
    time_t active;		  /* when subscriber last polled for data */
    struct gps_policy_t policy;	  /* configurable bits */
    pthread_mutex_t mutex;	  /* serialize access to fd */
};

#define subscribed(sub, devp)    (sub->policy.watcher && (sub->policy.devpath[0]=='\0' || strcmp(sub->policy.devpath, devp->gpsdata.dev.path)==0))

static struct subscriber_t subscribers[MAX_CLIENTS];	/* indexed by client file descriptor */

static void lock_subscriber(struct subscriber_t *sub)
{
    (void)pthread_mutex_lock(&sub->mutex);
}

static void unlock_subscriber(struct subscriber_t *sub)
{
    (void)pthread_mutex_unlock(&sub->mutex);
}

static struct subscriber_t *allocate_client(void)
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
    lock_subscriber(sub);
    if (sub->fd == UNALLOCATED_FD) {
	unlock_subscriber(sub);
	return;
    }
    c_ip = netlib_sock2ip(sub->fd);
    (void)shutdown(sub->fd, SHUT_RDWR);
    gpsd_log(&context.errout, LOG_SPIN,
	     "close(%d) in detach_client()\n",
	     sub->fd);
    (void)close(sub->fd);
    gpsd_log(&context.errout, LOG_INF,
	     "detaching %s (sub %d, fd %d) in detach_client\n",
	     c_ip, sub_index(sub), sub->fd);
    FD_CLR(sub->fd, &all_fds);
    adjust_max_fd(sub->fd, false);
    sub->active = 0;
    sub->policy.watcher = false;
    sub->policy.json = false;
    sub->policy.nmea = false;
    sub->policy.raw = 0;
    sub->policy.scaled = false;
    sub->policy.timing = false;
    sub->policy.split24 = false;
    sub->policy.devpath[0] = '\0';
    sub->fd = UNALLOCATED_FD;
    unlock_subscriber(sub);
}

static ssize_t throttled_write(struct subscriber_t *sub, char *buf,
			       size_t len)
/* write to client -- throttle if it's gone or we're close to buffer overrun */
{
    ssize_t status;

    if (context.errout.debug >= LOG_CLIENT) {
	if (isprint((unsigned char) buf[0]))
	    gpsd_log(&context.errout, LOG_CLIENT,
		     "=> client(%d): %s\n", sub_index(sub), buf);
	else {
#ifndef __clang_analyzer__
	    char *cp, buf2[MAX_PACKET_LENGTH * 3];
	    buf2[0] = '\0';
	    for (cp = buf; cp < buf + len; cp++)
		str_appendf(buf2, sizeof(buf2),
			       "%02x", (unsigned int)(*cp & 0xff));
	    gpsd_log(&context.errout, LOG_CLIENT,
		     "=> client(%d): =%s\n", sub_index(sub),	buf2);
#endif /* __clang_analyzer__ */
	}
    }

#if defined(PPS_ENABLE)
    gpsd_acquire_reporting_lock();
#endif /* PPS_ENABLE */
    status = send(sub->fd, buf, len, 0);
#if defined(PPS_ENABLE)
    gpsd_release_reporting_lock();

#endif /* PPS_ENABLE */
    if (status == (ssize_t) len)
	return status;
    else if (status > -1) {
	gpsd_log(&context.errout, LOG_INF,
		 "short write disconnecting client(%d)\n",
		 sub_index(sub));
	detach_client(sub);
	return 0;
    } else if (errno == EAGAIN || errno == EINTR)
	return 0;		/* no data written, and errno says to retry */
    else if (errno == EBADF)
	gpsd_log(&context.errout, LOG_WARN,
		 "client(%d) has vanished.\n", sub_index(sub));
    else if (errno == EWOULDBLOCK
	     && time(NULL) - sub->active > NOREAD_TIMEOUT)
	gpsd_log(&context.errout, LOG_INF,
		 "client(%d) timed out.\n", sub_index(sub));
    else
	gpsd_log(&context.errout, LOG_INF,
		 "client(%d) write: %s\n",
		 sub_index(sub), strerror(errno));
    detach_client(sub);
    return status;
}

static void notify_watchers(struct gps_device_t *device,
			    bool onjson, bool onpps,
			    const char *sentence, ...)
/* notify all JSON-watching clients of a given device about an event */
{
    va_list ap;
    char buf[BUFSIZ];
    struct subscriber_t *sub;

    va_start(ap, sentence);
    (void)vsnprintf(buf, sizeof(buf), sentence, ap);
    va_end(ap);

    for (sub = subscribers; sub < subscribers + MAX_CLIENTS; sub++)
	if (sub->active != 0 && subscribed(sub, device)) {
	    if ((onjson && sub->policy.json) || (onpps && sub->policy.pps))
		(void)throttled_write(sub, buf, strlen(buf));
	}
}
#endif /* SOCKET_EXPORT_ENABLE */

static void deactivate_device(struct gps_device_t *device)
/* deactivate device, but leave it in the pool (do not free it) */
{
#ifdef SOCKET_EXPORT_ENABLE
    notify_watchers(device, true, false,
		    "{\"class\":\"DEVICE\",\"path\":\"%s\",\"activated\":0}\r\n",
		    device->gpsdata.dev.path);
#endif /* SOCKET_EXPORT_ENABLE */
    if (!BAD_SOCKET(device->gpsdata.gps_fd)) {
	FD_CLR(device->gpsdata.gps_fd, &all_fds);
	adjust_max_fd(device->gpsdata.gps_fd, false);
#ifdef NTPSHM_ENABLE
	ntpshm_link_deactivate(device);
#endif /* NTPSHM_ENABLE */
	gpsd_deactivate(device);
    }
}

#if defined(SOCKET_EXPORT_ENABLE) || defined(CONTROL_SOCKET_ENABLE)
/* *INDENT-OFF* */
static struct gps_device_t *find_device(const char
								 *device_name)
/* find the device block for an existing device name */
{
    struct gps_device_t *devp;

    for (devp = devices; devp < devices + MAX_DEVICES; devp++)
    {
        if (allocated_device(devp) && NULL != device_name &&
            strcmp(devp->gpsdata.dev.path, device_name) == 0)
            return devp;
    }
    return NULL;
}
/* *INDENT-ON* */
#endif /* defined(SOCKET_EXPORT_ENABLE) || defined(CONTROL_SOCKET_ENABLE) */

static bool open_device( struct gps_device_t *device)
/* open the input device
 * return: false on failure
 *         true on success
 */
{
    int activated = -1;

    if (NULL == device ) {
	return false;
    }
    activated = gpsd_activate(device, O_OPTIMIZE);
    if ( ( 0 > activated ) && ( PLACEHOLDING_FD != activated ) ) {
	/* failed to open device, and it is not a /dev/ppsX */
	return false;
    }

#ifdef NTPSHM_ENABLE
    /*
     * Now is the right time to grab the shared memory segment(s)
     * to communicate the navigation message derived and (possibly)
     * 1PPS derived time data to ntpd/chrony.
     */
    ntpshm_link_activate(device);
    gpsd_log(&context.errout, LOG_INF,
	     "PPS:%s ntpshm_link_activate: %d\n",
	     device->gpsdata.dev.path,
	     device->shm_clock != NULL);
#endif /* NTPSHM_ENABLE */

    gpsd_log(&context.errout, LOG_INF,
	     "device %s activated\n", device->gpsdata.dev.path);
    if ( PLACEHOLDING_FD == activated ) {
	/* it is a /dev/ppsX, no need to select() it */
        return true;
    }
    FD_SET(device->gpsdata.gps_fd, &all_fds);
    adjust_max_fd(device->gpsdata.gps_fd, true);
    ++highwater;
    return true;
}

bool gpsd_add_device(const char *device_name, bool flag_nowait)
/* add a device to the pool; open it right away if in nowait mode
 * return: false on failure
 *         true on success
 */
{
    struct gps_device_t *devp;
    bool ret = false;
    /* we can't handle paths longer than GPS_PATH_MAX, so don't try */
    if (strlen(device_name) >= GPS_PATH_MAX) {
	gpsd_log(&context.errout, LOG_ERROR,
		 "ignoring device %s: path length exceeds maximum %d\n",
		 device_name, GPS_PATH_MAX);
	return false;
    }
    /* stash devicename away for probing when the first client connects */
    for (devp = devices; devp < devices + MAX_DEVICES; devp++)
	if (!allocated_device(devp)) {
	    gpsd_init(devp, &context, device_name);
#ifdef NTPSHM_ENABLE
	    ntpshm_session_init(devp);
#endif /* NTPSHM_ENABLE */
	    gpsd_log(&context.errout, LOG_INF,
		     "stashing device %s at slot %d\n",
		     device_name, (int)(devp - devices));
	    if (!flag_nowait) {
		devp->gpsdata.gps_fd = UNALLOCATED_FD;
		ret = true;
	    } else {
		ret = open_device(devp);
	    }
#ifdef SOCKET_EXPORT_ENABLE
	    notify_watchers(devp, true, false,
			    "{\"class\":\"DEVICE\",\"path\":\"%s\",\"activated\":%lf}\r\n",
			    devp->gpsdata.dev.path, timestamp());
#endif /* SOCKET_EXPORT_ENABLE */
	    break;
	}
    return ret;
}

/* convert hex to binary, write it, unchanged, to GPS */
static int write_gps(char *device, char *hex) {
    struct gps_device_t *devp;
    size_t len;
    int st;

    if ((devp = find_device(device)) == NULL) {
	gpsd_log(&context.errout, LOG_INF, "GPS <=: %s not active\n", device);
	return 1;
    }
    if (devp->context->readonly || (devp->sourcetype <= source_blockdev)) {
	gpsd_log(&context.errout, LOG_WARN,
		 "GPS <=: attempted to write to a read-only device\n");
	return 1;
    }

    len = strlen(hex);
    /* NOTE: this destroys the original buffer contents */
    st = gpsd_hexpack(hex, hex, len);
    if (st <= 0) {
	gpsd_log(&context.errout, LOG_INF,
		 "GPS <=: invalid hex string (error %d).\n", st);
	return 1;
    }
    gpsd_log(&context.errout, LOG_INF,
	     "GPS <=: writing %d bytes fromhex(%s) to %s\n",
	     st, hex, device);
    if (write(devp->gpsdata.gps_fd, hex, (size_t) st) <= 0) {
	gpsd_log(&context.errout, LOG_WARN,
		 "GPS <=: write to device failed\n");
	return 1;
    }
    return 0;
}

#ifdef CONTROL_SOCKET_ENABLE
static char *snarfline(char *p, char **out)
/* copy the rest of the command line, before CR-LF */
{
    char *q;
    static char stash[BUFSIZ];

    for (q = p; isprint((unsigned char) *p) && !isspace((unsigned char) *p) && (p - q < (ssize_t) sizeof(stash) - 1);
	 p++)
	continue;
    (void)memcpy(stash, q, (size_t) (p - q));
    stash[p - q] = '\0';
    *out = stash;
    return p;
}

static void handle_control(int sfd, char *buf)
/* handle privileged commands coming through the control socket */
{
    char *stash;
    struct gps_device_t *devp;

     /*
      * The only other place in the code that knows about the format
      * of the + and - commands is the gpsd_control() function in
      * gpsdctl.c.  Be careful about keeping them in sync, or
      * hotplugging will have mysterious failures.
      */
    if (buf[0] == '-') {
	/* remove device named after - */
	(void)snarfline(buf + 1, &stash);
	gpsd_log(&context.errout, LOG_INF,
		 "<= control(%d): removing %s\n", sfd, stash);
	if ((devp = find_device(stash))) {
	    deactivate_device(devp);
	    free_device(devp);
	    ignore_return(write(sfd, "OK\n", 3));
	} else
	    ignore_return(write(sfd, "ERROR\n", 6));
    } else if (buf[0] == '+') {
	/* add device named after + */
	(void)snarfline(buf + 1, &stash);
	if (find_device(stash)) {
	    gpsd_log(&context.errout, LOG_INF,
		     "<= control(%d): %s already active \n", sfd,
		     stash);
	    ignore_return(write(sfd, "ERROR\n", 6));
	} else {
	    gpsd_log(&context.errout, LOG_INF,
		     "<= control(%d): adding %s\n", sfd, stash);
	    if (gpsd_add_device(stash, NOWAIT))
		ignore_return(write(sfd, "OK\n", 3));
	    else {
		ignore_return(write(sfd, "ERROR\n", 6));
		gpsd_log(&context.errout, LOG_INF,
			 "control(%d): adding %s failed, too many devices active\n",
			 sfd, stash);
	    }
	}
    } else if (buf[0] == '!') {
	/* split line after ! into device=string, send string to device */
	char *eq;
	(void)snarfline(buf + 1, &stash);
	eq = strchr(stash, '=');
	if (eq == NULL) {
	    gpsd_log(&context.errout, LOG_WARN,
		     "<= control(%d): ill-formed command \n",
		     sfd);
	    ignore_return(write(sfd, "ERROR\n", 6));
	} else {
	    *eq++ = '\0';
	    if ((devp = find_device(stash))) {
		if (devp->context->readonly || (devp->sourcetype <= source_blockdev)) {
		    gpsd_log(&context.errout, LOG_WARN,
			     "<= control(%d): attempted to write to a read-only device\n",
			     sfd);
		    ignore_return(write(sfd, "ERROR\n", 6));
		} else {
		    gpsd_log(&context.errout, LOG_INF,
			     "<= control(%d): writing to %s \n", sfd,
			     stash);
		    if (write(devp->gpsdata.gps_fd, eq, strlen(eq)) <= 0) {
			gpsd_log(&context.errout, LOG_WARN,
				 "<= control(%d): write to device failed\n",
				 sfd);
			ignore_return(write(sfd, "ERROR\n", 6));
		    } else {
			ignore_return(write(sfd, "OK\n", 3));
		    }
		}
	    } else {
		gpsd_log(&context.errout, LOG_INF,
			 "<= control(%d): %s not active \n", sfd,
			 stash);
		ignore_return(write(sfd, "ERROR\n", 6));
	    }
	}
    } else if (buf[0] == '&') {
	/* split line after & into dev=hexdata, send unpacked hexdata to dev */
	char *eq;

	(void)snarfline(buf + 1, &stash);
	eq = strchr(stash, '=');
	if (eq == NULL) {
	    gpsd_log(&context.errout, LOG_WARN,
		     "<= control(%d): ill-formed command\n",
		     sfd);
	    ignore_return(write(sfd, "ERROR\n", 6));
	} else {
	    *eq++ = '\0';
	    if (0 == write_gps(stash, eq)) {
		ignore_return(write(sfd, "OK\n", 3));
            } else {
		ignore_return(write(sfd, "ERROR\n", 6));
            }
	}
    } else if (strstr(buf, "?devices")==buf) {
	/* write back devices list followed by OK */
	for (devp = devices; devp < devices + MAX_DEVICES; devp++) {
	    char *path = devp->gpsdata.dev.path;
	    ignore_return(write(sfd, path, strlen(path)));
	    ignore_return(write(sfd, "\n", 1));
	}
	ignore_return(write(sfd, "OK\n", 3));
    } else {
	/* unknown command */
	ignore_return(write(sfd, "ERROR\n", 6));
    }
}
#endif /* CONTROL_SOCKET_ENABLE */

#ifdef SOCKET_EXPORT_ENABLE
static bool awaken(struct gps_device_t *device)
/* awaken a device and notify all watchers*/
{
    /* open that device */
    if (!initialized_device(device)) {
	if (!open_device(device)) {
	    gpsd_log(&context.errout, LOG_WARN,
		     "%s: open failed\n",
		     device->gpsdata.dev.path);
	    free_device(device);
	    return false;
	}
    }

    if (!BAD_SOCKET(device->gpsdata.gps_fd)) {
	gpsd_log(&context.errout, LOG_PROG,
		 "device %d (fd=%d, path %s) already active.\n",
		 (int)(device - devices),
		 device->gpsdata.gps_fd, device->gpsdata.dev.path);
	return true;
    } else {
	if (gpsd_activate(device, O_OPTIMIZE) < 0) {
	    gpsd_log(&context.errout, LOG_ERROR,
		     "%s: device activation failed.\n",
		     device->gpsdata.dev.path);
		    gpsd_log(&context.errout, LOG_ERROR,
				"%s: activation failed, freeing device\n",
				device->gpsdata.dev.path);
		    /* FIXME: works around a crash bug, but prevents retries */
		    free_device(device);
	    return false;
	} else {
	    gpsd_log(&context.errout, LOG_RAW,
			"flagging descriptor %d in assign_channel()\n",
			device->gpsdata.gps_fd);
	    FD_SET(device->gpsdata.gps_fd, &all_fds);
	    adjust_max_fd(device->gpsdata.gps_fd, true);
	    return true;
	}
    }
}

#ifdef RECONFIGURE_ENABLE
#if __UNUSED_RECONFIGURE__
static bool privileged_user(struct gps_device_t *device)
/* is this channel privileged to change a device's behavior? */
{
    /* grant user privilege if he's the only one listening to the device */
    struct subscriber_t *sub;
    int subcount = 0;
    for (sub = subscribers; sub < subscribers + MAX_CLIENTS; sub++) {
	if (subscribed(sub, device))
	    subcount++;
    }
    /*
     * Yes, zero subscribers is possible. For example, gpsctl talking
     * to the daemon connects but doesn't necessarily issue a ?WATCH
     * before shipping a request, which means it isn't marked as a
     * subscriber.
     */
    return subcount <= 1;
}
#endif /* __UNUSED_RECONFIGURE__ */

static void set_serial(struct gps_device_t *device,
		       speed_t speed, char *modestring)
/* set serial parameters for a device from a speed and modestring */
{
    unsigned int stopbits = device->gpsdata.dev.stopbits;
    char parity = device->gpsdata.dev.parity;
    int wordsize = 8;
    struct timespec delay;

#ifndef __clang_analyzer__
    while (isspace((unsigned char) *modestring))
	modestring++;
    if (*modestring && strchr("78", *modestring) != NULL) {
	wordsize = (int)(*modestring++ - '0');
	if (*modestring && strchr("NOE", *modestring) != NULL) {
	    parity = *modestring++;
	    while (isspace((unsigned char) *modestring))
		modestring++;
	    if (*modestring && strchr("12", *modestring) != NULL)
		stopbits = (unsigned int)(*modestring - '0');
	}
    }
#endif /* __clang_analyzer__ */

    gpsd_log(&context.errout, LOG_PROG,
	     "set_serial(%s,%u,%s) %c%d\n",
	     device->gpsdata.dev.path,
	     (unsigned int)speed, modestring, parity, stopbits);
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

	    /* wait 50,000 uSec */
	    delay.tv_sec = 0;
	    delay.tv_nsec = 50000000L;
	    nanosleep(&delay, NULL);

	    gpsd_set_speed(device, speed, parity, stopbits);
	}
    }
    /* *INDENT-ON* */
}
#endif /* RECONFIGURE_ENABLE */

#ifdef SOCKET_EXPORT_ENABLE
static void json_devicelist_dump(char *reply, size_t replylen)
{
    struct gps_device_t *devp;
    (void)strlcpy(reply, "{\"class\":\"DEVICES\",\"devices\":[", replylen);
    for (devp = devices; devp < devices + MAX_DEVICES; devp++)
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

    str_rstrip_char(reply, ',');
    (void)strlcat(reply, "]}\r\n", replylen);
}
#endif /* SOCKET_EXPORT_ENABLE */

static void rstrip(char *str)
/* strip trailing \r\n\t\SP from a string */
{
    char *strend;
    strend = str + strlen(str) - 1;
    while (isspace((unsigned char) *strend)) {
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

    if (str_starts_with(buf, "?DEVICES;")) {
	buf += 9;
	json_devicelist_dump(reply, replylen);
    } else if (str_starts_with(buf, "?WATCH")
	       && (buf[6] == ';' || buf[6] == '=')) {
	const char *start = buf;
	buf += 6;
	if (*buf == ';') {
	    ++buf;
	} else {
	    int status = json_watch_read(buf + 1, &sub->policy, &end);
#ifndef TIMING_ENABLE
	    sub->policy.timing = false;
#endif /* TIMING_ENABLE */
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
		gpsd_log(&context.errout, LOG_ERROR, "response: %s\n", reply);
	    } else if (sub->policy.watcher) {
		if (sub->policy.devpath[0] == '\0') {
		    /* awaken all devices */
		    for (devp = devices; devp < devices + MAX_DEVICES; devp++)
			if (allocated_device(devp)) {
			    (void)awaken(devp);
			    if (devp->sourcetype == source_gpsd) {
			        /* forward to master gpsd */
				(void)gpsd_write(devp, start,
				                 (size_t)(end-start));
			    }
			}
		} else {
		    devp = find_device(sub->policy.devpath);
		    if (devp == NULL) {
			(void)snprintf(reply, replylen,
				       "{\"class\":\"ERROR\",\"message\":\"No such device as %s\"}\r\n",
				       sub->policy.devpath);
			gpsd_log(&context.errout, LOG_ERROR,
				 "response: %s\n", reply);
			goto bailout;
		    } else if (awaken(devp)) {
			if (devp->sourcetype == source_gpsd) {
			    (void)gpsd_write(devp, start, (size_t)(end-start));
			}
		    } else {
			(void)snprintf(reply, replylen,
				       "{\"class\":\"ERROR\",\"message\":\"Can't assign %s\"}\r\n",
				       sub->policy.devpath);
			gpsd_log(&context.errout, LOG_ERROR,
				 "response: %s\n", reply);
			goto bailout;
		    }
		}
	    }
	}
	/* display a device list and the user's policy */
	json_devicelist_dump(reply + strlen(reply), replylen - strlen(reply));
	json_watch_dump(&sub->policy,
			reply + strlen(reply), replylen - strlen(reply));
    } else if (str_starts_with(buf, "?DEVICE")
	       && (buf[7] == ';' || buf[7] == '=')) {
	struct devconfig_t devconf;
	buf += 7;
	devconf.path[0] = '\0';	/* initially, no device selection */
	if (*buf == ';') {
	    ++buf;
	} else {
#ifdef RECONFIGURE_ENABLE
	    struct gps_device_t *device;
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
	    if (status != 0) {
		(void)snprintf(reply, replylen,
			       "{\"class\":\"ERROR\",\"message\":\"Invalid DEVICE: \"%s\"}\r\n",
			       json_error_string(status));
		gpsd_log(&context.errout, LOG_ERROR, "response: %s\n", reply);
		goto bailout;
	    } else {
		if (devconf.path[0] != '\0') {
		    /* user specified a path, try to assign it */
		    device = find_device(devconf.path);
		    /* do not optimize away, we need 'device' later on! */
		    if (device == NULL || !awaken(device)) {
			(void)snprintf(reply, replylen,
				       "{\"class\":\"ERROR\",\"message\":\"Can't open %s.\"}\r\n",
				       devconf.path);
			gpsd_log(&context.errout, LOG_ERROR,
				 "response: %s\n", reply);
			goto bailout;
		    }
		} else {
		    /* no path specified */
		    int devcount = 0;
		    for (devp = devices; devp < devices + MAX_DEVICES; devp++)
			if (allocated_device(devp)) {
			    device = devp;
			    devcount++;
			}
		    if (devcount == 0) {
			(void)strlcat(reply,
				      "{\"class\":\"ERROR\",\"message\":\"Can't perform DEVICE configuration, no devices attached.\"}\r\n",
				      replylen);
			gpsd_log(&context.errout, LOG_ERROR,
				 "response: %s\n", reply);
			goto bailout;
		    } else if (devcount > 1) {
			str_appendf(reply, replylen,
				       "{\"class\":\"ERROR\",\"message\":\"No path specified in DEVICE, but multiple devices are attached.\"}\r\n");
			gpsd_log(&context.errout, LOG_ERROR,
				 "response: %s\n", reply);
			goto bailout;
		    }
		    /* we should have exactly one device now */
		}
		if (device->device_type == NULL)
		    str_appendf(reply, replylen,
				   "{\"class\":\"ERROR\",\"message\":\"Type of %s is unknown.\"}\r\n",
				   device->gpsdata.dev.path);
		else {
		    const struct gps_type_t *dt = device->device_type;
		    bool no_serial_change =
			(devconf.baudrate == DEVDEFAULT_BPS)
			&& (devconf.parity == DEVDEFAULT_PARITY)
			&& (devconf.stopbits == DEVDEFAULT_STOPBITS);

		    /* interpret defaults */
		    if (devconf.baudrate == DEVDEFAULT_BPS)
			devconf.baudrate =
			    (unsigned int) gpsd_get_speed(device);
		    if (devconf.parity == DEVDEFAULT_PARITY)
			devconf.stopbits = device->gpsdata.dev.stopbits;
		    if (devconf.stopbits == DEVDEFAULT_STOPBITS)
			devconf.stopbits = device->gpsdata.dev.stopbits;
		    if (0 == isfinite(devconf.cycle))
			devconf.cycle = device->gpsdata.dev.cycle;

		    /* now that channel is selected, apply changes */
		    if (devconf.driver_mode != device->gpsdata.dev.driver_mode
			&& devconf.driver_mode != DEVDEFAULT_NATIVE
			&& dt->mode_switcher != NULL)
			dt->mode_switcher(device, devconf.driver_mode);
		    if (!no_serial_change) {
			char serialmode[3];
			serialmode[0] = devconf.parity;
			serialmode[1] = '0' + devconf.stopbits;
			serialmode[2] = '\0';
			set_serial(device,
				   (speed_t) devconf.baudrate, serialmode);
		    }
		    if (devconf.cycle != device->gpsdata.dev.cycle
			&& devconf.cycle >= dt->min_cycle
			&& dt->rate_switcher != NULL)
			if (dt->rate_switcher(device, devconf.cycle))
			    device->gpsdata.dev.cycle = devconf.cycle;
	            if ('\0' != devconf.hexdata[0]) {
			write_gps(device->gpsdata.dev.path, devconf.hexdata);
		    }
		}
	    }
#else /* RECONFIGURE_ENABLE */
	    str_appendf(reply, replylen,
			   "{\"class\":\"ERROR\",\"message\":\"Device configuration support not compiled.\"}\r\n");
#endif /* RECONFIGURE_ENABLE */
	}
	/* dump a response for each selected channel */
	for (devp = devices; devp < devices + MAX_DEVICES; devp++)
	    if (!allocated_device(devp))
		continue;
	    else if (devconf.path[0] != '\0'
		     && strcmp(devp->gpsdata.dev.path, devconf.path) != 0)
		continue;
	    else {
		json_device_dump(devp,
				 reply + strlen(reply),
				 replylen - strlen(reply));
	    }
    } else if (str_starts_with(buf, "?POLL;")) {
	char tbuf[JSON_DATE_MAX+1];
	int active = 0;
	buf += 6;
	for (devp = devices; devp < devices + MAX_DEVICES; devp++)
	    if (allocated_device(devp) && subscribed(sub, devp))
		if ((devp->observed & GPS_TYPEMASK) != 0)
		    active++;
	(void)snprintf(reply, replylen,
		       "{\"class\":\"POLL\",\"time\":\"%s\",\"active\":%d,\"tpv\":[",
		       unix_to_iso8601(timestamp(), tbuf, sizeof(tbuf)), active);
	for (devp = devices; devp < devices + MAX_DEVICES; devp++) {
	    if (allocated_device(devp) && subscribed(sub, devp)) {
		if ((devp->observed & GPS_TYPEMASK) != 0) {
		    json_tpv_dump(devp, &sub->policy,
				  reply + strlen(reply),
				  replylen - strlen(reply));
		    rstrip(reply);
		    (void)strlcat(reply, ",", replylen);
		}
	    }
	}
	str_rstrip_char(reply, ',');
	(void)strlcat(reply, "],\"gst\":[", replylen);
	for (devp = devices; devp < devices + MAX_DEVICES; devp++) {
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
	str_rstrip_char(reply, ',');
	(void)strlcat(reply, "],\"sky\":[", replylen);
	for (devp = devices; devp < devices + MAX_DEVICES; devp++) {
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
	str_rstrip_char(reply, ',');
	(void)strlcat(reply, "]}\r\n", replylen);
    } else if (str_starts_with(buf, "?VERSION;")) {
	buf += 9;
	json_version_dump(reply, replylen);
    } else {
	const char *errend;
	errend = buf + strlen(buf) - 1;
	while (isspace((unsigned char) *errend) && errend > buf)
	    --errend;
	(void)snprintf(reply, replylen,
		       "{\"class\":\"ERROR\",\"message\":\"Unrecognized request '%.*s'\"}\r\n",
		       (int)(errend - buf), buf);
	gpsd_log(&context.errout, LOG_ERROR, "ERROR response: %s\n", reply);
	buf += strlen(buf);
    }
  bailout:
    *after = buf;
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
    if (TEXTUAL_PACKET_TYPE(device->lexer.type)
	&& (sub->policy.raw > 0 || sub->policy.nmea)) {
	(void)throttled_write(sub,
			      (char *)device->lexer.outbuffer,
			      device->lexer.outbuflen);
	return;
    }

    /*
     * Also, simply copy if user has specified
     * super-raw mode.
     */
    if (sub->policy.raw > 1) {
	(void)throttled_write(sub,
			      (char *)device->lexer.outbuffer,
			      device->lexer.outbuflen);
	return;
    }
#ifdef BINARY_ENABLE
    /*
     * Maybe the user wants a binary packet hexdumped.
     */
    if (sub->policy.raw == 1) {
	const char *hd =
	    gpsd_hexdump(device->msgbuf, sizeof(device->msgbuf),
			 (char *)device->lexer.outbuffer,
			 device->lexer.outbuflen);
	(void)strlcat((char *)hd, "\r\n", sizeof(device->msgbuf));
	(void)throttled_write(sub, (char *)hd, strlen(hd));
    }
#endif /* BINARY_ENABLE */
}

static void pseudonmea_report(struct subscriber_t *sub,
			  gps_mask_t changed,
			  struct gps_device_t *device)
/* report pseudo-NMEA in appropriate circumstances */
{
    if (GPS_PACKET_TYPE(device->lexer.type)
	&& !TEXTUAL_PACKET_TYPE(device->lexer.type)) {
	char buf[MAX_PACKET_LENGTH * 3 + 2];

	if ((changed & REPORT_IS) != 0) {
	    nmea_tpv_dump(device, buf, sizeof(buf));
	    gpsd_log(&context.errout, LOG_IO,
		     "<= GPS (binary tpv) %s: %s\n",
		     device->gpsdata.dev.path, buf);
	    (void)throttled_write(sub, buf, strlen(buf));
	}

	if ((changed & (SATELLITE_SET|USED_IS)) != 0) {
	    nmea_sky_dump(device, buf, sizeof(buf));
	    gpsd_log(&context.errout, LOG_IO,
		     "<= GPS (binary sky) %s: %s\n",
		     device->gpsdata.dev.path, buf);
	    (void)throttled_write(sub, buf, strlen(buf));
	}

	if ((changed & SUBFRAME_SET) != 0) {
	    nmea_subframe_dump(device, buf, sizeof(buf));
	    gpsd_log(&context.errout, LOG_IO,
		     "<= GPS (binary subframe) %s: %s\n",
		     device->gpsdata.dev.path, buf);
	    (void)throttled_write(sub, buf, strlen(buf));
	}
#ifdef AIVDM_ENABLE
	if ((changed & AIS_SET) != 0) {
	    nmea_ais_dump(device, buf, sizeof(buf));
	    gpsd_log(&context.errout, LOG_IO,
		     "<= AIS (binary ais) %s: %s\n",
		     device->gpsdata.dev.path, buf);
	    (void)throttled_write(sub, buf, strlen(buf));
	}
#endif /* AIVDM_ENABLE */
    }
}
#endif /* SOCKET_EXPORT_ENABLE */

static void all_reports(struct gps_device_t *device, gps_mask_t changed)
/* report on the current packet from a specified device */
{
#ifdef SOCKET_EXPORT_ENABLE
    struct subscriber_t *sub;

    /* add any just-identified device to watcher lists */
    if ((changed & DRIVER_IS) != 0) {
	bool listeners = false;
	for (sub = subscribers;
	     sub < subscribers + MAX_CLIENTS; sub++)
	    if (sub->active != 0
		&& sub->policy.watcher
		&& subscribed(sub, device))
		listeners = true;
	if (listeners) {
	    (void)awaken(device);
	}
    }

    /* handle laggy response to a firmware version query */
    if ((changed & (DEVICEID_SET | DRIVER_IS)) != 0) {
	if (device->device_type == NULL)
	    gpsd_log(&context.errout, LOG_ERROR,
		     "internal error - device type of %s not set when expected\n",
		     device->gpsdata.dev.path);
	else
	{
	    char id2[GPS_JSON_RESPONSE_MAX];
	    json_device_dump(device, id2, sizeof(id2));
	    notify_watchers(device, true, false, id2);
	}
    }
#endif /* SOCKET_EXPORT_ENABLE */

    /*
     * If the device provided an RTCM packet, repeat it to all devices.
     */
    if ((changed & RTCM2_SET) != 0 || (changed & RTCM3_SET) != 0) {
	if ((changed & RTCM2_SET) != 0
                   && device->lexer.outbuflen > RTCM_MAX) {
	    gpsd_log(&context.errout, LOG_ERROR,
		     "overlong RTCM packet (%zd bytes)\n",
		     device->lexer.outbuflen);
	} else if ((changed & RTCM3_SET) != 0
		   && device->lexer.outbuflen > RTCM3_MAX) {
	    gpsd_log(&context.errout, LOG_ERROR,
		     "overlong RTCM3 packet (%zd bytes)\n",
		     device->lexer.outbuflen);
	} else {
	    struct gps_device_t *dp;
	    for (dp = devices; dp < devices+MAX_DEVICES; dp++) {
		if (allocated_device(dp)) {
/* *INDENT-OFF* */
		    if (dp->device_type->rtcm_writer != NULL) {
			if (dp->device_type->rtcm_writer(dp,
							     (const char *)device->lexer.outbuffer,
							     device->lexer.outbuflen) == 0)
			    gpsd_log(&context.errout, LOG_ERROR,
				     "Write to RTCM sink failed\n");
			else {
			    gpsd_log(&context.errout, LOG_IO,
				     "<= DGPS: %zd bytes of RTCM relayed.\n",
				     device->lexer.outbuflen);
			}
		    }
/* *INDENT-ON* */
		}
	    }
	}
    }


#ifdef NTP_ENABLE
    /*
     * Time is eligible for shipping to NTPD if the driver has
     * asserted NTPTIME_IS at any point in the current cycle.
     */
    if ((changed & CLEAR_IS)!=0)
	device->ship_to_ntpd = false;
    if ((changed & NTPTIME_IS)!=0)
	device->ship_to_ntpd = true;
    /*
     * Only update the NTP time if we've seen the leap-seconds data.
     * Else we may be providing GPS time.
     */
    if ((changed & TIME_SET) == 0) {
	//gpsd_log(&context.errout, LOG_PROG, "NTP: No time this packet\n");
    } else if ( 0 >= device->fixcnt && !batteryRTC ) {
        /* many GPS spew random times until a valid GPS fix */
        /* allow override with -r optin */
	//gpsd_log(&context.errout, LOG_PROG, "NTP: no fix\n");
    } else if (0 == isfinite(device->newdata.time)) {
	//gpsd_log(&context.errout, LOG_PROG, "NTP: bad new time\n");
#if defined(PPS_ENABLE)
    } else if (device->newdata.time <= device->pps_thread.fix_in.real.tv_sec) {
	//gpsd_log(&context.errout, LOG_PROG, "NTP: Not a new time\n");
#endif /* PPS_ENABLE */
    } else if (!device->ship_to_ntpd) {
	//gpsd_log(&context.errout, LOG_PROG, "NTP: No precision time report\n");
    } else {
	struct timedelta_t td;
#if defined(PPS_ENABLE)
	struct gps_device_t *ppsonly;
#endif /* PPS_ENABLE */

	ntp_latch(device, &td);

#if defined(PPS_ENABLE)
	/* propagate this in-band-time to all PPS-only devices */
	for (ppsonly = devices; ppsonly < devices + MAX_DEVICES; ppsonly++)
	    if (ppsonly->sourcetype == source_pps)
		pps_thread_fixin(&ppsonly->pps_thread, &td);
#endif /* PPS_ENABLE */

#ifdef NTPSHM_ENABLE
	if (device->shm_clock != NULL) {
	    (void)ntpshm_put(device, device->shm_clock, &td);
	}
#endif /* NTPSHM_ENABLE */

#ifdef SOCKET_EXPORT_ENABLE
	notify_watchers(device, false, true,
			"{\"class\":\"TOFF\",\"device\":\"%s\",\"real_sec\":%ld, \"real_nsec\":%ld,\"clock_sec\":%ld,\"clock_nsec\":%ld}\r\n",
			device->gpsdata.dev.path,
			td.real.tv_sec, td.real.tv_nsec,
			td.clock.tv_sec, td.clock.tv_nsec);
#endif /* SOCKET_EXPORT_ENABLE */

    }
#endif /* NTP_ENABLE */

    /*
     * If no reliable end of cycle, must report every time
     * a sentence changes position or mode. Likely to
     * cause display jitter.
     */
    if (!device->cycle_end_reliable && (changed & (LATLON_SET | MODE_SET))!=0)
	changed |= REPORT_IS;

    /* a few things are not per-subscriber reports */
    if ((changed & REPORT_IS) != 0) {
#ifdef NETFEED_ENABLE
	if (device->gpsdata.fix.mode == MODE_3D) {
	    struct gps_device_t *dgnss;
	    /*
	     * Pass the fix to every potential caster, here.
	     * netgnss_report() individual caster types get to
	     * make filtering decisiona.
	     */
	    for (dgnss = devices; dgnss < devices + MAX_DEVICES; dgnss++)
		if (dgnss != device)
		    netgnss_report(&context, device, dgnss);
	}
#endif /* NETFEED_ENABLE */
#if defined(DBUS_EXPORT_ENABLE)
	if (device->gpsdata.fix.mode > MODE_NO_FIX)
	    send_dbus_fix(device);
#endif /* defined(DBUS_EXPORT_ENABLE) */
    }

#ifdef SHM_EXPORT_ENABLE
    if ((changed & (REPORT_IS|GST_SET|SATELLITE_SET|SUBFRAME_SET|
		    ATTITUDE_SET|RTCM2_SET|RTCM3_SET|AIS_SET)) != 0)
	shm_update(&context, &device->gpsdata);
#endif /* SHM_EXPORT_ENABLE */

#ifdef SOCKET_EXPORT_ENABLE
    /* update all subscribers associated with this device */
    for (sub = subscribers; sub < subscribers + MAX_CLIENTS; sub++) {
	if (sub == NULL || sub->active == 0 || !subscribed(sub, device))
	    continue;

#ifdef PASSTHROUGH_ENABLE
	/* this is for passing through JSON packets */
	if ((changed & PASSTHROUGH_IS) != 0) {
	    (void)strlcat((char *)device->lexer.outbuffer,
			  "\r\n",
			  sizeof(device->lexer.outbuffer));
	    (void)throttled_write(sub,
				  (char *)device->lexer.outbuffer,
				  device->lexer.outbuflen+2);
	    continue;
	}
#endif /* PASSTHROUGH_ENABLE */

	/* report raw packets to users subscribed to those */
	raw_report(sub, device);

	/* some listeners may be in watcher mode */
	if (sub->policy.watcher) {
	    if (changed & DATA_IS) {
		/* guard keeps mask dumper from eating CPU */
		if (context.errout.debug >= LOG_PROG)
		    gpsd_log(&context.errout, LOG_PROG,
			     "Changed mask: %s with %sreliable cycle detection\n",
			     gps_maskdump(changed),
			     device->cycle_end_reliable ? "" : "un");
		if ((changed & REPORT_IS) != 0)
		    gpsd_log(&context.errout, LOG_PROG,
			     "time to report a fix\n");

		if (sub->policy.nmea)
		    pseudonmea_report(sub, changed, device);

		if (sub->policy.json)
		{
		    char buf[GPS_JSON_RESPONSE_MAX * 4];

		    if ((changed & AIS_SET) != 0)
			if (device->gpsdata.ais.type == 24
			    && device->gpsdata.ais.type24.part != both
			    && !sub->policy.split24)
			    continue;

		    json_data_report(changed,
				     device, &sub->policy,
				     buf, sizeof(buf));
		    if (buf[0] != '\0')
			(void)throttled_write(sub, buf, strlen(buf));

		}
	    }
	}
    } /* subscribers */
#endif /* SOCKET_EXPORT_ENABLE */
}

#ifdef SOCKET_EXPORT_ENABLE
/* Execute GPSD requests (?POLL, ?WATCH, etc.) from a buffer.
 * The entire request must be in the buffer.
 */
static int handle_gpsd_request(struct subscriber_t *sub, const char *buf)
{
    char reply[GPS_JSON_RESPONSE_MAX + 1];

    reply[0] = '\0';
    if (buf[0] == '?') {
	const char *end;
	for (end = buf; *buf != '\0'; buf = end)
	    if (isspace((unsigned char) *buf))
		end = buf + 1;
	    else
		handle_request(sub, buf, &end,
			       reply + strlen(reply),
			       sizeof(reply) - strlen(reply));
    }
    return (int)throttled_write(sub, reply, strlen(reply));
}
#endif /* SOCKET_EXPORT_ENABLE */

#if defined(CONTROL_SOCKET_ENABLE) && defined(PPS_ENABLE) && defined(SOCKET_EXPORT_ENABLE)
static void ship_pps_message(struct gps_device_t *session,
				   struct timedelta_t *td)
/* on PPS interrupt, ship a message to all clients */
{
    int precision = -20;

    if ( source_usb == session->sourcetype) {
        /* PPS over USB not so good */
	precision = -10;
    }

    /* real_XXX - the time the GPS thinks it is at the PPS edge */
    /* clock_XXX - the time the system clock thinks it is at the PPS edge */
    notify_watchers(session, true, true,
		    "{\"class\":\"PPS\",\"device\":\"%s\",\"real_sec\":%ld, \"real_nsec\":%ld,\"clock_sec\":%ld,\"clock_nsec\":%ld,\"precision\":%d}\r\n",
		    session->gpsdata.dev.path,
		    td->real.tv_sec, td->real.tv_nsec,
		    td->clock.tv_sec, td->clock.tv_nsec,
                    precision);

    /*
     * PPS receipt resets the device's timeout.  This keeps PPS-only
     * devices, which never deliver in-band data, from timing out.
     */
    session->gpsdata.online = timestamp();
}
#endif


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
	gpsd_log(&context.errout, LOG_ERROR, "no DGPS server list found.\n");
	return;
    }

    for (sp = keep; sp < keep + SERVER_SAMPLE; sp++) {
	sp->dist = DGPS_THRESHOLD;
	sp->server[0] = '\0';
    }
    hold.lat = hold.lon = 0;
    while (fgets(buf, (int)sizeof(buf), sfp)) {
	char *cp = strchr(buf, '#');
	if (cp != NULL)
	    *cp = '\0';
	if (sscanf(buf, "%32lf %32lf %256s", &hold.lat, &hold.lon, hold.server) ==
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
		*tp = hold;
	}
    }
    (void)fclose(sfp);

    if (keep[0].server[0] == '\0') {
	gpsd_log(&context.errout, LOG_ERROR,
		 "no DGPS servers within %dm.\n",
		 (int)(DGPS_THRESHOLD / 1000));
	return;
    }

    /* sort them and try the closest first */
    qsort((void *)keep, SERVER_SAMPLE, sizeof(struct dgps_server_t), srvcmp);
    for (sp = keep; sp < keep + SERVER_SAMPLE; sp++) {
	if (sp->server[0] != '\0') {
	    gpsd_log(&context.errout, LOG_INF,
		     "%s is %dkm away.\n", sp->server,
		     (int)(sp->dist / 1000));
	    if (dgpsip_open(context, sp->server) >= 0)
		break;
	}
    }
}
#endif /* __UNUSED_AUTOCONNECT__ */

#ifdef PPS_ENABLE
#define CONDITIONALLY_UNUSED
#else
#define CONDITIONALLY_UNUSED UNUSED
#endif /* PPS_ENABLE */
static void gpsd_terminate(struct gps_context_t *context CONDITIONALLY_UNUSED)
/* finish cleanly, reverting device configuration */
{
    int dfd;

    for (dfd = 0; dfd < MAX_DEVICES; dfd++) {
	if (allocated_device(&devices[dfd])) {
	    (void)gpsd_wrap(&devices[dfd]);
	}
    }
#ifdef PPS_ENABLE
    context->pps_hook = NULL;	/* tell any PPS-watcher thread to die */
#endif /* PPS_ENABLE */
}

int main(int argc, char *argv[])
{
    /* some of these statics suppress -W warnings due to longjmp() */
#ifdef SOCKET_EXPORT_ENABLE
    static char *gpsd_service = NULL;
    struct subscriber_t *sub;
#endif /* SOCKET_EXPORT_ENABLE */
    fd_set rfds;
#ifdef CONTROL_SOCKET_ENABLE
    fd_set control_fds;
#endif /* CONTROL_SOCKET_ENABLE */
#ifdef CONTROL_SOCKET_ENABLE
    static socket_t csock;
    socket_t cfd;
    static char *control_socket = NULL;
#endif /* CONTROL_SOCKET_ENABLE */
#if defined(SOCKET_EXPORT_ENABLE) || defined(CONTROL_SOCKET_ENABLE)
    sockaddr_t fsin;
#endif /* defined(SOCKET_EXPORT_ENABLE) || defined(CONTROL_SOCKET_ENABLE) */
    static char *pid_file = NULL;
    struct gps_device_t *device;
    int i, option;
    int msocks[2] = {-1, -1};
    bool device_opened = false;
    bool go_background = true;
    volatile bool in_restart;

    gps_context_init(&context, "gpsd");

#ifdef CONTROL_SOCKET_ENABLE
    INVALIDATE_SOCKET(csock);
#if defined(PPS_ENABLE) && defined(SOCKET_EXPORT_ENABLE)
    context.pps_hook = ship_pps_message;
#endif /* PPS_ENABLE && SOCKET_EXPORT_ENABLE */
#endif /* CONTROL_SOCKET_ENABLE */

    while ((option = getopt(argc, argv, "F:D:S:bGhlNnrP:V")) != -1) {
	switch (option) {
	case 'D':
	    context.errout.debug = (int)strtol(optarg, 0, 0);
#ifdef CLIENTDEBUG_ENABLE
	    gps_enable_debug(context.errout.debug, stderr);
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
#ifndef FORCE_GLOBAL_ENABLE
	case 'G':
	    listen_global = true;
	    break;
#endif /* FORCE_GLOBAL_ENABLE */
	case 'l':		/* list known device types and exit */
	    typelist();
	    break;
	case 'S':
#ifdef SOCKET_EXPORT_ENABLE
	    gpsd_service = optarg;
#endif /* SOCKET_EXPORT_ENABLE */
	    break;
	case 'n':
#ifndef FORCE_NOWAIT
	    nowait = true;
#endif /* FORCE_NOWAIT */
	    break;
	case 'r':
	    batteryRTC = true;
	    break;
	case 'P':
	    pid_file = optarg;
	    break;
	case 'V':
	    (void)printf("%s: %s (revision %s)\n", argv[0], VERSION, REVISION);
	    exit(EXIT_SUCCESS);
	case 'h':
	case '?':
	default:
	    usage();
	    exit(EXIT_SUCCESS);
	}
    }

    /* sanity check */
    if (argc - optind > MAX_DEVICES) {
	gpsd_log(&context.errout, LOG_ERROR,
		 "too many devices on command line\n");
	exit(1);
    }

#if defined(SYSTEMD_ENABLE) && defined(CONTROL_SOCKET_ENABLE)
    sd_socket_count = sd_get_socket_count();
    if (sd_socket_count > 0 && control_socket != NULL) {
        gpsd_log(&context.errout, LOG_WARN,
		 "control socket passed on command line ignored\n");
        control_socket = NULL;
    }
#endif

#if defined(CONTROL_SOCKET_ENABLE) || defined(SYSTEMD_ENABLE)
    if (
#ifdef CONTROL_SOCKET_ENABLE
	control_socket == NULL
#endif
#if defined(CONTROL_SOCKET_ENABLE) && defined(SYSTEMD_ENABLE)
	&&
#endif
#ifdef SYSTEMD_ENABLE
	sd_socket_count <= 0
#endif
	&& optind >= argc) {
	gpsd_log(&context.errout, LOG_ERROR,
		 "can't run with neither control socket nor devices\n");
	exit(EXIT_FAILURE);
    }

    /*
     * Control socket has to be created before we go background in order to
     * avoid a race condition in which hotplug scripts can try opening
     * the socket before it's created.
     */
#if defined(SYSTEMD_ENABLE) && defined(CONTROL_SOCKET_ENABLE)
    if (sd_socket_count > 0) {
        csock = SD_SOCKET_FDS_START;
        FD_SET(csock, &all_fds);
        adjust_max_fd(csock, true);
    }
#endif
#ifdef CONTROL_SOCKET_ENABLE
    if (control_socket) {
	(void)unlink(control_socket);
	if (BAD_SOCKET(csock = filesock(control_socket))) {
	    gpsd_log(&context.errout, LOG_ERROR,
		     "control socket create failed, netlib error %d\n",
		     csock);
	    exit(EXIT_FAILURE);
	} else
	    gpsd_log(&context.errout, LOG_SPIN,
		     "control socket %s is fd %d\n",
		     control_socket, csock);
	FD_SET(csock, &all_fds);
	adjust_max_fd(csock, true);
	gpsd_log(&context.errout, LOG_PROG,
		 "control socket opened at %s\n",
		 control_socket);
    }
#endif /* CONTROL_SOCKET_ENABLE */
#else
    if (optind >= argc) {
	gpsd_log(&context.errout, LOG_ERROR,
		 "can't run with no devices specified\n");
	exit(EXIT_FAILURE);
    }
#endif /* defined(CONTROL_SOCKET_ENABLE) || defined(SYSTEMD_ENABLE) */


    /* might be time to daemonize */
    if (go_background) {
	/* not SuS/POSIX portable, but we have our own fallback version */
	if (os_daemon(0, 0) != 0)
	    gpsd_log(&context.errout, LOG_ERROR,
		     "daemonization failed: %s\n",strerror(errno));
    }

    if (pid_file != NULL) {
	FILE *fp;

	if ((fp = fopen(pid_file, "w")) != NULL) {
	    (void)fprintf(fp, "%u\n", (unsigned int)getpid());
	    (void)fclose(fp);
	} else {
	    gpsd_log(&context.errout, LOG_ERROR,
		     "Cannot create PID file: %s.\n", pid_file);
	}
    }

    openlog("gpsd", LOG_PID, LOG_USER);
    gpsd_log(&context.errout, LOG_INF, "launching (Version %s)\n", VERSION);

#ifdef SOCKET_EXPORT_ENABLE
    if (!gpsd_service)
	gpsd_service =
	    getservbyname("gpsd", "tcp") ? "gpsd" : DEFAULT_GPSD_PORT;
    if (passivesocks(gpsd_service, "tcp", QLEN, msocks) < 1) {
	gpsd_log(&context.errout, LOG_ERR,
		 "command sockets creation failed, netlib errors %d, %d\n",
		 msocks[0], msocks[1]);
	if (pid_file != NULL)
	    (void)unlink(pid_file);
	exit(EXIT_FAILURE);
    }
    gpsd_log(&context.errout, LOG_INF, "listening on port %s\n", gpsd_service);
#endif /* SOCKET_EXPORT_ENABLE */

#ifdef NTPSHM_ENABLE
    if (getuid() == 0) {
	errno = 0;
	// nice() can ONLY succeed when run as root!
	// do not even bother as non-root
	if (nice(NICEVAL) == -1 && errno != 0)
	    gpsd_log(&context.errout, LOG_WARN,
		     "PPS: o=priority setting failed. Time accuracty will be degraded\n");
    }
    /*
     * By initializing before we drop privileges, we guarantee that even
     * hotplugged devices added *after* we drop privileges will be able
     * to use segments 0 and 1.
     */
    (void)ntpshm_context_init(&context);
#endif /* NTPSHM_ENABLE */

#if defined(DBUS_EXPORT_ENABLE)
    /* we need to connect to dbus as root */
    if (initialize_dbus_connection()) {
	/* the connection could not be started */
	gpsd_log(&context.errout, LOG_ERROR,
		 "unable to connect to the DBUS system bus\n");
    } else
	gpsd_log(&context.errout, LOG_PROG,
		 "successfully connected to the DBUS system bus\n");
#endif /* defined(DBUS_EXPORT_ENABLE) */

#ifdef SHM_EXPORT_ENABLE
    /* create the shared segment as root so readers can't mess with it */
    (void)shm_acquire(&context);
#endif /* SHM_EXPORT_ENABLE */

    /*
     * We open devices specified on the command line *before* dropping
     * privileges in case one of them is a serial device with PPS support
     * and we need to set the line discipline, which requires root.
     */
    in_restart = false;
    for (i = optind; i < argc; i++) {
      if (!gpsd_add_device(argv[i], NOWAIT)) {
	    gpsd_log(&context.errout, LOG_ERROR,
		     "initial GPS device %s open failed\n",
		     argv[i]);
	} else {
            device_opened = true;
	}
    }

    if (
#ifdef CONTROL_SOCKET_ENABLE
       control_socket == NULL &&
#endif
#ifdef SYSTEMD_ENABLE
       sd_socket_count <= 0 &&
#endif
       !device_opened) {
       gpsd_log(&context.errout, LOG_ERROR,
                "can't run with neither control socket nor devices open\n");
       exit(EXIT_FAILURE);
    }


    /* drop privileges */
    if (0 == getuid()) {
	struct passwd *pw;
	struct stat stb;

	/* make default devices accessible even after we drop privileges */
	for (i = optind; i < argc; i++)
	    /* coverity[toctou] */
	    if (stat(argv[i], &stb) == 0)
		(void)chmod(argv[i], stb.st_mode | S_IRGRP | S_IWGRP);
	/*
	 * Drop privileges.  Up to now we've been running as root.
	 * Instead, set the user ID to 'nobody' (or whatever the gpsd
	 * user set by thre build is) and the group ID to the owning
	 * group of a prototypical TTY device. This limits the scope
	 * of any compromises in the code.  It requires that all GPS
	 * devices have their group read/write permissions set.
	 */
	if (setgroups(0, NULL) != 0)
	    gpsd_log(&context.errout, LOG_ERROR,
		     "setgroups() failed, errno %s\n",
		     strerror(errno));
#ifdef GPSD_GROUP
	{
	    struct group *grp = getgrnam(GPSD_GROUP);
	    if (grp)
		if (setgid(grp->gr_gid) != 0)
		    gpsd_log(&context.errout, LOG_ERROR,
			     "setgid() failed, errno %s\n",
			     strerror(errno));
	}
#else
	if ((optind < argc && stat(argv[optind], &stb) == 0)
	    || stat(PROTO_TTY, &stb) == 0) {
	    gpsd_log(&context.errout, LOG_PROG,
		     "changing to group %d\n", stb.st_gid);
	    if (setgid(stb.st_gid) != 0)
		gpsd_log(&context.errout, LOG_ERROR,
			 "setgid() failed, errno %s\n",
			 strerror(errno));
	}
#endif
	pw = getpwnam(GPSD_USER);
	if (pw)
	    if (setuid(pw->pw_uid) != 0)
		gpsd_log(&context.errout, LOG_ERROR,
			    "setuid() failed, errno %s\n",
			    strerror(errno));
    }
    gpsd_log(&context.errout, LOG_INF,
	     "running with effective group ID %d\n", getegid());
    gpsd_log(&context.errout, LOG_INF,
	     "running with effective user ID %d\n", geteuid());

#ifdef SOCKET_EXPORT_ENABLE
    for (i = 0; i < NITEMS(subscribers); i++) {
	subscribers[i].fd = UNALLOCATED_FD;
	(void)pthread_mutex_init(&subscribers[i].mutex, NULL);
    }
#endif /* SOCKET_EXPORT_ENABLE*/

    {
	struct sigaction sa;

	sa.sa_flags = 0;
#ifdef __COVERITY__
	/*
	 * Obsolete and unused.  We're only doing this to pacify Coverity
	 * which otherwise throws an UNINIT event here. Don't swap with the
	 * handler initialization, they're unioned on some architectures.
	 */
	sa.sa_restorer = NULL;
#endif /* __COVERITY__ */
	sa.sa_handler = onsig;
	(void)sigfillset(&sa.sa_mask);
	(void)sigaction(SIGHUP, &sa, NULL);
	(void)sigaction(SIGINT, &sa, NULL);
	(void)sigaction(SIGTERM, &sa, NULL);
	(void)sigaction(SIGQUIT, &sa, NULL);
	(void)signal(SIGPIPE, SIG_IGN);
    }

    /* daemon got termination or interrupt signal */
    if (setjmp(restartbuf) > 0) {
	gpsd_terminate(&context);
	in_restart = true;
	gpsd_log(&context.errout, LOG_WARN, "gpsd restarted by SIGHUP\n");
    }

    signalled = 0;

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

    /*
     * If we got here via SIGINT, reopen any command-line devices. PPS
     * through these won't work, as we've dropped privileges and can
     * no longer change line disciplines.
     */
    if (in_restart)
	for (i = optind; i < argc; i++) {
	  if (!gpsd_add_device(argv[i], NOWAIT)) {
		gpsd_log(&context.errout, LOG_ERROR,
			 "GPS device %s open failed\n",
			 argv[i]);
	    }
	}

    while (0 == signalled) {
	fd_set efds;

	switch(gpsd_await_data(&rfds, &efds, maxfd, &all_fds, &context.errout))
	{
	case AWAIT_GOT_INPUT:
	    break;
	case AWAIT_NOT_READY:
	    for (device = devices; device < devices + MAX_DEVICES; device++)
		/*
		 * The file descriptor validity check is reqiured on some ARM
		 * platforms to prevent a core dump.  This may be due to an
		 * implimentation error in FD_ISSET().
		 */
		if (allocated_device(device)
		    && (0 <= device->gpsdata.gps_fd && device->gpsdata.gps_fd < (socket_t)FD_SETSIZE)
		    && FD_ISSET(device->gpsdata.gps_fd, &efds)) {
		    deactivate_device(device);
		    free_device(device);
		}
	    continue;
	case AWAIT_FAILED:
	    exit(EXIT_FAILURE);
	}

#ifdef SOCKET_EXPORT_ENABLE
	/* always be open to new client connections */
	for (i = 0; i < AFCOUNT; i++) {
	    if (msocks[i] >= 0 && FD_ISSET(msocks[i], &rfds)) {
		socklen_t alen = (socklen_t) sizeof(fsin);
		socket_t ssock =
		    accept(msocks[i], (struct sockaddr *)&fsin, &alen);

		if (BAD_SOCKET(ssock))
		    gpsd_log(&context.errout, LOG_ERROR,
			     "accept: %s\n", strerror(errno));
		else {
		    struct subscriber_t *client = NULL;
		    int opts = fcntl(ssock, F_GETFL);
		    static struct linger linger = { 1, RELEASE_TIMEOUT };
		    char *c_ip;

		    if (opts >= 0)
			(void)fcntl(ssock, F_SETFL, opts | O_NONBLOCK);

		    c_ip = netlib_sock2ip(ssock);
		    client = allocate_client();
		    if (client == NULL) {
			gpsd_log(&context.errout, LOG_ERROR,
				 "Client %s connect on fd %d -"
				 "no subscriber slots available\n", c_ip,
				    ssock);
			(void)close(ssock);
		    } else
			if (setsockopt
			    (ssock, SOL_SOCKET, SO_LINGER, (char *)&linger,
			     (int)sizeof(struct linger)) == -1) {
			gpsd_log(&context.errout, LOG_ERROR,
				 "Error: SETSOCKOPT SO_LINGER\n");
			(void)close(ssock);
		    } else {
			char announce[GPS_JSON_RESPONSE_MAX];
			FD_SET(ssock, &all_fds);
			adjust_max_fd(ssock, true);
			client->fd = ssock;
			client->active = time(NULL);
			gpsd_log(&context.errout, LOG_SPIN,
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
	    socket_t ssock = accept(csock, (struct sockaddr *)&fsin, &alen);

	    if (BAD_SOCKET(ssock))
		gpsd_log(&context.errout, LOG_ERROR,
			 "accept: %s\n", strerror(errno));
	    else {
		gpsd_log(&context.errout, LOG_INF,
			 "control socket connect on fd %d\n",
			 ssock);
		FD_SET(ssock, &all_fds);
		FD_SET(ssock, &control_fds);
		adjust_max_fd(ssock, true);
	    }
	    FD_CLR(csock, &rfds);
	}

	/* read any commands that came in over the control socket */
	for (cfd = 0; cfd < (int)FD_SETSIZE; cfd++)
	    if (FD_ISSET(cfd, &control_fds)) {
		char buf[BUFSIZ];
		ssize_t rd;

		while ((rd = read(cfd, buf, sizeof(buf) - 1)) > 0) {
		    buf[rd] = '\0';
		    gpsd_log(&context.errout, LOG_CLIENT,
			     "<= control(%d): %s\n", cfd, buf);
		    /* coverity[tainted_data] Safe, never handed to exec */
		    handle_control(cfd, buf);
		}
		gpsd_log(&context.errout, LOG_SPIN,
			 "close(%d) of control socket\n", cfd);
		(void)close(cfd);
		FD_CLR(cfd, &all_fds);
		FD_CLR(cfd, &control_fds);
		adjust_max_fd(cfd, false);
	    }
#endif /* CONTROL_SOCKET_ENABLE */

	/* poll all active devices */
	for (device = devices; device < devices + MAX_DEVICES; device++)
	    if (allocated_device(device) && device->gpsdata.gps_fd > 0)
		switch (gpsd_multipoll(FD_ISSET(device->gpsdata.gps_fd, &rfds),
				       device, all_reports, DEVICE_REAWAKE))
		{
		case DEVICE_READY:
		    FD_SET(device->gpsdata.gps_fd, &all_fds);
		    adjust_max_fd(device->gpsdata.gps_fd, true);
		    break;
		case DEVICE_UNREADY:
		    FD_CLR(device->gpsdata.gps_fd, &all_fds);
		    adjust_max_fd(device->gpsdata.gps_fd, false);
		    break;
		case DEVICE_ERROR:
		case DEVICE_EOF:
		    deactivate_device(device);
		    break;
		default:
		    break;
		}

#ifdef __UNUSED_AUTOCONNECT__
	if (context.fixcnt > 0 && !context.autconnect) {
	    for (device = devices; device < devices + MAX_DEVICES; device++) {
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
	for (sub = subscribers; sub < subscribers + MAX_CLIENTS; sub++) {
	    if (sub->active == 0)
		continue;

	    lock_subscriber(sub);
	    if (FD_ISSET(sub->fd, &rfds)) {
		char buf[BUFSIZ];
		int buflen;

		unlock_subscriber(sub);

		gpsd_log(&context.errout, LOG_PROG,
			 "checking client(%d)\n",
			 sub_index(sub));
		if ((buflen =
		     (int)recv(sub->fd, buf, sizeof(buf) - 1, 0)) <= 0) {
		    detach_client(sub);
		} else {
		    if (buf[buflen - 1] != '\n')
			buf[buflen++] = '\n';
		    buf[buflen] = '\0';
		    gpsd_log(&context.errout, LOG_CLIENT,
			     "<= client(%d): %s\n", sub_index(sub), buf);

		    /*
		     * When a command comes in, update subscriber.active to
		     * timestamp() so we don't close the connection
		     * after COMMAND_TIMEOUT seconds. This makes
		     * COMMAND_TIMEOUT useful.
		     */
		    sub->active = time(NULL);
		    if (handle_gpsd_request(sub, buf) < 0)
			detach_client(sub);
		}
	    } else {
		unlock_subscriber(sub);

		if (!sub->policy.watcher
		    && time(NULL) - sub->active > COMMAND_TIMEOUT) {
		    gpsd_log(&context.errout, LOG_WARN,
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
	 *
	 * Re-poll devices that are disconnected, but have potential
	 * subscribers in the same cycle.
	 */
	for (device = devices; device < devices + MAX_DEVICES; device++) {

	    bool device_needed = NOWAIT;

	    if (!allocated_device(device))
		continue;

	    if (!device_needed)
		for (sub=subscribers; sub<subscribers+MAX_CLIENTS; sub++) {
		    if (sub->active == 0)
			continue;
		    device_needed = subscribed(sub, device);
		    if (device_needed)
			break;
		}

	    if (!device_needed && device->gpsdata.gps_fd > -1 &&
		    device->lexer.type != BAD_PACKET) {
		if (device->releasetime == 0) {
		    device->releasetime = time(NULL);
		    gpsd_log(&context.errout, LOG_PROG,
			     "device %d (fd %d) released\n",
			     (int)(device - devices),
			     device->gpsdata.gps_fd);
		} else if (time(NULL) - device->releasetime > RELEASE_TIMEOUT) {
		    gpsd_log(&context.errout, LOG_PROG,
			     "device %d closed\n",
			     (int)(device - devices));
		    gpsd_log(&context.errout, LOG_RAW,
			     "unflagging descriptor %d\n",
			     device->gpsdata.gps_fd);
		    deactivate_device(device);
		}
	    }

	    if (device_needed && BAD_SOCKET(device->gpsdata.gps_fd) &&
		    (device->opentime == 0 ||
		    time(NULL) - device->opentime > DEVICE_RECONNECT)) {
		device->opentime = time(NULL);
		gpsd_log(&context.errout, LOG_INF,
			 "reconnection attempt on device %d\n",
			 (int)(device - devices));
		(void)awaken(device);
	    }
	}
#endif /* SOCKET_EXPORT_ENABLE */

	/*
	 * Might be time for graceful shutdown if no command-line
	 * devices were specified, there are no subscribers, there are
	 * no active devices, and there *have been* active
	 * devices. The goal is to go away and free up text space when
	 * the daemon was hotplug-activated but there are no
	 * subscribers and the last GPS has unplugged, and the point
	 * of the last check is to prevent shutdown when the daemon
	 * has been launched but not yet received its first device
	 * over the socket.
	 */
	if (argc == optind && highwater > 0) {
	    int subcount = 0, devcount = 0;
#ifdef SOCKET_EXPORT_ENABLE
	    for (sub = subscribers; sub < subscribers + MAX_CLIENTS; sub++)
		if (sub->active != 0)
		    ++subcount;
#endif /* SOCKET_EXPORT_ENABLE */
	    for (device = devices; device < devices + MAX_DEVICES; device++)
		if (allocated_device(device))
		    ++devcount;
	    if (subcount == 0 && devcount == 0) {
		gpsd_log(&context.errout, LOG_SHOUT,
			 "no subscribers or devices, shutting down.\n");
		goto shutdown;
	    }
	}
    }

    /* if we make it here, we got a signal... deal with it */
    /* restart on SIGHUP, clean up and exit otherwise */
    if (SIGHUP == (int)signalled)
	longjmp(restartbuf, 1);

    gpsd_log(&context.errout, LOG_WARN,
	     "received terminating signal %d.\n", (int)signalled);
shutdown:
    gpsd_terminate(&context);

    gpsd_log(&context.errout, LOG_WARN, "exiting.\n");

#ifdef SOCKET_EXPORT_ENABLE
    /*
     * A linger option was set on each client socket when it was
     * created.  Now, shut them down gracefully, letting I/O drain.
     * This is an attempt to avoid the sporadic race errors at the ends
     * of our regression tests.
     */
    for (sub = subscribers; sub < subscribers + MAX_CLIENTS; sub++) {
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

