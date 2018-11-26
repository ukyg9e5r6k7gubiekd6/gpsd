/*
 * gpsrinex: read "RAW" messages from a gpsd and output a RINEX 3 obs file.
 *
 * gpsrinex will read live data from gpsd and create a file of RINEX 3
 * observations.  Currently this only works if the GPS is a u-blox
 * GPS and is sending UBX-RXM-RAWX messages.
 *
 * The u-blox must be configured for u-blox binary messages.  GLONASS,
 * GALILEO, and BEIDOU must be off.  Optionally SBAS on, but can be
 * flakey.
 *
 * Too much data for 9600!
 *
 * To configure a u-blox to output the proper data:
 *    # gpsctl -s 115200
 *    # sleep 2
 *    # ubxtool -d NMEA
 *    # ubstool -e BINARY
 *    # ubxtool -d GLONASS
 *    # ubxtool -d BEIDOU
 *    # ubxtool -d GALILEO
 *    # ubxtool -d SBAS
 *    # ubxtool -e RAWX
 *
 * After collecting the default number of observations, gpsrinex will
 * create the RINEX .obs file and exit.  Upload this file to an
 * offline processing service to get cm accuracy.
 *
 * One service known to work with obsrinex output is [CSRS-PPP]:
 *  https://webapp.geod.nrcan.gc.ca/geod/tools-outils/ppp.php
 *
 * Examples:
 *    To collect 4 hours of samples as 30 second intervals:
 *        # gpsrinex -i 30 -n 480
 *
 * To generate RINEX 3 from a u-blox capture file:
 *     Grab 4 hours of raw live data:
 *         # gpspipe -x 14400 -R > 4h-raw.ubx
 *     Feed that data to gpsfake:
 *         # gpsfake -1 -P 3000 4h-raw.ubx
 *     In another window, convert that raw to RINEX 3:
 *         # gpsrinex -i 1 -n 1000000
 *
 * See also:
 *     [1] RINEX: The Receiver Independent Exchange Format, Version 3.03
 *     ftp://igs.org/pub/data/format/rinex303.pdf
 *
 *     [2] GPSTk, http://www.gpstk.org/
 *
 *     [3] Nischan, Thomas (2016):
 *     GFZRNX - RINEX GNSS Data Conversion and Manipulation Toolbox.
 *     GFZ  Data Services.  http://dx.doi.org/10.5880/GFZ.1.1.2016.002
 *
 *     [4] RTKLIB: An Open Source Program Package for GNSS Positioning
 *     http://www.rtklib.com/
 *
 * This file is Copyright (c) 2018 by the GPSD project
 * SPDX-License-Identifier: BSD-2-clause
 *
 */

#ifdef __linux__
/* isfinite() needs _POSIX_C_SOURCE >= 200112L
 * isnan(+Inf) is false, isfinite(+Inf) is false
 * use isfinite() to make sure a float is valid
 */
#define _POSIX_C_SOURCE 200112L
#endif /* __linux__ */

#ifndef _XOPEN_SOURCE
/* need >= 500 for strdup() */
#define _XOPEN_SOURCE 500
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <errno.h>
#include <libgen.h>
#include <signal.h>
#include <assert.h>
#include <unistd.h>

#include "gps.h"
#include "gpsd_config.h"
#include "gpsdclient.h"
#include "revision.h"
#include "os_compat.h"

static char *progname;
static struct fixsource_t source;
static double ecefx = 0.0;
static double ecefy = 0.0;
static double ecefz = 0.0;
static timespec_t start_time = {0};      /* report gen time, UTC */
static timespec_t first_mtime = {0};     /* GPS time, not UTC */
static timespec_t last_mtime = {0};      /* GPS time, not UTC */

