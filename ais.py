#!/usr/bin/env python
#
# A Python AIVDM/AIVDO decoder
#

class bitfield:
    "Object defining the interpretation of an AIS bitfield."
    def __init__(self, name, width, dtype, oob, legend,
                 validator=None, formatter=None):
        self.name = name	# Name of field, for internal use and JSON
        self.width = width	# Bit width
        self.type = dtype	# Data type: signed, unsigned, string, or raw
        self.oob = oob		# Out-of-band value to be rendewred as /n/a
        self.legend = legend	# Human-friendly description of field
        self.validator = validator
        self.formatter = formatter

class spare:
    "Describes spare bits,, not to be interpreted."
    def __init__(self, width):
        self.width = width

class dispatch:
    "Describes how to dispatch to a message type variant."
    def __init__(self, fieldname, subtypes):
        self.fieldname = fieldname
        self.subtypes = subtypes
    
# Message-type-specific information begins here.

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
    bitfield("course",  12, 'unsigned',	0xe10,      "Course Over Ground"),
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
             formatter=ship_type_legends),
    bitfield("to_bow",        9, 'unsigned',    0, "Dimension to Bow"),
    bitfield("to_stern",      9, 'unsigned',    0, "Dimension to Stern"),
    bitfield("to_port",       6, 'unsigned',    0, "Dimension to Port"),
    bitfield("to_starbord",   6, 'unsigned',    0, "Dimension to Starboard"),
    bitfield("epfd",          4, 'unsigned',    0, "Position Fix Type",
             formatter=epfd_type_legends),
    bitfield("month",         4, 'unsigned',    0, "ETA month"),
    bitfield("day",           5, 'unsigned',    0, "ETA day"),
    bitfield("hour",          5, 'unsigned',   24, "ETA hour"),
    bitfield("minute",        6, 'unsigned',   60, "ETA minute"),
    bitfield("second",        8, 'unsigned',    0, "Draught"),
    bitfield("destination", 120, 'string',   None, "Destination"),
    bitfield("dte",           1, 'unsigned', None, "DTE"),
    spare(1),
    )

type6 = (
    bitfield("seqno",            2, 'unsigned', None, "Sequence Number"),
    bitfield("dest_mmsi",       30, 'unsigned', None, "Destination MMSI"),
    bitfield("retransmit",       1, 'unsigned', None, "Retransmit flag"),
    spare(1),
    bitfield("application_id",  16, 'unsigned', 0,    "Application ID"),
    bitfield("data",           920, 'raw',      None, "Data"),
    )

aivdm_decode = [
    bitfield('msgtype',       6, 'unsigned',    0, "Message Type",
        validator=lambda n: n>0 and n<=6),
    bitfield('repeat',	      2, 'unsigned', None, "Repeat Indicator"),
    bitfield('mmsi',         30, 'unsigned',    0, "MMSI"),
    dispatch('msgtype',      [None, cnb, cnb, cnb, type4, type5,
                              type6]),
    ]

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

# Message-type-specific information ends here

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
    def from_sixbit(self, data):
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
    def __repr__(self):
        return str(self.bitlen) + ":" + "".join(map(lambda d: "%02x" % d, self.bits[:(self.bitlen + 7)/8]))

class AISUnpackingException:
    def __init__(self, fieldname, value):
        self.fieldname = fieldname
        self.value = value
    def __repr__(self):
        return "Validation on fieldname %s failed (value %s)" % (self.fieldname, self.value)

def aivdm_unpack(data, offset, instructions):
    "Unpack fields from data according to instructions."
    cooked = []
    values = {}
    for inst in instructions:
        if isinstance(inst, spare):
            offset += inst.width
        elif isinstance(inst, dispatch):
            i = values[inst.fieldname]
            # This is the recursion that lets us handle variant types
            cooked += aivdm_unpack(data, offset, inst.subtypes[i])
        elif isinstance(inst, bitfield):
            if inst.type == 'unsigned':
                value = data.ubits(offset, inst.width)
            elif inst.type == 'signed':
                value = data.sbits(offset, inst.width)
            elif inst.type == 'string':
                value = ''
                for i in range(inst.width/6):
                    value += "@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^- !\"#$%&`()*+,-./0123456789:;<=>?"[data.ubits(offset + 6*i, 6)]
                value = value.replace("@", " ").rstrip()
            elif inst.type == 'raw':
                value = BitVector(data.bits[offset/8:], data.bitlen-offset)
            values[inst.name] = value
            if inst.validator and not inst.validator(value):
                raise AISUnpackingException(inst.name, value)
            offset += inst.width
            cooked.append((inst.name, value, inst.type, inst.legend, inst.formatter))
    return cooked

if __name__ == "__main__":
    import sys, getopt

    try:
        (options, arguments) = getopt.getopt(sys.argv[1:], "js")
    except getopt.GetoptError, msg:
        print "ais.py: " + str(msg)
        raise SystemExit, 1

    scaled = False
    json = False
    for (switch, val) in options:
        if (switch == '-s'):
            scaled = True
        if (switch == '-j'):
            json = True

    payload = ''
    while True:
        line = sys.stdin.readline()
        if not line:
            break
        # Ignore comments
        if line.startswith("#"):
            continue
        # Assemble fragments from single- and multi-line payloads
        fields = line.split(",")
        expect = fields[1]
        fragment = fields[2]
        if fragment == '1':
            payload = ''
        payload += fields[5]
        if fragment < expect:
            continue
        # Render assembled payload to packed bytes
        bits = BitVector()
        bits.from_sixbit(payload)
        # Magic recursive unpacking operation
        cooked = aivdm_unpack(bits, 0, aivdm_decode)
        # We now have a list of tuples containing unpacked fields
        # Collect some field groups into ISO8601 format
        for (offset, template, label, legend, formatter) in field_groups:
            segment = cooked[offset:offset+len(template)]
            if map(lambda x: x[0], segment) == template:
                group = formatter(*map(lambda x: x[1], segment))
                group = (label, group, legend, None)
                cooked = cooked[:offset]+[group]+cooked[offset+len(template):]
        # Report generation
        if not json:
            print ",".join(map(lambda x: str(x[1]), cooked))
        else:
            print "{" + ",".join(map(lambda x: '"' + x[0] + '"=' + str(x[1]), cooked)) + "}"

