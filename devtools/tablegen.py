#!/usr/bin/env python
#
# This tool is intended to automate away the drudgery in bring up support
# for a new AIS message type.  It parses the tabular description of a message
# and generates various useful code snippets from that.  It can also be used to
# correct offsets in the tables themselves.
#
# Requires the AIVDM.txt file on standard input. Takes a single argument,
# which must match a string in a //: Type comment.  Things you can generate:
#
# * -t: A corrected version of the table.  It will redo all the offsets to be
#   in conformance with the bit widths. (The other options rely only on the
#   bit widths). If the old and new tables are different, an error message
#   describing the corrections will be emitted to standard error.
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
# * -a: Generate all of -s, -d, -c, and -r, and -t, not to stdout but to
#   files named with 'tablegen' as a distinguishing part of the stem.
#   The stem name can be overridden with the -o option.
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
# This code interprets magic comments in the input
#
# //: Type
#    The token following "Type" is the name of the table
# //: xxxx vocabulary
#    A subtable describing a controlled vocabulary for field xxxx in the
#    preceding table.
#
# TO-DO: generate code for ais.py.

import sys, getopt

def correct_table(wfp):
    # Writes the corrected table.
    print >>sys.stderr, "Total bits:", base
    for (i, t) in enumerate(table):
        if offsets[i].strip():
            print >>wfp, "|" + offsets[i] + t[owidth+1:].rstrip()
        else:
            print >>wfp, t.rstrip()

def make_driver_code(wfp):
    # Writes calls to bit-extraction macros.
    # Requires UBITS, SBITS, UCHARS to act as they do in the AIVDM driver.
    # Also relies on bitlen to be the message bit length, and i to be
    # available as abn index variable.
    record = after is None
    arrayname = None
    base = '\t'
    step = " " * 4
    indent = base
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
            if ftype[0] == 'a':
                arrayname = name
                explicit = ftype[1] == '^'
                print >>wfp, '#define ARRAY_BASE %s' % offsets[i].strip()
                print >>wfp, '#define ELEMENT_SIZE %s' % trailing
                if explicit:
                    lengthfield = last
                    print >>wfp, indent + "for (i = 0; i < %s; i++) {" % lengthfield 
                else:
                    lengthfield = "n" + arrayname
                    print >>wfp, indent + "for (i = 0; ARRAY_BASE + (ELEMENT_SIZE*i) < bitlen; i++) {" 
                indent += step
                print >>wfp, indent + "int a = ARRAY_BASE + (ELEMENT_SIZE*i);" 
                continue
            offset = offsets[i].split('-')[0]
            if arrayname:
                target = "%s.%s[i].%s" % (structnme, arrayname, name)
                offset = "a + " + offset 
            else:
                target = "%s.%s" % (structname, name)
            if ftype[0].lower() in ('u', 'i', 'e'):
                print >>wfp, indent + "%s\t= %sBITS(%s, %s);" % \
                      (target, {'u':'U', 'e':'U', 'i':'S'}[ftype[0].lower()], offset, width)
            elif ftype == 't':
                print >>wfp, indent + "UCHARS(%s, %s);" % (offset, target)
            elif ftype == 'b':
                print >>wfp, indent + "%s\t= (bool)UBITS(%s, 1);" % (target, offset)
            else:
                print >>wfp, indent + "/* %s bits of type %s */" % (width,ftype)
            last = name
    if arrayname:
        indent = base
        print >>wfp, indent + "}"
        if not explicit:
            print >>wfp, indent + "%s.%s = ind;" % (structname, lengthfield)
        print >>wfp, "#undef ARRAY_BASE" 
        print >>wfp, "#undef ELEMENT_SIZE" 

