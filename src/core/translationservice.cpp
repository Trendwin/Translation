#include "translationservice.h"

TranslationService::TranslationService(IClientProtocol *client, RequestManager *requests, QObject *parent)
    : QObject(parent), m_client(client), m_requests(requests)
{
    connect(requests, &RequestManager::stateChanged, this, &TranslationService::requestStateChanged);
    connect(requests, &RequestManager::frameReady, this, &TranslationService::outgoingFrame);
    connect(requests, &RequestManager::rawDataReceived, this, &TranslationService::rawDataReceived);
    connect(requests, &RequestManager::unsolicitedMessage, this, &TranslationService::unsolicitedMessage);
    connect(requests, &RequestManager::protocolError, this, [this](const TranslationError &e) { emit errorOccurred(0, e); });
    connect(requests, &RequestManager::completed, this, &TranslationService::onCompleted);
}

quint64 TranslationService::submitCommand(const QString &text)
{
    const quint64 id = m_nextRequestId++;
    UnifiedCommand command; command.requestId = id;
    TranslationError error;
    if (!m_client->parseCommand(text, &command, &error)) {
        emit requestStateChanged(id, RequestState::Failed, error.message);
        emit errorOccurred(id, error);
        return id;
    }
    m_requests->enqueue(command);
    return id;
}

void TranslationService::onCompleted(const UnifiedResult &result)
{
    QString reply;
    TranslationError error;
    if (!m_client->buildReply(result, &reply, &error)) { emit errorOccurred(result.requestId, error); return; }
    emit customerReply(result.requestId, reply);
}
