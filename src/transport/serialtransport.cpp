#include "serialtransport.h"

namespace {
QString serialReason(QSerialPort &port)
{
    // 常见驱动错误先给出可操作的中文原因，再保留系统详情用于排查。
    QString reason;
    switch (port.error()) {
    case QSerialPort::DeviceNotFoundError: reason = QString::fromUtf8(u8"找不到串口设备，请检查连接并刷新串口"); break;
    case QSerialPort::PermissionError: reason = QString::fromUtf8(u8"串口被占用或无访问权限，请关闭其他串口软件"); break;
    case QSerialPort::ResourceError: reason = QString::fromUtf8(u8"串口设备已断开或驱动不可用"); break;
    case QSerialPort::WriteError: reason = QString::fromUtf8(u8"串口写出失败，请检查下游连接"); break;
    case QSerialPort::ReadError: reason = QString::fromUtf8(u8"串口读取失败，请检查下游连接"); break;
    default: reason = QString::fromUtf8(u8"串口操作失败"); break;
    }
    return reason + QString::fromUtf8(u8"；系统详情：") + port.errorString();
}
}

SerialTransport::SerialTransport(QObject *parent) : ITransport(parent)
{
    // 串口层只转交原始字节，不在readyRead中假定一次读取就是完整协议帧。
    // 半包/粘包及请求归属分别交给协议适配器、请求管理器处理。
    connect(&m_port, &QSerialPort::readyRead, this, [this]() { emit bytesReceived(m_port.readAll()); });
    connect(&m_port, &QSerialPort::bytesWritten, this, [this](qint64 count) {
        if (!m_pendingRequest) return;
        // QSerialPort::write 只表示进入本地发送缓冲；累计 bytesWritten 到完整帧长度后，
        // 才能把请求状态推进为“已写出”。这与设备是否应答、动作是否完成是三个独立状态。
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
            emit sendFinished(id, false, serialReason(m_port));
        }
        if (value == QSerialPort::ResourceError) {
            const QString reason = serialReason(m_port);
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
    emit connectionChanged(opened, opened ? QString::fromUtf8(u8"串口已连接") : serialReason(m_port));
    return opened;
}

void SerialTransport::close()
{
    if (!m_port.isOpen()) return;
    m_port.close();
    m_pendingRequest = 0;
    m_pendingBytes = 0;
    emit connectionChanged(false, QString::fromUtf8(u8"串口已关闭"));
}

void SerialTransport::sendBytes(quint64 requestId, const QByteArray &data)
{
    if (!m_port.isOpen()) { emit sendFinished(requestId, false, QString::fromUtf8(u8"串口未连接")); return; }
    // 串口层只允许一个未完成写操作，防止不同请求的字节在驱动缓冲中交织。
    // 运动命令失败或超时后不在这里自动重发，以免设备重复执行动作。
    if (m_pendingRequest) { emit sendFinished(requestId, false, QString::fromUtf8(u8"串口仍在发送上一报文")); return; }
    m_pendingRequest = requestId;
    m_pendingBytes = data.size();
    // write的返回值是驱动接纳字节数；完整写出由bytesWritten累计确认，设备应答还要另等。
    const qint64 accepted = m_port.write(data);
    if (accepted != data.size() && m_pendingRequest) {
        m_pendingRequest = 0;
        m_pendingBytes = 0;
        const QString reason = accepted < 0 ? serialReason(m_port)
            : QString::fromUtf8(u8"串口未接纳完整报文，已断开连接；请重新连接并查询设备状态，禁止自动重发运动");
        // 部分报文已经进入驱动时，其设备效果未知。关闭连接而不是拼接另一个请求。
        m_port.close();
        emit sendFinished(requestId, false, reason);
        emit connectionChanged(false, reason);
    }
}
