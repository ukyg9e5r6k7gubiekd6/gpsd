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
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <assert.h>

#include "config.h"
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
#if defined(HAVE_SYS_TIME_H)
#include <sys/time.h>
#endif

#include "gpsd.h"
#include "sirf.h"

#define QLEN			5

struct gps_session_t *session;
static char *device_name = DEFAULT_DEVICE_NAME;
static char *pid_file = NULL;
static fd_set all_fds, nmea_fds, watcher_fds;
static int debuglevel, nfds, go_background = 1, in_background = 0;

static jmp_buf	restartbuf;
#define THROW_SIGHUP	1

static void restart(int sig UNUSED)
{
    longjmp(restartbuf, THROW_SIGHUP);
}

static void onsig(int sig)
{
    gpsd_wrap(session);
    gpsd_report(1, "Received signal %d. Exiting...\n", sig);
    exit(10 + sig);
}

static void store_pid(pid_t pid)
{
	FILE *fp;

	if ((fp = fopen(pid_file, "w")) != NULL) {
		fprintf(fp, "%u\n", pid);
		(void) fclose(fp);
	} else {
		gpsd_report(1, "Cannot create PID file: %s.\n", pid_file);
	}
}

static int daemonize(void)
{
    int fd;
    pid_t pid;

    switch (pid = fork()) {
    case -1:
	return -1;
    case 0:	/* child side */
	break;
    default:	/* parent side */
	if (pid_file)
		store_pid(pid);
	exit(0);
    }

    if (setsid() == -1)
	return -1;
    chdir("/");
    if ((fd = open(_PATH_DEVNULL, O_RDWR, 0)) != -1) {
	dup2(fd, STDIN_FILENO);
	dup2(fd, STDOUT_FILENO);
	dup2(fd, STDERR_FILENO);
	if (fd > 2)
	    close(fd);
    }
    in_background = 1;
    return 0;
}

void gpsd_report(int errlevel, const char *fmt, ... )
/* assemble command in printf(3) style, use stderr or syslog */
{
    if (errlevel <= debuglevel) {
	char buf[BUFSIZ];
	va_list ap;

	strcpy(buf, "gpsd: ");
	va_start(ap, fmt) ;
	vsnprintf(buf + strlen(buf), sizeof(buf)-strlen(buf), fmt, ap);
	va_end(ap);

	if (in_background)
	    syslog((errlevel == 0) ? LOG_ERR : LOG_NOTICE, "%s", buf);
	else
	    fputs(buf, stderr);
    }
}

static void usage(void)
{
    printf("usage:  gpsd [options] \n\
  Options include: \n\
  -p string (default %s)   = set GPS device name \n"
#ifdef NON_NMEA_ENABLE
"  -T devtype (default 'n')       = set GPS device type \n"
#endif /* NON_NMEA_ENABLE */
"  -S integer (default %4s)      = set port for daemon \n"
#ifdef TRIPMATE_ENABLE
"  -i %%f[NS]:%%f[EW]               = set initial latitude/longitude \n"
#endif /* TRIPMATE_ENABLE */
"  -s baud_rate                   = set baud rate on GPS device \n\
  -d host[:port]                 = set DGPS server \n\
  -P pidfile                     = set file to record process ID \n\
  -D integer (default 0)         = set debug level \n\
  -h                             = help message \n",
	   DEFAULT_DEVICE_NAME, DEFAULT_GPSD_PORT);
#ifdef NON_NMEA_ENABLE
    {
    struct gps_type_t **dp;
    printf("Here are the available driver types:\n"); 
    for (dp = gpsd_drivers; *dp; dp++)
	if ((*dp)->typekey)
	    printf("   %c -- %s\n", (*dp)->typekey, (*dp)->typename);
    }
#else
    printf("This gpsd was compiled with support for NMEA only.\n");
#endif /* NON_NMEA_ENABLE */
}

