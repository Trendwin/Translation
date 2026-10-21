#include "qushengsdk.h"
#include <QRegularExpression>
#include <cmath>

// 阅读顺序：definitions登记协议 → buildRequest补齐/校验 → encode生成D0...Dn。
// 新增指令须同时补定义、编码及sdkreplies中的回告布局，不能只添加可选名称。
namespace {
using Support = QushengSdkSupport;
const Support Simple = Support::Simple, Deferred = Support::Deferred,
              LayoutPending = Support::LayoutPending, ReceiveOnly = Support::ReceiveOnly;

// 将紧凑的登记项转成统一元数据，保证界面默认提示与编码器使用同一参数定义。
QushengParameterDefinition p(const char *key, const char *label, const char *type,
                            qint64 minimum, qint64 maximum, QVariant initial = QVariant(),
                            QString deviceKey = QString(), bool required = false,
                            QList<qint64> allowed = {})
{
    QushengParameterDefinition result;
    result.key = QString::fromLatin1(key); result.label = QString::fromUtf8(label);
    result.type = QString::fromLatin1(type); result.minimum = minimum; result.maximum = maximum;
    result.defaultValue = initial; result.deviceKey = deviceKey;
    result.required = required; result.allowedValues = allowed;
    return result;
}

QushengParameterDefinition motor(int maximum = 0x1c)
{
    // 普通步进范围11～1C；0x1A仅支持11～13，通过maximum收窄而非编码时截断。
    return p("devId", u8"电机编号", "uint8", 0x11, maximum, 0x11, QStringLiteral("devId"));
}

// 这里是独立SDK的协议登记表，而不是客户命令匹配表。所有协议能力和参数范围都从
// 本表读取；未开放的接口仍保留参数定义，但不能通过传入原始字节绕过能力检查。
QMap<quint8, QushengCommandDefinition> definitions()
{
    QMap<quint8, QushengCommandDefinition> table;
    auto add = [&table](quint8 code, const char *name, Support support,
                        QList<QushengParameterDefinition> parameters = {}, bool query = false) {
        QushengCommandDefinition command;
        command.code = code; command.name = QString::fromUtf8(name); command.support = support;
        command.parameters = parameters; command.query = query; table.insert(code, command);
    };
    // 功能掩码会触发板卡重新初始化。没有能替用户决定的初始值，零值也不是“无操作”。
    add(0x01, u8"设备功能使能", Simple, {p("enableMask", u8"功能使能位", "uint16", 0, 65534, QVariant(), QString(), true)});
    add(0x03, u8"基本状态查询", Simple, {}, true);
    add(0x04, u8"设备信息查询", Simple, {}, true);
    add(0x05, u8"紧急停止", Simple, {p("devId", u8"设备编号", "uint8", 0, 255, 255)});
    add(0x08, u8"心跳监控设置", Simple, {p("intervalMs", u8"心跳间隔", "uint16", 0, 65535, 0)});
    add(0x09, u8"错误主动上报设置", Simple, {p("enabled", u8"启用上报", "uint8", 0, 1, 1)});
    add(0x0c, u8"主通信配置查询", Simple, {}, true);
    // 距离/速度的单位来自趋盛协议（微米或0.01度），SDK不推断客户单位或自动换算。
    // 0xFFFFFF是协议的不停止标记，SDK保留完整24位；软件运动适配器仍限制自身业务范围。
    add(0x11, u8"步进电机运动控制", Simple, {motor(),
        p("mode", u8"工作模式", "uint8", 1, 4, 4, QString(), false, {1, 2, 4}),
        p("action", u8"电机动作", "uint8", 1, 4, 1),
        p("speedUmPerS", u8"移动速度", "uint24", 0, 0xffffff, 1000, QStringLiteral("speedUmPerS")),
        p("positionUm", u8"移动距离", "uint24", 0, 0xffffff, QVariant(), QString(), true)});
    add(0x12, u8"电机信息查询", Simple, {motor()}, true);
    add(0x14, u8"步进配置查询", Simple, {motor()}, true);
    add(0x16, u8"步进运行参数查询", Simple, {motor()}, true);
    add(0x1a, u8"运行中速度修改", Simple, {motor(0x13),
        p("speed", u8"移动速度", "uint24", 0, 0xffffff, 1000, QStringLiteral("speedUmPerS"))});
    add(0x21, u8"电磁阀全组控制", Simple, {
        p("mode", u8"控制方式", "uint8", 0, 2, 0),
        p("valveMask", u8"阀门位图", "uint30", 0, 0x3fffffff, 0),
        p("duration", u8"开启时间", "uint32", 0, 4294967295LL, 0)});
    // 模块编号有独立地址空间：不能把设备页的步进编号11传到阀/采样/风扇接口。
    add(0x22, u8"电磁阀单路控制", Simple, {p("devId", u8"阀门编号", "uint8", 0x31, 0x4e, 0x31),
        p("duration", u8"开启时间", "uint32", 0, 4294967295LL, 0)});
    add(0x23, u8"电磁阀状态查询", Simple, {}, true);
    add(0x24, u8"电磁阀超时设置", Simple, {
        p("group1Timeout", u8"第一组超时", "uint32", 0, 4294967295LL, 0),
        p("group2Timeout", u8"第二组超时", "uint32", 0, 4294967295LL, 0),
        p("group3Timeout", u8"第三组超时", "uint32", 0, 4294967295LL, 0)});
    add(0x25, u8"电磁阀参数查询", Simple, {}, true);
    add(0x44, u8"模数采样查询", Simple, {p("devId", u8"采样通道", "uint8", 0x61, 0x68, 0x61)}, true);
    add(0x46, u8"模数上报间隔设置", Simple, {p("devId", u8"采样通道", "uint8", 0x61, 0x68, 0x61),
        p("intervalMs", u8"上报间隔", "uint24", 0, 0xffffff, 0)});
    add(0x54, u8"数模输出设置", Simple, {p("devId", u8"输出通道", "uint8", 0x71, 0x78, 0x71),
        p("voltageMv", u8"输出电压", "uint24", 0, 0xffffff, 0)});
    add(0x63, u8"电源上电时序设置", Simple, {
        p("peripheralDelay", u8"外设上电延时", "uint32", 0, 4294967295LL, 0),
        p("power24Delay", u8"24伏上电延时", "uint32", 0, 4294967295LL, 0),
        p("power12Delay", u8"12伏上电延时", "uint32", 0, 4294967295LL, 0),
        p("power5Delay", u8"5伏上电延时", "uint32", 0, 4294967295LL, 0)});
    add(0x64, u8"电源上电时序查询", Simple, {p("devId", u8"电源模块", "uint8", 0xc0, 0xdf, 0xc5)}, true);
    add(0x65, u8"电源控制", Simple, {p("devId", u8"电源模块", "uint8", 0xc5, 0xc7, 0xc5),
        p("enabled", u8"开启电源", "uint8", 0, 1, 0)});
    const auto fan = []() { return p("devId", u8"风扇编号", "uint8", 0xe0, 0xe2, 0xe0); };
    add(0x81, u8"风扇控制", Simple, {fan(), p("percent", u8"转速百分比", "uint8", 0, 100, 0)});
    add(0x82, u8"风扇配置查询", Simple, {fan()}, true);
    add(0x83, u8"风扇速度查询", Simple, {fan()}, true);
    // PWM阈值尺度与测速间隔单位原文未明确；按实际字节宽度保存数值，不能假定百分比/毫秒。
    add(0x84, u8"风扇配置设置", Simple, {fan(),
        p("speedMode", u8"测速方式", "uint8", 0, 1, 0),
        p("pulsesPerRevolution", u8"每转脉冲数", "uint8", 1, 255, 1),
        p("filterWindow", u8"滤波窗口", "uint8", 0, 255, 1),
        p("minOutput", u8"输出低阈值", "uint8", 0, 255, 0),
        p("maxOutput", u8"输出高阈值", "uint8", 0, 255, 255),
        p("measurementInterval", u8"测速间隔", "uint32", 0, 4294967295LL, 1000)});
    add(0x91, u8"数字输出控制", Simple, {p("mode", u8"控制方式", "uint8", 0, 2, 0),
        p("outputMask", u8"输出位图", "uint6", 0, 63, 0)});
    add(0x92, u8"数字输入查询", Simple, {}, true);
    add(0xa1, u8"直流电机控制", Simple, {
        p("devId", u8"电机编号", "uint8", 0x21, 0x2e, 0x21, QString(), false, {0x21,0x22,0x23,0x24,0x25,0x26,0x2e}),
        p("pwm", u8"方向与输出值", "int8", -128, 127, 0)});
    // XYZ 子类型与 TrendView GenXYZCmd 一致：两字节模块头、三轴位置、三轴速度。
    // 位置/速度单位由下位机配置决定。1000是软件初始值，并非厂家默认值。
    add(0xc1, u8"虚拟模块控制（XYZ）", Simple, {
        p("moduleType", u8"模块类型", "uint8", 1, 1, 1, QString(), false, {1}),
        p("moduleIndex", u8"虚拟模块编号", "uint8", 0, 255, QVariant(), QString(), true),
        p("xPosition", u8"X位置", "uint24", 0, 0x7fffff, QVariant(), QString(), true),
        p("yPosition", u8"Y位置", "uint24", 0, 0x7fffff, QVariant(), QString(), true),
        p("zPosition", u8"Z位置", "uint24", 0, 0x7fffff, QVariant(), QString(), true),
        p("xSpeed", u8"X速度", "uint24", 0, 0x7fffff, 1000, QStringLiteral("speedUmPerS")),
        p("ySpeed", u8"Y速度", "uint24", 0, 0x7fffff, 1000, QStringLiteral("speedUmPerS")),
        p("zSpeed", u8"Z速度", "uint24", 0, 0x7fffff, 1000, QStringLiteral("speedUmPerS"))});

    // 完整目录接口保留，复杂结构按名字/类型标明必需输入。不提供空内容或全零硬件配置
    // 来假装实现升级、动作或事件编排；未来增加专用结构接口时仍须遵守本层能力门禁。
    add(0x0B, u8"主通信接口配置", Deferred, {
        p("interfaceType", u8"通信接口", "uint8", 0, 3, QVariant(), QString(), true),
        p("uartOptions", u8"串口选项", "structure", 0, 0),
        p("canOptions", u8"CAN选项", "structure", 0, 0),
        p("networkOptions", u8"网络选项", "structure", 0, 0)});
    add(0x13, u8"步进电机配置", Deferred, {motor(),
        p("hardwareFlags", u8"硬件选项", "structure", 0, 0, QVariant(), QString(), true),
        p("microsteps", u8"电机细分", "uint16", 0, 256, QVariant(), QString(), true),
        p("staticCurrent", u8"静态电流", "uint16", 0, 65535, QVariant(), QString(), true),
        p("runningCurrent", u8"运行电流", "uint16", 0, 65535, QVariant(), QString(), true),
        p("lead", u8"电机导程", "uint32", 0, 4294967295LL, QVariant(), QString(), true),
        p("encoderResolution", u8"编码器分辨率", "uint32", 0, 4294967295LL, QVariant(), QString(), true),
        p("encoderRatio", u8"编码器与电机比值", "uint32", 0, 4294967295LL, QVariant(), QString(), true),
        p("feedbackThreshold", u8"反馈报错阈值", "uint32", 0, 4294967295LL, QVariant(), QString(), true),
        p("quietSpeedThreshold", u8"静音切换速度", "uint32", 0, 4294967295LL, QVariant(), QString(), true),
        p("maxPosition", u8"运行极限位置", "uint32", 0, 4294967295LL, QVariant(), QString(), true)});
    QList<QushengParameterDefinition> runParameters = {motor(), p("persistent", u8"掉电保持", "uint8", 0, 1, 0)};
    const char *runKeys[] = {"homingLeaveDistance","homingSearchDistance","mechanicalZeroDistance","homingAccelTargetSpeed","homingMaxSpeed","homingAcceleration1","homingAcceleration2","homingDeceleration2","homingDeceleration1","motionAccelTargetSpeed","motionAcceleration1","motionAcceleration2","motionDeceleration2","motionDeceleration1"};
    const char *runLabels[] = {u8"离开零点距离",u8"寻找零点距离",u8"机械零点距离",u8"回零分段目标速度",u8"回零最大速度",u8"回零第一段加速度",u8"回零第二段加速度",u8"回零第二段减速度",u8"回零第一段减速度",u8"运动分段目标速度",u8"运动第一段加速度",u8"运动第二段加速度",u8"运动第二段减速度",u8"运动第一段减速度"};
    for (int i = 0; i < 14; ++i) runParameters.append(p(runKeys[i], runLabels[i], "int32", -2147483648LL, 2147483647LL, QVariant(), QString(), true));
    add(0x15, u8"步进运行参数设置", Deferred, runParameters);
    add(0x17, u8"电机运动完成上报", ReceiveOnly);
    add(0x45, u8"模数采样主动上报", ReceiveOnly);
    add(0x71, u8"固件更新启动", Deferred, {p("fileLength", u8"文件总长度", "uint32", 1, 4294967295LL, QVariant(), QString(), true)});
    add(0x72, u8"固件更新数据", LayoutPending, {p("frameIndex", u8"分块序号", "uint16", 0, 65535, QVariant(), QString(), true),
        p("firmwareData", u8"固件分块", "bytes", 1, 240, QVariant(), QString(), true)});
    add(0x73, u8"固件更新结束", Deferred, {p("checksum", u8"文件校验和", "uint32", 0, 4294967295LL, QVariant(), QString(), true)});
    add(0x74, u8"固件更新状态查询", Deferred, {}, true);
    const auto moduleParameters = []() { return QList<QushengParameterDefinition>{
        p("moduleType", u8"模块类型", "uint8", 1, 2, QVariant(), QString(), true),
        p("moduleIndex", u8"模块编号", "uint8", 0, 255, QVariant(), QString(), true)}; };
    auto moduleConfig = moduleParameters();
    moduleConfig << p("xyzConfiguration", u8"XYZ配置", "structure", 0, 0) << p("valvePumpConfiguration", u8"阀泵配置", "structure", 0, 0);
    add(0xc2, u8"虚拟模块配置", Deferred, moduleConfig);
    add(0xc3, u8"虚拟模块配置查询", Deferred, moduleParameters(), true);
    const auto setId = []() { return p("setId", u8"指令集合编号", "uint8", 1, 255, QVariant(), QString(), true); };
    const auto sequence = []() { return p("sequence", u8"指令序号", "uint8", 1, 255, QVariant(), QString(), true); };
    add(0xd1, u8"动作编辑", Deferred, {setId(), sequence(),
        p("timestampMs", u8"执行时间戳", "uint32", 0, 4294967295LL, QVariant(), QString(), true),
        p("command", u8"内部指令码", "uint8", 0, 255, QVariant(), QString(), true),
        p("commandData", u8"内部指令参数", "bytes", 0, 248, QVariant(), QString(), true)});
    add(0xd2, u8"动作内容查询", Deferred, {setId(), sequence()}, true);
    add(0xd3, u8"动作运行", Deferred, {setId(), p("reportMode", u8"主动上报方式", "uint8", 0, 2, 0)});
    add(0xd4, u8"动作停止", Deferred);
    add(0xd6, u8"动作重置", Deferred, {p("setId", u8"指令集合编号", "uint8", 0, 255, QVariant(), QString(), true),
        p("sequence", u8"删除指令序号", "uint8", 0, 255, 0)});
    add(0xd7, u8"动作状态查询", Deferred, {}, true);
    add(0xd8, u8"动作列表查询", Deferred, {}, true);
    add(0xd9, u8"动作状态主动上报", ReceiveOnly);
    const auto eventId = []() { return p("eventId", u8"事件编号", "uint8", 1, 255, QVariant(), QString(), true); };
    add(0xe1, u8"事件定义设置", Deferred, {eventId(),
        p("devId", u8"设备模块编号", "uint8", 0, 255, QVariant(), QString(), true),
        p("eventType", u8"事件类型", "uint8", 0, 255, QVariant(), QString(), true),
        p("eventThresholds", u8"事件阈值", "structure", 0, 0)});
    add(0xe2, u8"事件触发配置", Deferred, {eventId(),
        p("triggerFlags", u8"触发方式", "structure", 0, 0, QVariant(), QString(), true),
        p("sequence", u8"触发指令序号", "uint8", 0, 255, 0),
        p("command", u8"触发指令码", "uint8", 0, 255, QVariant(), QString(), true),
        p("commandData", u8"触发指令参数", "bytes", 0, 251, QVariant(), QString(), true)});
    add(0xe3, u8"事件重置", Deferred, {p("eventId", u8"事件编号", "uint8", 0, 255, QVariant(), QString(), true)});
    add(0xe4, u8"事件定义查询", Deferred, {eventId()}, true);
    add(0xe5, u8"事件触发枚举", Deferred, {}, true);
    add(0xe6, u8"事件触发主动上报", ReceiveOnly);
    return table;
}

const QMap<quint8, QushengCommandDefinition> &registry()
{
    // 协议登记是编译后的常量；客户JSON只能选择/映射，不能修改实现能力或参数范围。
    static const auto table = definitions();
    return table;
}

bool fail(TranslationError *error, const char *code, const QString &message)
{
    if (error) { error->code = QString::fromLatin1(code); error->message = message; }
    return false;
}

bool supportCheck(quint8 command, TranslationError *error)
{
    // 在默认补齐和写字节之前拒绝未开放接口，使失败输出不会成为可发送的半成品。
    if (!registry().contains(command)) return fail(error, "SDK_UNKNOWN_COMMAND", QString::fromUtf8(u8"未知趋盛命令"));
    switch (registry().value(command).support) {
    case Support::Simple: return true;
    case Support::ReceiveOnly: return fail(error, "SDK_RECEIVE_ONLY", QString::fromUtf8(u8"该命令仅接收主动上报，不能发送"));
    case Support::LayoutPending: return fail(error, "SDK_LAYOUT_PENDING", QString::fromUtf8(u8"协议布局待确认，暂不能组帧"));
    case Support::Deferred: return fail(error, "SDK_DEFERRED", QString::fromUtf8(u8"该复杂指令接口已保留，本阶段暂未实现"));
    }
    return false;
}

// QVariant::toLongLong会把小数截断，bool也能转换。不能用这种转换来确认协议整数；
// 先检查实际类型、有限性和完整十进制文本，再统一保存qint64，避免24/32位溢出或静默纠错。
bool integer(const QVariant &value, qint64 *number)
{
    bool ok = false;
    switch (value.type()) {
    case QVariant::Int: case QVariant::LongLong: case QVariant::UInt:
        *number = value.toLongLong(&ok); return ok;
    case QVariant::ULongLong: {
        const qulonglong unsignedNumber = value.toULongLong(&ok);
        if (!ok || unsignedNumber > 0x7fffffffffffffffULL) return false;
        *number = qint64(unsignedNumber); return true;
    }
    case QVariant::Double: {
        const double real = value.toDouble();
        if (!std::isfinite(real) || std::floor(real) != real || real < -2147483648.0 || real > 4294967295.0) return false;
        *number = qint64(real); return true;
    }
    case QVariant::String: {
        static const QRegularExpression decimal(QStringLiteral("^[+-]?[0-9]+$"));
        const auto match = decimal.match(value.toString());
        if (!match.hasMatch() || match.capturedLength() != value.toString().size()) return false;
        *number = value.toString().toLongLong(&ok); return ok;
    }
    default: return false;
    }
}

bool validateValue(const QushengParameterDefinition &parameter, const QVariant &value,
                   qint64 *number, TranslationError *error)
{
    if (!integer(value, number)) return fail(error, "SDK_PARAMETER_INTEGER", parameter.label + QString::fromUtf8(u8"必须是整数"));
    if (*number < parameter.minimum || *number > parameter.maximum ||
        (!parameter.allowedValues.isEmpty() && !parameter.allowedValues.contains(*number)))
        return fail(error, "SDK_PARAMETER_RANGE", parameter.label + QString::fromUtf8(u8"越界或取值不合法"));
    return true;
}

bool keysKnown(const QushengCommandDefinition &command, const QVariantMap &values, TranslationError *error)
{
    // 拼错参数名不能被忽略后悄悄使用默认值；预置和明确映射都须使用已登记的key。
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        bool known = false;
        for (const auto &parameter : command.parameters) if (parameter.key == it.key()) { known = true; break; }
        if (!known) return fail(error, "SDK_UNKNOWN_PARAMETER", QString::fromUtf8(u8"未知趋盛参数：") + it.key());
    }
    return true;
}

