#ifndef TRANSLATIONSERVICE_H
#define TRANSLATIONSERVICE_H
#include "requestmanager.h"

// 界面只提交已按配置转换的统一命令；设备结果只显示在本机，不生成客户回写报文。
class TranslationService : public QObject
{
    Q_OBJECT
public:
    TranslationService(RequestManager *requests, QObject *parent = nullptr);
    quint64 submitUnifiedCommand(UnifiedCommand command);
    void cancelRequest(quint64 requestId) { m_requests->cancel(requestId); }
signals:
    void requestStateChanged(quint64 requestId, RequestState state, const QString &detail);
    void outgoingFrame(quint64 requestId, const QByteArray &frame);
    void rawDataReceived(const QByteArray &data);
    void completed(const UnifiedResult &result);
    void errorOccurred(quint64 requestId, const TranslationError &error);
    void unsolicitedMessage(const ProtocolMessage &message);
private:
    RequestManager *m_requests;
    quint64 m_nextRequestId = 1;
};
#endif
