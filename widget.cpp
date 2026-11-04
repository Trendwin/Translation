#include "widget.h"

#include "src/core/requestmanager.h"
#include "src/core/numericformula.h"
#include "src/core/translationservice.h"
#include "src/qusheng/qushengprotocol.h"
#include "src/qusheng/qushengcommandcatalog.h"
#include "src/qusheng/sdk/qushengsdk.h"
#include "src/transport/serialtransport.h"
#include "src/transport/upstreamserialreceiver.h"
#include "src/transport/virtualcomprovider.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCloseEvent>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QEvent>
#include <QDialogButtonBox>
#include <QDir>
#include <QListView>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QSerialPortInfo>
#include <QSpinBox>
#include <QSplitter>
#include <QSignalBlocker>
#include <QSet>
#include <QScrollArea>
#include <QRegularExpression>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextCursor>
#include <QTimer>
#include <QResizeEvent>
#include <QUuid>
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

QJsonObject receiveFormatKey(const ProtocolConfig &config)
{
    QJsonObject key{{"external", config.root.value("external")}};
    if (config.hasMappedCommand()) {
        QJsonArray commands;
        for (const auto &value : config.root.value("mappings").toArray()) {
            const auto mapping = value.toObject();
            commands.append(QJsonObject{{"commandCode", mapping.value("commandCode")},
                                        {"format", config.inputFormat(mapping)}});
        }
        key.insert("commands", commands);
    }
    return key;
}

QString normalizedPortName(QString name)
{
    name = name.trimmed();
    if (name.startsWith(QStringLiteral("\\\\.\\"))) name.remove(0, 4);
    return name.toUpper();
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
        {"moduleType", QString::fromUtf8(u8"模块类型")}, {"moduleIndex", QString::fromUtf8(u8"虚拟模块编号")},
        {"xPosition", QString::fromUtf8(u8"X位置（下位机数值）")}, {"yPosition", QString::fromUtf8(u8"Y位置（下位机数值）")},
        {"zPosition", QString::fromUtf8(u8"Z位置（下位机数值）")}, {"xSpeed", QString::fromUtf8(u8"X速度（下位机数值）")},
        {"ySpeed", QString::fromUtf8(u8"Y速度（下位机数值）")}, {"zSpeed", QString::fromUtf8(u8"Z速度（下位机数值）")},
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
    for (int index = 0; index < box->count(); ++index)
        if (box->itemData(index).toString() == data && box->itemData(index, Qt::UserRole + 4).toString() != "preset") {
            box->setCurrentIndex(index); return;
        }
}


bool isListSource(const QComboBox *box)
{ return box && box->currentData(Qt::UserRole + 1).toBool(); }

int sourceItemIndex(const QComboBox *box)
{ return box ? box->currentData(Qt::UserRole + 2).toInt() : 0; }

bool isFormulaSource(const QComboBox *box)
{ return box && box->currentData(Qt::UserRole + 4).toString() == "formula"; }

bool formulaAxis(const QString &operation, const QString &key)
{ return operation == "QUSHENG_C1" && QStringList({"xPosition", "yPosition", "zPosition"}).contains(key); }

void addFormulaSource(QComboBox *box, const QString &operation, const QString &key)
{
    if (!formulaAxis(operation, key)) return;
    box->addItem(QString::fromUtf8(u8"fx 公式"), "@formula");
    box->setItemData(box->count() - 1, "formula", Qt::UserRole + 4);
}

class AxisFormulaEdit : public QLineEdit
{
public:
    explicit AxisFormulaEdit(QWidget *parent) : QLineEdit(parent), button(new QPushButton("fx", this))
    { button->hide(); }
    QPushButton *button;
protected:
    void resizeEvent(QResizeEvent *event) override
    { QLineEdit::resizeEvent(event); button->setGeometry(qMax(0, width() - 32), 1, 30, qMax(18, height() - 2)); }
};

QJsonObject renameExpressionFields(QJsonObject expression, const QMap<QString, QString> &renames)
{
    if (expression.value("kind").toString() == "formula") {
        auto variables = expression.value("variables").toObject();
        for (auto it = variables.begin(); it != variables.end(); ++it) it.value() = renameExpressionFields(it.value().toObject(), renames);
        expression.insert("variables", variables);
    } else if (QStringList({"field", "linear"}).contains(expression.value("kind").toString())
               && expression.value("scope").toString() != "preset" && renames.contains(expression.value("field").toString()))
        expression.insert("field", renames.value(expression.value("field").toString()));
    return expression;
}

bool isPresetSource(const QComboBox *box)
{ return box && box->currentData(Qt::UserRole + 4).toString() == "preset"; }

QString sourceScope(const QComboBox *box)
{ return isFormulaSource(box) ? QStringLiteral("formula") : isPresetSource(box) ? QStringLiteral("preset") : QStringLiteral("input"); }

void applySourceScope(QJsonObject *expression, const QComboBox *box)
{
    if (isPresetSource(box)) expression->insert("scope", "preset");
    else if (expression->value("scope").toString() == "preset") expression->remove("scope");
}

bool presetValues(const QString &text, QJsonArray *values)
{
    *values = QJsonArray();
    const auto parts = text.split(',');
    if (parts.isEmpty() || parts.size() > 64) return false;
    static const QRegularExpression integer(QStringLiteral("^[+-]?[0-9]+$"));
    for (const auto &part : parts) {
        const QString token = part.trimmed(); bool ok = false;
        const qlonglong number = token.toLongLong(&ok);
        if (!ok || !integer.match(token).hasMatch() || number < -9007199254740991LL || number > 9007199254740991LL) return false;
        values->append(double(number));
    }
    return true;
}

void restoreSource(QComboBox *box, const QString &name, bool list, int itemIndex = 0, const QString &scope = QStringLiteral("input"))
{
    bool sameTypeAvailable = false;
    bool qualified = true;
    for (int i = 0; i < box->count(); ++i) {
        if (box->itemData(i).toString() != name || box->itemData(i, Qt::UserRole + 1).toBool() != list
                || (box->itemData(i, Qt::UserRole + 4).toString().isEmpty() ? QStringLiteral("input") : box->itemData(i, Qt::UserRole + 4).toString()) != scope) continue;
        sameTypeAvailable = true;
        qualified = box->itemData(i, Qt::UserRole + 3).toBool();
        if (!list || box->itemData(i, Qt::UserRole + 2).toInt() == itemIndex) {
            box->setCurrentIndex(i);
            return;
        }
    }
    if (name.isEmpty()) { box->setCurrentIndex(0); return; }
    QString label;
    if (list && sameTypeAvailable) {
        label = (qualified ? name + QString::fromUtf8(u8" · ") : QString())
            + QString::fromUtf8(u8"第%1个参数（超出输入项数）").arg(itemIndex);
    } else {
        label = QString::fromUtf8(u8"来源不可用或类型已变化：%1").arg(name);
        if (list) label += QString::fromUtf8(u8" · 第%1个参数").arg(itemIndex);
    }
    if (scope == "preset") label = QString::fromUtf8(u8"预设失效或项号越界：%1 · 第%2项").arg(name).arg(itemIndex);
    box->addItem(label, name);
    box->setItemData(box->count() - 1, list, Qt::UserRole + 1);
    box->setItemData(box->count() - 1, itemIndex, Qt::UserRole + 2);
    box->setItemData(box->count() - 1, scope, Qt::UserRole + 4);
    box->setCurrentIndex(box->count() - 1);
}

QJsonObject renameMappingFields(QJsonObject mapping, const QMap<QString, QString> &renames)
{
    if (mapping.value("parameters").isObject()) {
        auto parameters = mapping.value("parameters").toObject();
        for (auto it = parameters.begin(); it != parameters.end(); ++it) {
            it.value() = renameExpressionFields(it.value().toObject(), renames);
        }
        mapping.insert("parameters", parameters);
    }
    if (mapping.value("steps").isArray()) {
        auto steps = mapping.value("steps").toArray();
        for (int i = 0; i < steps.size(); ++i) steps[i] = renameMappingFields(steps[i].toObject(), renames);
        mapping.insert("steps", steps);
    }
    if (mapping.value("input").isObject()) {
        auto input = mapping.value("input").toObject();
        if (input.value("omitFields").isArray()) {
            auto omitted = input.value("omitFields").toArray();
            for (int i = 0; i < omitted.size(); ++i)
                if (renames.contains(omitted[i].toString())) omitted[i] = renames.value(omitted[i].toString());
            input.insert("omitFields", omitted);
        }
        if (input.value("parameterCounts").isObject()) {
            const auto counts = input.value("parameterCounts").toObject(); QJsonObject renamed;
            for (auto it = counts.begin(); it != counts.end(); ++it) renamed.insert(renames.value(it.key(), it.key()), it.value());
            input.insert("parameterCounts", renamed);
        }
        mapping.insert("input", input);
    }
    return mapping;
}

QStringList fieldPreview(const QString &name, const QVariant &value)
{
    if (value.type() != QVariant::List) return {QStringLiteral("%1 = %2").arg(name, value.toString())};
    QStringList values, lines;
    const QVariantList items = value.toList();
    for (const auto &item : items) values.append(item.toString());
    lines.append(QStringLiteral("%1 = [%2]").arg(name, values.join(", ")));
    for (int i = 0; i < items.size(); ++i)
        lines.append(QString::fromUtf8(u8"  %1 · 第 %2 项 = %3").arg(name).arg(i + 1).arg(items.at(i).toString()));
    return lines;
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

Widget::Widget(QWidget *parent, const QString &draftFilePath, const QString &virtualStatePath)
    : QWidget(parent),
      m_protocol(new QushengProtocol(this)),
      m_transport(new SerialTransport(this)),
      m_upstream(new UpstreamSerialReceiver(this)),
      m_virtualCom(new VirtualComProvider(this, virtualStatePath.isEmpty() && !draftFilePath.isEmpty()
                                         ? draftFilePath + QStringLiteral(".upstream-port.json") : virtualStatePath)),
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
    QTimer::singleShot(0, m_virtualCom, &VirtualComProvider::refresh);
}

