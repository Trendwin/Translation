#ifndef UPSTREAMFORWARDER_H
#define UPSTREAMFORWARDER_H

#include "config/protocolconfig.h"
#include "model/translationtypes.h"
#include <QObject>
#include <QSet>

class ITransport;
class TranslationService;

// Bridges complete upstream frames to the existing ordered downstream queue.
// Only saved configuration snapshots are used; disconnected input is not retained.
class UpstreamForwarder : public QObject
{
    Q_OBJECT
public:
    UpstreamForwarder(ITransport *transport, TranslationService *service, QObject *parent = nullptr);
    void setConfig(const ProtocolConfig &config);
    void forwardFrame(const QByteArray &frame);

signals:
    void queued(quint64 groupId, const QString &commandCode, int stepCount);
    void rejected(const QString &code, const QString &message);
    void progress(quint64 groupId, int step, int total, const QString &detail);
    void finished(const UnifiedGroupResult &result);

private:
    struct DeferredEvent {
        bool completed = false;
        UnifiedGroupResult result;
        quint64 groupId = 0;
        int step = 0;
        int total = 0;
        QString detail;
    };
    void groupProgress(quint64 groupId, int step, int total, const QString &detail);
    void groupCompleted(const UnifiedGroupResult &result);
    void disconnected();

    ITransport *m_transport;
    TranslationService *m_service;
    ProtocolConfig m_config;
    QSet<quint64> m_pendingGroups;
    QList<DeferredEvent> m_deferred;
    bool m_submitting = false;
    bool m_disconnectedDuringSubmission = false;
};

#endif
