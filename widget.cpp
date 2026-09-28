#include "widget.h"
#include "ui_widget.h"
#include "src/core/requestmanager.h"
#include "src/core/translationservice.h"
#include "src/qusheng/qushengprotocol.h"
#include "src/transport/serialtransport.h"
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSerialPortInfo>
#include <QTextCursor>
#include <QTextEdit>
#include <QTableWidget>
#include <QTabWidget>
#include <QVBoxLayout>

namespace {
QByteArray unescape(const QString &text)
{
    QByteArray out;
    for(int i=0;i<text.size();++i) {
        if(text.at(i)=='\\' && i+1<text.size()) {
            const QChar next=text.at(++i);
            if(next=='r') out.append('\r'); else if(next=='n') out.append('\n');
            else if(next=='t') out.append('\t'); else out.append(next.toLatin1());
        } else out.append(text.at(i).toLatin1());
    }
    return out;
}
}

Widget::Widget(QWidget *parent):QWidget(parent),ui(new Ui::Widget),
    m_protocol(new QushengProtocol(this)),m_transport(new SerialTransport(this)),
    m_upstreamTransport(new SerialTransport(this)),
    m_requests(new RequestManager(m_protocol,m_transport,this)),
    m_service(new TranslationService(nullptr,m_requests,this))
{
    ui->setupUi(this);
    resize(960,900);
    setWindowTitle(QString::fromUtf8(u8"配置驱动协议转换 - 预览默认不发送"));
    ui->demoLabel->setText(QString::fromUtf8(u8"外部报文 → 统一命令 → 趋盛协议"));
    ui->commandLabel->setText(QString::fromUtf8(u8"手动输入（ASCII 可用 \\r；二进制输入十六进制）"));
    ui->sendButton->setText(QString::fromUtf8(u8"真实发送"));
    ui->cancelButton->setText(QString::fromUtf8(u8"取消本地等待（不停止设备）"));
    ui->commandEdit->hide();

    auto *editorLabel=new QLabel(QString::fromUtf8(u8"协议配置 JSON 草稿（编辑后先验证，再显式启用）"),this);
    auto *tabs=new QTabWidget(this);
    m_configEdit=new QTextEdit(this); m_configEdit->setMinimumHeight(160);
    tabs->addTab(m_configEdit,QString::fromUtf8(u8"完整 JSON"));
    auto *fieldPage=new QWidget(this); auto *fieldLayout=new QVBoxLayout(fieldPage);
    m_fieldTable=new QTableWidget(this); m_fieldTable->setColumnCount(8);
    m_fieldTable->setHorizontalHeaderLabels({"name","type","offset","length","endian","min","max","constant"});
    m_fieldTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    fieldLayout->addWidget(m_fieldTable);
    auto *fieldButtons=new QHBoxLayout;
    auto *fieldAdd=new QPushButton(QString::fromUtf8(u8"添加字段"),this);
    auto *fieldDelete=new QPushButton(QString::fromUtf8(u8"删除选中"),this);
    auto *fieldApply=new QPushButton(QString::fromUtf8(u8"应用字段到草稿"),this);
    fieldButtons->addWidget(fieldAdd); fieldButtons->addWidget(fieldDelete); fieldButtons->addWidget(fieldApply);
    fieldLayout->addLayout(fieldButtons); tabs->addTab(fieldPage,QString::fromUtf8(u8"字段编辑"));
    auto *mapPage=new QWidget(this); auto *mapLayout=new QVBoxLayout(mapPage);
    m_mappingTable=new QTableWidget(this); m_mappingTable->setColumnCount(4);
    m_mappingTable->setHorizontalHeaderLabels({"when JSON","operation","responsePolicy","parameters JSON"});
    m_mappingTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    mapLayout->addWidget(m_mappingTable);
    auto *mapButtons=new QHBoxLayout;
    auto *mapAdd=new QPushButton(QString::fromUtf8(u8"添加映射"),this);
    auto *mapDelete=new QPushButton(QString::fromUtf8(u8"删除选中"),this);
    auto *mapApply=new QPushButton(QString::fromUtf8(u8"应用映射到草稿"),this);
    mapButtons->addWidget(mapAdd); mapButtons->addWidget(mapDelete); mapButtons->addWidget(mapApply);
    mapLayout->addLayout(mapButtons); tabs->addTab(mapPage,QString::fromUtf8(u8"映射编辑"));
    connect(fieldAdd,&QPushButton::clicked,this,[this]() { m_fieldTable->insertRow(m_fieldTable->rowCount()); });
    connect(fieldDelete,&QPushButton::clicked,this,[this]() { if(m_fieldTable->currentRow()>=0) m_fieldTable->removeRow(m_fieldTable->currentRow()); });
    connect(fieldApply,&QPushButton::clicked,this,&Widget::applyFieldTable);
    connect(mapAdd,&QPushButton::clicked,this,[this]() { m_mappingTable->insertRow(m_mappingTable->rowCount()); });
    connect(mapDelete,&QPushButton::clicked,this,[this]() { if(m_mappingTable->currentRow()>=0) m_mappingTable->removeRow(m_mappingTable->currentRow()); });
    connect(mapApply,&QPushButton::clicked,this,&Widget::applyMappingTable);
    auto *configButtons=new QHBoxLayout;
    auto *loadButton=new QPushButton(QString::fromUtf8(u8"导入"),this);
    auto *saveButton=new QPushButton(QString::fromUtf8(u8"导出"),this);
    auto *validateButton=new QPushButton(QString::fromUtf8(u8"校验"),this);
    auto *refreshEditorButton=new QPushButton(QString::fromUtf8(u8"从 JSON 刷新表格"),this);
    m_enableButton=new QPushButton(QString::fromUtf8(u8"启用配置"),this);
    configButtons->addWidget(loadButton); configButtons->addWidget(saveButton);
    configButtons->addWidget(validateButton); configButtons->addWidget(refreshEditorButton); configButtons->addWidget(m_enableButton);
    ui->verticalLayout->insertWidget(1,editorLabel);
    ui->verticalLayout->insertWidget(2,tabs);
    ui->verticalLayout->insertLayout(3,configButtons);

    auto *upstreamRow=new QHBoxLayout;
    upstreamRow->addWidget(new QLabel(QString::fromUtf8(u8"上游串口"),this));
    m_upstreamPort=new QComboBox(this); m_upstreamBaud=new QComboBox(this);
    m_upstreamConnect=new QPushButton(QString::fromUtf8(u8"连接上游"),this);
    m_bridge=new QCheckBox(QString::fromUtf8(u8"启用桥接（真实下发）"),this);
    upstreamRow->addWidget(m_upstreamPort); upstreamRow->addWidget(m_upstreamBaud);
    upstreamRow->addWidget(m_upstreamConnect); upstreamRow->addWidget(m_bridge);
    ui->verticalLayout->insertLayout(5,upstreamRow);
    auto *inputRow=new QHBoxLayout;
    m_inputEncoding=new QComboBox(this); m_inputEncoding->addItems({"ASCII","HEX"});
    m_input=new QLineEdit(this); m_input->setText(QStringLiteral("/1A2000R\\r"));
    m_previewButton=new QPushButton(QString::fromUtf8(u8"预览（不发送）"),this);
    inputRow->addWidget(m_inputEncoding); inputRow->addWidget(m_input); inputRow->addWidget(m_previewButton);
    int commandRow=0;
    for(int i=0;i<ui->verticalLayout->count();++i)
        if(ui->verticalLayout->itemAt(i)->layout()==ui->commandLayout) { commandRow=i; break; }
    ui->verticalLayout->insertLayout(commandRow+1,inputRow);
    auto *simulationRow=new QHBoxLayout;
    simulationRow->addWidget(new QLabel(QString::fromUtf8(u8"模拟设备回告 HEX"),this));
    m_simulatedReply=new QLineEdit(this);
    m_simulatedReply->setPlaceholderText(QString::fromUtf8(u8"输入完整趋盛帧，仅用于离线解码"));
    auto *simulateButton=new QPushButton(QString::fromUtf8(u8"预览回告"),this);
    simulationRow->addWidget(m_simulatedReply); simulationRow->addWidget(simulateButton);
    ui->verticalLayout->insertLayout(commandRow+2,simulationRow);
    m_previewEdit=new QTextEdit(this); m_previewEdit->setReadOnly(true); m_previewEdit->setMinimumHeight(120);
    ui->verticalLayout->insertWidget(ui->verticalLayout->indexOf(ui->sentLabel),m_previewEdit);

    const QList<int> rates={9600,19200,38400,57600,115200};
    for(int rate:rates) { ui->baudCombo->addItem(QString::number(rate),rate); m_upstreamBaud->addItem(QString::number(rate),rate); }
    ui->baudCombo->setCurrentIndex(ui->baudCombo->findData(115200));
    m_upstreamBaud->setCurrentIndex(m_upstreamBaud->findData(38400));
    connect(ui->refreshButton,&QPushButton::clicked,this,&Widget::refreshPorts);
    connect(ui->connectButton,&QPushButton::clicked,this,[this]() {
        if(m_transport->isConnected()) m_transport->close();
        else if(!ui->portCombo->currentData().toString().isEmpty()) m_transport->open(ui->portCombo->currentData().toString(),ui->baudCombo->currentData().toInt());
    });
    connect(m_upstreamConnect,&QPushButton::clicked,this,[this]() {
        if(m_upstreamTransport->isConnected()) { m_upstreamTransport->close(); return; }
        const QString port=m_upstreamPort->currentData().toString();
        if(port.isEmpty() || (port==ui->portCombo->currentData().toString() && m_transport->isConnected())) {
            appendLog(QString::fromUtf8(u8"上游和下游不能占用同一串口")); return;
        }
        m_upstreamTransport->open(port,m_upstreamBaud->currentData().toInt());
    });
    connect(loadButton,&QPushButton::clicked,this,[this]() {
        const QString path=QFileDialog::getOpenFileName(this,QString::fromUtf8(u8"导入协议配置"),QString(),"JSON (*.json)");
        if(path.isEmpty()) return;
        QFile file(path); if(!file.open(QIODevice::ReadOnly)) { appendLog(file.errorString()); return; }
        m_configEdit->setPlainText(QString::fromUtf8(file.readAll()));
        refreshEditors();
        appendLog(QString::fromUtf8(u8"已导入草稿；当前启用配置未改变"));
    });
    connect(saveButton,&QPushButton::clicked,this,[this]() {
        ProtocolConfig config; TranslationError error;
        if(!ProtocolConfig::fromJson(m_configEdit->toPlainText().toUtf8(),&config,&error)) { appendLog(error.code+": "+error.message); return; }
        const QString path=QFileDialog::getSaveFileName(this,QString::fromUtf8(u8"导出协议配置"),QString(),"JSON (*.json)");
        if(!path.isEmpty() && !config.save(path,&error)) appendLog(error.code+": "+error.message);
    });
    connect(validateButton,&QPushButton::clicked,this,[this]() {
        ProtocolConfig config; TranslationError error;
        appendLog(ProtocolConfig::fromJson(m_configEdit->toPlainText().toUtf8(),&config,&error)
                  ? QString::fromUtf8(u8"配置有效；版本 SHA-256：")+config.version.left(12):error.code+": "+error.message);
    });
    connect(refreshEditorButton,&QPushButton::clicked,this,&Widget::refreshEditors);
    connect(m_enableButton,&QPushButton::clicked,this,&Widget::enableConfig);
    connect(m_previewButton,&QPushButton::clicked,this,&Widget::preview);
    connect(simulateButton,&QPushButton::clicked,this,&Widget::simulateReply);
    connect(ui->sendButton,&QPushButton::clicked,this,[this]() { submitInput(inputBytes(),false); });
    connect(ui->cancelButton,&QPushButton::clicked,this,[this]() { if(m_lastRequestId) m_service->cancelRequest(m_lastRequestId); });
    connect(m_service,&TranslationService::outgoingFrame,this,[this](quint64,const QByteArray &frame) { ui->sentEdit->setPlainText(frame.toHex(' ').toUpper()); });
    connect(m_service,&TranslationService::rawDataReceived,this,[this](const QByteArray &raw) { ui->receivedEdit->append(raw.toHex(' ').toUpper()); });
    connect(m_service,&TranslationService::completed,this,&Widget::onCompleted);
    connect(m_service,&TranslationService::requestStateChanged,this,[this](quint64 id,RequestState state,const QString &detail) {
        m_requestActive=state==RequestState::Queued || state==RequestState::Sending || state==RequestState::Written
                || state==RequestState::WaitingReply || state==RequestState::Acknowledged || state==RequestState::InMotion;
        ui->statusLabel->setText(QString::fromUtf8(u8"请求 %1：%2").arg(id).arg(stateText(state)));
        appendLog(QString::fromUtf8(u8"请求 %1 %2：%3").arg(id).arg(stateText(state),detail)); updateSendEnabled();
    });
    connect(m_service,&TranslationService::errorOccurred,this,[this](quint64,const TranslationError &e) { appendLog(e.code+": "+e.message); });
    connect(m_service,&TranslationService::unsolicitedMessage,this,[this](const ProtocolMessage &m) {
        appendLog(QString::fromUtf8(u8"主动或迟到帧 0x%1；不据此完成请求").arg(m.messageType,2,16,QLatin1Char('0')));
    });
    connect(m_requests,&RequestManager::deviceBusyChanged,this,[this](bool busy) {
        appendLog(busy?QString::fromUtf8(u8"设备状态：忙或未知"):QString::fromUtf8(u8"设备状态：空闲")); updateSendEnabled();
    });
    connect(m_transport,&SerialTransport::connectionChanged,this,&Widget::updateConnectionUi);
    connect(m_upstreamTransport,&SerialTransport::connectionChanged,this,[this](bool connected,const QString &reason) {
        ++m_upstreamGeneration; m_upstreamReplies.clear(); m_upstreamWriting=false;
        m_upstreamPort->setEnabled(!connected); m_upstreamBaud->setEnabled(!connected);
        m_upstreamConnect->setText(connected?QString::fromUtf8(u8"断开上游"):QString::fromUtf8(u8"连接上游"));
        appendLog(reason); updateSendEnabled();
    });
    connect(m_upstreamTransport,&SerialTransport::sendFinished,this,[this](quint64,bool success,const QString &reason) {
        m_upstreamWriting=false;
        if(!success) appendLog(QString::fromUtf8(u8"上游回告写出失败：")+reason);
        if(!m_upstreamReplies.isEmpty()) queueUpstreamReply(QByteArray());
    });
    connect(m_upstreamTransport,&SerialTransport::bytesReceived,this,[this](const QByteArray &chunk) {
        if(!m_bridge->isChecked() || !m_upstreamEngine) return;
        QList<TranslationError> errors;
        const QList<QByteArray> frames=m_upstreamEngine->feed(chunk,&errors);
        for(const TranslationError &e:errors) {
            appendLog(QString::fromUtf8(u8"上游拒绝：")+e.code+" "+e.message);
            UnifiedResult rejected; rejected.error=e;
            Origin origin; origin.config=m_activeConfig; origin.upstream=true; origin.upstreamGeneration=m_upstreamGeneration;
            queueUpstreamReply(buildResponse(rejected,origin));
        }
        for(const QByteArray &frame:frames) submitInput(frame,true);
    });
    connect(m_bridge,&QCheckBox::toggled,this,[this](bool on) {
        if(on) { TranslationError error;
            if(!m_configEnabled || !m_activeConfig.liveReady(&error) || !m_transport->isConnected() || !m_upstreamTransport->isConnected()) {
                m_bridge->setChecked(false); appendLog(error.code.isEmpty()?QString::fromUtf8(u8"两路串口需连接后才能桥接"):error.message); return;
            }
        }
        appendLog(on?QString::fromUtf8(u8"桥接已显式启用"):QString::fromUtf8(u8"桥接已关闭"));
    });
    refreshPorts(); updateConnectionUi(false,QString());
    QFile initial(QDir(QCoreApplication::applicationDirPath()).filePath("../../examples/dt-preview.json"));
    if(initial.open(QIODevice::ReadOnly)) m_configEdit->setPlainText(QString::fromUtf8(initial.readAll()));
    refreshEditors();
    appendLog(QString::fromUtf8(u8"默认仅预览；示例配置参数未确认，真实发送会被拒绝。"));
}

