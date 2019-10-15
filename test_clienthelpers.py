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

test1 = [(0, 0, "  0.00000000"),          # deg_dd
         (0, 89.999, " 89.99900000"),
         (0, 90.1, " 90.10000000"),
         (0, 180.21, "180.21000000"),
         (0, 359.321, "359.32100000"),
         (0, 360.0, "  0.00000000"),
         (1, 0, "  0 00.000000'"),        # deg_ddmm
         (1, 89.999, " 89 59.940000'"),
         (1, 90.1, " 90 06.000000'"),
         (1, 180.21, "180 12.600000'"),
         (1, 359.321, "359 19.260000'"),
         (1, 360.0, "  0 00.000000'"),
         (2, 0, "  0 00' 00.00000\""),    # deg_ddmmss
         (2, 89.999, " 89 59' 56.40000\""),
         (2, 90.1, " 90 06' 00.00000\""),
         (2, 180.21, "180 12' 36.00000\""),
         (2, 359.321, "359 19' 15.60000\""),
         (2, 360.0, "  0 00' 00.00000\""),
         ]

# maidenhead
# keep in sync with tests/test_gpsdclient.c
test2 = [(48.86471, 2.37305, "JN18eu", "Paris"),
         (41.93498, 12.43652, "JN61fw", "Rome"),
         (39.9771, -75.1685, "FM29jx", "Philadelphia"),
         (-23.4028, -50.9766, "GG46mo", "Sao Paulo"),
         (90, 180, "RR99xx", "North Pole"),
         (-90, -180, "AA00aa", "South Pole"),
         ]

