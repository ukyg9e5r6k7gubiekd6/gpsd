/* $Id$ */
/*
 * The generic GPS packet monitor.
 */
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#ifndef S_SPLINT_S
#include <unistd.h>
#endif /* S_SPLINT_S */
#include <assert.h>
#include <signal.h>
#include <setjmp.h>
/* Cygwin has only _timezone and not timezone unless the following is set */
#if defined(__CYGWIN__)
#define timezonevar
#endif /* defined(__CYGWIN__) */
#include <time.h>
#include <termios.h>
#include <fcntl.h>	/* for O_RDWR */
#include <stdarg.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/ioctl.h>	/* for O_RDWR */

#include "gpsd_config.h"
#ifdef HAVE_NCURSES_H
#include <ncurses.h>
#else
#include <curses.h>
#endif /* HAVE_NCURSES_H */
#include "gpsd.h"

#include "bits.h"

#if defined(HAVE_SYS_TIME_H)
#include <sys/time.h>
#endif
#if defined (HAVE_SYS_SELECT_H)
#include <sys/select.h>
#endif

#include "gpsdclient.h"
#include "gpsmon.h"

#ifdef S_SPLINT_S
extern struct tm *localtime_r(const time_t *,/*@out@*/struct tm *tp)/*@modifies tp@*/;
#endif /* S_SPLINT_S */

extern int netlib_connectsock(const char *, const char *, const char *);

#define BUFLEN		2048

/* external capability tables */
extern struct monitor_object_t nmea_mmt, sirf_mmt, garmin_mmt, ashtech_mmt;
extern struct monitor_object_t italk_mmt, ubx_mmt, superstar2_mmt;
extern struct monitor_object_t fv18_mmt, gpsclock_mmt, mkt3301_mmt;
extern struct monitor_object_t oncore_mmt;

/* These are public */
struct gps_device_t	session;
WINDOW *devicewin;
int gmt_offset;

/* These are private */
static struct gps_context_t	context;
static int controlfd = -1;
static bool serial, curses_active;
static int debuglevel = 0;
static WINDOW *statwin, *cmdwin;
/*@null@*/static WINDOW *packetwin;
static FILE *logfile;
static char *type_name;
/*@ -nullassign @*/
static const struct monitor_object_t *monitor_objects[] = {
#ifdef NMEA_ENABLE
    &nmea_mmt,
#if defined(GARMIN_ENABLE) && defined(NMEA_ENABLE)
    &garmin_mmt,
#endif /* GARMIN_ENABLE && NMEA_ENABLE */
#ifdef ASHTECH_ENABLE
    &ashtech_mmt,
#endif /* ASHTECH_ENABLE */
#ifdef FV18_ENABLE
    &fv18_mmt,
#endif /* FV18_ENABLE */
#ifdef GPSCLOCK_ENABLE
    &gpsclock_mmt,
#endif /* GPSCLOCK_ENABLE */
#ifdef MKT3301_ENABLE
    &mkt3301_mmt,
#endif /* MKT3301_ENABLE */
#endif /* NMEA_ENABLE */
#if defined(SIRF_ENABLE) && defined(BINARY_ENABLE)
    &sirf_mmt,
#endif /* defined(SIRF_ENABLE) && defined(BINARY_ENABLE) */
#if defined(UBX_ENABLE) && defined(BINARY_ENABLE)
    &ubx_mmt,
#endif /* defined(UBX_ENABLE) && defined(BINARY_ENABLE) */
#if defined(ITRAX_ENABLE) && defined(BINARY_ENABLE)
    &italk_mmt,
#endif /* defined(ITALK_ENABLE) && defined(BINARY_ENABLE) */
#if defined(SUPERSTAR2_ENABLE) && defined(BINARY_ENABLE)
    &superstar2_mmt,
#endif /* defined(SUPERSTAR2_ENABLE) && defined(BINARY_ENABLE) */
#if defined(ONCORE_ENABLE) && defined(BINARY_ENABLE)
    &oncore_mmt,
#endif /* defined(ONCORE_ENABLE) && defined(BINARY_ENABLE) */
    NULL,
};
static const struct monitor_object_t **active;
/*@ +nullassign @*/

