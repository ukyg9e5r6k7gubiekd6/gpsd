#include "config.h"
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <syslog.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <netdb.h>

#if defined (HAVE_PATH_H)
#include <paths.h>
#else
#if !defined (_PATH_DEVNULL)
#define _PATH_DEVNULL    "/dev/null"
#endif
#endif

#if defined (HAVE_STRINGS_H)
#include <strings.h>
#endif


#if defined (HAVE_SYS_TERMIOS_H)
#include <sys/termios.h>
#else
#if defined (HAVE_TERMIOS_H)
#include <termios.h>
#endif
#endif

#if defined (HAVE_SYS_SELECT_H)
#include <sys/select.h>
#endif

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#if defined(HAVE_SYS_TIME_H)
#include <sys/time.h>
#endif

#include "outdata.h"
#include "nmea.h"
#include "gpsd.h"
#include "version.h"

#define QLEN		5
#define BUFSIZE		4096


struct session_t session;

char *default_device_name = "/tmp/gpslog";

static int nfds, dsock;
static char *device_name;
static int ttyfd = -1;
static FILE *fp = 0;
static int reopen = 0;

static int handle_input(int input, fd_set * afds, fd_set * nmea_fds);
static int handle_request(int fd, fd_set * fds);


int gpslog_open()
{
    ttyfd = open(device_name, O_RDONLY | O_NONBLOCK);
    if (ttyfd < 0)
	return (-1);

    fp = fdopen(ttyfd, "r");
    return ttyfd;
}

void gpslog_close()
{
  if (ttyfd != -1) {
    close(ttyfd);
    ttyfd = -1;
  }
  if (fp) {
    fclose(fp);
    fp = 0;
  }
}

static void onsig(int sig)
{
    gpslog_close();
    close(dsock);
    syslog(LOG_NOTICE, "Received signal %d. Exiting...", sig);
    exit(10 + sig);
}

static void sigusr1(int sig)
{
  reopen = 1;
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
    return 0;
}

static void usage()
{
	    fputs("usage:  gpsd [options] \n\
  options include: \n\
  -p string    = set gps log file path to replay \n\
  -S integer   = set port for daemon \n\
  -D integer   = set debug level \n\
  -h           = help message \n\
", stderr);
}

static void print_settings(char *service)
{
    fprintf(stderr, "command line options:\n");
    fprintf(stderr, "  debug level:        %d\n", session.debug);
    fprintf(stderr, "  gps device name:    %s\n", device_name);
    fprintf(stderr, "  gpsd port:          %s\n", service);
}

static void deactivate()
{
    session.fdin = -1;
    session.fdout = -1;
    gpslog_close();
    syslog(LOG_NOTICE, "Closed gps");
    session.gNMEAdata.mode = 1;
    session.gNMEAdata.status = 0;
}

static int activate()
{
    int input;

    if ((input = gpslog_open()) < 0)
	errexit("gpslog open: ");
    syslog(LOG_NOTICE, "Opened gps");
    session.fdin = input;
    session.fdout = -1;

    return input;
}

int main(int argc, char *argv[])
{
    char *default_service = "gpsd";
    char *service = 0;
    struct sockaddr_in fsin;
    int msock;
    fd_set rfds;
    fd_set afds;
    fd_set nmea_fds;
    int alen;
    int fd, input;
    int need_gps;
    extern char *optarg;
    int option;
    int fixcnt = 0;

    while ((option = getopt(argc, argv, "D:S:hp:")) != -1) {
	switch (option) {
	case 'D':
	    session.debug = (int) strtol(optarg, 0, 0);
	    break;
	case 'S':
	    service = optarg;
	    break;
	case 'p':
	    device_name = optarg;
	    break;
	case 'h':
	case '?':
	default:
	    usage();
	    exit(0);
	}
    }

    if (!device_name) device_name = default_device_name;

    if (!service) {
	if (!getservbyname(default_service, "tcp"))
	    service = "2947";
	else service = default_service;
    }

    if (session.debug > 0) 
      print_settings(service);
    
    if (session.debug < 2)
	daemonize();

    /* Handle some signals */
    signal(SIGUSR1, sigusr1);
    signal(SIGINT, onsig);
    signal(SIGHUP, onsig);
    signal(SIGTERM, onsig);
    signal(SIGQUIT, onsig);
    signal(SIGPIPE, SIG_IGN);

    openlog("gpsplay", LOG_PID, LOG_USER);
    syslog(LOG_NOTICE, "Gpsplay started (Version %s)", VERSION);
    syslog(LOG_NOTICE, "Gpsplay listening on port %s", service);

    msock = passiveTCP(service, QLEN);

    nfds = getdtablesize();

    FD_ZERO(&afds);
    FD_ZERO(&nmea_fds);
    FD_SET(msock, &afds);

    /* mark fds closed */
    input = -1;
    session.fdin = input;
    session.fdout = -1;

    while (1) {
	struct timeval tv;

	tv.tv_sec = 1;
	tv.tv_usec = 0;

        memcpy((char *)&rfds, (char *)&afds, sizeof(rfds));

	if (select(nfds, &rfds, NULL, NULL, &tv) < 0) {
	    if (errno == EINTR)
		continue;
	    errexit("select");
	}

	need_gps = 0;

	if (reopen && input != -1) {
	    deactivate();
	    input = activate();
	}

	if (FD_ISSET(msock, &rfds)) {
	    int ssock;

	    alen = sizeof(fsin);
	    ssock = accept(msock, (struct sockaddr *) &fsin, &alen);

	    if (ssock < 0)
		errlog("accept");

	    else FD_SET(ssock, &afds);
	}

	if (input >= 0) {
	    handle_input(input, &afds, &nmea_fds);
	}

	if (session.gNMEAdata.status > 0) 
	    fixcnt++;
	
	for (fd = 0; fd < nfds; fd++) {
	    if (fd != msock && fd != input && fd != dsock && 
		FD_ISSET(fd, &rfds)) {
		if (input == -1) {
		    input = activate();
		}
		if (handle_request(fd, &nmea_fds) == 0) {
		    (void) close(fd);
		    FD_CLR(fd, &afds);
		    FD_CLR(fd, &nmea_fds);
		}
	    }
	    if (fd != msock && fd != input && FD_ISSET(fd, &afds)) {
		need_gps++;
	    }
	}

	if (!need_gps && input != -1) {
	    FD_CLR(input, &afds);
	    input = -1;
	    deactivate();
	}
    }
}

