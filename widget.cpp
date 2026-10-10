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
#include <QRegularExpression>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextCursor>
#include <QTextEdit>
#include <QVBoxLayout>
#include <limits>

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
    const QMap<QString, QString> names = {{"positionUm", QString::fromUtf8(u8"目标位置（数值）")},
        {"distanceUm", QString::fromUtf8(u8"移动距离（数值）")},
        {"minPositionUm", QString::fromUtf8(u8"最小位置（数值）")}, {"maxPositionUm", QString::fromUtf8(u8"最大位置（数值）")},
        {"expectedPositionUm", QString::fromUtf8(u8"本次计算终点（数值）")},
        {"speedUmPerS", QString::fromUtf8(u8"速度（下位机数值）")}, {"absoluteAction", QString::fromUtf8(u8"运动动作值")},
        {"basicStatus", QString::fromUtf8(u8"设备基本状态字（HEX）")}, {"motorState", QString::fromUtf8(u8"电机状态码")},
        {"feedbackPositionUm", QString::fromUtf8(u8"反馈位置（数值）")},
        {"hardwareVersion", QString::fromUtf8(u8"硬件版本")}, {"iapVersion", QString::fromUtf8(u8"引导程序版本")},
        {"firmwareVersion", QString::fromUtf8(u8"固件版本")}, {"deviceIdentifier", QString::fromUtf8(u8"设备标识")},
        {"boardType", QString::fromUtf8(u8"控制板类型")},
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

QString operationName(const QString &operation)
{
    const QMap<QString, QString> names = {{"ABSOLUTE_MOVE", QString::fromUtf8(u8"绝对位置运动")},
        {"RELATIVE_FORWARD", QString::fromUtf8(u8"正向相对运动")},
        {"RELATIVE_BACKWARD", QString::fromUtf8(u8"反向相对运动")},
        {"QUERY_MOTOR", QString::fromUtf8(u8"查询电机位置")},
        {"QUERY_DEVICE_INFO", QString::fromUtf8(u8"查询设备版本")}};
    return names.value(operation, QString::fromUtf8(u8"未支持的操作：%1").arg(operation));
}

bool isQuery(const QString &operation) { return operation.startsWith("QUERY_"); }

QString mappingParameterKey(const QString &operation)
{
    return operation == "RELATIVE_FORWARD" || operation == "RELATIVE_BACKWARD" ? QStringLiteral("distanceUm") : QStringLiteral("positionUm");
}

QString multiplierText(const QJsonObject &expression)
{
    qlonglong numerator = expression.value("numerator").toVariant().toLongLong();
    qlonglong denominator = expression.value("denominator").toVariant().toLongLong();
    if (denominator <= 0) return QStringLiteral("1");
    const bool negative = numerator < 0;
    numerator = qAbs(numerator);
    QString result = QString::number(numerator / denominator);
    qlonglong remainder = numerator % denominator;
    QString decimals;
    for (int i = 0; remainder && i < 9; ++i) {
        remainder *= 10;
        decimals += QString::number(remainder / denominator);
        remainder %= denominator;
    }
    // 不能把 1/3 等无限小数显示成近似值再悄悄写回；保留分数，并留在高级 JSON 中编辑。
    if (remainder) return QStringLiteral("%1/%2").arg(expression.value("numerator").toVariant().toString(),
                                                     expression.value("denominator").toVariant().toString());
    if (!decimals.isEmpty()) result += "." + decimals;
    return (negative ? QStringLiteral("-") : QString()) + result;
}

bool decimalRatio(const QString &text, qlonglong *numerator, qlonglong *denominator)
{
    static const QRegularExpression decimal(QStringLiteral("^[+-]?[0-9]+(?:\\.[0-9]{1,9})?$"));
    if (!decimal.match(text.trimmed()).hasMatch()) return false;
    QString digits = text.trimmed();
    const int point = digits.indexOf('.');
    *denominator = 1;
    if (point >= 0) {
        for (int i = point + 1; i < digits.size(); ++i) *denominator *= 10;
        digits.remove(point, 1);
    }
    bool ok = false;
    *numerator = digits.toLongLong(&ok);
    if (!ok || *numerator == std::numeric_limits<qlonglong>::min()) return false;
    qlonglong a = qAbs(*numerator), b = *denominator;
    while (b) { const qlonglong remainder = a % b; a = b; b = remainder; }
    if (a) { *numerator /= a; *denominator /= a; }
    // 按十进制字符生成精确分数，避免 0.1 被浮点表示误差变成另一个换算系数。
    return *numerator >= -1000000000LL && *numerator <= 1000000000LL && *denominator <= 1000000000LL;
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
        u8"配置定义的单条运动与查询；组合指令后续扩展。编辑草稿不会影响当前已启用版本。"), this);
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
        QString::fromUtf8(u8"固定长度按字节数读取；读取到帧尾放最后；ASCII 命令码可按配置识别不同长度"),
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
        u8"选择一条映射，在下方编辑。默认按客户数值直接转换，需要换算时再填写倍数。"), this);
    mappingHint->setWordWrap(true);
    mappingLayout->addWidget(mappingHint);
    m_mappingTable = new QTableWidget(this);
    m_mappingTable->setObjectName("mappingTable");
    m_mappingTable->setColumnCount(4);
    m_mappingTable->setHorizontalHeaderLabels({QString::fromUtf8(u8"客户命令"), QString::fromUtf8(u8"执行动作"),
        QString::fromUtf8(u8"参数说明"), QString::fromUtf8(u8"反馈时机")});
    m_mappingTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_mappingTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_mappingTable->verticalHeader()->hide();
    m_mappingTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_mappingTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_mappingTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_mappingTable->setMinimumHeight(160);
    m_mappingTable->setToolTip(QString::fromUtf8(u8"点击一行，在下方编辑此命令。编辑后点击保存到草稿，再校验和启用。"));
    const QStringList mappingTips = {QString::fromUtf8(u8"客户报文中的命令码，支持不同长度"),
        QString::fromUtf8(u8"选择趋盛命令模板"), QString::fromUtf8(u8"来源字段、换算倍数或固定值"),
        QString::fromUtf8(u8"运动可选择接纳后反馈或完成后反馈；查询收到回告后反馈")};
    for (int c = 0; c < mappingTips.size(); ++c) m_mappingTable->horizontalHeaderItem(c)->setToolTip(mappingTips.at(c));
    mappingLayout->addWidget(m_mappingTable, 1);

    m_mappingEditor = new QGroupBox(QString::fromUtf8(u8"编辑选中映射"), this);
    m_mappingEditor->setObjectName("mappingEditor");
    auto *editorLayout = new QVBoxLayout(m_mappingEditor);
    auto *editorForm = new QFormLayout;
    m_mappingCode = new QLineEdit(this); m_mappingCode->setObjectName("mappingCode");
    m_mappingCode->setToolTip(QString::fromUtf8(u8"填写客户命令码本身；不要填写地址、帧头或帧尾。"));
    m_mappingOperation = combo(this, {{QString::fromUtf8(u8"绝对位置运动"), "ABSOLUTE_MOVE"},
        {QString::fromUtf8(u8"正向相对运动"), "RELATIVE_FORWARD"}, {QString::fromUtf8(u8"反向相对运动"), "RELATIVE_BACKWARD"},
        {QString::fromUtf8(u8"查询电机位置"), "QUERY_MOTOR"}, {QString::fromUtf8(u8"查询设备版本"), "QUERY_DEVICE_INFO"}});
    m_mappingOperation->setObjectName("mappingOperation");
    m_mappingOperation->setToolTip(QString::fromUtf8(u8"选择一条趋盛动作或查询；不执行混合指令和命令组合。"));
    m_mappingSource = new QComboBox(this); m_mappingSource->setObjectName("mappingSource");
    m_mappingSource->setToolTip(QString::fromUtf8(u8"选择客户报文中的数字参数，或使用固定值。"));
    m_mappingValue = new QLineEdit(this); m_mappingValue->setObjectName("mappingValue");
    m_mappingValue->setToolTip(QString::fromUtf8(u8"固定目标位置或距离数值；必须填写整数，请核对实际设备单位。"));
    m_mappingMultiplier = new QLineEdit(this); m_mappingMultiplier->setObjectName("mappingMultiplier");
    m_mappingMultiplier->setToolTip(QString::fromUtf8(u8"客户字段乘以此数得到下位机数值；默认一倍，可填整数或最多九位小数。分数系数可在高级 JSON 中编辑。"));
    m_mappingResponse = combo(this, {{QString::fromUtf8(u8"设备接纳命令后"), "ack"}, {QString::fromUtf8(u8"动作完成后"), "complete"}});
    m_mappingResponse->setObjectName("mappingResponse");
    m_mappingResponse->setToolTip(QString::fromUtf8(u8"接纳命令只表示设备收到运动请求；等待完成会继续查询电机状态。"));
    editorForm->addRow(QString::fromUtf8(u8"客户命令"), m_mappingCode);
    editorForm->addRow(QString::fromUtf8(u8"执行动作"), m_mappingOperation);
    editorForm->addRow(QString::fromUtf8(u8"位置来源"), m_mappingSource);
    editorForm->addRow(QString::fromUtf8(u8"固定数值"), m_mappingValue);
    editorForm->addRow(QString::fromUtf8(u8"换算倍数"), m_mappingMultiplier);
    editorForm->addRow(QString::fromUtf8(u8"反馈时机"), m_mappingResponse);
    editorLayout->addLayout(editorForm);
    m_mappingParameterHint = new QLabel(this); m_mappingParameterHint->setObjectName("mappingParameterHint");
    m_mappingParameterHint->setWordWrap(true); editorLayout->addWidget(m_mappingParameterHint);
    auto *footerLine = new QHBoxLayout;
    m_mappingDefaultFooter = new QCheckBox(QString::fromUtf8(u8"使用协议默认帧尾"), this);
    m_mappingDefaultFooter->setObjectName("mappingDefaultFooter");
    m_mappingDefaultFooter->setToolTip(QString::fromUtf8(u8"取消勾选后，为这条命令单独填写帧尾；其他命令仍使用默认帧尾。"));
    m_mappingFooter = new QLineEdit(this); m_mappingFooter->setObjectName("mappingFooter");
    m_mappingFooter->setPlaceholderText(QString::fromUtf8(u8"此命令帧尾，例如 \\r"));
    m_mappingFooter->setToolTip(QString::fromUtf8(u8"按客户协议的编码填写；ASCII 可使用 \\r、\\n 转义。"));
    footerLine->addWidget(m_mappingDefaultFooter); footerLine->addWidget(m_mappingFooter, 1); editorLayout->addLayout(footerLine);
    m_mappingMore = new QGroupBox(QString::fromUtf8(u8"更多设置"), this);
    m_mappingMore->setObjectName("mappingMore"); m_mappingMore->setCheckable(true); m_mappingMore->setChecked(false);
    m_mappingMore->setFlat(true);
    auto *moreLayout = new QVBoxLayout(m_mappingMore);
    m_mappingMoreContent = new QWidget(this); auto *moreContentLayout = new QVBoxLayout(m_mappingMoreContent);
    auto *moreForm = new QFormLayout;
    m_mappingOffset = new QLineEdit(this); m_mappingOffset->setObjectName("mappingOffset");
    m_mappingOffset->setToolTip(QString::fromUtf8(u8"完成乘除换算后增加的数值；必须为整数，通常填零。"));
    m_mappingRounding = combo(this, {{QString::fromUtf8(u8"必须得到整数"), "exact"}, {QString::fromUtf8(u8"向下取整"), "floor"},
        {QString::fromUtf8(u8"向上取整"), "ceil"}, {QString::fromUtf8(u8"四舍五入"), "nearest"}});
    m_mappingRounding->setObjectName("mappingRounding");
    m_mappingRounding->setToolTip(QString::fromUtf8(u8"换算后有小数时如何处理；必须得到整数会拒绝有余数的输入。"));
    moreForm->addRow(QString::fromUtf8(u8"换算后增加"), m_mappingOffset);
    moreForm->addRow(QString::fromUtf8(u8"小数处理"), m_mappingRounding);
    moreContentLayout->addLayout(moreForm);
    m_mappingOmissions = new QGroupBox(QString::fromUtf8(u8"此命令不包含的字段"), this);
    m_mappingOmissions->setObjectName("mappingOmissions"); new QVBoxLayout(m_mappingOmissions);
    m_mappingOmissions->setToolTip(QString::fromUtf8(u8"勾选的参数不会从这条报文读取。例如查询报文可以没有位置参数。"));
    moreContentLayout->addWidget(m_mappingOmissions); moreLayout->addWidget(m_mappingMoreContent);
    m_mappingMoreContent->hide(); editorLayout->addWidget(m_mappingMore);
    mappingLayout->addWidget(m_mappingEditor);
    auto *mappingButtons = new QHBoxLayout;
    auto *addMapping = new QPushButton(QString::fromUtf8(u8"添加命令映射"), this);
    auto *removeMapping = new QPushButton(QString::fromUtf8(u8"删除选中映射"), this);
    auto *saveMapping = new QPushButton(QString::fromUtf8(u8"保存到草稿"), this); saveMapping->setObjectName("saveMappings");
    addMapping->setToolTip(QString::fromUtf8(u8"增加一条客户命令，在下方填写动作和参数。"));
    removeMapping->setToolTip(QString::fromUtf8(u8"删除当前选中的映射；保存前仍可重新导入配置。"));
    saveMapping->setToolTip(saveFormat->toolTip());
    mappingButtons->addWidget(addMapping); mappingButtons->addWidget(removeMapping); mappingButtons->addWidget(saveMapping);
    mappingButtons->addStretch(); mappingLayout->addLayout(mappingButtons);
    tabs->addTab(scrollPage(mappingPage), QString::fromUtf8(u8"命令映射"));

    auto *devicePage = new QWidget(this);
    auto *deviceLayout = new QVBoxLayout(devicePage);
    auto *deviceGroup = new QGroupBox(QString::fromUtf8(u8"趋盛设备参数"), this);
    auto *deviceForm = new QFormLayout(deviceGroup);
    m_destination = new QSpinBox(this); m_destination->setRange(1, 255);
    m_source = new QSpinBox(this); m_source->setRange(1, 255);
    m_devId = new QSpinBox(this); m_devId->setRange(0x11, 0x1c); m_devId->setDisplayIntegerBase(16);
    m_action = new QSpinBox(this); m_action->setRange(1, 3); m_action->setSingleStep(2);
    m_speed = new QSpinBox(this); m_speed->setRange(1, 0xfffffe);
    m_minPosition = new QSpinBox(this); m_minPosition->setRange(0, 0xfffffe);
    m_maxPosition = new QSpinBox(this); m_maxPosition->setRange(0, 0xfffffe);
    m_destination->setToolTip(QString::fromUtf8(u8"控制板地址，范围 1–255，必须与软件源地址不同。"));
    m_source->setToolTip(QString::fromUtf8(u8"软件地址，范围 1–255。"));
    m_devId->setToolTip(QString::fromUtf8(u8"电机编号 0x11–0x1C，按设备资料确认。"));
    m_action->setToolTip(QString::fromUtf8(u8"只允许 1 或 3；具体机械方向需实机确认。"));
    m_speed->setToolTip(QString::fromUtf8(u8"下位机速度数值，24 位范围且不可为零；物理单位需按实际设备确认。"));
    m_minPosition->setToolTip(QString::fromUtf8(u8"设备位置数值下限，与目标位置使用同一单位。"));
    m_maxPosition->setToolTip(QString::fromUtf8(u8"设备位置数值上限，不允许 0xFFFFFF 的连续运动值。"));
    deviceForm->addRow(QString::fromUtf8(u8"控制板目标地址"), m_destination);
    deviceForm->addRow(QString::fromUtf8(u8"软件源地址"), m_source);
    deviceForm->addRow(QString::fromUtf8(u8"电机编号（十六进制）"), m_devId);
    deviceForm->addRow(QString::fromUtf8(u8"绝对运动动作值"), m_action);
    deviceForm->addRow(QString::fromUtf8(u8"速度（下位机数值）"), m_speed);
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
    m_replyResult = new QTextEdit(this); m_replyResult->setReadOnly(true); m_replyResult->setObjectName("replyResult");
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
    connect(addMapping, &QPushButton::clicked, this, [this]() {
        if (!saveMappingEditor()) return;
        addMappingRow(); m_mappingTable->selectRow(m_mappingTable->rowCount() - 1); m_mappingEditorDirty = true; markDraftDirty();
    });
    connect(removeMapping, &QPushButton::clicked, this, [this]() {
        const int row = m_mappingTable->currentRow();
        if (row < 0) return;
        m_loadingMapping = true; m_mappingTable->removeRow(row); m_loadingMapping = false;
        m_mappingEditorRow = -1; m_mappingEditorDirty = false;
        if (m_mappingTable->rowCount()) m_mappingTable->selectRow(qMin(row, m_mappingTable->rowCount() - 1));
        loadMappingEditor(m_mappingTable->currentRow()); markDraftDirty();
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
    connect(m_mappingTable, &QTableWidget::currentCellChanged, this, [this](int row, int, int, int) {
        if (m_loadingUi || m_loadingMapping) return;
        if (!saveMappingEditor()) {
            const QSignalBlocker blocker(m_mappingTable);
            m_mappingTable->selectRow(m_mappingEditorRow); return;
        }
        loadMappingEditor(row);
    });
    for (QLineEdit *edit : {m_mappingCode, m_mappingValue, m_mappingFooter, m_mappingOffset})
        connect(edit, &QLineEdit::textChanged, this, &Widget::mappingEdited);
    connect(m_mappingMultiplier, &QLineEdit::textChanged, this, [this]() {
        if (!m_loadingMapping) m_multiplierChanged = true;
        mappingEdited();
    });
    connect(m_mappingOperation, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        if (!m_loadingMapping) {
            const QString operation = m_mappingOperation->currentData().toString();
            const bool query = isQuery(operation);
            if (query != isQuery(m_editorOperation)) {
                for (QCheckBox *check : m_mappingOmissions->findChildren<QCheckBox *>())
                    if (check->property("omissionAllowed").toBool() && check->property("fieldRole").toString() == "parameter") check->setChecked(query);
                if (query) selectData(m_mappingResponse, "ack");
                else if (m_mappingSource->currentData().toString().isEmpty() && m_mappingSource->count() > 2) m_mappingSource->setCurrentIndex(1);
            }
            m_editorOperation = operation;
        }
        updateMappingControls(); mappingEdited();
    });
    connect(m_mappingSource, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { updateMappingControls(); mappingEdited(); });
    connect(m_mappingResponse, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &Widget::mappingEdited);
    connect(m_mappingRounding, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &Widget::mappingEdited);
    connect(m_mappingDefaultFooter, &QCheckBox::toggled, this, [this]() { updateMappingControls(); mappingEdited(); });
    connect(m_mappingMore, &QGroupBox::toggled, m_mappingMoreContent, &QWidget::setVisible);
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
    connect(m_input, &QLineEdit::textChanged, this, &Widget::updateLiveState);
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
        // 相对运动先查位置、再下运动、最后监测完成；必须保留每次真实下发，不能覆盖成只剩最后一帧。
        m_targetOutput->append(QString::fromUtf8(u8"实际下发：\n") + frame.toHex(' ').toUpper());
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
                if (it.key() == "motorState") {
                    const QStringList states = {QString::fromUtf8(u8"未回零"), QString::fromUtf8(u8"空闲"),
                        QString::fromUtf8(u8"回零中"), QString::fromUtf8(u8"运行中"), QString::fromUtf8(u8"报错")};
                    const int state = it.value().toInt();
                    value += QStringLiteral("（%1）").arg(state >= 0 && state < states.size() ? states.at(state) : QString::fromUtf8(u8"未知状态"));
                }
                // 0xFFFFFF 是协议定义的无效位置标记，不能把它显示成一个可以运动到的普通终点数值。
                if ((it.key() == "positionUm" || it.key() == "feedbackPositionUm") && it.value().toLongLong() == 0xffffff)
                    value = QString::fromUtf8(u8"未回零或未配置");
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
        QDir(QCoreApplication::applicationDirPath()).filePath("examples/dt-simple-commands.json"),
        QDir(QCoreApplication::applicationDirPath()).filePath("../../examples/dt-simple-commands.json"),
        QDir::current().filePath("examples/dt-simple-commands.json")
    };
    for (const QString &path : candidates) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) continue;
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
        if (document.isObject()) {
            m_draftRoot = document.object();
            loadUiFromDraft();
            m_input->setText(QStringLiteral("/1A2000R\\r"));
            appendLog(QString::fromUtf8(u8"已载入 DT 风格单条命令验证配置，默认按数值直接转换；DT 不是程序内置协议。"));
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
                                    {QString::fromUtf8(u8"读取到帧尾"), QStringLiteral("toFooter")},
                                    {QString::fromUtf8(u8"按命令码识别"), QStringLiteral("mappedCommand")}});
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