#define display	(void)mvwprintw

void monitor_fixframe(WINDOW *win)
{
    int ymax, xmax, ycur, xcur;

    assert(win!= NULL);
    getyx(win, ycur, xcur);
    getmaxyx(win, ymax, xmax);
    (void)mvwaddch(win, ycur, xmax-1, ACS_VLINE);
}

/******************************************************************************
 *
 * Device-independent I/O routines
 *
 ******************************************************************************/

void gpsd_report(int errlevel UNUSED, const char *fmt, ... )
/* our version of the logger */
{
    if (errlevel <= debuglevel && packetwin!=NULL) {
	va_list ap;
	va_start(ap, fmt);
	if (!curses_active)
	    (void)vprintf(fmt, ap);
	else
	    (void)wprintw(packetwin, (char *)fmt, ap);
	va_end(ap);
    }
}

/*@ -globstate @*/
static ssize_t readpkt(void)
{
    /*@ -type -shiftnegative -compdef -nullpass @*/
    struct timeval timeval;
    fd_set select_set;
    gps_mask_t changed;

    FD_ZERO(&select_set);
    FD_SET(session.gpsdata.gps_fd,&select_set);
    if (controlfd < -1)
	FD_SET(controlfd,&select_set);
    timeval.tv_sec = 0;
    timeval.tv_usec = 500000;
    if (select(session.gpsdata.gps_fd + 1,&select_set,NULL,NULL,&timeval) == -1)
	return EOF;

    if (!FD_ISSET(session.gpsdata.gps_fd,&select_set))
	return EOF;

    changed = gpsd_poll(&session);
    if (changed & ERROR_SET)
	return EOF;

    if (logfile != NULL) {
	/*@ -shiftimplementation -sefparams +charint @*/
	assert(fwrite(session.packet.outbuffer,		      sizeof(char), session.packet.outbuflen,
		      logfile) >= 1);
	/*@ +shiftimplementation +sefparams -charint @*/
    }
    return session.packet.outbuflen;
    /*@ +type +shiftnegative +compdef +nullpass @*/
}
/*@ +globstate @*/

static void packet_dump(char *buf, size_t buflen)
{
    if (packetwin != NULL) {
	size_t i;
	bool printable = true;
	for (i = 0; i < buflen; i++)
	    if (!isprint(buf[i]) && !isspace(buf[i]))
		printable = false;
	if (printable) {
	    for (i = 0; i < buflen; i++)
		if (isprint(buf[i]))
		    (void)waddch(packetwin, (chtype)buf[i]);
		else
		    (void)wprintw(packetwin, "\\x%02x", (unsigned char)buf[i]);
	} else {
	    for (i = 0; i < buflen; i++)
		(void)wprintw(packetwin, "%02x", (unsigned char)buf[i]);
	}
	(void)wprintw(packetwin, "\n");
    }
}


#ifdef ALLOW_CONTROLSEND
static void monitor_dump_send(void)
{
    if (packetwin != NULL) {
	(void)wattrset(packetwin, A_BOLD);
	(void)wprintw(packetwin, ">>>");
	packet_dump(session.msgbuf, session.msgbuflen);
	(void)wattrset(packetwin, A_NORMAL);
    }
}