def make_structure(wfp):
    # Write a structure definition correponding to the table.
    global structname
    record = after is None
    baseindent = 8
    step = 4
    inwards = step
    arrayname = None
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
            if ftype[0] == 'a':
                arrayname = name
                if ftype[1] == '^':
                    lengthfield = last
                    ftype = ftype[1:]
                else:
                    lengthfield = "n%s" % arrayname
                    print >>wfp, tabify(baseindent + inwards) + "signed int %s;" % lengthfield
                if arrayname.endswith("s"):
                    typename = arrayname[:-1]
                else:
                    typename = arrayname
                print >>wfp, tabify(baseindent + inwards) + "struct %s_t {" % typename
                inwards += step
                arraydim = ftype[1:]
                continue
            elif ftype == 'u' or ftype == 'e' or ftype[0] == 'U':
                decl = "unsigned int %s;\t/* %s */" % (name, description)
            elif ftype == 'i' or ftype[0] == 'I':
                decl = "signed int %s;\t/* %s */" % (name, description)
            elif ftype == 'b':
                decl = "bool %s;\t/* %s */" % (name, description)
            elif ftype == 't':
                stl = int(width)/6
                decl = "char %s[%d+1];\t/* %s */" % (name, stl, description)
            else:
                decl = "/* %s bits of type %s */" % (width, ftype)
            print >>wfp, tabify(baseindent + inwards) + decl
        last = name
    if arrayname:
        inwards -= step
        print >>wfp, tabify(baseindent + inwards) + "} %s[%s];" % (arrayname, arraydim)
    if "->" in structname:
        typename = structname.split("->")[1]
    if "." in typename:
        structname = structname.split(".")[1]    
    print >>wfp, tabify(baseindent) + "} %s;" % typename

def make_json_dumper(wfp):
    # Write the skeleton of a JSON dump corresponding to the table.
    # Also, if there are subtables, some initializers
    if subtables:
        for (name, lines) in subtables:
            wfp.write("    const char *%s_vocabulary[] = {\n" % name)
            for line in lines:
                value = line[1]
                if value.endswith(" (default)"):
                    value = value[:-10]
                wfp.write('        "%s",\n' % value)
            wfp.write("    };\n")
            wfp.write('#define DISPLAY_%s(n) (((n) < (unsigned int)NITEMS(%s_vocabulary)) ? %s_vocabulary[n] : "INVALID %s")\n' % (name.upper(), name, name, name.upper()))
        wfp.write("\n")
    record = after is None
    # Elements of each tuple type except 'a':
    #   1. variable name,
    #   2. unscaled printf format
    #   3. wrapper for unscaled variable reference 
    #   4. scaled printf format
    #   5. wrapper for scaled variable reference
    # Elements of 'a' tuple:
    #   1. Name of array field
    #   2. None
    #   3. None
    #   4. None
    #   5. Name of length field
    tuples = []
    vocabularies = [x[0] for x in subtables]
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
            fmt_text = r'\"%s_text\":' % name
            if ftype == 'u':
                tuples.append((name,
                               fmt+"%u", "%s",
                               None, None))
            elif ftype == 'e':
                tuples.append((name,
                               fmt+"%u", "%s",
                               None, None))
                if vocabularies:
                    this = vocabularies.pop(0)
                    ref = "DISPLAY_%s(%%s)" % (this.upper())
                else:
                    ref = 'FOO[%s]'
                tuples.append((name,
                               fmt_text+r"\"%s\"", ref,
                               None, None))
            elif ftype == 'i':
                tuples.append((name,
                               fmt+"%d", "%s",
                               None, None))
            elif ftype == 't':
                tuples.append((name,
                               fmt+r'\"%s\"', "%s",
                               None, None))
            elif ftype == 'b':
                tuples.append((name,
                               fmt+r'\"%s\"', "JSON_BOOL(%s)",
                               None, None))
            elif ftype[0] == 'd':
                print >>sys.stderr, "Cannot generate code for data members"
                sys.exit(1)
            elif ftype[0] == 'U':
                tuples.append((name,
                               fmt+"%u", "%s",
                               fmt+"%%.%sf" % ftype[1], '%s / SCALE'))
            elif ftype[0] == 'I':
                tuples.append((name,
                               fmt+"%d", "%s",
                               fmt+"%%.%sf" % ftype[1], '%s / SCALE'))
            elif ftype[0] == 'a':
                ftype = ftype[1:]
                if ftype[0] == '^':
                    lengthfield = last
                else:
                    lengthfield = "n" + name
                tuples.append((name, None, None, None, lengthfield))
            else:
                print >>sys.stderr, "Unknown type code", ftype
                sys.exit(1)
        last = name
    startspan = 0
    def scaled(i):
        return tuples[i][3] is not None
    def tslice(e, i):
        return map(lambda x: x[i], tuples[startspan:e+1])
    base = " " * 8
    step = " " * 4
    inarray = None
    header = "(void)snprintf(buf + strlen(buf), buflen - strlen(buf),"
    for (i, (var, uf, uv, sf, sv)) in enumerate(tuples):
        if uf == None:
            print >>wfp, base + "for (i = 0; i < %s.%s; i++) {" % (structname, sv)
            inarray = var
            base = " " * 12
            startspan = i+1
            continue        
        # At end of tuples, or if scaled flag changes, or if next op is array,
        # flush out dump code for a span of fields.
        if i+1 == len(tuples):
            endit = '}",'
        elif tuples[i+1][1] == None:
            endit = r',\"%s\":[",' % tuples[i+1][0]
        elif scaled(i) != scaled(i+1):
            endit =  ',",'
        else:
            endit = None
        if endit:
            if not scaled(i):
                print >>wfp, base + header
                if inarray:
                    prefix = '{"'
                else:
                    prefix = '"'
                print >>wfp, base + step + prefix +','.join(tslice(i,1)) + endit
                for (j, t) in enumerate(tuples[startspan:i+1]):
                    if inarray:
                        ref = structname + "." + inarray + "[i]." + t[0]
                    else:
                        ref = structname + "." + t[0]
                    wfp.write(base + step + t[2] % ref)
                    if j == i - startspan:
                        wfp.write(");\n")
                    else:
                        wfp.write(",\n")
            else:
                print >>wfp, base + "if (scaled)"
                print >>wfp, base + step + header
                print >>wfp, base + step*2 + '"'+','.join(tslice(i,3)) + endit
                for (j, t) in enumerate(tuples[startspan:i+1]):
                    if inarray:
                        ref = structname + "." + inarray + "[i]." + t[0]
                    else:
                        ref = structname + "." + t[0]
                    wfp.write(base + step*2 + t[4] % ref)
                    if j == i - startspan:
                        wfp.write(");\n")
                    else:
                        wfp.write(",\n")
                print >>wfp, base + "else"
                print >>wfp, base + step + header
                print >>wfp, base + step*2 + '"'+','.join(tslice(i,1)) + endit
                for (j, t) in enumerate(tuples[startspan:i+1]):
                    if inarray:
                        ref = structname + "." + inarray + "[i]." + t[0]
                    else:
                        ref = structname + "." + t[0]
                    wfp.write(base + step*2 + t[2] % ref)
                    if j == i - startspan:
                        wfp.write(");\n")
                    else:
                        wfp.write(",\n")
            startspan = i+1
    # If we were looking at a trailing array, close scope 
    if inarray:
        base = " " * 8
        print >>wfp, base + "}"
        print >>wfp, base + "if (buf[strlen(buf)-1] == ',')"
        print >>wfp, base + step + r"buf[strlen(buf)-1] = '\0';"
        print >>wfp, base + "(void)strlcat(buf, \"]}\", buflen - strlen(buf));"

