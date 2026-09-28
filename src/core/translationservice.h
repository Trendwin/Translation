#ifndef TRANSLATIONSERVICE_H
#define TRANSLATIONSERVICE_H
#include "requestmanager.h"
#include "protocol/iclientprotocol.h"

// 界面的唯一业务入口。依赖对象由调用方所有，服务不越过接口访问具体实现。
class TranslationService : public QObject
{
    Q_OBJECT
public:
    TranslationService(IClientProtocol *client, RequestManager *requests, QObject *parent = nullptr);
    quint64 submitCommand(const QString &text);
    quint64 submitUnifiedCommand(UnifiedCommand command);
    void cancelRequest(quint64 requestId) { m_requests->cancel(requestId); }
signals:
    void requestStateChanged(quint64 requestId, RequestState state, const QString &detail);
    void outgoingFrame(quint64 requestId, const QByteArray &frame);
    void rawDataReceived(const QByteArray &data);
    void customerReply(quint64 requestId, const QString &reply);
    void customerReplyBytes(quint64 requestId, const QByteArray &reply);
    void completed(const UnifiedResult &result);
    void errorOccurred(quint64 requestId, const TranslationError &error);
    void unsolicitedMessage(const ProtocolMessage &message);
private slots:
    void onCompleted(const UnifiedResult &result);
private:
    IClientProtocol *m_client;
    RequestManager *m_requests;
    quint64 m_nextRequestId = 1;
};
#endif
