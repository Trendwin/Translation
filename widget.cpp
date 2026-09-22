#include "widget.h"
#include "ui_widget.h"
#include "src/core/requestmanager.h"
#include "src/core/translationservice.h"
#include "src/demo/democlientprotocol.h"
#include "src/demo/demointernalprotocol.h"
#include "src/demo/mocktransport.h"
#include <QDateTime>
#include <QPushButton>
#include <QTextCursor>
#include <QTextEdit>

Widget::Widget(QWidget *parent) : QWidget(parent), ui(new Ui::Widget),
    m_client(new DemoClientProtocol(this)), m_protocol(new DemoInternalProtocol(this)),
    m_transport(new MockTransport(this)), m_requests(new RequestManager(m_protocol, m_transport, this)),
    m_service(new TranslationService(m_client, m_requests, this))
{
    ui->setupUi(this);
    setWindowTitle(QStringLiteral("指令翻译框架（演示协议）"));
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
        ui->replyEdit->setPlainText(reply);
    });
    connect(m_service, &TranslationService::requestStateChanged, this,
            [this](quint64 id, RequestState state, const QString &detail) {
        ui->statusLabel->setText(QStringLiteral("请求 %1：%2").arg(id).arg(stateText(state)));
        appendLog(QStringLiteral("请求 %1 %2：%3").arg(id).arg(stateText(state), detail));
    });
    connect(m_service, &TranslationService::errorOccurred, this, [this](quint64 id, const TranslationError &e) {
        appendLog(QStringLiteral("错误 请求=%1 [%2] %3").arg(id).arg(e.code, e.message));
    });
    connect(m_service, &TranslationService::unsolicitedMessage, this, [this](const ProtocolMessage &m) {
        appendLog(QStringLiteral("演示主动上报/无关帧 type=0x%1（已分发，不缓存）")
                  .arg(m.messageType, 2, 16, QLatin1Char('0')));
    });
    appendLog(QStringLiteral("当前全部为演示实现。可输入：PING motor1、FAIL motor1、TIMEOUT motor1"));
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
