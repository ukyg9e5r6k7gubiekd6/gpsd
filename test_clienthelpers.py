#!/usr/bin/env python
#
# Test gps/clienthelpers.py
#

# This code runs compatibly under Python 2 and 3.x for x >= 2.
# Preserve this property!
from __future__ import absolute_import, print_function, division

import os                 # for os.environ()
import subprocess
import sys

import gps.clienthelpers
import gps.misc

test1 = [
    # deg_dd
    (0, 0, "  0.00000000"),
    (0, 89.999, " 89.99900000"),
    (0, 90.1, " 90.10000000"),
    (0, 180.21, "180.21000000"),
    (0, 359.321, "359.32100000"),
    (0, 360.0, "  0.00000000"),
    # deg_ddmm
    (1, 0, "  0 00.000000'"),
    (1, 89.999, " 89 59.940000'"),
    (1, 90.1, " 90 06.000000'"),
    (1, 180.21, "180 12.600000'"),
    (1, 359.321, "359 19.260000'"),
    (1, 360.0, "  0 00.000000'"),
    # deg_ddmmss
    (2, 0, "  0 00' 00.00000\""),
    (2, 89.999, " 89 59' 56.40000\""),
    (2, 90.1, " 90 06' 00.00000\""),
    (2, 180.21, "180 12' 36.00000\""),
    (2, 359.321, "359 19' 15.60000\""),
    (2, 360.0, "  0 00' 00.00000\""),
    ]

test2 = [
    # maidenhead
    (48.86471, 2.37305, "JN18eu", "Paris"),
    (41.93498, 12.43652, "JN61fw", "Rome"),
    (39.9771, -75.1685, "FM29jx", "Philadelphia"),
    (-23.4028, -50.9766, "GG46mo", "Sao Paulo"),
    ]

test3 = [
    # wgs84 separation
    (-27.1127, -109.3497, -5.4363644476), # Easter Island
    (-25.5920,   21.0937, 27.337067632),  # Kalahari Desert
    ( 71.7069,  -42.6043, 41.9313767802), # Greenland
    ( -5.7837,  144.3317, 70.5250281251), # Kuk swamp P
    ]

test4 = [
    # gpsd gpsd_units
    ('GPSD_UNITS' , 'imperial', gps.clienthelpers.imperial),
    ('GPSD_UNITS' , 'nautical', gps.clienthelpers.nautical),
    ('GPSD_UNITS' , 'metric', gps.clienthelpers.metric),
    ('LC_MEASUREMENT', 'en_US', gps.clienthelpers.imperial),
    ('LC_MEASUREMENT', 'C', gps.clienthelpers.imperial),
    ('LC_MEASUREMENT', 'POSIX', gps.clienthelpers.imperial),
    ('LC_MEASUREMENT', 'ru_RU', gps.clienthelpers.metric),
    ('LANG', 'en_US', gps.clienthelpers.imperial),
    ('LANG', 'C', gps.clienthelpers.imperial),
    ('LANG', 'POSIX', gps.clienthelpers.imperial),
    ('LANG', 'ru_RU', gps.clienthelpers.metric),
    ]

errors = 0

for test in test1:
    (deg_type, deg, expected) = test
    result = gps.clienthelpers.deg_to_str(deg_type, deg)
    if result != expected:
        print("fail: deg_to_str(%d, %.3f) got %s expected %s" %
              (deg_type, deg, result, expected))
        errors += 1

for (lat, lon, maidenhead, location) in test2:
    converted = gps.clienthelpers.maidenhead(lat, lon)
    if converted != maidenhead:
        sys.stderr.write(
            "fail: maidenhead test%s, %s (%s)) expected %s got %s\n" %
            (lat, lon, maidenhead, location, converted))
        errors += 1

for (lat, lon, wgs84) in test3:
    separation = gps.clienthelpers.wgs84_separation(lat, lon)
    # check to 1 millimeter
    diff = separation - wgs84
    if 0.001 < diff:
        sys.stderr.write(
            "fail: wgs84_separation(%s, %s) expected %s got %s\n" %
            (lat, lon, wgs84, separation))
        errors += 1


savedenv = os.environ
os.unsetenv('GPSD_UNITS')
os.unsetenv('LC_MEASUREMENT')
os.unsetenv('LANG')
for (key, val, expected) in test4:
    os.environ[key] = val

    result = gps.clienthelpers.gpsd_units()
    os.unsetenv(key)

    if result != expected:
        print("fail: gpsd_units() %s=%s got %s expected %d" %
              (key, val, str(result), expected))
        errors += 1

# restore environment
os.environ = savedenv

if errors:
    print("test_clienthhelpers.py: %d tests failed" % errors)
    sys.exit(1)
else:
    print("test_clienthhelpers.py: OK")
    sys.exit(0)
