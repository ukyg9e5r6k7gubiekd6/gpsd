#!/usr/bin/env python
#
# Takes a single argument, the line number of a table start.
# Generates various useful code snippets from tables in the
# AIVDM descriptions, or corrects offsets in the tables themselves. 

import sys, getopt

if __name__ == '__main__':
    try:
        (options, arguments) = getopt.getopt(sys.argv[1:], "tc:s:")
    except getopt.GetoptError, msg:
        print "tablecheck.py: " + str(msg)
        raise SystemExit, 1
    generate = maketable = makestruct = False
    for (switch, val) in options:
        if switch == '-c':
            generate = True     # Logic for this is not yet implemented.
            structname = val
        elif switch == '-s':
            makestruct = True
            structname = val
        elif switch == '-t':
            maketable = True

    if not generate and not maketable and not makestruct:
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
    widths = []
    for line in table:
        if '|' in line:
            widths.append(line.split('|')[2].strip())
        else:
            widths.append('')

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
    owidth = max(*map(len, offsets)) 
    for (i, off) in enumerate(offsets):
        offsets[i] += " " * (owidth - len(offsets[i]))

    # Here's where we generate useful output.
    if maketable:
        # Writes the corrected table to standard output.
        print >>sys.stderr, "Total bits:", base 
        for (i, t) in enumerate(table):
            if offsets[i]:
                print "|" + offsets[i] + t[owidth+1:].rstrip()
            else:
                print t.rstrip()
    elif generate:
        # Writes calls to bit-extraction macros to standard output.
        # Requites UBITS and SBITS to act as they do in the AIVDM driver.
        for (i, t) in enumerate(table):
            if '|' in t:
                fields = map(lambda s: s.strip(), t.split('|'))
                width = fields[2]
                name = fields[4]
                ftype = fields[5]
                if ftype == 'x':
                    print "\t/* skip %s bits */" % width
                elif ftype in ('u', 'i'):
                    offset = offsets[i].split('-')[0]
                    print "\t%s.%s\t= %sBITS(%s, %s);" % \
                          (structname, name, {'u':'U', 'i':'S'}[ftype], offset, width)
                else:
                    print "\t/* %s bits of type %s */" % (width, ftype)
    elif makestruct:
        # Write a structure definition correponding to the table.
        print "\tstruct {"
        for (i, t) in enumerate(table):
            if '|' in t:
                fields = map(lambda s: s.strip(), t.split('|'))
                description = fields[3].strip()
                name = fields[4]
                ftype = fields[5]
                if ftype == 'x':
                    continue
                if ftype == 'u':
                    print "\t\tunsigned int %s;\t/* %s*/" % (name, description)
                elif ftype == 'i':
                    print "\t\tint %s;\t/* %s*/" % (name, description)
                elif ftype == 'b':
                    print "\t\tint %s;\t/* %s*/" % (name, description)
                elif ftype == 't':
                    print "\t\tchar %s[];\t/* %s*/" % (name, description)
                else:
                    print "\t/* %s bits of type %s */" % (width, ftype)
        print "\t} %s;" % structname

# end
  
