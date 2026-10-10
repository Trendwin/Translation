#include "widget.h"

#include "src/core/requestmanager.h"
#include "src/core/translationservice.h"
#include "src/qusheng/qushengprotocol.h"
#include "src/transport/serialtransport.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSerialPortInfo>
#include <QSpinBox>
#include <QSignalBlocker>
#include <QScrollArea>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextCursor>
#include <QTextEdit>
#include <QVBoxLayout>

namespace {
QComboBox *combo(QWidget *parent, const QList<QPair<QString, QString>> &values)
{
    auto *box = new QComboBox(parent);
    for (const auto &value : values) box->addItem(value.first, value.second);
    return box;
}

QString visibleBytes(const QByteArray &bytes)
{
    QString result;
    for (char byte : bytes) {
        if (byte == '\r') result += QStringLiteral("\\r");
        else if (byte == '\n') result += QStringLiteral("\\n");
        else if (byte == '\t') result += QStringLiteral("\\t");
        else if (quint8(byte) < 0x20 || quint8(byte) >= 0x7f)
            result += QStringLiteral("\\x%1").arg(quint8(byte), 2, 16, QLatin1Char('0')).toUpper();
        else result += QChar::fromLatin1(byte);
    }
    return result;
}

QScrollArea *scrollPage(QWidget *page)
{
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(page);
    return scroll;
}

QString displayName(const QString &key)
{
    const QMap<QString, QString> names = {{"positionUm", QString::fromUtf8(u8"目标位置（微米）")},
        {"speedUmPerS", QString::fromUtf8(u8"速度（微米/秒）")}, {"absoluteAction", QString::fromUtf8(u8"运动动作值")},
        {"basicStatus", QString::fromUtf8(u8"设备基本状态字（HEX）")}, {"motorState", QString::fromUtf8(u8"电机状态码")},
        {"deviceBusy", QString::fromUtf8(u8"设备忙闲状态")}};
    return names.value(key, key);
}

QString itemText(const QTableWidget *table, int row, int column)
{
    const QTableWidgetItem *item = table->item(row, column);
    return item ? item->text().trimmed() : QString();
}

QString comboData(const QTableWidget *table, int row, int column)
{
    const auto *box = qobject_cast<QComboBox *>(table->cellWidget(row, column));
    return box ? box->currentData().toString() : QString();
}

void selectData(QComboBox *box, const QString &data)
{
    const int index = box->findData(data);
    if (index >= 0) box->setCurrentIndex(index);
}
}

Widget::Widget(QWidget *parent)
    : QWidget(parent),
      m_protocol(new QushengProtocol(this)),
      m_transport(new SerialTransport(this)),
      m_requests(new RequestManager(m_protocol, m_transport, this)),
      m_service(new TranslationService(m_requests, this))
{
    buildUi();
    connectSignals();
    refreshPorts();
    loadInitialExample();
    updateConnectionUi(false, QString::fromUtf8(u8"下游串口未连接"));
}

