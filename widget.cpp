#include "widget.h"

#include "src/core/requestmanager.h"
#include "src/core/translationservice.h"
#include "src/qusheng/qushengprotocol.h"
#include "src/qusheng/qushengcommandcatalog.h"
#include "src/qusheng/sdk/qushengsdk.h"
#include "src/transport/serialtransport.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QListView>
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
#include <QSet>
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
        {"deviceBusy", QString::fromUtf8(u8"设备忙闲状态")}, {"devId", QString::fromUtf8(u8"设备编号")},
        {"heartbeat", QString::fromUtf8(u8"心跳")}, {"errors", QString::fromUtf8(u8"故障记录")},
        {"voltageMv", QString::fromUtf8(u8"电压")},
        {"faultCode", QString::fromUtf8(u8"故障代码")}, {"raw", QString::fromUtf8(u8"原始数据")},
        {"inputMask", QString::fromUtf8(u8"输入位图")}, {"configurationRaw", QString::fromUtf8(u8"配置原始数据")},
        {"flowControl", QString::fromUtf8(u8"流控")}, {"parity", QString::fromUtf8(u8"校验位")},
        {"stopBits", QString::fromUtf8(u8"停止位")}, {"baudrate", QString::fromUtf8(u8"波特率")},
        {"canBaudrateIndex", QString::fromUtf8(u8"CAN速率编号")}, {"ip", QString::fromUtf8(u8"本机地址")},
        {"subnetMask", QString::fromUtf8(u8"子网掩码")}, {"gateway", QString::fromUtf8(u8"网关")},
        {"dns", QString::fromUtf8(u8"域名服务器")}, {"serverIp", QString::fromUtf8(u8"服务器地址")},
        {"serverPort", QString::fromUtf8(u8"服务器端口")}};
    if (names.contains(key)) return names.value(key);
    for (const QJsonValue &command : QushengCommandCatalog::commands())
        for (const QJsonValue &parameter : command.toObject().value("parameters").toArray())
            if (parameter.toObject().value("key").toString() == key) return parameter.toObject().value("label").toString();
    return key;
}

QString displayValue(const QString &key, const QVariant &value)
{
    if (value.type() == QVariant::ByteArray) return value.toByteArray().toHex(' ').toUpper();
    if (value.type() == QVariant::List) {
        QStringList records;
        for (const QVariant &record : value.toList()) records << displayValue(QString(), record);
        return records.join(QString::fromUtf8(u8"；"));
    }
    if (value.type() == QVariant::Map) {
        QStringList fields; const QVariantMap record = value.toMap();
        for (auto it = record.begin(); it != record.end(); ++it)
            fields << QStringLiteral("%1 = %2").arg(displayName(it.key()), displayValue(it.key(), it.value()));
        return fields.join(QString::fromUtf8(u8"，"));
    }
    if (key == "deviceBusy") return value.toBool() ? QString::fromUtf8(u8"运动中或状态未知") : QString::fromUtf8(u8"空闲");
    if (key == "heartbeat") return value.toBool() ? QString::fromUtf8(u8"已收到") : QString::fromUtf8(u8"未收到");
    if (key == "motorState") {
        const QStringList states = {QString::fromUtf8(u8"未回零"), QString::fromUtf8(u8"空闲"),
            QString::fromUtf8(u8"回零中"), QString::fromUtf8(u8"运行中"), QString::fromUtf8(u8"报错")};
        const int state = value.toInt();
        return value.toString() + QStringLiteral(" (%1)").arg(state >= 0 && state < states.size() ? states.at(state) : QString::fromUtf8(u8"未知状态"));
    }
    if ((key == "positionUm" || key == "feedbackPositionUm") && value.toLongLong() == 0xffffff)
        return QString::fromUtf8(u8"未回零或未配置");
    return value.toString();
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
    const QString name = QushengCommandCatalog::name(operation);
    return name == operation ? QString::fromUtf8(u8"未找到指令：%1").arg(operation) : name;
}

bool isQuery(const QString &operation)
{
    return operation.startsWith("QUERY_") || QushengCommandCatalog::find(operation).value("query").toBool();
}

bool isMotionEditor(const QString &operation)
{
    return operation == "ABSOLUTE_MOVE" || operation == "RELATIVE_FORWARD" || operation == "RELATIVE_BACKWARD"
        || operation == "MOTOR_FORWARD" || operation == "MOTOR_BACKWARD";
}

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

Widget::Widget(QWidget *parent, const QString &draftFilePath)
    : QWidget(parent),
      m_protocol(new QushengProtocol(this)),
      m_transport(new SerialTransport(this)),
      m_requests(new RequestManager(m_protocol, m_transport, this)),
      m_service(new TranslationService(m_requests, this)),
      m_draftStore(draftFilePath)
{
    buildUi();
    connectSignals();
    refreshPorts();
    loadInitialDraft();
    filterMappingCommands();
    updateConnectionUi(false, QString::fromUtf8(u8"下游串口未连接"));
}

