#include "qushengsdk.h"
#include <QStringList>

// 只解析趋盛命令数据，整帧CRC/地址/序号由QushengProtocol处理。
// 普通应答先核对执行结果头和长度，再读取扩展字段；上报另走decodeReport。
namespace {
quint32 be(const QByteArray &data, int at, int size)
{
    // 所有调用点须先通过对应布局长度检查；本工具只读取1～4字节大端无符号值。
    quint32 value = 0;
    for (int i = 0; i < size; ++i) value = (value << 8) | quint8(data.at(at + i));
    return value;
}

qint64 signed32(const QByteArray &data, int at)
{
    const quint32 raw = be(data, at, 4);
    // 避免uint32转int32的实现相关行为：在线补码用64位减2^32，负运行参数不会变成大正数。
    return raw & 0x80000000u ? qint64(raw) - Q_INT64_C(4294967296) : qint64(raw);
}

bool fail(TranslationError *error, const char *code, const QString &message)
{
    if (error) *error = {QLatin1String(code), message};
    return false;
}

bool reset(const QByteArray &payload, QushengSdkReply *reply, TranslationError *error)
{
    if (error) *error = TranslationError();
    if (!reply) return fail(error, "SDK_OUTPUT", QString::fromUtf8(u8"回告输出未提供"));
    *reply = QushengSdkReply();
    // 每次调用先保留本次原始数据；解析失败也不得让界面显示上一条成功结果。
    reply->rawPayload = payload;
    if (payload.size() > 255)
        return fail(error, "SDK_PAYLOAD_LENGTH", QString::fromUtf8(u8"趋盛数据负载不能超过255字节"));
    return true;
}

bool unsupported(quint8 command, TranslationError *error)
{
    switch (command) {
    case 0x17: case 0x45: case 0xd9: case 0xe6:
        return fail(error, "SDK_RECEIVE_ONLY", QString::fromUtf8(u8"此命令为主动上报，不能按请求应答解析"));
    case 0x72:
        return fail(error, "SDK_LAYOUT_PENDING", QString::fromUtf8(u8"协议布局存在歧义，回告接口等待确认"));
    case 0x0b: case 0x13: case 0x15: case 0x71: case 0x73: case 0x74:
    case 0xc2: case 0xc3: case 0xd1: case 0xd2: case 0xd3: case 0xd4:
    case 0xd6: case 0xd7: case 0xd8: case 0xe1: case 0xe2: case 0xe3: case 0xe4: case 0xe5:
        return fail(error, "SDK_DEFERRED", QString::fromUtf8(u8"本阶段尚未开放此命令的回告解析"));
    default:
        return fail(error, "SDK_UNKNOWN_COMMAND", QString::fromUtf8(u8"未知趋盛命令码"));
    }
}

bool simple(quint8 command)
{ return QushengSdk::isSupported(command); }

int successLength(quint8 command, const QByteArray &payload)
{
    // 返回值包含4字节执行结果头，不包含AA AA帧头等传输字段；-1表示布局不合法。
    // 查询按各自布局校验，不能仅“至少4字节”就把缺失数据当成0；异常回告另接受4字节。
    switch (command) {
    case 0x03: return payload.size() >= 4 && (payload.size() - 4) % 4 == 0 ? payload.size() : -1;
    case 0x04: return payload.size() == 28 || payload.size() == 29 ? payload.size() : -1;
    case 0x0c:
        if (payload.size() < 5) return -1;
        switch (quint8(payload.at(4))) { case 0: case 1: return 10; case 2: return 6; case 3: return 27; default: return -1; }
    // 实机0x12在文档D0～D10之后附带3字节保留位；仅兼容这两种完整长度。
    case 0x12: return payload.size() == 11 || payload.size() == 14 ? payload.size() : -1;
    case 0x14: return 36;
    case 0x16: return 60;
    case 0x23: return 8;
    case 0x25: return 16;
    case 0x44: return 7;
    case 0x64: return 20;
    case 0x82: return 13;
    case 0x83: return 5;
    // 0x84原表只给D4..D9六字节扩展，区别于0x82的完整九字节配置；不补造缺失字段。
    case 0x84: return payload.size() == 4 || payload.size() == 10 ? payload.size() : -1;
    case 0x92: return 7; // V0.1.7修订为24路，表格D4..D6优先于正文遗留D4..D5。
    default: return 4;
    }
}

void configuration(QVariantMap *values, const QByteArray &data, int at)
{
    // 14查询在应答头后直接返回13设置命令的配置段，故at=4，没有额外DevID。
    // 先2字节硬件标志，再3个uint16及6个uint32，读取顺序与协议表一致。
    values->insert("hardwareFlags", be(data, at, 2));
    const char *words[] = {"microsteps", "staticCurrent", "runningCurrent"};
    for (int i = 0; i < 3; ++i) values->insert(QLatin1String(words[i]), be(data, at + 2 + 2 * i, 2));
    const char *dwords[] = {"lead", "encoderResolution", "encoderRatio", "feedbackThreshold", "quietSpeedThreshold", "maxPosition"};
    for (int i = 0; i < 6; ++i) values->insert(QLatin1String(dwords[i]), be(data, at + 8 + 4 * i, 4));
}

void runConfiguration(QVariantMap *values, const QByteArray &data)
{
    // 查询0x16不返回DevID/掉电保持位，而是从设置命令D2开始的14个int32。
    const char *fields[] = {"homingLeaveDistance", "homingSearchDistance", "mechanicalZeroDistance",
        "homingAccelTargetSpeed", "homingMaxSpeed", "homingAcceleration1", "homingAcceleration2",
        "homingDeceleration2", "homingDeceleration1", "motionAccelTargetSpeed", "motionAcceleration1",
        "motionAcceleration2", "motionDeceleration2", "motionDeceleration1"};
    for (int i = 0; i < 14; ++i) values->insert(QLatin1String(fields[i]), signed32(data, 4 + 4 * i));
}

void fanConfiguration(QVariantMap *values, const QByteArray &data)
{
    const char *fields[] = {"speedMode", "pulsesPerRevolution", "filterWindow", "minOutput", "maxOutput"};
    for (int i = 0; i < 5; ++i) values->insert(QLatin1String(fields[i]), quint8(data.at(4 + i)));
    values->insert("measurementInterval", be(data, 9, 4));
}

void interfaceConfiguration(QVariantMap *values, const QByteArray &data)
{
    // 0C在D4给出接口类型，后续是不同长度的串口/CAN/网络分支，不是并排的全部配置。
    // successLength已按该类型验证负载，避免用网络偏移读取短串口应答。
    const quint8 type = quint8(data.at(4));
    values->insert("interfaceType", type);
    if (type <= 1) {
        const quint8 flags = quint8(data.at(5));
        values->insert("uartOptions", flags);
        values->insert("flowControl", (flags >> 3) & 3);
        values->insert("parity", (flags >> 1) & 3);
        values->insert("stopBits", (flags & 1) + 1);
        values->insert("baudrate", be(data, 6, 4));
    } else if (type == 2) {
        values->insert("canBaudrateIndex", quint8(data.at(5)));
    } else {
        const char *fields[] = {"ip", "subnetMask", "gateway", "dns", "serverIp"};
        for (int i = 0; i < 5; ++i) {
            QStringList octets;
            for (int j = 0; j < 4; ++j) octets.append(QString::number(quint8(data.at(5 + 4 * i + j))));
            values->insert(QLatin1String(fields[i]), octets.join('.'));
        }
        values->insert("serverPort", be(data, 25, 2));
    }
}
}

