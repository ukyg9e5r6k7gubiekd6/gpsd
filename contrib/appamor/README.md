## Apparmor profile for gpsd

[AppArmor](https://en.wikipedia.org/wiki/AppArmor "wikipedia:AppArmor")
is a [Mandatory Access Control](https://wiki.archlinux.org/index.php/Mandatory_Access_Control "Mandatory Access Control")
(MAC) system, implemented upon the
[Linux Security Modules](https://en.wikipedia.org/wiki/Linux_Security_Modules "wikipedia:Linux Security Modules") (LSM).

Distributions using Apparmor are Debian, Ubuntu, various derivates, SuSE
and various others. Please note that RedHat, CentOS and Fedora are using
SELinux, which does obviously not use Apparmor profiles.

The file *usr.sbin.gpsd* is the apparmor profile created by the
Ubuntu/Cacnonical developers for the usage in Debian and Ubuntu. If you
want to use it, you need

 - an apparmor installation. Please consult the documentation of your
distribution, some useful documentation can be found in
[Debian Wiki](https://wiki.debian.org/AppArmor/HowToUse) and
[Arch Wiki](https://wiki.archlinux.org/index.php/AppArmor). Make sure
apparmor in general is working as expected before adding new profiles.
 - to copy the file to */etc/apparmor.d* and name it according to the
location of your gpsd binary. The proper name should be the output of
`command -v gpsd | sed 's,^/,,;s,/,.,g'`
 - to edit your copied file, at least you need to fix the location of
gpsd if necessary, also check if the rules apply to your installation or
if you want to limit them (for example: if you have one fixed serial device
you might now want to allow gpsd to talk to all tty devices).

Before enforcing your new profile you might want to run it in complain
mode: `aa-complain /etc/apparmor.d/your.new.profile`. Give it a good
testing, if everything works as expected, put it into enforce mode:
`aa-enforce /etc/apparmor.d/your.new.profile`
