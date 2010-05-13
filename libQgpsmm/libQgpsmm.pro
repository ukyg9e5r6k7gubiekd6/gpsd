# -------------------------------------------------
# Project created by QtCreator 2010-03-26T10:37:44
# -------------------------------------------------
QT += network
QT -= gui
TARGET = Qgpsmm
TEMPLATE = lib
DEFINES += LIBQGPSMM_LIBRARY
DEFINES += USE_QT
#QMAKE_EXT_CPP += .c
!win32: QMAKE_CFLAGS += -D_GNU_SOURCE
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
    ../json.h \
    ../ais_json.i

!win32 {

    _IGNORE = $$system(test -r gpsutils.cpp || ln -s ../gpsutils.c gpsutils.cpp)
    _IGNORE = $$system(test -r libgps_core.cpp || ln -s ../libgps_core.c libgps_core.cpp)
    QMAKE_CLEAN += gpsutils.cpp libgps_core.cpp

    isEmpty( VERSION ) {
        VERSION = $$system($${MAKE} -s -C .. print_libgps_VERSION)
    }
    HEADERS += \
        ../gpsd.h

    # Prefix: base instalation directory
    isEmpty( PREFIX ) {
        PREFIX = /usr/local
    }
    target.path = $${PREFIX}/lib
    INSTALLS += target

    header.path = $${PREFIX}/include
    header.files = ../libgpsmm.h ../gps.h
    INSTALLS += header


}

win32 {

    # TODO:
    # - library version handling
    # - add missing files (if there are any)
    # - test build

    HEADERS += \
        gpsd.h

    gpsutilscpp.target = gpsutils.cpp
    gpsutilscpp.commands = copy ..\gpsutils.c gpsutils.cpp
    gpsutilscpp.depends = FORCE

    libgps_corecpp.target = libgps_core.cpp
    libgps_corecpp.commands = copy ..\libgps_core.c libgps_core.cpp
    libgps_corecpp.depends = FORCE

    gpsdhcreate.target = gpsd.h
    gpsdhcreate.commands = "copy /Y /B ..\gpsd.h-head + mingw\gpsd_config.h + ..\gpsd.h-tail  gpsd.h" && "copy /Y mingw\gpsd_config.h .." && "copy /Y mingw\ais_json.i .."
    gpsdhcreate.depends = FORCE

    PRE_TARGETDEPS += gpsd.h
    QMAKE_EXTRA_TARGETS += gpsdhcreate
}
