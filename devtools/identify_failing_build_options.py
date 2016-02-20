#!/usr/bin/env python

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
    import subprocess

    failed_configurations = []
    dev_null = open('/dev/null', 'w')

    def _run(command, phase):
        if subprocess.call(command, stdout=dev_null) == 0:
            return True
        failed_configurations.append(command)
        print command
        with open(phase + '_build_configs.txt', 'a') as failed_configs:
            failed_configs.write(' '.join(command) + '\n')
        return False

    static_params = [key + '=on' for key in always_on]
    static_params += [key + '=off' for key in always_off]

    for i in range(starting_number_of_options, len(knobs)):
        print 'Testing at length {}'.format(i)

        for row in itertools.combinations(knobs, i):
            print row
            params = static_params + [key + '=on' for key in row]

            # print {'on_params': row, 'scons_params': params}

            if os.path.exists('.scons-option-cache'):
                os.remove('.scons-option-cache')
            subprocess.call(['scons', '-c'], stdout=dev_null)

            if _run(['scons', '-j9'] + params, 'build'):
                _run(['scons', 'check'] + params, 'check')

    return failed_configurations

if __name__ == '__main__':
    failed = main(0)
    for row in failed:
        print ' '.join(row)
