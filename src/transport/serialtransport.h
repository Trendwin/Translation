#ifndef SERIALTRANSPORT_H
#define SERIALTRANSPORT_H

#include "itransport.h"
#include <QSerialPort>

class SerialTransport : public ITransport
{
    Q_OBJECT
public:
    explicit SerialTransport(QObject *parent = nullptr);
    bool isConnected() const override { return m_port.isOpen(); }
    void sendBytes(quint64 requestId, const QByteArray &data) override;
    bool open(const QString &portName, qint32 baudRate = QSerialPort::Baud115200);
    void close();
    QString errorString() const { return m_port.errorString(); }
private:
    QSerialPort m_port;
    quint64 m_pendingRequest = 0;
    qint64 m_pendingBytes = 0;
};

#endif
