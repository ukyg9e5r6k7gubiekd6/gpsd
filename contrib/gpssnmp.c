/* gpssnmp - poll local gpsd for SNMP variables
 * 
 * Copyright (c) 2016 David Taylor <gpsd@david.taylor.name>
 *
 * Copyright (c)2018 by the GPSD project
 * SPDX-License-Identifier: BSD-2-clause
 *
 * To build this:
 *     gcc -o gpssnmp gpssnmp.c -lgps
 *
 */

#include <gps.h>
#include <stdio.h>
#include <string.h>

static void usage() {
    printf("\n"
        "Usage:\n"
        "\n"
        "to get OID_VISIBLE\n"
        "   $ gpssnmp -g .1.3.6.1.2.1.25.1.31\n"
        "   .1.3.6.1.2.1.25.1.31\n"
        "   gauge\n"
        "   13\n"
        "\n"
        "to get OID_USED\n"
        "   $ gpssnmp -g .1.3.6.1.2.1.25.1.32\n"
        "   .1.3.6.1.2.1.25.1.32\n"
        "   gauge\n"
        "   4\n"
        "\n"
        "to get OID_SNR_AVG\n"
        "   $ gpssnmp -g .1.3.6.1.2.1.25.1.33\n"
        "   .1.3.6.1.2.1.25.1.33\n"
        "   gauge\n"
        "   22.250000\n"
        "\n");
}

int main (int argc, char **argv) {
  struct gps_data_t gpsdata;

  #define OID_VISIBLE ".1.3.6.1.2.1.25.1.31"
  #define OID_USED ".1.3.6.1.2.1.25.1.32"
  #define OID_SNR_AVG ".1.3.6.1.2.1.25.1.33"

  if ((argc > 2) && (strcmp ("-g", argv[1]) == 0)) {
    int i;
    double snr_total=0;
    double snr_avg = 0.0;
    int status, used, visible;

    status = gps_open (GPSD_SHARED_MEMORY, DEFAULT_GPSD_PORT, &gpsdata);
    status = gps_read (&gpsdata);
    used  = gpsdata.satellites_used;
    visible = gpsdata.satellites_visible;
    for(i=0; i<=used; i++) {
        if (gpsdata.skyview[i].used > 0 && gpsdata.skyview[i].ss > 1) {
//          printf ("i: %d, P:%d, ss: %f\n", i, gpsdata.skyview[i].PRN,
//                  gpsdata.skyview[i].ss);
            snr_total+=gpsdata.skyview[i].ss;
        }
    }
    gps_close (&gpsdata);
    if (used > 0) {
        snr_avg = snr_total / used;
    }
    if (strcmp (OID_VISIBLE, argv[2]) == 0) {
	printf (OID_VISIBLE);
	printf ("\n");
	printf ("gauge\n");
	printf ("%d\n", visible);
    }
    if (strcmp (OID_USED, argv[2]) == 0) {
	printf (OID_USED);
	printf ("\n");
	printf ("gauge\n");
	printf ("%d\n", used);
    }
    if (strcmp (OID_SNR_AVG, argv[2]) == 0) {
	printf (OID_SNR_AVG);
	printf ("\n");
	printf ("gauge\n");
	printf ("%lf\n", snr_avg);
    }
  } else {
    usage();
  }
  return 0;
}
