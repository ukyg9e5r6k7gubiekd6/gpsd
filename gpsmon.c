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
#include <sys/time.h>		/* expected to declare select(2) a la SuS */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <fcntl.h>
#ifndef S_SPLINT_S
#include <unistd.h>
#endif /* S_SPLINT_S */

#include "gpsd.h"
#include "gps_json.h"
#include "gpsmon.h"
#include "gpsdclient.h"
#include "revision.h"

#define BUFLEN		2048

/* external capability tables */
extern struct monitor_object_t nmea_mmt, sirf_mmt, ashtech_mmt;
extern struct monitor_object_t garmin_mmt, garmin_bin_ser_mmt;
extern struct monitor_object_t italk_mmt, ubx_mmt, superstar2_mmt;
extern struct monitor_object_t fv18_mmt, gpsclock_mmt, mtk3301_mmt;
extern struct monitor_object_t oncore_mmt, tnt_mmt, aivdm_mmt;
#ifdef NMEA_ENABLE
extern const struct gps_type_t driver_nmea0183;
#endif /* NMEA_ENABLE */

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
static size_t promptlen = 0;
struct termios cooked, rare;
struct fixsource_t source;

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
#ifdef AIVDM_ENABLE
    &aivdm_mmt,
#endif /* AIVDM_ENABLE */
#endif /* NMEA_ENABLE */
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
/*@ +nullassign @*/

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

/******************************************************************************
 *
 * Visualization helpers
 *
 ******************************************************************************/

static void visibilize(/*@out@*/char *buf2, size_t len2, const char *buf)
/* string is mostly printable, dress up the nonprintables a bit */
{
    const char *sp;

    buf2[0] = '\0';
    for (sp = buf; *sp != '\0' && strlen(buf2)+4 < len2; sp++)
	if (isprint(*sp) || (sp[0] == '\n' && sp[1] == '\0')
	  || (sp[0] == '\r' && sp[2] == '\0'))
	    (void)snprintf(buf2 + strlen(buf2), 2, "%c", *sp);
	else
	    (void)snprintf(buf2 + strlen(buf2), 6, "\\x%02x",
			   (unsigned)(*sp & 0xff));
}

/*@-compdef -mustdefine@*/
static void cond_hexdump(/*@out@*/char *buf2, size_t len2, 
			 const char *buf, size_t len)
/* pass through visibilized if all printable, hexdump otherwise */
{
    size_t i;
    bool printable = true;
    for (i = 0; i < len; i++)
	if (!isprint(buf[i]) && !isspace(buf[i]))
	    printable = false;
    if (printable) {
	size_t j;
	for (i = j = 0; i < len && j < len2 - 1; i++)
	    if (isprint(buf[i])) {
		buf2[j++] = buf[i];
		buf2[j] = '\0';
	    }
	    else {
		(void)snprintf(&buf2[j], len2-strlen(buf2), "\\x%02x", (unsigned int)(buf[i] & 0xff));
		j = strlen(buf2);
	    }
    } else {
	buf2[0] = '\0';
	for (i = 0; i < len; i++)
	    (void)snprintf(buf2 + strlen(buf2), len2 - strlen(buf2),
			   "%02x", (unsigned int)(buf[i] & 0xff));
    }
}
/*@+compdef +mustdefine@*/

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

static void packet_dump(const char *buf, size_t buflen)
{
    if (packetwin != NULL) {
	char buf2[buflen * 2];
	cond_hexdump(buf2, buflen * 2, buf, buflen);
	(void)waddstr(packetwin, buf2);
	(void)waddch(packetwin, (chtype)'\n');
    }
}

#if defined(CONTROLSEND_ENABLE) || defined(RECONFIGURE_ENABLE)
static void monitor_dump_send(/*@in@*/ const char *buf, size_t len)
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

/*@-compdef@*/
static void packet_vlog(/*@out@*/char *buf, size_t len, const char *fmt, va_list ap)
{
    char buf2[BUFSIZ];

    (void)vsnprintf(buf + strlen(buf), len, fmt, ap);
    visibilize(buf2, sizeof(buf2), buf);

    report_lock();
    if (!curses_active)
	(void)fputs(buf2, stdout);
    else if (packetwin != NULL)
	(void)waddstr(packetwin, buf2);
    if (logfile != NULL)
	(void)fputs(buf2, logfile);
    report_unlock();
}
/*@+compdef@*/

