# 协议配置说明（schemaVersion 1）

普通用户主要通过中文页面编辑。完整 JSON 位于高级配置，修改后须明确“应用 JSON 到草稿”。保存、校验和启用彼此独立，顶部显示草稿和已启用版本。

## 共用客户格式 external

| 属性 | 中文含义与边界 |
|---|---|
| encoding | ascii 或 hex，决定输入框如何转成字节 |
| header、footer | 必填帧头和默认帧尾；ASCII 支持 `\r`、`\n`、`\t`、`\xNN`，HEX 填偶数个十六进制字符 |
| byteOrder | big 或 little，只影响 HEX 多字节整数 |
| fields | 有顺序的字段数组，连续读取，不填写 offset |

字段的 name 必须唯一；role 为 command（命令码）、parameter（参数）或 fixed（固定字段），整个格式必须只有一个命令码字段。type 支持 ascii、uint、int。

lengthMode 支持 fixed（固定长度）、toFooter（读取到帧尾）和 mappedCommand（按保存的命令码识别）。固定字段 length 是字节数；toFooter 只能位于最后。mappedCommand 仅用于 ASCII 命令码，之前只能有固定长度字段；按配置中客户命令码最长前缀识别，之后严格核对剩余字段和帧尾，不能用短命令吞掉多余参数。

fixedValue 是固定字段必须匹配的值；min/max 只适用于整数。ASCII 整数只接受十进制数字，有符号整数可有负号，不接受空白、正号、小数。HEX 整数固定1–8字节，使用 byteOrder；统一计算范围为有符号64位，超范围拒绝。JSON 数值范围限于可精确保存的整数 ±9007199254740991。

## 命令映射 mappings

| 属性 | 含义 |
|---|---|
| commandCode | 配置定义的客户命令码；不可为空、重复或超过128字符 |
| operation | 受支持的趋盛目标模板，见下表 |
| responsePolicy | ack：设备接纳后产生一次结果；complete：等待运动完成。查询只能为 ack |
| parameters | 参数关系对象；查询使用空对象 |
| input.omitFields | 此命令不读取的共用字段名称，仅允许命令码之后的字段，不能省略地址前缀或命令码 |
| input.footer | 可选独立帧尾；缺省时用 external.footer |

使用独立输入设置必须把命令码长度方式设为 mappedCommand。省略字段仍由名称指定，不按操作猜测报文结构。映射不能引用已经省略的字段。

| operation | 参数关系 | 趋盛发送与状态 |
|---|---|---|
| ABSOLUTE_MOVE | positionUm | 0x11，绝对位置模式4；完成时0x12核对空闲和目标 |
| RELATIVE_FORWARD | distanceUm | 先0x12核对当前位置和终点，再0x11，位置模式2、动作1 |
| RELATIVE_BACKWARD | distanceUm | 先0x12核对当前位置和终点，再0x11，位置模式2、动作3 |
| QUERY_MOTOR | 空对象 | 0x12，数据为电机DevID；显示状态、电机位置和独立负反馈位置 |
| QUERY_DEVICE_INFO | 空对象 | 0x04，数据0000；显示硬件、IAP、主控版本及设备标识 |

参数关系保持 field（直接引用）、constant（固定值）、linear（线性换算）三种。常用界面只选择来源/固定值和换算倍数。有限位数的十进制倍数精确转换成整数分数，避免浮点舍入改变目标值。既有高级分数修改无关选项后仍保持原值。

高级 linear 公式为 来源 × numerator ÷ denominator + offset，round 可为 exact、floor、ceil、nearest。分子范围 ±1000000000，分母1–1000000000，偏移默认0、范围 ±1000000000；除零、溢出、exact产生余数均拒绝生成目标帧。固定值0–16777214；0xFFFFFF是趋盛特殊持续运行值，本轮禁止作为位置或距离。

## 数值与设备 device

本轮默认示例不解决客户和趋盛设备物理单位对应问题，输入数值直接引用。例如输入2000得到positionUm=2000；不擅自按客户步距乘十。参数名保留历史及趋盛线上单位约定，线上的位置/距离字段仍是24位大端整数，速度亦是24位。将来确认单位后在配置中编辑换算，不修改客户解析器。

设备属性：destination/source为不同且非零的板地址；devId范围0x11–0x1C；absoluteAction为1或3；speedUmPerS为正整数；minPositionUm/maxPositionUm为有效行程边界，最大不超过16777214。速度和运动方向来自设备/模板，不能在两处重复设置。

绝对位置在预览和组帧前检查行程。相对距离先检查不超过行程跨度，实发再查询当前值，只有设备已回零且空闲、当前值和计算终点均在行程内才发运动。前置查询失败不会发送0x11，也不自动重试运动。

人工设备确认不写入 JSON，每次启动、修改/启用配置或重连后须在中文界面重新勾选。

## 示例与兼容

默认 `dt-simple-commands.json` 包含11条由配置描述的运动和查询。运动使用默认帧尾 `R\r`；查询省略“位置”，使用独立帧尾 `\r`。所有客户命令字符只在配置、文档和测试中。DT不是内置解析分支。

`dt-preview.json` 是上一阶段的固定1字节命令码/十倍换算回归；其含义不变，仍可加载。ASCII/HEX旧顺序字段配置继续兼容；旧实验 external.format、offset/endian字段、双连接结构明确拒绝，不能静默解释成新配置。

完整帧与缓存均不超过4096字节。全固定长度分帧按长度定位帧尾，允许二进制参数中出现同值尾字节；toFooter模式不支持转义分隔符，尾字段不能包含完整帧尾序列。新可变命令模式在半包、粘包中重新按保存映射确定字段布局与边界。
