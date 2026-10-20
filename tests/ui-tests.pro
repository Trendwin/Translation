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
    ../src/qusheng/qushengprotocol.cpp ../src/transport/serialtransport.cpp
HEADERS += ../widget.h ../src/core/requestmanager.h ../src/core/translationservice.h \
    ../src/config/draftstore.h \
    ../src/qusheng/qushengcommandcatalog.h \
    ../src/protocol/iinternalprotocol.h ../src/transport/itransport.h \
    ../src/qusheng/qushengprotocol.h ../src/transport/serialtransport.h

include($$PWD/../src/qusheng/sdk/qusheng-sdk.pri)

SOURCES += ../src/config/textformat.cpp
HEADERS += ../src/config/textformat.h

SOURCES += ../src/ui/textformateditor.cpp
HEADERS += ../src/ui/textformateditor.h

SOURCES += ../src/config/sampleformat.cpp
HEADERS += ../src/config/sampleformat.h
