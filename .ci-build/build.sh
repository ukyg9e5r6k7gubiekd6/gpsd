#!/bin/bash
set -e
set -x

export PATH=/usr/sbin:/usr/bin:/sbin:/bin

eval "$(dpkg-buildflags --export=sh)"

export DEB_HOST_MULTIARCH=$(dpkg-architecture -qDEB_HOST_MULTIARCH)

SCONSOPTS="${SCONSOPTS} $@ prefix=/usr"
SCONSOPTS="${SCONSOPTS} systemd=yes"
SCONSOPTS="${SCONSOPTS} nostrip=yes"
SCONSOPTS="${SCONSOPTS} dbus_export=yes"
SCONSOPTS="${SCONSOPTS} docdir=/usr/share/doc/gpsd"
SCONSOPTS="${SCONSOPTS} libdir=/usr/lib/${DEB_HOST_MULTIARCH}"
SCONSOPTS="${SCONSOPTS} gpsd_user=gpsd"
SCONSOPTS="${SCONSOPTS} gpsd_group=dialout"
SCONSOPTS="${SCONSOPTS} debug=yes"

if dpkg -s qtbase5-dev 1>/dev/null 2>&1; then
    SCONSOPTS="${SCONSOPTS} qt_versioned=5"
fi

export SCONSOPTS


export PYTHONS="$(pyversions -v -r '>= 2.3'; py3versions -v -r '>= 3.4')"

if [ -z "$PYTHONS" ]; then
    export PYTHONS="2 3"
fi

if [ -n "${SCAN_BUILD}" ]; then
    export PYTHONS="3"
fi

for py in $PYTHONS; do
    python${py}     /usr/bin/scons ${SCONSOPTS} --clean
    rm -f .sconsign.*.dblite
    ${SCAN_BUILD} python${py}     /usr/bin/scons ${SCONSOPTS} build-all
    python${py}     /usr/bin/scons ${SCONSOPTS} check
done

