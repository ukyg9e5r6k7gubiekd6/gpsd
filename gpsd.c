#include "config.h"
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

#define QLEN			5

struct gps_session_t *session;
static char *device_name = DEFAULT_DEVICE_NAME;
static int in_background = 0;
static fd_set all_fds, nmea_fds, watcher_fds;
static int debuglevel;
static int nfds;

static jmp_buf	restartbuf;
#define THROW_SIGHUP	1

static void restart(int sig)
{
    longjmp(restartbuf, THROW_SIGHUP);
}

static void onsig(int sig)
{
    gpsd_wrap(session);
    gpsd_report(1, "Received signal %d. Exiting...\n", sig);
    exit(10 + sig);
}

static int daemonize()
{
    int fd;
    pid_t pid;

    pid = fork();

    switch (pid) {
    case -1:
	return -1;
    case 0:
	break;
    default:
	_exit(pid);
    }

    if (setsid() == -1)
	return -1;
    chdir("/");
    fd = open(_PATH_DEVNULL, O_RDWR, 0);
    if (fd != -1) {
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
    char buf[BUFSIZ];
    va_list ap;

    strcpy(buf, "gpsd: ");
    va_start(ap, fmt) ;
#ifdef HAVE_VSNPRINTF
    vsnprintf(buf + strlen(buf), sizeof(buf)-strlen(buf), fmt, ap);
#else
    vsprintf(buf + strlen(buf), fmt, ap);
#endif
    va_end(ap);

    if (errlevel > debuglevel)
	return;

    if (in_background) {
	if (errlevel == 0)
	    syslog(LOG_ERR, buf);
	else
	    syslog(LOG_NOTICE, buf);
    }
    else
	fputs(buf, stderr);
}

static void usage()
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
"  -s baud_rate                   = set baud rate on gps device \n\
  -d host[:port]                 = set DGPS server \n\
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

    /*
     * All writes to client sockets go through this function.
     *
     * This code addresses two cases.  First, client has dropped the connection.
     * Second, client is still connected but not actually picking up data and
     * our buffers are backing up.  If we let this continue, the write buffers
     * will fill and the effect will be denial-of-service to clients that are
     * better behaved.
     *
     * Our strategy is brutally simple and takes advantage of the fact that
     * GPS data has a short shelf life.  If the client doesn't pick it up 
     * within a few minutes, it's probably not useful to that client.  So if
     * data is backing up to a client, drop that client.  That's why we set
     * the client socket to nonblocking.
     */
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

static int validate(int fd)
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
    char reply[BUFSIZE], *p;
    int i, j;
    time_t cur_time;

    cur_time = time(NULL);

    sprintf(reply, "GPSD");
    p = buf;
    while (*p) {
	switch (toupper(*p++)) {
	case 'A':
	    if (!validate(fd))
		strcat(reply, ",A=?");
	    else
		sprintf(reply + strlen(reply),
			",A=%f", session->gNMEAdata.altitude);
	    break;
	case 'D':
	    if (session->gNMEAdata.utc[0])
		sprintf(reply + strlen(reply),
			",D=%s", session->gNMEAdata.utc);
	    else
		strcat(reply, ",D=?");
	    break;
	case 'L':
	    sprintf(reply + strlen(reply), ",l=1 " VERSION " admpqrstvwxy");
	    break;
	case 'M':
		sprintf(reply + strlen(reply),
			",M=%d", session->gNMEAdata.mode);
	    break;
	case 'P':
	    if (!validate(fd))
		strcat(reply, ",P=?");
	    else
		sprintf(reply + strlen(reply),
			",P=%f %f", session->gNMEAdata.latitude,
			session->gNMEAdata.longitude);
	    break;
	case 'Q':
	    if (!validate(fd))
		strcat(reply, ",Q=?");
	    else
		sprintf(reply + strlen(reply),
			",Q=%d %f %f %f",
			session->gNMEAdata.satellites_used,
			session->gNMEAdata.pdop, session->gNMEAdata.hdop, session->gNMEAdata.vdop);
	    break;
	case 'R':
	    if (*p == '1' || *p == '+') {
		FD_SET(fd, &nmea_fds);
		gpsd_report(3, "%d turned on raw mode\n", fd);
		sprintf(reply + strlen(reply), ",R=1");
		p++;
	    } else if (*p == '0' || *p == '-') {
		FD_CLR(fd, &nmea_fds);
		gpsd_report(3, "%d turned off raw mode\n", fd);
		sprintf(reply + strlen(reply), ",R=0");
		p++;
	    } else if (FD_ISSET(fd, &nmea_fds)) {
		FD_CLR(fd, &nmea_fds);
		gpsd_report(3, "%d turned off raw mode\n", fd);
		sprintf(reply + strlen(reply), ",R=0");
	    } else {
		FD_SET(fd, &nmea_fds);
		gpsd_report(3, "%d turned on raw mode\n", fd);
		sprintf(reply + strlen(reply), ",R=1");
	    }
	    break;
	case 'S':
	    sprintf(reply + strlen(reply), ",S=%d", session->gNMEAdata.status);
	    break;
	case 'T':
	    if (!validate(fd))
		strcat(reply, ",T=?");
	    else
		sprintf(reply + strlen(reply),
			",T=%f", session->gNMEAdata.track);
	    break;
	case 'V':
	    if (!validate(fd))
		strcat(reply, ",V=?");
	    else
		sprintf(reply + strlen(reply),
			",V=%f", session->gNMEAdata.speed);
	    break;
	case 'W':
	    if (*p == '1' || *p == '+') {
		FD_SET(fd, &watcher_fds);
		gpsd_report(3, "%d turned on watching\n", fd);
		sprintf(reply + strlen(reply), ",W=1");
		p++;
	    } else if (*p == '0' || *p == '-') {
		FD_CLR(fd, &watcher_fds);
		gpsd_report(3, "%d turned off watching\n", fd);
		sprintf(reply + strlen(reply), ",W=0");
		p++;
	    } else if (FD_ISSET(fd, &watcher_fds)) {
		FD_CLR(fd, &watcher_fds);
		gpsd_report(3, "%d turned off watching\n", fd);
		sprintf(reply + strlen(reply), ",W=0");
	    } else {
		FD_SET(fd, &watcher_fds);
		gpsd_report(3, "%d turned on watching\n", fd);
		sprintf(reply + strlen(reply), ",W=1");
	    }
	    break;
        case 'X':
	    if (session->gNMEAdata.gps_fd == -1)
		strcat(reply, ",X=0");
	    else
		strcat(reply, ",X=1");
	    break;
	case 'Y':
	    if (!session->gNMEAdata.satellites)
		strcat(reply, ",Y=?");
	    else {
		int used;
		sprintf(reply + strlen(reply),
			",Y=%d:", session->gNMEAdata.satellites);
		if (SEEN(session->gNMEAdata.satellite_stamp))
		    for (i = 0; i < session->gNMEAdata.satellites; i++) {
			used = 0;
			for (j = 0; j < session->gNMEAdata.satellites_used; j++)
			    if (session->gNMEAdata.used[j] == session->gNMEAdata.PRN[i]) {
				used = 1;
				break;
			    }
			if (session->gNMEAdata.PRN[i])
			    sprintf(reply + strlen(reply),
				    "%d %d %d %d %d:", 
				    session->gNMEAdata.PRN[i], 
				    session->gNMEAdata.elevation[i],
				    session->gNMEAdata.azimuth[i],
				    session->gNMEAdata.ss[i],
				    used);
		    }
		}
	    break;
	case '\r':
	case '\n':
	    goto breakout;
	}
    }
 breakout:
    strcat(reply, "\r\n");

    return throttled_write(fd, reply, strlen(reply));
}