static int throttled_write(int fd, char *buf, int len)
/* write to client -- throttle if it's gone or we're close to buffer overrun */
{
    int status;

    gpsd_report(3, "=> client(%d): %s", fd, buf);
    if ((status = write(fd, buf, len)) > -1)
	return status;
    if (errno == EBADF)
	gpsd_report(3, "Client on %d has vanished.\n", fd);
    else if (errno == EWOULDBLOCK)
	gpsd_report(3, "Dropped client on %d to avoid overrun.\n", fd);
    else
	gpsd_report(3, "Client write to %d: %s\n", fd, strerror(errno));
    FD_CLR(fd, &all_fds); FD_CLR(fd, &nmea_fds); FD_CLR(fd, &watcher_fds);
    return status;
}

static int validate(void)
{
#define VALIDATION_COMPLAINT(level, legend) \
        gpsd_report(level, legend " (status=%d, mode=%d).\r\n", \
		    session->gNMEAdata.status, session->gNMEAdata.mode)
    if ((session->gNMEAdata.status == STATUS_NO_FIX) != (session->gNMEAdata.mode == MODE_NO_FIX)) {
	VALIDATION_COMPLAINT(3, "GPS is confused about whether it has a fix");
	return 0;
    }
    else if (session->gNMEAdata.status > STATUS_NO_FIX && session->gNMEAdata.mode > MODE_NO_FIX) {
	VALIDATION_COMPLAINT(3, "GPS has a fix");
	return session->gNMEAdata.mode;
    }
    VALIDATION_COMPLAINT(3, "GPS has no fix");
    return 0;
#undef VALIDATION_CONSTRAINT
}

