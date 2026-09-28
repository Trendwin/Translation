# Translation

Qt 5.14.2 / C++11 protocol conversion application. The current first stage provides a configuration driven external parser and semantic mapper, a guarded Qusheng encoder, two serial connections, and an offline preview. Read [the protocol basis and acceptance record](docs/phase1.md) before enabling real hardware.

## Start without hardware

1. Open `Translation.pro` with the installed Qt 5.14.2 kit and build. `tests/tests.pro` is the Qt Test project.
2. The app loads `examples/dt-preview.json` as an editable draft. Enter `/1A2000R\r`, then click **预览（不发送）**. The example values are illustrative and cannot enable real send.
3. Import `examples/ascii-preview.json` and preview `!MOVE,2000\r`. Import `examples/binary-preview.json`, switch input to HEX, and preview `AA 55 01 D0 07 E6 48`. Both produce the same unified absolute target and target frame for sequence zero. Click **启用配置** only to bind the validated draft as the active snapshot; preview is still safe.
4. Edit fields in **字段编辑**, and mappings in **映射编辑**. Click **应用…到草稿**, validate, then explicitly enable the new version. Raw JSON editing remains available for device profile, CRC, response templates and advanced expressions. Import and export use UTF-8 JSON.
5. A simulated full device reply can be entered in **模拟设备回告 HEX** to inspect decoding. A successful 0x11 reply proves command acceptance only. Real sending and bridging require confirmed configuration flags plus explicitly connected downstream/upstream serial ports.

The app never invents address, DevID, speed, unit calibration or stroke limits. The included example numbers must not be used for a real pump. See [configuration format](docs/config-schema.md) and [Windows build notes](docs/windows-build.md).
