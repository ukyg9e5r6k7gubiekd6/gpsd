#!/usr/bin/env python
#
# A Python AIVDM/AIVDO decoder
#
# This file is Copyright (c) 2010 by the GPSD project
# BSD terms apply: see the file COPYING in the distribution root for details.
#
# This decoder works by defining a declarative pseudolanguage in which
# to describe the process of extracting packed bitfields from an AIS
# message, a set of tables which contain instructions in the pseudolanguage,
# and a small amount of code for interpreting it.
#
# Known bugs:
# * Doesn't join parts A and B of Type 24 together yet.
# * Only handles the broadcast case of type 22.  The problem is that the
#   addressed field is located *after* the variant parts. Grrrr... 
# * Message type 26 is presently unsupported. It hasn't been observed
#   in the wild yet as of Jan 2010; not a lot of point in trying util
#   we have test data.
#
# Decoding for 1-15, 18-21, and 24 have been tested against live data.
# Decoding for 16-17, 22-23, and 25-26 have not.

# Here are the pseudoinstructions in the pseudolanguage.

class bitfield:
    "Object defining the interpretation of an AIS bitfield."
    # The only un-obvious detail is the use of the oob (out-of-band)
    # member.  This isn't used in data extraction, but rather to cut
    # down on the number of custom formatting hooks.  With this we
    # handle the case where the field should be reported as an integer
    # or "n/a".
    def __init__(self, name, width, dtype, oob, legend,
                 validator=None, formatter=None, conditional=None):
        self.name = name		# Fieldname, for internal use and JSON
        self.width = width		# Bit width
        self.type = dtype		# Data type: signed/unsigned/string/raw
        self.oob = oob			# Out-of-band value to be shown as n/a
        self.legend = legend		# Human-friendly description of field
        self.validator = validator	# Validation checker
        self.formatter = formatter	# Custom reporting hook.
        self.conditional = conditional	# Evaluation guard for this field

class spare:
    "Describes spare bits,, not to be interpreted."
    def __init__(self, width, conditional=None):
        self.width = width
        self.conditional = conditional	# Evaluation guard for this field

class dispatch:
    "Describes how to dispatch to a message type variant on a subfield value."
    def __init__(self, fieldname, subtypes, compute=lambda x: x, conditional=None):
        self.fieldname = fieldname	# Value of view to dispatch on
        self.subtypes = subtypes	# Possible subtypes to dispatch to
        self.compute = compute		# Pass value through this pre-dispatch
        self.conditional = conditional	# Evaluation guard for this field

# Message-type-specific information begins here. There are four
# different kinds of things in it: (1) string tables for expanding
# enumerated-type codes, (2) hook functions, (3) instruction tables,
# and (4) field group declarations.  This is the part that could, in
# theory, be generated from a portable higher-level specification in
# XML; only the hook functions are actually language-specific, and
# your XML definition could in theory embed several different ones for
# code generation in Python, Java, Perl, etc.

cnb_status_legends = (
	"Under way using engine",
	"At anchor",
	"Not under command",
	"Restricted manoeuverability",
	"Constrained by her draught",
	"Moored",
	"Aground",
	"Engaged in fishing",
	"Under way sailing",
	"Reserved for HSC",
	"Reserved for WIG",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"Not defined",
    )

def cnb_rot_format(n):
    if n == -128:
        return "n/a"
    elif n == -127:
        return "fastleft"
    elif n == 127:
        return "fastright"
    else:
        return str(n * n / 4.733);

def cnb_latlon_format(n):
    return str(n / 600000.0)

def cnb_speed_format(n):
    if n == 1023:
        return "n/a"
    elif n == 1022:
        return "fast"
    else:
        return str(n / 10.0);

def cnb_course_format(n):
    return str(n / 10.0);

def cnb_second_format(n):
    if n == 60:
        return "n/a"
    elif n == 61:
        return "manual input"
    elif n == 62:
        return "dead reckoning"
    elif n == 63:
        return "inoperative"
    else:
        return str(n);

# Common Navigation Block is the format for AIS types 1, 2, and 3
cnb = (
    bitfield("status",   4, 'unsigned', 0,         "Navigation Status",
             formatter=cnb_status_legends),
    bitfield("turn",     8, 'signed',   -128,      "Rate of Turn",
             formatter=cnb_rot_format),       
    bitfield("speed",   10, 'unsigned', 1023,      "Speed Over Ground",
             formatter=cnb_speed_format),
    bitfield("accuracy", 1, 'unsigned', None,      "Position Accuracy"),
    bitfield("lon",     28, 'signed',   0x6791AC0, "Longitude",
             formatter=cnb_latlon_format),
    bitfield("lat",     27, 'signed',   0x3412140,  "Latitude",
             formatter=cnb_latlon_format),
    bitfield("course",  12, 'unsigned',	0xe10,      "Course Over Ground",
             formatter=cnb_course_format),
    bitfield("heading",  9, 'unsigned', 511,        "True Heading"),
    bitfield("second",   6, 'unsigned', None,       "Time Stamp",
             formatter=cnb_second_format),
    bitfield("maneuver", 2, 'unsigned', None,       "Maneuver Indicator"),
    spare(3),  
    bitfield("raim",     1, 'unsigned', None,       "RAIM flag"),
    bitfield("radio",   19, 'unsigned', None,       "Radio status"),
)

