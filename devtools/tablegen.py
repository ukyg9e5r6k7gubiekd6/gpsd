#!/usr/bin/env python
#
# This tool is intended to automate away the drudgery in bring up support
# for a new AIS message type.  It parses the tabular description of a message
# and generates various useful code snippets from that.  It can also be used to
# correct offsets in the tables themselves.
#
# Requires the AIVDM.txt file on standard input. Takes a single argument,
# the line number of a table start.  Things you can generate:
#
# * -s: A structure definition capturing the message info, with member
#   names extracted from the table and types computed from it.
#
# * -c: Bit-extraction code for the AIVDM driver.  Grind out the right sequence
#   of UBITS, SBITS, and UCHARS macros, and assignments to structure members,
#   guaranteed correct if the table offsets and widths are.
#
# * -d: Code to dump the contents of the unpacked message structure as JSON. If
#   the structure has float members, you'll get an if/then/else  guarded by
#   the scaled flag.
#
# * -r: A Python initializer stanza for jsongen.py, which is in turn used to
#   generate the specification structure for a JSON parse that reads JSON
#   into an instance of the message structure.
#
# * -t: A corrected version of the table.  It will redo all the offsets to be
#   in conformance with the bit widths.  
#
# * -a: Generate all of the above, not to stdout but to files named with
#   the argument as a distinguishing part of the stem.
#
# This generates almost all the code required to support a new message type.
# It's not quite "Look, ma, no handhacking!" You'll need to add default
# values to the Python stanza. If the structure definition contains character
# arrays, you'll have to fill in the dimensions by hand.  You'll need to add
# a bit of glue to ais_json.c so that json_ais_read() actually calls the parser
# handing it the specification structure as a control argument.
#
# The -a, -c, -s, -d, and -r modes all take an argument, which should be a
# structure reference prefix to be prepended (before a dot) to each fieldname.
# Usually you'll need this to look something like "ais->typeN", but it could be
# "ais->typeN.FOO" if the generated code has to operate on a union member
# inside a type 6 or 8, or something similar.
#
# The -S and -E options allow you to generate code only for a specified span
# of fields in the table.  This may be useful for dealing with groups of
# messages that have a common head section.
#
# TO-DO: generate code for ais.py.

import sys, getopt

def correct_table(wfp):
    # Writes the corrected table to standard output.
    print >>sys.stderr, "Total bits:", base 
    for (i, t) in enumerate(table):
        if offsets[i].strip():
            print >>wfp, "|" + offsets[i] + t[owidth+1:].rstrip()
        else:
            print >>wfp, t.rstrip()

def make_driver_code(wfp):
    # Writes calls to bit-extraction macros to standard output.
    # Requites UBITS, SBITS, UCHARS to act as they do in the AIVDM driver.
    record = after is None
    for (i, t) in enumerate(table):
        if '|' in t:
            fields = map(lambda s: s.strip(), t.split('|'))
            width = fields[2]
            name = fields[4]
            ftype = fields[5]
            if after == name:
                record = True
                continue
            if before == name:
                record = False
                continue
            if not record:
                continue
            if ftype == 'x':
                print >>wfp,"\t/* skip %s bit%s */" % (width, ["", "s"][width>'1'])
                continue
            offset = offsets[i].split('-')[0]
            if ftype[0].lower() in ('u', 'i'):
                print >>wfp,"\t%s.%s\t= %sBITS(%s, %s);" % \
                      (structname, name, {'u':'U', 'e':'U', 'i':'S'}[ftype[0].lower()], offset, width)
            elif ftype == 't':
                print >>wfp,"\tUCHARS(%s, %s.%s);" % (offset, structname, name)
            else:
                print >>wfp,"\t/* %s bits of type %s */" % (width, ftype)

def make_structure(wfp):
    # Write a structure definition correponding to the table.
    record = after is None
    baseindent = 8
    step = 4
    def tabify(n):
        return ('\t' * (n / 8)) + (" " * (n % 8)) 
    print >>wfp, tabify(baseindent) + "struct {"
    for (i, t) in enumerate(table):
        if '|' in t:
            fields = map(lambda s: s.strip(), t.split('|'))
            width = fields[2]
            description = fields[3].strip()
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
            if ftype == 'u' or ftype == 'e' or ftype[0] == 'U':
                decl = "unsigned int %s;\t/* %s */" % (name, description)
            elif ftype == 'i' or ftype[0] == 'I':
                decl = "signed int %s;\t/* %s */" % (name, description)
            elif ftype == 'b':
                decl = "signed int %s;\t/* %s */" % (name, description)
            elif ftype == 't':
                stl = int(width)/6
                decl = "char %s[%d+1];\t/* %s */" % (name, stl, description)
            else:
                decl += "/* %s bits of type %s */" % (width, ftype)
            print >>wfp, tabify(baseindent + step) + decl
    print >>wfp, tabify(baseindent) + "} %s;" % structname

