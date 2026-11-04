#include "upstreamserialreceiver.h"

#include <QSignalBlocker>
#include <QJsonArray>

namespace {
const int maximumFrameBytes = 4096;
const int maximumReadBytes = 4096;

QJsonObject formatSignature(const ProtocolConfig &config)
{
    QJsonObject signature{{"external", config.root.value("external")}};
    if (config.hasMappedCommand()) {
        QJsonArray commands;
        for (const auto &value : config.root.value("mappings").toArray()) {
            const auto mapping = value.toObject();
            commands.append(QJsonObject{{"commandCode", mapping.value("commandCode")},
                {"omitFields", mapping.value("input").toObject().value("omitFields")}});
        }
        signature.insert("commands", commands);
    }
    return signature;
}
}

UpstreamSerialReceiver::UpstreamSerialReceiver(QObject *parent) : QObject(parent)
{
    m_port.setReadBufferSize(64 * 1024);
    connect(&m_port, &QSerialPort::readyRead, this, &UpstreamSerialReceiver::readAvailable);
    connect(&m_port, &QSerialPort::errorOccurred, this, &UpstreamSerialReceiver::handleSerialError);
}

UpstreamSerialReceiver::~UpstreamSerialReceiver()
{
    // Member objects are destroyed before QObject disconnects this receiver.
    // Release the port quietly while the parser and all receiver state still exist.
    disconnect(&m_port, nullptr, this, nullptr);
    const QSignalBlocker blocker(&m_port);
    if (m_port.isOpen()) m_port.close();
}

bool UpstreamSerialReceiver::setConfig(const ProtocolConfig &config)
{
    TranslationError error;
    if (!config.validateFormat(&error)) {
        ++m_generation;
        m_engine.reset();
        m_formatSignature = QJsonObject();
        emit errorOccurred(QString::fromUtf8(u8"上游报文格式不可用：%1；仍可查看原始接收数据。")
                           .arg(error.message));
        return false;
    }
    const auto signature = formatSignature(config);
    if (m_engine && signature == m_formatSignature) return true;
    ++m_generation;
    m_formatSignature = signature;
    m_engine.reset(new ConversionEngine(config));
    return true;
}

bool UpstreamSerialReceiver::open(const QString &portName, qint32 baudRate)
{
    close();
    if (portName.trimmed().isEmpty() || baudRate <= 0) {
        const QString reason = QString::fromUtf8(u8"请选择上游串口和有效波特率。");
        emit errorOccurred(reason);
        emit connectionChanged(false, reason);
        return false;
    }

    bool opened = false;
    QString reason;
    QString nativeName = portName.trimmed();
    m_portName = nativeName;
#ifdef Q_OS_WIN
    const QString devicePrefix = QStringLiteral("\\\\.\\");
    const QString extendedPrefix = QStringLiteral("\\\\?\\");
    if (nativeName.startsWith(devicePrefix)) m_portName = nativeName.mid(devicePrefix.size());
    else if (!nativeName.startsWith(extendedPrefix)) nativeName.prepend(devicePrefix);
#endif
    {
        // Treat configuring and opening as one operation. Synchronous driver errors
        // are reported once below, rather than reentering the disconnection handler.
        const QSignalBlocker blocker(&m_port);
        m_port.setPortName(nativeName);
        const bool configured = m_port.setBaudRate(baudRate)
                && m_port.setDataBits(QSerialPort::Data8)
                && m_port.setParity(QSerialPort::NoParity)
                && m_port.setStopBits(QSerialPort::OneStop)
                && m_port.setFlowControl(QSerialPort::NoFlowControl);
        opened = configured && m_port.open(QIODevice::ReadOnly);
        reason = opened ? QString::fromUtf8(u8"上游串口已连接") : serialError();
    }
    if (!opened) emit errorOccurred(reason);
    emit connectionChanged(opened, reason);
    return opened;
}

void UpstreamSerialReceiver::close()
{
    const bool wasConnected = m_port.isOpen();
    {
        const QSignalBlocker blocker(&m_port);
        if (wasConnected) m_port.close();
    }
    clear();
    if (wasConnected) emit connectionChanged(false, QString::fromUtf8(u8"上游串口已关闭"));
}

void UpstreamSerialReceiver::clear()
{
    ++m_generation;
    if (m_engine) m_engine->clear();
}

void UpstreamSerialReceiver::readAvailable()
{
    while (m_port.isOpen() && m_port.bytesAvailable() > 0) {
        const QByteArray bytes = m_port.read(maximumReadBytes);
        if (bytes.isEmpty()) break;
        feedBytes(bytes);
    }
}

void UpstreamSerialReceiver::feedBytes(const QByteArray &bytes)
{
    if (bytes.isEmpty()) return;
    const quint64 generation = m_generation;
    emit rawReceived(bytes);
    if (generation != m_generation || !m_engine) return;

    int position = 0;
    while (position < bytes.size() && generation == m_generation && m_engine) {
        const int remaining = maximumFrameBytes - m_engine->bufferedByteCount();
        const int count = qMin(bytes.size() - position, remaining > 0 ? remaining : 1);
        const QByteArray piece = bytes.mid(position, count);
        QList<TranslationError> errors;
        const QList<QByteArray> frames = m_engine->feed(piece, &errors);
        bool overflow = false;
        for (const auto &error : errors) {
            overflow |= error.code == QStringLiteral("INPUT_OVERFLOW");
            emit errorOccurred(error.message);
            if (generation != m_generation) return;
        }
        if (overflow) {
            // The engine discarded its full oversized partial frame. Feed the byte
            // that exceeded the limit again: it might start the next valid frame.
            continue;
        }
        position += count;
        for (const auto &frame : frames) {
            emit frameReceived(frame);
            if (generation != m_generation) return;
        }
    }
}

QString UpstreamSerialReceiver::serialError() const
{
    QString reason;
    switch (m_port.error()) {
    case QSerialPort::DeviceNotFoundError:
        reason = QString::fromUtf8(u8"找不到上游串口，请刷新串口列表"); break;
    case QSerialPort::PermissionError:
        reason = QString::fromUtf8(u8"上游串口被占用或无访问权限"); break;
    case QSerialPort::ResourceError:
        reason = QString::fromUtf8(u8"上游串口已断开或驱动不可用"); break;
    case QSerialPort::ReadError:
        reason = QString::fromUtf8(u8"上游串口读取失败"); break;
    default:
        reason = QString::fromUtf8(u8"上游串口操作失败"); break;
    }
    return reason + QString::fromUtf8(u8"；系统详情：") + m_port.errorString();
}

void UpstreamSerialReceiver::handleSerialError(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::NoError || !m_port.isOpen()) return;
    const QString reason = serialError();
    const bool disconnected = error == QSerialPort::ResourceError
            || error == QSerialPort::ReadError || error == QSerialPort::PermissionError
            || error == QSerialPort::DeviceNotFoundError;
    if (disconnected) {
        const QSignalBlocker blocker(&m_port);
        m_port.close();
    }
    clear();
    emit errorOccurred(reason);
    if (disconnected) emit connectionChanged(false, reason);
}