void Widget::buildUi()
{
    setWindowTitle(QString::fromUtf8(u8"配置驱动协议转换"));
    resize(1180, 900);
    auto *root = new QVBoxLayout(this);
    auto *title = new QLabel(QString::fromUtf8(u8"编辑客户协议格式 → 输入报文 → 解析与预览 → 连接下游 → 真实发送 → 查看回告"), this);
    title->setStyleSheet(QStringLiteral("font-size:18px;font-weight:600"));
    root->addWidget(title);
    auto *hint = new QLabel(QString::fromUtf8(
        u8"第一阶段只保证一条由配置定义的绝对位置运动链路。编辑草稿不会影响当前已启用版本。"), this);
    hint->setWordWrap(true);
    root->addWidget(hint);

    auto *versions = new QHBoxLayout;
    m_draftVersion = new QLabel(QString::fromUtf8(u8"当前草稿：未加载"), this);
    m_activeVersion = new QLabel(QString::fromUtf8(u8"当前已启用版本：无"), this);
    m_validationState = new QLabel(QString::fromUtf8(u8"配置状态：待校验"), this);
    versions->addWidget(m_draftVersion);
    versions->addWidget(m_activeVersion);
    versions->addWidget(m_validationState, 1);
    root->addLayout(versions);

    auto *tabs = new QTabWidget(this);
    root->addWidget(tabs, 1);
    tabs->setMinimumHeight(260);

    auto *formatPage = new QWidget(this);
    auto *formatLayout = new QVBoxLayout(formatPage);
    auto *boundaryForm = new QFormLayout;
    m_encoding = combo(this, {{QStringLiteral("ASCII 文本"), QStringLiteral("ascii")},
                              {QStringLiteral("HEX 字节"), QStringLiteral("hex")}});
    m_encoding->setToolTip(QString::fromUtf8(u8"决定输入框如何转成字节。ASCII 支持 \\r、\\n 和 \\xNN 转义。"));
    m_header = new QLineEdit(this);
    m_header->setToolTip(QString::fromUtf8(u8"完整报文必须以此内容开始；不能为空。HEX 模式填写偶数个十六进制字符。"));
    m_footer = new QLineEdit(this);
    m_footer->setToolTip(QString::fromUtf8(u8"完整报文必须以此内容结束；ASCII 模式可填写 R\\r。"));
    m_byteOrderLabel = new QLabel(QString::fromUtf8(u8"多字节整数"), this);
    m_byteOrder = combo(this, {{QStringLiteral("大端（高字节在前）"), QStringLiteral("big")},
                               {QStringLiteral("小端（低字节在前）"), QStringLiteral("little")}});
    m_byteOrder->setToolTip(QString::fromUtf8(u8"仅 HEX 输入中的多字节整数使用；ASCII 数字不受影响。"));
    boundaryForm->addRow(QString::fromUtf8(u8"编码方式"), m_encoding);
    boundaryForm->addRow(QString::fromUtf8(u8"帧头"), m_header);
    boundaryForm->addRow(QString::fromUtf8(u8"帧尾"), m_footer);
    formatLayout->addLayout(boundaryForm);

    m_fieldTable = new QTableWidget(this);
    m_fieldTable->setObjectName("fieldTable");
    m_fieldTable->setColumnCount(9);
    m_fieldTable->setHorizontalHeaderLabels({
        QString::fromUtf8(u8"顺序"), QString::fromUtf8(u8"字段名称"), QString::fromUtf8(u8"字段作用"),
        QString::fromUtf8(u8"数据类型"), QString::fromUtf8(u8"长度方式"), QString::fromUtf8(u8"长度"),
        QString::fromUtf8(u8"固定值"), QString::fromUtf8(u8"最小值"), QString::fromUtf8(u8"最大值")});
    m_fieldTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_fieldTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_fieldTable->verticalHeader()->hide();
    m_fieldTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_fieldTable->setToolTip(QString::fromUtf8(
        u8"字段按顺序连续读取。读取到帧尾只能用于最后一个字段；固定字段会核对固定值。"));
    const QStringList fieldTips = {
        QString::fromUtf8(u8"报文有效载荷中的读取顺序"),
        QString::fromUtf8(u8"字段唯一名称，供命令映射引用"),
        QString::fromUtf8(u8"命令码用于选择映射；参数供换算；固定字段必须核对固定值"),
        QString::fromUtf8(u8"ASCII 原样读取；整数按编码方式解析"),
        QString::fromUtf8(u8"固定长度按字节数读取；读取到帧尾只能放最后"),
        QString::fromUtf8(u8"固定长度的字节数"),
        QString::fromUtf8(u8"固定字段期望值"),
        QString::fromUtf8(u8"可选的输入下限"),
        QString::fromUtf8(u8"可选的输入上限")};
    for (int column = 0; column < fieldTips.size(); ++column)
        m_fieldTable->horizontalHeaderItem(column)->setToolTip(fieldTips.at(column));
    formatLayout->addWidget(m_fieldTable);
    auto *fieldButtons = new QHBoxLayout;
    auto *addField = new QPushButton(QString::fromUtf8(u8"添加字段"), this);
    auto *removeField = new QPushButton(QString::fromUtf8(u8"删除选中字段"), this);
    auto *fieldUp = new QPushButton(QString::fromUtf8(u8"上移"), this);
    auto *fieldDown = new QPushButton(QString::fromUtf8(u8"下移"), this);
    auto *saveFormat = new QPushButton(QString::fromUtf8(u8"保存到草稿"), this);
    addField->setToolTip(QString::fromUtf8(u8"在字段表末尾增加一行。"));
    removeField->setToolTip(QString::fromUtf8(u8"删除当前选中的字段行。"));
    fieldUp->setToolTip(QString::fromUtf8(u8"把选中字段提前一个读取顺序。"));
    fieldDown->setToolTip(QString::fromUtf8(u8"把选中字段推后一个读取顺序。"));
    saveFormat->setToolTip(QString::fromUtf8(u8"把当前中文表单写入草稿；不会启用，也不会影响在途请求。"));
    for (auto *button : {addField, removeField, fieldUp, fieldDown, saveFormat}) fieldButtons->addWidget(button);
    fieldButtons->addStretch();
    formatLayout->addLayout(fieldButtons);
    tabs->addTab(scrollPage(formatPage), QString::fromUtf8(u8"客户协议格式"));

    auto *mappingPage = new QWidget(this);
    auto *mappingLayout = new QVBoxLayout(mappingPage);
    auto *mappingHint = new QLabel(QString::fromUtf8(
        u8"一行表示一个客户命令码。首版映射目标位置（微米）。线性换算：来源值 × 系数分子 ÷ 系数分母 + 偏移。"), this);
    mappingHint->setWordWrap(true);
    mappingLayout->addWidget(mappingHint);
    m_mappingTable = new QTableWidget(this);
    m_mappingTable->setObjectName("mappingTable");
    m_mappingTable->setColumnCount(11);
    m_mappingTable->setHorizontalHeaderLabels({
        QString::fromUtf8(u8"客户命令码"), QString::fromUtf8(u8"目标操作"), QString::fromUtf8(u8"响应方式"),
        QString::fromUtf8(u8"目标参数"), QString::fromUtf8(u8"关系类型"), QString::fromUtf8(u8"来源字段"),
        QString::fromUtf8(u8"固定值"), QString::fromUtf8(u8"系数分子"), QString::fromUtf8(u8"系数分母"),
        QString::fromUtf8(u8"偏移"), QString::fromUtf8(u8"舍入方式")});
    m_mappingTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_mappingTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_mappingTable->setToolTip(QString::fromUtf8(
        u8"直接引用原值不变；固定值忽略来源字段；线性换算按 来源×分子÷分母+偏移 计算。"));
    const QStringList mappingTips = {
        QString::fromUtf8(u8"与字段表中命令码字段比较的值"),
        QString::fromUtf8(u8"第一阶段仅开放趋盛绝对位置运动"),
        QString::fromUtf8(u8"选择命令应答后给出结果，或等待动作完成"),
        QString::fromUtf8(u8"统一命令参数，单位为微米"),
        QString::fromUtf8(u8"直接引用、固定值或线性换算"),
        QString::fromUtf8(u8"被引用或换算的客户字段"),
        QString::fromUtf8(u8"关系类型为固定值时使用"),
        QString::fromUtf8(u8"线性换算乘数的分子，可以为正数、零或负数"),
        QString::fromUtf8(u8"线性换算除数，不能为零"),
        QString::fromUtf8(u8"除法完成后增加的整数偏移"),
        QString::fromUtf8(u8"除法产生余数时的明确处理方式")};
    for (int column = 0; column < mappingTips.size(); ++column)
        m_mappingTable->horizontalHeaderItem(column)->setToolTip(mappingTips.at(column));
    mappingLayout->addWidget(m_mappingTable);
    auto *mappingButtons = new QHBoxLayout;
    auto *addMapping = new QPushButton(QString::fromUtf8(u8"添加命令映射"), this);
    auto *removeMapping = new QPushButton(QString::fromUtf8(u8"删除选中映射"), this);
    auto *saveMapping = new QPushButton(QString::fromUtf8(u8"保存到草稿"), this);
    addMapping->setToolTip(QString::fromUtf8(u8"增加一条客户命令码映射。"));
    removeMapping->setToolTip(QString::fromUtf8(u8"删除当前选中的命令映射。"));
    saveMapping->setToolTip(saveFormat->toolTip());
    mappingButtons->addWidget(addMapping);
    mappingButtons->addWidget(removeMapping);
    mappingButtons->addWidget(saveMapping);
    mappingButtons->addStretch();
    mappingLayout->addLayout(mappingButtons);
    tabs->addTab(scrollPage(mappingPage), QString::fromUtf8(u8"命令映射"));

    auto *devicePage = new QWidget(this);
    auto *deviceLayout = new QVBoxLayout(devicePage);
    auto *deviceGroup = new QGroupBox(QString::fromUtf8(u8"趋盛设备参数"), this);
    auto *deviceForm = new QFormLayout(deviceGroup);
    m_destination = new QSpinBox(this); m_destination->setRange(1, 255);
    m_source = new QSpinBox(this); m_source->setRange(1, 255);
    m_devId = new QSpinBox(this); m_devId->setRange(0x11, 0x1c); m_devId->setDisplayIntegerBase(16);
    m_action = new QSpinBox(this); m_action->setRange(1, 3); m_action->setSingleStep(2);
    m_speed = new QSpinBox(this); m_speed->setRange(1, 0xfffffe); m_speed->setSuffix(QString::fromUtf8(u8" μm/s"));
    m_minPosition = new QSpinBox(this); m_minPosition->setRange(0, 0xfffffe); m_minPosition->setSuffix(QString::fromUtf8(u8" μm"));
    m_maxPosition = new QSpinBox(this); m_maxPosition->setRange(0, 0xfffffe); m_maxPosition->setSuffix(QString::fromUtf8(u8" μm"));
    m_destination->setToolTip(QString::fromUtf8(u8"控制板地址，范围 1–255，必须与软件源地址不同。"));
    m_source->setToolTip(QString::fromUtf8(u8"软件地址，范围 1–255。"));
    m_devId->setToolTip(QString::fromUtf8(u8"电机编号 0x11–0x1C，按设备资料确认。"));
    m_action->setToolTip(QString::fromUtf8(u8"只允许 1 或 3；具体机械方向需实机确认。"));
    m_speed->setToolTip(QString::fromUtf8(u8"运动速度，单位微米/秒，24 位范围且不可为零。"));
    m_minPosition->setToolTip(QString::fromUtf8(u8"设备行程下限，单位微米。"));
    m_maxPosition->setToolTip(QString::fromUtf8(u8"设备行程上限，单位微米，不允许 0xFFFFFF 的连续运动值。"));
    deviceForm->addRow(QString::fromUtf8(u8"控制板目标地址"), m_destination);
    deviceForm->addRow(QString::fromUtf8(u8"软件源地址"), m_source);
    deviceForm->addRow(QString::fromUtf8(u8"电机 DevID（十六进制）"), m_devId);
    deviceForm->addRow(QString::fromUtf8(u8"绝对运动动作值"), m_action);
    deviceForm->addRow(QString::fromUtf8(u8"速度"), m_speed);
    deviceForm->addRow(QString::fromUtf8(u8"最小位置"), m_minPosition);
    deviceForm->addRow(QString::fromUtf8(u8"最大位置"), m_maxPosition);
    auto *saveDevice = new QPushButton(QString::fromUtf8(u8"保存设备参数到草稿"), this);
    saveDevice->setToolTip(saveFormat->toolTip());
    deviceForm->addRow(saveDevice);
    deviceLayout->addWidget(deviceGroup);

    auto *serialGroup = new QGroupBox(QString::fromUtf8(u8"下游串口连接"), this);
    auto *serialLayout = new QHBoxLayout(serialGroup);
    m_port = new QComboBox(this);
    m_port->setToolTip(QString::fromUtf8(u8"选择连接趋盛控制板的串口。"));
    auto *refreshPort = new QPushButton(QString::fromUtf8(u8"刷新串口"), this);
    refreshPort->setToolTip(QString::fromUtf8(u8"重新枚举本机可用串口。"));
    m_baud = new QComboBox(this);
    m_baud->setToolTip(QString::fromUtf8(u8"波特率必须与控制板一致；首版使用 8 数据位、无校验、1 停止位、无流控。"));
    for (int rate : {9600, 19200, 38400, 57600, 115200})
        m_baud->addItem(QString::number(rate), rate);
    m_baud->setCurrentIndex(m_baud->findData(115200));
    m_connectButton = new QPushButton(QString::fromUtf8(u8"连接下游"), this);
    m_connectButton->setToolTip(QString::fromUtf8(u8"只连接趋盛控制板使用的设备串口。"));
    m_connectionState = new QLabel(QString::fromUtf8(u8"未连接"), this);
    serialLayout->addWidget(new QLabel(QString::fromUtf8(u8"串口"), this));
    serialLayout->addWidget(m_port, 1);
    serialLayout->addWidget(refreshPort);
    serialLayout->addWidget(new QLabel(QString::fromUtf8(u8"波特率"), this));
    serialLayout->addWidget(m_baud);
    serialLayout->addWidget(m_connectButton);
    serialLayout->addWidget(m_connectionState);
    deviceLayout->addWidget(serialGroup);
    m_deviceConfirmed = new QCheckBox(QString::fromUtf8(
        u8"我已对照设备资料或实机核对地址、DevID、速度、行程、动作值及位置换算"), this);
    m_deviceConfirmed->setObjectName("deviceConfirmed");
    m_deviceConfirmed->setToolTip(QString::fromUtf8(
        u8"该确认只对当前运行会话有效；配置或设备参数变化后必须重新确认。"));
    deviceLayout->addWidget(m_deviceConfirmed);
    m_liveReason = new QLabel(this);
    m_liveReason->setObjectName("liveReason");
    m_liveReason->setWordWrap(true);
    root->addWidget(m_liveReason);
    deviceLayout->addStretch();
    tabs->addTab(scrollPage(devicePage), QString::fromUtf8(u8"设备与连接"));

    auto *advancedPage = new QWidget(this);
    auto *advancedLayout = new QVBoxLayout(advancedPage);
    auto *advancedHint = new QLabel(QString::fromUtf8(
        u8"高级配置显示完整 JSON。普通编辑请使用前面的中文页面；手工修改 JSON 后需点击“应用 JSON 到草稿”。"), this);
    advancedHint->setWordWrap(true);
    advancedLayout->addWidget(advancedHint);
    auto *advancedForm = new QFormLayout;
    advancedForm->addRow(m_byteOrderLabel, m_byteOrder);
    advancedLayout->addLayout(advancedForm);
    m_advancedJson = new QTextEdit(this);
    m_advancedJson->setObjectName("advancedJson");
    m_advancedJson->setFontFamily(QStringLiteral("Consolas"));
    advancedLayout->addWidget(m_advancedJson);
    auto *applyJson = new QPushButton(QString::fromUtf8(u8"应用 JSON 到草稿"), this);
    applyJson->setToolTip(QString::fromUtf8(u8"解析当前 JSON 并刷新中文表单；不会自动启用。"));
    advancedLayout->addWidget(applyJson);
    tabs->addTab(scrollPage(advancedPage), QString::fromUtf8(u8"高级配置"));

    auto *configButtons = new QHBoxLayout;
    auto *importButton = new QPushButton(QString::fromUtf8(u8"导入配置"), this);
    auto *exportButton = new QPushButton(QString::fromUtf8(u8"导出草稿"), this);
    auto *validateButton = new QPushButton(QString::fromUtf8(u8"校验配置"), this);
    auto *enableButton = new QPushButton(QString::fromUtf8(u8"启用配置"), this);
    importButton->setToolTip(QString::fromUtf8(u8"从 JSON 文件载入草稿，不改变当前已启用配置。"));
    exportButton->setToolTip(QString::fromUtf8(u8"校验并保存当前草稿 JSON。"));
    validateButton->setToolTip(QString::fromUtf8(u8"检查帧边界、字段顺序、命令映射、换算和设备参数。"));
    enableButton->setToolTip(QString::fromUtf8(u8"把已校验草稿复制为新的运行快照；在途请求不允许切换版本。"));
    for (auto *button : {importButton, exportButton, validateButton, enableButton}) configButtons->addWidget(button);
    configButtons->addStretch();
    root->addLayout(configButtons);

    auto *debugGroup = new QGroupBox(QString::fromUtf8(u8"调试与运行"), this);
    auto *debugLayout = new QVBoxLayout(debugGroup);
    auto *inputLayout = new QHBoxLayout;
    m_inputEncoding = new QLabel(QString::fromUtf8(u8"草稿编码：未确定"), this);
    m_input = new QLineEdit(this);
    m_input->setObjectName("customerInput");
    m_input->setPlaceholderText(QString::fromUtf8(u8"输入一条完整客户报文；ASCII 控制字符用 \\r、\\n 表示"));
    m_previewButton = new QPushButton(QString::fromUtf8(u8"预览但不发送"), this);
    m_sendButton = new QPushButton(QString::fromUtf8(u8"真实发送到下游"), this);
    m_cancelButton = new QPushButton(QString::fromUtf8(u8"取消本地等待"), this);
    m_cancelButton->setToolTip(QString::fromUtf8(u8"只取消软件等待，不会向设备发送停止命令。"));
    m_previewButton->setToolTip(QString::fromUtf8(u8"使用当前草稿解析和组帧，不访问串口。"));
    inputLayout->addWidget(m_inputEncoding);
    inputLayout->addWidget(m_input, 1);
    inputLayout->addWidget(m_previewButton);
    inputLayout->addWidget(m_sendButton);
    inputLayout->addWidget(m_cancelButton);
    m_queryButton = new QPushButton(QString::fromUtf8(u8"查询设备状态"), this);
    m_queryButton->setToolTip(QString::fromUtf8(u8"用于超时、断线或取消等待后重新确认设备忙闲；只发送趋盛状态查询，不执行运动。"));
    inputLayout->addWidget(m_queryButton);
    debugLayout->addLayout(inputLayout);
    auto *outputs = new QHBoxLayout;
    m_previewOutput = new QTextEdit(this); m_previewOutput->setReadOnly(true);
    m_previewOutput->setObjectName("previewOutput");
    m_targetOutput = new QTextEdit(this); m_targetOutput->setReadOnly(true);
    m_targetOutput->setObjectName("targetOutput");
    m_rawReply = new QTextEdit(this); m_rawReply->setReadOnly(true);
    m_replyResult = new QTextEdit(this); m_replyResult->setReadOnly(true);
    // 解析过程需要足够宽度显示字段与换算，右侧按页查看目标帧、原始回告和解析结果。
    // 控件均保留原始内容，切页不会改变请求状态，也不触发任何发送。
    auto *previewColumn = new QVBoxLayout;
    previewColumn->addWidget(new QLabel(QString::fromUtf8(u8"解析与换算预览"), this));
    m_previewOutput->setMinimumHeight(190);
    previewColumn->addWidget(m_previewOutput);
    outputs->addLayout(previewColumn, 3);
    auto *deviceOutputs = new QTabWidget(this);
    deviceOutputs->addTab(m_targetOutput, QString::fromUtf8(u8"趋盛目标帧"));
    deviceOutputs->addTab(m_rawReply, QString::fromUtf8(u8"原始设备回告"));
    deviceOutputs->addTab(m_replyResult, QString::fromUtf8(u8"回告解析结果"));
    deviceOutputs->setMinimumHeight(210);
    outputs->addWidget(deviceOutputs, 2);
    debugLayout->addLayout(outputs);
    auto *offline = new QHBoxLayout;
    m_offlineReply = new QLineEdit(this);
    m_offlineReply->setPlaceholderText(QString::fromUtf8(u8"可粘贴完整趋盛 HEX 帧做离线分帧校验"));
    auto *offlineButton = new QPushButton(QString::fromUtf8(u8"离线解析设备帧"), this);
    offlineButton->setToolTip(QString::fromUtf8(u8"只检查粘贴的趋盛 HEX 帧完整性，不改变请求状态。"));
    offline->addWidget(new QLabel(QString::fromUtf8(u8"模拟回告"), this));
    offline->addWidget(m_offlineReply, 1);
    offline->addWidget(offlineButton);
    debugLayout->addLayout(offline);
    m_requestState = new QLabel(QString::fromUtf8(u8"执行状态：空闲"), this);
    debugLayout->addWidget(m_requestState);
    tabs->insertTab(2, scrollPage(debugGroup), QString::fromUtf8(u8"报文调试"));

    m_log = new QTextEdit(this);
    m_log->setReadOnly(true);
    m_log->setMaximumHeight(80);
    root->addWidget(new QLabel(QString::fromUtf8(u8"运行日志（最多 200 行）"), this));
    root->addWidget(m_log);

    connect(addField, &QPushButton::clicked, this, [this]() { addFieldRow(); markDraftDirty(); });
    connect(removeField, &QPushButton::clicked, this, [this]() {
        if (m_fieldTable->currentRow() >= 0) m_fieldTable->removeRow(m_fieldTable->currentRow());
        for (int row = 0; row < m_fieldTable->rowCount(); ++row)
            m_fieldTable->item(row, 0)->setText(QString::number(row + 1));
        markDraftDirty();
    });
    connect(fieldUp, &QPushButton::clicked, this, [this]() {
        const int row = m_fieldTable->currentRow();
        if (row <= 0) return;
        if (!saveUiToDraft()) return;
        QJsonArray fields = m_draftRoot.value("external").toObject().value("fields").toArray();
        const QJsonValue value = fields.takeAt(row); fields.insert(row - 1, value);
        QJsonObject external = m_draftRoot.value("external").toObject(); external.insert("fields", fields);
        m_draftRoot.insert("external", external); loadUiFromDraft(); m_fieldTable->selectRow(row - 1); markDraftDirty();
    });
    connect(fieldDown, &QPushButton::clicked, this, [this]() {
        const int row = m_fieldTable->currentRow();
        if (row < 0 || row >= m_fieldTable->rowCount() - 1) return;
        if (!saveUiToDraft()) return;
        QJsonArray fields = m_draftRoot.value("external").toObject().value("fields").toArray();
        const QJsonValue value = fields.takeAt(row); fields.insert(row + 1, value);
        QJsonObject external = m_draftRoot.value("external").toObject(); external.insert("fields", fields);
        m_draftRoot.insert("external", external); loadUiFromDraft(); m_fieldTable->selectRow(row + 1); markDraftDirty();
    });
    connect(saveFormat, &QPushButton::clicked, this, &Widget::saveUiToDraft);
    connect(addMapping, &QPushButton::clicked, this, [this]() { addMappingRow(); markDraftDirty(); });
    connect(removeMapping, &QPushButton::clicked, this, [this]() {
        if (m_mappingTable->currentRow() >= 0) m_mappingTable->removeRow(m_mappingTable->currentRow());
        markDraftDirty();
    });
    connect(saveMapping, &QPushButton::clicked, this, &Widget::saveUiToDraft);
    connect(saveDevice, &QPushButton::clicked, this, &Widget::saveUiToDraft);
    connect(applyJson, &QPushButton::clicked, this, &Widget::applyAdvancedJson);
    connect(importButton, &QPushButton::clicked, this, &Widget::importDraft);
    connect(exportButton, &QPushButton::clicked, this, &Widget::exportDraft);
    connect(validateButton, &QPushButton::clicked, this, &Widget::validateDraft);
    connect(enableButton, &QPushButton::clicked, this, &Widget::enableDraft);
    connect(refreshPort, &QPushButton::clicked, this, &Widget::refreshPorts);
    connect(offlineButton, &QPushButton::clicked, this, &Widget::parseOfflineReply);
}

