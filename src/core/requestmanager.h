#ifndef REQUESTMANAGER_H
#define REQUESTMANAGER_H
#include "protocol/iinternalprotocol.h"
#include "transport/itransport.h"
#include <QQueue>
#include <QTimer>

class RequestManager : public QObject
{
    Q_OBJECT
public:
    RequestManager(IInternalProtocol *protocol,ITransport *transport,QObject *parent=nullptr);
    void enqueue(const UnifiedCommand &command);
    void cancel(quint64 requestId);
    void setTimeoutMs(int milliseconds) { m_ackTimeoutMs=milliseconds; }
    void setTimeouts(int sendMs,int ackMs,int motionMs);
    bool deviceBusy() const { return m_deviceBusy; }
signals:
    void stateChanged(quint64 requestId,RequestState state,const QString &detail);
    void frameReady(quint64 requestId,const QByteArray &frame);
    void rawDataReceived(const QByteArray &data);
    void completed(const UnifiedResult &result);
    void unsolicitedMessage(const ProtocolMessage &message);
    void protocolError(const TranslationError &error);
    void deviceBusyChanged(bool busy);
private:
    void startNext();
    void onSendFinished(quint64 requestId,bool success,const QString &reason);
    void onBytesReceived(const QByteArray &data);
    void onConnectionChanged(bool connected,const QString &reason);
    void failCurrent(RequestState state,const QString &code,const QString &detail);
    void finishCurrent();
    void startProbe();
    void scheduleProbe();
    void setBusy(bool busy);
    IInternalProtocol *m_protocol;
    ITransport *m_transport;
    QQueue<UnifiedCommand> m_queue;
    UnifiedCommand m_current,m_probe;
    bool m_active=false,m_probeActive=false,m_acked=false,m_upstreamReplied=false,m_deviceBusy=false;
    int m_sendTimeoutMs=1000,m_ackTimeoutMs=800,m_motionTimeoutMs=30000;
    QTimer m_sendTimer,m_ackTimer,m_motionTimer,m_pollTimer;
};
#endif