bool monitor_control_send(/*@in@*/unsigned char *buf, size_t len)
{
    if (controlfd == -1)
	return false;
    else {
	int savefd;
	ssize_t st;

	if (!serial) {
	    /*@ -sefparams @*/
	    assert(write(controlfd, "!", 1) != -1);
	    assert(write(controlfd, session.gpsdata.dev.path, strlen(session.gpsdata.dev.path)) != -1);
	    assert(write(controlfd, "=", 1) != -1);
	    /*@ +sefparams @*/
	    /*
	     * Ugh...temporarily con the libgpsd layer into using the
	     * socket descriptor.
	     */
	    savefd = session.gpsdata.gps_fd;
	    session.gpsdata.gps_fd = controlfd;
	}

	st = (*active)->driver->control_send(&session, (char *)buf, len);

	if (!serial) {
	    /* stop pretending now */
	    session.gpsdata.gps_fd = controlfd;
	    /* enough room for "ERROR\r\n\0" */
	    /*@ -sefparams @*/
	    assert(read(controlfd, buf, 8) != -1);
	    /*@ +sefparams @*/
	}
	monitor_dump_send();
	return (st != -1);
    }
}

static bool monitor_raw_send(/*@in@*/unsigned char *buf, size_t len)
{
    if (controlfd == -1)
	return false;
    else {
	ssize_t st;

	if (!serial) {
	    /*@ -sefparams @*/
	    assert(write(controlfd, "!", 1) != -1);
	    assert(write(controlfd, session.gpsdata.dev.path,
			 strlen(session.gpsdata.dev.path)) != -1);
	    assert(write(controlfd, "=", 1) != -1);
	    /*@ +sefparams @*/
	}

	st = write(controlfd, (char *)buf, len);

	if (!serial) {
	    /* enough room for "ERROR\r\n\0" */
	    /*@ -sefparams @*/
	    assert(read(controlfd, buf, 8) != -1);
	    /*@ +sefparams @*/
	}
	(void)memcpy(session.msgbuf, buf, len);
	session.msgbuflen = len;
	monitor_dump_send();
	return (st > 0 && (size_t)st == len);
    }
}
#endif /* ALLOW_CONTROLSEND */

/*****************************************************************************
 *
 * Main sequence and display machinery
 *
 *****************************************************************************/

static long tzoffset(void)
{
    time_t now = time(NULL);
    struct tm tm;
    long res = 0;

    tzset();
#ifdef HAVE_TIMEZONE
    res = timezone;
#else
    res = localtime_r(&now, &tm)->tm_gmtoff;
#endif
#ifdef HAVE_DAYLIGHT
    if (daylight != 0 && localtime_r(&now, &tm)->tm_isdst != 0)
	res -= 3600;
#else
    if (localtime_r(&now, &tm)->tm_isdst)
	res -= 3600;
#endif
    return res;
}

static void command(char buf[], size_t len, const char *fmt, ... )
/* assemble command in printf(3) style, use stderr or syslog */
{
    va_list ap;
    ssize_t n;

    va_start(ap, fmt) ;
    (void)vsnprintf(buf, len, fmt, ap);
    va_end(ap);

    /*@i1@*/assert(write(session.gpsdata.gps_fd, buf, strlen(buf)) != -1);
    n = read(session.gpsdata.gps_fd, buf, len);
    if (n >= 0) {
	buf[n] = '\0';
	while (isspace(buf[strlen(buf)-1]))
	    buf[strlen(buf)-1] = '\0';
    }
}

void monitor_complain(const char *fmt, ...)
{
    va_list ap;
    (void)wmove(cmdwin, 0, (int)strlen(type_name)+2);
    (void)wclrtoeol(cmdwin);
    (void)wattrset(cmdwin, A_BOLD | A_BLINK);
    va_start(ap, fmt) ;
    (void)vwprintw(cmdwin, (char *)fmt, ap);
    va_end(ap);
    (void)wattrset(cmdwin, A_NORMAL);
    (void)wrefresh(cmdwin);
    (void)wgetch(cmdwin);
}


void monitor_log(const char *fmt, ...)
{
    if (packetwin != NULL) {
	va_list ap;
	va_start(ap, fmt);
	(void)vwprintw(packetwin, (char *)fmt, ap);
	va_end(ap);
    }
}