void Widget::buildUi()
{
    setWindowTitle(QString::fromUtf8(u8"配置驱动协议转换"));
    resize(1100, 740);
    QFile appearance(QStringLiteral(":/appearance/translation.qss"));
    if (appearance.open(QIODevice::ReadOnly))
        setStyleSheet(QString::fromUtf8(appearance.readAll()));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 10, 12, 10);
    root->setSpacing(8);
    m_draftVersion = new QLabel(QString::fromUtf8(u8"配置：未加载"), this);
    m_activeVersion = new QLabel(QString::fromUtf8(u8"启用：无"), this);
    m_validationState = new QLabel(this);
    m_draftVersion->setObjectName("draftVersion"); m_activeVersion->setObjectName("activeVersion");
    m_validationState->setObjectName("validationState"); m_validationState->setWordWrap(true);
    // 版本诊断仍保留在隐藏的高级页；主界面只在底部显示需要处理的错误。
    m_draftVersion->hide(); m_activeVersion->hide();

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
    m_formatRangesToggle = new QCheckBox(QString::fromUtf8(u8"数值范围"), this);
    m_formatRangesToggle->setObjectName("formatRangesToggle");
    formatLayout->addWidget(m_formatRangesToggle);
    connect(m_formatRangesToggle, &QCheckBox::toggled, this, [this]() { updateTableControls(); });

    m_fieldTable = new QTableWidget(this);
    m_fieldTable->setObjectName("fieldTable");
    m_fieldTable->setColumnCount(9);
    m_fieldTable->setHorizontalHeaderLabels({
        QString::fromUtf8(u8"顺序"), QString::fromUtf8(u8"字段名称"), QString::fromUtf8(u8"字段作用"),
        QString::fromUtf8(u8"数据类型"), QString::fromUtf8(u8"长度方式"), QString::fromUtf8(u8"长度 / 分隔符"),
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
        QString::fromUtf8(u8"固定长度填字节数；读到分隔符填写 &、;、=、\\n 等，读取后消耗分隔符。HEX 模式填十六进制字节。"),
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
    addField->setToolTip(QString::fromUtf8(u8"在字段表末尾增加一行。"));
    removeField->setToolTip(QString::fromUtf8(u8"删除当前选中的字段行。"));
    fieldUp->setToolTip(QString::fromUtf8(u8"把选中字段提前一个读取顺序。"));
    fieldDown->setToolTip(QString::fromUtf8(u8"把选中字段推后一个读取顺序。"));
    for (auto *button : {addField, removeField, fieldUp, fieldDown}) fieldButtons->addWidget(button);
    fieldButtons->addStretch();
    formatLayout->addLayout(fieldButtons);
    tabs->addTab(scrollPage(formatPage), QString::fromUtf8(u8"客户协议格式"));

    auto *mappingPage = new QWidget(this);
    auto *mappingLayout = new QVBoxLayout(mappingPage);
    auto *mappingSplitter = new QSplitter(Qt::Vertical, mappingPage);
    m_mappingSplitter = mappingSplitter;
    mappingSplitter->setObjectName("mappingSplitter");
    mappingSplitter->installEventFilter(this);
    connect(mappingSplitter, &QSplitter::splitterMoved, this, [this](int, int) {
        m_mappingSplitterAdjusted = true;
    });
    mappingSplitter->setChildrenCollapsible(false);
    auto *savedMappings = new QWidget(mappingSplitter);
    auto *savedLayout = new QVBoxLayout(savedMappings);
    savedLayout->setContentsMargins(0, 0, 0, 0);
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
    m_mappingTable->setMinimumHeight(80);
    m_mappingTable->setToolTip(QString::fromUtf8(u8"选择映射后可删除；新增请填写下方表单。"));
    const QStringList mappingTips = {QString::fromUtf8(u8"客户报文中的命令码，支持不同长度"),
        QString::fromUtf8(u8"选择趋盛命令模板"), QString::fromUtf8(u8"来源字段、换算倍数或固定值")};
    for (int c = 0; c < mappingTips.size(); ++c) m_mappingTable->horizontalHeaderItem(c)->setToolTip(mappingTips.at(c));
    savedLayout->addWidget(m_mappingTable, 1);

    m_mappingEditor = new QGroupBox(QString::fromUtf8(u8"添加新映射"), this);
    m_mappingEditor->setObjectName("mappingEditor");
    auto *editorLayout = new QVBoxLayout(m_mappingEditor);
    m_mappingEditor->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    m_mappingEditor->setToolTip(QString::fromUtf8(u8"仅添加新映射，不修改上方已有映射。多条指令须逐条添加或保存，再添加整组映射。"));
    editorLayout->setAlignment(Qt::AlignTop);
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
    auto *searchLine = new QHBoxLayout; searchLine->setObjectName("mappingSearchRow"); searchLine->addWidget(m_mappingSearch);
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
    m_mappingSource->setToolTip(QString::fromUtf8(u8"直接选择第几个参数、字段、固定值或无参数报文。"));
    m_mappingMotor = new QComboBox(this); m_mappingMotor->setObjectName("mappingMotor");
    m_mappingMotor->addItem(QString::fromUtf8(u8"设备默认"), 0);
    for (int id = 0x11; id <= 0x16; ++id)
        m_mappingMotor->addItem(QStringLiteral("0x%1").arg(QString::number(id, 16).toUpper()), id);
    m_mappingMotor->setToolTip(QString::fromUtf8(u8"仅设置本条映射的电机。设备默认沿用 SDK 预置、设备参数或软件初始值。"));
    m_mappingValue = new QLineEdit(this); m_mappingValue->setObjectName("mappingValue");
    m_mappingValue->setToolTip(QString::fromUtf8(u8"固定整数。"));
    m_mappingMultiplier = new QLineEdit(this); m_mappingMultiplier->setObjectName("mappingMultiplier");
    m_mappingMultiplier->setToolTip(QString::fromUtf8(u8"客户字段乘以此倍数。"));
    editorForm->addRow(QString::fromUtf8(u8"客户命令"), m_mappingCode);
    m_groupInputMode = new QComboBox(this); m_groupInputMode->setObjectName("groupInputMode");
    m_groupInputMode->addItem(QString::fromUtf8(u8"读取客户参数"), "parameters");
    m_groupInputMode->addItem(QString::fromUtf8(u8"无参数报文（省略参数字段）"), "none");
    editorForm->addRow(QString::fromUtf8(u8"整组共用输入"), m_groupInputMode);
    m_mappingSteps = new QTableWidget(this); m_mappingSteps->setObjectName("mappingSteps");
    m_mappingSteps->setColumnCount(3);
    m_mappingSteps->setHorizontalHeaderLabels({QString::fromUtf8(u8"顺序"), QString::fromUtf8(u8"趋盛指令清单（可重复）"), QString::fromUtf8(u8"独立参数")});
    m_mappingSteps->setSelectionBehavior(QAbstractItemView::SelectRows); m_mappingSteps->setSelectionMode(QAbstractItemView::SingleSelection);
    m_mappingSteps->setEditTriggers(QAbstractItemView::NoEditTriggers); m_mappingSteps->verticalHeader()->hide();
    m_mappingSteps->verticalHeader()->setDefaultSectionSize(26);
    m_mappingSteps->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_mappingSteps->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_mappingSteps->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_mappingStepState = new QLabel(this);
    m_mappingStepState->setObjectName("mappingStepState");
    m_mappingStepState->setWordWrap(true);
    editorLayout->addWidget(m_mappingStepState);
    editorLayout->addWidget(m_mappingSteps, 1);
    auto *stepActions = new QHBoxLayout;
    auto *saveStep = new QPushButton(QString::fromUtf8(u8"添加到清单"), this); saveStep->setObjectName("saveMappingStep");
    m_saveMappingStep = saveStep;
    auto *newStep = new QPushButton(QString::fromUtf8(u8"新增下一条"), this); newStep->setObjectName("newMappingStep");
    auto *deleteStep = new QPushButton(QString::fromUtf8(u8"删除本条"), this); deleteStep->setObjectName("deleteMappingStep");
    auto *stepUp = new QPushButton(QString::fromUtf8(u8"上移"), this); stepUp->setObjectName("mappingStepUp");
    auto *stepDown = new QPushButton(QString::fromUtf8(u8"下移"), this); stepDown->setObjectName("mappingStepDown");
    auto *discardStep = new QPushButton(QString::fromUtf8(u8"放弃本条修改"), this); discardStep->setObjectName("discardMappingStep");
    for (auto *action : {saveStep, newStep, deleteStep, stepUp, stepDown, discardStep}) stepActions->addWidget(action);
    editorLayout->addLayout(stepActions);
    connect(saveStep, &QPushButton::clicked, this, [this]() { commitMappingStep(); });
    connect(newStep, &QPushButton::clicked, this, [this]() {
        if (!requireSavedMappingStep() || m_selectedStep < 0) return;
        loadMappingStep(-1);
    });
    connect(discardStep, &QPushButton::clicked, this, [this]() { loadMappingStep(m_selectedStep); });
    connect(deleteStep, &QPushButton::clicked, this, [this]() {
        if (m_selectedStep < 0) return;
        const int next = qMin(m_selectedStep, m_stepDrafts.size() - 2);
        m_stepDrafts.removeAt(m_selectedStep); m_stepDirty = false; refreshMappingSteps(); loadMappingStep(next);
    });
    const auto moveStep = [this](int direction) {
        if (!requireSavedMappingStep()) return;
        const int next = m_selectedStep + direction;
        if (m_selectedStep < 0 || next < 0 || next >= m_stepDrafts.size()) return;
        const auto step = m_stepDrafts.takeAt(m_selectedStep); m_stepDrafts.insert(next, step);
        refreshMappingSteps(); loadMappingStep(next);
    };
    connect(stepUp, &QPushButton::clicked, this, [moveStep]() { moveStep(-1); });
    connect(stepDown, &QPushButton::clicked, this, [moveStep]() { moveStep(1); });
    connect(m_mappingSteps, &QTableWidget::currentCellChanged, this, [this](int row, int, int, int) { selectMappingStep(row); });
    connect(m_groupInputMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        if (!m_loadingStep) { updateMappingSources(); refreshMappingSteps(); }
    });

    editorForm->addRow(QString::fromUtf8(u8"查找"), searchLine);
    editorForm->addRow(QString::fromUtf8(u8"趋盛指令"), m_mappingOperation);
    editorForm->addRow(QString::fromUtf8(u8"电机编号"), m_mappingMotor);
    editorForm->addRow(QString::fromUtf8(u8"取值来源"), m_mappingSource);
    editorForm->addRow(QString::fromUtf8(u8"固定数值"), m_mappingValue);
    editorForm->addRow(QString::fromUtf8(u8"换算倍数"), m_mappingMultiplier);
    editorLayout->insertLayout(1, editorForm);
    m_mappingCountsGroup = new QGroupBox(QString::fromUtf8(u8"新映射接收的参数项数"), this);
    auto *countsLayout = new QVBoxLayout(m_mappingCountsGroup);
    countsLayout->setContentsMargins(4, 4, 4, 4);
    m_mappingCounts = new QTableWidget(this); m_mappingCounts->setObjectName("mappingParameterCounts");
    m_mappingCounts->setColumnCount(2);
    m_mappingCounts->setHorizontalHeaderLabels({QString::fromUtf8(u8"整数列表字段"), QString::fromUtf8(u8"输入项数")});
    m_mappingCounts->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_mappingCounts->verticalHeader()->hide();
    m_mappingCounts->setToolTip(QString::fromUtf8(u8"列表用英文逗号分隔，例如50为1项，20,20,20为3项。实际项数必须完全相同，即使部分目标固定或重复引用。"));
    countsLayout->addWidget(m_mappingCounts);
    m_mappingCountsGroup->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    editorLayout->addWidget(m_mappingCountsGroup);
    m_valveEditor = new QGroupBox(QString::fromUtf8(u8"选择阀门与开关来源"), this);
    m_valveEditor->setObjectName("valveSelectionEditor");
    auto *valveLayout = new QVBoxLayout(m_valveEditor);
    valveLayout->setContentsMargins(6, 6, 6, 6);
    valveLayout->setSpacing(4);
    auto *valveForm = new QFormLayout;
    m_valveMode = new QComboBox(this); m_valveMode->setObjectName("valveMode");
    m_valveMode->addItem(QString::fromUtf8(u8"未确定（请检查取值来源；选择动作可改固定值）"), -1);
    m_valveMode->addItem(QString::fromUtf8(u8"关闭选中阀（其余不变）"), 0);
    m_valveMode->addItem(QString::fromUtf8(u8"开启选中阀（其余不变）"), 1);
    m_valveMode->addItem(QString::fromUtf8(u8"按位覆盖全部（未选阀关闭）"), 2);
    m_valveMode->addItem(QString::fromUtf8(u8"按客户参数开关（1开、0关）"), -2);
    qobject_cast<QStandardItemModel *>(m_valveMode->model())->item(0)->setEnabled(false);
    m_valveNumber = new QComboBox(this); m_valveNumber->setObjectName("valveNumber");
    m_valveNumber->setMaxVisibleItems(12);
    m_valveNumber->addItem(QString::fromUtf8(u8"未确定（请检查取值来源；选择阀号可改固定值）"), 0);
    qobject_cast<QStandardItemModel *>(m_valveNumber->model())->item(0)->setEnabled(false);
    for (int number = 1; number <= 30; ++number)
        m_valveNumber->addItem(QString::fromUtf8(u8"%1号阀（0x%2）").arg(number).arg(QString::number(0x30 + number, 16).toUpper()), 0x30 + number);
    valveForm->addRow(QString::fromUtf8(u8"动作"), m_valveMode);
    m_valveStateSource = new QComboBox(this); m_valveStateSource->setObjectName("valveStateSource");
    valveForm->addRow(QString::fromUtf8(u8"开关参数"), m_valveStateSource);
    valveForm->addRow(QString::fromUtf8(u8"仅开启的阀号"), m_valveNumber);
    valveLayout->addLayout(valveForm);
    m_valveActionHint = new QLabel(this); m_valveActionHint->setObjectName("valveActionHint"); m_valveActionHint->setWordWrap(true);
    valveLayout->addWidget(m_valveActionHint);
    m_valveChecksGroup = new QGroupBox(QString::fromUtf8(u8"选一个阀即单路，选多个即多路"), this);
    auto *valveChecksLayout = new QVBoxLayout(m_valveChecksGroup);
    valveChecksLayout->setContentsMargins(4, 4, 4, 4);
    auto *valveGroups = new QHBoxLayout;
    for (int group = 0; group < 3; ++group) {
        auto *box = new QGroupBox(QString::fromUtf8(u8"%1～%2号阀").arg(group * 10 + 1).arg(group * 10 + 10), this);
        auto *grid = new QGridLayout(box); grid->setContentsMargins(5, 5, 5, 5); grid->setVerticalSpacing(2);
        for (int item = 0; item < 10; ++item) {
            const int number = group * 10 + item + 1;
            auto *check = new QCheckBox(QString::fromUtf8(u8"%1号阀（0x%2）").arg(number).arg(QString::number(0x30 + number, 16).toUpper()), this);
            check->setObjectName("valveCheck" + QString::number(number));
            m_valveChecks.append(check); grid->addWidget(check, item % 5, item / 5);
            connect(check, &QCheckBox::toggled, this, [this]() {
                quint32 mask = 0;
                for (int index = 0; index < m_valveChecks.size(); ++index)
                    if (m_valveChecks[index]->isChecked()) mask |= quint32(1) << index;
                setValveConstant("valveMask", mask);
            });
        }
        valveGroups->addWidget(box);
    }
    valveChecksLayout->addLayout(valveGroups);
    auto *valveButtons = new QHBoxLayout;
    auto *allValves = new QPushButton(QString::fromUtf8(u8"全选"), this); allValves->setObjectName("selectAllValves");
    auto *noValves = new QPushButton(QString::fromUtf8(u8"清空选择"), this); noValves->setObjectName("clearValves");
    valveButtons->addWidget(allValves); valveButtons->addWidget(noValves); valveButtons->addStretch();
    valveChecksLayout->addLayout(valveButtons); valveLayout->addWidget(m_valveChecksGroup);
    connect(allValves, &QPushButton::clicked, this, [this]() { setValveConstant("valveMask", 0x3fffffff); });
    connect(noValves, &QPushButton::clicked, this, [this]() { setValveConstant("valveMask", 0); });
    const auto setMode = [this](int index) {
        const int value = m_valveMode->itemData(index).toInt();
        if (value >= 0) { m_valveStatePending = false; setValveConstant("mode", quint32(value)); }
        else if (value == -2) setValveStateSource();
    };
    const auto setNumber = [this](int index) { const int value = m_valveNumber->itemData(index).toInt(); if (value >= 0x31) setValveConstant("devId", quint32(value)); };
    connect(m_valveMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, setMode);
    connect(m_valveMode, QOverload<int>::of(&QComboBox::activated), this, setMode);
    connect(m_valveStateSource, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { setValveStateSource(); });
    connect(m_valveNumber, QOverload<int>::of(&QComboBox::currentIndexChanged), this, setNumber);
    connect(m_valveNumber, QOverload<int>::of(&QComboBox::activated), this, setNumber);
    m_valveRawParametersToggle = new QCheckBox(QString::fromUtf8(u8"原始参数"), this);
    m_valveRawParametersToggle->setObjectName("valveRawParametersToggle");
    valveLayout->addWidget(m_valveRawParametersToggle);
    connect(m_valveRawParametersToggle, &QCheckBox::toggled, this, [this]() { updateCatalogParameterVisibility(); });
    editorLayout->addWidget(m_valveEditor);
    editorLayout->addWidget(m_mappingParameters);
    auto *mappingButtons = new QHBoxLayout;
    auto *addMapping = new QPushButton(QString::fromUtf8(u8"添加命令映射"), this);
    addMapping->setObjectName("addMapping");
    auto *removeMapping = new QPushButton(QString::fromUtf8(u8"删除选中映射"), this);
    removeMapping->setObjectName("removeMapping");
    addMapping->setToolTip(QString::fromUtf8(u8"将下方填写的新映射添加到表格末尾。"));
    removeMapping->setToolTip(QString::fromUtf8(u8"删除当前选中的映射；保存前仍可重新导入配置。"));
    mappingButtons->addWidget(addMapping); mappingButtons->addWidget(removeMapping);
    removeMapping->setEnabled(false);
    mappingButtons->addStretch(); savedLayout->addLayout(mappingButtons);
    auto *mappingScroll = scrollPage(m_mappingEditor);
    mappingScroll->setObjectName("mappingEditorScroll");
    mappingSplitter->addWidget(savedMappings);
    mappingSplitter->addWidget(mappingScroll);
    mappingSplitter->setStretchFactor(0, 1);
    mappingSplitter->setStretchFactor(1, 1);
    mappingLayout->addWidget(mappingSplitter);
    tabs->addTab(mappingPage, QString::fromUtf8(u8"命令映射"));

    auto *devicePage = new QWidget(this);
    auto *deviceLayout = new QVBoxLayout(devicePage);
    auto *deviceGroup = new QGroupBox(QString::fromUtf8(u8"趋盛设备参数"), this);
    auto *deviceForm = new QFormLayout(deviceGroup);
    auto *motionDefaults = new QGroupBox(QString::fromUtf8(u8"默认运动参数"), this); motionDefaults->setObjectName("motionDefaults");
    auto *motionForm = new QFormLayout(motionDefaults);
    deviceLayout->setAlignment(Qt::AlignTop);
    for (auto *group : {deviceGroup, motionDefaults}) group->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    m_destination = new QSpinBox(this); m_destination->setRange(1, 255);
    m_source = new QSpinBox(this); m_source->setRange(1, 255);
    m_destination->setObjectName("deviceDestination"); m_source->setObjectName("deviceSource");
    m_devId = new QSpinBox(this); m_devId->setObjectName("defaultMotorId"); m_devId->setRange(0x11, 0x16); m_devId->setDisplayIntegerBase(16);
    m_action = new QSpinBox(this); m_action->setObjectName("legacyAbsoluteAction"); m_action->setRange(1, 3); m_action->setSingleStep(2);
    m_speed = new QSpinBox(this); m_speed->setObjectName("defaultMotorSpeed"); m_speed->setRange(1, 0xfffffe);
    m_minPosition = new QSpinBox(this); m_minPosition->setRange(0, 0xfffffe);
    m_maxPosition = new QSpinBox(this); m_maxPosition->setRange(0, 0xfffffe);
    m_minPosition->setObjectName("defaultMinPosition"); m_maxPosition->setObjectName("defaultMaxPosition");
    m_destination->setToolTip(QString::fromUtf8(u8"控制板地址，范围 1–255，必须与软件源地址不同。"));
    m_source->setToolTip(QString::fromUtf8(u8"软件地址，范围 1–255。"));
    m_devId->setToolTip(QString::fromUtf8(u8"电机编号 0x11–0x16，按设备资料确认。"));
    m_action->setToolTip(QString());
    m_speed->setToolTip(QString());
    m_minPosition->setToolTip(QString::fromUtf8(u8"设备位置数值下限，与目标位置使用同一单位。"));
    m_maxPosition->setToolTip(QString::fromUtf8(u8"设备位置数值上限，不允许 0xFFFFFF 的连续运动值。"));
    deviceForm->addRow(QString::fromUtf8(u8"控制板目标地址"), m_destination);
    deviceForm->addRow(QString::fromUtf8(u8"软件源地址"), m_source);
    motionForm->addRow(QString::fromUtf8(u8"电机编号（十六进制）"), m_devId);
    motionForm->addRow(QString::fromUtf8(u8"旧版绝对映射方向"), m_action);
    motionForm->addRow(QString::fromUtf8(u8"默认运动速度"), m_speed);
    motionForm->addRow(QString::fromUtf8(u8"最小位置"), m_minPosition);
    motionForm->addRow(QString::fromUtf8(u8"最大位置"), m_maxPosition);
    deviceLayout->addWidget(deviceGroup);
    deviceLayout->addWidget(motionDefaults);
    auto *presetGroup = new QGroupBox(QString::fromUtf8(u8"位置与校准参数"), this);
    presetGroup->setObjectName("parameterPresetsGroup");
    auto *presetLayout = new QVBoxLayout(presetGroup);
    presetLayout->addWidget(new QLabel(QString::fromUtf8(u8"原始整数，不自动换算；每组1～64项。"), this));
    m_parameterPresets = new QTableWidget(this); m_parameterPresets->setObjectName("parameterPresetsTable");
    m_parameterPresets->setColumnCount(2);
    m_parameterPresets->setHorizontalHeaderLabels({QString::fromUtf8(u8"名称"), QString::fromUtf8(u8"参数值（逗号分隔）")});
    m_parameterPresets->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_parameterPresets->verticalHeader()->hide();
    m_parameterPresets->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_parameterPresets->setMinimumHeight(110); m_parameterPresets->setMaximumHeight(180);
    presetLayout->addWidget(m_parameterPresets);
    auto *presetActions = new QHBoxLayout;
    auto *addPreset = new QPushButton(QString::fromUtf8(u8"添加参数组"), this); addPreset->setObjectName("addParameterPreset");
    auto *removePreset = new QPushButton(QString::fromUtf8(u8"删除选中参数组"), this); removePreset->setObjectName("removeParameterPreset");
    presetActions->addWidget(addPreset); presetActions->addWidget(removePreset); presetActions->addStretch();
    presetLayout->addLayout(presetActions); presetGroup->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    deviceLayout->addWidget(presetGroup);
    connect(addPreset, &QPushButton::clicked, this, [this]() {
        if (m_parameterPresets->rowCount() >= 64) { m_validationState->setText(QString::fromUtf8(u8"最多64组位置与校准参数。")); return; }
        addParameterPresetRow(); m_parameterPresets->setCurrentCell(m_parameterPresets->rowCount() - 1, 0); parameterPresetsChanged();
    });
    connect(removePreset, &QPushButton::clicked, this, [this]() {
        if (m_parameterPresets->currentRow() < 0) return;
        m_parameterPresets->removeRow(m_parameterPresets->currentRow()); parameterPresetsChanged();
    });
    connect(m_parameterPresets, &QTableWidget::cellChanged, this, [this]() { parameterPresetsChanged(); });

    auto *serialGroup = new QGroupBox(QString::fromUtf8(u8"下游串口连接"), this);
    serialGroup->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    auto *serialLayout = new QHBoxLayout(serialGroup);
    m_port = new QComboBox(this);
    m_port->setObjectName("downstreamPort");
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
    deviceLayout->addStretch();
    tabs->addTab(scrollPage(devicePage), QString::fromUtf8(u8"设备与连接"));

    auto *advancedPage = new QWidget(this);
    advancedPage->setObjectName("advancedPage");
    auto *advancedLayout = new QVBoxLayout(advancedPage);
    advancedLayout->addWidget(m_draftVersion); advancedLayout->addWidget(m_activeVersion);
    auto *advancedForm = new QFormLayout;
    advancedForm->addRow(m_byteOrderLabel, m_byteOrder);
    advancedLayout->addLayout(advancedForm);
    m_advancedJson = new QTextEdit(this);
    m_advancedJson->setObjectName("advancedJson");
    m_advancedJson->setFontFamily(QStringLiteral("Consolas"));
    advancedLayout->addWidget(m_advancedJson);
    auto *applyJson = new QPushButton(QString::fromUtf8(u8"应用 JSON"), this);
    applyJson->setObjectName("applyJson");
    applyJson->setToolTip(QString::fromUtf8(u8"保存当前 JSON 并刷新中文表单；有效配置自动启用。"));
    advancedLayout->addWidget(applyJson);
    advancedPage->hide();

    auto *configButtons = new QHBoxLayout;
    auto *saveConfig = new QPushButton(QString::fromUtf8(u8"保存配置"), this);
    saveConfig->setObjectName("saveConfig");
    saveConfig->setToolTip(QString::fromUtf8(u8"保存各页配置，下次打开自动恢复。\n%1")
                           .arg(QDir::toNativeSeparators(m_draftStore.filePath())));
    auto *importButton = new QPushButton(QString::fromUtf8(u8"导入配置"), this);
    importButton->setObjectName("importConfig");
    auto *exportButton = new QPushButton(QString::fromUtf8(u8"导出配置"), this);
    exportButton->setObjectName("exportConfig");
    importButton->setToolTip(QString::fromUtf8(u8"从 JSON 文件导入并保存，有效配置自动启用。"));
    exportButton->setToolTip(QString::fromUtf8(u8"将已保存配置导出为 JSON 文件。"));
    const int configWidth = qMax(saveConfig->sizeHint().width(), qMax(importButton->sizeHint().width(), exportButton->sizeHint().width()));
    for (auto *button : {saveConfig, importButton, exportButton}) {
        button->setFixedWidth(configWidth); configButtons->addWidget(button);
    }
    configButtons->addWidget(m_validationState, 1);
    root->addLayout(configButtons);

    auto *debugGroup = new QGroupBox(QString::fromUtf8(u8"调试与运行"), this);
    auto *debugLayout = new QVBoxLayout(debugGroup);
    auto *inputLayout = new QHBoxLayout;
    m_inputEncoding = new QLabel(QString::fromUtf8(u8"配置编码：未确定"), this);
    m_input = new QLineEdit(this);
    m_input->setObjectName("customerInput");
    m_input->setPlaceholderText(QString::fromUtf8(u8"客户报文"));
    m_previewButton = new QPushButton(QString::fromUtf8(u8"预览"), this);
    m_previewButton->setObjectName("previewButton");
    m_sendButton = new QPushButton(QString::fromUtf8(u8"发送"), this);
    m_sendButton->setObjectName("sendButton");
    m_cancelButton = new QPushButton(QString::fromUtf8(u8"取消本组"), this);
    m_cancelButton->setObjectName("cancelButton");
    m_cancelButton->setToolTip(QString::fromUtf8(u8"取消最近组的本地等待和剩余发送，不向设备发送停止，也不回滚已发动作。"));
    m_previewButton->setToolTip(QString::fromUtf8(u8"使用当前配置解析和组帧，不访问串口。"));
    inputLayout->addWidget(m_inputEncoding);
    inputLayout->addWidget(m_input, 1);
    auto *parseButton = new QPushButton(QString::fromUtf8(u8"解析格式"), this); parseButton->setObjectName("parseFormatButton");
    parseButton->setToolTip(QString::fromUtf8(u8"仅按当前顺序字段解析，不要求映射或设备参数，不生成目标帧。"));
    inputLayout->addWidget(parseButton);
    connect(parseButton, &QPushButton::clicked, this, &Widget::parseFormatPreview);
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
    auto *communicationTabs = new QTabWidget(this); communicationTabs->setObjectName("communicationTabs");
    communicationTabs->addTab(scrollPage(debugGroup), QString::fromUtf8(u8"手动调试"));
    auto *upstreamPage = new QWidget(this);
    auto *upstreamLayout = new QVBoxLayout(upstreamPage);
    auto *upstreamPorts = new QHBoxLayout;
    m_providedPort = new QLabel(QString::fromUtf8(u8"提供给上游的串口：尚未初始化"), this);
    m_providedPort->setObjectName("providedUpstreamPort"); m_providedPort->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_virtualComAction = new QPushButton(QString::fromUtf8(u8"重试"), this); m_virtualComAction->setObjectName("virtualComAction");
    m_virtualComAction->setToolTip(QString::fromUtf8(u8"首次初始化需授权安装虚拟串口驱动，并创建软件专用的一对端口。"));
    m_upstreamState = new QLabel(QString::fromUtf8(u8"正在检查虚拟串口"), this); m_upstreamState->setObjectName("upstreamState"); m_upstreamState->setWordWrap(true);
    upstreamPorts->addWidget(m_providedPort, 1); upstreamPorts->addWidget(m_virtualComAction);
    upstreamLayout->addLayout(upstreamPorts);
    upstreamLayout->addWidget(m_upstreamState);
    auto *upstreamHint = new QLabel(QString::fromUtf8(u8"上游软件选择上面提供的串口。115200 · 8 数据位 · 无校验 · 1 停止位 · 无流控\n按已保存的客户格式接收与解析。"), this);
    upstreamHint->setWordWrap(true); upstreamLayout->addWidget(upstreamHint);
    auto *upstreamStats = new QHBoxLayout;
    m_upstreamCounters = new QLabel(this); m_upstreamCounters->setObjectName("upstreamCounters"); upstreamStats->addWidget(m_upstreamCounters, 1);
    auto *upstreamClear = new QPushButton(QString::fromUtf8(u8"清空接收"), this); upstreamClear->setObjectName("upstreamClear"); upstreamStats->addWidget(upstreamClear);
    upstreamLayout->addLayout(upstreamStats);
    auto *receiveResults = new QTabWidget(this); receiveResults->setObjectName("upstreamResults");
    m_upstreamRaw = new QPlainTextEdit(this); m_upstreamRaw->setObjectName("upstreamRaw"); m_upstreamRaw->setReadOnly(true); m_upstreamRaw->setMaximumBlockCount(500);
    m_upstreamFrames = new QPlainTextEdit(this); m_upstreamFrames->setObjectName("upstreamFrames"); m_upstreamFrames->setReadOnly(true); m_upstreamFrames->setMaximumBlockCount(500);
    receiveResults->addTab(m_upstreamRaw, QString::fromUtf8(u8"原始数据 · ASCII / HEX"));
    receiveResults->addTab(m_upstreamFrames, QString::fromUtf8(u8"完整报文与字段"));
    upstreamLayout->addWidget(receiveResults, 1);
    communicationTabs->addTab(upstreamPage, QString::fromUtf8(u8"上游虚拟串口"));
    tabs->insertTab(2, communicationTabs, QString::fromUtf8(u8"报文调试"));
    updateUpstreamCounters();
    connect(upstreamClear, &QPushButton::clicked, this, [this]() {
        m_upstream->clear(); m_upstreamBytes = m_upstreamFramesCount = m_upstreamErrors = 0;
        m_upstreamRaw->clear(); m_upstreamFrames->clear(); updateUpstreamCounters();
    });
    connect(m_virtualComAction, &QPushButton::clicked, this, [this]() {
        if (m_virtualCom->isBusy()) return;
        if (m_virtualNeedsInitialization) m_virtualCom->initialize();
        else m_virtualCom->refresh();
    });
    connect(m_virtualCom, &VirtualComProvider::needsInitialization, this, [this](bool needed) {
        m_virtualNeedsInitialization = needed;
        if (needed) {
            m_upstream->close();
            m_providedPublicPort.clear(); m_providedInternalPort.clear();
            m_providedPort->setText(QString::fromUtf8(u8"提供给上游的串口：尚未初始化"));
            m_upstreamState->setText(QString::fromUtf8(u8"请初始化虚拟串口"));
            refreshPorts();
        }
        m_virtualComAction->setText(needed ? QString::fromUtf8(u8"初始化虚拟串口") : QString::fromUtf8(u8"重试"));
        m_virtualComAction->setVisible(needed || !m_upstream->isConnected());
    });
    connect(m_virtualCom, &VirtualComProvider::busyChanged, this, [this](bool busy) {
        m_virtualComAction->setEnabled(!busy);
    });
    connect(m_virtualCom, &VirtualComProvider::statusChanged, this, [this](const QString &message) {
        m_upstreamState->setText(message);
    });
    connect(m_virtualCom, &VirtualComProvider::ready, this, [this](const QString &publicPort, const QString &internalPort) {
        m_providedPublicPort = publicPort; m_providedInternalPort = internalPort;
        m_providedPort->setText(QString::fromUtf8(u8"提供给上游的串口：%1").arg(publicPort));
        refreshPorts();
        const QString downstream = normalizedPortName(m_port->currentData().toString());
        if (m_transport->isConnected() && (downstream == normalizedPortName(publicPort) || downstream == normalizedPortName(internalPort))) {
            m_upstreamState->setText(QString::fromUtf8(u8"虚拟串口正用于下游连接，请先断开该下游连接后重试"));
            m_virtualComAction->show(); return;
        }
        if (!m_upstream->isConnected() || normalizedPortName(m_upstream->portName()) != normalizedPortName(internalPort))
            m_upstream->open(internalPort, 115200);
    });
    connect(m_upstream, &UpstreamSerialReceiver::connectionChanged, this, [this](bool connected, const QString &reason) {
        m_virtualComAction->setVisible(!connected);
        m_upstreamState->setText(connected ? QString::fromUtf8(u8"等待上游报文") : reason);
    });
    connect(m_upstream, &UpstreamSerialReceiver::rawReceived, this, [this](const QByteArray &bytes) {
        m_upstreamState->setText(QString::fromUtf8(u8"接收中"));
        m_upstreamBytes += quint64(bytes.size()); updateUpstreamCounters();
        const QByteArray shown = bytes.left(2048);
        m_upstreamRaw->appendPlainText(QString::fromUtf8(u8"收到 %1 字节\nASCII  %2\nHEX    %3")
            .arg(bytes.size()).arg(visibleBytes(shown), QString::fromLatin1(shown.toHex(' ').toUpper())));
        if (bytes.size() > shown.size()) m_upstreamRaw->appendPlainText(QString::fromUtf8(u8"本次显示前 %1 字节，字节统计包含全部数据").arg(shown.size()));
    });
    connect(m_upstream, &UpstreamSerialReceiver::frameReceived, this, [this](const QByteArray &frame) {
        ConversionPreview parsed; TranslationError error;
        if (!ConversionEngine(m_upstreamConfig).parsePreview(frame, &parsed, &error)) {
            ++m_upstreamErrors; updateUpstreamCounters();
            m_upstreamFrames->appendPlainText(QString::fromUtf8(u8"解析失败：%1").arg(error.message)); return;
        }
        ++m_upstreamFramesCount; updateUpstreamCounters();
        QStringList lines{QString::fromUtf8(u8"完整有效帧 %1 · %2 字节").arg(m_upstreamFramesCount).arg(frame.size()),
                          visibleBytes(frame.left(2048))};
        for (const QString &name : parsed.fieldOrder) {
            const QVariant value = parsed.fields.value(name);
            lines += fieldPreview(name, value);
            if (value.type() == QVariant::List)
                lines += QString::fromUtf8(u8"  实际项数：%1").arg(value.toList().size());
        }
        lines += parsed.notices;
        const QString text = lines.join('\n');
        m_upstreamFrames->appendPlainText(text.left(8192));
        if (text.size() > 8192) m_upstreamFrames->appendPlainText(QString::fromUtf8(u8"本帧显示已截断"));
    });
    connect(m_upstream, &UpstreamSerialReceiver::errorOccurred, this, [this](const QString &reason) {
        ++m_upstreamErrors; updateUpstreamCounters(); m_upstreamState->setText(reason);
        m_upstreamFrames->appendPlainText(QString::fromUtf8(u8"接收提示：%1").arg(reason.left(2048)));
    });

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
    connect(addMapping, &QPushButton::clicked, this, [this]() {
        QJsonObject mapping;
        if (!readGroupMapping(&mapping)) return;
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
    connect(saveConfig, &QPushButton::clicked, this, &Widget::saveConfiguration);
    connect(applyJson, &QPushButton::clicked, this, &Widget::applyAdvancedJson);
    connect(importButton, &QPushButton::clicked, this, &Widget::importDraft);
    connect(exportButton, &QPushButton::clicked, this, &Widget::exportDraft);
    connect(refreshPort, &QPushButton::clicked, this, &Widget::refreshPorts);
}

void Widget::connectSignals()
{
    for (auto *line : {m_mappingValue, m_mappingMultiplier})
        connect(line, &QLineEdit::textChanged, this, [this]() {
            if (!m_loadingStep && !m_loadingMapping) { m_stepDirty = true; updateMappingStepState(); }
        });
    for (auto *box : {m_mappingSource, m_mappingMotor, m_mappingOperation})
        connect(box, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
            if (!m_loadingStep && !m_loadingMapping) { m_stepDirty = true; updateMappingStepState(); }
        });
    connect(m_advancedJson, &QTextEdit::textChanged, this, [this]() {
        if (m_loadingUi) return;
        m_jsonDirty = true;
        m_draftVersion->setText(QString::fromUtf8(u8"配置：JSON 未应用"));
        m_validationState->clear();
        updateLiveState();
    });
    connect(m_encoding, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        const bool binary = m_encoding->currentData().toString() == "hex";
        m_byteOrderLabel->setVisible(binary);
        m_byteOrder->setVisible(binary);
        m_header->setPlaceholderText(binary ? QString::fromUtf8(u8"例如 AA55") : QString::fromUtf8(u8"例如 /"));
        m_footer->setPlaceholderText(binary ? QString::fromUtf8(u8"例如 0D0A") : QString::fromUtf8(u8"例如 R\\r"));
        markDraftDirty();
        updateMappingSources();
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
            const bool previousMotion = isMotionEditor(m_editorOperation);
            m_mappingMotor->setCurrentIndex(0);
            // 新动作重新选择取值方式；已有表格内容不受影响。
            updateMappingSources();
            if (!isMotionEditor(operation)) {
                selectData(m_mappingSource, isQuery(operation) || QushengCommandCatalog::isStopOrReset(operation) ? "@none" : "@fields");
                loadCatalogParameters(QJsonObject());
            } else if (!previousMotion) {
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
    connect(m_mappingSource, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { updateMappingSources(); updateMappingControls(); });
    for (QSpinBox *spin : {m_destination, m_source, m_devId, m_action, m_speed, m_minPosition, m_maxPosition})
        connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() {
            markDraftDirty();
        });
    connect(m_devId, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        if (!m_loadingUi && m_unresolvedMotorId && value >= 0x11) {
            m_unresolvedMotorId = false;
            m_devId->setSpecialValueText(QString()); m_devId->setMinimum(0x11);
        }
    });
    connect(m_connectButton, &QPushButton::clicked, this, [this]() {
        if (m_transport->isConnected()) m_transport->close();
        else if (!m_port->currentData().toString().isEmpty()
                 && (normalizedPortName(m_port->currentData().toString()) == normalizedPortName(m_providedPublicPort)
                     || normalizedPortName(m_port->currentData().toString()) == normalizedPortName(m_providedInternalPort)))
            m_validationState->setText(QString::fromUtf8(u8"此串口属于上游虚拟串口，请选择其他下游串口"));
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
            m_replyResult->append(lines.join('\n'));
        } else {
            m_replyResult->append(QString::fromUtf8(u8"请求 %1 失败\n错误代码：%2\n原因：%3")
                .arg(result.requestId).arg(result.error.code, result.error.message));
        }
    };
    // 每份已解码回告立即显示；运动接纳、执行完成与本地等待终止仍是不同状态。
    connect(m_service, &TranslationService::replyReceived, this, showReply);
    connect(m_service, &TranslationService::completed, this, [showReply](const UnifiedResult &result) {
        if (!result.success) showReply(result); // 本地超时或取消也必须保留可见原因。
    });
    connect(m_service, &TranslationService::groupProgress, this, [this](quint64 group, int step, int total, const QString &detail) {
        m_requestState->setText(QString::fromUtf8(u8"组%1：第%2/%3条；%4").arg(group).arg(step).arg(total).arg(detail));
        m_requestState->setProperty("groupProgressText", m_requestState->text());
        appendLog(m_requestState->text());
    });
    connect(m_service, &TranslationService::groupCompleted, this, [this](const UnifiedGroupResult &result) {
        const QString location = result.failedStep > 0 ? QString::fromUtf8(u8"第%1条停止").arg(result.failedStep)
            : result.acceptedSteps > 0 ? QString::fromUtf8(u8"剩余指令已停止") : QString::fromUtf8(u8"发送前已停止");
        const QString detail = result.success ? QString::fromUtf8(u8"全部收到成功应答；动作完成未确认")
            : QString::fromUtf8(u8"%1：%2；剩余%3条未发送").arg(location, result.error.message).arg(result.remainingSteps);
        const QString text = QString::fromUtf8(u8"组%1：已接纳%2/%3条；%4").arg(result.groupId).arg(result.acceptedSteps).arg(result.totalSteps).arg(detail);
        m_replyResult->append(text); m_requestState->setText(text); appendLog(text);
        m_requestState->setProperty("groupProgressText", text);
    });
    connect(m_service, &TranslationService::requestStateChanged, this,
            [this](quint64 id, RequestState state, const QString &detail) {
        // 请求可以已经显示成功，但仍等本地写完；也可能取消的是排队停止而原运动仍在执行。
        // 是否还有请求由管理器决定，不能仅凭最近一个状态信号推断串口可用。
        m_requestActive = m_requests->hasPendingRequests();
        const QString groupText = m_requestState->property("groupProgressText").toString();
        m_requestState->setText(groupText.isEmpty() ? QString::fromUtf8(u8"执行状态：请求 %1，%2；%3")
                                .arg(id).arg(stateText(state), detail) : groupText);
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
            m_draftRoot = draft; loadUiFromDraft(); activateSavedConfiguration();
            appendLog(QString::fromUtf8(u8"已恢复本地配置：") + QDir::toNativeSeparators(m_draftStore.filePath()));
            return;
        }
    }
    QJsonObject example;
    if (DraftStore(QStringLiteral(":/examples/dt-simple-commands.json")).load(&example, nullptr)) {
        m_draftRoot = example;
        loadUiFromDraft();
        if (!restoreError.isValid()) activateSavedConfiguration();
        m_input->setText(QStringLiteral("/1A2000R\\r"));
        appendLog(QString::fromUtf8(u8"已载入 DT 风格单条命令验证配置，默认按数值直接转换；DT 不是程序内置协议。"));
    } else {
        appendLog(QString::fromUtf8(u8"未找到示例配置，请导入 JSON 配置。"));
    }
    if (restoreError.isValid()) {
        m_validationState->setText(QString::fromUtf8(u8"恢复失败：%1；原文件已保留\n%2")
                                   .arg(restoreError.message, QDir::toNativeSeparators(m_draftStore.filePath())));
        appendLog(m_validationState->text());
    }
}

bool Widget::persistDraft(const QJsonObject &draft)
{
    TranslationError error;
    if (m_draftStore.save(draft, &error)) return true;
    // 写盘失败不得显示“已保存”，也不得校验旧内存值来掩盖当前未保存的编辑。
    if (!m_jsonDirty) m_formDirty = true;
    m_draftVersion->setText(QString::fromUtf8(u8"配置：未保存"));
    m_validationState->setText(QString::fromUtf8(u8"保存失败：%1\n%2")
                               .arg(error.message, QDir::toNativeSeparators(m_draftStore.filePath())));
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
    name->setData(Qt::UserRole, field.value("name").toString()); name->setData(Qt::UserRole + 1, field); m_fieldTable->setItem(row, 1, name);
    auto *role = combo(this, {{QString::fromUtf8(u8"命令码"), QStringLiteral("command")},
                              {QString::fromUtf8(u8"参数"), QStringLiteral("parameter")},
                              {QString::fromUtf8(u8"固定字段"), QStringLiteral("fixed")},
                              {QString::fromUtf8(u8"普通字段"), QStringLiteral("metadata")},
                              {QString::fromUtf8(u8"执行控制（仅解析）"), QStringLiteral("execution")},
                              {QString::fromUtf8(u8"校验值（未校验）"), QStringLiteral("checksum")}});
    auto *type = combo(this, {{QStringLiteral("ASCII"), QStringLiteral("ascii")},
                              {QString::fromUtf8(u8"无符号整数"), QStringLiteral("uint")},
                              {QString::fromUtf8(u8"有符号整数"), QStringLiteral("int")},
                              {QString::fromUtf8(u8"整数列表"), QStringLiteral("integerList")}});
    type->setToolTip(QString::fromUtf8(u8"整数列表仅用于ASCII参数：英文逗号分隔有符号整数，单项合法；不接受空项、空白或小数，上下限逐项检查。"));
    auto *lengthMode = combo(this, {{QString::fromUtf8(u8"固定长度"), QStringLiteral("fixed")},
                                    {QString::fromUtf8(u8"读取到帧尾"), QStringLiteral("toFooter")},
                                    {QString::fromUtf8(u8"按命令码识别"), QStringLiteral("mappedCommand")},
                                    {QString::fromUtf8(u8"读到分隔符"), QStringLiteral("delimited")}});
    selectData(role, field.value("role").toString("parameter"));
    selectData(type, field.value("type").toString("ascii"));
    selectData(lengthMode, field.value("lengthMode").toString("fixed"));
    m_fieldTable->setCellWidget(row, 2, role);
    m_fieldTable->setCellWidget(row, 3, type);
    m_fieldTable->setCellWidget(row, 4, lengthMode);
    m_fieldTable->setItem(row, 5, new QTableWidgetItem(field.value("lengthMode").toString() == "delimited"
        ? field.value("delimiter").toString() : field.contains("length") ? QString::number(field.value("length").toInt()) : QString()));
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
    if (mapping.contains("steps")) {
        QStringList labels;
        const auto steps = mapping.value("steps").toArray();
        for (int i = 0; i < steps.size(); ++i) labels.append(QString::number(i + 1) + ". " + operationName(steps[i].toObject().value("operation").toString()));
        m_mappingTable->item(row, 1)->setText(QString::fromUtf8(u8"共%1条指令").arg(steps.size()));
        const QString issue = mappingFieldIssue(mapping);
        m_mappingTable->item(row, 2)->setText(labels.join(QString::fromUtf8(u8" → ")) + (issue.isEmpty() ? QString() : QString::fromUtf8(u8"；失效：") + issue));
        return;
    }
    const QString operation = mapping.value("operation").toString();
    m_mappingTable->item(row, 1)->setText(operationName(operation)
        + (QushengCommandCatalog::isExecutable(operation) ? QString() : QString::fromUtf8(u8"（待实现）")));
    QString summary = QString::fromUtf8(u8"无需目标参数");
    if (isMotionEditor(operation)) {
        const QJsonObject expression = mapping.value("parameters").toObject().value(mappingParameterKey(operation)).toObject();
        if (expression.value("kind").toString() == "constant") summary = QString::fromUtf8(u8"固定数值 %1").arg(expression.value("value").toVariant().toString());
        else summary = sourceName(expression) + (expression.value("kind").toString() == "linear"
                     ? QString::fromUtf8(u8" × %1").arg(multiplierText(expression)) : QString());
        if (expression.value("offset").toDouble() != 0 || expression.value("round").toString("exact") != "exact")
            summary += QString::fromUtf8(u8"（高级配置）");
    } else if (operation == "QUSHENG_21" || operation == "QUSHENG_22") {
        const auto parameters = mapping.value("parameters").toObject();
        QMap<QString, QJsonObject> definitions;
        for (const auto &value : QushengCommandCatalog::find(operation).value("parameters").toArray()) {
            const auto definition = value.toObject(); definitions.insert(definition.value("key").toString(), definition);
        }
        const auto source = [&](const QString &key) {
            const auto expression = parameters.value(key).toObject();
            if (expression.isEmpty()) return parameterDefaultText(definitions.value(key), operation);
            if (expression.value("kind").toString() == "constant")
                return QString::fromUtf8(u8"固定值 %1").arg(expression.value("value").toVariant().toString());
            return sourceName(expression) + (expression.value("kind").toString() == "linear"
                ? QString::fromUtf8(u8"（高级换算）") : QString());
        };
        const auto staticValue = [&](const QString &key, qlonglong minimum, qlonglong maximum, qlonglong *result) {
            const auto expression = parameters.value(key).toObject();
            QJsonValue value;
            if (expression.isEmpty()) {
                const auto preset = m_draftRoot.value("sdkDefaults").toObject().value(operation).toObject();
                value = preset.contains(key) ? preset.value(key) : definitions.value(key).value("default");
            } else if (expression.value("kind").toString() == "constant") value = expression.value("value");
            else return false;
            bool ok = false; const qlonglong number = value.toVariant().toLongLong(&ok);
            if (!ok || !value.isDouble() || value.toDouble() != double(number) || number < minimum || number > maximum) return false;
            *result = number; return true;
        };
        qlonglong mask = 0, mode = 0, number = 0;
        if (operation == "QUSHENG_22") {
            summary = staticValue("devId", 0x31, 0x4e, &number)
                ? QString::fromUtf8(u8"开启 %1号阀").arg(number - 0x30)
                : QString::fromUtf8(u8"仅开启；阀号按%1决定").arg(source("devId"));
        } else {
            const bool maskKnown = staticValue("valveMask", 0, 0x3fffffff, &mask);
            const bool modeKnown = staticValue("mode", 0, 2, &mode);
            QStringList valves;
            if (maskKnown) for (int bit = 0; bit < 30; ++bit)
                if (mask & (qlonglong(1) << bit)) valves.append(QString::number(bit + 1));
            const QString target = maskKnown ? (valves.isEmpty() ? QString::fromUtf8(u8"未选阀")
                : QString::fromUtf8(u8"%1号阀").arg(valves.join(QString::fromUtf8(u8"、"))))
                : QString::fromUtf8(u8"阀号按%1决定").arg(source("valveMask"));
            if (modeKnown && mode == 2)
                summary = maskKnown && mask == 0 ? QString::fromUtf8(u8"全部阀关闭")
                    : QString::fromUtf8(u8"开启选中阀，未选阀关闭；%1").arg(target);
            else if (modeKnown)
                summary = maskKnown && mask == 0 ? QString::fromUtf8(u8"未选阀（无操作）")
                    : QString::fromUtf8(u8"%1 %2，其余不变").arg(mode == 1 ? QString::fromUtf8(u8"开启") : QString::fromUtf8(u8"关闭"), target);
            else {
                const auto allowed = parameters.value("mode").toObject().value("allowedValues").toArray();
                summary = QString::fromUtf8(u8"%1；%2决定开关%3").arg(target, source("mode"),
                    allowed.size() == 2 && allowed.contains(0) && allowed.contains(1)
                    ? QString::fromUtf8(u8"（1开0关）") : QString::fromUtf8(u8"（原控制方式0/1/2）"));
            }
        }
        summary += QString::fromUtf8(u8"；开启时间：%1").arg(source("duration"));
    } else {
        QStringList references; const QJsonObject parameters = mapping.value("parameters").toObject();
        for (const QJsonValue &value : QushengCommandCatalog::find(operation).value("parameters").toArray()) {
            const QJsonObject definition = value.toObject(); if (!definition.value("editable").toBool()) continue;
            if (definition.value("key").toString() == "devId" && QushengCommandCatalog::isStopOrReset(operation)) continue;
            const QJsonObject relation = parameters.value(definition.value("key").toString()).toObject();
            const QString source = relation.isEmpty() ? parameterDefaultText(definition, operation)
                : relation.value("kind").toString() == "constant" ? relation.value("value").toVariant().toString()
                : sourceName(relation) + (relation.value("kind").toString() == "linear"
                  ? QString::fromUtf8(u8" × %1（高级）").arg(multiplierText(relation)) : QString());
            references << definition.value("label").toString() + " = " + source;
        }
        if (!references.isEmpty()) summary = references.join(QString::fromUtf8(u8"；"));
    }
    if (isMotionEditor(operation) || QushengCommandCatalog::isStopOrReset(operation)) {
        const QJsonObject motor = mapping.value("parameters").toObject().value("devId").toObject();
        const QString selected = motor.isEmpty() ? QString::fromUtf8(u8"设备默认")
            : motor.value("kind").toString() == "constant"
              ? QStringLiteral("0x%1").arg(motor.value("value").toInt(), 2, 16, QLatin1Char('0'))
              : motor.value("field").toString();
        // 停止和复位也显示本条实际选择，不用全局电机标签代替。
        summary.prepend(QString::fromUtf8(u8"电机 %1；").arg(selected));
    }
    const QJsonObject counts = mapping.value("input").toObject().value("parameterCounts").toObject();
    for (auto it = counts.begin(); it != counts.end(); ++it)
        summary += QString::fromUtf8(u8"；%1 输入%2项").arg(it.key()).arg(it.value().toVariant().toString());
    if (!mapping.value("input").toObject().value("omitFields").toArray().isEmpty())
        summary += legacyMappedInput() ? QString::fromUtf8(u8"；旧短报文省略字段") : QString::fromUtf8(u8"；客户参数不用于取值，字段仍解析");
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
    m_valveStatePending = false;
    const QSignalBlocker rawBlocker(m_valveRawParametersToggle);
    m_valveRawParametersToggle->setChecked(false);
    const QSignalBlocker blocker(m_mappingParameters);
    const QString operation = m_mappingOperation->currentData().toString();
    m_mappingParameters->setProperty("operation", operation); m_mappingParameters->setProperty("originalParameters", QVariant::fromValue(parameters));
    m_mappingParameters->horizontalHeaderItem(2)->setText(operation == "QUSHENG_C1" ? QString::fromUtf8(u8"固定值 / 公式") : QString::fromUtf8(u8"固定值"));
    m_mappingParameters->setRowCount(0);
    if (isMotionEditor(operation)) return;
    for (const QJsonValue &value : QushengCommandCatalog::find(operation).value("parameters").toArray()) {
        const QJsonObject parameter = value.toObject(); if (!parameter.value("editable").toBool()) continue;
        const QString key = parameter.value("key").toString(); const QJsonObject expression = parameters.value(key).toObject();
        if (key == "devId" && QushengCommandCatalog::isStopOrReset(operation)) continue;
        const int row = m_mappingParameters->rowCount(); m_mappingParameters->insertRow(row);
        auto *label = new QTableWidgetItem(parameter.value("label").toString()); label->setFlags(label->flags() & ~Qt::ItemIsEditable);
        const QString parameterTip = parameter.value("label").toString();
        if (expression.value("kind").toString() == "linear")
            label->setText(label->text() + QString::fromUtf8(u8"（高级）"));
        label->setToolTip(parameterTip); label->setData(Qt::UserRole, parameter); label->setData(Qt::UserRole + 1, expression); label->setData(Qt::UserRole + 2, false);
        m_mappingParameters->setItem(row, 0, label);
        auto *source = new QComboBox(m_mappingParameters); source->addItem(parameterDefaultText(parameter, operation), "");
        addNumericSources(source, mappingOmittedFields());
        source->addItem(QString::fromUtf8(u8"固定值"), "@constant");
        addFormulaSource(source, operation, key);
        const bool formula = expression.value("kind").toString() == "formula";
        const QString selected = formula ? "@formula" : expression.value("kind").toString() == "constant" ? "@constant" : expression.value("field").toString();
        restoreSource(source, selected, !formula && expression.contains("itemIndex"), expression.value("itemIndex").toInt(), formula ? QStringLiteral("formula") : expression.value("scope").toString("input")); source->setProperty("previousIndex", source->currentIndex()); source->setToolTip(QString::fromUtf8(u8"客户字段或固定值。")); m_mappingParameters->setCellWidget(row, 1, source);
        const QString emptyHint = source->itemText(0).contains(QString::fromUtf8(u8"默认"))
            ? QString::fromUtf8(u8"无需填写") : QString::fromUtf8(u8"先选择来源");
        auto *fixed = formulaAxis(operation, key) ? static_cast<QLineEdit *>(new AxisFormulaEdit(m_mappingParameters)) : new QLineEdit(m_mappingParameters); fixed->setText(expression.value("value").toVariant().toString()); fixed->setPlaceholderText(!isPresetSource(source) && selected == "@constant" ? QString::fromUtf8(u8"填写整数")
            : selected.isEmpty() ? emptyHint : QString::fromUtf8(u8"无需填写"));
        fixed->setEnabled(!isPresetSource(source) && selected == "@constant"); fixed->setToolTip(parameter.value("label").toString()); m_mappingParameters->setCellWidget(row, 2, fixed);
        connect(source, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, label, source, fixed, emptyHint, row]() {
            if (isFormulaSource(source)) {
                if (!editAxisFormula(row)) {
                    const QSignalBlocker blocker(source); source->setCurrentIndex(source->property("previousIndex").toInt());
                }
                source->setProperty("previousIndex", source->currentIndex()); updateFormulaCell(row); return;
            }
            source->setProperty("previousIndex", source->currentIndex());
            if (!m_updatingValveState && m_mappingOperation->currentData().toString() == "QUSHENG_21"
                    && label->data(Qt::UserRole).toJsonObject().value("key").toString() == "mode") {
                m_valveStatePending = false;
                if (!isPresetSource(source) && (source->currentData().toString().isEmpty() || source->currentData().toString() == "@constant")) {
                    auto expression = label->data(Qt::UserRole + 1).toJsonObject();
                    expression.remove("allowedValues"); label->setData(Qt::UserRole + 1, expression);
                }
            }
            if (!m_loadingStep && !m_loadingMapping) { m_stepDirty = true; updateMappingStepState(); }
            label->setData(Qt::UserRole + 2, true); const QString chosen = source->currentData().toString();
            fixed->setEnabled(!isPresetSource(source) && chosen == "@constant");
            fixed->setPlaceholderText(!isPresetSource(source) && chosen == "@constant" ? QString::fromUtf8(u8"填写整数")
                : chosen.isEmpty() ? emptyHint : QString::fromUtf8(u8"无需填写"));
            updateValveEditor(); updateFormulaCell(row);
        });
        connect(fixed, &QLineEdit::textChanged, this, [this, label]() {
            label->setData(Qt::UserRole + 2, true);
            if (!m_loadingStep && !m_loadingMapping) { m_stepDirty = true; updateMappingStepState(); }
            updateValveEditor();
        });
        if (formulaAxis(operation, key)) {
            auto *edit = static_cast<AxisFormulaEdit *>(fixed); edit->button->setObjectName("axisFormulaButton_" + key);
            connect(edit->button, &QPushButton::clicked, this, [this, row]() { editAxisFormula(row); });
            updateFormulaCell(row);
        }
    }
    // 少量参数不铺开大块空白；较多参数保留表内滚动，外层说明仍可继续滚动。
    m_mappingParameters->setFixedHeight(qMin(190, qMax(76, (m_mappingParameters->rowCount() + 1) * 30 + 6)));
    updateValveEditor();
}

