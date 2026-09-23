# Windows / Qt Creator 构建与中文编码检查

## 编码结论

工程内的 `.cpp`、`.h`、`.ui` 和 `.pro` 均应保存为无 BOM 的 UTF-8。界面文件已经声明
`encoding="UTF-8"`，C++ 中文常量使用 `QStringLiteral`；工程中不需要、也不应增加
`toLocal8Bit()` / `fromLocal8Bit()` 往返转换。

`Translation.pro` 的 `win32-msvc` 作用域没有限定 Debug 或 Release，因此其中的
`QMAKE_CFLAGS += /utf-8` 和 `QMAKE_CXXFLAGS += /utf-8` 会同时进入两种配置。若 `.ui`
文字正常而 C++ 中的窗口标题、日志等异常，通常说明正在运行添加 `/utf-8` 以前编译的
目标文件或 EXE，而不是字体问题。

## Qt Creator 完整重建

1. 在 Qt Creator 左侧 **项目** 中确认 Kit 为 **Desktop Qt 5.14.2 MSVC2017**，并记下
   当前配置（Debug 或 Release）的构建目录和运行配置中的 EXE 路径。
2. 选择 **构建 > 清理项目 “Translation”**，随后选择 **构建 > 运行 qmake**。
3. 为避免旧目标文件残留，关闭 Qt Creator，删除当前 shadow build 目录；如果曾在源码目录
   构建，也删除源码目录中的 `debug`、`release`、`Makefile*`、`ui_widget.h`、`moc_*` 和
   `object_script.*`。不要删除源码目录下受版本控制的文件。
4. 重新打开工程，选择 **构建 > 重新构建项目 “Translation”**。分别切换 Debug、Release
   后重复“运行 qmake”和“重新构建”，可在编译输出中确认 `cl` 命令包含 `/utf-8`。
5. 从 Qt Creator 的 **应用程序输出** 首行或 **项目 > 运行 > 可执行文件**确认启动路径。
   该路径应为当前构建目录生成的 `bin/debug/Translation.exe` 或
   `bin/release/Translation.exe`。也可先关闭程序，查看该 EXE 的修改时间是否与本次构建
   一致，再从该绝对路径启动；不要运行桌面上遗留的旧副本。

## 人工验收

- 检查标题、连接/断开、刷新、状态、日志和错误原因中的中文无乱码。
- 启动即列出串口；刷新后仍选择原端口。无端口时显示“未发现串口”且不能连接。
- 确认列表中的描述只用于显示，连接使用端口名；刷新和切换选择均不打开设备、不发送报文。
- 连接后端口、波特率、刷新均禁用且按钮为“断开”；断开或拔出后控件恢复。
- 串口被占用时界面保持未连接并在日志显示原因，不重复弹窗。
- 未连接或请求正在发送/等待回告时“发送”不可用，请求结束后仅在仍连接时恢复。
