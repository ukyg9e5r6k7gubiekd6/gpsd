#!/usr/bin/env python
# encoding: utf-8

"""webgps.py

This is a Python port of webgps.c
from http://www.wireless.org.au/~jhecker/gpsd/
by Beat Bolli <me+gps@drbeat.li>

It creates a skyview of the currently visible GPS satellites and their tracks
over a time period.

Usage:
    ./webgps.py [duration]

    duration may be
    - a number of seconds
    - a number followed by a time unit ('s' for secinds, 'm' for minutes,
      'h' for hours or 'd' for days, e.g. '4h' for a duration of four hours)
    - the letter 'c' for continuous operation

If duration is missing, the current skyview is generated and webgps.py exits
immediately. This is the same as giving a duration of 0.

If a duration is given, webgps.py runs for this duration and generates the
tracks of the GPS satellites in view. If the duration is the letter 'c',
the script never exits and continuously updates the skyview.

webgps.py generates two files: a HTML5 file that can be browsed, and a
JavaScript file that contains the drawing commands for the skyview. The HTML5
file auto-refreshes every five minutes. The generated file names are
"gpsd-<duration>.html" and "gpsd-<duration>.js".

If webgps.py is interrupted with Ctrl-C before the duration is over, it saves
the current tracks into the file "tracks.p". This is a Python "pickle" file.
If this file is present on start of webgps.py, it is loaded. This allows to
restart webgps.py without losing accumulated satellite tracks.
"""

from __future__ import absolute_import, print_function, division

import math
import os
import pickle
import sys
import time

from gps import *

gps_version = '3.18.1'
if gps.__version__ != gps_version:
    sys.stderr.write("webgps.py: ERROR: need gps module version %s, got %s\n" %
                     (gps_version, gps.__version__))
    sys.exit(1)


TRACKMAX = 1024
STALECOUNT = 10

DIAMETER = 200


def polartocart(el, az):
    radius = DIAMETER * (1 - el / 90.0)   # * math.cos(Deg2Rad(float(el)))
    theta = Deg2Rad(float(az - 90))
    return (
        # Changed this back to normal orientation - fw
        int(radius * math.cos(theta) + 0.5),
        int(radius * math.sin(theta) + 0.5)
    )


class Track:
    '''Store the track of one satellite.'''

    def __init__(self, prn):
        self.prn = prn
        self.stale = 0
        self.posn = []          # list of (x, y) tuples

    def add(self, x, y):
        pos = (x, y)
        self.stale = STALECOUNT
        if not self.posn or self.posn[-1] != pos:
            self.posn.append(pos)
            if len(self.posn) > TRACKMAX:
                self.posn = self.posn[-TRACKMAX:]
            return 1
        return 0

    def track(self):
        '''Return the track as canvas drawing operations.'''
        return('M(%d,%d); ' % self.posn[0] + ''.join(['L(%d,%d); ' %
               p for p in self.posn[1:]]))


