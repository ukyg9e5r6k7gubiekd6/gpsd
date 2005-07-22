#!/usr/bin/python
#
# Wrap bitfield references in rtcm.c in inversion macros.

import sys, re

state = 0
ids = {}

refmatch = "[a-z]*->w[0-9]+\.%s"	# Match a bitfield reference

while True:
    line = sys.stdin.readline()
    if not line:
        break

    # Where are we?
    if re.compile("#pragma pack").match(line):
        state = 1
    elif re.compile("end of bitfield declarations").match(line):
        state = 0
    elif line.find("static void unpack") > -1:
        state = 2
    elif state == 2 and line[0] == '}':
        state = 0

    if state == 1:
        # Gather information on bitfield widths and signs 
        id = None
        regexp = re.compile(r"uint\s+(.*):([0-9]*);")
        m = regexp.search(line)
        if m:
            (id, sign, width) = (m.group(1), "unsigned", m.group(2))
        else:
            regexp = re.compile(r"int\s+(.*):([0-9]*);")
            m = regexp.search(line)
            if m:
                (id, sign, width) = (m.group(1), "signed", m.group(2))

        # Filter that information
        if id and width != "1" and id not in ("parity", "_pad"):
            if id not in ids:
                ids[id] = sign + width
            elif ids[id] != sign + width:
                sys.stderr.write("*** Inconsistent declaration for %s!\n" % id)
                sys.exit(1)

    # Some lines get copied out unaltered, some get hacked.
    if state <= 1:
        #sys.stdout.write(line)
        continue

    # Do the actual substitutions
    for (key, value) in ids.items():
        m = re.compile(refmatch % key).search(line)
        if m:
            line = line[:m.start(0)] + value + "(" + m.group(0) + ")" + line[m.end(0):]
    sys.stdout.write(line)
        
