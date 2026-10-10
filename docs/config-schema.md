# 协议配置说明（schemaVersion 1）

配置文件包含 external、mappings 和 device。普通用户通过中文界面编辑；“高级配置”页显示完整 JSON。

## 客户报文 external

- encoding：ascii 或 hex。
- header、footer：必填。ASCII 可写 \r、\n、\t、\xNN；HEX 写偶数个十六进制字符。
- byteOrder：big 或 little，只作用于 HEX 输入中的多字节整数。
- fields：按数组顺序连续读取，不使用普通用户可见的 offset。

字段属性：

- name：唯一字段名称。
- role：command（命令码）、parameter（参数）或 fixed（固定字段）。必须且只能有一个命令码字段。
- type：ascii、uint、int。
- lengthMode：fixed 或 toFooter。toFooter 只能位于最后一项。
- length：固定长度字段的字节数。
- fixedValue：固定字段必须匹配的值。
- min、max：可选范围。

ASCII 中的整数按十进制字符解析。HEX 中的整数按字节解析并使用 byteOrder。

## 命令映射 mappings

每条映射包含：

- commandCode：与命令码字段比较。
- operation：当前只能是 ABSOLUTE_MOVE。
- responsePolicy：ack 表示收到命令应答即可产生一次成功结果，但后台仍维护设备忙闲；complete 表示等到动作完成。
- parameters.positionUm：统一位置参数，单位为微米。

参数关系支持 field 直接引用、constant 固定值和 linear 线性换算。线性换算按“来源字段 × numerator ÷ denominator + offset”计算，round 可为 exact、floor、ceil、nearest。除零、溢出和 exact 产生余数都会拒绝生成下位机命令。

## 设备配置 device

- destination、source：趋盛帧目标地址和源地址，二者不能相同。
- devId：当前电机 DevID，范围 0x11 到 0x1C。
- absoluteAction：绝对位置运动动作值，当前编码器接受 1 或 3。
- speedUmPerS：微米/秒。
- minPositionUm、maxPositionUm：设备行程边界，微米。

“已经核对”不存入 JSON。程序每次启动或启用新配置后，都要求用户在“设备与连接”页重新勾选确认项。

## DT 风格示例

示例使用帧头 /、帧尾 R\r，依次读取地址 1 字节、命令码 1 字节、位置到帧尾。命令码 A 的线性换算由 JSON 定义。它不代表程序内置 DT 能力。

## 首版严格约束和界面说明

字段表的“顺序”由数组顺序决定，不能填写偏移；“读取到帧尾”只能放在最后。固定长度以**字节**为单位。ASCII 中无符号整数只接受十进制数字；有符号整数可有前导负号，不接受空白、正号、小数或混入其他字符。HEX 中的整数字段固定为 1–8 字节，使用高级配置中的 byteOrder；统一计算范围为有符号 64 位整数，过大输入拒绝转换。ASCII 字段只接受 ASCII 字节。

固定字段必须给出 fixedValue；数字固定值按数值核对，ASCII 固定值按原文核对且必须满足固定长度。min/max 只适用于整数字段，必须是 JSON 数字形式的整数，范围限制在 ±9007199254740991 以内以保证保存精度，无符号下限不得为负数。字段名称不能空白、带首尾空格或重复。固定值、数值范围和长度等无关单元格会按字段作用/类型隐藏或禁用。

位置固定值必须为 0–16777214 的整数。线性换算的分子允许 -1000000000 至 1000000000（含零），分母必须为 1–1000000000，偏移默认 0、范围 ±1000000000；必须明确舍入方式。所有乘法、偏移和设备行程均在趋盛组帧前检查。首版只映射 positionUm，速度和动作值统一来自设备参数，防止同一参数在两个页面出现不同来源。

ASCII 帧边界和输入框支持 `\r`、`\n`、`\t`、`\xNN`（ASCII 范围内）及双反斜杠；非法转义和中文输入明确拒绝。HEX 允许字节间空格，禁止奇数长度或非十六进制字符。

完整帧不超过 4096 字节。全固定长度的通用分帧器按配置长度定位帧尾，允许参数字节中出现与帧尾相同的字节。读取到帧尾模式不支持转义分隔符，尾字段不能包含完整的帧尾序列；复杂长度字段或转义协议留待后续扩展。

配置校验不等于启用。表单修改后点击“保存到草稿”，高级 JSON 修改后点击“应用 JSON 到草稿”，然后校验、启用。当前已启用版本是不可变快照；设备确认项不保存在 JSON，启用、改动、重连后须重新确认。未保存改动会阻止校验、启用、导出和预览旧草稿，发送按钮持续显示具体中文原因。

旧实验配置中 external.format、字段 offset/endian 等结构不再兼容，须重建为 encoding、顺序 fields 和 commandCode 映射。示例文件已经全部更新为通用结构。保留 schemaVersion 是为了以后明确迁移，而不是静默把旧配置解释成新语义。
