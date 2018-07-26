/*
 * The generic GPS packet monitor.
 *
 * This file is Copyright (c) 2010 by the GPSD project
 * SPDX-License-Identifier: BSD-2-clause
 */

/* for vsnprintf() FreeBSD wants __ISO_C_VISIBLE >= 1999 */
#define __ISO_C_VISIBLE 1999

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
#include <math.h>
#include <sys/time.h>		/* expected to declare select(2) a la SuS */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <fcntl.h>
#include <unistd.h>

#include "gpsd.h"
#include "gps_json.h"
#include "gpsmon.h"
#include "gpsdclient.h"
#include "revision.h"
#include "strfuncs.h"
#include "timespec.h"

#define BUFLEN		2048

/* needed under FreeBSD */
#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX	255
#endif /* HOST_NAME_MAX */

/* external capability tables */
extern struct monitor_object_t nmea_mmt, sirf_mmt, ashtech_mmt;
extern struct monitor_object_t garmin_mmt, garmin_bin_ser_mmt;
extern struct monitor_object_t italk_mmt, ubx_mmt, superstar2_mmt;
extern struct monitor_object_t fv18_mmt, gpsclock_mmt, mtk3301_mmt;
extern struct monitor_object_t oncore_mmt, tnt_mmt, aivdm_mmt;
#ifdef NMEA0183_ENABLE
extern const struct gps_type_t driver_nmea0183;
#endif /* NMEA0183_ENABLE */

/* These are public */
struct gps_device_t session;
WINDOW *devicewin;
bool serial;

/* These are private */
static struct gps_context_t context;
static bool curses_active;
static WINDOW *statwin, *cmdwin;
static WINDOW *packetwin;
static FILE *logfile;
static char *type_name;
static size_t promptlen = 0;
static struct termios cooked, rare;
static struct fixsource_t source;
static char hostname[HOST_NAME_MAX];
#ifdef NTP_ENABLE
static struct timedelta_t time_offset;
#endif /* NTP_ENABLE */

#ifdef PASSTHROUGH_ENABLE
/* no methods, it's all device window */
extern const struct gps_type_t driver_json_passthrough;
const struct monitor_object_t json_mmt = {
    .initialize = NULL,
    .update = NULL,
    .command = NULL,
    .wrap = NULL,
    .min_y = 0, .min_x = 80,	/* no need for a device window */
    .driver = &driver_json_passthrough,
};
#endif /* PASSTHROUGH_ENABLE */

