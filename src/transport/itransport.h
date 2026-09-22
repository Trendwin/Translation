#ifndef ITRANSPORT_H
#define ITRANSPORT_H

#include <QObject>
#include <QByteArray>

// 通信层只搬运原始字节。对象由组装它的上层所有；sendFinished 只表示数据发送，不表示设备执行成功。
class ITransport : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    ~ITransport() override = default;
    virtual bool isConnected() const = 0;
    virtual void sendBytes(quint64 requestId, const QByteArray &data) = 0;
signals:
    void bytesReceived(const QByteArray &data);
    void sendFinished(quint64 requestId, bool success, const QString &reason);
    void connectionChanged(bool connected, const QString &reason);
};

#endif