void Widget::buildUi()
{
    setWindowTitle(QString::fromUtf8(u8"配置驱动协议转换（报文调试版）"));
    resize(1100, 740);
    auto *root = new QVBoxLayout(this);
    auto *versions = new QHBoxLayout;
    m_draftVersion = new QLabel(QString::fromUtf8(u8"草稿：未加载"), this);
    m_activeVersion = new QLabel(QString::fromUtf8(u8"启用：无"), this);
    m_validationState = new QLabel(QString::fromUtf8(u8"校验：待校验"), this);
    m_draftVersion->setObjectName("draftVersion"); m_activeVersion->setObjectName("activeVersion");
    m_validationState->setObjectName("validationState"); m_validationState->setWordWrap(true);
    versions->addWidget(m_draftVersion);
    versions->addWidget(m_activeVersion);
    versions->addWidget(m_validationState, 1);
    root->addLayout(versions);

    auto *tabs = new QTabWidget(this); tabs->setObjectName("mainTabs");
    root->addWidget(tabs, 1);
    tabs->setMinimumHeight(260);

    auto *formatPage = new QWidget(this);
    auto *formatLayout = new QVBoxLayout(formatPage);
    auto *boundaryForm = new QFormLayout;
    m_encoding = combo(this, {{QString::fromUtf8(u8"ASCII 文本"), QStringLiteral("ascii")},
                              {QString::fromUtf8(u8"HEX 字节"), QStringLiteral("hex")}});
    m_encoding->setToolTip(QString());
    m_header = new QLineEdit(this); m_header->setObjectName("formatHeader");
    m_header->setToolTip(QString());
    m_footer = new QLineEdit(this); m_footer->setObjectName("formatFooter");
    m_footer->setToolTip(QString::fromUtf8(u8"完整报文必须以此内容结束；ASCII 模式可填写 R\\r。"));
    m_byteOrderLabel = new QLabel(QString::fromUtf8(u8"多字节整数"), this);
    m_byteOrder = combo(this, {{QString::fromUtf8(u8"大端（高字节在前）"), QStringLiteral("big")},
                               {QString::fromUtf8(u8"小端（低字节在前）"), QStringLiteral("little")}});
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
    m_fieldTable->setToolTip(QString());
    const QStringList fieldTips = {
        QString::fromUtf8(u8"报文有效载荷中的读取顺序"),
        QString::fromUtf8(u8"字段唯一名称，供命令映射引用"),
        QString::fromUtf8(u8"命令码、参数或固定字段。"),
        QString::fromUtf8(u8"ASCII 原样读取；整数按编码方式解析"),
        QString::fromUtf8(u8"顺序读取；命令码可按配置匹配。"),
        QString::fromUtf8(u8"固定长度的字节数"),
        QString::fromUtf8(u8"固定字段期望值"),
        QString::fromUtf8(u8"可选的输入下限"),
        QString::fromUtf8(u8"可选的输入上限")};
    for (int column = 0; column < fieldTips.size(); ++column)
        m_fieldTable->horizontalHeaderItem(column)->setToolTip(fieldTips.at(column));
    formatLayout->addWidget(m_fieldTable);
    auto *fieldButtons = new QHBoxLayout;
    auto *addField = new QPushButton(QString::fromUtf8(u8"添加字段"), this);
    addField->setObjectName("addField");
    auto *removeField = new QPushButton(QString::fromUtf8(u8"删除选中字段"), this);
    removeField->setObjectName("removeField");
    auto *fieldUp = new QPushButton(QString::fromUtf8(u8"上移"), this);
    fieldUp->setObjectName("fieldUp");
    auto *fieldDown = new QPushButton(QString::fromUtf8(u8"下移"), this);
    fieldDown->setObjectName("fieldDown");
    auto *saveFormat = new QPushButton(QString::fromUtf8(u8"保存到草稿"), this);
    saveFormat->setObjectName("saveFields");
    addField->setToolTip(QString::fromUtf8(u8"在字段表末尾增加一行。"));
    removeField->setToolTip(QString::fromUtf8(u8"删除当前选中的字段行。"));
    fieldUp->setToolTip(QString::fromUtf8(u8"把选中字段提前一个读取顺序。"));
    fieldDown->setToolTip(QString::fromUtf8(u8"把选中字段推后一个读取顺序。"));
    saveFormat->setToolTip(QString::fromUtf8(u8"保存编辑，需另行校验和启用。"));
    for (auto *button : {addField, removeField, fieldUp, fieldDown, saveFormat}) fieldButtons->addWidget(button);
    fieldButtons->addStretch();
    formatLayout->addLayout(fieldButtons);
    tabs->addTab(scrollPage(formatPage), QString::fromUtf8(u8"客户协议格式"));

    auto *mappingPage = new QWidget(this);
    auto *mappingLayout = new QVBoxLayout(mappingPage);
    m_mappingTable = new QTableWidget(this);
    m_mappingTable->setObjectName("mappingTable");
    m_mappingTable->setColumnCount(3);
    m_mappingTable->setHorizontalHeaderLabels({QString::fromUtf8(u8"客户命令"), QString::fromUtf8(u8"执行动作"),
        QString::fromUtf8(u8"参数说明")});
    m_mappingTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_mappingTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_mappingTable->verticalHeader()->hide();
    m_mappingTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_mappingTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_mappingTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_mappingTable->setMinimumHeight(160);
    m_mappingTable->setToolTip(QString::fromUtf8(u8"选择映射后可删除；新增请填写下方表单。"));
    const QStringList mappingTips = {QString::fromUtf8(u8"客户报文中的命令码，支持不同长度"),
        QString::fromUtf8(u8"选择趋盛命令模板"), QString::fromUtf8(u8"来源字段、换算倍数或固定值")};
    for (int c = 0; c < mappingTips.size(); ++c) m_mappingTable->horizontalHeaderItem(c)->setToolTip(mappingTips.at(c));
    mappingLayout->addWidget(m_mappingTable, 1);

    m_mappingEditor = new QGroupBox(QString::fromUtf8(u8"添加新映射"), this);
    m_mappingEditor->setObjectName("mappingEditor");
    auto *editorLayout = new QVBoxLayout(m_mappingEditor);
    auto *editorForm = new QFormLayout;
    m_mappingCode = new QLineEdit(this); m_mappingCode->setObjectName("mappingCode");
    m_mappingCode->setToolTip(QString::fromUtf8(u8"填写客户命令码本身；不要填写地址、帧头或帧尾。"));
    m_mappingSearch = new QLineEdit(this); m_mappingSearch->setObjectName("mappingSearch");
    m_mappingSearch->setPlaceholderText(QString::fromUtf8(u8"CMD / 名称"));
    m_mappingSearch->setToolTip(QString::fromUtf8(u8"筛选指令。"));
    m_mappingOperation = new QComboBox(this); m_mappingOperation->setObjectName("mappingOperation");
    m_mappingOperation->setView(new QListView(m_mappingOperation));
    m_mappingOperation->setMaxVisibleItems(12);
    for (const QJsonValue &value : QushengCommandCatalog::options()) {
        const QJsonObject entry = value.toObject(); const QString op = entry.value("operation").toString();
        const QString status = QushengCommandCatalog::isReceiveOnly(op) ? QString::fromUtf8(u8"仅接收")
            : QushengCommandCatalog::isExecutable(op) ? QString() : QString::fromUtf8(u8"待实现");
        // JSON 中文名称由 JSON 解码器读取，状态文案显式按 UTF-8 转换。
        // QStringLiteral 的非 ASCII 标点还会经过编译器输入字符集和 Qt 宏，某些构建会产生坏码点；
        // 这里仅用 ASCII 拼接格式，保留中文名称。换字体不能修复已经解错的字符。
        m_mappingOperation->addItem(QStringLiteral("%1 | %2").arg(entry.value("code").toString(), entry.value("name").toString())
            + (status.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(status)), op);
        m_mappingOperation->setItemData(m_mappingOperation->count() - 1, entry.value("name").toString(), Qt::ToolTipRole);
        if (QushengCommandCatalog::isReceiveOnly(op))
            qobject_cast<QStandardItemModel *>(m_mappingOperation->model())->item(m_mappingOperation->count() - 1)->setEnabled(false);
    }
    auto *searchLine = new QHBoxLayout; searchLine->addWidget(m_mappingSearch);
    m_mappingParameters = new QTableWidget(this); m_mappingParameters->setObjectName("mappingParameters");
    m_mappingParameters->setColumnCount(3);
    m_mappingParameters->setHorizontalHeaderLabels({QString::fromUtf8(u8"趋盛参数"), QString::fromUtf8(u8"取值来源"), QString::fromUtf8(u8"固定值")});
    m_mappingParameters->verticalHeader()->hide(); m_mappingParameters->setMinimumHeight(100); m_mappingParameters->setMaximumHeight(190);
    m_mappingParameters->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_mappingParameters->horizontalHeaderItem(0)->setToolTip(QString::fromUtf8(u8"趋盛参数。"));
    m_mappingParameters->horizontalHeaderItem(1)->setToolTip(QString::fromUtf8(u8"客户字段或固定值。"));
    m_mappingParameters->horizontalHeaderItem(2)->setToolTip(QString::fromUtf8(u8"仅固定值需要填写整数；不会自动填零。"));
    connect(m_mappingSearch, &QLineEdit::textChanged, this, &Widget::filterMappingCommands);
    m_mappingSource = new QComboBox(this); m_mappingSource->setObjectName("mappingSource");
    m_mappingSource->setToolTip(QString::fromUtf8(u8"选择客户报文中的数字参数，或使用固定值。"));
    m_mappingValue = new QLineEdit(this); m_mappingValue->setObjectName("mappingValue");
    m_mappingValue->setToolTip(QString::fromUtf8(u8"固定整数。"));
    m_mappingMultiplier = new QLineEdit(this); m_mappingMultiplier->setObjectName("mappingMultiplier");
    m_mappingMultiplier->setToolTip(QString::fromUtf8(u8"客户字段乘以此倍数。"));
    editorForm->addRow(QString::fromUtf8(u8"客户命令"), m_mappingCode);
    editorForm->addRow(QString::fromUtf8(u8"查找"), searchLine);
    editorForm->addRow(QString::fromUtf8(u8"趋盛指令"), m_mappingOperation);
    editorForm->addRow(QString::fromUtf8(u8"位置来源"), m_mappingSource);
    editorForm->addRow(QString::fromUtf8(u8"固定数值"), m_mappingValue);
    editorForm->addRow(QString::fromUtf8(u8"换算倍数"), m_mappingMultiplier);
    editorLayout->addLayout(editorForm);
    editorLayout->addWidget(m_mappingParameters);
    m_mappingOmissions = new QGroupBox(QString::fromUtf8(u8"省略字段"), this);
    m_mappingOmissions->setObjectName("mappingOmissions");
    auto *omissionLayout = new QHBoxLayout(m_mappingOmissions); omissionLayout->setContentsMargins(8, 2, 8, 2);
    editorLayout->addWidget(m_mappingOmissions);
    mappingLayout->addWidget(m_mappingEditor);
    auto *mappingButtons = new QHBoxLayout;
    auto *addMapping = new QPushButton(QString::fromUtf8(u8"添加命令映射"), this);
    addMapping->setObjectName("addMapping");
    auto *removeMapping = new QPushButton(QString::fromUtf8(u8"删除选中映射"), this);
    removeMapping->setObjectName("removeMapping");
    auto *saveMapping = new QPushButton(QString::fromUtf8(u8"保存到草稿"), this); saveMapping->setObjectName("saveMappings");
    addMapping->setToolTip(QString::fromUtf8(u8"将下方填写的新映射添加到表格末尾。"));
    removeMapping->setToolTip(QString::fromUtf8(u8"删除当前选中的映射；保存前仍可重新导入配置。"));
    saveMapping->setToolTip(saveFormat->toolTip());
    mappingButtons->addWidget(addMapping); mappingButtons->addWidget(removeMapping); mappingButtons->addWidget(saveMapping);
    removeMapping->setEnabled(false);
    // 保存入口放在列表旁，常用编辑在同一页完成。
    mappingButtons->addStretch(); mappingLayout->insertLayout(1, mappingButtons);
    tabs->addTab(scrollPage(mappingPage), QString::fromUtf8(u8"命令映射"));

    auto *devicePage = new QWidget(this);
    auto *deviceLayout = new QVBoxLayout(devicePage);
    auto *deviceGroup = new QGroupBox(QString::fromUtf8(u8"趋盛设备参数"), this);
    auto *deviceForm = new QFormLayout(deviceGroup);
    m_destination = new QSpinBox(this); m_destination->setRange(1, 255);
    m_source = new QSpinBox(this); m_source->setRange(1, 255);
    m_devId = new QSpinBox(this); m_devId->setRange(0x11, 0x1c); m_devId->setDisplayIntegerBase(16);
    m_action = new QSpinBox(this); m_action->setObjectName("legacyAbsoluteAction"); m_action->setRange(1, 3); m_action->setSingleStep(2);
    m_speed = new QSpinBox(this); m_speed->setObjectName("defaultMotorSpeed"); m_speed->setRange(1, 0xfffffe);
    m_minPosition = new QSpinBox(this); m_minPosition->setRange(0, 0xfffffe);
    m_maxPosition = new QSpinBox(this); m_maxPosition->setRange(0, 0xfffffe);
    m_destination->setToolTip(QString::fromUtf8(u8"控制板地址，范围 1–255，必须与软件源地址不同。"));
    m_source->setToolTip(QString::fromUtf8(u8"软件地址，范围 1–255。"));
    m_devId->setToolTip(QString::fromUtf8(u8"电机编号 0x11–0x1C，按设备资料确认。"));
    m_action->setToolTip(QString());
    m_speed->setToolTip(QString());
    m_minPosition->setToolTip(QString::fromUtf8(u8"设备位置数值下限，与目标位置使用同一单位。"));
    m_maxPosition->setToolTip(QString::fromUtf8(u8"设备位置数值上限，不允许 0xFFFFFF 的连续运动值。"));
    deviceForm->addRow(QString::fromUtf8(u8"控制板目标地址"), m_destination);
    deviceForm->addRow(QString::fromUtf8(u8"软件源地址"), m_source);
    deviceForm->addRow(QString::fromUtf8(u8"电机编号（十六进制）"), m_devId);
    deviceForm->addRow(QString::fromUtf8(u8"旧版绝对映射方向"), m_action);
    deviceForm->addRow(QString::fromUtf8(u8"默认运动速度"), m_speed);
    deviceForm->addRow(QString::fromUtf8(u8"最小位置"), m_minPosition);
    deviceForm->addRow(QString::fromUtf8(u8"最大位置"), m_maxPosition);
    auto *saveDevice = new QPushButton(QString::fromUtf8(u8"保存到草稿"), this);
    saveDevice->setObjectName("saveDevice");
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
    m_baud->setToolTip(QString());
    for (int rate : {9600, 19200, 38400, 57600, 115200})
        m_baud->addItem(QString::number(rate), rate);
    m_baud->setCurrentIndex(m_baud->findData(115200));
    m_connectButton = new QPushButton(QString::fromUtf8(u8"连接"), this);
    m_connectButton->setObjectName("connectButton");
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
    m_deviceConfirmed = new QCheckBox(QString::fromUtf8(u8"参数已确认"), this);
    m_deviceConfirmed->setObjectName("deviceConfirmed");
    m_deviceConfirmed->setToolTip(QString::fromUtf8(
        u8"该确认只对当前运行会话有效；配置或设备参数变化后必须重新确认。"));
    deviceLayout->addWidget(m_deviceConfirmed);
    deviceLayout->addStretch();
    tabs->addTab(scrollPage(devicePage), QString::fromUtf8(u8"设备与连接"));

    auto *advancedPage = new QWidget(this);
    auto *advancedLayout = new QVBoxLayout(advancedPage);
    auto *advancedForm = new QFormLayout;
    advancedForm->addRow(m_byteOrderLabel, m_byteOrder);
    advancedLayout->addLayout(advancedForm);
    m_advancedJson = new QTextEdit(this);
    m_advancedJson->setObjectName("advancedJson");
    m_advancedJson->setFontFamily(QStringLiteral("Consolas"));
    advancedLayout->addWidget(m_advancedJson);
    auto *applyJson = new QPushButton(QString::fromUtf8(u8"应用到草稿"), this);
    applyJson->setObjectName("applyJson");
    applyJson->setToolTip(QString::fromUtf8(u8"解析当前 JSON 并刷新中文表单；不会自动启用。"));
    advancedLayout->addWidget(applyJson);
    tabs->addTab(scrollPage(advancedPage), QString::fromUtf8(u8"高级配置"));

    auto *configButtons = new QHBoxLayout;
    auto *importButton = new QPushButton(QString::fromUtf8(u8"导入配置"), this);
    importButton->setObjectName("importConfig");
    auto *exportButton = new QPushButton(QString::fromUtf8(u8"导出草稿"), this);
    exportButton->setObjectName("exportConfig");
    auto *validateButton = new QPushButton(QString::fromUtf8(u8"校验配置"), this);
    validateButton->setObjectName("validateConfig");
    auto *enableButton = new QPushButton(QString::fromUtf8(u8"启用配置"), this);
    enableButton->setObjectName("enableConfig");
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
    m_input->setPlaceholderText(QString::fromUtf8(u8"客户报文"));
    m_previewButton = new QPushButton(QString::fromUtf8(u8"预览"), this);
    m_previewButton->setObjectName("previewButton");
    m_sendButton = new QPushButton(QString::fromUtf8(u8"发送"), this);
    m_sendButton->setObjectName("sendButton");
    m_cancelButton = new QPushButton(QString::fromUtf8(u8"取消等待"), this);
    m_cancelButton->setObjectName("cancelButton");
    m_cancelButton->setToolTip(QString::fromUtf8(u8"只取消软件等待，不会向设备发送停止命令。"));
    m_previewButton->setToolTip(QString::fromUtf8(u8"使用当前草稿解析和组帧，不访问串口。"));
    inputLayout->addWidget(m_inputEncoding);
    inputLayout->addWidget(m_input, 1);
    inputLayout->addWidget(m_previewButton);
    inputLayout->addWidget(m_sendButton);
    inputLayout->addWidget(m_cancelButton);
    m_queryButton = new QPushButton(QString::fromUtf8(u8"查询状态"), this);
    m_queryButton->setObjectName("queryButton");
    m_queryButton->setToolTip(QString::fromUtf8(u8"查询电机状态。"));
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
    previewColumn->addWidget(new QLabel(QString::fromUtf8(u8"解析预览"), this));
    m_previewOutput->setMinimumHeight(190);
    previewColumn->addWidget(m_previewOutput);
    outputs->addLayout(previewColumn, 3);
    auto *deviceOutputs = new QTabWidget(this); deviceOutputs->setObjectName("deviceOutputs");
    deviceOutputs->addTab(m_targetOutput, QString::fromUtf8(u8"目标帧"));
    deviceOutputs->addTab(m_rawReply, QString::fromUtf8(u8"原始回告"));
    deviceOutputs->addTab(m_replyResult, QString::fromUtf8(u8"解析结果"));
    deviceOutputs->setMinimumHeight(210);
    outputs->addWidget(deviceOutputs, 2);
    debugLayout->addLayout(outputs);
    m_liveReason = new QLabel(this); m_liveReason->setObjectName("liveReason");
    debugLayout->addWidget(m_liveReason);
    m_requestState = new QLabel(QString::fromUtf8(u8"执行状态：空闲"), this);
    m_requestState->setObjectName("requestState");
    debugLayout->addWidget(m_requestState);
    tabs->insertTab(2, scrollPage(debugGroup), QString::fromUtf8(u8"报文调试"));

    m_log = new QTextEdit(this); m_log->setObjectName("runLog"); m_log->setReadOnly(true);
    deviceOutputs->addTab(m_log, QString::fromUtf8(u8"日志"));

    connect(addField, &QPushButton::clicked, this, [this]() { addFieldRow(); fieldFormatChanged(); });
    connect(removeField, &QPushButton::clicked, this, [this]() {
        if (m_fieldTable->currentRow() >= 0) m_fieldTable->removeRow(m_fieldTable->currentRow());
        for (int row = 0; row < m_fieldTable->rowCount(); ++row)
            m_fieldTable->item(row, 0)->setText(QString::number(row + 1));
        fieldFormatChanged();
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
        QJsonObject mapping;
        if (!readNewMapping(&mapping)) return;
        for (int row=0;row<m_mappingTable->rowCount();++row) {
            if (m_mappingTable->item(row,0)->data(Qt::UserRole).toJsonObject().value("commandCode") == mapping.value("commandCode")) {
                m_validationState->setText(QString::fromUtf8(u8"添加失败：客户命令已存在，请先删除原映射"));
                return;
            }
        }
        addMappingRow(mapping);
        m_mappingTable->setCurrentCell(m_mappingTable->rowCount()-1,0);
        m_mappingTable->scrollToItem(m_mappingTable->item(m_mappingTable->rowCount()-1,0));
        resetMappingForm(); markDraftDirty(); m_mappingCode->setFocus();
    });
    connect(removeMapping, &QPushButton::clicked, this, [this]() {
        const int row=m_mappingTable->currentRow();
        if (row<0) return;
        m_mappingTable->removeRow(row);
        if (m_mappingTable->rowCount()) m_mappingTable->setCurrentCell(qMin(row,m_mappingTable->rowCount()-1),0);
        updateMappingControls(); markDraftDirty();
    });
    connect(m_mappingTable,&QTableWidget::currentCellChanged,this,[removeMapping](int row,int,int,int) {
        removeMapping->setEnabled(row>=0);
    });
    connect(saveMapping, &QPushButton::clicked, this, &Widget::saveUiToDraft);
    connect(saveDevice, &QPushButton::clicked, this, &Widget::saveUiToDraft);
    connect(applyJson, &QPushButton::clicked, this, &Widget::applyAdvancedJson);
    connect(importButton, &QPushButton::clicked, this, &Widget::importDraft);
    connect(exportButton, &QPushButton::clicked, this, &Widget::exportDraft);
    connect(validateButton, &QPushButton::clicked, this, &Widget::validateDraft);
    connect(enableButton, &QPushButton::clicked, this, &Widget::enableDraft);
    connect(refreshPort, &QPushButton::clicked, this, &Widget::refreshPorts);
}