bool crossCheck(quint8 command, const QVariantMap &parameters, TranslationError *error)
{
    // 单参数范围校验之后再检查跨字段关系、保留位和离散通道。
    // 例如两个风扇阈值各自合法，低阈值仍不能超过高阈值。
    if (command == 0x01 && (parameters.value(QStringLiteral("enableMask")).toLongLong() & 1))
        return fail(error, "SDK_RESERVED_BIT", QString::fromUtf8(u8"功能使能位的位0为预留位，必须为0"));
    if (command == 0x84 && parameters.value(QStringLiteral("minOutput")).toLongLong() > parameters.value(QStringLiteral("maxOutput")).toLongLong())
        return fail(error, "SDK_PARAMETER_RELATION", QString::fromUtf8(u8"输出低阈值不能大于高阈值"));
    if (command == 0x05) {
        const qint64 id = parameters.value(QStringLiteral("devId")).toLongLong();
        const bool valid = QushengSdk::isDeviceId(quint8(id)) || id == 0xff;
        if (!valid) return fail(error, "SDK_PARAMETER_RANGE", QString::fromUtf8(u8"设备编号不是协议定义的通道"));
    }
    return true;
}

void appendBigEndian(QByteArray *bytes, quint32 value, int width)
{
    // 趋盛多字节参数固定高字节先发，独立于客户格式的大小端设置。
    // 调用前已检查位宽；A1负PWM转uint32后取低字节，保留线上8位补码。
    for (int i = width - 1; i >= 0; --i) bytes->append(char((value >> (8 * i)) & 0xff));
}
}

