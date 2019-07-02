# Make core client functions available without prefix.
#
# This file is Copyright (c) 2010 by the GPSD project
# BSD terms apply: see the file COPYING in the distribution root for details.
#
# This code runs compatibly under Python 2 and 3.x for x >= 2.
# Preserve this property!
from __future__ import absolute_import  # Ensure Python2 behaves like Python 3

from .gps import *
from .misc import *

# Keep in sync with GPSD_PROTO_MAJOR_VERSION and GPSD_PROTO_MINOR_VERSION in
# gpsd.h
api_major_version = 3   # bumped on incompatible changes
api_minor_version = 14  # bumped on compatible changes

# keep in sync with gpsd_version in SConstruct
__version__ = '3.19'

# The 'client' module exposes some C utility functions for Python clients.
# The 'packet' module exposes the packet getter via a Python interface.
