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
        # Side effect: build the list of vendors.
        self.vendors = []
        for section in self.sections():
            if self.get(section, "type") == "vendor":
                self.vendors.append(section)
        self.vendors.sort()
        for section in self.sections():
            if self.get(section, "type") == "device":
                if not self.has_option(section, "vendor"):
                    raise ConfigParser.Error("%s has no vendor" % section)
                if self.get(section, "vendor") not in self.vendors:
                    raise ConfigParser.Error("%s has invalid vendor" % section)
if __name__ == "__main__":
    d = GPSDictionary()