#ifdef RECONFIGURE_ENABLE
static void announce_log(/*@in@*/ const char *fmt, ...)
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

static /*@observer@*/ const char *promptgen(void)
{
    static char buf[sizeof(session.gpsdata.dev.path)];

    if (serial)
	(void)snprintf(buf, sizeof(buf),
		       "%s %u %u%c%u",
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

 /*@-observertrans -nullpass -globstate@*/
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
/*@+observertrans +nullpass +globstate@*/

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

    /*@ -onlytrans @*/
    statwin = newwin(CMDWINHEIGHT, 30, 0, 0);
    cmdwin = newwin(CMDWINHEIGHT, 0, 0, 30);
    packetwin = newwin(0, 0, CMDWINHEIGHT, 0);
    if (statwin == NULL || cmdwin == NULL || packetwin == NULL)
	return false;
    (void)scrollok(packetwin, true);
    (void)wsetscrreg(packetwin, 0, LINES - CMDWINHEIGHT);
    /*@ +onlytrans @*/

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
	    devicewin = newwin((*active)->min_y, (*active)->min_x, 1, 0);
	    if ((devicewin == NULL) || ((*active)->initialize != NULL && !(*active)->initialize())) {
		monitor_complain("Internal initialization failure - screen "
				 "must be at least 80x24. Aborting.");
		return false;
	    }

	    /*@ -onlytrans @*/
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
	    /*@ +onlytrans @*/
	}
	return true;
    }

    monitor_complain("No monitor matches %s.", devtype->type_name);
    return false;
}

/*@-globstate@*/
static void select_packet_monitor(struct gps_device_t *device)
{
    static int last_type = BAD_PACKET;

    /*
     * Switch display types on packet receipt.  Note, this *doesn't*
     * change the selection of the current device driver; that's done
     * within gpsd_multipoll() before this hook is called.
     */
    if (device->packet.type != last_type) {
	const struct gps_type_t *active_type = device->device_type;
#ifdef NMEA_ENABLE
	if (device->packet.type == NMEA_PACKET
	    && ((device->device_type->flags & DRIVER_STICKY) != 0))
	    active_type = &driver_nmea0183;
#endif /* NMEA_ENABLE */
	if (!switch_type(active_type))
	    longjmp(terminate, TERM_DRIVER_SWITCH);
	else {
	    refresh_statwin();
	    refresh_cmdwin();
	}
	last_type = device->packet.type;
    }

    if (active != NULL
	&& device->packet.outbuflen > 0
	&& (*active)->update != NULL)
	(*active)->update();
    if (devicewin != NULL)
	(void)wnoutrefresh(devicewin);
}
/*@+globstate@*/

