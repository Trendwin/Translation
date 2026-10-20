QT += core testlib
CONFIG += c++11 console testcase
TEMPLATE = app
TARGET = tst_framework
INCLUDEPATH += ../src
RESOURCES += ../resources.qrc

include($$PWD/../build-encoding.pri)

SOURCES += tst_framework.cpp \
    ../src/demo/demointernalprotocol.cpp \
    ../src/qusheng/qushengprotocol.cpp \
    ../src/qusheng/qushengcommandcatalog.cpp \
    ../src/config/protocolconfig.cpp \
    ../src/core/conversionengine.cpp \
    ../src/core/requestmanager.cpp
HEADERS += ../src/model/translationtypes.h \
    ../src/protocol/iinternalprotocol.h \
    ../src/demo/demointernalprotocol.h \
    ../src/qusheng/qushengprotocol.h \
    ../src/qusheng/qushengcommandcatalog.h \
    ../src/config/protocolconfig.h \
    ../src/core/conversionengine.h \
    ../src/core/requestmanager.h \
    ../src/transport/itransport.h

include($$PWD/../src/qusheng/sdk/qusheng-sdk.pri)

SOURCES += ../src/config/textformat.cpp
HEADERS += ../src/config/textformat.h
