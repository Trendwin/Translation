#ifndef QUSHENGPROTOCOL_H
#define QUSHENGPROTOCOL_H

#include "protocol/iinternalprotocol.h"

class QushengProtocol : public IInternalProtocol
{
    Q_OBJECT
public:
    using IInternalProtocol::IInternalProtocol;
    bool encodeCommand(const UnifiedCommand &, QByteArray *, TranslationError *) const override;
    QList<ProtocolMessage> feedReceivedData(const QByteArray &, QList<TranslationError> *) override;
    bool matchesReply(const UnifiedCommand &, const ProtocolMessage &) const override;
    bool decodeReply(const UnifiedCommand &, const ProtocolMessage &, UnifiedResult *, TranslationError *) const override;
    void clearReceiveBuffer() override { m_buffer.clear(); }
    static quint16 crc16(const QByteArray &data);
    static QByteArray makeReply(quint8 destination, quint8 source, quint8 deviceId,
                                quint8 command, quint8 status);
private:
    QByteArray m_buffer;
};

#endif