epfd_type_legends = (
	"Undefined",
	"GPS",
	"GLONASS",
	"Combined GPS/GLONASS",
	"Loran-C",
	"Chayka",
	"Integrated navigation system",
	"Surveyed",
	"Galileo",
    )

type4 = (
    bitfield("year",    14,  "unsigned", 0,         "Year"),
    bitfield("month",    4,  "unsigned", 0,         "Month"),
    bitfield("day",      5,  "unsigned", 0,         "Day"),
    bitfield("hour",     5,  "unsigned", 24,        "Hour"),
    bitfield("minute",   6,  "unsigned", 60,        "Minute"),
    bitfield("second",   6,  "unsigned", 60,        "Second"),
    bitfield("accuracy", 1,  "unsigned", None,      "Fix quality"),
    bitfield("lon",     28,  "signed",   0x6791AC0, "Longitude",
             formatter=cnb_latlon_format),
    bitfield("lat",     27,  "signed",   0x3412140, "Latitude",
             formatter=cnb_latlon_format),
    bitfield("epfd",     4,  "unsigned", None,      "Type of EPFD",
             validator=lambda n: n >= 0 and n <= 8 or n == 15,
             formatter=epfd_type_legends),
    spare(10),
    bitfield("raim",     1,  "unsigned", None,      "RAIM flag "),
    bitfield("radio",   19,  "unsigned", None,      "SOTDMA state"),
    )

ship_type_legends = (
	"Not available",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
	"Wing in ground (WIG) - all ships of this type",
	"Wing in ground (WIG) - Hazardous category A",
	"Wing in ground (WIG) - Hazardous category B",
	"Wing in ground (WIG) - Hazardous category C",
	"Wing in ground (WIG) - Hazardous category D",
	"Wing in ground (WIG) - Reserved for future use",
	"Wing in ground (WIG) - Reserved for future use",
	"Wing in ground (WIG) - Reserved for future use",
	"Wing in ground (WIG) - Reserved for future use",
	"Wing in ground (WIG) - Reserved for future use",
	"Fishing",
	"Towing",
	"Towing: length exceeds 200m or breadth exceeds 25m",
	"Dredging or underwater ops",
	"Diving ops",
	"Military ops",
	"Sailing",
	"Pleasure Craft",
	"Reserved",
	"Reserved",
	"High speed craft (HSC) - all ships of this type",
	"High speed craft (HSC) - Hazardous category A",
	"High speed craft (HSC) - Hazardous category B",
	"High speed craft (HSC) - Hazardous category C",
	"High speed craft (HSC) - Hazardous category D",
	"High speed craft (HSC) - Reserved for future use",
	"High speed craft (HSC) - Reserved for future use",
	"High speed craft (HSC) - Reserved for future use",
	"High speed craft (HSC) - Reserved for future use",
	"High speed craft (HSC) - No additional information",
	"Pilot Vessel",
	"Search and Rescue vessel",
	"Tug",
	"Port Tender",
	"Anti-pollution equipment",
	"Law Enforcement",
	"Spare - Local Vessel",
	"Spare - Local Vessel",
	"Medical Transport",
	"Ship according to RR Resolution No. 18",
	"Passenger - all ships of this type",
	"Passenger - Hazardous category A",
	"Passenger - Hazardous category B",
	"Passenger - Hazardous category C",
	"Passenger - Hazardous category D",
	"Passenger - Reserved for future use",
	"Passenger - Reserved for future use",
	"Passenger - Reserved for future use",
	"Passenger - Reserved for future use",
	"Passenger - No additional information",
	"Cargo - all ships of this type",
	"Cargo - Hazardous category A",
	"Cargo - Hazardous category B",
	"Cargo - Hazardous category C",
	"Cargo - Hazardous category D",
	"Cargo - Reserved for future use",
	"Cargo - Reserved for future use",
	"Cargo - Reserved for future use",
	"Cargo - Reserved for future use",
	"Cargo - No additional information",
	"Tanker - all ships of this type",
	"Tanker - Hazardous category A",
	"Tanker - Hazardous category B",
	"Tanker - Hazardous category C",
	"Tanker - Hazardous category D",
	"Tanker - Reserved for future use",
	"Tanker - Reserved for future use",
	"Tanker - Reserved for future use",
	"Tanker - Reserved for future use",
	"Tanker - No additional information",
	"Other Type - all ships of this type",
	"Other Type - Hazardous category A",
	"Other Type - Hazardous category B",
	"Other Type - Hazardous category C",
	"Other Type - Hazardous category D",
	"Other Type - Reserved for future use",
	"Other Type - Reserved for future use",
	"Other Type - Reserved for future use",
	"Other Type - Reserved for future use",
	"Other Type - no additional information",
)

