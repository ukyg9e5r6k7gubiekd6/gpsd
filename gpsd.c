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

#include "nmea.h"
#include "gpsd.h"
#include "outdata.h"
#include "version.h"

#define QLEN		5
#define BUFSIZE		4096
#define GPS_TIMEOUT	5		/* Consider GPS connection loss after 5 sec */

int gps_timeout = GPS_TIMEOUT;
int debug = 0;
int device_speed = B4800;
int device_type;
char *device_name = 0;
char *latitude = 0;
char *longitude = 0;
char latd = 'N';
char lond = 'W';
				/* command line option defaults */
char *default_device_name = "/dev/gps";
char *default_latitude = "3600.000";
char *default_longitude = "12300.000";

int nfds, dsock;
int verbose = 1;
int bincount;

int reopen = 0;

static int handle_input(int input, fd_set * afds, fd_set * nmea_fds);
static int handle_request(int fd, fd_set * fds);

static void onsig(int sig)
{
    serial_close();
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

static void send_dgps()
{
  char buf[BUFSIZE];

  sprintf(buf, "R %0.8f %0.8f %0.2f\r\n", gNMEAdata.latitude,
	  gNMEAdata.longitude, gNMEAdata.altitude);
  write(dsock, buf, strlen(buf));
}

static void usage()
{
	    fputs("usage:  gpsd [options] \n\
  options include: \n\
  -D integer   [ set debug level ] \n\
  -L longitude [ set longitude ] \n\
  -S integer   [ set port for daemon ] \n\
  -T e         [ earthmate flag ] \n\
  -h           [ help message ] \n\
  -l latitude  [ set latitude ] \n\
  -p string    [ set gps device name ] \n\
  -s baud_rate [ set baud rate on gps device ] \n\
  -t timeout   [ set timeout in seconds on fix/mode validity ] \n\
  -c           [ use dgps service for corrections ] \n\
  -d host      [ set dgps server ] \n\
  -r port      [ set dgps rtcm-sc104 port ] \n\
", stderr);
}

static int set_baud(long baud)
{
    int speed;
    
    if (baud < 200)
	baud *= 1000;
    if (baud < 2400)
	speed = B1200;
    else if (baud < 4800)
	speed = B2400;
    else if (baud < 9600)
	speed = B4800;
    else if (baud < 19200)
	speed = B9600;
    else if (baud < 38400)
	speed = B19200;
    else
	speed = B38400;

    return speed;
}

static int set_device_type(char what)
{
    int type;

    if (what=='t')
	type = DEVICE_TRIPMATE;
    else if (what=='e')
	type = DEVICE_EARTHMATE;
    else {
	fprintf(stderr, "Invalid device type \"%s\"\n"
		"Using GENERIC instead\n", optarg);
	type = 0;
    }
    return type;
}


static void print_settings(char *service, char *dgpsserver,
	char *dgpsport, int need_dgps)
{
    fprintf(stderr, "command line options:\n");
    fprintf(stderr, "  debug level:        %d\n", debug);
    fprintf(stderr, "  gps device name:    %s\n", device_name);
    fprintf(stderr, "  gps device speed:   %d\n", device_speed);
    fprintf(stderr, "  gpsd port:          %s\n", service);
    if (need_dgps) {
      fprintf(stderr, "  dgps server:        %s\n", dgpsserver);
      fprintf(stderr, "  dgps port:        %s\n", dgpsport);
    }
    fprintf(stderr, "  latitude:           %s%c\n", latitude, latd);
    fprintf(stderr, "  longitude:          %s%c\n", longitude, lond);
}

static int handle_dgps()
{
    char buf[BUFSIZE];
    int rtcmbytes, cnt;

    if ((rtcmbytes=read(dsock, buf, BUFSIZE))>0 && (gNMEAdata.fdout!=-1)) {

	if (device_type == DEVICE_EARTHMATEb)
	    cnt = em_send_rtcm(buf, rtcmbytes);
	else
	    cnt = write(gNMEAdata.fdout, buf, rtcmbytes);
	
	if (cnt<=0)
	    syslog(LOG_WARNING, "Write to rtcm sink failed");
    }
    else {
	syslog(LOG_WARNING, "Read from rtcm source failed");
    }

    return rtcmbytes;
}

static void deactivate()
{
    gNMEAdata.fdin = -1;
    gNMEAdata.fdout = -1;
    serial_close();
    if (device_type == DEVICE_EARTHMATEb)
	device_type = DEVICE_EARTHMATE;
    syslog(LOG_NOTICE, "Closed gps");
    gNMEAdata.mode = 1;
    gNMEAdata.status = 0;
}

static int activate()
{
    int input;

    if ((input = serial_open()) < 0)
	errexit("Exiting - serial open");
 
    syslog(LOG_NOTICE, "Opened gps");
    gNMEAdata.fdin = input;
    gNMEAdata.fdout = input;

    return input;
}

int main(int argc, char *argv[])
{
    char *default_service = "gpsd";
    char *default_dgpsserver = "dgps.wsrcc.com";
    char *default_dgpsport = "rtcm-sc104";
    char *service = 0;
    char *dgpsport = 0;
    char *dgpsserver = 0;
    struct sockaddr_in fsin;
    int msock;
    fd_set rfds;
    fd_set afds;
    fd_set nmea_fds;
    int alen;
    int fd, input;
    int need_gps, need_dgps = 0, need_init = 1;
    extern char *optarg;
    int option;
    char buf[BUFSIZE];
    int sentdgps = 0, fixcnt = 0;

    while ((option = getopt(argc, argv, "D:L:S:T:hncl:p:s:d:r:t:")) != -1) {
	switch (option) {
	case 'T':
	    device_type = set_device_type(*optarg);
	    break;
	case 'D':
	    debug = (int) strtol(optarg, 0, 0);
	    break;
	case 'd':
	    dgpsserver = optarg;
	    break;
	case 'L':
	    if (optarg[strlen(optarg)-1] == 'W' || optarg[strlen(optarg)-1] == 'w'
		|| optarg[strlen(optarg)-1] == 'E' || optarg[strlen(optarg)-1] == 'e') {
		lond = toupper(optarg[strlen(optarg)-1]);
		longitude = optarg;
		longitude[strlen(optarg)-1] = '\0';
	    } else
		fprintf(stderr,
		  "skipping invalid longitude (-L) option; "
		  "%s must end in W or E\n", optarg);
	    break;
	case 'S':
	    service = optarg;
	    break;
	case 'r':
	    dgpsport = optarg;
	    break;
	case 'l':
	    if (optarg[strlen(optarg)-1] == 'N' || optarg[strlen(optarg)-1] == 'n'
		|| optarg[strlen(optarg)-1] == 'S' || optarg[strlen(optarg)-1] == 's') {
		latd = toupper(optarg[strlen(optarg) - 1]);
		latitude = optarg;
		latitude[strlen(optarg) - 1] = '\0';
	    } else
		fprintf(stderr,
			"skipping invalid latitude (-l) option;  "
		       	"%s must end in N or S\n", optarg);
	    break;
	case 'p':
	    device_name = optarg;
	    break;
	case 's':
	    device_speed = set_baud(strtol(optarg, NULL, 0));
	    break;
	case 'c':
	    need_dgps = 1;
	    break;
	case 'n':
	    need_init = 0;
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

    if (need_init && !latitude) latitude = default_latitude;
    if (need_init && !longitude) longitude = default_longitude;
    
    if (!service) {
	if (!getservbyname(default_service, "tcp"))
	    service = "2947";
	else service = default_service;
    }

    if (need_dgps && !dgpsserver) dgpsserver = default_dgpsserver;
    if (need_dgps && !dgpsport) dgpsport = default_dgpsport;
    
    if (debug > 0) 
	print_settings(service, dgpsserver, dgpsport, need_dgps);
    
    if (debug < 2)
	daemonize();

    /* Handle some signals */
    signal(SIGUSR1, sigusr1);
    signal(SIGINT, onsig);
    signal(SIGHUP, onsig);
    signal(SIGTERM, onsig);
    signal(SIGQUIT, onsig);
    signal(SIGPIPE, SIG_IGN);

    openlog("gpsd", LOG_PID, LOG_USER);
    syslog(LOG_NOTICE, "Gpsd started (Version %s)", VERSION);
    syslog(LOG_NOTICE, "Gpsd listening on port %s", service);

    msock = passiveTCP(service, QLEN);

    nfds = getdtablesize();

    FD_ZERO(&afds);
    FD_ZERO(&nmea_fds);
    FD_SET(msock, &afds);

    if (need_dgps) {
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
    gNMEAdata.fdin = input;
    gNMEAdata.fdout = input;

    gNMEAdata.v_latlon = gps_timeout;
    gNMEAdata.v_alt = gps_timeout;
    gNMEAdata.v_speed = gps_timeout;
    gNMEAdata.v_status = gps_timeout;
    gNMEAdata.v_mode = gps_timeout;

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
	    gNMEAdata.last_update = time(NULL);
	    if (device_type == DEVICE_EARTHMATEb) 
		handle_EMinput(input, &afds, &nmea_fds);
	    else
		handle_input(input, &afds, &nmea_fds);
	}

	if (gNMEAdata.status > 0) 
	    fixcnt++;
	
	if (fixcnt > 10) {
	    if (!sentdgps) {
		sentdgps++;
		if (need_dgps)
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

static int validate_sm(time_t cur_time)
{
    int invalidate = 0;
    int ostatus, omode;
    int status = 0;

     ostatus = gNMEAdata.status;
     omode   = gNMEAdata.mode;

    /* status: 0 = no fix, 1 = fix, 2 = dgps fix */
    /* mode:   1 = no fix, 2 = 2D, 3 = 3D */

    /* always slave mode to status, consider mode only if status is valid */
   
    if (debug>1) fprintf(stderr, "status=%d, mode=%d\n",
    			ostatus, omode);

    if ((gNMEAdata.ts_status + gNMEAdata.v_status) >= cur_time) {
        status = ostatus;
	if (debug>1) fprintf(stderr, "status is valid!\n");
	if ((gNMEAdata.ts_mode + gNMEAdata.v_mode) >= cur_time) {
	    switch (ostatus) {
		case 0:
		    if (omode != 1) invalidate = 1;
		    break;
		case 1:
		case 2:
		    if ((omode!=2) && (omode!=3)) invalidate = 2;
		    break;
		default:
		    invalidate = 3;
		    break;
	    }
	}
    }
    else {
	gNMEAdata.ts_mode = 0;	/* invalidate mode */
    }
    gNMEAdata.cmask &= ~(C_STATUS|C_MODE);

    if (invalidate) {
	syslog(LOG_ERR, "Impossible status(%d)/mode(%d) reason(%d)\n",
		ostatus, omode, invalidate);
	gNMEAdata.ts_status = 0;
	gNMEAdata.ts_mode = 0;
	status = 0;
    }
    return status;
}

static int handle_request(int fd, fd_set * fds)
{
    char buf[BUFSIZE];
    char reply[BUFSIZE];
    char *p;
    int cc, status;
    time_t cur_time;

    cc = read(fd, buf, sizeof(buf) - 1);
    if (cc < 0)
	return 0;

    buf[cc] = '\0';

    cur_time = time(NULL);
    status = validate_sm(cur_time);		/* validate status and mode */

    sprintf(reply, "GPSD");
    p = buf;
    while (*p) {
	if (status) {
	    switch (*p) {
	    case 'P':
	    case 'p':
		if ((gNMEAdata.ts_latlon + gNMEAdata.v_latlon) >= cur_time) {
		    sprintf(reply + strlen(reply),
			    ",P=%f %f",
			    gNMEAdata.latitude,
			    gNMEAdata.longitude);
		}
		break;
	    case 'A':
	    case 'a':
		if ((gNMEAdata.ts_alt + gNMEAdata.v_alt) >= cur_time) {
		    sprintf(reply + strlen(reply),
			    ",A=%f",
			    gNMEAdata.altitude);
		}
		break;
	    case 'V':
	    case 'v':
		if ((gNMEAdata.ts_speed + gNMEAdata.v_speed) >= cur_time) {
		    sprintf(reply + strlen(reply),
			    ",V=%f",
			    gNMEAdata.speed);
		}
		break;
	    }
	}

	switch (*p) {
	case 'D':
	case 'd':
	    sprintf(reply + strlen(reply),
		    ",D=%s",
		    gNMEAdata.utc);
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
	case '\r':
	case '\n':
	    *p = '\0';		/* ignore the rest */
	    break;
	case 'S':
	case 's':
	    if ((gNMEAdata.ts_status + gNMEAdata.v_status) >= cur_time) {
		sprintf(reply + strlen(reply),
			",S=%d",
			gNMEAdata.status);
	    }
	    break;
	case 'M':
	case 'm':
	    if ((gNMEAdata.ts_mode + gNMEAdata.v_mode) >= cur_time) {
		sprintf(reply + strlen(reply),
			",M=%d",
			gNMEAdata.mode);
	    }
	    break;
	case 'Q':
	case 'q':
	    sprintf(reply + strlen(reply),
		    ",Q=%d %d %f %f %f",
		    gNMEAdata.in_view, gNMEAdata.satellites,
		    gNMEAdata.pdop, gNMEAdata.hdop, gNMEAdata.vdop);
	    break;
	case 'I':
	case 'i':
	    {
	    	int i;
		char *q;
		
		q = p;
		i = (int)strtol(p+1, &q, 10);
		if (i>=0 && i<gNMEAdata.in_view) {
		    fprintf(stderr, "i=%d in view=%d\n", i, gNMEAdata.in_view);
		    sprintf(reply + strlen(reply),
			    ",I=%d %d %d %d",
			    gNMEAdata.PRN[i], gNMEAdata.azimuth[i],
			    gNMEAdata.elevation[i], gNMEAdata.ss[i]);
		}
		else sprintf(reply + strlen(reply), ",I=-1 -1 -1 -1");
		if (q!=p) p = q-1;
	    }
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
    static int offset = 0;

    while (offset < BUFSIZE) {
	if (read(input, buf + offset, 1) != 1)
	    return 1;

	if (buf[offset] == '\n' || buf[offset] == '\r') {
	    buf[offset] = '\0';
	    if (strlen(buf)) {
	        handle_message(buf);
		strcat(buf, "\r\n");
		send_nmea(afds, nmea_fds, buf);
	    }
	    offset = 0;
	    return 1;
	}

	offset++;
	buf[offset] = '\0';
    }
    offset = 0;			/* discard input ! */
    return 1;
}

void errlog(char *s)
{
    syslog(LOG_ERR, "%s: %s\n", s, strerror(errno));
}

void  errexit(char *s)
{
    syslog(LOG_ERR, "%s: %s\n", s, strerror(errno));
    serial_close();
    close(dsock);
    exit(2);
}
