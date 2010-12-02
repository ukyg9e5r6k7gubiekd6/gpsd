/* test driver for the ECEF to WGS84 conversions in geoid.c
 *
 * This file is Copyright (c) 2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details. 
 */

#include <stdlib.h>

#include "gpsd.h"

int main(int argc, char **argv)
{
    double lat, lon;

    if (argc != 3) {
	fprintf(stderr, "Usage: %s lat lon\n", argv[0]);
	return 1;
    }

    lat = atof(argv[1]);
    lon = atof(argv[2]);

    if (lat > 90. || lat < -90.) {
	fprintf(stderr, " -90 <= lat=%s(%.f) <= 90 ?\n", argv[1], lat);
	return 1;
    }

    if (lon > 180. || lat < -180.) {
	fprintf(stderr, " -180 <= lon=%s(%.f) <= 180 ?\n", argv[2], lon);
	return 1;
    }

    printf(" lat= %f lon= %f geoid correction= %f\n",
	   lat, lon, wgs84_separation(lat, lon));

    return 0;
}
