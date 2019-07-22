#!/usr/bin/env python
#
# Test gps/clienthelpers.py
#
# This code runs compatibly under Python 2 and 3.x for x >= 2.
# Preserve this property!
"""Partial test suite for gps.clienthelpers module."""

from __future__ import absolute_import, print_function, division

import math               # for math.fabs()
import os                 # for os.environ()
import sys                # for stderr, etc.

import gps.clienthelpers
import gps.misc

debug = 0

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
    (90, 180, "SS00aa", "North Pole"),
    (-90, -180, "AA00aa", "South Pole"),
    ]

test3 = [
    # wgs84 separation, cm precision
    # online calculator:
    # https://geographiclib.sourceforge.io/cgi-bin/GeoidEval
    # what MSL is this?  EGM208, EGM96, EGM84, or??
    # Seems closest to EGM2008, but still off by a meter.

    # Easter Island: EGM2008 -3.8178, EGM96 -4.9979, EGM84 -5.1408
    (-27.1127, -109.3497, -5.75, "Easter Island"),
    # Kalahar: EGM2008 27.6676, EGM96 27.6659, EGM84 27.1081
    (-25.5920, 21.0937, 27.62,  "Kalahari Desert"),
    # Greenland: EGM2008 40.3981, EGM96 40.5912, EGM84 41.7056
    (71.7069, -42.6043, 40.11, "Greenland"),
    # Kuk Swamp: EGM2008 82.3395, EGM96 82.6719, EGM84 79.2188
    # seems to be over a gravitational anomaly
    (-5.7837, 144.3317, 71.08, "Kuk Swamp PG"),
    # KBDN: EGM2008 -20.3509, EGM96 -19.8008, EGM84 -18.5562
    (44.094556, -121.200222, -21.95, "Bend Airport, OR (KBDN)"),
    # PANC: EGM2008 7.6508, EGM96 7.8563, EGM84 8.0838
    (61.171333, -149.991164, 13.54, "Anchorage Airport, AK (PANC)"),
    # KDEN: EGM2008 -18.1941, EGM96 -18.4209, EGM84 -15.9555
    (39.861666, -104.673166, -18.15, "Denver Airport, CO (KDEN)"),
    # KDEN: EGM2008 46.4499, EGM96 46.3061, EGM84 47.7620
    (51.46970, -0.45943, 46.26, "London Heathrow Airport, UK (LHR)"),
    # SCPE: EGM2008 37.9575, EGM96 39.3446, EGM84 46.6672
    (-22.92170, -68.158401, 35.46, "San Pedro de Atacama Airport, CL (SCPE)"),
    # SIN: EGM2008 8.6453, EGM96 8.3503, EGM84 8.2509
    (1.350190, 103.994003, 7.51, "Singapore Changi Airport, SG (SIN)"),
    # UURB: EGM2008 13.6322, EGM96 13.6448, EGM84 13.1280
    (55.617199, 38.06000, 13.22, "Moscow Bykovo Airport, RU (UUBB)"),
    # SYD: EGM2008 22.0915, EGM96 22.2038, EGM84 21.7914
    (-33.946098, 151.177002, 21.89, "Sydney Airport, AU (SYD)"),

    # some 10x10 points, s/b exact EGM2008, +/- rounding
    # North Poll: EGM2008 14.8980, EGM96 13.6050, EGM84 13.0980
    (90, 0, 14.90, "North Poll"),
    # Doyle: EGM2008 -23.3366, EGM96 -23.3278, EGM84 -21.1672
    (40, -120, -23.34, "Near Doyle, CA"),
    # Equator 0, EGM2008 17.2260, EGM96 17.1630, EGM84 18.3296
    (0, 0, 17.23, "Equator 0W"),
    # South Poll: EGM2008 -30.1500, EGM96 -29.5350, EGM84 -29.7120
    (-90, 0, -30.15, "South Poll"),

    # some 5x5 points, s/b exact EGM2008, +/- rounding
    # -5, -5: EGM2008 17.1068, EGM96 16.8362, EGM84 17.5916
    (-5, -5, 17.11, "-5, -5"),
    # -5, 5: EGM2008 9.3988, EGM96 9.2399, EGM84 9.7948
    (-5, 5, 9.40, "-5, 5"),
    # 5, 5:  EGM2008 21.2609, EGM96 20.8917, EGM84 20.3509
    (5, 5, 21.26, "5, 5"),
    # 5, -5: EGM2008 25.7668, EGM96 25.6144, EGM84 25.1224
    (5, -5, 25.77, "5, 5"),

    # test data for some former corners in the code
    # 0, -78.452222: EGM2008 26.8978, EGM96 25.3457, EGM84 26.1507
    (0, -78.452222, 15.98, "Equatorial Sign Bolivia"),
    # 51.4778067, 0: EGM2008 45.8961, EGM96 45.7976, EGM84 47.2468
    (51.4778067, 0, 45.46, "Lawn Greenwich Observatory UK"),
    ]

test4 = [
    # gpsd gpsd_units
    ('GPSD_UNITS', 'imperial', gps.clienthelpers.imperial),
    ('GPSD_UNITS', 'nautical', gps.clienthelpers.nautical),
    ('GPSD_UNITS', 'metric', gps.clienthelpers.metric),
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

for (lat, lon, wgs84, location) in test3:
    separation = gps.clienthelpers.wgs84_separation(lat, lon)
    # check to 1 millimeter
    diff = separation - wgs84
    if debug:
        print("diff %f sep %f wgs84 %f" % (diff, separation, wgs84))
    if 0.009 < math.fabs(diff):
        sys.stderr.write(
            "fail: wgs84_separation(%s, %s) (%s) expected %.2f got %.2f\n" %
            (lat, lon, location, wgs84, separation))
        errors += 1


savedenv = os.environ
# from the python doc:
# calls to unsetenv() don't update os.environ, so it is actually
# preferable to delete items of os.environ.
for key in ['GPSD_UNITS', 'LC_MEASUREMENT', 'LANG']:
    if key in os.environ:
        del os.environ[key]

for (key, val, expected) in test4:
    os.environ[key] = val

    result = gps.clienthelpers.gpsd_units()
    del os.environ[key]

    if result != expected:
        print("fail: gpsd_units() %s=%s got %s expected %d" %
              (key, val, str(result), expected))
        errors += 1

# restore environment
os.environ = savedenv

if errors:
    print("test_clienthelpers.py: %d tests failed" % errors)
    sys.exit(1)
else:
    print("test_clienthelpers.py: OK")
    sys.exit(0)
