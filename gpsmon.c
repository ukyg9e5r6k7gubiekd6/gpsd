/* $Id$ */
/*
 * GPS packet monitor
 *
 * A Useful commands:
 *	l -- toggle packet logging
 *	b -- change baud rate.
 *      n -- change from native t o binary mode orr vice-versa.
 *	s -- send hex bytes to device.
 *	q -- quit, leaving device in binary mode.
 *      Ctrl-S -- freeze display.
 *      Ctrl-Q -- unfreeze display.
 *
 * There may be chipset-specific commands associated with driver method 
 * tables, as well.
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

#include "gpsmon.h"

#ifdef S_SPLINT_S
extern struct tm *localtime_r(const time_t *,/*@out@*/struct tm *tp)/*@modifies tp@*/;
#endif /* S_SPLINT_S */

extern int netlib_connectsock(const char *, const char *, const char *);

#define BUFLEN		2048

/* These are public */
struct gps_context_t	context;
struct gps_device_t	session;
WINDOW *devicewin, *debugwin;
int gmt_offset;

/* These are private */
static int controlfd = -1;
static bool serial, curses_active;
static int debuglevel = 0;
static WINDOW *statwin, *cmdwin;
static FILE *logfile;
static char *type_name;

#define display	(void)mvwprintw

/* external capability tables */
extern struct mdevice_t sirf_mdt;

static void fixframe(WINDOW *win)
{
    int ymax, xmax, ycur, xcur;
    getyx(win, ycur, xcur);
    getmaxyx(win, ymax, xmax);
    (void)mvwaddch(win, ycur, xmax-1, ACS_VLINE);
}

/******************************************************************************
 *
 * The NMEA driver, for generic NMEA devices.
 *
 ******************************************************************************/

extern const struct gps_type_t nmea;

static WINDOW *nmeawin, *satwin, *gprmcwin, *gpggawin, *gpgsawin;
static clock_t last_tick, tick_interval;

#define SENTENCELINE 1

static bool nmea_windows(void)
{
    int i;

    /*@ -onlytrans @*/
    nmeawin = derwin(devicewin, 3,  80, 0, 0);
    (void)wborder(nmeawin, 0, 0, 0, 0, 0, 0, 0, 0);
    (void)syncok(nmeawin, true);

    wattrset(nmeawin, A_BOLD);
    mvwaddstr(nmeawin, 2, 34, " Sentences ");
    wattrset(nmeawin, A_NORMAL);
    /*@ +onlytrans @*/

    satwin  = derwin(devicewin, 15, 20, 3, 0);
    (void)wborder(satwin, 0, 0, 0, 0, 0, 0, 0, 0),
    (void)syncok(satwin, true);
    (void)wattrset(satwin, A_BOLD);
    display(satwin, 1, 1, " Ch SV  Az El S/N");
    for (i = 0; i < SIRF_CHANNELS; i++)
	display(satwin, (int)(i+2), 1, "%2d",i);
    display(satwin, 14, 7, " GSV ");
    (void)wattrset(satwin, A_NORMAL);

    gprmcwin  = derwin(devicewin, 7, 30, 3, 20);
    (void)wborder(gprmcwin, 0, 0, 0, 0, 0, 0, 0, 0),
    (void)syncok(gprmcwin, true);
    (void)wattrset(gprmcwin, A_BOLD);
    display(gprmcwin, 1, 1, "Time: ");
    display(gprmcwin, 2, 1, "Latitude: ");
    display(gprmcwin, 3, 1, "Longitude: ");
    display(gprmcwin, 4, 1, "Speed: ");
    display(gprmcwin, 5, 1, "Course: ");
    display(gprmcwin, 6, 12, " RMC ");
    (void)wattrset(gprmcwin, A_NORMAL);

    gpggawin  = derwin(devicewin, 3, 30, 10, 20);
    (void)wborder(gpggawin, 0, 0, 0, 0, 0, 0, 0, 0),
    (void)syncok(gpggawin, true);
    (void)wattrset(gpggawin, A_BOLD);
    display(gpggawin, 1, 1, "Altitude: ");
    display(gpggawin, 2, 12, " GGA ");
    (void)wattrset(gpggawin, A_NORMAL);

    gpgsawin  = derwin(devicewin, 4, 30, 13, 20);
    (void)wborder(gpgsawin, 0, 0, 0, 0, 0, 0, 0, 0),
    (void)syncok(gpgsawin, true);
    (void)wattrset(gpgsawin, A_BOLD);
    display(gpgsawin, 1, 1, "Mode: ");
    display(gpgsawin, 2, 1, "Sats: ");
    display(gpgsawin, 1, 12, "HDOP: ");
    display(gpgsawin, 3, 12, " GSA ");
    (void)wattrset(gpgsawin, A_NORMAL);

    last_tick = timestamp();

    return (nmeawin != NULL);
}

