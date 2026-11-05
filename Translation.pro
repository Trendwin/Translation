QT += core gui widgets serialport

CONFIG += c++11
TEMPLATE = app
TARGET = Translation

DEFINES += QT_DEPRECATED_WARNINGS
RESOURCES += resources.qrc

include($$PWD/build-encoding.pri)

CONFIG(debug, debug|release) {
    DESTDIR = $$PWD/bin/debug
} else {
    DESTDIR = $$PWD/bin/release
}

INCLUDEPATH += $$PWD/src

SOURCES += \
    main.cpp \
    widget.cpp \
    src/core/requestmanager.cpp \
    src/core/translationservice.cpp \
    src/core/upstreamforwarder.cpp \
    src/core/conversionengine.cpp \
    src/config/protocolconfig.cpp \
    src/config/draftstore.cpp \
    src/qusheng/qushengprotocol.cpp \
    src/qusheng/qushengcommandcatalog.cpp \
    src/transport/serialtransport.cpp \
    src/transport/upstreamserialreceiver.cpp \
    src/transport/virtualcomprovider.cpp

HEADERS += \
    src/core/numericformula.h \
    src/config/draftstore.h \
    widget.h \
    src/model/translationtypes.h \
    src/protocol/iinternalprotocol.h \
    src/transport/itransport.h \
    src/core/requestmanager.h \
    src/core/translationservice.h \
    src/core/upstreamforwarder.h \
    src/core/conversionengine.h \
    src/config/protocolconfig.h \
    src/qusheng/qushengprotocol.h \
    src/qusheng/qushengcommandcatalog.h \
    src/transport/serialtransport.h \
    src/transport/upstreamserialreceiver.h \
    src/transport/virtualcomprovider.h

win32: LIBS += -lshell32 -ladvapi32 -lwintrust -lcrypt32

qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

include($$PWD/src/qusheng/sdk/qusheng-sdk.pri)
include($$PWD/build-runtime.pri)
