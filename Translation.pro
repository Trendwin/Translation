# Translation project skeleton (Qt 5.14.2 / C++11).
TEMPLATE = app
TARGET = Translation

QT += core gui widgets
CONFIG += c++11
DEFINES += QT_DEPRECATED_WARNINGS

# Enable these modules when QML/serial-port implementation is added:
# QT += quick qml serialport

# Follow FreControl's output layout; separate Debug and Release products.
CONFIG(debug, debug|release) {
    DESTDIR     = "$$PWD/bin/debug"
    OBJECTS_DIR = "$$PWD/build/debug/obj"
    MOC_DIR     = "$$PWD/build/debug/moc"
    RCC_DIR     = "$$PWD/build/debug/rcc"
    UI_DIR      = "$$PWD/build/debug/ui"
} else {
    DESTDIR     = "$$PWD/bin/release"
    OBJECTS_DIR = "$$PWD/build/release/obj"
    MOC_DIR     = "$$PWD/build/release/moc"
    RCC_DIR     = "$$PWD/build/release/rcc"
    UI_DIR      = "$$PWD/build/release/ui"
}

INCLUDEPATH += "$$PWD/src"

# Keep the existing QWidget entry point.
# Add real source/header files here as the src modules are implemented.
SOURCES += \
    main.cpp \
    widget.cpp

HEADERS += \
    widget.h

FORMS += \
    widget.ui

# Reserved for future QML pages and reusable components.
QML_IMPORT_PATH += "$$PWD/qml"
QML_DESIGNER_IMPORT_PATH += "$$PWD/qml"

# Empty directory placeholders are project files, not compilation inputs.
DISTFILES += \
    bin/debug/.gitkeep \
    bin/release/.gitkeep \
    build/.gitkeep \
    config/.gitkeep \
    docs/.gitkeep \
    qml/components/.gitkeep \
    qml/pages/.gitkeep \
    src/adapter/.gitkeep \
    src/model/.gitkeep \
    src/protocol/.gitkeep \
    src/service/.gitkeep \
    src/viewmodel/.gitkeep \
    src/worker/.gitkeep

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
