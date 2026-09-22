#ifndef DEMOINTERNALPROTOCOL_H
#define DEMOINTERNALPROTOCOL_H
#include "protocol/iinternalprotocol.h"

// 演示帧：AA 55、1 字节长度、类型、负载、异或校验。该格式不得用于真实设备。
class DemoInternalProtocol : public IInternalProtocol
{
    Q_OBJECT
public:
    using IInternalProtocol::IInternalProtocol;
    bool encodeCommand(const UnifiedCommand &, QByteArray *, TranslationError *) const override;
    QList<ProtocolMessage> feedReceivedData(const QByteArray &, QList<TranslationError> *) override;
    bool matchesReply(const UnifiedCommand &, const ProtocolMessage &) const override;
    bool decodeReply(const UnifiedCommand &, const ProtocolMessage &, UnifiedResult *, TranslationError *) const override;
    void clearReceiveBuffer() override { m_buffer.clear(); }
    static QByteArray makeFrame(quint8 type, const QByteArray &payload);
private:
    QByteArray m_buffer;
    static const int MaxBufferSize = 4096;
};
#endif
