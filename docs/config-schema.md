# 协议配置说明（schemaVersion 1）

普通用户主要通过中文页面编辑。完整 JSON 位于高级配置，修改后须明确“应用 JSON 到草稿”。保存、校验和启用彼此独立，顶部显示草稿和已启用版本。

## 顺序字段与格式调试

界面只有原九列顺序字段表，schemaVersion仍为1。`ProtocolConfig::validateFormat`仅检查格式；`ConversionEngine::parsePreview`只输出字段与提示。`convert`复用同一解析结果，继续校验映射、SDK和设备。零条mappings允许保存、导出和独立解析，不要求device；转换未匹配命令时明确失败。

模板/样例格式已停用，`external.mode: "textTemplate"`或仍含`external.textFormat`均返回`UNSUPPORTED_FORMAT`。导入这种文件不会覆盖当前草稿或原文件。启动加载时可查看其保留的`external.fields`，但显式保存前禁止解析和发送；保存时移除已知废弃键并把原值保留于根`retiredFormatBackup`。读取不会自动写回。其余未知根、external、device和字段扩展随表单保存保留。

## 本地草稿与启动恢复

格式、命令映射和设备参数属于同一份草稿。表单“保存到草稿”、应用高级JSON、导入配置会写入本地`draft.json`，内容仍为原协议JSON，不新增用户需要编辑的存储开关。Windows默认路径为`%LOCALAPPDATA%/Trendwin/Translation/draft.json`，不依赖exe目录或当前工作目录。

草稿落盘只要求JSON对象可保存，不要求运行配置已通过校验，未完成的映射也能重启继续编辑。使用QSaveFile先完整写入再原子替换；写入失败保留上次文件，界面保持未保存并阻止用旧校验结果启用当前编辑。

启动优先恢复本地草稿；无草稿时加载示例。损坏或无法读取时显示中文恢复原因，保留原文件，不自动覆盖。恢复的是编辑草稿，启用快照和设备确认仍属于当前运行会话，真实发送需重新校验、启用、连接并确认。

## 共用客户格式 external

| 属性 | 中文含义与边界 |
|---|---|
| encoding | ascii 或 hex，决定输入框如何转成字节 |
| header、footer | 必填共用帧头和帧尾；ASCII 支持 `\r`、`\n`、`\t`、`\xNN`，HEX 填偶数个十六进制字符 |
| byteOrder | big 或 little，只影响 HEX 多字节整数 |
| fields | 有顺序的字段数组，连续读取，不填写 offset |

字段的 name 必须唯一，整个格式必须只有一个command命令码字段。role支持command（命令码）、parameter（可映射参数）、fixed（固定字段）、metadata（普通字段）、execution（执行控制仅解析）、checksum（校验值未校验）。type支持ascii、uint、int、integerList。integerList仅允许ASCII的parameter字段，按英文逗号拆分严格有符号十进制整数，单项合法；min/max逐项检查。execution字段存在于实际输入时拒绝转换发送；checksum仅提取，不验证客户校验算法。

普通字段表改名会同步同一字段的映射来源及省略设置，JSON中的引用名称随保存更新。数字参数来源必须是role=parameter且type为uint/int/integerList的字段（列表必须明确itemIndex）；删除、类型或作用变化造成失效时，错误包含客户命令码、目标参数和来源字段。JSON整体替换需自行保持名称引用一致，详见[field-adaptation.md](field-adaptation.md)。

lengthMode支持fixed（固定长度）、toFooter（读取到帧尾）、mappedCommand（按保存命令码识别）和delimited（读到分隔符）。fixed使用length字节数，delimited使用delimiter字符串，两者在界面共用“长度 / 分隔符”一列。delimited读取到第一次出现的分隔符并消耗该分隔符，字段值不能为空；分隔符按encoding严格解码，必须1～128字节、不能包含完整帧尾。toFooter只能位于最后。mappedCommand仅用于ASCII命令码，前面可放固定或按分隔符读取的字段；按已保存命令码最长前缀识别，之后仍严格核对剩余字段和帧尾。命令码本身使用delimited时无需映射即可独立提取未知命令。

