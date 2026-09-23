#include "demointernalprotocol.h"

QByteArray DemoInternalProtocol::makeFrame(quint8 type, const QByteArray &payload)
{
    if (payload.size() > 253) return QByteArray();
    QByteArray frame("\xAA\x55", 2);
    frame.append(char(payload.size() + 1));
    frame.append(char(type));
    frame.append(payload);
    quint8 check = 0;
    for (int i = 2; i < frame.size(); ++i) check ^= quint8(frame.at(i));
    frame.append(char(check));
    return frame;
}

bool DemoInternalProtocol::encodeCommand(const UnifiedCommand &command, QByteArray *frame, TranslationError *error) const
{
    if (!frame || !error) return false;
    const QByteArray payload = command.operation.toUtf8() + '|' + command.targetDevice.toUtf8();
    *frame = makeFrame(0x01, payload);
    if (frame->isEmpty()) { *error = {"DEMO_TOO_LONG", QString::fromUtf8(u8"演示报文负载过长")}; return false; }
    return true;
}

QList<ProtocolMessage> DemoInternalProtocol::feedReceivedData(const QByteArray &data, QList<TranslationError> *errors)
{
    QList<ProtocolMessage> out;
    m_buffer.append(data);
    if (m_buffer.size() > MaxBufferSize) {
        m_buffer.clear();
        if (errors) errors->append({"RX_OVERFLOW", QString::fromUtf8(u8"接收缓存超过 4096 字节，已清空")});
        return out;
    }
    const QByteArray header("\xAA\x55", 2);
    while (true) {
        const int start = m_buffer.indexOf(header);
        if (start < 0) { if (m_buffer.endsWith(char(0xAA))) m_buffer = QByteArray(1, char(0xAA)); else m_buffer.clear(); break; }
        if (start > 0) m_buffer.remove(0, start);
        if (m_buffer.size() < 4) break;
        const int bodyLength = quint8(m_buffer.at(2));
        if (bodyLength < 1) { m_buffer.remove(0, 2); continue; }
        const int frameLength = 2 + 1 + bodyLength + 1;
        if (m_buffer.size() < frameLength) break;
        const QByteArray frame = m_buffer.left(frameLength);
        m_buffer.remove(0, frameLength);
        quint8 check = 0;
        for (int i = 2; i < frame.size() - 1; ++i) check ^= quint8(frame.at(i));
        if (check != quint8(frame.back())) { if (errors) errors->append({"DEMO_CHECKSUM", QString::fromUtf8(u8"演示帧校验失败")}); continue; }
        ProtocolMessage message;
        message.messageType = quint8(frame.at(3));
        message.payload = frame.mid(4, bodyLength - 1);
        message.wireData = frame;
        out.append(message);
    }
    return out;
}

bool DemoInternalProtocol::matchesReply(const UnifiedCommand &command, const ProtocolMessage &message) const
{
    if (message.messageType != 0x81) return false;
    const QList<QByteArray> fields = message.payload.split('|');
    return fields.size() >= 2 && fields.at(0) == command.operation.toUtf8()
            && fields.at(1) == command.targetDevice.toUtf8();
}

bool DemoInternalProtocol::decodeReply(const UnifiedCommand &command, const ProtocolMessage &message,
                                       UnifiedResult *result, TranslationError *error) const
{
    if (!result || !error || !matchesReply(command, message)) {
        if (error) *error = {"DEMO_NOT_MATCHED", QString::fromUtf8(u8"演示回告与当前请求不匹配")};
        return false;
    }
    const QList<QByteArray> fields = message.payload.split('|');
    result->requestId = command.requestId;
    result->success = fields.size() >= 3 && fields.at(2) == "OK";
    if (result->success) result->data.insert("value", QString::fromUtf8(fields.value(3, "PONG")));
    else result->error = {"DEVICE_REJECTED", QString::fromUtf8(u8"演示下位机返回失败")};
    return true;
}