void Widget::addMappingRow(const QJsonObject &value)
{
    QJsonObject mapping = value;
    if (mapping.isEmpty()) mapping = QJsonObject{{"commandCode", ""}, {"operation", "ABSOLUTE_MOVE"},
        {"responsePolicy", "ack"}, {"parameters", QJsonObject{{"positionUm", QJsonObject{{"kind", "field"}, {"field", ""}}}}}};
    const int row = m_mappingTable->rowCount();
    m_mappingTable->insertRow(row);
    for (int c = 0; c < 4; ++c) m_mappingTable->setItem(row, c, new QTableWidgetItem);
    updateMappingSummary(row, mapping);
}

void Widget::updateMappingSummary(int row, const QJsonObject &mapping)
{
    const QSignalBlocker blocker(m_mappingTable);
    // 列表只显示摘要，整条映射存于行数据；普通表单不能丢弃高级 JSON 的备注、输入覆盖或扩展元数据。
    m_mappingTable->item(row, 0)->setData(Qt::UserRole, mapping);
    m_mappingTable->item(row, 0)->setText(mapping.value("commandCode").toString());
    m_mappingTable->item(row, 1)->setText(operationName(mapping.value("operation").toString()));
    const bool query = isQuery(mapping.value("operation").toString());
    const QJsonObject expression = mapping.value("parameters").toObject().value(mappingParameterKey(mapping.value("operation").toString())).toObject();
    QString summary = QString::fromUtf8(u8"无参数");
    if (!query) {
        if (expression.value("kind").toString() == "constant") summary = QString::fromUtf8(u8"固定数值 %1").arg(expression.value("value").toVariant().toString());
        else summary = expression.value("field").toString() + (expression.value("kind").toString() == "linear"
                     ? QString::fromUtf8(u8" × %1").arg(multiplierText(expression)) : QString::fromUtf8(u8"（直接使用）"));
        if (expression.value("offset").toDouble() != 0 || expression.value("round").toString("exact") != "exact")
            summary += QString::fromUtf8(u8"（含更多设置）");
    }
    m_mappingTable->item(row, 2)->setText(summary);
    m_mappingTable->item(row, 3)->setText(query ? QString::fromUtf8(u8"收到查询回告后") :
        mapping.value("responsePolicy").toString() == "complete" ? QString::fromUtf8(u8"动作完成后") : QString::fromUtf8(u8"设备接纳命令后"));
    for (int c = 0; c < 4; ++c) m_mappingTable->item(row, c)->setToolTip(m_mappingTable->horizontalHeaderItem(c)->toolTip());
}

