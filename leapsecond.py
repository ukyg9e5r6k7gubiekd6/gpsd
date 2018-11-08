#!/usr/bin/env python
"""

Usage: leapsecond.py [-v] { [-h] | [-f filename] | [-g filename]
                     | [-H filename] | [-I isodate] | [-O unixdate]
                     | [-i rfcdate] | [-o unixdate] | [-n MMMYYYY] }

Options:

  -I take a date in ISO8601 format and convert to Unix-UTC time

  -O take a date in Unix-UTC time and convert to ISO8601.

  -i take a date in RFC822 format and convert to Unix-UTC time

  -o take a date in Unix-UTC time and convert to RFC822.

  -f fetch leap-second offset data and save to local cache file

  -H make leapsecond include

  -h print this help

  -v be verbose

  -g generate a plot of leap-second dates over time. The command you
     probably want is something like (depending on if your gnuplot install
     does or does not support X11.

     leapsecond.py -g leapseconds.cache | gnuplot --persist
     leapsecond.py -g leapseconds.cache | gnuplot -e 'set terminal svg' - \\
         | display

  -n compute Unix gmt time for an IERS leap-second event given as a
     three-letter English Gregorian month abbreviation followed by a
     4-digit year.

Public urls and local cache file used:

http://hpiers.obspm.fr/iers/bul/bulc/bulletinc.dat
http://hpiers.obspm.fr/iers/bul/bulc/UTC-TAI.history
ftp://maia.usno.navy.mil/ser7/tai-utc.dat
leapseconds.cache

This file is Copyright (c) 2013 by the GPSD project
SPDX-License-Identifier: BSD-2-clause

"""
# This code runs compatibly under Python 2 and 3.x for x >= 2.
# Preserve this property!
from __future__ import absolute_import, print_function, division

import calendar
import math
import os
import random
import re
import signal
import sys
import time

try:
    import urllib.request as urlrequest  # Python 3
except ImportError:
    import urllib as urlrequest          # Python 2

# Set a socket timeout for slow servers
import socket
socket.setdefaulttimeout(30)
del socket

# *** Duplicate some code from gps.misc to avoid a dependency ***

# Determine a single class for testing "stringness"
try:
    STR_CLASS = basestring  # Base class for 'str' and 'unicode' in Python 2
except NameError:
    STR_CLASS = str         # In Python 3, 'str' is the base class

# Polymorphic str/bytes handling

BINARY_ENCODING = 'latin-1'

if bytes is str:  # In Python 2 these functions can be null transformations

    polystr = str

else:  # Otherwise we do something real

    def polystr(o):
        "Convert bytes or str to str with proper encoding."
        if isinstance(o, str):
            return o
        if isinstance(o, bytes):
            return str(o, encoding=BINARY_ENCODING)
        raise ValueError


def isotime(s):
    """Convert timestamps in ISO8601 format to and from Unix time including
    optional fractional seconds.
    """

    if isinstance(s, int):
        return time.strftime("%Y-%m-%dT%H:%M:%S", time.gmtime(s))
    elif isinstance(s, float):
        date = int(s)
        msec = s - date
        date = time.strftime("%Y-%m-%dT%H:%M:%S", time.gmtime(s))
        return date + "." + repr(msec)[3:]
    elif isinstance(s, STR_CLASS):
        if s[-1] == "Z":
            s = s[:-1]
        if "." in s:
            (date, msec) = s.split(".")
        else:
            date = s
            msec = "0"
        # Note: no leap-second correction!
        return calendar.timegm(time.strptime(date, "%Y-%m-%dT%H:%M:%S")) \
            + float("0." + msec)
    else:
        raise TypeError

# *** End of duplicated code ***


verbose = 0

