#!/usr/bin/env bash
set -e
set -x



export PATH=/usr/sbin:/usr/bin:/sbin:/bin
if uname -a | grep -qi freebsd; then
	export PATH="${PATH}:/usr/local/bin:/usr/local/sbin"
fi

if [ "${USE_CCACHE}" = "true" ] && [ -n "${CCACHE_DIR}" ] && command -v ccache >/dev/null; then
	export PATH="/usr/lib/ccache:${PATH}"
	mkdir -p "${CCACHE_DIR}"
else
	export USE_CCACHE="false"
fi

if command -v lsb_release >/dev/null && lsb_release -d | grep -qi -e debian -e ubuntu; then
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

export SCONS=$(command -v scons)

if command -v nproc >/dev/null; then
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
    _SCONS="${SCONS} target_python=python${py}"
    python${py}     ${_SCONS} ${SCONSOPTS} --clean
    rm -f .sconsign.*.dblite
    ${SCAN_BUILD} python${py}     ${_SCONS} ${SCONS_PARALLEL}${SCONSOPTS} build-all
    if [ -z "${NOCHECK}" ]; then
	    python${py}     ${_SCONS} ${SCONSOPTS} check
    fi
done



if [ "${USE_CCACHE}" = "true" ]; then
	ccache -s
fi

