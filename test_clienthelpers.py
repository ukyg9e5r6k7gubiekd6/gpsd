#!/usr/bin/env python
#
# Test gps/clienthelpers.py
#

# This code runs compatibly under Python 2 and 3.x for x >= 2.
# Preserve this property!
from __future__ import absolute_import, print_function, division

import sys

import gps.clienthelpers

test1 = {
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
}

errors = 0

for test in test1:
    (deg_type, deg, expected) = test
    result = gps.clienthelpers.deg_to_str(deg_type, deg)
    if result != expected:
        print("fail: deg_to_str(%d, %.3f) got %s expected %s" %
              (deg_type, deg, result, expected))
        errors += 1

if errors:
    print("test_clienthhelpers.py: %d tests failed" % errors)
    sys.exit(1)
else:
    print("test_clienthhelpers.py: OK")
    sys.exit(0)
