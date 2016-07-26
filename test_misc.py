#!/usr/bin/env python
#
# Test gps/misc.py
#

# This code runs compatibly under Python 2 and 3.x for x >= 2.
# Preserve this property!
from __future__ import absolute_import, print_function, division

import sys, gps.misc

errors = 0
# values from here: https://en.wikipedia.org/wiki/Decimal_degrees#Precision
# Note the wikipedia numbers are NOT ellipsoid corrected:
# EarthDistanceSmall() is ellipsoid corrected.
#
# I suspect these numbers are not quite right
# someone will promptly correct me?
tests = [
    # equator 1 degree
    (  0.00000000, 0.00000000,  1.00000000, 0.00000000, 110574),
    (  0.00000000, 0.00000000,  0.00000000, 1.00000000, 110574),
    (  0.00000000, 0.00000000,  1.00000010, 1.00000000, 156372),
    # 23N 1 degree
    ( 23.00000000, 0.00000000, 24.00000000, 0.00000000, 110751),
    ( 23.00000000, 0.00000000, 23.00000000, 1.00000000, 101940),
    ( 23.00000000, 0.00000000, 24.00000000, 1.00000000, 150270),
    # 45N 1 degree
    ( 45.00000000, 0.00000000, 46.00000000, 0.00000000, 111141),
    ( 45.00000000, 0.00000000, 45.00000000, 1.00000000,  78582),
    ( 45.00000000, 0.00000000, 46.00000000, 1.00000000, 135723),
    # 67N 1 degree
    ( 67.00000000, 0.00000000, 68.00000000, 0.00000000, 111528),
    ( 67.00000000, 0.00000000, 67.00000000, 1.00000000,  43575),
    ( 67.00000000, 0.00000000, 68.00000000, 1.00000000, 119416),
    # equator 10e-7
    (  0.00000000, 0.00000000,  0.00000010, 0.00000000, 0.011057),
    (  0.00000000, 0.00000000,  0.00000000, 0.00000010, 0.011057),
    (  0.00000000, 0.00000000,  0.00000010, 0.00000010, 0.015638),
    # 23N 10e-7
    ( 23.00000000, 0.00000000, 23.00000010, 0.00000000, 0.011074),
    ( 23.00000000, 0.00000000, 23.00000000, 0.00000010, 0.010194),
    ( 23.00000000, 0.00000000, 23.00000010, 0.00000010, 0.015052),
    # 45N 10e-7
    ( 45.00000000, 0.00000000, 45.00000010, 0.00000000, 0.011113),
    ( 45.00000000, 0.00000000, 45.00000000, 0.00000010, 0.007858),
    ( 45.00000000, 0.00000000, 45.00000010, 0.00000010, 0.013611),
    # 67N 10e-7
    ( 67.00000000, 0.00000000, 67.00000010, 0.00000000, 0.011152),
    ( 67.00000000, 0.00000000, 67.00000000, 0.00000010, 0.0043575),
    ( 67.00000000, 0.00000000, 67.00000010, 0.00000010, 0.011973),
]

# EarthDistanceSmall
for ( lat1, lon1, lat2, lon2, dist) in tests :
    distance = gps.misc.EarthDistanceSmall( (lat1, lon1), (lat2, lon2))
    # compare to 0.01%
    diff = dist - distance
    max_diff = dist * 0.0001
    if abs( diff ) > max_diff:
        sys.stderr.write( \
            "misc test: %.8f %.8f, %.8f %.8f, expected %.7f got %.7f\n" \
            % (lat1, lon1, lat2, lon2, dist, distance))
        errors += 1
    else:
        print("OK" )

if errors:
    sys.exit(1)
else:
    sys.exit(0)
