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
#include <sys/un.h>
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

#define DEFAULT_DEVICE_NAME	"/dev/gps"

#define QLEN			5

static fd_set all_fds;
static int debuglevel, in_background = 0;
static jmp_buf restartbuf;

static void onsig(int sig)
{
    longjmp(restartbuf, sig+1);
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

static int have_fix(struct gps_device_t *device)
{
    if (!device) {
	gpsd_report(4, "Client has no device");
	return 0;
    }
#define VALIDATION_COMPLAINT(level, legend) \
        gpsd_report(level, legend " (status=%d, mode=%d).\r\n", \
		    device->gpsdata.status, device->gpsdata.fix.mode)
    if ((device->gpsdata.status == STATUS_NO_FIX) != (device->gpsdata.fix.mode == MODE_NO_FIX)) {
	VALIDATION_COMPLAINT(3, "GPS is confused about whether it has a fix");
	return 0;
    }
    else if (device->gpsdata.status > STATUS_NO_FIX && device->gpsdata.fix.mode != MODE_NO_FIX) {
	VALIDATION_COMPLAINT(3, "GPS has a fix");
	return 1;
    }
    VALIDATION_COMPLAINT(3, "GPS has no fix");
    return 0;
#undef VALIDATION_CONSTRAINT
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

static int filesock(char *filename)
{
    struct sockaddr_un addr;
    int s;

    if ((s = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
	gpsd_report(0, "Can't create device-control socket\n");
	return -1;
    }
    strcpy(addr.sun_path, filename);
    addr.sun_family = AF_UNIX;
    bind(s, (struct sockaddr *) &addr, strlen(addr.sun_path) +
	 sizeof (addr.sun_family));
    if (listen(s, QLEN) < 0) {
	gpsd_report(0, "Can't listen on local socket %s\n", filename);
	return -1;
    }
    return s;
}

/*
 * Multi-session support requires us to have two arrays, one of GPS 
 * devices currently available and one of client sessions.  The number
 * of slots in each array is limited by the maximum number of client
 * sessions we can have open.
 */
#define MAXDEVICES	FD_SETSIZE

struct gps_device_t *channels[MAXDEVICES];

static struct subscriber_t {
    int active;				/* is this a subscriber? */
    int tied;				/* client set device with F */
    int watcher;			/* is client in watcher mode? */
    int raw;				/* is client in raw mode? */
    struct gps_device_t *device;	/* device subscriber listens to */
} subscribers[FD_SETSIZE];		/* indexed by client file descriptor */

static void detach_client(int cfd)
{
    close(cfd);
    FD_CLR(cfd, &all_fds);
    subscribers[cfd].raw = 0;
    subscribers[cfd].watcher = 0;
    subscribers[cfd].active = 0;
    subscribers[cfd].device = NULL;
}

static int throttled_write(int cfd, char *buf, int len)
/* write to client -- throttle if it's gone or we're close to buffer overrun */
{
    int status;

    gpsd_report(3, "=> client(%d): %s", cfd, buf);
    if ((status = write(cfd, buf, len)) > -1)
	return status;
    if (errno == EBADF)
	gpsd_report(3, "Client on %d has vanished.\n", cfd);
    else if (errno == EWOULDBLOCK)
	gpsd_report(3, "Dropped client on %d to avoid overrun.\n", cfd);
    else
	gpsd_report(3, "Client write to %d: %s\n", cfd, strerror(errno));
    detach_client(cfd);
    return status;
}

static void notify_watchers(struct gps_device_t *device, char *sentence, ...)
/* notify all clients watching a given device of an event */
{
    int cfd;
    va_list ap;
    char buf[BUFSIZ];

    va_start(ap, sentence) ;
    vsnprintf(buf, sizeof(buf), sentence, ap);
    va_end(ap);

    for (cfd = 0; cfd < FD_SETSIZE; cfd++)
	if (subscribers[cfd].watcher && subscribers[cfd].device == device)
	    throttled_write(cfd, buf, strlen(buf));
}

static void raw_hook(struct gps_data_t *ud UNUSED, char *sentence)
/* hook to be executed on each incoming packet */
{
    int cfd;

    for (cfd = 0; cfd < FD_SETSIZE; cfd++) {
	/* copy raw NMEA sentences from GPS to clients in raw mode */
	if (subscribers[cfd].raw)
	    throttled_write(cfd, sentence, strlen(sentence));
    }
}

static struct gps_device_t **find_device(char *device_name)
/* find the channel block for and existing device name */
{
    struct gps_device_t **chp;

    for (chp = channels; chp < channels + MAXDEVICES; chp++)
	if (*chp && !strcmp((*chp)->gpsdata.gps_device, device_name))
	    return chp;
    return NULL;
}

static struct gps_device_t *open_device(char *device_name, int nowait)
/* open and initialize a new channel block */
{
    struct gps_device_t **chp, *device = gpsd_init(device_name);

    for (chp = channels; chp < channels + MAXDEVICES; chp++) {
	if (!*chp) {
	    *chp = device;
	    goto found;
	}
    }
    return NULL;
found:
    device->gpsdata.raw_hook = raw_hook;
    if (nowait) {
	if (gpsd_activate(device) < 0) {
	    return NULL;
	}
	FD_SET(device->gpsdata.gps_fd, &all_fds);
    }

    return *chp;
}

static char *getline(char *p, char **out)
/* copy the rest of the command line, before CR-LF */
{
    char	*stash, *q;

    for (q = p; isprint(*p) && !isspace(*p); p++)
	continue;
    stash = (char *)malloc(p-q+1); 
    memcpy(stash, q, p-q);
    stash[p-q] = '\0';
    *out = stash;
    return p;
}

static int assign_channel(struct subscriber_t *user)
{
    /* if subscriber has no device... */
    if (!user->device) {
	time_t most_recent = 0;
	struct gps_device_t **channel, *mychannel = NULL;

	/* ...connect him to the most recently active device */
	for(channel = channels; channel<channels+MAXDEVICES; channel++)
	    if (*channel && (*channel)->gpsdata.sentence_time >= most_recent) {
		most_recent = (*channel)->gpsdata.sentence_time;
		mychannel = *channel;
		break;
	    }
	if (!mychannel)
	    return 0;
	else
	    user->device = mychannel;
    }

    /* and open that device */
    if (user->device->gpsdata.gps_fd == -1) {
	gpsd_deactivate(user->device);
	if (gpsd_activate(user->device) < 0) 
	    return 0;
	else {
	    FD_SET(user->device->gpsdata.gps_fd, &all_fds);
	    if (user->watcher && !user->tied) {
		write(user-subscribers, "F=", 2);
		write(user-subscribers, 
		      user->device->gpsdata.gps_device,
		      strlen(user->device->gpsdata.gps_device));
		write(user-subscribers, "\r\n", 2);
	    }
	    notify_watchers(user->device, "GPSD,X=%f\r\n", timestamp());
	}
    }

    return 1;
}

static int handle_request(int cfd, char *buf, int buflen)
/* interpret a client request; cfd is the socket back to the client */
{
    char reply[BUFSIZ], phrase[BUFSIZ], *p, *stash;
    int i, j;
    struct subscriber_t *whoami = subscribers + cfd;
    struct gps_device_t **newchan;

    strcpy(reply, "GPSD");
    p = buf;
    while (*p && p - buf < buflen) {
	phrase[0] = '\0';
	switch (toupper(*p++)) {
	case 'A':
	    if (assign_channel(whoami) && 
			have_fix(whoami->device) && 
			whoami->device->gpsdata.fix.mode == MODE_3D)
		sprintf(phrase, ",A=%.3f", 
			whoami->device->gpsdata.fix.altitude);
	    else
		strcpy(phrase, ",A=?");
	    break;
	case 'B':		/* change baud rate (SiRF only) */
	    if (assign_channel(whoami) && *p == '=') {
		i = atoi(++p);
		while (isdigit(*p)) p++;
		if (whoami->device->device_type->speed_switcher)
		    if (whoami->device->device_type->speed_switcher(whoami->device, i)) {
			/* 
			 * Allow the control string time to register at the
			 * GPS before we do the baud rate switch, which 
			 * effectively trashes the UART's buffer.
			 *
			 * This definitely fails below 40 milliseconds on a
			 * BU-303b. 50ms is also verified by Chris Kuethe on 
			 *        Pharos iGPS360 + GSW 2.3.1ES + prolific
			 *        Rayming TN-200 + GSW 2.3.1 + ftdi
			 *        Rayming TN-200 + GSW 2.3.2 + ftdi
			 * so it looks pretty solid.
			 *
			 * The minimum delay time is probably constant
			 * across any given type of UART.
			 */
			tcdrain(whoami->device->gpsdata.gps_fd);
			usleep(50000);
			gpsd_set_speed(whoami->device, (speed_t)i, 1);
		    }
	    }
	    if (whoami->device)
		sprintf(phrase, ",B=%d %d N %d", 
		    gpsd_get_speed(&whoami->device->ttyset),
			9 - whoami->device->gpsdata.stopbits, 
			whoami->device->gpsdata.stopbits);
	    else
		strcpy(phrase, ".B=?");
	    break;
	case 'C':
	    if (assign_channel(whoami))
		sprintf(phrase, ",C=%d", 
			whoami->device->device_type->cycle);
	    else
		strcpy(phrase, ".C=?");
	    break;
	case 'D':
	    strcpy(phrase, ",D=");
	    if (assign_channel(whoami) && whoami->device->gpsdata.fix.time)
		unix_to_iso8601(whoami->device->gpsdata.fix.time, phrase+3);
	    else
		strcpy(phrase, "?");
	    break;
	case 'E':
	    if (assign_channel(whoami) && have_fix(whoami->device)) {
		if (whoami->device->gpsdata.fix.eph 
			|| whoami->device->gpsdata.fix.epv)
		    sprintf(phrase, ",E=%.2f %.2f %.2f", 
			    whoami->device->gpsdata.epe, 
			    whoami->device->gpsdata.fix.eph, 
			    whoami->device->gpsdata.fix.epv);
		else if (whoami->device->gpsdata.pdop || whoami->device->gpsdata.hdop || whoami->device->gpsdata.vdop)
		    sprintf(phrase, ",E=%.2f %.2f %.2f", 
			    whoami->device->gpsdata.pdop * UERE(whoami->device), 
			    whoami->device->gpsdata.hdop * UERE(whoami->device), 
			    whoami->device->gpsdata.vdop * UERE(whoami->device));
	    } else
		strcpy(phrase, ",E=?");
	    break;
	case 'F':
	    if (*p == '=') {
		p = getline(++p, &stash);
		gpsd_report(1,"<= client(%d): switching to %s\n",cfd,stash);
		if ((newchan = find_device(stash))) {
		    whoami->device = *newchan;
		    whoami->tied = 1;
		}
		free(stash);
	    }
	    if (whoami->device)
		snprintf(phrase, sizeof(phrase), ",F=%s", 
			 whoami->device->gpsdata.gps_device);
	    else
		strcpy(phrase, ".F=?");
	    break;
	case 'I':
	    if (assign_channel(whoami))
		snprintf(phrase, sizeof(phrase), ",I=%s", 
			 whoami->device->device_type->typename);
	    else
		strcpy(phrase, ".B=?");
	    break;
	case 'K':
	    for (j = i = 0; i < MAXDEVICES; i++)
		if (channels[i])
		    j++;
	    sprintf(phrase, ",K=%d ", j);
	    for (i = 0; i < MAXDEVICES; i++) {
		if (channels[i] && strlen(phrase)+strlen(channels[i]->gpsdata.gps_device)+1 < sizeof(phrase)) {
		    strcat(phrase, channels[i]->gpsdata.gps_device);
		    strcat(phrase, " ");
		}
	    }
	    phrase[strlen(phrase)-1] = '\0';
	    break;
	case 'L':
	    sprintf(phrase, ",L=2 " VERSION " abcdefiklmnpqrstuvwxy");	//ghj
	    break;
	case 'M':
	    if (!assign_channel(whoami) && whoami->device->gpsdata.fix.mode == MODE_NOT_SEEN)
		strcpy(phrase, ",M=?");
	    else
		sprintf(phrase, ",M=%d", whoami->device->gpsdata.fix.mode);
	    break;
	case 'N':
	    if (!assign_channel(whoami))
		strcpy(phrase, ".N=?");
	    else if (!whoami->device->device_type->mode_switcher)
		strcpy(phrase, ",N=0");
	    else {
		if (*p == '=') ++p;
		if (*p == '1' || *p == '+') {
		    whoami->device->device_type->mode_switcher(whoami->device, 1);
		    p++;
		} else if (*p == '0' || *p == '-') {
		    whoami->device->device_type->mode_switcher(whoami->device, 0);
		    p++;
		}
	    }
	    sprintf(phrase, ",N=%d", whoami->device->gpsdata.driver_mode);
	    break;
	case 'O':
	    if (!assign_channel(whoami) || !have_fix(whoami->device))
		strcpy(phrase, ",O=?");
	    else {
		sprintf(phrase, ",O=%s %.2f %.3f %.6f %.6f",
			whoami->device->gpsdata.tag[0] ? whoami->device->gpsdata.tag : "-",
			whoami->device->gpsdata.fix.time, whoami->device->gpsdata.fix.ept, 
			whoami->device->gpsdata.fix.latitude, whoami->device->gpsdata.fix.longitude);
		if (whoami->device->gpsdata.fix.mode == MODE_3D)
		    sprintf(phrase+strlen(phrase), " %7.2f",
			    whoami->device->gpsdata.fix.altitude);
		else
		    strcat(phrase, "       ?");
		if (whoami->device->gpsdata.fix.eph)
		    sprintf(phrase+strlen(phrase), " %5.2f",  whoami->device->gpsdata.fix.eph);
		else
		    strcat(phrase, "        ?");
		if (whoami->device->gpsdata.fix.epv)
		    sprintf(phrase+strlen(phrase), " %5.2f",  whoami->device->gpsdata.fix.epv);
		else
		    strcat(phrase, "        ?");
		if (whoami->device->gpsdata.fix.track != TRACK_NOT_VALID)
		    sprintf(phrase+strlen(phrase), " %8.4f %8.3f",
			    whoami->device->gpsdata.fix.track, whoami->device->gpsdata.fix.speed);
		else
		    strcat(phrase, "        ?        ?");
		if (whoami->device->gpsdata.fix.mode == MODE_3D)
		    sprintf(phrase+strlen(phrase), " %6.3f", whoami->device->gpsdata.fix.climb);
		else
		    strcat(phrase, "      ?");
		strcat(phrase, " ?");	/* can't yet derive track error */ 
		if (whoami->device->gpsdata.valid & SPEEDERR_SET)
		    sprintf(phrase+strlen(phrase), " %5.2f",
			    whoami->device->gpsdata.fix.eps);		    
		else
		    strcat(phrase, "      ?");
		if (whoami->device->gpsdata.valid & CLIMBERR_SET)
		    sprintf(phrase+strlen(phrase), " %5.2f",
			    whoami->device->gpsdata.fix.epc);		    
		else
		    strcat(phrase, "      ?");
	    }
	    break;
	case 'P':
	    if (assign_channel(whoami) && have_fix(whoami->device))
		sprintf(phrase, ",P=%.4f %.4f", 
			whoami->device->gpsdata.fix.latitude, 
			whoami->device->gpsdata.fix.longitude);
	    else
		strcpy(phrase, ",P=?");
	    break;
	case 'Q':
	    if (assign_channel(whoami) && (whoami->device->gpsdata.pdop || whoami->device->gpsdata.hdop || whoami->device->gpsdata.vdop))
		sprintf(phrase, ",Q=%d %.2f %.2f %.2f",
			whoami->device->gpsdata.satellites_used, whoami->device->gpsdata.pdop, whoami->device->gpsdata.hdop, whoami->device->gpsdata.vdop);
	    else
		strcpy(phrase, ",Q=?");
	    break;
	case 'R':
	    if (*p == '=') ++p;
	    if (*p == '1' || *p == '+') {
		assign_channel(whoami);
		subscribers[cfd].raw = 1;
		gpsd_report(3, "%d turned on raw mode\n", cfd);
		sprintf(phrase, ",R=1");
		p++;
	    } else if (*p == '0' || *p == '-') {
		subscribers[cfd].raw = 0;
		gpsd_report(3, "%d turned off raw mode\n", cfd);
		sprintf(phrase, ",R=0");
		p++;
	    } else if (subscribers[cfd].raw) {
		subscribers[cfd].raw = 0;
		gpsd_report(3, "%d turned off raw mode\n", cfd);
		sprintf(phrase, ",R=0");
	    } else {
		assign_channel(whoami);
		subscribers[cfd].raw = 1;
		gpsd_report(3, "%d turned on raw mode\n", cfd);
		sprintf(phrase, ",R=1");
	    }
	    break;
	case 'S':
	    if (assign_channel(whoami))
		sprintf(phrase, ",S=%d", whoami->device->gpsdata.status);
	    else
		strcpy(phrase, ",S=?");
	    break;
	case 'T':
	    if (assign_channel(whoami) && have_fix(whoami->device) && whoami->device->gpsdata.fix.track != TRACK_NOT_VALID)
		sprintf(phrase, ",T=%.4f", whoami->device->gpsdata.fix.track);
	    else
		strcpy(phrase, ",T=?");
	    break;
	case 'U':
	    if (assign_channel(whoami) && have_fix(whoami->device) && whoami->device->gpsdata.fix.mode == MODE_3D)
		sprintf(phrase, ",U=%.3f", whoami->device->gpsdata.fix.climb);
	    else
		strcpy(phrase, ",U=?");
	    break;
	case 'V':
	    if (assign_channel(whoami) && have_fix(whoami->device) && whoami->device->gpsdata.fix.track != TRACK_NOT_VALID)
		sprintf(phrase, ",V=%.3f", whoami->device->gpsdata.fix.speed);
	    else
		strcpy(phrase, ",V=?");
	    break;
	case 'W':
	    if (*p == '=') ++p;
	    if (*p == '1' || *p == '+') {
		subscribers[cfd].watcher = 1;
		assign_channel(whoami);
		sprintf(phrase, ",W=1");
		p++;
	    } else if (*p == '0' || *p == '-') {
		subscribers[cfd].watcher = 0;
		sprintf(phrase, ",W=0");
		p++;
	    } else if (subscribers[cfd].watcher) {
		subscribers[cfd].watcher = 0;
		sprintf(phrase, ",W=0");
	    } else {
		subscribers[cfd].watcher = 1;
		assign_channel(whoami);
		gpsd_report(3, "%d turned on watching\n", cfd);
		sprintf(phrase, ",W=1");
	    }
	    break;
        case 'X':
	    if (assign_channel(whoami) && whoami->device)
		sprintf(phrase, ",X=%f", whoami->device->gpsdata.online);
	    else
		strcpy(phrase, ".X=?");
	    break;
	case 'Y':
	    if (assign_channel(whoami) && whoami->device->gpsdata.satellites) {
		int used, reported = 0;
		strcpy(phrase, ",Y=");
		if (whoami->device->gpsdata.tag[0])
		    strcat(phrase, whoami->device->gpsdata.tag);
		else
		    strcat(phrase, "-");
		if (whoami->device->gpsdata.valid & TIME_SET)
		    sprintf(phrase+strlen(phrase), " %f ", 
			    whoami->device->gpsdata.sentence_time);
		else
		    strcat(phrase, " ? ");
		sprintf(phrase+strlen(phrase), "%d:", 
			whoami->device->gpsdata.satellites);
		for (i = 0; i < whoami->device->gpsdata.satellites; i++) {
		    used = 0;
		    for (j = 0; j < whoami->device->gpsdata.satellites_used; j++)
			if (whoami->device->gpsdata.used[j] == whoami->device->gpsdata.PRN[i]) {
			    used = 1;
			    break;
			}
		    if (whoami->device->gpsdata.PRN[i]) {
			sprintf(phrase+strlen(phrase), "%d %d %d %d %d:", 
				whoami->device->gpsdata.PRN[i], 
				whoami->device->gpsdata.elevation[i],whoami->device->gpsdata.azimuth[i],
				whoami->device->gpsdata.ss[i],
				used);
			reported++;
		    }
		}
		if (whoami->device->gpsdata.satellites != reported)
		    gpsd_report(1,"Satellite count %d != PRN count %d\n",
				whoami->device->gpsdata.satellites, reported);
	    } else
		strcpy(phrase, ",Y=?");
	    break;
	case 'Z':
	    assign_channel(whoami); 
	    if (*p == '=') ++p;
	    if (*p == '1' || *p == '+') {
		whoami->device->gpsdata.profiling = 1;
		gpsd_report(3, "%d turned on profiling mode\n", cfd);
		sprintf(phrase, ",Z=1");
		p++;
	    } else if (*p == '0' || *p == '-') {
		whoami->device->gpsdata.profiling = 0;
		gpsd_report(3, "%d turned off profiling mode\n", cfd);
		sprintf(phrase, ",Z=0");
		p++;
	    } else {
		whoami->device->gpsdata.profiling = !whoami->device->gpsdata.profiling;
		gpsd_report(3, "%d toggled profiling mode\n", cfd);
		sprintf(phrase, ",Z=%d", whoami->device->gpsdata.profiling);
	    }
	    break;
        case '$':
	    if (whoami->device->gpsdata.sentence_time)
		sprintf(phrase, ",$=%s %d %f %f %f %f %f %f",
			whoami->device->gpsdata.tag,
			whoami->device->gpsdata.sentence_length,
			whoami->device->gpsdata.sentence_time,
			whoami->device->gpsdata.d_xmit_time - whoami->device->gpsdata.sentence_time,
			whoami->device->gpsdata.d_recv_time - whoami->device->gpsdata.sentence_time,
			whoami->device->gpsdata.d_decode_time - whoami->device->gpsdata.sentence_time,
			whoami->device->poll_times[cfd] - whoami->device->gpsdata.sentence_time,
			timestamp() - whoami->device->gpsdata.sentence_time);
	    else
		sprintf(phrase, ",$=%s %d 0 %f %f %f %f %f",
			whoami->device->gpsdata.tag,
			whoami->device->gpsdata.sentence_length,
			whoami->device->gpsdata.d_xmit_time,
			whoami->device->gpsdata.d_recv_time - whoami->device->gpsdata.d_xmit_time,
			whoami->device->gpsdata.d_decode_time - whoami->device->gpsdata.d_xmit_time,
			whoami->device->poll_times[cfd] - whoami->device->gpsdata.d_xmit_time,
			timestamp() - whoami->device->gpsdata.d_xmit_time);
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
    strcat(reply, "\r\n");

    return throttled_write(cfd, reply, strlen(reply));
}

static void handle_control(int sfd, char *buf)
/* handle privileged commands coming through the control socket */
{
    char	*p, *stash;
    struct gps_device_t	**chp;
    int cfd;

    if (buf[0] == '-') {
	p = getline(buf+1, &stash);
	gpsd_report(1,"<= control(%d): removing %s\n", sfd, stash);
	if ((chp = find_device(stash))) {
	    gpsd_deactivate(*chp);
	    notify_watchers(*chp, "X=0\r\n");
	    for (cfd = 0; cfd < FD_SETSIZE; cfd++)
		if (subscribers[cfd].device == *chp)
		    subscribers[cfd].device = NULL;
	    *chp = NULL;
	}
	free(stash);
    } else if (buf[0] == '+') {
	p = getline(buf+1, &stash);
	if (find_device(stash))
	    gpsd_report(1,"<= control(%d): %s already active \n", sfd, stash);
	else {
	    gpsd_report(1,"<= control(%d): adding %s \n", sfd, stash);
	    open_device(stash, 1);
	}
	free(stash);
    }
}

int main(int argc, char *argv[])
{
    static char *pid_file = NULL;
    static int st, changed, dsock = -1, csock = -1, nowait = 0;
    static char *dgpsserver = NULL;
    static char *service = NULL; 
    static char *device_name = DEFAULT_DEVICE_NAME;
    static char *control_socket = NULL;
    struct gps_device_t *device, **channel;
    struct sockaddr_in fsin;
    fd_set rfds, control_fds;
    int option, msock, cfd, dfd, go_background = 1;
    extern char *optarg;

    debuglevel = 0;
    while ((option = getopt(argc, argv, "F:D:S:d:f:hNnp:P:v")) != -1) {
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
	case 'F':
	    control_socket = optarg;
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
	daemonize();

    if (pid_file) {
	FILE *fp;

	if ((fp = fopen(pid_file, "w")) != NULL) {
	    fprintf(fp, "%u\n", getpid());
	    (void) fclose(fp);
	} else {
	    gpsd_report(1, "Cannot create PID file: %s.\n", pid_file);
	}
    }

    /* user may want to re-initialize all channels */
    if ((st = setjmp(restartbuf)) > 0) {
	for (dfd = 0; dfd < FD_SETSIZE; dfd++) {
	    if (channels[dfd])
		gpsd_wrap(channels[dfd]);
	}
	if (st == SIGHUP+1)
	    gpsd_report(1, "gpsd restarted by SIGHUP\n");
	else if (st > 0) {
	    gpsd_report(1,"Received terminating signal %d. Exiting...\n",st-1);
	    exit(10 + st);
	}
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
	gpsd_report(0,"command socket create failed, netlib error %d\n",msock);
	exit(2);
    }
    gpsd_report(1, "listening on port %s\n", service);
    if (control_socket) {
	unlink(control_socket);
	if ((csock = filesock(control_socket)) < 0) {
	    gpsd_report(0,"control socket create failed, netlib error %d\n",msock);
	    exit(2);
	}
	FD_SET(csock, &all_fds);
    }

    if (dgpsserver) {
	dsock = gpsd_open_dgps(dgpsserver);
	if (dsock >= 0)
	    FD_SET(dsock, &all_fds);
	else
	    gpsd_report(1, "Can't connect to DGPS server, netlib error %d\n",dsock);
    }

    FD_SET(msock, &all_fds);
    FD_ZERO(&control_fds);

    device = open_device(device_name, nowait);
    if (!device) {
	gpsd_report(0, "exiting - GPS device nonexistent or can't be read\n");
	exit(2);
    }
    if (dsock >= 0)
	device->dsock = dsock;

    for (;;) {
	struct timeval tv;

        memcpy((char *)&rfds, (char *)&all_fds, sizeof(rfds));
	if (device->dsock > -1)
	    FD_CLR(device->dsock, &rfds);

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

	/* always be open to new client connections */
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
		subscribers[ssock].active = 1;
		subscribers[ssock].tied = 0;
	    }
	    FD_CLR(msock, &rfds);
	}

	/* also be open to new control-socket connections */
	if (csock > -1 && FD_ISSET(csock, &rfds)) {
	    socklen_t alen = sizeof(fsin);
	    int ssock = accept(csock, (struct sockaddr *) &fsin, &alen);

	    if (ssock < 0)
		gpsd_report(0, "accept: %s\n", strerror(errno));
	    else {
		int opts = fcntl(ssock, F_GETFL);

		if (opts >= 0)
		    fcntl(ssock, F_SETFL, opts | O_NONBLOCK);
		gpsd_report(3, "control socket connect on %d\n", ssock);
		FD_SET(ssock, &all_fds);
		FD_SET(ssock, &control_fds);
	    }
	}

	/* read any commands that came in over control sockets */
	for (cfd = 0; cfd < FD_SETSIZE; cfd++)
	    if (FD_ISSET(cfd, &control_fds)) {
		char buf[BUFSIZ];

		if (read(cfd, buf, sizeof(buf)-1) > 0) {
		    gpsd_report(1, "<= control(%d): %s\n", cfd, buf);
		    handle_control(cfd, buf);
		}
	    }

	/* poll all active devices */
	for (channel = channels; channel < channels + MAXDEVICES; channel++) {
	    struct gps_device_t *device = *channel;

	    if (!device)
		continue;
	    /* we may need to force the GPS open */
	    if (nowait && device->gpsdata.gps_fd == -1) {
		gpsd_deactivate(device);
		if (gpsd_activate(device) >= 0) {
		    FD_SET(device->gpsdata.gps_fd, &all_fds);
		    notify_watchers(device, "GPSD,X=%f\r\n", timestamp());
		}
	    }

	    /* get data from the device */
	    changed = 0;
	    if (device->gpsdata.gps_fd >= 0 && !((changed=gpsd_poll(device)) & ONLINE_SET)) {
		gpsd_report(3, "GPS is offline\n");
		FD_CLR(device->gpsdata.gps_fd, &all_fds);
		gpsd_deactivate(device);
		notify_watchers(device, "GPSD,X=0\r\n");
	    }

	    for (cfd = 0; cfd < FD_SETSIZE; cfd++) {
		/* some listeners may be in watcher mode */
		if (subscribers[cfd].watcher) {
		    char cmds[4] = ""; 
		    device->poll_times[cfd] = timestamp();
		    if (changed &~ ONLINE_SET) {
			if (changed & LATLON_SET)
			    strcat(cmds, "o");
			if (changed & SATELLITE_SET)
			    strcat(cmds, "y");
		    }
		    if (device->gpsdata.profiling && device->packet_full)
			strcat(cmds, "$");
		    if (cmds[0])
			handle_request(cfd, cmds, strlen(cmds));
		}
	    }
	}

	/* accept and execute commands for all clients */
	for (cfd = 0; cfd < FD_SETSIZE; cfd++) {
	    if (subscribers[cfd].active && FD_ISSET(cfd, &rfds)) {
		char buf[BUFSIZ];
		int buflen;

		gpsd_report(3, "checking %d \n", cfd);
		if ((buflen = read(cfd, buf, sizeof(buf) - 1)) <= 0) {
		    detach_client(cfd);
		} else {
		    buf[buflen] = '\0';
		    gpsd_report(1, "<= client: %s", buf);

		    if (subscribers[cfd].device)
			subscribers[cfd].device->poll_times[cfd] = timestamp();
		    if (handle_request(cfd, buf, buflen) < 0)
			detach_client(cfd);
		}
	    }
	}

	/* close devices with no remaining subscribers */
	for (channel = channels; channel < channels + MAXDEVICES; channel++) {
	    if (*channel) {
		int need_gps = 0;

		for (cfd = 0; cfd < FD_SETSIZE; cfd++)
		    if (subscribers[cfd].active&&subscribers[cfd].device==*channel)
			need_gps++;

		if (!nowait && !need_gps && (*channel)->gpsdata.gps_fd > -1) {
		    FD_CLR((*channel)->gpsdata.gps_fd, &all_fds);
		    gpsd_deactivate(*channel);
		}
	    }
	}
    }

    return 0;
}