def make_json_dumper(wfp):
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
            elif ftype == 'e':
                names.append(name)
                fmt += "%u"
                scaled += "%u"
                unscaled += "%s"   # Will throw error at compilation time
                has_scale.append(True)
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
            elif ftype[0] == 'd':
                names.append("/* data length */")
                names.append("gpsd_hexdump(" + name + ")")
                fmt + "%zd:%s"
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
            else:
                print >>sys.stderr, "Unknown type code", ftype
                sys.exit(0)
            scaled += ","
            unscaled += ","
    scaled = scaled[:-1]
    unscaled = unscaled[:-1]
    if scaled == unscaled:
        print >>wfp, baseindent + header
        print >>wfp, (baseindent + step) + '"%s",' % unscaled
        for (i, n) in enumerate(names):
            if i < len(names) - 1:
                print >>wfp, (baseindent + step) + structname + '.%s,' % n
            else:
                print >>wfp, (baseindent + step) + structname + '.%s);' % n
    else:
        print >>wfp, (baseindent + step) + "if (scaled)"
        print >>wfp, (baseindent + step*2) + header
        print >>wfp, (baseindent + step*3) + '"%s",' % scaled
        for (i, n) in enumerate(names):
            if has_scale[i]:
                n += " * SCALE"
            arg = (baseindent + step*3) + structname + '.%s' % n
            if i < len(names) - 1:
                print >>wfp, arg + ","
            else:
                print >>wfp, arg + ");"
        print >>wfp, (baseindent + step) + "else"
        print >>wfp, (baseindent + step*2) + header
        print >>wfp, (baseindent + step*3) + '"%s",' % unscaled
        for (i, n) in enumerate(names):
            arg = (baseindent + step*3) + structname + '.%s' % n
            if i < len(names) - 1:
                print >>wfp, arg + ','
            else:
                print >>wfp, arg + ';'

def make_json_generator(wfp):
    # Write a stanza for jsongen.py.in describing how to generate a
    # JSON parser initializer from this table. You need to fill in
    # __INITIALIZER__ and default values after this is generated.
    baseindent = " " * 8
    step = " " * 4
    record = after is None
    print >>wfp, '''\
    {
        "initname" : "__INITIALIZER__",
        "headers": ("AIS_HEADER",),
        "structname": "%s",
        "fieldmap":(
            # fieldname    type        default''' % (structname,)
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
            # Depends on the assumption that the resd code
            # always sees unscaled JSON.
            readtype = {
                'u': "uinteger",
                'U': "uinteger",
                'e': "uinteger",
                'i': "integer",
                'I': "integer",
                'b': "boolean",
                't': "string",
                'd': "string",
                }[ftype[0]]
            default = {
                'u': "'PUT_DEFAULT_HERE'",
                'U': "'PUT_DEFAULT_HERE'",
                'e': "'PUT DEFAYLT HERE'",
                'i': "'PUT_DEFAULT_HERE'",
                'I': "'PUT_DEFAULT_HERE'",
                'b': "\'false\'",
                't': "None",
                }[ftype[0]]
            print >>wfp, "            ('%s',%s '%s',%s %s)," % (name,
                                                     " "*(10-len(name)),
                                                     readtype,
                                                     " "*(8-len(readtype)),
                                                     default)
    print >>wfp, "        ),"
    print >>wfp, "    },"

if __name__ == '__main__':
    try:
        (options, arguments) = getopt.getopt(sys.argv[1:], "a:tc:s:d:S:E:r:")
    except getopt.GetoptError, msg:
        print "tablecheck.py: " + str(msg)
        raise SystemExit, 1
    generate = maketable = makestruct = makedump = readgen = all = False
    after = before = None
    for (switch, val) in options:
        if switch == '-a':
            all = True
            structname = val
        elif switch == '-c':
            generate = True
            structname = val
        elif switch == '-s':
            makestruct = True
            structname = val
        elif switch == '-t':
            maketable = True
        elif switch == '-d':
            makedump = True
            structname = val
        elif switch == '-r':
            readgen = True
            structname = val
        elif switch == '-S':
            after = val
        elif switch == '-E':
            before = val

    if not generate and not maketable and not makestruct and not makedump and not readgen and not all:
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
            if line[0] == '|':
                fields = line.split("|")
                fields[1] = " " * len(fields[1])
                line = "|".join(fields)
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
    if all:
        make_driver_code(open(structname + ".c", "w"))
        make_structure(open(structname + ".h", "w"))
        make_json_dumper(open(structname + "_json.c", "w"))
        make_json_generator(open(structname + ".py", "w"))
    elif maketable:
        correct_table(sys.stdout)
    elif generate:
        make_driver_code(sys.stdout)
    elif makestruct:
        make_structure(sys.stdout)
    elif makedump:
        make_json_dumper(sys.stdout)
    elif readgen:
        make_json_generator(sys.stdout)
# end
  
