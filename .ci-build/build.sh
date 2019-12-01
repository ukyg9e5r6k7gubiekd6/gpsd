#!/bin/bash
set -e
set -x

export PATH=/usr/sbin:/usr/bin:/sbin:/bin

if lsb_release -d | grep -qi -e debian -e ubuntu; then
	eval "$(dpkg-buildflags --export=sh)"
	export DEB_HOST_MULTIARCH=$(dpkg-architecture -qDEB_HOST_MULTIARCH)
	export PYTHONS="$(pyversions -v -r '>= 2.3'; py3versions -v -r '>= 3.4')"
	SCONSOPTS="${SCONSOPTS} libdir=/usr/lib/${DEB_HOST_MULTIARCH}"
else
	SCONSOPTS="${SCONSOPTS} libdir=/usr/lib"
fi

SCONSOPTS="${SCONSOPTS} $@ prefix=/usr"
SCONSOPTS="${SCONSOPTS} systemd=yes"
SCONSOPTS="${SCONSOPTS} nostrip=yes"
SCONSOPTS="${SCONSOPTS} dbus_export=yes"
SCONSOPTS="${SCONSOPTS} docdir=/usr/share/doc/gpsd"
SCONSOPTS="${SCONSOPTS} gpsd_user=gpsd"
SCONSOPTS="${SCONSOPTS} gpsd_group=dialout"
SCONSOPTS="${SCONSOPTS} debug=yes"
SCONSOPTS="${SCONSOPTS} qt_versioned=5"

if which nproc >/dev/null; then
	SCONS_PARALLEL="-j $(nproc) "
else
	SCONS_PARALLEL=""
fi

export SCONSOPTS

if [ -z "$PYTHONS" ]; then
    export PYTHONS="3"
fi

if [ -n "${SCAN_BUILD}" ]; then
    export PYTHONS="3"
fi

for py in $PYTHONS; do
    python${py}     /usr/bin/scons ${SCONSOPTS} --clean
    rm -f .sconsign.*.dblite
    ${SCAN_BUILD} python${py}     /usr/bin/scons ${SCONS_PARALLEL}${SCONSOPTS} build-all
    python${py}     /usr/bin/scons ${SCONSOPTS} check
done