type5 = (
    bitfield("ais_version",   2, 'unsigned', None, "AIS Version"),
    bitfield("imo_id",       30, 'unsigned',    0, "IMO Identification Number"),
    bitfield("callsign",     42, 'string',   None, "Call Sign"),              
    bitfield("shipname",    120, 'string',   None, "Vessel Name"),
    bitfield("shiptype",      8, 'unsigned', None, "Ship Type",
             #validator=lambda n: n >= 0 and n <= 99,
             formatter=ship_type_legends),
    bitfield("to_bow",        9, 'unsigned',    0, "Dimension to Bow"),
    bitfield("to_stern",      9, 'unsigned',    0, "Dimension to Stern"),
    bitfield("to_port",       6, 'unsigned',    0, "Dimension to Port"),
    bitfield("to_starbord",   6, 'unsigned',    0, "Dimension to Starboard"),
    bitfield("epfd",          4, 'unsigned',    0, "Position Fix Type",
             validator=lambda n: n >= 0 and n <= 8 or n == 15,
             formatter=epfd_type_legends),
    bitfield("month",         4, 'unsigned',    0, "ETA month"),
    bitfield("day",           5, 'unsigned',    0, "ETA day"),
    bitfield("hour",          5, 'unsigned',   24, "ETA hour"),
    bitfield("minute",        6, 'unsigned',   60, "ETA minute"),
    bitfield("draught",       8, 'unsigned',    0, "Draught",
             formatter=lambda n: n/10.0),
    bitfield("destination", 120, 'string',   None, "Destination"),
    bitfield("dte",           1, 'unsigned', None, "DTE"),
    spare(1),
    )

type6 = (
    bitfield("seqno",            2, 'unsigned', None, "Sequence Number"),
    bitfield("dest_mmsi",       30, 'unsigned', None, "Destination MMSI"),
    bitfield("retransmit",       1, 'unsigned', None, "Retransmit flag"),
    spare(1),
    bitfield("dac",             10, 'unsigned', 0,    "DAC"),
    bitfield("fid",              6, 'unsigned', 0,    "Functional ID"),
    bitfield("data",           920, 'raw',      None, "Data"),
    )

type7 = (
    spare(2),
    bitfield("mmsi1",           30, 'unsigned', 0,    "MMSI number 1"),
    spare(2),
    bitfield("mmsi2",           30, 'unsigned', 0,    "MMSI number 2"),
    spare(2),
    bitfield("mmsi3",           30, 'unsigned', 0,    "MMSI number 3"),
    spare(2),
    bitfield("mmsi1",           30, 'unsigned', 0,    "MMSI number 4"),
    spare(2),
    )

type8 = (
    spare(2),
    bitfield("dac",            10,  'unsigned', 0,     "DAC"),
    bitfield("fid",            6,   'unsigned', 0,     "Functional ID"),
    bitfield("data",           952, 'raw',      None,  "Data"),
    )

def type9_alt_format(n):
    if n == 4094:
        return ">=4094"
    else:
        return str(n)

def type9_speed_format(n):
    if n == 1023:
        return "n/a"
    elif n == 1022:
        return "fast"
    else:
        return str(n);

type9 = (
    bitfield("alt",         12, 'unsigned', 4095,      "Altitude",
             formatter=type9_alt_format),
    bitfield("speed",       10, 'unsigned', 1023,      "SOG",
             formatter=type9_speed_format),
    bitfield("accuracy",    1,  'unsigned', None,      "Position Accuracy"),
    bitfield("lon",         28, 'signed',   0x6791AC0, "Longitude",
             formatter=cnb_latlon_format),
    bitfield("lat",         27, 'signed',   0x3412140, "Latitude",
             formatter=cnb_latlon_format),
    bitfield("course",      12, 'unsigned', 0xe10,     "Course Over Ground",
             formatter=cnb_course_format),
    bitfield("second",      6,  'unsigned', 60,        "Time Stamp",
             formatter=cnb_second_format),
    bitfield("regional",    8,  'unsigned', None,      "Regional reserved"),
    bitfield("dte",         1,  'unsigned', None,      "DTE"),
    spare(3),
    bitfield("assigned",    1,  'unsigned', None,      "Assigned"),
    bitfield("raim",        1,  'unsigned', None,      "RAIM flag"),
    bitfield("radio",       19, 'unsigned', None,      "Radio status"),
    )

type10 = (
    spare(2),
    bitfield("dest_mmsi",       30, 'unsigned', None, "Destination MMSI"), 
    spare(2),
   )

type12 = (
    bitfield("seqno",            2, 'unsigned', None, "Sequence Number"),
    bitfield("dest_mmsi",       30, 'unsigned', None, "Destination MMSI"),
    bitfield("retransmit",       1, 'unsigned', None, "Retransmit flag"),
    spare(1),
    bitfield("text",           936, 'string',   None, "Text"),
    )

type14 = (
    spare(2),
    bitfield("text",           968, 'string',   None, "Text"),
    )

type15 = (
    spare(2),
    bitfield("mmsi1",     30, 'unsigned', 0, "First interrogated MMSI"),
    bitfield("type1_1",   6,  'unsigned', 0, "First message type"),
    bitfield("offset1_1", 12, 'unsigned', 0, "First slot offset"),
    spare(2),
    bitfield("type1_2",   6,  'unsigned', 0, "Second message type"),
    bitfield("offset1_2", 12, 'unsigned', 0, "Second slot offset"),
    spare(2),
    bitfield("mmsi2",     30, 'unsigned', 0, "Second interrogated MMSI"),
    bitfield("type2_1",   6,  'unsigned', 0, "Message type"),
    bitfield("offset2_1", 12, 'unsifned', 0, "Slot offset"),
    spare(2),
    )