void Widget::mappingEdited()
{
    if (m_loadingMapping || m_loadingUi || m_mappingEditorRow < 0) return;
    m_mappingEditorDirty = true; markDraftDirty();
}

void Widget::loadMappingEditor(int row)
{
    m_loadingMapping = true; m_mappingEditorRow = row;
    m_mappingEditor->setEnabled(row >= 0);
    const QJsonObject mapping = row >= 0 ? m_mappingTable->item(row, 0)->data(Qt::UserRole).toJsonObject() : QJsonObject();
    m_mappingCode->setText(mapping.value("commandCode").toString());
    const QString operation = mapping.value("operation").toString("ABSOLUTE_MOVE");
    m_editorOperation = operation;
    if (m_mappingOperation->findData(operation) < 0) m_mappingOperation->addItem(operationName(operation), operation);
    selectData(m_mappingOperation, operation);
    selectData(m_mappingResponse, mapping.value("responsePolicy").toString("ack"));
    const QJsonObject expression = mapping.value("parameters").toObject().value(mappingParameterKey(operation)).toObject();
    m_mappingValue->setText(expression.value("value").toVariant().toString());
    m_mappingMultiplier->setText(expression.value("kind").toString() == "linear" ? multiplierText(expression) : QStringLiteral("1"));
    m_mappingOffset->setText(expression.value("offset").toVariant().toString().isEmpty() ? "0" : expression.value("offset").toVariant().toString());
    selectData(m_mappingRounding, expression.value("round").toString("exact"));
    const QJsonObject input = mapping.value("input").toObject();
    m_mappingDefaultFooter->setChecked(!input.contains("footer"));
    m_mappingFooter->setText(input.value("footer").toString());
    auto *omitLayout = m_mappingOmissions->layout();
    while (QLayoutItem *item = omitLayout->takeAt(0)) { delete item->widget(); delete item; }
    QStringList omitted;
    for (const QJsonValue &field : input.value("omitFields").toArray()) omitted << field.toString();
    QStringList availableNames;
    bool commandSeen = false;
    for (int r = 0; r < m_fieldTable->rowCount(); ++r) {
        if (comboData(m_fieldTable, r, 2) == "command") commandSeen = true;
        else if (commandSeen) availableNames << itemText(m_fieldTable, r, 1);
    }
    QStringList all = availableNames;
    for (const QString &name : omitted) if (!all.contains(name)) all << name;
    for (const QString &name : all) {
        const bool available = availableNames.contains(name);
        auto *check = new QCheckBox(available ? name : QString::fromUtf8(u8"不可省略或未找到：%1").arg(name), m_mappingOmissions);
        check->setProperty("fieldName", name); check->setChecked(omitted.contains(name));
        check->setProperty("omissionAllowed", available);
        for (int r = 0; r < m_fieldTable->rowCount(); ++r)
            if (itemText(m_fieldTable, r, 1) == name) check->setProperty("fieldRole", comboData(m_fieldTable, r, 2));
        check->setEnabled(available);
        // 命令码前的公共字段决定识别位置，不能因某条映射省略它们而改变匹配规则。
        check->setToolTip(available ? QString::fromUtf8(u8"勾选后，这条命令的报文不读取此字段。")
            : QString::fromUtf8(u8"命令码及其前面的公共字段不能省略；失效配置保留供校验提示。")); omitLayout->addWidget(check);
        connect(check, &QCheckBox::toggled, this, [this]() { updateMappingSources(); mappingEdited(); });
    }
    updateMappingSources();
    const QString source = expression.value("kind").toString() == "constant" ? QStringLiteral("@constant") : expression.value("field").toString();
    if (!source.isEmpty() && m_mappingSource->findData(source) < 0) m_mappingSource->addItem(QString::fromUtf8(u8"未找到字段：%1").arg(source), source);
    selectData(m_mappingSource, source);
    updateMappingControls(); m_mappingEditorDirty = false; m_multiplierChanged = false; m_loadingMapping = false;
}

