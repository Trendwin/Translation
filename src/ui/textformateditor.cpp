#include "textformateditor.h"
#include "config/textformat.h"
#include "core/conversionengine.h"
#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QTableWidget>
#include <QTextEdit>
#include <QToolButton>
#include <QVBoxLayout>

namespace
{
QTableWidget *table(QWidget *parent, const QString &name, const QStringList &labels)
{
    auto *t = new QTableWidget(parent);
    t->setObjectName(name);
    t->setColumnCount(labels.size());
    t->setHorizontalHeaderLabels(labels);
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    t->verticalHeader()->hide();
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setMinimumHeight(140);
    t->setMaximumHeight(210);
    return t;
}
QString text(QTableWidget *t, int row, int col)
{
    return t->item(row, col) ? t->item(row, col)->text() : QString();
}
} // namespace
TextFormatEditor::TextFormatEditor(QWidget *parent) : QWidget(parent)
{
    setObjectName("textFormatEditor");
    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    buildSampleUi(rootLayout);
    auto *advancedToggle = new QToolButton(this);
    advancedToggle->setObjectName("textAdvancedToggle");
    advancedToggle->setText(QString::fromUtf8(u8"高级设置：模板与字段表"));
    advancedToggle->setCheckable(true);
    advancedToggle->setArrowType(Qt::RightArrow);
    advancedToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    auto *advanced = new QWidget(this);
    advanced->setObjectName("textAdvancedSettings");
    auto *layout = new QVBoxLayout(advanced);
    layout->setContentsMargins(0, 0, 0, 0);
    rootLayout->addWidget(advancedToggle);
    rootLayout->addWidget(advanced);
    advanced->hide();
    connect(advancedToggle, &QToolButton::toggled, this,
            [advancedToggle, advanced](bool open)
            {
                advanced->setVisible(open);
                advancedToggle->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
            });
    auto *help = new QLabel(
        QString::fromUtf8(
            u8"模板只填写帧头与帧尾之间的内容。固定文字原样匹配，{字段名} 表示变长字段；必须包含 {command} "
            u8"和 {arguments}。字段按首次出现的后续固定文字定界，字段值不能包含该分隔文字。可用 "
            u8"\\r、\\n、\\t、\\xNN。每包一条命令。"),
        this);
    help->setWordWrap(true);
    layout->addWidget(help);
    m_template = new QLineEdit(this);
    m_template->setObjectName("textPayloadTemplate");
    m_template->setPlaceholderText("{command}={arguments}");
    auto *form = new QFormLayout;
    form->addRow(QString::fromUtf8(u8"外层正文模板"), m_template);
    layout->addLayout(form);
    m_fields = table(this, "textOuterFields",
                     {QString::fromUtf8(u8"外层字段名"), QString::fromUtf8(u8"数据类型"),
                      QString::fromUtf8(u8"用途"), QString::fromUtf8(u8"固定值（可选）"),
                      QString::fromUtf8(u8"最小值"), QString::fromUtf8(u8"最大值")});
    layout->addWidget(m_fields);
    auto fieldButtons = [this, layout](QTableWidget *t, bool commandParameters = false)
    {
        auto *buttons = new QHBoxLayout;
        auto *add = new QPushButton(QString::fromUtf8(u8"添加字段"), this);
        auto *remove = new QPushButton(QString::fromUtf8(u8"删除字段"), this);
        add->setProperty("commandParameterControl", commandParameters);
        remove->setProperty("commandParameterControl", commandParameters);
        buttons->addWidget(add);
        buttons->addWidget(remove);
        buttons->addStretch();
        layout->addLayout(buttons);
        connect(add, &QPushButton::clicked, this,
                [this, t]()
                {
                    addField(t, {{"name", "value"},
                                 {"type", "uint"},
                                 {"role", t == m_fields ? "metadata" : "parameter"}});
                    notifyChanged();
                });
        connect(remove, &QPushButton::clicked, this,
                [this, t]()
                {
                    if (t->currentRow() >= 0)
                        t->removeRow(t->currentRow());
                    notifyChanged();
                });
        connect(t, &QTableWidget::cellChanged, this, [this]() { notifyChanged(); });
    };
    fieldButtons(m_fields);
    layout->addWidget(new QLabel(
        QString::fromUtf8(u8"独立命令格式（先选择命令，再编辑下方参数；无需先建立趋盛映射）"), this));
    m_commands = table(this, "textCommands",
                       {QString::fromUtf8(u8"命令码"), QString::fromUtf8(u8"参数模板（零参数可留空）")});
    layout->addWidget(m_commands);
    auto *buttons = new QHBoxLayout;
    auto *add = new QPushButton(QString::fromUtf8(u8"添加命令格式"), this);
    add->setObjectName("addTextCommand");
    auto *remove = new QPushButton(QString::fromUtf8(u8"删除命令格式"), this);
    buttons->addWidget(add);
    buttons->addWidget(remove);
    buttons->addStretch();
    layout->addLayout(buttons);
    m_parameterContext = new QLabel(this);
    m_parameterContext->setObjectName("textParameterContext");
    layout->addWidget(m_parameterContext);
    m_parameters = table(this, "textCommandFields",
                         {QString::fromUtf8(u8"选中命令的参数名"), QString::fromUtf8(u8"数据类型"),
                          QString::fromUtf8(u8"用途"), QString::fromUtf8(u8"固定值（可选）"),
                          QString::fromUtf8(u8"最小值"), QString::fromUtf8(u8"最大值")});
    layout->addWidget(m_parameters);
    fieldButtons(m_parameters, true);
    updateParameterContext();
    auto *notice = new QLabel(
        QString::fromUtf8(
            u8"普通元数据仅展示；可映射参数可作为趋盛参数来源。等待、延时等执行含义请选择“执行控制（仅解析）”"
            u8"，本版本会拒绝转换发送。校验值仅提取文本，尚未校验。外层固定地址、类型等可直接写进模板。"),
        this);
    notice->setWordWrap(true);
    layout->addWidget(notice);
    connect(m_template, &QLineEdit::textChanged, this, [this]() { notifyChanged(); });
    connect(m_commands, &QTableWidget::cellChanged, this,
            [this]()
            {
                if (!m_loading)
                    updateParameterContext();
                notifyChanged();
            });
    connect(m_commands, &QTableWidget::currentCellChanged, this, [this]() { selectCommand(); });
    connect(add, &QPushButton::clicked, this,
            [this]()
            {
                storeParameters();
                const int r = m_commands->rowCount();
                m_commands->insertRow(r);
                auto *item = new QTableWidgetItem("NEW");
                item->setData(Qt::UserRole, QJsonObject{{"fields", QJsonArray()}});
                m_commands->setItem(r, 0, item);
                m_commands->setItem(r, 1, new QTableWidgetItem());
                m_commands->setCurrentCell(r, 0);
                notifyChanged();
            });
    connect(remove, &QPushButton::clicked, this,
            [this]()
            {
                const int r = m_commands->currentRow();
                if (r < 0)
                    return;
                m_loading = true;
                m_selected = -1;
                m_commands->removeRow(r);
                m_parameters->setRowCount(0);
                m_loading = false;
                selectCommand();
                notifyChanged();
            });
}
void TextFormatEditor::notifyChanged()
{
    if (!m_loading)
    {
        invalidateSample();
        refreshSampleSummary(false);
        emit changed();
    }
}
void TextFormatEditor::addField(QTableWidget *t, const QJsonObject &f)
{
    const QSignalBlocker blocker(t);
    const int r = t->rowCount();
    t->insertRow(r);
    auto *name = new QTableWidgetItem(f.value("name").toString());
    name->setData(Qt::UserRole, f);
    t->setItem(r, 0, name);
    auto *type = new QComboBox(t);
    type->addItem(QString::fromUtf8(u8"文本"), "ascii");
    type->addItem(QString::fromUtf8(u8"无符号整数"), "uint");
    type->addItem(QString::fromUtf8(u8"有符号整数"), "int");
    type->setCurrentIndex(type->findData(f.value("type").toString("uint")));
    t->setCellWidget(r, 1, type);
    auto *role = new QComboBox(t);
    role->addItem(QString::fromUtf8(u8"普通元数据"), "metadata");
    role->addItem(QString::fromUtf8(u8"可映射参数"), "parameter");
    role->addItem(QString::fromUtf8(u8"执行控制（仅解析）"), "execution");
    role->addItem(QString::fromUtf8(u8"校验值（未校验）"), "checksum");
    role->setCurrentIndex(role->findData(f.value("role").toString("parameter")));
    t->setCellWidget(r, 2, role);
    t->setItem(r, 3, new QTableWidgetItem(f.value("fixedValue").toVariant().toString()));
    t->setItem(r, 4, new QTableWidgetItem(f.value("min").toVariant().toString()));
    t->setItem(r, 5, new QTableWidgetItem(f.value("max").toVariant().toString()));
    auto *minimumItem = t->item(r, 4);
    auto *maximumItem = t->item(r, 5);
    auto updateRanges = [minimumItem, maximumItem, type]()
    {
        const bool numeric = type->currentData().toString() != "ascii";
        for (auto *item : {minimumItem, maximumItem})
        {
            item->setFlags(numeric ? item->flags() | Qt::ItemIsEditable | Qt::ItemIsEnabled
                                   : item->flags() & ~Qt::ItemIsEditable & ~Qt::ItemIsEnabled);
            item->setToolTip(QString::fromUtf8(u8"可选整数范围；留空表示不限制"));
        }
    };
    updateRanges();
    connect(type, QOverload<int>::of(&QComboBox::currentIndexChanged), this, updateRanges);
    connect(type, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { notifyChanged(); });
    connect(role, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { notifyChanged(); });
}
QJsonArray TextFormatEditor::readFields(QTableWidget *t) const
{
    QJsonArray fields;
    for (int r = 0; r < t->rowCount(); ++r)
    {
        auto f = t->item(r, 0)->data(Qt::UserRole).toJsonObject();
        f.insert("name", text(t, r, 0));
        f.insert("type", qobject_cast<QComboBox *>(t->cellWidget(r, 1))->currentData().toString());
        f.insert("role", qobject_cast<QComboBox *>(t->cellWidget(r, 2))->currentData().toString());
        f.remove("fixedValue");
        if (!text(t, r, 3).isEmpty())
            f.insert("fixedValue", text(t, r, 3));
        f.remove("min");
        f.remove("max");
        if (f.value("type").toString() != "ascii")
        {
            for (int column = 4; column <= 5; ++column)
            {
                const QString value = text(t, r, column);
                if (value.isEmpty())
                    continue;
                bool ok = false;
                const double number = value.toDouble(&ok);
                // Keep invalid input as an invalid draft; never turn it into zero.
                f.insert(column == 4 ? "min" : "max", ok ? QJsonValue(number) : QJsonValue(value));
            }
        }
        fields.append(f);
    }
    return fields;
}
void TextFormatEditor::storeParameters() const
{
    if (m_selected < 0 || m_selected >= m_commands->rowCount() || !m_commands->item(m_selected, 0))
        return;
    auto c = m_commands->item(m_selected, 0)->data(Qt::UserRole).toJsonObject();
    c.insert("fields", readFields(m_parameters));
    const QSignalBlocker blocker(m_commands);
    m_commands->item(m_selected, 0)->setData(Qt::UserRole, c);
}
void TextFormatEditor::selectCommand()
{
    if (m_loading)
        return;
    storeParameters();
    m_loading = true;
    m_selected = m_commands->currentRow();
    m_parameters->setRowCount(0);
    if (m_selected >= 0 && m_commands->item(m_selected, 0))
        for (const auto &f :
             m_commands->item(m_selected, 0)->data(Qt::UserRole).toJsonObject().value("fields").toArray())
            addField(m_parameters, f.toObject());
    m_loading = false;
    updateParameterContext();
}
void TextFormatEditor::setFormat(const QJsonObject &f)
{
    m_loading = true;
    m_pendingSampleEdits = false;
    m_original = f;
    m_selected = -1;
    m_template->setText(f.value("template").toString());
    m_fields->setRowCount(0);
    m_commands->setRowCount(0);
    m_parameters->setRowCount(0);
    for (const auto &v : f.value("fields").toArray())
        addField(m_fields, v.toObject());
    for (const auto &v : f.value("commands").toArray())
    {
        const auto c = v.toObject();
        int r = m_commands->rowCount();
        m_commands->insertRow(r);
        auto *item = new QTableWidgetItem(c.value("code").toString());
        item->setData(Qt::UserRole, c);
        m_commands->setItem(r, 0, item);
        m_commands->setItem(r, 1, new QTableWidgetItem(c.value("template").toString()));
    }
    m_loading = false;
    if (m_commands->rowCount())
    {
        m_commands->setCurrentCell(0, 0);
        selectCommand();
    }
    updateParameterContext();
    refreshSampleSummary();
    invalidateSample();
}
QJsonObject TextFormatEditor::format() const
{
    storeParameters();
    auto f = m_original;
    f.insert("template", m_template->text());
    f.insert("fields", readFields(m_fields));
    QJsonArray commands;
    for (int r = 0; r < m_commands->rowCount(); ++r)
    {
        auto c = m_commands->item(r, 0)->data(Qt::UserRole).toJsonObject();
        c.insert("code", text(m_commands, r, 0));
        c.insert("template", text(m_commands, r, 1));
        commands.append(c);
    }
    f.insert("commands", commands);
    return f;
}

