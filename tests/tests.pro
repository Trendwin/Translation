QT += core serialport testlib
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
    ../src/core/requestmanager.cpp \
    ../src/core/translationservice.cpp \
    ../src/core/upstreamforwarder.cpp \
    ../src/transport/upstreamserialreceiver.cpp \
    ../src/transport/virtualcomprovider.cpp
HEADERS += ../src/model/translationtypes.h \
    ../src/core/numericformula.h \
    ../src/protocol/iinternalprotocol.h \
    ../src/demo/demointernalprotocol.h \
    ../src/qusheng/qushengprotocol.h \
    ../src/qusheng/qushengcommandcatalog.h \
    ../src/config/protocolconfig.h \
    ../src/core/conversionengine.h \
    ../src/core/requestmanager.h \
    ../src/core/translationservice.h \
    ../src/core/upstreamforwarder.h \
    ../src/transport/itransport.h \
    ../src/transport/upstreamserialreceiver.h \
    ../src/transport/virtualcomprovider.h

win32: LIBS += -lshell32 -ladvapi32 -lwintrust -lcrypt32

include($$PWD/../src/qusheng/sdk/qusheng-sdk.pri)
