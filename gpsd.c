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
#include <stdarg.h>

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

#include "gps.h"
#include "gpsd.h"
#include "version.h"

#define QLEN		5
#define GPS_TIMEOUT	5	/* Consider GPS connection loss after 5 sec */

/* the default driver is NMEA */
struct gpsd_t session;

static int gps_timeout = GPS_TIMEOUT;
static char *device_name = 0;
static char *default_device_name = "/dev/gps";
static int in_background = 0;
static fd_set all_fds;
static fd_set nmea_fds;
static fd_set watcher_fds;
static int reopen;

static void onsig(int sig)
{
    gps_wrap(&session);
    gpscli_report(1, "Received signal %d. Exiting...\n", sig);
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
    in_background = 1;
    return 0;
}

void gpscli_report(int errlevel, const char *fmt, ... )
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

    if (errlevel > session.debug)
	return;

    if (in_background)
    {
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
	    fputs("usage:  gpsd [options] \n\
  options include: \n\
  -p string          = set GPS device name \n\
  -T {e|t}           = set GPS device type \n\
  -S integer         = set port for daemon \n\
  -i %f[NS]:%f[EW]   = set initial latitude/longitude \n\
  -s baud_rate       = set baud rate on gps device \n\
  -t timeout         = set timeout in seconds on fix/mode validity \n\
  -d host[:port]     = set DGPS server \n\
  -D integer         = set debug level \n\
  -h                 = help message \n\
", stderr);
}

static void print_settings(char *service, char *dgpsserver)
{
    fprintf(stderr, "command line options:\n");
    fprintf(stderr, "  debug level:        %d\n", session.debug);
    fprintf(stderr, "  gps device name:    %s\n", device_name);
    fprintf(stderr, "  gpsd port:          %s\n", service);
    if (dgpsserver) {
      fprintf(stderr, "  dgps server:        %s\n", dgpsserver);
    }
    if (session.initpos.latitude && session.initpos.longitude) {
      fprintf(stderr, "  latitude:           %s%c\n", session.initpos.latitude, session.initpos.latd);
      fprintf(stderr, "  longitude:          %s%c\n", session.initpos.longitude, session.initpos.lond);
    }
}

static int validate(void)
{
    gpscli_report(0, "Fix (status=%d, mode=%d).\n", session.gNMEAdata.status, session.gNMEAdata.mode);
    if ((session.gNMEAdata.status == STATUS_NO_FIX) != (session.gNMEAdata.mode == MODE_NO_FIX))
    {
	 gpscli_report(0, "GPS is confused about whether it has a fix (status=%d, mode=%d).\n", session.gNMEAdata.status, session.gNMEAdata.mode);
	 return 0;
    }
    else if (session.gNMEAdata.status > STATUS_NO_FIX && session.gNMEAdata.mode > MODE_NO_FIX) {
	 gpscli_report(0, "GPS is has a fix (status=%d, mode=%d).\n", session.gNMEAdata.status, session.gNMEAdata.mode);
	return session.gNMEAdata.mode;
    }
    return 0;
}

