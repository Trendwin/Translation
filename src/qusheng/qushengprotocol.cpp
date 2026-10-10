#include "qushengprotocol.h"

namespace {
const quint8 FrameByte = 0xaa;
const int FrameOverhead = 10;
const quint32 Maximum24Bit = 0xffffffu;
const quint32 ContinuousMoveDistance = 0xffffffu;

// 0x11 的速度和位置均以微米相关单位进入编码器，线上固定占 3 字节且高字节在前。
// 这里不接受客户协议字段，避免外部格式绕过统一命令和设备行程校验。
void appendBe24(QByteArray *out, quint32 value)
{
    out->append(char(value >> 16));
    out->append(char(value >> 8));
    out->append(char(value));
}

quint16 readBe16(const QByteArray &data, int at)
{
    return (quint16(quint8(data.at(at))) << 8) | quint8(data.at(at + 1));
}

bool readUnsigned24Parameter(const UnifiedCommand &command, const char *name, quint32 *value)
{
    if (!command.parameters.contains(QLatin1String(name))) return false;
    bool ok = false;
    const qlonglong signedValue = command.parameters.value(QLatin1String(name)).toLongLong(&ok);
    if (!ok || signedValue < 0 || signedValue > Maximum24Bit) return false;
    *value = quint32(signedValue);
    return true;
}

int commandByte(const QString &operation)
{
    if (operation == "QUERY_MOTOR") return 0x12;
    if (operation == "QUERY_DEVICE_INFO") return 0x04;
    return 0x11;
}

quint32 readBe24(const QByteArray &data, int at)
{
    return (quint32(quint8(data.at(at))) << 16) | (quint32(quint8(data.at(at + 1))) << 8)
            | quint8(data.at(at + 2));
}
}

quint16 QushengProtocol::crc16(const QByteArray &data)
{
    // 固定采用现有趋盛样例验证过的 CRC16（初值 FFFF、多项式 1021）。
    // 覆盖从帧头到数据末尾，CRC 高字节在前；首版不向客户开放算法编辑。
    quint16 crc = 0xffff;
    for (char byte : data) {
        crc ^= quint16(quint8(byte)) << 8;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 0x8000) ? quint16((crc << 1) ^ 0x1021) : quint16(crc << 1);
    }
    return crc;
}

QByteArray QushengProtocol::makeFrame(quint8 destination, quint8 source, quint8 sequence,
                                      quint8 command, const QByteArray &data)
{
    if (data.size() > 255) return QByteArray();
    QByteArray frame;
    frame.reserve(data.size() + FrameOverhead);
    frame.append(char(FrameByte));
    frame.append(char(FrameByte));
    frame.append(char(destination));
    frame.append(char(source));
    frame.append(char(data.size()));
    frame.append(char(~quint8(data.size())));
    frame.append(char(sequence));
    frame.append(char(command));
    frame.append(data);
    const quint16 crc = crc16(frame);
    frame.append(char(crc >> 8));
    frame.append(char(crc));
    return frame;
}