static bool switch_type(const struct gps_type_t *devtype)
{
    const struct monitor_object_t **trial, **newobject;
    newobject = NULL;
    for (trial = monitor_objects; *trial; trial++)
	if ((*trial)->driver == devtype)
	    newobject = trial;
    if (newobject) {
	int leftover;
	if (LINES < (*newobject)->min_y + 1 || COLS < (*newobject)->min_x) {
	    monitor_complain("New type requires %dx%d screen",
			     (*newobject)->min_x, (*newobject)->min_y + 1);
	} else {
	    if (active != NULL) {
		(*active)->wrap();
		(void)delwin(devicewin);
	    }
	    active = newobject;
	    devicewin = newwin((*active)->min_y,
			       (*active)->min_x,1,0);
	    if ((devicewin == NULL) || !(*active)->initialize()) {
		monitor_complain("Internal initialization failure - screen "
				 "must be at least 80x24. aborting.");
		return false;
	    }

	    /*@ -onlytrans @*/
	    leftover = LINES-1-(*active)->min_y;
	    if (leftover <= 0) {
		if (packetwin != NULL)
		    (void)delwin(packetwin);
		packetwin = NULL;
	    } else if (packetwin == NULL) {
		packetwin = newwin(leftover, COLS, (*active)->min_y+1, 0);
		(void)scrollok(packetwin, true);
		(void)wsetscrreg(packetwin, 0, leftover-1);
	    } else {
		(void)wresize(packetwin, leftover, COLS);
		(void)mvwin(packetwin, (*active)->min_y+1, 0);
		(void)wsetscrreg(packetwin, 0, leftover-1);
	    }
	    /*@ +onlytrans @*/
	}
	return true;
    }

    monitor_complain("No matching monitor type.");
    return false;
}

static jmp_buf assertbuf;

static void onsig(int sig UNUSED)
{
    longjmp(assertbuf, 1);
}

