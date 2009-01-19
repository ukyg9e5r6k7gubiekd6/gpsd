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

if __name__ == "__main__":
    d = GPSDictionary()