void Widget::updateFormulaCell(int row)
{
    auto *label = m_mappingParameters->item(row, 0);
    if (!label || !formulaAxis(m_mappingOperation->currentData().toString(), label->data(Qt::UserRole).toJsonObject().value("key").toString())) return;
    auto *source = qobject_cast<QComboBox *>(m_mappingParameters->cellWidget(row, 1));
    auto *edit = static_cast<AxisFormulaEdit *>(m_mappingParameters->cellWidget(row, 2));
    const bool formula = isFormulaSource(source); const QSignalBlocker blocker(edit);
    if (formula) edit->setText(label->data(Qt::UserRole + 1).toJsonObject().value("formula").toString());
    else if (edit->property("showingFormula").toBool()) edit->clear();
    edit->setProperty("showingFormula", formula);
    edit->setReadOnly(formula); edit->setTextMargins(0,0,formula ? 34 : 0,0);
    edit->setEnabled(formula || (!isPresetSource(source) && source->currentData().toString() == "@constant"));
    edit->button->setVisible(formula);
    edit->setToolTip(formula ? QString::fromUtf8(u8"公式：") + label->data(Qt::UserRole + 1).toJsonObject().value("formula").toString()
                            : label->data(Qt::UserRole).toJsonObject().value("label").toString());
}

