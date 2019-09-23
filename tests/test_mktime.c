/*
 * tests for mktime(), mkgmtime(), timespec_to_iso8601() and
 * iso8601_to_timespec().
 * mktime() is a libc function, why test it?
 *
 * This file is Copyright (c) 2010-2019 by the GPSD project
 * SPDX-License-Identifier: BSD-2-clause
 */
#include <limits.h>
#include <math.h>       /* for fabs() */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../gps.h"
#include "../compiler.h"
#include "../timespec.h"

static struct
{
    struct tm t;
    time_t result;
} tests[] = {
    /* *INDENT-OFF* */
    /* sec, min,  h, md, mon, year, wd,  yd, isdst, gmtoff, zone,
     * time_t what */

    /* lower limit */
    {{   0,   0,  0,  1,   0,   70,  0,   0,     0,      0,    0}, 0},
    /* upper limit */
    {{   7,  14,  3, 19,   0,  138,  0,   0,     0,      0,    0}, 0x7fffffff},
    /* leap year */
    {{   0,   0, 12,  1,   0,   99,  0,   0,     0,      0,    0}, 915192000},
    /* leap year */
    {{   0,   0, 12,  1,   1,   99,  0,   0,     0,      0,    0}, 917870400},
    /* leap year */
    {{   0,   0, 12,  1,   2,   99,  0,   0,     0,      0,    0}, 920289600},
    /* leap year */
    {{   0,   0, 12,  1,   8,   99,  0,   0,     0,      0,    0}, 936187200},
    /* leap year */
    {{   0,   0, 12,  1,   0,  100,  0,   0,     0,      0,    0}, 946728000},
    /* leap year */
    {{   0,   0, 12,  1,   1,  100,  0,   0,     0,      0,    0}, 949406400},
    /* leap year */
    {{   0,   0, 12,  1,   2,  100,  0,   0,     0,      0,    0}, 951912000},
    /* leap year */
    {{   0,   0, 12,  1,   8,  100,  0,   0,     0,      0,    0}, 967809600},
    /* leap year */
    {{   0,   0, 12,  1,   0,  101,  0,   0,     0,      0,    0}, 978350400},
    /* leap year */
    {{   0,   0, 12,  1,   1,  101,  0,   0,     0,      0,    0}, 981028800},
    /* leap year */
    {{   0,   0, 12,  1,   2,  101,  0,   0,     0,      0,    0}, 983448000},
    /* leap year */
    {{   0,   0, 12,  1,   8,  101,  0,   0,     0,      0,    0}, 999345600},
    /* leap year */
    {{   0,   0, 12,  1,   0,  102,  0,   0,     0,      0,    0}, 1009886400},
    /* leap year */
    {{   0,   0, 12,  1,   1,  102,  0,   0,     0,      0,    0}, 1012564800},
    /* leap year */
    {{   0,   0, 12,  1,   2,  102,  0,   0,     0,      0,    0}, 1014984000},
    /* leap year */
    {{   0,   0, 12,  1,   8,  102,  0,   0,     0,      0,    0}, 1030881600},
    /* leap year */
    {{   0,   0, 12,  1,   0,  103,  0,   0,     0,      0,    0}, 1041422400},
    /* leap year */
    {{   0,   0, 12,  1,   1,  103,  0,   0,     0,      0,    0}, 1044100800},
    /* leap year */
    {{   0,   0, 12,  1,   2,  103,  0,   0,     0,      0,    0}, 1046520000},
    /* leap year */
    {{   0,   0, 12,  1,   8,  103,  0,   0,     0,      0,    0}, 1062417600},
    /* leap year */
    {{   0,   0, 12,  1,   0,  104,  0,   0,     0,      0,    0}, 1072958400},
    /* leap year */
    {{   0,   0, 12,  1,   1,  104,  0,   0,     0,      0,    0}, 1075636800},
    /* leap year */
    {{   0,   0, 12,  1,   2,  104,  0,   0,     0,      0,    0}, 1078142400},
    {{   0,   0, 12,  1,   2,  104,  0,   0,     0,      0,    0}, 1078142400},
    /* leap year */
    {{   0,   0, 12,  1,   8,  104,  0,   0,     0,      0,    0}, 1094040000},
    /* leap year */
    {{   0,   0, 12,  1,   0,  108,  0,   0,     0,      0,    0}, 1199188800},
    /* leap year */
    {{   0,   0, 12,  1,   1,  108,  0,   0,     0,      0,    0}, 1201867200},
    /* leap year */
    {{   0,   0, 12,  1,   2,  108,  0,   0,     0,      0,    0}, 1204372800},
    /* leap year */
    {{   0,   0, 12,  1,   8,  108,  0,   0,     0,      0,    0}, 1220270400},
    /* year wrap */
    {{  59,  59, 23, 31,  12,  110,  0,   0,     0,      0,    0}, 1296518399},
    /* year wrap */
    {{   0,   0,  0,  1,   0,  111,  0,   0,     0,      0,    0}, 1293840000},
    /* year wrap */
    {{  59,  59, 23, 31,  12,  111,  0,   0,     0,      0,    0}, 1328054399},
    /* year wrap */
    {{   0,   0,  0,  1,   0,  112,  0,   0,     0,      0,    0}, 1325376000},
    /* year wrap */
    {{  59,  59, 23, 31,  12,  112,  0,   0,     0,      0,    0}, 1359676799},
    /* year wrap */
    {{   0,   0,  0,  1,   0,  113,  0,   0,     0,      0,    0}, 1356998400},
    /* month wrap */
    {{  59,  59, 23, 31,   0,  115,  0,   0,     0,      0,    0}, 1422748799},
    /* month wrap */
    {{   0,   0,  0,  1,   1,  115,  0,   0,     0,      0,    0}, 1422748800},
    /* month wrap */
    {{  59,  59, 23, 28,   1,  115,  0,   0,     0,      0,    0}, 1425167999},
    /* month wrap */
    {{   0,   0,  0,  1,   2,  115,  0,   0,     0,      0,    0}, 1425168000},
    /* month wrap */
    {{  59,  59, 23, 31,   2,  115,  0,   0,     0,      0,    0}, 1427846399},
    /* month wrap */
    {{   0,   0,  0,  1,   3,  115,  0,   0,     0,      0,    0}, 1427846400},
    /* month wrap */
    {{  59,  59, 23, 30,   3,  115,  0,   0,     0,      0,    0}, 1430438399},
    /* month wrap */
    {{   0,   0,  0,  1,   4,  115,  0,   0,     0,      0,    0}, 1430438400},
    /* month wrap */
    {{  59,  59, 23, 31,   4,  115,  0,   0,     0,      0,    0}, 1433116799},
    /* month wrap */
    {{   0,   0,  0,  1,   5,  115,  0,   0,     0,      0,    0}, 1433116800},
    /* month wrap */
    {{  59,  59, 23, 30,   5,  115,  0,   0,     0,      0,    0}, 1435708799},
    /* month wrap */
    {{   0,   0,  0,  1,   6,  115,  0,   0,     0,      0,    0}, 1435708800},
    /* month wrap */
    {{  59,  59, 23, 31,   6,  115,  0,   0,     0,      0,    0}, 1438387199},
    /* month wrap */
    {{   0,   0,  0,  1,   7,  115,  0,   0,     0,      0,    0}, 1438387200},
    /* month wrap */
    {{  59,  59, 23, 31,   7,  115,  0,   0,     0,      0,    0}, 1441065599},
    /* month wrap */
    {{   0,   0,  0,  1,   8,  115,  0,   0,     0,      0,    0}, 1441065600},
    /* month wrap */
    {{  59,  59, 23, 30,   8,  115,  0,   0,     0,      0,    0}, 1443657599},
    /* month wrap */
    {{   0,   0,  0,  1,   9,  115,  0,   0,     0,      0,    0}, 1443657600},
    /* month wrap */
    {{  59,  59, 23, 31,   9,  115,  0,   0,     0,      0,    0}, 1446335999},
    /* month wrap */
    {{   0,   0,  0,  1,  10,  115,  0,   0,     0,      0,    0}, 1446336000},
    /* month wrap */
    {{  59,  59, 23, 30,  10,  115,  0,   0,     0,      0,    0}, 1448927999},
    /* month wrap */
    {{   0,   0,  0,  1,  11,  115,  0,   0,     0,      0,    0}, 1448928000},
    /* month wrap */
    {{  59,  59, 23, 31,  11,  115,  0,   0,     0,      0,    0}, 1451606399},
    /* month wrap */
    {{   0,   0,  0,  1,   0,  116,  0,   0,     0,      0,    0}, 1451606400},
    /* *INDENT-ON* */
};


