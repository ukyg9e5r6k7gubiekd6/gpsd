Summary: service daemon for mediating access to a GPS
Name: gpsd
Version: 1.93
Release: 1
License: GPL
Group: System Environment/Daemons
URL: http://berlios.de/gpsd/
Source0: %{name}-%{version}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root
#Destinations: mailto:gpsd-announce@lists.berlios.de, mailto:gpsd-users@lists.berlios.de, mailto:gpsd-dev@lists.berlios.de

%description 
gpsd is a service daemon that mediates access to a GPS sensor
connected to the host computer by serial or USB interface, making its
data on the location/course/velocity of the sensor available to be
queried on TCP port 2947 of the host computer.  With gpsd, multiple
GPS client applications (such as navigational and wardriving software) 
can share access to a GPS without contention or loss of data.  Also,
gpsd responds to queries with a format that is substantially easier
to parse than NMEA 0183.  A library that manages access to gpsd for
an application is included.

%prep
%setup -q

%build
configure --prefix=/usr
make %{?_smp_mflags} gpsd gpsd.1 libgps.a libgps.3

%install
rm -rf $RPM_BUILD_ROOT
mkdir -p "$RPM_BUILD_ROOT"%{_bindir}
mkdir -p "$RPM_BUILD_ROOT"%{_mandir}/man1/
cp gpsd "$RPM_BUILD_ROOT"%{_bindir}
cp gpsd.1 "$RPM_BUILD_ROOT"%{_mandir}/man1/
mkdir -p "$RPM_BUILD_ROOT"%{_libdir}/
cp libgps.a "$RPM_BUILD_ROOT"%{_libdir}
mkdir -p "$RPM_BUILD_ROOT"%{_mandir}/man3/
cp libgps.3 "$RPM_BUILD_ROOT"%{_mandir}/man3/
cp libgpsd.3 "$RPM_BUILD_ROOT"%{_mandir}/man3/
mkdir -p "$RPM_BUILD_ROOT"%{_includedir}
cp gpsd.h "$RPM_BUILD_ROOT"%{_includedir}
cp gps.h "$RPM_BUILD_ROOT"%{_includedir}
mkdir -p "$RPM_BUILD_ROOT"/etc/init.d/
cp gpsd.init "$RPM_BUILD_ROOT"/etc/init.d/gpsd

%clean
[ "$RPM_BUILD_ROOT" -a "$RPM_BUILD_ROOT" != / ] && rm -rf "$RPM_BUILD_ROOT"

%post
/sbin/ldconfig
/sbin/chkconfig --add gpsd
/sbin/chkconfig gpsd on

%preun
/sbin/chkconfig --del gpsd

%postun
/sbin/ldconfig

%files
%defattr(-,root,root,-)
%doc README INSTALL COPYING gpsd.xml libgps.xml libgpsd.xml HACKING TODO
%defattr(-,root,root,-)
%attr(755, root, root) %{_bindir}/gpsd
%{_mandir}/man1/gpsd.1*
%{_libdir}/libgps.a
%{_mandir}/man3/libgps.3*
%{_mandir}/man3/libgpsd.3*
%{_includedir}/gps.h
%{_includedir}/gpsd.h
%attr(755, root, root) /etc/init.d/gpsd

%changelog
* Mon Aug 23 2004 Eric S. Raymond <esr@golux.thyrsus.com> - 1.93-1
- Fourth prerelease. Daemon-side timeouts are gone, they complicated
  the interface without adding anything.  Command responses now 
  contain ? to tag invalid data.

* Sun Aug 22 2004 Eric S. Raymond <esr@golux.thyrsus.com> - 1.92-1
- Third prerelease.  Clients in watcher mode now get notified when
  the GPS goes online or offline.  Major name changes -- old libgps
  is new libgpsd and vice-versa (so the high-level interface is more
  prominent.  Specfile now includes code to install gpsd so it will
  be started at boot time.  -D2 now causes command error messages
  to be echoed to the client.

* Sat Aug 21 2004 Eric S. Raymond <esr@snark.thyrsus.com> - 1.91-1
- Second pre-2.0 release.  Features a linkable C library that hides the 
  details of communicating with the daemon.  The daemon now recovers
  gracefully from having the GPS unplugged and plugged in at any time;
  one of the bits of status it can report is whether the GPS is online.
  The gps and xgpsspeed clients now query the daemon; their code 
  for direct access to the serial port has been deliberately removed.

* Sun Aug 15 2004 Eric S. Raymond <esr@snark.thyrsus.com> - 1.90
- Creation of specfile.

