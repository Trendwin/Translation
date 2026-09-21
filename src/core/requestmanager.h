#ifndef REQUESTMANAGER_H
#define REQUESTMANAGER_H
#include "protocol/iinternalprotocol.h"
#include "transport/itransport.h"
#include <QQueue>
#include <QTimer>

// 串行请求管理器；协议和通信对象均为外部所有，且必须比本对象活得久。
class RequestManager : public QObject
{
    Q_OBJECT
public:
    RequestManager(IInternalProtocol *protocol, ITransport *transport, QObject *parent = nullptr);
    void enqueue(const UnifiedCommand &command);
    void cancel(quint64 requestId);
    void setTimeoutMs(int milliseconds) { m_timeoutMs = milliseconds; }
signals:
    void stateChanged(quint64 requestId, RequestState state, const QString &detail);
    void frameReady(quint64 requestId, const QByteArray &frame);
    void rawDataReceived(const QByteArray &data);
    void completed(const UnifiedResult &result);
    void unsolicitedMessage(const ProtocolMessage &message);
    void protocolError(const TranslationError &error);
private slots:
    void startNext();
    void onSendFinished(quint64 requestId, bool success, const QString &reason);
    void onBytesReceived(const QByteArray &data);
    void onConnectionChanged(bool connected, const QString &reason);
    void onTimeout();
private:
    void finishFailure(RequestState state, const QString &code, const QString &message);
    IInternalProtocol *m_protocol;
    ITransport *m_transport;
    QQueue<UnifiedCommand> m_queue;
    UnifiedCommand m_current;
    bool m_active = false;
    int m_timeoutMs = 800;
    QTimer m_timer;
};
#endif