static int handle_request(int fd, char *buf, int buflen)
{
    char reply[BUFSIZE];
    char *p;
    int sc, i;
    time_t cur_time;

    cur_time = time(NULL);

#define STALE_COMPLAINT(l, f) gpscli_report(1, l " data is stale: %ld + %d >= %ld\n", session.gNMEAdata.f.last_refresh, session.gNMEAdata.f.time_to_live, cur_time)


    sprintf(reply, "GPSD");
    p = buf;
    while (*p) {
	switch (*p++)
	{
	case 'A':
	case 'a':
	    if (!validate())
		break;
	    else if (FRESH(session.gNMEAdata.altitude_stamp,cur_time)) {
		sprintf(reply + strlen(reply),
			",A=%f",
			session.gNMEAdata.altitude);
	    }
	    else if (session.debug > 1)
		STALE_COMPLAINT("Altitude", altitude_stamp);
	    break;
	case 'D':
	case 'd':
	    if (session.gNMEAdata.utc[0])
		sprintf(reply + strlen(reply),
			",D=%s",
			session.gNMEAdata.utc);
	    break;
	case 'L':
	case 'l':
	    sprintf(reply + strlen(reply),
		    ",l=1 " VERSION " acdmpqrsvxy"
#ifdef PROCESS_PRWIZCH
		    "z"
#endif
);
	    break;
	case 'M':
	case 'm':
	    if (FRESH(session.gNMEAdata.mode_stamp, cur_time)) {
		sprintf(reply + strlen(reply),
			",M=%d",
			session.gNMEAdata.mode);
	    }
	    else if (session.debug > 1)
		STALE_COMPLAINT("Mode", mode_stamp);
	    break;
	case 'P':
	case 'p':
	    if (!validate())
		break;
	    else if (FRESH(session.gNMEAdata.latlon_stamp,cur_time)) {
		sprintf(reply + strlen(reply),
			",P=%f %f",
			session.gNMEAdata.latitude,
			session.gNMEAdata.longitude);
	    }
	    else if (session.debug > 1)
		STALE_COMPLAINT("Position", latlon_stamp);
	    break;
	case 'Q':
	case 'q':
	    sprintf(reply + strlen(reply),
		    ",Q=%d %f %f %f",
		    session.gNMEAdata.satellites_used,
		    session.gNMEAdata.pdop, session.gNMEAdata.hdop, session.gNMEAdata.vdop);
	    break;
	case 'R':
	case 'r':
	    if (*p == '1' || *p == '+') {
		FD_SET(fd, &nmea_fds);
		sprintf(reply + strlen(reply),
			",R=1");
		p++;
	    } else if (*p == '0' || *p == '-') {
		FD_CLR(fd, &nmea_fds);
		sprintf(reply + strlen(reply),
			",R=0");
		p++;
	    } else if (FD_ISSET(fd, &nmea_fds)) {
		FD_CLR(fd, &nmea_fds);
		sprintf(reply + strlen(reply),
			",R=0");
	    } else {
		FD_SET(fd, &nmea_fds);
		sprintf(reply + strlen(reply),
			",R=1");
	    }
	    break;
	case 'S':
	case 's':
	    if (FRESH(session.gNMEAdata.status_stamp, cur_time)) {
		sprintf(reply + strlen(reply),
			",S=%d",
			session.gNMEAdata.status);
	    }
	    else if (session.debug > 1)
		STALE_COMPLAINT("Status", status_stamp);
	    break;
	case 'T':
	case 't':
	    if (!validate())
		break;
	    if (FRESH(session.gNMEAdata.track_stamp, cur_time)) {
		sprintf(reply + strlen(reply),
			",T=%f",
			session.gNMEAdata.track);
	    }
	    else if (session.debug > 1)
		STALE_COMPLAINT("Track", track_stamp);
	    break;
	case 'V':
	case 'v':
	    if (!validate())
		break;
	    else if (FRESH(session.gNMEAdata.speed_stamp, cur_time)) {
		sprintf(reply + strlen(reply),
			",V=%f",
			session.gNMEAdata.speed);
	    }
	    else if (session.debug > 1)
		STALE_COMPLAINT("Speed", altitude_stamp);
	    break;
	case 'W':
	case 'w':
	    if (*p == '1' || *p == '+') {
		FD_SET(fd, &watcher_fds);
		sprintf(reply + strlen(reply),
			",W=1");
		p++;
	    } else if (*p == '0' || *p == '-') {
		FD_CLR(fd, &watcher_fds);
		sprintf(reply + strlen(reply),
			",W=0");
		p++;
	    } else if (FD_ISSET(fd, &watcher_fds)) {
		FD_CLR(fd, &watcher_fds);
		sprintf(reply + strlen(reply),
			",W=0");
	    } else {
		FD_SET(fd, &watcher_fds);
		sprintf(reply + strlen(reply),
			",W=1");
	    }
	    break;
        case 'X':
        case 'x':
	    if (session.fdin == -1)
		strcat(reply, ",X=0");
	    else
		strcat(reply, ",X=1");
	    break;
	case 'Y':
	case 'y':
	    sc = 0;
	    if (SEEN(session.gNMEAdata.satellite_stamp))
		for (i = 0; i < MAXCHANNELS; i++)
		    if (session.gNMEAdata.PRN[i])
			sc++;
	    sprintf(reply + strlen(reply),
		    ",Y=%d:", sc);
	    if (SEEN(session.gNMEAdata.satellite_stamp))
		for (i = 0; i < MAXCHANNELS; i++)
		    if (session.gNMEAdata.PRN[i])
			sprintf(reply + strlen(reply),"%d %d %d %d:", 
				session.gNMEAdata.PRN[i], 
				session.gNMEAdata.elevation[i],
				session.gNMEAdata.azimuth[i],
			        session.gNMEAdata.ss[i]);
	    break;
#ifdef PROCESS_PRWIZCH
	case 'Z':
	case 'z':
	    sc = 0;
	    if (SEEN(session.gNMEAdata.signal_quality_stamp))
	    {
		for (i = 0; i < MAXCHANNELS; i++)
		    if (session.gNMEAdata.Zs[i])
			sc++;
	    }
	    sprintf(reply + strlen(reply),
		    ",Z=%d ", sc);
	    for (i = 0; i < MAXCHANNELS; i++)
	    	if (SEEN(session.gNMEAdata.signal_quality_stamp))
		{
		    if (session.gNMEAdata.Zs[i])
			sprintf(reply + strlen(reply),"%d %02d ", 
				session.gNMEAdata.Zs[i], 
				session.gNMEAdata.Zv[i] * (int)(99.0 / 7.0));
		}
#endif /* PROCESS_PRWIZCH */
	    break;
	case '\r':
	case '\n':
	    goto breakout;
	}
    }
 breakout:
    strcat(reply, "\r\n");

    if (session.debug >= 2)
	gpscli_report(1, "=> client: %s", reply);
    if (buflen && write(fd, reply, strlen(reply) + 1) < 0)
	return 0;
    else
	return 1;
}