void Widget::connectSignals()
{
    connect(m_advancedJson, &QTextEdit::textChanged, this, [this]() {
        if (m_loadingUi) return;
        m_jsonDirty = true;
        m_draftValid = false;
        m_deviceConfirmed->setChecked(false);
        m_draftVersion->setText(QString::fromUtf8(u8"当前草稿：高级 JSON 有未应用修改"));
        m_validationState->setText(QString::fromUtf8(u8"配置状态：请应用 JSON 到草稿"));
        updateLiveState();
    });
    connect(m_encoding, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        const bool binary = m_encoding->currentData().toString() == "hex";
        m_byteOrderLabel->setVisible(binary);
        m_byteOrder->setVisible(binary);
        m_header->setPlaceholderText(binary ? QStringLiteral("例如 AA55") : QStringLiteral("例如 /"));
        m_footer->setPlaceholderText(binary ? QStringLiteral("例如 0D0A") : QStringLiteral("例如 R\\r"));
        markDraftDirty();
    });
    for (QLineEdit *edit : {m_header, m_footer})
        connect(edit, &QLineEdit::textChanged, this, &Widget::markDraftDirty);
    connect(m_byteOrder, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &Widget::markDraftDirty);
    connect(m_fieldTable, &QTableWidget::cellChanged, this, [this](int, int column) {
        Q_UNUSED(column);
        markDraftDirty();
    });
    connect(m_mappingTable, &QTableWidget::cellChanged, this, &Widget::markDraftDirty);
    for (QSpinBox *spin : {m_destination, m_source, m_devId, m_action, m_speed, m_minPosition, m_maxPosition})
        connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() {
            m_deviceConfirmed->setChecked(false);
            markDraftDirty();
        });
    connect(m_deviceConfirmed, &QCheckBox::toggled, this, &Widget::updateLiveState);
    connect(m_connectButton, &QPushButton::clicked, this, [this]() {
        if (m_transport->isConnected()) m_transport->close();
        else if (!m_port->currentData().toString().isEmpty())
            m_transport->open(m_port->currentData().toString(), m_baud->currentData().toInt());
    });
    connect(m_transport, &SerialTransport::connectionChanged, this, &Widget::updateConnectionUi);
    connect(m_previewButton, &QPushButton::clicked, this, [this]() { preview(false); });
    connect(m_sendButton, &QPushButton::clicked, this, [this]() { preview(true); });
    connect(m_queryButton, &QPushButton::clicked, this, [this]() {
        if (!m_configEnabled || !m_deviceConfirmed->isChecked() || !m_transport->isConnected()
                || m_requestActive) return;
        UnifiedCommand query;
        query.operation = "QUERY_MOTOR";
        query.action = ActionType::Query;
        query.targetDevice = QString::number(m_activeConfig.root.value("device").toObject().value("devId").toInt());
        query.configVersion = m_activeConfig.version;
        m_lastRequestId = m_service->submitUnifiedCommand(query);
    });
    connect(m_cancelButton, &QPushButton::clicked, this, [this]() {
        if (m_lastRequestId) m_service->cancelRequest(m_lastRequestId);
    });
    connect(m_service, &TranslationService::outgoingFrame, this,
            [this](quint64, const QByteArray &frame) {
        m_targetOutput->setPlainText(QString::fromUtf8(u8"实际下发：\n") + frame.toHex(' ').toUpper());
    });
    connect(m_service, &TranslationService::rawDataReceived, this,
            [this](const QByteArray &raw) { m_rawReply->append(raw.toHex(' ').toUpper()); });
    connect(m_service, &TranslationService::completed, this, [this](const UnifiedResult &result) {
        if (result.success) {
            QStringList lines{QString::fromUtf8(u8"请求 %1：设备回告成功").arg(result.requestId)};
            for (auto it = result.data.begin(); it != result.data.end(); ++it) {
                QString value = it.value().toString();
                if (it.key() == "basicStatus") value = it.value().toByteArray().toHex(' ').toUpper();
                if (it.key() == "deviceBusy") value = it.value().toBool() ? QString::fromUtf8(u8"运动中或状态未知") : QString::fromUtf8(u8"空闲");
                lines << QStringLiteral("%1 = %2").arg(displayName(it.key()), value);
            }
            m_replyResult->setPlainText(lines.join('\n'));
        } else {
            m_replyResult->setPlainText(QString::fromUtf8(u8"请求 %1 失败\n错误代码：%2\n原因：%3")
                .arg(result.requestId).arg(result.error.code, result.error.message));
        }
    });
    connect(m_service, &TranslationService::requestStateChanged, this,
            [this](quint64 id, RequestState state, const QString &detail) {
        m_requestActive = state == RequestState::Queued || state == RequestState::Sending
                || state == RequestState::Written || state == RequestState::WaitingReply
                || state == RequestState::Acknowledged || state == RequestState::InMotion;
        m_requestState->setText(QString::fromUtf8(u8"执行状态：请求 %1，%2；%3")
                                .arg(id).arg(stateText(state), detail));
        appendLog(QString::fromUtf8(u8"请求 %1：%2；%3").arg(id).arg(stateText(state), detail));
        updateLiveState();
    });
    connect(m_service, &TranslationService::errorOccurred, this,
            [this](quint64, const TranslationError &error) {
        appendLog(QStringLiteral("%1：%2").arg(error.code, error.message));
        m_replyResult->append(QString::fromUtf8(u8"错误代码：%1\n原因：%2").arg(error.code, error.message));
    });
    connect(m_service, &TranslationService::unsolicitedMessage, this,
            [this](const ProtocolMessage &message) {
        appendLog(QString::fromUtf8(u8"收到主动或迟到的 0x%1 帧，未用于完成当前新请求")
                  .arg(message.messageType, 2, 16, QLatin1Char('0')));
    });
    connect(m_requests, &RequestManager::deviceBusyChanged, this, [this](bool busy) {
        appendLog(busy ? QString::fromUtf8(u8"设备状态：运动中或最终状态未知")
                       : QString::fromUtf8(u8"设备状态：已确认空闲"));
        updateLiveState();
    });
}

