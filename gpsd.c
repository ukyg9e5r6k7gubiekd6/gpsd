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

#define QLEN			5

static fd_set all_fds, nmea_fds, watcher_fds;
static int debuglevel, in_background = 0;
static jmp_buf restartbuf;

static void onsig(int sig)
{
    longjmp(restartbuf, sig+1);
}

static int daemonize(char *pid_file)
{
    int fd;
    pid_t pid;

    switch (pid = fork()) {
    case -1:
	return -1;
    case 0:	/* child side */
	break;
    default:	/* parent side */
	if (pid_file) {
	    FILE *fp;

	    if ((fp = fopen(pid_file, "w")) != NULL) {
		fprintf(fp, "%u\n", pid);
		(void) fclose(fp);
	    } else {
		gpsd_report(1, "Cannot create PID file: %s.\n", pid_file);
	    }
	}
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
  -f string (default %s)  	= set GPS device name \n\
  -S integer (default %s)	= set port for daemon \n\
  -d host[:port]         	= set DGPS server \n\
  -P pidfile              	= set file to record process ID \n\
  -D integer (default 0)  	= set debug level \n\
  -h                     	= help message \n",
	   DEFAULT_DEVICE_NAME, DEFAULT_GPSD_PORT);
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

static int have_fix(struct gps_session_t *session)
{
#define VALIDATION_COMPLAINT(level, legend) \
        gpsd_report(level, legend " (status=%d, mode=%d).\r\n", \
		    session->gNMEAdata.status, session->gNMEAdata.mode)
    if ((session->gNMEAdata.status == STATUS_NO_FIX) != (session->gNMEAdata.mode == MODE_NO_FIX)) {
	VALIDATION_COMPLAINT(3, "GPS is confused about whether it has a fix");
	return 0;
    }
    else if (session->gNMEAdata.status > STATUS_NO_FIX && session->gNMEAdata.mode != MODE_NO_FIX) {
	VALIDATION_COMPLAINT(3, "GPS has a fix");
	return 1;
    }
    VALIDATION_COMPLAINT(3, "GPS has no fix");
    return 0;
#undef VALIDATION_CONSTRAINT
}

static void notify_watchers(char *sentence)
/* notify all watching clients of an event */
{
    int fd;

    for (fd = 0; fd < FD_SETSIZE; fd++)
	if (FD_ISSET(fd, &watcher_fds))
	    throttled_write(fd, sentence, strlen(sentence));
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

/* restrict the scope of the command-state globals as much as possible */
static struct gps_session_t *session;
static int need_gps;

static int handle_request(int fd, char *buf, int buflen, int explicit)
/* interpret a client request; fd is the socket back to the client */
{
    char reply[BUFSIZ], phrase[BUFSIZ], *p, *q;
    int i, j;
    struct gps_data_t *ud = &session->gNMEAdata;
    int icd = 0;

    sprintf(reply, "GPSD");
    p = buf;
    while (*p && p - buf < buflen) {
	phrase[0] = '\0';
	switch (toupper(*p++)) {
	case 'A':
	    if (have_fix(session) && SEEN(ud->altitude_stamp))
		sprintf(phrase, ",A=%f", ud->altitude);
	    else if (explicit)
		strcpy(phrase, ",A=?");
	    break;
	case 'B':		/* change baud rate (SiRF only) */
	    if (*p == '=') {
		i = atoi(++p);
		while (isdigit(*p)) p++;
		if (session->device_type->speed_switcher)
		    if (session->device_type->speed_switcher(session, i))
			gpsd_set_speed(session, (speed_t)i, 1);
	    }
	    sprintf(phrase, ",B=%d %d N %d", 
		    gpsd_get_speed(&session->ttyset),
		    9 - ud->stopbits, ud->stopbits);
	    break;
	case 'C':
	    sprintf(phrase, ",C=%d", session->device_type->cycle);
	    break;
	case 'D':
	    if (ud->utc[0]) {
		sprintf(phrase, ",D=%s", ud->utc);
		icd = 1;
	    } else if (explicit)
		strcpy(phrase, ",D=?");
	    break;
	case 'E':
	    if (have_fix(session)) {
		if (ud->seen_sentences & PGRME)
		    sprintf(phrase, ",E=%.2f %.2f %.2f", ud->epe, ud->eph, ud->epv);
		else if (SEEN(ud->fix_quality_stamp))
		    sprintf(phrase, ",E=%.2f %.2f %.2f", 
			    ud->pdop * UERE(session), 
			    ud->hdop * UERE(session), 
			    ud->vdop * UERE(session));
	    } else if (explicit)
		strcpy(phrase, ",E=?");
	    break;
	case 'F':
	    if (*p == '=') {
		char	*bufcopy;
		for (q = ++p; isgraph(*p); p++)
		    continue;
		bufcopy = (char *)malloc(p-q+1); 
		memcpy(bufcopy, q, p-q);
		bufcopy[p-q] = '\0';
		gpsd_report(1, "Switch to %s requested\n", bufcopy);

		if (need_gps > 1) 
		    gpsd_report(1, "Switch to %s failed, %d clients\n", bufcopy, need_gps);
		else {
		    char *stash_device;
		    gpsd_deactivate(session);
		    stash_device = session->gpsd_device;
		    session->gpsd_device = strdup(bufcopy);
		    session->gNMEAdata.baudrate = 0;	/* so it'll hunt */
		    session->driverstate = 0;
		    if (gpsd_activate(session) >= 0) {
			gpsd_report(1, "Switch to %s succeeded\n", bufcopy);
			free(stash_device);
		    } else {
			gpsd_report(1, "Switch to %s failed\n", bufcopy);
			free(session->gpsd_device);
			session->gpsd_device = stash_device;
			session->gNMEAdata.baudrate = 0;
			session->driverstate = 0;
		    }
		}
		gpsd_report(1, "GPS is %s\n", session->gpsd_device);
	    }
	    sprintf(phrase, ",F=%s", session->gpsd_device);
	    break;
	case 'I':
	    sprintf(phrase, ",I=%s", session->device_type->typename);
	    break;
	case 'L':
	    sprintf(phrase, ",L=1 " VERSION " abcdefilmpqrstuvwxy");	//ghjkno
	    break;
	case 'M':
	    if (ud->mode == MODE_NOT_SEEN)
		strcpy(phrase, ",M=?");
	    else
		sprintf(phrase, ",M=%d", ud->mode);
	    break;
	case 'P':
	    if (have_fix(session) && SEEN(ud->latlon_stamp))
		sprintf(phrase, ",P=%f %f", 
			ud->latitude, ud->longitude);
	    else if (explicit)
		strcpy(phrase, ",P=?");
	    break;
	case 'Q':
	    if (SEEN(ud->fix_quality_stamp))
		sprintf(phrase, ",Q=%d %.2f %.2f %.2f",
			ud->satellites_used, ud->pdop, ud->hdop, ud->vdop);
	    else if (explicit)
		strcpy(phrase, ",Q=?");
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
	    if (have_fix(session) && SEEN(ud->track_stamp))
		sprintf(phrase, ",T=%f", ud->track);
	    else if (explicit)
		strcpy(phrase, ",T=?");
	    break;
	case 'U':
	    if (have_fix(session) && SEEN(ud->climb_stamp))
		sprintf(phrase, ",U=%f", ud->climb);
	    else if (explicit)
		strcpy(phrase, ",U=?");
	    break;
	case 'V':
	    if (have_fix(session) && SEEN(ud->speed_stamp))
		sprintf(phrase, ",V=%f", ud->speed);
	    else if (explicit)
		strcpy(phrase, ",V=?");
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
	    if (SEEN(ud->satellite_stamp)) {
		int used, reported = 0;
		sprintf(phrase, ",Y=%d:", ud->satellites);
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
	    } else if (explicit)
		strcpy(phrase, ",Y=?");
	    break;
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
		ud->profiling = !ud->profiling;
		gpsd_report(3, "%d toggled profiling mode\n", fd);
		sprintf(phrase, ",Z=%d", ud->profiling);
	    }
	    break;

	case '\r': case '\n':
	    goto breakout;
	}
	if (strlen(reply) + strlen(phrase) < sizeof(reply) - 1)
	    strcat(reply, phrase);
	else
	    return -1;	/* Buffer would overflow.  Just return an error */
    }
 breakout:
    if (ud->profiling && icd) {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	sprintf(phrase, ",$=%s %.4d %.4f %.4f %.4f %.4f %.4f %.4lf",
		ud->tag,
		ud->sentence_length,
		ud->gps_time,
		ud->d_xmit_time - ud->gps_time,
		ud->d_recv_time - ud->gps_time,
		ud->d_decode_time - ud->gps_time,
		session->poll_times[fd] - ud->gps_time,
		timestamp() - ud->gps_time); 
	if (strlen(reply) + strlen(phrase) < sizeof(reply) - 1)
	    strcat(reply, phrase);
    }
    strcat(reply, "\r\n");

    return throttled_write(fd, reply, strlen(reply));
}

static void raw_hook(struct gps_data_t *ud, char *sentence)
/* hook to be executed on each incoming sentence group */
{
    int fd, mask = 0;
    char cmds[16], *sp;

    for (sp = sentence; *sp; sp++) {
	if (*sp == '$') {
	    if (PREFIX("$GPRMC", sp)) {
		mask |= GPRMC;
	    } else if (PREFIX("$GPGGA", sp)) {
		mask |= GPGGA;
	    } else if (PREFIX("$GPGLL", sp)) {
		mask |= GPGLL;
	    } else if (PREFIX("$GPVTG", sp)) {
		mask |= GPVTG;
	    } else if (PREFIX("$GPGSA", sp)) {
		mask |= GPGSA;
	    } else if (PREFIX("$GPGSV", sp)) {
		if (nmea_sane_satellites(ud))
		    mask |= GPGSV;
	    } else if (PREFIX("$PGRME", sp)) {
		mask |= PGRME;
	    }
	}
    }
    cmds[0] = '\0';
    if (mask & (GPRMC | GPGGA | GPGLL))
	strcat(cmds, "dp");
    if (mask & (GPGGA))
	strcat(cmds, "a");
    if (mask & (GPRMC | GPVTG))
	strcat(cmds, "tuv");
    if (mask & (GPRMC | GPGGA))
	strcat(cmds, "s");
    if (mask & (GPGSA | GPGGA))
	strcat(cmds, "m");
    if (mask & (GPGGA))
	strcat(cmds, "q");
    if (mask & (GPGSV))
	strcat(cmds, "y");
    if (mask & (GPGSA | PGRME))
	strcat(cmds, "e");

    for (fd = 0; fd < FD_SETSIZE; fd++) {
	/* copy raw NMEA sentences from GPS to clients in raw mode */
	if (FD_ISSET(fd, &nmea_fds))
	    throttled_write(fd, sentence, strlen(sentence));

	/* some listeners may be in watcher mode */
	if (FD_ISSET(fd, &watcher_fds)) {
	    handle_request(fd, cmds, strlen(cmds), 0);
	}
    }
}

int main(int argc, char *argv[])
{
    static char *pid_file = NULL;
    static int st, nowait = 0, gpsd_speed = 0;
    static char *dgpsserver = NULL;
    static char *service = NULL; 
    static char *device_name = DEFAULT_DEVICE_NAME;
    struct sockaddr_in fsin;
    fd_set rfds;
    int option, msock, fd, go_background = 1;
    extern char *optarg;

    debuglevel = 0;
    while ((option = getopt(argc, argv, "D:S:d:f:hNnp:P:v")) != -1) {
	switch (option) {
	case 'D':
	    debuglevel = (int) strtol(optarg, 0, 0);
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
	case 'n':
	    nowait = 1;
	    break;
	case 'f':
	case 'p':
	    device_name = optarg;
	    break;
	case 'P':
	    pid_file = optarg;
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
	daemonize(pid_file);

    /* user may want to re-initialize the session */
    if ((st = setjmp(restartbuf)) == SIGHUP+1) {
	gpsd_wrap(session);
	gpsd_report(1, "gpsd restarted by SIGHUP\n");
    } else if (st > 0) {
	gpsd_wrap(session);
	gpsd_report(1, "Received terminating signal %d. Exiting...\n", st-1);
	exit(10 + st);
    }

    /* Handle some signals */
    signal(SIGHUP, onsig);
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

    FD_ZERO(&all_fds); FD_ZERO(&nmea_fds); FD_ZERO(&watcher_fds);
    FD_SET(msock, &all_fds);

    session = gpsd_init(dgpsserver);
    if (gpsd_speed)
	session->gNMEAdata.baudrate = gpsd_speed;
    session->gpsd_device = strdup(device_name);
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
	if (select(FD_SETSIZE, &rfds, NULL, NULL, &tv) < 0) {
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
		notify_watchers("GPSD,X=1\r\n");
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
	for (fd = 0; fd < FD_SETSIZE; fd++) {
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
			notify_watchers("GPSD,X=1\r\n");
		    }
		}

		if (FD_ISSET(fd, &rfds)) {
		    char buf[BUFSIZ];
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

			session->poll_times[fd] = timestamp();
			if (handle_request(fd, buf, buflen, 1) < 0) {
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