static const struct monitor_object_t *monitor_objects[] = {
#ifdef NMEA0183_ENABLE
    &nmea_mmt,
#if defined(GARMIN_ENABLE) && defined(NMEA0183_ENABLE)
    &garmin_mmt,
#endif /* GARMIN_ENABLE && NMEA0183_ENABLE */
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
#ifdef AIVDM_ENABLE
    &aivdm_mmt,
#endif /* AIVDM_ENABLE */
#endif /* NMEA0183_ENABLE */
#if defined(SIRF_ENABLE) && defined(BINARY_ENABLE)
    &sirf_mmt,
#endif /* defined(SIRF_ENABLE) && defined(BINARY_ENABLE) */
#if defined(UBLOX_ENABLE) && defined(BINARY_ENABLE)
    &ubx_mmt,
#endif /* defined(UBLOX_ENABLE) && defined(BINARY_ENABLE) */
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

static const struct monitor_object_t **active;
static const struct gps_type_t *fallback;

static jmp_buf terminate;

#define display	(void)mvwprintw

/* termination codes */
#define TERM_SELECT_FAILED	1
#define TERM_DRIVER_SWITCH	2
#define TERM_EMPTY_READ 	3
#define TERM_READ_ERROR 	4
#define TERM_SIGNAL		5
#define TERM_QUIT		6

/* PPS monitoring */
#if defined(PPS_ENABLE)
static inline void report_lock(void)
{
    gpsd_acquire_reporting_lock();
}

static inline void report_unlock(void)
{
    gpsd_release_reporting_lock();
}
#else
static inline void report_lock(void) { }
static inline void report_unlock(void) { }
#endif /* PPS_ENABLE */

#define PPSBAR "-------------------------------------" \
	       " PPS " \
	       "-------------------------------------\n"

/* Dummy conditional for *display* of (possibly remote) PPS events */
#define PPS_DISPLAY_ENABLE 1

/******************************************************************************
 *
 * Visualization helpers
 *
 ******************************************************************************/

#ifdef PPS_ENABLE
static void visibilize(char *buf2, size_t len2, const char *buf)
/* string is mostly printable, dress up the nonprintables a bit */
{
    const char *sp;

    buf2[0] = '\0';
    for (sp = buf; *sp != '\0' && strlen(buf2)+4 < len2; sp++)
	if (isprint((unsigned char) *sp) || (sp[0] == '\n' && sp[1] == '\0')
	  || (sp[0] == '\r' && sp[2] == '\0'))
	    (void)snprintf(buf2 + strlen(buf2), 2, "%c", *sp);
	else
	    (void)snprintf(buf2 + strlen(buf2), 6, "\\x%02x",
			   (unsigned)(*sp & 0xff));
}
#endif /* PPS_ENABLE */

static void cond_hexdump(char *buf2, size_t len2,
			 const char *buf, size_t len)
/* pass through visibilized if all printable, hexdump otherwise */
{
    size_t i;
    bool printable = true;
    for (i = 0; i < len; i++)
	if (!isprint((unsigned char) buf[i]) && !isspace((unsigned char) buf[i]))
	    printable = false;
    if (printable) {
	size_t j;
	for (i = j = 0; i < len && j < len2 - 1; i++)
	    if (isprint((unsigned char) buf[i])) {
		buf2[j++] = buf[i];
		buf2[j] = '\0';
	    }
	    else {
		if (TEXTUAL_PACKET_TYPE(session.lexer.type)) {
		    if (i == len - 1 && buf[i] == '\n')
			continue;
		    if (i == len - 2 && buf[i] == '\r')
			continue;
		}
		(void)snprintf(&buf2[j], len2-strlen(buf2), "\\x%02x", (unsigned int)(buf[i] & 0xff));
		j = strlen(buf2);
	    }
    } else {
	buf2[0] = '\0';
	for (i = 0; i < len; i++)
	    str_appendf(buf2, len2, "%02x", (unsigned int)(buf[i] & 0xff));
    }
}

#ifdef NTP_ENABLE
void toff_update(WINDOW *win, int y, int x)
{
    assert(win != NULL);
    if (time_offset.real.tv_sec != 0)
    {
	/* NOTE: can not use double here due to precision requirements */
	struct timespec timedelta;
	int i, ymax, xmax;
	getmaxyx(win, ymax, xmax);
	assert(ymax > 0);  /* squash a compiler warning */
	(void)wmove(win, y, x);
	/*
	 * The magic number shortening the field works because
	 * we know we'll never see more than 5 digits of seconds
	 * rather than 10.
	 */
	for (i = 0; i < TIMESPEC_LEN-4 && x + i < xmax - 1; i++)
	    (void)waddch(win, ' ');
	TS_SUB(&timedelta, &time_offset.clock, &time_offset.real);
	if ( 86400 < (long)labs(timedelta.tv_sec) ) {
	    /* more than one day off, overflow */
	    /* need a bigger field to show it */
	    (void)mvwaddstr(win, y, x, "> 1 day");
	} else {
	    char buf[TIMESPEC_LEN];
	    timespec_str(&timedelta, buf, sizeof(buf));
	    (void)mvwaddstr(win, y, x, buf);
	}
    }
}
#endif /* NTP_ENABLE */

/* FIXME:  Decouple this reporting from local PPS monitoring. */
#ifdef PPS_ENABLE
void pps_update(WINDOW *win, int y, int x)
{
    struct timedelta_t ppstimes;

    assert(win != NULL);
    if (pps_thread_ppsout(&session.pps_thread, &ppstimes) > 0) {
	/* NOTE: can not use double here due to precision requirements */
	struct timespec timedelta;
	int i, ymax, xmax;
	getmaxyx(win, ymax, xmax);
	assert(ymax > 0);  /* squash a compiler warning */
	(void)wmove(win, y, x);
	/* see toff_update() for explanation of the magic number */
	for (i = 0; i < TIMESPEC_LEN-4 && x + i < xmax - 1; i++)
	    (void)waddch(win, ' ');
	TS_SUB( &timedelta, &ppstimes.clock, &ppstimes.real);
        if ( 86400 < (long)labs(timedelta.tv_sec) ) {
	    /* more than one day off, overflow */
            /* need a bigger field to show it */
	    (void)mvwaddstr(win, y, x, "> 1 day");
        } else {
	    char buf[TIMESPEC_LEN];
	    timespec_str(&timedelta, buf, sizeof(buf));
	    (void)mvwaddstr(win, y, x, buf);
        }
	(void)wnoutrefresh(win);
    }
}
#endif /* PPS_ENABLE */

/******************************************************************************
 *
 * Curses I/O
 *
 ******************************************************************************/

void monitor_fixframe(WINDOW * win)
{
    int ymax, xmax, ycur, xcur;

    assert(win != NULL);
    getyx(win, ycur, xcur);
    getmaxyx(win, ymax, xmax);
    assert(xcur > -1 && ymax > 0);  /* squash a compiler warning */
    (void)mvwaddch(win, ycur, xmax - 1, ACS_VLINE);
}

#if defined(CONTROLSEND_ENABLE) || defined(RECONFIGURE_ENABLE)
static void packet_dump(const char *buf, size_t buflen)
{
    if (packetwin != NULL) {
	char buf2[buflen * 2];
	cond_hexdump(buf2, buflen * 2, buf, buflen);
	(void)waddstr(packetwin, buf2);
	(void)waddch(packetwin, (chtype)'\n');
    }
}

static void monitor_dump_send(const char *buf, size_t len)
{
    if (packetwin != NULL) {
	report_lock();
	(void)wattrset(packetwin, A_BOLD);
	(void)wprintw(packetwin, ">>>");
	packet_dump(buf, len);
	(void)wattrset(packetwin, A_NORMAL);
	report_unlock();
    }
}
#endif /* defined(CONTROLSEND_ENABLE) || defined(RECONFIGURE_ENABLE) */

static void gpsmon_report(const char *buf)
/* log to the packet window if curses is up, otherwise stdout */
{
    /* report locking is left to caller */
    if (!curses_active)
	(void)fputs(buf, stdout);
    else if (packetwin != NULL)
	(void)waddstr(packetwin, buf);
    if (logfile != NULL)
	(void)fputs(buf, logfile);
}

#ifdef PPS_ENABLE
static void packet_vlog(char *buf, size_t len, const char *fmt, va_list ap)
{
    char buf2[BUFSIZ];

    visibilize(buf2, sizeof(buf2), buf);

    report_lock();
    (void)vsnprintf(buf2 + strlen(buf2), len, fmt, ap);
    gpsmon_report(buf2);
    report_unlock();
}
#endif /* PPS_ENABLE */

#ifdef RECONFIGURE_ENABLE
static void announce_log(const char *fmt, ...)
{
    char buf[BUFSIZ];
    va_list ap;
    va_start(ap, fmt);
    (void)vsnprintf(buf, sizeof(buf) - 5, fmt, ap);
    va_end(ap);

   if (packetwin != NULL) {
	report_lock();
	(void)wattrset(packetwin, A_BOLD);
	(void)wprintw(packetwin, ">>>");
	(void)waddstr(packetwin, buf);
	(void)wattrset(packetwin, A_NORMAL);
	(void)wprintw(packetwin, "\n");
	report_unlock();
   }
   if (logfile != NULL) {
       (void)fprintf(logfile, ">>>%s\n", buf);
   }
}
#endif /* RECONFIGURE_ENABLE */

static void monitor_vcomplain(const char *fmt, va_list ap)
{
    assert(cmdwin!=NULL);
    (void)wmove(cmdwin, 0, (int)promptlen);
    (void)wclrtoeol(cmdwin);
    (void)wattrset(cmdwin, A_BOLD);
    (void)vwprintw(cmdwin, (char *)fmt, ap);
    (void)wattrset(cmdwin, A_NORMAL);
    (void)wrefresh(cmdwin);
    (void)doupdate();

    (void)wgetch(cmdwin);
    (void)wmove(cmdwin, 0, (int)promptlen);
    (void)wclrtoeol(cmdwin);
    (void)wrefresh(cmdwin);
    (void)wmove(cmdwin, 0, (int)promptlen);
    (void)doupdate();
}

void monitor_complain(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    monitor_vcomplain(fmt, ap);
    va_end(ap);
}

void monitor_log(const char *fmt, ...)
{
    if (packetwin != NULL) {
	va_list ap;
	report_lock();
	va_start(ap, fmt);
	(void)vwprintw(packetwin, (char *)fmt, ap);
	va_end(ap);
	report_unlock();
    }
}

static const char *promptgen(void)
{
    static char buf[sizeof(session.gpsdata.dev.path) + HOST_NAME_MAX + 20];

    if (serial)
	(void)snprintf(buf, sizeof(buf),
		       "%s:%s %u %u%c%u",
		       hostname,
		       session.gpsdata.dev.path,
		       session.gpsdata.dev.baudrate,
		       9 - session.gpsdata.dev.stopbits,
		       session.gpsdata.dev.parity,
		       session.gpsdata.dev.stopbits);
    else {
	(void)strlcpy(buf, session.gpsdata.dev.path, sizeof(buf));
	if (source.device != NULL) {
	    (void) strlcat(buf, ":", sizeof(buf));
	    (void) strlcat(buf, source.device, sizeof(buf));
	}
    }
    return buf;
}

static void refresh_statwin(void)
/* refresh the device-identification window */
{
    /* *INDENT-OFF* */
    type_name =
	session.device_type ? session.device_type->type_name : "Unknown device";
    /* *INDENT-ON* */
    (void)wclear(statwin);
    (void)wattrset(statwin, A_BOLD);
    (void)mvwaddstr(statwin, 0, 0, promptgen());
    (void)wattrset(statwin, A_NORMAL);
    (void)wnoutrefresh(statwin);
}

static void refresh_cmdwin(void)
/* refresh the command window */
{
    (void)wmove(cmdwin, 0, 0);
    (void)wprintw(cmdwin, type_name);
    promptlen = strlen(type_name);
    if (fallback != NULL && strcmp(fallback->type_name, type_name) != 0) {
	(void)waddch(cmdwin, (chtype)' ');
	(void)waddch(cmdwin, (chtype)'(');
	(void)waddstr(cmdwin, fallback->type_name);
	(void)waddch(cmdwin, (chtype)')');
	promptlen += strlen(fallback->type_name) + 3;
    }
    (void)wprintw(cmdwin, "> ");
    promptlen += 2;
    (void)wclrtoeol(cmdwin);
    (void)wnoutrefresh(cmdwin);
}

static bool curses_init(void)
{
    (void)initscr();
    (void)cbreak();
    (void)intrflush(stdscr, FALSE);
    (void)keypad(stdscr, true);
    (void)clearok(stdscr, true);
    (void)clear();
    (void)noecho();
    curses_active = true;

#define CMDWINHEIGHT	1

    statwin = newwin(CMDWINHEIGHT, 30, 0, 0);
    cmdwin = newwin(CMDWINHEIGHT, 0, 0, 30);
    packetwin = newwin(0, 0, CMDWINHEIGHT, 0);
    if (statwin == NULL || cmdwin == NULL || packetwin == NULL)
	return false;
    (void)scrollok(packetwin, true);
    (void)wsetscrreg(packetwin, 0, LINES - CMDWINHEIGHT);

    (void)wmove(packetwin, 0, 0);

    refresh_statwin();
    refresh_cmdwin();
    return true;
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
	    monitor_complain("%s requires %dx%d screen",
			     (*newobject)->driver->type_name,
			     (*newobject)->min_x, (*newobject)->min_y + 1);
	} else {
	    int leftover;

	    if (active != NULL) {
		if ((*active)->wrap != NULL)
		    (*active)->wrap();
		(void)delwin(devicewin);
	    }
	    active = newobject;
	    if (devicewin)
		delwin(devicewin);
	    devicewin = newwin((*active)->min_y, (*active)->min_x, 1, 0);
	    /* screen might have JSON on it from the init sequence */
	    (void)clearok(stdscr, true);
	    (void)clear();
	    if ((devicewin == NULL) || ((*active)->initialize != NULL && !(*active)->initialize())) {
		monitor_complain("Internal initialization failure - screen "
				 "must be at least 80x24. Aborting.");
		return false;
	    }

	    leftover = LINES - 1 - (*active)->min_y;
	    report_lock();
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
	    report_unlock();
	}
	return true;
    }

    monitor_complain("No monitor matches %s.", devtype->type_name);
    return false;
}