void Widget::connectSignals()
{
    connect(m_advancedJson, &QTextEdit::textChanged, this, [this]() {
        if (m_loadingUi) return;
        m_jsonDirty = true;
        m_draftValid = false;
        m_deviceConfirmed->setChecked(false);
        m_draftVersion->setText(QString::fromUtf8(u8"草稿：JSON 未应用"));
        m_validationState->setText(QString::fromUtf8(u8"校验：待应用"));
        updateLiveState();
    });
    connect(m_encoding, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        const bool binary = m_encoding->currentData().toString() == "hex";
        m_byteOrderLabel->setVisible(binary);
        m_byteOrder->setVisible(binary);
        m_header->setPlaceholderText(binary ? QString::fromUtf8(u8"例如 AA55") : QString::fromUtf8(u8"例如 /"));
        m_footer->setPlaceholderText(binary ? QString::fromUtf8(u8"例如 0D0A") : QString::fromUtf8(u8"例如 R\\r"));
        markDraftDirty();
    });
    for (QLineEdit *edit : {m_header, m_footer})
        connect(edit, &QLineEdit::textChanged, this, &Widget::markDraftDirty);
    connect(m_byteOrder, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &Widget::markDraftDirty);
    connect(m_fieldTable, &QTableWidget::cellChanged, this, [this](int, int column) {
        if (column == 1) fieldFormatChanged(); else markDraftDirty();
    });
    connect(m_mappingOperation, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        if (!m_loadingMapping) {
            const QString operation = m_mappingOperation->currentData().toString();
            if (QushengCommandCatalog::isReceiveOnly(operation)) {
                const QSignalBlocker blocker(m_mappingOperation); selectData(m_mappingOperation, m_editorOperation); return;
            }
            // 新增表单独立于表格；切换目标只改变新表单，不保存或改写已有行。
            const bool supported = QushengCommandCatalog::isExecutable(operation);
            const bool previousMotion = isMotionEditor(m_editorOperation);
            const bool omitParameters = isQuery(operation) || QushengCommandCatalog::isStopOrReset(operation);
            const bool previouslyOmitted = isQuery(m_editorOperation) || QushengCommandCatalog::isStopOrReset(m_editorOperation);
            if (supported && omitParameters != previouslyOmitted) {
                for (QCheckBox *check : m_mappingOmissions->findChildren<QCheckBox *>())
                    if (check->property("omissionAllowed").toBool() && check->property("fieldRole").toString() == "parameter") check->setChecked(omitParameters);
                if (!omitParameters && m_mappingSource->currentData().toString().isEmpty() && m_mappingSource->count() > 2) m_mappingSource->setCurrentIndex(1);
            }
            // 不同指令的参数没有隐含等价关系。完整目录与现有运动模板之间切换时不迁移位置到别的参数。
            if (!isMotionEditor(operation)) loadCatalogParameters(QJsonObject());
            else if (!previousMotion) {
                selectData(m_mappingSource, ""); m_mappingValue->clear(); m_mappingMultiplier->setText("1");
            }
            m_editorOperation = operation;
            if (!QushengCommandCatalog::isLegacyOperation(operation)) {
                const QSignalBlocker blocker(m_mappingOperation);
                for (int i = m_mappingOperation->count() - 1; i >= 0; --i)
                    if (m_mappingOperation->itemData(i, Qt::UserRole + 1).toBool()) m_mappingOperation->removeItem(i);
                selectData(m_mappingOperation, operation);
            }
        }
        updateMappingControls();
    });
    connect(m_mappingSource, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { updateMappingControls(); });
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
        ProtocolConfig config;
        if (!debugConfig(&config)) return;
        const QJsonObject device = config.root.value("device").toObject();
        UnifiedCommand query;
        query.operation = "QUERY_MOTOR"; query.action = ActionType::Query; query.packetDebug = true;
        query.targetDevice = QString::number(device.value("devId").toInt());
        query.configVersion = config.version;
        query.destinationAddress = quint8(device.value("destination").toInt());
        query.sourceAddress = quint8(device.value("source").toInt());
        m_lastRequestId = m_service->submitUnifiedCommand(query);
    });
    connect(m_cancelButton, &QPushButton::clicked, this, [this]() {
        if (m_lastRequestId) m_service->cancelRequest(m_lastRequestId);
    });
    connect(m_service, &TranslationService::outgoingFrame, this,
            [this](quint64, const QByteArray &frame) {
        // 相对运动先查位置、再下运动；应答后不再主动查询；必须保留每次真实下发，不能覆盖成只剩最后一帧。
        m_targetOutput->append(QString::fromUtf8(u8"实际下发：\n") + frame.toHex(' ').toUpper());
    });
    connect(m_service, &TranslationService::rawDataReceived, this,
            [this](const QByteArray &raw) { m_rawReply->append(raw.toHex(' ').toUpper()); });
    const auto showReply = [this](const UnifiedResult &result) {
        if (result.success) {
            QStringList lines{QString::fromUtf8(u8"请求 %1：设备回告成功").arg(result.requestId)};
            for (auto it = result.data.begin(); it != result.data.end(); ++it) {
                lines << QStringLiteral("%1 = %2").arg(displayName(it.key()), displayValue(it.key(), it.value()));
            }
            m_replyResult->setPlainText(lines.join('\n'));
        } else {
            m_replyResult->setPlainText(QString::fromUtf8(u8"请求 %1 失败\n错误代码：%2\n原因：%3")
                .arg(result.requestId).arg(result.error.code, result.error.message));
        }
    };
    // 每份已解码回告立即显示；运动接纳、执行完成与本地等待终止仍是不同状态。
    connect(m_service, &TranslationService::replyReceived, this, showReply);
    connect(m_service, &TranslationService::completed, this, [showReply](const UnifiedResult &result) {
        if (!result.success) showReply(result); // 本地超时或取消也必须保留可见原因。
    });
    connect(m_service, &TranslationService::requestStateChanged, this,
            [this](quint64 id, RequestState state, const QString &detail) {
        // 请求可以已经显示成功，但仍等本地写完；也可能取消的是排队停止而原运动仍在执行。
        // 是否还有请求由管理器决定，不能仅凭最近一个状态信号推断串口可用。
        m_requestActive = m_requests->hasPendingRequests();
        m_requestState->setText(QString::fromUtf8(u8"执行状态：请求 %1，%2；%3")
                                .arg(id).arg(stateText(state), detail));
        appendLog(QString::fromUtf8(u8"请求 %1：%2；%3").arg(id).arg(stateText(state), detail));
        updateLiveState();
    });
    connect(m_service, &TranslationService::errorOccurred, this,
            [this](quint64, const TranslationError &error) {
        appendLog(QStringLiteral("%1: %2").arg(error.code, error.message));
        m_replyResult->append(QString::fromUtf8(u8"错误代码：%1\n原因：%2").arg(error.code, error.message));
    });
    connect(m_service, &TranslationService::unsolicitedMessage, this,
            [this](const ProtocolMessage &message) {
        // 主动上报只更新显示；不发 completed，也不能代替当前请求的匹配回告。
        QushengSdkReply report; TranslationError error;
        QStringList lines{QString::fromUtf8(u8"主动或未匹配报文 0x%1").arg(message.messageType, 2, 16, QLatin1Char('0')),
            QString::fromUtf8(u8"原始帧：") + QString::fromLatin1((message.wireData.isEmpty() ? message.payload : message.wireData).toHex(' ').toUpper())};
        if (QushengSdk::decodeReport(quint8(message.messageType), message.payload, &report, &error)) {
            for (auto it = report.values.begin(); it != report.values.end(); ++it)
                lines << QStringLiteral("%1 = %2").arg(displayName(it.key()), displayValue(it.key(), it.value()));
        } else lines << QString::fromUtf8(u8"未解析：") + error.message;
        m_replyResult->append(lines.join('\n'));
        appendLog(QString::fromUtf8(u8"收到主动或迟到的 0x%1 帧，未用于完成当前新请求")
                  .arg(message.messageType, 2, 16, QLatin1Char('0')));
    });
    connect(m_requests, &RequestManager::deviceBusyChanged, this, [this](bool busy) {
        appendLog(busy ? QString::fromUtf8(u8"设备状态：运动中或最终状态未知")
                       : QString::fromUtf8(u8"设备状态：已确认空闲"));
        updateLiveState();
    });
    connect(m_requests, &RequestManager::activityChanged, this, [this]() {
        m_requestActive = m_requests->hasPendingRequests(); updateLiveState();
    });
}