type16 = (
    spare(2),
    bitfield("mmsi1",     30, 'unsigned', 0, "Interrogated MMSI 1"),
    bitfield("offset1",   12, 'unsigned', 0, "First slot offset"),
    bitfield("increment1",10, 'unsigned', 0, "First slot increment"),
    bitfield("mmsi2",     30, 'unsigned', 0, "Interrogated MMSI 2"),
    bitfield("offset2",   12, 'unsigned', 0, "Second slot offset"),
    bitfield("increment2",10, 'unsigned', 0, "Second slot increment"),
    spare(2),
    )

def short_latlon_format(n):
    return str(n / 600.0)

type17 = (
    spare(2),
    bitfield("lon",         18, 'signed',   0x1a838, "Longitude",
             formatter=short_latlon_format),
    bitfield("lat",         17, 'signed',   0xd548,  "Latitude",
             formatter=short_latlon_format),
    spare(5),
    bitfield("data",      736,  'raw',      None,    "DGNSS data"),
    )

type18 = (
    bitfield("reserved",    8,  'unsigned', None,      "Regional reserved"),
    bitfield("speed",       10, 'unsigned', 1023,      "Speed Over Ground",
             formatter=cnb_speed_format),
    bitfield("accuracy",    1,  'unsigned', None,      "Position Accuracy"),
    bitfield("lon",         28, 'signed',   0x6791AC0, "Longitude",
             formatter=cnb_latlon_format),
    bitfield("lat",         27, 'signed',   0x3412140, "Latitude",
             formatter=cnb_latlon_format),
    bitfield("course",      12, 'unsigned', 0xE10,     "Course Over Ground",
             formatter=cnb_course_format),
    bitfield("heading",     9,  'unsigned', 511,       "True Heading"),
    bitfield("second",      6,  'unsigned', None,      "Time Stamp",
             formatter=cnb_second_format),
    bitfield("regional",    2,  'unsigned', None,      "Regional reserved"),
    bitfield("cs",          1,  'unsigned', None,      "CS Unit"),
    bitfield("display",     1,  'unsigned', None,      "Display flag"),
    bitfield("dsc",         1,  'unsigned', None,      "DSC flag"),
    bitfield("band",        1,  'unsigned', None,      "Band flag"),
    bitfield("msg22",       1,  'unsigned', None,      "Message 22 flag"),
    bitfield("assigned",    1,  'unsigned', None,      "Assigned"),
    bitfield("raim",        1,  'unsigned', None,      "RAIM flag"),
    bitfield("radio",       20, 'unsigned', None,      "Radio status"),
    )

type19 = (
    bitfield("reserved",    8,  'unsigned', None,      "Regional reserved"),
    bitfield("speed",       10, 'unsigned', 1023,      "Speed Over Ground",
             formatter=cnb_speed_format),
    bitfield("accuracy",    1,  'unsigned', None,      "Position Accuracy"),
    bitfield("lon",         28, 'signed',   0x6791AC0, "Longitude",
             formatter=cnb_latlon_format),
    bitfield("lat",         27, 'signed',   0x3412140, "Latitude",
             formatter=cnb_latlon_format),
    bitfield("course",      12, 'unsigned', 0xE10,     "Course Over Ground",
             formatter=cnb_course_format),
    bitfield("heading",     9,  'unsigned', 511,       "True Heading"),
    bitfield("second",      6,  'unsigned', None,      "Time Stamp",
             formatter=cnb_second_format),
    bitfield("regional",    4,  'unsigned', None,      "Regional reserved"),
    bitfield("shipname",  120,  'string',   None,      "Vessel Name"),
    bitfield("shiptype",    8,  'unsigned', None,      "Ship Type",
             #validator=lambda n: n >= 0 and n <= 99,
             formatter=ship_type_legends),
    bitfield("to_bow",      9,  'unsigned', 0,         "Dimension to Bow"),
    bitfield("to_stern",    9,  'unsigned', 0,         "Dimension to Stern"),
    bitfield("to_port",     6,  'unsigned', 0,         "Dimension to Port"),
    bitfield("to_starbord", 6,  'unsigned', 0,         "Dimension to Starboard"),
    bitfield("epfd",        4,  'unsigned', 0,         "Position Fix Type",
             validator=lambda n: n >= 0 and n <= 8 or n == 15,
             formatter=epfd_type_legends),
    bitfield("assigned",    1,  'unsigned', None,      "Assigned"),
    bitfield("raim",        1,  'unsigned', None,      "RAIM flag"),
    bitfield("radio",       20, 'unsigned', None,      "Radio status"),
    )

type20 = (
    spare(2),
    bitfield("offset1",    12, 'unsigned', 0, "Offset number"),
    bitfield("number1",     4, 'unsigned', 0, "Reserved slots"),
    bitfield("timeout1",    3, 'unsigned', 0, "Time-out"),
    bitfield("increment1", 11, 'unsigned', 0, "Increment"),
    bitfield("offset2",    12, 'unsigned', 0, "Offset number 2"),
    bitfield("number2",     4, 'unsigned', 0, "Reserved slots"),
    bitfield("timeout2",    3, 'unsigned', 0, "Time-out"),
    bitfield("increment2", 11, 'unsigned', 0, "Increment"),
    bitfield("offset3",    12, 'unsigned', 0, "Offset number 3"),
    bitfield("number3",     4, 'unsigned', 0, "Reserved slots"),
    bitfield("timeout3",    3, 'unsigned', 0, "Time-out"),
    bitfield("increment3", 11, 'unsigned', 0, "Increment"),
    bitfield("offset4",    12, 'unsigned', 0, "Offset number 4"),
    bitfield("number4",     4, 'unsigned', 0, "Reserved slots"),
    bitfield("timeout4",    3, 'unsigned', 0, "Time-out"),
    bitfield("increment4", 11, 'unsigned', 0, "Increment"),
    )

