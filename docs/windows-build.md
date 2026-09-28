# Windows / Qt 5.14.2 build

The project uses Qt Widgets, SerialPort, Qt Test and C++11. Source files are UTF-8 without BOM; `win32-msvc` enables `/utf-8`. The local verified kit was **Qt 5.14.2 MinGW 7.3.0 32-bit**. Open `Translation.pro` in Qt Creator, run qmake, then build Debug or Release. For tests, open `tests/tests.pro`, run qmake and execute `tst_framework`.

An equivalent PowerShell build uses the Qt `qmake.exe` and `mingw32-make.exe` of the same kit. Put both `C:\Qt\Qt5.14.2\5.14.2\mingw73_32\bin` and `C:\Qt\Qt5.14.2\Tools\mingw730_32\bin` on `PATH` before invoking qmake. Use separate shadow build directories for the app and tests. The `.pro` file places the app binary under `bin/debug` or `bin/release`; the default JSON example is loaded relative to this location.

The UI starts in offline preview mode. Import a JSON example, inspect or edit its fields and mappings, validate, then explicitly enable the configuration. The real send control stays disabled until the device profile and conversion/CRC confirmation flags are set and the downstream serial port is connected. The bridge additionally requires a distinct upstream serial port and its own enable checkbox. Keep upstream and downstream serial parameters independent. Ordinary cancellation ends local waiting and does not issue a stop command.

If Qt Creator displays garbled Chinese text, clean and rebuild the exact active Kit and verify the EXE launch path rather than converting strings through a local code page. The default port list is refreshed without opening a device. A selected port name is passed to `QSerialPort`; its description is display only.
