#!/usr/bin/env python
#
# Usage: leapsecond.py [-i rfcdate] [-o unixdate] [-n MMMYYYY]

# With no option, get the current leap-second value.  This is the
# offset between UTC and GPS time, which changes occasionally due to
# variations in the Earth's rotation.
#
# With the -i option, take a date in RFC822 format and convert to Unix
# local time
#
# With the -o option, take a date in Unix local time and convert to RFC822.
#
# With -c, generate a C initializer listing leap seconds in Unix time.
#
# With -g, generate a plot of the leap-second trend over time. The command
# you probably want is "leapsecond.py -g leapseconds.cache | gnuplot -persist".
#
# With the -n option, compute Unix local time for an IERS leap-second event
# given as a three-letter English Gregorian month abbreviation followed by
# a 4-digit year.
#
# This file is Copyright (c) 2010 by the GPSD project
# BSD terms apply: see the file COPYING in the distribution root for details.
#
import os, urllib, re, random, time, calendar, math

__locations = [
    (
    # U.S. Navy's offset-history file
    "ftp://maia.usno.navy.mil/ser7/tai-utc.dat",
    r" TAI-UTC= +([0-9-]+)[^\n]*\n$",
    1,
    19,	# Magic TAI-GPS offset
    ),
    (
    # International Earth Rotation Service Bulletin C
    "http://hpiers.obspm.fr/iers/bul/bulc/bulletinc.dat",
    r" UTC-TAI = ([0-9-]+)",
    -1,
    19,	# Magic TAI-GPS offset
    ),
]

# File containing cached offset data.
# Two fields: the offset, and the start of the current six-month span
# between times it might change, in seconds since Unix epoch GMT.
__cachepath = "/var/run/leapsecond"

def retrieve():
    "Retrieve current leap-second from Web sources."
    random.shuffle(__locations)	# To spread the load
    for (url, regexp, sign, offset) in __locations:
        try:
            ifp = urllib.urlopen(url)
            txt = ifp.read()
            ifp.close()
            m = re.search(regexp, txt)
            if m:
                return int(m.group(1)) * sign - offset
        except:
            pass
    else:
        return None

def last_insertion_time():
    "Give last potential insertion time for a leap second."
    # We need the Unix times for midnights Jan 1 and Jul 1 this year.
    when = time.gmtime()
    when.tm_mday = 1
    when.tm_hour = when.tm_min = when.tm_sec = 0
    when.tm_mon = 1; jan = int(calendar.timegm(when))
    when.tm_mon = 7; jul = int(calendar.timegm(when))
    # We have the UTC times of the potential insertion points this year.
    now = time()
    if now > jul:
        return jul
    else:
        return jan

def get():
    "Fetch GPS offset, from local cache file if possible."
    stale = False
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
            stale = True
    # We now know whether the cached data is stale
    if not stale:
        return offset
    else:
        current_offset = retrieve()
        # Try to cache this for later
        if current_offset != None:
            try:
                cfp = open(__cachepath, "w")
                cfp.write("%d %d\n" % (offset, last_insertion))
                cfp.close()
            except (IOError, OSError):
                pass
        return current_offset

def save_leapseconds(outfile):
    "Fetch the USNO leap-second history data and make a leap-second list."
    skip = True
    leapsecs = []
    # This code assumes that after 1980, leap-second increments are
    # always integrally one second and every increment is listed here
    leapsecs = []
    try:
        fp = open(outfile, "w")
        for line in urllib.urlopen("ftp://maia.usno.navy.mil/ser7/tai-utc.dat"):
            if line.startswith(" 1980"):
                skip = False
            if skip:
                continue
            fields = line.strip().split()
            md = leapbound(fields[0], fields[1])
            fp.write(repr(rfc822_to_unix(md)) + "\n")
        fp.close()
    except IOError:
        print >>sys.stderr, "Fetch from USNO failed, %s not updated." % outfile

def fetch_leapsecs(filename):
    "Get a list of leap seconds from the local cache of the USNO history"
    leapsecs = []
    for line in open(str(filename)):
        leapsecs.append(float(line.strip()))
    return leapsecs

def make_leapsecond_include(infile):
    leapsecs = fetch_leapsecs(infile)
    leapsecs.append(time.time())          # Add sentinel
    def label(i):
        if i == len(leapsecs) - 1:
            return '?'
        else:
            return str(i)
    year = time.strftime("%Y", time.localtime(time.time()))
    return ("#define CENTURY_BASE\t%s00\n" % year[:2]) + ("#define LEAPSECOND_NOW\t%d\n" % (len(leapsecs)-2))

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
    "RFC822 date to local Unix time."
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
        print "%d	/* %s */" % (rfc822_to_unix(tv), tv)

def leapbound(year, month):
    "Return a leap-second date in RFC822 form."
    # USNO lists JAN and JUL (month following the leap second).
    # IERS lists DEC and JUN (month preceding the leap second).
    if month.upper() == "JAN":
        tv = "31 Dec %s 23:59:60" % (int(year)-1)
    elif month.upper() in ("JUN", "JUL"):
        tv = "30 Jun %s 23:59:59" % year
    elif month.upper() == "DEC":
        tv = "31 Dec %s 23:59:59" % year
    return tv

if __name__ == '__main__':
    import sys, getopt
    (options, arguments) = getopt.getopt(sys.argv[1:], "f:g:h:i:n:o:")
    for (switch, val) in options:
        if (switch == '-f'):    # Fetch USNO data to cache locally
            save_leapseconds(val)
            raise SystemExit, 0
        elif (switch == '-g'):  # Graph the leap_second history
            graph_history(val)
            raise SystemExit, 0
        elif (switch == '-h'):  # make leapsecond include
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

        print "Current leap second:", retrieve()
        raise SystemExit, 0

# End
