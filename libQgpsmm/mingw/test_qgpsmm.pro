QT += network
QT -= gui
TARGET = test_qgpsmm
DESTDIR = ../binaries
TEMPLATE = app

INCLUDEPATH += $$PWD \
       .. \
       ../..

SOURCES = ../../test_gpsmm.cpp
LIBS += -L../binaries -lQgpsmm

# just in case sombody runs this on !win32:

!win32 {
	LIBS += -Wl,-rpath,$$PWD/../binaries
}
