#!/usr/bin/env python
#
# Test grid locator conversion.
#
# Midenhead specification at
#       http://en.wikipedia.org/wiki/Maidenhead_Locator_System
# Test conversions generated using
#       http://f6fvy.free.fr/qthLocator/

# This code runs compatibly under Python 2 and 3.x for x >= 2.
# Preserve this property!
from __future__ import absolute_import, print_function, division

import sys, gps.clienthelpers

errors = 0
for (lat, lon, maidenhead, location) in [
    (48.86471, 2.37305, "JN18eu", "Paris"),
    (41.93498, 12.43652, "JN61fw", "Rome"),
    (39.9771, -75.1685, "FM29jx", "Philadelphia"),
    (-23.4028, -50.9766, "GG46mo", "Sao Paulo"),
]:
    converted = gps.clienthelpers.maidenhead(lat, lon)
    if converted != maidenhead:
        sys.stderr.write("maidenhead test: from %s %s (%s) expected %s got %s\n" \
            % (lat, lon, location, maidenhead, converted))
        errors += 1
    else:
        print("%s OK" % location)

if errors:
    sys.exit(1)
else:
    sys.exit(0)