static void select_packet_monitor(struct gps_device_t *device)
{
    static int last_type = BAD_PACKET;

    /*
     * Switch display types on packet receipt.  Note, this *doesn't*
     * change the selection of the current device driver; that's done
     * within gpsd_multipoll() before this hook is called.
     */
    if (device->lexer.type != last_type) {
	const struct gps_type_t *active_type = device->device_type;
#ifdef NMEA0183_ENABLE
	if (device->lexer.type == NMEA_PACKET
	    && ((device->device_type->flags & DRIVER_STICKY) != 0))
	    active_type = &driver_nmea0183;
#endif /* NMEA0183_ENABLE */
	if (!switch_type(active_type))
	    longjmp(terminate, TERM_DRIVER_SWITCH);
	else {
	    refresh_statwin();
	    refresh_cmdwin();
	}
	last_type = device->lexer.type;
    }

    if (active != NULL
	&& device->lexer.outbuflen > 0
	&& (*active)->update != NULL)
	(*active)->update();
    if (devicewin != NULL)
	(void)wnoutrefresh(devicewin);
}

/* Control-L character */
#define CTRL_L 0x0C

static char *curses_get_command(void)
/* char-by-char nonblocking input, return accumulated command line on \n */
{
    static char input[80];
    static char line[80];
    int c;

    c = wgetch(cmdwin);
    if (CTRL_L == c) {
        /* ^L is to repaint the screen */
	(void)clearok(stdscr, true);
	if (active != NULL && (*active)->initialize != NULL)
	    (void)(*active)->initialize();
    } else if (c != '\r' && c != '\n') {
	size_t len = strlen(input);

	if (c == '\b' || c == KEY_LEFT || c == (int)erasechar()) {
	    input[len--] = '\0';
	} else if (isprint(c)) {
	    input[len] = (char)c;
	    input[++len] = '\0';
	    (void)waddch(cmdwin, (chtype)c);
	    (void)wrefresh(cmdwin);
	    (void)doupdate();
	}

	return NULL;
    }

    (void)wmove(cmdwin, 0, (int)promptlen);
    (void)wclrtoeol(cmdwin);
    (void)wrefresh(cmdwin);
    (void)doupdate();

    /* user finished entering a command */
    if (input[0] == '\0')
	return NULL;
    else {
	(void) strlcpy(line, input, sizeof(line));
	input[0] = '\0';
    }

    /* handle it in the currently selected monitor object if possible */
    if (serial && active != NULL && (*active)->command != NULL) {
	int status = (*active)->command(line);
	if (status == COMMAND_TERMINATE)
	    longjmp(terminate, TERM_QUIT);
	else if (status == COMMAND_MATCH)
	    return NULL;
	assert(status == COMMAND_UNKNOWN);
    }

    return line;
}

