# Copyright 2005 Amaury Jacquot
# Author: Amaury Jacquot <sxpert@esitcom.org>

DESCRIPTION="gpsd is a daemon that listens to a GPS or Loran receiver and
translates the positional data into a simplified format that can be more easily
used by other programs like chart plotters. The package comes with a sample
client that plots the location of the currently visible GPS satellites (if
available) and a speedometer. It can also use DGPS/ip, and run as a DGPS/ip
server, and, if the PPS signal is available, be used for NTP synchronization"
HOMEPAGE="http://gpsd.berlios.de"
SRC_URI="http://download.berlios.de/gpsd/${P}.tar.gz"

LICENSE="BSD"
SLOT="0"
KEYWORDS="x86 ppc sparc amd64 alpha"

IUSE="motif"
DEPEND="motif? (=x11-libs/openmotif)"

src_compile() {
	econf \
	$(use_with motif x) \
	|| die "econf failed"
	emake || die "compile failed"
}

src_install() {
	make DESTDIR=${D} install
}
