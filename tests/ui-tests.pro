QT += core gui widgets serialport testlib
CONFIG += c++11 console testcase
TEMPLATE = app
TARGET = tst_widget
INCLUDEPATH += ../src ..
RESOURCES += ../resources.qrc
include($$PWD/../build-encoding.pri)
SOURCES += tst_widget.cpp ../widget.cpp \
    ../src/qusheng/qushengcommandcatalog.cpp \
    ../src/config/protocolconfig.cpp ../src/core/conversionengine.cpp \
    ../src/config/draftstore.cpp \
    ../src/core/requestmanager.cpp ../src/core/translationservice.cpp \
    ../src/core/upstreamforwarder.cpp \
    ../src/qusheng/qushengprotocol.cpp ../src/transport/serialtransport.cpp \
    ../src/transport/upstreamserialreceiver.cpp \
    ../src/transport/virtualcomprovider.cpp
HEADERS += ../widget.h ../src/core/requestmanager.h ../src/core/translationservice.h \
    ../src/core/upstreamforwarder.h \
    ../src/core/numericformula.h \
    ../src/config/draftstore.h \
    ../src/qusheng/qushengcommandcatalog.h \
    ../src/protocol/iinternalprotocol.h ../src/transport/itransport.h \
    ../src/qusheng/qushengprotocol.h ../src/transport/serialtransport.h \
    ../src/transport/upstreamserialreceiver.h \
    ../src/transport/virtualcomprovider.h

win32: LIBS += -lshell32 -ladvapi32 -lwintrust -lcrypt32

include($$PWD/../src/qusheng/sdk/qusheng-sdk.pri)
