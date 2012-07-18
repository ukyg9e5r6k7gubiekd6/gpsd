/*
 * The generic GPS packet monitor.
 *
 * This file is Copyright (c) 2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <setjmp.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <time.h>
#ifdef HAVE_TERMIOS_H
#include <termios.h>
#endif /* HAVE_TERMIOS_H */
#include <sys/time.h>		/* expected to declare select(2) a la SuS */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#ifndef S_SPLINT_S
#include <unistd.h>
#endif /* S_SPLINT_S */
#include <curses.h>

#include "gpsd_config.h"
#ifdef HAVE_BLUEZ
#include <bluetooth/bluetooth.h>
#endif
#include "gpsd.h"
#include "gpsdclient.h"
#include "gpsmon.h"
#include "revision.h"

#define BUFLEN		2048

/* external capability tables */
extern struct monitor_object_t nmea_mmt, sirf_mmt, ashtech_mmt;
extern struct monitor_object_t garmin_mmt, garmin_bin_ser_mmt;
extern struct monitor_object_t italk_mmt, ubx_mmt, superstar2_mmt;
extern struct monitor_object_t fv18_mmt, gpsclock_mmt, mtk3301_mmt;
extern struct monitor_object_t oncore_mmt, tnt_mmt;

/* These are public */
struct gps_device_t session;
WINDOW *devicewin;
bool serial;

/* These are private */
static struct gps_context_t context;
static bool curses_active;
static WINDOW *statwin, *cmdwin;
/*@null@*/ static WINDOW *packetwin;
/*@null@*/ static FILE *logfile;
static char *type_name;

#ifdef PASSTHROUGH_ENABLE
/* no methods, it's all device window */
extern const struct gps_type_t json_passthrough;
const struct monitor_object_t json_mmt = {
    .initialize = NULL,
    .update = NULL,
    .command = NULL,
    .wrap = NULL,
    .min_y = 0, .min_x = 80,	/* no need for a device window */
    .driver = &json_passthrough,
};
#endif /* PASSTHROUGH_ENABLE */

/*@ -nullassign @*/
static const struct monitor_object_t *monitor_objects[] = {
#ifdef NMEA_ENABLE
    &nmea_mmt,
#if defined(GARMIN_ENABLE) && defined(NMEA_ENABLE)
    &garmin_mmt,
#endif /* GARMIN_ENABLE && NMEA_ENABLE */
#if defined(GARMIN_ENABLE) && defined(BINARY_ENABLE)
    &garmin_bin_ser_mmt,
#endif /* defined(GARMIN_ENABLE) && defined(BINARY_ENABLE) */
#ifdef ASHTECH_ENABLE
    &ashtech_mmt,
#endif /* ASHTECH_ENABLE */
#ifdef FV18_ENABLE
    &fv18_mmt,
#endif /* FV18_ENABLE */
#ifdef GPSCLOCK_ENABLE
    &gpsclock_mmt,
#endif /* GPSCLOCK_ENABLE */
#ifdef MTK3301_ENABLE
    &mtk3301_mmt,
#endif /* MTK3301_ENABLE */
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
#ifdef TNT_ENABLE
    &tnt_mmt,
#endif /* TNT_ENABLE */
#ifdef PASSTHROUGH_ENABLE
    &json_mmt,
#endif /* PASSTHROUGH_ENABLE */
    NULL,
};

static const struct monitor_object_t **active, **fallback;
/*@ +nullassign @*/

static jmp_buf terminate;

#define display	(void)mvwprintw

/* ternination codes */
#define TERM_SELECT_FAILED	1
#define TERM_DRIVER_SWITCH	2
#define TERM_EMPTY_READ 	3
#define TERM_READ_ERROR 	4