void Widget::updateMappingSources()
{
    const QSignalBlocker blocker(m_mappingSource);
    const QString selected = m_mappingSource->currentData().toString();
    QStringList omitted;
    for (QCheckBox *check : m_mappingOmissions->findChildren<QCheckBox *>())
        if (check->isChecked()) omitted << check->property("fieldName").toString();
    m_mappingSource->clear(); m_mappingSource->addItem(QString::fromUtf8(u8"请选择字段"), "");
    for (int r = 0; r < m_fieldTable->rowCount(); ++r) {
        const QString name = itemText(m_fieldTable, r, 1);
        if (comboData(m_fieldTable, r, 2) == "parameter" && comboData(m_fieldTable, r, 3) != "ascii" && !name.isEmpty() && !omitted.contains(name))
            m_mappingSource->addItem(name, name);
    }
    m_mappingSource->addItem(QString::fromUtf8(u8"固定值"), "@constant");
    // 删除、改名、改为文本或省略字段后保留失效引用，让校验指出问题，不能自动换成另一个参数。
    if (!selected.isEmpty() && m_mappingSource->findData(selected) < 0)
        m_mappingSource->addItem(QString::fromUtf8(u8"未找到可用字段：%1").arg(selected), selected);
    selectData(m_mappingSource, selected);
}

