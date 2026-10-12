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
    // 每个匹配且已解码的回告立即交给界面；显示结果与释放运动请求是两件事。
    void replyReceived(const UnifiedResult &result);
    void completed(const UnifiedResult &result);
    void unsolicitedMessage(const ProtocolMessage &message);
    void protocolError(const TranslationError &error);
    void deviceBusyChanged(bool busy);
private:
    void startNext();
    void sendCurrent();
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
    // completionEmitted 只表示结果信号是否已经发出；设备忙闲状态仍由后续 0x12 查询独立维护。
    bool m_active=false,m_probeActive=false,m_acked=false,m_completionEmitted=false,m_deviceBusy=false;
    // 相对动作的前置查询不能与动作完成轮询合并：前者成功前绝不能发送运动。
    bool m_preflight=false;
    int m_sendTimeoutMs=1000,m_ackTimeoutMs=800,m_motionTimeoutMs=30000;
    QTimer m_sendTimer,m_ackTimer,m_motionTimer,m_pollTimer;
};
#endif