static int handle_request(int fd, char *buf, int buflen)
/* interpret a client request; fd is the socket back to the client */
{
    char reply[BUFSIZE], phrase[BUFSIZE], *p;
    int i, j;
    struct gps_data_t *ud = &session->gNMEAdata;

    sprintf(reply, "GPSD");
    p = buf;
    while (*p && p - buf < buflen) {
	switch (toupper(*p++)) {
	case 'A':
	    if (!validate())
		strcpy(phrase, ",A=?");
	    else
		sprintf(phrase, ",A=%f", ud->altitude);
	    break;
	case 'B':		/* change baud rate (SiRF only) */
#ifdef PROFILING
	    if (*p == '=') {
		i = atoi(++p);
		while (isdigit(*p)) p++;
		sirf_mode(session, 0, i);
	    }
#endif /* PROFILING */
	    sprintf(phrase, ",B=%d %d N %d", 
		    gpsd_get_speed(&session->ttyset),
		    9-session->device_type->stopbits,
		    session->device_type->stopbits);
	    break;
	case 'C':
	    sprintf(phrase, ",C=%d", session->device_type->cycle);
	    break;
	case 'D':
	    if (ud->utc[0]) {
		sprintf(phrase, ",D=%s", ud->utc);
#ifdef PROFILING
		if (ud->profiling) {
		    struct timeval tv;
		    gettimeofday(&tv, NULL);
		    sprintf(phrase+strlen(phrase), ",$=%s:%lf:%d:%ld.%ld",
			    ud->tag,
			    ud->recv_time,
			    ud->sentence_length,
			    tv.tv_sec, tv.tv_usec); 
		}
#endif /* PROFILING */
	    } else
		strcpy(phrase, ",D=?");
	    break;
	case 'L':
	    sprintf(phrase, ",l=1 " VERSION " abcdmpqrstvwxy");
	    break;
	case 'M':
	    if (ud->mode == MODE_NOT_SEEN)
		strcpy(phrase, ",M=?");
	    else
		sprintf(phrase, ",M=%d", ud->mode);
	    break;
	case 'P':
	    if (!validate())
		strcpy(phrase, ",P=?");
	    else
		sprintf(phrase, ",P=%f %f", 
			ud->latitude, ud->longitude);
	    break;
	case 'Q':
	    if (!validate())
		strcpy(phrase, ",Q=?");
	    else
		sprintf(phrase, ",Q=%d %f %f %f",
			ud->satellites_used, ud->pdop, ud->hdop, ud->vdop);
	    break;
	case 'R':
	    if (*p == '=') ++p;
	    if (*p == '1' || *p == '+') {
		FD_SET(fd, &nmea_fds);
		gpsd_report(3, "%d turned on raw mode\n", fd);
		sprintf(phrase, ",R=1");
		p++;
	    } else if (*p == '0' || *p == '-') {
		FD_CLR(fd, &nmea_fds);
		gpsd_report(3, "%d turned off raw mode\n", fd);
		sprintf(phrase, ",R=0");
		p++;
	    } else if (FD_ISSET(fd, &nmea_fds)) {
		FD_CLR(fd, &nmea_fds);
		gpsd_report(3, "%d turned off raw mode\n", fd);
		sprintf(phrase, ",R=0");
	    } else {
		FD_SET(fd, &nmea_fds);
		gpsd_report(3, "%d turned on raw mode\n", fd);
		sprintf(phrase, ",R=1");
	    }
	    break;
	case 'S':
	    sprintf(phrase, ",S=%d", ud->status);
	    break;
	case 'T':
	    if (!validate())
		strcpy(phrase, ",T=?");
	    else
		sprintf(phrase, ",T=%f", ud->track);
	    break;
	case 'V':
	    if (!validate())
		strcpy(phrase, ",V=?");
	    else
		sprintf(phrase, ",V=%f", ud->speed);
	    break;
	case 'W':
	    if (*p == '=') ++p;
	    if (*p == '1' || *p == '+') {
		FD_SET(fd, &watcher_fds);
		gpsd_report(3, "%d turned on watching\n", fd);
		sprintf(phrase, ",W=1");
		p++;
	    } else if (*p == '0' || *p == '-') {
		FD_CLR(fd, &watcher_fds);
		gpsd_report(3, "%d turned off watching\n", fd);
		sprintf(phrase, ",W=0");
		p++;
	    } else if (FD_ISSET(fd, &watcher_fds)) {
		FD_CLR(fd, &watcher_fds);
		gpsd_report(3, "%d turned off watching\n", fd);
		sprintf(phrase, ",W=0");
	    } else {
		FD_SET(fd, &watcher_fds);
		gpsd_report(3, "%d turned on watching\n", fd);
		sprintf(phrase, ",W=1");
	    }
	    break;
        case 'X':
	    sprintf(phrase, ",X=%d", ud->online);
	    break;
	case 'Y':
	    if (!ud->satellites)
		strcpy(phrase, ",Y=?");
	    else {
		int used;
		sprintf(phrase, ",Y=%d:", ud->satellites);
		if (SEEN(ud->satellite_stamp)) {
		    int reported = 0;
		    for (i = 0; i < ud->satellites; i++) {
			used = 0;
			for (j = 0; j < ud->satellites_used; j++)
			    if (ud->used[j] == ud->PRN[i]) {
				used = 1;
				break;
			    }
			if (ud->PRN[i]) {
			    sprintf(phrase+strlen(phrase), "%d %d %d %d %d:", 
				    ud->PRN[i], 
				    ud->elevation[i],ud->azimuth[i],
				    ud->ss[i],
				    used);
			    reported++;
			}
		    }
		    assert(reported == ud->satellites);
		}
	    }
	    break;
#ifdef PROFILING
	case 'Z':
	    if (*p == '=') ++p;
	    if (*p == '1' || *p == '+') {
		ud->profiling = 1;
		gpsd_report(3, "%d turned on profiling mode\n", fd);
		sprintf(phrase, ",Z=1");
		p++;
	    } else if (*p == '0' || *p == '-') {
		ud->profiling = 0;
		gpsd_report(3, "%d turned off profiling mode\n", fd);
		sprintf(phrase, ",Z=0");
		p++;
	    } else if (FD_ISSET(fd, &nmea_fds)) {
		ud->profiling = 0;
		gpsd_report(3, "%d turned off profiling mode\n", fd);
		sprintf(phrase, ",Z=0");
	    } else {
		ud->profiling=1;
		gpsd_report(3, "%d turned on profiling mode\n", fd);
		sprintf(phrase, ",Z=1");
	    }
	    break;
#endif /* PROFILING */

	case '\r': case '\n':
	    goto breakout;
	}
	if (strlen(reply) + strlen(phrase) < sizeof(reply) - 1)
	    strcat(reply, phrase);
	else
	    return -1;	/* Buffer would overflow.  Just return an error */
    }
 breakout:
    strcat(reply, "\r\n");

    return throttled_write(fd, reply, strlen(reply));
}

