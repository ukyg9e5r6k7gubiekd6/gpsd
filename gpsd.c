/* $Id$ */
#include <sys/types.h>
#ifndef S_SPLINT_S
#include <sys/socket.h>
#endif /* S_SPLINT_S */
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <syslog.h>
#include <signal.h>
#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <string.h>
#include <netdb.h>
#include <stdarg.h>
#include <setjmp.h>
#include <stdio.h>
#include <assert.h>
#include <pwd.h>
#include <grp.h>
#include <stdbool.h>
#include <math.h>

#include "gpsd_config.h"
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
#include "gps_json.h"
#include "timebase.h"

/*
 * The name of a tty device from which to pick up whatever the local
 * owning group for tty devices is.  Used when we drop privileges.
 */
#if defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
#define PROTO_TTY "/dev/tty00"  /* correct for *BSD */
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
 * reclaim client fds.  ASSIGNMENT_TIMEOUT fends off programs
 * that open connections and just sit there, not issuing a WATCH or
 * doing anything else that triggers a device assignment.  Clients
 * in watcher or raw mode that don't read their data will get dropped
 * when throttled_write() fills up the outbound buffers and the
 * NOREAD_TIMEOUT expires.  Clients in the original polling mode have
 * to be timed out as well; that's what POLLER_TIMOUT is for.  
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
#define ASSIGNMENT_TIMEOUT	60
#define NOREAD_TIMEOUT		60*3
#define POLLER_TIMEOUT  	60*15
#define RELEASE_TIMEOUT		60

#define QLEN			5

/* 
 * If ntpshm is enabled, we renice the process to this priority level.
 * For precise timekeeping increase priority.
 */
#define NICEVAL	-10

/* Needed because 4.x versions of GCC are really annoying */
#define ignore_return(funcall)	assert(funcall != -23)

/*
 * Manifest names for the gnss_type enum - must be kept synced with it.
 * Also, masks so we can tell what packet types correspond to each class.
 */
struct classmap_t classmap[CLASSMAP_NITEMS] = {
    /* name	typemask	packetmask */
    {"ANY",	0,       	0},
    {"GPS",	SEEN_GPS, 	GPS_TYPEMASK},
    {"RTCM2",	SEEN_RTCM2,	PACKET_TYPEMASK(RTCM2_PACKET)},
    {"RTCM3",	SEEN_RTCM3,	PACKET_TYPEMASK(RTCM3_PACKET)},
    {"AIS",	SEEN_AIS,  	PACKET_TYPEMASK(AIVDM_PACKET)},
};

static fd_set all_fds;
static int maxfd;
static int debuglevel;
static bool in_background = false;
static bool listen_global = false;
static bool nowait = false;
static jmp_buf restartbuf;

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

static volatile sig_atomic_t signalled;

static void onsig(int sig)
{
    /* just set a variable, and deal with it in the main loop */
    signalled = (sig_atomic_t)sig;
}