/* total count of observations by gnssid [0-6]
 *  0 = GPS       RINEX G
 *  1 = SBAS      RINEX S
 *  2 = Galileo   RINEX E
 *  3 - BeiDou    RINEX C
 *  4 = IMES      not supported by RINEX
 *  5 = QZSS      RINEX J
 *  6 = GLONASS   RINEX R
 *      IRNSS     RINEX I
 *
 * RINEX 3 observation codes [1]:
 * C1C  L1 C/A Pseudorange
 * C1P  L1 P Pseudorange
 * C1W  L1 Z-tracking Pseudorange
 * D1C  L1 C/A Doppler
 * L1C  L1 C/A Carrier Phase
 * L1P  L1 P Carrier Phase
 * L1W  L1 Z-tracking Carrier Phase
 * C2C  L2 C/A Pseudorange
 * C2P  L2 P Pseudorange
 * C2W  L2 Z-tracking Pseudorange
 * D2C  L2 C/A Doppler
 * L2C  L2 C/A Carrier phase
 * L2P  L1 P Carrier Phase
 * L2W  L2 Z-tracking Carrier Phase
 *
 * CSRS-PPP supports:
 * GPS:      C1C  L1C  C2C  L2C  C1W  L1W  C2W  L2W
 * GLONASS : C1C  L1C  C2C  L2C  C1P  L1P  C2P  L2P
 *
 */
typedef enum {C1C = 0, D1C, L1C, C2C, D2C, L2C, CODEMAX} obs_codes;
/* structure to hold count of observations by gnssid:svid
 * MAXCHANNEL+1 is just a WAG of max size */
#define MAXCNT (MAXCHANNELS + 1)
static struct obs_cnt_t {
        unsigned char gnssid;
        unsigned char svid;     /* svid of 0 means unused slot */
        obs_codes obs_cnts[CODEMAX+1];
        unsigned int count;        /* count of obscode */
} obs_cnt[MAXCNT] = {0};

static FILE * tmp_file;             /* file handle for temp file */
static int sample_count = 20;       /* number of measurement sets to get */
/* seconds between measurement sets */
static unsigned int sample_interval = 30;
static int debug = 0;               /* debug level */
static struct gps_data_t gpsdata;
static FILE *log_file;

/* convert a gnssid to the RINEX 3 constellation code
 * see [1] Section 3.5
 */
static char gnssid2rinex(int gnssid)
{
    switch (gnssid) {
    case 0:     /* 0 = GPS */
        return 'G';
    case 1:     /* 1 = SBAS */
        return 'S';
    case 2:     /* 2 = Galileo */
        return 'E';
    case 3:     /* 3 = BeiDou */
        return 'C';
    case 4:     /* 4 = IMES - unsupported */
        return 'X';
    case 5:     /* 5 = QZSS */
        return 'J';
    case 6:     /* 6 = GLONASS */
        return 'R';
    default:    /* Huh? */
        return 'x';
    }
}

/* obs_cnt_inc()
 *
 * increment an observation count
 */
static void obs_cnt_inc(unsigned char gnssid, unsigned char svid,
                        obs_codes obs_code)
{
    int i;

    if (CODEMAX <= obs_code) {
        /* should never happen... */
        fprintf(stderr, "ERROR: obs_code_inc() obs_code %d out of range\n",
                obs_code);
        exit(1);
    }
    /* yeah, slow and ugly, linear search. */
    for (i = 0; i < MAXCNT; i++) {
        if (0 == obs_cnt[i].svid) {
            /* end of list, not found, so add this item */
            obs_cnt[i].gnssid = gnssid;
            obs_cnt[i].svid = svid;
            obs_cnt[i].obs_cnts[obs_code] = 1;
            break;
        }
        if (obs_cnt[i].gnssid != gnssid) {
            continue;
        }
        if (obs_cnt[i].svid != svid) {
            continue;
        }
        /* found it, increment it */
        obs_cnt[i].obs_cnts[obs_code]++;
        if (99999 < obs_cnt[i].obs_cnts[obs_code]) {
            /* RINEX 3 max is 99999 */
            obs_cnt[i].obs_cnts[obs_code] = 99999;
        }
        break;
    }
    /* fell out because table full, item added, or item incremented */
    return;
}

/* compare two obs_cnt, for sorting by gnssid, and svid */
static int compare_obs_cnt(const void  *A, const void  *B)
{
    const struct obs_cnt_t *a = (const struct obs_cnt_t *)A;
    const struct obs_cnt_t *b = (const struct obs_cnt_t *)B;
    unsigned char a_gnssid = a->gnssid;
    unsigned char b_gnssid = b->gnssid;

    /* 0 = svid means unused, make those last */
    if (0 == a->svid) {
	a_gnssid = 255;
    }
    if (0 == b->svid) {
	b_gnssid = 255;
    }
    if (a_gnssid != b_gnssid) {
        return a_gnssid - b_gnssid;
    }
    /* put unused last */
    if (a->svid != b->svid) {
        return a->svid - b->svid;
    }
    /* two blank records */
    return 0;
}

