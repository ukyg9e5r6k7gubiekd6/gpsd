#!/usr/bin/env python
#
# Get the current leap-second value.  This is the offset between UTC and
# GPS time, which changes occasionally due to variations in the Earth's
# rotation.
#
# This file is Copyright (c) 2010 by the GPSD project
# BSD terms apply: see the file COPYING in the distribution root for details.
#
import os, urllib, re, random, time, calendar

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

if __name__ == '__main__':
    import sys, getopt
    next = False
    (options, arguments) = getopt.getopt(sys.argv[1:], "n:")
    for (switch, val) in options:
        if (switch == '-n'):  # Compute possible next leapsecond
            next = True

    if not next:
        print "Current leap second:", retrieve()
    elif val[:3].lower() not in ("jun", "dec"):
        print >>sys.stderr, "leapsecond.py: -n argument must begin with "\
              "'Jun' or 'Dec'"
        raise SystemExit, 1
    else:
        month = val[:3].lower()
        if len(val) != 7:
            print >>sys.stderr, "leapsecond.py: -n argument must end "\
                  "with a a 4-digit year."                
            raise SystemExit, 1
        try:
            year = int(val[3:])
        except ValueError:
            print >>sys.stderr, "leapsecond.py: -n argument must end "\
                  "with a 4-digit year."
            raise SystemExit, 1
        # Date looks valid
        if month == "jun":
            fmt = "30 Jun %d 23:59:59" % year
        else:
            fmt = "31 Dec %d 23:59:59" % year
        next = time.mktime(time.strptime(fmt , "%d %b %Y %H:%M:%S"))
        print "#define START_SUBFRAME	%d	/* %s */" % (next, fmt)