bool Widget::editAxisFormula(int row)
{
    auto *label = m_mappingParameters->item(row, 0);
    if (!label || !formulaAxis(m_mappingOperation->currentData().toString(), label->data(Qt::UserRole).toJsonObject().value("key").toString())) return false;
    const auto original = label->data(Qt::UserRole + 1).toJsonObject();
    QDialog dialog(this); dialog.setObjectName("axisFormulaDialog"); dialog.setWindowTitle(QString::fromUtf8(u8"轴位置公式")); dialog.resize(620,500);
    auto *layout = new QVBoxLayout(&dialog);
    auto *formula = new QLineEdit(&dialog); formula->setObjectName("formulaText"); formula->setPlaceholderText("p1_1+(a-1)*90");
    if (original.value("kind").toString() == "formula") formula->setText(original.value("formula").toString());
    layout->addWidget(new QLabel(QString::fromUtf8(u8"公式（支持 + - * / 和括号）"), &dialog)); layout->addWidget(formula);
    auto *round = new QComboBox(&dialog); round->setObjectName("formulaRounding");
    for (const auto &entry : QList<QPair<QString,QString>>{{QString::fromUtf8(u8"四舍五入成整数"),"nearest"},{QString::fromUtf8(u8"要求整数（小数报错）"),"exact"},{QString::fromUtf8(u8"向下取整"),"floor"},{QString::fromUtf8(u8"向上取整"),"ceil"}}) round->addItem(entry.first,entry.second);
    selectData(round, original.value("kind").toString() == "formula" ? original.value("round").toString("nearest") : QStringLiteral("nearest"));
    auto *form = new QFormLayout; form->addRow(QString::fromUtf8(u8"结果处理"),round); layout->addLayout(form);
    auto *variables = new QTableWidget(&dialog); variables->setObjectName("formulaVariables"); variables->setColumnCount(2);
    variables->setHorizontalHeaderLabels({QString::fromUtf8(u8"变量"),QString::fromUtf8(u8"取值来源（双击插入）")});
    variables->verticalHeader()->hide(); variables->horizontalHeader()->setSectionResizeMode(0,QHeaderView::ResizeToContents); variables->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);
    variables->setSelectionBehavior(QAbstractItemView::SelectRows); variables->setSelectionMode(QAbstractItemView::SingleSelection); variables->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(variables,1);
    QJsonObject input, counts;
    for (int i=0;i<m_mappingCounts->rowCount();++i) counts.insert(m_mappingCounts->item(i,0)->text(),qobject_cast<QSpinBox *>(m_mappingCounts->cellWidget(i,1))->value());
    if (!counts.isEmpty()) input.insert("parameterCounts",counts);
    const auto omitted = mappingOmittedFields(); if (!omitted.isEmpty()) input.insert("omitFields",omitted);
    auto referenceIssue = [&](const QJsonObject &binding) { return mappingFieldIssue(QJsonObject{{"parameters",QJsonObject{{"value",binding}}},{"input",input}}); };
    auto identity = [](const QJsonObject &binding) { return QString::fromUtf8(QJsonDocument(QJsonObject{{"scope",binding.value("scope").toString("input")},{"field",binding.value("field")},{"itemIndex",binding.value("itemIndex")}}).toJson(QJsonDocument::Compact)); };
    QSet<QString> aliases, bindings;
    auto addVariable = [&](QString alias, const QJsonObject &binding) {
        const auto key = identity(binding); if (bindings.contains(key)) return;
        QString unique = alias; int suffix=2; while (aliases.contains(unique)) unique = alias+"_"+QString::number(suffix++);
        aliases.insert(unique); bindings.insert(key);
        const int i=variables->rowCount(); variables->insertRow(i);
        auto *name = new QTableWidgetItem(unique); name->setData(Qt::UserRole,binding); variables->setItem(i,0,name);
        const auto issue = referenceIssue(binding);
        auto *description = new QTableWidgetItem(sourceName(binding)+(issue.isEmpty()?QString():QString::fromUtf8(u8"（失效）"))); description->setToolTip(issue); variables->setItem(i,1,description);
    };
    // Existing aliases always win over newly suggested names. Neither a preset
    // rename nor a different list order is allowed to change an existing binding.
    const auto existing = original.value("kind").toString() == "formula" ? original.value("variables").toObject() : QJsonObject();
    for (auto it=existing.begin();it!=existing.end();++it) {
        const int i=variables->rowCount(); variables->insertRow(i); aliases.insert(it.key()); bindings.insert(identity(it.value().toObject()));
        auto *name=new QTableWidgetItem(it.key()); name->setData(Qt::UserRole,it.value().toObject()); variables->setItem(i,0,name);
        const auto issue=referenceIssue(it.value().toObject()); auto *description=new QTableWidgetItem(sourceName(it.value().toObject())+(issue.isEmpty()?QString():QString::fromUtf8(u8"（失效）"))); description->setToolTip(issue); variables->setItem(i,1,description);
    }
    QComboBox available; addNumericSources(&available,omitted);
    int inputIndex=0; QMap<QString,int> presetNumbers; int number=0;
    for (const auto &entry:parameterPresetRecords()) presetNumbers.insert(entry.toObject().value("id").toString(),++number);
    for (int i=0;i<available.count();++i) {
        available.setCurrentIndex(i); QJsonObject binding{{"kind","field"},{"field",available.currentData().toString()}};
        if (isListSource(&available)) binding.insert("itemIndex",sourceItemIndex(&available)); applySourceScope(&binding,&available);
        QString alias;
        if (isPresetSource(&available)) alias=QStringLiteral("p%1_%2").arg(presetNumbers.value(available.currentData().toString())).arg(sourceItemIndex(&available));
        else { alias=QString(QChar('a'+inputIndex%26)); if(inputIndex>=26)alias+=QString::number(inputIndex/26); ++inputIndex; }
        addVariable(alias,binding);
    }
    auto insertVariable = [&]() { const int i=variables->currentRow(); if(i>=0){formula->insert(variables->item(i,0)->text());formula->setFocus();} };
    auto *insert = new QPushButton(QString::fromUtf8(u8"插入选中变量"),&dialog); insert->setObjectName("formulaInsert"); layout->addWidget(insert);
    connect(insert,&QPushButton::clicked,&dialog,insertVariable); connect(variables,&QTableWidget::cellDoubleClicked,&dialog,[&](int,int){insertVariable();});
    auto *error = new QLabel(&dialog); error->setObjectName("formulaError"); error->setWordWrap(true); error->setStyleSheet("color:#a32020;"); layout->addWidget(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog); buttons->button(QDialogButtonBox::Ok)->setObjectName("formulaAccept"); buttons->button(QDialogButtonBox::Cancel)->setObjectName("formulaCancel");
    buttons->button(QDialogButtonBox::Ok)->setText(QString::fromUtf8(u8"确定")); buttons->button(QDialogButtonBox::Cancel)->setText(QString::fromUtf8(u8"取消")); layout->addWidget(buttons);
    QJsonObject accepted;
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]() {
        QSet<QString> used; QString issue;
        if(!NumericFormula::inspect(formula->text(),&used,&issue)){error->setText(issue);return;}
        QJsonObject selected;
        for (const auto &alias:used) {
            QJsonObject binding; bool found=false;
            for(int i=0;i<variables->rowCount();++i)if(variables->item(i,0)->text()==alias){binding=variables->item(i,0)->data(Qt::UserRole).toJsonObject();found=true;break;}
            if(!found){error->setText(QString::fromUtf8(u8"变量“%1”未绑定，请从表中插入变量。").arg(alias));return;}
            issue=referenceIssue(binding); if(!issue.isEmpty()){error->setText(QString::fromUtf8(u8"变量“%1”的来源失效：%2").arg(alias,issue));return;} selected.insert(alias,binding);
        }
        accepted=original;
        for(const QString &key:{"field","scope","itemIndex","value","numerator","denominator","offset"})accepted.remove(key);
        accepted.insert("kind","formula"); accepted.insert("formula",formula->text()); accepted.insert("variables",selected); accepted.insert("round",round->currentData().toString()); dialog.accept();
    });
    if(dialog.exec()!=QDialog::Accepted)return false;
    label->setData(Qt::UserRole+1,accepted);label->setData(Qt::UserRole+2,true);
    auto *source=qobject_cast<QComboBox *>(m_mappingParameters->cellWidget(row,1));
    {const QSignalBlocker blocker(source);restoreSource(source,"@formula",false,0,"formula");}
    source->setProperty("previousIndex",source->currentIndex()); updateFormulaCell(row);
    if(!m_loadingStep&&!m_loadingMapping){m_stepDirty=true;updateMappingStepState();}
    return true;
}