void TextFormatEditor::updateParameterContext()
{
    const bool selected = m_selected >= 0 && m_selected < m_commands->rowCount();
    m_parameterContext->setText(
        selected ? QString::fromUtf8(u8"命令“%1”的参数").arg(text(m_commands, m_selected, 0))
                 : QString::fromUtf8(u8"请先选择一条命令格式，再编辑参数"));
    m_parameters->setEnabled(selected);
    for (auto *button : findChildren<QPushButton *>())
        if (button->property("commandParameterControl").toBool())
            button->setEnabled(selected);
}

void TextFormatEditor::buildSampleUi(QVBoxLayout *layout)
{
    auto *intro =
        new QLabel(QString::fromUtf8(
                       u8"粘贴完整报文，拆分后点击一行设置含义，再应用。上方帧头、帧尾也是样例使用的边界。"),
                   this);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    auto *summary = new QHBoxLayout;
    summary->addWidget(new QLabel(QString::fromUtf8(u8"已配置命令"), this));
    m_knownCommands = new QComboBox(this);
    m_knownCommands->setObjectName("sampleKnownCommands");
    summary->addWidget(m_knownCommands, 1);
    layout->addLayout(summary);
    auto *sampleRow = new QHBoxLayout;
    sampleRow->addWidget(new QLabel(QString::fromUtf8(u8"完整样例"), this));
    m_sample = new QLineEdit(this);
    m_sample->setObjectName("formatSampleInput");
    m_sample->setPlaceholderText(QString::fromUtf8(u8"包含帧头、帧尾；可写 \\r、\\n 等转义"));
    sampleRow->addWidget(m_sample, 1);
    auto *suggest = new QPushButton(QString::fromUtf8(u8"建议分隔符"), this);
    suggest->setObjectName("suggestSampleSeparators");
    sampleRow->addWidget(suggest);
    layout->addLayout(sampleRow);
    auto *separators = new QHBoxLayout;
    auto separatorEdit = [this, separators](const QString &label, const QString &name, const QString &value,
                                            const QString &tip)
    {
        separators->addWidget(new QLabel(label, this));
        auto *edit = new QLineEdit(value, this);
        edit->setObjectName(name);
        edit->setMaximumWidth(65);
        edit->setToolTip(tip);
        separators->addWidget(edit);
        connect(edit, &QLineEdit::textChanged, this, [this]() { invalidateSample(); });
        return edit;
    };
    m_regionSeparator = separatorEdit(QString::fromUtf8(u8"分段"), "sampleRegionSeparator", ";",
                                      QString::fromUtf8(u8"例如 头部;命令;尾部 中的 ;。没有可留空。"));
    m_fieldSeparator = separatorEdit(QString::fromUtf8(u8"普通字段之间"), "sampleFieldSeparator", "&",
                                     QString::fromUtf8(u8"例如 地址&序号 中的 &。没有可留空。"));
    m_assignmentSeparator =
        separatorEdit(QString::fromUtf8(u8"命令与参数之间"), "sampleCommandSeparator", "=",
                      QString::fromUtf8(u8"例如 MOVE=100 中的 =；正文必须只有一处。也可改为 : 等符号。"));
    m_parameterSeparator = separatorEdit(QString::fromUtf8(u8"多个参数之间"), "sampleParameterSeparator", ",",
                                         QString::fromUtf8(u8"例如 10,20,30 中的 ,。只有一个参数可留空。"));
    separators->addStretch();
    layout->addLayout(separators);
    auto *buttons = new QHBoxLayout;
    auto *split = new QPushButton(QString::fromUtf8(u8"拆分预览"), this);
    split->setObjectName("splitSampleButton");
    m_applySample = new QPushButton(QString::fromUtf8(u8"应用此命令格式"), this);
    m_applySample->setObjectName("applySampleButton");
    m_applySample->setEnabled(false);
    auto *parse = new QPushButton(QString::fromUtf8(u8"解析当前样例"), this);
    parse->setObjectName("parseCurrentSampleButton");
    auto *discard = new QPushButton(QString::fromUtf8(u8"放弃本次拆分"), this);
    discard->setObjectName("discardSampleButton");
    connect(discard, &QPushButton::clicked, this, &TextFormatEditor::discardSample);
    buttons->addWidget(split);
    buttons->addWidget(m_applySample);
    buttons->addWidget(parse);
    buttons->addWidget(discard);
    buttons->addStretch();
    layout->addLayout(buttons);
    m_sampleStatus = new QLabel(QString::fromUtf8(u8"尚无拆分预览。先粘贴样例，再点击“拆分预览”。"), this);
    m_sampleStatus->setObjectName("sampleFormatStatus");
    m_sampleStatus->setWordWrap(true);
    layout->addWidget(m_sampleStatus);
    m_splitPreview = new QLabel(this);
    m_splitPreview->setObjectName("sampleSplitPreview");
    m_splitPreview->setWordWrap(true);
    m_splitPreview->setTextFormat(Qt::RichText);
    m_splitPreview->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
    layout->addWidget(m_splitPreview);
    connect(m_splitPreview, &QLabel::linkActivated, this,
            [this](const QString &index)
            {
                for (int row = 0; row < m_sampleTable->rowCount(); ++row)
                    if (m_sampleTable->item(row, 0)->data(Qt::UserRole).toInt() == index.toInt())
                    {
                        m_sampleTable->setCurrentCell(row, 0);
                        m_sampleTable->scrollToItem(m_sampleTable->item(row, 0));
                        break;
                    }
            });
    m_sampleTable = table(this, "samplePieces",
                          {QString::fromUtf8(u8"样例片段"), QString::fromUtf8(u8"含义"),
                           QString::fromUtf8(u8"字段名"), QString::fromUtf8(u8"类型")});
    m_sampleTable->setMinimumHeight(150);
    m_sampleTable->setMaximumHeight(180);
    m_sampleTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(m_sampleTable);
    m_pieceEditor = new QWidget(this);
    auto *settings = new QGridLayout(m_pieceEditor);
    settings->setContentsMargins(0, 0, 0, 0);
    m_pieceName = new QLineEdit(this);
    m_pieceName->setObjectName("samplePieceName");
    m_pieceType = new QComboBox(this);
    m_pieceType->setObjectName("samplePieceType");
    m_pieceType->addItem(QString::fromUtf8(u8"文本"), "ascii");
    m_pieceType->addItem(QString::fromUtf8(u8"无符号整数"), "uint");
    m_pieceType->addItem(QString::fromUtf8(u8"有符号整数"), "int");
    m_pieceRole = new QComboBox(this);
    m_pieceRole->setObjectName("samplePieceRole");
    m_pieceRole->addItem(QString::fromUtf8(u8"普通字段（仅展示）"), "metadata");
    m_pieceRole->addItem(QString::fromUtf8(u8"参数（可映射）"), "parameter");
    m_pieceRole->addItem(QString::fromUtf8(u8"执行控制（仅解析）"), "execution");
    m_pieceRole->addItem(QString::fromUtf8(u8"校验值（未校验）"), "checksum");
    m_pieceRole->addItem(QString::fromUtf8(u8"固定文字"), "literal");
    m_pieceRole->addItem(QString::fromUtf8(u8"命令码（由分隔符定位）"), "command");
    qobject_cast<QStandardItemModel *>(m_pieceRole->model())
        ->item(m_pieceRole->count() - 1)
        ->setEnabled(false);
    m_pieceFixed = new QLineEdit(this);
    m_pieceFixed->setObjectName("samplePieceFixed");
    m_pieceFixed->setPlaceholderText(QString::fromUtf8(u8"可选"));
    m_pieceMin = new QLineEdit(this);
    m_pieceMin->setObjectName("samplePieceMin");
    m_pieceMax = new QLineEdit(this);
    m_pieceMax->setObjectName("samplePieceMax");
    const QList<QPair<QString, QWidget *>> controls{
        {QString::fromUtf8(u8"字段名"), m_pieceName}, {QString::fromUtf8(u8"含义"), m_pieceRole},
        {QString::fromUtf8(u8"类型"), m_pieceType},   {QString::fromUtf8(u8"固定值"), m_pieceFixed},
        {QString::fromUtf8(u8"最小值"), m_pieceMin},  {QString::fromUtf8(u8"最大值"), m_pieceMax}};
    for (int i = 0; i < controls.size(); ++i)
    {
        settings->addWidget(new QLabel(controls.at(i).first, this), i / 3, (i % 3) * 2);
        settings->addWidget(controls.at(i).second, i / 3, (i % 3) * 2 + 1);
    }
    m_pieceEditor->setEnabled(false);
    layout->addWidget(m_pieceEditor);
    m_sampleResult = new QTextEdit(this);
    m_sampleResult->setObjectName("sampleParseResult");
    m_sampleResult->setReadOnly(true);
    m_sampleResult->setMaximumHeight(110);
    m_sampleResult->hide();
    layout->addWidget(m_sampleResult);
    connect(split, &QPushButton::clicked, this, &TextFormatEditor::splitSample);
    connect(m_applySample, &QPushButton::clicked, this, &TextFormatEditor::applySample);
    connect(parse, &QPushButton::clicked, this, &TextFormatEditor::parseSample);
    connect(m_sample, &QLineEdit::textChanged, this, [this]() { invalidateSample(); });
    connect(suggest, &QPushButton::clicked, this,
            [this]()
            {
                const auto s = SampleFormat::suggestSeparators(m_sample->text());
                m_regionSeparator->setText(s.value("region").toString());
                m_fieldSeparator->setText(s.value("field").toString());
                m_assignmentSeparator->setText(s.value("assignment").toString());
                m_parameterSeparator->setText(s.value("parameter").toString());
                m_sampleStatus->setText(
                    QString::fromUtf8(u8"已按样例建议分隔符，请检查后点击“拆分预览”；现有格式未改变。"));
            });
    connect(m_knownCommands, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this]()
            {
                const QString code = m_knownCommands->currentData().toString();
                const auto samples = m_original.value("sampleEditor").toObject().value("samples").toObject();
                m_sample->setText(samples.value(code).toString());
                invalidateSample();
            });
    connect(m_sampleTable, &QTableWidget::currentCellChanged, this, [this]() { selectSamplePiece(); });
    for (auto *edit : {m_pieceName, m_pieceFixed, m_pieceMin, m_pieceMax})
        connect(edit, &QLineEdit::textChanged, this, [this]() { editSamplePiece(); });
    for (auto *box : {m_pieceRole, m_pieceType})
        connect(box, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this]() { editSamplePiece(); });
}