/* return number of unique PRN in a gnssid from obs_cnt.
 * return all PRNs if 255 == gnssid */
static int obs_cnt_prns(unsigned char gnssid)
{
    int i;
    int prn_cnt = 0;

    for (i = 0; i < MAXCNT; i++) {
        if (0 == obs_cnt[i].svid) {
            /* end of list, done */
            break;
        }
        if ((255 != gnssid) && (gnssid != obs_cnt[i].gnssid)) {
            /* wrong gnssid */
            continue;
        }
        prn_cnt++;
    }
    /* fell out because table full, item added, or item incremented */
    return prn_cnt;
}

/* print_rinex_header()
 * Print a RINEX 3 header to the file "log_file".
 * Some of the data in the header is only known after processing all
 * the raw data.
 */
static void print_rinex_header(void)
{
    int i, j;
    char tmstr[40];              /* time: yyyymmdd hhmmss UTC */
    struct tm *report_time;
    struct tm *first_time;
    struct tm *last_time;
    int cnt;                     /* number of obs for one sat */
    int prn_count[7] = {0};   /* count of PRN per gnssid */

    if ( 3 < debug) {
        (void)fprintf(stderr,"doing header\n");
    }

    report_time = gmtime(&(start_time.tv_sec));
    (void)strftime(tmstr, sizeof(tmstr), "%Y%m%d %H%M%S UTC", report_time);

    (void)fprintf(log_file,
        "%9s%11s%-20s%-20s%-20s\n",
        "3.03", "", "OBSERVATION DATA", "M: Mixed", "RINEX VERSION / TYPE");
    (void)fprintf(log_file,
        "%-20s%-20s%-20s%-20s\n",
        "gpsrinex 3.19-dev", "", tmstr,
        "PGM / RUN BY / DATE");
    (void)fprintf(log_file, "%-60s%-20s\n",
         "Source: gpsd live data", "COMMENT");
    (void)fprintf(log_file, "%-60s%-20s\n", "XXXX", "MARKER NAME");
    (void)fprintf(log_file, "%-60s%-20s\n", "NON_PHYSICAL", "MARKER TYPE");
    (void)fprintf(log_file, "%-20s%-20s%-20s%-20s\n",
                  "Unknown", "Unknown", "", "OBSERVER / AGENCY");
    (void)fprintf(log_file, "%-20s%-20s%-20s%-20s\n",
                  "", "", "", "REC # / TYPE / VERS");
    (void)fprintf(log_file, "%-20s%-20s%-16s%4s%-20s\n",
                  "", "", "", "NONE", "ANT # / TYPE");
    (void)fprintf(log_file, "%14.4f%14.4f%14.4f%18s%-20s\n",
        ecefx, ecefy, ecefz, "", "APPROX POSITION XYZ");
    (void)fprintf(log_file, "%14.4f%14.4f%14.4f%18s%-20s\n",
        0.0, 0.0, 0.0, "", "ANTENNA: DELTA H/E/N");
    (void)fprintf(log_file, "%6d%6d%48s%-20s\n", 1, 1,
         "", "WAVELENGTH FACT L1/2");

    /* get PRN stats */
    qsort(obs_cnt, MAXCNT, sizeof(struct obs_cnt_t), compare_obs_cnt);
    for (i = 0; i < 7; i++ ) {
        prn_count[i] = obs_cnt_prns(i);
    }
    /* CSRS-PPP needs C1C, L1C or C1C, L1C, D1C
     * CSRS-PPP refuses files with L1C first
     * convbin wants C1C, L1C, D1C
     * for some reason gfzrnx_lx wants C1C, D1C, L1C, not C1C, L1C, D1C */
    if (0 < prn_count[0]) {
        /* GPS */
        (void)fprintf(log_file, "%c%5d%4s%4s%4s%4s%4s%4s%4s%4s%22s%-20s\n",
             gnssid2rinex(0), 5, "C1C", "L1C", "D1C", "C2C", "L2C", "",
             "", "", "", "SYS / # / OBS TYPES");
    }
    if (0 < prn_count[1]) {
        /* SBAS, L1 and L5 only */
        (void)fprintf(log_file, "%c%5d%4s%4s%4s%4s%4s%4s%4s%4s%22s%-20s\n",
             gnssid2rinex(1), 3, "C1C", "L1C", "D1C", "", "", "",
             "", "", "", "SYS / # / OBS TYPES");
    }
    if (0 < prn_count[2]) {
        /* GALILEO, E1, E5 aand E6 only  */
        (void)fprintf(log_file, "%c%5d%4s%4s%4s%4s%4s%4s%4s%4s%22s%-20s\n",
             gnssid2rinex(2), 3, "C1C", "L1C", "D1C", "", "", "",
             "", "", "", "SYS / # / OBS TYPES");
    }
    if (0 < prn_count[3]) {
        /* BeiDou, BDS */
        (void)fprintf(log_file, "%c%5d%4s%4s%4s%4s%4s%4s%4s%4s%22s%-20s\n",
             gnssid2rinex(3), 5, "C1C", "L1C", "D1C", "C2C", "L2C", "",
             "", "", "", "SYS / # / OBS TYPES");
    }
    if (0 < prn_count[5]) {
        /* QZSS */
        (void)fprintf(log_file, "%c%5d%4s%4s%4s%4s%4s%4s%4s%4s%22s%-20s\n",
             gnssid2rinex(5), 5, "C1C", "L1C", "D1C", "C2C", "L2C", "",
             "", "", "", "SYS / # / OBS TYPES");
    }
    if (0 < prn_count[6]) {
        /* GLONASS */
        (void)fprintf(log_file, "%c%5d%4s%4s%4s%4s%4s%4s%4s%4s%22s%-20s\n",
             gnssid2rinex(6), 5, "C1C", "L1C", "D1C", "C2C", "L2C", "",
             "", "", "", "SYS / # / OBS TYPES");
    }

    (void)fprintf(log_file, "%6d%54s%-20s\n", obs_cnt_prns(255),
                  "", "# OF SATELLITES");
    /* get all the PRN / # OF OBS */
    for (i = 0; i <= MAXCNT; i++) {
        cnt = 0;

        if (0 == obs_cnt[i].svid) {
            /* done */
            break;
        }
        for (j = 0; j < CODEMAX; j++) {
            cnt += obs_cnt[i].obs_cnts[j];
        }
        if (0 > cnt) {
            /* no  counts for this sat */
            continue;
        }
        switch (obs_cnt[i].gnssid) {
        case 0:
            /* GPS */
	    (void)fprintf(log_file,"   %c%02d%6u%6u%6u%6u%6u%24s%-20s\n",
			  gnssid2rinex(obs_cnt[i].gnssid), obs_cnt[i].svid,
			  obs_cnt[i].obs_cnts[C1C],
			  obs_cnt[i].obs_cnts[L1C],
			  obs_cnt[i].obs_cnts[D1C],
			  obs_cnt[i].obs_cnts[C2C],
			  obs_cnt[i].obs_cnts[L2C],
			  "", "PRN / # OF OBS");
            break;
        case 1:
            /* SBAS */
            /* FALLTHROUGH */
        case 2:
            /* GALILEO */
            /* FALLTHROUGH */
        default:
	    (void)fprintf(log_file,"   %c%02d%6u%6u%6u%6s%6s%24s%-20s\n",
			  gnssid2rinex(obs_cnt[i].gnssid), obs_cnt[i].svid,
			  obs_cnt[i].obs_cnts[C1C],
			  obs_cnt[i].obs_cnts[L1C],
			  obs_cnt[i].obs_cnts[D1C],
			  "", "",
			  "", "PRN / # OF OBS");
        }
    }

    (void)fprintf(log_file, "%10.3f%50s%-20s\n",
                  (double)sample_interval, "", "INTERVAL");

    /* GPS time not UTC */
    first_time = gmtime(&(first_mtime.tv_sec));
    (void)fprintf(log_file, "%6d%6d%6d%6d%6d%5d.%07ld%8s%9s%-20s\n",
         first_time->tm_year + 1900,
         first_time->tm_mon + 1,
         first_time->tm_mday,
         first_time->tm_hour,
         first_time->tm_min,
         first_time->tm_sec,
         (long)(first_mtime.tv_nsec / 100),
         "GPS", "",
         "TIME OF FIRST OBS");

    /* GPS time not UTC */
    last_time = gmtime(&(last_mtime.tv_sec));
    (void)fprintf(log_file, "%6d%6d%6d%6d%6d%5d.%07ld%8s%9s%-20s\n",
         last_time->tm_year + 1900,
         last_time->tm_mon + 1,
         last_time->tm_mday,
         last_time->tm_hour,
         last_time->tm_min,
         last_time->tm_sec,
         (long)(last_mtime.tv_nsec / 100),
         "GPS", "",
         "TIME OF LAST OBS");
    if (0 < prn_count[0]) {
        /* GPS */
        (void)fprintf(log_file, "%-60s%-20s\n",
             "G L1C", "SYS / PHASE SHIFT");
        (void)fprintf(log_file, "%-60s%-20s\n",
             "G L2C", "SYS / PHASE SHIFT");
    }
    if (0 < prn_count[1]) {
        /* SBAS, L1 and L5 only */
        (void)fprintf(log_file, "%-60s%-20s\n",
             "S L1C", "SYS / PHASE SHIFT");
    }
    if (0 < prn_count[2]) {
        /* GALILEO, E1, E5 and E6 */
        (void)fprintf(log_file, "%-60s%-20s\n",
             "E L1C", "SYS / PHASE SHIFT");
    }
    if (0 < prn_count[3]) {
        /* BeiDou */
        (void)fprintf(log_file, "%-60s%-20s\n",
             "B L1C", "SYS / PHASE SHIFT");
        (void)fprintf(log_file, "%-60s%-20s\n",
             "B L2C", "SYS / PHASE SHIFT");
    }
    if (0 < prn_count[5]) {
        /* QZSS */
        (void)fprintf(log_file, "%-60s%-20s\n",
             "J L1C", "SYS / PHASE SHIFT");
        (void)fprintf(log_file, "%-60s%-20s\n",
             "J L2C", "SYS / PHASE SHIFT");
    }
    if (0 < prn_count[6]) {
        /* GLONASS */
        (void)fprintf(log_file, "%-60s%-20s\n",
             "R L1I", "SYS / PHASE SHIFT");
        (void)fprintf(log_file, "%-60s%-20s\n",
             "R L2I", "SYS / PHASE SHIFT");
    }
    (void)fprintf(log_file, "%-60s%-20s\n",
         "", "END OF HEADER");
    if ( 3 < debug) {
        (void)fprintf(stderr,"done header\n");
    }
    return;
}