aide_type_legends = (
	"Unspecified",
	"Reference point",
	"RACON",
	"Fixed offshore structure",
	"Spare, Reserved for future use.",
	"Light, without sectors",
	"Light, with sectors",
	"Leading Light Front",
	"Leading Light Rear",
	"Beacon, Cardinal N",
	"Beacon, Cardinal E",
	"Beacon, Cardinal S",
	"Beacon, Cardinal W",
	"Beacon, Port hand",
	"Beacon, Starboard hand",
	"Beacon, Preferred Channel port hand",
	"Beacon, Preferred Channel starboard hand",
	"Beacon, Isolated danger",
	"Beacon, Safe water",
	"Beacon, Special mark",
	"Cardinal Mark N",
	"Cardinal Mark E",
	"Cardinal Mark S",
	"Cardinal Mark W",
	"Port hand Mark",
	"Starboard hand Mark",
	"Preferred Channel Port hand",
	"Preferred Channel Starboard hand",
	"Isolated danger",
	"Safe Water",
	"Special Mark",
	"Light Vessel / LANBY / Rigs",
        )

type21 = (
    bitfield("aid_type",        5, 'unsigned',  0,         "Aid type",
             formatter=aide_type_legends),
    bitfield("name",          120, 'string',    None,      "Name"),
    bitfield("accuracy",        1, 'unsigned',  0,         "Position Accuracy"),
    bitfield("lon",            28, 'signed',    0x6791AC0, "Longitude",
             formatter=cnb_latlon_format),
    bitfield("lat",            27, 'signed',    0x3412140, "Latitude",
             formatter=cnb_latlon_format),
    bitfield("to_bow",          9, 'unsigned',  0,         "Dimension to Bow"),
    bitfield("to_stern",        9, 'unsigned',  0,         "Dimension to Stern"),
    bitfield("to_port",         6, 'unsigned',  0,         "Dimension to Port"),
    bitfield("to_starboard",    6, 'unsigned',  0,         "Dimension to Starboard"),
    bitfield("epfd",            4, 'unsigned',  0,         "Position Fix Type",
             validator=lambda n: n >= 0 and n <= 8 or n == 15,
             formatter=epfd_type_legends),
    bitfield("second",          6, 'unsigned',  0,         "UTC Second"),
    bitfield("off_position",    1, 'unsigned',  0,         "Off-Position Indicator"),
    bitfield("regional",        8, 'unsigned',  0,         "Regional reserved"),
    bitfield("raim",            1, 'unsigned',  0,         "RAIM flag"),
    bitfield("virtual_aid",     1, 'unsigned',  0,         "Virtual-aid flag"),
    bitfield("assigned",        1, 'unsigned',  0,         "Assigned-mode flag"),
    spare(1),
    bitfield("name",           88, 'string',    0,         "Name Extension"),
    )

type22 = (
    spare(2),
    bitfield("channel_a", 12, 'unsigned',  0,       "Channel A"),
    bitfield("channel_b", 12, 'unsigned',  0,       "Channel B"),
    bitfield("txrx",       4, 'unsigned',  0,       "Tx/Rx mode"),
    bitfield("power",      1, 'unsigned',  0,       "Power"),
    bitfield("ne_lon",    18, 'signed',    0x1a838, "NE Longitude",
             formatter=short_latlon_format),
    bitfield("ne_lat",    17, 'signed',    0xd548,  "NE Latitude",
             formatter=short_latlon_format),
    bitfield("sw_lon",    18, 'signed',    0x1a838, "SW Longitude",
             formatter=short_latlon_format),
    bitfield("sw_lat",    17, 'signed',    0xd548,  "SW Latitude",
             formatter=short_latlon_format),
    bitfield("addressed",  1, 'unsigned',  0,       "Addressed"),
    bitfield("band_a",     1, 'unsigned',  0,       "Channel A Band"),
    bitfield("band_a",     1, 'unsigned',  0,       "Channel A Band"),
    bitfield("zonesize",   3, 'unsigned',  0,       "Zone size"),
    spare(23),
    )

station_type_legends = (
	"All types of mobiles",
	"Reserved for future use",
	"All types of Class B mobile stations",
	"SAR airborne mobile station",
	"Aid to Navigation station",
	"Class B shipborne mobile station",
	"Regional use and inland waterways",
	"Regional use and inland waterways",
	"Regional use and inland waterways",
	"Regional use and inland waterways",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
	"Reserved for future use",
        )

type23 = (
    spare(2),
    bitfield("ne_lon",    18, 'signed',    0x1a838, "NE Longitude",
             formatter=short_latlon_format),
    bitfield("ne_lat",    17, 'signed',    0xd548,  "NE Latitude",
             formatter=short_latlon_format),
    bitfield("sw_lon",    18, 'signed',    0x1a838, "SW Longitude",
             formatter=short_latlon_format),
    bitfield("sw_lat",    17, 'signed',    0xd548,  "SW Latitude",
             formatter=short_latlon_format),
    bitfield("stationtype",4, 'unsigned',  0,       "Station Type",
             validator=lambda n: n >= 0 and n <= 31,
             formatter=station_type_legends),
    bitfield("shiptype",   8, 'unsigned',  0,       "Ship Type",
             #validator=lambda n: n >= 0 and n <= 99,
             formatter=ship_type_legends),
    spare(22),
    bitfield("txrx",       2, 'unsigned',  0,       "Tx/Rx mode"),
    bitfield("interval",   4, 'unsigned',  0,       "Reporting interval"),
    bitfield("txrx",       4, 'unsigned',  0,       "Quiet time"),
    )