static void raw_hook(char *sentence)
/* hook to be executed on each incoming sentence */
{
    int fd;

    for (fd = 0; fd < getdtablesize(); fd++) {
	/* copy raw NMEA sentences from GPS */
	if (FD_ISSET(fd, &nmea_fds)) {
	    gpscli_report(1, "=> client: %s\n", sentence);
	    if (write(fd, sentence, strlen(sentence)) < 0) {
		gpscli_report(3, "Raw write %s\n", strerror(errno));
		FD_CLR(fd, &all_fds);
		FD_CLR(fd, &nmea_fds);
	    }
	}
	if (FD_ISSET(fd, &watcher_fds)) {
	    /* some listeners may be in push mode */
	    int ok = 1;

	    ++sentence;
#define PUBLISH(fd, cmds)	handle_request(fd, cmds, sizeof(cmds)-1)
	    if (strncmp(GPRMC, sentence, 5) == 0) {
		ok = PUBLISH(fd, "xptvds");
	    } else if (strncmp(GPGGA, sentence, 5) == 0) {
		ok = PUBLISH(fd, "xsa");	
	    } else if (strncmp(GPGLL, sentence, 5) == 0) {
		ok = PUBLISH(fd, "xp");
	    } else if (strncmp(PMGNST, sentence, 5) == 0) {
		ok = PUBLISH(fd, "xsm");
	    } else if (strncmp(GPVTG, sentence, 5) == 0) {
		ok = PUBLISH(fd, "xtv");
	    } else if (strncmp(GPGSA, sentence, 5) == 0) {
		ok = PUBLISH(fd, "xqm");
	    } else if (strncmp(GPGSV, sentence, 5) == 0) {
		if (nmea_sane_satellites(&session.gNMEAdata))
		    ok = PUBLISH(fd, "xy");
#ifdef PROCESS_PRWIZCH
	    } else if (strncmp(PRWIZCH, sentence, 7) == 0) {
		ok = PUBLISH(fd, "xz");
#endif /* PROCESS_PRWIZCH */
	    }
#undef PUBLISH
	    if (!ok) {
		gpscli_report(1, "Watcher write %s", strerror(errno));
		FD_CLR(fd, &all_fds);
		FD_CLR(fd, &watcher_fds);
	    }
	}
    }
}