fixedValue 是固定字段必须匹配的值；min/max 只适用于整数。ASCII 整数只接受十进制数字，有符号整数可有负号，不接受空白、正号、小数。HEX整数固定1–8字节，或按分隔符读取且实际不超过8字节，使用byteOrder；统一计算范围为有符号64位，超范围拒绝。JSON 数值范围限于可精确保存的整数 ±9007199254740991。

## 命令映射 mappings

| 属性 | 含义 |
|---|---|
| commandCode | 配置定义的客户命令码；不可为空、重复或超过128字符 |
| operation | 趋盛目录中的下发目标；已有模板见下表，其他项为 QUSHENG_XX（XX为大写两位指令码） |
| parameters | 参数关系对象；查询使用空对象 |
| input.parameterCounts | 对象：每个有效、未省略的integerList字段必须指定1～2048的输入项数；实际数量必须完全相同，与目标参数个数无关 |
| input.omitFields | 此命令不读取的共用字段名称，仅允许命令码之后的字段，不能省略地址前缀或命令码 |

使用字段省略设置必须把命令码长度方式设为mappedCommand或delimited。省略字段仍由名称指定，不按操作猜测报文结构。映射不能引用已经省略的字段。

| operation | 参数关系 | 趋盛发送与状态 |
|---|---|---|
| MOTOR_FORWARD | positionUm、可选devId | 0x11，固定绝对模式4、动作1；速度沿用默认、电机可按映射设置 |
| MOTOR_STOP | 可选devId | 固定绝对模式4、动作2、距离0；应答后不自动查询 |
| MOTOR_BACKWARD | positionUm、可选devId | 0x11，固定绝对模式4、动作3；速度沿用默认、电机可按映射设置 |
| MOTOR_RESET | 可选devId | 固定绝对模式4、动作4、距离0；应答不表示机械动作完成 |
| ABSOLUTE_MOVE（旧版兼容） | positionUm | 绝对模式4、设备absoluteAction；应答后不自动查询机械完成 |
| RELATIVE_FORWARD（旧版兼容） | distanceUm | 先0x12核对当前位置和终点，再0x11，位置模式2、动作1 |
| RELATIVE_BACKWARD（旧版兼容） | distanceUm | 先0x12核对当前位置和终点，再0x11，位置模式2、动作3 |
| QUERY_MOTOR | 空对象或devId | 0x12，未映射编号时使用默认值；显示状态、电机位置和独立负反馈位置 |
| QUERY_DEVICE_INFO | 空对象 | 0x04，数据0000；显示硬件、IAP、主控版本及设备标识 |

目录元数据保存于 `data/qusheng-commands.json`，随程序资源编译，不写入用户的客户协议格式。普通下发项例如 `QUSHENG_81`（风扇控制）可以选择并保存到 `mappings`；数字参数使用SDK定义的稳定英文 `key`。尚未设置的参数不写入 `parameters`，运行时由SDK默认规则补齐，不用零值代替。结构和字节数据本轮只展示说明。待实现映射只保存 `ack` 策略，不表示已实现回告解析或动作完成判断。

0x11新建选项只有四种动作。模式、动作、速度不可加入其parameters映射；前进/后退positionUm必须完整，停止/复位距离固定0。四动作与旧ABS/REL均允许可选devId关系：常用界面选择设备默认（不写关系）或0x11–0x16的固定编号，例如 `"devId":{"kind":"constant","value":18}`。高级JSON可引用整数参数字段，转换时同样检查编号范围。未设置时保留按操作SDK预置、设备默认、SDK初始值的回退顺序。旧停止/复位草稿的positionUm关系兼容读取但不求值，界面载入/保存时清理，不能影响固定零距离。旧QUSHENG_11通用参数关系仍可加载保存，但仅供兼容且不能生成目标帧。软件不静默迁移用户的相对移动配置。

