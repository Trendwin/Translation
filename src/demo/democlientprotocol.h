#ifndef DEMOCLIENTPROTOCOL_H
#define DEMOCLIENTPROTOCOL_H
#include "protocol/iclientprotocol.h"

// 仅用于框架演示，绝非任何客户的正式协议。
class DemoClientProtocol : public IClientProtocol
{
    Q_OBJECT
public:
    using IClientProtocol::IClientProtocol;
    bool parseCommand(const QString &, UnifiedCommand *, TranslationError *) const override;
    bool buildReply(const UnifiedResult &, QString *, TranslationError *) const override;
};
#endif