static void notify_watchers(char *sentence)
/* notify all watching clients of an event */
{
    int fd;

    for (fd = 0; fd < nfds; fd++)
	if (FD_ISSET(fd, &watcher_fds))
	    throttled_write(fd, sentence, strlen(sentence));
}

static void raw_hook(char *sentence)
/* hook to be executed on each incoming sentence */
/* CAUTION: only one NMEA sentence per call */
{
    int fd;

#ifdef PROFILING
    char *sp, *tp;
    if (sentence[0] != '$')
	session->gNMEAdata.tag[0] = '\0';
    else {
	for (tp = session->gNMEAdata.tag, sp = sentence+1; *sp && *sp != ','; sp++, tp++)
	    *tp = *sp;
	*tp = '\0';
    }
    session->gNMEAdata.sentence_length = strlen(sentence);
#endif /* PROFILING */

    for (fd = 0; fd < nfds; fd++) {
	/* copy raw NMEA sentences from GPS to clients in raw mode */
	if (FD_ISSET(fd, &nmea_fds))
	    throttled_write(fd, sentence, strlen(sentence));

	/* some listeners may be in watcher mode */
	if (FD_ISSET(fd, &watcher_fds)) {
#define PUBLISH(fd, cmds)	handle_request(fd, cmds, sizeof(cmds)-1)
	    if (PREFIX("$GPRMC", sentence)) {
		PUBLISH(fd, "pdtvs");
	    } else if (PREFIX("$GPGGA", sentence)) {
		PUBLISH(fd, "pdasm");	
	    } else if (PREFIX("$GPGLL", sentence)) {
		PUBLISH(fd, "pd");
	    } else if (PREFIX("$GPVTG", sentence)) {
		PUBLISH(fd, "tv");
	    } else if (PREFIX("$GPGSA", sentence)) {
		PUBLISH(fd, "qm");
	    } else if (PREFIX("$GPGSV", sentence)) {
		if (nmea_sane_satellites(&session->gNMEAdata))
		    PUBLISH(fd, "y");
	    }
#undef PUBLISH
	}
    }
}