def make_json_generator(wfp):
    # Write a stanza for jsongen.py.in describing how to generate a
    # JSON parser initializer from this table. You need to fill in
    # __INITIALIZER__ and default values after this is generated.
    extra = ""
    arrayname = None
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
            if ftype[0] == 'a':
                arrayname = name
                if arrayname.endswith("s"):
                    typename = arrayname[:-1]
                else:
                    typename = arrayname
                readtype = 'array'
                dimension = ftype[1:]
                if dimension[0] == '^':
                    lengthfield = last
                    dimension = dimension[1:]
                else:
                    lengthfield = "n" + arrayname
                extra = " " * 8
                print >>wfp, "        ('%s',%s 'array', (" % \
                      (arrayname, " "*(10-len(arrayname)))
                print >>wfp, "            ('%s_t', '%s', (" % (typename, lengthfield)
            else:
                # Depends on the assumption that the read code
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
                typedefault = {
                    'u': "'PUT_DEFAULT_HERE'",
                    'U': "'PUT_DEFAULT_HERE'",
                    'e': "'PUT DEFAULT HERE'",
                    'i': "'PUT_DEFAULT_HERE'",
                    'I': "'PUT_DEFAULT_HERE'",
                    'b': "\'false\'",
                    't': "None",
                    }[ftype[0]]
                namedefaults = {
                    "month": "'0'",
                    "day": "'0'",
                    "hour": "'24'",
                    "minute": "'60'",
                    "second": "'60'",
                    }
                default = namedefaults.get(name) or typedefault
                print >>wfp, extra + "        ('%s',%s '%s',%s %s)," % (name,
                                                     " "*(10-len(name)),
                                                     readtype,
                                                     " "*(8-len(readtype)),
                                                     default)
                if ftype[0] == 'e':
                    print >>wfp, extra + "        ('%s_text',%s'ignore',   None)," % \
                          (name, " "*(6-len(name)))

            last = name
    if arrayname:
        print >>wfp, "                    )))),"
    print >>wfp, "        ),"
    print >>wfp, "    },"