void Widget::loadInitialExample()
{
    const QStringList candidates = {
        QDir(QCoreApplication::applicationDirPath()).filePath("examples/dt-preview.json"),
        QDir(QCoreApplication::applicationDirPath()).filePath("../../examples/dt-preview.json"),
        QDir::current().filePath("examples/dt-preview.json")
    };
    for (const QString &path : candidates) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) continue;
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
        if (document.isObject()) {
            m_draftRoot = document.object();
            loadUiFromDraft();
            m_input->setText(QStringLiteral("/1A2000R\\r"));
            appendLog(QString::fromUtf8(u8"已载入 DT 风格通用配置示例；DT 不是程序内置协议。"));
            return;
        }
    }
    appendLog(QString::fromUtf8(u8"未找到示例配置，请导入 JSON 配置。"));
}

void Widget::addFieldRow(const QJsonObject &field)
{
    const int row = m_fieldTable->rowCount();
    m_fieldTable->insertRow(row);
    auto *order = new QTableWidgetItem(QString::number(row + 1));
    order->setFlags(order->flags() & ~Qt::ItemIsEditable);
    m_fieldTable->setItem(row, 0, order);
    m_fieldTable->setItem(row, 1, new QTableWidgetItem(field.value("name").toString()));
    auto *role = combo(this, {{QString::fromUtf8(u8"命令码"), QStringLiteral("command")},
                              {QString::fromUtf8(u8"参数"), QStringLiteral("parameter")},
                              {QString::fromUtf8(u8"固定字段"), QStringLiteral("fixed")}});
    auto *type = combo(this, {{QStringLiteral("ASCII"), QStringLiteral("ascii")},
                              {QString::fromUtf8(u8"无符号整数"), QStringLiteral("uint")},
                              {QString::fromUtf8(u8"有符号整数"), QStringLiteral("int")}});
    auto *lengthMode = combo(this, {{QString::fromUtf8(u8"固定长度"), QStringLiteral("fixed")},
                                    {QString::fromUtf8(u8"读取到帧尾"), QStringLiteral("toFooter")}});
    selectData(role, field.value("role").toString("parameter"));
    selectData(type, field.value("type").toString("ascii"));
    selectData(lengthMode, field.value("lengthMode").toString("fixed"));
    m_fieldTable->setCellWidget(row, 2, role);
    m_fieldTable->setCellWidget(row, 3, type);
    m_fieldTable->setCellWidget(row, 4, lengthMode);
    m_fieldTable->setItem(row, 5, new QTableWidgetItem(field.contains("length")
        ? QString::number(field.value("length").toInt()) : QString()));
    m_fieldTable->setItem(row, 6, new QTableWidgetItem(field.value("fixedValue").toVariant().toString()));
    m_fieldTable->setItem(row, 7, new QTableWidgetItem(field.contains("min")
        ? field.value("min").toVariant().toString() : QString()));
    m_fieldTable->setItem(row, 8, new QTableWidgetItem(field.contains("max")
        ? field.value("max").toVariant().toString() : QString()));
    for (int col = 0; col < m_fieldTable->columnCount(); ++col) {
        if (m_fieldTable->item(row, col)) m_fieldTable->item(row, col)->setToolTip(m_fieldTable->horizontalHeaderItem(col)->toolTip());
        if (m_fieldTable->cellWidget(row, col)) m_fieldTable->cellWidget(row, col)->setToolTip(m_fieldTable->horizontalHeaderItem(col)->toolTip());
    }
    for (QComboBox *box : {role, type, lengthMode})
        connect(box, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &Widget::markDraftDirty);
}