void monitor_fixframe(WINDOW * win)
{
    int ymax, xmax, ycur, xcur;

    assert(win != NULL);
    getyx(win, ycur, xcur);
    getmaxyx(win, ymax, xmax);
    assert(xcur > -1 && ymax > 0);  /* squash a compiler warning */
    (void)mvwaddch(win, ycur, xmax - 1, ACS_VLINE);
}

/******************************************************************************
 *
 * Device-independent I/O routines
 *
 ******************************************************************************/

static void visibilize(/*@out@*/char *buf2, size_t len, const char *buf)
{
    const char *sp;

    buf2[0] = '\0';
    for (sp = buf; *sp != '\0' && strlen(buf2)+4 < len; sp++)
	if (isprint(*sp) || (sp[0] == '\n' && sp[1] == '\0') 
	  || (sp[0] == '\r' && sp[2] == '\0'))
	    (void)snprintf(buf2 + strlen(buf2), 2, "%c", *sp);
	else
	    (void)snprintf(buf2 + strlen(buf2), 6, "\\x%02x",
			   0x00ff & (unsigned)*sp);
}

void gpsmon_report(int errlevel, const char *fmt, va_list ap)
/* our version of the logger */
{
    char buf[BUFSIZ]; 
    char buf2[BUFSIZ];
    char *err_str;

    switch ( errlevel ) {
    case LOG_ERROR:
	err_str = "ERROR: ";
	break;
    case LOG_SHOUT:
	err_str = "SHOUT: ";
	break;
    case LOG_WARN:
	err_str = "WARN: ";
	break;
    case LOG_INF:
	err_str = "INFO: ";
	break;
    case LOG_DATA:
	err_str = "DATA: ";
	break;
    case LOG_PROG:
	err_str = "PROG: ";
	break;
    case LOG_IO:
	err_str = "IO: ";
	break;
    case LOG_SPIN:
	err_str = "SPIN: ";
	break;
    case LOG_RAW:
	err_str = "RAW: ";
	break;
    default:
	err_str = "UNK: ";
    }

    (void)strlcpy(buf, "gpsd:", BUFSIZ);
    (void)strncat(buf, err_str, BUFSIZ - strlen(buf) );
    if (errlevel <= context.debug && packetwin != NULL) {
	(void)vsnprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), fmt, ap);
	visibilize(buf2, sizeof(buf2), buf);
	if (!curses_active)
	    (void)fputs(buf2, stdout);
	else
	    (void)waddstr(packetwin, buf2);
	if (logfile != NULL)
	    (void)fputs(buf2, logfile);
    }
}

static ssize_t readpkt(void)
{
    /*@ -globstate -type -shiftnegative -compdef -nullpass @*/
    struct timeval timeval;
    fd_set select_set;
    gps_mask_t changed;

    FD_ZERO(&select_set);
    FD_SET(session.gpsdata.gps_fd, &select_set);
    /*
     * If the timeout on this select isn't longer than the device's
     * cycle time, the code will be prone to flaky timing-dependent
     * failures.
     */
    timeval.tv_sec = 2;
    timeval.tv_usec = 0;
    if (select(session.gpsdata.gps_fd + 1, &select_set, NULL, NULL, &timeval)
	== -1)
	longjmp(terminate, TERM_SELECT_FAILED);

    if (!FD_ISSET(session.gpsdata.gps_fd, &select_set))
	longjmp(terminate, TERM_SELECT_FAILED);

    changed = gpsd_poll(&session);
    if (changed == 0)
	longjmp(terminate, TERM_EMPTY_READ);

    /* conditional prevents mask dumper from eating CPU */
    if (context.debug >= LOG_DATA)
	gpsd_report(LOG_DATA,
		    "packet mask = %s\n",
		    gps_maskdump(session.gpsdata.set));

    if ((changed & ERROR_SET) != 0)
	longjmp(terminate, TERM_READ_ERROR);

    if (logfile != NULL && session.packet.outbuflen > 0) {
	/*@ -shiftimplementation -sefparams +charint @*/
	assert(fwrite
	       (session.packet.outbuffer, sizeof(char),
		session.packet.outbuflen, logfile) >= 1);
	/*@ +shiftimplementation +sefparams -charint @*/
    }
    return session.packet.outbuflen;
    /*@ +globstate +type +shiftnegative +compdef +nullpass @*/
}