/* tests for timespec_to_iso8601() */
static struct
{
    timespec_t ts_time;
    char *iso8601;        /* iso8601 result */
} tests1[] = {
    /* time zero */
    {{0, 0}, "1970-01-01T00:00:00.000Z"},

    /* before/after leap second end of 2008, notice no :60! */
    {{1230767999L, 1000000L}, "2008-12-31T23:59:59.001Z"},
    {{1230767999L, 10000000L}, "2008-12-31T23:59:59.010Z"},
    {{1230767999L, 100000000L}, "2008-12-31T23:59:59.100Z"},
    {{1230768000L, 20000000L}, "2009-01-01T00:00:00.020Z"},

    /* test for rounding at %.3f */
    {{1541766896L, 999412000L}, "2018-11-09T12:34:56.999Z"},
    {{1541766896L, 999499000L}, "2018-11-09T12:34:56.999Z"},
    {{1541766896L, 999500000L}, "2018-11-09T12:34:57.000Z"},
    {{1541766896L, 999501000L}, "2018-11-09T12:34:57.000Z"},

    /* the end of time: 2038 */
    {{2147483647L, 123456000L}, "2038-01-19T03:14:07.123Z"},
    {{2147483648L, 123456000L}, "2038-01-19T03:14:08.123Z"},
};