__locations = [
    (
        # U.S. Navy's offset-history file
        "ftp://maia.usno.navy.mil/ser7/tai-utc.dat",
        r" TAI-UTC= +([0-9-]+)[^\n]*\n$",
        1,
        19,  # Magic TAI-GPS offset -> (leapseconds 1980)
        "ftp://maia.usno.navy.mil/ser7/tai-utc.dat",
    ),
    (
        # International Earth Rotation Service Bulletin C
        "http://hpiers.obspm.fr/iers/bul/bulc/bulletinc.dat",
        r" UTC-TAI = ([0-9-]+)",
        -1,
        19,  # Magic TAI-GPS offset -> (leapseconds 1980)
        "http://hpiers.obspm.fr/iers/bul/bulc/UTC-TAI.history",
    ),
]

GPS_EPOCH = 315964800               # 6 Jan 1980 00:00:00
SECS_PER_WEEK = 60 * 60 * 24 * 7    # Seconds per GPS week
ROLLOVER = 1024                     # 10-bit week rollover


def gps_week(t):
    return (t - GPS_EPOCH) // SECS_PER_WEEK % ROLLOVER


def gps_rollovers(t):
    return (t - GPS_EPOCH) // SECS_PER_WEEK // ROLLOVER


def retrieve():
    "Retrieve current leap-second from Web sources."
    random.shuffle(__locations)  # To spread the load
    for (url, regexp, sign, offset, _) in __locations:
        try:
            if os.path.exists(url):
                ifp = open(url)
            else:
                ifp = urlrequest.urlopen(url)
            txt = polystr(ifp.read())
            ifp.close()
            if verbose:
                sys.stderr.write("%s\n" % txt)
            m = re.search(regexp, txt)
            if m:
                return int(m.group(1)) * sign - offset
        except IOError:
            if verbose:
                sys.stderr.write("IOError: %s\n" % url)
    return None


def last_insertion_time():
    "Give last potential insertion time for a leap second."
    # We need the Unix times for midnights Jan 1 and Jul 1 this year.
    when = time.gmtime()
    (tm_year, tm_mon, tm_mday, tm_hour, tm_min,
     tm_sec, tm_wday, tm_yday, tm_isdst) = when

    tm_mday = 1
    tm_hour = tm_min = tm_sec = 0
    tm_mon = 1
    jan_t = (tm_year, tm_mon, tm_mday, tm_hour, tm_min,
             tm_sec, tm_wday, tm_yday, tm_isdst)
    jan = int(calendar.timegm(jan_t))
    tm_mon = 7
    jul_t = (tm_year, tm_mon, tm_mday, tm_hour, tm_min,
             tm_sec, tm_wday, tm_yday, tm_isdst)
    jul = int(calendar.timegm(jul_t))
    # We have the UTC times of the potential insertion points this year.
    now = time.time()
    if now > jul:
        return jul

    return jan


def save_leapseconds(outfile):
    """Fetch the leap-second history data and make a leap-second list since
    Unix epoch GMT (1970-01-01T00:00:00).
    """

    random.shuffle(__locations)  # To spread the load
    for (_, _, _, _, url) in __locations:
        skip = True
        try:
            fetchobj = urlrequest.urlopen(url)
        except IOError:
            sys.stderr.write("Fetch from %s failed.\n" % url)
            continue
        # This code assumes that after 1980, leap-second increments are
        # always integrally one second and every increment is listed here
        fp = open(outfile, "w")
        for line in fetchobj:
            line = polystr(line)
            if verbose:
                sys.stderr.write("%s\n" % line[:-1])
            if line.startswith(" 1980"):
                skip = False
            if skip:
                continue
            fields = line.strip().split()
            if len(fields) < 2:
                continue
            md = leapbound(fields[0], fields[1])
            if verbose:
                sys.stderr.write("# %s\n" % md)
            fp.write(repr(iso_to_unix(md)) + "\t# " + str(md) + "\n")
        fp.close()
        return
    sys.stderr.write("%s not updated.\n" % outfile)


def fetch_leapsecs(filename):
    "Get a list of leap seconds from the local cache of the USNO history"
    leapsecs = []
    for line in open(str(filename)):
        leapsecs.append(float(line.strip().split()[0]))
    return leapsecs