/*@ -globstate */
static void nmea_update(size_t len)
{
    if (len > 0 && session.packet.outbuflen > 0)
    {
	static char sentences[NMEA_MAX];

	if (session.packet.outbuffer[0] == '$') {
	    int ymax, xmax;
	    double now;
	    char newid[NMEA_MAX];
	    getmaxyx(nmeawin, ymax, xmax);
	    (void)strlcpy(newid, (char *)session.packet.outbuffer+1, 
			  strcspn((char *)session.packet.outbuffer+1, ",")+1);
	    if (strstr(sentences, newid) == NULL) {
		char *s_end = sentences + strlen(sentences);
		if (strlen(sentences) + strlen(newid) < xmax-2) {
		    *s_end++ = ' '; 
		    (void)strcpy(s_end, newid);
		} else {
		    *--s_end = '.';
		    *--s_end = '.';
		    *--s_end = '.';
		}
	        mvwaddstr(nmeawin, SENTENCELINE, 1, sentences);
	    }

	    /* 
	     * If the interval between this and last update is 
	     * the longest we've seen yet, boldify the corresponding
	     * tag.
	     */
	    now = timestamp();
	    if (now > last_tick && (now - last_tick) > tick_interval)
	    {
		char *findme = strstr(sentences, newid);

		tick_interval = now - last_tick;
		if (findme != NULL) {
		    mvwchgat(nmeawin, SENTENCELINE, 1, xmax-13, A_NORMAL, 0, NULL);
		    mvwchgat(nmeawin, 
		    	 SENTENCELINE, 1+(findme-sentences), 
		    	 strlen(newid),
		    	 A_BOLD, 0, NULL);
		}
	    }
	    last_tick = now;

	    if (strcmp(newid, "GPGSV") == 0) {
		int i;

		for (i = 0; i < session.gpsdata.satellites; i++) {
		    (void)wmove(satwin, i+2, 3);
		    (void)wprintw(satwin, " %3d %3d%3d %3d", 
				  session.gpsdata.PRN[i],
				  session.gpsdata.azimuth[i],
				  session.gpsdata.elevation[i],
				  session.gpsdata.ss[i]);
		}
	    }

	    if (strcmp(newid, "GPRMC") == 0) {
		char scr[128];
		if (isnan(session.gpsdata.fix.time)==0) {
		    (void)unix_to_iso8601(session.gpsdata.fix.time, scr, sizeof(scr));
		} else
		    (void)snprintf(scr, sizeof(scr), "n/a");
		(void)mvwaddstr(gprmcwin, 1, 7, scr);


		/* Fill in the latitude. */
		if (session.gpsdata.fix.mode >= MODE_2D && isnan(session.gpsdata.fix.latitude)==0) {
		    (void)snprintf(scr, sizeof(scr), "%s %c", 
				   deg_to_str(deg_ddmmss,  fabs(session.gpsdata.fix.latitude)), 
				   (session.gpsdata.fix.latitude < 0) ? 'S' : 'N');
		} else
		    (void)snprintf(scr, sizeof(scr), "n/a");
		(void)mvwprintw(gprmcwin, 2, 11, "%-17s", scr);

		/* Fill in the longitude. */
		if (session.gpsdata.fix.mode >= MODE_2D && isnan(session.gpsdata.fix.longitude)==0) {
		    (void)snprintf(scr, sizeof(scr), "%s %c", 
				   deg_to_str(deg_ddmmss,  fabs(session.gpsdata.fix.longitude)), 
				   (session.gpsdata.fix.longitude < 0) ? 'W' : 'E');
		} else
		    (void)snprintf(scr, sizeof(scr), "n/a");
		(void)mvwprintw(gprmcwin, 3, 11, "%-17s", scr);

		/* Fill in the speed. */
		if (session.gpsdata.fix.mode >= MODE_2D && isnan(session.gpsdata.fix.track)==0)
		    (void)snprintf(scr, sizeof(scr), "%.1f meters/sec", session.gpsdata.fix.speed);
		else
		    (void)snprintf(scr, sizeof(scr), "n/a");
		(void)mvwprintw(gprmcwin, 4, 11, "%-17s", scr);

		if (session.gpsdata.fix.mode >= MODE_2D && isnan(session.gpsdata.fix.track)==0)
		    (void)snprintf(scr, sizeof(scr), "%.1f deg", session.gpsdata.fix.track);
		else
		    (void)snprintf(scr, sizeof(scr), "n/a");
		(void)mvwprintw(gprmcwin, 5, 11, "%-17s", scr);
	    }

	    if (strcmp(newid, "GPGGA") == 0) {
		char scr[128];
		/* Fill in the altitude. */
		if (session.gpsdata.fix.mode == MODE_3D && isnan(session.gpsdata.fix.altitude)==0)
		    (void)snprintf(scr, sizeof(scr), "%.1f meters",session.gpsdata.fix.altitude);
		else
		    (void)snprintf(scr, sizeof(scr), "n/a");
		(void)mvwprintw(gpggawin, 1, 11, "%-17s", scr);
	    }

	    if (strcmp(newid, "GPGSA") == 0) {
		int i;
		(void)mvwprintw(gpgsawin, 1,7, "%1d", session.gpsdata.fix.mode);
		(void)wmove(gpgsawin, 2, 7);
		(void)wclrtoeol(gpgsawin);
		for (i = 0; i < session.gpsdata.satellites_used; i++) {
		    (void)wprintw(gpgsawin, "%d ", session.gpsdata.used[i]);
		}
		fixframe(gpgsawin);
		(void)wmove(gpgsawin, 1, 18); 
		(void)wclrtoeol(gpgsawin);
		(void)wprintw(gpgsawin, "%2.2f", session.gpsdata.hdop);
		fixframe(gpgsawin);
		//FIXME: VDOP and PDOP too, when we get a GPS that reports 'em.
	    }
	}
    }
}
/*@ +globstate */

