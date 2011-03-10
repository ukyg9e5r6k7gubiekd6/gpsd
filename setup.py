#!/usr/bin/python
# This file is Copyright (c) 2010 by the GPSD project
# BSD terms apply: see the file COPYING in the distribution root for details.
#
# Creates build/lib.linux-${arch}-${pyvers}/gpspacket.so,
# where ${arch} is an architecture and ${pyvers} is a Python version.

from distutils.core import setup, Extension

import os
import re
import sys

# For VPATH builds, this script must be run from $(srcdir) with the
# abs_builddir environment variable set to the location of the build
# directory.  This is necessary because Python's distutils package
# does not have built-in support for VPATH builds.

MAKE = os.getenv('MAKE', "make")
abs_builddir = os.getenv("abs_builddir", os.curdir)

# These dependencies are enforced here and not in the Makefile to make
# it easier to build the Python parts without building everything else
# (the user can run 'python setup.py' without having to run 'make').
needed_files = ['gpsd.h', 'packet_names.h']

# Sources and include directories
gpspacket_sources = ["gpspacket.c", "packet.c", "isgps.c",
            "driver_rtcm2.c", "strl.c", "hex.c", "crc24q.c"]
include_dirs = [ os.path.realpath(os.path.dirname(__file__)),
                 abs_builddir]

def make(f_name):
    cmd = "%s -C %s %s" %(MAKE, abs_builddir, f_name)
    print(cmd)
    make_cmd = os.popen(cmd)
    make_out = make_cmd.read()
    print(make_out)
    if make_cmd.close():
        sys.exit(1)
    return make_out



if not 'clean' in sys.argv:
    # Ensure we have a properly configured environment if not being called
    # from make.
    if not os.getenv('MAKE', None):
        if not os.path.exists(os.path.join(abs_builddir, 'Makefile')):
            sys.stderr.write('\nPlease run configure first!\n')
            sys.exit(1)
        else:
            make('Makefile')
    if not os.path.exists(os.path.join(abs_builddir, 'gpsd_config.h')):
        sys.stderr.write('\nPlease run configure first!\n')
        sys.exit(1)

    for f_name in needed_files:
        # TODO:  Shouldn't make be run unconditionally in case a
        # dependency of f_name has been updated?
        if not os.path.exists(os.path.join(abs_builddir, f_name)):
            make(f_name)

    version = os.getenv('version', None) or re.findall(r'^[0-9].*', make('version'), re.M)[0] 
else:
    version=''

manpages = []
try:
    where = sys.argv.index('--mangenerator')
    # Doesn't matter what it is, just that we have one
    if sys.argv[where+1]:
        manpages=[('share/man/man1', ['gpscat.1', 'gpsfake.1','gpsprof.1',
                                      'xgps.1', 'xgpsspeed.1'])]
        print("Installing manual pages, generator is %s" %( sys.argv[where+1]))
    sys.argv = sys.argv[:where] + sys.argv[where+2:]
except ValueError:
    pass
if not manpages:
    print("No XML processor, omitting manual-page installation.")





setup( name="gps",
       version=version,
       description='Python libraries for the gpsd service daemon',
       url="http://gpsd.berlios.de/",
       author='the GPSD project',
       author_email="gpsd-dev@lists.berlios.de",
       license="BSD",
       ext_modules=[
    	Extension("gps.packet", gpspacket_sources, include_dirs=include_dirs),
    	Extension("gps.clienthelpers", ["gpsclient.c", "geoid.c", "gpsdclient.c", "strl.c"], include_dirs=include_dirs)
        ],
       packages = ['gps'],
       scripts = ['gpscat','gpsfake','gpsprof', 'xgps', 'xgpsspeed'],
       data_files= manpages
     )
