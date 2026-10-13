# Windows 构建与运行

可使用 `C:\Qt\Qt5.14.2\5.14.2\mingw73_32` 的 Qt 5.14.2 和 `C:\Qt\Qt5.14.2\Tools\mingw730_32` 的 GCC 7.3.0，或 Qt 的 MSVC2017 kit。应用、核心测试、界面测试共用 `build-encoding.pri`：MSVC 显式 `/utf-8`，MinGW 显式设置输入与执行字符集为 UTF-8。

## 命令目录乱码修正

命令码、JSON 中文名称和悬停说明正常，原来用于拼接的中点、全角括号却显示成“路”“锛?”，因此不是缺少中文字体或目录 JSON 内容损坏。实际用 MSVC2017 14.16.27023、Qt5.14.2、`/utf-8` 做了最小复现：`QStringLiteral("%1 · %2（%3）")` 的拼接标点产生错误字符，且 `%3` 被破坏；相同内容用 `QString::fromUtf8(u8"...")` 转换正确。

命令列表现使用 ASCII 格式 `%1 | %2 (%3)`，保留目录里的中文名称和中文状态，例如 `0x22 | 电磁阀单路控制 (发送待实现)`。其余控件的中文文案明确用 `QString::fromUtf8(u8"...")`；源码中的 `QStringLiteral` 仅用于 ASCII。字体选择不能修复已经生成的错误码点，单独增加 `/utf-8` 也不足以修复这次已复现的 Qt 宏/编译器组合问题。JSON 仍由 JSON 解码器读取，两条链路不应混为一谈。

更新后在 Qt Creator 重新运行 qmake，再使用新的构建目录全量构建；不要直接复用旧目标文件或运行以前输出的 exe。检查“趋盛指令”下拉列表中的 `0x17`、`0x22` 和已实现运动预设，名称、状态和括号应全部正常。应用与测试必须使用同一 kit 的 DLL；切换 MinGW/MSVC 时分别构建。

本次修复实际使用本机 MSVC2017 14.16.27023 / Qt5.14.2 32位和 MinGW GCC7.3.0 / Qt5.14.2 32位分别全量构建应用、核心和界面测试，两套环境均为38项核心、14项界面测试通过，无失败或跳过。新增用截图中的指令独立核对文字，并检查全部59项；两套环境均渲染展开的命令下拉并确认中文正常。

## PowerShell 示例

在项目目录执行。构建在独立目录中进行，避免复用其他 kit 的目标文件：

```powershell
$env:PATH = 'C:\Qt\Qt5.14.2\Tools\mingw730_32\bin;C:\Qt\Qt5.14.2\5.14.2\mingw73_32\bin;' + $env:PATH
New-Item -ItemType Directory -Force build/app,build/core,build/ui,build/sdk-requests,build/sdk-replies | Out-Null
qmake Translation.pro -o build/app/Makefile
mingw32-make -C build/app -j4
qmake tests/tests.pro -o build/core/Makefile
mingw32-make -C build/core -j4
& build/core/release/tst_framework.exe
qmake tests/sdk-requests.pro -o build/sdk-requests/Makefile
mingw32-make -C build/sdk-requests -j4
& build/sdk-requests/release/tst_sdk_requests.exe
qmake tests/sdk-replies.pro -o build/sdk-replies/Makefile
mingw32-make -C build/sdk-replies -j4
& build/sdk-replies/release/tst_sdk_replies.exe
qmake tests/ui-tests.pro -o build/ui/Makefile
mingw32-make -C build/ui -j4
$env:QT_QPA_PLATFORM = 'offscreen'
$env:QT_QPA_FONTDIR = 'C:\Windows\Fonts'
$env:TRANSLATION_RENDER_DIR = (Join-Path (Get-Location) 'build/screenshots')
& build/ui/release/tst_widget.exe
Remove-Item Env:QT_QPA_PLATFORM
Remove-Item Env:QT_QPA_FONTDIR
```

应用输出为 `bin/release/Translation.exe`。实际桌面运行前清除测试用的 offscreen 环境变量；确保 PATH 中的 Qt DLL 和编译器 kit 一致，不能混用 MinGW/MSVC 的 DLL。

程序从应用旁的 `examples/`、项目 `bin/release` 向上两级的 `examples/`，或当前目录的 `examples/` 查找默认示例。发布包必须携带该目录；也可通过中文界面导入自己的 JSON。

完整趋盛指令目录 `data/qusheng-commands.json` 通过资源文件编译进应用和测试，不依赖运行时的当前目录。修改目录后需要重新构建；用户客户格式和映射仍通过JSON导入导出。

## 初次运行核对

本项目当前主要使用MSVC套件，后续常规改动以MSVC构建和相关测试验收；MinGW为历史兼容验证，不要求每轮重复。

保存后重启核对：把字段“位置”改名，点击保存到草稿，退出再启动应保持新名称及全部映射引用。修改帧头/帧尾、客户命令码和设备参数也应恢复；当前会话仍需重新校验、启用和确认。草稿保存在`%LOCALAPPDATA%/Trendwin/Translation/draft.json`，无需为重启保留另行导出。

客户协议格式页应显示帧头 `/`、帧尾 `R\r`、地址/命令码/位置三个中文字段；命令映射页应显示11条验证映射，只有三列摘要；选中后在下方编辑。默认示例为 dt-simple-commands.json，运动与查询统一使用R+回车帧尾；原始DT查询另用dt-query-preview.json，参数直接使用数值，不默认乘十。报文调试页输入 `/1A2000R\r` 后可离线预览。未启用、未确认或未连接时，真实发送按钮应禁用并显示原因。高级配置的完整 JSON 不自动应用，须明确点击应用按钮。取消本地等待不会停止设备。

本轮核心与界面测试均通过；offscreen 的 propagateSizeHints 提示属于测试渲染平台提示，不影响断言或实际 Windows 桌面运行。没有完成真实设备验收。

2026-10-13 SDK集成验证：两套kit均成功构建应用，48项核心、22项界面、71项SDK请求、74项SDK回告通过。SDK测试仅依赖QtCore，不链接Widgets/SerialPort。导入sdk-simple-controls.json检查风扇、阀、DAC、直流与查询；参数表可选择默认值、客户字段或固定值，未映射值由SDK补齐，布局待确认条目仍禁止发送。

映射页保留CMD/名称搜索和完整可选池。待实现目标以短标签标记，可保存标量参数关系。类别筛选、完整目录浏览窗口和协议说明已移除；仅接收报文仍明确标记，禁止下发映射。

0x11提供前进、停止、后退、复位四个可发送选项。前进/后退仅编辑距离来源，停止/复位无距离控件、固定零距离。固定模式、默认速度等说明保留在文档，不再常驻界面。旧相对配置加载时出现临时兼容项，行摘要标“旧版”。字段表、映射表和保存按钮应直接可见；调试页保留预览、发送、原始回告、解析与日志。详见expert-ui.md。

停止/复位开放验证：两套kit均成功构建应用，59项核心、23项界面通过。导入motor-actions.json预览!S\r、!H\r，分别应含11420003E8000000、11440003E8000000载荷。真实操作须启用并确认设备参数。停止同一电机运动时可直接发送映射好的停止报文；停止仅替代本地运动等待，设备停稳仍由回告及查询确认。
