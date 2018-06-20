"""
gpscap - GPS/AIS capability dictionary class.

This file is Copyright (c) 2010 by the GPSD project
SPDX-License-Identifier: BSD-2-clause
"""
# This code runs compatibly under Python 2 and 3.x for x >= 2.
# Preserve this property!
from __future__ import absolute_import, print_function, division

try:
    import configparser
except ImportError:
    import ConfigParser as configparser


class GPSDictionary(configparser.RawConfigParser):
    def __init__(self, *files):
        "Initialize the capability dictionary"
        configparser.RawConfigParser.__init__(self)
        if not files:
            files = ["gpscap.ini", "/usr/share/gpsd/gpscap.ini"]
        try:
            self.read(files, encoding='utf-8')
        except TypeError:
            self.read(files)  # For Python 2.6
        # Resolve uses= members
        while True:
            keepgoing = False
            for section in self.sections():
                if self.has_option(section, "uses"):
                    parent = self.get(section, "uses")
                    if self.has_option(parent, "uses"):
                        continue
                    # Found a parent section without a uses = part.
                    for heritable in self.options(parent):
                        if not self.has_option(section, heritable):
                            self.set(section,
                                     heritable,
                                     self.get(parent, heritable))
                            keepgoing = True
                    self.remove_option(section, "uses")
            if not keepgoing:
                break
        # Sanity check: All items must have a type field.
        for section in self.sections():
            if not self.has_option(section, "type"):
                raise configparser.Error("%s has no type" % section)
            elif (self.get(section, "type")
                  not in ("engine", "vendor", "device")):
                raise configparser.Error("%s has invalid type" % section)
        # Sanity check: All devices must point at a vendor object.
        # Side effect: build the lists of vendors and devices.
        self.vendors = []
        self.devices = []
        for section in self.sections():
            if self.get(section, "type") == "vendor":
                self.vendors.append(section)
            if self.get(section, "type") == "device":
                self.devices.append(section)
        self.vendors.sort()
        for section in self.sections():
            if self.get(section, "type") == "device":
                if not self.has_option(section, "vendor"):
                    raise configparser.Error("%s has no vendor" % section)
                if self.get(section, "vendor") not in self.vendors:
                    raise configparser.Error("%s has invalid vendor" % section)

    def HTMLDump(self, ofp):
        thead = """
<table style='border:1px solid gray;font-size:small;background-color:#CCCCCC'>
<caption>Listing %s devices from %s vendors</caption>
<tr>
<th>Name</th>
<th>Packaging</th>
<th>Engine</th>
<th>Interface</th>
<th>Tested with</th>
<th>NMEA version</th>
<th>PPS</th>
<th style='width:50%%'>Notes</th>
</tr>
"""
        vhead1 = "<tr><td style='text-align:center;' colspan='8'>" \
                 "<a href='%s'>%s</a></td></tr>\n"
        vhead2 = "<tr><td style='text-align:center;' colspan='8'>" \
                 "<a href='%s'>%s</a><br><p>%s</p></td></tr>\n"
        hotpluggables = ("pl2303", "CP2101")
        ofp.write(thead % (len(self.devices), len(self.vendors)))
        for vendor in self.vendors:
            if self.has_option(vendor, "notes"):
                ofp.write(vhead2 % (self.get(vendor, "vendor_site"), vendor,
                                    self.get(vendor, "notes")))
            else:
                ofp.write(vhead1 % (self.get(vendor, "vendor_site"), vendor))
            relevant = []
            for dev in self.devices:
                if self.get(dev, "vendor") == vendor:
                    relevant.append(dev)
            relevant.sort()
            for dev in relevant:
                rowcolor = "white"
                if self.get(dev, "packaging") == "OEM module":
                    rowcolor = "#32CD32"
                elif self.get(dev, "packaging") == "chipset":
                    rowcolor = "#FFFFE0"
                elif self.get(dev, "packaging") == "handset":
                    rowcolor = "#00FFFF"
                elif self.get(dev, "packaging") == "hansdfree":
                    rowcolor = "#008B8B"

                ofp.write("<tr itemscope itemtype='http://schema.org/Product'"
                          " style='background-color:%s'>\n" % rowcolor)
                namefield = dev
                if self.has_option(dev, "techdoc"):
                    namefield = "<a href='%s'>%s</a>" \
                        % (self.get(dev, "techdoc"), dev)
                if ((self.has_option(dev, "discontinued") and
                     self.getboolean(dev, "discontinued"))):
                    namefield = namefield + "&nbsp;<img title='Device " \
                        "discontinued' src='discontinued.png' " \
                        "alt='Discontinued icon'>"
                ofp.write("<td itemprop='name'>%s</td>\n" % namefield)
                ofp.write("<td>%s</td>\n" % self.get(dev, "packaging"))
                engine = self.get(dev, "engine")
                if self.has_option(engine, "techdoc"):
                    engine = "<a href='%s'>%s</a>" \
                        % (self.get(engine, "techdoc"), engine)
                if self.has_option(dev, "subtype"):
                    engine += " (" + self.get(dev, "subtype") + ")"
                ofp.write("<td>%s</td>\n" % engine)
                interfaces = self.get(dev, "interfaces")
                if self.has_option(dev, "pps"):
                    interfaces += ",PPS"
                ofp.write("<td>%s</td>\n" % interfaces)
                testfield = ""
                if self.has_option(dev, "tested"):
                    tested = self.get(dev, "tested")
                    if tested == "regression":
                        testfield += "<img title='Have regression test' " \
                                     "src='regression.png' " \
                                     "alt='Regression-test icon'>"
                    else:
                        testfield += tested
                if ((self.has_option(dev, "configurable") and
                     self.get(dev, "configurable") == 'insane')):
                    testfield += "<img title='Requires -b option' " \
                                 "src='noconfigure.png' " \
                                 "alt='No-configure icon'>"
                if self.get(dev, "rating") == "excellent":
                    testfield += "<img src='star.png' alt='Star icon'>" \
                                 "<img src='star.png' alt='Star icon'>" \
                                 "<img src='star.png' alt='Star icon'>" \
                                 "<img src='star.png' alt='Star icon'>"
                elif self.get(dev, "rating") == "good":
                    testfield += "<img src='star.png' alt='Star icon'>" \
                                 "<img src='star.png' alt='Star icon'>" \
                                 "<img src='star.png' alt='Star icon'>"
                elif self.get(dev, "rating") == "fair":
                    testfield += "<img src='star.png' alt='Star icon'>" \
                                 "<img src='star.png' alt='Star icon'>"
                elif self.get(dev, "rating") == "poor":
                    testfield += "<img src='star.png' alt='Star icon'>"
                elif self.get(dev, "rating") == "broken":
                    testfield += "<img title='Device is broken' " \
                                 "src='bomb.png' alt='Bomb icon'>"
                if ((self.has_option(dev, "usbchip") and
                     self.get(dev, "usbchip") in hotpluggables)):
                    testfield += "<img title='udev hotplug' " \
                                 "src='hotplug.png' alt='Hotplug icon'>"
                ofp.write("<td>%s</td>\n" % testfield)
                nmea = "&nbsp;"
                if self.has_option(dev, "nmea"):
                    nmea = self.get(dev, "nmea")
                ofp.write("<td>%s</td>\n" % nmea)
                if ((self.has_option(dev, "pps") and
                     self.get(dev, "pps") == "True")):
                    pps_accuracy = time_offset = ""
                    if self.has_option(dev, "pps_accuracy"):
                        pps_accuracy = self.get(dev, "pps_accuracy")
                    if self.has_option(dev, "time_offset"):
                        time_offset = self.get(dev, "time_offset")
                    if pps_accuracy and time_offset:
                        ofp.write("<td>%s<br>%s</td>\n"
                                  % (pps_accuracy, time_offset))
                    else:
                        ofp.write("<td>?<br>\n")
                else:
                    ofp.write("<td>No</td>\n")
                if self.has_option(dev, "notes"):
                    notes = self.get(dev, "notes")
                else:
                    notes = ""
                if self.has_option(dev, "submitter"):
                    notes += " Reported by %s." % self.get(
                        dev, "submitter").replace("@", "&#x40;").replace(
                        "<", "&lt;").replace(">", "&gt;")
                ofp.write("<td itemscope itemtype='http://schema.org/"
                          "description'>%s</td>\n" % notes)
                ofp.write("</tr>\n")
        ofp.write("</table>\n")


if __name__ == "__main__":
    import sys
    try:
        d = GPSDictionary()
        d.HTMLDump(sys.stdout)
    except configparser.Error as e:
        sys.stderr.write(sys.argv[0] + ":%s\n" % e)
        raise SystemExit(1)