/* print_rinex_footer()
 * print a RINEX 3 footer to the file "log_file".
 * Except RINEX 3 has no footer.  So what this really does is
 * call the header function, then move the processed observations from
 * "tmp_file" to "log_file".
 */
static void print_rinex_footer(void)
{
    char buffer[4096];

    /* print the header */
    print_rinex_header();
    /* now replay the data in the tmp_file into the output */
    (void)fflush(tmp_file);
    rewind(tmp_file);
    while (true) {
        size_t count;

        count = fread(buffer, 1, sizeof(buffer), tmp_file);
        if (0 >= count ) {
            break;
        }
        (void)fwrite(buffer, 1, count, log_file);
    }
    (void)fclose(tmp_file);
    (void)fclose(log_file);
    (void)gps_close(&gpsdata);
}

/* compare two meas_t, for sorting by gnssid, svid, and sigid */
static int compare_meas(const void  *A, const void  *B)
{
    const struct meas_t *a = (const struct meas_t*)A;
    const struct meas_t *b = (const struct meas_t*)B;

    if (a->gnssid != b->gnssid) {
        return a->gnssid - b->gnssid;
    }
    if (a->svid != b->svid) {
        return a->svid - b->svid;
    }
    if (a->sigid != b->sigid) {
        return a->sigid - b->sigid;
    }
    /* two blank records */
    return 0;
}


