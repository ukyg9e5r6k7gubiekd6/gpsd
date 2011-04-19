#!/usr/bin/env python
#
# Takes a single argument, the line number of a table start.

import sys, getopt

if __name__ == '__main__':
    try:
        (options, arguments) = getopt.getopt(sys.argv[1:], "tc:")
    except getopt.GetoptError, msg:
        print "tablecheck.py: " + str(msg)
        raise SystemExit, 1
    generate = maketable = False
    for (switch, val) in options:
        if switch == '-c':
            generate = True     # Logic for this is not yet implemented.
            prefix = val
        elif switch == '-t':
            maketable = True

    if not generate and not maketable:
        print >>sys.stderr, "tablecheck.py: no mode selected"
        sys.exit(1)

    # First, read in the table
    startline = int(arguments[0])
    table = []
    keep = False
    i = 0
    for line in sys.stdin:
        i += 1
        if i == startline:
            if line.startswith("|="):
                keep = True
            else:
                print >>sys.stderr, "Bad table start"
                sys.exit(1)
        elif line.startswith("|="):
            keep = False
        if keep:
            table.append(line)
    table = table[2:]
    widths = map(lambda s: s.split('|')[2].strip(), table)

    # Compute offsets for an AIVDM message breakdown, given the bit widths.
    offsets = []
    base = 0
    for w in widths:
        if not w:
            offsets.append('')
        else:
            w = int(w)
            offsets.append("%d-%d" % (base, base + w - 1))
            base += w
    print >>sys.stderr, "Total bits:", base 
    owidth = max(*map(len, offsets)) 
    for (i, off) in enumerate(offsets):
        offsets[i] += " " * (owidth - len(offsets[i]))

    if maketable:
        # Writes the corrected table to standard output.
        for (i, t) in enumerate(table):
            print "|" + offsets[i] + t[owidth+1:].rstrip()
    elif generate:
        # Writes calls to bit-extraction macros to standard output
        for (i, t) in enumerate(table):
            fields = map(lambda s: s.strip(), t.split('|'))
            name = fields[4]
            if name:
                offset = offsets[i].split('-')[0]
                width = fields[2]
                print "\t%s.%s\t= UBITS(%s, %s);" % (prefix, name, offset, width)

    