bool QushengSdk::decodeReply(quint8 command, const QByteArray &payload,
                            QushengSdkReply *reply, TranslationError *error)
{
    if (!reset(payload, reply, error)) return false;
    if (!simple(command)) return unsupported(command, error);
    if (payload.size() < 4)
        return fail(error, "SDK_REPLY_LENGTH", QString::fromUtf8(u8"趋盛回告缺少完整执行结果和基本状态"));

    // 0x21原表是2字节错误码+2字节状态。此例外不能与一般1+3头直接合并，
    // 否则低字节错误会被吞入状态，错误帧反而被当成成功。
    const quint32 deviceError = command == 0x21 ? be(payload, 0, 2) : quint8(payload.at(0));
    const bool shortError = deviceError != 0 && payload.size() == 4;
    // 拒绝回告允许只有结果头，设备无须附上查询数据；成功查询则必须含完整扩展。
    if (!shortError && successLength(command, payload) != payload.size())
        return fail(error, "SDK_REPLY_LENGTH", QString::fromUtf8(u8"趋盛回告长度与命令布局不符"));
    if (!deviceError && command == 0x12 && (quint8(payload.at(4)) >> 4) > 4)
        return fail(error, "SDK_MOTOR_STATE", QString::fromUtf8(u8"未知电机状态"));

    reply->errorCode = deviceError;
    reply->basicStatus = command == 0x21 ? payload.mid(2, 2) : payload.mid(1, 3);
    reply->success = deviceError == 0;
    // 设备明确拒绝仍是成功解析的回告；返回true让请求层结束等待，并显示实际错误码。
    if (!reply->success) return true;
    QVariantMap &values = reply->values;
    switch (command) {
    case 0x03: {
        QVariantList records;
        for (int at = 4; at < payload.size(); at += 4) {
            QVariantMap record;
            record.insert("devId", quint8(payload.at(at)));
            record.insert("faultCode", be(payload, at + 1, 3));
            record.insert("raw", payload.mid(at, 4)); records.append(record);
        }
        values.insert("errors", records); break;
    }
    case 0x04: {
        const char *keys[] = {"hardwareVersion", "iapVersion", "firmwareVersion"};
        for (int i = 0; i < 3; ++i) {
            const int at = 4 + 4 * i;
            values.insert(QLatin1String(keys[i]), QStringLiteral("%1.%2.%3")
                .arg(quint8(payload.at(at))).arg(quint8(payload.at(at + 1))).arg(be(payload, at + 2, 2)));
        }
        values.insert("deviceIdentifier", QString::fromLatin1(payload.mid(16, 12).toHex(' ').toUpper()));
        if (payload.size() == 29) values.insert("boardType", quint8(payload.at(28)));
        break;
    }
    case 0x0c: interfaceConfiguration(&values, payload); break;
    case 0x12: {
        // 应答D4高4位是状态，D5～D7当前位置，D8～D10负反馈位置，两种位置分别保留。
        // 14字节实机回告的D11～D13按保留位跳过，取值不限；rawPayload仍保留完整数据。
        // SDK负责解码；“空闲且到达本次目标”由请求层判断，不在这里宣告动作完成。
        const int state = (quint8(payload.at(4)) >> 4) & 0x0f;
        values.insert("motorState", state); values.insert("positionUm", be(payload, 5, 3));
        values.insert("feedbackPositionUm", be(payload, 8, 3)); break;
    }
    case 0x14: configuration(&values, payload, 4); break;
    case 0x16: runConfiguration(&values, payload); break;
    case 0x23:
        // 状态查询把30路放在bit31..2，不能复用0x21控制的模式+掩码布局。
        values.insert("valveMask", be(payload, 4, 4) >> 2); break;
    case 0x25: {
        const char *keys[] = {"group1Timeout", "group2Timeout", "group3Timeout"};
        for (int i = 0; i < 3; ++i) values.insert(QLatin1String(keys[i]), be(payload, 4 + 4 * i, 4));
        break;
    }
    case 0x44: values.insert("voltageMv", be(payload, 4, 3)); break;
    case 0x64: {
        const char *keys[] = {"peripheralDelay", "power24Delay", "power12Delay", "power5Delay"};
        for (int i = 0; i < 4; ++i) values.insert(QLatin1String(keys[i]), be(payload, 4 + 4 * i, 4));
        break;
    }
    case 0x82: fanConfiguration(&values, payload); break;
    case 0x83: values.insert("percent", quint8(payload.at(4))); break;
    case 0x84: if (payload.size() == 10) values.insert("configurationRaw", payload.mid(4)); break;
    case 0x92: values.insert("inputMask", be(payload, 4, 3)); break;
    default: break; // 设置类只有执行结果头，没有额外业务字段。
    }
    return true;
}