QList<quint8> QushengSdk::commandCodes() { return registry().keys(); }
QushengCommandDefinition QushengSdk::definition(quint8 command) { return registry().value(command); }
bool QushengSdk::isSupported(quint8 command) { return registry().contains(command) && registry().value(command).support == Support::Simple; }
bool QushengSdk::isDeviceId(quint8 id)
{
    // 物理模块编号用于设备参数与故障上报；0xFF是广播操作，不能作为上报模块。
    return (id >= 0x11 && id <= 0x1c) || (id >= 0x21 && id <= 0x26) || id == 0x2e ||
        (id >= 0x31 && id <= 0x4e) || (id >= 0x61 && id <= 0x68) || (id >= 0x71 && id <= 0x78) ||
        (id >= 0x80 && id <= 0x9f) || (id >= 0xc0 && id <= 0xdf) || (id >= 0xe0 && id <= 0xe2);
}

bool QushengSdk::buildRequest(quint8 command, const QVariantMap &supplied,
                              const QVariantMap &defaults, const QVariantMap &device,
                              QushengSdkRequest *request, TranslationError *error)
{
    if (request) *request = QushengSdkRequest();
    if (error) *error = TranslationError();
    if (!request) return fail(error, "SDK_OUTPUT_REQUIRED", QString::fromUtf8(u8"缺少请求输出对象"));
    if (!supportCheck(command, error)) return false;
    const auto spec = definition(command);
    if (!keysKnown(spec, supplied, error) || !keysKnown(spec, defaults, error)) return false;
    // 先在局部对象中完成全部参数，避免失败时调用方读到部分更新的请求。
    QushengSdkRequest resolved;
    resolved.command = command;
    for (const auto &parameter : spec.parameters) {
        QVariant value;
        QString source;
        // contains检查的是“是否明确提供”，因此固定值0仍会覆盖预置/默认值。
        // 已提供但无效的值会报错，不自动回退，以免隐藏配置错误。
        if (supplied.contains(parameter.key)) { value = supplied.value(parameter.key); source = QString::fromUtf8(u8"明确映射"); }
        else if (defaults.contains(parameter.key)) { value = defaults.value(parameter.key); source = QString::fromUtf8(u8"SDK预置"); }
        else if (!parameter.deviceKey.isEmpty() && device.contains(parameter.deviceKey)) {
            value = device.value(parameter.deviceKey); source = QString::fromUtf8(u8"设备参数");
        } else { value = parameter.defaultValue; source = QString::fromUtf8(u8"软件初始值"); }
        // 停止和复位不需要业务位置，允许补零。前进/后退目标必须由映射或保存的预置明确提供。
        if (!value.isValid() && command == 0x11 && parameter.key == QStringLiteral("positionUm") &&
            (resolved.parameters.value(QStringLiteral("action")).toLongLong() == 2 || resolved.parameters.value(QStringLiteral("action")).toLongLong() == 4)) value = 0;
        if (!value.isValid()) {
            const QString label = command == 0x01 ? QString::fromUtf8(u8"设备功能掩码") : parameter.label;
            return fail(error, "SDK_PARAMETER_REQUIRED", label + QString::fromUtf8(u8"需明确提供"));
        }
        qint64 number;
        if (!validateValue(parameter, value, &number, error)) return false;
        resolved.parameters.insert(parameter.key, QVariant::fromValue(number));
        resolved.parameterSources.insert(parameter.key, source);
    }
    if (!encode(command, resolved.parameters, &resolved.payload, error)) return false;
    // 只在全部参数与载荷通过检查后发布快照，失败不能留下可误发的半个请求。
    *request = resolved;
    return true;
}