static void packet_dump(const char *buf, size_t buflen)
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
		    (void)waddch(packetwin, (chtype) buf[i]);
		else
		    (void)wprintw(packetwin, "\\x%02x",
				  (unsigned char)buf[i]);
	} else {
	    for (i = 0; i < buflen; i++)
		(void)wprintw(packetwin, "%02x", (unsigned char)buf[i]);
	}
	(void)wprintw(packetwin, "\n");
    }
}

#if defined(CONTROLSEND_ENABLE) || defined(RECONFIGURE_ENABLE)
static void monitor_dump_send(/*@in@*/ const char *buf, size_t len)
{
    if (packetwin != NULL) {
	(void)wattrset(packetwin, A_BOLD);
	(void)wprintw(packetwin, ">>>");
	packet_dump(buf, len);
	(void)wattrset(packetwin, A_NORMAL);
    }
}
#endif /* defined(CONTROLSEND_ENABLE) || defined(RECONFIGURE_ENABLE) */

#ifdef RECONFIGURE_ENABLE
static void announce_log(/*@in@*/ const char *str)
{
   if (packetwin != NULL) {
	(void)wattrset(packetwin, A_BOLD);
	(void)wprintw(packetwin, ">>>");
	(void)waddstr(packetwin, str);
	(void)wattrset(packetwin, A_NORMAL);
	(void)wprintw(packetwin, "\n");
   }
   if (logfile != NULL) {
       (void)fprintf(logfile, ">>>%s\n", str);
   }
}
#endif /* RECONFIGURE_ENABLE */


#ifdef CONTROLSEND_ENABLE
bool monitor_control_send( /*@in@*/ unsigned char *buf, size_t len)
{
    if (!serial)
	return false;
    else {
	ssize_t st;

	context.readonly = false;
	st = (*active)->driver->control_send(&session, (char *)buf, len);
	context.readonly = true;
	monitor_dump_send((const char *)buf, len);
	return (st != -1);
    }
}

static bool monitor_raw_send( /*@in@*/ unsigned char *buf, size_t len)
{
    if (!serial)
	return false;
    else {
	ssize_t st;
	st = write(session.gpsdata.gps_fd, (char *)buf, len);
	monitor_dump_send((const char *)buf, len);
	return (st > 0 && (size_t) st == len);
    }
}
#endif /* CONTROLSEND_ENABLE */

/*****************************************************************************
 *
 * Main sequence and display machinery
 *
 *****************************************************************************/

void monitor_complain(const char *fmt, ...)
{
    va_list ap;
    assert(cmdwin!=NULL);
    (void)wmove(cmdwin, 0, (int)strlen(type_name) + 2);
    (void)wclrtoeol(cmdwin);
    (void)wattrset(cmdwin, A_BOLD | A_BLINK);
    va_start(ap, fmt);
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
    for (trial = monitor_objects; *trial; trial++) {
	if (strcmp((*trial)->driver->type_name, devtype->type_name)==0) {
	    newobject = trial;
	    break;
	}
    }
    if (newobject) {
	if (LINES < (*newobject)->min_y + 1 || COLS < (*newobject)->min_x) {
	    monitor_complain("New type requires %dx%d screen",
			     (*newobject)->min_x, (*newobject)->min_y + 1);
	} else {
	    int leftover;
	    if (active != NULL) {
		if ((*active)->wrap != NULL)
		    (*active)->wrap();
		(void)delwin(devicewin);
	    }
	    active = newobject;
	    devicewin = newwin((*active)->min_y, (*active)->min_x, 1, 0);
	    if ((devicewin == NULL) || ((*active)->initialize != NULL && !(*active)->initialize())) {
		monitor_complain("Internal initialization failure - screen "
				 "must be at least 80x24. aborting.");
		return false;
	    }

	    /*@ -onlytrans @*/
	    leftover = LINES - 1 - (*active)->min_y;
	    if (leftover <= 0) {
		if (packetwin != NULL)
		    (void)delwin(packetwin);
		packetwin = NULL;
	    } else if (packetwin == NULL) {
		packetwin = newwin(leftover, COLS, (*active)->min_y + 1, 0);
		(void)scrollok(packetwin, true);
		(void)wsetscrreg(packetwin, 0, leftover - 1);
	    } else {
		(void)wresize(packetwin, leftover, COLS);
		(void)mvwin(packetwin, (*active)->min_y + 1, 0);
		(void)wsetscrreg(packetwin, 0, leftover - 1);
	    }
	    /*@ +onlytrans @*/
	}
	return true;
    }

    monitor_complain("No monitor matches %s.", devtype->type_name);
    return false;
}