def make_leapsecond_include(infile):
    """Get the current leap second count and century from the local cache
    usable as C preprocessor #define
    """

    # Underscore prefixes avoids warning W0612 from pylint,
    # which doesn't count substitution through locals() as use.
    leapjumps = fetch_leapsecs(infile)
    now = int(time.time())
    _century = time.strftime("%Y", time.gmtime(now))[:2] + "00"
    _week = gps_week(now)
    _rollovers = gps_rollovers(now)
    _isodate = isotime(now - now % SECS_PER_WEEK)
    _leapsecs = 0
    for leapjump in leapjumps:
        if leapjump < time.time():
            _leapsecs += 1
    return """\
/*
 * Constants used for GPS time detection and rollover correction.
 *
 * Correct for week beginning %(_isodate)s
 */
#define BUILD_CENTURY\t%(_century)s
#define BUILD_WEEK\t%(_week)d                   # Assumes 10-bit week counter
#define BUILD_LEAPSECONDS\t%(_leapsecs)d
#define BUILD_ROLLOVERS\t%(_rollovers)d         # Assumes 10-bit week counter
""" % locals()


def conditional_leapsecond_fetch(outfile, timeout):
    """Conditionally fetch leapsecond data,
    w. timeout in case of evil firewalls.
    """

    if not os.path.exists(outfile):
        stale = True
    else:
        # If there can't have been a leapsecond insertion since the
        # last time the cache was updated, we don't need to refresh.
        # This test cuts way down on the frequency with which we fetch.
        stale = last_insertion_time() > os.path.getmtime(outfile)
    if not stale:
        return True

    def handler(_signum, _frame):
        raise IOError

    try:
        signal.signal(signal.SIGALRM, handler)
    except ValueError:
        # Parallel builds trigger this - signal only works in main thread
        sys.stdout.write("Signal set failed; ")
        return False
    signal.alarm(timeout)
    sys.stdout.write("Attempting leap-second fetch...")
    try:
        save_leapseconds(outfile)
        sys.stdout.write("succeeded.\n")
    except IOError:
        sys.stdout.write("failed; ")
        return False
    signal.alarm(0)
    return True


def leastsquares(tuples):
    "Generate coefficients for a least-squares fit to the specified data."
    sum_x = 0
    sum_y = 0
    sum_xx = 0
    sum_xy = 0
    for (x, y) in tuples:
        sum_x = sum_x + x
        sum_y = sum_y + y
        xx = math.pow(x, 2)
        sum_xx = sum_xx + xx
        xy = x * y
        sum_xy = sum_xy + xy
    n = len(tuples)
    c = (-sum_x * sum_xy + sum_xx * sum_y) / (n * sum_xx - sum_x * sum_x)
    b = (-sum_x * sum_y + n * sum_xy) / (n * sum_xx - sum_x * sum_x)
    # y = b * x + c
    maxerr = 0
    for (x, y) in tuples:
        err = y - (x * b + c)
        if err > maxerr:
            maxerr = err
    return (b, c, maxerr)


def iso_to_unix(tv):
    "Local Unix time to iso date."
    return calendar.timegm(time.strptime(tv, "%Y-%m-%dT%H:%M:%S"))


def unix_to_iso(tv):
    "ISO date to UTC Unix time."
    return time.strftime("%Y-%m-%dT%H:%M:%S", time.gmtime(tv))


def graph_history(filename):
    "Generate a GNUPLOT plot of the leap-second history."
    raw = fetch_leapsecs(filename)
    (b, c, e) = leastsquares(list(zip(list(range(len(raw))), raw)))
    e /= (60 * 60 * 24 * 7)
    dates = [time.strftime("%Y-%m-%d", time.localtime(t)) for t in raw]
    # Adding 190 days to scale
    enddate = time.strftime("%Y-%m-%d", time.localtime(raw[-1] + 16416000))
    fmt = ''
    fmt += '# Least-squares approximation of Unix time from leapsecond is:\n'
    fmt += 'lsq(x) = %s * x + %s\n' % (b, c)
    fmt += '# Maximum residual error is %.2f weeks\n' % e
    fmt += 'set autoscale\n'
    fmt += 'set ylabel "GPS-UTC (s)"\n'
    fmt += 'set yrange [-1:%d]\n' % (len(dates))
    fmt += 'set xlabel "Leap second date"\n'
    fmt += 'set xtics rotate by 300\n'
    fmt += 'set timefmt "%Y-%m-%d"\n'
    fmt += 'set xdata time\n'
    fmt += 'set format x "%Y-%m-%d"\n'
    fmt += 'set xrange ["%s":"%s"]\n' % ("1979-09-01", enddate)
    fmt += 'set key left top box\n'
    fmt += 'plot "-" using 3:1 title "Leap second inserted" with points ;\n'
    for (i, (r, d)) in enumerate(zip(raw, dates)):
        fmt += "%d\t%s\t%s\n" % (i, r, d)
    fmt += 'e\n'
    print(fmt)


