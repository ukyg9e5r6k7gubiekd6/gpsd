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
]


def main(starting_number_of_options=0):
    import itertools
    failed_configurations = []

    for i in range(starting_number_of_options, len(knobs)):
        jj = itertools.combinations(knobs, i)
        print 'Testing at length {}'.format(i)

        for row in list(jj):
            print row
            params = []

            for key in always_on:
                params.append(key + "=on")

            for key in always_off:
                params.append(key + "=off")

            for key in knobs:
                if key in row:
                    params.append(key + "=on")

            # print {'on_params': row, 'scons_params': params}

            dev_null = open('/dev/null', 'w')
            import subprocess
            command = ['scons', '-j9']
            command.extend(params)
            if os.path.exists('.scons-option-cache'):
                os.remove('.scons-option-cache')
            retval = subprocess.call(['scons', '-c'], stdout=dev_null)

            retval = subprocess.call(command, stdout=dev_null)
            if retval != 0:
                failed_configurations.append(command)
                print command
                with open('failed_build_configs.txt', 'a') as failed_configs:
                    failed_configs.write(' '.join(command) + '\n')

            if retval == 0:
                command = ['scons', 'check']
                command.extend(params)
                retval = subprocess.call(command, stdout=dev_null)
                if retval != 0:
                    print command
                with open('check_build_configs.txt', 'a') as failed_configs:
                    failed_configs.write(str(retval) + ' ' + ' '.join(command) + '\n')

    return failed_configurations

if __name__ == '__main__':
    failed = main(0)
    for row in failed:
        print ' '.join(row)