配置校验检查目录身份、方向、参数名和已填写表达式；允许待实现参数草稿未完成。0x17、0x45、0xD9、0xE6 为仅接收报文，不能作为映射目标。未列入目录的操作会拒绝。转换命中目录中未实现执行器的操作时返回中文“发送待实现”，不生成目标帧；JSON添加可执行标志不能绕过此限制。已支持模板继续执行原有严格校验。

参数关系保持 field（直接引用）、constant（固定值）、linear（线性换算）三种。常用界面只选择来源/固定值和换算倍数。有限位数的十进制倍数精确转换成整数分数，避免浮点舍入改变目标值。既有高级分数修改无关选项后仍保持原值。

简洁表单不再提供偏移/取整控件；已有表达式的offset、round和精确分数保留，普通编辑不会将它们改成零或默认取整。所有命令统一使用external.footer；勾选的省略字段继续写入input.omitFields。旧responsePolicy的ack/complete可在核心加载，但运行均立即反馈；界面载入和保存会清除responsePolicy与input.footer，不保留隐藏的旧控制项。核心校验拒绝独立帧尾覆盖。

高级 linear 公式为 来源 × numerator ÷ denominator + offset，round 可为 exact、floor、ceil、nearest。分子范围 ±1000000000，分母1–1000000000，偏移默认0、范围 ±1000000000；除零、溢出、exact产生余数均拒绝生成目标帧。运动固定值0–16777214；0xFFFFFF是趋盛特殊持续运行值，软件运动链路禁止作为位置或距离。普通SDK参数按自身类型/范围校验，例如A1的PWM允许-128～127，阀持续时间可为32位无符号整数，不能套用位置范围。

## 数值与设备 device

本轮默认示例不解决客户和趋盛设备物理单位对应问题，输入数值直接引用。例如输入2000得到positionUm=2000；不擅自按客户步距乘十。参数名保留历史及趋盛线上单位约定，线上的位置/距离字段仍是24位大端整数，速度亦是24位。将来确认单位后在配置中编辑换算，不修改客户解析器。

设备属性：destination/source为不同且非零的板地址；devId范围0x11–0x16；speedUmPerS为默认运动速度正整数；minPositionUm/maxPositionUm为有效行程边界，最大不超过16777214。仅包含旧ABSOLUTE_MOVE时要求absoluteAction为1或3；新四种动作方向固定，无需填写此旧字段。默认速度随运动帧发送，暂不支持从其他客户报文缓存速度。

绝对位置在预览和组帧前检查行程。相对距离先检查不超过行程跨度，实发再查询当前值，只有设备已回零且空闲、当前值和计算终点均在行程内才发运动。前置查询失败不会发送0x11，也不自动重试运动。

人工设备确认不写入 JSON，每次启动、修改/启用配置或重连后须在中文界面重新勾选。

## 示例与兼容

默认 `dt-simple-commands.json` 包含11条由配置描述的运动和查询。A/a使用新前进模板，字节保持原绝对模式/动作1；P/p、D/d保留旧相对语义。这份验证示例统一使用帧尾 `R\r`，查询仅省略“位置”。原始DT查询另用 `dt-query-preview.json`，共用帧尾为 `\r`。所有客户命令字符只在配置、文档和测试中。DT不是内置解析分支。

`command-catalog-draft.json` 展示另一个自行定义的客户格式：`!MOVE2000\r` 可预览绝对位置运动，`!FAN50\r` 命中风扇控制草稿并显示发送待实现。固定风扇编号224仅为编辑演示，不代表实际设备已确认；目录参数范围不替代客户字段取值限制。

`dt-preview.json` 是上一阶段的固定1字节命令码/十倍换算回归；其含义不变，仍可加载。ASCII/HEX旧顺序字段配置继续兼容；旧实验 external.format、offset/endian字段、双连接结构明确拒绝，不能静默解释成新配置。

