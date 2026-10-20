#include "textformateditor.h"
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
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
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
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
        emit changed();
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
