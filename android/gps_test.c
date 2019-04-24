/*
 * This file is Copyright (c) 2019 by the GPSD project
 * SPDX-License-Identifier: BSD-2-clause
 */

#include <stdlib.h>
#include <time.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include "gps.h"

int main (int argc, char **argv) {
    struct gps_data_t gps_data;
    int gpsopen = -2;

    /* debug
     * FILE *fp = fopen("/data/bin/gpslog", "a+");
     * gps_enable_debug(3, fp);
     */

    printf("Usage: gpsdtest [host]\n\n");

    memset(&gps_data, 0, sizeof(gps_data));

    while (1) {
	if (gpsopen < 0) {
	    gpsopen = gps_open((argc == 1 ? "localhost" : argv[1]),
                               "2947", &gps_data);
	    if (0 == gpsopen) {
		printf("gps_open returned 0 (success)\n");
		gps_stream(&gps_data, WATCH_ENABLE, NULL);
	    } else {
		printf("gps_open failed, returned: %d\n", gpsopen);
		gpsopen = -1;
		sleep(5);
		continue;
	    }
	}

	if (gps_waiting (&gps_data, 2000000)) {
	    errno = 0;
	    if (gps_read (&gps_data, NULL, 0) != -1) {
		if (gps_data.status >= 1 && gps_data.fix.mode >= 2){
		    printf("\nHave a fix: ");
		    if (gps_data.fix.mode == 2)
                        printf("2D\n");
                    else
                        printf("3D\n");

		    printf("Latitude: %f\n", gps_data.fix.latitude);
		    printf("Longitude: %f\n", gps_data.fix.longitude);
		    printf("Speed: %f\n", gps_data.fix.speed);
		    printf("Bearing: %f\n", gps_data.fix.track);
		    printf("H Accuracy: %f\n", gps_data.fix.eph);
		    printf("S Accuracy: %f\n", gps_data.fix.eps);
		    printf("B Accuracy: %f\n", gps_data.fix.epd);
		    printf("Time: %ld\n", (long) gps_data.fix.time);
		    printf("Altitude: %f\n", gps_data.fix.altitude);
		    printf("V Accuracy: %f\n\n", gps_data.fix.epv);
		}

		printf("Satellites visible: %d\n",
                       gps_data.satellites_visible);
		for (int i = 0; i < gps_data.satellites_visible; i++) {
		    printf("SV type: ");
		    switch (gps_data.skyview[i].gnssid) {
                    case 0:
                            printf("GPS, ");
                            break;
                    case 1:
                            printf("SBAS, ");
                            break;
                    case 2:
                            printf("Galileo, ");
                            break;
                    case 3:
                            printf("Beidou, ");
                            break;
                    case 4:
                            printf("Unknown, ");
                            break;
                    case 5:
                            printf("QZSS, ");
                            break;
                    case 6:
                            printf("Glonass, ");
                            break;
		    }

		    printf("SVID: %d, SNR: %d, Elevation: %d, "
                           "Azimuth: %d, Used: %d\n",
                            gps_data.skyview[i].svid,
                            (int)gps_data.skyview[i].ss,
                            gps_data.skyview[i].elevation,
                            gps_data.skyview[i].azimuth,
                            gps_data.skyview[i].used);
		}
	    }
	}
    }
}
