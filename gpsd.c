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

#include "gpsd.h"
#include "version.h"

// Temporary forward declarations
void gps_init(char *dgpsserver, char *dgpsport);
void gps_activate(void), gps_deactivate(void);
void gps_poll(void);
void gps_force_repoll(void);

#define QLEN		5
#define BUFSIZE		4096
#define GPS_TIMEOUT	5	/* Consider GPS connection loss after 5 sec */

/* the default driver is NMEA */
struct session_t session = {&nmea};

static int gps_timeout = GPS_TIMEOUT;
static char *device_name = 0;
static char *default_device_name = "/dev/gps";
static int in_background = 0;
static fd_set afds;
static fd_set nmea_fds;

static void onsig(int sig)
{
    gps_close();
    gpscli_report(1, "Received signal %d. Exiting...\n", sig);
    exit(10 + sig);
}

static void sigusr1(int sig)
{
    gps_force_repoll();
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

static int validate(void)
{
    if ((session.gNMEAdata.status == STATUS_NO_FIX) != (session.gNMEAdata.mode == MODE_NO_FIX))
    {
	 gpscli_report(0, "GPS is confused about whether it has a fix (status=%d, mode=%d).\n", session.gNMEAdata.status, session.gNMEAdata.mode);
	 return 0;
    }
    return 1;
}

static int handle_request(int fd)
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

    if (session.debug >= 2)
	gpscli_report(1, "<= client: %s", buf);
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
#if 0	/* we're trying to discourage raw mode */
        case 'C':
        case 'c':
            if (FD_ISSET(fd, fds))
                FD_CLR(fd, fds);
            sprintf(reply + strlen(reply),
                         " ,R=0");
            break;
#endif
	case 'D':
	case 'd':
	    sprintf(reply + strlen(reply),
		    ",D=%s",
		    session.gNMEAdata.utc);
	    break;
	case 'L':
	case 'l':
	    sprintf(reply + strlen(reply),
		    ",l=1," VERSION ",acdmpqrsvxyz");
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
		    ",Q=%d %d %f %f %f",
		    session.gNMEAdata.in_view, session.gNMEAdata.satellites,
		    session.gNMEAdata.pdop, session.gNMEAdata.hdop, session.gNMEAdata.vdop);
	    break;
	case 'R':
	case 'r':
	    if (FD_ISSET(fd, &nmea_fds)) {
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
#if 0	/* we're trying to discourage raw mode */
        case 'X':
        case 'x':
            if (!FD_ISSET(fd, fds))
                FD_SET(fd, fds);
             sprintf(reply + strlen(reply),
                         " ,R=1");
            break;
#endif
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

    if (session.debug >= 2)
	gpscli_report(1, "=> client: %s", reply);
    if (cc && write(fd, reply, strlen(reply) + 1) < 0)
	return 0;

    return cc;
}

void gps_send_NMEA(fd_set *afds, fd_set *nmea_fds, char *buf)
/* copy raw NMEA sentences from GPS */
{
    int fd;

    for (fd = 0; fd < getdtablesize(); fd++) {
	if (FD_ISSET(fd, nmea_fds)) {
	    gpscli_report(1, "=> client: %s", buf);
	    if (write(fd, buf, strlen(buf)) < 0) {
		gpscli_report(1, "Raw write %s", strerror(errno));
		FD_CLR(fd, afds);
		FD_CLR(fd, nmea_fds);
	    }
	}
    }
}

static void raw_hook(char *buf)
{
    gps_send_NMEA(&afds, &nmea_fds, buf);
}

