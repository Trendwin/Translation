#include "mocktransport.h"
#include "demointernalprotocol.h"
#include <QTimer>

MockTransport::MockTransport(QObject *parent) : ITransport(parent) {}

void MockTransport::setConnected(bool connected)
{
    if (m_connected == connected) return;
    m_connected = connected;
    emit connectionChanged(connected, connected ? QStringLiteral("演示通信已连接") : QStringLiteral("演示通信已断开"));
}

void MockTransport::sendBytes(quint64 requestId, const QByteArray &data)
{
    if (!m_connected) { emit sendFinished(requestId, false, QStringLiteral("演示通信未连接")); return; }
    QTimer::singleShot(20, this, [this, requestId, data]() {
        emit sendFinished(requestId, true, QString());
        if (data.size() < 5) return;
        const QByteArray request = data.mid(4, quint8(data.at(2)) - 1);
        const QList<QByteArray> fields = request.split('|');
        if (fields.value(0) == "TIMEOUT") return; // 故意无回告，验证超时收尾。
        const QByteArray status = fields.value(0) == "FAIL" ? "ERROR" : "OK";
        const QByteArray reply = DemoInternalProtocol::makeFrame(0x81,
            fields.value(0) + '|' + fields.value(1) + '|' + status + "|PONG");
        const QByteArray report = DemoInternalProtocol::makeFrame(0x90, "DEMO_REPORT");
        // 先发半包，再把余下半包与主动上报粘在一起，持续验证缓存和分帧。
        const int cut = reply.size() / 2;
        QTimer::singleShot(30, this, [this, reply, cut]() { emit bytesReceived(reply.left(cut)); });
        QTimer::singleShot(60, this, [this, reply, report, cut]() { emit bytesReceived(reply.mid(cut) + report); });
    });
}