class SatTracks(gps):
    '''gpsd client writing HTML5 and <canvas> output.'''

    def __init__(self):
        super(SatTracks, self).__init__()
        self.sattrack = {}      # maps PRNs to Tracks
        self.state = None
        self.statetimer = time.time()
        self.needsupdate = 0

    def html(self, fh, jsfile):
        fh.write("""<!DOCTYPE html>

<html>
<head>
\t<meta http-equiv=Refresh content=300>
\t<meta charset='utf-8'>
\t<title>GPSD Satellite Positions and Readings</title>
\t<style type='text/css'>
\t\t.num td { text-align: right; }
\t\tth { text-align: left; }
\t</style>
\t<script src='%s'></script>
</head>
<body>
\t<table border=1>
\t\t<tr>
\t\t\t<td>
\t\t\t\t<table border=0 class=num>
\t\t\t\t\t<tr><th>PRN:</th><th>Elev:</th><th>Azim:</th><th>SNR:</th>
<th>Used:</th></tr>
""" % jsfile)

        sats = self.satellites[:]
        sats.sort(key=lambda x: x.PRN)
        for s in sats:
            fh.write("\t\t\t\t\t<tr><td>%d</td><td>%d</td><td>%d</td>"
                     "<td>%d</td><td>%s</td></tr>\n" %
                     (s.PRN, s.elevation, s.azimuth, s.ss,
                      s.used and 'Y' or 'N'))

        fh.write("\t\t\t\t</table>\n\t\t\t\t<table border=0>\n")

        def row(l, v):
            fh.write("\t\t\t\t\t<tr><th>%s:</th><td>%s</td></tr>\n" % (l, v))

        def deg_to_str(a, hemi):
            return '%.6f %c' % (abs(a), hemi[a < 0])

        row('Time', self.utc or 'N/A')

        if self.fix.mode >= MODE_2D:
            row('Latitude', deg_to_str(self.fix.latitude, 'SN'))
            row('Longitude', deg_to_str(self.fix.longitude, 'WE'))
            row('Altitude', self.fix.mode == MODE_3D and "%f m" %
                self.fix.altitude or 'N/A')
            row('Speed', isfinite(self.fix.speed) and "%f m/s" %
                self.fix.speed or 'N/A')
            row('Course', isfinite(self.fix.track) and "%f°" %
                self.fix.track or 'N/A')
        else:
            row('Latitude', 'N/A')
            row('Longitude', 'N/A')
            row('Altitude', 'N/A')
            row('Speed', 'N/A')
            row('Course', 'N/A')

        row('EPX', isfinite(self.fix.epx) and "%f m" % self.fix.epx or 'N/A')
        row('EPY', isfinite(self.fix.epy) and "%f m" % self.fix.epy or 'N/A')
        row('EPV', isfinite(self.fix.epv) and "%f m" % self.fix.epv or 'N/A')
        row('Climb', self.fix.mode == MODE_3D and isfinite(self.fix.climb) and
            "%f m/s" % self.fix.climb or 'N/A')

        state = "INIT"
        if not (self.valid & ONLINE_SET):
            newstate = 0
            state = "OFFLINE"
        else:
            newstate = self.fix.mode
            if newstate == MODE_2D:
                state = "2D FIX"
            elif newstate == MODE_3D:
                state = "3D FIX"
            else:
                state = "NO FIX"
        if newstate != self.state:
            self.statetimer = time.time()
            self.state = newstate
        row('State', "%s (%d secs)" % (state, time.time() - self.statetimer))

        fh.write("""\t\t\t\t</table>
\t\t\t</td>
\t\t\t<td>
\t\t\t\t<canvas id=satview width=425 height=425>
\t\t\t\t\t<p>Your browser needs HTML5 &lt;canvas> support to display
 the satellite view correctly.</p>
\t\t\t\t</canvas>
\t\t\t\t<script type='text/javascript'>draw_satview();</script>
\t\t\t</td>
\t\t</tr>
\t</table>
</body>
</html>
""")

    def js(self, fh):
        fh.write("""// draw the satellite view

function draw_satview() {
    var c = document.getElementById('satview');
    if (!c.getContext) return;
    var ctx = c.getContext('2d');
    if (!ctx) return;

    var circle = Math.PI * 2,
        M = function (x, y) { ctx.moveTo(x, y); },
        L = function (x, y) { ctx.lineTo(x, y); };

    ctx.save();
    ctx.clearRect(0, 0, c.width, c.height);
    ctx.translate(210, 210);

    // grid and labels
    ctx.strokeStyle = 'black';
    ctx.beginPath();
    ctx.arc(0, 0, 200, 0, circle, 0);
    ctx.stroke();

    ctx.beginPath();
    ctx.strokeText('N', -4, -202);
    ctx.strokeText('W', -210, 4);
    ctx.strokeText('E', 202, 4);
    ctx.strokeText('S', -4, 210);

    ctx.strokeStyle = 'grey';
    ctx.beginPath();
    ctx.arc(0, 0, 100, 0, circle, 0);
    M(2, 0);
    ctx.arc(0, 0,   2, 0, circle, 0);
    ctx.stroke();

    ctx.strokeStyle = 'lightgrey';
    ctx.save();
    ctx.beginPath();
    M(0, -200); L(0, 200); ctx.rotate(circle / 8);
    M(0, -200); L(0, 200); ctx.rotate(circle / 8);
    M(0, -200); L(0, 200); ctx.rotate(circle / 8);
    M(0, -200); L(0, 200);
    ctx.stroke();
    ctx.restore();

    // tracks
    ctx.lineWidth = 0.6;
    ctx.strokeStyle = 'red';
""")

        # Draw the tracks
        for t in self.sattrack.values():
            if t.posn:
                fh.write("    ctx.globalAlpha = %s; ctx.beginPath(); "
                         "%sctx.stroke();\n" %
                         (t.stale == 0 and '0.66' or '1', t.track()))

        fh.write("""
    // satellites
    ctx.lineWidth = 1;
    ctx.strokeStyle = 'black';
""")

        # Draw the satellites
        for s in self.satellites:
            el, az = s.elevation, s.azimuth
            if el == 0 and az == 0:
                continue  # Skip satellites with unknown position
            x, y = polartocart(el, az)
            fill = not s.used and 'lightgrey' or \
                s.ss < 30 and 'red' or \
                s.ss < 35 and 'yellow' or \
                s.ss < 40 and 'green' or 'lime'

            # Center PRNs in the marker
            offset = s.PRN < 10 and 3 or s.PRN >= 100 and -3 or 0

            fh.write("    ctx.beginPath(); ctx.fillStyle = '%s'; " % fill)
            if s.PRN > 32:      # Draw a square for SBAS satellites
                fh.write("ctx.rect(%d, %d, 16, 16); " % (x - 8, y - 8))
            else:
                fh.write("ctx.arc(%d, %d, 8, 0, circle, 0); " % (x, y))
            fh.write("ctx.fill(); ctx.stroke(); "
                     "ctx.strokeText('%s', %d, %d);\n" %
                     (s.PRN, x - 6 + offset, y + 4))

        fh.write("""
    ctx.restore();
}
""")

    def make_stale(self):
        for t in self.sattrack.values():
            if t.stale:
                t.stale -= 1

    def delete_stale(self):
        stales = []
        for prn in self.sattrack.keys():
            if self.sattrack[prn].stale == 0:
                stales.append(prn)
                self.needsupdate = 1
        for prn in stales:
            del self.sattrack[prn]

    def insert_sat(self, prn, x, y):
        try:
            t = self.sattrack[prn]
        except KeyError:
            self.sattrack[prn] = t = Track(prn)
        if t.add(x, y):
            self.needsupdate = 1

    def update_tracks(self):
        self.make_stale()
        for s in self.satellites:
            x, y = polartocart(s.elevation, s.azimuth)
            self.insert_sat(s.PRN, x, y)
        self.delete_stale()

    def generate_html(self, htmlfile, jsfile):
        fh = open(htmlfile, 'w')
        self.html(fh, jsfile)
        fh.close()

    def generate_js(self, jsfile):
        fh = open(jsfile, 'w')
        self.js(fh)
        fh.close()

    def run(self, suffix, period):
        jsfile = 'gpsd' + suffix + '.js'
        htmlfile = 'gpsd' + suffix + '.html'
        if period is not None:
            end = time.time() + period
        self.needsupdate = 1
        self.stream(WATCH_ENABLE | WATCH_NEWSTYLE)
        for report in self:
            if report['class'] not in ('TPV', 'SKY'):
                continue
            self.update_tracks()
            if self.needsupdate:
                self.generate_js(jsfile)
                self.needsupdate = 0
            self.generate_html(htmlfile, jsfile)
            if period is not None and (
                period <= 0 and self.fix.mode >= MODE_2D or
                period > 0 and time.time() > end
            ):
                break


def main():
    argv = sys.argv[1:]

    factors = {
        's': 1, 'm': 60, 'h': 60 * 60, 'd': 24 * 60 * 60
    }
    arg = argv and argv[0] or '0'
    if arg[-1:] in factors.keys():
        period = int(arg[:-1]) * factors[arg[-1]]
    elif arg == 'c':
        period = None
    else:
        period = int(arg)
    prefix = '-' + arg

    sat = SatTracks()

    # restore the tracks
    pfile = 'tracks.p'
    if os.path.isfile(pfile):
        p = open(pfile, 'rb')
        try:
            sat.sattrack = pickle.load(p)
        except ValueError:
            print("Ignoring incompatible tracks file.", file=sys.stderr)
        p.close()

    try:
        sat.run(prefix, period)
    except KeyboardInterrupt:
        # save the tracks
        p = open(pfile, 'wb')
        # No protocol is backward-compatible from Python 3 to Python 2,
        # so we just use the default and punt at load time if needed.
        pickle.dump(sat.sattrack, p)
        p.close()


if __name__ == '__main__':
    main()
