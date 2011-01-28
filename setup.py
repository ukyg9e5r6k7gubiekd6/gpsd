# This file is Copyright (c) 2010 by the GPSD project
# BSD terms apply: see the file COPYING in the distribution root for details.
#
# Creates build/lib.linux-${arch}-${pyvers}/gpspacket.so,
# where ${arch} is an architecture and ${pyvers} is a Python version.

from distutils.core import setup, Extension

import os
import sys

# For VPATH builds, this script must be run from $(srcdir) with the
# abs_builddir environment variable set to the location of the build
# directory.  This is necessary because Python's distutils package
# does not have built-in support for VPATH builds.

# These dependencies are enforced here and not in the Makefile to make
# it easier to build the Python parts without building everything else
# (the user can run 'python setup.py' without having to run 'make').
needed_files = ['gpsd.h', 'packet_names.h']
created_files = []

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

MAKE = ("MAKE" in os.environ) and os.environ["MAKE"] or "make"
if not 'clean' in sys.argv:
    abs_builddir = ("abs_builddir" in os.environ) and os.environ["abs_builddir"] or ""
    if not os.path.exists(os.path.join(abs_builddir, 'gpsd_config.h')):
        sys.stderr.write('\nPlease run configure first!\n')
        sys.exit(1)

    cdcmd = abs_builddir and ("cd '" + abs_builddir + "' && ") or ""
    for f_name in needed_files:
        # TODO:  Shouldn't make be run unconditionally in case a
        # dependency of f_name has been updated?
        if not os.path.exists(os.path.join(abs_builddir, f_name)):
            cmd = cdcmd + MAKE + " '" + f_name + "'"
            print(cmd)
            make_out = os.popen(cmd)
            print(make_out.read())
            if make_out.close():
                sys.exit(1)
            created_files.append(f_name)

gpspacket_sources = ["gpspacket.c", "packet.c", "isgps.c",
            "driver_rtcm2.c", "strl.c", "hex.c", "crc24q.c"]
include_dirs = [ os.path.realpath(os.path.dirname(__file__)) ]

setup( name="gps",
       version=os.environ['version'],
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