if __name__ == '__main__':
    try:
        (options, arguments) = getopt.getopt(sys.argv[1:], "a:tc:s:d:S:E:r:o:")
    except getopt.GetoptError, msg:
        print "tablecheck.py: " + str(msg)
        raise SystemExit, 1
    generate = maketable = makestruct = makedump = readgen = all = False
    after = before = None
    filestem = "tablegen"
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
        elif switch == '-o':
            filestem = val

    if not generate and not maketable and not makestruct and not makedump and not readgen and not all:
        print >>sys.stderr, "tablecheck.py: no mode selected"
        sys.exit(1)

    # First, read in the table.
    # Sets the following:
    #    table - the table lines
    #    widths - array of table widths
    #    ranges - array of table offsets
    #    trailing - bit length of the table or trailing array element
    #    subtables - list of following vocabulary tables.
    tablename = arguments[0]
    table = []
    ranges = []
    subtables = []
    state = 0
    for line in sys.stdin:
        if state == 0 and line.startswith("//: Type") and tablename in line:
            state = 1
            continue
        elif state == 1:		# Found table tag
            if line.startswith("|="):
                state = 2
                continue
        elif state == 2:		# Found table header
            if line.startswith("|="):
                state = 3
                continue
            elif line[0] == '|':
                fields = line.split("|")
                trailing = fields[1]
                ranges.append(fields[1].strip())
                fields[1] = " " * len(fields[1])
                line = "|".join(fields)
            else:
                ranges.append('')
            table.append(line)
            continue
        elif state == 3:		# Found table end
            state = 4
            continue
        elif state == 4:		# Skipping until subsidiary table
            if line.startswith("//:") and "vocabulary" in line:
                subtable_name = line.split()[1]
                subtable_content = []
                state = 5
        elif state == 5:		# Seen subtable header
            if line.startswith("|="):
                state = 6
                continue
        elif state == 6:		# Parsing subtable content
            if line.startswith("|="):
                subtables.append((subtable_name, subtable_content))
                state = 4
                continue
            elif line[0] == '|':
                subtable_content.append([f.strip() for f in line[1:].strip().split("|")])
            continue
    if state == 0:
        print >>sys.stderr, "Can't find named table."
        sys.exit(1)        
    elif state < 3:
        print >>sys.stderr, "Ill-formed table (in state %d)." % state
        sys.exit(1)
    table = table[1:]
    ranges = ranges[1:]
    widths = []
    for line in table:
        fields = line.split('|')
        if '|' not in line:        # Continuation line
            widths.append('')
        elif fields[5][0] == 'a':     # Array boundary indicator
            widths.append(None)
        else:
            widths.append(fields[2].strip())
    if '-' in trailing:
        trailing = trailing.split('-')[1]
    trailing = str(int(trailing)+1)

    # Compute offsets for an AIVDM message breakdown, given the bit widths.
    offsets = []
    base = 0
    corrections = False
    for w in widths:
        if w is None:
            offsets.append(`base`)
            base = 0
        elif w == '':
            offsets.append('')
        else:
            w = int(w)
            offsets.append("%d-%d" % (base, base + w - 1))
            base += w
    if filter(lambda p: p[0] != p[1], zip(ranges, offsets)):
        corrections = True
        print "Offset corrections:"
        for (old, new) in zip(ranges, offsets):
            if old != new:
                print >>sys.stderr, old, "->", new 
    owidth = max(*map(len, offsets)) 
    for (i, off) in enumerate(offsets):
        offsets[i] += " " * (owidth - len(offsets[i]))

    # Here's where we generate useful output.
    if all:
        if corrections:
            correct_table(open(filestem + ".txt", "w"))
        make_driver_code(open(filestem + ".c", "w"))
        make_structure(open(filestem + ".h", "w"))
        make_json_dumper(open(filestem + "_json.c", "w"))
        make_json_generator(open(filestem + ".py", "w"))
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
  