void Widget::updateMappingControls()
{
    const bool query = isQuery(m_mappingOperation->currentData().toString());
    const bool constant = m_mappingSource->currentData().toString() == "@constant";
    auto visibility = [](QWidget *control, bool show) {
        control->setVisible(show);
        if (auto *form = qobject_cast<QFormLayout *>(control->parentWidget()->layout()))
            if (QWidget *label = form->labelForField(control)) label->setVisible(show);
    };
    // QFormLayout 放在 group 的纵向布局里；标签按其实际所属表单定位，避免隐藏控件后留下空英文/技术标签。
    for (auto *control : {static_cast<QWidget *>(m_mappingSource), static_cast<QWidget *>(m_mappingValue),
                         static_cast<QWidget *>(m_mappingMultiplier), static_cast<QWidget *>(m_mappingResponse)}) {
        bool show = !query;
        if (control == m_mappingValue) show &= constant;
        if (control == m_mappingMultiplier) show &= !constant;
        visibility(control, show);
        auto *layout = m_mappingEditor->layout();
        for (int i = 0; i < layout->count(); ++i) if (auto *form = qobject_cast<QFormLayout *>(layout->itemAt(i)->layout()))
            if (QWidget *label = form->labelForField(control)) {
                label->setVisible(show);
                if (control == m_mappingSource) if (auto *text = qobject_cast<QLabel *>(label))
                    text->setText(mappingParameterKey(m_mappingOperation->currentData().toString()) == "distanceUm"
                        ? QString::fromUtf8(u8"距离来源") : QString::fromUtf8(u8"位置来源"));
            }
    }
    m_mappingOffset->setEnabled(!query && !constant); m_mappingRounding->setEnabled(!query && !constant);
    m_mappingFooter->setVisible(!m_mappingDefaultFooter->isChecked());
    const QString target = mappingParameterKey(m_mappingOperation->currentData().toString()) == "distanceUm"
            ? QString::fromUtf8(u8"移动距离（数值）") : QString::fromUtf8(u8"目标位置（数值）");
    m_mappingParameterHint->setText(query ? QString::fromUtf8(u8"无参数；收到设备回告后显示结果。请核对此命令帧尾。") :
        constant ? target + QString::fromUtf8(u8"使用固定值。") : target + QString::fromUtf8(u8" = 客户字段 × 换算倍数（默认一倍）。"));
}