/******************************************************************************
 *
 * Mode-independent I/O
 *
 * Below this line, all calls to curses-dependent functions are guarded
 * by curses_active and have ttylike alternatives.
 *
 ******************************************************************************/

#ifdef PPS_ENABLE
static void packet_log(const char *fmt, ...)
{
    char buf[BUFSIZ];
    va_list ap;

    buf[0] = '\0';
    va_start(ap, fmt);
    packet_vlog(buf, sizeof(buf), fmt, ap);
    va_end(ap);
}
#endif /* PPS_ENABLE */

static ssize_t gpsmon_serial_write(struct gps_device_t *session,
		   const char *buf,
		   const size_t len)
/* pass low-level data to devices, echoing it to the log window */
{
#if defined(CONTROLSEND_ENABLE) || defined(RECONFIGURE_ENABLE)
    monitor_dump_send((const char *)buf, len);
#endif /* defined(CONTROLSEND_ENABLE) || defined(RECONFIGURE_ENABLE) */
    return gpsd_serial_write(session, buf, len);
}

#ifdef CONTROLSEND_ENABLE
bool monitor_control_send( unsigned char *buf, size_t len)
{
    if (!serial)
	return false;
    else {
	ssize_t st;

	context.readonly = false;
	st = session.device_type->control_send(&session, (char *)buf, len);
	context.readonly = true;
	return (st != -1);
    }
}

static bool monitor_raw_send( unsigned char *buf, size_t len)
{
    ssize_t st = gpsd_write(&session, (char *)buf, len);
    return (st > 0 && (size_t) st == len);
}
#endif /* CONTROLSEND_ENABLE */

static void complain(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);

    if (curses_active)
	monitor_vcomplain(fmt, ap);
    else {
	(void)vfprintf(stderr, fmt, ap);
	(void)fputc('\n', stderr);
    }

    va_end(ap);
}

/*****************************************************************************
 *
 * Main sequence
 *
 *****************************************************************************/

