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

#include "outdata.h"
#include "nmea.h"
#include "gpsd.h"
#include "version.h"

#define QLEN		5
#define BUFSIZE		4096
#define GPS_TIMEOUT	5	/* Consider GPS connection loss after 5 sec */

/* the default driver is NMEA */
struct session_t session = {&nmea};

static int gps_timeout = GPS_TIMEOUT;
static int device_speed = B4800;
static char *device_name = 0;
static char *default_device_name = "/dev/gps";
static int nfds, dsock;
static int reopen = 0;
static int in_background = 0;

static int handle_request(int fd, fd_set * fds);

static void onsig(int sig)
{
    serial_close();
    close(dsock);
    report(1, "Received signal %d. Exiting...\n", sig);
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

void report(int errlevel, const char *fmt, ... )
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

static void send_dgps()
{
  char buf[BUFSIZE];

  sprintf(buf, "R %0.8f %0.8f %0.2f\r\n", session.gNMEAdata.latitude,
	  session.gNMEAdata.longitude, session.gNMEAdata.altitude);
  write(dsock, buf, strlen(buf));
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

static struct gps_type_t *set_device_type(char what)
/* select a device driver by key letter */
{
    struct gps_type_t **dp, *drivers[] = {&nmea, 
					  &tripmate,
					  &earthmate_a, 
					  &earthmate_b,
    					  &logfile};
    for (dp = drivers; dp < drivers + sizeof(drivers)/sizeof(drivers[0]); dp++)
	if ((*dp)->typekey == what) {
	    fprintf(stderr, "Selecting %s driver...\n", (*dp)->typename);
	    goto foundit;
	}
    fprintf(stderr, "Invalid device type \"%s\"\n"
	    "Using GENERIC instead\n", optarg);
 foundit:;
    return *dp;
}

static void print_settings(char *service, char *dgpsserver, char *dgpsport)
{
    fprintf(stderr, "command line options:\n");
    fprintf(stderr, "  debug level:        %d\n", session.debug);
    fprintf(stderr, "  gps device name:    %s\n", device_name);
    fprintf(stderr, "  gps device speed:   %d\n", device_speed);
    fprintf(stderr, "  gpsd port:          %s\n", service);
    if (dgpsserver) {
      fprintf(stderr, "  dgps server:        %s\n", dgpsserver);
      fprintf(stderr, "  dgps port:          %s\n", dgpsport);
    }
    if (session.initpos.latitude && session.initpos.longitude) {
      fprintf(stderr, "  latitude:           %s%c\n", session.initpos.latitude, session.initpos.latd);
      fprintf(stderr, "  longitude:          %s%c\n", session.initpos.longitude, session.initpos.lond);
    }
}

static int handle_dgps()
{
    char buf[BUFSIZE];
    int rtcmbytes;

    if ((rtcmbytes=read(dsock,buf,BUFSIZE))>0 && (session.fdout!=-1))
    {
	if (session.device_type->rctm_writer(buf, rtcmbytes) <= 0)
	    report(1, "Write to rtcm sink failed\n");
    }
    else 
    {
	report(1, "Read from rtcm source failed\n");
    }

    return rtcmbytes;
}

static void deactivate()
{
    session.fdin = -1;
    session.fdout = -1;
    serial_close();
    if (session.device_type->wrapup)
	session.device_type->wrapup();
    report(1, "closed GPS\n");
    session.gNMEAdata.mode = 1;
    session.gNMEAdata.status = 0;
}

static int activate()
{
    int input;

    if ((input = serial_open(device_name, device_speed ? device_speed : session.device_type->baudrate)) < 0)
	errexit("Exiting - serial open\n");
 
    report(1, "opened GPS\n");
    session.fdin = input;
    session.fdout = input;

    return input;
}

int main(int argc, char *argv[])
{
    char *default_service = "gpsd";
    char *service = NULL;
    char *dgpsport = "rtcm-sc104";
    char *dgpsserver = NULL;
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
    char buf[BUFSIZE], *colon;
    int sentdgps = 0, fixcnt = 0;
    time_t now;

    session.debug = 1;
    while ((option = getopt(argc, argv, "D:S:T:hi:p:s:d:t:")) != -1) {
	switch (option) {
	case 'T':
	    session.device_type = set_device_type(*optarg);
	    break;
	case 'D':
	    session.debug = (int) strtol(optarg, 0, 0);
	    break;
	case 'S':
	    service = optarg;
	    break;
	case 'd':
	    dgpsserver = optarg;
	    if ((colon = strchr(optarg, ':'))) {
		dgpsport = colon+1;
		*colon = '\0';
	    }
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
	case 'p':
	    device_name = optarg;
	    break;
	case 's':
	    device_speed = strtol(optarg, NULL, 0);
	    break;
	case 't':
	    gps_timeout = strtol(optarg, NULL, 0);
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
	print_settings(service, dgpsserver, dgpsport);
    
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
    report(1, "gpsd started (Version %s)\n", VERSION);
    msock = passiveTCP(service, QLEN);
    report(1, "gpsd listening on port %s\n", service);

    nfds = getdtablesize();

    FD_ZERO(&afds);
    FD_ZERO(&nmea_fds);
    FD_SET(msock, &afds);

    if (dgpsserver) {
	char hn[256];

	if (!getservbyname(dgpsport, "tcp"))
	    dgpsport = "2101";

	dsock = connectsock(dgpsserver, dgpsport, "tcp");
	if (dsock < 0) 
	    errexit("Can't connect to dgps server");

	gethostname(hn, sizeof(hn));

	sprintf(buf, "HELO %s gpsd %s\r\nR\r\n", hn, VERSION);
	write(dsock, buf, strlen(buf));
	FD_SET(dsock, &afds);
    }

    /* mark fds closed */
    input = -1;
    session.fdin = input;
    session.fdout = input;

    now = time(NULL);
    INIT(session.gNMEAdata.latlon_stamp, now, gps_timeout);
    INIT(session.gNMEAdata.altitude_stamp, now, gps_timeout);
    INIT(session.gNMEAdata.speed_stamp, now, gps_timeout);
    INIT(session.gNMEAdata.status_stamp, now, gps_timeout);
    INIT(session.gNMEAdata.mode_stamp, now, gps_timeout);
    session.gNMEAdata.mode = MODE_NO_FIX;

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

	if (reopen && input == -1) {
	    FD_CLR(input, &afds);
	    deactivate();
	    input = activate();
	    FD_SET(input, &afds);
	}

	if (FD_ISSET(dsock, &rfds))
	    handle_dgps();

	if (FD_ISSET(msock, &rfds)) {
	    int ssock;

	    alen = sizeof(fsin);
	    ssock = accept(msock, (struct sockaddr *) &fsin, &alen);

	    if (ssock < 0)
		errlog("accept");

	    else FD_SET(ssock, &afds);
	}

	if (input >= 0 && FD_ISSET(input, &rfds)) {
	    session.device_type->handle_input(input, &afds, &nmea_fds);
	}

	if (session.gNMEAdata.status > 0) 
	    fixcnt++;
	
	if (fixcnt > 10) {
	    if (!sentdgps) {
		sentdgps++;
		if (dgpsserver)
		    send_dgps();
	    }
	}

	for (fd = 0; fd < nfds; fd++) {
	    if (fd != msock && fd != input && fd != dsock && 
		FD_ISSET(fd, &rfds)) {
		if (input == -1) {
		    input = activate();
		    FD_SET(input, &afds);
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

static int validate(void)
{
    if ((session.gNMEAdata.status == STATUS_NO_FIX) != (session.gNMEAdata.mode == MODE_NO_FIX))
    {
	 report(0, "GPS is confused about whether it has a fix (status=%d, mode=%d).\n", session.gNMEAdata.status, session.gNMEAdata.mode);
	 return 0;
    }
    return 1;
}

static int handle_request(int fd, fd_set * fds)
{
    char buf[BUFSIZE];
    char reply[BUFSIZE];
    char *p;
    int cc, sc, i;
    time_t cur_time;

    cc = read(fd, buf, sizeof(buf) - 1);
    if (cc < 0)
	return 0;

    buf[cc] = '\0';

    cur_time = time(NULL);

#define STALE_COMPLAINT(l, f) report(1, l " data is stale: %ld + %d >= %ld\n", session.gNMEAdata.f.last_refresh, session.gNMEAdata.f.time_to_live, cur_time)

    sprintf(reply, "GPSD");
    p = buf;
    while (*p) {
	switch (*p++)
	{
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
	case 'D':
	case 'd':
	    sprintf(reply + strlen(reply),
		    ",D=%s",
		    session.gNMEAdata.utc);
	    break;
        case 'X':
        case 'x':
            if (!FD_ISSET(fd, fds))
                FD_SET(fd, fds);
             sprintf(reply + strlen(reply),
                         " ,R=1");
            break;
        case 'C':
        case 'c':
            if (FD_ISSET(fd, fds))
                FD_CLR(fd, fds);
            sprintf(reply + strlen(reply),
                         " ,R=0");
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
	case 'L':
	case 'l':
	    sprintf(reply + strlen(reply),
		    ",l=1");
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
	case 'M':
	case 'm':
	    if (FRESH(session.gNMEAdata.mode_stamp, cur_time)) {
		sprintf(reply + strlen(reply),
			",M=%d",
			session.gNMEAdata.mode);
	    }
	    else if (session.debug > 1)
		STALE_COMPLAINT("Mode", status_stamp);
	    break;
	case 'Q':
	case 'q':
	    sprintf(reply + strlen(reply),
		    ",Q=%d %d %f %f %f",
		    session.gNMEAdata.in_view, session.gNMEAdata.satellites,
		    session.gNMEAdata.pdop, session.gNMEAdata.hdop, session.gNMEAdata.vdop);
	    break;
	case 'Y':
	case 'y':
	    sc = 0;
	    if (session.gNMEAdata.cmask & C_SAT)
		for (i = 0; i < MAXSATS; i++)
		    if (session.gNMEAdata.PRN[i])
			sc++;
	    sprintf(reply + strlen(reply),
		    ",Y=%d ", sc);
	    for (i = 0; i < MAXSATS; i++)
		if (session.gNMEAdata.cmask & C_SAT)
		    if (session.gNMEAdata.PRN[i])
			sprintf(reply + strlen(reply),"%d %2d %2d ", 
				session.gNMEAdata.PRN[i], 
				session.gNMEAdata.elevation[i],
				session.gNMEAdata.azimuth[i]);
	    break;
	case 'Z':
	case 'z':
	    sc = 0;
	    if (session.gNMEAdata.cmask & C_SAT)
	    {
		for (i = 0; i < MAXSATS; i++)
		    if (session.gNMEAdata.PRN[i])
			sc++;
	    }
	    else if (session.gNMEAdata.cmask & C_ZCH)
	    {
		for (i = 0; i < MAXSATS; i++)
		    if (session.gNMEAdata.Zs[i])
			sc++;
	    }
	    sprintf(reply + strlen(reply),
		    ",Z=%d ", sc);
	    for (i = 0; i < MAXSATS; i++)
		if (session.gNMEAdata.cmask & C_SAT)
		{
		    if (session.gNMEAdata.PRN[i])
			sprintf(reply + strlen(reply),"%d %02d ", 
				session.gNMEAdata.PRN[i], 
				session.gNMEAdata.ss[i]);
		}
	    	else
		{
		    if (session.gNMEAdata.Zs[i])
			sprintf(reply + strlen(reply),"%d %02d ", 
				session.gNMEAdata.Zs[i], 
				session.gNMEAdata.Zv[i] * (int)(99.0 / 7.0));
		}
	    break;
	case '\r':
	case '\n':
	    goto breakout;
	}
    }
 breakout:
    strcat(reply, "\r\n");

    if (cc && write(fd, reply, strlen(reply) + 1) < 0)
	return 0;

    return cc;
}

void send_nmea(fd_set *afds, fd_set *nmea_fds, char *buf)
/* write to whatever client might be listening */
{
    int fd;

    for (fd = 0; fd < nfds; fd++) {
	if (FD_ISSET(fd, nmea_fds)) {
	    report(1, "--> %s", buf);
	    if (write(fd, buf, strlen(buf)) < 0) {
		report(1, "Raw write %s", strerror(errno));
		FD_CLR(fd, afds);
		FD_CLR(fd, nmea_fds);
	    }
	}
    }
}

void errlog(char *s)
{
    report(0, "%s: %s\n", s, strerror(errno));
}

void  errexit(char *s)
{
    report(0, "%s: %s\n", s, strerror(errno));
    serial_close();
    close(dsock);
    exit(2);
}
