#include "qushengprotocol.h"
#include "qushengcommandcatalog.h"
#include "sdk/qushengsdk.h"

namespace {
const quint8 FrameByte = 0xaa;
const int FrameOverhead = 10;
const quint32 Maximum24Bit = 0xffffffu;
const quint32 ContinuousMoveDistance = 0xffffffu;

// 流拆帧仍只读取线上长度和CRC；具体命令字节布局统一交给SDK，不能从客户字段猜测。
quint16 readBe16(const QByteArray &data, int at)
{
    return (quint16(quint8(data.at(at))) << 8) | quint8(data.at(at + 1));
}

}

quint16 QushengProtocol::crc16(const QByteArray &data)
{ return QushengSdk::crc16(data); }

QByteArray QushengProtocol::makeFrame(quint8 destination, quint8 source, quint8 sequence,
                                     quint8 command, const QByteArray &data)
{ return QushengSdk::makeFrame(destination, source, sequence, command, data); }

bool QushengProtocol::encodeCommand(const UnifiedCommand &command, QByteArray *frame,
                                    TranslationError *error) const
{
    if (frame) frame->clear();
    if (!frame || !error) return false;
    // SDK支持组帧，不等于软件已确认停止/复位的执行完成语义；旧许可独立保留。
    if (!QushengCommandCatalog::isExecutable(command.operation)) {
        *error = {"COMMAND_NOT_IMPLEMENTED", QString::fromUtf8(u8"该接口尚未开放软件发送，或协议布局待确认")}; return false;
    }
    if (m_destination == 0 || m_source == 0 || m_destination == m_source) {
        *error = {"QUSHENG_ADDRESS", QString::fromUtf8(u8"趋盛地址未配置或冲突")}; return false;
    }
    if (m_nextSequence > 255) {
        *error = {"QUSHENG_SEQUENCE_EXHAUSTED", QString::fromUtf8(u8"本次连接已用完8位序号；请重新连接")}; return false;
    }
    const int code = QushengCommandCatalog::commandCode(command.operation);
    QVariantMap supplied = command.parameters;
    QVariantMap device;
    device.insert("devId", command.targetDevice.toUInt());
    if (QushengCommandCatalog::usesMotionEditor(command.operation)) {
        const bool relative = command.operation == "RELATIVE_FORWARD" || command.operation == "RELATIVE_BACKWARD";
        bool idOk = false; const uint id = command.targetDevice.toUInt(&idOk);
        if (!idOk || id < 0x11 || id > 0x1c) {
            *error = {"QUSHENG_DEVID", QString::fromUtf8(u8"电机DevID无效")}; return false;
        }
        const QString positionKey = relative ? QStringLiteral("distanceUm") : QStringLiteral("positionUm");
        bool speedOk = false, positionOk = false, actionOk = false;
        const qlonglong speed = command.parameters.value("speedUmPerS").toLongLong(&speedOk);
        const qlonglong position = command.parameters.value(positionKey).toLongLong(&positionOk);
        int action = QushengCommandCatalog::fixedAction(command.operation);
        if (relative) action = command.operation == "RELATIVE_FORWARD" ? 1 : 3;
        if (!action) action = command.parameters.value("absoluteAction").toInt(&actionOk);
        else actionOk = true;
        if (!speedOk || !positionOk || !actionOk || speed <= 0 || speed > Maximum24Bit
                || position < 0 || position >= ContinuousMoveDistance || (action != 1 && action != 3)) {
            *error = {"QUSHENG_RANGE", QString::fromUtf8(u8"速度、位置或方向不在有效范围")}; return false;
        }
        // 旧应用参数转换为SDK规范，不能把行程元数据或用户注入mode/action传进固定运动模板。
        supplied = QVariantMap{{"devId", id}, {"mode", relative ? 2 : 4}, {"action", action},
                              {"speedUmPerS", speed}, {"positionUm", position}};
    }
    QushengSdkRequest request;
    if (!QushengSdk::buildRequest(quint8(code), supplied, QVariantMap(), device, &request, error)) return false;
    const quint8 sequence = quint8(m_nextSequence);
    const QByteArray encoded = makeFrame(m_destination, m_source, sequence, quint8(code), request.payload);
    if (encoded.isEmpty()) { *error = {"QUSHENG_LENGTH", QString::fromUtf8(u8"趋盛负载过长")}; return false; }
    ++m_nextSequence; *frame = encoded;
    m_pendingRequestId = command.requestId; m_pendingSequence = sequence; m_hasPendingSequence = true;
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
    const int expectedCmd = QushengCommandCatalog::commandCode(command.operation);
    // 08空心跳、09合法模块故障记录可能使用与当前设置请求相同的序号。
    // 报文布局的判别留在趋盛适配层，通用请求状态层不读取这些SDK专用字节。
    if (message.messageType == 0x08 || message.messageType == 0x09) {
        QushengSdkReply report; TranslationError reportError;
        if (QushengSdk::decodeReport(quint8(message.messageType), message.payload, &report, &reportError)) return false;
    }
    // 成功/错误回告长度由SDK逐族检查；序号、地址和命令仍在传输适配层隔离。
    return m_hasPendingSequence && command.requestId == m_pendingRequestId && frame.size() >= FrameOverhead
        && quint8(frame.at(2)) == m_source && quint8(frame.at(3)) == m_destination
        && quint8(frame.at(6)) == m_pendingSequence && quint8(frame.at(7)) == expectedCmd;
}

bool QushengProtocol::decodeReply(const UnifiedCommand &command, const ProtocolMessage &message,
                                  UnifiedResult *result, TranslationError *error) const
{
    if (!result || !error) return false;
    *result = UnifiedResult(); result->requestId = command.requestId;
    QushengSdkReply reply;
    if (!QushengSdk::decodeReply(quint8(message.messageType), message.payload, &reply, error)) return false;
    result->success = reply.success; result->data = reply.values;
    result->data.insert("basicStatus", reply.basicStatus);
    if (!reply.success) result->error = {"QUSHENG_REJECTED", QString::fromUtf8(u8"趋盛设备返回错误码0x%1").arg(reply.errorCode, 2, 16, QLatin1Char('0'))};
    return true;
}