static void gpsmon_hook(struct gps_device_t *device, gps_mask_t changed UNUSED)
/* per-packet hook */
{
    char buf[BUFSIZ];

/* FIXME:  If the following condition is false, the display is screwed up. */
#if defined(SOCKET_EXPORT_ENABLE) && defined(PPS_DISPLAY_ENABLE)
    if (!serial && str_starts_with((char*)device->lexer.outbuffer, "{\"class\":\"TOFF\",")) {
	const char *end = NULL;
	int status = json_toff_read((const char *)device->lexer.outbuffer,
				   &session.gpsdata,
				   &end);
	if (status != 0) {
	    complain("Ill-formed TOFF packet: %d (%s)", status, json_error_string(status));
	    return;
	} else {
	    if (!curses_active)
		(void)fprintf(stderr,
			      "TOFF=%ld.%09ld real=%ld.%09ld\n",
			      (long)session.gpsdata.toff.clock.tv_sec,
			      (long)session.gpsdata.toff.clock.tv_nsec,
			      (long)session.gpsdata.toff.real.tv_sec,
			      (long)session.gpsdata.toff.real.tv_nsec);
#ifdef NTP_ENABLE
	    time_offset = session.gpsdata.toff;
#endif /* NTP_ENABLE */
	    return;
	}
    } else if (!serial && str_starts_with((char*)device->lexer.outbuffer, "{\"class\":\"PPS\",")) {
	const char *end = NULL;
	struct gps_data_t noclobber;
	int status = json_pps_read((const char *)device->lexer.outbuffer,
				   &noclobber,
				   &end);
	if (status != 0) {
	    complain("Ill-formed PPS packet: %d (%s)", status, json_error_string(status));
	    return;
	} else {
	    struct timespec timedelta;
	    char timedelta_str[TIMESPEC_LEN];

	    TS_SUB( &timedelta, &noclobber.pps.clock, &noclobber.pps.real);
	    timespec_str( &timedelta, timedelta_str, sizeof(timedelta_str) );

	    if (!curses_active) {
		char pps_clock_str[TIMESPEC_LEN];
		char pps_real_str[TIMESPEC_LEN];

		timespec_str( &noclobber.pps.clock, pps_clock_str,
			sizeof(pps_clock_str) );
		timespec_str( &noclobber.pps.real, pps_real_str,
			sizeof(pps_real_str) );

		(void)fprintf(stderr,
			      "PPS=%.20s clock=%.20s offset=%.20s\n",
			      pps_clock_str,
			      pps_real_str,
                              timedelta_str);
	    }

	    (void)snprintf(buf, sizeof(buf),
			"------------------- PPS offset: %.20s ------\n",
			timedelta_str);
/* FIXME:  Decouple this from the pps_thread code. */
#ifdef PPS_ENABLE
	    /*
	     * In direct mode this would be a bad idea, but we're not actually
	     * watching for handshake events on a spawned thread here.
	     */
	    /* coverity[missing_lock] */
	    session.pps_thread.pps_out = noclobber.pps;
	    /* coverity[missing_lock] */
	    session.pps_thread.ppsout_count++;
#endif /* PPS_ENABLE */
	}
    }
    else
#endif /* SOCKET_EXPORT_ENABLE && PPS_DISPLAY_ENABLE */
    {
#ifdef __future__
	if (!serial)
	{
	    if (device->lexer.type == JSON_PACKET)
	    {
		const char *end = NULL;
		libgps_json_unpack((char *)device->lexer.outbuffer, &session.gpsdata, &end);
	    }
	}
#endif /* __future__ */

	if (curses_active)
	    select_packet_monitor(device);

	(void)snprintf(buf, sizeof(buf), "(%d) ",
		       (int)device->lexer.outbuflen);
	cond_hexdump(buf + strlen(buf), sizeof(buf) - strlen(buf),
		     (char *)device->lexer.outbuffer,device->lexer.outbuflen);
	(void)strlcat(buf, "\n", sizeof(buf));
    }

    report_lock();

    if (!curses_active)
	(void)fputs(buf, stdout);
    else {
	if (packetwin != NULL) {
	    (void)waddstr(packetwin, buf);
	    (void)wnoutrefresh(packetwin);
	}
	(void)doupdate();
    }

    if (logfile != NULL && device->lexer.outbuflen > 0) {
        UNUSED size_t written_count = fwrite
               (device->lexer.outbuffer, sizeof(char),
                device->lexer.outbuflen, logfile);
        assert(written_count >= 1);
    }

    report_unlock();

#ifdef NTP_ENABLE
    /* Update the last fix time seen for PPS if we've actually seen one,
     * and it is a new second. */
    if ( 0 == isfinite(device->newdata.time)) {
	// "NTP: bad new time
#if defined(PPS_ENABLE)
    } else if (device->newdata.time <= device->pps_thread.fix_in.real.tv_sec) {
	// "NTP: Not a new time
#endif /* PPS_ENABLE */
    } else
	ntp_latch(device, &time_offset);
#endif /* NTP_ENABLE */
}

