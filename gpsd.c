
#include "config.h"
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <syslog.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>

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
#include <sys/time.h>
#include <netinet/in.h>
#include <stdio.h>
#include "nmea.h"
#include "gpsd.h"
#include "version.h"

#define QLEN		5
#define BUFSIZE		4096

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
char *default_longitude = "-12300.000";

int nfds;
int verbose = 1;
int bincount;

static int handle_input(int input, fd_set * afds, fd_set * nmea_fds);
extern int handle_EMinput(int input);
static int handle_request(int fd, fd_set * fds);

static void onsig(int sig)
{
    serial_close();
    syslog(LOG_NOTICE, "Received signal %d. Exiting...", sig);
    exit(10 + sig);
}

int daemonize()
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

int main(int argc, char *argv[])
{
    char *default_service = "5678";
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
    double baud;

    while ((option = getopt(argc, argv, "D:L:S:T:hl:p:s:")) != -1) {
	switch (option) {
	case 'T':
	    switch (*optarg) {
	    case 't':
		device_type = DEVICE_TRIPMATE;
		break;
	    case 'e':
		device_type = DEVICE_EARTHMATE;
		break;
	    default:
		fprintf(stderr, "Invalide device type \"%s\"\n"
			"Using GENERIC instead\n", optarg);
		break;
	    }
	    break;
	case 'D':
	    debug = (int) strtol(optarg, 0, 0);
	    break;
	case 'L':
	    if (optarg[strlen(optarg) - 1] == 'W' || optarg[strlen(optarg) - 1] == 'w'
		|| optarg[strlen(optarg) - 1] == 'E' || optarg[strlen(optarg) - 1] == 'e') {
		lond = toupper(optarg[strlen(optarg) - 1]);
		longitude = optarg;
		longitude[strlen(optarg) - 1] = '\0';
	    } else
		fprintf(stderr, "skipping invalid longitude (-L) option;  %s must end in W or E\n", optarg);
	    break;
	case 'S':
	    service = optarg;
	    break;
	case 'l':
	    if (optarg[strlen(optarg) - 1] == 'N' || optarg[strlen(optarg) - 1] == 'n'
		|| optarg[strlen(optarg) - 1] == 'S' || optarg[strlen(optarg) - 1] == 's') {
		latd = toupper(optarg[strlen(optarg) - 1]);
		latitude = optarg;
		latitude[strlen(optarg) - 1] = '\0';
	    } else
		fprintf(stderr, "skipping invalid latitude (-l) option;  %s must end in N or S\n", optarg);
	    break;
	case 'p':
	    device_name = optarg;
	    break;
	case 's':
	    baud = strtod(optarg, 0);
	    if (baud < 200)
		baud *= 1000;
	    if (baud < 2400)
		device_speed = B1200;
	    else if (baud < 4800)
		device_speed = B2400;
	    else if (baud < 9600)
		device_speed = B4800;
	    else if (baud < 19200)
		device_speed = B9600;
	    else if (baud < 38400)
		device_speed = B19200;
	    else
		device_speed = B38400;
	    break;
	case 'h':
	case '?':
	default:
	    fputs("usage:  gpsd [options] \n\
  options include: \n\
  -D integer   [ set debug level ] \n\
  -L longitude [ set longitude ] \n\
  -S integer   [ set port for daemon ] \n\
  -T e         [ erthmate flag ] \n\
  -h           [ help message ] \n\
  -l latitude  [ set latitude ] \n\
  -p string    [ set gps device name ] \n\
  -s baud_rate [ set baud rate on gps device ] \n\
", stderr);
	    exit(0);
	}
    }
    if (!device_name)
	device_name = default_device_name;
    if (!latitude)
	latitude = default_latitude;
    if (!longitude)
	longitude = default_longitude;
    if (!service)
	service = default_service;
    if (debug > 0) {
	fprintf(stderr, "command line options:\n");
	fprintf(stderr, "  debug level:        %d\n", debug);
	fprintf(stderr, "  gps device name:    %s\n", device_name);
	fprintf(stderr, "  gps device speed:   %d\n", device_speed);
	fprintf(stderr, "  gpsd port:          %s\n", service);
	fprintf(stderr, "  latitude:           %s%c\n", latitude, latd);
	fprintf(stderr, "  longitude:          %s%c\n", longitude, lond);
    }
    if (debug < 2)
	daemonize();

    /* Handle some signals */
    signal(SIGINT, onsig);
    signal(SIGHUP, onsig);
    signal(SIGTERM, onsig);

    openlog("gpsd", LOG_PID, LOG_USER);
    syslog(LOG_NOTICE, "Gpsd started (Version %s)", VERSION);
    syslog(LOG_NOTICE, "Gpsd listening on port %s", service);

    msock = passiveTCP(service, QLEN);

    nfds = getdtablesize();

    FD_ZERO(&afds);
    FD_ZERO(&nmea_fds);
    FD_SET(msock, &afds);

    input = -1;


    while (1) {
	bcopy((char *) &afds, (char *) &rfds, sizeof(rfds));

	if (select(nfds, &rfds, (fd_set *) 0, (fd_set *) 0,
		   (struct timeval *) 0) < 0) {
	    if (errno == EINTR)
		continue;
	    errexit("select");
	}
	if (FD_ISSET(msock, &rfds)) {
	    int ssock;

	    alen = sizeof(fsin);
	    ssock = accept(msock, (struct sockaddr *) &fsin,
			   &alen);

	    if (ssock < 0)
		errexit("accept");

	    FD_SET(ssock, &afds);
	}
	if (input >= 0 && FD_ISSET(input, &rfds)) {
	    if (device_type == DEVICE_EARTHMATEb)
		handle_EMinput(input);
	    else
		handle_input(input, &afds, &nmea_fds);
	}
	need_gps = 0;
	for (fd = 0; fd < nfds; fd++) {
	    if (fd != msock && fd != input && FD_ISSET(fd, &rfds)) {
		if (input == -1) {
		    if ((input = serial_open()) < 0)
			errexit("serial open: ");
		    syslog(LOG_NOTICE, "Opened gps");
		    FD_SET(input, &afds);
		    gNMEAdata.fdin = input;
		    gNMEAdata.fdout = input;
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
	    gNMEAdata.fdin = input;
	    gNMEAdata.fdout = input;
	    serial_close();
	    if (device_type == DEVICE_EARTHMATEb)
		device_type = DEVICE_EARTHMATE;
	    syslog(LOG_NOTICE, "Closed gps");
	    gNMEAdata.mode = 1;
	    gNMEAdata.status = 0;
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
		    gNMEAdata.latitude,
		    gNMEAdata.longitude);
	    break;
	case 'D':
	case 'd':
	    sprintf(reply + strlen(reply),
		    ",D=%s",
		    gNMEAdata.utc);
	    break;
	case 'A':
	case 'a':
	    sprintf(reply + strlen(reply),
		    ",A=%f",
		    gNMEAdata.altitude);
	    break;
	case 'V':
	case 'v':
	    sprintf(reply + strlen(reply),
		    ",V=%f",
		    gNMEAdata.speed);
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
		    gNMEAdata.status);
	    break;
	case 'M':
	case 'm':
	    sprintf(reply + strlen(reply),
		    ",M=%d",
		    gNMEAdata.mode);
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

static int handle_input(int input, fd_set * afds, fd_set * nmea_fds)
{
    static unsigned char buf[BUFSIZE];	/* that is more then a sentence */
    static int offset = 0;
    int fd;

    while (offset < BUFSIZE) {
	if (read(input, buf + offset, 1) != 1)
	    return 1;

	if (buf[offset] == '\n' || buf[offset] == '\r') {
	    buf[offset] = '\0';
	    if (strlen(buf)) {
		handle_message(buf);
		strcat(buf, "\r\n");
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
	    offset = 0;
	    return 1;
	}
	// The following tries to recognise if the EarthMate is
	// in binary mode. If so, it will switch to EarthMate mode.

	if (device_type == DEVICE_EARTHMATE) {
	    if (offset) {
		if (buf[offset - 1] == (unsigned char) 0xff) {
		    if (buf[offset] == (unsigned char) 0x81) {
			if (bincount++ == 5) {
			    syslog(LOG_NOTICE,
				   "Found an EarthMate (syn).");
			    device_type = DEVICE_EARTHMATEb;
			    return 0;
			}
		    }
		}
	    }
	}
	offset++;
	buf[offset] = '\0';
    }
    offset = 0;			/* discard input ! */
    return 1;
}

int errexit(char *s)
{
    syslog(LOG_ERR, "%s: %s\n", s, strerror(errno));
    serial_close();
    exit(2);
}
