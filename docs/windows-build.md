# Windows 构建与运行

本轮实际使用 `C:\Qt\Qt5.14.2\5.14.2\mingw73_32` 的 Qt 5.14.2 和 `C:\Qt\Qt5.14.2\Tools\mingw730_32` 的 GCC 7.3.0。MSVC kit 也保留 `/utf-8`，但本轮没有验证 MSVC 构建。

## PowerShell 示例

在项目目录执行。构建在独立目录中进行，避免复用其他 kit 的目标文件：

```powershell
$env:PATH = 'C:\Qt\Qt5.14.2\Tools\mingw730_32\bin;C:\Qt\Qt5.14.2\5.14.2\mingw73_32\bin;' + $env:PATH
New-Item -ItemType Directory -Force build/app,build/core,build/ui | Out-Null
qmake Translation.pro -o build/app/Makefile
mingw32-make -C build/app -j4
qmake tests/tests.pro -o build/core/Makefile
mingw32-make -C build/core -j4
& build/core/release/tst_framework.exe
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

## 初次运行核对

客户协议格式页应显示帧头 `/`、帧尾 `R\r`、地址/命令码/位置三个中文字段；命令映射页应显示 A 到绝对位置运动。报文调试页输入 `/1A2000R\r` 后可离线预览。未启用、未确认或未连接时，真实发送按钮应禁用并显示原因。高级配置的完整 JSON 不自动应用，须明确点击应用按钮。取消本地等待不会停止设备。

本轮核心与界面测试均通过；offscreen 的 propagateSizeHints 提示属于测试渲染平台提示，不影响断言或实际 Windows 桌面运行。没有完成真实设备验收。
