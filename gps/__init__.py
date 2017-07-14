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

api_major_version = 5   # bumped on incompatible changes
api_minor_version = 0   # bumped on compatible changes

# The 'client' module exposes some C utility functions for Python clients.
# The 'packet' module exposes the packet getter via a Python interface.
