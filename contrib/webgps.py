#!/usr/bin/env python
# encoding: utf-8

# webgps.py
#
# This is a Python port of webgps.c from http://www.wireless.org.au/~jhecker/gpsd/
# by Beat Bolli <me+gps@drbeat.li>
#

import time, calendar, math, socket, sys, os, select, pickle
from gps import *

TRACKMAX = 1024
STALECOUNT = 10

DIAMETER = 200
XYOFFSET = 10

def polartocart(el, az):
    radius = DIAMETER * (1 - el / 90.0) # * math.cos(Deg2Rad(float(el)))
    theta = Deg2Rad(float(az - 90))
    return (
        int(radius * math.cos(theta) + 0.5) + DIAMETER + XYOFFSET,
        int(radius * math.sin(theta) + 0.5) + DIAMETER + XYOFFSET
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
            #print self.prn, self.posn
            return 1
        return 0

class SatTracks(gps):
    '''gpsd client writing HTML and SVG output.'''

    def __init__(self):
        gps.__init__(self)
        self.sattrack = {}      # maps PRNs to Tracks
        self.state = None
        self.statetimer = time.time()
        self.needsupdate = 0

    def html(self, svgfile):
        self.fh.write("""<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML Transitional 1.0//EN"
\t"http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd">

<html xmlns="http://www.w3.org/1999/xhtml">
<head>
\t<meta http-equiv="Refresh" content="300" />
\t<title>GPSD Satellite Positions and Readings</title>
\t<style type="text/css"><!--
\t\t.num td { text-align: right; }
\t\tth { text-align: left; }
\t--></style>
</head>
<body>
\t<table border="1">
\t\t<tr>
\t\t\t<td>
\t\t\t\t<table border="0" class="num">
\t\t\t\t\t<tr><th>PRN:</th><th>Elev:</th><th>Azim:</th><th>SNR:</th><th>Used:</th></tr>
""")

        sats = self.satellites[:]
        sats.sort(lambda a, b: a.PRN - b.PRN)
        for s in sats:
            self.fh.write("\t\t\t\t\t<tr><td>%d</td><td>%d</td><td>%d</td><td>%d</td><td>%s</td></tr>\n" % (
                s.PRN, s.elevation, s.azimuth, s.ss, s.used and 'Y' or 'N'
            ))

	self.fh.write("\t\t\t\t</table>\n\t\t\t\t<table border=\"0\">\n")

	def row(l, v):
	    self.fh.write("\t\t\t\t\t<tr><th>%s:</th><td>%s</td></tr>\n" % (l, v))

	def deg_to_str(a, hemi):
	    return '%.6f %c' % (abs(a), hemi[a < 0])

	row('Time', self.utc or 'N/A')

	if self.fix.mode >= MODE_2D:
	    row('Latitude', deg_to_str(self.fix.latitude, 'SN'))
	    row('Longitude', deg_to_str(self.fix.longitude, 'WE'))
	    row('Altitude', self.fix.mode == MODE_3D and "%f m" % self.fix.altitude or 'N/A')
	    row('Speed', not isnan(self.fix.speed) and "%f m/s" % self.fix.speed or 'N/A')
	    row('Course', not isnan(self.fix.track) and "%f°" % self.fix.track or 'N/A')
	else:
	    row('Latitude', 'N/A')
	    row('Longitude', 'N/A')
	    row('Altitude', 'N/A')
	    row('Speed', 'N/A')
	    row('Course', 'N/A')

	row('EPX', not isnan(self.fix.epx) and "%f m" % self.fix.epx or 'N/A')
	row('EPY', not isnan(self.fix.epy) and "%f m" % self.fix.epy or 'N/A')
	row('EPV', not isnan(self.fix.epv) and "%f m" % self.fix.epv or 'N/A')
	row('Climb', self.fix.mode == MODE_3D and not isnan(self.fix.climb) and
	    "%f m/s" % self.fix.climb or 'N/A'
	)

	if not (self.valid & ONLINE_SET):
	    newstate = 0
	    state = "OFFLINE"
	else:
	    newstate = self.fix.mode
	    if newstate == MODE_2D:
                state = self.status == STATUS_DGPS_FIX and "2D DIFF FIX" or "2D FIX"
	    elif newstate == MODE_3D:
		state = self.status == STATUS_DGPS_FIX and "3D DIFF FIX" or "3D FIX"
	    else:
		state = "NO FIX"
	if newstate != self.state:
	    self.statetimer = time.time()
	    self.state = newstate
	row('State', state + " (%d secs)" % (time.time() - self.statetimer))

	self.fh.write("\t\t\t\t</table>\n\t\t\t</td>\n")

	# SVG stuff
	self.fh.write("\t\t\t<td>\n\t\t\t\t<object data=\"%s\" \
width=\"425\" height=\"425\" type=\"image/svg+xml\" />\n\
\t\t\t</td>\n\t\t</tr>\n" % svgfile)

	self.fh.write("\t</table>\n</body>\n</html>\n")

    def svg(self):
        self.fh.write("""<?xml version="1.0" standalone="no"?>
<!DOCTYPE svg PUBLIC "-//W3C//DTD SVG 1.1//EN"
        "http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd">
<svg width="100%" height="100%" version="1.1" xmlns="http://www.w3.org/2000/svg">
\t<g transform="translate(0,0)">
\t\t<circle cx="210" cy="210" r="200" stroke="black" stroke-width="1" fill="white"/>
\t\t<circle cx="210" cy="210" r="100" stroke="grey" stroke-width="1" fill="white"/>
\t\t<circle cx="210" cy="210" r="2" stroke="grey" stroke-width="1" fill="white"/>
\t\t<line x1="210" y1="10" x2="210" y2="410" stroke="lightgrey"/>
\t\t<line x1="10" y1="210" x2="410" y2="210" stroke="lightgrey"/>
\t\t<line x1="68.578644" y1="68.578644" x2="351.42136" y2="351.42136" stroke="lightgrey"/>
\t\t<line x1="68.578644" y1="351.42136" x2="351.42136" y2="68.578644" stroke="lightgrey"/>
\t\t<g font-size="10" stroke="black" stroke-width="0.5">
\t\t\t<text x="206" y="8">N</text>
\t\t\t<text x="0" y="214">W</text>
\t\t\t<text x="412" y="214">E</text>
\t\t\t<text x="206" y="420">S</text>
\t\t</g>
"""
        )

        # Draw the tracks
        self.fh.write('\t\t<g stroke-width="0.6" stroke="red" fill="none">\n')
        for t in self.sattrack.values():
            if t.posn:
                self.fh.write('\t\t\t<polyline points="%s"%s/>\n' % (
                    ' '.join(['%d,%d' % p for p in t.posn]), t.stale == 0 and ' opacity=".33"' or ''
                ))
        self.fh.write('\t\t</g>\n')

        # Draw the satellites
        self.fh.write('\t\t<g stroke-width="1" stroke="black" fill="black" font-size="10">\n')
        for s in self.satellites:
            x, y = polartocart(s.elevation, s.azimuth)
            fill = s.ss < 30 and 'red' or s.ss < 35 and 'yellow' or s.ss < 40 and 'green' or 'lime'
            opaque = not s.used and ' opacity=".33"' or ''

            # Center PRNs in the marker
            offset = s.PRN < 10 and 3 or s.PRN >= 100 and -3 or 0

            if s.PRN > 32:      # draw a diamond for SBAS satellites
                self.fh.write(
                    '\t\t\t<path d="M%d %d l-8 -8 -8 8 8 8 8 -8" fill="%s"%s/>\n' %
                    (x + 8, y, fill, opaque)
                )
            else:
                self.fh.write(
                    '\t\t\t<circle cx="%d" cy="%d" r="8" fill="%s"%s/>\n' %
                    (x, y, fill, opaque)
                )
            self.fh.write('\t\t\t<text x="%d" y="%d">%d</text>\n' % (x - 6 + offset, y + 4, s.PRN))

        self.fh.write('\t\t</g>\n\t</g>\n</svg>\n')

    def make_stale(self):
        for t in self.sattrack.values():
            if t.stale:
                t.stale -= 1

    def delete_stale(self):
        for prn in self.sattrack.keys():
            if self.sattrack[prn].stale == 0:
                del self.sattrack[prn]
                self.needsupdate = 1

    def insert_sat(self, prn, x, y):
        try:
            t = self.sattrack[prn]
        except KeyError:
            self.sattrack[prn] = t = Track(prn)
        self.needsupdate += t.add(x, y)

    def update_tracks(self):
        self.make_stale()
        for s in self.satellites:
            x, y = polartocart(s.elevation, s.azimuth)
            if self.insert_sat(s.PRN, x, y):
                self.needsupdate = 1
        self.delete_stale()

    def generate_html(self, htmlfile, svgfile):
        self.fh = open(htmlfile, 'w')
        self.html(svgfile)
        self.fh.close()

    def generate_svg(self, svgfile):
        self.fh = open(svgfile, 'w')
        self.svg()
        self.fh.close()

    def run(self, period):
	end = time.time() + period
	self.stream(WATCH_ENABLE | WATCH_NEWSTYLE)
	for report in self:
	    if report['class'] not in ('TPV', 'SKY'):
		continue
	    self.needsupdate = 0
	    self.update_tracks()
	    self.generate_html('gpsd.html', 'gpsd.svg')
	    if self.needsupdate:
		self.generate_svg('gpsd.svg')
		if period <= 0 and not isnan(self.fix.time):
		    break
	    if period > 0 and time.time() > end:
		break

def main():
    argv = sys.argv[1:]

    period = argv and argv[0] or '0'
    if period[-1:] in 'smhd':
	period = int(period[:-1]) * {'s': 1, 'm': 60, 'h': 60*60, 'd': 24*60*60}[period[-1]]
    else:
	period = int(period)

    sat = SatTracks()

    # restore the tracks
    pfile = 'tracks.p'
    if os.path.isfile(pfile):
        p = open(pfile)
        sat.sattrack = pickle.load(p)
        p.close()

    try:
        sat.run(period)
    except KeyboardInterrupt:
        # save the tracks
        p = open(pfile, 'w')
        pickle.dump(sat.sattrack, p)
        p.close()

if __name__ == '__main__':
    main()
