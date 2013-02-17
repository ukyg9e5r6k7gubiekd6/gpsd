#!/usr/bin/env python
"""

Usage: leapsecond.py [-v] { [-h] | [-f filename] | [-g filename] | [-H filename]
    | [-I isodate] | [-O unixdate] | [-i rfcdate] | [-o unixdate] | [-n MMMYYYY] }

With no option, get the current leap-second value.  This is the offset between
UTC and GPS time, which changes occasionally due to variations in the Earth's
rotation.

Options:

  -I take a date in ISO8601 format and convert to Unix gmt time

  -O take a date in Unix gmt time and convert to ISO8601.

  -i take a date in RFC822 format and convert to Unix gmt time

  -o take a date in Unix gmt time and convert to RFC822.

  -c generate a C initializer listing leap seconds in Unix time.

  -f fetch USNO data and save to local cache file

  -H make leapsecond include

  -h print this help

  -v be verbose

  -g generate a plot of the leap-second trend over time. The command you
     probably want is "leapsecond.py -g leapseconds.cache | gnuplot -persist".

  -n compute Unix gmt time for an IERS leap-second event given as a three-letter
     English Gregorian month abbreviation followed by a 4-digit year.

Public urls and local cache file used:

http://hpiers.obspm.fr/iers/bul/bulc/bulletinc.dat
ftp://maia.usno.navy.mil/ser7/tai-utc.dat
file:///var/run/leapsecond
leapseconds.cache

This file is Copyright (c) 2013 by the GPSD project
BSD terms apply: see the file COPYING in the distribution root for details.

"""

import os, urllib, re, random, time, calendar, math, sys

verbose = 0

__locations = [
    (
    # U.S. Navy's offset-history file
    "ftp://maia.usno.navy.mil/ser7/tai-utc.dat",
    r" TAI-UTC= +([0-9-]+)[^\n]*\n$",
    1,
    19, # Magic TAI-GPS offset -> (leapseconds 1980)
    ),
    (
    # International Earth Rotation Service Bulletin C
    "http://hpiers.obspm.fr/iers/bul/bulc/bulletinc.dat",
    r" UTC-TAI = ([0-9-]+)",
    -1,
    19, # Magic TAI-GPS offset -> (leapseconds 1980)
    ),
]

# File containing cached offset data.
# Two fields: the offset, and the start of the current six-month span
# between times it might change, in seconds since Unix epoch GMT (1970-01-01T00:00:00).
__cachepath = "/var/run/leapsecond"

def isotime(s):
    "Convert timestamps in ISO8661 format to and from Unix time including optional fractional seconds."
    if type(s) == type(1):
        return time.strftime("%Y-%m-%dT%H:%M:%S", time.gmtime(s))
    elif type(s) == type(1.0):
        date = int(s)
        msec = s - date
        date = time.strftime("%Y-%m-%dT%H:%M:%S", time.gmtime(s))
        return date + "." + repr(msec)[3:]
    elif type(s) == type("") or type(s) == type(u""):
        if s[-1] == "Z":
            s = s[:-1]
        if "." in s:
            (date, msec) = s.split(".")
        else:
            date = s
            msec = "0"
        # Note: no leap-second correction!
        return calendar.timegm(time.strptime(date, "%Y-%m-%dT%H:%M:%S")) + float("0." + msec)
    else:
        raise TypeError

def retrieve():
    "Retrieve current leap-second from Web sources."
    global verbose
    random.shuffle(__locations) # To spread the load
    for (url, regexp, sign, offset) in __locations:
        try:
            if os.path.exists(url):
                ifp = open(url)
            else:
                ifp = urllib.urlopen(url)
            txt = ifp.read()
            ifp.close()
            if verbose:
                print >>sys.stderr, "%s" % txt
            m = re.search(regexp, txt)
            if m:
                return int(m.group(1)) * sign - offset
        except IOError:
            if verbose:
                print >>sys.stderr, "IOError: %s" % url
            pass
    else:
        return None