/* convert an observation item and return it as a (F14,3,I1,I1)
 * in a static buffer */
static const char * fmt_obs(double val, unsigned char lli, unsigned char snr)
{
    static char buf[20];
    char lli_c;         /* set zero lli to blank */
    char snr_c;         /* set zero snr to blank */

    if (!isfinite(val)) {
        /* bad value, return 16 blanks */
        return "                ";
    }
    switch (lli) {
    case 0:
    default:
        lli_c = ' ';
        break;
    case 1:
        lli_c = '1';
        break;
    case 2:
        lli_c = '2';
        break;
    case 3:
        lli_c = '3';
        break;
    }
    if ((1 > snr) || (9 < snr)) {
        snr_c = ' ';
    } else {
        snr_c = 48 + snr;
    }
    (void)snprintf(buf, sizeof(buf), "%14.3f%c%1c", val, lli_c, snr_c);
    return buf;
}

/* print_raw()
 * print one epoch of observations into "tmp_file"
 */
static void print_raw(struct gps_data_t *gpsdata)
{
    struct tm *tmp_now;
    int nsat = 0;
    int i;

    if ((last_mtime.tv_sec + (time_t)sample_interval) >
        gpsdata->raw.mtime.tv_sec) {
        /* not time yet */
        return;
    }
    /* opus insists (time % interval) = 0 */
    if (0 != (gpsdata->raw.mtime.tv_sec % sample_interval)) {
        return;
    }

    /* go through list twice, first just to get a count */
    for (i = 0; i < MAXCHANNELS; i++) {
        if (0 == gpsdata->raw.meas[i].svid) {
            continue;
        }
        if (4 == gpsdata->raw.meas[i].gnssid) {
            /* skip IMES */
            continue;
        }
        if (6 < gpsdata->raw.meas[i].gnssid) {
            /* invalid gnssid */
            continue;
        }
        nsat++;
    }
    if (0 >= nsat) {
        /* nothing to do */
        return;
    }

    /* save time of last measurement, GPS time, not UTC */
    last_mtime = gpsdata->raw.mtime;     /* structure copy */
    if (0 == first_mtime.tv_sec) {
        /* save time of first measurement */
        first_mtime = last_mtime;     /* structure copy */
    }

    tmp_now = gmtime(&(last_mtime.tv_sec));
    (void)fprintf(tmp_file,"> %4d %02d %02d %02d %02d %02d.%07ld  0%3d\n",
         tmp_now->tm_year + 1900,
         tmp_now->tm_mon + 1,
         tmp_now->tm_mday,
         tmp_now->tm_hour,
         tmp_now->tm_min,
         tmp_now->tm_sec,
         (long)(last_mtime.tv_nsec / 100), nsat);

    /* RINEX 3 wants records in each epoch sorted by gnssid.
     * To look nice: sort by gnssid and svid */
    qsort(gpsdata->raw.meas, MAXCHANNELS, sizeof(gpsdata->raw.meas[0]),
          compare_meas);
    for (i = 0; i < MAXCHANNELS; i++) {
        char gnssid;
        char svid;
        unsigned char snr;

        if (0 == gpsdata->raw.meas[i].svid) {
            continue;
        }

        svid = gpsdata->raw.meas[i].svid;
        gnssid = gnssid2rinex(gpsdata->raw.meas[i].gnssid);

        switch (gpsdata->raw.meas[i].gnssid) {
        case 0:
            /* GPS */
            break;
        case 1:
            /* SBAS, per section 8.4 of RINEX 3.03 spec */
            break;
        case 2:
            /* GALILEO */
            break;
        case 3:
            /* Beidou */
            break;
        case 4:
            /* IMES */
            /* FALLTHROUGH */
        default:
            /* huh? */
            continue;
        case 5:
            /* QZSS */
            break;
        case 6:
            /* GLONASS */
            break;
        }

        /* map snr to RINEX snr flag [1-9] */
        if ( 12 > gpsdata->raw.meas[i].snr) {
            snr = 1;
        } else if ( 18 >= gpsdata->raw.meas[i].snr) {
            snr = 2;
        } else if ( 23 >= gpsdata->raw.meas[i].snr) {
            snr = 3;
        } else if ( 29 >= gpsdata->raw.meas[i].snr) {
            snr = 4;
        } else if ( 35 >= gpsdata->raw.meas[i].snr) {
            snr = 5;
        } else if ( 41 >= gpsdata->raw.meas[i].snr) {
            snr = 6;
        } else if ( 47 >= gpsdata->raw.meas[i].snr) {
            snr = 7;
        } else if ( 53 >= gpsdata->raw.meas[i].snr) {
            snr = 8;
        } else {
            /* snr >= 54 */
            snr = 9;
        }
        /* check for slip */
        /* FIXME: use actual interval */
        if (gpsdata->raw.meas[i].locktime < (sample_interval * 1000)) {
            gpsdata->raw.meas[i].lli |= 2;
        }

        if (0 != isfinite(gpsdata->raw.meas[i].pseudorange)) {
            obs_cnt_inc(gpsdata->raw.meas[i].gnssid, gpsdata->raw.meas[i].svid,
                        C1C);
        }

        if (0 != isfinite(gpsdata->raw.meas[i].carrierphase)) {
            obs_cnt_inc(gpsdata->raw.meas[i].gnssid, gpsdata->raw.meas[i].svid,
                        L1C);
        }

        if (0 != isfinite(gpsdata->raw.meas[i].doppler)) {
            obs_cnt_inc(gpsdata->raw.meas[i].gnssid, gpsdata->raw.meas[i].svid,
                        D1C);
        }

        if (0 != isfinite(gpsdata->raw.meas[i].c2c)) {
            obs_cnt_inc(gpsdata->raw.meas[i].gnssid, gpsdata->raw.meas[i].svid,
                        C2C);
        }

        if (0 != isfinite(gpsdata->raw.meas[i].l2c)) {
            obs_cnt_inc(gpsdata->raw.meas[i].gnssid, gpsdata->raw.meas[i].svid,
                        L2C);
        }

        /* line no longer must be 80 chars in RINEX 3 */
        (void)fprintf(tmp_file,"%c%02d", gnssid, svid);
        (void)fputs(fmt_obs(gpsdata->raw.meas[i].pseudorange, 0, snr),
                    tmp_file);
        (void)fputs(fmt_obs(gpsdata->raw.meas[i].carrierphase,
                            gpsdata->raw.meas[i].lli, 0), tmp_file);
        (void)fputs(fmt_obs(gpsdata->raw.meas[i].doppler, 0, 0), tmp_file);
        (void)fputs(fmt_obs(gpsdata->raw.meas[i].c2c, 0, 0), tmp_file);
        (void)fputs(fmt_obs(gpsdata->raw.meas[i].l2c, 0, 0), tmp_file);
        (void)fputs("\n", tmp_file);

        nsat--;
    }
    sample_count--;
    if (0 != nsat) {
        (void)fprintf(stderr,"ERROR: satellite count mismatch %d\n", nsat);
        exit(1);
    }
}

