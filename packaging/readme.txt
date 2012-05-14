= Recommendations for distribution integrators =

The X11 subdirectory contains icons and a project logo for use
in desktop packaging.

Usable deb and RPM specifications have their own subdirectories here.
Our package files want to set up a hotplug script to notify gpsd
when a potential GPS device goes active and should be polled.  The
goal is zero configuration; users should never have to tell gpsd how
to configure itself.

Bluetooth has a requirement to be able to write to the gpsd control
socket from a userland device manager.  Accordingly, you probably 
want to set up a gpsd privilege group and make sure the Bluetooth
device manager is in it.

== The chrpath perplex ==
 
Some distribution makers have considered the use of chrpath to be a
wart on the build recipe.

Here's the problem.  I want to build build binaries that (a) link
dynamically, (b) can be tested in the build directory without
installing to system space (in particular, so I can run the regression
tests without disturbing a production installation) and (c)
won't carry a potential exploit into system space when the binaries
are installed.

The potential exploit is the remnant presence of the build directory in
the binary's internal list of places it will look for shared libraries.
We need that to be there for testing purposes, but we want it gone
in the version of the binary that's copied to /usr/lib.  Otherwise
there are threat scenarios with a maliciously crafted library.

Without chrpath I can get any two of those three, but I can't get
all three. If I choose static linking I get (b) and (c), if I choose
dynamic linking without chrpath I get (a) and (b).
