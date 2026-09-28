#include "qushengprotocol.h"

namespace {
const quint8 FrameByte = 0xaa;
const int FrameOverhead = 10;
const quint32 Maximum24Bit = 0xffffffu;
const quint32 ContinuousMoveDistance = 0xffffffu;

// Command 0x11 encodes every 24-bit unsigned field in network (big-endian)
// byte order.  Each field always occupies exactly three bytes.
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
}

quint16 QushengProtocol::crc16(const QByteArray &data)
{
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
    if (command.operation!=QStringLiteral("ABSOLUTE_MOVE")) {
        *error = {"QUSHENG_UNSUPPORTED", QString::fromUtf8(u8"只支持绝对定位及电机查询")};
        return false;
    }
    quint32 speed=0, position=0, action=0;
    if (!readUnsigned24Parameter(command,"speedUmPerS",&speed)
            || !readUnsigned24Parameter(command,"positionUm",&position)
            || !readUnsigned24Parameter(command,"absoluteAction",&action)
            || speed==0 || position==ContinuousMoveDistance || (action!=1 && action!=3)) {
        *error = {"QUSHENG_RANGE", QString::fromUtf8(u8"速度、绝对位置或方向不在有效范围")};
        return false;
    }
    QByteArray payload;
    payload.append(char(devId));
    payload.append(char(0x40 | action)); // AbsolutePosition mode=4; action from confirmed profile.
    appendBe24(&payload, speed);    // D2..D4: 24-bit unsigned, high byte first
    appendBe24(&payload, position); // D5..D7: absolute target, high byte first

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
            // A trailing AA may be the first byte of a header split across reads.
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
    const int expectedCmd=command.operation==QStringLiteral("QUERY_MOTOR") ? 0x12 : 0x11;
    const int expectedLength=expectedCmd==0x12 ? 11 : 4;
    return m_hasPendingSequence && command.requestId==m_pendingRequestId
            && frame.size()==expectedLength+FrameOverhead && quint8(frame.at(2))==m_source
            && quint8(frame.at(3))==m_destination && quint8(frame.at(4))==expectedLength
            && quint8(frame.at(6))==m_pendingSequence && quint8(frame.at(7))==expectedCmd
            && message.payload.size()==expectedLength;
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
        const quint32 position=(quint32(quint8(message.payload.at(5)))<<16)
                | (quint32(quint8(message.payload.at(6)))<<8) | quint8(message.payload.at(7));
        result->data.insert("motorState",state);
        result->data.insert("positionUm",position);
    }
    if (!result->success)
        result->error = {"QUSHENG_REJECTED", QString::fromUtf8(u8"趋盛设备返回错误码 0x%1").arg(errorCode, 2, 16, QLatin1Char('0'))};
    return true;
}
