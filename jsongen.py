#!/usr/bin/env python
#
# Never hand-hack what you can generate...
#
# This code generates template declarations for AIS-JSON parsing from a
# declarative specification of a JSON structure - and generate those
# declarative specification from the Python format strings for
# dumping the structures
#
import sys, getopt

#
# Here is the information that makes it all work - attribute, type, and
# defult information for all fields.  We can generate either a JSON
# parser template spec or the C code for the correspondong dump functio
# from this information. Doing it this way guarantees consistency.
#
ais_specs = (
    {
    "initname" : "json_ais1",
    "header": "\tAIS_HEADER,",
    "structname": "ais->type123",
    "fieldmap":(
        # fieldname   type        default
        ('status',   'uinteger', '0'),
        ('turn',     'integer',  'AIS_TURN_NOT_AVAILABLE'),
        ('speed',    'uinteger', 'SPEED_NOT_AVAILABLE'),
        ('accuracy', 'boolean',  'false'),
        ('lon',      'integer',  'AIS_LON_NOT_AVAILABLE'),
        ('lat',      'integer',  'AIS_LAT_NOT_AVAILABLE'),
        ('course',   'uinteger', 'AIS_COURSE_NOT_AVAILABLE'),
        ('heading',  'integer',  'AIS_HEADING_NOT_AVAILABLE'),
        ('second',   'uinteger', 'AIS_SEC_NOT_AVAILABLE'),
        ('maneuver', 'integer',  'AIS_SEC_INOPERATIVE'),
        ('raim',     'boolean',  'false'),
        ('radio',    'integer',  '0'),
        ),
    },
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
    {
    "initname" : "json_ais5",
    "header": "\tAIS_HEADER,",
    "structname": "ais->type5",
    "fieldmap":(
        # fieldname   type        default
        ('imo',           'uinteger',      '0'),
        ('ais_version',   'uinteger',      '0'),
        ('callsign',      'string',        None),
        ('shipname',      'string',        None),
        ('shiptype',      'string',        None),
        ('to_bow',        'uinteger',      '0'),
        ('to_stern',      'uinteger',      '0'),
        ('to_port',       'uinteger',      '0'),
        ('to_starboard',  'uinteger',      '0'),
        ('epfd',          'string',        None),
        ('eta',           'string',        None),
        ('draught',       'real',          '0.0'),
        ('destination',   'string',        None),
        ('dte',           'uinteger',      '1'),
        ),
    "stringbuffered":("eta",),
    },
)

# Map shared field names to data types 
overrides = {
    "raim":"boolean",
    "accuracy":"boolean",
    }

# Give this global the string spec you need to convert with -g
# We do it this mildly odd way only becaue passing Python multiline
# string literals on the command line is inconvenient.
stringspec = \
           "\"seqno\":%u,\"dest_mmsi\":%u,"\
           "\"retransmit\":%u,\"application_id\":%u,"\
           "\"data\":\"%u:%s\"}\r\n"

# You should not need to modify anything below this liine.

def generate(spec):
    for (attr, itype, default) in spec["fieldmap"]:
        if attr in spec.get("stringbuffered", []):
            print "    char %s[JSON_VAL_MAX+1];" % attr
    print "    const struct json_attr_t %s[] = {" % spec["initname"]
    if "header" in spec:
        print spec["header"]
    for (attr, itype, default) in spec["fieldmap"]:
        structname = spec["structname"]
        if itype == "string":
            deref = ""
        else:
            deref = "&"
        if attr in spec.get("stringbuffered", []):
            target = attr
        else:
            target = structname + "." + attr
        print '\t{"%s",%s%s,%s.addr.%s = %s%s,' % \
               (attr, " "*(12-len(attr)), itype, " "*(10-len(itype)), itype, deref, target)
        leader = " " * 35
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
        "\"": "string",
        }
    dftmap = {
        "integer":  "0",
        "uinteger": "0",
        "real":     "0.0",
        "string":   None,
        "boolean":  "false"
        }
    strspec = strspec.strip()
    if strspec[-1] == "}":
        strspec = strspec[:-1]
    else:
        print "Missing terminating }"
        sys.exit(1)
    print '    "fieldmap":('
    for item in strspec.split(","):
        itype = fmtmap[item[-1]]
        attr = item[:item.find(":")]
        if attr[0] == '"':
            attr = attr[1:]
        if attr[-1] == '"':
            attr = attr[:-1]
        if attr in overrides:
            itype = overrides[attr]
        dflt = dftmap[itype]
        if dflt is not None:
            dflt = `dflt`
        print "        ('%s',%s'%s',%s%s)," % (attr, " "*(14-len(attr)), itype, " "*(14-len(itype)), dflt)
    print "        )"


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
