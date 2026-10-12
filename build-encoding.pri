# 三个入口共用字符集约定，避免应用正确而测试仍用本机代码页，或切换 kit 后出现乱码。
# UTF-8 源码/字符串编译和运行时 JSON 解码是不同阶段；项目统一编译参数，中文文案仍显式 fromUtf8。
win32-msvc {
    QMAKE_CFLAGS += /utf-8
    QMAKE_CXXFLAGS += /utf-8
}
win32-g++ {
    QMAKE_CFLAGS += -finput-charset=UTF-8 -fexec-charset=UTF-8
    QMAKE_CXXFLAGS += -finput-charset=UTF-8 -fexec-charset=UTF-8
}