static bool do_command(const char *line)
{
#ifdef RECONFIGURE_ENABLE
    unsigned int v;
    struct timespec delay;
#endif /* RECONFIGURE_ENABLE */
#ifdef CONTROLSEND_ENABLE
    unsigned char buf[BUFLEN];
#endif /* CONTROLSEND_ENABLE */
    const char *arg;

    if (isspace((unsigned char) line[1])) {
	for (arg = line + 2; *arg != '\0' && isspace((unsigned char) *arg); arg++)
	    arg++;
	arg++;
    } else
	arg = line + 1;

    switch (line[0]) {
#ifdef RECONFIGURE_ENABLE
    case 'c':	/* change cycle time */
	if (session.device_type == NULL)
	    complain("No device defined yet");
	else if (!serial)
	    complain("Only available in low-level mode.");
	else {
	    double rate = strtod(arg, NULL);
	    const struct gps_type_t *switcher = session.device_type;

	    if (fallback != NULL && fallback->rate_switcher != NULL)
		switcher = fallback;
	    if (switcher->rate_switcher != NULL) {
		/* *INDENT-OFF* */
		context.readonly = false;
		if (switcher->rate_switcher(&session, rate)) {
		    announce_log("[Rate switcher called.]");
		} else
		    complain("Rate not supported.");
		context.readonly = true;
		/* *INDENT-ON* */
	    } else
		complain
		    ("Device type %s has no rate switcher",
		     switcher->type_name);
	}
#endif /* RECONFIGURE_ENABLE */
	break;
    case 'i':	/* start probing for subtype */
	if (session.device_type == NULL)
	    complain("No GPS type detected.");
	else if (!serial)
	    complain("Only available in low-level mode.");
	else {
	    if (strcspn(line, "01") == strlen(line))
		context.readonly = !context.readonly;
	    else
		context.readonly = (atoi(line + 1) == 0);
#ifdef RECONFIGURE_ENABLE
	    announce_log("[probing %sabled]", context.readonly ? "dis" : "en");
#endif /* RECONFIGURE_ENABLE */
	    if (!context.readonly)
		/* magic - forces a reconfigure */
		session.lexer.counter = 0;
	}
	break;

    case 'l':	/* open logfile */
	report_lock();
	if (logfile != NULL) {
	    if (packetwin != NULL)
		(void)wprintw(packetwin,
			      ">>> Logging off\n");
	    (void)fclose(logfile);
	}

	if ((logfile = fopen(line + 1, "a")) != NULL)
	    if (packetwin != NULL)
		(void)wprintw(packetwin,
			      ">>> Logging to %s\n", line + 1);
	report_unlock();
	break;

#ifdef RECONFIGURE_ENABLE
    case 'n':	/* change mode */
	/* if argument not specified, toggle */
	if (strcspn(line, "01") == strlen(line)) {
	    /* *INDENT-OFF* */
	    v = (unsigned int)TEXTUAL_PACKET_TYPE(
		session.lexer.type);
	    /* *INDENT-ON* */
	} else
	    v = (unsigned)atoi(line + 1);
	if (session.device_type == NULL)
	    complain("No device defined yet");
	else if (!serial)
	    complain("Only available in low-level mode.");
	else {
	    const struct gps_type_t *switcher = session.device_type;

	    if (fallback != NULL && fallback->mode_switcher != NULL)
		switcher = fallback;
	    if (switcher->mode_switcher != NULL) {
		context.readonly = false;
		announce_log("[Mode switcher to mode %d]", v);
		switcher->mode_switcher(&session, (int)v);
		context.readonly = true;
		(void)tcdrain(session.gpsdata.gps_fd);

		/* wait 50,000 uSec */
		delay.tv_sec = 0;
		delay.tv_nsec = 50000000L;
		nanosleep(&delay, NULL);

		/*
		 * Session device change will be set to NMEA when
		 * gpsmon resyncs.  So stash the current type to
		 * be restored if we do 'n' from NMEA mode.
		 */
		if (v == 0)
		    fallback = switcher;
	    } else
		complain
		    ("Device type %s has no mode switcher",
		     switcher->type_name);
	}
	break;
#endif /* RECONFIGURE_ENABLE */

    case 'q':	/* quit */
	return false;

#ifdef RECONFIGURE_ENABLE
    case 's':	/* change speed */
	if (session.device_type == NULL)
	    complain("No device defined yet");
	else if (!serial)
	    complain("Only available in low-level mode.");
	else {
	    speed_t speed;
	    char parity = session.gpsdata.dev.parity;
	    unsigned int stopbits =
		(unsigned int)session.gpsdata.dev.stopbits;
	    char *modespec;
	    const struct gps_type_t *switcher = session.device_type;

	    if (fallback != NULL && fallback->speed_switcher != NULL)
		switcher = fallback;
	    modespec = strchr(arg, ':');
	    if (modespec != NULL) {
		if (strchr("78", *++modespec) == NULL) {
		    complain
			("No support for that word length.");
		    break;
		}
		parity = *++modespec;
		if (strchr("NOE", parity) == NULL) {
		    complain("What parity is '%c'?.",
				     parity);
		    break;
		}
		stopbits = (unsigned int)*++modespec;
		if (strchr("12", (char)stopbits) == NULL) {
		    complain("Stop bits must be 1 or 2.");
		    break;
		}
		stopbits = (unsigned int)(stopbits - '0');
	    }
	    speed = (unsigned)atoi(arg);
	    /* *INDENT-OFF* */
	    if (switcher->speed_switcher) {
		context.readonly = false;
		if (switcher->speed_switcher(&session, speed,
					     parity, (int)
					     stopbits)) {
		    announce_log("[Speed switcher called.]");
		    /*
		     * See the comment attached to the 'DEVICE'
		     * command in gpsd.  Allow the control
		     * string time to register at the GPS
		     * before we do the baud rate switch,
		     * which effectively trashes the UART's
		     * buffer.
		     */
		    (void)tcdrain(session.gpsdata.gps_fd);
		    /* wait 50,000 uSec */
		    delay.tv_sec = 0;
		    delay.tv_nsec = 50000000L;
		    nanosleep(&delay, NULL);

		    (void)gpsd_set_speed(&session, speed,
					 parity, stopbits);
		} else
		    complain
			("Speed/mode combination not supported.");
		context.readonly = true;
	    } else
		complain
		    ("Device type %s has no speed switcher",
		     switcher->type_name);
	    /* *INDENT-ON* */
	    if (curses_active)
		refresh_statwin();
	}
	break;
#endif /* RECONFIGURE_ENABLE */

    case 't':	/* force device type */
	if (!serial)
	    complain("Only available in low-level mode.");
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
		complain
		    ("No driver type matches '%s'.", arg);
	    } else if (matchcount == 1) {
		assert(forcetype != NULL);
		/* *INDENT-OFF* */
		if (switch_type(forcetype))
		    (void)gpsd_switch_driver(&session,
					     forcetype->type_name);
		/* *INDENT-ON* */
		if (curses_active)
		    refresh_cmdwin();
	    } else {
		complain("Multiple driver type names match '%s'.", arg);
	    }
	}
	break;
