/* gpsctl.c -- tweak the control settings on a GPS
 *
 * This file is Copyright (c) 2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <assert.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h>
#include <termios.h>
#ifndef S_SPLINT_S
#include <unistd.h>
#endif /* S_SPLINT_S */

#include "gpsd.h"
#include "revision.h"

static int debuglevel;
static unsigned int timeout = 8;

/*
 * Set this as high or higher than the maximum number of subtype 
 * probes in drivers.c.
 */
#define REDIRECT_SNIFF	15

void gpsd_report(int errlevel UNUSED, const char *fmt, ... )
/* our version of the logger */
{
    char *err_str;
    if (errlevel <= debuglevel) {
	va_list ap;
	va_start(ap, fmt);
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

	(void)fputs("gpsctl:", stderr);
	(void)fputs(err_str, stderr);
	(void)vfprintf(stderr, fmt, ap);
	va_end(ap);
    }
}

/*@ -noret @*/
static gps_mask_t get_packet(struct gps_device_t *session)
/* try to get a well-formed packet from the GPS */
{
    static fd_set rfds;
    gps_mask_t fieldmask;
#ifdef COMPAT_SELECT
    struct timeval tv;
#else
    struct timespec tv;
    sigset_t oldset, blockset;

    (void)sigemptyset(&blockset);
    (void)sigaddset(&blockset, SIGHUP);
    (void)sigaddset(&blockset, SIGINT);
    (void)sigaddset(&blockset, SIGTERM);
    (void)sigaddset(&blockset, SIGQUIT);
    (void)sigprocmask(SIG_BLOCK, &blockset, &oldset);
#endif /* COMPAT_SELECT */

    FD_ZERO(&rfds);
    for (;;) {
	FD_CLR(session->gpsdata.gps_fd, &rfds);

	/*@ -usedef -type -nullpass -compdef @*/
	/*
	 * If the timeout on this select isn't longer than the device's
	 * cycle time, the code will be prone to flaky timing-dependent
	 * failures.
	 */
	errno = 0;
	tv.tv_sec = 2;
#ifdef COMPAT_SELECT
	tv.tv_usec = 0;
	if (select(session->gpsdata.gps_fd + 1, &rfds, NULL, NULL, &tv) == -1) {
#else
	tv.tv_nsec = 0;
	if (pselect(session->gpsdata.gps_fd + 1, &rfds, NULL, NULL, &tv, &oldset) == -1) {
#endif
	    if (errno == EINTR || !FD_ISSET(session->gpsdata.gps_fd, &rfds))
		continue;
	    gpsd_report(LOG_ERROR, "select %s\n", strerror(errno));
	    exit(2);
	}
	/*@ +usedef +type +nullpass +compdef @*/

	fieldmask = gpsd_poll(session);

	/* conditional prevents mask dumper from eating CPU */
	if (debuglevel >= LOG_DATA)
	    gpsd_report(LOG_DATA,
			"packet mask = %s\n",
			gps_maskdump(session->gpsdata.set));

	if ((fieldmask &~ ONLINE_SET)!=0)
	    return fieldmask;
    }
}
/*@ +noret @*/

static void settle(struct gps_device_t *session)
/* allow the device to settle after a control operation */
{
    /*
     * See the 'deep black magic' comment in serial.c:set_serial().
     */
    (void)tcdrain(session->gpsdata.gps_fd);
    (void)usleep(50000);
    (void)tcdrain(session->gpsdata.gps_fd);
}

static bool gps_query(/*@out@*/struct gps_data_t *gpsdata, 
		       gps_mask_t expect,
		       const int timeout,
		       const char *fmt, ... )
/* ship a command and wait on an expected response type */
{
    static fd_set rfds;
    char buf[BUFSIZ];
    va_list ap;
    time_t starttime;
#ifdef COMPAT_SELECT
    struct timeval tv;
#else
    struct timespec tv;
    sigset_t oldset, blockset;

    (void)sigemptyset(&blockset);
    (void)sigaddset(&blockset, SIGHUP);
    (void)sigaddset(&blockset, SIGINT);
    (void)sigaddset(&blockset, SIGTERM);
    (void)sigaddset(&blockset, SIGQUIT);
    (void)sigprocmask(SIG_BLOCK, &blockset, &oldset);
#endif /* COMPAT_SELECT */

    va_start(ap, fmt);
    (void)vsnprintf(buf, sizeof(buf)-2, fmt, ap);
    va_end(ap);
    if (buf[strlen(buf)-1] != '\n')
	(void)strlcat(buf, "\n", BUFSIZ);
    /*@-usedef@*/
    if (write(gpsdata->gps_fd, buf, strlen(buf)) <= 0) {
	gpsd_report(LOG_ERROR, "gps_query(), write failed\n");
	return false;
    }
    /*@+usedef@*/
    gpsd_report(LOG_PROG, "gps_query(), wrote, %s\n", buf);

    FD_ZERO(&rfds);
    starttime = time(NULL);
    for (;;) {
	FD_CLR(gpsdata->gps_fd, &rfds);

	gpsd_report(LOG_PROG, "waiting...\n");

	/*@ -usedef -type -nullpass -compdef @*/
	tv.tv_sec = 2;
#ifdef COMPAT_SELECT
	tv.tv_usec = 0;
	if (select(gpsdata->gps_fd + 1, &rfds, NULL, NULL, &tv) == -1) {
#else
	tv.tv_nsec = 0;
	if (pselect(gpsdata->gps_fd + 1, &rfds, NULL, NULL, &tv, &oldset) == -1) {
#endif
	    if (errno == EINTR || !FD_ISSET(gpsdata->gps_fd, &rfds))
		continue;
	    gpsd_report(LOG_ERROR, "select %s\n", strerror(errno));
	    exit(2);
	}
	/*@ +usedef +type +nullpass +compdef @*/

	gpsd_report(LOG_PROG, "reading...\n");

	(void)gps_read(gpsdata);
	if (ERROR_SET & gpsdata->set) {
	    gpsd_report(LOG_ERROR, "error '%s'\n", gpsdata->error);
	    return false;
	}

	/*@ +ignorequals @*/
	if ((expect & gpsdata->set) != 0)
	    return true;
	else if (time(NULL) - starttime > timeout) {
	    gpsd_report(LOG_ERROR, 
			"timed out after %d seconds\n",
			timeout);
	    return false;
	}
	/*@ -ignorequals @*/
    }

    return false;
}

static void onsig(int sig)
{
    if (sig == SIGALRM) {
	gpsd_report(LOG_ERROR, "packet recognition timed out.\n");
	exit(EXIT_FAILURE);
    } else {
	gpsd_report(LOG_ERROR, "killed by signal %d\n", sig);
	exit(EXIT_SUCCESS);
    }
}

static char /*@observer@*/ *gpsd_id( /*@in@ */ struct gps_device_t *session)
/* full ID of the device for reports, including subtype */
{
    static char buf[128];
    if ((session == NULL) || (session->device_type == NULL) ||
	(session->device_type->type_name == NULL))
	return "unknown,";
    (void)strlcpy(buf, session->device_type->type_name, sizeof(buf));
    if (session->subtype[0] != '\0') {
	(void)strlcat(buf, " ", sizeof(buf));
	(void)strlcat(buf, session->subtype, sizeof(buf));
    }
    return (buf);
}

int main(int argc, char **argv)
{
    int option, status;
    char *device = NULL, *devtype = NULL; 
    char *speed = NULL, *control = NULL, *rate = NULL;
    bool to_binary = false, to_nmea = false, reset = false; 
    bool lowlevel=false, echo=false;
    struct gps_data_t gpsdata;
    const struct gps_type_t *forcetype = NULL;
    const struct gps_type_t **dp;
#ifdef CONTROLSEND_ENABLE
    char cooked[BUFSIZ];
    ssize_t cooklen = 0;
#endif /* RECONFIGURE_ENABLE */

#define USAGE	"usage: gpsctl [-l] [-b | -n | -r] [-D n] [-s speed] [-c rate] [-T timeout] [-V] [-t devtype] [-x control] [-e] <device>\n"
    while ((option = getopt(argc, argv, "bec:fhlnrs:t:x:D:T:V")) != -1) {
	switch (option) {
	case 'b':		/* switch to vendor binary mode */
	    to_binary = true;
	    break;
	case 'c':
#ifdef RECONFIGURE_ENABLE
	    rate = optarg;
#else
	    gpsd_report(LOG_ERROR, "cycle-change capability has been conditioned out.\n");
#endif /* RECONFIGURE_ENABLE */
	    break;
	case 'x':		/* ship specified control string */
#ifdef CONTROLSEND_ENABLE
	    control = optarg;
	    lowlevel = true;
	    if ((cooklen = hex_escapes(cooked, control)) <= 0) {
		gpsd_report(LOG_ERROR, 
			    "invalid escape string (error %d)\n", (int)cooklen);
		exit(EXIT_FAILURE);
	    }
#else
	    gpsd_report(LOG_ERROR, "control_send capability has been conditioned out.\n");	    
#endif /* CONTROLSEND_ENABLE */
	    break;
	case 'e':		/* echo specified control string with wrapper */
	    lowlevel = true;
	    echo = true;
	    break;
	case 'f':		/* force direct access to the device */
	    lowlevel = true;
	    break;
        case 'l':		/* list known device types */
	    for (dp = gpsd_drivers; *dp; dp++) {
#ifdef RECONFIGURE_ENABLE
		if ((*dp)->mode_switcher != NULL)
		    (void)fputs("-[bn]\t", stdout);
		else
		    (void)fputc('\t', stdout);
		if ((*dp)->speed_switcher != NULL)
		    (void)fputs("-s\t", stdout);
		else
		    (void)fputc('\t', stdout);
		if ((*dp)->rate_switcher != NULL)
		    (void)fputs("-c\t", stdout);
		else
		    (void)fputc('\t', stdout);
#endif /* RECONFIGURE_ENABLE */
#ifdef CONTROLSEND_ENABLE
		if ((*dp)->control_send != NULL)
		    (void)fputs("-x\t", stdout);
		else
		    (void)fputc('\t', stdout);
#endif /* CONTROLSEND_ENABLE */
		(void)puts((*dp)->type_name);
	    }
	    exit(EXIT_SUCCESS);
	case 'n':		/* switch to NMEA mode */
#ifdef RECONFIGURE_ENABLE
	    to_nmea = true;
#else
	    gpsd_report(LOG_ERROR, "speed-change capability has been conditioned out.\n");
#endif /* RECONFIGURE_ENABLE */
	    break;
	case 'r':		/* force-switch to default mode */
#ifdef RECONFIGURE_ENABLE
	    reset = true;
	    lowlevel = false;	/* so we'll abort if the daemon is running */
#else
	    gpsd_report(LOG_ERROR, "reset capability has been conditioned out.\n");
#endif /* RECONFIGURE_ENABLE */
	    break;
	case 's':		/* change output baud rate */
#ifdef RECONFIGURE_ENABLE
	    speed = optarg;
#else
	    gpsd_report(LOG_ERROR, "speed-change capability has been conditioned out.\n");
#endif /* RECONFIGURE_ENABLE */
	    break;
	case 't':		/* force the device type */
	    devtype = optarg;
	    break;
	case 'T':		/* set the timeout on packet recognition */
	    timeout = (unsigned)atoi(optarg);
	    break;
	case 'D':		/* set debugging level */
	    debuglevel = atoi(optarg);
#ifdef CLIENTDEBUG_ENABLE
	    gps_enable_debug(debuglevel, stderr);
#endif /* CLIENTDEBUG_ENABLE */
	    break;
	case 'V':
	    (void)fprintf(stderr, "version %s (revision %s)\n",
			  VERSION, REVISION);
	    break;
	case 'h':
	default:
	    fprintf(stderr, USAGE);
	    break;
	}
    }

    if (optind < argc)
	device = argv[optind];

    if (devtype != NULL) {
	int matchcount = 0;
	for (dp = gpsd_drivers; *dp; dp++) {
	    if (strstr((*dp)->type_name, devtype) != NULL) {
		forcetype = *dp;
		matchcount++;
	    }
	}
	if (matchcount == 0)
	    gpsd_report(LOG_ERROR, "no driver type name matches '%s'.\n", devtype);
	else if (matchcount == 1) {
	    assert(forcetype != NULL);
	    gpsd_report(LOG_PROG, "%s driver selected.\n", forcetype->type_name);
	} else {
	    forcetype = NULL;
	    gpsd_report(LOG_ERROR, "%d driver type names match '%s'.\n",
			matchcount, devtype);
	}
    }    

    if (((int)to_nmea + (int)to_binary + (int)reset) > 1) {
	gpsd_report(LOG_ERROR, "make up your mind, would you?\n");
	exit(EXIT_SUCCESS);
    }

    (void) signal(SIGINT, onsig);
    (void) signal(SIGTERM, onsig);
    (void) signal(SIGQUIT, onsig);

    /*@-nullpass@*/ /* someday, add null annotation to the gpsopen() params */
    if (!lowlevel) {
	/* Try to open the stream to gpsd. */
	if (gps_open(NULL, NULL, &gpsdata) != 0) {
	    gpsd_report(LOG_ERROR, "no gpsd running or network error: %s.\n", 
			netlib_errstr(errno));
	    lowlevel = true;
	}
    }
    /*@-nullpass@*/

    if (!lowlevel) {
	int i, devcount;

	/* what devices have we available? */
	if (!gps_query(&gpsdata, DEVICELIST_SET, (int)timeout, "?DEVICES;\n")) {
	    gpsd_report(LOG_ERROR, "no DEVICES response received.\n"); 
	    (void)gps_close(&gpsdata);
	    exit(EXIT_FAILURE);
	}
	if (gpsdata.devices.ndevices == 0) {
	    gpsd_report(LOG_ERROR, "no devices connected.\n"); 
	    (void)gps_close(&gpsdata);
	    exit(EXIT_FAILURE);
	} else if (gpsdata.devices.ndevices > 1 && device == NULL) {
	    gpsd_report(LOG_ERROR, 
			"multiple devices and no device specified.\n");
	    (void)gps_close(&gpsdata);
	    exit(EXIT_FAILURE);
	}
	gpsd_report(LOG_PROG,"%d device(s) found.\n",gpsdata.devices.ndevices);

	/* try to mine the devicelist return for the data we want */
	if (gpsdata.devices.ndevices == 1 && device == NULL) {
	    device = gpsdata.dev.path;
	    i = 0;
	} else {
	    assert(device != NULL);
	    for (i = 0; i < gpsdata.devices.ndevices; i++)
		if (strcmp(device, gpsdata.devices.list[i].path) == 0) {
		    goto devicelist_entry_matches;
		}
	    gpsd_report(LOG_ERROR, "specified device not found in device list.\n");
	    (void)gps_close(&gpsdata);
	    exit(EXIT_FAILURE);
	devicelist_entry_matches:;
	}
	(void)memcpy(&gpsdata.dev, 
		     &gpsdata.devices.list[i],
		     sizeof(struct devconfig_t));
	devcount = gpsdata.devices.ndevices;

	/* if the device has not identified, watch it until it does so */
	if (gpsdata.dev.driver[0] == '\0') {
	    if (gps_stream(&gpsdata, WATCH_ENABLE|WATCH_JSON, NULL) == -1) {
		gpsd_report(LOG_ERROR, "stream set failed.\n");
		(void)gps_close(&gpsdata);
		exit(EXIT_FAILURE);
	    }

	    while (devcount > 0) {
		errno = 0;
		if (gps_read(&gpsdata) == -1) {
		    gpsd_report(LOG_ERROR, "data read failed.\n");
		    (void)gps_close(&gpsdata);
		    exit(EXIT_FAILURE);
		}

		if (gpsdata.set & DEVICE_SET) {
		    --devcount;
		    assert(gpsdata.dev.path[0]!='\0' && gpsdata.dev.driver[0]!='\0');
		    if (strcmp(gpsdata.dev.path, device) == 0) {
			goto matching_device_seen;
		    }
		}
	    }
	    gpsd_report(LOG_ERROR, "data read failed.\n");
	    (void)gps_close(&gpsdata);
	    exit(EXIT_FAILURE);
	matching_device_seen:;
	}

	/* sanity check */
	if (gpsdata.dev.driver[0] == '\0') {
	    gpsd_report(LOG_SHOUT, "%s can't be identified.\n",
			gpsdata.dev.path);
	    (void)gps_close(&gpsdata);
	    exit(EXIT_SUCCESS);
	}

	/* if no control operation was specified, just ID the device */
	if (speed==NULL && rate == NULL && !to_nmea && !to_binary && !reset) {
		gpsd_report(LOG_SHOUT, "%s identified as %s at %d\n",
			    gpsdata.dev.path,
			    gpsdata.dev.driver,
			    gpsdata.dev.baudrate);
	}

	status = 0;
#ifdef RECONFIGURE_ENABLE
	if (reset)
	{
	    gpsd_report(LOG_PROG, "cannot reset with gpsd running.\n");
	    exit(EXIT_SUCCESS);
	}

	/*@-boolops@*/
	if (to_nmea) {
	    if (!gps_query(&gpsdata, DEVICE_SET, (int)timeout, "?DEVICE={\"path\":\"%s\",\"native\":0}\r\n", device) || (gpsdata.dev.driver_mode != MODE_NMEA)) {
		gpsd_report(LOG_ERROR, "%s mode change to NMEA failed\n", gpsdata.dev.path);
		status = 1;
	    } else
		gpsd_report(LOG_PROG, "%s mode change succeeded\n", gpsdata.dev.path);
	}
	else if (to_binary) {
	    if (gps_query(&gpsdata, DEVICE_SET, (int)timeout, "?DEVICE={\"path\":\"%s\",\"native\":1}\r\n", device) || (gpsdata.dev.driver_mode != MODE_BINARY)) {
		gpsd_report(LOG_ERROR, "%s mode change to native mode failed\n", gpsdata.dev.path);
		status = 1;
	    } else
		gpsd_report(LOG_PROG, "%s mode change succeeded\n", gpsdata.dev.path);
	}
	/*@+boolops@*/
	if (speed != NULL) {
	    char parity = 'N';
	    char stopbits = '1';
	    if (strchr(speed, ':') == NULL)
		(void)gps_query(&gpsdata,
				DEVICE_SET, (int)timeout,
				 "?DEVICE={\"path\":\"%s\",\"bps\":%s}\r\n", 
				 device, speed);
	    else {
		char *modespec = strchr(speed, ':');
		/*@ +charint @*/
		status = 0;
		if (modespec!=NULL) {
		    *modespec = '\0';
		    if (strchr("78", *++modespec) == NULL) {
			gpsd_report(LOG_ERROR, "No support for that word lengths.\n");
			status = 1;
		    }
		    parity = *++modespec;
		    if (strchr("NOE", parity) == NULL) {
			gpsd_report(LOG_ERROR, "What parity is '%c'?\n", parity);
			status = 1;
		    }
		    stopbits = *++modespec;
		    if (strchr("12", stopbits) == NULL) {
			gpsd_report(LOG_ERROR, "Stop bits must be 1 or 2.\n");
			status = 1;
		    }
		}
		if (status == 0)
		    (void)gps_query(&gpsdata,
				    DEVICE_SET, (int)timeout,
				     "?DEVICE={\"path\":\"%s\",\"bps\":%s,\"parity\":\"%c\",\"stopbits\":%c}\r\n", 
				     device, speed, parity, stopbits);
	    }
	    if (atoi(speed) != (int)gpsdata.dev.baudrate) {
		gpsd_report(LOG_ERROR, "%s driver won't support %s%c%c\n", 
			    gpsdata.dev.path,
			    speed, parity, stopbits);
		status = 1;
	    } else
		gpsd_report(LOG_PROG, "%s change to %s%c%c succeeded\n", 
			    gpsdata.dev.path,
			    speed, parity, stopbits);
	}
	if (rate != NULL) {
	    (void)gps_query(&gpsdata, 
			    DEVICE_SET, (int)timeout,
			    "?DEVICE={\"path\":\"%s\",\"cycle\":%s}\n", 
			    device, rate);
	}
#endif /* RECONFIGURE_ENABLE */
	(void)gps_close(&gpsdata);
	exit(status);
#ifdef RECONFIGURE_ENABLE
    } else if (reset) {
	/* hard reset will go through lower-level operations */
	const int speeds[] = {2400, 4800, 9600, 19200, 38400, 57600, 115200};
	static struct gps_context_t	context;	/* start it zeroed */
	static struct gps_device_t	session;	/* zero this too */
	int i;

	if (device == NULL || forcetype == NULL) {
		gpsd_report(LOG_ERROR, "device and type must be specified for the reset operation.\n");
		exit(EXIT_FAILURE);
	    }

	/*@ -mustfreeonly -immediatetrans @*/
	gps_context_init(&context);
	context.debug = debuglevel;
	session.context = &context;
	gpsd_tty_init(&session);
	(void)strlcpy(session.gpsdata.dev.path, device, sizeof(session.gpsdata.dev.path));
	session.device_type = forcetype;
	(void)gpsd_open(&session);
	(void)gpsd_set_raw(&session);
	(void)session.device_type->speed_switcher(&session, 4800, 'N', 1);
	(void)tcdrain(session.gpsdata.gps_fd);
	for(i = 0; i < (int)(sizeof(speeds) / sizeof(speeds[0])); i++) {
	    (void)gpsd_set_speed(&session, speeds[i], 'N', 1);
	    (void)session.device_type->speed_switcher(&session, 4800, 'N', 1);
	    (void)tcdrain(session.gpsdata.gps_fd);
	}
	gpsd_set_speed(&session, 4800, 'N', 1);
	for (i = 0; i < 3; i++)
	    if (session.device_type->mode_switcher)
		session.device_type->mode_switcher(&session, MODE_NMEA);
	gpsd_wrap(&session);
	exit(EXIT_SUCCESS);
	/*@ +mustfreeonly +immediatetrans @*/
#endif /* RECONFIGURE_ENABLE */
    } else {
	/* access to the daemon failed, use the low-level facilities */
	static struct gps_context_t	context;	/* start it zeroed */
	static struct gps_device_t	session;	/* zero this too */
	/*@ -mustfreeonly -immediatetrans @*/
	gps_context_init(&context);
	context.debug = debuglevel;
	session.context = &context;	/* in case gps_init isn't called */

	if (echo)
	    context.readonly = true;

	(void) alarm(timeout);
	(void) signal(SIGALRM, onsig);
	/*
	 * Unless the user has forced a type and only wants to see the
	 * string (not send it) we now need to try to open the device
	 * and find out what is actually there.
	 */
	if (!(forcetype != NULL && echo)) {
	    int seq;

	    if (device == NULL) {
		gpsd_report(LOG_ERROR, "device must be specified for low-level access.\n");
		exit(EXIT_FAILURE);
	    }
	    gpsd_time_init(&context, time(NULL));
	    gpsd_init(&session, &context, device);
	    gpsd_report(LOG_PROG, "initialization passed.\n");
	    if (gpsd_activate(&session) == -1) {
		gpsd_report(LOG_ERROR,
			      "activation of device %s failed, errno=%d\n",
			      device, errno);
		exit(2);
	    }
	    /* hunt for packet type and serial parameters */
	    for (seq = 0; session.device_type == NULL; seq++) {
		if (get_packet(&session) == ERROR_SET) {
		    gpsd_report(LOG_ERROR,
				"autodetection failed.\n");
		    exit(2);
		} else if (session.packet.type > COMMENT_PACKET) {
		    gpsd_report(LOG_IO,
				"autodetection after %d reads finds packet type %d.\n", seq, session.packet.type);
		    (void) alarm(0);
		    break;
		}
	    }
	    gpsd_report(LOG_PROG, "%s looks like a %s at %d.\n",
			device, gpsd_id(&session), session.gpsdata.dev.baudrate);

	    if (forcetype!=NULL && strcmp("Generic NMEA", session.device_type->type_name) !=0 && strcmp(forcetype->type_name, session.device_type->type_name)!=0) {
		gpsd_report(LOG_ERROR, "'%s' doesn't match non-generic type '%s' of selected device.\n", forcetype->type_name, session.device_type->type_name);
	    }

	    /* 
	     * If we've identified this as an NMEA device, we have to eat
	     * packets for a while to see if one of our probes elicits an
	     * ID response telling us that it's really a SiRF or
	     * something.  If so, the libgpsd(3) layer will automatically
	     * redispatch to the correct driver type.
	     */
	    if (strcmp(session.device_type->type_name, "Generic NMEA") == 0) {
		int dummy;
		for (dummy = 0; dummy < REDIRECT_SNIFF; dummy++) {
		    if ((get_packet(&session) & DEVICEID_SET)!=0)
			break;
		}
	    }
	    gpsd_report(LOG_SHOUT, "%s identified as a %s at %d.\n",
			device, gpsd_id(&session), session.gpsdata.dev.baudrate);
	}

	/* if no control operation was specified, we're done */
	if (speed==NULL && !to_nmea && !to_binary && control==NULL)
	    exit(EXIT_SUCCESS);

	/* maybe user wants to see the packet rather than send it */
	if (echo)
	    session.gpsdata.gps_fd = fileno(stdout);

	/* control op specified; maybe we forced the type */
	if (forcetype != NULL)
	    (void)gpsd_switch_driver(&session, forcetype->type_name);

	/* now perform the actual control function */
	status = 0;
#ifdef RECONFIGURE_ENABLE
	/*@ -nullderef @*/
	if (to_nmea || to_binary) {
	    bool write_enable = context.readonly;
	    context.readonly = false;
	    if (session.device_type->mode_switcher == NULL) {
		gpsd_report(LOG_SHOUT, 
			      "%s devices have no mode switch.\n",
			      session.device_type->type_name);
		status = 1;
	    } else {
		int target_mode = to_nmea ? MODE_NMEA : MODE_BINARY;

		gpsd_report(LOG_SHOUT, 
			      "switching to mode %s.\n",
			    to_nmea ? "NMEA" : "BINARY");
		session.device_type->mode_switcher(&session, target_mode);
		settle(&session);
	    }
	    context.readonly = write_enable;
	}
	if (speed) {
	    char parity = echo ? 'N': session.gpsdata.dev.parity;
	    int stopbits = echo ? 1 : session.gpsdata.dev.stopbits;
	    char *modespec;

	    modespec = strchr(speed, ':');
	    /*@ +charint @*/
	    status = 0;
	    if (modespec!=NULL) {
		*modespec = '\0';
		if (strchr("78", *++modespec) == NULL) {
		    gpsd_report(LOG_ERROR, "No support for that word lengths.\n");
		    status = 1;
		}
		parity = *++modespec;
		if (strchr("NOE", parity) == NULL) {
		    gpsd_report(LOG_ERROR, "What parity is '%c'?\n", parity);
		    status = 1;
		}
		stopbits = *++modespec;
		if (strchr("12", parity) == NULL) {
		    gpsd_report(LOG_ERROR, "Stop bits must be 1 or 2.\n");
		    status = 1;
		}
		stopbits = (int)(stopbits-'0');
	    }
	    if (status == 0) {
		if (session.device_type->speed_switcher == NULL) {
		    gpsd_report(LOG_ERROR, 
				"%s devices have no speed switch.\n",
				session.device_type->type_name);
		    status = 1;
		}
		else if (session.device_type->speed_switcher(&session, 
							     (speed_t)atoi(speed),
							     parity, 
							     stopbits)) {
		    settle(&session);
		    gpsd_report(LOG_PROG, "%s change to %s%c%d succeeded\n", 
			    session.gpsdata.dev.path,
			    speed, parity, stopbits);
		} else {
		    gpsd_report(LOG_ERROR, "%s driver won't support %s%c%d.\n",
				session.gpsdata.dev.path,
				speed, parity, stopbits);
		    status = 1;
		}
	    }
	}
	if (rate) {
	    bool write_enable = context.readonly;
	    context.readonly = false;
	    if (session.device_type->rate_switcher == NULL) {
		gpsd_report(LOG_ERROR, 
			      "%s devices have no rate switcher.\n",
			      session.device_type->type_name);
		status = 1;
	    } else {
		double rate_dbl = strtod(rate, NULL);

		if (!session.device_type->rate_switcher(&session, rate_dbl)) {
		    gpsd_report(LOG_ERROR, "rate switch failed.\n");
		    status = 1;
		}
		settle(&session);
	    }
	    context.readonly = write_enable;
	}
#endif /* RECONFIGURE_ENABLE */
#ifdef CONTROLSEND_ENABLE
	/*@ -compdef @*/
	if (control) {
	    bool write_enable = context.readonly;
	    context.readonly = false;
	    if (session.device_type->control_send == NULL) {
		gpsd_report(LOG_ERROR, 
			      "%s devices have no control sender.\n",
			      session.device_type->type_name);
		status = 1;
	    } else {
		if (session.device_type->control_send(&session, 
						      cooked, 
						      (size_t)cooklen) == -1) {
		    gpsd_report(LOG_ERROR, "control transmission failed.\n");
		    status = 1;
		}
		settle(&session);
	    }
	    context.readonly = write_enable;
	}
	/*@ +compdef @*/
#endif /* CONTROLSEND_ENABLE */

	exit(status);
	/*@ +nullderef @*/
	/*@ +mustfreeonly +immediatetrans @*/
    }
}