void Widget::addMappingRow(const QJsonObject &mapping)
{
    const int row = m_mappingTable->rowCount();
    m_mappingTable->insertRow(row);
    m_mappingTable->setItem(row, 0, new QTableWidgetItem(mapping.value("commandCode").toVariant().toString()));
    auto *operation = combo(this, {{QString::fromUtf8(u8"绝对位置运动"), QStringLiteral("ABSOLUTE_MOVE")}});
    auto *response = combo(this, {{QString::fromUtf8(u8"命令应答后返回"), QStringLiteral("ack")},
                                  {QString::fromUtf8(u8"动作完成后返回"), QStringLiteral("complete")}});
    selectData(operation, mapping.value("operation").toString("ABSOLUTE_MOVE"));
    selectData(response, mapping.value("responsePolicy").toString("ack"));
    m_mappingTable->setCellWidget(row, 1, operation);
    m_mappingTable->setCellWidget(row, 2, response);
    auto *target = new QTableWidgetItem(QString::fromUtf8(u8"目标位置（微米）"));
    target->setFlags(target->flags() & ~Qt::ItemIsEditable);
    m_mappingTable->setItem(row, 3, target);
    const QJsonObject expression = mapping.value("parameters").toObject().value("positionUm").toObject();
    auto *kind = combo(this, {{QString::fromUtf8(u8"直接引用"), QStringLiteral("field")},
                              {QString::fromUtf8(u8"固定值"), QStringLiteral("constant")},
                              {QString::fromUtf8(u8"线性换算"), QStringLiteral("linear")}});
    selectData(kind, expression.value("kind").toString("linear"));
    m_mappingTable->setCellWidget(row, 4, kind);
    auto *source = new QComboBox(this);
    source->addItem(QString::fromUtf8(u8"请选择字段"), QString());
    for (int fieldRow = 0; fieldRow < m_fieldTable->rowCount(); ++fieldRow) {
        const QString name = itemText(m_fieldTable, fieldRow, 1);
        if (!name.isEmpty()) source->addItem(name, name);
    }
    const QString selectedSource = expression.value("field").toString();
    if (!selectedSource.isEmpty() && source->findData(selectedSource) < 0)
        source->addItem(QString::fromUtf8(u8"未找到字段：%1").arg(selectedSource), selectedSource);
    selectData(source, selectedSource);
    m_mappingTable->setCellWidget(row, 5, source);
    m_mappingTable->setItem(row, 6, new QTableWidgetItem(expression.value("value").toVariant().toString()));
    m_mappingTable->setItem(row, 7, new QTableWidgetItem(expression.contains("numerator")
        ? expression.value("numerator").toVariant().toString() : QStringLiteral("1")));
    m_mappingTable->setItem(row, 8, new QTableWidgetItem(expression.contains("denominator")
        ? expression.value("denominator").toVariant().toString() : QStringLiteral("1")));
    m_mappingTable->setItem(row, 9, new QTableWidgetItem(expression.contains("offset")
        ? expression.value("offset").toVariant().toString() : QStringLiteral("0")));
    auto *rounding = combo(this, {{QString::fromUtf8(u8"必须整除"), QStringLiteral("exact")},
                                  {QString::fromUtf8(u8"向下取整"), QStringLiteral("floor")},
                                  {QString::fromUtf8(u8"向上取整"), QStringLiteral("ceil")},
                                  {QString::fromUtf8(u8"四舍五入"), QStringLiteral("nearest")}});
    selectData(rounding, expression.value("round").toString("exact"));
    m_mappingTable->setCellWidget(row, 10, rounding);
    for (int col = 0; col < m_mappingTable->columnCount(); ++col) {
        if (m_mappingTable->item(row, col)) m_mappingTable->item(row, col)->setToolTip(m_mappingTable->horizontalHeaderItem(col)->toolTip());
        if (m_mappingTable->cellWidget(row, col)) m_mappingTable->cellWidget(row, col)->setToolTip(m_mappingTable->horizontalHeaderItem(col)->toolTip());
    }
    for (QComboBox *box : {operation, response, kind, source, rounding})
        connect(box, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &Widget::markDraftDirty);
}

