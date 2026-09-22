#ifndef ICLIENTPROTOCOL_H
#define ICLIENTPROTOCOL_H

#include "model/translationtypes.h"
#include <QObject>

// 客户协议适配器：同步完成文字与统一模型之间的转换，不参与通信，也不持有请求。
class IClientProtocol : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    ~IClientProtocol() override = default;
    virtual bool parseCommand(const QString &text, UnifiedCommand *command,
                              TranslationError *error) const = 0;
    virtual bool buildReply(const UnifiedResult &result, QString *reply,
                            TranslationError *error) const = 0;
};

#endif
