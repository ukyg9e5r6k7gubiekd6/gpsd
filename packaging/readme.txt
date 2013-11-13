= Recommendations for distribution integrators =

The X11 subdirectory contains icons and a project logo for use
in desktop packaging.

Usable deb and RPM specifications have their own subdirectories here.
Our package files want to set up a hotplug script to notify gpsd
when a potential GPS device goes active and should be polled.  The
goal is zero configuration; users should *never* have to tell gpsd how
to set itself up.

Bluetooth has a requirement to be able to write to the gpsd control
socket from a userland device manager.  Accordingly, you probably 
want to set up a gpsd privilege group and make sure the Bluetooth
device manager is in it.

To avoid problems with gpsd not starting up properly when devices are
hotplugged, make sure the installed gpsd will have read and write
permissions on all serial devices that a GPS might be connected to (on
Linux, this means at least /dev/ttyS*, /dev/ttyUSB*, and
/dev/ttyACM*).

The gpsd daemon needs to be started as root for best performance (it
wants to nice itself, and needs root access to kernel PPS devices).
But very soon after startup it drops privileges.  gpsd normally
figures out which group it should move to by looking at the ownership
of a prototypical tty (look in gpsd.c for this code) but the owning
user and group can be compiled in with build-system options.

Make *sure* whatever group gpsd lands in after privilege-dropping has
dialout access - otherwise your users will see mysterious failures
which they will wrongly attribute to GPSD itself.

// end
