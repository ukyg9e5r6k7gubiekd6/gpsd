QT += network
QT -= gui
TARGET = test_qgpsmm
TEMPLATE = app

INCLUDEPATH += $$PWD \
       .. \
       ../..

SOURCES = ../../test_gpsmm.cpp
LIBS += -L .. -lQgpsmm