test3 = [
    #  wgs84 separation, cm precision
    #  online calculator:
    #  https://geographiclib.sourceforge.io/cgi-bin/GeoidEval
    #
    # magnetic variation, hundredths of a degree precision.
    #
    # same tests as tests/test_geoid.c.  Keep them in sync.
    #

    # Easter Island: EGM2008 -3.8178, EGM96 -4.9979, EGM84 -5.1408
    # wmm2015 2.90
    (27.1127, -109.3497, -31.29, 8.45, "Easter Island"),
    # Kalahari: EGM2008, 20.6560, EGM96, 20.8419, EGM84 23.4496
    # wmm2015 6.73
    (25.5920, 21.0937, 21.80, 3.35,  "Kalahari Desert"),
    # Greenland: EGM2008 40.3981, EGM96 40.5912, EGM84 41.7056
    # wmm2015 28.26  AWEFUL!
    (71.7069, -42.6043, 40.11, -28.25, "Greenland"),
    # Kuk Swamp: EGM2008 62.8837, EGM96, 62.8002, EGM84, 64.5655
    # wmm2015 9.11
    # seems to be over a gravitational anomaly
    (5.7837, 144.3317, 62.34, 2.42, "Kuk Swamp PG"),
    # KBDN: EGM2008 -20.3509, EGM96 -19.8008, EGM84 -18.5562
    # wmm2015 14.61
    (44.094556, -121.200222, -21.95, 14.40, "Bend Airport, OR (KBDN)"),
    # PANC: EGM2008 7.6508, EGM96 7.8563, EGM84 8.0838
    # wmm2015 15.78
    (61.171333, -149.991164, 13.54, 15.52, "Anchorage Airport, AK (PANC)"),
    # KDEN: EGM2008 -18.1941, EGM96 -18.4209, EGM84 -15.9555
    # wmm2015 7.95
    (39.861666, -104.673166, -18.15, 7.84, "Denver Airport, CO (KDEN)"),
    # LHR: EGM2008 46.4499, EGM96 46.3061, EGM84 47.7620
    # wmm2015 0.17
    (51.46970, -0.45943, 46.26, -0.28, "London Heathrow Airport, UK (LHR)"),
    # SCPE: EGM2008 37.9592, EGM96 39.3400, EGM84 46.6604
    # wmm2015 -6.17
    (-22.92170, -68.158401, 35.46, -6.11,
     "San Pedro de Atacama Airport, CL (SCPE)"),
    # SIN: EGM2008 8.6453, EGM96 8.3503, EGM84 8.2509
    # wmm2015 0.22
    (1.350190, 103.994003, 7.51, 0.17, "Singapore Changi Airport, SG (SIN)"),
    # UURB: EGM2008 13.6322, EGM96 13.6448, EGM84 13.1280
    # wmm2015 11.41
    (55.617199, 38.06000, 13.22, 11.42, "Moscow Bykovo Airport, RU (UUBB)"),
    # SYD: EGM2008 13.0311, EGM96 13.3736, EGM84 13.3147
    # wmm2015 -4.28
    (33.946098, 151.177002, 13.59, -4.26, "Sydney Airport, AU (SYD)"),
    # Doyle: EGM2008 -23.3366, EGM96 -23.3278, EGM84 -21.1672
    # wmm2015 13.35
    (40, -120, -23.34, 13.35, "Near Doyle, CA"),

    # test calc at delta lat == 0
    # North Poll: EGM2008 14.8980, EGM96 13.6050, EGM84 13.0980
    # wmm2015 1.75
    (90, 0, 14.90, 1.75, "North Poll 0"),
    # wmm2015 3.75
    (90, 2, 14.90, 3.75, "North Poll 2"),
    # wmm2015 4.25
    (90, 2.5, 14.90, 4.25, "North Poll 2.5"),
    # wmm2015 1.75
    (90, 3, 14.90, 4.75, "North Poll 3"),
    # wmm2015 1.75
    (90, 5, 14.90, 6.75, "North Poll 5"),
    # wmm2015 -178.25
    (90, 180, 14.90, -178.25, "North Poll"),

    # Equator 0, EGM2008 17.2260, EGM96 17.1630, EGM84 18.3296
    # wmm2015 -4.84
    (0, 0, 17.23, -4.84, "Equator 0W"),

    # South Poll: EGM2008 -30.1500, EGM96 -29.5350, EGM84 -29.7120
    # wmm2015 -30.80
    (-90, 0, -30.15, -30.80, "South Poll"),

    # test calc at delta lon == 0
    # 2 0: EGM2008 17.1724, EGM96 16.8962, EGM84 17.3676
    # wmm2015 -4.17
    (2, 0, 18.42, -4.23, "2N 0W"),
    # 2.5 0: EGM2008 16.5384, EGM96 16.5991, EGM84 17.0643
    # wmm2015 -4.02
    (2.5, 0, 18.71, -4.08, "2.5N 0W"),
    # 3 0: EGM2008 16.7998, EGM96 16.6161, EGM84 16.7857
    # wmm2015 -3.87
    (3, 0, 19.01, -3.92, "3N 0W"),
    # 3.5 0: EGM2008 17.0646, EGM96 17.0821, EGM84 16.7220
    # wmm2015 -3.72
    (3.5, 0, 19.31, -3.77, "3.5N 0W"),
    # 5 0: EGM2008 20.1991, EGM96 20.4536, EGM84 20.3181
    # wmm2015 -3.31
    (5, 0, 20.20, -3.31, "5N 0W"),

    # test calc on diagonal
    # Equator 0, EGM2008 17.2260, EGM96 17.1630, EGM84 18.3296
    # wmm2015 -4.84
    # 2 2: EGM2008 16.2839, EGM96 16.1579, EGM84 17.5354
    # wmm2015 -3.53
    (2, 2, 18.39, -3.60, "2N 2E"),
    # 2.5 2.5: EGM2008 15.7918, EGM96 15.5314, EGM84 16.3230
    # wmm2015 -3.24
    (2.5, 2.5, 18.78, -3.30, "2.5N 2.5E"),
    # 3 3: EGM2008 15.2097, EGM96 15.0751, EGM84 14.6542
    # wmm2015 -2.95
    (3, 3, 19.20, -3.01, "3N 3E"),
    # 3.5 3.5: EGM2008 14.8706, EGM96 14.6668, EGM84 13.9592
    # wmm2015 -3.72
    (3.5, 3.5, 19.66, -2.73, "3.5N 3.5E"),

    # some 5x5 points, s/b exact EGM2008, +/- rounding
    # 5, 5:  EGM2008 21.2609, EGM96 20.8917, EGM84 20.3509
    # wmm2015 -1.91
    (5, 5, 21.26, -1.91, "5, 5"),
    # -5, -5: EGM2008 17.1068, EGM96 16.8362, EGM84 17.5916
    # wmm2015 -9.03
    (-5, -5, 17.11, -9.03, "-5, -5"),
    # -5, 5: EGM2008 9.3988, EGM96 9.2399, EGM84 9.7948
    # wmm2015 -4.80
    (-5, 5, 9.40, -4.80, "-5, 5"),
    # 5, -5: EGM2008 25.7668, EGM96 25.6144, EGM84 25.1224
    # wmm2015 -4.90
    (5, -5, 25.77, -4.90, "5, 5"),

    # test data for some former corners in the code
    # 0, -78.452222: EGM2008 26.8978, EGM96 25.3457, EGM84 26.1507
    # wmm2015 -3.87
    (0, -78.452222, 15.98, -3.89, "Equatorial Sign Bolivia"),
    # 51.4778067, 0: EGM2008 45.8961, EGM96 45.7976, EGM84 47.2468
    # wmm2015 -0.10
    (51.4778067, 0, 45.46, -0.11, "Lawn Greenwich Observatory UK"),
    # 0, 180: EGM2008 21.2813, EGM96 21.1534, EGM84 21.7089
    # wmm2015 9.75
    (0, 180, 21.28, 9.75, "Far away from Google default"),
    (0, -180, 21.28, 9.75, "Away far from Google default"),
]

# gpsd gpsd_units
test4 = [('GPSD_UNITS', 'imperial', gps.clienthelpers.imperial),
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

# check wgs84_separation()
for (lat, lon, wgs84, var, desc) in test3:
    separation = gps.clienthelpers.wgs84_separation(lat, lon)
    # check to 1 millimeter
    diff = separation - wgs84
    if debug:
        print("diff %f sep %f wgs84 %f" % (diff, separation, wgs84))
    if 0.009 < math.fabs(diff):
        sys.stderr.write(
            "fail: wgs84_separation(%s, %s) (%s) expected %.2f got %.2f\n" %
            (lat, lon, desc, wgs84, separation))
        errors += 1

# check mag_var()
for (lat, lon, wgs84, var, desc) in test3:
    magvar = gps.clienthelpers.mag_var(lat, lon)
    # check to 0.1 degree
    diff = magvar - var
    if debug:
        print("diff %f magvar %f s/b %f" % (diff, magvar, var))
    if 0.09 < math.fabs(diff):
        sys.stderr.write(
            "fail: mag_var(%s, %s) (%s) expected %.2f got %.2f\n" %
            (lat, lon, desc, var, magvar))
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