#ifdef CONTROLSEND_ENABLE
    case 'x':	/* send control packet */
	if (session.device_type == NULL)
	    complain("No device defined yet");
	else if (!serial)
	    complain("Only available in low-level mode.");
	else {
	    int st = gpsd_hexpack(arg, (char *)buf, strlen(arg));
	    if (st < 0)
		complain("Invalid hex string (error %d)", st);
	    else if (session.device_type->control_send == NULL)
		complain("Device type %s has no control-send method.",
			 session.device_type->type_name);
	    else if (!monitor_control_send(buf, (size_t) st))
		complain("Control send failed.");
	}
	break;

    case 'X':	/* send raw packet */
	if (!serial)
	    complain("Only available in low-level mode.");
	else {
	    ssize_t len = (ssize_t) gpsd_hexpack(arg, (char *)buf, strlen(arg));
	    if (len < 0)
		complain("Invalid hex string (error %d)", len);
	    else if (!monitor_raw_send(buf, (size_t) len))
		complain("Raw send failed.");
	}
	break;
#endif /* CONTROLSEND_ENABLE */

    default:
	complain("Unknown command '%c'", line[0]);
	break;
    }

    /* continue accepting commands */
    return true;
}

#ifdef PPS_ENABLE
static char *pps_report(volatile struct pps_thread_t *pps_thread UNUSED,
			struct timedelta_t *td UNUSED) {
    packet_log(PPSBAR);
    return "gpsmon";
}
#endif /* PPS_ENABLE */

static jmp_buf assertbuf;

static void onsig(int sig UNUSED)
{
    if (sig == SIGABRT)
	longjmp(assertbuf, 1);
    else
	longjmp(terminate, TERM_SIGNAL);
}

#define WATCHRAW	"?WATCH={\"raw\":2,\"pps\":true}\r\n"
#define WATCHRAWDEVICE	"?WATCH={\"raw\":2,\"pps\":true,\"device\":\"%s\"}\r\n"
#define WATCHNMEA	"?WATCH={\"nmea\":true,\"pps\":true}\r\n"
#define WATCHNMEADEVICE	"?WATCH={\"nmea\":true,\"pps\":true,\"device\":\"%s\"}\r\n"

/* this placement avoids a compiler warning */
static const char *cmdline;

