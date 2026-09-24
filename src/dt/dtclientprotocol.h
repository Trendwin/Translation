#ifndef DTCLIENTPROTOCOL_H
#define DTCLIENTPROTOCOL_H

#include "protocol/iclientprotocol.h"

// DT A<n> is mapped to Qusheng's forward action in position mode. The terminator is optional on
// input because a line editor normally removes it.
class DtClientProtocol : public IClientProtocol
{
    Q_OBJECT
public:
    using IClientProtocol::IClientProtocol;
    bool parseCommand(const QString &text, UnifiedCommand *command,
                      TranslationError *error) const override;
    bool buildReply(const UnifiedResult &result, QString *reply,
                    TranslationError *error) const override;
    bool buildReplyBytes(const UnifiedResult &result, QByteArray *reply,
                         TranslationError *error) const override;
    static QByteArray successReply(int axis);
};

#endif
