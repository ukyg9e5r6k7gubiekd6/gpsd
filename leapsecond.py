#!/usr/bin/env python
#
# Get the current leap-second value.  This is the offset between GMT and
# GPS time, which changes occasionally due to variations in the Earth's
# rotation.
#
import urllib, re

__locations = (
    (
    "http://hpiers.obspm.fr/iers/bul/bulc/bulletinc.dat",
    r" UTC-TAI = ([0-9-]+)",
    ),
)

def get():
    "Retrieve current leap-second from web sources."
    for (url, regexp) in __locations:
        ifp = urllib.urlopen(url)
        txt = ifp.read()
        ifp.close()
        m = re.search(regexp, txt)
        return int(m.group(1))
    
if __name__ == '__main__':
    print "GPS offset is: %d\n" % get()