QJsonObject TextFormatEditor::sampleSeparators() const
{
    return {{"region", m_regionSeparator->text()},
            {"field", m_fieldSeparator->text()},
            {"assignment", m_assignmentSeparator->text()},
            {"parameter", m_parameterSeparator->text()}};
}
QJsonObject TextFormatEditor::sampleExternal() const
{
    return {{"mode", "textTemplate"},
            {"encoding", "ascii"},
            {"header", m_frameHeader},
            {"footer", m_frameFooter},
            {"textFormat", format()}};
}
void TextFormatEditor::setFrameBoundaries(const QString &header, const QString &footer)
{
    if (header == m_frameHeader && footer == m_frameFooter)
        return;
    m_frameHeader = header;
    m_frameFooter = footer;
    invalidateSample();
}
void TextFormatEditor::invalidateSample()
{
    m_hasSampleDraft = false;
    m_applySample->setEnabled(false);
    m_pieceEditor->setEnabled(false);
    m_sampleResult->clear();
    m_sampleResult->hide();
    if (!m_loading)
        m_sampleStatus->setText(
            QString::fromUtf8(u8"样例或设置尚未应用。点击“拆分预览”检查；现有格式保持不变。"));
}
void TextFormatEditor::refreshSampleSummary(bool restoreInputs)
{
    const QSignalBlocker blocker(m_knownCommands);
    const QString chosen = m_knownCommands->currentData().toString();
    const auto current = format();
    m_knownCommands->clear();
    for (const auto &v : current.value("commands").toArray())
    {
        const auto c = v.toObject();
        m_knownCommands->addItem(QString::fromUtf8(u8"%1 · %2 个参数")
                                     .arg(c.value("code").toString())
                                     .arg(c.value("fields").toArray().size()),
                                 c.value("code").toString());
    }
    if (m_knownCommands->count() == 0)
        m_knownCommands->addItem(QString::fromUtf8(u8"尚未配置命令"), QString());
    const auto editor = current.value("sampleEditor").toObject();
    const int i = m_knownCommands->findData(restoreInputs ? editor.value("lastCommand").toString() : chosen);
    if (i >= 0)
        m_knownCommands->setCurrentIndex(i);
    if (!restoreInputs)
        return;
    const auto separators = editor.value("separators").toObject();
    if (!separators.isEmpty())
    {
        m_regionSeparator->setText(separators.value("region").toString());
        m_fieldSeparator->setText(separators.value("field").toString());
        m_assignmentSeparator->setText(separators.value("assignment").toString());
        m_parameterSeparator->setText(separators.value("parameter").toString());
    }
    m_sample->setText(
        editor.value("samples").toObject().value(m_knownCommands->currentData().toString()).toString());
}
void TextFormatEditor::splitSample()
{
    if (m_pendingSampleEdits)
    {
        m_sampleStatus->setText(
            QString::fromUtf8(u8"当前还有未应用的拆分或字段设置，请先应用，或点击“放弃本次拆分”。"));
        return;
    }
    m_hasSampleDraft = false;
    m_applySample->setEnabled(false);
    m_sampleResult->clear();
    m_sampleResult->hide();
    TranslationError error;
    SampleFormatDraft draft;
    if (!SampleFormat::prepare(sampleExternal(), m_sample->text(), sampleSeparators(), &draft, &error))
    {
        m_pieceEditor->setEnabled(false);
        m_sampleStatus->setText(
            QString::fromUtf8(u8"拆分失败：%1（%2）。现有格式未改变。").arg(error.message, error.code));
        return;
    }
    m_sampleDraft = draft;
    m_hasSampleDraft = true;
    m_pendingSampleEdits = !draft.existingCommand;
    m_applySample->setEnabled(true);
    refreshSampleRows();
    m_applySample->setText(draft.existingCommand ? QString::fromUtf8(u8"应用字段设置")
                                                 : QString::fromUtf8(u8"应用此命令格式"));
    m_sampleStatus->setText(
        draft.existingCommand
            ? QString::fromUtf8(
                  u8"已按现有格式拆分，可直接解析；修改字段后点击“应用字段设置”。")
            : QString::fromUtf8(u8"尚未应用：点击片段设置含义，确认后应用。外层字段默认仅展示，等待/"
                                u8"延时、校验等用途请自行指定。"));
}
void TextFormatEditor::refreshSampleRows(bool loadInspector)
{
    const QSignalBlocker blocker(m_sampleTable);
    const int selected = selectedSamplePiece();
    m_sampleTable->setRowCount(0);
    const QMap<QString, QString> roles{{"metadata", QString::fromUtf8(u8"普通字段")},
                                       {"parameter", QString::fromUtf8(u8"参数")},
                                       {"execution", QString::fromUtf8(u8"执行控制（仅解析）")},
                                       {"checksum", QString::fromUtf8(u8"校验值（未校验）")}};
    QString preview;
    QList<int> visible;
    for (int i = 0; i < m_sampleDraft.pieces.size(); ++i)
    {
        const auto &piece = m_sampleDraft.pieces.at(i);
        const QString bytes = SampleFormat::escaped(piece.bytes).toHtmlEscaped();
        if (piece.scope == "command" || !piece.locked)
            preview += QStringLiteral("<a href='%1' style='color:#1464a0'>%2</a>").arg(i).arg(bytes);
        else
            preview += QStringLiteral("<span style='color:#777777'>%1</span>").arg(bytes);
    }
    m_splitPreview->setText(preview);
    // Keep the command and its arguments together, with the envelope fields below.
    for (const QString &scope : {QStringLiteral("command"), QStringLiteral("local"), QStringLiteral("outer")})
        for (int i = 0; i < m_sampleDraft.pieces.size(); ++i)
        {
            const auto &p = m_sampleDraft.pieces.at(i);
            if (p.scope == scope && (p.scope == "command" || !p.locked))
                visible.append(i);
        }
    int selection = -1;
    for (const int index : visible)
    {
        const auto &p = m_sampleDraft.pieces.at(index);
        const int row = m_sampleTable->rowCount();
        m_sampleTable->insertRow(row);
        const QString meaning = p.scope == "command" ? QString::fromUtf8(u8"命令码")
                                : p.literal          ? QString::fromUtf8(u8"固定文字")
                                            : roles.value(p.field.value("role").toString("parameter"));
        const QStringList columns{SampleFormat::escaped(p.bytes), meaning,
                                  p.scope == "command" ? QString::fromUtf8(u8"命令码")
                                  : p.literal          ? QString()
                                                       : p.field.value("name").toString(),
                                  p.literal || p.scope == "command"
                                      ? QString()
                                      : QMap<QString, QString>{{"ascii", QString::fromUtf8(u8"文本")},
                                                               {"uint", QString::fromUtf8(u8"无符号整数")},
                                                               {"int", QString::fromUtf8(u8"有符号整数")}}
                                            .value(p.field.value("type").toString())};
        for (int col = 0; col < columns.size(); ++col)
            m_sampleTable->setItem(row, col, new QTableWidgetItem(columns.at(col)));
        m_sampleTable->item(row, 0)->setData(Qt::UserRole, index);
        if (index == selected)
            selection = row;
    }
    if (m_sampleTable->rowCount())
        m_sampleTable->setCurrentCell(selection >= 0 ? selection : 0, 0);
    if (loadInspector)
        selectSamplePiece();
    else
    {
        const auto &p = m_sampleDraft.pieces.at(selectedSamplePiece());
        m_pieceName->setEnabled(!p.literal);
        m_pieceType->setEnabled(!p.literal);
        m_pieceFixed->setEnabled(!p.literal);
        m_pieceMin->setEnabled(!p.literal && p.field.value("type").toString() != "ascii");
        m_pieceMax->setEnabled(m_pieceMin->isEnabled());
    }
}
int TextFormatEditor::selectedSamplePiece() const
{
    const int row = m_sampleTable->currentRow();
    return row >= 0 && m_sampleTable->item(row, 0) ? m_sampleTable->item(row, 0)->data(Qt::UserRole).toInt()
                                                   : -1;
}
void TextFormatEditor::selectSamplePiece()
{
    const int row = selectedSamplePiece();
    if (!m_hasSampleDraft || row < 0 || row >= m_sampleDraft.pieces.size())
    {
        m_pieceEditor->setEnabled(false);
        return;
    }
    const auto &p = m_sampleDraft.pieces.at(row);
    m_loadingPiece = true;
    m_pieceName->setText(p.scope == "command" ? QString::fromUtf8(u8"命令码")
                                              : p.field.value("name").toString());
    m_pieceType->setCurrentIndex(m_pieceType->findData(p.field.value("type").toString("ascii")));
    m_pieceRole->setCurrentIndex(m_pieceRole->findData(p.scope == "command" ? "command"
                                                       : p.literal
                                                           ? "literal"
                                                           : p.field.value("role").toString("parameter")));
    m_pieceFixed->setText(p.field.value("fixedValue").toVariant().toString());
    m_pieceMin->setText(p.field.value("min").toVariant().toString());
    m_pieceMax->setText(p.field.value("max").toVariant().toString());
    m_pieceEditor->setEnabled(!p.locked);
    m_pieceName->setEnabled(!p.literal);
    m_pieceType->setEnabled(!p.literal);
    m_pieceFixed->setEnabled(!p.literal);
    m_pieceMin->setEnabled(!p.literal && p.field.value("type").toString() != "ascii");
    m_pieceMax->setEnabled(m_pieceMin->isEnabled());
    m_loadingPiece = false;
}
void TextFormatEditor::editSamplePiece()
{
    const int row = selectedSamplePiece();
    if (m_loadingPiece || !m_hasSampleDraft || row < 0 || row >= m_sampleDraft.pieces.size())
        return;
    auto &p = m_sampleDraft.pieces[row];
    if (p.locked)
        return;
    const QString role = m_pieceRole->currentData().toString();
    p.literal = role == "literal";
    if (!p.literal)
    {
        p.field.insert("name", m_pieceName->text());
        p.field.insert("type", m_pieceType->currentData().toString());
        p.field.insert("role", role);
        p.field.remove("fixedValue");
        p.field.remove("min");
        p.field.remove("max");
        if (!m_pieceFixed->text().isEmpty())
            p.field.insert("fixedValue", m_pieceFixed->text());
        if (p.field.value("type").toString() != "ascii")
            for (const auto &entry :
                 QList<QPair<QString, QLineEdit *>>{{"min", m_pieceMin}, {"max", m_pieceMax}})
                if (!entry.second->text().isEmpty())
                {
                    bool ok = false;
                    const double n = entry.second->text().toDouble(&ok);
                    p.field.insert(entry.first, ok ? QJsonValue(n) : QJsonValue(entry.second->text()));
                }
    }
    m_sampleStatus->setText(
        QString::fromUtf8(u8"字段设置尚未应用。确认后点击“%1”。").arg(m_applySample->text()));
    m_pendingSampleEdits = true;
    refreshSampleRows(false);
}
void TextFormatEditor::applySample()
{
    if (!m_hasSampleDraft)
        return;
    TranslationError error;
    QJsonObject updated;
    if (!SampleFormat::apply(sampleExternal(), m_sampleDraft, sampleSeparators(), &updated, &error))
    {
        m_sampleStatus->setText(
            QString::fromUtf8(u8"应用失败：%1（%2）。现有格式未改变。").arg(error.message, error.code));
        return;
    }
    const QString code = m_sampleDraft.code;
    m_pendingSampleEdits = false;
    setFormat(updated);
    emit changed();
    splitSample();
    m_sampleStatus->setText(
        QString::fromUtf8(u8"已应用命令“%1”；点击“解析当前样例”核对，再用底部“保存配置”持久保存。")
            .arg(code));
}
void TextFormatEditor::parseSample()
{
    ProtocolConfig config;
    config.root = {{"schemaVersion", 1}, {"external", sampleExternal()}};
    TranslationError error;
    QByteArray input;
    ConversionPreview preview;
    if (!ProtocolConfig::decodeBytes(m_sample->text(), "ascii", &input, &error) ||
        !ConversionEngine(config).parsePreview(input, &preview, &error))
    {
        m_sampleResult->setPlainText(QString::fromUtf8(u8"解析失败：%1（%2）。若只是拆分预览，请先应用。")
                                         .arg(error.message, error.code));
    }
    else
    {
        QStringList lines{QString::fromUtf8(u8"解析成功：命令 %1；%2 个字段（未发送）")
                              .arg(preview.matchedCommandCode)
                              .arg(preview.fields.size())};
        for (const auto &name : preview.fieldOrder)
            lines << QStringLiteral("%1 = %2").arg(name, preview.fields.value(name).toString());
        lines += preview.notices;
        m_sampleResult->setPlainText(lines.join('\n'));
    }
    m_sampleResult->show();
}

void TextFormatEditor::discardSample()
{
    m_pendingSampleEdits = false;
    invalidateSample();
    m_sampleTable->setRowCount(0);
    m_splitPreview->clear();
    m_sampleStatus->setText(QString::fromUtf8(u8"已放弃本次拆分与未应用字段设置，现有格式未改变。"));
}
