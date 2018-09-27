#!/bin/bash
set -e
set -x

export PATH=/usr/sbin:/usr/bin:/sbin:/bin

eval "$(dpkg-buildflags --export=sh)"

export DEB_HOST_MULTIARCH=$(dpkg-architecture -qDEB_HOST_MULTIARCH)

SCONSOPTS="$@ prefix=/usr"
SCONSOPTS="${SCONSOPTS} systemd=yes"
SCONSOPTS="${SCONSOPTS} nostrip=yes"
SCONSOPTS="${SCONSOPTS} dbus_export=yes"
SCONSOPTS="${SCONSOPTS} docdir=/usr/share/doc/gpsd"
SCONSOPTS="${SCONSOPTS} libdir=/usr/lib/${DEB_HOST_MULTIARCH}"
SCONSOPTS="${SCONSOPTS} gpsd_user=gpsd"
SCONSOPTS="${SCONSOPTS} gpsd_group=dialout"
SCONSOPTS="${SCONSOPTS} debug=yes"
### SCONSOPTS="${SCONSOPTS} qt=yes"  # The default qt=yes must be overridable
SCONSOPTS="${SCONSOPTS} xgps=no"  # Until we figure out the right Gtk3 packages

if dpkg -s qtbase5-dev 1>/dev/null 2>&1; then
    SCONSOPTS="${SCONSOPTS} qt_versioned=5"
fi

export SCONSOPTS

export PYTHONS="$(pyversions -v -r '>= 2.3')"

if [ "$PYTHONS" = "" ]; then
    export PYTHONS="2"
fi

for py in $PYTHONS; do
    python${py}     /usr/bin/scons ${SCONSOPTS}
    python${py}-dbg /usr/bin/scons ${SCONSOPTS}
done

/usr/bin/scons build-all www check