bool Widget::valveValue(const QString &key, quint32 *value, QString *origin) const
{
    const QString operation = m_mappingOperation->currentData().toString();
    for (int row = 0; row < m_mappingParameters->rowCount(); ++row) {
        const auto *label = m_mappingParameters->item(row, 0);
        const auto definition = label->data(Qt::UserRole).toJsonObject();
        if (definition.value("key").toString() != key) continue;
        auto *source = qobject_cast<QComboBox *>(m_mappingParameters->cellWidget(row, 1));
        auto *fixed = qobject_cast<QLineEdit *>(m_mappingParameters->cellWidget(row, 2));
        if (!source || !fixed) break;
        QString text;
        if (!isPresetSource(source) && source->currentData().toString() == "@constant") {
            *origin = QString::fromUtf8(u8"固定值"); text = fixed->text().trimmed();
        } else if (source->currentData().toString().isEmpty()) {
            *origin = parameterDefaultText(definition, operation);
            const auto preset = m_draftRoot.value("sdkDefaults").toObject().value(operation).toObject();
            text = (preset.contains(key) ? preset.value(key) : definition.value("default")).toVariant().toString();
        } else {
            *origin = (isPresetSource(source) ? QString::fromUtf8(u8"预设来源：") : QString::fromUtf8(u8"客户来源：")) + source->currentText();
            if (label->data(Qt::UserRole + 1).toJsonObject().value("kind").toString() == "linear")
                *origin += QString::fromUtf8(u8"（保留线性关系）");
            return false;
        }
        bool ok = false; const qlonglong number = text.toLongLong(&ok);
        if (!ok || number < definition.value("min").toDouble() || number > definition.value("max").toDouble()) {
            *origin += QString::fromUtf8(u8"尚未设置或无效"); return false;
        }
        *value = quint32(number); return true;
    }
    *origin = QString::fromUtf8(u8"尚未设置"); return false;
}

void Widget::setValveConstant(const QString &key, quint32 value)
{
    const QString operation = m_mappingOperation->currentData().toString();
    if (operation != "QUSHENG_21" && operation != "QUSHENG_22") return;
    for (int row = 0; row < m_mappingParameters->rowCount(); ++row) {
        if (m_mappingParameters->item(row, 0)->data(Qt::UserRole).toJsonObject().value("key").toString() != key) continue;
        auto *source = qobject_cast<QComboBox *>(m_mappingParameters->cellWidget(row, 1));
        auto *fixed = qobject_cast<QLineEdit *>(m_mappingParameters->cellWidget(row, 2));
        if (!source || !fixed) return;
        if (key == "mode") {
            auto expression = m_mappingParameters->item(row, 0)->data(Qt::UserRole + 1).toJsonObject();
            const bool constraintRemoved = expression.contains("allowedValues");
            expression.remove("allowedValues"); m_mappingParameters->item(row, 0)->setData(Qt::UserRole + 1, expression);
            m_mappingParameters->item(row, 0)->setData(Qt::UserRole + 2, true);
            if (constraintRemoved && !m_loadingStep && !m_loadingMapping) { m_stepDirty = true; updateMappingStepState(); }
        }
        // 通过原参数编辑入口改变对应关系；不接触时长或其它表达式及扩展元数据。
        // Source and value form one selector transaction; an empty intermediate
        // value must not open the raw editor as if the saved mapping were invalid.
        m_updatingValveState = true;
        selectData(source, "@constant"); fixed->setText(QString::number(value));
        m_updatingValveState = false;
        updateValveEditor(); return;
    }
}

void Widget::setValveStateSource()
{
    if (m_mappingOperation->currentData().toString() != "QUSHENG_21") return;
    for (int row = 0; row < m_mappingParameters->rowCount(); ++row) {
        auto *label = m_mappingParameters->item(row, 0);
        if (label->data(Qt::UserRole).toJsonObject().value("key").toString() != "mode") continue;
        auto *source = qobject_cast<QComboBox *>(m_mappingParameters->cellWidget(row, 1));
        auto expression = label->data(Qt::UserRole + 1).toJsonObject();
        expression.insert("kind", "field"); expression.insert("field", m_valveStateSource->currentData().toString());
        expression.insert("allowedValues", QJsonArray{0, 1});
        applySourceScope(&expression, m_valveStateSource);
        for (const QString &key : {"value", "numerator", "denominator", "offset", "round", "itemIndex"}) expression.remove(key);
        if (isListSource(m_valveStateSource)) expression.insert("itemIndex", sourceItemIndex(m_valveStateSource));
        m_valveStatePending = expression.value("field").toString().isEmpty();
        m_updatingValveState = true;
        restoreSource(source, expression.value("field").toString(), expression.contains("itemIndex"), expression.value("itemIndex").toInt(), expression.value("scope").toString("input"));
        m_updatingValveState = false;
        label->setData(Qt::UserRole + 1, expression); label->setData(Qt::UserRole + 2, true);
        label->setText(label->data(Qt::UserRole).toJsonObject().value("label").toString());
        if (!m_loadingStep && !m_loadingMapping) { m_stepDirty = true; updateMappingStepState(); }
        updateValveEditor(); return;
    }
}

void Widget::updateValveEditor()
{
    // The helper updates the raw source and expression together. Do not classify
    // the intermediate source change as an imported advanced expression.
    if (!m_valveEditor || m_updatingValveState) return;
    const QString operation = m_mappingOperation->currentData().toString();
    const bool group = operation == "QUSHENG_21", single = operation == "QUSHENG_22";
    m_valveEditor->setVisible(group || single);
    if (!group && !single) return;
    m_valveChecksGroup->setVisible(group); m_valveMode->setVisible(group); m_valveNumber->setVisible(single);
    bool stateParameter = m_valveStatePending;
    QComboBox *rawModeSource = nullptr;
    for (int row = 0; group && row < m_mappingParameters->rowCount(); ++row) {
        const auto *label = m_mappingParameters->item(row, 0);
        if (label->data(Qt::UserRole).toJsonObject().value("key").toString() != "mode") continue;
        rawModeSource = qobject_cast<QComboBox *>(m_mappingParameters->cellWidget(row, 1));
        const auto expression = label->data(Qt::UserRole + 1).toJsonObject();
        const auto allowed = expression.value("allowedValues").toArray();
        stateParameter = stateParameter || (rawModeSource && !rawModeSource->currentData().toString().isEmpty()
            && (isPresetSource(rawModeSource) || rawModeSource->currentData().toString() != "@constant") && allowed.size() == 2 && allowed.contains(0) && allowed.contains(1));
    }
    {
        const QSignalBlocker stateBlocker(m_valveStateSource);
        m_valveStateSource->clear(); m_valveStateSource->addItem(QString::fromUtf8(u8"请选择开关参数"), "");
        addNumericSources(m_valveStateSource, mappingOmittedFields());
        if (rawModeSource && (isPresetSource(rawModeSource) || rawModeSource->currentData().toString() != "@constant"))
            restoreSource(m_valveStateSource, rawModeSource->currentData().toString(), isListSource(rawModeSource), sourceItemIndex(rawModeSource), sourceScope(rawModeSource));
    }
    m_valveStateSource->setVisible(group && stateParameter);
    auto *form = qobject_cast<QFormLayout *>(m_valveEditor->layout()->itemAt(0)->layout());
    if (form) {
        if (auto *label = form->labelForField(m_valveMode)) label->setVisible(group);
        if (auto *label = form->labelForField(m_valveNumber)) label->setVisible(single);
        if (auto *label = form->labelForField(m_valveStateSource)) label->setVisible(group && stateParameter);
    }
    bool advancedSource = false;
    quint32 mode = 0, mask = 0, number = 0; QString modeOrigin, maskOrigin, numberOrigin;
    if (group) {
        const bool modeKnown = valveValue("mode", &mode, &modeOrigin);
        const bool maskKnown = valveValue("valveMask", &mask, &maskOrigin);
        advancedSource = !maskKnown || (!modeKnown && !stateParameter);
        for (int row = 0; row < m_mappingParameters->rowCount(); ++row) {
            const auto *label = m_mappingParameters->item(row, 0);
            if (label->data(Qt::UserRole).toJsonObject().value("key").toString() == "mode"
                    && label->data(Qt::UserRole + 1).toJsonObject().value("kind").toString() == "linear") advancedSource = true;
        }
        const QSignalBlocker modeBlocker(m_valveMode);
        m_valveMode->setCurrentIndex(m_valveMode->findData(stateParameter ? -2 : modeKnown ? int(mode) : -1));
        QStringList selected;
        for (int index = 0; index < m_valveChecks.size(); ++index) {
            const QSignalBlocker blocker(m_valveChecks[index]);
            const bool checked = maskKnown && (mask & (quint32(1) << index));
            m_valveChecks[index]->setChecked(checked);
            if (checked) selected.append(QString::number(index + 1));
        }
        QString effect;
        if (stateParameter) effect = QString::fromUtf8(u8"1开、0关，其余阀不变；其他值拒绝转换。")
            + (m_valveStatePending ? QString::fromUtf8(u8"请选择开关参数。") : QString());
        else if (!modeKnown) effect = QString::fromUtf8(u8"保留原控制方式来源：0关闭选中、1开启选中、2覆盖全部；请在下表查看来源。可改选“按客户参数开关”限制为1开0关。");
        else if (modeKnown && mode == 2) effect = QString::fromUtf8(u8"按位覆盖全部：选中阀开启，未选阀全部关闭。");
        else effect = QString::fromUtf8(u8"仅开关选中阀，其余不变。");
        if (maskKnown && mask == 0)
            effect += modeKnown && mode == 2 ? QString::fromUtf8(u8" 当前未选阀，将关闭全部阀。")
                                            : QString::fromUtf8(u8" 当前未选阀；开/关选中模式不会操作任何阀。");
        m_valveActionHint->setText(effect + QString::fromUtf8(u8" 开启时间0使用设备超时设置。"));
        m_valveActionHint->setStyleSheet(modeKnown && mode == 2 ? "color:#9b4300;font-weight:bold;" : QString());
        const QString selection = maskKnown ? (selected.isEmpty() ? QString::fromUtf8(u8"未选阀")
            : QString::fromUtf8(u8"已选%1号阀").arg(selected.join(QString::fromUtf8(u8"、"))))
            : QString::fromUtf8(u8"尚不能显示固定阀号，请查看来源；勾选将改为固定阀号");
        m_valveEditor->setToolTip(QString::fromUtf8(u8"动作来源：%1；阀号来源：%2。%3。勾选阀号只更新阀位图，开关参数和开启时间来源保持独立。")
            .arg(modeOrigin, maskOrigin, selection));
    } else {
        const bool known = valveValue("devId", &number, &numberOrigin);
        advancedSource = !known;
        const QSignalBlocker blocker(m_valveNumber);
        m_valveNumber->setCurrentIndex(m_valveNumber->findData(known ? int(number) : 0));
        m_valveActionHint->setStyleSheet(QString());
        m_valveActionHint->setText(QString::fromUtf8(u8"仅开启；时间0不等于关闭。关闭请用0x21。"));
        m_valveEditor->setToolTip(QString::fromUtf8(u8"阀号来源：%1。%2；选择阀号会改用固定阀号，客户参数来源和时间仍可在下表设置。")
            .arg(numberOrigin, known ? QString::fromUtf8(u8"当前%1号阀，协议编号0x%2").arg(number - 0x30).arg(QString::number(number, 16).toUpper())
                                    : QString::fromUtf8(u8"尚未确定固定阀号，请查看实际来源")));
    }
    if (advancedSource) {
        const QSignalBlocker blocker(m_valveRawParametersToggle);
        m_valveRawParametersToggle->setChecked(true);
    }
    updateCatalogParameterVisibility();
}

void Widget::updateCatalogParameterVisibility()
{
    const QString operation = m_mappingOperation->currentData().toString();
    const bool valve = operation == "QUSHENG_21" || operation == "QUSHENG_22";
    const bool raw = m_valveRawParametersToggle->isChecked();
    int visible = 0;
    for (int row = 0; row < m_mappingParameters->rowCount(); ++row) {
        const QString key = m_mappingParameters->item(row, 0)->data(Qt::UserRole).toJsonObject().value("key").toString();
        const bool hidden = valve && !raw && (key == "mode" || key == "valveMask" || key == "devId");
        m_mappingParameters->setRowHidden(row, hidden);
        if (!hidden) ++visible;
    }
    m_mappingParameters->setFixedHeight(qMin(190, 30 + visible * 30));
    scheduleMappingLayout();
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
    if (m_mappingOperation->currentData().toString() == "QUSHENG_21" && m_valveStatePending) {
        *reason = QString::fromUtf8(u8"请选择决定1开、0关的开关参数。"); return false;
    }
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
        if (isFormulaSource(source)) {
            parameters->insert(key, expression); continue;
        }
        if (expression.value("kind").toString() == "formula") {
            expression.remove("formula"); expression.remove("variables"); expression.remove("round");
        }
        if (!isPresetSource(source) && selected == "@constant") {
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
        if ((isPresetSource(source) || selected != "@constant") && isListSource(source))
            expression.insert("itemIndex", sourceItemIndex(source));
        else expression.remove("itemIndex");
        if (expression.value("kind").toString() == "constant") expression.remove("scope");
        else applySourceScope(&expression, source);
        parameters->insert(key, expression);
    }
    return true;
}

void Widget::refreshMappingSteps()
{
    const QSignalBlocker blocker(m_mappingSteps);
    m_mappingSteps->setRowCount(0);
    for (int i = 0; i < m_stepDrafts.size(); ++i) {
        const auto step = m_stepDrafts[i].toObject();
        m_mappingSteps->insertRow(i);
        m_mappingSteps->setItem(i, 0, new QTableWidgetItem(QString::number(i + 1)));
        m_mappingSteps->setItem(i, 1, new QTableWidgetItem(operationName(step.value("operation").toString())));
        QStringList parameters;
        const auto relations = step.value("parameters").toObject();
        for (auto it = relations.begin(); it != relations.end(); ++it) {
            const auto expression = it.value().toObject();
            parameters.append(displayName(it.key()) + "=" + (expression.value("kind").toString() == "constant"
                ? expression.value("value").toVariant().toString() : sourceName(expression)));
        }
        m_mappingSteps->setItem(i, 2, new QTableWidgetItem(parameters.join(QString::fromUtf8(u8"；"))));
    }
    m_mappingSteps->setMinimumHeight(qMin(240, qMax(96, 26 + 26 * m_stepDrafts.size())));
    m_mappingSteps->setVisible(!m_stepDrafts.isEmpty());
    if (m_selectedStep >= 0 && m_selectedStep < m_stepDrafts.size()) m_mappingSteps->setCurrentCell(m_selectedStep, 0);
    updateMappingControls();
}

bool Widget::commitMappingStep()
{
    QJsonObject mapping;
    if (!readNewMapping(&mapping)) return false;
    if (m_selectedStep < 0 && m_stepDrafts.size() >= 64) {
        m_validationState->setText(QString::fromUtf8(u8"一组最多64条指令")); return false;
    }
    if (m_stepDrafts.isEmpty()) {
        const QSignalBlocker blocker(m_groupInputMode);
        selectData(m_groupInputMode, mapping.value("input").toObject().value("omitFields").toArray().isEmpty() ? "parameters" : "none");
    }
    m_groupInput = mapping.value("input").toObject();
    mapping.remove("commandCode"); mapping.remove("input");
    if (m_selectedStep < 0) { m_selectedStep = m_stepDrafts.size(); m_stepDrafts.append(mapping); }
    else m_stepDrafts[m_selectedStep] = mapping;
    m_stepOriginal = mapping; m_stepDirty = false;
    refreshMappingSteps(); updateMappingSources();
    m_validationState->setText(QString::fromUtf8(u8"第%1条已保存，清单共%2条；完成后点击“添加命令映射”").arg(m_selectedStep + 1).arg(m_stepDrafts.size()));
    return true;
}

