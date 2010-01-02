"""

gpscap - GPS/AIS capability dictionary class.

"""
import ConfigParser

class GPSDictionary(ConfigParser.RawConfigParser):
    def __init__(self, *files):
        "Initialize the capability dictionary"
        ConfigParser.RawConfigParser.__init__(self)
        if not files:
            files = ["gpscap.ini", "/usr/share/gpsd/gpscap.ini"]
        self.read(files)
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
                raise ConfigParser.Error("%s has no type" % section)
            elif self.get(section, "type") not in ("engine", "vendor", "device"):
                raise ConfigParser.Error("%s has invalid type" % section)
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
                    raise ConfigParser.Error("%s has no vendor" % section)
                if self.get(section, "vendor") not in self.vendors:
                    raise ConfigParser.Error("%s has invalid vendor" % section)

    def HTMLDump(self, ofp):
        thead = """<table border='1' style='font-size:small;' bgcolor='#CCCCCC'>
<caption>Listing %s devices from %s vendors</caption>
<tr>
<th>Name</th>
<th>Packaging</th>
<th>Engine</th>
<th>Interface</th>
<th>Tested with</th>
<th>NMEA version</th>
<th width='50%%'>Notes</th>
</tr>
"""
        vhead = "<tr><td style='text-align:center;' colspan='7'><a href='%s'>%s</a></td></tr>\n"
        hotpluggables = ("pl2303", "CP2101")
        ofp.write(thead % (len(self.devices), len(self.vendors)))
        for vendor in self.vendors:
            ofp.write(vhead % (self.get(vendor, "vendor_site"), vendor))
            relevant = []
            for dev in self.devices:
                if self.get(dev, "vendor") == vendor:
                    relevant.append(dev)
            relevant.sort()
            for dev in relevant:
                rowcolor = "#FFFFFF"
                if self.get(dev, "packaging") == "OEM module":
                    rowcolor = "LimeGreen"
                elif self.get(dev, "packaging") == "chipset":
                    rowcolor = "LightYellow"
                elif self.get(dev, "packaging") == "handset":
                    rowcolor = "Cyan"
                elif self.get(dev, "packaging") == "hansdfree":
                    rowcolor = "DarkCyan"

                ofp.write("<tr bgcolor='%s'>\n" % rowcolor)
                namefield = dev
                if self.has_option(dev, "techdoc"):
                    namefield = "<a href='%s'>%s</a>" % (self.get(dev, "techdoc"), dev)
                if self.has_option(dev, "discontinued"):
                    namefield = namefield + "&nbsp;<img title='Device discontinued' src='discontinued.png'/>"
                ofp.write("<td>%s</td>\n" % namefield)
                ofp.write("<td>%s</td>\n" % self.get(dev, "packaging"))
                engine = self.get(dev, "engine")
                if self.has_option(engine, "techdoc"):
                    engine = "<a href='%s'>%s</a>" % (self.get(engine, "techdoc"), engine)
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
                        testfield += "<img title='Have regression test' src='regression.png'>"
                    else:
                        testfield += tested
                if self.has_option(dev, "noconfigure"):
                    testfield += "<img title='Requires -b option' src='noconfigure.png'>"
                if self.get(dev, "rating") == "excellent":
                    testfield += "<img src='star.png'/><img src='star.png'/><img src='star.png'/><img src='star.png'/>"
                elif self.get(dev, "rating") == "good":
                    testfield += "<img src='star.png'/><img src='star.png'/'><img src='star.png'/>"
                elif self.get(dev, "rating") == "fair":
                    testfield += "<img src='star.png'/><img src='star.png'/>"
                elif self.get(dev, "rating") == "poor":
                    testfield += "<img src='star.png'/>"
                elif self.get(dev, "rating") == "broken":
                    testfield += "<img title='Device is broken' src='bomb.png'/>"
                if self.has_option(dev, "usbchip") and self.get(dev, "usbchip") in hotpluggables:
                    testfield += "<img src='hotplug.png'/>"
                ofp.write("<td>%s</td>\n" % testfield)
                nmea = "&nbsp;"
                if self.has_option(dev, "nmea"):
                    nmea = self.get(dev, "nmea")
                ofp.write("<td>%s</td>\n" % nmea)
                if self.has_option(dev, "notes"):
                    notes = self.get(dev, "notes")
                else:
                    notes = ""
                if self.has_option(dev, "submitter"):
                    notes += " Reported by %s." % self.get(dev, "submitter")
                notes = notes.replace("@", "&#x40;").replace("<", "&lt;").replace(">", "&gt;")
                ofp.write("<td>%s</td>\n" % notes)
                ofp.write("</tr>\n")
        ofp.write("</table>\n")


if __name__ == "__main__":
    import sys
    d = GPSDictionary()
    d.HTMLDump(sys.stdout)
