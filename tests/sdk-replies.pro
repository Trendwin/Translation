QT += core testlib
QT -= gui
CONFIG += c++11 console testcase
TEMPLATE = app
TARGET = tst_sdk_replies
include($$PWD/../build-encoding.pri)
include($$PWD/../src/qusheng/sdk/qusheng-sdk.pri)
SOURCES += tst_sdk_replies.cpp