bool Widget::saveMappingEditor()
{
    if (m_mappingEditorRow < 0 || !m_mappingEditorDirty) return true;
    auto fail = [this](const QString &reason) { m_validationState->setText(QString::fromUtf8(u8"保存失败：") + reason); appendLog(m_validationState->text()); return false; };
    QJsonObject mapping = m_mappingTable->item(m_mappingEditorRow, 0)->data(Qt::UserRole).toJsonObject();
    if (m_mappingCode->text().trimmed().isEmpty()) return fail(QString::fromUtf8(u8"请填写客户命令。"));
    mapping.insert("commandCode", m_mappingCode->text().trimmed());
    const QString previousOperation = mapping.value("operation").toString();
    const QString operation = m_mappingOperation->currentData().toString(); mapping.insert("operation", operation);
    mapping.insert("responsePolicy", isQuery(operation) ? "ack" : m_mappingResponse->currentData().toString());
    QJsonObject parameters = mapping.value("parameters").toObject();
    if (isQuery(operation)) parameters = QJsonObject();
    else {
        const QString parameterKey = mappingParameterKey(operation);
        const QString previousKey = mappingParameterKey(previousOperation);
        QJsonObject expression = parameters.value(previousKey).toObject();
        if (previousKey != parameterKey) parameters.remove(previousKey);
        const QString source = m_mappingSource->currentData().toString();
        if (source == "@constant") {
            bool ok = false; const qlonglong value = m_mappingValue->text().trimmed().toLongLong(&ok);
            if (!ok || value < 0 || value > 0xfffffe) return fail(QString::fromUtf8(u8"固定数值必须填写零到 16777214 的整数。"));
            expression.insert("kind", "constant"); expression.insert("value", double(value));
            expression.remove("field"); for (const QString &key : {"numerator", "denominator", "offset", "round"}) expression.remove(key);
        } else {
            if (source.isEmpty()) return fail(QString::fromUtf8(u8"请选择位置来源字段。"));
            const bool wasLinear = expression.value("kind").toString() == "linear";
            expression.insert("field", source); expression.remove("value");
            // 未修改倍数时保留原分数；改命令名或反馈时机不能让高级关系中的 1/3、偏移、舍入丢失。
            qlonglong numerator = 1, denominator = 1;
            if (m_multiplierChanged || !wasLinear) {
                if (!decimalRatio(m_mappingMultiplier->text(), &numerator, &denominator))
                    return fail(QString::fromUtf8(u8"换算倍数请填写有效整数或最多九位小数；分数请在高级 JSON 中编辑。"));
            } else { numerator = expression.value("numerator").toVariant().toLongLong(); denominator = expression.value("denominator").toVariant().toLongLong(); }
            bool ok = false; const qlonglong offset = m_mappingOffset->text().trimmed().toLongLong(&ok);
            if (!ok || offset < -1000000000LL || offset > 1000000000LL) return fail(QString::fromUtf8(u8"换算后增加值必须填写十亿以内的整数。"));
            const QString rounding = m_mappingRounding->currentData().toString();
            if (!wasLinear && numerator == denominator && offset == 0 && rounding == "exact") {
                expression.insert("kind", "field");
                for (const QString &key : {"numerator", "denominator", "offset", "round"}) expression.remove(key);
            } else {
                expression.insert("kind", "linear"); expression.insert("numerator", double(numerator)); expression.insert("denominator", double(denominator));
                expression.insert("offset", double(offset)); expression.insert("round", rounding);
            }
        }
        parameters.insert(parameterKey, expression);
    }
    mapping.insert("parameters", parameters);
    QJsonObject input = mapping.value("input").toObject();
    QJsonArray omitted;
    for (QCheckBox *check : m_mappingOmissions->findChildren<QCheckBox *>()) if (check->isChecked()) omitted.append(check->property("fieldName").toString());
    if (omitted.isEmpty()) input.remove("omitFields"); else input.insert("omitFields", omitted);
    if (m_mappingDefaultFooter->isChecked()) input.remove("footer");
    else { if (m_mappingFooter->text().isEmpty()) return fail(QString::fromUtf8(u8"此命令帧尾不能为空。")); input.insert("footer", m_mappingFooter->text()); }
    if (input.isEmpty()) mapping.remove("input"); else mapping.insert("input", input);
    updateMappingSummary(m_mappingEditorRow, mapping); m_mappingEditorDirty = false; m_multiplierChanged = false;
    return true;
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
    m_mappingEditorRow = -1; m_mappingEditorDirty = false;
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
    if (m_mappingTable->rowCount()) m_mappingTable->selectRow(0);
    loadMappingEditor(m_mappingTable->currentRow());
    updateTableControls();
    updateLiveState();
}