int main(int argc, char *argv[])
{
    char *default_service = "gpsd";
    char *service = NULL;
    char *dgpsport = "rtcm-sc104";
    char *dgpsserver = NULL;
    struct sockaddr_in fsin;
    fd_set rfds;
    int msock, nfds;
    int alen;
    extern char *optarg;
    int option;
    char *colon;
    int fd;
    int need_gps;

    session.debug = 1;
    while ((option = getopt(argc, argv, "D:S:T:hi:p:d:t:")) != -1) {
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

    if (session.debug > 1) 
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
    gpscli_report(1, "gpsd started (Version %s)\n", VERSION);
    msock = netlib_passiveTCP(service, QLEN);
    gpscli_report(1, "gpsd listening on port %s\n", service);

    FD_ZERO(&afds);
    FD_ZERO(&nmea_fds);
    FD_SET(msock, &afds);
    nfds = getdtablesize();

    gps_init(dgpsserver, dgpsport);
    if (session.dsock >= 0)
	FD_SET(session.dsock, &afds);

    while (1) {
	struct timeval tv;

        memcpy((char *)&rfds, (char *)&afds, sizeof(rfds));

	/* poll for input, waiting at most a second */
	tv.tv_sec = 1;
	tv.tv_usec = 0;
	if (select(nfds, &rfds, NULL, NULL, &tv) < 0) {
	    if (errno == EINTR)
		continue;
	    gpscli_errexit("select");
	}

	if (FD_ISSET(msock, &rfds)) {
	    int ssock;

	    alen = sizeof(fsin);
	    ssock = accept(msock, (struct sockaddr *) &fsin, &alen);

	    if (ssock < 0)
		gpscli_report(0, "accept: %s\n", strerror(errno));

	    else 
		FD_SET(ssock, &afds);
	    FD_CLR(msock, &rfds);
	}

	/* open or reopen the GPS if it's needed */
	if (session.reopen && session.fdin == -1) {
	    FD_CLR(session.fdin, &afds);
	    gps_deactivate();
	    gps_activate();
	    FD_SET(session.fdin, &afds);
	}

	gps_poll();

	if (session.dsock > -1)
	    FD_CLR(session.dsock, &rfds);
	if (session.fdin > -1)
	    FD_CLR(session.fdin, &rfds);

	/* accept and execute commands for all clients */
	need_gps = 0;
	for (fd = 0; fd < getdtablesize(); fd++) {
	    if (FD_ISSET(fd, &rfds)) {
		if (session.fdin == -1) {
		    gps_activate();
		    FD_SET(session.fdin, &afds);
		}
		if (handle_request(fd) == 0) {
		    (void) close(fd);
		    FD_CLR(fd, &afds);
		}
	    }
	    if (fd != session.fdin && FD_ISSET(fd, &afds)) {
		need_gps++;
	    }
	}

	if (!need_gps && session.fdin != -1) {
	    FD_CLR(session.fdin, &afds);
	    session.fdin = -1;
	    gps_deactivate();
	}
    }
}

void gpscli_errexit(char *s)
{
    gpscli_report(0, "%s: %s\n", s, strerror(errno));
    gps_close();
    exit(2);
}

/* LIBRARY STUFF STARTS HERE */ 

static void onexit(void)
{
    close(session.dsock);
}

void gps_init(char *dgpsserver, char *dgpsport)
/* initialize GPS polling */
{
    time_t now = time(NULL);

    session.dsock = -1;
    if (dgpsserver) {
	char hn[256], buf[BUFSIZE];

	if (!getservbyname(dgpsport, "tcp"))
	    dgpsport = "2101";

	session.dsock = netlib_connectsock(dgpsserver, dgpsport, "tcp");
	if (session.dsock < 0)
	    gpscli_errexit("Can't connect to dgps server");

	gethostname(hn, sizeof(hn));

	sprintf(buf, "HELO %s gpsd %s\r\nR\r\n", hn, VERSION);
	write(session.dsock, buf, strlen(buf));
	atexit(onexit);
    }

    /* mark fds closed */
    session.fdin = -1;
    session.fdout = -1;

    INIT(session.gNMEAdata.latlon_stamp, now, gps_timeout);
    INIT(session.gNMEAdata.altitude_stamp, now, gps_timeout);
    INIT(session.gNMEAdata.speed_stamp, now, gps_timeout);
    INIT(session.gNMEAdata.status_stamp, now, gps_timeout);
    INIT(session.gNMEAdata.mode_stamp, now, gps_timeout);
    session.gNMEAdata.mode = MODE_NO_FIX;
}

static void send_dgps(void)
{
  char buf[BUFSIZE];

  sprintf(buf, "R %0.8f %0.8f %0.2f\r\n", session.gNMEAdata.latitude,
	  session.gNMEAdata.longitude, session.gNMEAdata.altitude);
  write(session.dsock, buf, strlen(buf));
}

void gps_deactivate(void)
{
    session.fdin = -1;
    session.fdout = -1;
    gps_close();
    if (session.device_type->wrapup)
	session.device_type->wrapup();
    gpscli_report(1, "closed GPS\n");
    session.gNMEAdata.mode = 1;
    session.gNMEAdata.status = 0;
}

void gps_activate(void)
{
    int input;

    if ((input = gps_open(device_name, session.device_type->baudrate)) < 0)
    {
	gpscli_errexit("Exiting - serial open\n");
    }
    else
    {
	gpscli_report(1, "opened GPS\n");
	session.fdin = input;
	session.fdout = input;
    }
}

#include <sys/ioctl.h>

static int is_input_waiting(int fd)
{
    int	count;
    if (fd < 0 || ioctl(fd, FIONREAD, &count) < 0)
	return 0;
    return count;
}

void gps_poll(void)
{
    /* accept a DGPS correction if one is pending */
    if (is_input_waiting(session.dsock))
    {
	char buf[BUFSIZE];
	int rtcmbytes;

	if ((rtcmbytes=read(session.dsock,buf,BUFSIZE))>0 && (session.fdout!=-1))
	{
	    if (session.device_type->rctm_writer(buf, rtcmbytes) <= 0)
		gpscli_report(1, "Write to rtcm sink failed\n");
	}
	else 
	{
	    gpscli_report(1, "Read from rtcm source failed\n");
	}
    }

    /* update the scoreboard structure from the GPS */
    if (is_input_waiting(session.fdin)) {
	session.device_type->handle_input(session.fdin, raw_hook);
    }

    /* count the good fixes */
    if (session.gNMEAdata.status > 0) 
	session.fixcnt++;

    /* may be time to ship a DGPS correction to the GPS */
    if (session.fixcnt > 10) {
	if (!session.sentdgps) {
	    session.sentdgps++;
	    if (session.dsock > -1)
		send_dgps();
	}
    }
}

void gps_force_repoll(void)
{
    session.reopen = 1;
}

/* LIBRARY STUFF HERE ENDS */
