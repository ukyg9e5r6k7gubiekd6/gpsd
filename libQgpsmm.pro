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


SOURCES += \
    ../libgpsmm.cpp \
    gpsutils.cpp \
    ../libgps_json.c \
    ../hex.c \
    libgps_core.cpp \
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

unix {
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

gpsutilscpp.target = gpsutils.cpp
win32:gpsutilscpp.commands = copy ..\gpsutils.c gpsutils.cpp
else:gpsutilscpp.commands = cp ../gpsutils.c gpsutils.cpp
gpsutilscpp.depends = FORCE

libgps_corecpp.target = libgps_core.cpp
win32:libgps_corecpp.commands = copy ..\libgps_core.c libgps_core.cpp
else:libgps_corecpp.commands = cp ../libgps_core.c libgps_core.cpp
libgps_corecpp.depends = FORCE

gpsdhcreate.target = gpsd.h
win32:gpsdhcreate.commands = "copy /Y /B ..\gpsd.h-head + mingw\gpsd_config.h + ..\gpsd.h-tail  ..\gpsd.h" && "copy /Y mingw\gpsd_config.h .." && "copy /Y mingw\ais_json.i .."
else:gpsdhcreate.commands = cat ../gpsd.h-head ../gpsd_config.h ../gpsd.h-tail > ../gpsd.h
gpsdhcreate.depends = FORCE

PRE_TARGETDEPS += gpsutils.cpp libgps_core.cpp gpsd.h
QMAKE_EXTRA_TARGETS += gpsutilscpp libgps_corecpp gpsdhcreate

