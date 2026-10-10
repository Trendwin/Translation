#include "translationservice.h"

TranslationService::TranslationService(RequestManager *requests, QObject *parent)
    : QObject(parent), m_requests(requests)
{
    connect(requests, &RequestManager::stateChanged, this, &TranslationService::requestStateChanged);
    connect(requests, &RequestManager::frameReady, this, &TranslationService::outgoingFrame);
    connect(requests, &RequestManager::rawDataReceived, this, &TranslationService::rawDataReceived);
    connect(requests, &RequestManager::unsolicitedMessage, this, &TranslationService::unsolicitedMessage);
    connect(requests, &RequestManager::protocolError, this,
            [this](const TranslationError &e) { emit errorOccurred(0, e); });
    connect(requests, &RequestManager::completed, this, &TranslationService::completed);
}

quint64 TranslationService::submitUnifiedCommand(UnifiedCommand command)
{
    // 请求编号在统一入口分配；复制的命令携带启用版本及数值，不持有可变草稿引用。
    command.requestId = m_nextRequestId++;
    m_requests->enqueue(command);
    return command.requestId;
}
