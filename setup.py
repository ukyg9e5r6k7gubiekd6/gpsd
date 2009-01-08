# $Id$
# Creates build/lib.linux-${arch}-${pyvers}/gpspacket.so,
# where ${arch} is an architecture and ${pyvers} is a Python version.

from distutils.core import setup, Extension

import os
import sys

needed_files = ['packet_names.h', 'gpsfake.1', 'gpsprof.1']
created_files = []

if not 'clean' in sys.argv:
    if not os.path.exists('gpsd_config.h'):
        sys.stderr.write('\nPlease run configure first!\n')
        sys.exit(1)

    for f_name in needed_files:
        if not os.path.exists(f_name):
            make_in, make_out = os.popen2('make %s' % f_name)
            print make_out.read()
            make_out.close()
            make_in.close()
            created_files.append(f_name)

gpspacket_sources = ["gpspacket.c", "packet.c", "isgps.c",
            "rtcm2.c", "strl.c", "hex.c", "crc24q.c"]

setup( name="gpsd",
       version="1.0",
       ext_modules=[
    	Extension("gpspacket", gpspacket_sources),
    	Extension("gpslib", ["gpslib.c", "geoid.c"])
        ],
       py_modules = ['gpsfake','gps', 'leapsecond'],
       data_files=[('bin', ['gpsfake','gpsprof']),
           ('share/man/man1', ['gpsfake.1','gpsprof.1'])]
     )