bool Widget::requireSavedMappingStep()
{
    if (!m_stepDirty) return true;
    m_validationState->setText(QString::fromUtf8(u8"本条尚未保存到清单，请先点击“%1”或“放弃本条修改”；本次操作未执行。")
        .arg(m_selectedStep < 0 ? QString::fromUtf8(u8"添加到清单") : QString::fromUtf8(u8"保存本条修改")));
    return false;
}

void Widget::updateMappingStepState()
{
    const bool editing = m_selectedStep >= 0;
    findChild<QPushButton *>("newMappingStep")->setVisible(editing);
    findChild<QPushButton *>("deleteMappingStep")->setVisible(editing);
    findChild<QPushButton *>("mappingStepUp")->setVisible(m_stepDrafts.size() > 1);
    findChild<QPushButton *>("mappingStepDown")->setVisible(m_stepDrafts.size() > 1);
    findChild<QPushButton *>("discardMappingStep")->setVisible(m_stepDirty);
    m_mappingStepState->setVisible(editing || m_stepDirty || !m_stepDrafts.isEmpty());
    m_saveMappingStep->setText(editing ? QString::fromUtf8(u8"保存本条修改") : QString::fromUtf8(u8"添加到清单"));
    m_mappingStepState->setText(editing
        ? QString::fromUtf8(u8"正在编辑第%1条%2；清单共%3条").arg(m_selectedStep + 1)
            .arg(m_stepDirty ? QString::fromUtf8(u8"（未保存修改）") : QString::fromUtf8(u8"（已保存）")).arg(m_stepDrafts.size())
        : QString::fromUtf8(u8"正在新增指令（尚未入清单）；清单共%1条%2").arg(m_stepDrafts.size())
            .arg(m_stepDirty ? QString::fromUtf8(u8"；请添加到清单或放弃修改") : QString()));
    scheduleMappingLayout();
}

void Widget::loadMappingStep(int row)
{
    const QString templateOperation = m_mappingOperation->currentData().toString();
    const int templateMotor = m_mappingMotor->currentData().toInt();
    const bool existing = row >= 0 && row < m_stepDrafts.size();
    m_loadingStep = true; m_loadingMapping = true;
    m_selectedStep = row;
    m_stepOriginal = existing ? m_stepDrafts[row].toObject() : QJsonObject();
    const QString operation = existing ? m_stepOriginal.value("operation").toString() : templateOperation;
    if (m_mappingOperation->findData(operation) < 0) m_mappingOperation->addItem(operationName(operation), operation);
    selectData(m_mappingOperation, operation); m_editorOperation = operation;
    m_mappingValue->clear(); m_mappingMultiplier->setText("1");
    m_mappingMotor->setCurrentIndex(existing ? 0 : m_mappingMotor->findData(templateMotor));
    updateMappingSources();
    const auto parameters = m_stepOriginal.value("parameters").toObject();
    if (isMotionEditor(operation)) {
        const auto expression = parameters.value(mappingParameterKey(operation)).toObject();
        const bool fixedInput = !m_stepDrafts.isEmpty() && m_groupInputMode->currentData().toString() == "none";
        const QString selected = expression.value("kind").toString() == "constant" || (expression.isEmpty() && fixedInput)
            ? "@constant" : expression.value("field").toString();
        restoreSource(m_mappingSource, selected, expression.contains("itemIndex"), expression.value("itemIndex").toInt(), expression.value("scope").toString("input"));
        m_mappingValue->setText(expression.value("value").toVariant().toString());
        m_mappingMultiplier->setText(expression.value("kind").toString() == "linear" ? multiplierText(expression) : QStringLiteral("1"));
    } else selectData(m_mappingSource, isQuery(operation) || QushengCommandCatalog::isStopOrReset(operation) ? "@none" : "@fields");
    const auto motor = parameters.value("devId").toObject();
    if (motor.value("kind").toString() == "constant") m_mappingMotor->setCurrentIndex(m_mappingMotor->findData(motor.value("value").toInt()));
    loadCatalogParameters(parameters);
    m_stepDirty = false; m_loadingMapping = false; m_loadingStep = false;
    const QSignalBlocker blocker(m_mappingSteps);
    if (row >= 0) m_mappingSteps->setCurrentCell(row, 0); else { m_mappingSteps->clearSelection(); m_mappingSteps->setCurrentCell(-1, -1); }
    m_validationState->clear();
    updateMappingControls();
}

void Widget::selectMappingStep(int row)
{
    if (m_switchingStep || m_loadingStep || row == m_selectedStep || row < 0) return;
    m_switchingStep = true;
    if (!requireSavedMappingStep()) {
        const QSignalBlocker blocker(m_mappingSteps);
        m_mappingSteps->clearSelection();
        m_mappingSteps->setCurrentCell(m_selectedStep, 0);
        m_switchingStep = false; return;
    }
    loadMappingStep(row); m_switchingStep = false;
}

bool Widget::readGroupMapping(QJsonObject *mapping)
{
    if (m_stepDrafts.isEmpty()) return readNewMapping(mapping);
    if (!requireSavedMappingStep()) return false;
    QJsonObject input = m_groupInput;
    const auto omitted = mappingOmittedFields();
    input.insert("omitFields", omitted);
    QJsonObject counts;
    for (int row = 0; row < m_mappingCounts->rowCount(); ++row)
        counts.insert(m_mappingCounts->item(row, 0)->text(), qobject_cast<QSpinBox *>(m_mappingCounts->cellWidget(row, 1))->value());
    if (counts.isEmpty()) input.remove("parameterCounts"); else input.insert("parameterCounts", counts);
    *mapping = QJsonObject{{"commandCode", m_mappingCode->text().trimmed()}, {"input", input}};
    if (m_stepDrafts.size() == 1) {
        const auto step = m_stepDrafts.first().toObject();
        for (auto it = step.begin(); it != step.end(); ++it) mapping->insert(it.key(), it.value());
    } else mapping->insert("steps", m_stepDrafts);
    QJsonObject draft;
    if (!collectUiDraft(&draft)) return false;
    ProtocolConfig candidate; candidate.root = draft; candidate.root.insert("mappings", QJsonArray{*mapping}); TranslationError error;
    if (!candidate.validate(&error)) {
        m_validationState->setText(QString::fromUtf8(u8"添加失败：%1").arg(error.message)); return false;
    }
    return true;
}

void Widget::resetMappingForm()
{
    m_loadingMapping=true;
    m_stepDrafts = QJsonArray(); m_groupInput = QJsonObject(); m_stepOriginal = QJsonObject(); m_selectedStep = -1; m_stepDirty = false;
    refreshMappingSteps();
    m_mappingCode->clear(); m_mappingSearch->clear();
    selectData(m_mappingOperation,"MOTOR_FORWARD"); m_editorOperation="MOTOR_FORWARD";
    m_mappingValue->clear(); m_mappingMultiplier->setText("1");
    m_mappingMotor->setCurrentIndex(0);
    m_listCounts.clear();
    updateMappingSources(); selectData(m_mappingSource,"");
    loadCatalogParameters(QJsonObject()); updateMappingControls();
    m_loadingMapping=false;
}

bool Widget::legacyMappedInput() const
{
    for (int row = 0; row < m_fieldTable->rowCount(); ++row)
        if (comboData(m_fieldTable, row, 2) == "command")
            return comboData(m_fieldTable, row, 4) == "mappedCommand";
    return false;
}

QJsonArray Widget::mappingOmittedFields() const
{
    QJsonArray omitted;
    const bool none = !m_stepDrafts.isEmpty() ? m_groupInputMode->currentData().toString() == "none" : !isPresetSource(m_mappingSource) && m_mappingSource->currentData().toString() == "@none";
    if (!none) return omitted;
    bool commandSeen = false;
    for (int row = 0; row < m_fieldTable->rowCount(); ++row) {
        const QString role = comboData(m_fieldTable, row, 2);
        if (role == "command") commandSeen = true;
        else if (commandSeen && role == "parameter") omitted.append(itemText(m_fieldTable, row, 1));
    }
    return omitted;
}

void Widget::fieldFormatChanged()
{
    if (m_loadingUi || m_syncingFields) return;
    markDraftDirty();
    if (!synchronizeFieldNames()) { refreshMappingSummaries(); return; }
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
        for (int row = 0; row < m_mappingTable->rowCount(); ++row)
            updateMappingSummary(row, renameMappingFields(m_mappingTable->item(row, 0)->data(Qt::UserRole).toJsonObject(), renames));
        for (int i = 0; i < m_stepDrafts.size(); ++i) m_stepDrafts[i] = renameMappingFields(m_stepDrafts[i].toObject(), renames);
        m_stepOriginal = renameMappingFields(m_stepOriginal, renames);
        m_groupInput = renameMappingFields(QJsonObject{{"input", m_groupInput}}, renames).value("input").toObject();
        const auto originalParameters = m_mappingParameters->property("originalParameters").toJsonObject();
        m_mappingParameters->setProperty("originalParameters", renameMappingFields(QJsonObject{{"parameters", originalParameters}}, renames).value("parameters").toObject());
        for (int row = 0; row < m_mappingParameters->rowCount(); ++row) {
            auto *label = m_mappingParameters->item(row, 0);
            const auto expression = label->data(Qt::UserRole + 1).toJsonObject();
            label->setData(Qt::UserRole + 1, renameMappingFields(QJsonObject{{"parameters", QJsonObject{{"value", expression}}}}, renames)
                .value("parameters").toObject().value("value").toObject());
        }
        const QSignalBlocker fieldBlocker(m_fieldTable);
        for (int row = 0; row < m_fieldTable->rowCount(); ++row)
            m_fieldTable->item(row, 1)->setData(Qt::UserRole, itemText(m_fieldTable, row, 1));
        // 新增表单的来源也跟随字段改名，但不触碰或提交新表单内容。
        const bool previousLoading=m_loadingMapping; m_loadingMapping=true;
        auto renameSource=[&renames](QComboBox *box) {
            const QString old=box->currentData().toString();
            if (sourceScope(box) != "input" || !renames.contains(old)) return;
            const QString name=renames.value(old);
            box->setItemData(box->currentIndex(), name);
            box->setItemText(box->currentIndex(), name);
        };
        renameSource(m_mappingSource);
        for (int row=0;row<m_mappingParameters->rowCount();++row)
            if (auto *box=qobject_cast<QComboBox *>(m_mappingParameters->cellWidget(row,1))) renameSource(box);
        const auto previousCounts = m_listCounts;
        m_listCounts.clear();
        for (auto it = previousCounts.begin(); it != previousCounts.end(); ++it)
            m_listCounts.insert(renames.value(it.key(), it.key()), it.value());
        m_loadingMapping=previousLoading; m_syncingFields=false; refreshMappingSteps();
    } else {
        const QSignalBlocker blocker(m_fieldTable);
        for (int row = 0; row < m_fieldTable->rowCount(); ++row)
            m_fieldTable->item(row, 1)->setData(Qt::UserRole, itemText(m_fieldTable, row, 1));
    }
    return true;
}

QString Widget::mappingFieldIssue(const QJsonObject &mapping) const
{
    if (mapping.contains("steps")) {
        const auto steps = mapping.value("steps").toArray();
        for (int i = 0; i < steps.size(); ++i) {
            auto flat = mapping; flat.remove("steps");
            flat.insert("operation", steps[i].toObject().value("operation")); flat.insert("parameters", steps[i].toObject().value("parameters"));
            const QString issue = mappingFieldIssue(flat);
            if (!issue.isEmpty()) return QString::fromUtf8(u8"第%1条：%2").arg(i + 1).arg(issue);
        }
        return QString();
    }
    QSet<QString> numeric, omittable, seen; bool commandSeen = false;
    QSet<QString> lists;
    for (int row = 0; row < m_fieldTable->rowCount(); ++row) {
        const QString name = itemText(m_fieldTable, row, 1);
        if (name.isEmpty() || seen.contains(name)) return QString::fromUtf8(u8"字段名称为空或重复");
        seen.insert(name);
        const QString role = comboData(m_fieldTable, row, 2);
        if (role == "command") commandSeen = true; else if (commandSeen) omittable.insert(name);
        if (role == "parameter" && comboData(m_fieldTable, row, 3) != "ascii") numeric.insert(name);
        if (role == "parameter" && comboData(m_fieldTable, row, 3) == "integerList") lists.insert(name);
    }
    QSet<QString> wireLists = lists;
    for (const QJsonValue &omitted : mapping.value("input").toObject().value("omitFields").toArray()) {
        if (!omittable.contains(omitted.toString())) return QString::fromUtf8(u8"声明不使用的字段不可用：%1").arg(omitted.toString());
        numeric.remove(omitted.toString());
        lists.remove(omitted.toString());
        if (legacyMappedInput()) wireLists.remove(omitted.toString());
    }
    const QJsonObject input = mapping.value("input").toObject();
    if (input.contains("parameterCounts") && !input.value("parameterCounts").isObject())
        return QString::fromUtf8(u8"列表输入项数必须为对象");
    const auto counts = input.value("parameterCounts").toObject();
    for (auto it = counts.begin(); it != counts.end(); ++it)
        if (!wireLists.contains(it.key()) || !it.value().isDouble() || it.value().toDouble() != it.value().toInt()
                || it.value().toInt() < 1 || it.value().toInt() > 2048)
            return QString::fromUtf8(u8"列表输入项数不可用：%1").arg(it.key());
    for (const QString &name : lists)
        if (!counts.contains(name)) return QString::fromUtf8(u8"请设置列表“%1”的输入项数").arg(name);
    const QJsonObject parameters = mapping.value("parameters").toObject();
    for (const QJsonValue &value : parameters) {
        const QJsonObject expression = value.toObject(); const QString kind = expression.value("kind").toString();
        if (kind == "formula") {
            QSet<QString> used; QString error;
            if (!NumericFormula::inspect(expression.value("formula").toString(), &used, &error)) return error;
            const auto variables = expression.value("variables").toObject(); QJsonObject references;
            for (const auto &alias : used) {
                const auto binding = variables.value(alias).toObject();
                if (binding.value("kind").toString() != "field") return QString::fromUtf8(u8"公式变量“%1”缺少有效来源。").arg(alias);
                references.insert(alias, binding);
            }
            const QString issue = mappingFieldIssue(QJsonObject{{"parameters", references}, {"input", mapping.value("input")}});
            if (!issue.isEmpty()) return QString::fromUtf8(u8"公式来源失效：") + issue;
            continue;
        }
        if ((kind == "field" || kind == "linear") && expression.value("scope").toString() == "preset") {
            bool found = false;
            for (const auto &value : parameterPresetRecords()) {
                const auto preset = value.toObject();
                if (preset.value("id") != expression.value("field")) continue;
                found = true; const auto index = expression.value("itemIndex");
                if (!index.isDouble() || index.toDouble() != index.toInt() || index.toInt() < 1 || index.toInt() > preset.value("values").toArray().size())
                    return QString::fromUtf8(u8"预设“%1”的项号不可用：%2").arg(preset.value("name").toString()).arg(index.toInt());
            }
            if (!found) return QString::fromUtf8(u8"预设已删除或不可用：%1").arg(expression.value("field").toString());
            continue;
        }
        if ((kind == "field" || kind == "linear") && !numeric.contains(expression.value("field").toString()))
            return QString::fromUtf8(u8"来源字段不可用或已声明不使用：%1").arg(expression.value("field").toString());
        if (kind == "field" || kind == "linear") {
            const QString field = expression.value("field").toString();
            if (lists.contains(field)) {
                const auto index = expression.value("itemIndex");
                if (!index.isDouble() || index.toDouble() != index.toInt() || index.toInt() < 1 || index.toInt() > counts.value(field).toInt())
                    return QString::fromUtf8(u8"列表“%1”的项号应为1～%2").arg(field).arg(counts.value(field).toInt());
            } else if (expression.contains("itemIndex"))
                return QString::fromUtf8(u8"标量字段“%1”不能带列表项号").arg(field);
        }
    }
    return QString();
}

void Widget::refreshMappingSummaries()
{
    for (int row = 0; row < m_mappingTable->rowCount(); ++row)
        updateMappingSummary(row, m_mappingTable->item(row, 0)->data(Qt::UserRole).toJsonObject());
}


void Widget::updateMappingCounts()
{
    const QJsonArray omitted = mappingOmittedFields();
    m_mappingCounts->setRowCount(0);
    for (int field = 0; field < m_fieldTable->rowCount(); ++field) {
        const QString name = itemText(m_fieldTable, field, 1);
        if (m_encoding->currentData().toString() != "ascii" || name.isEmpty() || omitted.contains(name)
                || comboData(m_fieldTable, field, 2) != "parameter" || comboData(m_fieldTable, field, 3) != "integerList") continue;
        if (!m_listCounts.contains(name)) m_listCounts.insert(name, 1);
        const int row = m_mappingCounts->rowCount(); m_mappingCounts->insertRow(row);
        auto *label = new QTableWidgetItem(name); label->setFlags(label->flags() & ~Qt::ItemIsEditable);
        m_mappingCounts->setItem(row, 0, label);
        auto *count = new QSpinBox(m_mappingCounts); count->setRange(1, 2048); count->setValue(m_listCounts.value(name));
        count->setObjectName("parameterCount_" + name);
        m_mappingCounts->setCellWidget(row, 1, count);
        connect(count, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, name](int value) {
            m_listCounts.insert(name, value);
            updateMappingSources(false);
        });
    }
    m_mappingCounts->setFixedHeight(qMin(126, (m_mappingCounts->rowCount() + 1) * 30 + 6));
    m_mappingCountsGroup->setVisible(m_mappingCounts->rowCount() > 0);
    // Leave room for the list count and all four XYZ parameters in the normal window.
}

void Widget::addParameterPresetRow(const QJsonObject &preset)
{
    const QSignalBlocker blocker(m_parameterPresets);
    const int row = m_parameterPresets->rowCount(); m_parameterPresets->insertRow(row);
    auto record = preset;
    if (record.isEmpty()) record.insert("id", QUuid::createUuid().toString(QUuid::WithoutBraces));
    auto *name = new QTableWidgetItem(record.value("name").toString()); name->setData(Qt::UserRole, record);
    m_parameterPresets->setItem(row, 0, name);
    QStringList values;
    for (const auto &value : record.value("values").toArray()) values.append(value.toVariant().toString());
    m_parameterPresets->setItem(row, 1, new QTableWidgetItem(values.join(",")));
}

QJsonArray Widget::parameterPresetRecords() const
{
    QJsonArray result;
    for (int row = 0; row < m_parameterPresets->rowCount(); ++row) {
        const auto *name = m_parameterPresets->item(row, 0);
        auto record = name ? name->data(Qt::UserRole).toJsonObject() : QJsonObject();
        record.insert("name", itemText(m_parameterPresets, row, 0));
        QJsonArray values;
        if (!presetValues(itemText(m_parameterPresets, row, 1), &values)) values = QJsonArray();
        record.insert("values", values); result.append(record);
    }
    return result;
}