type24a = (
    bitfield("shipname",    120, 'string',   None, "Vessel Name"),
    spare(8),
    )


type24b1 = (
    bitfield("callsign",     42, 'string',   None, "Call Sign"),              
    bitfield("to_bow",        9, 'unsigned',    0, "Dimension to Bow"),
    bitfield("to_stern",      9, 'unsigned',    0, "Dimension to Stern"),
    bitfield("to_port",       6, 'unsigned',    0, "Dimension to Port"),
    bitfield("to_starbord",   6, 'unsigned',    0, "Dimension to Starboard"),
    spare(8),
    )

type24b2 = (
    bitfield('mothership_mmsi', 30, 'unsigned',    0, "Mothership MMSI"),
    spare(8),
    )

type24b = (
    bitfield("shiptype",      8, 'unsigned', None, "Ship Type",
             validator=lambda n: n >= 0 and n <= 99,
             formatter=ship_type_legends),
    bitfield("vendorid",     42, 'string',   None, "Vendor ID"),
    dispatch("mmsi", [type24b1, type24b2], lambda m: 1 if `m`[:2]=='98' else 0),
    )

type24 = (
    bitfield('partno', 2, 'unsigned', None, "Part Number"),
    dispatch('partno', [type24a, type24b]),
    )

type25 = (
    bitfield("addressed",     1, 'unsigned',    None, "Addressing flag"),
    bitfield("structured",    1, 'unsigned',    None, "Dimension to Bow"),
    bitfield("dest_mmsi",    30, 'unsigned',       0, "Destinstion MMSI",
             conditional=lambda i, v: v["addressed"]),
    bitfield("app_id",       16, 'unsigned',       0, "Application ID",
             conditional=lambda i, v: v["structured"]),
    bitfield("data",       None, 'raw',         None, "Data"),
    )

# No type 26 handling yet, we'd need new machinery to contrain how many
# bits the data spec eats in order to recover the radio bits after it.

aivdm_decode = (
    bitfield('msgtype',       6, 'unsigned',    0, "Message Type",
        validator=lambda n: n > 0 and n <= 26),
    bitfield('repeat',	      2, 'unsigned', None, "Repeat Indicator"),
    bitfield('mmsi',         30, 'unsigned',    0, "MMSI"),
    # This is the master dispatch on AIS message type
    dispatch('msgtype',      [None,   cnb,    cnb,    cnb,    type4,
                              type5,  type6,  type7,   type8,  type9,
                              type10, type4,  type12,  type7,  type14,
                              type15, type16, type17,  type18, type19,
                              type20, type21, type22,  type23, type24,
                              type25]),
    )

# Length ranges.  We use this for integrity checking.
# When a renge is a tuple, it's (minimum, maximum).
lengths = {
    1:  168,
    2:  168,
    3:  168,
    4:  168,
    5:  424,
    6:  (88, 1008),
    7:  (72, 168),
    8:  (56, 1008),
    9:  168,
    10: 72,
    11: 168,
    12: (72, 1008),
    13: (72, 168),
    14: (40, 1008),
    15: (88, 168),
    16: (96, 144),
    17: (80, 816),
    18: 168,
    19: 312,
    20: (72, 160),
    21: (272, 360),
    22: 168,
    23: 160,
    24: (160, 168),
    25: 168,
    26: (60, 1004),
    }

field_groups = (
    # This one occurs in message type 4
    (3, ["year", "month", "day", "hour", "minute", "second"],
     "time", "Timestamp",
     lambda y, m, d, h, n, s: "%02d:%02d:%02dT%02d:%02d:%02dZ" % (y, m, d, h, n, s)),
    # This one is in message 5
    (13, ["month", "day", "hour", "minute", "second"],
     "eta", "Estimated Time of Arrival",
     lambda m, d, h, n, s: "%02d:%02dT%02d:%02d:%02dZ" % (m, d, h, n, s)),
)

# Message-type-specific information ends here.
#
# Next, the execution machinery for the pseudolanguage. There isn't much of
# this: the whole point of the design is to embody most of the information
# about the AIS format in the pseudoinstruction tables.

from array import array

BITS_PER_BYTE = 8