void Widget::loadUiFromDraft()
{
    m_loadingUi = true;
    const QSignalBlocker jsonBlocker(m_advancedJson);
    const QJsonObject external = m_draftRoot.value("external").toObject();
    selectData(m_encoding, external.value("encoding").toString("ascii"));
    m_header->setText(external.value("header").toString());
    m_footer->setText(external.value("footer").toString());
    selectData(m_byteOrder, external.value("byteOrder").toString("big"));
    const bool binary = m_encoding->currentData().toString() == "hex";
    m_byteOrderLabel->setVisible(binary);
    m_byteOrder->setVisible(binary);
    m_fieldTable->setRowCount(0);
    for (const QJsonValue &field : external.value("fields").toArray()) addFieldRow(field.toObject());
    m_mappingTable->setRowCount(0);
    for (const QJsonValue &mapping : m_draftRoot.value("mappings").toArray()) addMappingRow(mapping.toObject());
    const QJsonObject device = m_draftRoot.value("device").toObject();
    m_destination->setValue(device.value("destination").toInt(2));
    m_source->setValue(device.value("source").toInt(1));
    m_devId->setValue(device.value("devId").toInt(0x11));
    m_action->setValue(device.value("absoluteAction").toInt(1));
    m_speed->setValue(device.value("speedUmPerS").toInt(1000));
    m_minPosition->setValue(device.value("minPositionUm").toInt(0));
    m_maxPosition->setValue(device.value("maxPositionUm").toInt(60000));
    m_advancedJson->setPlainText(QString::fromUtf8(QJsonDocument(m_draftRoot).toJson(QJsonDocument::Indented)));
    m_draftValid = false;
    m_draftVersion->setText(QString::fromUtf8(u8"当前草稿：已载入，待校验"));
    m_validationState->setText(QString::fromUtf8(u8"配置状态：待校验"));
    m_inputEncoding->setText(QString::fromUtf8(u8"输入编码：")
        + (m_encoding->currentData().toString() == "hex" ? QStringLiteral("HEX") : QStringLiteral("ASCII")));
    m_deviceConfirmed->setChecked(false);
    m_formDirty = false;
    m_jsonDirty = false;
    m_loadingUi = false;
    updateTableControls();
    updateLiveState();
}

bool Widget::saveUiToDraft()
{
    if (m_jsonDirty) {
        appendLog(QString::fromUtf8(u8"请先应用高级 JSON 到草稿，避免表单覆盖尚未应用的 JSON。"));
        return false;
    }
    // 所有数字先严格读取，任何失败都保留旧草稿；不能把 abc 或小数隐式保存成 0。
    auto readInteger = [this](const QString &text, qlonglong *value, const QString &label) {
        bool ok = false;
        *value = text.toLongLong(&ok);
        if (!ok) {
            m_validationState->setText(QString::fromUtf8(u8"保存失败：%1必须填写整数").arg(label));
            appendLog(m_validationState->text());
        }
        return ok;
    };
    QJsonObject root = m_draftRoot;
    if (!root.contains("schemaVersion")) root.insert("schemaVersion", 1);
    if (!root.contains("name")) root.insert("name", QString::fromUtf8(u8"用户协议配置"));
    QJsonObject external;
    external.insert("encoding", m_encoding->currentData().toString());
    external.insert("header", m_header->text());
    external.insert("footer", m_footer->text());
    external.insert("byteOrder", m_byteOrder->currentData().toString());
    QJsonArray fields;
    for (int row = 0; row < m_fieldTable->rowCount(); ++row) {
        QJsonObject field;
        field.insert("name", itemText(m_fieldTable, row, 1));
        field.insert("role", comboData(m_fieldTable, row, 2));
        field.insert("type", comboData(m_fieldTable, row, 3));
        const QString lengthMode = comboData(m_fieldTable, row, 4);
        field.insert("lengthMode", lengthMode);
        if (lengthMode == "fixed") {
            qlonglong length;
            if (!readInteger(itemText(m_fieldTable, row, 5), &length, QString::fromUtf8(u8"字段长度"))) return false;
            field.insert("length", double(length));
        }
        if (comboData(m_fieldTable, row, 2) == "fixed") field.insert("fixedValue", itemText(m_fieldTable, row, 6));
        bool ok = false;
        if (comboData(m_fieldTable, row, 3) != "ascii" && !itemText(m_fieldTable, row, 7).isEmpty()) {
            const double number = itemText(m_fieldTable, row, 7).toDouble(&ok);
            if (!ok) { appendLog(QString::fromUtf8(u8"第 %1 行最小值不是数字").arg(row + 1)); return false; }
            field.insert("min", number);
        }
        if (comboData(m_fieldTable, row, 3) != "ascii" && !itemText(m_fieldTable, row, 8).isEmpty()) {
            const double number = itemText(m_fieldTable, row, 8).toDouble(&ok);
            if (!ok) { appendLog(QString::fromUtf8(u8"第 %1 行最大值不是数字").arg(row + 1)); return false; }
            field.insert("max", number);
        }
        fields.append(field);
    }
    external.insert("fields", fields);
    root.insert("external", external);

    QJsonArray mappings;
    for (int row = 0; row < m_mappingTable->rowCount(); ++row) {
        QJsonObject expression;
        const QString kind = comboData(m_mappingTable, row, 4);
        expression.insert("kind", kind);
        if (kind == "constant") {
            qlonglong value;
            if (!readInteger(itemText(m_mappingTable, row, 6), &value, QString::fromUtf8(u8"固定位置"))) return false;
            expression.insert("value", double(value));
        } else {
            expression.insert("field", comboData(m_mappingTable, row, 5));
            if (kind == "linear") {
                for (auto pair : QList<QPair<int, QString>>{{7, "numerator"}, {8, "denominator"}, {9, "offset"}}) {
                    qlonglong number;
                    if (!readInteger(itemText(m_mappingTable, row, pair.first), &number,
                                     m_mappingTable->horizontalHeaderItem(pair.first)->text())) return false;
                    expression.insert(pair.second, double(number));
                }
                expression.insert("round", comboData(m_mappingTable, row, 10));
            }
        }
        QJsonObject mapping;
        mapping.insert("commandCode", itemText(m_mappingTable, row, 0));
        mapping.insert("operation", comboData(m_mappingTable, row, 1));
        mapping.insert("responsePolicy", comboData(m_mappingTable, row, 2));
        mapping.insert("parameters", QJsonObject{{QStringLiteral("positionUm"), expression}});
        mappings.append(mapping);
    }
    root.insert("mappings", mappings);
    root.insert("device", QJsonObject{
        {"destination", m_destination->value()}, {"source", m_source->value()},
        {"devId", m_devId->value()}, {"absoluteAction", m_action->value()},
        {"speedUmPerS", m_speed->value()}, {"minPositionUm", m_minPosition->value()},
        {"maxPositionUm", m_maxPosition->value()}});
    m_draftRoot = root;
    const QSignalBlocker jsonBlocker(m_advancedJson);
    m_advancedJson->setPlainText(QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented)));
    m_draftValid = false;
    m_draftVersion->setText(QString::fromUtf8(u8"当前草稿：已保存，待校验"));
    m_validationState->setText(QString::fromUtf8(u8"配置状态：待校验"));
    m_inputEncoding->setText(QString::fromUtf8(u8"输入编码：")
        + (m_encoding->currentData().toString() == "hex" ? QStringLiteral("HEX") : QStringLiteral("ASCII")));
    m_deviceConfirmed->setChecked(false);
    m_formDirty = false;
    appendLog(QString::fromUtf8(u8"中文表单已保存到草稿；当前已启用版本未改变。"));
    updateLiveState();
    return true;
}

void Widget::markDraftDirty()
{
    if (m_loadingUi || m_draftRoot.isEmpty()) return;
    m_formDirty = true;
    m_draftValid = false;
    m_deviceConfirmed->setChecked(false);
    updateTableControls();
    m_draftVersion->setText(QString::fromUtf8(u8"当前草稿：表单有未保存修改"));
    m_validationState->setText(QString::fromUtf8(u8"配置状态：请先保存到草稿"));
    updateLiveState();
}

bool Widget::draftSaved()
{
    // “表单”“草稿”“启用快照”是三个不同状态。未保存表单绝不能被旧草稿的校验成功掩盖。
    if (!m_formDirty && !m_jsonDirty) return true;
    const QString reason = m_jsonDirty ? QString::fromUtf8(u8"请先应用高级 JSON 到草稿。")
                                      : QString::fromUtf8(u8"请先点击“保存到草稿”，再校验、启用、预览或导出。");
    m_validationState->setText(QString::fromUtf8(u8"配置状态：") + reason);
    appendLog(reason);
    return false;
}