void Widget::loadInitialDraft()
{
    TranslationError restoreError;
    if (m_draftStore.exists()) {
        QJsonObject draft;
        if (m_draftStore.load(&draft, &restoreError)) {
            m_draftRoot = draft; loadUiFromDraft();
            appendLog(QString::fromUtf8(u8"已恢复本地草稿：") + m_draftStore.filePath());
            return;
        }
    }
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
            if (restoreError.isValid()) {
                m_validationState->setText(QString::fromUtf8(u8"恢复失败：%1；原文件已保留").arg(restoreError.message));
                appendLog(m_validationState->text());
            }
            return;
        }
    }
    appendLog(QString::fromUtf8(u8"未找到示例配置，请导入 JSON 配置。"));
    if (restoreError.isValid()) {
        m_validationState->setText(QString::fromUtf8(u8"恢复失败：%1；原文件已保留").arg(restoreError.message));
        appendLog(m_validationState->text());
    }
}

bool Widget::persistDraft(const QJsonObject &draft)
{
    TranslationError error;
    if (m_draftStore.save(draft, &error)) return true;
    // 写盘失败不得显示“已保存”，也不得校验旧内存值来掩盖当前未保存的编辑。
    if (!m_jsonDirty) m_formDirty = true;
    m_draftValid = false; m_deviceConfirmed->setChecked(false);
    m_draftVersion->setText(QString::fromUtf8(u8"草稿：未保存"));
    m_validationState->setText(QString::fromUtf8(u8"保存失败：") + error.message);
    appendLog(m_validationState->text()); updateLiveState();
    return false;
}

void Widget::addFieldRow(const QJsonObject &field)
{
    // 一行的 item 和下拉都创建完成后才参与联动；半行不能用于判断字段类型或身份。
    const QSignalBlocker blocker(m_fieldTable);
    const int row = m_fieldTable->rowCount();
    m_fieldTable->insertRow(row);
    auto *order = new QTableWidgetItem(QString::number(row + 1));
    order->setFlags(order->flags() & ~Qt::ItemIsEditable);
    m_fieldTable->setItem(row, 0, order);
    auto *name = new QTableWidgetItem(field.value("name").toString());
    // UI 的 item 跟随这一字段；上次已同步名称不是行号，也不写入 JSON。
    // 载入整份 JSON 会新建 item，因此没有依据的整体替换不会猜测重命名。
    name->setData(Qt::UserRole, field.value("name").toString()); m_fieldTable->setItem(row, 1, name);
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
        connect(box, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &Widget::fieldFormatChanged);
}

void Widget::addMappingRow(const QJsonObject &value)
{
    QJsonObject mapping = value;
    if (mapping.isEmpty()) mapping = QJsonObject{{"commandCode", ""}, {"operation", "MOTOR_FORWARD"},
        {"parameters", QJsonObject{{"positionUm", QJsonObject{{"kind", "field"}, {"field", ""}}}}}};
    const int row = m_mappingTable->rowCount();
    m_mappingTable->insertRow(row);
    for (int c = 0; c < 3; ++c) m_mappingTable->setItem(row, c, new QTableWidgetItem);
    updateMappingSummary(row, mapping);
}

void Widget::updateMappingSummary(int row, const QJsonObject &mapping)
{
    const QSignalBlocker blocker(m_mappingTable);
    // 列表只显示摘要，整条映射存于行数据；普通表单不能丢弃高级 JSON 的备注、输入覆盖或扩展元数据。
    m_mappingTable->item(row, 0)->setData(Qt::UserRole, mapping);
    m_mappingTable->item(row, 0)->setText(mapping.value("commandCode").toString());
    const QString operation = mapping.value("operation").toString();
    m_mappingTable->item(row, 1)->setText((QushengCommandCatalog::isLegacyOperation(operation) ? QString::fromUtf8(u8"旧版") : QString()) + operationName(operation)
        + (QushengCommandCatalog::isExecutable(operation) ? QString() : QString::fromUtf8(u8"（待实现）")));
    QString summary = QString::fromUtf8(u8"无参数");
    if (isMotionEditor(operation)) {
        const QJsonObject expression = mapping.value("parameters").toObject().value(mappingParameterKey(operation)).toObject();
        if (expression.value("kind").toString() == "constant") summary = QString::fromUtf8(u8"固定数值 %1").arg(expression.value("value").toVariant().toString());
        else summary = expression.value("field").toString() + (expression.value("kind").toString() == "linear"
                     ? QString::fromUtf8(u8" × %1").arg(multiplierText(expression)) : QString());
        if (expression.value("offset").toDouble() != 0 || expression.value("round").toString("exact") != "exact")
            summary += QString::fromUtf8(u8"（高级配置）");
    } else {
        QStringList references; const QJsonObject parameters = mapping.value("parameters").toObject();
        for (const QJsonValue &value : QushengCommandCatalog::find(operation).value("parameters").toArray()) {
            const QJsonObject definition = value.toObject(); if (!definition.value("editable").toBool()) continue;
            const QJsonObject relation = parameters.value(definition.value("key").toString()).toObject();
            const QString source = relation.isEmpty() ? parameterDefaultText(definition, operation)
                : relation.value("kind").toString() == "constant" ? relation.value("value").toVariant().toString()
                : relation.value("field").toString() + (relation.value("kind").toString() == "linear"
                  ? QString::fromUtf8(u8" × %1（高级）").arg(multiplierText(relation)) : QString());
            references << definition.value("label").toString() + " = " + source;
        }
        if (!references.isEmpty()) summary = references.join(QString::fromUtf8(u8"；"));
    }
    const QString issue = mappingFieldIssue(mapping);
    m_mappingTable->item(row, 2)->setText(summary + (issue.isEmpty() ? QString() : QString::fromUtf8(u8"（待修正）")));
    m_mappingTable->item(row, 2)->setToolTip(issue);
    for (int c = 0; c < 2; ++c) m_mappingTable->item(row, c)->setToolTip(m_mappingTable->horizontalHeaderItem(c)->toolTip());
}