def last_insertion_time():
    "Give last potential insertion time for a leap second."
    # We need the Unix times for midnights Jan 1 and Jul 1 this year.
    when = time.gmtime()
    (tm_year, tm_mon, tm_mday, tm_hour, tm_min,
     tm_sec, tm_wday, tm_yday, tm_isdst) = when

    tm_mday = 1
    tm_hour = tm_min = tm_sec = 0
    tm_mon = 1;
    jan = (tm_year, tm_mon, tm_mday, tm_hour, tm_min,
           tm_sec, tm_wday, tm_yday, tm_isdst)
    jan = int(calendar.timegm(jan))
    tm_mon = 7;
    jul = (tm_year, tm_mon, tm_mday, tm_hour, tm_min,
           tm_sec, tm_wday, tm_yday, tm_isdst)
    jul = int(calendar.timegm(jul))
    # We have the UTC times of the potential insertion points this year.
    now = time.time()
    if now > jul:
        return jul
    else:
        return jan

def get():
    "Fetch GPS offset, from local cache file if possible."
    global verbose
    stale = False
    offset = None
    valid_from = None
    last_insertion = last_insertion_time()
    if not os.path.exists(__cachepath):
        stale = True
    else:
        try:
            cfp = open(__cachepath)
            txt = cfp.read()
            cfp.close()
            (offset, valid_from) = map(int, txt.split())
            if valid_from < last_insertion:
                stale = True
        except (IOError, OSError, ValueError):
            if verbose:
                print >>sys.stderr, "can't read %s" % __cachepath
            stale = True
    # We now know whether the cached data is stale
    if not stale:
        return (offset, valid_from)
    else:
        current_offset = retrieve()
        # Try to cache this for later
        if current_offset != None:
            try:
                cfp = open(__cachepath, "w")
                cfp.write("%d %d\n" % (current_offset, last_insertion))
                cfp.close()
            except (IOError, OSError):
                if verbose:
                    print >>sys.stderr, "can't write %s" % __cachepath
                pass
        return (current_offset, valid_from)

def save_leapseconds(outfile):
    "Fetch the USNO leap-second history data and make a leap-second list since Unix epoch GMT (1970-01-01T00:00:00)."
    global verbose
    skip = True
    try:
        fetchobj = urllib.urlopen("ftp://maia.usno.navy.mil/ser7/tai-utc.dat")
        # This code assumes that after 1980, leap-second increments are
        # always integrally one second and every increment is listed here
        fp = open(outfile, "w")
        for line in fetchobj:
            if verbose:
                print >>sys.stderr, "%s" % line[:-1]
            if line.startswith(" 1980"):
                skip = False
            if skip:
                continue
            fields = line.strip().split()
            md = leapbound(fields[0], fields[1])
            if verbose:
                print >>sys.stderr, "# %s" % md
            fp.write(repr(iso_to_unix(md)) + "\t# (" + repr(md)  + ")\n")
        fp.close()
    except IOError:
        print >>sys.stderr, "Fetch from USNO failed, %s not updated." % outfile

def fetch_leapsecs(filename):
    "Get a list of leap seconds from the local cache of the USNO history"
    leapsecs = []
    for line in open(str(filename)):
        leapsecs.append(float(line.strip().split()[0]))
    return leapsecs

def make_leapsecond_include(infile):
    "Get the current leap second count and century from the local cache usable as c preprocessor #define"
    leapjumps = fetch_leapsecs(infile)
    year = time.strftime("%Y", time.gmtime(time.time()))
    leapsecs = 0
    for leapjump in leapjumps:
        if leapjump < time.time():
            leapsecs += 1
    return ("#define CENTURY_BASE\t%s00\n" % year[:2]) + ("#define LEAPSECOND_NOW\t%d\n" % (leapsecs-1))

def leastsquares(tuples):
    "Generate coefficients for a least-squares fit to the specified data."
    sum_x=0
    sum_y=0
    sum_xx=0
    sum_xy=0
    for (x, y) in tuples:
        sum_x = sum_x+x
        sum_y = sum_y+y
        xx = math.pow(x,2)
        sum_xx = sum_xx+xx
        xy = x*y
        sum_xy = sum_xy+xy
    n = len(tuples)
    c = (-sum_x*sum_xy+sum_xx*sum_y)/(n*sum_xx-sum_x*sum_x)
    b = (-sum_x*sum_y+n*sum_xy)/(n*sum_xx-sum_x*sum_x)
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
    "iso date to gmt Unix time."
    return time.strftime("%Y-%m-%dT%H:%M:%S", time.gmtime(tv))