bool Widget::collectParameterPresets(QJsonArray *presets, QString *reason) const
{
    const auto stored = m_draftRoot.value("parameterPresets");
    if (!stored.isUndefined() && !stored.isArray()) {
        *reason = QString::fromUtf8(u8"原位置与校准参数不是数组，无法在表格中完整显示；请导入修正后的配置，原内容未覆盖。"); return false;
    }
    for (const auto &record : stored.toArray()) if (!record.isObject()) {
        *reason = QString::fromUtf8(u8"原位置与校准参数含非对象记录，无法在表格中完整显示；请导入修正后的配置，原内容未覆盖。"); return false;
    }
    if (m_parameterPresets->rowCount() > 64) { *reason = QString::fromUtf8(u8"位置与校准参数最多64组。"); return false; }
    QSet<QString> ids, names; *presets = parameterPresetRecords();
    for (int row = 0; row < presets->size(); ++row) {
        const auto record = presets->at(row).toObject(); const QString id = record.value("id").toString(), name = record.value("name").toString();
        if (id.trimmed().isEmpty() || ids.contains(id) || name.isEmpty() || names.contains(name)) {
            *reason = QString::fromUtf8(u8"第%1组参数名称或标识为空、重复，请填写唯一名称。").arg(row + 1); return false;
        }
        ids.insert(id); names.insert(name);
        if (record.value("values").toArray().isEmpty()) {
            *reason = QString::fromUtf8(u8"参数组“%1”需填写1～64个逗号分隔整数，不能留空项；范围为±9007199254740991。").arg(name); return false;
        }
    }
    return true;
}

QString Widget::sourceName(const QJsonObject &expression) const
{
    if (expression.value("kind").toString() == "formula") return QStringLiteral("fx(%1)").arg(expression.value("formula").toString());
    QString field = expression.value("field").toString();
    if (expression.value("scope").toString() == "preset") {
        bool found = false;
        for (const auto &value : parameterPresetRecords()) {
            const auto record = value.toObject();
            if (record.value("id").toString() == field) { field = record.value("name").toString(); found = true; break; }
        }
        field = (found ? QString::fromUtf8(u8"预设 · ") : QString::fromUtf8(u8"失效预设 · ")) + field;
    }
    return expression.contains("itemIndex") ? QString::fromUtf8(u8"%1 · 第 %2 项").arg(field).arg(expression.value("itemIndex").toVariant().toString()) : field;
}

void Widget::parameterPresetsChanged()
{
    if (m_loadingUi) return;
    const bool loading = m_loadingMapping; m_loadingMapping = true;
    updateMappingSources(false); refreshMappingSteps(); markDraftDirty(); m_loadingMapping = loading;
    QJsonArray ignored; QString reason;
    if (!collectParameterPresets(&ignored, &reason)) m_validationState->setText(reason);
}

void Widget::addNumericSources(QComboBox *box, const QJsonArray &omitted, bool includeInput)
{
    int numericFields = 0;
    for (int row = 0; row < m_fieldTable->rowCount(); ++row)
        if (comboData(m_fieldTable, row, 2) == "parameter" && comboData(m_fieldTable, row, 3) != "ascii"
                && !itemText(m_fieldTable, row, 1).isEmpty() && !omitted.contains(itemText(m_fieldTable, row, 1))) ++numericFields;
    box->setMaxVisibleItems(12);
    for (int row = 0; includeInput && row < m_fieldTable->rowCount(); ++row) {
        const QString name = itemText(m_fieldTable, row, 1);
        const QString type = comboData(m_fieldTable, row, 3);
        if (comboData(m_fieldTable, row, 2) != "parameter" || type == "ascii" || name.isEmpty() || omitted.contains(name)) continue;
        const bool list = type == "integerList";
        const int count = list ? m_listCounts.value(name, 1) : 1;
        for (int item = 1; item <= count; ++item) {
            const QString label = list ? (numericFields > 1 ? name + QString::fromUtf8(u8" · ") : QString())
                + QString::fromUtf8(u8"第%1个参数").arg(item) : name;
            box->addItem(label, name);
            box->setItemData(box->count() - 1, list, Qt::UserRole + 1);
            if (list) box->setItemData(box->count() - 1, item, Qt::UserRole + 2);
            box->setItemData(box->count() - 1, numericFields > 1, Qt::UserRole + 3);
        }
    }
    for (const auto &value : parameterPresetRecords()) {
        const auto preset = value.toObject();
        if (preset.value("name").toString().isEmpty()) continue;
        for (int item = 1; item <= preset.value("values").toArray().size(); ++item) {
            box->addItem(QString::fromUtf8(u8"预设 · %1 · 第%2项").arg(preset.value("name").toString()).arg(item), preset.value("id").toString());
            const int index = box->count() - 1;
            box->setItemData(index, true, Qt::UserRole + 1); box->setItemData(index, item, Qt::UserRole + 2);
            box->setItemData(index, "preset", Qt::UserRole + 4);
        }
    }
}

void Widget::updateMappingSources(bool rebuildCounts)
{
    const QSignalBlocker blocker(m_mappingSource);
    const bool motion = isMotionEditor(m_mappingOperation->currentData().toString());
    const bool grouped = !m_stepDrafts.isEmpty();
    const bool groupWithoutParameters = grouped && m_groupInputMode->currentData().toString() == "none";
    QString selected = m_mappingSource->currentData().toString();
    const bool selectedList = isListSource(m_mappingSource);
    const int selectedIndex = sourceItemIndex(m_mappingSource);
    const QString selectedScope = sourceScope(m_mappingSource);
    if (rebuildCounts) updateMappingCounts();
    if (!motion && selected != "@none") selected = "@fields";
    else if (motion && selectedScope != "preset" && selected == "@fields") selected.clear();
    if (motion && selectedScope != "preset" && grouped && (selected == "@none" || (groupWithoutParameters && selected.isEmpty())))
        selected = "@constant";
    m_mappingSource->clear();
    m_mappingSource->addItem(motion ? QString::fromUtf8(u8"请选择来源") : QString::fromUtf8(u8"客户参数"), motion ? "" : "@fields");
    if (motion) {
        addNumericSources(m_mappingSource, {}, !groupWithoutParameters);
        m_mappingSource->addItem(QString::fromUtf8(u8"固定值"), "@constant");
    }
    if (!motion || !grouped) {
        const bool relative = mappingParameterKey(m_mappingOperation->currentData().toString()) == "distanceUm";
        const QString unused = legacyMappedInput() ? QString::fromUtf8(u8"无参数短报文") : QString::fromUtf8(u8"不使用客户参数");
        m_mappingSource->addItem(motion ? unused + (relative ? QString::fromUtf8(u8"（使用固定距离）") : QString::fromUtf8(u8"（使用固定位置）"))
                                       : unused + (legacyMappedInput() ? QString::fromUtf8(u8"（省略参数字段）") : QString::fromUtf8(u8"（字段仍解析）")), "@none");
    }
    restoreSource(m_mappingSource, selected, motion && selectedList, motion ? selectedIndex : 0, motion ? selectedScope : QStringLiteral("input"));
    const QJsonArray omitted = mappingOmittedFields();
    for (int row = 0; row < m_mappingParameters->rowCount(); ++row) {
        auto *source = qobject_cast<QComboBox *>(m_mappingParameters->cellWidget(row, 1)); if (!source) continue;
        const QSignalBlocker sourceBlocker(source);
        const QString chosen = source->currentData().toString(); const bool list = isListSource(source);
        const int itemIndex = sourceItemIndex(source);
        const QString scope = sourceScope(source);
        source->clear(); source->addItem(parameterDefaultText(m_mappingParameters->item(row, 0)->data(Qt::UserRole).toJsonObject(),
            m_mappingOperation->currentData().toString()), "");
        addNumericSources(source, omitted);
        source->addItem(QString::fromUtf8(u8"固定值"), "@constant");
        addFormulaSource(source, m_mappingOperation->currentData().toString(), m_mappingParameters->item(row, 0)->data(Qt::UserRole).toJsonObject().value("key").toString());
        restoreSource(source, chosen, list, itemIndex, scope);
        source->setProperty("previousIndex", source->currentIndex());
        updateFormulaCell(row);
    }
    updateMappingControls();
}
bool Widget::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_mappingSplitter && (event->type() == QEvent::Show || event->type() == QEvent::Resize))
        scheduleMappingLayout();
    return QWidget::eventFilter(watched, event);
}

void Widget::scheduleMappingLayout()
{
    if (!m_mappingSplitter || m_mappingSplitterAdjusted || m_mappingLayoutPending) return;
    m_mappingLayoutPending = true;
    // Wait for visibility and the form's layout requests to settle, including
    // switching from a short motor form to the valve or multi-step editor.
    QTimer::singleShot(0, this, [this]() {
        m_mappingLayoutPending = false;
        if (m_mappingSplitterAdjusted || !m_mappingSplitter->isVisible()) return;
        m_mappingEditor->layout()->activate();
        const int available = m_mappingSplitter->height() - m_mappingSplitter->handleWidth();
        if (available <= 0) return;
        const int listMinimum = qMax(120, available / 4);
        const int needed = m_mappingEditor->sizeHint().height() + 4;
        // A multi-step editor uses the spare height for its step list. A plain
        // single-command form needs only its content height, leaving the rest
        // to the saved mappings above.
        const int desired = m_stepDrafts.size() > 1 ? available - listMinimum : needed;
        const int editorHeight = qMax(100, qMin(desired, available - listMinimum));
        m_mappingSplitter->setSizes({available - editorHeight, editorHeight});
    });
}

void Widget::updateMappingControls()
{
    scheduleMappingLayout();
    updateMappingStepState();
    updateValveEditor();
    const bool legacyInput = legacyMappedInput();
    m_groupInputMode->setItemText(m_groupInputMode->findData("parameters"), legacyInput
        ? QString::fromUtf8(u8"读取客户参数") : QString::fromUtf8(u8"使用客户参数取值"));
    m_groupInputMode->setItemText(m_groupInputMode->findData("none"), legacyInput
        ? QString::fromUtf8(u8"无参数短报文（省略参数字段）") : QString::fromUtf8(u8"不使用客户参数（字段仍按格式解析）"));
    m_groupInputMode->setToolTip(legacyInput ? QString::fromUtf8(u8"按命令码识别兼容旧短报文，省略已指定的参数字段。")
        : QString::fromUtf8(u8"此选项只控制映射是否使用客户参数；不会删除报文中的字段或分隔符。"));
    m_mappingSource->setToolTip(legacyInput ? QString::fromUtf8(u8"旧短报文可省略指定客户字段；运动仍需要明确的固定位置或距离。")
        : QString::fromUtf8(u8"仅选择本映射的取值来源，不改变格式表的字段布局；不使用的字段仍会解析。"));
    const QString operation = m_mappingOperation->currentData().toString();
    const bool motion = isMotionEditor(operation);
    const bool grouped = !m_stepDrafts.isEmpty();
    m_mappingEditor->layout()->setAlignment(m_stepDrafts.size() > 1 ? Qt::Alignment() : Qt::Alignment(Qt::AlignTop));
    m_groupInputMode->setVisible(grouped);
    m_mappingEditor->layout()->setSpacing(grouped ? 3 : 6);
    for (int i = 0; i < m_mappingEditor->layout()->count(); ++i)
        if (auto *form = qobject_cast<QFormLayout *>(m_mappingEditor->layout()->itemAt(i)->layout())) {
            form->setVerticalSpacing(grouped ? 2 : 6);
            if (auto *label = form->labelForField(m_groupInputMode)) label->setVisible(grouped);
            if (auto *search = m_mappingEditor->findChild<QHBoxLayout *>("mappingSearchRow"))
                if (auto *label = form->labelForField(search)) label->setVisible(!grouped);
        }
    m_mappingSearch->setVisible(!grouped);
    m_mappingParameters->verticalHeader()->setDefaultSectionSize(grouped ? 25 : 30);
    m_mappingCounts->setFixedHeight(grouped ? qMin(114, 28 + 26 * m_mappingCounts->rowCount())
                                          : qMin(126, (m_mappingCounts->rowCount() + 1) * 30 + 6));
    m_mappingParameters->setVisible(!motion && m_mappingParameters->rowCount() > 0);
    updateCatalogParameterVisibility();
    const bool motor = motion || QushengCommandCatalog::isStopOrReset(operation);
    m_mappingMotor->setVisible(motor);
    for (int i = 0; i < m_mappingEditor->layout()->count(); ++i)
        if (auto *form = qobject_cast<QFormLayout *>(m_mappingEditor->layout()->itemAt(i)->layout()))
            if (QWidget *label = form->labelForField(m_mappingMotor)) label->setVisible(motor);
    const bool constant = !isPresetSource(m_mappingSource) && m_mappingSource->currentData().toString() == "@constant";
    const bool noParameters = !isPresetSource(m_mappingSource) && (grouped ? m_groupInputMode->currentData().toString() == "none"
                                      : m_mappingSource->currentData().toString() == "@none");
    const QString motionValueName = mappingParameterKey(operation) == "distanceUm" ? QString::fromUtf8(u8"运动距离") : QString::fromUtf8(u8"目标位置");
    m_mappingValue->setPlaceholderText(QString::fromUtf8(u8"填写%1整数").arg(motionValueName));
    auto visibility = [](QWidget *control, bool show) {
        control->setVisible(show);
        if (auto *form = qobject_cast<QFormLayout *>(control->parentWidget()->layout()))
            if (QWidget *label = form->labelForField(control)) label->setVisible(show);
    };
    // QFormLayout 放在 group 的纵向布局里；标签按其实际所属表单定位，避免隐藏控件后留下空英文/技术标签。
    for (auto *control : {static_cast<QWidget *>(m_mappingSource), static_cast<QWidget *>(m_mappingValue),
                         static_cast<QWidget *>(m_mappingMultiplier)}) {
        bool show = control == m_mappingSource ? (motion || !grouped) : motion;
        if (control == m_mappingValue) show &= constant || noParameters;
        if (control == m_mappingMultiplier) show &= !constant && !noParameters;
        visibility(control, show);
        auto *layout = m_mappingEditor->layout();
        for (int i = 0; i < layout->count(); ++i) if (auto *form = qobject_cast<QFormLayout *>(layout->itemAt(i)->layout()))
            if (QWidget *label = form->labelForField(control)) {
                label->setVisible(show);
                if (control == m_mappingSource) if (auto *text = qobject_cast<QLabel *>(label))
                    text->setText(QString::fromUtf8(u8"取值来源"));
                if (control == m_mappingValue) if (auto *text = qobject_cast<QLabel *>(label))
                    text->setText(motionValueName + QString::fromUtf8(u8"（必填）"));
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
        if (!isPresetSource(m_mappingSource) && (source=="@constant" || source=="@none")) {
            bool ok=false; const qlonglong value=m_mappingValue->text().trimmed().toLongLong(&ok);
            if (!ok || value<0 || value>0xfffffe) {
                const QString valueName = mappingParameterKey(operation) == "distanceUm" ? QString::fromUtf8(u8"运动距离") : QString::fromUtf8(u8"目标位置");
                return fail(QString::fromUtf8(u8"当前指令“%1”需要明确的%2，请填写零到 16777214 的整数；不使用客户参数也不能省略%2。")
                    .arg(operationName(operation), valueName));
            }
            expression = m_stepOriginal.value("parameters").toObject().value(mappingParameterKey(operation)).toObject();
            expression.insert("kind", "constant"); expression.insert("value", double(value));
            for (const QString &key : {"field", "scope", "itemIndex", "numerator", "denominator", "offset", "round"}) expression.remove(key);
        } else {
            if (source.isEmpty()) return fail(QString::fromUtf8(u8"请选择位置来源字段。"));
            const auto original = m_stepOriginal.value("parameters").toObject().value(mappingParameterKey(operation)).toObject();
            qlonglong numerator=1,denominator=1;
            const bool preserveLinear = original.value("kind").toString() == "linear" && m_mappingMultiplier->text() == multiplierText(original);
            if (!preserveLinear && !decimalRatio(m_mappingMultiplier->text(),&numerator,&denominator))
                return fail(QString::fromUtf8(u8"换算倍数请填写有效整数或最多九位小数。"));
            expression = original;
            expression.insert("kind", original.value("kind").toString() == "linear" ? "linear" : "field");
            expression.insert("field", source);
            applySourceScope(&expression, m_mappingSource);
            expression.remove("value");
            if (!preserveLinear && (numerator!=denominator || original.value("kind").toString() == "linear")) {
                expression.insert("kind","linear"); expression.insert("numerator",double(numerator));
                expression.insert("denominator",double(denominator));
                if (!expression.contains("offset")) expression.insert("offset",0);
                if (!expression.contains("round")) expression.insert("round","exact");
            }
        }
        if (isListSource(m_mappingSource)) expression.insert("itemIndex", sourceItemIndex(m_mappingSource));
        else expression.remove("itemIndex");
        parameters.insert(mappingParameterKey(operation),expression);
    } else {
        QString reason;
        if (!catalogParameters(&parameters,&reason)) return fail(reason);
    }
    if ((isMotionEditor(operation) || QushengCommandCatalog::isStopOrReset(operation))
            && m_mappingMotor->currentData().toInt() != 0)
        parameters.insert("devId", QJsonObject{{"kind", "constant"}, {"value", m_mappingMotor->currentData().toInt()}});
    QJsonObject mapping = m_stepOriginal;
    mapping.insert("commandCode", code); mapping.insert("operation", operation); mapping.insert("parameters", parameters);
    const QJsonArray omitted = mappingOmittedFields();
    for (const QJsonValue &value : parameters) {
        const QJsonObject expression = value.toObject();
        if (expression.value("scope").toString() != "preset" && omitted.contains(expression.value("field")))
            return fail(legacyMappedInput() ? QString::fromUtf8(u8"无参数短报文不能引用被省略的字段，请选择默认值或固定值。")
                : QString::fromUtf8(u8"本映射已声明不使用该客户字段；如需引用，请选择读取客户参数。格式表中的字段仍会解析。"));
    }
    // 停止/复位显式选择客户参数时也保存空列表，避免重启时被旧配置兼容逻辑改成无参数。
    if (!omitted.isEmpty() || QushengCommandCatalog::isStopOrReset(operation))
        mapping.insert("input",QJsonObject{{"omitFields",omitted}});
    QJsonObject input = mapping.value("input").toObject(), counts;
    for (int row = 0; row < m_mappingCounts->rowCount(); ++row) {
        const QString field = m_mappingCounts->item(row, 0)->text();
        counts.insert(field, qobject_cast<QSpinBox *>(m_mappingCounts->cellWidget(row, 1))->value());
    }
    if (!counts.isEmpty()) { input.insert("parameterCounts", counts); mapping.insert("input", input); }
    const QString issue = mappingFieldIssue(mapping);
    if (!issue.isEmpty()) return fail(issue);
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
    {
        const QSignalBlocker presetBlocker(m_parameterPresets); m_parameterPresets->setRowCount(0);
        for (const auto &value : m_draftRoot.value("parameterPresets").toArray()) addParameterPresetRow(value.toObject());
    }
    m_mappingTable->setRowCount(0);
    for (const QJsonValue &mapping : m_draftRoot.value("mappings").toArray()) addMappingRow(mapping.toObject());
    const QJsonObject device = m_draftRoot.value("device").toObject();
    m_destination->setValue(device.value("destination").toInt(2));
    m_source->setValue(device.value("source").toInt(1));
    const QJsonValue storedId = device.value("devId");
    m_unresolvedMotorId = device.contains("devId") && (!storedId.isDouble()
        || storedId.toDouble() != storedId.toInt() || storedId.toInt() < 0x11 || storedId.toInt() > 0x16);
    m_devId->setMinimum(m_unresolvedMotorId ? 0x10 : 0x11);
    const QString originalId = storedId.isDouble() && storedId.toDouble() == storedId.toInt() && storedId.toInt() >= 0
        ? "0x" + QString::number(storedId.toInt(), 16).toUpper() : storedId.toVariant().toString();
    m_devId->setSpecialValueText(m_unresolvedMotorId
        ? QString::fromUtf8(u8"需重选（原值%1）").arg(originalId) : QString());
    m_devId->setValue(m_unresolvedMotorId ? 0x10 : storedId.toInt(0x11));
    m_action->setValue(device.value("absoluteAction").toInt(1));
    m_speed->setValue(device.value("speedUmPerS").toInt(1000));
    m_minPosition->setValue(device.value("minPositionUm").toInt(0));
    m_maxPosition->setValue(device.value("maxPositionUm").toInt(60000));
    m_advancedJson->setPlainText(QString::fromUtf8(QJsonDocument(m_draftRoot).toJson(QJsonDocument::Indented)));
    m_draftVersion->setText(QString::fromUtf8(u8"配置：已载入"));
    m_validationState->clear();
    m_inputEncoding->setText(QString::fromUtf8(u8"输入编码：")
        + (m_encoding->currentData().toString() == "hex" ? QStringLiteral("HEX") : QStringLiteral("ASCII")));
    m_formDirty = false;
    m_jsonDirty = false;
    m_loadingUi = false;
    resetMappingForm();
    updateTableControls();
    refreshMappingSummaries();
    updateLiveState();
}

bool Widget::saveConfiguration()
{
    QWidget *editor = QApplication::focusWidget();
    if (editor && isAncestorOf(editor)) editor->clearFocus();
    if (m_formDirty && m_jsonDirty) {
        m_validationState->setText(QString::fromUtf8(u8"保存失败：表单和 JSON 都有修改，请在高级配置中选择要保留的内容"));
        appendLog(m_validationState->text());
        return false;
    }
    return m_jsonDirty ? applyAdvancedJson() : saveUiToDraft();
}

bool Widget::saveUiToDraft()
{
    if (m_unresolvedMotorId) {
        m_validationState->setText(QString::fromUtf8(u8"旧电机编号无效，原配置已保留；请在设备页重新选择0x11～0x16，再保存或发送。"));
        return false;
    }
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
    // 所有数字先严格读取，任何失败都保留旧配置；不能把 abc 或小数隐式保存成 0。
    auto readInteger = [this](const QString &text, qlonglong *value, const QString &label) {
        bool ok = false;
        *value = text.toLongLong(&ok);
        if (!ok) {
            m_validationState->setText(QString::fromUtf8(u8"保存失败：%1必须填写整数").arg(label));
            appendLog(m_validationState->text());
        }
        return ok;
    };
    QJsonArray presets; QString presetReason;
    if (!collectParameterPresets(&presets, &presetReason)) {
        m_validationState->setText(QString::fromUtf8(u8"保存失败：") + presetReason); return false;
    }
    QJsonObject root = m_draftRoot;
    if (!presets.isEmpty() || root.contains("parameterPresets")) root.insert("parameterPresets", presets);
    if (!root.contains("schemaVersion")) root.insert("schemaVersion", 1);
    if (!root.contains("name")) root.insert("name", QString::fromUtf8(u8"用户协议配置"));
    QJsonObject external = root.value("external").toObject();
    if (ProtocolConfig::retiredFormat(root)) {
        // Explicit saving is the only recovery action. Preserve the retired data as an opaque backup.
        QJsonObject backup = root.value("retiredFormatBackup").toObject();
        backup.insert("mode", external.value("mode"));
        backup.insert("textFormat", external.value("textFormat"));
        root.insert("retiredFormatBackup", backup);
    }
    external.remove("mode"); external.remove("textFormat");
    external.insert("encoding", m_encoding->currentData().toString());
    external.insert("header", m_header->text());
    external.insert("footer", m_footer->text());
    external.insert("byteOrder", m_byteOrder->currentData().toString());
    QJsonArray fields;
    for (int row = 0; row < m_fieldTable->rowCount(); ++row) {
        QJsonObject field = m_fieldTable->item(row, 1)->data(Qt::UserRole + 1).toJsonObject();
        for (const QString &key : {QStringLiteral("length"), QStringLiteral("delimiter"), QStringLiteral("fixedValue"), QStringLiteral("min"), QStringLiteral("max")}) field.remove(key);
        field.insert("name", itemText(m_fieldTable, row, 1));
        field.insert("role", comboData(m_fieldTable, row, 2));
        field.insert("type", comboData(m_fieldTable, row, 3));
        const QString lengthMode = comboData(m_fieldTable, row, 4);
        field.insert("lengthMode", lengthMode);
        if (lengthMode == "fixed") {
            qlonglong length;
            if (!readInteger(itemText(m_fieldTable, row, 5), &length, QString::fromUtf8(u8"字段长度"))) return false;
            field.insert("length", double(length));
        } else if (lengthMode == "delimited") field.insert("delimiter", itemText(m_fieldTable, row, 5));
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
    QJsonObject device = root.value("device").toObject();
    const QJsonObject deviceValues{
        {"destination", m_destination->value()}, {"source", m_source->value()},
        {"devId", m_unresolvedMotorId ? device.value("devId") : QJsonValue(m_devId->value())}, {"absoluteAction", m_action->value()},
        {"speedUmPerS", m_speed->value()}, {"minPositionUm", m_minPosition->value()},
        {"maxPositionUm", m_maxPosition->value()}};
    for (auto it = deviceValues.begin(); it != deviceValues.end(); ++it) device.insert(it.key(), it.value());
    root.insert("device", device);
    *draft = root;
    return true;
}

bool Widget::storeUiDraft(const QJsonObject &root)
{
    if (!persistDraft(root)) return false;
    m_draftRoot = root;
    const QSignalBlocker jsonBlocker(m_advancedJson);
    m_advancedJson->setPlainText(QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented)));
    m_draftVersion->setText(QString::fromUtf8(u8"配置：已保存"));
    m_validationState->clear();
    m_inputEncoding->setText(QString::fromUtf8(u8"输入编码：")
        + (m_encoding->currentData().toString() == "hex" ? QStringLiteral("HEX") : QStringLiteral("ASCII")));
    m_formDirty = false;
    appendLog(QString::fromUtf8(u8"配置已保存到本地。"));
    refreshMappingSummaries(); updateMappingControls(); activateSavedConfiguration();
    return true;
}

