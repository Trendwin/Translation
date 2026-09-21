QT += core gui widgets

CONFIG += c++11
TEMPLATE = app
TARGET = Translation

DEFINES += QT_DEPRECATED_WARNINGS

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
    src/demo/democlientprotocol.cpp \
    src/demo/demointernalprotocol.cpp \
    src/demo/mocktransport.cpp

HEADERS += \
    widget.h \
    src/model/translationtypes.h \
    src/protocol/iclientprotocol.h \
    src/protocol/iinternalprotocol.h \
    src/transport/itransport.h \
    src/core/requestmanager.h \
    src/core/translationservice.h \
    src/demo/democlientprotocol.h \
    src/demo/demointernalprotocol.h \
    src/demo/mocktransport.h

FORMS += widget.ui

qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