Widget::~Widget() { delete ui; }
void Widget::refreshEditors()
{
    const QJsonDocument doc=QJsonDocument::fromJson(m_configEdit->toPlainText().toUtf8());
    if(!doc.isObject()) { appendLog(QString::fromUtf8(u8"草稿 JSON 无效，不能刷新编辑表")); return; }
    const QJsonObject root=doc.object(),external=root.value("external").toObject();
    const QJsonArray fields=external.value("fields").toArray();
    m_fieldTable->setRowCount(fields.size());
    const QStringList keys={"name","type","offset","length","endian","min","max","constant"};
    for(int row=0;row<fields.size();++row) {
        const QJsonObject field=fields.at(row).toObject();
        for(int col=0;col<keys.size();++col)
            m_fieldTable->setItem(row,col,new QTableWidgetItem(field.contains(keys.at(col))?field.value(keys.at(col)).toVariant().toString():QString()));
    }
    const QJsonArray mappings=root.value("mappings").toArray();
    m_mappingTable->setRowCount(mappings.size());
    for(int row=0;row<mappings.size();++row) {
        const QJsonObject map=mappings.at(row).toObject();
        m_mappingTable->setItem(row,0,new QTableWidgetItem(QString::fromUtf8(QJsonDocument(map.value("when").toObject()).toJson(QJsonDocument::Compact))));
        m_mappingTable->setItem(row,1,new QTableWidgetItem(map.value("operation").toString()));
        m_mappingTable->setItem(row,2,new QTableWidgetItem(map.value("responsePolicy").toString()));
        m_mappingTable->setItem(row,3,new QTableWidgetItem(QString::fromUtf8(QJsonDocument(map.value("parameters").toObject()).toJson(QJsonDocument::Compact))));
    }
}
void Widget::applyFieldTable()
{
    const QJsonDocument doc=QJsonDocument::fromJson(m_configEdit->toPlainText().toUtf8());
    if(!doc.isObject()) { appendLog(QString::fromUtf8(u8"草稿 JSON 无效")); return; }
    QJsonObject root=doc.object(),external=root.value("external").toObject(); QJsonArray fields;
    const QStringList keys={"name","type","offset","length","endian","min","max","constant"};
    for(int row=0;row<m_fieldTable->rowCount();++row) {
        QJsonObject field;
        for(int col=0;col<keys.size();++col) {
            const auto *item=m_fieldTable->item(row,col); const QString value=item?item->text().trimmed():QString();
            if(value.isEmpty()) continue;
            if(col==2 || col==3 || col==5 || col==6 || (col==7 && value.at(0).isDigit())) {
                bool ok=false; const qlonglong number=value.toLongLong(&ok);
                if(!ok) { appendLog(QString::fromUtf8(u8"字段数值无效：")+value); return; }
                field.insert(keys.at(col),double(number));
            } else field.insert(keys.at(col),value);
        }
        fields.append(field);
    }
    external.insert("fields",fields); root.insert("external",external);
    ProtocolConfig updated; TranslationError error;
    if(!ProtocolConfig::fromJson(QJsonDocument(root).toJson(),&updated,&error)) { appendLog(error.code+": "+error.message); return; }
    m_configEdit->setPlainText(QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented)));
    appendLog(QString::fromUtf8(u8"字段已应用到草稿；启用版本未改变"));
}
void Widget::applyMappingTable()
{
    const QJsonDocument doc=QJsonDocument::fromJson(m_configEdit->toPlainText().toUtf8());
    if(!doc.isObject()) { appendLog(QString::fromUtf8(u8"草稿 JSON 无效")); return; }
    QJsonObject root=doc.object(); QJsonArray mappings;
    for(int row=0;row<m_mappingTable->rowCount();++row) {
        auto cell=[this,row](int column) { const auto *item=m_mappingTable->item(row,column); return item?item->text().trimmed():QString(); };
        const QJsonDocument when=QJsonDocument::fromJson(cell(0).toUtf8());
        const QJsonDocument params=QJsonDocument::fromJson(cell(3).toUtf8());
        if(!when.isObject() || !params.isObject()) { appendLog(QString::fromUtf8(u8"映射条件和参数必须是 JSON 对象")); return; }
        QJsonObject map; map.insert("when",when.object()); map.insert("operation",cell(1));
        map.insert("responsePolicy",cell(2)); map.insert("parameters",params.object()); mappings.append(map);
    }
    root.insert("mappings",mappings); ProtocolConfig updated; TranslationError error;
    if(!ProtocolConfig::fromJson(QJsonDocument(root).toJson(),&updated,&error)) { appendLog(error.code+": "+error.message); return; }
    m_configEdit->setPlainText(QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented)));
    appendLog(QString::fromUtf8(u8"映射已应用到草稿；启用版本未改变"));
}
void Widget::appendLog(const QString &text)
{
    ui->logEdit->append(QDateTime::currentDateTime().toString("HH:mm:ss.zzz ")+text);
    while(ui->logEdit->document()->blockCount()>200) {
        QTextCursor c(ui->logEdit->document()); c.movePosition(QTextCursor::Start);
        c.select(QTextCursor::BlockUnderCursor); c.removeSelectedText(); c.deleteChar();
    }
}
void Widget::refreshPorts()
{
    const QString down=ui->portCombo->currentData().toString(),up=m_upstreamPort->currentData().toString();
    ui->portCombo->clear(); m_upstreamPort->clear();
    for(const QSerialPortInfo &port:QSerialPortInfo::availablePorts()) {
        const QString label=port.portName()+(port.description().isEmpty()?QString():" - "+port.description());
        ui->portCombo->addItem(label,port.portName()); m_upstreamPort->addItem(label,port.portName());
    }
    if(ui->portCombo->count()==0) { ui->portCombo->addItem(QString::fromUtf8(u8"未发现串口"),QString()); m_upstreamPort->addItem(QString::fromUtf8(u8"未发现串口"),QString()); }
    if(ui->portCombo->findData(down)>=0) ui->portCombo->setCurrentIndex(ui->portCombo->findData(down));
    if(m_upstreamPort->findData(up)>=0) m_upstreamPort->setCurrentIndex(m_upstreamPort->findData(up));
    updateSendEnabled();
}
void Widget::updateConnectionUi(bool connected,const QString &reason)
{
    if(connected) m_protocol->resetSession();
    ui->connectButton->setText(connected?QString::fromUtf8(u8"断开下游"):QString::fromUtf8(u8"连接下游"));
    ui->portCombo->setEnabled(!connected && !ui->portCombo->currentData().toString().isEmpty());
    ui->baudCombo->setEnabled(!connected); ui->refreshButton->setEnabled(!connected);
    ui->connectButton->setEnabled(connected || !ui->portCombo->currentData().toString().isEmpty());
    if(!connected) m_bridge->setChecked(false);
    if(!reason.isEmpty()) appendLog(reason);
    updateSendEnabled();
}
void Widget::updateSendEnabled()
{
    TranslationError error;
    const bool ready=m_configEnabled && m_activeConfig.liveReady(&error);
    ui->sendButton->setEnabled(ready && m_transport->isConnected() && !m_requestActive && !m_requests->deviceBusy());
    m_enableButton->setEnabled(!m_requestActive);
}
QByteArray Widget::inputBytes() const
{
    if(m_inputEncoding->currentText()!="HEX") return unescape(m_input->text());
    QString value=m_input->text(); value.remove(QRegularExpression(QStringLiteral("\\s+")));
    if(value.size()%2 || QRegularExpression(QStringLiteral("[^0-9A-Fa-f]")).match(value).hasMatch()) return QByteArray();
    return QByteArray::fromHex(value.toLatin1());
}
void Widget::preview()
{
    ProtocolConfig config; TranslationError error;
    if(!ProtocolConfig::fromJson(m_configEdit->toPlainText().toUtf8(),&config,&error)) { m_previewEdit->setPlainText(error.code+": "+error.message); return; }
    ConversionEngine engine(config); ConversionPreview result;
    if(!engine.convert(inputBytes(),false,&result,&error)) { m_previewEdit->setPlainText(error.code+": "+error.message); return; }
    QString info=QString::fromUtf8(u8"配置版本：")+config.version.left(12)+"\n";
    info+=QString::fromUtf8(u8"输入 HEX：")+result.input.toHex(' ').toUpper()+"\n";
    for(auto it=result.fields.begin();it!=result.fields.end();++it) info+=QString::fromUtf8(u8"字段 ")+it.key()+" = "+it.value().toString()+"\n";
    info+=QString::fromUtf8(u8"统一命令：")+result.command.operation+" / "+result.command.targetDevice+" / "+result.command.responsePolicy+"\n";
    for(auto it=result.command.parameters.begin();it!=result.command.parameters.end();++it)
        info+=it.key()+" = "+it.value().toString()+" "+result.command.parameterUnits.value(it.key())+"\n";
    info+=QString::fromUtf8(u8"趋盛目标帧 HEX：")+result.targetFrame.toHex(' ').toUpper()+"\n";
    info+=QString::fromUtf8(u8"回告：待收到真实或模拟设备帧后解码；当前预览不发送。\n");
    if(!config.liveReady(&error)) info+=QString::fromUtf8(u8"实发锁定：")+error.message;
    m_previewEdit->setPlainText(info);
}
void Widget::simulateReply()
{
    ProtocolConfig config; TranslationError error;
    if(!ProtocolConfig::fromJson(m_configEdit->toPlainText().toUtf8(),&config,&error)) { m_previewEdit->append(error.code+": "+error.message); return; }
    ConversionEngine engine(config); ConversionPreview preview;
    if(!engine.convert(inputBytes(),false,&preview,&error)) { m_previewEdit->append(error.code+": "+error.message); return; }
    const QJsonObject dev=config.root.value("device").toObject();
    QushengProtocol codec; codec.setAddresses(quint8(dev.value("destination").toInt()),quint8(dev.value("source").toInt()));
    QByteArray request; codec.encodeCommand(preview.command,&request,&error);
    const QByteArray raw=QByteArray::fromHex(m_simulatedReply->text().toLatin1());
    QList<TranslationError> errors; const auto messages=codec.feedReceivedData(raw,&errors);
    if(!errors.isEmpty()) { m_previewEdit->append(errors.first().code+": "+errors.first().message); return; }
    if(messages.size()!=1 || !codec.matchesReply(preview.command,messages.first())) {
        m_previewEdit->append(QString::fromUtf8(u8"回告与预览请求的地址、命令或序号不匹配；0x17 主动上报不能单独完成请求。")); return;
    }
    UnifiedResult result;
    if(!codec.decodeReply(preview.command,messages.first(),&result,&error)) { m_previewEdit->append(error.code+": "+error.message); return; }
    if(preview.command.operation=="ABSOLUTE_MOVE" && result.success) {
        result.data.insert("deviceBusy",true);
        if(preview.command.responsePolicy=="complete") {
            m_previewEdit->append(QString::fromUtf8(u8"0x11 命令已接纳，尚无运动完成证据；小写 a 不生成上游完成回告。")); return;
        }
    }
    Origin origin; origin.config=config; origin.policy=preview.command.responsePolicy;
    m_previewEdit->append(QString::fromUtf8(u8"模拟回告字段：")+QString::fromUtf8(QJsonDocument::fromVariant(result.data).toJson(QJsonDocument::Compact)));
    const QByteArray response=buildResponse(result,origin);
    m_previewEdit->append(response.isEmpty()?QString::fromUtf8(u8"上游错误码尚未确认，不能生成回告"):
                          QString::fromUtf8(u8"转换后的上游回告 HEX：")+response.toHex(' ').toUpper());
}
void Widget::enableConfig()
{
    if(m_requestActive) { appendLog(QString::fromUtf8(u8"在途请求完成前不能切换目标编解码器")); return; }
    ProtocolConfig config; TranslationError error;
    if(!ProtocolConfig::fromJson(m_configEdit->toPlainText().toUtf8(),&config,&error)) { appendLog(error.code+": "+error.message); return; }
    m_activeConfig=config; m_configEnabled=true;
    m_upstreamEngine.reset(new ConversionEngine(config));
    const QJsonObject dev=config.root.value("device").toObject();
    m_protocol->setAddresses(quint8(dev.value("destination").toInt()),quint8(dev.value("source").toInt()));
    appendLog(QString::fromUtf8(u8"已显式启用配置版本 ")+config.version.left(12)+QString::fromUtf8(u8"；草稿后续编辑不影响已接收请求"));
    updateSendEnabled();
}
void Widget::submitInput(const QByteArray &bytes,bool upstream)
{
    TranslationError error;
    if(!m_configEnabled || !m_activeConfig.liveReady(&error)) { appendLog(error.code.isEmpty()?QString::fromUtf8(u8"请先启用有效配置"):error.message); return; }
    ConversionEngine engine(m_activeConfig); ConversionPreview preview;
    if(!engine.convert(bytes,true,&preview,&error) || !m_transport->isConnected()) {
        if(error.code.isEmpty()) error={"DISCONNECTED",QString::fromUtf8(u8"下游串口未连接")};
        appendLog(error.code+": "+error.message);
        if(upstream) {
            UnifiedResult rejected; rejected.error=error;
            Origin origin; origin.config=m_activeConfig; origin.upstream=true; origin.upstreamGeneration=m_upstreamGeneration;
            queueUpstreamReply(buildResponse(rejected,origin));
        }
        return;
    }
    const quint64 id=m_service->submitUnifiedCommand(preview.command);
    Origin origin; origin.config=m_activeConfig; origin.upstream=upstream;
    origin.upstreamGeneration=m_upstreamGeneration; origin.policy=preview.command.responsePolicy;
    m_lastRequestId=id; m_origins.insert(id,origin);
}
QByteArray Widget::buildResponse(const UnifiedResult &result,const Origin &origin) const
{
    const QString format=origin.config.root.value("external").toObject().value("format").toString();
    if(format=="dt") {
        int errorCode=0;
        if(!result.success) {
            const QJsonObject mappings=origin.config.root.value("dtErrorCodes").toObject();
            const int fallback=origin.config.root.value("defaultDtErrorCode").toInt();
            if(fallback<1 || fallback>15) return QByteArray();
            errorCode=mappings.value(result.error.code).toInt(fallback);
        }
        const bool busy=result.data.value("deviceBusy").toBool();
        return QByteArray("/0")+char(0x40|(busy?0x20:0)|errorCode)+QByteArray("\x03\r\n",3);
    }
    const QJsonObject responses=origin.config.root.value("responses").toObject();
    QString kind=result.success?(origin.policy=="ack"?"ack":"complete"):"reject";
    QString templ=responses.value(kind).toString();
    if(format=="binary") return QByteArray::fromHex(templ.toLatin1());
    templ.replace("{code}",result.error.code);
    return unescape(templ);
}
void Widget::onCompleted(const UnifiedResult &result)
{
    if(!m_origins.contains(result.requestId)) return;
    const Origin origin=m_origins.take(result.requestId);
    const QByteArray reply=buildResponse(result,origin);
    ui->replyEdit->setPlainText(reply.toHex(' ').toUpper());
    if(origin.upstream && origin.upstreamGeneration==m_upstreamGeneration && m_upstreamTransport->isConnected())
        queueUpstreamReply(reply);
}
void Widget::queueUpstreamReply(const QByteArray &bytes)
{
    if(!bytes.isEmpty()) m_upstreamReplies.enqueue(bytes);
    if(m_upstreamWriting || m_upstreamReplies.isEmpty() || !m_upstreamTransport->isConnected()) return;
    m_upstreamWriting=true;
    m_upstreamTransport->sendBytes(m_nextUpstreamWriteId++,m_upstreamReplies.dequeue());
}
QString Widget::stateText(RequestState state)
{
    switch(state) {
    case RequestState::Queued:return QString::fromUtf8(u8"排队");
    case RequestState::Sending:return QString::fromUtf8(u8"写出中");
    case RequestState::Written:return QString::fromUtf8(u8"已写出");
    case RequestState::WaitingReply:return QString::fromUtf8(u8"等待命令应答");
    case RequestState::Acknowledged:return QString::fromUtf8(u8"命令已接纳");
    case RequestState::InMotion:return QString::fromUtf8(u8"运动中");
    case RequestState::Succeeded:return QString::fromUtf8(u8"完成");
    case RequestState::Failed:return QString::fromUtf8(u8"失败");
    case RequestState::TimedOut:return QString::fromUtf8(u8"超时");
    case RequestState::Cancelled:return QString::fromUtf8(u8"本地等待取消");
    }
    return QString::fromUtf8(u8"未知");
}
