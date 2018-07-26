#!/usr/bin/env python

# This code runs compatibly under Python 2 and 3.x for x >= 2.
# Preserve this property!
from __future__ import absolute_import, print_function, division

import os

always_on = [
    'minimal',
]

always_off = [
    'leapfetch',
]

other = [
    'debug',
    'chrpath',
    'ipv6',
    'manbuild',
    'nostrip',
    'slow',
    'profiling',
    'libQgpsmm',
]

knobs = [
    'aivdm',
    'ashtech',
    'bluez',
    'clientdebug',
    'control_socket',
    'controlsend',
    'coveraging',
    'dbus_export',
    'earthmate',
    'evermore',
    'force_global',
    'fury',
    'fv18',
    'garmin',
    'garmintxt',
    'geostar',
    'gpsclock',
    'gpsd',
    'gpsdclients',
    'itrax',
    'libgpsmm',
    'mtk3301',
    'navcom',
    'ncurses',
    'netfeed',
    'nmea0183',
    'nmea2000',
    'nofloats',
    'ntp',
    'ntpshm',
    'ntrip',
    'oceanserver',
    'oncore',
    'passthrough',
    'pps',
    'python',
    'qt',
    'reconfigure',
    'rtcm104v2',
    'rtcm104v3',
    'shared',
    'shm_export',
    'sirf',
    'socket_export',
    'squelch',
    'superstar2',
    'systemd',
    'timing',
    'tnt',
    'tripmate',
    'tsip',
    'ublox',
    'usb',
    'xgps',
]


def main(starting_number_of_options=0):
    import itertools
    import multiprocessing
    import shutil
    import subprocess

    num_cpus = multiprocessing.cpu_count()
    job_arg = '-j%d' % num_cpus

    failed_configurations = []
    dev_null = open('/dev/null', 'w')

    def _run(command, phase):
        if subprocess.call(command, stdout=dev_null) == 0:
            return True
        failed_configurations.append(command)
        print(command)
        with open('failed_%s_configs.txt' % phase, 'a') as failed_configs:
            failed_configs.write(' '.join(command) + '\n')
        return False

    static_params = [key + '=on' for key in always_on]
    static_params += [key + '=off' for key in always_off]

    for i in range(starting_number_of_options, len(knobs)):
        print('Testing at length {}'.format(i))

        for row in itertools.combinations(knobs, i):
            print(row)
            params = static_params + [key + '=on' for key in row]

            # print {'on_params': row, 'scons_params': params}

            # Clean before clearing cached options, in case options
            # affect what's cleaned.
            subprocess.call(['scons', '-c'], stdout=dev_null)
            # Now remove all the scons temporaries
            try:
                shutil.rmtree('.sconf_temp')
            except OSError:
                pass
            for f in ['.sconsign.dblite', '.scons-option-cache']:
                try:
                    os.remove(f)
                except OSError:
                    pass

            if _run(['scons', job_arg, 'build-all'] + params, 'build'):
                _run(['scons', job_arg, 'check'] + params, 'check')

    return failed_configurations


if __name__ == '__main__':
    failed = main(0)
    for row in failed:
        print(' '.join(row))