完整帧与缓存均不超过4096字节。全固定长度分帧按长度定位帧尾，允许二进制参数中出现同值尾字节；带delimited字段的正文不能包含完整帧尾序列，否则报多帧/边界错误。toFooter尾字段也不能包含完整帧尾。半包等待后续数据，粘包逐帧处理，字段分隔错误不沿用上次解析结果。详见[顺序字段说明](sequential-fields.md)。

## SDK默认参数

根对象可选sdkDefaults，以operation为键、各SDK参数数值为值。仅简单接口参与运行；数值类型、枚举、通道和组合关系由SDK校验。参数映射明确设置的值优先，未映射时使用SDK预置、设备参数或SDK软件初始值。参数表的“默认值”不生成field/constant表达式。

四种0x11模板的SDK预置仅devId/speedUmPerS，模式和动作固定；前进/后退位置通过映射提供，停止/复位距离固定0。0x01使能掩码必须明确提供。普通SDK指令不要求填写positionUm，允许只映射百分比、持续时间、电压或PWM。未知参数、固定请求字节覆盖、保留位及不合法通道在组帧前拒绝。详见[SDK架构与使用](qusheng-sdk-design.md)。

## XYZ虚拟模块（0xC1）

`operation: "QUSHENG_C1"` 的 `parameters` 只允许 `moduleIndex`、`xPosition`、`yPosition`、`zPosition` 四个可编辑关系，四项都须明确提供（也可由显式SDK预置提供）。编号0–255，位置0–8388607，允许明确固定0，不隐式补0。三轴可引用同一客户数值字段或分别引用不同整数参数字段，不改变客户顺序字段的读取方式。

SDK固定 `moduleType=1`，拒绝类型2。`xSpeed/ySpeed/zSpeed` 不能作为客户参数映射；各自按 `sdkDefaults.QUSHENG_C1`、`device.speedUmPerS`、软件初始1000补齐，范围0–8388607，显式0保持为0。单位由下位机配置决定。`targetDevice` 使用 `xyz:<moduleIndex>`，不会当作物理电机编号；全部补齐参数进入请求快照。完整布局、回告和示例见[XYZ说明](virtual-module-xyz.md)。

## 整数列表索引

`field`和`linear`表达式引用integerList时必须有从1开始的整数`itemIndex`，不得超过本映射`input.parameterCounts[field]`。标量来源及constant不允许itemIndex。例：`{"kind":"field","field":"values","itemIndex":2}`。列表字段改名时同步来源和数量键；删除、改类型不会静默删除索引或改绑。省略列表不能保留该列表的数量约束。

逗号仅是列表内部规则；字段仍按fixed/delimited/toFooter读取。列表不接受空项、空白、正号、小数或64位溢出；负值是否可发送由字段范围和目标SDK范围共同决定。格式独立解析不依赖映射项数；转换和调试实发严格检查数量。完整帧最多4096字节，配置也检查列表及命令码的最短帧预算，实际可容纳项数取决于外层格式。详见[列表使用说明](integer-list-parameters.md)。

物理电机范围按本设备六路限定为0x11～0x16（0x1A速度接口仍0x11～0x13）。设备默认、SDK预置和映射固定编号在配置校验检查，动态来源在转换和SDK编码时检查。导入先检查物理编号并拒绝非法值；启动恢复原始值，不自动clamp，显式重选前禁止表单保存和发送，但独立解析不依赖设备配置。虚拟模块编号不受物理电机范围影响。

界面将列表索引直接显示在“取值来源”下拉：单个列表为“第N个参数”，多个数字来源时带字段名。SDK表保持三列，不再使用独立项号控件；候选项同时保存field与itemIndex，数量变化保留原索引及失效状态。此简化不改变JSON字段、数量约束或换算表达式。
