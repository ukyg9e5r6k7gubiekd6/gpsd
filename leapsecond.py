#!/usr/bin/env python
#
# Get the current leap-second value.  This is the offset between UTC and
# GPS time, which changes occasionally due to variations in the Earth's
# rotation.
#
import urllib, re, random

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
        raise ValueError

if __name__ == '__main__':
    print "GPS offset is: %d" % retrieve()