/* quit_handler()
 * quit nicely on ^C.  That is: print the header and observation records
 * gathered so far.  Then exit.
 */
static void quit_handler(int signum)
{
    /* don't clutter the logs on Ctrl-C */
    if (signum != SIGINT)
        syslog(LOG_INFO, "exiting, signal %d received", signum);
    print_rinex_footer();
    (void)gps_close(&gpsdata);
    exit(EXIT_SUCCESS);
}

/* conditionally_log_fix()
 * take the new gpsdata and decide what to do with it.
 */
static void conditionally_log_fix(struct gps_data_t *gpsdata)
{
    if ( 4 < debug) {
        /* The (long long unsigned) is for 32/64-bit compatibility */
        (void)fprintf(tmp_file,"mode %d set %llx\n", gpsdata->fix.mode,
                      (long long unsigned)gpsdata->set);
    }

    /* mostly we don't care if 2D or 3D fix, let the post processor
     * decide */

    if ((MODE_2D < gpsdata->fix.mode) &&
        (LATLON_SET & gpsdata->set)) {
        /* got a good 3D fix */
        if (isfinite(gpsdata->fix.ecef.x) &&
            isfinite(gpsdata->fix.ecef.y) &&
            isfinite(gpsdata->fix.ecef.z)) {
            /* save ecef for "APPROX POS" */
            ecefx = gpsdata->fix.ecef.x;
            ecefy = gpsdata->fix.ecef.y;
            ecefz = gpsdata->fix.ecef.z;
        }
        if ( 3 < debug) {
            (void)fprintf(stderr,"got ECEF\n");
        }
    }

    if (RAW_SET & gpsdata->set) {
        if ( 3 < debug) {
            (void)fprintf(stderr,"got RAW\n");
        }
        print_raw(gpsdata);
    }
    return;
}

