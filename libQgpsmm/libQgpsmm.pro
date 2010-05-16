# -------------------------------------------------
# Project created by QtCreator 2010-03-26T10:37:44
# -------------------------------------------------
QT += network
QT -= gui
TARGET = Qgpsmm
TEMPLATE = lib
DEFINES += LIBQGPSMM_LIBRARY
DEFINES += USE_QT
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
    target.path = $${PREFIX}/lib
    INSTALLS += target

    header.path = $${PREFIX}/include
    header.files = ../libgpsmm.h ../gps.h
    INSTALLS += header

    QMAKE_CFLAGS += -D_GNU_SOURCE

}

win32 {

    # TODO:
    # - test build

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
