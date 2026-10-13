# 独立QtCore SDK构建入口；应用和测试共享同一编码、默认补齐、回告解析实现。
INCLUDEPATH += $$PWD/../..
HEADERS += $$PWD/qushengsdk.h $$PWD/../../model/translationtypes.h
SOURCES += $$PWD/sdkrequests.cpp $$PWD/sdkreplies.cpp $$PWD/sdkframes.cpp