def graph_history(filename):
    "Generate a GNUPLOT plot of the leap-second history."
    raw = fetch_leapsecs(filename)
    (b, c, e) = leastsquares(zip(range(len(raw)), raw))
    e /= (60 * 60 * 24 * 7)
    dates = map(lambda t: time.strftime("%Y-%m-%d",time.localtime(t)), raw)
    fmt = ''
    fmt += '# Least-squares approximation of Unix time from leapsecond is:\n'
    fmt += 'lsq(x) = %s * x + %s\n' % (b, c)
    fmt += '# Maximum residual error is %.2f weeks\n' % e
    fmt += 'set autoscale\n'
    fmt += 'set xlabel "Leap second offset"\n'
    fmt += 'set xrange [0:%d]\n' % (len(dates)-1)
    fmt += 'set ylabel "Leap second date"\n'
    fmt += 'set timefmt "%Y-%m-%d"\n'
    fmt += 'set ydata time\n'
    fmt += 'set format y "%Y-%m-%d"\n'
    fmt += 'set yrange ["%s":"%s"]\n' % (dates[0], dates[-1])
    fmt += 'set key left top box\n'
    fmt += 'set data style linespoints\n'
    fmt += 'plot "-" using 1:3 title "Leap-second trend";\n'
    for (i, (r, d)) in enumerate(zip(raw, dates)):
        fmt += "%d\t%s\t%s\n" % (i, r, d)
    fmt += 'e\n'
    print fmt

def rfc822_to_unix(tv):
    "Local Unix time to RFC822 date."
    return calendar.timegm(time.strptime(tv, "%d %b %Y %H:%M:%S"))

def unix_to_rfc822(tv):
    "RFC822 date to gmt Unix time."
    return time.strftime("%d %b %Y %H:%M:%S", time.gmtime(tv))

def printnext(val):
    "Compute Unix time correponsing to a scheduled leap second."
    if val[:3].lower() not in ("jun", "dec"):
        print >>sys.stderr, "leapsecond.py: -n argument must begin with "\
              "'Jun' or 'Dec'"
        raise SystemExit, 1
    else:
        month = val[:3].lower()
        if len(val) != 7:
            print >>sys.stderr, "leapsecond.py: -n argument must be of "\
                  "the form {jun|dec}nnnn."
            raise SystemExit, 1
        try:
            year = int(val[3:])
        except ValueError:
            print >>sys.stderr, "leapsecond.py: -n argument must end "\
                  "with a 4-digit year."
            raise SystemExit, 1
        # Date looks valid
        tv = leapbound(year, month)
        print "%d       /* %s */" % (iso_to_unix(tv), tv)

def leapbound(year, month):
    "Return a leap-second date in RFC822 form."
    # USNO lists JAN and JUL (month following the leap second).
    # IERS lists DEC and JUN (month preceding the leap second).
    if month.upper() == "JAN":
        tv = "%s-12-31T23:59:60" % (int(year)-1)
    elif month.upper() in ("JUN", "JUL"):
        tv = "%s-06-30T23:59:59" % year
    elif month.upper() == "DEC":
        tv = "%s-12-31T23:59:59" % year
    return tv

"""
Main part
"""
def usage():
    print __doc__
    raise SystemExit, 0

if __name__ == '__main__':
    import getopt
    (options, arguments) = getopt.getopt(sys.argv[1:], "hvf:g:H:i:n:o:I:O:")
    for (switch, val) in options:
        if (switch == '-h'):    # help, get usage only
            usage()
        if (switch == '-v'):    # be verbose
            verbose=1
        if (switch == '-f'):    # Fetch USNO data to cache locally
            save_leapseconds(val)
            raise SystemExit, 0
        elif (switch == '-g'):  # Graph the leap_second history
            graph_history(val)
            raise SystemExit, 0
        elif (switch == '-H'):  # make leapsecond include
            sys.stdout.write(make_leapsecond_include(val))
            raise SystemExit, 0
        elif (switch == '-i'):  # Compute Unix time from RFC822 date
            print rfc822_to_unix(val)
            raise SystemExit, 0
        elif (switch == '-n'):  # Compute possible next leapsecond
            printnext(val)
            raise SystemExit, 0
        elif (switch == '-o'):  # Compute RFC822 date from Unix time
            print unix_to_rfc822(float(val))
            raise SystemExit, 0
        elif (switch == '-I'):  # Compute Unix time from ISO8601 date
            print isotime(val)
            raise SystemExit, 0
        elif (switch == '-O'):  # Compute ISO8601 date from Unix time
            print isotime(float(val))
            raise SystemExit, 0

    (offset, valid_from) = get()
    print "Current leap second: %d, valid from %s" % (offset, unix_to_iso(valid_from))

# End