void Widget::filterMappingCommands()
{
    // 筛选只影响候选显示，不能自动选中首项或修改正在编辑的映射。
    auto *view = qobject_cast<QListView *>(m_mappingOperation->view());
    const QString text = m_mappingSearch->text().trimmed();
    for (int i = 0; i < m_mappingOperation->count(); ++i)
        view->setRowHidden(i, !text.isEmpty() && !m_mappingOperation->itemText(i).contains(text, Qt::CaseInsensitive));
}

void Widget::loadCatalogParameters(const QJsonObject &parameters)
{
    const QSignalBlocker blocker(m_mappingParameters);
    const QString operation = m_mappingOperation->currentData().toString();
    m_mappingParameters->setProperty("operation", operation); m_mappingParameters->setProperty("originalParameters", QVariant::fromValue(parameters));
    m_mappingParameters->setRowCount(0);
    if (isMotionEditor(operation)) return;
    for (const QJsonValue &value : QushengCommandCatalog::find(operation).value("parameters").toArray()) {
        const QJsonObject parameter = value.toObject(); if (!parameter.value("editable").toBool()) continue;
        const QString key = parameter.value("key").toString(); const QJsonObject expression = parameters.value(key).toObject();
        const int row = m_mappingParameters->rowCount(); m_mappingParameters->insertRow(row);
        auto *label = new QTableWidgetItem(parameter.value("label").toString()); label->setFlags(label->flags() & ~Qt::ItemIsEditable);
        const QString parameterTip = parameter.value("label").toString();
        if (expression.value("kind").toString() == "linear")
            label->setText(label->text() + QString::fromUtf8(u8"（高级）"));
        label->setToolTip(parameterTip); label->setData(Qt::UserRole, parameter); label->setData(Qt::UserRole + 1, expression); label->setData(Qt::UserRole + 2, false);
        m_mappingParameters->setItem(row, 0, label);
        auto *source = new QComboBox(m_mappingParameters); source->addItem(parameterDefaultText(parameter, operation), "");
        for (int i = 1; i < m_mappingSource->count(); ++i) source->addItem(m_mappingSource->itemText(i), m_mappingSource->itemData(i));
        const QString selected = expression.value("kind").toString() == "constant" ? "@constant" : expression.value("field").toString();
        if (!selected.isEmpty() && source->findData(selected) < 0) source->addItem(QString::fromUtf8(u8"未找到字段：%1").arg(selected), selected);
        selectData(source, selected); source->setToolTip(QString::fromUtf8(u8"客户字段或固定值。")); m_mappingParameters->setCellWidget(row, 1, source);
        const QString emptyHint = source->itemText(0).contains(QString::fromUtf8(u8"默认"))
            ? QString::fromUtf8(u8"无需填写") : QString::fromUtf8(u8"先选择来源");
        auto *fixed = new QLineEdit(m_mappingParameters); fixed->setText(expression.value("value").toVariant().toString()); fixed->setPlaceholderText(selected == "@constant" ? QString::fromUtf8(u8"填写整数")
            : selected.isEmpty() ? emptyHint : QString::fromUtf8(u8"无需填写"));
        fixed->setEnabled(selected == "@constant"); fixed->setToolTip(parameter.value("label").toString()); m_mappingParameters->setCellWidget(row, 2, fixed);
        connect(source, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, label, source, fixed, emptyHint]() {
            label->setData(Qt::UserRole + 2, true); const QString chosen = source->currentData().toString();
            fixed->setEnabled(chosen == "@constant");
            fixed->setPlaceholderText(chosen == "@constant" ? QString::fromUtf8(u8"填写整数")
                : chosen.isEmpty() ? emptyHint : QString::fromUtf8(u8"无需填写"));
        });
        connect(fixed, &QLineEdit::textChanged, this, [this, label]() { label->setData(Qt::UserRole + 2, true); });
    }
    // 少量参数不铺开大块空白；较多参数保留表内滚动，外层说明仍可继续滚动。
    m_mappingParameters->setFixedHeight(qMin(190, qMax(76, (m_mappingParameters->rowCount() + 1) * 30 + 6)));
}

QString Widget::parameterDefaultText(const QJsonObject &definition, const QString &operation) const
{
    const QString key = definition.value("key").toString();
    const QJsonObject defaults = m_draftRoot.value("sdkDefaults").toObject().value(operation).toObject();
    if (defaults.contains(key)) return QString::fromUtf8(u8"默认值（%1）").arg(defaults.value(key).toVariant().toString());
    const QString deviceKey = definition.value("deviceKey").toString();
    if (!deviceKey.isEmpty()) {
        // 默认值来源独立于映射表达式；空来源不会写入固定值，SDK 在本次请求中补齐并保存快照。
        const QMap<QString, QSpinBox *> values{{"devId", m_devId}, {"speedUmPerS", m_speed},
            {"destination", m_destination}, {"source", m_source}, {"absoluteAction", m_action},
            {"minPositionUm", m_minPosition}, {"maxPositionUm", m_maxPosition}};
        const auto *value = values.value(deviceKey, nullptr);
        return value ? QString::fromUtf8(u8"设备默认（%1）").arg(value->value()) : QString::fromUtf8(u8"设备默认");
    }
    if (definition.contains("default")) return QString::fromUtf8(u8"默认值（%1）").arg(definition.value("default").toVariant().toString());
    return definition.value("required").toBool() ? QString::fromUtf8(u8"需设置") : QString::fromUtf8(u8"暂未设置");
}

bool Widget::catalogParameters(QJsonObject *parameters, QString *reason) const
{
    // 空来源保留默认值机制；未设置与固定零不能合并。SDK 补齐默认，待实现指令仍不能生成 DATA。
    *parameters = m_mappingParameters->property("originalParameters").toJsonObject();
    if (QushengCommandCatalog::isStopOrReset(m_mappingOperation->currentData().toString())) parameters->remove("positionUm");
    for (int row = 0; row < m_mappingParameters->rowCount(); ++row) {
        const auto *label = m_mappingParameters->item(row, 0); const QJsonObject definition = label->data(Qt::UserRole).toJsonObject();
        const QString key = definition.value("key").toString();
        if (!label->data(Qt::UserRole + 2).toBool()) continue; // 保留高级 linear 表达式及扩展备注。
        auto *source = qobject_cast<QComboBox *>(m_mappingParameters->cellWidget(row, 1)); auto *fixed = qobject_cast<QLineEdit *>(m_mappingParameters->cellWidget(row, 2));
        const QString selected = source->currentData().toString();
        if (selected.isEmpty()) { parameters->remove(key); continue; }
        QJsonObject expression = label->data(Qt::UserRole + 1).toJsonObject();
        if (selected == "@constant") {
            bool ok = false; const qlonglong number = fixed->text().trimmed().toLongLong(&ok);
            if (!ok || (definition.contains("min") && number < definition.value("min").toDouble())
                || (definition.contains("max") && number > definition.value("max").toDouble())) {
                *reason = QString::fromUtf8(u8"参数“%1”的固定值必须为协议范围内的整数；请悬停查看说明。").arg(definition.value("label").toString()); return false;
            }
            if (definition.contains("allowedValues") && !definition.value("allowedValues").toArray().contains(QJsonValue(double(number)))) {
                QStringList allowed;
                for (const QJsonValue &value : definition.value("allowedValues").toArray()) allowed << value.toVariant().toString();
                *reason = QString::fromUtf8(u8"参数“%1”的固定值只能选择：%2。").arg(definition.value("label").toString(), allowed.join(QString::fromUtf8(u8"、"))); return false;
            }
            expression.insert("kind", "constant"); expression.insert("value", double(number)); expression.remove("field");
            for (const QString &remove : {"numerator", "denominator", "offset", "round"}) expression.remove(remove);
        } else {
            const bool linear = expression.value("kind").toString() == "linear";
            expression.insert("kind", linear ? "linear" : "field"); expression.insert("field", selected); expression.remove("value");
        }
        parameters->insert(key, expression);
    }
    return true;
}

void Widget::resetMappingForm()
{
    m_loadingMapping=true;
    m_mappingCode->clear(); m_mappingSearch->clear();
    selectData(m_mappingOperation,"MOTOR_FORWARD"); m_editorOperation="MOTOR_FORWARD";
    m_mappingValue->clear(); m_mappingMultiplier->setText("1");
    rebuildMappingOmissions(QJsonObject()); updateMappingSources(); selectData(m_mappingSource,"");
    loadCatalogParameters(QJsonObject()); updateMappingControls();
    m_loadingMapping=false;
}

void Widget::rebuildMappingOmissions(const QJsonObject &input)
{
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
        connect(check, &QCheckBox::toggled, this, [this]() { updateMappingSources(); });
    }
    m_mappingOmissions->setVisible(!all.isEmpty());
}