#undef SENTENCELINE

static void nmea_wrap(void)
{
    (void)delwin(nmeawin);
}

const struct mdevice_t nmea_mdt = {
    .initialize = nmea_windows,
    .update = nmea_update,
    .command = NULL,
    .wrap = nmea_wrap,
    .min_y = 18, .min_x = 80,
    .driver = &nmea,
};

const struct mdevice_t *drivers[] = {
    &nmea_mdt,
    &sirf_mdt,
    NULL,
};
const struct mdevice_t **active;

/******************************************************************************
 *
 * Device-independent I/O routines
 *
 ******************************************************************************/

void gpsd_report(int errlevel UNUSED, const char *fmt, ... )
/* our version of the logger */
{
    if (errlevel <= debuglevel) {
	va_list ap;
	va_start(ap, fmt);
	if (!curses_active)
	    (void)vprintf(fmt, ap);
	else
	    (void)wprintw(debugwin, fmt, ap);
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
    if (select(session.gpsdata.gps_fd + 1,&select_set,NULL,NULL,&timeval) < 0)
	return EOF;

    if (!FD_ISSET(session.gpsdata.gps_fd,&select_set))
	return EOF;

    (void)usleep(100000);

    changed = gpsd_poll(&session);
    if (changed & ERROR_SET)
	return EOF;

    if (logfile != NULL) {
	/*@ -shiftimplementation -sefparams +charint @*/
	assert(fwrite(session.packet.outbuffer, 
		      sizeof(char), session.packet.outbuflen, 
		      logfile) >= 1);
	/*@ +shiftimplementation +sefparams -charint @*/
    }
    return session.packet.outbuflen;
    /*@ +type +shiftnegative +compdef +nullpass @*/
}
/*@ +globstate @*/

static void packet_dump(char *buf, size_t buflen)
{
    size_t i;
    bool printable = true;
    for (i = 0; i < buflen; i++)
	if (!isprint(buf[i]) && !isspace(buf[i]))
	    printable = false;
    if (printable) {
	for (i = 0; i < buflen; i++)
	    if (isprint(buf[i]))
		(void)waddch(debugwin, (chtype)buf[i]);
	    else
		(void)wprintw(debugwin, "\\x%02x", (unsigned char)buf[i]);
    } else {
	for (i = 0; i < buflen; i++)
	    (void)wprintw(debugwin, "%02x", (unsigned char)buf[i]);
    }
    (void)wprintw(debugwin, "\n");
}


static void monitor_dump_send(void)
{
    (void)wattrset(debugwin, A_BOLD);
    (void)wprintw(debugwin, ">>>");
    packet_dump(session.msgbuf, session.msgbuflen);
    (void)wattrset(debugwin, A_NORMAL);
}

bool monitor_control_send(/*@in@*/unsigned char *buf, size_t len)
{
    monitor_dump_send();

    if (controlfd == -1) 
	return false;
    else {
	int savefd;
	ssize_t st;

	if (!serial) {
	    /*@ -sefparams @*/
	    assert(write(controlfd, "!", 1) != -1);
	    assert(write(controlfd, session.gpsdata.gps_device, strlen(session.gpsdata.gps_device)) != -1);
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
	return ((size_t)st == len);
    }
}

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

static void error_and_pause(const char *fmt, ...) 
{
    va_list ap;
    va_start(ap, fmt);
    (void)wmove(cmdwin, 0, strlen(type_name)+2);
    (void)wclrtoeol(cmdwin);
    (void)wattrset(cmdwin, A_BOLD | A_BLINK);
    (void)wprintw(cmdwin, fmt, ap);
    (void)wattrset(cmdwin, A_NORMAL);
    (void)wrefresh(cmdwin);
    (void)wgetch(cmdwin);
    va_end(ap);
}

static jmp_buf assertbuf;

static void onsig(int sig UNUSED)
{
    longjmp(assertbuf, 1);
}

int main (int argc, char **argv)
{
    unsigned int v;
    int option, status, last_type = BAD_PACKET;
    ssize_t len;
    char *p, *arg = NULL, *colon1 = NULL, *colon2 = NULL, *slash = NULL;
    char *server=NULL, *port = DEFAULT_GPSD_PORT, *device = NULL;
    char *controlsock = "/var/run/gpsd.sock";
    fd_set select_set;
    unsigned char buf[BUFLEN];
    char line[80];

    gmt_offset = (int)tzoffset();

    /*@ -branchstate @*/
    while ((option = getopt(argc, argv, "D:F:Vh")) != -1) {
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
	case 'h': case '?': default:
	    (void)fputs("usage:  gpsmon [-?hv] [-F controlsock] [server[:port:[device]]]\n", stderr);
	    exit(1);
	}
    }
    /*@ +branchstate @*/

    /*@ -nullpass -branchstate -compdef @*/
    if (optind < argc) {
	arg = strdup(argv[optind]);
	colon1 = strchr(arg, ':');
	slash = strchr(arg, '/');
	server = arg;
	if (colon1 != NULL) {
	    if (colon1 == arg)
		server = NULL;
	    else
		*colon1 = '\0';
	    port = colon1 + 1;
	    colon2 = strchr(port, ':');
	    if (colon2 != NULL) {
		if (colon2 == port)
		    port = NULL;
	        else
		    *colon2 = '\0';
		device = colon2 + 1;
	    }
	}
    }

    gpsd_init(&session, &context, NULL);

    /*@ -boolops */
    if (!arg || (arg && !slash) || (arg && colon1 && slash)) {	
	if (!server)
	    server = "127.0.0.1";
	if (!port)
	    port = DEFAULT_GPSD_PORT;
	session.gpsdata.gps_fd = netlib_connectsock(server, port, "tcp");
	if (session.gpsdata.gps_fd < 0) {
	    (void)fprintf(stderr, 
			  "%s: connection failure on %s:%s, error %d.\n", 
			  argv[0], server, port, session.gpsdata.gps_fd);
	    exit(1);
	}
	controlfd = open(controlsock, O_RDWR);
	/*@ -compdef @*/
	if (device)
	    command((char *)buf, sizeof(buf), "F=%s\r\n", device);
	else
	    command((char *)buf, sizeof(buf), "O\r\n");	/* force device allocation */
	command((char *)buf, sizeof(buf), "F\r\n");
	(void)strlcpy(session.gpsdata.gps_device, (char *)buf+7, PATH_MAX);
	command((char *)buf, sizeof(buf), "R=2\r\n");
	/*@ +compdef @*/
	serial = false;
    } else {
	(void)strlcpy(session.gpsdata.gps_device, arg, PATH_MAX);
	if (gpsd_activate(&session, false) == -1) {
	    gpsd_report(LOG_ERROR,
			  "activation of device %s failed, errno=%d\n",
			  session.gpsdata.gps_device, errno);
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

    /*@ -onlytrans @*/
    statwin   = newwin(1, 30, 0, 0);
    cmdwin    = newwin(1, 0,  0, 30);
    debugwin  = newwin(0, 0,  1, 0);
    if (statwin==NULL || cmdwin==NULL || debugwin==NULL)
	goto quit;
    (void)scrollok(debugwin, true);
    (void)wsetscrreg(debugwin, 0, LINES-1);
    /*@ +onlytrans @*/

    (void)wmove(debugwin,0, 0);

    FD_ZERO(&select_set);

    for (;;) {
	type_name = active ? (*active)->driver->type_name : "Unknown device";
	(void)wattrset(statwin, A_BOLD);
	if (serial)
	    display(statwin, 0, 0, "%s %4d %c %d", 
		    session.gpsdata.gps_device, 
		    gpsd_get_speed(&session.ttyset),
		    session.gpsdata.parity, 
		    session.gpsdata.stopbits);
	else
	    /*@ -nullpass @*/
	    display(statwin, 0, 0, "%s:%s:%s", 
		    server, port, session.gpsdata.gps_device);
	    /*@ +nullpass @*/
	(void)wattrset(statwin, A_NORMAL);
	(void)wmove(cmdwin, 0,0);

	/* get a packet -- calls gpsd_poll */
	if ((len = readpkt()) > 0 && session.packet.outbuflen > 0) {
	    (void)wprintw(debugwin, "(%d) ", session.packet.outbuflen);
	    packet_dump((char *)session.packet.outbuffer,session.packet.outbuflen);
	}

	/* switch types on packet receipt */
	if (session.packet.type != last_type) {
	    const struct mdevice_t **trial, **newdriver;
	    last_type = session.packet.type;
	    newdriver = NULL;
	    for (trial = drivers; *trial; trial++)
		if ((*trial)->driver == session.device_type)
		    newdriver = trial;
	    if (newdriver) {
		if (active != NULL) {
		    (*active)->wrap();
		    (void)delwin(devicewin);
		}
		active = newdriver;
		devicewin = newwin((*active)->min_y+1, (*active)->min_x+1,1,0);
		if (!(*active)->initialize())
		    goto quit;
		(void)wresize(debugwin, LINES-(*active)->min_y-1, 80);
		(void)mvwin(debugwin, (*active)->min_y+1, 0);
		(void)wsetscrreg(debugwin, 0, LINES-(*active)->min_y-2);
	    }
	}

	/* refresh all windows */
	(void)wprintw(cmdwin, type_name);
	(void)wprintw(cmdwin, "> ");
	(void)wclrtoeol(cmdwin);
	if (active != NULL)
	    (*active)->update(len);
	(void)wnoutrefresh(statwin);
	(void)wnoutrefresh(cmdwin);
	if (devicewin != 0)
	    (void)wnoutrefresh(devicewin);
	(void)wnoutrefresh(debugwin);
	(void)doupdate();

	/* rest of this invoked only if user has pressed a key */
	FD_SET(0,&select_set);
	FD_SET(session.gpsdata.gps_fd,&select_set);

	if (select(FD_SETSIZE, &select_set, NULL, NULL, NULL) < 0)
	    break;

	if (FD_ISSET(0,&select_set)) {
	    (void)wmove(cmdwin, 0,strlen((*active)->driver->type_name)+2);
	    (void)wrefresh(cmdwin);
	    (void)echo();
	    /*@ -usedef -compdef @*/
	    (void)wgetnstr(cmdwin, line, 80);
	    (void)noecho();
	    (void)wrefresh(debugwin);
	    (void)wrefresh(cmdwin);

	    if ((p = strchr(line,'\r')) != NULL)
		*p = '\0';

	    if (line[0] == '\0')
		continue;
	    /*@ +usedef +compdef @*/

	    p = line;

	    while (*p != '\0' && !isspace(*p))
		p++;
	    while (*p != '\0' && isspace(*p))
		p++;

	    if ((*active)->command != NULL) {
		status = (*active)->command(line);
		if (status == COMMAND_TERMINATE)
		    goto quit;
		else if (status == COMMAND_MATCH)
		    continue;
		assert(status == COMMAND_UNKNOWN);
	    }
	    switch (line[0])
	    {
	    case 'b':
		monitor_dump_send();
		if (active == NULL)
		    error_and_pause("No device defined yet");
		else if (serial) {
		    v = (unsigned)atoi(line+1);
		    /* Ugh...should have a controlfd slot 
		     * in the session structure, really
		     */
		    if ((*active)->driver->speed_switcher) {
			int dfd = session.gpsdata.gps_fd;
			session.gpsdata.gps_fd = controlfd;
			(void)(*active)->driver->speed_switcher(&session, v);
			/*
			 * See the comment attached to the 'B' command in gpsd.
			 * Allow the control string time to register at the
			 * GPS before we do the baud rate switch, which
			 * effectively trashes the UART's buffer.
			 */
			(void)tcdrain(session.gpsdata.gps_fd);
			(void)usleep(50000);
			session.gpsdata.gps_fd = dfd;
			(void)gpsd_set_speed(&session, v, 
					     (unsigned char)session.gpsdata.parity, 
					 session.gpsdata.stopbits);
		    } else
			error_and_pause("Device type has no speed switcher");
		} else {
		    line[0] = 'b';
		    /*@ -sefparams @*/
		    assert(write(session.gpsdata.gps_fd, line, strlen(line)) != -1);
		    /* discard response */
		    assert(read(session.gpsdata.gps_fd, buf, sizeof(buf)) != -1);
		    /*@ +sefparams @*/
		}
		break;

	    case 'n':
		/* if argument not specified, toggle */
		if (strcspn(line, "01") == strlen(line))
		    v = (unsigned int)TEXTUAL_PACKET_TYPE(session.packet.type);
		else
		    v = (unsigned)atoi(line+1);
		if (active == NULL)
		    error_and_pause("No device defined yet");
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
			error_and_pause("Device type has no mode switcher");
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

	    case 'i':				/* start probing for subtype */
		if (active == NULL)
		    error_and_pause("No driver active");
		else {
		    context.readonly = false;
		    (void)gpsd_switch_driver(&session, 
					     (*active)->driver->type_name);
		}
		break;

	    case 'l':				/* open logfile */
		if (logfile != NULL) {
		    (void)wprintw(debugwin, ">>> Logging to %s off", logfile);
		    (void)fclose(logfile);
		}

		if ((logfile = fopen(line+1,"a")) != NULL)
		    (void)wprintw(debugwin, ">>> Logging to %s on", logfile);
		break;

	    case 'q':
		goto quit;

	    case 's':
		len = 0;
		/*@ -compdef @*/
		while (*p != '\0')
		{
		    (void)sscanf(p,"%x",&v);
		    putbyte(buf, len, v);
		    len++;
		    while (*p != '\0' && !isspace(*p))
			p++;
		    while (*p != '\0' && isspace(*p))
			p++;
		}
		if (active == NULL)
		    error_and_pause("No device defined yet");
		else if ((*active)->driver->control_send != NULL)
		    (void)monitor_control_send(buf, (size_t)len);
		else
		    error_and_pause("Device type has no control-send method.");
		/*@ +compdef @*/
		break;
	    }
	}
    }
    /*@ +nullpass @*/

 quit:
    gpsd_close(&session);
    if (logfile)
	(void)fclose(logfile);
    (void)endwin();
    exit(0);
}

/* gpsmon.c ends here */