void Widget::markDraftDirty()
{
    if (m_loadingUi || m_draftRoot.isEmpty()) return;
    m_formDirty = true;
    updateTableControls();
    refreshMappingSummaries();
    m_draftVersion->setText(QString::fromUtf8(u8"配置：未保存"));
    m_validationState->clear();
    updateLiveState();
}

bool Widget::draftSaved()
{
    // “表单”“配置”“启用快照”是三个不同状态。未保存表单绝不能被旧配置的校验成功掩盖。
    if (!m_formDirty && !m_jsonDirty) return true;
    const QString reason = QString::fromUtf8(u8"请先点击“保存配置”，再预览或导出。");
    m_validationState->setText(reason);
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
        const bool length = QStringList({"fixed", "delimited"}).contains(comboData(m_fieldTable, row, 4));
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
    m_formatRangesToggle->setVisible(anyNumber);
    m_fieldTable->setColumnHidden(7, !anyNumber || !m_formatRangesToggle->isChecked());
    m_fieldTable->setColumnHidden(8, !anyNumber || !m_formatRangesToggle->isChecked());
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

void Widget::activateSavedConfiguration()
{
    refreshUpstreamConfig();
    ProtocolConfig config;
    TranslationError error;
    if (!ProtocolConfig::fromJson(QJsonDocument(m_draftRoot).toJson(), &config, &error)) {
        m_activeConfig = ProtocolConfig();
        m_activeVersion->setText(QString::fromUtf8(u8"启用：无"));
        m_validationState->setText(QString::fromUtf8(u8"配置有误：%1").arg(error.message));
        appendLog(QStringLiteral("%1: %2").arg(error.code, error.message));
        updateLiveState();
        return;
    }
    m_activeConfig = config;
    if (config.root.value("mappings").toArray().isEmpty()) {
        m_validationState->setText(QString::fromUtf8(u8"顺序格式已保存，尚无发送映射；可使用“解析格式”。"));
        updateLiveState(); return;
    }
    const QJsonObject device = config.root.value("device").toObject();
    // 只更新默认地址；已有请求保存自己的地址和序号，保存不能重置会话或打断回告。
    m_protocol->setAddresses(quint8(device.value("destination").toInt()),
                             quint8(device.value("source").toInt()));
    m_activeVersion->setText(QString::fromUtf8(u8"启用：%1").arg(config.version.left(12)));
    int pending = 0;
    for (const QJsonValue &value : config.root.value("mappings").toArray())
        if (!QushengCommandCatalog::isExecutable(value.toObject().value("operation").toString())) ++pending;
    m_validationState->setText(pending ? QString::fromUtf8(u8"%1 条指令待实现").arg(pending) : QString());
    appendLog(QString::fromUtf8(u8"已自动启用配置 %1").arg(config.version.left(12)));
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
    m_draftVersion->setText(QString::fromUtf8(u8"配置：已保存"));
    activateSavedConfiguration();
    appendLog(QString::fromUtf8(u8"高级 JSON 已应用并保存。"));
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
        : QString::fromUtf8(u8"是否保存修改到配置后关闭？"), &prompt));
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
        const bool saved = saveConfiguration();
        if (saved) {
            event->accept();
            return;
        }
    }
    // 取消、关闭提示框或保存失败都保留编辑窗口与原落盘配置。
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
    if (ProtocolConfig::retiredFormat(document.object())) {
        m_validationState->setText(QString::fromUtf8(u8"导入失败：UNSUPPORTED_FORMAT。模板与样例格式已停用；当前草稿和原文件未改变，请导入顺序字段配置。"));
        appendLog(m_validationState->text()); return;
    }
    ProtocolConfig candidate; candidate.root = document.object(); TranslationError motorError;
    if (!candidate.validateMotorIds(&motorError)) {
        m_validationState->setText(QString::fromUtf8(u8"导入失败：%1；当前草稿与原文件未改变。").arg(motorError.message));
        appendLog(motorError.code + ": " + motorError.message); return;
    }
    if (!persistDraft(document.object())) return;
    m_draftRoot = document.object();
    loadUiFromDraft();
    m_draftVersion->setText(QString::fromUtf8(u8"配置：已保存"));
    activateSavedConfiguration();
    appendLog(QString::fromUtf8(u8"配置已导入并保存到本地。"));
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
    const auto ports = QSerialPortInfo::availablePorts();
    const QSignalBlocker blocker(m_port);
    const QString selected = m_port->currentData().toString();
    const QString label = m_port->currentText();
    m_port->clear();
    for (const QSerialPortInfo &port : ports) {
        const QString name = normalizedPortName(port.portName());
        if (name == normalizedPortName(m_providedPublicPort) || name == normalizedPortName(m_providedInternalPort)) continue;
        m_port->addItem(QStringLiteral("%1 - %2").arg(port.portName(), port.description()), port.portName());
    }
    const bool reserved = normalizedPortName(selected) == normalizedPortName(m_providedPublicPort)
        || normalizedPortName(selected) == normalizedPortName(m_providedInternalPort);
    if (!selected.isEmpty() && m_port->findData(selected) < 0 && (!reserved || m_transport->isConnected())) m_port->addItem(label, selected);
    const int index = m_port->findData(selected);
    if (index >= 0) m_port->setCurrentIndex(index);
}

void Widget::updateUpstreamCounters()
{
    m_upstreamCounters->setText(QString::fromUtf8(u8"总字节 %1 · 完整有效帧 %2 · 错误 %3")
        .arg(m_upstreamBytes).arg(m_upstreamFramesCount).arg(m_upstreamErrors));
}

void Widget::refreshUpstreamConfig()
{
    ProtocolConfig updated; updated.root = m_draftRoot;
    const bool changed = m_upstreamConfig.root.isEmpty() || receiveFormatKey(m_upstreamConfig) != receiveFormatKey(updated);
    m_upstreamConfig = updated;
    if (!changed) return;
    const bool valid = m_upstream->setConfig(m_upstreamConfig);
    m_upstreamFrames->appendPlainText(valid ? QString::fromUtf8(u8"已更新为保存的客户格式，接收缓冲已清空")
                                          : QString::fromUtf8(u8"保存的客户格式不可用，请检查配置"));
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
    updateLiveState();
}

void Widget::updateLiveState()
{
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
    if (!requireSequentialFormat()) return false;
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
    const auto targetDescription = [](const UnifiedCommand &command) {
        if (command.operation == "QUSHENG_21") {
            const quint32 mask = command.parameters.value("valveMask").toUInt();
            const bool replaceAll = command.parameters.value("mode").toUInt() == 2;
            QStringList valves;
            for (int index = 0; index < 30; ++index)
                if (mask & (quint32(1) << index)) valves.append(QString::number(index + 1));
            if (valves.isEmpty()) return replaceAll ? QString::fromUtf8(u8"全部30路阀关闭") : QString::fromUtf8(u8"未选阀（无操作）");
            return QString::fromUtf8(u8"%1号阀%2").arg(valves.join(QString::fromUtf8(u8"、")))
                .arg(replaceAll ? QString::fromUtf8(u8"（未选阀关闭）") : command.parameters.value("mode").toUInt() == 1
                     ? QString::fromUtf8(u8"（开启，其余不变）") : QString::fromUtf8(u8"（关闭，其余不变）"));
        }
        if (command.operation == "QUSHENG_22") {
            const quint32 number = command.parameters.value("devId").toUInt();
            return QString::fromUtf8(u8"%1号阀（0x%2）").arg(number - 0x30).arg(QString::number(number, 16).toUpper());
        }
        return command.targetDevice;
    };
    QStringList lines;
    lines << QString::fromUtf8(u8"原始输入：%1").arg(visibleBytes(preview.input));
    lines << QString::fromUtf8(u8"识别字段：");
    for (const QString &name : preview.fieldOrder)
        lines += fieldPreview(name, preview.fields.value(name));
    lines << QString::fromUtf8(u8"命中的客户命令码：%1").arg(preview.matchedCommandCode);
    lines += preview.notices;
    if (preview.steps.size() > 1) {
        lines << QString::fromUtf8(u8"共%1条趋盛指令（成功应答后顺序发送；不代表机械动作完成）").arg(preview.steps.size());
        for (int i = 0; i < preview.steps.size(); ++i) {
            const auto &step = preview.steps[i];
            lines << QString::fromUtf8(u8"第%1条：%2，目标%3").arg(i + 1).arg(operationName(step.command.operation), targetDescription(step.command));
            for (auto it = step.parameterTrace.begin(); it != step.parameterTrace.end(); ++it)
                lines << QStringLiteral("  %1: %2").arg(displayName(it.key()), it.value());
        }
        return lines.join('\n');
    }
    lines << QString::fromUtf8(u8"参数换算：");
    for (auto it = preview.parameterTrace.begin(); it != preview.parameterTrace.end(); ++it)
        lines << QStringLiteral("  %1: %2").arg(displayName(it.key()), it.value());
    const bool valve = preview.command.operation == "QUSHENG_21" || preview.command.operation == "QUSHENG_22";
    const bool hasDevice = isMotionEditor(preview.command.operation) || preview.command.parameters.contains("devId")
        || preview.command.operation == "QUSHENG_C1";
    const QString device = valve ? targetDescription(preview.command) : hasDevice ? preview.command.targetDevice : QStringLiteral("-");
    lines << (valve ? QString::fromUtf8(u8"统一命令：操作=%1，目标=%2")
                   : QString::fromUtf8(u8"统一命令：操作=%1，设备编号=%2"))
             .arg(operationName(preview.command.operation), device);
    return lines.join('\n');
}

bool Widget::requireSequentialFormat()
{
    if (!ProtocolConfig::retiredFormat(m_draftRoot)) return true;
    const QString reason = QString::fromUtf8(u8"UNSUPPORTED_FORMAT：模板与样例格式已停用。仅显示其中保留的顺序字段；请检查并显式点击“保存配置”恢复后再解析或发送。原格式内容在保存时保留备份。");
    m_targetOutput->clear(); m_previewOutput->setPlainText(reason); m_validationState->setText(reason);
    return false;
}

void Widget::parseFormatPreview()
{
    m_targetOutput->clear(); m_previewOutput->clear();
    if (!requireSequentialFormat()) return;
    QJsonObject draft;
    if (!collectUiDraft(&draft)) { m_previewOutput->setPlainText(m_validationState->text()); return; }
    ProtocolConfig config; config.root = draft;
    TranslationError error; const QByteArray input = inputBytes(config, &error); ConversionPreview parsed;
    if (error.isValid() || !ConversionEngine(config).parsePreview(input, &parsed, &error)) {
        m_previewOutput->setPlainText(QString::fromUtf8(u8"格式解析失败：%1\n%2").arg(error.code, error.message)); return;
    }
    QStringList lines{QString::fromUtf8(u8"顺序字段解析成功（当前编辑内容；未生成或发送目标帧）")};
    for (const QString &name : parsed.fieldOrder) lines += fieldPreview(name, parsed.fields.value(name));
    lines += parsed.notices; m_previewOutput->setPlainText(lines.join('\n'));
}

void Widget::preview(bool live)
{
    m_targetOutput->clear();
    if (!requireSequentialFormat()) return;
    if (!live && !draftSaved()) {
        m_previewOutput->setPlainText(QString::fromUtf8(u8"预览暂停：请先点击“保存配置”。"));
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
    if (!engine.convertBatch(input, live, &converted, &conversionError)) {
        m_previewOutput->setPlainText(QString::fromUtf8(u8"转换已拒绝\n错误代码：%1\n原因：%2")
                                      .arg(conversionError.code, conversionError.message));
        appendLog(QStringLiteral("%1: %2").arg(conversionError.code, conversionError.message));
        return;
    }
    m_previewOutput->setPlainText((live ? QString::fromUtf8(u8"报文调试：使用当前编辑内容\n")
                                       : QString::fromUtf8(u8"使用当前已保存配置\n")) + previewText(converted));
    if (converted.steps.size() == 1) m_targetOutput->setPlainText(QString::fromUtf8(u8"预览帧（未发送）：\n") + converted.targetFrame.toHex(' ').toUpper());
    else {
        QStringList frames;
        for (int i = 0; i < converted.steps.size(); ++i)
            frames << QString::fromUtf8(u8"第%1条预览帧（未发送）：\n%2").arg(i + 1).arg(QString::fromLatin1(converted.steps[i].targetFrame.toHex(' ').toUpper()));
        m_targetOutput->setPlainText(frames.join('\n'));
    }
    if (!live) {
        appendLog(QString::fromUtf8(u8"预览完成，未访问串口。"));
        return;
    }
    m_rawReply->clear();
    m_replyResult->clear();
    QList<UnifiedCommand> commands;
    for (const auto &step : converted.steps) {
        auto command = step.command; command.packetDebug = true; commands.append(command);
    }
    m_lastRequestId = m_service->submitUnifiedGroup(commands);
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
