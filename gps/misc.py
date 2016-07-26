# misc.py - miscellaneous geodesy and time functions
#
# This file is Copyright (c) 2010 by the GPSD project
# BSD terms apply: see the file COPYING in the distribution root for details.

# This code runs compatibly under Python 2 and 3.x for x >= 2.
# Preserve this property!
from __future__ import absolute_import, print_function, division

import time, calendar, math, io

# Determine a single class for testing "stringness"
try:
    STR_CLASS = basestring  # Base class for 'str' and 'unicode' in Python 2
except NameError:
    STR_CLASS = str         # In Python 3, 'str' is the base class

# We need to be able to handle data which may be a mixture of text and binary
# data.  The text in this context is known to be limited to US-ASCII, so
# there aren't any issues regarding character sets, but we need to ensure
# that binary data is preserved.  In Python 2, this happens naturally with
# "strings" and the 'str' and 'bytes' types are synonyms.  But in Python 3,
# these are distinct types (with 'str' being based on Unicode), and conversions
# are encoding-sensitive.  The most straightforward encoding to use in this
# context is 'latin-1' (a.k.a.'iso-8859-1'), which directly maps all 256
# 8-bit character values to Unicode page 0.  Thus, if we can enforce the use
# of 'latin-1' encoding, we can preserve arbitrary binary data while correctly
# mapping any actual text to the proper characters.

BINARY_ENCODING = 'latin-1'

if bytes is str:  # In Python 2 these functions can be null transformations

    polystr = str
    polybytes = bytes

    def make_std_wrapper(stream):
        "Dummy stdio wrapper function."
        return stream

else:  # Otherwise we do something real

    def polystr(o):
        "Convert bytes or str to str with proper encoding."
        if isinstance(o, str):
            return o
        if isinstance(o, bytes):
            return str(o, encoding=BINARY_ENCODING)
        raise ValueError

    def polybytes(o):
        "Convert bytes or str to bytes with proper encoding."
        if isinstance(o, bytes):
            return o
        if isinstance(o, str):
            return bytes(o, encoding=BINARY_ENCODING)
        raise ValueError

    def make_std_wrapper(stream):
        "Standard input/output wrapper factory function"
        # This ensures that the encoding of standard output and standard
        # error on Python 3 matches the binary encoding we use to turn
        # bytes to Unicode in polystr above.
        #
        # newline="\n" ensures that Python 3 won't mangle line breaks
        # line_buffering=True ensures that interactive command sessions
        # work as expected
        return io.TextIOWrapper(stream.buffer, encoding=BINARY_ENCODING,
                                newline="\n", line_buffering=True)


# some multipliers for interpreting GPS output
# Note: A Texas Foot is ( meters * 3937/1200)
#       (Texas Natural Resources Code, Subchapter D, Sec 21.071 - 79)
#       not the same as an international fooot.
METERS_TO_FEET	= 3.28083989501312	# Meters to U.S./British feet
METERS_TO_MILES	= 0.000621371192237334	# Meters to miles
METERS_TO_FATHOMS = 0.546806649168854	# Meters to fathoms
KNOTS_TO_MPH	= 1.15077944802354	# Knots to miles per hour
KNOTS_TO_KPH	= 1.852		        # Knots to kilometers per hour
KNOTS_TO_MPS	= 0.514444444444445	# Knots to meters per second
MPS_TO_KPH	= 3.6		        # Meters per second to klicks/hr
MPS_TO_MPH	= 2.2369362920544	# Meters/second to miles per hour
MPS_TO_KNOTS	= 1.9438444924406	# Meters per second to knots

# for high precision the geoid height should be used.
# this code does not do that,

# EarthDistance code swiped from Kismet and corrected


def Deg2Rad(x):
    "Degrees to radians."
    return x * (math.pi / 180)


def Rad2Deg(x):
    "Radians to degrees."
    return x * (180 / math.pi)