int main (int argc, char **argv)
{
#ifdef ALLOW_CONTROLSEND
    unsigned int v;
#endif /* ALLOW_CONTROLSEND */
    int option, status, last_type = BAD_PACKET;
    ssize_t len;
    struct fixsource_t source;
    char *p, *controlsock = "/var/run/gpsd.sock";
    fd_set select_set;
    unsigned char buf[BUFLEN];
    char line[80];

    gmt_offset = (int)tzoffset();
    /*@ -observertrans @*/
    (void)putenv("TZ=GMT"); // for ctime()
    /*@ +observertrans @*/
    /*@ -branchstate @*/
    while ((option = getopt(argc, argv, "D:F:Vhl")) != -1) {
	switch (option) {
	case 'D':
	    debuglevel = atoi(optarg);
	    break;
	case 'F':
	    controlsock = optarg;
	    break;
	case 'V':
	    (void)printf("gpsmon %s\n", VERSION);
	    exit(0);
	case 'l':		/* list known device types */
	    (void) fputs("General commands available per type. '+' means there are private commands.\n", stdout);
	    for (active = monitor_objects; *active; active++) {
		(void)fputs("i l q ^S ^Q", stdout);
		(void)fputc(' ', stdout);
#ifdef ALLOW_RECONFIGURE
		if ((*active)->driver->mode_switcher != NULL)
		    (void)fputc('n', stdout);
		else
		    (void)fputc(' ', stdout);
		(void)fputc(' ', stdout);
		if ((*active)->driver->speed_switcher != NULL)
		    (void)fputc('s', stdout);
		else
		    (void)fputc(' ', stdout);
		(void)fputc(' ', stdout);
		if ((*active)->driver->rate_switcher != NULL)
		    (void)fputc('x', stdout);
		else
		    (void)fputc(' ', stdout);
		(void)fputc(' ', stdout);
#endif /* ALLOW_RECONFIGURE */
#ifdef ALLOW_CONTROLSEND
		if ((*active)->driver->control_send != NULL)
		    (void)fputc('x', stdout);
		else
		    (void)fputc(' ', stdout);
#endif /* ALLOW_CONTROLSEND */
		(void)fputc(' ', stdout);
		if ((*active)->command != NULL)
		    (void)fputc('+', stdout);
		else
		    (void)fputc(' ', stdout);
		(void)fputs("\t", stdout);
		(void)fputs((*active)->driver->type_name, stdout);
		(void)fputc('\n', stdout);
	    }
	    exit(0);
	case 'h': case '?': default:
	    (void)fputs("usage:  gpsmon [-?hVl] [-D debuglevel] [-F controlsock] [server[:port:[device]]]\n", stderr);
	    exit(1);
	}
    }
    /*@ +branchstate @*/

    if (optind < argc) {
	gpsd_source_spec(argv[optind], &source);
    } else
	gpsd_source_spec(NULL, &source);

    gpsd_init(&session, &context, NULL);

    /*@ -boolops */
    if (optind>=argc || source.device==NULL || strchr(argv[optind], ':')!=NULL) {

	session.gpsdata.gps_fd = netlib_connectsock(source.server, source.port, "tcp");
	if (session.gpsdata.gps_fd < 0) {
	    (void)fprintf(stderr,
			  "%s: connection failure on %s:%s, error %d = %s.\n",
			  argv[0], source.server, source.port, session.gpsdata.gps_fd, netlib_errstr(session.gpsdata.gps_fd));
	    exit(1);
	}
	controlfd = open(controlsock, O_RDWR);
	/*@ -compdef @*/
	// FIXME: This code needs to become protocol-agnostic
	if (source.device != NULL)
	    command((char *)buf, sizeof(buf), "F=%s\r\n", source.device);
	else
	    command((char *)buf, sizeof(buf), "O\r\n");	/* force device allocation */
	command((char *)buf, sizeof(buf), "F\r\n");
	(void)strlcpy(session.gpsdata.dev.path, (char *)buf+7, PATH_MAX);
	command((char *)buf, sizeof(buf), "R=2\r\n");
	/*@ +compdef @*/
	serial = false;
    } else {
	(void)strlcpy(session.gpsdata.dev.path, argv[optind], PATH_MAX);
	if (gpsd_activate(&session, false) == -1) {
	    gpsd_report(LOG_ERROR,
			  "activation of device %s failed, errno=%d\n",
			  session.gpsdata.dev.path, errno);
	    exit(2);
	}

	controlfd = session.gpsdata.gps_fd;
	serial = true;
    }
    /*@ +boolops */
    /*@ +nullpass +branchstate @*/

    /*
     * This is a monitoring utility. Disable autoprobing, because
     * in some cases (e.g. SiRFs) there is no way to probe a chip
     * type without flipping it to native mode.
     */
    context.readonly = true;

    /* quit cleanly if an assertion fails */
    (void)signal(SIGABRT, onsig);
    if (setjmp(assertbuf) > 0) {
	if (logfile)
	    (void)fclose(logfile);
	(void)endwin();
	(void)fputs("gpsmon: assertion failure, probable I/O error\n", stderr);
	exit(1);
    }

    (void)initscr();
    (void)cbreak();
    (void)noecho();
    (void)intrflush(stdscr, FALSE);
    (void)keypad(stdscr, true);
    curses_active = true;

#define CMDWINHEIGHT	1

    /*@ -onlytrans @*/
    statwin   = newwin(CMDWINHEIGHT, 30, 0, 0);
    cmdwin    = newwin(CMDWINHEIGHT, 0,  0, 30);
    packetwin  = newwin(0, 0,  CMDWINHEIGHT, 0);
    if (statwin==NULL || cmdwin==NULL || packetwin==NULL)
	goto quit;
    (void)scrollok(packetwin, true);
    (void)wsetscrreg(packetwin, 0, LINES-CMDWINHEIGHT);
    /*@ +onlytrans @*/

    (void)wmove(packetwin,0, 0);

    FD_ZERO(&select_set);

    /*@ -observertrans @*/
    for (;;) {
	type_name = session.device_type ? session.device_type->type_name : "Unknown device";
	(void)wattrset(statwin, A_BOLD);
	if (serial)
	    display(statwin, 0, 0, "%s %4d %c %d",
		    session.gpsdata.dev.path,
		    gpsd_get_speed(&session.ttyset),
		    session.gpsdata.dev.parity,
		    session.gpsdata.dev.stopbits);
	else
	    /*@ -nullpass @*/
	    display(statwin, 0, 0, "%s:%s:%s",
		    source.server, source.port, session.gpsdata.dev.path);
	    /*@ +nullpass @*/
	(void)wattrset(statwin, A_NORMAL);
	(void)wmove(cmdwin, 0,0);

	/* get a packet -- calls gpsd_poll() */
	if ((len = readpkt()) > 0 && session.packet.outbuflen > 0) {
	    /* switch types on packet receipt */
	    /*@ -nullpass */
	    if (session.packet.type != last_type) {
		last_type = session.packet.type;
		if (!switch_type(session.device_type))
		    goto quit;
	    }
	    /*@ +nullpass */

	    /* refresh all windows */
	    (void)wprintw(cmdwin, type_name);
	    (void)wprintw(cmdwin, "> ");
	    (void)wclrtoeol(cmdwin);
	    if (active != NULL && len > 0 && session.packet.outbuflen > 0)
		(*active)->update();
	    (void)wprintw(packetwin, "(%d) ", session.packet.outbuflen);
	    packet_dump((char *)session.packet.outbuffer,session.packet.outbuflen);
	    (void)wnoutrefresh(statwin);
	    (void)wnoutrefresh(cmdwin);
	    if (devicewin != NULL)
		(void)wnoutrefresh(devicewin);
	    if (packetwin != NULL)
		(void)wnoutrefresh(packetwin);
	    (void)doupdate();
	}

	/* rest of this invoked only if user has pressed a key */
	FD_SET(0,&select_set);
	FD_SET(session.gpsdata.gps_fd,&select_set);

	if (select(FD_SETSIZE, &select_set, NULL, NULL, NULL) == -1)
	    break;

	if (FD_ISSET(0,&select_set)) {
	    char *arg;
	    (void)wmove(cmdwin, 0,(int)strlen(type_name)+2);
	    (void)wrefresh(cmdwin);
	    (void)echo();
	    /*@ -usedef -compdef @*/
	    (void)wgetnstr(cmdwin, line, 80);
	    (void)noecho();
	    if (packetwin != NULL)
		(void)wrefresh(packetwin);
	    (void)wrefresh(cmdwin);

	    if ((p = strchr(line,'\r')) != NULL)
		*p = '\0';

	    if (line[0] == '\0')
		continue;
	    /*@ +usedef +compdef @*/

	    arg = line;
	    if (isspace(line[1])) {
		for (arg = line+2; *arg != '\0' && isspace(*arg); arg++)
		    arg++;
		arg++;
	    } else
		arg = line + 1;

	    if (active != NULL && (*active)->command != NULL) {
		status = (*active)->command(line);
		if (status == COMMAND_TERMINATE)
		    goto quit;
		else if (status == COMMAND_MATCH)
		    continue;
		assert(status == COMMAND_UNKNOWN);
	    }
	    switch (line[0])
	    {
#ifdef ALLOW_RECONFIGURE
	    case 'c':				/* change cycle time */
		if (active == NULL)
		    monitor_complain("No device defined yet");
		else if (serial) {
		    double rate = strtod(arg, NULL);
		    /* Ugh...should have a controlfd slot
		     * in the session structure, really
		     */
		    if ((*active)->driver->rate_switcher) {
			int dfd = session.gpsdata.gps_fd;
			session.gpsdata.gps_fd = controlfd;
			if ((*active)->driver->rate_switcher(&session, rate)) {
			    monitor_dump_send();
			} else
			    monitor_complain("Rate not supported.");
			session.gpsdata.gps_fd = dfd;
		    } else
			monitor_complain("Device type has no rate switcher");
		} else {
		    line[0] = 'c';
		    /*@ -sefparams @*/
		    assert(write(session.gpsdata.gps_fd, line, strlen(line)) != -1);
		    /* discard response */
		    assert(read(session.gpsdata.gps_fd, buf, sizeof(buf)) != -1);
		    /*@ +sefparams @*/
		}
		break;
#endif /* ALLOW_RECONFIGURE */

	    case 'i':				/* start probing for subtype */
		if (active == NULL)
		    monitor_complain("No GPS type detected.");
		else {
		    if (strcspn(line, "01") == strlen(line))
			context.readonly = !context.readonly;
		    else
			context.readonly = (atoi(line+1) == 0);
		    (void)gpsd_switch_driver(&session,
					     (*active)->driver->type_name);
		}
		break;

	    case 'l':				/* open logfile */
		if (logfile != NULL) {
		    if (packetwin != NULL)
			(void)wprintw(packetwin, ">>> Logging to %s off", logfile);
		    (void)fclose(logfile);
		}

		if ((logfile = fopen(line+1,"a")) != NULL)
		    if (packetwin != NULL)
			(void)wprintw(packetwin, ">>> Logging to %s on", logfile);
		break;

#ifdef ALLOW_RECONFIGURE
	    case 'n':		/* change mode */
		/* if argument not specified, toggle */
		if (strcspn(line, "01") == strlen(line))
		    v = (unsigned int)TEXTUAL_PACKET_TYPE(session.packet.type);
		else
		    v = (unsigned)atoi(line+1);
		if (active == NULL)
		    monitor_complain("No device defined yet");
		else if (serial) {
		    // FIXME: some sort of debug window display here?
		    /* Ugh...should have a controlfd slot
		     * in the session structure, really
		     */
		    if ((*active)->driver->mode_switcher) {
			int dfd = session.gpsdata.gps_fd;
			session.gpsdata.gps_fd = controlfd;
			(*active)->driver->mode_switcher(&session, (int)v);
			monitor_dump_send();
			(void)tcdrain(session.gpsdata.gps_fd);
			(void)usleep(50000);
			session.gpsdata.gps_fd = dfd;
		    } else
			monitor_complain("Device type has no mode switcher");
		} else {
		    line[0] = 'n';
		    line[1] = ' ';
		    line[2] = '0' + v;
		    /*@ -sefparams @*/
		    assert(write(session.gpsdata.gps_fd, line, strlen(line)) != -1);
		    /* discard response */
		    assert(read(session.gpsdata.gps_fd, buf, sizeof(buf)) != -1);
		    /*@ +sefparams @*/
		}
		break;
#endif /* ALLOW_RECONFIGURE */

	    case 'q':				/* quit */
		goto quit;

#ifdef ALLOW_RECONFIGURE
	    case 's':				/* change speed */
		if (active == NULL)
		    monitor_complain("No device defined yet");
		else if (serial) {
		    speed_t speed;
		    char parity = (char)session.gpsdata.dev.parity;
		    unsigned int stopbits = session.gpsdata.dev.stopbits;
		    char *modespec;

		    modespec = strchr(arg, ':');
		    /*@ +charint @*/
		    if (modespec!=NULL) {
			if (strchr("78", *++modespec) == NULL) {
			    monitor_complain("No support for that word length.");
			    break;
			}
			parity = *++modespec;
			if (strchr("NOE", parity) == NULL) {
			    monitor_complain("What parity is '%c'?.", parity);
			    break;
			}
			stopbits = (unsigned int)*++modespec;
			if (strchr("12", (char)stopbits) == NULL) {
			    monitor_complain("Stop bits must be 1 or 2.");
			    break;
			}
			stopbits = (unsigned int)(stopbits-'0');
		    }
		    /*@ -charint @*/
		    speed = (unsigned)atoi(arg);
		    /* Ugh...should have a controlfd slot
		     * in the session structure, really
		     */
		    if ((*active)->driver->speed_switcher) {
			int dfd = session.gpsdata.gps_fd;
			session.gpsdata.gps_fd = controlfd;
			if ((*active)->driver->speed_switcher(&session,
								speed,
								parity,
							      (int)stopbits)) {
			    monitor_dump_send();
			    /*
			     * See the comment attached to the 'B'
			     * command in gpsd.  Allow the control
			     * string time to register at the GPS
			     * before we do the baud rate switch,
			     * which effectively trashes the UART's
			     * buffer.
			     */
			    (void)tcdrain(session.gpsdata.gps_fd);
			    (void)usleep(50000);
			    (void)gpsd_set_speed(&session, speed,
						 (unsigned char)parity,
						 stopbits);
			} else
			    monitor_complain("Speed/mode cobination not supported.");
			session.gpsdata.gps_fd = dfd;
		    } else
			monitor_complain("Device type has no speed switcher");
		} else {
		    line[0] = 'b';
		    /*@ -sefparams @*/
		    assert(write(session.gpsdata.gps_fd, line, strlen(line)) != -1);
		    /* discard response */
		    assert(read(session.gpsdata.gps_fd, buf, sizeof(buf)) != -1);
		    /*@ +sefparams @*/
		}
		break;
#endif /* ALLOW_RECONFIGURE */

	    case 't':				/* force device type */
		if (strlen(arg) > 0) {
		    int matchcount = 0;
		    const struct gps_type_t **dp, *forcetype = NULL;
		    for (dp = gpsd_drivers; *dp; dp++) {
			if (strstr((*dp)->type_name, arg) != NULL) {
			    forcetype = *dp;
			    matchcount++;
			}
		    }
		    if (matchcount == 0) {
			monitor_complain("No driver type matches '%s'.", arg);
		    } else if (matchcount == 1) {
			assert(forcetype != NULL);
			if (switch_type(forcetype))
			    (int)gpsd_switch_driver(&session, forcetype->type_name);
		    } else {
			monitor_complain("Multiple driver type names match '%s'.", arg);
		    }
		}
		break;

#ifdef ALLOW_CONTROLSEND
	    case 'x':				/* send control packet */
		if (active == NULL)
		    monitor_complain("No device defined yet");
		else {
		    /*@ -compdef @*/
		    int st = gpsd_hexpack(arg, (char*)buf, strlen(arg));
		    if (st < 0)
			monitor_complain("Invalid hex string (error %d)", st);
		    else if ((*active)->driver->control_send == NULL)
			monitor_complain("Device type has no control-send method.");
		    else if (!monitor_control_send(buf, (size_t)st))
			monitor_complain("Control send failed.");
		}
		    /*@ +compdef @*/
		break;

	    case 'X':				/* send raw packet */
		/*@ -compdef @*/
		len = (ssize_t)gpsd_hexpack(arg, (char*)buf, strlen(arg));
		if (len < 0)
		    monitor_complain("Invalid hex string (error %d)", len);
		else if (!monitor_raw_send(buf, (size_t)len))
		    monitor_complain("Raw send failed.");
		/*@ +compdef @*/
		break;
#endif /* ALLOW_CONTROLSEND */

	    default:
		monitor_complain("Unknown command");
		break;
	    }
	}
    }
    /*@ +nullpass @*/
    /*@ +observertrans @*/

 quit:
    gpsd_close(&session);
    if (logfile)
	(void)fclose(logfile);
    (void)endwin();
    exit(0);
}

/* gpsmon.c ends here */
