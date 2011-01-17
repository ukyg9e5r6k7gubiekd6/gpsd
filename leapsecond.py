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
# With -c, generate a C table that maps leap-second offset to plausible years.
# With -p, generate a Python table.
#
# With the -n option, compute Unix local time for an IERS leap-second event
# given as a three-letter English Gregorian month abbreviation followed by
# a 4-digit year.
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

def rfc822_to_unix(tv):
    "Local Unix time to RFC822 date."
    return time.mktime(time.strptime(tv, "%d %b %Y %H:%M:%S"))

def unix_to_rfc822(tv):
    "RFC822 date to local Unix time."
    return time.strftime("%d %b %Y %H:%M:%S", time.localtime(tv))

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
    next = False
    from_rfc822 = False
    to_rfc822 = False
    c_epochs = py_epochs = False
    (options, arguments) = getopt.getopt(sys.argv[1:], "ci:n:o:p")
    for (switch, val) in options:
        if (switch == '-c'):
            c_epochs = True
        elif (switch == '-i'):  # Compute Unix time from RFC822 date
            from_rfc822 = True
        elif (switch == '-n'):  # Compute possible next leapsecond
            next = True
        elif (switch == '-o'):  # Compute RFC822 date from Unix time
            to_rfc822 = True
        elif (switch == '-p'):
            py_epochs = True

    if not next and not from_rfc822 and not to_rfc822 and not c_epochs and not py_epochs:
        print "Current leap second:", retrieve()
        raise SystemExit, 0

    if from_rfc822:
        print "#define FOO	%d	/* %s */" % (rfc822_to_unix(val), val)
        raise SystemExit, 0

    if to_rfc822:
        print unix_to_rfc822(float(val))
        raise SystemExit, 0

    if c_epochs or py_epochs:
        skip = True
        leapsecs = []
        # This code assumes that after 1980, leap-second increments are
        # always integrally one second and every increment is listed here
        leapsecs = []
        for line in urllib.urlopen("ftp://maia.usno.navy.mil/ser7/tai-utc.dat"):
            if line.startswith(" 1980"):
                skip = False
            if skip:
                continue
            fields = line.strip().split()
            leapsecs.append(leapbound(fields[0], fields[1]))
        leapsecs.append(unix_to_rfc822(time.time()))
	def label(i):
	    if i == len(leapsecs) - 1:
		return '?'
	    else:
		return str(i)
        if c_epochs:
            print '''
/*****************************************************************************

This code is generated from leapsecond.py; do not hand-hack!

Excerpted from my blog entry on this code:

The root cause of the Rollover of Doom is the peculiar time
reference that GPS uses.  Times are expressed as two numbers: a count
of weeks since the start of 1980, and a count of seconds in the
week. So far so good - except that, for hysterical raisins, the week
counter is only 10 bits long.  The first week rollover was in 1999;
the second will be in 2019.

So, what happens on your GPS when you reach counter zero?  Why, the
time it reports warps back to the date of the last rollover, currently
1999.  Obviously, if you are logging or computing anything
time-dependent through a rollover and relying on GPS time, you are
screwed.

Now, we do get one additional piece of time information: the current 
leap-second offset. The object of this exercise is to figure out what 
you can do with it.

In order to allow UTC to be computed from the GPS-week/GPS-second
pair, the satellite also broadcasts a cumulative leap-second offset.
The offset was 0 when the system first went live; in January 2010
it is 15 seconds. It is updated every 6 months based on spin measurements
by the <a href="http://www.iers.org/">IERS</a>.

For purposes of this exercise, you get to assume that you have a table
of leap seconds handy, in Unix time (seconds since midnight before 1
Jan 1970, UTC corrected).  You do *not* get to assume that your table
of leap seconds is current to date, only up to when you shipped your
software.

For extra evilness, you also do not get to assume that the week rollover
period is constant. The not-yet-deployed Block III satellites will have
13-bit week rollover counters, pushing the next rollover back to 2173AD.

For extra-special evilness, there are two different ways your GPS date
could be clobbered. after a rollover.  If your receiver firmware was
designed by an idiot, all GPS week/second pairs will be translated
into an offset from the last rollover, and date reporting will go
wonky precisely on the next rollover.  If your designer is slightly
more clever, GPS dates between the last rollover and the ship date of
the receiver firmware will be mapped into offsets from the *next*
rollover, and date reporting will stay sane for an entire 19 years
from that ship date.

You are presented with a GPS time (a week-counter/seconds-in-week
pair), and a leap-second offset. You also have your (incomplete) table
of leap seconds. The GPS week counter may invalid due to the Rollover
of Doom. Specify an algorithm that detects rollover cases as often as
possible, and explain which cases you cannot detect.

Hint: This problem is a Chinese finger-trap for careful and conscientious
programmers. The better you are, the worse this problem is likely to hurt
your brain. Embrace the suck.

I continue:

*Good* programmers try to solve this problem by using the leap second to
pin down the epoch date of the current rollover period so they can correct
the week counter.  There are several reasons this cannot work.

One is that you will not *know* the cumulative leap-second offset for
times after your software ships, so there is no way to know how the
leap-second offset you are handed maps to a year in that case.  Even
if you did, somehow, havd a list of all future leap seconds, the
mapping from leap second to year has about two years of uncertainty on
average.  This means applying that mapping could lead you to guess a
year on the wrong side of the nearest rollover line about one tenth of
the time.

Here is what you can do.

If the timestamp you are handed is within the range of the first and
last entries, check the leap-second offset.  If it is correct for that
range, there has been no rollover.  If it does not match the
leap-second offset for that range, your date is from a later rollover
period than your receiver was designed to handle and has gotten
clobbered.

The odd thing about this test is the range of rollover cases it can detect.
If your table covers a range of N seconds after the last rollover, it will
detect rollover from all future weeks as long as they are within N seconds
after *their* rollover.  It will be leaast effective from a software release
immediately after a rollover and most effective from a release immediately
before one.

Much of the time, this algorithm will return "I cannot tell".  The reason this
problem is like a Chinese finger trap is because good programmers will hurt
their brains trying to come up with a solution that does not punt any cases.

*****************************************************************************/

#include "gpsd.h"

int gpsd_check_leapsecond(const int leap, const double unixtime)
/* consistency-check a GPS-reported time against a leap second */
{
    static double c_epochs[] = {\
'''
            for (i, b) in enumerate(leapsecs):
                print "        %s,    // %s -> %s" % (rfc822_to_unix(b), b, label(i))
            print '''\
    };
    #define DIM(a) (int)(sizeof(a)/sizeof(a[0]))

    if (leap < 0 || leap >= DIM(c_epochs))
        return -1;   /* cannot tell, leap second out of table bounds */
    else if (unixtime < c_epochs[0] || unixtime >= c_epochs[DIM(c_epochs)-1])
        return -1;   /* cannot tell, time not in table */
    else if (unixtime >= c_epochs[leap] && unixtime <= c_epochs[leap+1])
        return 1;    /* leap second consistent with specified year */
    else
        return 0;    /* leap second inconsistent, probable rollover error */
}
'''
	else:
	    leapsecs = [(rfc822_to_unix(l), b, label(b)) for (i, b) in enumerate(leapsecs)]
	    import pprint
	    print '''# This table is generated by leapsecond.py; do not hand-hack!

leapsecs ='''
	    pprint.pprint(leapsecs)
        raise SystemExit, 0

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
        print "#define START_SUBFRAME	%d	/* %s */" % (rfc822_to_unix(tv), tv)