static int handle_request(int fd, fd_set * fds)
{
    char buf[BUFSIZE];
    char reply[BUFSIZE];
    char *p;
    int cc;

    cc = read(fd, buf, sizeof(buf) - 1);
    if (cc < 0)
        return 0;

    buf[cc] = '\0';

    sprintf(reply, "GPSD");
    p = buf;
    while (*p) {
	switch (*p) {
	case 'P':
	case 'p':
	    sprintf(reply + strlen(reply),
		    ",P=%f %f",
		    session.gNMEAdata.latitude,
		    session.gNMEAdata.longitude);
	    break;
	case 'D':
	case 'd':
	    sprintf(reply + strlen(reply),
		    ",D=%s",
		    session.gNMEAdata.utc);
	    break;
	case 'A':
	case 'a':
	    sprintf(reply + strlen(reply),
		    ",A=%f",
		    session.gNMEAdata.altitude);
	    break;
	case 'V':
	case 'v':
	    sprintf(reply + strlen(reply),
		    ",V=%f",
		    session.gNMEAdata.speed);
	    break;
	case 'R':
	case 'r':
	    if (FD_ISSET(fd, fds)) {
		FD_CLR(fd, fds);
		sprintf(reply + strlen(reply),
			",R=0");
	    } else {
		FD_SET(fd, fds);
		sprintf(reply + strlen(reply),
			",R=1");
	    }
	    break;
	case 'S':
	case 's':
	    sprintf(reply + strlen(reply),
		    ",S=%d",
		    session.gNMEAdata.status);
	    break;
	case 'M':
	case 'm':
	    sprintf(reply + strlen(reply),
		    ",M=%d",
		    session.gNMEAdata.mode);
	    break;
	case '\r':
	case '\n':
	    *p = '\0';		/* ignore the rest */
	    break;

	}
	p++;
    }
    strcat(reply, "\r\n");

    if (cc && write(fd, reply, strlen(reply) + 1) < 0)
	return 0;

    return cc;
}

void send_nmea(fd_set *afds, fd_set *nmea_fds, char *buf)
{
    int fd;

    for (fd = 0; fd < nfds; fd++) {
	if (FD_ISSET(fd, nmea_fds)) {
	    if (write(fd, buf, strlen(buf)) < 0) {
		syslog(LOG_NOTICE, "Raw write: %s", strerror(errno));
		FD_CLR(fd, afds);
		FD_CLR(fd, nmea_fds);
	    }
	}
    }
}

static int handle_input(int input, fd_set *afds, fd_set *nmea_fds)
{
    static unsigned char buf[BUFSIZE];	/* that is more then a sentence */
    char *p;

    if (fgets(buf, BUFSIZE-1, fp) == NULL) {
	fseek(fp, 0L, SEEK_SET);
	fgets(buf, BUFSIZE-1, fp);
    }
    p = strrchr(buf, '\r');
    if (p) *p = '\0';
    p = strrchr(buf, '\n');
    if (p) *p = '\0';
	
    if (strlen(buf)) {
	nmea_handle_message(buf);
	strcat(buf, "\r\n");
	send_nmea(afds, nmea_fds, buf);
    }

    return 1;
}

void errlog(char *s)
{
    syslog(LOG_ERR, "%s: %s\n", s, strerror(errno));
}

void  errexit(char *s)
{
    syslog(LOG_ERR, "%s: %s\n", s, strerror(errno));
    gpslog_close();
    close(dsock);
    exit(2);
}
