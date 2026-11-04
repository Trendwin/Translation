#ifndef UPSTREAMSERIALRECEIVER_H
#define UPSTREAMSERIALRECEIVER_H

#include "config/protocolconfig.h"
#include "core/conversionengine.h"
#include <QObject>
#include <QSerialPort>
#include <memory>

// Receives customer bytes and extracts complete frames. This endpoint has no write API.
class UpstreamSerialReceiver : public QObject
{
    Q_OBJECT
public:
    explicit UpstreamSerialReceiver(QObject *parent = nullptr);
    ~UpstreamSerialReceiver() override;
    bool setConfig(const ProtocolConfig &config);
    bool open(const QString &portName, qint32 baudRate = 115200);
    void close();
    bool isConnected() const { return m_port.isOpen(); }
    QString portName() const { return m_portName; }
    void clear();
    void feedBytes(const QByteArray &bytes);

signals:
    void rawReceived(const QByteArray &bytes);
    void frameReceived(const QByteArray &frame);
    void errorOccurred(const QString &reason);
    void connectionChanged(bool connected, const QString &reason);

private:
    void readAvailable();
    QString serialError() const;
    void handleSerialError(QSerialPort::SerialPortError error);

    QSerialPort m_port;
    QString m_portName;
    std::unique_ptr<ConversionEngine> m_engine;
    QJsonObject m_formatSignature;
    quint64 m_generation = 0;
};

#endif