bool QushengSdk::decodeReport(quint8 command, const QByteArray &payload,
                             QushengSdkReply *reply, TranslationError *error)
{
    if (!reset(payload, reply, error)) return false;
    // 主动上报没有通用ERR_CODE/basicStatus头。即使0x09恰好4字节，仍是DevID+故障码。
    // 是否属于当前请求由协议适配器按地址/序号判断；SDK不能仅依据负载长度替代这个判断。
    switch (command) {
    case 0x08:
        if (!payload.isEmpty()) return fail(error, "SDK_REPORT_LENGTH", QString::fromUtf8(u8"心跳主动上报应无数据"));
        reply->values.insert("heartbeat", true); break;
    case 0x09: {
        if (payload.isEmpty() || payload.size() % 4 != 0)
            return fail(error, "SDK_REPORT_LENGTH", QString::fromUtf8(u8"错误主动上报应由完整四字节记录组成"));
        QVariantList records;
        for (int at = 0; at < payload.size(); at += 4) {
            // 有效模块编号也用于区分同CMD的设置应答，不能把普通ERR_CODE当成DevID。
            if (!QushengSdk::isDeviceId(quint8(payload.at(at))))
                return fail(error, "SDK_REPORT_DEVICE", QString::fromUtf8(u8"错误上报的模块编号无效"));
            QVariantMap record;
            record.insert("devId", quint8(payload.at(at))); record.insert("faultCode", be(payload, at + 1, 3));
            record.insert("raw", payload.mid(at, 4)); records.append(record);
        }
        reply->values.insert("errors", records); break;
    }
    case 0x17: {
        // 与12应答不同，上报从D0直接开始编号，不含4字节执行结果头。
        if (payload.size() != 8) return fail(error, "SDK_REPORT_LENGTH", QString::fromUtf8(u8"电机主动上报应为八字节"));
        const int id = quint8(payload.at(0)), state = quint8(payload.at(1)) >> 4;
        if (id < 0x11 || id > 0x1c) return fail(error, "SDK_REPORT_DEVICE", QString::fromUtf8(u8"电机主动上报编号无效"));
        if (state > 4) return fail(error, "SDK_MOTOR_STATE", QString::fromUtf8(u8"未知电机状态"));
        reply->values.insert("devId", id); reply->values.insert("motorState", state);
        reply->values.insert("positionUm", be(payload, 2, 3)); reply->values.insert("feedbackPositionUm", be(payload, 5, 3));
        break;
    }
    case 0x45:
        if (payload.size() != 4) return fail(error, "SDK_REPORT_LENGTH", QString::fromUtf8(u8"模数主动上报应为四字节"));
        if (quint8(payload.at(0)) < 0x61 || quint8(payload.at(0)) > 0x68)
            return fail(error, "SDK_REPORT_DEVICE", QString::fromUtf8(u8"模数主动上报通道无效"));
        reply->values.insert("devId", quint8(payload.at(0))); reply->values.insert("voltageMv", be(payload, 1, 3)); break;
    case 0xd9: case 0xe6:
        return fail(error, "SDK_DEFERRED", QString::fromUtf8(u8"动作及事件主动上报解析将在后续阶段开放"));
    default:
        return fail(error, "SDK_NOT_REPORT", QString::fromUtf8(u8"此命令不是本阶段支持的主动上报"));
    }
    reply->success = true;
    // 这里success只表示上报解析成功；09的具体模块故障仍在values.errors中。
    return true;
}
