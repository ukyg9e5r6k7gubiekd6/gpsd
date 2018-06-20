#!/usr/bin/env python
#
# Christian Gagneraud - 2012
# Simple python script that will parse json dictionaries on its input,
# If it fails, it will print the offending line and an error message.
# The goal is to check that GPSD outputs valid JSON.
#
# This code runs compatibly under Python 2 and 3.x for x >= 2.
# Preserve this property!
from __future__ import absolute_import, print_function, division

import json
import sys

success = True
lc = 0
for line in sys.stdin.readlines():
    lc += 1
    try:
        # Load the json dictionary, it should raise an error if it is malformed
        item = json.loads(line)
    except ValueError as e:
        success = False
        print("%d: %s" % (lc, line.strip()))
        print("%d: %s" % (lc, e))

exit(0 if success else 1)