bool QushengSdk::encode(quint8 command, const QVariantMap &parameters, QByteArray *payload, TranslationError *error)
{
    // encode也可被SDK调用方直接使用，不能假定参数一定已经过buildRequest。
    // 清空旧负载后重查能力、参数名、数值和组合关系，成功才发布本次字节。
    if (payload) payload->clear();
    if (error) *error = TranslationError();
    if (!payload) return fail(error, "SDK_OUTPUT_REQUIRED", QString::fromUtf8(u8"缺少载荷输出对象"));
    if (!supportCheck(command, error)) return false;
    const auto spec = definition(command);
    if (!keysKnown(spec, parameters, error)) return false;
    for (const auto &parameter : spec.parameters) {
        if (!parameters.contains(parameter.key)) return fail(error, "SDK_PARAMETER_REQUIRED", parameter.label + QString::fromUtf8(u8"缺少已解析参数"));
        qint64 number;
        if (!validateValue(parameter, parameters.value(parameter.key), &number, error)) return false;
    }
    if (!crossCheck(command, parameters, error)) return false;
    QByteArray bytes;
    auto append = [&](const char *key, int width) {
        appendBigEndian(&bytes, quint32(parameters.value(QString::fromLatin1(key)).toLongLong()), width);
    };
    switch (command) {
    case 0x01: append("enableMask", 2); break;
    // 这些查询的D段为空；04/23则要求两个固定零字节，不能混作“查询都无数据”。
    case 0x03: case 0x0c: case 0x25: case 0x92: break;
    case 0x04: case 0x23: bytes.append(2, char(0)); break;
    case 0x05: case 0x12: case 0x14: case 0x16: case 0x44: case 0x64: case 0x82: case 0x83: append("devId", 1); break;
    case 0x08: append("intervalMs", 2); break;
    case 0x09: append("enabled", 1); break;
    case 0x11:
        // D0编号；D1高4位模式、低4位动作；D2～D4速度；D5～D7距离/目标位置。
        // positionUm沿用历史字段名，含义由mode决定：绝对模式是目标，相对模式是距离。
        append("devId", 1);
        bytes.append(char((parameters.value(QStringLiteral("mode")).toUInt() << 4) | parameters.value(QStringLiteral("action")).toUInt()));
        append("speedUmPerS", 3); append("positionUm", 3); break;
    case 0x1a: append("devId", 1); append("speed", 3); break;
    case 0x21:
        // 控制时Mode占最高2位、阀1占bit0。0x23返回阀1在bit2；两个布局绝不能混用。
        appendBigEndian(&bytes, (parameters.value(QStringLiteral("mode")).toUInt() << 30) | parameters.value(QStringLiteral("valveMask")).toUInt(), 4);
        append("duration", 4); break;
    case 0x22: append("devId", 1); append("duration", 4); break;
    case 0x24: append("group1Timeout", 4); append("group2Timeout", 4); append("group3Timeout", 4); break;
    // intervalMs/voltageMv分别使用毫秒/毫伏；客户值的单位转换由映射表达式承担。
    case 0x46: append("devId", 1); append("intervalMs", 3); break;
    case 0x54: append("devId", 1); append("voltageMv", 3); break;
    case 0x63: append("peripheralDelay", 4); append("power24Delay", 4); append("power12Delay", 4); append("power5Delay", 4); break;
    case 0x65: append("devId", 1); append("enabled", 1); break;
    case 0x81: append("devId", 1); append("percent", 1); break;
    case 0x84:
        // 编号后的5项各占1字节，最后测速间隔占4字节；不是按宿主结构体内存直接复制。
        append("devId", 1); append("speedMode", 1); append("pulsesPerRevolution", 1);
        append("filterWindow", 1); append("minOutput", 1); append("maxOutput", 1); append("measurementInterval", 4); break;
    case 0x91:
        // 单字节的高2位是控制方式，低6位是输出掩码；有效范围已保证两部分不重叠。
        bytes.append(char((parameters.value(QStringLiteral("mode")).toUInt() << 6) | parameters.value(QStringLiteral("outputMask")).toUInt())); break;
    case 0xa1: append("devId", 1); append("pwm", 1); break;
    case 0xc1:
        append("moduleType", 1); append("moduleIndex", 1);
        append("xPosition", 3); append("yPosition", 3); append("zPosition", 3);
        append("xSpeed", 3); append("ySpeed", 3); append("zSpeed", 3); break;
    default: return fail(error, "SDK_DEFERRED", QString::fromUtf8(u8"该接口暂不能组帧"));
    }
    *payload = bytes;
    return true;
}
