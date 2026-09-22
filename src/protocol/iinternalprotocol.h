#ifndef IINTERNALPROTOCOL_H
#define IINTERNALPROTOCOL_H

#include "model/translationtypes.h"
#include <QObject>

// 内部协议接口。真实实现应在这里提供编码、分帧、校验、解析及严格的回告匹配规则。
class IInternalProtocol : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    ~IInternalProtocol() override = default;
    virtual bool encodeCommand(const UnifiedCommand &command, QByteArray *frame,
                               TranslationError *error) const = 0;
    // 可多次输入半包，也可一次输入多个粘连帧；返回本次新解析出的消息。
    virtual QList<ProtocolMessage> feedReceivedData(const QByteArray &data,
                                                     QList<TranslationError> *errors) = 0;
    virtual bool matchesReply(const UnifiedCommand &command,
                              const ProtocolMessage &message) const = 0;
    virtual bool decodeReply(const UnifiedCommand &command, const ProtocolMessage &message,
                             UnifiedResult *result, TranslationError *error) const = 0;
    virtual void clearReceiveBuffer() = 0;
};

#endif