def rfc822_to_unix(tv):
    "Local Unix time to RFC822 date."
    return calendar.timegm(time.strptime(tv, "%d %b %Y %H:%M:%S"))


def unix_to_rfc822(tv):
    "RFC822 date to gmt Unix time."
    return time.strftime("%d %b %Y %H:%M:%S", time.gmtime(tv))


def printnext(val):
    "Compute Unix time correponsing to a scheduled leap second."
    if val[:3].lower() not in ("jun", "dec"):
        sys.stderr.write("leapsecond.py: -n argument must begin with "
                         "'Jun' or 'Dec'\n")
        raise SystemExit(1)
    else:
        month = val[:3].lower()
        if len(val) != 7:
            sys.stderr.wrrite("leapsecond.py: -n argument must be of "
                              "the form {jun|dec}nnnn.\n")
            raise SystemExit(1)
        try:
            year = int(val[3:])
        except ValueError:
            sys.stderr.write("leapsecond.py: -n argument must end "
                             "with a 4-digit year.\n")
            raise SystemExit(1)
        # Date looks valid
        tv = leapbound(year, month)
        print("%d       /* %s */" % (iso_to_unix(tv), tv))


def leapbound(year, month):
    "Return a leap-second date in RFC822 form."
    # USNO lists JAN and JUL (month following the leap second).
    # IERS lists DEC. and JUN. (month preceding the leap second).
    # Note: It is also possible for leap seconds to occur in end-Mar and
    # end-Sep although none have occurred yet
    if month.upper()[:3] == "JAN":
        tv = "%s-12-31T23:59:60" % (int(year) - 1)
    elif month.upper()[:3] in ("JUN", "JUL"):
        tv = "%s-06-30T23:59:59" % year
    elif month.upper()[:3] == "DEC":
        tv = "%s-12-31T23:59:59" % year
    return tv

# Main part


def usage():
    print(__doc__)
    raise SystemExit(0)


if __name__ == '__main__':
    import getopt
    (options, arguments) = getopt.getopt(sys.argv[1:], "hvf:g:H:i:n:o:I:O:")
    for (switch, val) in options:
        if switch == '-h':    # help, get usage only
            usage()
        elif switch == '-v':    # be verbose
            verbose = 1
        elif switch == '-f':    # Fetch USNO data to cache locally
            save_leapseconds(val)
            raise SystemExit(0)
        elif switch == '-g':  # Graph the leap_second history
            graph_history(val)
            raise SystemExit(0)
        elif switch == '-H':  # make leapsecond include
            sys.stdout.write(make_leapsecond_include(val))
            raise SystemExit(0)
        elif switch == '-i':  # Compute Unix time from RFC822 date
            print(rfc822_to_unix(val))
            raise SystemExit(0)
        elif switch == '-n':  # Compute possible next leapsecond
            printnext(val)
            raise SystemExit(0)
        elif switch == '-o':  # Compute RFC822 date from Unix time
            print(unix_to_rfc822(float(val)))
            raise SystemExit(0)
        elif switch == '-I':  # Compute Unix time from ISO8601 date
            print(isotime(val))
            raise SystemExit(0)
        elif switch == '-O':  # Compute ISO8601 date from Unix time
            print(isotime(float(val)))
            raise SystemExit(0)

# End