bool QushengProtocol::encodeCommand(const UnifiedCommand &command, QByteArray *frame,
                                    TranslationError *error) const
{
    if (!frame || !error) return false;
    if (m_destination==0 || m_source==0 || m_destination==m_source) {
        *error = {"QUSHENG_ADDRESS", QString::fromUtf8(u8"趋盛地址未配置或冲突")};
        return false;
    }
    if (m_nextSequence>255) {
        *error={"QUSHENG_SEQUENCE_EXHAUSTED",QString::fromUtf8(u8"本次连接已用完 8 位序号；重新连接后再发送，避免迟到帧回绕匹配")};
        return false;
    }
    // 设备信息查询作用于控制板，无需电机 DevID；协议明确发送两个零字节而非空数据。
    if (command.operation == "QUERY_DEVICE_INFO") {
        const quint8 sequence = quint8(m_nextSequence++);
        *frame = makeFrame(m_destination, m_source, sequence, 0x04, QByteArray(2, '\0'));
        m_pendingRequestId = command.requestId; m_pendingSequence = sequence; m_hasPendingSequence = true;
        return true;
    }
    bool idOk=false;
    const uint devId=command.targetDevice.toUInt(&idOk);
    if (!idOk || devId<0x11 || devId>0x1c) {
        *error = {"QUSHENG_DEVID", QString::fromUtf8(u8"电机 DevID 无效")};
        return false;
    }
    if (command.operation==QStringLiteral("QUERY_MOTOR")) {
        const quint8 sequence=quint8(m_nextSequence++);
        *frame=makeFrame(m_destination,m_source,sequence,0x12,QByteArray(1,char(devId)));
        m_pendingRequestId=command.requestId; m_pendingSequence=sequence; m_hasPendingSequence=true;
        return true;
    }
    const bool relative = command.operation == "RELATIVE_FORWARD" || command.operation == "RELATIVE_BACKWARD";
    if (!relative && command.operation!=QStringLiteral("ABSOLUTE_MOVE")) {
        *error = {"QUSHENG_UNSUPPORTED", QString::fromUtf8(u8"只支持单条位置运动、电机查询及设备版本查询")};
        return false;
    }
    quint32 speed=0, position=0, action=0;
    if (!readUnsigned24Parameter(command,"speedUmPerS",&speed)
            || !readUnsigned24Parameter(command,relative ? "distanceUm" : "positionUm",&position)
            || !readUnsigned24Parameter(command,"absoluteAction",&action)
            || speed==0 || position==ContinuousMoveDistance || (action!=1 && action!=3)) {
        *error = {"QUSHENG_RANGE", QString::fromUtf8(u8"速度、绝对位置或方向不在有效范围")};
        return false;
    }
    QByteArray payload;
    payload.append(char(devId));
    // 相对模式高四位为 2、绝对模式为 4；方向由通用目标模板决定，与客户命令字符无关。
    if (relative) action = command.operation == "RELATIVE_FORWARD" ? 1 : 3;
    payload.append(char((relative ? 0x20 : 0x40) | action));
    appendBe24(&payload, speed);    // D2..D4：速度，24 位无符号大端。
    appendBe24(&payload, position); // D5..D7：绝对目标或相对距离，24 位大端；单位沿用趋盛约定，不在此猜客户步距。

    const quint8 sequence = quint8(m_nextSequence++);
    *frame = makeFrame(m_destination, m_source, sequence, 0x11, payload);
    m_pendingRequestId = command.requestId;
    m_pendingSequence = sequence;
    m_hasPendingSequence = true;
    return true;
}

QByteArray QushengProtocol::makeReply(quint8 sequence, quint8 errorCode,
                                      const QByteArray &basicStatus)
{
    if (basicStatus.size() != 3) return QByteArray();
    QByteArray data;
    data.append(char(errorCode));
    data.append(basicStatus);
    return makeFrame(0x01, 0x02, sequence, 0x11, data);
}

QList<ProtocolMessage> QushengProtocol::feedReceivedData(const QByteArray &data,
                                                          QList<TranslationError> *errors)
{
    QList<ProtocolMessage> messages;
    m_buffer.append(data);
    if (m_buffer.size() > 4096) {
        m_buffer.clear();
        if (errors) errors->append({"RX_OVERFLOW", QString::fromUtf8(u8"接收缓存超过 4096 字节，已清空")});
        return messages;
    }

    for (;;) {
        const int header = m_buffer.indexOf(QByteArray::fromHex("aaaa"));
        if (header < 0) {
            // 缓存末尾的 AA 可能是拆包帧头的第一个字节，必须保留到下一次接收。
            if (!m_buffer.isEmpty() && quint8(m_buffer.at(m_buffer.size() - 1)) == FrameByte)
                m_buffer = QByteArray(1, char(FrameByte));
            else
                m_buffer.clear();
            break;
        }
        if (header > 0) m_buffer.remove(0, header);
        if (m_buffer.size() < 6) break;

        const quint8 length = quint8(m_buffer.at(4));
        const quint8 inverseLength = quint8(m_buffer.at(5));
        if (quint8(length ^ inverseLength) != 0xff) {
            m_buffer.remove(0, 1);
            if (errors) errors->append({"QUSHENG_LENGTH", QString::fromUtf8(u8"趋盛帧长度反码校验失败")});
            continue;
        }
        const int totalLength = int(length) + FrameOverhead;
        if (m_buffer.size() < totalLength) break;

        const QByteArray frame = m_buffer.left(totalLength);
        if (readBe16(frame, totalLength - 2) != crc16(frame.left(totalLength - 2))) {
            m_buffer.remove(0, 1);
            if (errors) errors->append({"QUSHENG_CRC", QString::fromUtf8(u8"趋盛回告 CRC16 校验失败")});
            continue;
        }
        m_buffer.remove(0, totalLength);
        ProtocolMessage message;
        message.messageType = quint8(frame.at(7));
        message.payload = frame.mid(8, length);
        message.wireData = frame;
        messages.append(message);
    }
    return messages;
}