void Widget::updateTableControls()
{
    if (m_loadingUi) return;
    const QSignalBlocker fieldBlocker(m_fieldTable), mappingBlocker(m_mappingTable);
    auto editable = [](QTableWidgetItem *item, bool enabled) {
        if (!item) return;
        item->setFlags(enabled ? item->flags() | Qt::ItemIsEditable | Qt::ItemIsEnabled
                               : item->flags() & ~Qt::ItemIsEditable & ~Qt::ItemIsEnabled);
        item->setBackground(enabled ? QBrush() : QBrush(QColor(242, 242, 242)));
    };
    bool anyFixed = false, anyNumber = false, anyLength = false;
    QStringList names;
    for (int row = 0; row < m_fieldTable->rowCount(); ++row) {
        const bool fixed = comboData(m_fieldTable, row, 2) == "fixed";
        const bool number = comboData(m_fieldTable, row, 3) != "ascii";
        const bool length = comboData(m_fieldTable, row, 4) == "fixed";
        anyFixed |= fixed; anyNumber |= number; anyLength |= length;
        editable(m_fieldTable->item(row, 5), length);
        editable(m_fieldTable->item(row, 6), fixed);
        editable(m_fieldTable->item(row, 7), number);
        editable(m_fieldTable->item(row, 8), number);
        const QString name = itemText(m_fieldTable, row, 1);
        if (!name.isEmpty()) names << name;
    }
    m_fieldTable->setColumnHidden(5, !anyLength);
    m_fieldTable->setColumnHidden(6, !anyFixed);
    m_fieldTable->setColumnHidden(7, !anyNumber);
    m_fieldTable->setColumnHidden(8, !anyNumber);
    bool anyConstant = false, anySource = false, anyLinear = false;
    for (int row = 0; row < m_mappingTable->rowCount(); ++row) {
        const QString kind = comboData(m_mappingTable, row, 4);
        const bool constant = kind == "constant", linear = kind == "linear";
        anyConstant |= constant; anySource |= !constant; anyLinear |= linear;
        auto *source = qobject_cast<QComboBox *>(m_mappingTable->cellWidget(row, 5));
        if (source) {
            const QSignalBlocker sourceBlocker(source);
            const QString selected = source->currentData().toString();
            source->clear();
            source->addItem(QString::fromUtf8(u8"请选择字段"), QString());
            for (const QString &name : names) source->addItem(name, name);
            // 删除或改名不能静默选中别的参数；保留失效引用，让配置校验指出原因。
            if (!selected.isEmpty() && !names.contains(selected))
                source->addItem(QString::fromUtf8(u8"未找到字段：%1").arg(selected), selected);
            selectData(source, selected);
            source->setEnabled(!constant);
        }
        editable(m_mappingTable->item(row, 6), constant);
        for (int col : {7, 8, 9}) editable(m_mappingTable->item(row, col), linear);
        if (m_mappingTable->cellWidget(row, 10)) m_mappingTable->cellWidget(row, 10)->setEnabled(linear);
    }
    m_mappingTable->setColumnHidden(5, !anySource);
    m_mappingTable->setColumnHidden(6, !anyConstant);
    for (int col : {7, 8, 9, 10}) m_mappingTable->setColumnHidden(col, !anyLinear);
}

void Widget::validateDraft()
{
    if (!draftSaved()) return;
    ProtocolConfig config;
    TranslationError error;
    if (!ProtocolConfig::fromJson(QJsonDocument(m_draftRoot).toJson(), &config, &error)) {
        m_draftValid = false;
        m_validationState->setText(QString::fromUtf8(u8"配置状态：校验失败（%1）").arg(error.message));
        appendLog(QStringLiteral("%1：%2").arg(error.code, error.message));
        updateLiveState();
        return;
    }
    m_validatedDraft = config;
    m_draftValid = true;
    m_draftVersion->setText(QString::fromUtf8(u8"当前草稿：%1").arg(config.version.left(12)));
    m_validationState->setText(QString::fromUtf8(u8"配置状态：校验成功，可启用"));
    appendLog(QString::fromUtf8(u8"配置校验成功，草稿版本 %1").arg(config.version.left(12)));
    updateLiveState();
}

void Widget::enableDraft()
{
    if (!draftSaved()) return;
    if (m_requestActive) {
        appendLog(QString::fromUtf8(u8"当前请求尚未结束，不能切换运行配置。"));
        return;
    }
    if (!m_draftValid) {
        appendLog(QString::fromUtf8(u8"请先保存并校验当前草稿，再点击启用配置。"));
        return;
    }
    m_activeConfig = m_validatedDraft;
    m_configEnabled = true;
    const QJsonObject device = m_activeConfig.root.value("device").toObject();
    m_protocol->setAddresses(quint8(device.value("destination").toInt()),
                             quint8(device.value("source").toInt()));
    // 同一物理连接里保留序号，切换配置不能重用旧序号而误匹配迟到的设备应答。
    m_activeVersion->setText(QString::fromUtf8(u8"当前已启用版本：%1").arg(m_activeConfig.version.left(12)));
    m_validationState->setText(QString::fromUtf8(u8"配置状态：已显式启用"));
    m_deviceConfirmed->setChecked(false);
    appendLog(QString::fromUtf8(u8"已启用配置快照；真实发送前还需勾选设备参数确认项。"));
    updateLiveState();
}

void Widget::applyAdvancedJson()
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(m_advancedJson->toPlainText().toUtf8(), &parseError);
    if (!document.isObject()) {
        appendLog(QString::fromUtf8(u8"高级 JSON 无效：%1").arg(parseError.errorString()));
        return;
    }
    m_draftRoot = document.object();
    loadUiFromDraft();
    appendLog(QString::fromUtf8(u8"高级 JSON 已应用到草稿；当前已启用版本未改变。"));
}

void Widget::importDraft()
{
    const QString path = QFileDialog::getOpenFileName(this, QString::fromUtf8(u8"导入协议配置"),
                                                       QString(), QStringLiteral("JSON (*.json)"));
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { appendLog(file.errorString()); return; }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) { appendLog(QString::fromUtf8(u8"导入文件不是有效 JSON 对象")); return; }
    m_draftRoot = document.object();
    loadUiFromDraft();
    appendLog(QString::fromUtf8(u8"配置已导入为草稿；当前已启用版本未改变。"));
}

void Widget::exportDraft()
{
    if (!draftSaved()) return;
    ProtocolConfig config;
    TranslationError error;
    if (!ProtocolConfig::fromJson(QJsonDocument(m_draftRoot).toJson(), &config, &error)) {
        appendLog(QStringLiteral("%1：%2").arg(error.code, error.message));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, QString::fromUtf8(u8"导出协议配置"),
                                                       QString(), QStringLiteral("JSON (*.json)"));
    if (!path.isEmpty() && !config.save(path, &error))
        appendLog(QStringLiteral("%1：%2").arg(error.code, error.message));
}

void Widget::refreshPorts()
{
    const QString selected = m_port->currentData().toString();
    m_port->clear();
    for (const QSerialPortInfo &port : QSerialPortInfo::availablePorts())
        m_port->addItem(QStringLiteral("%1 — %2").arg(port.portName(), port.description()), port.portName());
    const int index = m_port->findData(selected);
    if (index >= 0) m_port->setCurrentIndex(index);
}

void Widget::updateConnectionUi(bool connected, const QString &reason)
{
    m_port->setEnabled(!connected);
    m_baud->setEnabled(!connected);
    m_connectButton->setText(connected ? QString::fromUtf8(u8"断开下游") : QString::fromUtf8(u8"连接下游"));
    m_connectionState->setText(connected ? QString::fromUtf8(u8"已连接") : QString::fromUtf8(u8"未连接"));
    if (!reason.isEmpty()) appendLog(reason);
    if (connected) m_protocol->resetSession();
    m_deviceConfirmed->setChecked(false);
    updateLiveState();
}

