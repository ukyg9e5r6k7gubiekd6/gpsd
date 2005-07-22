#!/usr/bin/python
#
# Wrap bitfield references in rtcm.c in inversion macros.
# FIXME: should be enhanced to replace macros already present

import sys, re, getopt

state = 0
ids = {}

refmatch = "[a-z_]*->w[0-9]+\.%s(?![a-z_])"	# Match a bitfield reference

(options, arguments) = getopt.getopt(sys.argv[1:], "l")
list = False;
for (switch, val) in options:
    if (switch == '-l'):
        list = True

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
        # Sort keys so longest IDs will be matched longest first
        keys = ids.keys()
        keys.sort(lambda x, y: len(y) - len(x) or cmp(x, y))
        sys.stderr.write(`keys` + "\n")
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
        if not list:
            sys.stdout.write(line)
        continue

    # Do the actual substitutions
    for key in keys:
        m = re.compile(refmatch % key).search(line)
        #sys.stderr.write("Looking for: %s\n" % refmatch % key)
        if m:
            line = line[:m.start(0)] + ids[key] + "(" + m.group(0) + ")" + line[m.end(0):]
    if not list:
        sys.stdout.write(line)

if list:
    already = []
    vals = ids.values()
    vals.sort()
    print "#if __BYTE_ORDER == __LITTLE_ENDIAN"
    for v in vals:
        if not v in already:
            print "#define %s(x)	x" % v
            already.append(v)
    print "#endif"
