#!/usr/bin/env python
#
# Never hand-hack what you can generate...
#
# This code senerate template declarations for AIS-JSON parsing from a
# declarative specification of a JSON structure - and generate those
# declarative specification from the Python format strings for
# dumping the structures
#
import sys, getopt

# Make shared field names to data types 
typemap = {
    "raim":"boolean",
    "accuracy":"boolean",
    "timestamp":"string",
    }

ais_specs = (
    {
    "initname" : "json_ais4",
    "header": "\tAIS_HEADER,",
    "structname": "ais->type4",
    "fieldmap":(
        # fieldname   type        default
        ('timestamp', 'string',   None),
        ('accuracy',  'boolean',  "true"),
        ('lon',       'integer',  "AIS_LON_NOT_AVAILABLE"),
        ('lat',       'integer',  "AIS_LAT_NOT_AVAILABLE"),
        ('epfd',      'uinteger', "0"),
        ('raim',      'boolean',  "false"),
        ('radio',     'integer',  "0"),
        ),
    "stringbuffered":("timestamp",),
    },
)
    
def generate(spec):
    print "    const struct json_attr_t %s[] = {" % spec["initname"]
    if spec["header"]:
        print spec["header"]
    for (attr, itype, default) in spec["fieldmap"]:
        structname = spec["structname"]
        if itype == "string":
            deref = ""
        else:
            deref = "&"
        if attr in spec["stringbuffered"]:
            target = attr
        else:
            target = structname + "." + attr
        print '\t{"%s",%s%s,%s.addr.%s = %s%s,' % \
               (attr, " "*(12-len(attr)),itype, " "*(12-len(itype)), itype, deref, target)
        leader = " " * 37
        if itype == "string":
            print leader + ".maxlen = sizeof(%s)}," % target
        else:
            print leader + ".dflt.%s = %s}," % (itype, default)

    print """\
	{NULL},
    };

"""

def string_to_specifier(strspec):
    "Compile a Python-style format string to an attribute-type fieldmap."
    # Map C and Python-type format letters to JSON parser datatypes 
    fmtmap = {
        "d": "integer",
        "u": "uinteger",
        "f": "real",
        "a": "string",
        }
    strspec = strspec.strip()
    if strspec[-1] == "}":
        strspec = strspec[:-1]
    else:
        print "Missing terminating }"
        sys.exit(1)
    print '"fieldmap":('
    for item in strspec.split(","):
        if "timestamp" in item:
            (attr, itype) = ("timestamp", "string")
        else:
            itype = None
            (attr, fmt) = item.split(":")
            if attr[0] == '"':
                attr = attr[1:]
            if attr[-1] == '"':
                attr = attr[:-1]
            if attr in typemap:
                itype = typemap[attr]
            if fmt[-1] in fmtmap:
                itype = fmtmap[fmt[-1]]
        print "    " + `(attr, itype)` + ","
    print "    )"


# Edit into this global the string spec you need to convert
stringspec = \
"\"timestamp\":\"%4u:%02u:%02uT%02u:%02u:%02uZ\","\
     "\"accuracy\":%s,\"lon\":%d,\"lat\":%d,"\
     "\"epfd\":%u,\"raim\":%s,\"radio\":%d}\r\n"

if __name__ == '__main__':
    try:
        (options, arguments) = getopt.getopt(sys.argv[1:], "g")
    except getopt.GetoptError, msg:
        print "jsongen.py: " + str(msg)
        raise SystemExit, 1

    specify = False
    for (switch, val) in options:
        if (switch == '-g'):
            specify = True

    if specify:
        string_to_specifier(stringspec)
    else:
        for description in ais_specs:
            generate(description)
