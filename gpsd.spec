Summary: service daemon for mediating access to a GPS
Name: gpsd
Version: 1.90
Release: 1
License: GPL
Group: System Environment/Daemons
URL: http://www.pygps.org/gpsd/
Source0: %{name}-%{version}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root

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
cp libgpsd.a "$RPM_BUILD_ROOT"%{_libdir}
mkdir -p "$RPM_BUILD_ROOT"%{_mandir}/man3/
cp libgps.3 "$RPM_BUILD_ROOT"%{_mandir}/man3/
cp gpsd.h "$RPM_BUILD_ROOT"%{_includedir}

%clean
[ "$RPM_BUILD_ROOT" -a "$RPM_BUILD_ROOT" != / ] && rm -rf "$RPM_BUILD_ROOT"

%post
/sbin/ldconfig

%postun
/sbin/ldconfig

%files
%defattr(-,root,root,-)
%doc README INSTALL COPYING gpsd.xml libgps.xml HACKING TODO
%defattr(-,root,root,-)
%{_bindir}/gpsd
%{_mandir}/man1/gpsd.1*
%{_libdir}/libgps.a
%{_libdir}/libgpsd.a
%{_mandir}/man3/libgps.3*
%{_includedir}/gps.h

%changelog
* Sun Aug 15 2004 Eric S. Raymond <esr@snark.thyrsus.com> - 1.90
- Creation of specfile.

