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
 * If you have a u-blox 9 then enable GLONASS as well.
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

#include "gpsd_config.h"  /* must be before all includes */

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

/* total count of observations by u-blox gnssid [0-6]
 *  0 = GPS       RINEX G
 *  1 = SBAS      RINEX S
 *  2 = Galileo   RINEX E
 *  3 - BeiDou    RINEX C
 *  4 = IMES      not supported by RINEX
 *  5 = QZSS      RINEX J
 *  6 = GLONASS   RINEX R
 *  7 = IRNSS     RINEX I
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
 * C2L  L2C (L), Pseudo Range, BeiDou
 * D2L  L2C (L), Doppler, BeiDou
 * L2L  L2C (L), Carrier Phase, BeiDou
 *
 * L5I  L5 I Pseudo Range
 * C5I  L5 I Carrier Phase
 * D5I  L5 I Doppler
 *
 * CSRS-PPP supports:
 * GPS:      C1C  L1C  C2C  L2C  C1W  L1W  C2W  L2W
 * GLONASS : C1C  L1C  C2C  L2C  C1P  L1P  C2P  L2P
 *
 */
typedef enum {C1C = 0, D1C, L1C,
              C2C, D2C, L2C,
              C2L, D2L, L2L,
              C5I, D5I, L5I,
              C7I, D7I, L7I,
              C7Q, D7Q, L7Q, CODEMAX} obs_codes;
/* structure to hold count of observations by gnssid:svid
 * MAXCHANNEL+1 is just a WAG of max size */
#define MAXCNT (MAXCHANNELS + 1)
static struct obs_cnt_t {
        unsigned char gnssid;
        unsigned char svid;     /* svid of 0 means unused slot */
        unsigned int obs_cnts[CODEMAX+1];    /* count of obscode */
} obs_cnt[MAXCNT] = {{0}};

static FILE * tmp_file;             /* file handle for temp file */
static int sample_count = 20;       /* number of measurement sets to get */
/* seconds between measurement sets */
static unsigned int sample_interval = 30;

#define DEBUG_QUIET 0
#define DEBUG_INFO 1
#define DEBUG_PROG 2
#define DEBUG_RAW 3
static int debug = DEBUG_INFO;               /* debug level */

static struct gps_data_t gpsdata;
static FILE *log_file;

/* convert a u-blox/gpsd gnssid to the RINEX 3 constellation code
 * see [1] Section 3.5
 */
