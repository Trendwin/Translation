#include "upstreamforwarder.h"
#include "conversionengine.h"
#include "translationservice.h"
#include "transport/itransport.h"
#include <QJsonDocument>

namespace {
const int MaximumPendingGroups = 128;
}

UpstreamForwarder::UpstreamForwarder(ITransport *transport, TranslationService *service, QObject *parent)
    : QObject(parent), m_transport(transport), m_service(service)
{
    connect(service, &TranslationService::groupProgress, this, &UpstreamForwarder::groupProgress);
    connect(service, &TranslationService::groupCompleted, this, &UpstreamForwarder::groupCompleted);
    connect(transport, &ITransport::connectionChanged, this, [this](bool connected, const QString &) {
        if (!connected) disconnected();
    });
}

void UpstreamForwarder::setConfig(const ProtocolConfig &config)
{
    // Invalid saved edits must replace the old snapshot as well, so they cannot
    // silently leave an older mapping active. Valid roots receive a stable version.
    m_config = config;
    ProtocolConfig normalized;
    TranslationError error;
    if (ProtocolConfig::fromJson(QJsonDocument(config.root).toJson(), &normalized, &error))
        m_config = normalized;
}

void UpstreamForwarder::forwardFrame(const QByteArray &frame)
{
    if (!m_transport->isConnected()) {
        emit rejected(QStringLiteral("DISCONNECTED"), QString::fromUtf8(u8"下游串口未连接，本帧未转发"));
        return;
    }
    if (m_submitting) {
        emit rejected(QStringLiteral("FORWARD_BUSY"), QString::fromUtf8(u8"正在提交上一条上游报文，本帧未转发"));
        return;
    }
    if (m_pendingGroups.size() >= MaximumPendingGroups) {
        emit rejected(QStringLiteral("FORWARD_QUEUE_FULL"), QString::fromUtf8(u8"自动转发队列已满，本帧未转发，请等待下游处理"));
        return;
    }

    // Conversion validates the complete saved configuration and every step before
    // anything enters the queue. In particular, retain field and motion limits.
    const ProtocolConfig snapshot = m_config;
    ConversionPreview converted;
    TranslationError error;
    if (!ConversionEngine(snapshot).convertBatch(frame, true, &converted, &error)) {
        emit rejected(error.code, error.message);
        return;
    }
    QList<UnifiedCommand> commands;
    for (const auto &step : converted.steps) {
        auto command = step.command;
        // Keep the current runtime's sequence wrap/direct relative-send policy;
        // this does not relax configuration limits, already checked above.
        command.packetDebug = true;
        commands.append(command);
    }

    m_submitting = true;
    m_disconnectedDuringSubmission = false;
    m_deferred.clear();
    const quint64 groupId = m_service->submitUnifiedGroup(commands);
    m_pendingGroups.insert(groupId);
    const auto events = m_deferred;
    m_deferred.clear();
    const bool disconnectedDuringSubmission = m_disconnectedDuringSubmission;
    m_submitting = false;
    emit queued(groupId, converted.matchedCommandCode, commands.size());
    // Enqueue may synchronously fail or interrupt another group. Replay only the
    // events belonging to this submission after its allocated ID is known.
    for (const auto &event : events) {
        if (event.completed) {
            if (event.result.groupId == groupId) groupCompleted(event.result);
        } else if (event.groupId == groupId) {
            groupProgress(event.groupId, event.step, event.total, event.detail);
        }
    }
    if ((disconnectedDuringSubmission || !m_transport->isConnected()) && m_pendingGroups.contains(groupId))
        m_service->cancelRequest(groupId);
}

void UpstreamForwarder::groupProgress(quint64 groupId, int step, int total, const QString &detail)
{
    if (m_pendingGroups.contains(groupId)) {
        emit progress(groupId, step, total, detail);
    } else if (m_submitting) {
        DeferredEvent event;
        event.groupId = groupId; event.step = step; event.total = total; event.detail = detail;
        m_deferred.append(event);
    }
}

void UpstreamForwarder::groupCompleted(const UnifiedGroupResult &result)
{
    if (m_pendingGroups.remove(result.groupId)) {
        emit finished(result);
    } else if (m_submitting) {
        DeferredEvent event;
        event.completed = true; event.result = result;
        m_deferred.append(event);
    }
}

void UpstreamForwarder::disconnected()
{
    if (m_submitting) m_disconnectedDuringSubmission = true;
    // Cancel synchronously while the transport is still disconnected. Waiting
    // for queued delivery could otherwise allow a quick reconnect to send old work.
    const auto groups = m_pendingGroups;
    for (quint64 groupId : groups) m_service->cancelRequest(groupId);
}