/*@-statictrans -globstate@*/
static /*@null@*/ char *curses_get_command(void)
/* char-by-char nonblocking input, return accumulated command line on \n */
{
    static char input[80];
    static char line[80];
    int c;

    c = wgetch(cmdwin);
    if (c == CTRL('L')) {
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
/*@+statictrans +globstate@*/

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

void gpsd_report(const int debuglevel, const int errlevel, const char *fmt, ...)
/* our version of the logger */
{
    char buf[BUFSIZ]; 
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
    case LOG_CLIENT:
	err_str = "CLIENT: ";
	break;
    case LOG_INF:
	err_str = "INFO: ";
	break;
    case LOG_PROG:
	err_str = "PROG: ";
	break;
    case LOG_IO:
	err_str = "IO: ";
	break;
    case LOG_DATA:
	err_str = "DATA: ";
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

    (void)strlcpy(buf, "gpsmon:", BUFSIZ);
    (void)strncat(buf, err_str, BUFSIZ - strlen(buf));
    if (errlevel <= debuglevel) {
	va_list ap;
	va_start(ap, fmt);
	packet_vlog(buf, sizeof(buf), fmt, ap);
	va_end(ap);
    }
}

ssize_t gpsd_write(struct gps_device_t *session,
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
bool monitor_control_send( /*@in@*/ unsigned char *buf, size_t len)
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

static bool monitor_raw_send( /*@in@*/ unsigned char *buf, size_t len)
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

/*@-observertrans -nullpass -globstate -compdef -uniondef@*/
static void gpsmon_hook(struct gps_device_t *device, gps_mask_t changed UNUSED)
/* per-packet hook */
{
    char buf[BUFSIZ];
    struct timedrift_t td;

#ifdef PPS_ENABLE
    if (!serial && strncmp((char*)device->packet.outbuffer, "{\"class\":\"PPS\",", 13) == 0)
    {
	const char *end = NULL;
	struct gps_data_t noclobber;
	int status = json_pps_read((const char *)device->packet.outbuffer,
				   &noclobber,
				   &end);
	if (status != 0) {
	    /* FIXME: figure out why using json_error_string() core dumps */
	    complain("Ill-formed PPS packet: %d", status);
	    buf[0] = '\0';
	} else {
	    /*@-type@*/ /* splint is confused about struct timespec */
	    double timedelta = timespec_diff_ns(noclobber.timedrift.real, 
						noclobber.timedrift.clock) * 1e-9;
	    if (!curses_active)
		(void)fprintf(stderr,
			      "Drift clock=%lu.%09lu clock=%lu.%09lu offset=%.9f\n",
			      (unsigned long)noclobber.timedrift.clock.tv_sec,
			      (unsigned long)noclobber.timedrift.clock.tv_nsec,
			      (unsigned long)noclobber.timedrift.real.tv_sec,
			      (unsigned long)noclobber.timedrift.real.tv_nsec,
			      timedelta);
	    /*@+type@*/

	    (void)strlcpy(buf, PPSBAR, BUFSIZ);
	    session.ppslast = noclobber.timedrift;
	    /* coverity[missing_lock] */
	    session.ppscount++;
	}
    }
    else
#endif /* PPS_ENABLE */
    {
#ifdef __future__
	if (!serial)
	{
	    if (device->packet.type == JSON_PACKET)
	    {
		const char *end = NULL;
		libgps_json_unpack((char *)device->packet.outbuffer, &session.gpsdata, &end);
	    }
	}
#endif /* __future__ */

	if (curses_active)
	    select_packet_monitor(device);

	(void)snprintf(buf, sizeof(buf), "(%d) ",
		       (int)device->packet.outbuflen);
	cond_hexdump(buf + strlen(buf), sizeof(buf) - strlen(buf),
		     (char *)device->packet.outbuffer,device->packet.outbuflen);
	(void)strlcat(buf, "\n", sizeof(buf) - strlen(buf));
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

    if (logfile != NULL && device->packet.outbuflen > 0) {
        /*@ -shiftimplementation -sefparams +charint @*/
        assert(fwrite
               (device->packet.outbuffer, sizeof(char),
                device->packet.outbuflen, logfile) >= 1);
        /*@ +shiftimplementation +sefparams -charint @*/
    }

    report_unlock();

    /* Update the last fix time seen for PPS. FIXME: do this here? */
    ntpshm_latch(device, &td);
}
/*@+observertrans +nullpass +globstate +compdef +uniondef@*/

/*@-globstate -usedef -compdef@*/
static bool do_command(const char *line)
{
#ifdef RECONFIGURE_ENABLE
    unsigned int v;
#endif /* RECONFIGURE_ENABLE */
    unsigned char buf[BUFLEN];
    const char *arg;

    if (isspace(line[1])) {
	for (arg = line + 2; *arg != '\0' && isspace(*arg); arg++)
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
		session.packet.counter = 0;
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
		session.packet.type);
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
		(void)usleep(50000);
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
	    /*@ +charint @*/
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
	    /*@ -charint @*/
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
		    (void)usleep(50000);
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
	    /*@ -compdef @*/
	    int st = gpsd_hexpack(arg, (char *)buf, strlen(arg));
	    if (st < 0)
		complain("Invalid hex string (error %d)", st);
	    else if (session.device_type->control_send == NULL)
		complain("Device type %s has no control-send method.", 
			 session.device_type->type_name);
	    else if (!monitor_control_send(buf, (size_t) st))
		complain("Control send failed.");
	    /*@ +compdef @*/
	}
	break;

    case 'X':	/* send raw packet */
	if (!serial)
	    complain("Only available in low-level mode.");
	else {
	    /*@ -compdef @*/
	    ssize_t len = (ssize_t) gpsd_hexpack(arg, (char *)buf, strlen(arg));
	    if (len < 0)
		complain("Invalid hex string (error %d)", len);
	    else if (!monitor_raw_send(buf, (size_t) len))
		complain("Raw send failed.");
	    /*@ +compdef @*/
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
/*@+globstate +usedef +compdef@*/

#ifdef PPS_ENABLE
static /*@observer@*/ char *pps_report(struct gps_device_t *session UNUSED,
			struct timedrift_t *td UNUSED) {
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

/*@-onlytrans -branchstate@*/
int main(int argc, char **argv)
{
    int option;
    char *explanation;
    int bailout = 0, matches = 0;
    bool nmea = false;
    fd_set all_fds;
    fd_set rfds;
    int maxfd = 0;
    char inbuf[80];
    volatile bool nocurses = false;

    /*@ -observertrans @*/
    (void)putenv("TZ=UTC");	// for ctime()
    /*@ +observertrans @*/
    gps_context_init(&context);	// initialize the report mutex
    while ((option = getopt(argc, argv, "aD:LVhl:nt:?")) != -1) {
	switch (option) {
	case 'a':
	    nocurses = true;
	    break;
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
	    exit(EXIT_SUCCESS);
	case 'V':
	    (void)printf("gpsmon: %s (revision %s)\n", VERSION, REVISION);
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
		if (strncmp((*active)->driver->type_name, optarg, strlen(optarg)) == 0)
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
	serial = (strncmp(argv[optind], "/dev", 4) == 0);
	gpsd_source_spec(argv[optind], &source);
    } else {
	serial = false;
	gpsd_source_spec(NULL, &source);
    }

    if (serial) {
	assert(source.device != NULL);	/* clue to splint */
	(void) strlcpy(session.gpsdata.dev.path, 
		       source.device, 
		       sizeof(session.gpsdata.dev.path));
    } else {
	assert(source.server != NULL);	/* clue to splint */
	if (strstr(source.server, "//") == 0)
	    (void) strlcpy(session.gpsdata.dev.path, 
			   "tcp://",
			   sizeof(session.gpsdata.dev.path));
	else
	    session.gpsdata.dev.path[0] = '\0';
	(void)snprintf(session.gpsdata.dev.path + strlen(session.gpsdata.dev.path),
		       sizeof(session.gpsdata.dev.path) - strlen(session.gpsdata.dev.path),
		       "%s:%s", source.server, source.port);
    }

    if (gpsd_activate(&session, O_PROBEONLY) == -1) {
	(void)fprintf(stderr,
		      "gpsmon: activation of device %s failed, errno=%d (%s)\n",
		      session.gpsdata.dev.path, errno, strerror(errno));
	exit(EXIT_FAILURE);
    }


    if (serial) {
#ifdef PPS_ENABLE
	session.thread_report_hook = pps_report;
	pps_thread_activate(&session);
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
    FD_SET(0, &all_fds);	/* accept keystroke inputs */

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
#ifdef EFDS
	    fd_set efds;
#endif /* EFDS */
	    switch(gpsd_await_data(&rfds, maxfd, &all_fds, context.debug))
	    {
	    case AWAIT_GOT_INPUT:
		break;
	    case AWAIT_NOT_READY:
#ifdef EFDS
		/* no recovery from bad fd is possible */
		if (FD_ISSET(session.gpsdata.gps_fd, &efds))
		    longjmp(terminate, TERM_SELECT_FAILED);
#endif /* EFDS */
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
		    /* coverity[string_null] */
		    ssize_t st = read(0, &inbuf, 1);

		    if (st == 1) {
#ifdef PPS_ENABLE
			gpsd_acquire_reporting_lock();
#endif /* PPS_ENABLE*/
			(void)tcflush(0, TCIFLUSH);
			(void)tcsetattr(0, TCSANOW, &cooked);
			(void)fputs("gpsmon: ", stdout);
			(void)fputs(promptgen(), stdout);
			(void)fputs("> ", stdout);
			(void)putchar(inbuf[0]);
			cmdline = fgets(inbuf+1, (int)strlen(inbuf)-1, stdin);
			cmdline--;
		    }
		}
		if (cmdline != NULL && !do_command(cmdline))
		    longjmp(terminate, TERM_QUIT);
		if (!curses_active) {
		    (void)sleep(2);
		    (void)tcsetattr(0, TCSANOW, &rare);
#ifdef PPS_ENABLE
		    gpsd_release_reporting_lock();
#endif /* PPS_ENABLE*/
		}
	    }
	}
    }

  quit:
    /* we'll fall through to here on longjmp() */

#ifdef PPS_ENABLE
    /* Shut down PPS monitoring. */
    if (serial)
       (void)pps_thread_deactivate(&session);
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
/*@+onlytrans +branchstate@*/

/* gpsmon.c ends here */