static jmp_buf assertbuf;

static void onsig(int sig UNUSED)
{
    longjmp(assertbuf, 1);
}

#define WATCHRAW	"?WATCH={\"raw\":2}\r\n"
#define WATCHRAWDEVICE	"?WATCH={\"raw\":2,\"device\":\"%s\"}\r\n"
#define WATCHNMEA	"?WATCH={\"nmea\":true}\r\n"
#define WATCHNMEADEVICE	"?WATCH={\"nmea\":true,\"device\":\"%s\"}\r\n"

int main(int argc, char **argv)
{
#ifdef RECONFIGURE_ENABLE
    unsigned int v;
#endif /* RECONFIGURE_ENABLE */
    int option, status, last_type = BAD_PACKET;
    ssize_t len;
    struct fixsource_t source;
    fd_set select_set;
    unsigned char buf[BUFLEN];
    char line[80], *explanation, *p;
    int bailout = 0, matches = 0;
    bool nmea = false;

    set_report_callback(gpsmon_report);
    /*@ -observertrans @*/
    (void)putenv("TZ=UTC");	// for ctime()
    /*@ +observertrans @*/
    /*@ -branchstate @*/
    while ((option = getopt(argc, argv, "D:LVhl:nt:?")) != -1) {
	switch (option) {
	case 'D':
	    context.debug = atoi(optarg);
	    break;
	case 'L':		/* list known device types */
	    (void)
		fputs
		("General commands available per type. '+' means there are private commands.\n",
		 stdout);
	    for (active = monitor_objects; *active; active++) {
		(void)fputs("i l q ^S ^Q", stdout);
		(void)fputc(' ', stdout);
#ifdef RECONFIGURE_ENABLE
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
#endif /* RECONFIGURE_ENABLE */
#ifdef CONTROLSEND_ENABLE
		if ((*active)->driver->control_send != NULL)
		    (void)fputc('x', stdout);
		else
		    (void)fputc(' ', stdout);
#endif /* CONTROLSEND_ENABLE */
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
	case 'V':
	    (void)printf("gpsmon: %s (revision %s)\n", VERSION, REVISION);
	    exit(0);
	case 'l':		/* enable logging at startup */
	    logfile = fopen(optarg, "w");
	    if (logfile == NULL) {
		(void)fprintf(stderr, "Couldn't open logfile for writing.\n");
		exit(1);
	    }
	    break;
        case 't':
	    fallback = NULL;
	    for (active = monitor_objects; *active; active++) {
		if (strncmp((*active)->driver->type_name, optarg, strlen(optarg)) == 0)
		{
		    fallback = active;
		    matches++;
		}
	    }
	    if (matches > 1) { 
		(void)fprintf(stderr, "-T option matched more than one driver.\n");
		exit(1);
	    }
	    else if (matches == 0) { 
		(void)fprintf(stderr, "-T option didn't match any driver.\n");
		exit(1);
	    }
	    active = NULL;
	    break;
	case 'n':
	    nmea = true;
	    break;
	case 'h':
	case '?':
	default:
	    (void)
		fputs
		("usage:  gpsmon [-?hVln] [-D debuglevel] [-t type] [server[:port:[device]]]\n",
		 stderr);
	    exit(1);
	}
    }
    /*@ +branchstate @*/

    if (optind < argc) {
	gpsd_source_spec(argv[optind], &source);
    } else
	gpsd_source_spec(NULL, &source);

    gpsd_time_init(&context, time(NULL));
    gpsd_init(&session, &context, NULL);

    /*@ -boolops */
    if ((optind >= argc || source.device == NULL
	|| strchr(argv[optind], ':') != NULL)
#ifdef HAVE_BLUEZ
        && bachk(argv[optind])) {
#else
	) {
#endif
	(void)gps_open(source.server, source.port, &session.gpsdata);
	if (session.gpsdata.gps_fd < 0) {
	    (void)fprintf(stderr,
			  "%s: connection failure on %s:%s, error %d = %s.\n",
			  argv[0], source.server, source.port,
			  session.gpsdata.gps_fd,
			  netlib_errstr(session.gpsdata.gps_fd));
	    exit(1);
	}
	if (source.device != NULL) {
	    if (nmea) {
	        (void)gps_send(&session.gpsdata, WATCHNMEADEVICE, source.device);
	    } else {
	        (void)gps_send(&session.gpsdata, WATCHRAWDEVICE, source.device);
	    }
	    /*
	     *  The gpsdata.dev member is filled only in JSON mode,
	     *  but we are in super-raw mode.
	     */
	    (void)strlcpy(session.gpsdata.dev.path, source.device,
			  sizeof(session.gpsdata.dev.path));
	} else {
	    if (nmea) {
	        (void)gps_send(&session.gpsdata, WATCHNMEA);
		session.gpsdata.dev.path[0] = '\0';
	    } else {
	        (void)gps_send(&session.gpsdata, WATCHRAW);
		session.gpsdata.dev.path[0] = '\0';
	    }
	}
	serial = false;
    } else {
	(void)strlcpy(session.gpsdata.dev.path, argv[optind],
		      sizeof(session.gpsdata.dev.path));
	if (gpsd_activate(&session) == -1) {
	    gpsd_report(LOG_ERROR,
			"activation of device %s failed, errno=%d\n",
			session.gpsdata.dev.path, errno);
	    exit(2);
	}

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
	(void)fputs("gpsmon: assertion failure, probable I/O error\n",
		    stderr);
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
    statwin = newwin(CMDWINHEIGHT, 30, 0, 0);
    cmdwin = newwin(CMDWINHEIGHT, 0, 0, 30);
    packetwin = newwin(0, 0, CMDWINHEIGHT, 0);
    if (statwin == NULL || cmdwin == NULL || packetwin == NULL)
	goto quit;
    (void)scrollok(packetwin, true);
    (void)wsetscrreg(packetwin, 0, LINES - CMDWINHEIGHT);
    /*@ +onlytrans @*/

    (void)wmove(packetwin, 0, 0);

    FD_ZERO(&select_set);

    if ((bailout = setjmp(terminate)) == 0) {
	/*@ -observertrans @*/
	for (;;) {
	    /* *INDENT-OFF* */
	    type_name =
		session.device_type ? session.device_type->type_name : "Unknown device";
	    /* *INDENT-ON* */
	    (void)wattrset(statwin, A_BOLD);
	    if (serial)
		display(statwin, 0, 0, "%s %u %d%c%d",
			session.gpsdata.dev.path,
			session.gpsdata.dev.baudrate,
			9 - session.gpsdata.dev.stopbits,
			session.gpsdata.dev.parity,
			session.gpsdata.dev.stopbits);
	    else
		/*@ -nullpass @*/
		display(statwin, 0, 0, "%s:%s:%s",
			source.server, source.port, session.gpsdata.dev.path);
	    /*@ +nullpass @*/
	    (void)wattrset(statwin, A_NORMAL);
	    (void)wmove(cmdwin, 0, 0);

	    /* get a packet -- calls gpsd_poll() */
	    if ((len = readpkt()) > 0 && session.packet.outbuflen > 0) {
		/* switch types on packet receipt */
		/*@ -nullpass */
		if (session.packet.type != last_type) {
		    last_type = session.packet.type;
		    if (!switch_type(session.device_type))
			longjmp(terminate, TERM_DRIVER_SWITCH);
		}
		/*@ +nullpass */

		/* refresh all windows */
		(void)wprintw(cmdwin, type_name);
		(void)wprintw(cmdwin, "> ");
		(void)wclrtoeol(cmdwin);
		if (active != NULL 
			&& len > 0 && session.packet.outbuflen > 0
			&& (*active)->update != NULL)
		    (*active)->update();
		(void)wprintw(packetwin, "(%d) ", session.packet.outbuflen);
		packet_dump((char *)session.packet.outbuffer,
			    session.packet.outbuflen);
		(void)wnoutrefresh(statwin);
		(void)wnoutrefresh(cmdwin);
		if (devicewin != NULL)
		    (void)wnoutrefresh(devicewin);
		if (packetwin != NULL)
		    (void)wnoutrefresh(packetwin);
		(void)doupdate();
	    }

	    /* rest of this invoked only if user has pressed a key */
	    FD_SET(0, &select_set);
	    FD_SET(session.gpsdata.gps_fd, &select_set);

	    if (select(FD_SETSIZE, &select_set, NULL, NULL, NULL) == -1)
		break;

	    if (FD_ISSET(0, &select_set)) {
		char *arg;
		(void)wmove(cmdwin, 0, (int)strlen(type_name) + 2);
		(void)wrefresh(cmdwin);
		(void)echo();
		/*@ -usedef -compdef @*/
		(void)wgetnstr(cmdwin, line, 80);
		(void)noecho();
		if (packetwin != NULL)
		    (void)wrefresh(packetwin);
		(void)wrefresh(cmdwin);

		if ((p = strchr(line, '\r')) != NULL)
		    *p = '\0';

		if (line[0] == '\0')
		    continue;
		/*@ +usedef +compdef @*/

		if (isspace(line[1])) {
		    for (arg = line + 2; *arg != '\0' && isspace(*arg); arg++)
			arg++;
		    arg++;
		} else
		    arg = line + 1;

		if (serial && active != NULL && (*active)->command != NULL) {
		    status = (*active)->command(line);
		    if (status == COMMAND_TERMINATE)
			goto quit;
		    else if (status == COMMAND_MATCH)
			continue;
		    assert(status == COMMAND_UNKNOWN);
		}
		switch (line[0]) {
#ifdef RECONFIGURE_ENABLE
		case 'c':	/* change cycle time */
		    if (active == NULL)
			monitor_complain("No device defined yet");
		    else if (!serial)
			monitor_complain("Only available in low-level mode.");
		    else {
			double rate = strtod(arg, NULL);
			const struct monitor_object_t **switcher = active;

			if (fallback != NULL && (*fallback)->driver->rate_switcher != NULL)
			    switcher = fallback;
			if ((*switcher)->driver->rate_switcher) {
			    /* *INDENT-OFF* */
			    context.readonly = false;
			    if ((*switcher)->driver->rate_switcher(&session, rate)) {
				announce_log("Rate switcher callled.");
			    } else
				monitor_complain("Rate not supported.");
			    context.readonly = true;
			    /* *INDENT-ON* */
			} else
			    monitor_complain
				("Device type has no rate switcher");
		    }
#endif /* RECONFIGURE_ENABLE */
		    break;
		case 'i':	/* start probing for subtype */
		    if (active == NULL)
			monitor_complain("No GPS type detected.");
		    else if (!serial)
			monitor_complain("Only available in low-level mode.");
		    else {
			if (strcspn(line, "01") == strlen(line))
			    context.readonly = !context.readonly;
			else
			    context.readonly = (atoi(line + 1) == 0);
			/* *INDENT-OFF* */
			(void)gpsd_switch_driver(&session,
				 (*active)->driver->type_name);
			/* *INDENT-ON* */
		    }
		    break;

		case 'l':	/* open logfile */
		    if (logfile != NULL) {
			if (packetwin != NULL)
			    (void)wprintw(packetwin,
					  ">>> Logging to %s off", logfile);
			(void)fclose(logfile);
		    }

		    if ((logfile = fopen(line + 1, "a")) != NULL)
			if (packetwin != NULL)
			    (void)wprintw(packetwin,
					  ">>> Logging to %s on", logfile);
		    break;

#ifdef RECONFIGURE_ENABLE
		case 'n':	/* change mode */
		    /* if argument not specified, toggle */
		    if (strcspn(line, "01") == strlen(line)) {
			/* *INDENT-OFF* */
			v = (unsigned int)TEXTUAL_PACKET_TYPE(
			    session.packet.type);
			/* *INDENT-ON* */
		    } else
			v = (unsigned)atoi(line + 1);
		    if (active == NULL)
			monitor_complain("No device defined yet");
		    else if (!serial)
			monitor_complain("Only available in low-level mode.");
		    else {
			const struct monitor_object_t **switcher = active;

			if (fallback != NULL && (*fallback)->driver->mode_switcher != NULL)
			    switcher = fallback;
			if ((*switcher)->driver->mode_switcher) {
			    context.readonly = false;
			    (*switcher)->driver->mode_switcher(&session,
							     (int)v);
			    context.readonly = true;
			    announce_log("Mode switcher called");
			    (void)tcdrain(session.gpsdata.gps_fd);
			    (void)usleep(50000);
			} else
			    monitor_complain
				("Device type has no mode switcher");
		    }
		    break;
#endif /* RECONFIGURE_ENABLE */

		case 'q':	/* quit */
		    goto quit;

#ifdef RECONFIGURE_ENABLE
		case 's':	/* change speed */
		    if (active == NULL)
			monitor_complain("No device defined yet");
		    else if (!serial)
			monitor_complain("Only available in low-level mode.");
		    else {
			speed_t speed;
			char parity = session.gpsdata.dev.parity;
			unsigned int stopbits =
			    (unsigned int)session.gpsdata.dev.stopbits;
			char *modespec;
			const struct monitor_object_t **switcher = active;

			if (fallback != NULL && (*fallback)->driver->speed_switcher != NULL)
			    switcher = fallback;

			modespec = strchr(arg, ':');
			/*@ +charint @*/
			if (modespec != NULL) {
			    if (strchr("78", *++modespec) == NULL) {
				monitor_complain
				    ("No support for that word length.");
				break;
			    }
			    parity = *++modespec;
			    if (strchr("NOE", parity) == NULL) {
				monitor_complain("What parity is '%c'?.",
						 parity);
				break;
			    }
			    stopbits = (unsigned int)*++modespec;
			    if (strchr("12", (char)stopbits) == NULL) {
				monitor_complain("Stop bits must be 1 or 2.");
				break;
			    }
			    stopbits = (unsigned int)(stopbits - '0');
			}
			/*@ -charint @*/
			speed = (unsigned)atoi(arg);
			/* *INDENT-OFF* */
			if ((*switcher)->driver->speed_switcher) {
			    context.readonly = false;
			    if ((*switcher)->
				driver->speed_switcher(&session, speed,
						       parity, (int)
						       stopbits)) {
				announce_log("Speed switcher called.");
				/*
				 * See the comment attached to the 'DEVICE'
				 * command in gpsd.  Allow the control
				 * string time to register at the GPS
				 * before we do the baud rate switch,
				 * which effectively trashes the UART's
				 * buffer.
				 */
				(void)tcdrain(session.gpsdata.gps_fd);
				(void)usleep(50000);
				(void)gpsd_set_speed(&session, speed,
						     parity, stopbits);
			    } else
				monitor_complain
				    ("Speed/mode combination not supported.");
			    context.readonly = true;
			} else
			    monitor_complain
				("Device type has no speed switcher");
			/* *INDENT-ON* */
		    }
		    break;
#endif /* RECONFIGURE_ENABLE */

		case 't':	/* force device type */
		    if (!serial)
			monitor_complain("Only available in low-level mode.");
		    else if (strlen(arg) > 0) {
			int matchcount = 0;
			const struct gps_type_t **dp, *forcetype = NULL;
			for (dp = gpsd_drivers; *dp; dp++) {
			    if (strstr((*dp)->type_name, arg) != NULL) {
				forcetype = *dp;
				matchcount++;
			    }
			}
			if (matchcount == 0) {
			    monitor_complain
				("No driver type matches '%s'.", arg);
			} else if (matchcount == 1) {
			    assert(forcetype != NULL);
			    /* *INDENT-OFF* */
			    if (switch_type(forcetype))
				(void)gpsd_switch_driver(&session,
							 forcetype->type_name);
			    /* *INDENT-ON* */
			} else {
			    monitor_complain
				("Multiple driver type names match '%s'.",
				 arg);
			}
		    }
		    break;

#ifdef CONTROLSEND_ENABLE
		case 'x':	/* send control packet */
		    if (active == NULL)
			monitor_complain("No device defined yet");
		    else if (!serial)
			monitor_complain("Only available in low-level mode.");
		    else {
			/*@ -compdef @*/
			int st = gpsd_hexpack(arg, (char *)buf, strlen(arg));
			if (st < 0)
			    monitor_complain
				("Invalid hex string (error %d)", st);
			else if ((*active)->driver->control_send == NULL)
			    monitor_complain
				("Device type has no control-send method.");
			else if (!monitor_control_send(buf, (size_t) st))
			    monitor_complain("Control send failed.");
			/*@ +compdef @*/
		    }
		    break;

		case 'X':	/* send raw packet */
		    if (!serial)
			monitor_complain("Only available in low-level mode.");
		    else {
			/*@ -compdef @*/
			len =
			    (ssize_t) gpsd_hexpack(arg, (char *)buf, strlen(arg));
			if (len < 0)
			    monitor_complain("Invalid hex string (error %d)",
					     len);
			else if (!monitor_raw_send(buf, (size_t) len))
			    monitor_complain("Raw send failed.");
			/*@ +compdef @*/
		    }
		    break;
#endif /* CONTROLSEND_ENABLE */

		default:
		    monitor_complain("Unknown command");
		    break;
		}
	    }
	}
	/*@ +nullpass @*/
	/*@ +observertrans @*/
    }

  quit:
    /* we'll fall through to here on longjmp() */
    gpsd_close(&session);
    if (logfile)
	(void)fclose(logfile);
    (void)endwin();

    explanation = NULL;
    switch (bailout) {
    case TERM_SELECT_FAILED:
	explanation = "select(2) failed\n";
	break;
    case TERM_DRIVER_SWITCH:
	explanation = "Driver type switch failed\n";
	break;
    case TERM_EMPTY_READ:
	explanation = "Device went offline\n";
	break;
    case TERM_READ_ERROR:
	explanation = "Read error from device\n";
	break;
    }

    if (explanation != NULL)
	(void)fputs(explanation, stderr);
    exit(0);
}

/* gpsmon.c ends here */