static char gnssid2rinex(int gnssid)
{
    switch (gnssid) {
    case GNSSID_GPS:      /* 0 = GPS */
        return 'G';
    case GNSSID_SBAS:     /* 1 = SBAS */
        return 'S';
    case GNSSID_GAL:      /* 2 = Galileo */
        return 'E';
    case GNSSID_BD:       /* 3 = BeiDou */
        return 'C';
    case GNSSID_IMES:     /* 4 = IMES - unsupported */
        return 'X';
    case GNSSID_QZSS:     /* 5 = QZSS */
        return 'J';
    case GNSSID_GLO:      /* 6 = GLONASS */
        return 'R';
    case GNSSID_IRNSS:    /* 7 = IRNSS */
        return 'I';
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
            /* end of list, not found, so add this gnssid:svid */
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
    int prn_count[GNSSID_CNT] = {0};   /* count of PRN per gnssid */

    if (DEBUG_PROG <= debug) {
        (void)fprintf(stderr, "doing header\n");
    }

    report_time = gmtime(&(start_time.tv_sec));
    (void)strftime(tmstr, sizeof(tmstr), "%Y%m%d %H%M%S UTC", report_time);

    (void)fprintf(log_file,
        "%9s%11s%-20s%-20s%-20s\n",
        "3.03", "", "OBSERVATION DATA", "M: Mixed", "RINEX VERSION / TYPE");
    (void)fprintf(log_file,
        "%-20s%-20s%-20s%-20s\n",
        "gpsrinex 3.19.1~dev", "", tmstr,
        "PGM / RUN BY / DATE");
    (void)fprintf(log_file, "%-60s%-20s\n",
         "Source: gpsd live data", "COMMENT");
    (void)fprintf(log_file, "%-60s%-20s\n", "XXXX", "MARKER NAME");
    (void)fprintf(log_file, "%-60s%-20s\n", "NON_PHYSICAL", "MARKER TYPE");
    (void)fprintf(log_file, "%-20s%-20s%-20s%-20s\n",
                  "Unknown", "Unknown", "", "OBSERVER / AGENCY");
    (void)fprintf(log_file, "%-20s%-20s%-20s%-20s\n",
                  "0", "UNKNOWN", "0", "REC # / TYPE / VERS");
    (void)fprintf(log_file, "%-20s%-20s%-20s%-20s\n",
                  "0", "UNKNOWN EXT     NONE", "" , "ANT # / TYPE");
    if (isfinite(ecefx) &&
	isfinite(ecefy) &&
	isfinite(ecefz)) {
	(void)fprintf(log_file, "%14.4f%14.4f%14.4f%18s%-20s\n",
	    ecefx, ecefy, ecefz, "", "APPROX POSITION XYZ");
    } else if (DEBUG_INFO <= debug) {
	(void)fprintf(stderr, "INFO: missing ECEF\n");
    }

    (void)fprintf(log_file, "%14.4f%14.4f%14.4f%18s%-20s\n",
        0.0, 0.0, 0.0, "", "ANTENNA: DELTA H/E/N");
    (void)fprintf(log_file, "%6d%6d%48s%-20s\n", 1, 1,
         "", "WAVELENGTH FACT L1/2");

    /* get PRN stats */
    qsort(obs_cnt, MAXCNT, sizeof(struct obs_cnt_t), compare_obs_cnt);
    for (i = 0; i < GNSSID_CNT; i++ ) {
        prn_count[i] = obs_cnt_prns(i);
    }
    /* CSRS-PPP needs C1C, L1C or C1C, L1C, D1C
     * CSRS-PPP refuses files with L1C first
     * convbin wants C1C, L1C, D1C
     * for some reason gfzrnx_lx wants C1C, D1C, L1C, not C1C, L1C, D1C */
    if (0 < prn_count[GNSSID_GPS]) {
        /* GPS, code G */
        (void)fprintf(log_file, "%c%5d%4s%4s%4s%4s%4s%4s%4s%4s%22s%-20s\n",
             gnssid2rinex(GNSSID_GPS), 5, "C1C", "L1C", "D1C", "C2C", "L2C",
             "D2C", "", "", "", "SYS / # / OBS TYPES");
    }
    if (0 < prn_count[GNSSID_SBAS]) {
        /* SBAS, L1 and L5 only, code S */
        (void)fprintf(log_file, "%c%5d%4s%4s%4s%4s%4s%4s%4s%4s%22s%-20s\n",
             gnssid2rinex(GNSSID_SBAS), 3, "C1C", "L1C", "D1C", "", "", "",
             "", "", "", "SYS / # / OBS TYPES");
    }
    if (0 < prn_count[GNSSID_GAL]) {
        /* Galileo, E1, E5 aand E6 only, code E  */
        (void)fprintf(log_file, "%c%5d%4s%4s%4s%4s%4s%4s%4s%4s%22s%-20s\n",
             gnssid2rinex(GNSSID_GAL), 3, "C1C", "L1C", "D1C", "C7Q",
             "L7Q", "D7Q", "", "", "", "SYS / # / OBS TYPES");
    }
    if (0 < prn_count[GNSSID_BD]) {
        /* BeiDou, BDS, code C */
        (void)fprintf(log_file, "%c%5d%4s%4s%4s%4s%4s%4s%4s%4s%22s%-20s\n",
             gnssid2rinex(GNSSID_BD), 5, "C1C", "L1C", "D1C", "C7I", "L7I",
             "D7I", "", "", "", "SYS / # / OBS TYPES");
    }
    if (0 < prn_count[GNSSID_QZSS]) {
        /* QZSS, code J */
        (void)fprintf(log_file, "%c%5d%4s%4s%4s%4s%4s%4s%4s%4s%22s%-20s\n",
             gnssid2rinex(GNSSID_QZSS), 5, "C1C", "L1C", "D1C", "C2L",
             "L2L", "D2L", "", "", "", "SYS / # / OBS TYPES");
    }
    if (0 < prn_count[GNSSID_GLO]) {
        /* GLONASS, R */
        (void)fprintf(log_file, "%c%5d%4s%4s%4s%4s%4s%4s%4s%4s%22s%-20s\n",
             gnssid2rinex(GNSSID_GLO), 5, "C1C", "L1C", "D1C", "C2C", "L2C",
             "D2C", "", "", "", "SYS / # / OBS TYPES");
    }

    (void)fprintf(log_file, "%6d%54s%-20s\n", obs_cnt_prns(255),
                  "", "# OF SATELLITES");

    /* get all the PRN / # OF OBS */
    for (i = 0; i < MAXCNT; i++) {
        cnt = 0;

        if (0 == obs_cnt[i].svid) {
            /* done */
            break;
        }
        for (j = 0; j < CODEMAX; j++) {
            cnt += obs_cnt[i].obs_cnts[j];
        }
        if (0 > cnt) {
            /* no counts for this sat */
            continue;
        }
        switch (obs_cnt[i].gnssid) {
        case GNSSID_GPS:
            /* GPS, code G */
	    (void)fprintf(log_file,"   %c%02d%6u%6u%6u%6u%6u%6u%18s%-20s\n",
			  gnssid2rinex(obs_cnt[i].gnssid), obs_cnt[i].svid,
			  obs_cnt[i].obs_cnts[C1C],
			  obs_cnt[i].obs_cnts[L1C],
			  obs_cnt[i].obs_cnts[D1C],
			  obs_cnt[i].obs_cnts[C2C],
			  obs_cnt[i].obs_cnts[L2C],
			  obs_cnt[i].obs_cnts[D2C],
			  "", "PRN / # OF OBS");
            break;
        case GNSSID_SBAS:
            /* SBAS, L1C and L5C, code S */
	    (void)fprintf(log_file,"   %c%02d%6u%6u%6u%6u%6u%6u%18s%-20s\n",
			  gnssid2rinex(obs_cnt[i].gnssid), obs_cnt[i].svid,
			  obs_cnt[i].obs_cnts[C1C],
			  obs_cnt[i].obs_cnts[L1C],
			  obs_cnt[i].obs_cnts[D1C],
			  obs_cnt[i].obs_cnts[C5I],
			  obs_cnt[i].obs_cnts[L5I],
			  obs_cnt[i].obs_cnts[D5I],
			  "", "PRN / # OF OBS");
            break;
        case GNSSID_GAL:
            /* Galileo, code E */
	    (void)fprintf(log_file,"   %c%02d%6u%6u%6u%6u%6u%6u%18s%-20s\n",
			  gnssid2rinex(obs_cnt[i].gnssid), obs_cnt[i].svid,
			  obs_cnt[i].obs_cnts[C1C],
			  obs_cnt[i].obs_cnts[L1C],
			  obs_cnt[i].obs_cnts[D1C],
			  obs_cnt[i].obs_cnts[C7Q],
			  obs_cnt[i].obs_cnts[L7Q],
			  obs_cnt[i].obs_cnts[D7Q],
			  "", "PRN / # OF OBS");
            break;
        case GNSSID_BD:
            /* BeiDou, code C */
	    (void)fprintf(log_file,"   %c%02d%6u%6u%6u%6u%6u%6u%18s%-20s\n",
			  gnssid2rinex(obs_cnt[i].gnssid), obs_cnt[i].svid,
			  obs_cnt[i].obs_cnts[C1C],
			  obs_cnt[i].obs_cnts[L1C],
			  obs_cnt[i].obs_cnts[D1C],
			  obs_cnt[i].obs_cnts[C7I],
			  obs_cnt[i].obs_cnts[L7I],
			  obs_cnt[i].obs_cnts[D7I],
			  "", "PRN / # OF OBS");
            break;
        case GNSSID_QZSS:
            /* QZSS, code J */
	    (void)fprintf(log_file,"   %c%02d%6u%6u%6u%6u%6u%6u%18s%-20s\n",
			  gnssid2rinex(obs_cnt[i].gnssid), obs_cnt[i].svid,
			  obs_cnt[i].obs_cnts[C1C],
			  obs_cnt[i].obs_cnts[L1C],
			  obs_cnt[i].obs_cnts[D1C],
			  obs_cnt[i].obs_cnts[C2L],
			  obs_cnt[i].obs_cnts[L2L],
			  obs_cnt[i].obs_cnts[D2L],
			  "", "PRN / # OF OBS");
            break;
        case GNSSID_GLO:
            /* GLONASS, code R */
	    (void)fprintf(log_file,"   %c%02d%6u%6u%6u%6u%6u%6u%18s%-20s\n",
			  gnssid2rinex(obs_cnt[i].gnssid), obs_cnt[i].svid,
			  obs_cnt[i].obs_cnts[C1C],
			  obs_cnt[i].obs_cnts[L1C],
			  obs_cnt[i].obs_cnts[D1C],
			  obs_cnt[i].obs_cnts[C2C],
			  obs_cnt[i].obs_cnts[L2C],
			  obs_cnt[i].obs_cnts[D2C],
			  "", "PRN / # OF OBS");
            break;
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

    if (0 < prn_count[GNSSID_GPS]) {
        /* GPS, code G */
        (void)fprintf(log_file, "%-60s%-20s\n",
             "G L1C", "SYS / PHASE SHIFT");
        (void)fprintf(log_file, "%-60s%-20s\n",
             "G L2C", "SYS / PHASE SHIFT");
    }
    if (0 < prn_count[GNSSID_SBAS]) {
        /* SBAS, L1 and L5 only, code S */
        (void)fprintf(log_file, "%-60s%-20s\n",
             "S L1C", "SYS / PHASE SHIFT");
        (void)fprintf(log_file, "%-60s%-20s\n",
             "E L5Q", "SYS / PHASE SHIFT");
    }
    if (0 < prn_count[GNSSID_GAL]) {
        /* GALILEO, E1, E5 and E6, code E */
        (void)fprintf(log_file, "%-60s%-20s\n",
             "E L1C", "SYS / PHASE SHIFT");
        (void)fprintf(log_file, "%-60s%-20s\n",
             "E L7Q", "SYS / PHASE SHIFT");
    }
    if (0 < prn_count[GNSSID_BD]) {
        /* BeiDou, code C */
        (void)fprintf(log_file, "%-60s%-20s\n",
             "B L1C", "SYS / PHASE SHIFT");
        (void)fprintf(log_file, "%-60s%-20s\n",
             "B L7I", "SYS / PHASE SHIFT");
    }
    if (0 < prn_count[GNSSID_QZSS]) {
        /* QZSS, code J */
        (void)fprintf(log_file, "%-60s%-20s\n",
             "J L1C", "SYS / PHASE SHIFT");
        (void)fprintf(log_file, "%-60s%-20s\n",
             "J L2L", "SYS / PHASE SHIFT");
    }
    if (0 < prn_count[GNSSID_GLO]) {
        /* GLONASS, code R */
        (void)fprintf(log_file, "%-60s%-20s\n",
             "R L1C", "SYS / PHASE SHIFT");
        (void)fprintf(log_file, "%-60s%-20s\n",
             "R L2C", "SYS / PHASE SHIFT");
    }
    (void)fprintf(log_file, "%-60s%-20s\n",
         "", "END OF HEADER");
    if (DEBUG_PROG <= debug) {
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

/* one_sig() - print one signal
 *
 * one CxC s LxC DxC
 */
static void one_sig(struct meas_t *meas)
{
    unsigned char snr;
    unsigned gnssid = meas->gnssid;
    unsigned svid = meas->svid;
    unsigned sigid = meas->sigid;
    obs_codes cxx = C1C;
    obs_codes lxx = L1C;
    obs_codes dxx = D1C;

    if (DEBUG_PROG <= debug) {
        (void)fprintf(stderr, "INFO: one_sig() %c %u:%u:%u\n",
                      gnssid2rinex(gnssid),
                      gnssid, svid, sigid);
    }

    switch (sigid) {
    default:
        (void)fprintf(stderr, "ERROR: one_sig() gnmssid %u unknown sigid %u\n",
                      gnssid, sigid);
        /* FALLTHROUGH */
    case 0:
        /* L1C */
	cxx = C1C;
	lxx = L1C;
	dxx = D1C;
        break;
    case 2:
	/* GLONASS L2 OF or BeiDou B2I D1 */
        if (GNSSID_BD == gnssid) {
            /* WAG */
	    cxx = C7I;
	    lxx = L7I;
	    dxx = D7I;
        } else {
	    cxx = C2C;
	    lxx = L2C;
	    dxx = D2C;
        }
        break;
    case 3:
	/* GPS L2 or BD B2I D2 */
	cxx = C2C;
	lxx = L2C;
	dxx = D2C;
        break;
    case 5:
	/* QZSS L2C (L) */
	cxx = C2L;
	lxx = L2L;
	dxx = D2L;
        break;
    case 6:
	/* Galileo E5 bQ */
	cxx = C7Q;
	lxx = L7Q;
	dxx = D7Q;
        break;
    }

    /* map snr to RINEX snr flag [1-9] */
    if (0 == meas->snr) {
	snr = 0;
    } else if (12 > meas->snr) {
	snr = 1;
    } else if (18 >= meas->snr) {
	snr = 2;
    } else if (23 >= meas->snr) {
	snr = 3;
    } else if (29 >= meas->snr) {
	snr = 4;
    } else if (35 >= meas->snr) {
	snr = 5;
    } else if (41 >= meas->snr) {
	snr = 6;
    } else if (47 >= meas->snr) {
	snr = 7;
    } else if (53 >= meas->snr) {
	snr = 8;
    } else {
	/* snr >= 54 */
	snr = 9;
    }

    /* check for slip */
    /* FIXME: use actual interval */
    if (meas->locktime < (sample_interval * 1000)) {
	meas->lli |= 2;
    }

    if (0 != isfinite(meas->pseudorange)) {
	obs_cnt_inc(gnssid, svid, cxx);
    }

    if (0 != isfinite(meas->carrierphase)) {
	obs_cnt_inc(gnssid, svid, lxx);
    }

    if (0 != isfinite(meas->doppler)) {
	obs_cnt_inc(gnssid, svid, dxx);
    }

    (void)fputs(fmt_obs(meas->pseudorange, 0, snr), tmp_file);
    (void)fputs(fmt_obs(meas->carrierphase, meas->lli, 0), tmp_file);
    (void)fputs(fmt_obs(meas->doppler, 0, 0), tmp_file);
}


/* print_raw()
 * print one epoch of observations into "tmp_file"
 */
static void print_raw(struct gps_data_t *gpsdata)
{
    struct tm *tmp_now;
    unsigned nrec = 0;
    unsigned nsat = 0;
    unsigned i;
    unsigned char last_gnssid = 0;
    unsigned char last_svid = 0;
    int need_nl = 0;
    int got_l1 = 0;

    if ((last_mtime.tv_sec + (time_t)sample_interval) >
        gpsdata->raw.mtime.tv_sec) {
        /* not time yet */
        return;
    }
    /* opus insists (time % interval) = 0 */
    if (0 != (gpsdata->raw.mtime.tv_sec % sample_interval)) {
        return;
    }

    /* RINEX 3 wants records in each epoch sorted by gnssid.
     * To look nice: sort by gnssid and svid
     * To work nice, sort by gnssid, svid and sigid.
     * Each sigid is one record in RAW, but all sigid is one
     * record in RINEX
     */

    /* go through list three times, first just to get a count for sort */
    for (i = 0; i < MAXCHANNELS; i++) {
        if (0 == gpsdata->raw.meas[i].svid) {
            /* bad svid, end of list */
            break;
        }
        nrec++;
    }

    if (0 >= nrec) {
        /* nothing to do */
        return;
    }
    qsort(gpsdata->raw.meas, nrec, sizeof(gpsdata->raw.meas[0]),
          compare_meas);

    /* second just to get a count, needed for epoch header */
    for (i = 0; i < nrec; i++) {
        if (0 == gpsdata->raw.meas[i].svid) {
            /* bad svid */
            continue;
        }
        if (4 == gpsdata->raw.meas[i].gnssid) {
            /* skip IMES */
            continue;
        }
        if (GNSSID_CNT <= gpsdata->raw.meas[i].gnssid) {
            /* invalid gnssid */
            continue;
        }
        /* prevent separate sigid from double counting gnssid:svid */
        if ((last_gnssid == gpsdata->raw.meas[i].gnssid) &&
            (last_svid == gpsdata->raw.meas[i].svid)) {
            /* duplicate sat */
            continue;
        }
        last_gnssid = gpsdata->raw.meas[i].gnssid;
        last_svid = gpsdata->raw.meas[i].svid;
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

    /* print epoch header line */
    tmp_now = gmtime(&(last_mtime.tv_sec));
    (void)fprintf(tmp_file,"> %4d %02d %02d %02d %02d %02d.%07ld  0%3u\n",
         tmp_now->tm_year + 1900,
         tmp_now->tm_mon + 1,
         tmp_now->tm_mday,
         tmp_now->tm_hour,
         tmp_now->tm_min,
         tmp_now->tm_sec,
         (long)(last_mtime.tv_nsec / 100), nsat);

    last_gnssid = 0;
    last_svid = 0;
    need_nl = 0;
    got_l1 = 0;

    /* Print the observations, one gnssid:svid per line.
     * The fun is merging consecutive records (new sigid) of
     * same gnssid:svid */
    for (i = 0; i < nrec; i++) {
        char rinex_gnssid;
        unsigned char gnssid;
        unsigned char svid;
        unsigned char sigid;

        gnssid = gpsdata->raw.meas[i].gnssid;
        rinex_gnssid = gnssid2rinex(gnssid);
        svid = gpsdata->raw.meas[i].svid;
        sigid = gpsdata->raw.meas[i].sigid;

	if (DEBUG_RAW <= debug) {
	    (void)fprintf(stderr,"record: %u:%u:%u %s\n",
                          gnssid, svid, sigid,
			  gpsdata->raw.meas[i].obs_code);
	}

        if (0 == gpsdata->raw.meas[i].svid) {
            /* should not happen... */
            continue;
        }

        /* line can be longer than 80 chars in RINEX 3 */
        if ((last_gnssid != gpsdata->raw.meas[i].gnssid) ||
            (last_svid != gpsdata->raw.meas[i].svid)) {

	    if (0 != need_nl) {
		(void)fputs("\n", tmp_file);
	    }
            got_l1 = 0;
	    /* new record line gnssid:svid preamble  */
	    (void)fprintf(tmp_file,"%c%02d", rinex_gnssid, svid);
        }

        last_gnssid = gpsdata->raw.meas[i].gnssid;
        last_svid = gpsdata->raw.meas[i].svid;

        /* L1x */
        switch (gpsdata->raw.meas[i].sigid) {
        case 0:
            /* L1 */
            one_sig(&gpsdata->raw.meas[i]);
            got_l1 = 1;
            break;
        case 2:
            /* GLONASS L2 OF or BD B2I D1 */
	    if (0 == got_l1) {
		/* space to start of L2 */
		(void)fprintf(tmp_file, "%48s", "");
	    }
            one_sig(&gpsdata->raw.meas[i]);
            break;
        case 3:
            /* GPS L2 or BD B2I D2 */
	    if (0 == got_l1) {
		/* space to start of L2 */
		(void)fprintf(tmp_file, "%48s", "");
	    }
            one_sig(&gpsdata->raw.meas[i]);
            break;
        case 5:
            /* QZSS L2C (L) */
	    if (0 == got_l1) {
		/* space to start of L2 */
		(void)fprintf(tmp_file, "%48s", "");
	    }
            one_sig(&gpsdata->raw.meas[i]);
            break;
        case 6:
            /* Galileo E5 bQ */
	    if (0 == got_l1) {
		/* space to start of L2 */
		(void)fprintf(tmp_file, "%48s", "");
	    }
            one_sig(&gpsdata->raw.meas[i]);
            break;
        default:
	    (void)fprintf(stderr,
                          "ERROR: print_raw() gnssid %u unknown sigid %u\n",
                          gnssid, sigid);
            break;
        }

        need_nl = 1;
    }
    if (0 != need_nl) {
        (void)fputs("\n", tmp_file);
    }
    sample_count--;
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
    if (DEBUG_PROG <= debug) {
        /* The (long long unsigned) is for 32/64-bit compatibility */
        (void)fprintf(stderr, "mode %d set %llx\n", gpsdata->fix.mode,
                      (long long unsigned)gpsdata->set);
    }

    /* mostly we don't care if 2D or 3D fix, let the post processor
     * decide */

    if (MODE_2D < gpsdata->fix.mode) {
        /* got a good 3D fix */
	if (1.0 > ecefx &&
            isfinite(gpsdata->fix.ecef.x) &&
            isfinite(gpsdata->fix.ecef.y) &&
            isfinite(gpsdata->fix.ecef.z)) {
            /* save ecef for "APPROX POS" */
            ecefx = gpsdata->fix.ecef.x;
            ecefy = gpsdata->fix.ecef.y;
            ecefz = gpsdata->fix.ecef.z;

	    if (DEBUG_PROG <= debug) {
		(void)fprintf(stderr,"got ECEF\n");
	    }
        }
    }

    if (RAW_SET & gpsdata->set) {
        if (DEBUG_RAW <= debug) {
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
          "     [-i interval]     time between samples, default: %d\n"
          "     [-n count]        number samples to collect, default: %d\n"
          "     [-V]              print version and exit\n"
          "\n"
          "defaults to '%s -n %d -i %d localhost:2947'\n",
          progname, sample_interval, sample_count, progname, sample_count,
          sample_interval);
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

    /* init source defaults */
    source.server = (char *)"localhost";
    source.port = (char *)DEFAULT_GPSD_PORT;
    source.device = NULL;

    if (optind < argc) {
        /* in this case, switch to the method "socket" always */
        gpsd_source_spec(argv[optind], &source);
    }
    if (DEBUG_INFO <= debug) {
        (void)fprintf(stderr, "INFO: server: %s port: %s  device: %s\n",
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
        if (0 >= sample_count) {
            /* done */
            break;
        }
    }

    print_rinex_footer();

    exit(EXIT_SUCCESS);
}
