# Windows 构建与运行

默认使用MSVC，Qt为 `C:\Qt\Qt5.14.2\5.14.2\msvc2017`（32位）。本机可用MSVC2022 14.44的x86工具链；已有MSVC2017也可使用对应开发命令行。应用和测试共用 `build-encoding.pri`，MSVC显式 `/utf-8`。MinGW仅保留历史兼容记录，不作为每轮默认验证。

## 命令目录乱码修正

命令码、JSON 中文名称和悬停说明正常，原来用于拼接的中点、全角括号却显示成“路”“锛?”，因此不是缺少中文字体或目录 JSON 内容损坏。实际用 MSVC2017 14.16.27023、Qt5.14.2、`/utf-8` 做了最小复现：`QStringLiteral("%1 · %2（%3）")` 的拼接标点产生错误字符，且 `%3` 被破坏；相同内容用 `QString::fromUtf8(u8"...")` 转换正确。

命令列表现使用 ASCII 格式 `%1 | %2 (%3)`，保留目录里的中文名称和中文状态，例如 `0x22 | 电磁阀单路控制 (发送待实现)`。其余控件的中文文案明确用 `QString::fromUtf8(u8"...")`；源码中的 `QStringLiteral` 仅用于 ASCII。字体选择不能修复已经生成的错误码点，单独增加 `/utf-8` 也不足以修复这次已复现的 Qt 宏/编译器组合问题。JSON 仍由 JSON 解码器读取，两条链路不应混为一谈。

更新后在 Qt Creator 重新运行 qmake，再使用新的构建目录全量构建；不要直接复用旧目标文件或运行以前输出的 exe。检查“趋盛指令”下拉列表中的 `0x17`、`0x22` 和已实现运动预设，名称、状态和括号应全部正常。应用与测试必须使用同一 kit 的 DLL；切换 MinGW/MSVC 时分别构建。

本次修复实际使用本机 MSVC2017 14.16.27023 / Qt5.14.2 32位和 MinGW GCC7.3.0 / Qt5.14.2 32位分别全量构建应用、核心和界面测试，两套环境均为38项核心、14项界面测试通过，无失败或跳过。新增用截图中的指令独立核对文字，并检查全部59项；两套环境均渲染展开的命令下拉并确认中文正常。

## MSVC构建示例

打开Visual Studio的 **x86 Native Tools Command Prompt**，在项目根目录执行。不同套件分别使用独立构建目录，按本轮修改选择需要构建及运行的测试：

```bat
set "PATH=C:\Qt\Qt5.14.2\5.14.2\msvc2017\bin;%PATH%"
mkdir build-msvc\app build-msvc\core build-msvc\ui
cd build-msvc\app
qmake ..\..\Translation.pro
nmake /nologo
cd ..\core
qmake ..\..\tests\tests.pro
nmake /nologo
release\tst_framework.exe
cd ..\ui
qmake ..\..\tests\ui-tests.pro
nmake /nologo
set "QT_QPA_PLATFORM=offscreen"
set "QT_QPA_FONTDIR=C:\Windows\Fonts"
release\tst_widget.exe
set "QT_QPA_PLATFORM="
set "QT_QPA_FONTDIR="
cd ..\..
```

SDK请求/回告有独立工程 `tests/sdk-requests.pro`、`tests/sdk-replies.pro`，变更SDK时在各自独立目录按相同qmake、nmake流程验证。已有构建目录无需重复mkdir。原生窗口检查时使用Windows平台，不设置offscreen。

应用输出为 `bin/release/Translation.exe`。实际桌面运行前清除测试用的 offscreen 环境变量；确保 PATH 中的 Qt DLL 和编译器 kit 一致，不能混用 MinGW/MSVC 的 DLL。

Windows 应用构建会使用当前 Qt kit 的 `windeployqt`，将匹配配置的 Qt DLL 和平台插件补齐到 `bin/debug` 或 `bin/release`。即使程序无需重新链接，普通构建也会检查并补齐缺少的依赖；修改工程后先重新运行 qmake。部署工具不存在或执行失败时，构建会报错，不会将缺库的输出当作构建成功。

Debug 主窗口可以借助 Qt Creator 的临时运行环境找到 `Qt5SerialPortd.dll` 等库；“初始化虚拟串口”通过管理员权限另起进程，不能依赖这份临时环境，因此依赖必须放在程序旁。此步骤只准备本地构建输出，不创建或同步 Release/Debug 运行包，不修改 `config` 中的用户设置。Debug 仍用于安装了相应编译器调试运行库的开发电脑；对其他电脑交付运行包时另行执行正式打包流程。

默认示例 `examples/dt-simple-commands.json` 编译进程序，无需为启动示例附带外部 `examples/` 目录。可通过“导入配置”选择其他 JSON。

完整趋盛指令目录 `data/qusheng-commands.json` 通过资源文件编译进应用和测试，不依赖运行时的当前目录。修改目录后需要重新构建；用户客户格式和映射仍通过JSON导入导出。

## 初次运行核对

本项目当前默认使用MSVC套件，按本轮改动运行相关测试；MinGW为历史兼容验证，不要求每轮重复。

窗口标题不带括号后缀，顶部没有配置版本栏。主界面只有四页，高级配置页隐藏，参数确认框及省略字段勾选区已移除。底部直接显示“保存配置、导入配置、导出配置”三个同尺寸按钮，不再出现菜单、手动校验或启用操作。修改字段名称、帧头/帧尾、设备参数或添加映射后保存，有效配置应自动启用；重启应恢复修改及映射引用并自动启用。复杂JSON可通过配置文件导入导出处理，已有参数关系和扩展元数据应保留。

配置保存在 `Translation.exe` 旁的 `config/draft.json`。请解压到有写入权限的目录，同一目录运行的程序共用这份文件。需要带走设置时，将 `config` 目录与程序一起复制。恢复会自动启用有效配置，不自动连接串口或发送报文。无效配置显示具体错误并保持未启用，修复后保存即可。

默认示例为dt-simple-commands.json，字段为地址、命令码、位置，映射表只有三列摘要。表格仅用于显示与选择删除目标，下方表单只添加新映射，不编辑选中行；相对映射不带“旧版”前缀。

0x11新建选项为前进、停止、后退、复位。前进/后退输入绝对目标位置；停止/复位固定零距离、不显示距离控件，取值来源默认选“无参数”。Z映射复位后，`/1ZR\r`应正常生成复位帧，无需补数字。已有显式省略配置保持原值。

连接串口、输入报文后可直接发送。运动应答后不自动发0x12，旧相对运动也不做前置查询；等待应答、设备未知或序号回绕均不禁用按钮。日志、目标帧与回告在调试页检查，机械完成与报文应答分开确认。

界面布局详见[简洁界面设计](expert-ui.md)，发送行为详见[报文调试说明](packet-debug.md)。