class BitVector:
    "Fast bit-vector class based on Python built-in array type."
    def __init__(self, data=None, length=None):
        self.bits = array('B')
        self.bitlen = 0
        if data is not None:
            self.bits.extend(data)
            if length is None:
                self.bitlen = len(data) * 8
            else:
                self.bitlen = length
    def extend_to(self, length):
        "Extend vector to given bitlength."
        if length > self.bitlen:
            self.bits.extend([0]*((length - self.bitlen +7 )/8))
            self.bitlen = length
    def from_sixbit(self, data, pad=0):
        "Initialize bit vector from AIVDM-style six-bit armoring."
        self.bits.extend([0] * len(data))
        for ch in data:
            ch = ord(ch) - 48
            if ch > 40:
                ch -= 8
            for i in (5, 4, 3, 2, 1, 0):
                if (ch >> i) & 0x01:
                    self.bits[self.bitlen/8] |= (1 << (7 - self.bitlen % 8))
                self.bitlen += 1
        self.bitlen -= pad
    def ubits(self, start, width):
        "Extract a (zero-origin) bitfield from the buffer as an unsigned int."
        fld = 0
        for i in range(start/BITS_PER_BYTE, (start + width + BITS_PER_BYTE - 1) / BITS_PER_BYTE):
            fld <<= BITS_PER_BYTE
            fld |= self.bits[i]
        end = (start + width) % BITS_PER_BYTE
        if end != 0:
            fld >>= (BITS_PER_BYTE - end)
        fld &= ~(-1 << width)
        return fld
    def sbits(self, start, width):
        "Extract a (zero-origin) bitfield from the buffer as a signed int."
        fld = self.ubits(start, width);
        if fld & (1 << (width-1)):
            fld = -(2 ** width - fld)
        return fld
    def __len__(self):
        return self.bitlen
    def __repr__(self):
        "Used for dumping binary data."
        return str(self.bitlen) + ":" + "".join(map(lambda d: "%02x" % d, self.bits[:(self.bitlen + 7)/8]))

import sys, exceptions, re

class AISUnpackingException(exceptions.Exception):
    def __init__(self, lc, fieldname, value):
        self.lc = lc
        self.fieldname = fieldname
        self.value = value
    def __repr__(self):
        return "%d: validation on fieldname %s failed (value %s)" % (self.lc, self.fieldname, self.value)

def aivdm_unpack(lc, data, offset, values, instructions):
    "Unpack fields from data according to instructions."
    cooked = []
    for inst in instructions:
        if offset >= len(data):
            break
        elif inst.conditional is not None and not inst.conditional(inst,values):
            continue
        elif isinstance(inst, spare):
            offset += inst.width
        elif isinstance(inst, dispatch):
            i = inst.compute(values[inst.fieldname])
            # This is the recursion that lets us handle variant types
            cooked += aivdm_unpack(lc, data, offset, values, inst.subtypes[i])
        elif isinstance(inst, bitfield):
            if inst.type == 'unsigned':
                value = data.ubits(offset, inst.width)
            elif inst.type == 'signed':
                value = data.sbits(offset, inst.width)
            elif inst.type == 'string':
                value = ''
                # The try/catch error here is in case we run off the end
                # of a variable-length string field, as in messages 12 and 14
                try:
                    for i in range(inst.width/6):
                        newchar = "@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^- !\"#$%&'()*+,-./0123456789:;<=>?"[data.ubits(offset + 6*i, 6)]
                        if newchar == '@':
                            break
                        else:
                            value += newchar
                except IndexError:
                    pass
                value = value.replace("@", " ").rstrip()
            elif inst.type == 'raw':
                # Note: Doesn't rely on the length.
                value = BitVector(data.bits[offset/8:], len(data)-offset)
            values[inst.name] = value
            if inst.validator and not inst.validator(value):
                raise AISUnpackingException(lc, inst.name, value)
            offset += inst.width
            # An important thing about the unpacked representation this
            # generates is that it carries forward the meta-information from
            # the field type definition.  This stuff is then available for
            # use by report-generating code.
            cooked.append([inst, value])
    return cooked

def packet_scanner(source):
    "Get a span of AIVDM packets with contiguous fragment numbers."
    payloads = {'A':'', 'B':''}
    raw = ''
    well_formed = False
    lc = 0
    while True:
        lc += 1;
        line = source.readline()
        if not line:
            return
        raw += line
        line = line.strip()
        # Strip off USCG metadata 
        line = re.sub(r"(?<=\*[0-9A-F][0-9A-F]),.*", "", line)
        # Compute CRC-16 checksum
        packet = line[1:-3]	# Strip leading !, trailing * and CRC
        csum = 0
        for c in packet:
            csum ^= ord(c)
        csum = "%02X" % csum
        # Ignore comments
        if not line.startswith("!"):
            continue
        # Assemble fragments from single- and multi-line payloads
        fields = line.split(",")
        try:
            expect = fields[1]
            fragment = fields[2]
            channel = fields[4]
            if fragment == '1':
                payloads[channel] = ''
                well_formed = True
            payloads[channel] += fields[5]
            try:
                # This works because a mangled pad literal means
                # a malformed packet that will be caught by the CRC check. 
                pad = int(fields[6].split('*')[0])
            except ValueError:
                pad = 0
            crc = fields[6].split('*')[1].strip()
        except IndexError:
            if skiperr:
                sys.stderr.write("%d: malformed line %s\n" % (lc, line.strip()))
                well_formed = False
            else:
                raise AISUnpackingException(lc, "checksum", crc)
        if csum != crc:
            if skiperr:
                sys.stderr.write("%d: bad checksum %s, expecting %s: %s\n" % (lc, csum, `crc`, line.strip()))
                well_formed = False
            else:
                raise AISUnpackingException(lc, "checksum", crc)
        if fragment < expect or not well_formed:
            continue
        # Render assembled payload to packed bytes
        bits = BitVector()
        bits.from_sixbit(payloads[channel], pad)
        yield (lc, raw, bits)

