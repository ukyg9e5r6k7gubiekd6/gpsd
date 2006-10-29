/*
 * gpsctrl.c -- tweak the control settings on a GPS
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <getopt.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <assert.h>

#include "gpsd_config.h"
#include "gpsd.h"

static int debuglevel;

void gpsd_report(int errlevel UNUSED, const char *fmt, ... )
/* our version of the logger */
{
    if (errlevel <= debuglevel) {
	va_list ap;
	va_start(ap, fmt);
	(void)vfprintf(stderr, fmt, ap);
	va_end(ap);
    }
}

int main(int argc, char **argv)
{
    int option, status;
    char *err_str, *device = NULL, *speed = NULL;
    bool to_binary = false, to_nmea = false, lowlevel=false;
    struct gps_data_t *gpsdata;

    while ((option = getopt(argc, argv, "bfhns:D:V")) != -1) {
	switch (option) {
	case 'b':
	    to_binary = true;
	    break;
	case 'f':
	    lowlevel = true;	/* force direct access to the deamon */
	    break;
	case 'n':
	    to_nmea = true;
	    break;
	case 's':
	    speed = optarg;
	    break;
	case 'D':
	    debuglevel = atoi(optarg);
	    break;
	case 'V':
	    (void)fprintf(stderr, "gpsctrl at svn revision $Rev$\n");
	    break;
	case 'h':
	default:
	    fprintf(stderr, "usage: gpsctrl [-b | -n] [-s speed] [-V]\n");
	    break;
	}
    }

    if (optind < argc)
	device = argv[optind];

    if (to_nmea && to_binary) {
	(void)fprintf(stderr, "gpsctrl: make up your mind, would you?\n");
	exit(0);
    }

    if (speed==NULL && !to_nmea && !to_binary)
	exit(0);

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
	    case NL_NOCONNECT: err_str ="can't connect to host"; break;
	    default:           err_str ="Unknown"; break;
	    } 
	    (void)fprintf(stderr, 
			  "gpsctrl: no gpsd running or network error: %d, %s\n", 
			  errno, err_str);
	    lowlevel = true;
	}
    }

    if (!lowlevel) {
	/* OK, there's a daemon instance running.  Do things the easy way */
	(void)gps_query(gpsdata, "K\n");
	if (gpsdata->ndevices == 0) {
	    (void)fprintf(stderr, "gpsctrl: no devices connected.\n"); 
	    (void)gps_close(gpsdata);
	    exit(1);
	} else if (gpsdata->ndevices > 1 && device == NULL) {
	    (void)fprintf(stderr, 
			  "gpsctrl: multiple devices and no device specified.\n");
	    (void)gps_close(gpsdata);
	    exit(1);
	}

	if (gpsdata->ndevices > 1) {
	    int i;
	    assert(device != NULL);
	    for (i = 0; i < gpsdata->ndevices; i++)
		if (strcmp(device, gpsdata->devicelist[i]) == 0)
		    goto foundit;
	    (void)fprintf(stderr,  "gpsctrl: specified device not found.\n");
	    (void)gps_close(gpsdata);
	    exit(1);
	foundit:
	    (void)gps_query(gpsdata, "F=%s", device);
	}

	status = 0;
	if (to_nmea) {
	    (void)gps_query(gpsdata, "N=0");
	    if (gpsdata->driver_mode != 0) {
		(void)fprintf(stderr, "gpsctrl: mode change failed\n");
		status = 1;
	    }
	}
	else if (to_binary) {
	    (void)gps_query(gpsdata, "N=1");
	    if (gpsdata->driver_mode != 1) {
		(void)fprintf( stderr, "gpsctrl: mode change failed\n");
		status = 1;
	    }
	}
	if (speed != NULL) {
	    (void)gps_query(gpsdata, "B=%s", speed);
	    if (atoi(speed) != (int)gpsdata->baudrate) {
		(void)fprintf( stderr, "gpsctrl: speed change failed\n");
		status = 1;
	    }
	}
	(void)gps_close(gpsdata);
	exit(status);
    } else {
	/* access to the daemon failed, use the low-level facilities */
	struct gps_context_t	context;
	struct gps_device_t	session;

	if (device == NULL) {
	    (void)fprintf(stderr,  "gpsctrl: device must be specified for low-level access.\n");
	    exit(1);
	}
	gpsd_init(&session, &context, device);
	if (gpsd_activate(&session) == -1) {
	    (void)fprintf(stderr, 
			  "gpsd: activation of device %s failed, errno=%d\n",
			  device, errno);
	    exit(2);
	}
	/* hunt for packet type and serial parameters */
	while (session.device_type == NULL)
	    gpsd_poll(&session);

	status = 0;
	if (to_nmea) {
	    session.device_type->mode_switcher(&session, 0);
	    if (session.gpsdata.driver_mode != 0) {
		(void)fprintf(stderr, "gpsctrl: mode change failed\n");
		status = 1;
	    }
	}
	if (to_binary) {
	    session.device_type->mode_switcher(&session, 1);
	    if (session.gpsdata.driver_mode != 1) {
		(void)fprintf(stderr, "gpsctrl: mode change failed\n");
		status = 1;
	    }
	}
	if (speed)
	    if (!session.device_type->speed_switcher(&session, 
						     (speed_t)atoi(speed))) {
		(void)fprintf(stderr, "gpsctrl: mode change failed\n");
		status = 1;
	    }

	gpsd_wrap(&session);
	exit(status);
    }
}