static void notify_watchers(char *sentence)
/* notify all watching clients of an event */
{
    int fd;

    for (fd = 0; fd < nfds; fd++) {
	if (FD_ISSET(fd, &watcher_fds)) {
	    throttled_write(fd, sentence, strlen(sentence));
	}
    }
}

static void raw_hook(char *sentence)
/* hook to be executed on each incoming sentence */
{
    int fd;

    for (fd = 0; fd < nfds; fd++) {
	/* copy raw NMEA sentences from GPS to clients in raw mode */
	if (FD_ISSET(fd, &nmea_fds))
	    throttled_write(fd, sentence, strlen(sentence));

	/* some listeners may be in watcher mode */
	if (FD_ISSET(fd, &watcher_fds)) {
#define PUBLISH(fd, cmds)	handle_request(fd, cmds, sizeof(cmds)-1)
	    if (strncmp(GPRMC, sentence, sizeof(GPRMC)-1) == 0) {
		PUBLISH(fd, "pdtvs");
	    } else if (strncmp(GPGGA, sentence, sizeof(GPGGA)-1) == 0) {
		PUBLISH(fd, "pdas");	
	    } else if (strncmp(GPGLL, sentence, sizeof(GPGLL)-1) == 0) {
		PUBLISH(fd, "pd");
	    } else if (strncmp(GPVTG, sentence, sizeof(GPVTG)-1) == 0) {
		PUBLISH(fd, "tv");
	    } else if (strncmp(GPGSA, sentence, sizeof(GPGSA)-1) == 0) {
		PUBLISH(fd, "qm");
	    } else if (strncmp(GPGSV, sentence, sizeof(GPGSV)-1) == 0) {
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
    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *)&one, sizeof(one)) == -1) {
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

static int setnonblocking(int sock)
/* set socket to return EWOULDBLOCK if the write would block */
{
    int opts;
                                                                                
    opts = fcntl(sock, F_GETFL);
    if (opts < 0) {
        perror("fcntl(F_GETFL)");
        return -1;
    }
    opts = (opts | O_NONBLOCK);
    if (fcntl(sock, F_SETFL, opts) < 0) {
        perror("fcntl(F_SETFL)");
        return -1;
    }
    return 0;
}

int main(int argc, char *argv[])
{
    char *service = NULL;
    char *dgpsserver = NULL;
    struct sockaddr_in fsin;
    fd_set rfds;
    int msock, alen, fd, need_gps;
    extern char *optarg;
    int option, gpsd_speed = 0;
    char gpstype = 'n';
    int nowait = 0;

    debuglevel = 1;
    while ((option = getopt(argc, argv, "D:S:d:hnp:s:"
#if TRIPMATE_ENABLE
			    "i:"
#endif /* TRIPMATE_ENABLE */
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
	    break;
	case 'S':
	    service = optarg;
	    break;
	case 'd':
	    dgpsserver = optarg;
	    break;
#if TRIPMATE_ENABLE
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
		session->initpos.latitude = optarg;
 		session->initpos.latd = toupper(optarg[strlen(session->initpos.latitude) - 1]);
		session->initpos.latitude[strlen(session->initpos.latitude) - 1] = '\0';
		session->initpos.longitude = colon+1;
		session->initpos.lond = toupper(session->initpos.longitude[strlen(session->initpos.longitude)-1]);
		session->initpos.longitude[strlen(session->initpos.longitude)-1] = '\0';
	    }
	    break;
	}
#endif /* TRIPMATE_ENABLE */
	case 'n':
	    nowait = 1;
	    break;
	case 'p':
	    device_name = optarg;
	    break;
	case 's':
	    gpsd_speed = atoi(optarg);
	    break;
	case 'h':
	case '?':
	default:
	    usage();
	    exit(0);
	}
    }

    if (!service) {
	if (!getservbyname("gpsd", "tcp"))
	    service = DEFAULT_GPSD_PORT;
	else
	    service = "gpsd";
    }

    if (debuglevel < 2)
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
    nfds = getdtablesize();

    session = gpsd_init(gpstype, dgpsserver);
    if (gpsd_speed)
	session->baudrate = gpsd_speed;
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

    while (1) {
	struct timeval tv;

        memcpy((char *)&rfds, (char *)&all_fds, sizeof(rfds));

	/* 
	 * Poll for user commands or GPS data.  GPS sensors typically
	 * update once a second.  Nyquist's theorem tells us that it
	 * is optimal to sample at twice this frequency.
	 */
	tv.tv_sec = 0; tv.tv_usec = 500000;
	if (select(nfds, &rfds, NULL, NULL, &tv) < 0) {
	    if (errno == EINTR)
		continue;
	    gpsd_report(0, "select: %s\n", strerror(errno));
	    exit(2);
	}

	/* always be open to new connections */
	if (FD_ISSET(msock, &rfds)) {
	    int ssock;

	    alen = sizeof(fsin);
	    ssock = accept(msock, (struct sockaddr *) &fsin, &alen);

	    if (ssock < 0)
		gpsd_report(0, "accept: %s\n", strerror(errno));
	    else {
		gpsd_report(3, "client connect on %d\n", ssock);
		FD_SET(ssock, &all_fds);
		setnonblocking(ssock);
	    }
	    FD_CLR(msock, &rfds);
	}

	/* we may need to force the GPS open */
	if (nowait && session->gNMEAdata.gps_fd == -1) {
	    gpsd_deactivate(session);
	    if (gpsd_activate(session) >= 0) {
		notify_watchers("GPSD,X=1\r\n");
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

	/* this simplifies a later test */
	if (session->dsock > -1)
	    FD_CLR(session->dsock, &rfds);

	/* accept and execute commands for all clients */
	need_gps = 0;
	for (fd = 0; fd < getdtablesize(); fd++) {
	    if (fd == msock || fd == session->gNMEAdata.gps_fd)
		continue;
	    /*
	     * GPS must be opened if commands are waiting or any client is
	     * streaming (raw or watcher mode).
	     */
	    if (FD_ISSET(fd, &rfds) || FD_ISSET(fd, &nmea_fds) || FD_ISSET(fd, &watcher_fds)) {
		char buf[BUFSIZE];
		int buflen;

		if (session->gNMEAdata.gps_fd == -1) {
		    gpsd_deactivate(session);
		    if (gpsd_activate(session) >= 0) {
			notify_watchers("GPSD,X=1\r\n");
			FD_SET(session->gNMEAdata.gps_fd, &all_fds);
		    }
		}

		if (FD_ISSET(fd, &rfds)) {
		    buflen = read(fd, buf, sizeof(buf) - 1);
		    if (buflen <= 0) {
			(void) close(fd);
			FD_CLR(fd, &all_fds);
		    }
		    buf[buflen] = '\0';
		    gpsd_report(1, "<= client: %s", buf);
		    if (handle_request(fd, buf, buflen) < 0) {
			(void) close(fd);
			FD_CLR(fd, &all_fds);
		    }
		}
	    }
	    if (fd != session->gNMEAdata.gps_fd && fd != msock && FD_ISSET(fd, &all_fds)) {
		need_gps++;
	    }
	}

	if (!nowait && !need_gps && session->gNMEAdata.gps_fd != -1) {
	    FD_CLR(session->gNMEAdata.gps_fd, &all_fds);
	    session->gNMEAdata.gps_fd = -1;
	    gpsd_deactivate(session);
	}
    }

    gpsd_wrap(session);
}