void Widget::updateLiveState()
{
    const bool current = m_draftValid && !m_formDirty && !m_jsonDirty
            && m_validatedDraft.version == m_activeConfig.version;
    m_deviceConfirmed->setEnabled(m_configEnabled && current);
    QString reason;
    if (!m_configEnabled) reason = QString::fromUtf8(u8"真实发送不可用：尚未显式启用配置。");
    else if (!current)
        reason = QString::fromUtf8(u8"真实发送不可用：草稿已修改，请保存、校验并启用当前草稿，再确认设备参数。");
    else if (!m_deviceConfirmed->isChecked())
        reason = QString::fromUtf8(u8"真实发送不可用：请核对设备参数和位置换算后勾选确认项。");
    else if (!m_transport->isConnected())
        reason = QString::fromUtf8(u8"真实发送不可用：下游串口尚未连接。");
    else if (m_requestActive)
        reason = QString::fromUtf8(u8"真实发送不可用：当前请求尚未结束。");
    else if (m_requests->deviceBusy())
        reason = QString::fromUtf8(u8"真实发送不可用：设备仍在运动或最终状态未知。");
    else reason = QString::fromUtf8(u8"真实发送条件已满足。");
    const bool ready = m_configEnabled && current && m_deviceConfirmed->isChecked()
            && m_transport->isConnected() && !m_requestActive && !m_requests->deviceBusy();
    m_sendButton->setEnabled(ready);
    m_sendButton->setToolTip(reason);
    m_liveReason->setText(reason);
    m_liveReason->setStyleSheet(ready ? QStringLiteral("color:#087a32") : QStringLiteral("color:#a04400"));
    m_cancelButton->setEnabled(m_requestActive);
    m_queryButton->setEnabled(m_configEnabled && current && m_deviceConfirmed->isChecked()
                             && m_transport->isConnected() && !m_requestActive);
}

QByteArray Widget::inputBytes(const ProtocolConfig &config, TranslationError *error) const
{
    const QString encoding = config.root.value("external").toObject().value("encoding").toString();
    QByteArray bytes;
    ProtocolConfig::decodeBytes(m_input->text(), encoding, &bytes, error);
    return bytes;
}

QString Widget::previewText(const ConversionPreview &preview) const
{
    QStringList lines;
    lines << QString::fromUtf8(u8"原始输入：%1").arg(visibleBytes(preview.input));
    lines << QString::fromUtf8(u8"识别字段：");
    for (const QString &name : preview.fieldOrder)
        lines << QStringLiteral("  %1 = %2").arg(name, preview.fields.value(name).toString());
    lines << QString::fromUtf8(u8"命中的客户命令码：%1").arg(preview.matchedCommandCode);
    lines << QString::fromUtf8(u8"参数换算：");
    for (auto it = preview.parameterTrace.begin(); it != preview.parameterTrace.end(); ++it)
        lines << QStringLiteral("  %1：%2").arg(displayName(it.key()), it.value());
    lines << QString::fromUtf8(u8"统一命令：操作=%1，目标 DevID=%2，响应方式=%3，配置版本=%4")
             .arg(QString::fromUtf8(u8"绝对位置运动"), preview.command.targetDevice,
                  preview.command.responsePolicy == "ack" ? QString::fromUtf8(u8"命令应答后返回") : QString::fromUtf8(u8"动作完成后返回"),
                  preview.command.configVersion.left(12));
    for (auto it = preview.command.parameters.begin(); it != preview.command.parameters.end(); ++it)
        lines << QStringLiteral("  %1 = %2 %3").arg(displayName(it.key()), it.value().toString(),
                     preview.command.parameterUnits.value(it.key()));
    return lines.join('\n');
}

void Widget::preview(bool live)
{
    m_targetOutput->clear();
    if (!live && !draftSaved()) {
        m_previewOutput->setPlainText(QString::fromUtf8(u8"预览暂停：请先保存表单到草稿，或应用高级 JSON。"));
        return;
    }
    ProtocolConfig config;
    if (live) {
        if (!m_configEnabled || !m_draftValid || m_formDirty || m_jsonDirty
                || m_validatedDraft.version != m_activeConfig.version
                || !m_deviceConfirmed->isChecked() || !m_transport->isConnected()
                || m_requestActive || m_requests->deviceBusy()) {
            updateLiveState();
            appendLog(m_liveReason->text());
            return;
        }
        config = m_activeConfig; // 真实请求固定使用接收瞬间的已启用快照。
    } else {
        TranslationError configError;
        if (!ProtocolConfig::fromJson(QJsonDocument(m_draftRoot).toJson(), &config, &configError)) {
            m_previewOutput->setPlainText(QString::fromUtf8(u8"预览失败：%1").arg(configError.message));
            appendLog(QStringLiteral("%1：%2").arg(configError.code, configError.message));
            return;
        }
    }
    TranslationError inputError;
    const QByteArray input = inputBytes(config, &inputError);
    if (inputError.isValid()) {
        m_previewOutput->setPlainText(QString::fromUtf8(u8"输入错误：%1").arg(inputError.message));
        return;
    }
    ConversionEngine engine(config);
    ConversionPreview converted;
    TranslationError conversionError;
    if (!engine.convert(input, live, &converted, &conversionError)) {
        m_previewOutput->setPlainText(QString::fromUtf8(u8"转换已拒绝\n错误代码：%1\n原因：%2")
                                      .arg(conversionError.code, conversionError.message));
        appendLog(QStringLiteral("%1：%2").arg(conversionError.code, conversionError.message));
        return;
    }
    m_previewOutput->setPlainText((live ? QString::fromUtf8(u8"使用当前已启用版本\n")
                                       : QString::fromUtf8(u8"使用当前已保存草稿\n")) + previewText(converted));
    m_targetOutput->setPlainText(QString::fromUtf8(u8"预览帧（未发送）：\n")
                                 + converted.targetFrame.toHex(' ').toUpper());
    if (!live) {
        appendLog(QString::fromUtf8(u8"预览完成，未访问串口。"));
        return;
    }
    m_rawReply->clear();
    m_replyResult->clear();
    m_lastRequestId = m_service->submitUnifiedCommand(converted.command);
}

void Widget::parseOfflineReply()
{
    QByteArray compact = m_offlineReply->text().toLatin1();
    compact.replace(" ", "");
    if (compact.size() % 2 != 0 || QByteArray::fromHex(compact).toHex() != compact.toLower()) {
        m_replyResult->setPlainText(QString::fromUtf8(u8"离线解析失败：HEX 格式无效"));
        return;
    }
    QushengProtocol parser;
    QList<TranslationError> errors;
    const QList<ProtocolMessage> messages = parser.feedReceivedData(QByteArray::fromHex(compact), &errors);
    QStringList result;
    for (const TranslationError &error : errors)
        result << QStringLiteral("%1：%2").arg(error.code, error.message);
    for (const ProtocolMessage &message : messages)
        result << QString::fromUtf8(u8"完整趋盛帧：命令 0x%1，数据 %2")
                  .arg(message.messageType, 2, 16, QLatin1Char('0'))
                  .arg(QString::fromLatin1(message.payload.toHex(' ').toUpper()));
    if (result.isEmpty()) result << QString::fromUtf8(u8"尚未得到完整有效帧");
    m_replyResult->setPlainText(result.join('\n'));
}

void Widget::appendLog(const QString &text)
{
    m_log->append(QStringLiteral("[%1] %2").arg(QDateTime::currentDateTime().toString("HH:mm:ss"), text));
    while (m_log->document()->blockCount() > 200) {
        QTextCursor cursor(m_log->document());
        cursor.movePosition(QTextCursor::Start);
        cursor.select(QTextCursor::BlockUnderCursor);
        cursor.removeSelectedText();
        cursor.deleteChar();
    }
}

QString Widget::stateText(RequestState state)
{
    switch (state) {
    case RequestState::Queued: return QString::fromUtf8(u8"已排队");
    case RequestState::Sending: return QString::fromUtf8(u8"正在写出");
    case RequestState::Written: return QString::fromUtf8(u8"已写出");
    case RequestState::WaitingReply: return QString::fromUtf8(u8"等待设备命令应答");
    case RequestState::Acknowledged: return QString::fromUtf8(u8"设备已接纳命令");
    case RequestState::InMotion: return QString::fromUtf8(u8"动作进行中");
    case RequestState::Succeeded: return QString::fromUtf8(u8"已完成");
    case RequestState::Failed: return QString::fromUtf8(u8"失败");
    case RequestState::TimedOut: return QString::fromUtf8(u8"超时");
    case RequestState::Cancelled: return QString::fromUtf8(u8"已取消本地等待");
    }
    return QString::fromUtf8(u8"未知状态");
}
