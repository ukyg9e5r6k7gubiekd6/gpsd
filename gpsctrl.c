/*
 * gpsctrl.c -- tweak the control settings on a GPS
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <getopt.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include "gpsd_config.h"
#include "gps.h"

int main(int argc, char **argv)
{
    int option, status;
    char *err_str, *device = NULL, *speed = NULL;
    char buf[80];
    bool to_binary = false, to_nmea = false;
    struct gps_data_t *gpsdata;

    while ((option = getopt(argc, argv, "bhns:V")) != -1) {
	switch (option) {
	case 'b':
	    to_binary = true;
	    break;
	case 'n':
	    to_nmea = true;
	    break;
	case 's':
	    speed = optarg;
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

    /* Open the stream to gpsd. */
    /*@i@*/gpsdata = gps_open(NULL, NULL);
    if (!gpsdata) {
	switch (errno) {
	case NL_NOSERVICE:  err_str = "can't get service entry"; break;
	case NL_NOHOST:     err_str = "can't get host entry"; break;
	case NL_NOPROTO:    err_str = "can't get protocol entry"; break;
	case NL_NOSOCK:     err_str = "can't create socket"; break;
	case NL_NOSOCKOPT:  err_str = "error SETSOCKOPT SO_REUSEADDR"; break;
	case NL_NOCONNECT:  err_str = "can't connect to host"; break;
	default:            err_str = "Unknown"; break;
	} 
	(void)fprintf( stderr, 
		       "gpsctrl: no gpsd running or network error: %d, %s\n", 
		       errno, err_str);
	exit(2);
    }

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
}