void Widget::fieldFormatChanged()
{
    if (m_loadingUi || m_syncingFields) return;
    markDraftDirty();
    if (!synchronizeFieldNames()) { refreshMappingSummaries(); return; }
    QJsonArray omitted;
    for (QCheckBox *check : m_mappingOmissions->findChildren<QCheckBox *>())
        if (check->isChecked()) omitted.append(check->property("fieldName").toString());
    rebuildMappingOmissions(QJsonObject{{"omitFields", omitted}});
    updateMappingSources(); refreshMappingSummaries();
}

bool Widget::synchronizeFieldNames()
{
    if (m_loadingUi || m_syncingFields) return true;
    QMap<QString, QString> renames; QSet<QString> names;
    QMap<QString, int> previousCounts;
    for (int row = 0; row < m_fieldTable->rowCount(); ++row)
        ++previousCounts[m_fieldTable->item(row, 1)->data(Qt::UserRole).toString()];
    for (int row = 0; row < m_fieldTable->rowCount(); ++row) {
        const QString name = itemText(m_fieldTable, row, 1);
        if (name.isEmpty() || names.contains(name)) {
            // 输入中的空名、重复名只是尚未完成的编辑。此时不可迁移，否则交换名称会串绑。
            m_validationState->setText(name.isEmpty() ? QString::fromUtf8(u8"字段名称不能为空")
                : QString::fromUtf8(u8"字段名称重复：%1").arg(name));
            return false;
        }
        names.insert(name);
        const QString previous = m_fieldTable->item(row, 1)->data(Qt::UserRole).toString();
        if (!previous.isEmpty() && previous != name && previousCounts.value(previous) == 1)
            renames.insert(previous, name); // 已载入的非法重名没有可靠归属，不能猜测其引用对象。
    }
    if (!renames.isEmpty()) {
        // 字段改名同步现有映射和独立新增表单，不隐式提交新增表单。
        m_syncingFields = true;
        for (int row = 0; row < m_mappingTable->rowCount(); ++row) {
            QJsonObject mapping = m_mappingTable->item(row, 0)->data(Qt::UserRole).toJsonObject();
            QJsonObject parameters = mapping.value("parameters").toObject();
            for (auto it = parameters.begin(); it != parameters.end(); ++it) {
                QJsonObject expression = it.value().toObject(); const QString kind = expression.value("kind").toString();
                const QString previous = expression.value("field").toString();
                // 从原表达式只查一次旧名映射；不能依次字符串替换，否则 A→B、B→A 会相互覆盖。
                if ((kind == "field" || kind == "linear") && renames.contains(previous)) {
                    expression.insert("field", renames.value(previous)); it.value() = expression;
                }
            }
            if (mapping.value("parameters").isObject()) mapping.insert("parameters", parameters);
            if (mapping.value("input").isObject()) {
                QJsonObject input = mapping.value("input").toObject();
                if (input.value("omitFields").isArray()) {
                    QJsonArray omitted = input.value("omitFields").toArray();
                    for (int i = 0; i < omitted.size(); ++i)
                        if (omitted.at(i).isString() && renames.contains(omitted.at(i).toString()))
                            omitted[i] = renames.value(omitted.at(i).toString());
                    input.insert("omitFields", omitted);
                }
                mapping.insert("input", input);
            }
            updateMappingSummary(row, mapping);
        }
        const QSignalBlocker fieldBlocker(m_fieldTable);
        for (int row = 0; row < m_fieldTable->rowCount(); ++row)
            m_fieldTable->item(row, 1)->setData(Qt::UserRole, itemText(m_fieldTable, row, 1));
        // 新增表单的来源也跟随字段改名，但不触碰或提交新表单内容。
        const bool previousLoading=m_loadingMapping; m_loadingMapping=true;
        auto renameSource=[&renames](QComboBox *box) {
            const QString old=box->currentData().toString();
            if (!renames.contains(old)) return;
            const QString name=renames.value(old);
            if (box->findData(name)<0) box->addItem(name,name);
            selectData(box,name);
        };
        renameSource(m_mappingSource);
        for (int row=0;row<m_mappingParameters->rowCount();++row)
            if (auto *box=qobject_cast<QComboBox *>(m_mappingParameters->cellWidget(row,1))) renameSource(box);
        for (QCheckBox *check:m_mappingOmissions->findChildren<QCheckBox *>()) {
            const QString old=check->property("fieldName").toString();
            if (renames.contains(old)) { check->setProperty("fieldName",renames.value(old)); check->setText(renames.value(old)); }
        }
        m_loadingMapping=previousLoading; m_syncingFields=false;
    } else {
        const QSignalBlocker blocker(m_fieldTable);
        for (int row = 0; row < m_fieldTable->rowCount(); ++row)
            m_fieldTable->item(row, 1)->setData(Qt::UserRole, itemText(m_fieldTable, row, 1));
    }
    return true;
}

QString Widget::mappingFieldIssue(const QJsonObject &mapping) const
{
    QSet<QString> numeric, omittable, seen; bool commandSeen = false;
    for (int row = 0; row < m_fieldTable->rowCount(); ++row) {
        const QString name = itemText(m_fieldTable, row, 1);
        if (name.isEmpty() || seen.contains(name)) return QString::fromUtf8(u8"字段名称为空或重复");
        seen.insert(name);
        const QString role = comboData(m_fieldTable, row, 2);
        if (role == "command") commandSeen = true; else if (commandSeen) omittable.insert(name);
        if (role == "parameter" && comboData(m_fieldTable, row, 3) != "ascii") numeric.insert(name);
    }
    for (const QJsonValue &omitted : mapping.value("input").toObject().value("omitFields").toArray()) {
        if (!omittable.contains(omitted.toString())) return QString::fromUtf8(u8"省略字段不可用：%1").arg(omitted.toString());
        numeric.remove(omitted.toString());
    }
    const QJsonObject parameters = mapping.value("parameters").toObject();
    for (const QJsonValue &value : parameters) {
        const QJsonObject expression = value.toObject(); const QString kind = expression.value("kind").toString();
        if ((kind == "field" || kind == "linear") && !numeric.contains(expression.value("field").toString()))
            return QString::fromUtf8(u8"来源字段不可用：%1").arg(expression.value("field").toString());
    }
    return QString();
}

void Widget::refreshMappingSummaries()
{
    for (int row = 0; row < m_mappingTable->rowCount(); ++row)
        updateMappingSummary(row, m_mappingTable->item(row, 0)->data(Qt::UserRole).toJsonObject());
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
    // 删除、改为文本或省略字段后保留失效引用，让校验指出问题，不能自动换成另一个参数。
    if (!selected.isEmpty() && m_mappingSource->findData(selected) < 0)
        m_mappingSource->addItem(QString::fromUtf8(u8"未找到可用字段：%1").arg(selected), selected);
    selectData(m_mappingSource, selected);
    // 参数目录的多个下拉共用当前字段池。保留失效引用及未保存固定值，只更新候选，不能重新载入整张参数表。
    for (int row = 0; row < m_mappingParameters->rowCount(); ++row) {
        auto *source = qobject_cast<QComboBox *>(m_mappingParameters->cellWidget(row, 1)); if (!source) continue;
        const QSignalBlocker sourceBlocker(source); const QString chosen = source->currentData().toString();
        source->clear(); source->addItem(parameterDefaultText(m_mappingParameters->item(row, 0)->data(Qt::UserRole).toJsonObject(),
            m_mappingOperation->currentData().toString()), "");
        for (int i = 1; i < m_mappingSource->count(); ++i) source->addItem(m_mappingSource->itemText(i), m_mappingSource->itemData(i));
        if (!chosen.isEmpty() && source->findData(chosen) < 0) source->addItem(QString::fromUtf8(u8"未找到可用字段：%1").arg(chosen), chosen);
        selectData(source, chosen);
    }
}

void Widget::updateMappingControls()
{
    const QString operation = m_mappingOperation->currentData().toString();
    const bool motion = isMotionEditor(operation);
    m_mappingParameters->setVisible(!motion && m_mappingParameters->rowCount() > 0);
    const bool constant = m_mappingSource->currentData().toString() == "@constant";
    auto visibility = [](QWidget *control, bool show) {
        control->setVisible(show);
        if (auto *form = qobject_cast<QFormLayout *>(control->parentWidget()->layout()))
            if (QWidget *label = form->labelForField(control)) label->setVisible(show);
    };
    // QFormLayout 放在 group 的纵向布局里；标签按其实际所属表单定位，避免隐藏控件后留下空英文/技术标签。
    for (auto *control : {static_cast<QWidget *>(m_mappingSource), static_cast<QWidget *>(m_mappingValue),
                         static_cast<QWidget *>(m_mappingMultiplier)}) {
        bool show = motion;
        if (control == m_mappingValue) show &= constant;
        if (control == m_mappingMultiplier) show &= !constant;
        visibility(control, show);
        auto *layout = m_mappingEditor->layout();
        for (int i = 0; i < layout->count(); ++i) if (auto *form = qobject_cast<QFormLayout *>(layout->itemAt(i)->layout()))
            if (QWidget *label = form->labelForField(control)) {
                label->setVisible(show);
                if (control == m_mappingSource) if (auto *text = qobject_cast<QLabel *>(label))
                    text->setText(QushengCommandCatalog::fixedAction(operation) || mappingParameterKey(m_mappingOperation->currentData().toString()) == "distanceUm"
                        ? QString::fromUtf8(u8"距离来源") : QString::fromUtf8(u8"位置来源"));
            }
    }
    // 方向已固化在新动作中；只有真实存在旧版 ABS 映射时显示其兼容方向，不能让用户在两个地方修改同一动作。
    bool legacyAbsolute = false;
    for (int row = 0; row < m_mappingTable->rowCount(); ++row)
        if (m_mappingTable->item(row, 0)->data(Qt::UserRole).toJsonObject().value("operation").toString() == "ABSOLUTE_MOVE") legacyAbsolute = true;
    m_action->setVisible(legacyAbsolute);
    if (auto *form = qobject_cast<QFormLayout *>(m_action->parentWidget()->layout()))
        if (QWidget *label = form->labelForField(m_action)) label->setVisible(legacyAbsolute);
}

