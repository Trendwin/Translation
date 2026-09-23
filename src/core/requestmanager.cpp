#include "requestmanager.h"

RequestManager::RequestManager(IInternalProtocol *protocol, ITransport *transport, QObject *parent)
    : QObject(parent), m_protocol(protocol), m_transport(transport)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &RequestManager::onTimeout);
    connect(m_transport, &ITransport::sendFinished, this, &RequestManager::onSendFinished);
    connect(m_transport, &ITransport::bytesReceived, this, &RequestManager::onBytesReceived);
    connect(m_transport, &ITransport::connectionChanged, this, &RequestManager::onConnectionChanged);
}

void RequestManager::enqueue(const UnifiedCommand &command)
{
    m_queue.enqueue(command);
    emit stateChanged(command.requestId, RequestState::Queued, QString::fromUtf8(u8"已排队"));
    QTimer::singleShot(0, this, &RequestManager::startNext);
}

void RequestManager::startNext()
{
    if (m_active || m_queue.isEmpty()) return;
    m_current = m_queue.dequeue();
    m_active = true;
    if (!m_transport->isConnected()) { finishFailure(RequestState::Failed, "DISCONNECTED", QString::fromUtf8(u8"通信未连接")); return; }
    QByteArray frame;
    TranslationError error;
    if (!m_protocol->encodeCommand(m_current, &frame, &error)) {
        finishFailure(RequestState::Failed, error.code.isEmpty() ? "ENCODE_FAILED" : error.code,
                      error.message.isEmpty() ? QString::fromUtf8(u8"协议编码失败") : error.message);
        return;
    }
    emit frameReady(m_current.requestId, frame);
    emit stateChanged(m_current.requestId, RequestState::Sending, QString::fromUtf8(u8"正在发送；尚未表示下位机执行成功"));
    m_transport->sendBytes(m_current.requestId, frame);
}

void RequestManager::onSendFinished(quint64 requestId, bool success, const QString &reason)
{
    if (!m_active || requestId != m_current.requestId) return; // 丢弃上一请求的迟到发送回调。
    if (!success) { finishFailure(RequestState::Failed, "SEND_FAILED", reason); return; }
    emit stateChanged(requestId, RequestState::WaitingReply, QString::fromUtf8(u8"发送成功，等待下位机执行回告"));
    m_timer.start(m_timeoutMs);
}

void RequestManager::onBytesReceived(const QByteArray &data)
{
    emit rawDataReceived(data);
    QList<TranslationError> errors;
    const QList<ProtocolMessage> messages = m_protocol->feedReceivedData(data, &errors);
    for (const TranslationError &error : errors) emit protocolError(error);
    for (const ProtocolMessage &message : messages) {
        if (!m_active || !m_protocol->matchesReply(m_current, message)) {
            emit unsolicitedMessage(message); // 无关回告和主动上报不缓存。
            continue;
        }
        UnifiedResult result;
        TranslationError error;
        if (!m_protocol->decodeReply(m_current, message, &result, &error)) {
            finishFailure(RequestState::Failed, error.code, error.message);
            continue;
        }
        m_timer.stop();
        m_active = false;
        emit stateChanged(result.requestId, result.success ? RequestState::Succeeded : RequestState::Failed,
                          result.success ? QString::fromUtf8(u8"下位机执行成功") : result.error.message);
        emit completed(result);
        QTimer::singleShot(0, this, &RequestManager::startNext);
    }
}

void RequestManager::cancel(quint64 requestId)
{
    if (m_active && m_current.requestId == requestId) {
        finishFailure(RequestState::Cancelled, "CANCELLED", QString::fromUtf8(u8"请求已取消"));
        return;
    }
    for (int i = 0; i < m_queue.size(); ++i) if (m_queue.at(i).requestId == requestId) {
        const UnifiedCommand command = m_queue.takeAt(i);
        UnifiedResult result; result.requestId = command.requestId; result.error = {"CANCELLED", QString::fromUtf8(u8"排队请求已取消")};
        emit stateChanged(requestId, RequestState::Cancelled, result.error.message); emit completed(result); return;
    }
}

void RequestManager::onConnectionChanged(bool connected, const QString &reason)
{
    if (!connected && m_active) finishFailure(RequestState::Failed, "DISCONNECTED", reason);
}

void RequestManager::onTimeout() { if (m_active) finishFailure(RequestState::TimedOut, "TIMEOUT", QString::fromUtf8(u8"等待设备回告超时")); }

void RequestManager::finishFailure(RequestState state, const QString &code, const QString &message)
{
    if (!m_active) return;
    m_timer.stop();
    // 丢弃失败请求遗留的半帧，避免其在下一请求中继续拼接；完整迟到帧仍须由真实协议匹配规则识别。
    m_protocol->clearReceiveBuffer();
    UnifiedResult result; result.requestId = m_current.requestId; result.error = {code, message};
    m_active = false;
    emit stateChanged(result.requestId, state, message);
    emit completed(result);
    QTimer::singleShot(0, this, &RequestManager::startNext);
}
