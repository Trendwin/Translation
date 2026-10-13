QT = core testlib
CONFIG += c++11 console testcase
TEMPLATE = app
TARGET = tst_sdk_requests
include($$PWD/../build-encoding.pri)
include($$PWD/../src/qusheng/sdk/qusheng-sdk.pri)
SOURCES += tst_sdk_requests.cpp
