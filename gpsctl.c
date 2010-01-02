/* $Id$ */
/*
 * gpsctl.c -- tweak the control settings on a GPS
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include "gpsd_config.h"
#if HAVE_SYS_IOCTL_H
 #include <sys/ioctl.h>
#endif /* HAVE_SYS_IOCTL_H */
#ifndef S_SPLINT_S
#include <unistd.h>
#endif /* S_SPLINT_S */
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <assert.h>
#include <signal.h>

#include "gpsd.h"
#include "revision.h"

static int debuglevel;

/*
 * Set this as high or higher than the maximum number of subtype 
 * probes in drivers.c.
 */
#define REDIRECT_SNIFF	15

void gpsd_report(int errlevel UNUSED, const char *fmt, ... )
/* our version of the logger */
{
    if (errlevel <= debuglevel) {
	va_list ap;
	va_start(ap, fmt);
	(void)fputs("gpsctl: ", stderr);
	(void)vfprintf(stderr, fmt, ap);
	va_end(ap);
    }
}

/*@ -noret @*/
static gps_mask_t get_packet(struct gps_device_t *session)
/* try to get a well-formed packet from the GPS */
{
    gps_mask_t fieldmask;

    for (;;) {
	int waiting = 0;
	(void)ioctl(session->gpsdata.gps_fd, FIONREAD, &waiting);
	if (waiting == 0) {
	    (void)usleep(300);
	    continue;
	}
	fieldmask = gpsd_poll(session);
	if ((fieldmask &~ ONLINE_SET)!=0)
	    return fieldmask;
    }
}
/*@ +noret @*/

static int gps_query(struct gps_data_t *gpsdata, const char *fmt, ... )
/* query a gpsd instance for new data */
{
    char buf[BUFSIZ];
    va_list ap;
    int ret;

    va_start(ap, fmt);
    (void)vsnprintf(buf, sizeof(buf)-2, fmt, ap);
    va_end(ap);
    if (buf[strlen(buf)-1] != '\n')
	(void)strlcat(buf, "\n", BUFSIZ);
    if (write(gpsdata->gps_fd, buf, strlen(buf)) <= 0) {
	gpsd_report(LOG_ERROR, "gps_query(), write failed\n");
	return -1;
    }
    gpsd_report(LOG_PROG, "gps_query(), wrote, %s\n", buf);
    ret = gps_poll(gpsdata);
    if (ERROR_SET & gpsdata->set) {
	gpsd_report(LOG_ERROR, "gps_query() error '%s'\n", gpsdata->error);
    }
    return ret;

}

static void onsig(int sig)
{
    if (sig == SIGALRM) {
	gpsd_report(LOG_ERROR, "packet recognition timed out.\n");
	exit(1);
    } else {
	gpsd_report(LOG_ERROR, "killed by signal %d\n", sig);
	exit(0);
    }
}

