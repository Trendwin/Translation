QT += core testlib
CONFIG += c++11 console testcase
TEMPLATE = app
TARGET = tst_framework
INCLUDEPATH += ../src
SOURCES += tst_framework.cpp ../src/demo/demointernalprotocol.cpp
HEADERS += ../src/model/translationtypes.h ../src/protocol/iinternalprotocol.h ../src/demo/demointernalprotocol.h
