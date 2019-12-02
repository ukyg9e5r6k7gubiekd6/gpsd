#!/bin/bash
set -e
set -x

apt -y install ccache

PATH=/usr/sbin:/usr/bin:/sbin:/bin
export PATH="/usr/lib/ccache:$PATH"

if which nproc >/dev/null; then
        SCONS_PARALLEL="-j $(nproc) "
else
        SCONS_PARALLEL=""
fi

(
	scons --help | grep -B1 'default: True' | grep 'yes|no' | sed 's,:.*,=no,' | grep -v '^gpsd$'
	scons --help | grep -B1 'default: False' | grep 'yes|no' | sed 's,:.*,=yes,'
) | while read option; do
	scons --clean
	rm -f .sconsign.*.dblite
	scons ${SCONS_PARALLEL}${option} build
done

