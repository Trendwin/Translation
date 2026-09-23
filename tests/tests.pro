QT += core testlib
CONFIG += c++11 console testcase
TEMPLATE = app
TARGET = tst_framework
INCLUDEPATH += ../src

win32-msvc {
    QMAKE_CFLAGS += /utf-8
    QMAKE_CXXFLAGS += /utf-8
}

SOURCES += tst_framework.cpp \
    ../src/demo/demointernalprotocol.cpp \
    ../src/dt/dtclientprotocol.cpp \
    ../src/qusheng/qushengprotocol.cpp
HEADERS += ../src/model/translationtypes.h \
    ../src/protocol/iclientprotocol.h \
    ../src/protocol/iinternalprotocol.h \
    ../src/demo/demointernalprotocol.h \
    ../src/dt/dtclientprotocol.h \
    ../src/qusheng/qushengprotocol.h
