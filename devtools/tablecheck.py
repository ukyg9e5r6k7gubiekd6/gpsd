#!/usr/bin/env python
#
# Compute offsets for an AIVDM message breakdown, given the bit widths.
# Takes a single argument, the line number of a table start.
# Writes the corrected table to standard output.

import sys

if __name__ == '__main__':
    startline = int(sys.argv[1])

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
    
    for (i, t) in enumerate(table):
        print "|" + offsets[i] + t[owidth+1:].rstrip()


    