bool Widget::saveUiToDraft()
{
    if (m_jsonDirty) {
        appendLog(QString::fromUtf8(u8"请先应用高级 JSON 到草稿，避免表单覆盖尚未应用的 JSON。"));
        return false;
    }
    if (!saveMappingEditor()) return false;
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
    for (int row = 0; row < m_mappingTable->rowCount(); ++row)
        mappings.append(m_mappingTable->item(row, 0)->data(Qt::UserRole).toJsonObject());
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
    for (int row = 0; row < m_fieldTable->rowCount(); ++row) {
        auto *mode = qobject_cast<QComboBox *>(m_fieldTable->cellWidget(row, 4));
        if (auto *model = qobject_cast<QStandardItemModel *>(mode->model())) {
            const int index = mode->findData("mappedCommand");
            if (index >= 0) model->item(index)->setEnabled(m_encoding->currentData().toString() == "ascii"
                && comboData(m_fieldTable, row, 2) == "command" && comboData(m_fieldTable, row, 3) == "ascii");
        }
    }
    updateMappingSources();

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
    const bool recoveryQuery = m_requests->deviceBusy() && m_configEnabled && current && inputIsQuery();
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
    else if (m_requests->deviceBusy() && !recoveryQuery)
        reason = QString::fromUtf8(u8"真实发送不可用：设备仍在运动或最终状态未知；可输入查询报文确认状态。");
    else if (recoveryQuery)
        reason = QString::fromUtf8(u8"可发送当前查询报文；设备状态恢复前不能发送运动命令。");
    else reason = QString::fromUtf8(u8"真实发送条件已满足。");
    const bool ready = m_configEnabled && current && m_deviceConfirmed->isChecked()
            && m_transport->isConnected() && !m_requestActive && (!m_requests->deviceBusy() || recoveryQuery);
    m_sendButton->setEnabled(ready);
    m_sendButton->setToolTip(reason);
    m_liveReason->setText(reason);
    m_liveReason->setStyleSheet(ready ? QStringLiteral("color:#087a32") : QStringLiteral("color:#a04400"));
    m_cancelButton->setEnabled(m_requestActive);
    m_queryButton->setEnabled(m_configEnabled && current && m_deviceConfirmed->isChecked()
                             && m_transport->isConnected() && !m_requestActive);
}

