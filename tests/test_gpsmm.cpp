/*
 * Copyright (C) 2010 Eric S. Raymond.
 *
 * This software is distributed under a BSD-style license. See the
 * file "COPYING" in the top-level directory of the distribution for details.
 *
 */

/* This simple program shows the basic functionality of the C++ wrapper class */
#include <iostream>

#include <getopt.h>

#include "../libgpsmm.h"
#include "../timespec.h"
#include "../timespec_str.c"
#include "../gpsdclient.c"
/*     YES   --->  ^^^^
 Using .c rather than the .h to embed gpsd_source_spec() source here
  so that it is compiled in C++ rather than C of the gps library
 (otherwise fails to link as the signatures are unavailable/different)
*/
using namespace std;

void printfinite(double val)
{
    if (0 == isfinite(val))
        (void)fputs(" N/A   ", stdout);
    else
        (void)fprintf(stdout, "%6.2f ", val);
}

/*
 * We should get libgps_dump_state() from the client library, but
 * scons has a bug; we can't get it to add -lgps to the link line,
 * apparently because it doesn't honor parse_flags on a Program()
 * build of a C++ file.
 */
static void libgps_dump_state(struct gps_data_t *collect)
{
    char ts_str[TIMESPEC_LEN];

    /* no need to dump the entire state, this is a sanity check */
#ifndef USE_QT
    (void)fprintf(stdout, "flags: (0x%04x) %s\n",
                  (unsigned int)collect->set, gps_maskdump(collect->set));
#endif
    if (collect->set & ONLINE_SET)
        (void)fprintf(stdout, "ONLINE: %s\n",
                      timespec_str(&collect->online, ts_str, sizeof(ts_str)));
    if (collect->set & TIME_SET)
        (void)fprintf(stdout, "TIME: %s\n",
                      timespec_str(&collect->fix.time, ts_str, sizeof(ts_str)));

    if (collect->set & LATLON_SET)
        (void)fprintf(stdout, "LATLON: lat/lon: %lf %lf\n",
                      collect->fix.latitude, collect->fix.longitude);
    if (collect->set & ALTITUDE_SET)
        (void)fprintf(stdout, "ALTITUDE: altHAE: %lf  U: climb: %lf\n",
                      collect->fix.altHAE, collect->fix.climb);
    if (collect->set & SPEED_SET)
        (void)fprintf(stdout, "SPEED: %lf\n", collect->fix.speed);
    if (collect->set & TRACK_SET)
        (void)fprintf(stdout, "TRACK: track: %lf\n", collect->fix.track);
    if (collect->set & CLIMB_SET)
        (void)fprintf(stdout, "CLIMB: climb: %lf\n", collect->fix.climb);
    if (collect->set & STATUS_SET)
        (void)fprintf(stdout, "STATUS: status: %d\n", collect->status);
    if (collect->set & MODE_SET)
        (void)fprintf(stdout, "MODE: mode: %d\n", collect->fix.mode);
    if (collect->set & DOP_SET)
        (void)fprintf(stdout,
                      "DOP: satellites %d, pdop=%lf, hdop=%lf, vdop=%lf\n",
                      collect->satellites_used, collect->dop.pdop,
                      collect->dop.hdop, collect->dop.vdop);
    if (collect->set & VERSION_SET)
        (void)fprintf(stdout, "VERSION: release=%s rev=%s proto=%d.%d\n",
                      collect->version.release,
                      collect->version.rev,
                      collect->version.proto_major,
                      collect->version.proto_minor);
    if (collect->set & POLICY_SET)
        (void)fprintf(stdout,
                      "POLICY: watcher=%s nmea=%s raw=%d scaled=%s timing=%s, devpath=%s\n",
                      collect->policy.watcher ? "true" : "false",
                      collect->policy.nmea ? "true" : "false",
                      collect->policy.raw,
                      collect->policy.scaled ? "true" : "false",
                      collect->policy.timing ? "true" : "false",
                      collect->policy.devpath);
    if (collect->set & SATELLITE_SET) {
        int i;

        (void)fprintf(stdout, "SKY: satellites in view: %d\n",
                      collect->satellites_visible);
        for (i = 0; i < collect->satellites_visible; i++) {
            (void)fprintf(stdout, "    %3d", collect->skyview[i].PRN);
            printfinite(collect->skyview[i].elevation);
            printfinite(collect->skyview[i].azimuth);
            printfinite(collect->skyview[i].ss);
            (void)fprintf(stdout, collect->skyview[i].used ? " Y\n" : " N\n");
        }
    }
    if (collect->set & DEVICE_SET)
        (void)fprintf(stdout, "DEVICE: Device is '%s', driver is '%s'\n",
                      collect->dev.path, collect->dev.driver);
#ifdef OLDSTYLE_ENABLE
    if (collect->set & DEVICEID_SET)
        (void)fprintf(stdout, "GPSD ID is %s\n", collect->dev.subtype);
#endif /* OLDSTYLE_ENABLE */
    if (collect->set & DEVICELIST_SET) {
        int i;
        (void)fprintf(stdout, "DEVICELIST:%d devices:\n",
                      collect->devices.ndevices);
        for (i = 0; i < collect->devices.ndevices; i++) {
            (void)fprintf(stdout, "%d: path='%s' driver='%s'\n",
                          collect->devices.ndevices,
                          collect->devices.list[i].path,
                          collect->devices.list[i].driver);
        }
    }
}


int main(int argc, char *argv[])
{
    uint looper = UINT_MAX;

    // A typical C++ program may look to use a more native option parsing method
    //  such as boost::program_options
    // But for this test program we don't want extra dependencies
    // Hence use C style getopt for (build) simplicity
    int option;
    while ((option = getopt(argc, argv, "l:h?")) != -1) {
        switch (option) {
        case 'l':
            looper = atoi(optarg);
            break;
        case '?':
        case 'h':
        default:
            cout << "usage: " << argv[0] << " [-l n]\n";
            exit(EXIT_FAILURE);
            break;
        }
    }

    struct fixsource_t source;
    /* Grok the server, port, and device. */
    if (optind < argc) {
        gpsd_source_spec(argv[optind], &source);
    } else
        gpsd_source_spec(NULL, &source);

    //gpsmm gps_rec("localhost", DEFAULT_GPSD_PORT);
    gpsmm gps_rec(source.server, source.port);

    if ( !((std::string)source.server == (std::string)GPSD_SHARED_MEMORY ||
           (std::string)source.server == (std::string)GPSD_DBUS_EXPORT) ) {
        if (gps_rec.stream(WATCH_ENABLE|WATCH_JSON) == NULL) {
            cerr << "No GPSD running.\n";
            return 1;
        }
    }

    // Loop for the specified number of times
    // If not specified then by default it loops until ll simply goes out of bounds
    // So with the 5 second wait & a 4 byte uint - this equates to ~680 years
    //  - long enough for a test program :)
    for (uint ll = 0; ll < looper; ll++) {
        struct gps_data_t* newdata;

        if (!gps_rec.waiting(5000000))
          continue;

        if ((newdata = gps_rec.read()) == NULL) {
            cerr << "Read error.\n";
            return 1;
        } else {
            libgps_dump_state(newdata);
        }
    }

    cout << "Exiting\n";
    return 0;
}