int main(int argc, char **argv)
{
    int option, status;
    char *device = NULL, *devtype = NULL; 
    char *speed = NULL, *control = NULL, *rate = NULL;
    bool to_binary = false, to_nmea = false, reset = false; 
    bool lowlevel=false, echo=false;
    struct gps_data_t *gpsdata = NULL;
    const struct gps_type_t *forcetype = NULL;
    const struct gps_type_t **dp;
    unsigned int timeout = 4;
#ifdef ALLOW_CONTROLSEND
    char cooked[BUFSIZ];
    ssize_t cooklen = 0;
#endif /* ALLOW_RECONFIGURE */

#define USAGE	"usage: gpsctl [-l] [-b | -n | -r] [-D n] [-s speed] [-c rate] [-T timeout] [-V] [-t devtype] [-x control] [-e] <device>\n"
    while ((option = getopt(argc, argv, "bec:fhlnrs:t:x:D:T:V")) != -1) {
	switch (option) {
	case 'b':		/* switch to vendor binary mode */
	    to_binary = true;
	    break;
	case 'c':
#ifdef ALLOW_RECONFIGURE
	    rate = optarg;
#else
	    gpsd_report(LOG_ERROR, "cycle-change capability has been conditioned out.\n");
#endif /* ALLOW_RECONFIGURE */
	    break;
	case 'x':		/* ship specified control string */
#ifdef ALLOW_CONTROLSEND
	    control = optarg;
	    lowlevel = true;
	    if ((cooklen = hex_escapes(cooked, control)) <= 0) {
		gpsd_report(LOG_ERROR, 
			    "invalid escape string (error %d)\n", (int)cooklen);
		exit(1);
	    }
#else
	    gpsd_report(LOG_ERROR, "control_send capability has been conditioned out.\n");	    
#endif /* ALLOW_CONTROLSEND */
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
#ifdef ALLOW_RECONFIGURE
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
#endif /* ALLOW_RECONFIGURE */
#ifdef ALLOW_CONTROLSEND
		if ((*dp)->control_send != NULL)
		    (void)fputs("-x\t", stdout);
		else
		    (void)fputc('\t', stdout);
#endif /* ALLOW_CONTROLSEND */
		(void)puts((*dp)->type_name);
	    }
	    exit(0);
	case 'n':		/* switch to NMEA mode */
#ifdef ALLOW_RECONFIGURE
	    to_nmea = true;
#else
	    gpsd_report(LOG_ERROR, "speed-change capability has been conditioned out.\n");
#endif /* ALLOW_RECONFIGURE */
	    break;
	case 'r':		/* force-switch to default mode */
#ifdef ALLOW_RECONFIGURE
	    reset = true;
	    lowlevel = false;	/* so we'll abort if the daemon is running */
#else
	    gpsd_report(LOG_ERROR, "reset capability has been conditioned out.\n");
#endif /* ALLOW_RECONFIGURE */
	    break;
	case 's':		/* change output baud rate */
#ifdef ALLOW_RECONFIGURE
	    speed = optarg;
#else
	    gpsd_report(LOG_ERROR, "speed-change capability has been conditioned out.\n");
#endif /* ALLOW_RECONFIGURE */
	    break;
	case 't':		/* force the device type */
	    devtype = optarg;
	    break;
	case 'T':		/* force the device type */
	    timeout = (unsigned)atoi(optarg);
	    break;
	case 'D':		/* set debugging level */
	    debuglevel = atoi(optarg);
	    gpsd_hexdump_level = debuglevel;
#ifdef CLIENTDEBUG_ENABLE
	    gps_enable_debug(debuglevel, stderr);
#endif /* CLIENTDEBUG_ENABLE */
	    break;
	case 'V':
	    (void)fprintf(stderr, "gpsctl: version %s (revision %s)\n",
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

    if ((int)to_nmea + (int)to_binary + (int)reset > 1) {
	gpsd_report(LOG_ERROR, "make up your mind, would you?\n");
	exit(0);
    }

    (void) signal(SIGINT, onsig);
    (void) signal(SIGTERM, onsig);
    (void) signal(SIGQUIT, onsig);

    if (!lowlevel) {
	/* Try to open the stream to gpsd. */
	/*@i@*/gpsdata = gps_open(NULL, NULL);
	if (gpsdata == NULL) {
	    gpsd_report(LOG_ERROR, "no gpsd running or network error: %s.\n", 
			netlib_errstr(errno));
	    lowlevel = true;
	}
    }

    if (!lowlevel) {
	/* OK, there's a daemon instance running.  Do things the easy way */
	struct devconfig_t *devlistp;
	assert(gpsdata != NULL);
	(void)gps_poll(gpsdata);
	if ((gpsdata->set & DEVICELIST_SET) != 0) {
	    gpsd_report(LOG_ERROR, "no VERSION response received; update your gpsd.\n"); 
	    (void)gps_close(gpsdata);
	    exit(1);
	}
	(void)gps_query(gpsdata, "?DEVICES;\n");
	if ((gpsdata->set & DEVICELIST_SET) == 0) {
	    gpsd_report(LOG_ERROR, "no DEVICES response received.\n"); 
	    (void)gps_close(gpsdata);
	    exit(1);
	}

	if (gpsdata->devices.ndevices == 0) {
	    gpsd_report(LOG_ERROR, "no devices connected.\n"); 
	    (void)gps_close(gpsdata);
	    exit(1);
	} else if (gpsdata->devices.ndevices > 1 && device == NULL) {
	    gpsd_report(LOG_ERROR, 
			"multiple devices and no device specified.\n");
	    (void)gps_close(gpsdata);
	    exit(1);
	}
	gpsd_report(LOG_PROG,"%d device(s) found.\n",gpsdata->devices.ndevices);

	if (gpsdata->devices.ndevices == 1) {
	    devlistp = &gpsdata->devices.list[0];
	    device = devlistp->path;
	} else {
	    int i;
	    assert(device != NULL);
	    for (i = 0; i < gpsdata->devices.ndevices; i++)
		if (strcmp(device, gpsdata->devices.list[i].path) == 0)
		    goto foundit;
	    gpsd_report(LOG_ERROR, "specified device not found.\n");
	    (void)gps_close(gpsdata);
	    exit(1);
	foundit:
	    devlistp = &gpsdata->devices.list[i];
	}

	/* if no control operation was specified, just ID the device */
	if (speed==NULL && rate == NULL && !to_nmea && !to_binary && !reset) {
	    gpsd_report(LOG_SHOUT, "%s identified as %s at %d\n",
			devlistp->path, devlistp->driver, devlistp->baudrate);
	    exit(0);
	}

	status = 0;
#ifdef ALLOW_RECONFIGURE
	if (reset)
	{
	    gpsd_report(LOG_PROG, "cannot reset with gpsd running.\n");
	    exit(0);
	}

	/*@-boolops@*/
	if (to_nmea) {
	    (void)gps_query(gpsdata, "?DEVICE={\"path\":\"%s\",\"native\":0}\r\n", device); 
	    if ((gpsdata->set & ERROR_SET) || (gpsdata->dev.driver_mode != MODE_NMEA)) {
		gpsd_report(LOG_ERROR, "%s mode change to NMEA failed\n", gpsdata->dev.path);
		status = 1;
	    } else
		gpsd_report(LOG_PROG, "%s mode change succeeded\n", gpsdata->dev.path);
	}
	else if (to_binary) {
	    (void)gps_query(gpsdata, "?DEVICE={\"path\":\"%s\",\"native\":1}\r\n", device);
	    if ((gpsdata->set & ERROR_SET) || (gpsdata->dev.driver_mode != MODE_BINARY)) {
		gpsd_report(LOG_ERROR, "%s mode change to native mode failed\n", gpsdata->dev.path);
		status = 1;
	    } else
		gpsd_report(LOG_PROG, "%s mode change succeeded\n", gpsdata->dev.path);
	}
	/*@+boolops@*/
	if (speed != NULL) {
	    char parity = 'N';
	    char stopbits = '1';
	    if (strchr(speed, ':') == NULL)
		(void)gps_query(gpsdata,
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
		    (void)gps_query(gpsdata, 
				    "?DEVICE={\"path\":\"%s\",\"bps\":%s,\"parity\":\"%c\",\"stopbits\":%c}\r\n", 
				    device, speed, parity, stopbits);
	    }
	    if (atoi(speed) != (int)gpsdata->dev.baudrate) {
		gpsd_report(LOG_ERROR, "%s driver won't support %s%c%c\n", 
			    gpsdata->dev.path,
			    speed, parity, stopbits);
		status = 1;
	    } else
		gpsd_report(LOG_PROG, "%s change to %s%c%c succeeded\n", 
			    gpsdata->dev.path,
			    speed, parity, stopbits);
	}
	if (rate != NULL) {
	    (void)gps_query(gpsdata, 
			    "?DEVICE={\"path\":\"%s\",\"cycle\":%s}\n", 
			    device, rate);
	}
#endif /* ALLOW_RECONFIGURE */
	(void)gps_close(gpsdata);
	exit(status);
#ifdef ALLOW_RECONFIGURE
    } else if (reset) {
	/* hard reset will go through lower-level operations */
	const int speeds[] = {2400, 4800, 9600, 19200, 38400, 57600, 115200};
	static struct gps_context_t	context;	/* start it zeroed */
	static struct gps_device_t	session;	/* zero this too */
	int i;

	if (device == NULL || forcetype == NULL) {
		gpsd_report(LOG_ERROR, "device and type must be specified for the reset operation.\n");
		exit(1);
	    }

	/*@ -mustfreeonly -immediatetrans @*/
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
	exit(0);
	/*@ +mustfreeonly +immediatetrans @*/
#endif /* ALLOW_RECONFIGURE */
    } else {
	/* access to the daemon failed, use the low-level facilities */
	static struct gps_context_t	context;	/* start it zeroed */
	static struct gps_device_t	session;	/* zero this too */
	/*@ -mustfreeonly -immediatetrans @*/
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
		exit(1);
	    }
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
		} else {
		    gpsd_report(LOG_IO,
				"autodetection after %d reads.\n", seq);
		    (void) alarm(0);
		    break;
		}
	    }
	    gpsd_report(LOG_PROG, "%s looks like a %s at %d.\n",
			device, gpsd_id(&session), session.gpsdata.dev.baudrate);

	    if (forcetype!=NULL && strcmp("Generic NMEA", session.device_type->type_name) !=0 && strcmp(forcetype->type_name, session.device_type->type_name)!=0) {
		gpsd_report(LOG_ERROR, "'%s' doesn't match non-generic type '%s' of selected device.", forcetype->type_name, session.device_type->type_name);
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
	    exit(0);

	/* maybe user wants to see the packet rather than send it */
	if (echo)
	    session.gpsdata.gps_fd = fileno(stdout);

	/* control op specified; maybe we forced the type */
	if (forcetype != NULL)
	    (void)gpsd_switch_driver(&session, forcetype->type_name);

	/* now perform the actual control function */
	status = 0;
#ifdef ALLOW_RECONFIGURE
	/*@ -nullderef @*/
	if (to_nmea || to_binary) {
	    if (session.device_type->mode_switcher == NULL) {
		gpsd_report(LOG_SHOUT, 
			      "%s devices have no mode switch.\n",
			      session.device_type->type_name);
		status = 1;
	    } else {
		int target_mode = to_nmea ? MODE_NMEA : MODE_BINARY;
		int target_type = to_nmea ? NMEA_PACKET : session.device_type->packet_type;

		gpsd_report(LOG_SHOUT, 
			      "switching to mode %s.\n",
			    to_nmea ? "NMEA" : "BINARY");
		session.device_type->mode_switcher(&session, target_mode);


		/* 
		 * Hunt for packet type again (mode might have
		 * changed).  We've found by experiment that you can't
		 * close the connection to the device after a mode
		 * change but before you see a packet of the right
		 * type come back from it - otherwise you can hit a
		 * timing window where the mode-change control message
		 * gets ignored or flushed.
		 */
		if (!echo) {
		    /* suppresses probing for subtypes */
		    context.readonly = true;
		    (void)sleep(1);
		    (void) alarm(timeout);
		    for (;;) {
			if (get_packet(&session) == ERROR_SET) {
			    continue;
			} else if (session.packet.type == target_type) {
			    (void)alarm(0);
			    break;
			}
		    }
		    context.readonly = false;
		}
		/*@ -nullpass @*/
		gpsd_report(LOG_SHOUT, "after mode change, %s looks like a %s at %d.\n",
			    device, gpsd_id(&session), session.gpsdata.dev.baudrate);
		/*@ +nullpass @*/
	    }
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
		    /*
		     * See the 'deep black magic' comment in
		     * gpsd.c:set_serial() Probably not needed here,
		     * but it can't hurt.
		     */
		    (void)tcdrain(session.gpsdata.gps_fd);
		    (void)usleep(50000);
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
	    }
	    context.readonly = write_enable;
	}
#endif /* ALLOW_RECONFIGURE */
#ifdef ALLOW_CONTROLSEND
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
	    }
	    context.readonly = write_enable;
	}
	/*@ +compdef @*/
#endif /* ALLOW_CONTROLSEND */

	if (forcetype == NULL || !echo) {
	    /*
	     * Give the device time to settle before closing it.  Alas, this is
	     * voodoo programming; we don't know it will have any effect, but
	     * GPSes are notoriously prone to timing-dependent errors.
	     */
	    (void)usleep(300000);

	    gpsd_wrap(&session);
	}
	exit(status);
	/*@ +nullderef @*/
	/*@ +mustfreeonly +immediatetrans @*/
    }
}