static int daemonize(void)
{
    int fd;
    pid_t pid;

    /*@ -type @*/	/* weirdly, splint 3.1.2 is confused by fork() */
    switch (pid = fork()) {
    case -1:
	return -1;
    case 0:	/* child side */
	break;
    default:	/* parent side */
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

void gpsd_report(int errlevel, const char *fmt, ... )
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
	va_start(ap, fmt) ;
	(void)vsnprintf(buf + strlen(buf), sizeof(buf)-strlen(buf), fmt, ap);
	va_end(ap);

	buf2[0] = '\0';
	for (sp = buf; *sp != '\0'; sp++)
	    if (isprint(*sp) || (isspace(*sp) && (sp[1]=='\0' || sp[2]=='\0')))
		(void)snprintf(buf2+strlen(buf2), 2, "%c", *sp);
	    else
		(void)snprintf(buf2+strlen(buf2), 6, "\\x%02x", (unsigned)*sp);

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

    (void)printf("usage: gpsd [-b] [-n] [-N] [-D n] [-F sockfile] [-P pidfile] [-S port] [-h] device...\n\
  Options include: \n\
  -b		     	    = bluetooth-safe: open data sources read-only\n\
  -n			    = don't wait for client connects to poll GPS\n\
  -N			    = don't go into background\n\
  -F sockfile		    = specify control socket location\n\
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

static int passivesock(char *service, char *protocol, int qlen)
{
    struct servent *pse;
    struct protoent *ppe ;	/* splint has a bug here */
    struct sockaddr_in sin;
    int s, type, proto, one = 1;

    /*@ -mustfreefresh +matchanyintegral @*/
    memset((char *) &sin, 0, sizeof(sin));
    sin.sin_family = (sa_family_t)AF_INET;
    if (listen_global)
	sin.sin_addr.s_addr = htonl(INADDR_ANY);
    else
	sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if ((pse = getservbyname(service, protocol)))
	sin.sin_port = htons(ntohs((in_port_t)pse->s_port));
    else if ((sin.sin_port = htons((in_port_t)atoi(service))) == 0) {
	gpsd_report(LOG_ERROR, "Can't get \"%s\" service entry.\n", service);
	return -1;
    }
    ppe = getprotobyname(protocol);
    if (strcmp(protocol, "udp") == 0) {
	type = SOCK_DGRAM;
	/*@i@*/proto = (ppe) ? ppe->p_proto : IPPROTO_UDP;
    } else {
	type = SOCK_STREAM;
	/*@i@*/proto = (ppe) ? ppe->p_proto : IPPROTO_TCP;
    }
    if ((s = socket(PF_INET, type, proto)) == -1) {
	gpsd_report(LOG_ERROR, "Can't create socket\n");
	return -1;
    }
    if (setsockopt(s,SOL_SOCKET,SO_REUSEADDR,(char *)&one,(int)sizeof(one)) == -1) {
	gpsd_report(LOG_ERROR, "Error: SETSOCKOPT SO_REUSEADDR\n");
	return -1;
    }
    if (bind(s, (struct sockaddr *) &sin, (int)sizeof(sin)) == -1) {
	gpsd_report(LOG_ERROR, "Can't bind to port %s\n", service);
	if (errno == EADDRINUSE) {
		gpsd_report(LOG_ERROR, "Maybe gpsd is already running!\n");
	}
	return -1;
    }
    if (type == SOCK_STREAM && listen(s, qlen) == -1) {
	gpsd_report(LOG_ERROR, "Can't listen on port %s\n", service);
	return -1;
    }
    return s;
    /*@ +mustfreefresh -matchanyintegral @*/
}

static int filesock(char *filename)
{
    struct sockaddr_un addr;
    int sock;

    /*@ -mayaliasunique @*/
    if ((sock = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
	gpsd_report(LOG_ERROR, "Can't create device-control socket\n");
	return -1;
    }
    (void)strlcpy(addr.sun_path, filename, 104); /* from sys/un.h */
    /*@i1@*/addr.sun_family = AF_UNIX;
    (void)bind(sock, (struct sockaddr *) &addr,  (socklen_t)sizeof(addr));
    if (listen(sock, QLEN) == -1) {
	gpsd_report(LOG_ERROR, "can't listen on local socket %s\n", filename);
	return -1;
    }
    /*@ +mayaliasunique @*/
    return sock;
}

/*
 * Multi-session support requires us to have two arrays, one of GPS
 * devices currently available and one of client sessions.  The number
 * of slots in each array is limited by the maximum number of client
 * sessions we can have open.
 */

struct channel_t {
    struct gps_fix_t fixbuffer;		/* info to report to the client */
    struct gps_fix_t oldfix;		/* previous fix for error modeling */
    struct subscriber_t *subscriber;	/* subscriber monitoring this */
    struct gps_device_t *device;	/* device subscriber listens to */
};

struct subscriber_t {
    int fd;			/* client file descriptor. -1 if unused */
    double active;		/* when subscriber last polled for data */
#ifdef OLDSTYLE_ENABLE
    bool tied;				/* client set device with F */
#endif /* OLDSTYLE_ENABLE */
    struct policy_t policy;		/* configurable bits */
#if defined(OLDSTYLE_ENABLE) && defined(GPSDNG_ENABLE)
    bool new_style_responses;			/* protocol type desired */
#endif /* defined(OLDSTYLE_ENABLE) && defined(GPSDNG_ENABLE) */
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
#define channel_index(s) (int)((s) - channels)
#define allocated_device(devp)	 ((devp)->gpsdata.dev.path[0] != '\0')
#define free_device(devp)	 (devp)->gpsdata.dev.path[0] = '\0'
#define initialized_device(devp) ((devp)->context != NULL)

struct gps_device_t devices[MAXDEVICES];
struct channel_t channels[MAXSUBSCRIBERS*MAXDEVICES_PER_USER];
struct subscriber_t subscribers[MAXSUBSCRIBERS];		/* indexed by client file descriptor */

/*
 * If both protocols are enabled, we have to decide what kinds of
 * notifications to ship based on the protocol type of the last
 * command.  Otherwise the newstyle() macro evaluates to a constant,
 * and should be optimized out of condition guards that use it.
 */
#if defined(OLDSTYLE_ENABLE) && defined(GPSDNG_ENABLE)
#define newstyle(sub)	(sub)->new_style_responses
#elif defined(OLDSTYLE_ENABLE)
#define newstyle(sub)	false
#elif defined(GPSDNG_ENABLE)
#define newstyle(sub)	true
#endif

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

#ifdef GPSDNG_ENABLE
static int channel_count(struct subscriber_t *sub)
{
    int chancount = 0;
    struct channel_t *chp;

    for (chp = channels; chp < channels + NITEMS(channels); chp++)
	if (chp->subscriber == sub)
	    ++chancount;
    return chancount;
}

#endif /* GPSDNG_ENABLE */

static bool have_fix(struct channel_t *channel)
{
    if (!channel->device) {
	gpsd_report(LOG_PROG, "Client has no device\n");
	return false;
    }
#define VALIDATION_COMPLAINT(level, legend) \
	gpsd_report(level, legend " (status=%d, mode=%d).\n", \
		    channel->device->gpsdata.status, channel->fixbuffer.mode)
    if ((channel->device->gpsdata.status == STATUS_NO_FIX) != (channel->fixbuffer.mode == MODE_NO_FIX)) {
	VALIDATION_COMPLAINT(3, "GPS is confused about whether it has a fix");
	return false;
    }
    else if (channel->device->gpsdata.status > STATUS_NO_FIX && channel->fixbuffer.mode > MODE_NO_FIX) {
	VALIDATION_COMPLAINT(3, "GPS has a fix");
	return true;
    }
    VALIDATION_COMPLAINT(3, "GPS has no fix");
    return false;
#undef VALIDATION_COMPLAINT
}

static /*@null@*/ /*@observer@*/ struct subscriber_t* allocate_client(void)
{
    int cfd;
    for (cfd = 0; cfd < NITEMS(subscribers); cfd++) {
	if (subscribers[cfd].fd <= 0 ) {
	    gps_clear_fix(&channels[cfd].fixbuffer);
	    gps_clear_fix(&channels[cfd].oldfix);
	    subscribers[cfd].fd = cfd; /* mark subscriber as allocated */
	    return &subscribers[cfd];
	}
    }
    return NULL;
}

static void detach_client(struct subscriber_t *sub)
{
    char *c_ip;
    struct channel_t *channel; 
    if (-1 == sub->fd)
	return;
    c_ip = sock2ip(sub->fd);
    (void)shutdown(sub->fd, SHUT_RDWR);
    (void)close(sub->fd);
    gpsd_report(LOG_INF, "detaching %s (sub %d, fd %d) in detach_client\n",
	c_ip, sub_index(sub), sub->fd);
    FD_CLR(sub->fd, &all_fds);
    adjust_max_fd(sub->fd, false);
#ifdef OLDSYLE_ENABLE
    sub->tied = false;
#endif /* OLDSTYLE_ENABLE */
    sub->active = 0;
    sub->policy.buffer_policy = casoc;
    sub->policy.watcher = false;
    sub->policy.raw = 0;
    sub->policy.scaled = false;
    for (channel = channels; channel < channels + NITEMS(channels); channel++)
	if (channel->subscriber == sub)
	{
	    /*@i1@*/channel->device = NULL;
	    channel->subscriber = NULL;
	    
	}
    sub->fd = -1;
}

static ssize_t throttled_write(struct subscriber_t *sub, char *buf, ssize_t len)
/* write to client -- throttle if it's gone or we're close to buffer overrun */
{
    ssize_t status;

    if (debuglevel >= 3) {
	if (isprint(buf[0]))
	    gpsd_report(LOG_IO, "=> client(%d): %s", sub_index(sub), buf);
	else {
	    char *cp, buf2[MAX_PACKET_LENGTH*3];
	    buf2[0] = '\0';
	    for (cp = buf; cp < buf + len; cp++)
		(void)snprintf(buf2 + strlen(buf2),
			       sizeof(buf2)-strlen(buf2),
			      "%02x", (unsigned int)(*cp & 0xff));
	    gpsd_report(LOG_IO, "=> client(%d): =%s\r\n", sub_index(sub), buf2);
	}
    }

    status = write(sub->fd, buf, (size_t)len);
    if (status == len )
	return status;
    else if (status > -1) {
	gpsd_report(LOG_INF, "short write disconnecting client(%d)\n",
	    sub_index(sub));
	detach_client(sub);
	return 0;
    } else if (errno == EAGAIN || errno == EINTR)
	return 0; /* no data written, and errno says to retry */
     else if (errno == EBADF)
	gpsd_report(LOG_WARN, "client(%d) has vanished.\n", sub_index(sub));
    else if (errno == EWOULDBLOCK && timestamp() - sub->active > NOREAD_TIMEOUT)
	gpsd_report(LOG_INF, "client(%d) timed out.\n", sub_index(sub));
    else
	gpsd_report(LOG_INF, "client(%d) write: %s\n", sub_index(sub), strerror(errno));
    detach_client(sub);
    return status;
}

static void notify_watchers(struct gps_device_t *device, bool newstyle, char *sentence, ...)
/* notify all clients watching a given device of an event */
{
    struct channel_t *channel;
    va_list ap;
    char buf[BUFSIZ];

    va_start(ap, sentence) ;
    (void)vsnprintf(buf, sizeof(buf), sentence, ap);
    va_end(ap);

    for (channel = channels; channel < channels + NITEMS(channels); channel++)
    {
	struct subscriber_t *sub = channel->subscriber;
	if (channel->device==device && sub != NULL && (newstyle(sub) == newstyle))
	    (void)throttled_write(sub, buf, (ssize_t)strlen(buf));
    }
}

static void deactivate_device(struct gps_device_t *device)
/* deactivate device, but leave it in the pool (do not free it) */
{
    int cfd;

    for (cfd = 0; cfd < NITEMS(channels); cfd++)
	if (channels[cfd].device == device) {
	    channels[cfd].device = NULL;
	    channels[cfd].subscriber = NULL;
	}
#ifdef OLDSTYLE_ENABLE
    notify_watchers(device, false, "GPSD,X=0\r\n");
#endif /* OLDSTYLE_ENABLE */
#ifdef GPSDNG_ENABLE
    notify_watchers(device, true, 
		    "{\"class\":\"DEVICE\",\"path\":\"%s\",\"activated\":0}\r\n",
		    device->gpsdata.dev.path);
#endif /* GPSDNG_ENABLE */
    if (device->gpsdata.gps_fd != -1) {
	gpsd_deactivate(device);
	device->gpsdata.gps_fd = -1;	/* device is already disconnected */
    }
}

static void raw_hook(struct gps_data_t *ud,
		     char *sentence, size_t len, int level)
/* hook to be executed on each incoming packet */
{
    struct channel_t *channel; 

    for (channel = channels; channel < channels + NITEMS(channels); channel++) 
	if (channel->subscriber != NULL && channel->subscriber->policy.raw == level)
	{
	    struct subscriber_t *sub = channel->subscriber;

	    /* copy raw NMEA sentences from GPS to clients in raw mode */
	    if (sub != NULL && channel->device != NULL &&
		strcmp(ud->dev.path, channel->device->gpsdata.dev.path)==0)
		(void)throttled_write(sub, sentence, (ssize_t)len);
	    
	}
}

/*@ -globstate @*/
/*@null@*/ /*@observer@*/static struct gps_device_t *find_device(char *device_name)
/* find the device block for an existing device name */
{
    struct gps_device_t *devp;

    for (devp = devices; devp < devices + MAXDEVICES; devp++)
	if (allocated_device(devp) && strcmp(devp->gpsdata.dev.path, device_name)==0)
	    return devp;
    return NULL;
}

/*@ -nullret @*/
/*@ -statictrans @*/
static /*@null@*/ struct gps_device_t *open_device(char *device_name)
/* open and initialize a new channel block */
{
    struct gps_device_t *devp;

    /* special case: source may be a URI to a remote GNSS or DGPS service */
    if (netgnss_uri_check(device_name)) {
	int dsock = netgnss_uri_open(&context, device_name);
	if (dsock >= 0) {
	    FD_SET(dsock, &all_fds);
	    adjust_max_fd(dsock, true);
	}
	if (context.netgnss_service != netgnss_remotegpsd)
	    return &devices[0]; /* shaky, but only 0 versus nonzero is tested */
    }

    /* normal case: set up GPS/RTCM/AIS service */
    for (devp = devices; devp < devices + MAXDEVICES; devp++)
	if (!allocated_device(devp) || (strcmp(devp->gpsdata.dev.path, device_name)==0 && !initialized_device(devp))){
	    goto found;
	}
    return NULL;
found:
    gpsd_init(devp, &context, device_name);
    devp->gpsdata.raw_hook = raw_hook;
#ifdef OLDSTYLE_ENABLE
    /*
     * Bring the device all the way so we'll sniff packets from it and
     * discover up front what its device class is (e.g GPS, RTCM[23], AIS).
     * Otherwise clients trying to bind to a specific type won't know
     * what source types are actually available.
     */
    if (gpsd_activate(devp, true) < 0)
	return NULL;
#else
    /*
     * If we're running new protocol only there's no 'g' command
     * and thus we don't need to know device classes in advance.
     */
    if (gpsd_activate(devp, false) < 0)
	return NULL;
#endif /* OLDSTYLE_ENABLE */
    FD_SET(devp->gpsdata.gps_fd, &all_fds);
    adjust_max_fd(devp->gpsdata.gps_fd, true);
    return devp;
}

static /*@null@*/ struct gps_device_t *add_device(char *device_name)
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
		devp->gpsdata.raw_hook = raw_hook;
		gpsd_report(LOG_INF,"stashing device %s at slot %d\n",
			    device_name, 
			    (int)(devp - devices));
		devp->gpsdata.gps_fd = -1;
#ifdef GPSDNG_ENABLE
		notify_watchers(devp, true, 
				"{\"class\":\"DEVICE\",\"path\":\"%s\",\"activated\":%ld}\r\n",
				devp->gpsdata.dev.path,
				timestamp());
#endif /* GPSDNG_ENABLE */
		return devp;
	    }
	return NULL;
    }
}

/*@ +nullret @*/
/*@ +statictrans @*/
/*@ +globstate @*/

static bool allocation_filter(struct gps_device_t *device, gnss_type type)
/* does specified device match the user's type criteria? */
{
    /*
     * Might be we don't know the device type attached to this yet.
     * If we don't, open it and sniff packets until we do. 
     */
    if (allocated_device(device) && !initialized_device(device)) {
	if (open_device(device->gpsdata.dev.path) == NULL) {
	    gpsd_report(LOG_PROG, "allocation_filter: open failed\n");
	    free_device(device);
	    return false;
	}
    }

    gpsd_report(LOG_PROG, 
		"user requires %d=%s, device %d=%s emits packet type %d, observed mask is 0x%0x, checking against 0x%0x\n", 
		type, classmap[type].name,
		(int)(device - devices), device->gpsdata.dev.path,
		device->packet.type,
		device->observed,
		classmap[type].typemask);
    /* we might have type constraints */
    if (type == ANY)
	return true;
    else if (device->device_type == NULL)
	return false;
    else
	return (device->observed & classmap[type].packetmask) != 0;
}

/*@ -branchstate -usedef -globstate @*/
static struct channel_t *assign_channel(struct subscriber_t *user, 
				gnss_type type, struct gps_device_t *forcedev)
{
    struct channel_t *chp, *channel;
    bool was_unassigned;

    /* search for an already-assigned device with matching type or device */
    channel = NULL;
    for (chp = channels; chp < channels + NITEMS(channels); chp++)
	if ((forcedev != NULL && chp->device == forcedev)
	    ||
	    (chp->subscriber == user 
	     && chp->device != NULL 
	     && allocation_filter(chp->device, type))) {
	    gpsd_report(LOG_PROG, "client(%d): reusing channel %d (type %s)\n",
			sub_index(user), 
			(int)(chp-channels),
			classmap[type].name);
	    channel = chp;
	}
    /* if we didn't find one, allocate a new channel */
    if (channel == NULL) {
	for (chp = channels; chp < channels + NITEMS(channels); chp++)
	    if (chp->subscriber == NULL) {
		channel = chp;
		gpsd_report(LOG_PROG, "client(%d): attaching channel %d (type %s)\n",
			    sub_index(user), 
			    (int)(chp-channels),
			    classmap[type].name);
		break;
	    }
    }
    if (channel == NULL) {
	gpsd_report(LOG_ERROR, "client(%d): channel allocation for type %s failed.\n",
		    sub_index(user),
		    classmap[type].name);
	return NULL;
    }

    was_unassigned = (channel->device == NULL);

    /* if subscriber has no device... */
    if (was_unassigned) {
	if (forcedev != NULL ) {
	    channel->device = forcedev;
	} else {
	    double most_recent = 0;
	    int fix_quality = 0;
	    struct gps_device_t *devp;

	    gpsd_report(LOG_PROG, "client(%d): assigning channel...\n", sub_index(user));
	    /*@ -mustfreeonly @*/
	    for(devp = devices; devp < devices + MAXDEVICES; devp++)
		if (allocated_device(devp)) {
		    if (allocation_filter(devp, type)) {
			/*
			 * Grab device if it's:
			 * (1) The first we've seen,
			 * (2) Has a better quality fix than we've seen yet,
			 * (3) Fix of same quality we've seen but more recent.
			 */
			if (channel->device == NULL) {
			    channel->device = devp;
			    most_recent = devp->gpsdata.sentence_time;
			} else if (type == GPS && devp->gpsdata.status > fix_quality) {
			    channel->device = devp;
			    fix_quality = devp->gpsdata.status;
			} else if (type == GPS && devp->gpsdata.status == fix_quality && 
				   devp->gpsdata.sentence_time >= most_recent) {
			    channel->device = devp;
			    most_recent = devp->gpsdata.sentence_time;
			}
		    }
		}
	    /*@ +mustfreeonly @*/
	}
    }

    if (channel->device == NULL) {
	return NULL;
    }

    /* and open that device */
    if (channel->device->gpsdata.gps_fd != -1)
	gpsd_report(LOG_PROG,"client(%d): device %d (fd=%d, path %s) already active.\n",
		    sub_index(user), 
		    (int)(channel->device - devices),
		    channel->device->gpsdata.gps_fd,
		    channel->device->gpsdata.dev.path);
    else {
	if (gpsd_activate(channel->device, true) < 0) {

	    gpsd_report(LOG_ERROR, "client(%d): device activation failed.\n",
			sub_index(user));
	    return NULL;
	} else {
	    gpsd_report(LOG_RAW, "flagging descriptor %d in assign_channel()\n",
			channel->device->gpsdata.gps_fd);
	    FD_SET(channel->device->gpsdata.gps_fd, &all_fds);
	    adjust_max_fd(channel->device->gpsdata.gps_fd, true);
#ifdef OLDSTYLE_ENABLE
	    /*
	     * If user did an explicit F command tying him to a device, 
	     * he doesn't need a second notification that the device is
	     * attached.
	     */
	    if (!newstyle(user) && user->policy.watcher && !user->tied) {
		/*@ -sefparams @*/
		throttled_write(user, "GPSD,F=", 7);
		throttled_write(user,
				channel->device->gpsdata.dev.path,
				strlen(channel->device->gpsdata.dev.path));
		throttled_write(user, "\r\n", 2);
		/*@ +sefparams @*/
	    }
#endif /* OLDSTYLE_ENABLE */
	}
    }

    if (was_unassigned) {
	char buf[NMEA_MAX];

	buf[0] = '\0';
#ifdef OLDSTYLE_ENABLE
	if (!newstyle(user) && user->policy.watcher)
	    (void)snprintf(buf, sizeof(buf), "GPSD,X=%f,I=%s\r\n",
			   timestamp(), gpsd_id(channel->device));
#endif /* OLDSTYLE_ENABLE */
	/*@ -sefparams +matchanyintegral @*/
	if (buf[0])
	    throttled_write(user, buf, strlen(buf));
	/*@ +sefparams -matchanyintegral @*/
    }

    channel->subscriber = user;
    return channel;
}
/*@ +branchstate +usedef +globstate @*/

/*@ observer @*/static char *snarfline(char *p, /*@out@*/char **out)
/* copy the rest of the command line, before CR-LF */
{
    char *q;
    static char	stash[BUFSIZ];

    /*@ -temptrans -mayaliasunique @*/
    for (q = p; isprint(*p) && !isspace(*p) && /*@i@*/(p-q < BUFSIZ-1); p++)
	continue;
    (void)memcpy(stash, q, (size_t)(p-q));
    stash[p-q] = '\0';
    *out = stash;
    return p;
    /*@ +temptrans +mayaliasunique @*/
}

#ifdef ALLOW_RECONFIGURE
static bool privileged_channel(struct channel_t *channel)
/* is this channel privileged to change a device's behavior? */
{
    struct channel_t *chp;
    int channelcount = 0;

    /* grant user privilege if he's the only one listening to the device */
    for (chp = channels; chp < channels + NITEMS(channels); chp++)
	if (chp->device == channel->device)
	    channelcount++;
    return (channelcount == 1);
}
#endif /* ALLOW_CONFIGURE */

static void handle_control(int sfd, char *buf)
/* handle privileged commands coming through the control socket */
{
    char	*p, *stash, *eq;
    struct gps_device_t	*devp;

    /*@ -sefparams @*/
    if (buf[0] == '-') {
	p = snarfline(buf+1, &stash);
	gpsd_report(LOG_INF, "<= control(%d): removing %s\n", sfd, stash);
	if ((devp = find_device(stash))) {
	    if (devp->gpsdata.gps_fd > 0) {
		FD_CLR(devp->gpsdata.gps_fd, &all_fds);
		adjust_max_fd(devp->gpsdata.gps_fd, false);
	    }
	    deactivate_device(devp);
	    ignore_return(write(sfd, "OK\n", 3));
	} else
	    ignore_return(write(sfd, "ERROR\n", 6));
    } else if (buf[0] == '+') {
	p = snarfline(buf+1, &stash);
	if (find_device(stash)) {
	    gpsd_report(LOG_INF,"<= control(%d): %s already active \n", sfd, stash);
	    ignore_return(write(sfd, "ERROR\n", 6));
	} else {
	    gpsd_report(LOG_INF,"<= control(%d): adding %s \n", sfd, stash);
	    if (add_device(stash))
		ignore_return(write(sfd, "OK\n", 3));
	    else
		ignore_return(write(sfd, "ERROR\n", 6));
	}
    } else if (buf[0] == '!') {
	p = snarfline(buf+1, &stash);
	eq = strchr(stash, '=');
	if (eq == NULL) {
	    gpsd_report(LOG_WARN,"<= control(%d): ill-formed command \n", sfd);
	    ignore_return(write(sfd, "ERROR\n", 6));
	} else {
	    *eq++ = '\0';
	    if ((devp = find_device(stash))) {
		gpsd_report(LOG_INF,"<= control(%d): writing to %s \n", sfd, stash);
		ignore_return(write(devp->gpsdata.gps_fd, eq, strlen(eq)));
		ignore_return(write(sfd, "OK\n", 3));
	    } else {
		gpsd_report(LOG_INF,"<= control(%d): %s not active \n", sfd, stash);
		ignore_return(write(sfd, "ERROR\n", 6));
	    }
	}
    } else if (buf[0] == '&') {
	p = snarfline(buf+1, &stash);
	eq = strchr(stash, '=');
	if (eq == NULL) {
	    gpsd_report(LOG_WARN,"<= control(%d): ill-formed command \n", sfd);
	    ignore_return(write(sfd, "ERROR\n", 6));
	} else {
	    size_t len;
	    int st;
	    *eq++ = '\0';
	    len = strlen(eq)+5;
	    if ((devp = find_device(stash)) != NULL) {
		/* NOTE: this destroys the original buffer contents */
		st = gpsd_hexpack(eq, eq, len);
		if (st <= 0) {
		    gpsd_report(LOG_INF,"<= control(%d): invalid hex string (error %d).\n", sfd, st);
		    ignore_return(write(sfd, "ERROR\n", 6));
		}
		else
		{
		    gpsd_report(LOG_INF,"<= control(%d): writing %d bytes fromhex(%s) to %s\n", sfd, st, eq, stash);
		    ignore_return(write(devp->gpsdata.gps_fd, eq, (size_t)st));
		    ignore_return(write(sfd, "OK\n", 3));
		}
	    } else {
		gpsd_report(LOG_INF,"<= control(%d): %s not active\n", sfd, stash);
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
    char parity = (char)device->gpsdata.dev.parity;
    int wordsize = 8;

    if (strchr("78", *modestring)!= NULL) {
	while (isspace(*modestring))
	    modestring++;
	wordsize = (int)(*modestring++ - '0');
	if (strchr("NOE", *modestring)!= NULL) {
	    parity = *modestring++;
	    while (isspace(*modestring))
		modestring++;
	    if (strchr("12", *modestring)!=NULL)
		stopbits = (unsigned int)(*modestring - '0');
	}
    }

    /* no support for other word sizes yet */
    if (wordsize != (int)(9 - stopbits) && device->device_type->speed_switcher!=NULL)
	if (device->device_type->speed_switcher(device,
						speed,
						parity,
						(int)stopbits)) {
	    /*
	     * Deep black magic is required here. We have to
	     * allow the control string time to register at the
	     * GPS before we do the baud rate switch, which
	     * effectively trashes the UART's buffer.
	     *
	     * This definitely fails below 40 milliseconds on a
	     * BU-303b. 50ms is also verified by Chris Kuethe on
	     *	Pharos iGPS360 + GSW 2.3.1ES + prolific
	     *	Rayming TN-200 + GSW 2.3.1 + ftdi
	     *	Rayming TN-200 + GSW 2.3.2 + ftdi
	     * so it looks pretty solid.
	     *
	     * The minimum delay time is probably constant
	     * across any given type of UART.
	     */
	    (void)tcdrain(device->gpsdata.gps_fd);
	    (void)usleep(50000);
	    gpsd_set_speed(device, speed,
			   (unsigned char)parity, stopbits);
	}
}
#endif /* ALLOW_RECONFIGURE */

#ifdef OLDSTYLE_ENABLE
static struct channel_t *mandatory_assign_channel(struct subscriber_t *user, 
				gnss_type type, struct gps_device_t *forcedev)
{
    struct channel_t *channel = assign_channel(user, type, forcedev);
    if (channel == NULL)
	gpsd_report(LOG_ERROR, "client(%d): channel assignment (type %d=%s) failed.\n",
		    sub_index(user), type, classmap[type].name);
    return channel;
}

static bool handle_oldstyle(struct subscriber_t *sub, char *buf,
			    char *reply, size_t replylen)
/* interpret a client request; cfd is the socket back to the client */
{
    char phrase[BUFSIZ], *p, *stash;
    int i, j;
    struct channel_t *channel = NULL;

#if defined(OLDSTYLE_ENABLE) && defined(GPSDNG_ENABLE)
    sub->new_style_responses = false;
#endif /* defined(OLDSTYLE_ENABLE) && defined(GPSDNG_ENABLE) */

    (void)strlcpy(reply, "GPSD", replylen);
    replylen -= 4;
    p = buf;
    while (*p != '\0') {
	phrase[0] = '\0';
	switch (toupper(*p++)) {
	case 'A':
	    if ((channel=mandatory_assign_channel(sub, GPS, NULL))!= NULL && have_fix(channel) && channel->fixbuffer.mode == MODE_3D)
		(void)snprintf(phrase, sizeof(phrase), ",A=%.3f",
			channel->fixbuffer.altitude);
	    else
		(void)strlcpy(phrase, ",A=?", sizeof(phrase));
	    break;
#ifdef ALLOW_RECONFIGURE
	case 'B':		/* change baud rate */
#ifndef FIXED_PORT_SPEED
	    if ((channel=mandatory_assign_channel(sub, ANY, NULL))!= NULL && channel->device->device_type!=NULL && *p=='=' && privileged_channel(channel) && !context.readonly) {
		speed_t speed;

		speed = (speed_t)atoi(++p);
		while (isdigit(*p))
		    p++;
		while (isspace(*p))
		    p++;
#ifdef ALLOW_RECONFIGURE
		set_serial(channel->device, speed, p); 
#endif /* ALLOW_RECONFIGURE */
	    }
#endif /* FIXED_PORT_SPEED */
	    if (channel->device) {
		if ( channel->device->gpsdata.dev.parity == 0 ) {
			/* zero parity breaks the next snprintf */
			channel->device->gpsdata.dev.parity = (unsigned)'N';
		}
		(void)snprintf(phrase, sizeof(phrase), ",B=%d %u %c %u",
		    (int)gpsd_get_speed(&channel->device->ttyset),
			9 - channel->device->gpsdata.dev.stopbits,
			(int)channel->device->gpsdata.dev.parity,
			channel->device->gpsdata.dev.stopbits);
	    } else {
		(void)strlcpy(phrase, ",B=?", sizeof(phrase));
	    }
	    break;
	case 'C':
	    if ((channel=mandatory_assign_channel(sub, GPS, NULL))==NULL || channel->device->device_type==NULL)
		(void)strlcpy(phrase, ",C=?", sizeof(phrase));
	    else {
		const struct gps_type_t *dev = channel->device->device_type;
		if (*p == '=' && privileged_channel(channel)) {
		    double cycle = strtod(++p, &p);
		    if (dev->rate_switcher != NULL && cycle >= dev->min_cycle)
			if (dev->rate_switcher(channel->device, cycle))
			    channel->device->gpsdata.dev.cycle = cycle;
		}
		if (dev->rate_switcher == NULL)
		    (void)snprintf(phrase, sizeof(phrase),
				   ",C=%.2f", channel->device->gpsdata.dev.cycle);
		else
		    (void)snprintf(phrase, sizeof(phrase), ",C=%.2f %.2f",
				   channel->device->gpsdata.dev.cycle, channel->device->gpsdata.dev.mincycle);
	    }
	    break;
#endif /* ALLOW_RECONFIGURE */
	case 'D':
	    (void)strlcpy(phrase, ",D=", sizeof(phrase));
	    if ((channel=mandatory_assign_channel(sub, GPS, NULL))!= NULL && isnan(channel->fixbuffer.time)==0)
		(void)unix_to_iso8601(channel->fixbuffer.time,
				phrase+3, sizeof(phrase)-3);
	    else
		(void)strlcat(phrase, "?", sizeof(phrase));
	    break;
	case 'E':
	    (void)strlcpy(phrase, ",E=", sizeof(phrase));
	    if ((channel=mandatory_assign_channel(sub, GPS, NULL))!= NULL && have_fix(channel)) {
#if 0
		/*
		 * Only unpleasant choices here:
		 * 1. Always return ? for EPE (what we now do).
		 * 2. Get this wrong - what we used to do, before
		 *    noticing that the response generation for this
		 *    obsolete command had not been updated to go with
		 *    fix buffering.
		 * 3. Lift epe into the gps_fix_t structure, for no
		 *    functional reason other than this.
		 *    Unfortunately, this would force a bump in the
		 *    shared-library version.
		 */
		if (isnan(channel->device->gpsdata.epe) == 0)
		    (void)snprintf(phrase+strlen(phrase),
				   sizeof(phrase)-strlen(phrase),
				   "%.3f", channel->device->gpsdata.epe);
		else
#endif
		    (void)strlcat(phrase, "?", sizeof(phrase));
		if (isnan(channel->fixbuffer.eph) == 0)
		    (void)snprintf(phrase+strlen(phrase),
				   sizeof(phrase)-strlen(phrase),
				   " %.3f", channel->fixbuffer.eph);
		else
		    (void)strlcat(phrase, " ?", sizeof(phrase));
		if (isnan(channel->fixbuffer.epv) == 0)
		    (void)snprintf(phrase+strlen(phrase),
				   sizeof(phrase)-strlen(phrase),
				   " %.3f", channel->fixbuffer.epv);
		else
		    (void)strlcat(phrase, " ?", sizeof(phrase));
	    } else
		(void)strlcat(phrase, "?", sizeof(phrase));
	    break;
	case 'F':
	    /*@ -branchstate @*/
	    if (*p == '=') {
		p = snarfline(++p, &stash);
		gpsd_report(LOG_INF,"<= client(%d): switching to %s\n",sub_index(sub),stash);
		if ((channel = mandatory_assign_channel(sub, ANY, find_device(stash)))) {
		    sub->tied = true;
		}
	    }
	    /*@ +branchstate @*/
	    if (channel != NULL && channel->device != NULL)
		(void)snprintf(phrase, sizeof(phrase), ",F=%s",
			 channel->device->gpsdata.dev.path);
	    else
		(void)strlcpy(phrase, ",F=?", sizeof(phrase));
	    break;
	case 'G':
	    if (*p == '=') {
		gpsd_report(LOG_INF,"<= client(%d): requesting data type %s\n",sub_index(sub),++p);
		if (strncasecmp(p, "rtcm104v2", 7) == 0)
		    channel = mandatory_assign_channel(sub, RTCM2, NULL);
		if (strncasecmp(p, "rtcm104v3", 7) == 0)
		    channel = mandatory_assign_channel(sub, RTCM3, NULL);
		else if (strncasecmp(p, "gps", 3) == 0)
		    channel = mandatory_assign_channel(sub, GPS, NULL);
		else if (strncasecmp(p, "ais", 3) == 0)
		    channel = mandatory_assign_channel(sub, AIS, NULL);
		else
		    channel = mandatory_assign_channel(sub, ANY, NULL);
		p += strcspn(p, ",\r\n");
	    } else
		channel = mandatory_assign_channel(sub, ANY, NULL);
	    if (channel==NULL||channel->device==NULL||channel->device->packet.type==BAD_PACKET)
		(void)strlcpy(phrase, ",G=?", sizeof(phrase));
	    else if (channel->device->packet.type == RTCM2_PACKET)
		(void)snprintf(phrase, sizeof(phrase), ",G=RTCM104v2");
	    else
		(void)snprintf(phrase, sizeof(phrase), ",G=GPS");
	    break;
	case 'I':
	    if ((channel=mandatory_assign_channel(sub, GPS, NULL))!= NULL && channel->device->device_type!=NULL) {
		(void)snprintf(phrase, sizeof(phrase), ",I=%s",
			       gpsd_id(channel->device));
	    } else
		(void)strlcpy(phrase, ",I=?", sizeof(phrase));
	    break;
	case 'J':
	    if (*p == '=') ++p;
	    if (*p == '1' || *p == '+') {
		sub->policy.buffer_policy = nocasoc;
		p++;
	    } else if (*p == '0' || *p == '-') {
		sub->policy.buffer_policy = casoc;
		p++;
	    }
	    (void)snprintf(phrase, sizeof(phrase), ",J=%u", sub->policy.buffer_policy);
	    break;
	case 'K':
	    for (j = i = 0; i < MAXDEVICES; i++)
		if (allocated_device(&devices[i]))
		    j++;
	    (void)snprintf(phrase, sizeof(phrase), ",K=%d ", j);
	    for (i = 0; i < MAXDEVICES; i++) {
		if (allocated_device(&devices[i]) && strlen(phrase)+strlen(devices[i].gpsdata.dev.path)+1 < sizeof(phrase)) {
		    (void)strlcat(phrase, devices[i].gpsdata.dev.path, sizeof(phrase));
		    (void)strlcat(phrase, " ", sizeof(phrase));
		}
	    }
	    phrase[strlen(phrase)-1] = '\0';
	    break;
	case 'L':
	    (void)snprintf(phrase, sizeof(phrase), ",L=%d %d %s abcdefgijklmnopqrstuvwxyz", GPSD_API_MAJOR_VERSION, GPSD_API_MINOR_VERSION, VERSION);	//h
	    break;
	case 'M':
	    if ((channel=mandatory_assign_channel(sub, GPS, NULL))== NULL && (!channel->device || channel->fixbuffer.mode == MODE_NOT_SEEN))
		(void)strlcpy(phrase, ",M=?", sizeof(phrase));
	    else
		(void)snprintf(phrase, sizeof(phrase), ",M=%d", channel->fixbuffer.mode);
	    break;
	case 'N':
	    if ((channel=mandatory_assign_channel(sub, GPS, NULL))== NULL || channel->device->device_type == NULL)
		(void)strlcpy(phrase, ",N=?", sizeof(phrase));
	    else if (!channel->device->device_type->mode_switcher)
		(void)strlcpy(phrase, ",N=0", sizeof(phrase));
#ifdef ALLOW_RECONFIGURE
	    else if (privileged_channel(channel) && !context.readonly) {
		if (*p == '=') ++p;
		if (*p == '1' || *p == '+') {
		    channel->device->device_type->mode_switcher(channel->device, 1);
		    p++;
		} else if (*p == '0' || *p == '-') {
		    channel->device->device_type->mode_switcher(channel->device, 0);
		    p++;
		}
	    }
#endif /* ALLOW_RECONFIGURE */
	    if (!channel || !channel->device)
		(void)snprintf(phrase, sizeof(phrase), ",N=?");
	    else
		(void)snprintf(phrase, sizeof(phrase), ",N=%u", channel->device->gpsdata.dev.driver_mode);
	    break;
	case 'O':
	    if ((channel=mandatory_assign_channel(sub, GPS, NULL))== NULL || !have_fix(channel))
		(void)strlcpy(phrase, ",O=?", sizeof(phrase));
	    else {
		(void)snprintf(phrase, sizeof(phrase), ",O=%s",
			       channel->device->gpsdata.tag[0]!='\0' ? channel->device->gpsdata.tag : "-");
		if (isnan(channel->fixbuffer.time)==0)
		    (void)snprintf(phrase+strlen(phrase),
				   sizeof(phrase)-strlen(phrase),
				   " %.3f",
				   channel->fixbuffer.time);
		else
		    (void)strlcat(phrase, " ?", sizeof(phrase));
		if (isnan(channel->fixbuffer.ept)==0)
		    (void)snprintf(phrase+strlen(phrase),
				   sizeof(phrase)-strlen(phrase),
				   " %.3f",
				   channel->fixbuffer.ept);
		else
		    (void)strlcat(phrase, " ?", sizeof(phrase));
		if (isnan(channel->fixbuffer.latitude)==0)
		    (void)snprintf(phrase+strlen(phrase),
				   sizeof(phrase)-strlen(phrase),
				   " %.9f",
				   channel->fixbuffer.latitude);
		else
		    (void)strlcat(phrase, " ?", sizeof(phrase));
		if (isnan(channel->fixbuffer.longitude)==0)
		    (void)snprintf(phrase+strlen(phrase),
				   sizeof(phrase)-strlen(phrase),
				   " %.9f",
				   channel->fixbuffer.longitude);
		else
		    (void)strlcat(phrase, " ?", sizeof(phrase));
		if (isnan(channel->fixbuffer.altitude)==0)
		    (void)snprintf(phrase+strlen(phrase),
				   sizeof(phrase)-strlen(phrase),
				   " %.3f",
				   channel->fixbuffer.altitude);
		else
		    (void)strlcat(phrase, " ?", sizeof(phrase));
		if (isnan(channel->fixbuffer.eph)==0)
		    (void)snprintf(phrase+strlen(phrase),
				   sizeof(phrase)-strlen(phrase),
				  " %.3f",  channel->fixbuffer.eph);
		else
		    (void)strlcat(phrase, " ?", sizeof(phrase));
		if (isnan(channel->fixbuffer.epv)==0)
		    (void)snprintf(phrase+strlen(phrase),
				   sizeof(phrase)-strlen(phrase),
				   " %.3f",  channel->fixbuffer.epv);
		else
		    (void)strlcat(phrase, " ?", sizeof(phrase));
		if (isnan(channel->fixbuffer.track)==0)
		    (void)snprintf(phrase+strlen(phrase),
				   sizeof(phrase)-strlen(phrase),
				   " %.4f %.3f",
				   channel->fixbuffer.track,
				   channel->fixbuffer.speed);
		else
		    (void)strlcat(phrase, " ? ?", sizeof(phrase));
		if (isnan(channel->fixbuffer.climb)==0)
		    (void)snprintf(phrase+strlen(phrase),
				   sizeof(phrase)-strlen(phrase),
				   " %.3f",
				   channel->fixbuffer.climb);
		else
		    (void)strlcat(phrase, " ?", sizeof(phrase));
		if (isnan(channel->fixbuffer.epd)==0)
		    (void)snprintf(phrase+strlen(phrase),
				   sizeof(phrase)-strlen(phrase),
				   " %.4f",
				   channel->fixbuffer.epd);
		else
		    (void)strlcat(phrase, " ?", sizeof(phrase));
		if (isnan(channel->fixbuffer.eps)==0)
		    (void)snprintf(phrase+strlen(phrase),
			     sizeof(phrase)-strlen(phrase),
			     " %.2f", channel->fixbuffer.eps);
		else
		    (void)strlcat(phrase, " ?", sizeof(phrase));
		if (isnan(channel->fixbuffer.epc)==0)
		    (void)snprintf(phrase+strlen(phrase),
			     sizeof(phrase)-strlen(phrase),
			     " %.2f", channel->fixbuffer.epc);
		else
		    (void)strlcat(phrase, " ?", sizeof(phrase));
		if (channel->fixbuffer.mode > 0)
		    (void)snprintf(phrase+strlen(phrase),
			     sizeof(phrase)-strlen(phrase),
			     " %d", channel->fixbuffer.mode);
		else
		    (void)strlcat(phrase, " ?", sizeof(phrase));
	    }
	    break;
	case 'P':
	    if ((channel=mandatory_assign_channel(sub, GPS, NULL))!= NULL && have_fix(channel))
		(void)snprintf(phrase, sizeof(phrase), ",P=%.9f %.9f",
			channel->fixbuffer.latitude,
			channel->fixbuffer.longitude);
	    else
		(void)strlcpy(phrase, ",P=?", sizeof(phrase));
	    break;
	case 'Q':
#define ZEROIZE(x)	(isnan(x)!=0 ? 0.0 : x)
	    if ((channel=mandatory_assign_channel(sub, GPS, NULL))!= NULL &&
		(isnan(channel->device->gpsdata.pdop)==0
		 || isnan(channel->device->gpsdata.hdop)==0
		 || isnan(channel->device->gpsdata.vdop)==0))
		(void)snprintf(phrase, sizeof(phrase), ",Q=%d %.2f %.2f %.2f %.2f %.2f",
			channel->device->gpsdata.satellites_used,
			ZEROIZE(channel->device->gpsdata.pdop),
			ZEROIZE(channel->device->gpsdata.hdop),
			ZEROIZE(channel->device->gpsdata.vdop),
			ZEROIZE(channel->device->gpsdata.tdop),
			ZEROIZE(channel->device->gpsdata.gdop));
	    else
		(void)strlcpy(phrase, ",Q=?", sizeof(phrase));
#undef ZEROIZE
	    break;
	case 'R':
	    if (*p == '=') ++p;
	    if (*p == '2') {
		sub->policy.raw = 2;
		gpsd_report(LOG_INF, "client(%d) turned on super-raw mode\n", sub_index(sub));
		(void)snprintf(phrase, sizeof(phrase), ",R=2");
		p++;
	    } else if (*p == '1' || *p == '+') {
		sub->policy.raw = 1;
		gpsd_report(LOG_INF, "client(%d) turned on raw mode\n", sub_index(sub));
		(void)snprintf(phrase, sizeof(phrase), ",R=1");
		p++;
	    } else if (*p == '0' || *p == '-') {
		sub->policy.raw = 0;
		gpsd_report(LOG_INF, "client(%d) turned off raw mode\n", sub_index(sub));
		(void)snprintf(phrase, sizeof(phrase), ",R=0");
		p++;
	    } else if (sub->policy.raw) {
		sub->policy.raw = 0;
		gpsd_report(LOG_INF, "client(%d) turned off raw mode\n", sub_index(sub));
		(void)snprintf(phrase, sizeof(phrase), ",R=0");
	    } else {
		sub->policy.raw = 1;
		gpsd_report(LOG_INF, "client(%d) turned on raw mode\n", sub_index(sub));
		(void)snprintf(phrase, sizeof(phrase), ",R=1");
	    }
	    break;
	case 'S':
	    if ((channel=mandatory_assign_channel(sub, GPS, NULL))!= NULL)
		(void)snprintf(phrase, sizeof(phrase), ",S=%d", channel->device->gpsdata.status);
	    else
		(void)strlcpy(phrase, ",S=?", sizeof(phrase));
	    break;
	case 'T':
	    if ((channel=mandatory_assign_channel(sub, GPS, NULL))!= NULL && have_fix(channel) && isnan(channel->fixbuffer.track)==0)
		(void)snprintf(phrase, sizeof(phrase), ",T=%.4f", channel->fixbuffer.track);
	    else
		(void)strlcpy(phrase, ",T=?", sizeof(phrase));
	    break;
	case 'U':
	    if ((channel=mandatory_assign_channel(sub, GPS, NULL))!= NULL && have_fix(channel) && channel->fixbuffer.mode == MODE_3D)
		(void)snprintf(phrase, sizeof(phrase), ",U=%.3f", channel->fixbuffer.climb);
	    else
		(void)strlcpy(phrase, ",U=?", sizeof(phrase));
	    break;
	case 'V':
	    if ((channel=mandatory_assign_channel(sub, GPS, NULL))!= NULL && have_fix(channel) && isnan(channel->fixbuffer.speed)==0)
		(void)snprintf(phrase, sizeof(phrase), ",V=%.3f", channel->fixbuffer.speed * MPS_TO_KNOTS);
	    else
		(void)strlcpy(phrase, ",V=?", sizeof(phrase));
	    break;
	case 'W':
	    if ((channel = mandatory_assign_channel(sub, ANY, NULL))==NULL)
		(void)strlcpy(phrase, ",W=?", sizeof(phrase));
	    else {
		if (*p == '=') ++p;
		if (*p == '1' || *p == '+') {
		    sub->policy.watcher = true;
		    (void)snprintf(phrase, sizeof(phrase), ",W=1");
		    p++;
		} else if (*p == '0' || *p == '-') {
		    sub->policy.watcher = false;
		    (void)snprintf(phrase, sizeof(phrase), ",W=0");
		    p++;
		} else if (sub->policy.watcher) {
		    sub->policy.watcher = false;
		    (void)snprintf(phrase, sizeof(phrase), ",W=0");
		} else {
		    sub->policy.watcher = true;
		    gpsd_report(LOG_INF, "client(%d) turned on watching\n", sub_index(sub));
		    (void)snprintf(phrase, sizeof(phrase), ",W=1");
		}
	    }
	    break;
	case 'X':
	    if ((channel=mandatory_assign_channel(sub, ANY, NULL))!= NULL && channel->device != NULL)
		(void)snprintf(phrase, sizeof(phrase), ",X=%f", channel->device->gpsdata.online);
	    else
		(void)strlcpy(phrase, ",X=?", sizeof(phrase));
	    break;
	case 'Y':
	    if ((channel=mandatory_assign_channel(sub, GPS, NULL))!= NULL && channel->device->gpsdata.satellites > 0) {
		int used, reported = 0;
		(void)strlcpy(phrase, ",Y=", sizeof(phrase));
		if (channel->device->gpsdata.tag[0] != '\0')
		    (void)strlcat(phrase, channel->device->gpsdata.tag, sizeof(phrase));
		else
		    (void)strlcat(phrase, "-", sizeof(phrase));
		if (isnan(channel->device->gpsdata.sentence_time)==0)
		    (void)snprintf(phrase+strlen(phrase),
				   sizeof(phrase)-strlen(phrase),
				   " %.3f ",
				   channel->device->gpsdata.sentence_time);
		else
		    (void)strlcat(phrase, " ? ", sizeof(phrase));
		/* insurance against flaky drivers */
		for (i = 0; i < channel->device->gpsdata.satellites; i++)
		    if (channel->device->gpsdata.PRN[i])
			reported++;
		(void)snprintf(phrase+strlen(phrase),
			       sizeof(phrase)-strlen(phrase),
			       "%d:", reported);
		for (i = 0; i < channel->device->gpsdata.satellites; i++) {
		    used = 0;
		    for (j = 0; j < channel->device->gpsdata.satellites_used; j++)
			if (channel->device->gpsdata.used[j] == channel->device->gpsdata.PRN[i]) {
			    used = 1;
			    break;
			}
		    if (channel->device->gpsdata.PRN[i]) {
			(void)snprintf(phrase+strlen(phrase),
				      sizeof(phrase)-strlen(phrase),
				      "%d %d %d %.0f %d:",
				      channel->device->gpsdata.PRN[i],
				      channel->device->gpsdata.elevation[i],channel->device->gpsdata.azimuth[i],
				      channel->device->gpsdata.ss[i],
				      used);
		    }
		}
		if (channel->device->gpsdata.satellites != reported)
		    gpsd_report(LOG_WARN,"Satellite count %d != PRN count %d\n",
				channel->device->gpsdata.satellites, reported);
	    } else
		(void)strlcpy(phrase, ",Y=?", sizeof(phrase));
	    break;
	case 'Z':
	    if ((channel = mandatory_assign_channel(sub, GPS, NULL))==NULL)
		(void)strlcpy(phrase, ",Z=?", sizeof(phrase));
	    else {
		if (*p == '=') ++p;
		if (channel->device == NULL) {
		    (void)snprintf(phrase, sizeof(phrase), ",Z=?");
		    p++;
		} else if (*p == '1' || *p == '+') {
		    channel->device->gpsdata.profiling = true;
		    gpsd_report(LOG_INF, "client(%d) turned on profiling mode\n", sub_index(sub));
		    (void)snprintf(phrase, sizeof(phrase), ",Z=1");
		    p++;
		} else if (*p == '0' || *p == '-') {
		    channel->device->gpsdata.profiling = false;
		    gpsd_report(LOG_INF, "client(%d) turned off profiling mode\n", sub_index(sub));
		    (void)snprintf(phrase, sizeof(phrase), ",Z=0");
		    p++;
		} else {
		    channel->device->gpsdata.profiling = !channel->device->gpsdata.profiling;
		    gpsd_report(LOG_INF, "client(%d) toggled profiling mode\n", sub_index(sub));
		    (void)snprintf(phrase, sizeof(phrase), ",Z=%d",
				   (int)channel->device->gpsdata.profiling);
		}
	    }
	    break;
	case '$':
	    if ((channel=mandatory_assign_channel(sub, GPS, NULL))== NULL)
		(void)strlcpy(phrase, ",$=?", sizeof(phrase));
	    else if (channel->device->gpsdata.sentence_time!=0)
		(void)snprintf(phrase, sizeof(phrase), ",$=%s %d %lf %lf %lf %lf %lf %lf",
			channel->device->gpsdata.tag,
			(int)channel->device->gpsdata.sentence_length,
			channel->device->gpsdata.sentence_time,
			channel->device->gpsdata.d_xmit_time - channel->device->gpsdata.sentence_time,
			channel->device->gpsdata.d_recv_time - channel->device->gpsdata.sentence_time,
			channel->device->gpsdata.d_decode_time - channel->device->gpsdata.sentence_time,
			channel->device->poll_times[sub_index(sub)] - channel->device->gpsdata.sentence_time,
			timestamp() - channel->device->gpsdata.sentence_time);
	    else
		(void)snprintf(phrase, sizeof(phrase), ",$=%s %d 0 %lf %lf %lf %lf %lf",
			channel->device->gpsdata.tag,
			(int)channel->device->gpsdata.sentence_length,
			channel->device->gpsdata.d_xmit_time,
			channel->device->gpsdata.d_recv_time - channel->device->gpsdata.d_xmit_time,
			channel->device->gpsdata.d_decode_time - channel->device->gpsdata.d_xmit_time,
			channel->device->poll_times[sub_index(sub)] - channel->device->gpsdata.d_xmit_time,
			timestamp() - channel->device->gpsdata.d_xmit_time);
	    break;
	case '\r': case '\n':
	    goto breakout;
	}
	if (strlen(reply) + strlen(phrase) < replylen - 1)
	    (void)strlcat(reply, phrase, replylen);
	else
	    return false;	/* Buffer would overflow.  Just return an error */
    }
 breakout:
    (void)strlcat(reply, "\r\n", replylen);
    return true;
}
#endif /* OLDSTYLE_ENABLE */

#ifdef GPSDNG_ENABLE
static void json_devicelist_dump(char *reply, size_t replylen)
{
    struct gps_device_t *devp;
    (void)strlcpy(reply, 
		  "{\"class\":\"DEVICES\",\"devices\":[", replylen);
    for (devp = devices; devp < devices + MAXDEVICES; devp++)
	if (allocated_device(devp) && strlen(reply)+strlen(devp->gpsdata.dev.path)+3 < replylen-1) {
	    json_device_dump(devp,
			     reply+strlen(reply), replylen-strlen(reply));
	    (void)strlcat(reply, ",", replylen);
	}

	if (reply[strlen(reply)-1] == ',')
	    reply[strlen(reply)-1] = '\0';
	(void)strlcat(reply, "]}\r\n", replylen);
}

static void handle_newstyle_request(struct subscriber_t *sub, 
				    const char *buf, const char **after,
				    char *reply, size_t replylen)
{
    struct gps_device_t *devp;
    struct channel_t *channel;
    const char *end;

#if defined(OLDSTYLE_ENABLE) && defined(GPSDNG_ENABLE)
    sub->new_style_responses = true;
#endif /* defined(OLDSTYLE_ENABLE) && defined(GPSDNG_ENABLE) */

    /*
     * Still to be implemented: equivalents of Z $
     */
    if (strncmp(buf, "DEVICES;", 8) == 0) {
	buf += 8;
	json_devicelist_dump(reply, replylen);
    } else if (strncmp(buf, "WATCH", 5) == 0 && (buf[5] == ';' || buf[5] == '=')) {
	buf += 5;
	if (*buf == ';') {
	    ++buf;
	} else {
	    int status = json_watch_read(buf+1, &sub->policy, &end);
	    if (end == NULL)
		buf += strlen(buf);
	    else {
		if (*end == ';')
		    ++end;
		buf = end;
	    }
	    if (status == 0) {
		if (sub->policy.watcher)
		    for(devp = devices; devp < devices + MAXDEVICES; devp++)
			if (allocated_device(devp))
			    (void)assign_channel(sub, ANY, devp);
	    } else 
		(void)snprintf(reply+strlen(reply), replylen-strlen(reply),
			       "{\"class\":ERROR\",\"message\":\"Invalid WATCH: %s\"}\r\n",
			       json_error_string(status));
	}
	/* display the user's policy */
	json_watch_dump(&sub->policy, 
			     reply + strlen(reply),
			     replylen - strlen(reply));
    } else if (strncmp(buf, "DEVICE", 6) == 0 && (buf[6] == ';' || buf[6] == '=')) {
	struct devconfig_t devconf;
	buf += 6;
	devconf.path[0] = '\0';	/* initially, no device selection */
	if (*buf == ';') {
	    ++buf;
	} else {
	    struct channel_t *chp;
	    int status = json_device_read(buf+1, &devconf, &end);
	    if (end == NULL)
		buf += strlen(buf);
	    else {
		if (*end == ';')
		    ++end;
		buf = end;
	    }
	    channel = NULL;
	    if (status != 0) {
		(void)snprintf(reply, replylen, 
			       "{\"class\":ERROR\",\"message\":\"Invalid DEVICE: %s\"}\r\n",
			       json_error_string(status));
		goto bailout;
	    } else {
		if (devconf.path[0]) {
		    /* user specified a path, try to assign it */
		    if ((channel = assign_channel(sub, ANY, find_device(devconf.path))) == NULL)
			(void)snprintf(reply, replylen, 
				       "{\"class\":ERROR\",\"message\":\"Can't open %s.\"}\r\n",
				       devconf.path);
		} else {
		    /* no path specified */
		    int chcount = channel_count(sub);
		    if (chcount == 0) {
			(void)strlcat(reply, 
			  "{\"class\":\"ERROR\",\"message\":\"Can't perform DEVICE configuration, no channels attached.\"}\r\n",
			  replylen);
			goto bailout;
		    } else if (chcount > 1) {
			(void)snprintf(reply+strlen(reply), replylen-strlen(reply),
				   "{\"class\":ERROR\",\"message\":\"No path specified in DEVICE, but multiple channels are subscribed.\"}\r\n");
			goto bailout;
		    } else {
			/* we should have exactly one device now */
			for (chp = channels; chp < channels + NITEMS(channels); chp++)
			    if (chp->subscriber == sub) {
				channel = chp;
				break;
			}
		    }
		    assert(channel != NULL);
		    if (!privileged_channel(channel))
			(void)snprintf(reply+strlen(reply), replylen-strlen(reply),
				       "{\"class\":ERROR\",\"message\":\"Multiple subscribers, cannot change control bits.\"}\r\n");
		    else {
			char serialmode[3];
			double cycle;
			const struct gps_type_t *dt = channel->device->device_type;

			/* now that channel is selected, apply changes */
			if (devconf.driver_mode != channel->device->gpsdata.dev.driver_mode)
			    dt->mode_switcher(channel->device, devconf.driver_mode);
			serialmode[0] = devconf.parity;
			serialmode[1] = '0' + devconf.stopbits;
			serialmode[2] = '\0';
			set_serial(channel->device, 
				   (speed_t)devconf.baudrate, serialmode);
			if (dt->rate_switcher != NULL 
			    && isnan(devconf.cycle)!=0 
			    && devconf.cycle >= dt->min_cycle)
			if (dt->rate_switcher(channel->device, devconf.cycle))
			    channel->device->gpsdata.dev.cycle = cycle;
		    }
		}
	    }
	}
	/* dump a response for each selected channel */
	for (channel = channels; channel < channels + NITEMS(channels); channel++)
	    if (channel->subscriber != sub)
		continue;
	    else if (devconf.path[0] != '\0' && channel->device && strcmp(channel->device->gpsdata.dev.path, devconf.path)!=0)
		continue;
	    else {
		json_device_dump(channel->device, 
				    reply + strlen(reply),
				    replylen - strlen(reply));
	    }
    } else if (strncmp(buf, "VERSION;", 8) == 0) {
	buf += 8;
	json_version_dump(reply + strlen(reply), replylen - strlen(reply));
    } else {
	const char *errend;
	errend = buf + strlen(buf) - 1;
	if (isspace(*errend))
	    --errend;
	(void)snprintf(reply+strlen(reply), replylen-strlen(reply), 
		       "{\"class\":ERROR\",\"message\":\"Unrecognized request '%.*s'\"}\r\n",
		       (int)(errend-buf), buf);
	buf += strlen(buf);
    }
	bailout:
    *after = buf;
}
#endif /* GPSDNG_ENABLE */

static int handle_gpsd_request(struct subscriber_t *sub, const char *buf)
{
    char reply[GPS_JSON_RESPONSE_MAX+1];

    reply[0] = '\0';
#ifdef GPSDNG_ENABLE
    if (buf[0] == '?') {
	const char *end;
	for (end = ++buf; *buf; buf = end)
	    if (isspace(*buf))
		end = buf + 1;
	    else
		handle_newstyle_request(sub, buf, &end, reply, sizeof(reply));
    }
#endif /* GPSDNG_ENABLE */
#ifdef OLDSTYLE_ENABLE
    /* fall back to old-style requests */
    if (buf[0] != '\0')
	if (!handle_oldstyle(sub, (char *)buf, reply,sizeof(reply)))
	    return -1;
#endif /* OLDSTYLE_ENABLE */

    return (int)throttled_write(sub, reply, (ssize_t)strlen(reply));
}

/*@ -mustfreefresh @*/
int main(int argc, char *argv[])
{
    static char *pid_file = NULL;
    static int st, csock = -1;
    static gps_mask_t changed;
    static char *gpsd_service = NULL;
    static char *control_socket = NULL;
    struct gps_device_t *device;
    struct sockaddr_in fsin;
    fd_set rfds, control_fds;
    int i, option, msock, cfd, dfd;
    bool go_background = true;
    struct timeval tv;
    struct subscriber_t *sub;
    struct channel_t *channel;
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
	    debuglevel = (int) strtol(optarg, 0, 0);
	    gpsd_hexdump_level = debuglevel;
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
	    (void)printf("gpsd %s\n", VERSION);
	    exit(0);
	case 'h': case '?':
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
	gpsd_report(LOG_ERROR, "can't run with neither control socket nor devices\n");
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
	    gpsd_report(LOG_ERROR,"control socket create failed, netlib error %d\n",csock);
	    exit(2);
	}
	FD_SET(csock, &all_fds);
	adjust_max_fd(csock, true);
	gpsd_report(LOG_PROG, "control socket opened at %s\n", control_socket);
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
	gpsd_service = getservbyname("gpsd", "tcp") ? "gpsd" : DEFAULT_GPSD_PORT;
    /*@ +observertrans @*/
    if ((msock = passivesock(gpsd_service, "tcp", QLEN)) == -1) {
	gpsd_report(LOG_ERR,"command socket create failed, netlib error %d\n",msock);
	exit(2);
    }
    gpsd_report(LOG_INF, "listening on port %s\n", gpsd_service);

#ifdef NTPSHM_ENABLE
    if (getuid() == 0) {
	errno = 0;
	if (nice(NICEVAL) != -1 || errno == 0)
	    gpsd_report (2, "Priority setting failed.\n");
	(void)ntpshm_init(&context, nowait);
    } else {
	gpsd_report (LOG_INF, "Unable to start ntpshm.  gpsd must run as root.\n");
    }
#endif /* NTPSHM_ENABLE */

#ifdef DBUS_ENABLE
    /* we need to connect to dbus as root */
    if (initialize_dbus_connection()) {
	/* the connection could not be started */
	gpsd_report (LOG_ERROR, "unable to connect to the DBUS system bus\n");
    } else
	gpsd_report (LOG_PROG, "successfully connected to the DBUS system bus\n");
#endif /* DBUS_ENABLE */

    if (getuid() == 0 && go_background) {
	struct passwd *pw;
	struct stat stb;

	/* make default devices accessible even after we drop privileges */
	for (i = optind; i < argc; i++)
	    if (stat(argv[i], &stb) == 0)
		(void)chmod(argv[i], stb.st_mode|S_IRGRP|S_IWGRP);
	/*
	 * Drop privileges.  Up to now we've been running as root.  Instead,
	 * set the user ID to 'nobody' (or whatever the --enable-gpsd-user
	 * is) and the group ID to the owning group of a prototypical TTY
	 * device. This limits the scope of any compromises in the code.
	 * It requires that all GPS devices have their group read/write
	 * permissions set.
	 */
	if ((optind<argc&&stat(argv[optind], &stb)==0)||stat(PROTO_TTY,&stb)==0) {
	    gpsd_report(LOG_PROG, "changing to group %d\n", stb.st_gid);
	    if (setgid(stb.st_gid) != 0)
		gpsd_report(LOG_ERROR, "setgid() failed, errno %s\n", strerror(errno));
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
    }
    gpsd_report(LOG_INF, "running with effective group ID %d\n", getegid());
    gpsd_report(LOG_INF, "running with effective user ID %d\n", geteuid());

    for (channel = channels; channel < channels + NITEMS(channels); channel++) {
	gps_clear_fix(&channel->fixbuffer);
	gps_clear_fix(&channel->oldfix);
    }

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

    FD_SET(msock, &all_fds);
    adjust_max_fd(msock, true);
    FD_ZERO(&control_fds);

    /* optimization hack to defer having to read subframe data */
    if (time(NULL) < START_SUBFRAME)
	context.valid |= LEAP_SECOND_VALID;

    for (i = optind; i < argc; i++) {
	struct gps_device_t *device = add_device(argv[i]);
	if (!device) {
	    gpsd_report(LOG_ERROR, "GPS device %s nonexistent or can't be read\n", argv[i]);
	}
    }

    while (0 == signalled) {
	(void)memcpy((char *)&rfds, (char *)&all_fds, sizeof(rfds));

	gpsd_report(LOG_RAW+2, "select waits\n");
	/*
	 * Poll for user commands or GPS data.  The timeout doesn't
	 * actually matter here since select returns whenever one of
	 * the file descriptors in the set goes ready.  The point
	 * of tracking maxfd is to keep the set of descriptors that
	 * select(2) has to poll here as small as possible (for
	 * low-clock-rate SBCs and the like).
	 */
	/*@ -usedef @*/
	tv.tv_sec = 1; tv.tv_usec = 0;
	if (select(maxfd+1, &rfds, NULL, NULL, &tv) == -1) {
	    if (errno == EINTR)
		continue;
	    gpsd_report(LOG_ERROR, "select: %s\n", strerror(errno));
	    exit(2);
	}
	/*@ +usedef @*/

#ifdef __UNUSED__
	{
	    char dbuf[BUFSIZ];
	    dbuf[0] = '\0';
	    for (sub = subscribers; sub < subscribers + MAXSUBSCRIBERS; sub++)
		if (FD_ISSET(sub->fd, &all_fds))
		    (void)snprintf(dbuf + strlen(dbuf),
				   sizeof(dbuf)-strlen(dbuf),
				   " %d", sub->fd);
	    strlcat(dbuf, "} -> {", BUFSIZ);
	    for (sub = subscribers; sub < subscribers + MAXSUBSCRIBERS; sub++)
		if (FD_ISSET(sub->fd, &rfds))
		    (void)snprintf(dbuf + strlen(dbuf),
				   sizeof(dbuf)-strlen(dbuf),
				   " %d", sub->fd);
	    gpsd_report(LOG_RAW, "Polling descriptor set: {%s}\n", dbuf);
	}
#endif /* UNUSED */

	/* always be open to new client connections */
	if (FD_ISSET(msock, &rfds)) {
	    socklen_t alen = (socklen_t)sizeof(fsin);
	    char *c_ip;
	    /*@i1@*/int ssock = accept(msock, (struct sockaddr *) &fsin, &alen);

	    if (ssock == -1)
		gpsd_report(LOG_ERROR, "accept: %s\n", strerror(errno));
	    else {
		struct subscriber_t *client = NULL;
		int opts = fcntl(ssock, F_GETFL);

		if (opts >= 0)
		    (void)fcntl(ssock, F_SETFL, opts | O_NONBLOCK);

		c_ip = sock2ip(ssock);
		client = allocate_client();
		if (client == NULL) {
		    gpsd_report(LOG_ERROR, "Client %s connect on fd %d -"
			"no subscriber slots available\n", c_ip, ssock);
		    (void)close(ssock);
		} else {
#ifdef GPSDNG_ENABLE
		    char announce[GPS_JSON_RESPONSE_MAX];
#endif /* GPSDNG_ENABLE */
		    FD_SET(ssock, &all_fds);
		    adjust_max_fd(ssock, true);
		    client->fd = ssock;
		    client->active = timestamp();
#ifdef OLDSTYLE_ENABLE
		    client->tied = false;
#endif /* OLDSTYLE_ENABLE */
		    gpsd_report(LOG_INF, "client %s (%d) connect on fd %d\n",
			c_ip, sub_index(client), ssock);	
#ifdef GPSDNG_ENABLE
		    json_version_dump(announce, sizeof(announce));
		    (void)throttled_write(client, announce, strlen(announce));
#endif /* GPSDNG_ENABLE */
		}
	    }
	    FD_CLR(msock, &rfds);
	}

	/* also be open to new control-socket connections */
	if (csock > -1 && FD_ISSET(csock, &rfds)) {
	    socklen_t alen = (socklen_t)sizeof(fsin);
	    /*@i1@*/int ssock = accept(csock, (struct sockaddr *) &fsin, &alen);

	    if (ssock == -1)
		gpsd_report(LOG_ERROR, "accept: %s\n", strerror(errno));
	    else {
		gpsd_report(LOG_INF, "control socket connect on fd %d\n", ssock);
		FD_SET(ssock, &all_fds);
		FD_SET(ssock, &control_fds);
		adjust_max_fd(ssock, true);
	    }
	    FD_CLR(csock, &rfds);
	}

	if (context.dsock >= 0 && FD_ISSET(context.dsock, &rfds)) {
	    /* be ready for DGPS reports */
	    if (netgnss_poll(&context) == -1){
		FD_CLR(context.dsock, &all_fds);
		FD_CLR(context.dsock, &rfds);
		context.dsock = -1;
	    }
	}
	/* read any commands that came in over control sockets */
	for (cfd = 0; cfd < FD_SETSIZE; cfd++)
	    if (FD_ISSET(cfd, &control_fds)) {
		char buf[BUFSIZ];

		while (read(cfd, buf, sizeof(buf)-1) > 0) {
		    gpsd_report(LOG_IO, "<= control(%d): %s\n", cfd, buf);
		    handle_control(cfd, buf);
		}
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
	    if (device->device_type && device->context->netgnss_service != netgnss_remotegpsd)
		rtcm_relay(device);

	    /* get data from the device */
	    changed = 0;
	    if (device->gpsdata.gps_fd >= 0 && FD_ISSET(device->gpsdata.gps_fd, &rfds))
	    {
		gpsd_report(LOG_RAW+1, "polling %d\n", device->gpsdata.gps_fd);
		changed = gpsd_poll(device);
		if (changed == ERROR_SET) {
		    gpsd_report(LOG_WARN, "packet sniffer failed to sync up\n");
		    FD_CLR(device->gpsdata.gps_fd, &all_fds);
		    adjust_max_fd(device->gpsdata.gps_fd, false);
		    deactivate_device(device);
		} else if ((changed & ONLINE_SET) == 0) {
		    FD_CLR(device->gpsdata.gps_fd, &all_fds);
		    adjust_max_fd(device->gpsdata.gps_fd, false);
		    deactivate_device(device);
		}
		else {
		    /* we may need to add device to new-style watcher lists */
		    if ((changed & DEVICE_SET) != 0) {
			for (sub = subscribers; sub < subscribers + MAXSUBSCRIBERS; sub++)
			    if (sub->policy.watcher && newstyle(sub))
				(void)assign_channel(sub, ANY, device);
			    }
		    /* handle laggy response to a firmware version query */
		    if ((changed & DEVICEID_SET) != 0) {
			assert(device->device_type != NULL);
#ifdef OLDSTYLE_ENABLE
			{
			    char id1[NMEA_MAX];
			    (void)snprintf(id1, sizeof(id1), "GPSD,I=%s",
					   device->device_type->type_name);
			    if (device->subtype[0] != '\0') {
				(void)strlcat(id1, " ", sizeof(id1));
				(void)strlcat(id1, device->subtype,sizeof(id1));
			    }
			    (void)strlcat(id1, "\r\n", sizeof(id1));
			    notify_watchers(device, false, id1);
			}
#endif /* OLDSTYLE_ENABLE */
#ifdef GPSDNG_ENABLE
			{
			    char id2[NMEA_MAX];
			    (void)snprintf(id2, sizeof(id2), 
					   "{\"class\":\"DEVICE\",\"path\":\"%s\",\"subtype\":\"%s\"}\r\n",
					   device->gpsdata.dev.path,
					   device->subtype);
			    notify_watchers(device, true, id2);
			}
#endif /* GPSDNG_ENABLE */
		    }
		    /* copy/merge device data into subscriber fix buffers */
		    for (channel = channels;
			 channel < channels + NITEMS(channels);
			 channel++) {
			if (channel->device == device) {
			    if (channel->subscriber->policy.buffer_policy == casoc && (changed & CYCLE_START_SET)!=0)
				gps_clear_fix(&channel->fixbuffer);
			    /* don't downgrade mode if holding previous fix */
			    if (channel->fixbuffer.mode > channel->device->gpsdata.fix.mode)
				changed &=~ MODE_SET;
			    //gpsd_report(LOG_PROG,
			    //		"transfer mask on %s: %02x\n", channel->channel->gpsdata.tag, changed);
			    gps_merge_fix(&channel->fixbuffer,
					  changed,
					  &channel->device->gpsdata.fix);
			    gpsd_error_model(channel->device,
					     &channel->fixbuffer, &channel->oldfix);
			}
		    }
		}
		/* copy each RTCM-104 correction to all GPSes */
		if ((changed & RTCM2_SET) != 0 || (changed & RTCM3_SET) != 0) {
		    struct gps_device_t *gps;
		    for (gps = devices; gps < devices + MAXDEVICES; gps++)
			if (gps->device_type != NULL && gps->device_type->rtcm_writer != NULL)
			    (void)gps->device_type->rtcm_writer(gps, (char *)gps->packet.outbuffer, gps->packet.outbuflen);
		}
	    }

	    /* watch all channels */
	    for (channel = channels; channel < channels + NITEMS(channels); channel++) {
		struct subscriber_t *sub = channel->subscriber;
		/* some listeners may be in watcher mode */
		if (sub != NULL && sub->policy.watcher) {
		    char buf2[BUFSIZ];
		    channel->device->poll_times[sub - subscribers] = timestamp();
		    if (changed &~ ONLINE_SET) {
#ifdef OLDSTYLE_ENABLE
			if (!newstyle(sub)) {
			    char cmds[4] = "";
			    if (changed & (LATLON_SET | MODE_SET))
				(void)strlcat(cmds, "o", 4);
			    if (changed & SATELLITE_SET)
				(void)strlcat(cmds, "y", 4);
			    if (channel->device->gpsdata.profiling!=0)
				(void)strlcat(cmds, "$", 4);
			    if (cmds[0] != '\0') {
				(void)handle_oldstyle(sub, cmds, buf2, sizeof(buf2));
				(void)throttled_write(sub, buf2, strlen(buf2));
			    }
#ifdef AIVDM_ENABLE
			    if ((changed & AIS_SET) != 0) {
				aivdm_json_dump(&channel->device->aivdm.decoded, 
					   sub->policy.scaled, buf2, sizeof(buf2));
				(void)throttled_write(sub, buf2, strlen(buf2));
#endif /* AIVDM_ENABLE */
			    }
			}
#endif /* OLDSTYLE_ENABLE */
#if defined(OLDSTYLE_ENABLE) && defined(GPSDNG_ENABLE)
			else
#endif /* defined(OLDSTYLE_ENABLE) && defined(GPSDNG_ENABLE) */
#ifdef GPSDNG_ENABLE
			{
			    if (sub->policy.watcher && newstyle(sub) && (changed & (LATLON_SET | MODE_SET))!=0) {
				json_tpv_dump(&channel->device->gpsdata, &channel->fixbuffer, 
					      buf2, sizeof(buf2));
				(void)throttled_write(sub, buf2, strlen(buf2));
			    }
			    if (sub->policy.watcher && newstyle(sub) && (changed & SATELLITE_SET)!=0) {
				json_sky_dump(&channel->device->gpsdata,
					      buf2, sizeof(buf2));
				(void)throttled_write(sub, buf2, strlen(buf2));
			    }
#ifdef AIVDM_ENABLE
			    if (sub->policy.watcher && newstyle(sub) && (changed & AIS_SET)!=0) {
				aivdm_json_dump(&channel->device->aivdm.decoded, 
					   false, buf2, sizeof(buf2));
				(void)throttled_write(sub, buf2, strlen(buf2));
#endif /* AIVDM_ENABLE */
			    }
			}
#endif /* GPSDNG_ENABLE */
		    }
		}
	    }
#ifdef DBUS_ENABLE
	    if (changed &~ ONLINE_SET) {
		if (changed & (LATLON_SET | MODE_SET)) {
		    send_dbus_fix (channel);
		}
	    }
#endif
	}

#ifdef NOT_FIXED
	if (context.fixcnt > 0 && context.dsock == -1) {
	    for (device=devices; device < devices+MAXDEVICES; device++) {
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

		gpsd_report(LOG_PROG, "checking client(%d)\n", sub_index(sub));
		if ((buflen = (int)read(sub->fd, buf, sizeof(buf) - 1)) <= 0) {
		    detach_client(sub);
		} else {
		    if (buf[buflen-1] != '\n')
			buf[buflen++] = '\n';
		    buf[buflen] = '\0';
		    gpsd_report(LOG_IO, 
				"<= client(%d): %s", sub_index(sub), buf);

		    /*
		     * When a command comes in, update subscriber.active to
		     * timestamp() so we don't close the connection
		     * after POLLER_TIMEOUT seconds. This makes
		     * POLLER_TIMEOUT useful.
		     */
		    sub->active = timestamp();
		    for (channel = channels; channel < channels + NITEMS(channels); channel++)
			if (channel->device && channel->subscriber == sub)
			    channel->device->poll_times[sub_index(sub)] = sub->active;
		    if (handle_gpsd_request(sub, buf) < 0)
			detach_client(sub);
		}
	    } else {
		int devcount = 0, rawcount = 0;
		/* count devices attached by this subscriber */
		for (channel = channels; channel < channels + NITEMS(channels); channel++)
		    if (channel->device && channel->subscriber == sub) {
			devcount++;
			if (channel->subscriber->policy.raw > 0)
			    rawcount++;
		    }

		if (devcount == 0 && timestamp() - sub->active > ASSIGNMENT_TIMEOUT) {
		    gpsd_report(LOG_WARN, "client(%d) timed out before assignment request.\n", sub_index(sub));
		    detach_client(sub);
		} else if (devcount > 0 && !sub->policy.watcher && rawcount == 0 && timestamp() - sub->active > POLLER_TIMEOUT) {
		    gpsd_report(LOG_WARN, "client(%d) timed out on command wait.\n", cfd);
		    detach_client(sub);
		}
	    }
	}

	/*
	 * Mark devices with an identified packet type but no
	 * remaining subscribers to be closed.  The reason the test
	 * has this particular form is so that, immediately after
	 * device open, we'll keep reading packets until a type is
	 * identified even though there are no subscribers yet.  We
	 * need this to happen so that subscribers can later choose a
	 * device by packet type.
	 */
	if (!nowait)
	    for (device=devices; device < devices+MAXDEVICES; device++) {
		if (allocated_device(device)) {
		    if (device->packet.type != BAD_PACKET) {
			bool device_needed = false;

			for (cfd = 0; cfd < NITEMS(channels); cfd++)
			    if (channels[cfd].device == device)
				device_needed = true;

			if (!device_needed && device->gpsdata.gps_fd > -1) {
			    if (device->releasetime == 0) {
				device->releasetime = timestamp();
				gpsd_report(LOG_PROG, "device %d (fd %d) released\n", (int)(device-devices), device->gpsdata.gps_fd);
			    } else if (timestamp() - device->releasetime > RELEASE_TIMEOUT) {
				gpsd_report(LOG_PROG, "device %d closed\n", (int)(device-devices));
				gpsd_report(LOG_RAW, "unflagging descriptor %d\n", device->gpsdata.gps_fd);
				FD_CLR(device->gpsdata.gps_fd, &all_fds);
				adjust_max_fd(device->gpsdata.gps_fd, false);
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

    gpsd_report(LOG_WARN, "Received terminating signal %d. Exiting...\n",signalled);
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