static int passivesock(char *service, char *protocol, int qlen)
{
    struct servent *pse;
    struct protoent *ppe;
    struct sockaddr_in sin;
    int s, type, one = 1;

    memset((char *) &sin, '\0', sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;

    if ( (pse = getservbyname(service, protocol)) )
	sin.sin_port = htons(ntohs((u_short) pse->s_port));
    else if ((sin.sin_port = htons((u_short) atoi(service))) == 0) {
	gpsd_report(0, "Can't get \"%s\" service entry.\n", service);
	return -1;
    }
    if ((ppe = getprotobyname(protocol)) == 0) {
	gpsd_report(0, "Can't get \"%s\" protocol entry.\n", protocol);
	return -1;
    }
    if (strcmp(protocol, "udp") == 0)
	type = SOCK_DGRAM;
    else
	type = SOCK_STREAM;
    if ((s = socket(PF_INET, type, ppe->p_proto)) < 0) {
	gpsd_report(0, "Can't create socket\n");
	return -1;
    }
    if (setsockopt(s,SOL_SOCKET,SO_REUSEADDR,(char *)&one,sizeof(one)) == -1) {
	gpsd_report(0, "Error: SETSOCKOPT SO_REUSEADDR\n");
	return -1;
    }
    if (bind(s, (struct sockaddr *) &sin, sizeof(sin)) < 0) {
	gpsd_report(0, "Can't bind to port %s\n", service);
	return -1;
    }
    if (type == SOCK_STREAM && listen(s, qlen) < 0) {
	gpsd_report(0, "Can't listen on %s port%s\n", service);
	return -1;
    }
    return s;
}

int main(int argc, char *argv[])
{
    static int nowait = 0, gpsd_speed = 0;
    static char gpstype = 'n', *dgpsserver = NULL;
    char *service = NULL; 
    struct sockaddr_in fsin;
    fd_set rfds;
    int option, msock, fd, need_gps; 
    extern char *optarg;

    debuglevel = 0;
    while ((option = getopt(argc, argv, "D:S:d:hNnp:P:s:v"
#if TRIPMATE_ENABLE || defined(ZODIAC_ENABLE)
			    "i:"
#endif /* TRIPMATE_ENABLE || defined(ZODIAC_ENABLE) */
#ifdef NON_NMEA_ENABLE
			    "T:"
#endif /* NON_NMEA_ENABLE */
		)) != -1) {
	switch (option) {
#ifdef NON_NMEA_ENABLE
	case 'T':
	    gpstype = *optarg;
	    break;
#endif /* NON_NMEA_ENABLE */
	case 'D':
	    debuglevel = (int) strtol(optarg, 0, 0);
	    if (debuglevel >= 2)
		go_background = 0;
	    break;
	case 'N':
	    go_background = 0;
	    break;
	case 'S':
	    service = optarg;
	    break;
	case 'd':
	    dgpsserver = optarg;
	    break;
#if TRIPMATE_ENABLE || defined(ZODIAC_ENABLE)
	case 'i': {
	    char *colon;
	    if (!(colon = strchr(optarg, ':')) || colon == optarg)
		fprintf(stderr, 
			"gpsd: required format is latitude:longitude.\n");
	    else if (!strchr("NSns", colon[-1]))
		fprintf(stderr,
			"gpsd: latitude field is invalid; must end in N or S.\n");
	    else if (!strchr("EWew", optarg[strlen(optarg)-1]))
		fprintf(stderr,
			"gpsd: longitude field is invalid; must end in E or W.\n");
	   else {
		*colon = '\0';
		session->latitude = optarg;
 		session->latd = toupper(optarg[strlen(session->latitude) - 1]);
		session->latitude[strlen(session->latitude) - 1] = '\0';
		session->longitude = colon+1;
		session->lond = toupper(session->longitude[strlen(session->longitude)-1]);
		session->longitude[strlen(session->longitude)-1] = '\0';
	    }
	    break;
	}
#endif /* TRIPMATE_ENABLE || defined(ZODIAC_ENABLE) */
	case 'n':
	    nowait = 1;
	    break;
	case 'p':
	    device_name = optarg;
	    break;
	case 'P':
	    pid_file = optarg;
	    break;
	case 's':
	    gpsd_speed = atoi(optarg);
	    break;
	case 'v':
	    printf("gpsd %s\n", VERSION);
	    exit(0);
	case 'h': case '?':
	default:
	    usage();
	    exit(0);
	}
    }

    if (!service)
	service = getservbyname("gpsd", "tcp") ? "gpsd" : DEFAULT_GPSD_PORT;
    if (go_background)
	daemonize();

    /* Handle some signals */
    signal(SIGHUP, restart);
    signal(SIGINT, onsig);
    signal(SIGTERM, onsig);
    signal(SIGQUIT, onsig);
    signal(SIGPIPE, SIG_IGN);

    openlog("gpsd", LOG_PID, LOG_USER);
    gpsd_report(1, "launching (Version %s)\n", VERSION);
    if ((msock = passivesock(service, "tcp", QLEN)) < 0) {
	gpsd_report(0, "startup failed, netlib error %d\n", msock);
	exit(2);
    }
    gpsd_report(1, "listening on port %s\n", service);

    /* user may want to re-initialize the session */
    if (setjmp(restartbuf) == THROW_SIGHUP) {
	gpsd_wrap(session);
	gpsd_report(1, "gpsd restarted by SIGHUP\n");
    }

    FD_ZERO(&all_fds); FD_ZERO(&nmea_fds); FD_ZERO(&watcher_fds);
    FD_SET(msock, &all_fds);
    nfds = FD_SETSIZE;

    session = gpsd_init(gpstype, dgpsserver);
    if (gpsd_speed)
	session->gNMEAdata.baudrate = gpsd_speed;
    session->gpsd_device = device_name;
    session->gNMEAdata.raw_hook = raw_hook;
    if (session->dsock >= 0)
	FD_SET(session->dsock, &all_fds);
    if (nowait) {
	if (gpsd_activate(session) < 0) {
	    gpsd_report(0, "exiting - GPS device nonexistent or can't be read\n");
	    exit(2);
	}
	FD_SET(session->gNMEAdata.gps_fd, &all_fds);
    }

    for (;;) {
	struct timeval tv;

        memcpy((char *)&rfds, (char *)&all_fds, sizeof(rfds));

	/* 
	 * Poll for user commands or GPS data.  The timeout doesn't
	 * actually matter here since select returns whenever one of
	 * the file descriptors in the set goes ready. 
	 */
	tv.tv_sec = 1; tv.tv_usec = 0;
	if (select(nfds, &rfds, NULL, NULL, &tv) < 0) {
	    if (errno == EINTR)
		continue;
	    gpsd_report(0, "select: %s\n", strerror(errno));
	    exit(2);
	}

	/* always be open to new connections */
	if (FD_ISSET(msock, &rfds)) {
	    socklen_t alen = sizeof(fsin);
	    int ssock = accept(msock, (struct sockaddr *) &fsin, &alen);

	    if (ssock < 0)
		gpsd_report(0, "accept: %s\n", strerror(errno));
	    else {
		int opts = fcntl(ssock, F_GETFL);

		if (opts >= 0)
		    fcntl(ssock, F_SETFL, opts | O_NONBLOCK);
		gpsd_report(3, "client connect on %d\n", ssock);
		FD_SET(ssock, &all_fds);
	    }
	    FD_CLR(msock, &rfds);
	}

	/* we may need to force the GPS open */
	if (nowait && session->gNMEAdata.gps_fd == -1) {
	    gpsd_deactivate(session);
	    if (gpsd_activate(session) >= 0) {
		FD_SET(session->gNMEAdata.gps_fd, &all_fds);
	    }
	}

	/* get data from it */
	if (session->gNMEAdata.gps_fd >= 0 && gpsd_poll(session) < 0) {
	    gpsd_report(3, "GPS is offline\n");
	    FD_CLR(session->gNMEAdata.gps_fd, &all_fds);
	    gpsd_deactivate(session);
	    notify_watchers("GPSD,X=0\r\n");
	}

#ifdef PROFILING
	{
	    struct timeval tv;
	    gettimeofday(&tv, NULL);
	    session->gNMEAdata.recv_time = tv.tv_sec + tv.tv_usec / 1e6;
	}
#endif /* PROFILING */


	/* this simplifies a later test */
	if (session->dsock > -1)
	    FD_CLR(session->dsock, &rfds);

	/* accept and execute commands for all clients */
	need_gps = 0;
	for (fd = 0; fd < nfds; fd++) {
	    if (fd == msock || fd == session->gNMEAdata.gps_fd)
		continue;
	    /*
	     * GPS must be opened if commands are waiting or any client is
	     * streaming (raw or watcher mode).
	     */
	    if (FD_ISSET(fd, &rfds) || FD_ISSET(fd, &nmea_fds) || FD_ISSET(fd, &watcher_fds)) {
		if (session->gNMEAdata.gps_fd == -1) {
		    gpsd_deactivate(session);
		    if (gpsd_activate(session) >= 0) {
			FD_SET(session->gNMEAdata.gps_fd, &all_fds);
		    }
		}

		if (FD_ISSET(fd, &rfds)) {
		    char buf[BUFSIZE];
		    int buflen;
		    gpsd_report(3, "checking %d \n", fd);
		    if ((buflen = read(fd, buf, sizeof(buf) - 1)) <= 0) {
			(void) close(fd);
			FD_CLR(fd, &all_fds);
			FD_CLR(fd, &nmea_fds);
			FD_CLR(fd, &watcher_fds);
		    } else {
		        buf[buflen] = '\0';
			gpsd_report(1, "<= client: %s", buf);
			if (handle_request(fd, buf, buflen) < 0) {
			    (void) close(fd);
			    FD_CLR(fd, &all_fds);
			    FD_CLR(fd, &nmea_fds);
			    FD_CLR(fd, &watcher_fds);
			}
		    }
		}
	    }
	    if (fd != session->gNMEAdata.gps_fd && fd != msock && FD_ISSET(fd, &all_fds))
		need_gps++;
	}

	if (!nowait && !need_gps && session->gNMEAdata.gps_fd != -1) {
	    FD_CLR(session->gNMEAdata.gps_fd, &all_fds);
	    session->gNMEAdata.gps_fd = -1;
	    gpsd_deactivate(session);
	}
    }

    gpsd_wrap(session);
    return 0;
}
