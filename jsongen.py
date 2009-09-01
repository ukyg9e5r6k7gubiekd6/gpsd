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
    # AIS fields
    "raim":"boolean",
    "accuracy":"boolean",
    "timestamp":"string",
    }

# Map C and Python-type format letters to JSON parser datatypes 
fmtmap = {
    "d": "integer",
    "u": "uinteger",
    "f": "real",
    "a": "string",
    }


ais_specs = (
    ("json_ais4", "\tAIS_HEADER,", "ais->type4",
     "\"timestamp\":\"%4u:%02u:%02uT%02u:%02u:%02uZ\","\
     "\"accuracy\":%s,\"lon\":%d,\"lat\":%d,"\
     "\"epfd\":%u,\"raim\":%s,\"radio\":%d}\r\n"
     ),
    )
    
def generate(initname, header, structname, specifier):
    specifier = specifier.strip()
    if specifier[-1] == "}":
        specifier = specifier[:-1]
    else:
        print "Missing terminating }"
        sys.exit(1)

    print "    const struct json_attr_t %s[] = {" % initname
    if header:
        print header
    for item in specifier.split(","):
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
        if itype == "string":
            deref = ""
        else:
            deref = "&"
        print '\t{"%s",%s%s,%s.addr.%s = %s%s.%s,' % \
               (attr, " "*(12-len(attr)),itype, " "*(12-len(itype)), itype, deref, structname, attr)
        leader = " " * 37
        if itype == "string":
            print leader + ".maxlen = sizeof(%s.%s)}," % (structname, attr)
        elif itype == "boolean":
            print leader + ".dflt.boolean = false},"
        elif itype == "real":
            print leader + ".dflt.real = 0.0},"
        else:
            print leader + ".dflt.%s = 0}," % itype

    print """\
	{NULL},
    };

"""

if __name__ == '__main__':
    for description in ais_specs:
        generate(initname=description[0],
                 header=description[1],
                 structname=description[2],
                 specifier=description[3])