int main(int argc, char *argv[])
{
    char *default_service = "gpsd";
    char *service = NULL;
    char *dgpsserver = NULL;
    struct sockaddr_in fsin;
    fd_set rfds;
    int msock, nfds;
    int alen;
    extern char *optarg;
    int option, gps_speed = 0;
    char gpstype = 'n', *colon;
    int fd;
    int need_gps;
    int nowait = 0;

    session.debug = 1;
    while ((option = getopt(argc, argv, "D:S:T:d:hi:np:s:t:")) != -1) {
	switch (option) {
	case 'T':
	    gpstype = *optarg;
	    break;
	case 'D':
	    session.debug = (int) strtol(optarg, 0, 0);
	    break;
	case 'S':
	    service = optarg;
	    break;
	case 'd':
	    dgpsserver = optarg;
	    break;
	case 'i':
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
		session.initpos.latitude = optarg;
 		session.initpos.latd = toupper(optarg[strlen(session.initpos.latitude) - 1]);
		session.initpos.latitude[strlen(session.initpos.latitude) - 1] = '\0';
		session.initpos.longitude = colon+1;
		session.initpos.lond = toupper(session.initpos.longitude[strlen(session.initpos.longitude)-1]);
		session.initpos.longitude[strlen(session.initpos.longitude)-1] = '\0';
	    }
	    break;
	case 'n':
	    nowait = 1;
	    break;
	case 'p':
	    device_name = optarg;
	    break;
	case 't':
	    gps_timeout = strtol(optarg, NULL, 0);
	    break;
	case 's':
	    gps_speed = atoi(optarg);
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

    if (session.debug > 1) 
	print_settings(service, dgpsserver);
    
    if (session.debug < 2)
	daemonize();

    /* Handle some signals */
    signal(SIGUSR1, sigusr1);
    signal(SIGINT, onsig);
    signal(SIGHUP, onsig);
    signal(SIGTERM, onsig);
    signal(SIGQUIT, onsig);
    signal(SIGPIPE, SIG_IGN);

    openlog("gpsd", LOG_PID, LOG_USER);
    gpscli_report(1, "gpsd started (Version %s)\n", VERSION);
    msock = netlib_passiveTCP(service, QLEN);
    if (msock == -1)
	exit(2);	/* netlib_passiveTCP will have issued a message */
    gpscli_report(1, "gpsd listening on port %s\n", service);

    FD_ZERO(&all_fds);
    FD_ZERO(&nmea_fds);
    FD_ZERO(&watcher_fds);
    FD_SET(msock, &all_fds);
    nfds = getdtablesize();

    gps_init(&session, gps_timeout, gpstype, dgpsserver);
    if (gps_speed)
	session.baudrate = gps_speed;
    session.gps_device = device_name;
    session.gNMEAdata.raw_hook = raw_hook;
    if (session.dsock >= 0)
	FD_SET(session.dsock, &all_fds);

    if (nowait && (gps_activate(&session) < 0)) {
	gpscli_report(0, "exiting - GPS device nonexistent or can't be read\n");
	exit(2);
    }

    while (1) {
	struct timeval tv;

        memcpy((char *)&rfds, (char *)&all_fds, sizeof(rfds));

	/* poll for input, waiting at most a second */
	tv.tv_sec = 1;
	tv.tv_usec = 0;
	if (select(nfds, &rfds, NULL, NULL, &tv) < 0) {
	    if (errno == EINTR)
		continue;
	    gpscli_report(0, "select: %s\n", strerror(errno));
	    exit(2);
	}

	/* always be open to new connections */
	if (FD_ISSET(msock, &rfds)) {
	    int ssock;

	    alen = sizeof(fsin);
	    ssock = accept(msock, (struct sockaddr *) &fsin, &alen);

	    if (ssock < 0)
		gpscli_report(0, "accept: %s\n", strerror(errno));

	    else 
		FD_SET(ssock, &all_fds);
	    FD_CLR(msock, &rfds);
	}

	/* we may need to force the GPS open */
	if ((nowait || reopen) && session.fdin == -1) {
	    gps_deactivate(&session);
	    if (gps_activate(&session) >= 0)
		FD_SET(session.fdin, &all_fds);
	}

	/* get data from it */
	if (session.fdin >= 0 && gps_poll(&session) <= 0) {
	    gpscli_report(3, "GPS is offline\n");
	    FD_CLR(session.fdin, &all_fds);
	    gps_deactivate(&session);
	    if (nowait)
		reopen = 1;
	}

	/* this simplifies a later test */
	if (session.dsock > -1)
	    FD_CLR(session.dsock, &rfds);

	/* accept and execute commands for all clients */
	need_gps = 0;
	for (fd = 0; fd < getdtablesize(); fd++) {
	    if (fd != msock && fd != session.fdin && FD_ISSET(fd, &rfds)) {
		char buf[BUFSIZE];
		int buflen;

		if (session.fdin == -1) {
		    gps_deactivate(&session);
		    if (gps_activate(&session) >= 0)
			FD_SET(session.fdin, &all_fds);
		}
		buflen = read(fd, buf, sizeof(buf) - 1);
		if (buflen <= 0) {
		    (void) close(fd);
		    FD_CLR(fd, &all_fds);
		}
		buf[buflen] = '\0';
		if (session.debug >= 2)
		    gpscli_report(1, "<= client: %s", buf);
		if (handle_request(fd, buf, buflen) <= 0) {
		    (void) close(fd);
		    FD_CLR(fd, &all_fds);
		}
	    }
	    if (fd != session.fdin && fd != msock && FD_ISSET(fd, &all_fds)) {
		need_gps++;
	    }
	}

	if (!nowait && !need_gps && session.fdin != -1) {
	    FD_CLR(session.fdin, &all_fds);
	    session.fdin = -1;
	    gps_deactivate(&session);
	}
    }

    gps_wrap(&session);
}