int main(int argc UNUSED, char *argv[] UNUSED)
{
    int i;
    char tbuf[128];
    bool failed = false;
    timespec_t ts_time;
    char ts_buf[TIMESPEC_LEN];

    (void)setenv("TZ", "GMT", 1);

    /* test mktime() */
    for (i = 0; i < (int)(sizeof(tests) / sizeof(tests[0])); i++) {
        time_t ts = mktime(&tests[i].t);
        if (ts != tests[i].result) {
            failed = true;
            (void)strftime(tbuf, sizeof(tbuf), "%F %T", &tests[i].t);
            (void)printf("test_mktime: mktime() test %2d failed.\n"
                         "  Time returned from: %s should be %lu "
                         " (but was: %lu)\n",
                         i, tbuf, (unsigned long)tests[i].result,
                         (unsigned long)ts);
        }
    }

    /* test mkgmtime() */
    for (i = 0; i < (int)(sizeof(tests) / sizeof(tests[0])); i++) {
        time_t ts = mkgmtime(&tests[i].t);
        if (ts != tests[i].result) {
            failed = true;
            (void)strftime(tbuf, sizeof(tbuf), "%F %T", &tests[i].t);
            (void)printf("test_mktime: mkgmtime() test %2d failed.\n"
                         "  Time returned from: %s should be %lu "
                         " (but was: %lu)\n",
                         i, tbuf, (unsigned long)tests[i].result,
                         (unsigned long)ts);
        }
    }

    /* test timespec_to_iso8601() */
    for (i = 0; i < (int)(sizeof(tests1) / sizeof(tests1[0])); i++) {
        timespec_to_iso8601(tests1[i].ts_time, tbuf, sizeof(tbuf));
        if (0 != strcmp(tests1[i].iso8601, tbuf)) {
            if ( 4 >= sizeof(time_t)) {
                (void)printf("WARNING: time_t too small.  This gpsd binary "
                             "will fail at the 2038 roll over\n");
            } else {
                failed = true;
                (void)printf("test_mktime: timespec_to_iso8601() "
                             "test %s failed.\n"
                             "  Got %s, s/b %s\n",
                             timespec_str(&tests1[i].ts_time, ts_buf,
                                          sizeof(ts_buf)),
                             tbuf, tests1[i].iso8601);
            }
        }
    }

    /* test iso8601_to_timespec() */
    for (i = 0; i < (int)(sizeof(tests1) / sizeof(tests1[0])); i++) {
        timespec_t ts_tmp;
        ts_time = iso8601_to_timespec(tests1[i].iso8601);
        TS_SUB(&ts_tmp, &ts_time, &tests1[i].ts_time);
        if (0.001 <= fabs(TSTONS(&ts_tmp))) {
            failed = true;
            (void)printf("test_mktime: iso8601_to_timespec() test %s failed.\n"
                         "  Got %.3f, s/b %.3f\n",
                         tests1[i].iso8601, TSTONS(&ts_time),
                         TSTONS(&tests1[i].ts_time));
        }
    }

    return (int)failed;
}

/* end */

