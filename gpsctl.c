/* $Id$ */
/*
 * gpsctl.c -- tweak the control settings on a GPS
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#ifndef S_SPLINT_S
#include <unistd.h>
#endif /* S_SPLINT_S */
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <assert.h>
#include <signal.h>

#include "gpsd_config.h"
#include "gpsd.h"

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

static void onsig(int sig)
{
    if (sig == SIGALRM) {
	(void)fputs("gpsctl: packet recognition timed out.\n", stdout);
	exit(1);
    } else {
	(void)printf("gpsctl: killed by signal %d\n", sig);
	exit(0);
    }
}

int main(int argc, char **argv)
{
    int option, status;
    char *err_str, *device = NULL, *speed = NULL, *devtype = NULL, *control = NULL;
    bool to_binary = false, to_nmea = false, reset = false; 
    bool lowlevel=false, echo=false;
    struct gps_data_t *gpsdata = NULL;
    struct gps_type_t *forcetype = NULL;
    struct gps_type_t **dp;
    char cooked[BUFSIZ];
    ssize_t cooklen = 0;
    int timeout = 4;

#define USAGE	"usage: gpsctl [-l] [-b | -n | -r] [-D n] [-s speed] [-T timeout] [-V] [-t devtype] [-c control] [-e] <device>\n"
    while ((option = getopt(argc, argv, "bc:efhlnrs:t:D:T:V")) != -1) {
	switch (option) {
	case 'b':		/* switch to vendor binary mode */
	    to_binary = true;
	    break;
	case 'c':		/* ship specified control string */
	    control = optarg;
	    lowlevel = true;
	    if ((cooklen = hex_escapes(cooked, control)) <= 0) {
		gpsd_report(LOG_ERROR, 
			    "invalid escape string (error %d)\n", (int)cooklen);
		exit(1);
	    }
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
		if ((*dp)->mode_switcher != NULL)
		    (void)fputs("-[bn]\t", stdout);
		else
		    (void)fputc('\t', stdout);
		if ((*dp)->speed_switcher != NULL)
		    (void)fputs("-s\t", stdout);
		else
		    (void)fputc('\t', stdout);
		if ((*dp)->control_send != NULL || strcmp((*dp)->type_name, "uBlox UBX binary")==0)
		    (void)fputs("-c\t", stdout);
		else
		    (void)fputc('\t', stdout);
		(void)puts((*dp)->type_name);
	    }
	    exit(0);
	case 'n':		/* switch to NMEA mode */
	    to_nmea = true;
	    break;
	case 'r':		/* force-switch to default mode */
	    reset = true;
	    lowlevel = false;	/* so we'll abort if the daemon is running */
	    break;
	case 's':		/* change output baud rate */
	    speed = optarg;
	    break;
	case 't':		/* force the device type */
	    devtype = optarg;
	    break;
	case 'T':		/* force the device type */
	    timeout = atoi(optarg);
	    break;
	case 'D':		/* set debugging level */
	    debuglevel = atoi(optarg);
	    gpsd_hexdump_level = debuglevel;
	    break;
	case 'V':
	    (void)fprintf(stderr, "gpsctl at svn revision $Rev$\n");
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
	(void)fprintf(stderr, "gpsctl: make up your mind, would you?\n");
	exit(0);
    }

    (void) signal(SIGINT, onsig);
    (void) signal(SIGTERM, onsig);
    (void) signal(SIGQUIT, onsig);

    if (!lowlevel) {
	/* Try to open the stream to gpsd. */
	/*@i@*/gpsdata = gps_open(NULL, NULL);
	if (gpsdata == NULL) {
	    switch (errno) {
	    case NL_NOSERVICE: err_str ="can't get service entry"; break;
	    case NL_NOHOST:    err_str ="can't get host entry"; break;
	    case NL_NOPROTO:   err_str ="can't get protocol entry"; break;
	    case NL_NOSOCK:    err_str ="can't create socket"; break;
	    case NL_NOSOCKOPT: err_str ="error SETSOCKOPT SO_REUSEADDR"; break;
	    case NL_NOCONNECT: err_str ="can't connect"; break;
	    default:           err_str ="Unknown"; break;
	    } 
	    (void)fprintf(stderr, 
			  "gpsctl: no gpsd running or network error: %s.\n", 
			  err_str);
	    lowlevel = true;
	}
    }

    if (!lowlevel) {
	/* OK, there's a daemon instance running.  Do things the easy way */
	assert(gpsdata != NULL);
	(void)gps_query(gpsdata, "K\n");
	if (gpsdata->ndevices == 0) {
	    (void)fprintf(stderr, "gpsctl: no devices connected.\n"); 
	    (void)gps_close(gpsdata);
	    exit(1);
	} else if (gpsdata->ndevices > 1 && device == NULL) {
	    (void)fprintf(stderr, 
			  "gpsctl: multiple devices and no device specified.\n");
	    (void)gps_close(gpsdata);
	    exit(1);
	}
	gpsd_report(LOG_PROG, "gpsctl: %d device(s) found.\n", gpsdata->ndevices);

	if (gpsdata->ndevices > 1) {
	    int i;
	    assert(device != NULL);
	    for (i = 0; i < gpsdata->ndevices; i++)
		if (strcmp(device, gpsdata->devicelist[i]) == 0)
		    goto foundit;
	    (void)fprintf(stderr,  "gpsctl: specified device not found.\n");
	    (void)gps_close(gpsdata);
	    exit(1);
	foundit:
	    (void)gps_query(gpsdata, "F=%s", device);
	}

	/* if no control operation was specified, just ID the device */
	if (speed==NULL && !to_nmea && !to_binary && !reset) {
	    /* the O is to force a device binding */
	    (void)gps_query(gpsdata, "OFIB");
	    gpsd_report(LOG_SHOUT, "gpsctl: %s identified as %s at %d\n",
			gpsdata->gps_device,gpsdata->gps_id,gpsdata->baudrate);
	    exit(0);
	}

	if (reset)
	{
	    gpsd_report(LOG_PROG, "gpsctl: cannot reset with gpsd running.\n");
	    exit(0);
	}

	status = 0;
	if (to_nmea) {
	    (void)gps_query(gpsdata, "N=0");
	    if (gpsdata->driver_mode != MODE_NMEA) {
		(void)fprintf(stderr, "gpsctl: %s mode change to NMEA failed\n", gpsdata->gps_device);
		status = 1;
	    } else
		gpsd_report(LOG_PROG, "gpsctl: %s mode change succeeded\n", gpsdata->gps_device);
	}
	else if (to_binary) {
	    (void)gps_query(gpsdata, "N=1");
	    if (gpsdata->driver_mode != MODE_BINARY) {
		(void)fprintf(stderr, "gpsctl: %s mode change to native mode failed\n", gpsdata->gps_device);
		status = 1;
	    } else
		gpsd_report(LOG_PROG, "gpsctl: %s mode change succeeded\n", gpsdata->gps_device);
	}
	if (speed != NULL) {
	    (void)gps_query(gpsdata, "B=%s", speed);
	    if (atoi(speed) != (int)gpsdata->baudrate) {
		(void)fprintf( stderr, "gpsctl: %s speed change failed\n", gpsdata->gps_device);
		status = 1;
	    } else
		gpsd_report(LOG_PROG, "gpsctl: %s speed change succeeded\n", gpsdata->gps_device);
	}
	(void)gps_close(gpsdata);
	exit(status);
    } else if (reset) {
	/* hard reset will go through lower-level operations */
	const int speeds[] = {2400, 4800, 9600, 19200, 38400, 57600, 115200};
	static struct gps_context_t	context;	/* start it zeroed */
	static struct gps_device_t	session;	/* zero this too */
	int i;

	if (device == NULL || forcetype == NULL) {
		(void)fprintf(stderr,  "gpsctl: device and type must be specified for the reset operation.\n");
		exit(1);
	    }

	/*@ -mustfreeonly -immediatetrans @*/
	session.context = &context;
	gpsd_tty_init(&session);
	(void)strlcpy(session.gpsdata.gps_device, device, sizeof(session.gpsdata.gps_device));
	session.device_type = forcetype;
	(void)gpsd_open(&session);
	(void)gpsd_set_raw(&session);
	(void)session.device_type->speed_switcher(&session, 4800);
	(void)tcdrain(session.gpsdata.gps_fd);
	for(i = 0; i < (int)(sizeof(speeds) / sizeof(speeds[0])); i++) {
	    (void)gpsd_set_speed(&session, speeds[i], 'N', 1);
	    (void)session.device_type->speed_switcher(&session, 4800);
	    (void)tcdrain(session.gpsdata.gps_fd);
	}
	gpsd_set_speed(&session, 4800, 'N', 1);
	for (i = 0; i < 3; i++)
	    if (session.device_type->mode_switcher)
		session.device_type->mode_switcher(&session, MODE_NMEA);
	gpsd_wrap(&session);
	exit(0);
	/*@ +mustfreeonly +immediatetrans @*/
    } else {
	/* access to the daemon failed, use the low-level facilities */
	static struct gps_context_t	context;	/* start it zeroed */
	static struct gps_device_t	session;	/* zero this too */
	/*@ -mustfreeonly -immediatetrans @*/
	session.context = &context;	/* in case gps_init isn't called */

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
		(void)fprintf(stderr,  "gpsctl: device must be specified for low-level access.\n");
		exit(1);
	    }
	    gpsd_init(&session, &context, device);
	    gpsd_report(LOG_PROG, "gpsctl: initialization passed.\n");
	    if (gpsd_activate(&session, false) == -1) {
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
				"autodectection after %d reads.\n", seq);
		    (void) alarm(0);
		    break;
		}
	    }
	    gpsd_report(LOG_PROG, "gpsctl: %s looks like a %s at %d.\n",
			device, gpsd_id(&session), session.gpsdata.baudrate);

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
	    gpsd_report(LOG_SHOUT, "gpsctl: %s identified as a %s at %d.\n",
			device, gpsd_id(&session), session.gpsdata.baudrate);
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
		    /* suppresses probing for subrtpes */
		    context.readonly = true;
		    (void)sleep(1);
		    (void) alarm(timeout);
		    for (;;) {
			if (get_packet(&session) == ERROR_SET) {
			    continue;
			} else if (session.packet.type == target_type) {
			    alarm(0);
			    break;
			}
		    }
		    context.readonly = false;
		}
		gpsd_report(LOG_SHOUT, "after mode change, %s looks like a %s at %d.\n",
			    device, gpsd_id(&session), session.gpsdata.baudrate);
	    }
	}
	if (speed) {
	    if (session.device_type->speed_switcher == NULL) {
		gpsd_report(LOG_ERROR, 
			    "%s devices have no speed switch.\n",
			    session.device_type->type_name);
		status = 1;
	    }
	    else if (!session.device_type->speed_switcher(&session, 
							  (speed_t)atoi(speed))) {
		(void)fprintf(stderr, "gpsctl: speed change failed.\n");
		status = 1;
	    }
	}
	/*@ -compdef @*/
	if (control) {
	    /* 
	     * First, handle exceptional cases.  These are GPS chips
	     * like the Zodiac and UBX in which the packet payload is 
	     * not contiguous,with class or type fields before the data
	     * length.  This means they don't fit well in the model 
	     * assumed by the driver interface.
	     */
	    extern struct gps_type_t ubx_binary;
	    /*@ -usedef @*/
	    if (session.device_type == &ubx_binary) {
		if (!ubx_write(session.gpsdata.gps_fd,
			       (unsigned)cooked[0], (unsigned)cooked[1],
			       (unsigned char *)cooked + 2, 
			       (unsigned short)(cooklen - 2))) {
		    gpsd_report(LOG_ERROR, "control transmission failed.\n");
		    status = 1;
		}
      
	    } 
	    /*@ +usedef @*/
	    else if (session.device_type->control_send == NULL) {
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
	}
	/*@ +compdef @*/


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