def CalcRad(lat):
    "Radius of curvature in meters at specified latitude."
    a = 6378.137
    e2 = 0.081082 * 0.081082
    # the radius of curvature of an ellipsoidal Earth in the plane of a
    # meridian of latitude is given by
    #
    # R' = a * (1 - e^2) / (1 - e^2 * (sin(lat))^2)^(3/2)
    #
    # where a is the equatorial radius,
    # b is the polar radius, and
    # e is the eccentricity of the ellipsoid = sqrt(1 - b^2/a^2)
    #
    # a = 6378 km (3963 mi) Equatorial radius (surface to center distance)
    # b = 6356.752 km (3950 mi) Polar radius (surface to center distance)
    # e = 0.081082 Eccentricity
    sc = math.sin(Deg2Rad(lat))
    x = a * (1.0 - e2)
    z = 1.0 - e2 * sc * sc
    y = pow(z, 1.5)
    r = x / y

    r = r * 1000.0      # Convert to meters
    return r


def EarthDistance(c1, c2):
    "Distance in meters between two points specified in degrees."
    (lat1, lon1) = c1
    (lat2, lon2) = c2
    x1 = CalcRad(lat1) * math.cos(Deg2Rad(lon1)) * math.sin(Deg2Rad(90 - lat1))
    x2 = CalcRad(lat2) * math.cos(Deg2Rad(lon2)) * math.sin(Deg2Rad(90 - lat2))
    y1 = CalcRad(lat1) * math.sin(Deg2Rad(lon1)) * math.sin(Deg2Rad(90 - lat1))
    y2 = CalcRad(lat2) * math.sin(Deg2Rad(lon2)) * math.sin(Deg2Rad(90 - lat2))
    z1 = CalcRad(lat1) * math.cos(Deg2Rad(90 - lat1))
    z2 = CalcRad(lat2) * math.cos(Deg2Rad(90 -lat2))
    a = (x1 *x2 + y1 *y2 + z1 *z2) /pow(CalcRad((lat1 +lat2) /2), 2)
    # a should be in [1, -1] but can sometimes fall outside it by
    # a very small amount due to rounding errors in the preceding
    # calculations (this is prone to happen when the argument points
    # are very close together).  Thus we constrain it here.
    if abs(a) > 1:
        a = 1
    elif a < -1:
        a = -1
    return CalcRad((lat1 +lat2) / 2) * math.acos(a)

def EarthDistanceSmall(c1, c2):
    "Distance in meters between two close points specified in degrees."
    # This calculation is known as an Equirectangular Projection
    # fewer numeric issues for small angles that other methods
    (lat1, lon1) = c1
    (lat2, lon2) = c2
    avglat = (lat1 + lat2 ) /2
    phi = math.radians( avglat )  # radians of avg latitude
    # meters per degree at this latitude, corrected for WGS84 ellipsoid
    # Note the wikipedia numbers are NOT ellipsoid corrected:
    # https://en.wikipedia.org/wiki/Decimal_degrees#Precision
    m_per_d = (111132.954 - 559.822 * math.cos(2 * phi)
                          +   1.175 * math.cos(4 * phi))
    dlat = (lat1 - lat2) * m_per_d
    dlon = (lon1 - lon2) * m_per_d * math.cos( phi )

    dist = math.sqrt( math.pow(dlat, 2) + math.pow(dlon, 2 ))
    return dist;

def MeterOffset(c1, c2):
    "Return offset in meters of second arg from first."
    (lat1, lon1) = c1
    (lat2, lon2) = c2
    dx = EarthDistance((lat1, lon1), (lat1, lon2))
    dy = EarthDistance((lat1, lon1), (lat2, lon1))
    if lat1 < lat2:
        dy *= -1
    if lon1 < lon2:
        dx *= -1
    return (dx, dy)


def isotime(s):
    "Convert timestamps in ISO8661 format to and from Unix time."
    if isinstance(s, int):
        return time.strftime("%Y-%m-%dT%H:%M:%S", time.gmtime(s))
    elif isinstance(s, float):
        date = int(s)
        msec = s - date
        date = time.strftime("%Y-%m-%dT%H:%M:%S", time.gmtime(s))
        return date + "." + repr(msec)[3:]
    elif isinstance(s, STR_CLASS):
        if s[-1] == "Z":
            s = s[:-1]
        if "." in s:
            (date, msec) = s.split(".")
        else:
            date = s
            msec = "0"
        # Note: no leap-second correction!
        return calendar.timegm(time.strptime(date, "%Y-%m-%dT%H:%M:%S")) + float("0." + msec)
    else:
        raise TypeError

# End