bool QushengProtocol::matchesReply(const UnifiedCommand &command, const ProtocolMessage &message) const
{
    const QByteArray &frame = message.wireData;
    const int expectedCmd = commandByte(command.operation);
    const int length = message.payload.size();
    const bool errorReply = length == 4 && quint8(message.payload.at(0)) != 0;
    const bool validLength = expectedCmd == 0x04 ? (length == 28 || length == 29 || errorReply)
                          : expectedCmd == 0x12 ? (length == 11 || errorReply) : length == 4;
    return m_hasPendingSequence && command.requestId==m_pendingRequestId
            && validLength && frame.size()==length+FrameOverhead && quint8(frame.at(2))==m_source
            && quint8(frame.at(3))==m_destination && quint8(frame.at(4))==length
            && quint8(frame.at(6))==m_pendingSequence && quint8(frame.at(7))==expectedCmd
            ;
}

bool QushengProtocol::decodeReply(const UnifiedCommand &command, const ProtocolMessage &message,
                                  UnifiedResult *result, TranslationError *error) const
{
    if (!result || !error || !matchesReply(command, message)) {
        if (error) *error = {"QUSHENG_NOT_MATCHED", QString::fromUtf8(u8"回告与当前请求的地址、序号或命令不匹配")};
        return false;
    }
    result->requestId = command.requestId;
    const quint8 errorCode = quint8(message.payload.at(0)); // D0 是 ERR_CODE，不是 DevID。
    result->success = errorCode == 0;
    result->data.insert("basicStatus", message.payload.mid(1, 3));
    if (command.operation==QStringLiteral("QUERY_MOTOR") && result->success) {
        const int state=(quint8(message.payload.at(4))>>4)&0x0f;
        const quint32 position = readBe24(message.payload, 5);
        result->data.insert("motorState",state);
        result->data.insert("positionUm",position);
        result->data.insert("feedbackPositionUm", readBe24(message.payload, 8));
    }
    if (command.operation == "QUERY_DEVICE_INFO" && result->success) {
        // 协议 D4..D15 是三段版本，D16..D27 是设备标识；不冒充客户的固件校验码。
        auto versionAt = [&message](int at) {
            return QStringLiteral("%1.%2.%3").arg(quint8(message.payload.at(at)))
                    .arg(quint8(message.payload.at(at + 1))).arg(readBe16(message.payload, at + 2));
        };
        result->data.insert("hardwareVersion", versionAt(4));
        result->data.insert("iapVersion", versionAt(8));
        result->data.insert("firmwareVersion", versionAt(12));
        result->data.insert("deviceIdentifier", QString::fromLatin1(message.payload.mid(16, 12).toHex(' ').toUpper()));
        if (message.payload.size() == 29) result->data.insert("boardType", quint8(message.payload.at(28)));
    }
    if (!result->success)
        result->error = {"QUSHENG_REJECTED", QString::fromUtf8(u8"趋盛设备返回错误码 0x%1").arg(errorCode, 2, 16, QLatin1Char('0'))};
    return true;
}
