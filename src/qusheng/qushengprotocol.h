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
    static QByteArray makeFrame(quint8 destination, quint8 source, quint8 sequence,
                                quint8 command, const QByteArray &data);
    static QByteArray makeReply(quint8 sequence, quint8 errorCode,
                                const QByteArray &basicStatus = QByteArray(3, '\0'));
private:
    QByteArray m_buffer;
    mutable quint8 m_nextSequence = 0;
    mutable quint8 m_pendingSequence = 0;
    mutable quint64 m_pendingRequestId = 0;
    mutable bool m_hasPendingSequence = false;
};

#endif
