#include "widget.h"
#include "ui_widget.h"
#include "src/core/requestmanager.h"
#include "src/core/translationservice.h"
#include "src/dt/dtclientprotocol.h"
#include "src/qusheng/qushengprotocol.h"
#include "src/transport/serialtransport.h"
#include <QDateTime>
#include <QComboBox>
#include <QPushButton>
#include <QSerialPortInfo>
#include <QTextCursor>
#include <QTextEdit>

Widget::Widget(QWidget *parent) : QWidget(parent), ui(new Ui::Widget),
    m_client(new DtClientProtocol(this)), m_protocol(new QushengProtocol(this)),
    m_transport(new SerialTransport(this)), m_requests(new RequestManager(m_protocol, m_transport, this)),
    m_service(new TranslationService(m_client, m_requests, this))
{
    ui->setupUi(this);
    //setWindowTitle(QStringLiteral("DT / 趋盛指令翻译"));
    setWindowTitle(QString::fromUtf8(u8"DT / 趋盛指令翻译"));
    connect(ui->connectButton, &QPushButton::clicked, this, [this]() {
        if (m_transport->isConnected()) {
            m_transport->close();
            return;
        }
        const QString portName = ui->portCombo->currentData().toString();
        if (!portName.isEmpty())
            m_transport->open(portName, ui->baudCombo->currentData().toInt());
    });
    connect(ui->refreshButton, &QPushButton::clicked, this, &Widget::refreshPorts);
    connect(ui->sendButton, &QPushButton::clicked, this, [this]() {
        m_lastRequestId = m_service->submitCommand(ui->commandEdit->text());
    });
    connect(ui->cancelButton, &QPushButton::clicked, this, [this]() {
        if (m_lastRequestId) m_service->cancelRequest(m_lastRequestId);
    });
    connect(m_service, &TranslationService::outgoingFrame, this, [this](quint64, const QByteArray &data) {
        ui->sentEdit->setPlainText(data.toHex(' ').toUpper());
    });
    connect(m_service, &TranslationService::rawDataReceived, this, [this](const QByteArray &data) {
        ui->receivedEdit->append(data.toHex(' ').toUpper());
    });
    connect(m_service, &TranslationService::customerReply, this, [this](quint64, const QString &reply) {
        Q_UNUSED(reply)
    });
    connect(m_service, &TranslationService::customerReplyBytes, this, [this](quint64, const QByteArray &reply) {
        ui->replyEdit->setPlainText(reply.toHex(' ').toUpper());
    });
    connect(m_service, &TranslationService::requestStateChanged, this,
            [this](quint64 id, RequestState state, const QString &detail) {
        m_requestActive = state == RequestState::Queued || state == RequestState::Sending
                || state == RequestState::WaitingReply;
        updateSendEnabled();
        ui->statusLabel->setText(QStringLiteral("请求 %1：%2").arg(id).arg(stateText(state)));
        appendLog(QStringLiteral("请求 %1 %2：%3").arg(id).arg(stateText(state), detail));
    });
    connect(m_service, &TranslationService::errorOccurred, this, [this](quint64 id, const TranslationError &e) {
        appendLog(QStringLiteral("错误 请求=%1 [%2] %3").arg(id).arg(e.code, e.message));
    });
    connect(m_service, &TranslationService::unsolicitedMessage, this, [this](const ProtocolMessage &m) {
        appendLog(QStringLiteral("主动上报/无关帧 type=0x%1（已分发，不缓存）")
                  .arg(m.messageType, 2, 16, QLatin1Char('0')));
    });
    connect(m_transport, &SerialTransport::connectionChanged,
            this, &Widget::updateConnectionUi);
    const QList<int> baudRates = {9600, 19200, 38400, 57600, 115200};
    for (int baudRate : baudRates)
        ui->baudCombo->addItem(QString::number(baudRate), baudRate);
    ui->baudCombo->setCurrentIndex(ui->baudCombo->findData(115200));
    refreshPorts();
    updateConnectionUi(false, QString());
    appendLog(QStringLiteral("输入示例：/1A2000。默认 dst=02、src=01、DevID=11、速度=1000。"));
}

Widget::~Widget() { delete ui; }

void Widget::appendLog(const QString &text)
{
    ui->logEdit->append(QDateTime::currentDateTime().toString("HH:mm:ss.zzz ") + text);
    while (ui->logEdit->document()->blockCount() > 200) {
        QTextCursor cursor(ui->logEdit->document()); cursor.movePosition(QTextCursor::Start);
        cursor.select(QTextCursor::BlockUnderCursor); cursor.removeSelectedText(); cursor.deleteChar();
    }
}

void Widget::refreshPorts()
{
    // 显示友好描述，但 itemData 始终只保存可交给 QSerialPort 的真实端口名。
    const QString previousPort = ui->portCombo->currentData().toString();
    ui->portCombo->clear();
    const QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &port : ports) {
        QString displayName = port.portName();
        if (!port.description().isEmpty())
            displayName += QStringLiteral(" — ") + port.description();
        ui->portCombo->addItem(displayName, port.portName());
    }

    if (ui->portCombo->count() == 0) {
        ui->portCombo->addItem(QStringLiteral("未发现串口"), QString());
        ui->portCombo->setEnabled(false);
    } else {
        const int previousIndex = ui->portCombo->findData(previousPort);
        ui->portCombo->setCurrentIndex(previousIndex >= 0 ? previousIndex : 0);
        ui->portCombo->setEnabled(true);
    }
    updateSendEnabled();
    ui->connectButton->setEnabled(!ui->portCombo->currentData().toString().isEmpty());
}

void Widget::updateConnectionUi(bool connected, const QString &reason)
{
    ui->connectButton->setText(connected ? QStringLiteral("断开") : QStringLiteral("连接"));
    ui->portCombo->setEnabled(!connected && !ui->portCombo->currentData().toString().isEmpty());
    ui->baudCombo->setEnabled(!connected);
    ui->refreshButton->setEnabled(!connected);
    ui->connectButton->setEnabled(connected || !ui->portCombo->currentData().toString().isEmpty());
    updateSendEnabled();

    if (!reason.isEmpty()) {
        ui->statusLabel->setText(connected
                ? QStringLiteral("当前状态：已连接")
                : QStringLiteral("当前状态：未连接（%1）").arg(reason));
        appendLog(reason);
    } else if (!connected) {
        ui->statusLabel->setText(QStringLiteral("当前状态：未连接"));
    }
}

void Widget::updateSendEnabled()
{
    ui->sendButton->setEnabled(m_transport->isConnected() && !m_requestActive);
}

QString Widget::stateText(RequestState state)
{
    switch (state) {
    case RequestState::Queued: return QStringLiteral("排队");
    case RequestState::Sending: return QStringLiteral("发送中");
    case RequestState::WaitingReply: return QStringLiteral("等待回告");
    case RequestState::Succeeded: return QStringLiteral("完成");
    case RequestState::Failed: return QStringLiteral("失败");
    case RequestState::TimedOut: return QStringLiteral("超时");
    case RequestState::Cancelled: return QStringLiteral("已取消");
    }
    return QStringLiteral("未知");
}