int main(int argc, char **argv)
{
    int option;
    char *explanation;
    int bailout = 0, matches = 0;
    bool nmea = false;
    fd_set all_fds;
    fd_set rfds;
    volatile int maxfd = 0;
    char inbuf[80];
    volatile bool nocurses = false;
    int activated = -1;

    gethostname(hostname, sizeof(hostname)-1);
    (void)putenv("TZ=UTC");	// for ctime()
    gps_context_init(&context, "gpsmon");	// initialize the report mutex
    context.serial_write = gpsmon_serial_write;
    context.errout.report = gpsmon_report;
    while ((option = getopt(argc, argv, "aD:LVhl:nt:?")) != -1) {
	switch (option) {
	case 'a':
	    nocurses = true;
	    break;
	case 'D':
	    context.errout.debug = atoi(optarg);
#if defined(CLIENTDEBUG_ENABLE) && defined(SOCKET_EXPORT_ENABLE)
	    json_enable_debug(context.errout.debug - 2, stderr);
#endif
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
	    exit(EXIT_SUCCESS);
	case 'V':
	    (void)printf("%s: %s (revision %s)\n", argv[0], VERSION, REVISION);
	    exit(EXIT_SUCCESS);
	case 'l':		/* enable logging at startup */
	    logfile = fopen(optarg, "w");
	    if (logfile == NULL) {
		(void)fprintf(stderr, "Couldn't open logfile for writing.\n");
		exit(EXIT_FAILURE);
	    }
	    break;
        case 'T':
        case 't':
	    fallback = NULL;
	    for (active = monitor_objects; *active; active++) {
		if (str_starts_with((*active)->driver->type_name, optarg))
		{
		    fallback = (*active)->driver;
		    matches++;
		}
	    }
	    if (matches > 1) {
		(void)fprintf(stderr, "-t option matched more than one driver.\n");
		exit(EXIT_FAILURE);
	    }
	    else if (matches == 0) {
		(void)fprintf(stderr, "-t option didn't match any driver.\n");
		exit(EXIT_FAILURE);
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
		("usage: gpsmon [-?hVn] [-l logfile] [-D debuglevel] "
		 "[-t type] [server[:port:[device]]]\n",
		 stderr);
	    exit(EXIT_FAILURE);
	}
    }

    gpsd_time_init(&context, time(NULL));
    gpsd_init(&session, &context, NULL);

    /* Grok the server, port, and device. */
    if (optind < argc) {
	serial = str_starts_with(argv[optind], "/dev");
	gpsd_source_spec(argv[optind], &source);
    } else {
	serial = false;
	gpsd_source_spec(NULL, &source);
    }

    if (serial) {
        if (NULL == source.device) {
            /* this can happen with "gpsmon /dev:dd" */
	    (void) strlcpy(session.gpsdata.dev.path,
			   argv[optind],
			   sizeof(session.gpsdata.dev.path));
        } else {
	    (void) strlcpy(session.gpsdata.dev.path,
			   source.device,
			   sizeof(session.gpsdata.dev.path));
        }
    } else {
	if (strstr(source.server, "//") == 0)
	    (void) strlcpy(session.gpsdata.dev.path,
			   "tcp://",
			   sizeof(session.gpsdata.dev.path));
	else
	    session.gpsdata.dev.path[0] = '\0';
	str_appendf(session.gpsdata.dev.path, sizeof(session.gpsdata.dev.path),
		       "%s:%s", source.server, source.port);
    }

    activated = gpsd_activate(&session, O_PROBEONLY);
    if ( 0 > activated ) {
	if ( PLACEHOLDING_FD == activated ) {
		(void)fputs("gpsmon:ERROR: PPS device unsupported\n", stderr);
        }
	exit(EXIT_FAILURE);
    }


    if (serial) {
#ifdef PPS_ENABLE
	/* this guard suppresses a warning on Bluetooth devices */
	if (session.sourcetype == source_rs232 || session.sourcetype == source_usb) {
	    session.pps_thread.report_hook = pps_report;
	    #ifdef MAGIC_HAT_ENABLE
	    /*
	     * The HAT kludge. If we're using the HAT GPS on a
	     * Raspberry Pi or a workalike like the ODROIDC2, and
	     * there is a static "first PPS", and we have access because
	     * we're root, assume we want to use KPPS.
	     */
	    if (strcmp(session.pps_thread.devicename, MAGIC_HAT_GPS) == 0
		|| strcmp(session.pps_thread.devicename, MAGIC_LINK_GPS) == 0) {
		char *first_pps = pps_get_first();
		if (access(first_pps, R_OK | W_OK) == 0)
			session.pps_thread.devicename = first_pps;
		}
	    #endif /* MAGIC_HAT_ENABLE */
	    pps_thread_activate(&session.pps_thread);
	}
#endif /* PPS_ENABLE */
    }
    else {
	if (source.device != NULL)
	    (void)gps_send(&session.gpsdata, nmea ? WATCHNMEADEVICE : WATCHRAWDEVICE, source.device);
	else
	    (void)gps_send(&session.gpsdata, nmea ? WATCHNMEA : WATCHRAW);
    }

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
	if (curses_active)
	    (void)endwin();
	(void)fputs("gpsmon: assertion failure, probable I/O error\n",
		    stderr);
	exit(EXIT_FAILURE);
    }

    FD_ZERO(&all_fds);
#ifndef __clang_analyzer__
    FD_SET(0, &all_fds);	/* accept keystroke inputs */
#endif /* __clang_analyzer__ */


    FD_SET(session.gpsdata.gps_fd, &all_fds);
    if (session.gpsdata.gps_fd > maxfd)
	 maxfd = session.gpsdata.gps_fd;

    if ((bailout = setjmp(terminate)) == 0) {
	(void)signal(SIGQUIT, onsig);
	(void)signal(SIGINT, onsig);
	(void)signal(SIGTERM, onsig);
	if (nocurses) {
	    (void)fputs("gpsmon: ", stdout);
	    (void)fputs(promptgen(), stdout);
	    (void)fputs("\n", stdout);
	    (void)tcgetattr(0, &cooked);
	    (void)tcgetattr(0, &rare);
	    rare.c_lflag &=~ (ICANON | ECHO);
	    rare.c_cc[VMIN] = (cc_t)1;
	    (void)tcflush(0, TCIFLUSH);
	    (void)tcsetattr(0, TCSANOW, &rare);
	} else if (!curses_init())
	    goto quit;

	for (;;)
	{
	    fd_set efds;
	    switch(gpsd_await_data(&rfds, &efds, maxfd, &all_fds, &context.errout))
	    {
	    case AWAIT_GOT_INPUT:
		break;
	    case AWAIT_NOT_READY:
		/* no recovery from bad fd is possible */
		if (FD_ISSET(session.gpsdata.gps_fd, &efds))
		    longjmp(terminate, TERM_SELECT_FAILED);
		continue;
	    case AWAIT_FAILED:
		longjmp(terminate, TERM_SELECT_FAILED);
		break;
	    }

	    switch(gpsd_multipoll(FD_ISSET(session.gpsdata.gps_fd, &rfds),
				  &session, gpsmon_hook, 0))
	    {
	    case DEVICE_READY:
		FD_SET(session.gpsdata.gps_fd, &all_fds);
		break;
	    case DEVICE_UNREADY:
		longjmp(terminate, TERM_EMPTY_READ);
		break;
	    case DEVICE_ERROR:
		longjmp(terminate, TERM_READ_ERROR);
		break;
	    case DEVICE_EOF:
		longjmp(terminate, TERM_QUIT);
		break;
	    default:
		break;
	    }

	    if (FD_ISSET(0, &rfds)) {
		if (curses_active)
		    cmdline = curses_get_command();
		else
		{
		    /* coverity[string_null_argument] */
		    ssize_t st = read(0, &inbuf, 1);

		    if (st == 1) {
			report_lock();
			(void)tcflush(0, TCIFLUSH);
			(void)tcsetattr(0, TCSANOW, &cooked);
			(void)fputs("gpsmon: ", stdout);
			(void)fputs(promptgen(), stdout);
			(void)fputs("> ", stdout);
			(void)putchar(inbuf[0]);
			cmdline = fgets(inbuf+1, sizeof(inbuf)-1, stdin);
			if (cmdline)
			    cmdline--;
		    }
		}
		if (cmdline != NULL && !do_command(cmdline))
		    longjmp(terminate, TERM_QUIT);
		if (!curses_active) {
		    (void)sleep(2);
		    (void)tcsetattr(0, TCSANOW, &rare);
		    report_unlock();
		}
	    }
	}
    }

  quit:
    /* we'll fall through to here on longjmp() */

#ifdef PPS_ENABLE
    /* Shut down PPS monitoring. */
    if (serial)
       (void)pps_thread_deactivate(&session.pps_thread);
#endif /* PPS_ENABLE*/

    gpsd_close(&session);
    if (logfile)
	(void)fclose(logfile);
    if (curses_active)
	(void)endwin();
    else
	(void)tcsetattr(0, TCSANOW, &cooked);

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
    case TERM_SIGNAL:
    case TERM_QUIT:
	/* normal exit, no message */
	break;
    default:
	explanation = "Unknown error, should never happen.\n";
	break;
    }

    if (explanation != NULL)
	(void)fputs(explanation, stderr);
    exit(EXIT_SUCCESS);
}

/* gpsmon.c ends here */