bool Widget::readNewMapping(QJsonObject *result)
{
    auto fail=[this](const QString &reason) {
        m_validationState->setText(QString::fromUtf8(u8"添加失败：")+reason);
        appendLog(m_validationState->text()); return false;
    };
    const QString code=m_mappingCode->text().trimmed();
    if (code.isEmpty()) return fail(QString::fromUtf8(u8"请填写客户命令。"));
    const QString operation=m_mappingOperation->currentData().toString();
    if (QushengCommandCatalog::isReceiveOnly(operation))
        return fail(QString::fromUtf8(u8"此指令仅设备主动上报，不能创建下发映射。"));
    QJsonObject parameters;
    if (isMotionEditor(operation)) {
        QJsonObject expression;
        const QString source=m_mappingSource->currentData().toString();
        if (source=="@constant") {
            bool ok=false; const qlonglong value=m_mappingValue->text().trimmed().toLongLong(&ok);
            if (!ok || value<0 || value>0xfffffe)
                return fail(QString::fromUtf8(u8"固定数值必须填写零到 16777214 的整数。"));
            expression=QJsonObject{{"kind","constant"},{"value",double(value)}};
        } else {
            if (source.isEmpty()) return fail(QString::fromUtf8(u8"请选择位置来源字段。"));
            qlonglong numerator=1,denominator=1;
            if (!decimalRatio(m_mappingMultiplier->text(),&numerator,&denominator))
                return fail(QString::fromUtf8(u8"换算倍数请填写有效整数或最多九位小数。"));
            expression=QJsonObject{{"kind","field"},{"field",source}};
            if (numerator!=denominator) {
                expression.insert("kind","linear"); expression.insert("numerator",double(numerator));
                expression.insert("denominator",double(denominator)); expression.insert("offset",0); expression.insert("round","exact");
            }
        }
        parameters.insert(mappingParameterKey(operation),expression);
    } else {
        QString reason;
        if (!catalogParameters(&parameters,&reason)) return fail(reason);
    }
    QJsonObject mapping{{"commandCode",code},{"operation",operation},{"parameters",parameters}};
    QJsonArray omitted;
    for (QCheckBox *check:m_mappingOmissions->findChildren<QCheckBox *>())
        if (check->isChecked()) omitted.append(check->property("fieldName").toString());
    if (!omitted.isEmpty()) mapping.insert("input",QJsonObject{{"omitFields",omitted}});
    *result=mapping; return true;
}

void Widget::loadUiFromDraft()
{
    // 统一客户格式的帧尾和立即显示回告后，旧映射不能留下看不见却继续生效的开关。
    // 补齐旧界面未生成的停止/复位省略设置；显式省略列表和其他扩展元数据保持原值。
    QJsonArray optionalParameters;
    bool mappedCommand=false,afterCommand=false;
    for (const QJsonValue &value:m_draftRoot.value("external").toObject().value("fields").toArray()) {
        const QJsonObject field=value.toObject();
        if (field.value("role").toString()=="command") {
            afterCommand=true; mappedCommand=field.value("lengthMode").toString()=="mappedCommand";
        } else if (afterCommand && field.value("role").toString()=="parameter") optionalParameters.append(field.value("name"));
    }
    QJsonArray mappings = m_draftRoot.value("mappings").toArray();
    bool migrated = false;
    for (int i = 0; i < mappings.size(); ++i) {
        if (!mappings.at(i).isObject()) continue;
        QJsonObject mapping = mappings.at(i).toObject(); const QJsonObject previous = mapping;
        if (QushengCommandCatalog::isStopOrReset(mapping.value("operation").toString()) && mapping.value("parameters").isObject()) {
            QJsonObject parameters = mapping.value("parameters").toObject(); parameters.remove("positionUm");
            mapping.insert("parameters", parameters);
        }
        const QJsonValue originalInput=mapping.value("input");
        if (mappedCommand && !optionalParameters.isEmpty()
                && QushengCommandCatalog::isStopOrReset(mapping.value("operation").toString())
                && (originalInput.isUndefined() || (originalInput.isObject() && !originalInput.toObject().contains("omitFields")))) {
            QJsonObject input=originalInput.toObject(); input.insert("omitFields",optionalParameters); mapping.insert("input",input);
        }
        mapping.remove("responsePolicy");
        if (mapping.value("input").isObject()) {
            QJsonObject input = mapping.value("input").toObject(); input.remove("footer");
            if (input.isEmpty()) mapping.remove("input"); else mapping.insert("input", input);
        }
        if (mapping != previous) { mappings[i] = mapping; migrated = true; }
    }
    if (migrated) {
        m_draftRoot.insert("mappings", mappings);
        appendLog(QString::fromUtf8(u8"已清理旧映射中的不适用设置。"));
    }
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
    m_draftVersion->setText(QString::fromUtf8(u8"草稿：已载入"));
    m_validationState->setText(QString::fromUtf8(u8"校验：待校验"));
    m_inputEncoding->setText(QString::fromUtf8(u8"输入编码：")
        + (m_encoding->currentData().toString() == "hex" ? QStringLiteral("HEX") : QStringLiteral("ASCII")));
    m_deviceConfirmed->setChecked(false);
    m_formDirty = false;
    m_jsonDirty = false;
    m_loadingUi = false;
    resetMappingForm();
    updateTableControls();
    refreshMappingSummaries();
    updateLiveState();
}

bool Widget::saveUiToDraft()
{
    if (m_jsonDirty) {
        m_validationState->setText(QString::fromUtf8(u8"保存失败：请先应用 JSON"));
        appendLog(m_validationState->text());
        return false;
    }
    QJsonObject root;
    if (!collectUiDraft(&root)) return false;
    return storeUiDraft(root);
}

bool Widget::collectUiDraft(QJsonObject *draft)
{
    if (!synchronizeFieldNames()) return false;
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
            if (!ok) {
                m_validationState->setText(QString::fromUtf8(u8"保存失败：第 %1 行最小值不是数字").arg(row + 1));
                appendLog(m_validationState->text()); return false;
            }
            field.insert("min", number);
        }
        if (comboData(m_fieldTable, row, 3) != "ascii" && !itemText(m_fieldTable, row, 8).isEmpty()) {
            const double number = itemText(m_fieldTable, row, 8).toDouble(&ok);
            if (!ok) {
                m_validationState->setText(QString::fromUtf8(u8"保存失败：第 %1 行最大值不是数字").arg(row + 1));
                appendLog(m_validationState->text()); return false;
            }
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
    *draft = root;
    return true;
}

bool Widget::storeUiDraft(const QJsonObject &root)
{
    if (!persistDraft(root)) return false;
    m_draftRoot = root;
    const QSignalBlocker jsonBlocker(m_advancedJson);
    m_advancedJson->setPlainText(QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented)));
    m_draftValid = false;
    m_draftVersion->setText(QString::fromUtf8(u8"草稿：已保存"));
    m_validationState->setText(QString::fromUtf8(u8"校验：待校验"));
    m_inputEncoding->setText(QString::fromUtf8(u8"输入编码：")
        + (m_encoding->currentData().toString() == "hex" ? QStringLiteral("HEX") : QStringLiteral("ASCII")));
    m_deviceConfirmed->setChecked(false);
    m_formDirty = false;
    appendLog(QString::fromUtf8(u8"草稿已保存到本地；当前已启用版本未改变。"));
    refreshMappingSummaries(); updateMappingControls(); updateLiveState();
    return true;
}

void Widget::markDraftDirty()
{
    if (m_loadingUi || m_draftRoot.isEmpty()) return;
    m_formDirty = true;
    m_draftValid = false;
    m_deviceConfirmed->setChecked(false);
    updateTableControls();
    refreshMappingSummaries();
    m_draftVersion->setText(QString::fromUtf8(u8"草稿：未保存"));
    m_validationState->setText(QString::fromUtf8(u8"校验：待保存"));
    updateLiveState();
}

bool Widget::draftSaved()
{
    // “表单”“草稿”“启用快照”是三个不同状态。未保存表单绝不能被旧草稿的校验成功掩盖。
    if (!m_formDirty && !m_jsonDirty) return true;
    const QString reason = m_jsonDirty ? QString::fromUtf8(u8"请先应用高级 JSON 到草稿。")
                                      : QString::fromUtf8(u8"请先点击“保存到草稿”，再校验、启用、预览或导出。");
    m_validationState->setText(QString::fromUtf8(u8"校验：") + reason);
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
        m_validationState->setText(QString::fromUtf8(u8"校验：校验失败（%1）").arg(error.message));
        appendLog(QStringLiteral("%1: %2").arg(error.code, error.message));
        updateLiveState();
        return;
    }
    m_validatedDraft = config;
    m_draftValid = true;
    m_draftVersion->setText(QString::fromUtf8(u8"草稿：%1").arg(config.version.left(12)));
    int pending = 0;
    for (const QJsonValue &value : config.root.value("mappings").toArray())
        if (!QushengCommandCatalog::isExecutable(value.toObject().value("operation").toString())) ++pending;
    m_validationState->setText(pending ? QString::fromUtf8(u8"校验：通过 (%1 条待实现)").arg(pending)
                                      : QString::fromUtf8(u8"校验：通过"));
    appendLog(QString::fromUtf8(u8"配置校验成功，草稿版本 %1").arg(config.version.left(12)));
    updateLiveState();
}

void Widget::enableDraft()
{
    if (!draftSaved()) return;
    if (m_requestActive) {
        m_validationState->setText(QString::fromUtf8(u8"启用失败：当前请求尚未结束"));
        appendLog(m_validationState->text());
        return;
    }
    if (!m_draftValid) {
        m_validationState->setText(QString::fromUtf8(u8"启用失败：请先保存并校验草稿"));
        appendLog(m_validationState->text());
        return;
    }
    m_activeConfig = m_validatedDraft;
    m_configEnabled = true;
    const QJsonObject device = m_activeConfig.root.value("device").toObject();
    m_protocol->setAddresses(quint8(device.value("destination").toInt()),
                             quint8(device.value("source").toInt()));
    // 同一物理连接里保留序号，切换配置不能重用旧序号而误匹配迟到的设备应答。
    m_activeVersion->setText(QString::fromUtf8(u8"启用：%1").arg(m_activeConfig.version.left(12)));
    int pending = 0;
    for (const QJsonValue &value : m_activeConfig.root.value("mappings").toArray())
        if (!QushengCommandCatalog::isExecutable(value.toObject().value("operation").toString())) ++pending;
    m_validationState->setText(pending ? QString::fromUtf8(u8"校验：通过 (%1 条待实现)").arg(pending)
                                      : QString::fromUtf8(u8"校验：通过"));
    m_deviceConfirmed->setChecked(false);
    appendLog(QString::fromUtf8(u8"已启用配置快照；真实发送前还需勾选设备参数确认项。"));
    updateLiveState();
}

