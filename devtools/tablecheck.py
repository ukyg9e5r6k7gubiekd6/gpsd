#!/usr/bin/env python
#
# Takes a single argument, the line number of a table start.
# Generates various useful code snippets from tables in the
# AIVDM descriptions, or corrects offsets in the tables themselves. 

import sys, getopt

if __name__ == '__main__':
    try:
        (options, arguments) = getopt.getopt(sys.argv[1:], "tc:s:d:S:E:")
    except getopt.GetoptError, msg:
        print "tablecheck.py: " + str(msg)
        raise SystemExit, 1
    generate = maketable = makestruct = makedump = False
    after = before = None
    for (switch, val) in options:
        if switch == '-c':
            generate = True     # Logic for this is not yet implemented.
            structname = val
        elif switch == '-s':
            makestruct = True
            structname = val
        elif switch == '-t':
            maketable = True
        elif switch == '-d':
            makedump = True
            structname = val
        elif switch == '-S':
            after = val
        elif switch == '-E':
            before = val

    if not generate and not maketable and not makestruct and not makedump:
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
                          (structname, name, {'u':'U', 'i':'S'}[ftype.tolower()], offset, width)
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
                if ftype == 'u' or ftype[0] == 'U':
                    print "\t\tunsigned int %s;\t/* %s*/" % (name, description)
                elif ftype == 'i' or ftype[0] == 'I':
                    print "\t\tint %s;\t/* %s*/" % (name, description)
                elif ftype == 'b':
                    print "\t\tint %s;\t/* %s*/" % (name, description)
                elif ftype == 't':
                    print "\t\tchar %s[];\t/* %s*/" % (name, description)
                else:
                    print "\t/* %s bits of type %s */" % (width, ftype)
        print "\t} %s;" % structname
    elif makedump:
        # Write the skeleton of a JSON dump corresponding to the table
        baseindent = " " * 8
        step = " " * 4
        unscaled = ""
        scaled = ""
        has_scale = []
        names = []
        record = after is None
        header = "(void)snprintf(buf + strlen(buf), buflen - strlen(buf),"
        for (i, t) in enumerate(table):
            if '|' in t:
                fields = map(lambda s: s.strip(), t.split('|'))
                name = fields[4]
                ftype = fields[5]
                if after == name:
                    record = True
                    continue
                if before == name:
                    record = False
                    continue
                if ftype == 'x' or not record:
                    continue
                fmt = r'\"%s\":' % name
                if ftype == 'u':
                    names.append(name)
                    fmt += "%u"
                    scaled += fmt
                    unscaled += fmt
                    has_scale.append(False)
                elif ftype == 'i':
                    names.append(name)
                    fmt += "%d"
                    scaled += fmt
                    unscaled += fmt
                    has_scale.append(False)
                elif ftype == 'i':
                    names.append(name)
                    fmt += "%d"
                    scaled += fmt
                    unscaled += fmt
                    has_scale.append(False)
                elif ftype == 't':
                    names.append(name)
                    fmt += r'\"%s\"'
                    scaled += fmt
                    unscaled += fmt
                    has_scale.append(False)
                elif ftype == 'b':
                    names.append("JSON_BOOL(" + name + ")")
                    fmt += "%d"
                    scaled += fmt
                    unscaled += fmt
                    has_scale.append(False)
                elif ftype[0] == 'U':
                    names.append(name)
                    scaled += fmt + "%%.%sf" % ftype[1]
                    unscaled += fmt + "%u"
                    has_scale.append(True)
                elif ftype[0] == 'I':
                    names.append(name)
                    scaled += fmt + "%%.%sf" % ftype[1]
                    unscaled += fmt + "%d"
                    has_scale.append(True)
                scaled += ","
                unscaled += ","
        scaled = scaled[:-1]
        unscaled = unscaled[:-1]
        if scaled == unscaled:
            print baseindent + header
            print (baseindent + step) + '"%s",' % unscaled
            for (i, n) in enumerate(names):
                if i < len(names) - 1:
                    print (baseindent + step) + '%s,' % n
                else:
                    print (baseindent + step) + '%s);' % n
        else:
            print (baseindent + step) + "if (scaled)"
            print (baseindent + step*2) + header
            print (baseindent + step*3) + '"%s",' % scaled
            for (i, n) in enumerate(names):
                if has_scale[i]:
                    n += " * SCALE"
                if i < len(names) - 1:
                    print (baseindent + step*4) + '%s,' % n
                else:
                    print (baseindent + step*4) + '%s);' % n
            print (baseindent + step) + "else"
            print (baseindent + step*2) + header
            print (baseindent + step*3) + '"%s",' % unscaled
            for (i, n) in enumerate(names):
                if i < len(names) - 1:
                    print (baseindent + step*4) + '%s,' % n
                else:
                    print (baseindent + step*4) + '%s);' % n


# end
  
