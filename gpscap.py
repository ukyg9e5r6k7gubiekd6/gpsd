"""

gpscap - GPS capability dictionary class.

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
<tr>
<th>Name</th>
<th>Packaging</th>
<th>Engine</th>
<th>Interface</th>
<th>Tested with</th>
<th>NMEA version</th>
<th width='50%'>Notes</th>
</tr>
"""
        vhead = "<tr><td style='text-align:center;' colspan='7'><a href='%s'>%s</a></td></tr>\n"
        ofp.write(thead)
        for vendor in self.vendors:
            ofp.write(vhead % (self.get(vendor, "vendor_site"), vendor))
            relevant = []
            for dev in self.devices:
                if self.get(dev, "vendor") == vendor:
                    relevant.append(dev)
            relevant.sort()
            for dev in relevant:
                rowcolor = "#FFFFFF"
                if self.has_option(dev, "broken"):
                    rowcolor = "Pink"
                elif self.get(dev, "packaging") == "OEM module":
                    rowcolor = "LimeGreen"
                elif self.get(dev, "packaging") == "chipset":
                    rowcolor = "LightYellow"
                elif self.get(dev, "packaging") == "handset":
                    rowcolor = "Cyan"
                elif self.get(dev, "packaging") == "car mount":
                    rowcolor = "DarkCyan"

                ofp.write("<tr bgcolor='%s'>\n" % rowcolor)
                ofp.write("<td>%s</td>\n" % dev)
                ofp.write("<td>%s</td>\n" % self.get(dev, "packaging"))
                engine = self.get(dev, "engine")
                if self.has_option(engine, "reference"):
                    engine = "<a href='%s'>%s</a>" % (self.get(engine, "reference"), engine)
                if self.has_option(dev, "subtype"):
                    engine += " (" + self.get(dev, "subtype") + ")"
                ofp.write("<td>%s</td>\n" % engine)
                ofp.write("<td>%s</td>\n" % self.get(dev, "interfaces"))
                tested = ""
                if self.get(dev, "status") == "broken":
                    tested = "Broken"
                elif self.get(dev, "tested") == "regression":
                    tested = "*"
                else:
                    tested = self.get(dev, "tested")
                ofp.write("<td>%s</td>\n" % tested)
                nmea = "&nbsp;"
                if self.has_option(dev, "nmea"):
                    nmea = self.get(dev, "nmea")
                ofp.write("<td>%s</td>\n" % nmea)
                ofp.write("<td>%s</td>\n" % self.get(dev, "notes"))
                ofp.write("</tr>\n")
        ofp.write("</table>\n")


if __name__ == "__main__":
    import sys
    d = GPSDictionary()
    d.HTMLDump(sys.stdout)
