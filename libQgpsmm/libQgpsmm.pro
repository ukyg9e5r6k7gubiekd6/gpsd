# -------------------------------------------------
# Project created by QtCreator 2010-03-26T10:37:44
# -------------------------------------------------
QT += network
QT -= gui
TARGET = Qgpsmm
TEMPLATE = lib
DEFINES += LIBQGPSMM_LIBRARY
DEFINES += USE_QT
DESTDIR = binaries
INCLUDEPATH += $$PWD \
    ..

isEmpty( MAKE ) {
    MAKE = make
}

SOURCES += \
    gpsutils.cpp \
    libgps_core.cpp \
    ../libgpsmm.cpp \
    ../libgps_json.c \
    ../hex.c \
    ../gpsd_report.c \
    ../strl.c \
    ../shared_json.c \
    ../rtcm2_json.c \
    ../ais_json.c \
    ../json.c
HEADERS += libQgpsmm_global.h \
    ../gps.h \
    ../libgpsmm.h \
    ../gps_json.h \
    ../json.h

!win32 {

    isEmpty( VERSION ) {
        VERSION = $$system($${MAKE} -s -C .. print_libgps_VERSION)
    }
    HEADERS += \
        ../gpsd.h \
	../ais_json.i

    # Prefix: base instalation directory
    isEmpty( PREFIX ) {
        PREFIX = /usr/local
    }
    isEmpty( EXEC_PREFIX ) {
        EXEC_PREFIX = $${PREFIX}
    }
    isEmpty( LIBDIR ) {
        LIBDIR = /lib
    }
    isEmpty( INCLUDEDIR ) {
        INCLUDEDIR = /include
    }

    # TARGET_LIBDIR and TARGET_INCLUDEDIR allow to override
    # the library and header install paths.
    # This is mainly a workaround as QT was not able to use the proper
    # path on some platforms. Both TARGET_ variables will be
    # set from the autotools generated Makefile.
    # There should be a better way to handle this, though.
    isEmpty( TARGET_LIBDIR ) {
        TARGET_LIBDIR = $${EXEC_PREFIX}$${LIBDIR}
    }
    isEmpty( TARGET_INCLUDEDIR ) {
        TARGET_INCLUDEDIR = $${PREFIX}$${INCLUDEDIR}
    }
    target.path = $${TARGET_LIBDIR}
    INSTALLS += target

    header.path = $${TARGET_INCLUDEDIR}
    header.files = ../libgpsmm.h ../gps.h
    INSTALLS += header

    QMAKE_CFLAGS += -D_GNU_SOURCE

    # create pkg_config and prl files
    CONFIG += create_pc create_prl
    QMAKE_PKGCONFIG_REQUIRES = QtNetwork
    QMAKE_PKGCONFIG_DESTDIR = pkgconfig
    pkgconfig.path = $${TARGET_LIBDIR}
    INSTALLS += pkgconfig
}

win32 {

    include( mingw/version.pri )
    HEADERS += \
        gpsd.h \
	mingw/ais_json.i

    INCLUDEPATH = $$PWD/mingw $${INCLUDEPATH}

    gpsdhcreate.target = gpsd.h
    gpsdhcreate.commands = "copy /Y /B ..\gpsd.h-head + mingw\gpsd_config.h + ..\gpsd.h-tail  gpsd.h"
    gpsdhcreate.depends = FORCE

    PRE_TARGETDEPS += gpsd.h
    QMAKE_EXTRA_TARGETS += gpsdhcreate

}
