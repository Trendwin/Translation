#ifndef DTCLIENTPROTOCOL_H
#define DTCLIENTPROTOCOL_H

#include "protocol/iclientprotocol.h"

// Compatibility adapter for semantic DT parsing. Production conversion uses
// ConversionEngine with an explicit device calibration profile.
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
    static QByteArray statusReply(bool busy,int errorCode=0);
};

#endif