/* usage()
 * print usages, and exit
 */
static void usage(void)
{
    (void)fprintf(stderr,
          "Usage: %s [OPTIONS] [server[:port:[device]]]\n"
          "     [-D debuglevel]   Set debug level, default 0\n"
          "     [-f filename]     out to filename\n"
          "                       gpsrinexYYYYDDDDHHMM.obs\n"
          "     [-h]              print this usage and exit\n"
          "     [-i interval]     time between samples\n"
          "     [-n count]        number samples to collect, default: %d\n"
          "     [-V]              print version and exit\n"
          "defaults to '%s -n %d -i %d localhost:2947'\n",
          progname, sample_count, progname, sample_count, sample_interval);
    exit(EXIT_FAILURE);
}

/*
 *
 * Main
 *
 */
int main(int argc, char **argv)
{
    char tmstr[40];            /* time: YYYYDDDMMHH */
    struct tm *report_time;
    int ch;
    unsigned int flags = WATCH_ENABLE;
    char   *fname = NULL;
    int timeout = 10;

    progname = argv[0];

    log_file = stdout;
    while ((ch = getopt(argc, argv, "D:f:hi:n:V")) != -1) {
        switch (ch) {
        case 'D':
            debug = atoi(optarg);
            gps_enable_debug(debug, log_file);
            break;
       case 'f':       /* Output file name. */
            fname = strdup(optarg);
            break;
        case 'i':               /* set sampling interval */
            sample_interval = (time_t) atoi(optarg);
            if (sample_interval < 1)
                sample_interval = 1;
            if (sample_interval >= 3600)
                (void)fprintf(stderr,
                          "WARNING: saample interval is an hour or more!\n");
            break;
        case 'n':
            sample_count = atoi(optarg);
            break;
        case 'V':
            (void)fprintf(stderr, "%s: version %s (revision %s)\n",
                          progname, VERSION, REVISION);
            exit(EXIT_SUCCESS);
        default:
            usage();
            /* NOTREACHED */
        }
    }

    if ( 1 ) {
        source.server = (char *)"localhost";
        source.port = (char *)DEFAULT_GPSD_PORT;
        source.device = NULL;
    }

    if (optind < argc) {
        /* in this case, switch to the method "socket" always */
        gpsd_source_spec(argv[optind], &source);
    }
    if ( 2 < debug ) {
        (void)fprintf(stderr,"INFO: server: %s port: %s  device: %s\n",
                      source.server, source.port, source.device);
    }

    /* save start time of report */
    (void)clock_gettime(CLOCK_REALTIME, &start_time);
    report_time = gmtime(&(start_time.tv_sec));

    /* open the output file */
    if (NULL == fname) {
        (void)strftime(tmstr, sizeof(tmstr), "gpsrinex%Y%j%H%M%S.obs",
                       report_time);
        fname = tmstr;
    }
    log_file = fopen(fname, "w");
    if (log_file == NULL) {
        syslog(LOG_ERR, "ERROR: Failed to open %s: %s",
               fname, strerror(errno));
        exit(3);
    }

    /* clear the counts */
    memset(obs_cnt, 0, sizeof(obs_cnt));

    /* catch all interesting signals */
    (void)signal(SIGTERM, quit_handler);
    (void)signal(SIGQUIT, quit_handler);
    (void)signal(SIGINT, quit_handler);

    if (gps_open(source.server, source.port, &gpsdata) != 0) {
        (void)fprintf(stderr, "%s: no gpsd running or network error: %d, %s\n",
                      progname, errno, gps_errstr(errno));
        exit(EXIT_FAILURE);
    }

    if (source.device != NULL)
        flags |= WATCH_DEVICE;
    (void)gps_stream(&gpsdata, flags, source.device);

    tmp_file = tmpfile();
    if (NULL == tmp_file) {
        (void)fprintf(stderr, "ERROR: could not open temp file: %s\n",
                      strerror(errno));
        exit(2);
    }

    for (;;) {
        /* wait for gpsd */
        if (!gps_waiting(&gpsdata, timeout * 1000000)) {
            (void)fprintf(stderr, "gpsrinex: timeout\n");
            syslog(LOG_INFO, "timeout;");
            break;
        }
        (void)gps_read(&gpsdata, NULL, 0);
        if (ERROR_SET & gpsdata.set) {
            fprintf(stderr, "gps_read() error '%s'\n", gpsdata.error);
            exit(6);
        }
        conditionally_log_fix(&gpsdata);
        if ( 0 >= sample_count) {
            /* done */
            break;
        }
    }

    print_rinex_footer();

    exit(EXIT_SUCCESS);
}
