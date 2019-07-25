#!/usr/bin/env python
"""Simple program to exact 5x5 geoid seperations in EGM2008
from GeoidEval for use in geoidc"""

import sys
import subprocess


for lat in range(-90, 91, 5):
    if -90 != lat:
        sys.stdout.write("},")

    sys.stdout.write("\n    /* %d */\n    { " % lat)

    cnt = 0
    for lon in range(-180, 181, 5):
        ge = subprocess.Popen(["GeoidEval",
                                "-n", "egm2008-1"],
                                stdin=subprocess.PIPE,
                                stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE,
                                bufsize=0)
        out, err = ge.communicate(b"%d %d\n" % (lat, lon))
        # round to even cm
        val = round(float(out) * 100)
        sys.stdout.write("%5d" % val)
        cnt += 1
        if 0 == (cnt % 10):
            sys.stdout.write(",\n      ")
        elif 73 != cnt:
            sys.stdout.write(", ")

sys.stdout.write("}\n};\n")