bool Widget::inputIsQuery() const
{
    // 恢复查询必须由当前已启用配置合法解析，不能凭输入字符串看起来像某种查询便绕过运动互锁。
    TranslationError error;
    const QByteArray bytes = inputBytes(m_activeConfig, &error);
    if (error.isValid()) return false;
    ConversionPreview result;
    return ConversionEngine(m_activeConfig).convert(bytes, false, &result, &error) && isQuery(result.command.operation);
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
    lines << QString::fromUtf8(u8"统一命令：操作=%1，目标电机编号=%2，响应方式=%3，配置版本=%4")
             .arg(operationName(preview.command.operation), preview.command.targetDevice,
                  preview.command.responsePolicy == "ack" ? QString::fromUtf8(u8"命令应答后返回") : QString::fromUtf8(u8"动作完成后返回"),
                  preview.command.configVersion.left(12));
    for (auto it = preview.command.parameters.begin(); it != preview.command.parameters.end(); ++it)
        // 模型继续携带协议单位供后续标定使用；本轮界面按用户确认只展示直接传递的数值。
        lines << QStringLiteral("  %1 = %2").arg(displayName(it.key()), it.value().toString());
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
                || m_requestActive) {
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
    if (live && m_requests->deviceBusy() && !isQuery(converted.command.operation)) {
        updateLiveState(); appendLog(m_liveReason->text()); return;
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