def postprocess(cooked):
    "Postprocess cooked fields from a message."
    # Handle type 21 name extension
    if cooked[0][1] == 21:
        cooked[4][1] += cooked[19][1]
        cooked.pop(-1)
    return cooked

def parse_ais_messages(source, scaled=False, skiperr=False, verbose=0):
    "Generator code - read forever from source stream, parsing AIS messages."
    values = {}
    for (lc, raw, bits) in packet_scanner(source):
        values['length'] = bits.bitlen
        # Without the following magic, we'd have a subtle problem near
        # certain variable-length messages: DSV reports would
        # sometimes have fewer fields than expected, because the
        # unpacker would never generate cooked tuples for the omitted
        # part of the message.  Presently a known issue for types 15
        # and 16 only.  (Would never affect variable-length messages in
        # which the last field type is 'string' or 'raw').
        bits.extend_to(168)
        # Magic recursive unpacking operation
        try:
            cooked = aivdm_unpack(lc, bits, 0, values, aivdm_decode)
            # We now have a list of tuples containing unpacked fields
            # Collect some field groups into ISO8601 format
            for (offset, template, label, legend, formatter) in field_groups:
                segment = cooked[offset:offset+len(template)]
                if map(lambda x: x[0], segment) == template:
                    group = formatter(*map(lambda x: x[1], segment))
                    group = (label, group, 'string', legend, None)
                    cooked = cooked[:offset]+[group]+cooked[offset+len(template):]
            # Apply the postprocessor stage
            cooked = postprocess(cooked)
            # Now apply custom formatting hooks.
            if scaled:
                for (i, (inst, value)) in enumerate(cooked):
                    if value == inst.oob:
                        cooked[i][1] = "n/a"
                    elif inst.formatter:
                        if type(inst.formatter) == type(()):
                            # Assumes 0 is the legend for the "undefined" value 
                            if value >= len(inst.formatter):
                                value = 0
                            cooked[i][1] = inst.formatter[value]
                        elif type(formatter) == type(lambda x: x):
                            cooked[i][1] = inst.formatter(value)
            expected = lengths.get(values['msgtype'], None)
            # Check length; has to be done after so we have the type field 
            bogon = False
            if expected is not None:
                if type(expected) == type(0):
                    expected_range = (expected, expected)
                else:
                    expected_range = expected
                actual = values['length']
                if not (actual >= expected_range[0] and actual <= expected_range[1]):
                    bogon = True
                    if skiperr:
                        sys.stderr.write("%d: type %d expected %s bits but saw %s\n" % (lc, values['msgtype'], expected, actual))
                    else:
                        raise AISUnpackingException(lc, "length", actual)
            # We're done, hand back a decoding
            values = {}                    
            yield (raw, cooked, bogon)
            raw = ''
        except KeyboardInterrupt:
            raise KeyboardInterrupt
        except GeneratorExit:
            raise GeneratorExit
        except AISUnpackingException, e:
            if skiperr:
                sys.stderr.write("%s on %s\n" % (`e`, raw.strip()))
                continue
            else:
                raise e
        except:
            (exc_type, exc_value, exc_traceback) = sys.exc_info()
            sys.stderr.write("Unknown exception on line %d\n" % lc)
            if skiperr:
                continue
            else:
                raise exc_type, exc_value, exc_traceback

# The rest is just sequencing and report generation.

if __name__ == "__main__":
    import sys, getopt

    try:
        (options, arguments) = getopt.getopt(sys.argv[1:], "cdhjmqst:vx")
    except getopt.GetoptError, msg:
        print "ais.py: " + str(msg)
        raise SystemExit, 1

    dsv = False
    dump = False
    histogram = False
    json = False
    malformed = False
    quiet = False
    scaled = False
    types = []
    frequencies = {}
    verbose = 0
    skiperr = True
    for (switch, val) in options:
        if switch == '-c':
            dsv = True
        elif switch == '-d':
            dump = True
        elif switch == '-h':
            histogram = True
        elif switch == '-j':
            json = True
        elif switch == '-m':
            malformed = True
        elif switch == '-q':
            quiet = True
        elif switch == '-s':
            scaled = True
        elif switch == '-t':
            types = map(int, val.split(","))
        elif switch == '-v':
            verbose += 1
        elif switch == '-x':
            skiperr = False

    if not dsv and not histogram and not json and not malformed and not quiet:
            dump = True
    try:
        for (raw, parsed, bogon) in parse_ais_messages(sys.stdin, scaled, skiperr, verbose):
            msgtype = parsed[0][1]
            if types and msgtype not in types:
                continue
            if verbose >= 1:
                sys.stdout.write(raw)
            if not bogon:
                if json:
                    print "{" + ",".join(map(lambda x: '"' + x[0].name + '":' + str(x[1]), parsed)) + "}"
                elif dsv:
                    print "|".join(map(lambda x: str(x[1]), parsed))
                elif histogram:
                    frequencies[msgtype] = frequencies.get(msgtype, 0) + 1
                elif dump:
                    for (inst, value) in parsed:
                        print "%-25s: %s" % (inst.legend, value)
                    print "%%"
            elif malformed:
                sys.stdout.write(raw)
            sys.stdout.flush()
        if histogram:
            keys = frequencies.keys()
            keys.sort()
            for msgtype in keys:
                print "%d\t%d" % (msgtype, frequencies[msgtype])
    except KeyboardInterrupt:
        pass
# End
