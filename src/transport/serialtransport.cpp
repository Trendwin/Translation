#include "serialtransport.h"

SerialTransport::SerialTransport(QObject *parent) : ITransport(parent)
{
    connect(&m_port, &QSerialPort::readyRead, this, [this]() { emit bytesReceived(m_port.readAll()); });
    connect(&m_port, &QSerialPort::bytesWritten, this, [this](qint64 count) {
        if (!m_pendingRequest) return;
        m_pendingBytes -= count;
        if (m_pendingBytes <= 0) {
            const quint64 id = m_pendingRequest;
            m_pendingRequest = 0;
            emit sendFinished(id, true, QString());
        }
    });
    connect(&m_port, &QSerialPort::errorOccurred, this, [this](QSerialPort::SerialPortError value) {
        if (value == QSerialPort::NoError) return;
        if (m_pendingRequest) {
            const quint64 id = m_pendingRequest; m_pendingRequest = 0;
            emit sendFinished(id, false, m_port.errorString());
        }
        if (value == QSerialPort::ResourceError) {
            const QString reason = m_port.errorString();
            m_port.close();
            m_pendingBytes = 0;
            emit connectionChanged(false, reason);
        }
    });
}

bool SerialTransport::open(const QString &portName, qint32 baudRate)
{
    if (m_port.isOpen()) m_port.close();
    m_port.setPortName(portName);
    m_port.setBaudRate(baudRate);
    m_port.setDataBits(QSerialPort::Data8);
    m_port.setParity(QSerialPort::NoParity);
    m_port.setStopBits(QSerialPort::OneStop);
    m_port.setFlowControl(QSerialPort::NoFlowControl);
    const bool opened = m_port.open(QIODevice::ReadWrite);
    emit connectionChanged(opened, opened ? QStringLiteral("串口已连接") : m_port.errorString());
    return opened;
}

void SerialTransport::close()
{
    if (!m_port.isOpen()) return;
    m_port.close();
    m_pendingRequest = 0;
    m_pendingBytes = 0;
    emit connectionChanged(false, QStringLiteral("串口已关闭"));
}

void SerialTransport::sendBytes(quint64 requestId, const QByteArray &data)
{
    if (!m_port.isOpen()) { emit sendFinished(requestId, false, QStringLiteral("串口未连接")); return; }
    if (m_pendingRequest) { emit sendFinished(requestId, false, QStringLiteral("串口仍在发送上一报文")); return; }
    m_pendingRequest = requestId;
    m_pendingBytes = data.size();
    const qint64 accepted = m_port.write(data);
    if (accepted < 0) {
        m_pendingRequest = 0;
        emit sendFinished(requestId, false, m_port.errorString());
    }
}
