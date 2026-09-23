#include "qushengprotocol.h"

namespace {
const int RequestSize = 16;
const int ReplySize = 7;
void appendLe16(QByteArray *out, quint16 value) { out->append(char(value)); out->append(char(value >> 8)); }
void appendLe32(QByteArray *out, quint32 value) {
    for (int shift = 0; shift < 32; shift += 8) out->append(char(value >> shift));
}
quint16 readLe16(const QByteArray &data, int at) {
    return quint8(data.at(at)) | (quint16(quint8(data.at(at + 1))) << 8);
}
}

quint16 QushengProtocol::crc16(const QByteArray &data)
{
    quint16 crc = 0xffff;
    for (char byte : data) {
        crc ^= quint8(byte);
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 1) ? quint16((crc >> 1) ^ 0xa001) : quint16(crc >> 1);
    }
    return crc;
}

bool QushengProtocol::encodeCommand(const UnifiedCommand &command, QByteArray *frame,
                                    TranslationError *error) const
{
    if (!frame || !error) return false;
    if (command.operation != QStringLiteral("ABSOLUTE_MOVE")) {
        *error = {"QUSHENG_UNSUPPORTED", QStringLiteral("趋盛协议仅支持 DT A<n> 绝对定位")};
        return false;
    }
    const qlonglong signedPosition = command.parameters.value("position").toLongLong();
    QByteArray data;
    data.append(char(0x02));                 // destination
    data.append(char(0x01));                 // source
    data.append(char(0x11));                 // DevID
    data.append(char(0x11));                 // absolute-move command
    data.append(char(signedPosition < 0));   // 0: forward, 1: reverse
    data.append(char(0x01));                 // position mode
    appendLe32(&data, command.parameters.value("speed", 1000).toUInt());
    appendLe32(&data, quint32(signedPosition < 0 ? -signedPosition : signedPosition));
    appendLe16(&data, crc16(data));
    *frame = data;
    return frame->size() == RequestSize;
}

QByteArray QushengProtocol::makeReply(quint8 destination, quint8 source, quint8 deviceId,
                                      quint8 command, quint8 status)
{
    QByteArray frame;
    frame.append(char(destination)); frame.append(char(source)); frame.append(char(deviceId));
    frame.append(char(command)); frame.append(char(status));
    appendLe16(&frame, crc16(frame));
    return frame;
}

QList<ProtocolMessage> QushengProtocol::feedReceivedData(const QByteArray &data,
                                                          QList<TranslationError> *errors)
{
    QList<ProtocolMessage> messages;
    m_buffer.append(data);
    if (m_buffer.size() > 4096) {
        m_buffer.clear();
        if (errors) errors->append({"RX_OVERFLOW", QStringLiteral("接收缓存超过 4096 字节，已清空")});
        return messages;
    }
    while (m_buffer.size() >= ReplySize) {
        const QByteArray frame = m_buffer.left(ReplySize);
        if (readLe16(frame, ReplySize - 2) != crc16(frame.left(ReplySize - 2))) {
            m_buffer.remove(0, 1); // re-synchronise without discarding a possible following frame
            if (errors) errors->append({"QUSHENG_CRC", QStringLiteral("趋盛回告 CRC16 校验失败")});
            continue;
        }
        m_buffer.remove(0, ReplySize);
        ProtocolMessage message;
        message.messageType = quint8(frame.at(3));
        message.payload = frame.mid(0, 5);
        message.wireData = frame;
        messages.append(message);
    }
    return messages;
}

bool QushengProtocol::matchesReply(const UnifiedCommand &, const ProtocolMessage &message) const
{
    return message.payload.size() == 5 && quint8(message.payload.at(0)) == 0x01
            && quint8(message.payload.at(1)) == 0x02 && quint8(message.payload.at(2)) == 0x11
            && quint8(message.payload.at(3)) == 0x11;
}

bool QushengProtocol::decodeReply(const UnifiedCommand &command, const ProtocolMessage &message,
                                  UnifiedResult *result, TranslationError *error) const
{
    if (!result || !error || !matchesReply(command, message)) {
        if (error) *error = {"QUSHENG_NOT_MATCHED", QStringLiteral("回告与当前请求地址或命令不匹配")};
        return false;
    }
    result->requestId = command.requestId;
    result->success = quint8(message.payload.at(4)) == 0;
    result->data.insert("axis", command.parameters.value("axis"));
    if (!result->success)
        result->error = {"QUSHENG_REJECTED", QStringLiteral("趋盛设备返回状态 %1").arg(quint8(message.payload.at(4)))};
    return true;
}
