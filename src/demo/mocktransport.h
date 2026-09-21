#ifndef MOCKTRANSPORT_H
#define MOCKTRANSPORT_H
#include "transport/itransport.h"

// 模拟通信仅验证异步边界、半包、粘包和超时；接入真实收发代码时替换本类即可。
class MockTransport : public ITransport
{
    Q_OBJECT
public:
    explicit MockTransport(QObject *parent = nullptr);
    bool isConnected() const override { return m_connected; }
    void sendBytes(quint64 requestId, const QByteArray &data) override;
    void setConnected(bool connected);
private:
    bool m_connected = true;
};
#endif