bool Widget::applyAdvancedJson()
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(m_advancedJson->toPlainText().toUtf8(), &parseError);
    if (!document.isObject()) {
        m_validationState->setText(QString::fromUtf8(u8"JSON 无效：%1").arg(parseError.errorString()));
        appendLog(m_validationState->text());
        return false;
    }
    if (!persistDraft(document.object())) return false;
    m_draftRoot = document.object();
    loadUiFromDraft();
    appendLog(QString::fromUtf8(u8"高级 JSON 已应用到草稿；当前已启用版本未改变。"));
    return true;
}

void Widget::closeEvent(QCloseEvent *event)
{
    // 提交仍在编辑的表格单元格和设备数字，再判断是否有未保存修改。
    QWidget *editor = QApplication::focusWidget();
    if (editor && isAncestorOf(editor)) editor->clearFocus();
    if (!m_formDirty && !m_jsonDirty) {
        event->accept();
        return;
    }

    const bool conflict = m_formDirty && m_jsonDirty;
    QDialog prompt(this);
    prompt.setWindowTitle(QString::fromUtf8(u8"未保存修改"));
    prompt.setObjectName("unsavedClosePrompt");
    auto *layout = new QVBoxLayout(&prompt);
    layout->addWidget(new QLabel(conflict
        ? QString::fromUtf8(u8"表单和高级 JSON 都有修改，请取消关闭并选择要保存的内容。")
        : QString::fromUtf8(u8"是否保存修改到草稿后关闭？"), &prompt));
    auto *buttons = new QDialogButtonBox(&prompt);
    layout->addWidget(buttons);
    auto *save = buttons->addButton(QString::fromUtf8(u8"保存"), QDialogButtonBox::AcceptRole);
    auto *discard = buttons->addButton(QString::fromUtf8(u8"放弃"), QDialogButtonBox::DestructiveRole);
    auto *cancel = buttons->addButton(QString::fromUtf8(u8"取消"), QDialogButtonBox::RejectRole);
    save->setEnabled(!conflict);
    cancel->setDefault(true);
    cancel->setFocus();
    enum CloseChoice { CancelClose = 0, SaveDraft = 1, DiscardDraft = 2 };
    connect(save, &QPushButton::clicked, &prompt, [&prompt]() { prompt.done(SaveDraft); });
    connect(discard, &QPushButton::clicked, &prompt, [&prompt]() { prompt.done(DiscardDraft); });
    connect(cancel, &QPushButton::clicked, &prompt, &QDialog::reject);
    const int choice = prompt.exec();
    if (choice == DiscardDraft) {
        event->accept();
        return;
    }
    if (choice == SaveDraft) {
        const bool saved = m_jsonDirty ? applyAdvancedJson() : saveUiToDraft();
        if (saved) {
            event->accept();
            return;
        }
    }
    // 取消、关闭提示框或保存失败都保留编辑窗口与原落盘草稿。
    event->ignore();
}

void Widget::importDraft()
{
    const QString path = QFileDialog::getOpenFileName(this, QString::fromUtf8(u8"导入协议配置"),
                                                       QString(), QStringLiteral("JSON (*.json)"));
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        m_validationState->setText(QString::fromUtf8(u8"导入失败：") + file.errorString());
        appendLog(m_validationState->text()); return;
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!document.isObject()) {
        m_validationState->setText(QString::fromUtf8(u8"导入失败：文件不是有效 JSON 对象"));
        appendLog(m_validationState->text()); return;
    }
    if (!persistDraft(document.object())) return;
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
        m_validationState->setText(QString::fromUtf8(u8"导出失败：") + error.message);
        appendLog(QStringLiteral("%1: %2").arg(error.code, error.message));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, QString::fromUtf8(u8"导出协议配置"),
                                                       QString(), QStringLiteral("JSON (*.json)"));
    if (!path.isEmpty() && !config.save(path, &error)) {
        m_validationState->setText(QString::fromUtf8(u8"导出失败：") + error.message);
        appendLog(QStringLiteral("%1: %2").arg(error.code, error.message));
    }
}

void Widget::refreshPorts()
{
    const QString selected = m_port->currentData().toString();
    m_port->clear();
    for (const QSerialPortInfo &port : QSerialPortInfo::availablePorts())
        m_port->addItem(QStringLiteral("%1 - %2").arg(port.portName(), port.description()), port.portName());
    const int index = m_port->findData(selected);
    if (index >= 0) m_port->setCurrentIndex(index);
}

void Widget::updateConnectionUi(bool connected, const QString &reason)
{
    m_port->setEnabled(!connected);
    m_baud->setEnabled(!connected);
    m_connectButton->setText(connected ? QString::fromUtf8(u8"断开") : QString::fromUtf8(u8"连接"));
    m_connectionState->setText(connected ? QString::fromUtf8(u8"已连接") : QString::fromUtf8(u8"未连接"));
    if (!reason.isEmpty()) appendLog(reason);
    // 日志不再常驻，串口驱动的失败原因必须立即可见；系统详情仍完整保留在日志。
    if (!connected && reason.contains(QString::fromUtf8(u8"；系统详情：")))
        m_validationState->setText(reason.section(QString::fromUtf8(u8"；系统详情："), 0, 0));
    if (connected) m_protocol->resetSession();
    m_deviceConfirmed->setChecked(false);
    updateLiveState();
}

void Widget::updateLiveState()
{
    const bool current = m_draftValid && !m_formDirty && !m_jsonDirty
            && m_validatedDraft.version == m_activeConfig.version;
    m_deviceConfirmed->setEnabled(m_configEnabled && current);
    const QString reason = !m_transport->isConnected() ? QString::fromUtf8(u8"报文调试：串口未连接")
        : m_requestActive ? QString::fromUtf8(u8"报文调试：可继续发送（排队）")
                          : QString::fromUtf8(u8"报文调试：可直接发送");
    // 按钮始终可点击，错误在点击时具体显示，不用工作流或设备状态禁用发送。
    m_sendButton->setEnabled(true);
    m_queryButton->setEnabled(true);
    m_sendButton->setToolTip(reason);
    m_liveReason->setText(reason);
    m_liveReason->setStyleSheet(m_transport->isConnected() ? QStringLiteral("color:#087a32") : QStringLiteral("color:#a04400"));
    m_cancelButton->setEnabled(m_requestActive);
}

bool Widget::debugConfig(ProtocolConfig *config)
{
    QJsonObject root = m_draftRoot;
    if (m_jsonDirty) {
        const QJsonDocument json = QJsonDocument::fromJson(m_advancedJson->toPlainText().toUtf8());
        if (!json.isObject()) {
            m_previewOutput->setPlainText(QString::fromUtf8(u8"发送失败：高级 JSON 不是有效对象"));
            return false;
        }
        root = json.object();
    } else if (m_formDirty) {
        if (!collectUiDraft(&root)) {
            m_previewOutput->setPlainText(m_validationState->text());
            return false;
        }
    }
    // 调试快照不落盘：忽略软件字段范围/行程，协议本身的数值字节宽度仍由编码器检查。
    QJsonObject external = root.value("external").toObject();
    QJsonArray fields = external.value("fields").toArray();
    for (int i=0;i<fields.size();++i) {
        QJsonObject field = fields.at(i).toObject(); field.remove("min"); field.remove("max"); fields[i]=field;
    }
    external.insert("fields",fields); root.insert("external",external);
    QJsonObject device = root.value("device").toObject();
    device.insert("minPositionUm",0); device.insert("maxPositionUm",0xfffffe); root.insert("device",device);
    TranslationError error;
    if (!ProtocolConfig::fromJson(QJsonDocument(root).toJson(),config,&error)) {
        m_previewOutput->setPlainText(QString::fromUtf8(u8"报文生成失败：%1").arg(error.message));
        appendLog(error.code + ": " + error.message);
        return false;
    }
    return true;
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
        lines << QStringLiteral("  %1: %2").arg(displayName(it.key()), it.value());
    const QString device = isMotionEditor(preview.command.operation) || preview.command.parameters.contains("devId")
        ? preview.command.targetDevice : QStringLiteral("-");
    lines << QString::fromUtf8(u8"统一命令：操作=%1，设备编号=%2，配置版本=%3")
             .arg(operationName(preview.command.operation), device,
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
        if (!debugConfig(&config)) return;
    } else {
        TranslationError configError;
        if (!ProtocolConfig::fromJson(QJsonDocument(m_draftRoot).toJson(), &config, &configError)) {
            m_previewOutput->setPlainText(QString::fromUtf8(u8"预览失败：%1").arg(configError.message));
            appendLog(QStringLiteral("%1: %2").arg(configError.code, configError.message));
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
        appendLog(QStringLiteral("%1: %2").arg(conversionError.code, conversionError.message));
        return;
    }
    m_previewOutput->setPlainText((live ? QString::fromUtf8(u8"报文调试：使用当前编辑内容\n")
                                       : QString::fromUtf8(u8"使用当前已保存草稿\n")) + previewText(converted));
    m_targetOutput->setPlainText(QString::fromUtf8(u8"预览帧（未发送）：\n")
                                 + converted.targetFrame.toHex(' ').toUpper());
    if (!live) {
        appendLog(QString::fromUtf8(u8"预览完成，未访问串口。"));
        return;
    }
    m_rawReply->clear();
    m_replyResult->clear();
    converted.command.packetDebug = true;
    const QJsonObject device = config.root.value("device").toObject();
    converted.command.destinationAddress = quint8(device.value("destination").toInt());
    converted.command.sourceAddress = quint8(device.value("source").toInt());
    m_lastRequestId = m_service->submitUnifiedCommand(converted.command);
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
    case RequestState::Cancelled: return QString::fromUtf8(u8"已取消等待");
    }
    return QString::fromUtf8(u8"未知状态");
}
