#include <QtTest>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTabWidget>
#include <QTextEdit>
#include <QTranslator>
#include <QMenu>
#include <QGroupBox>
#include <QJsonArray>
#include <QJsonDocument>
#include "widget.h"
#include "transport/serialtransport.h"
#include "core/translationservice.h"

class WidgetTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void draftLifecycleAndVisibleFailures();
    void advancedJsonAndDynamicControls();
    void simpleMappingEditorAndInvalidNumbers();
    void advancedMappingIsPreserved();
    void queryAndRelativeMappingControls();
    void renderChinesePages();
private:
    QTranslator m_chinese;
};

void WidgetTest::initTestCase()
{
    QVERIFY(m_chinese.load(QStringLiteral(":/translations/qt_zh_CN.qm")));
    qApp->installTranslator(&m_chinese);
    QLineEdit edit;
    std::unique_ptr<QMenu> menu(edit.createStandardContextMenu());
    bool chineseCopy = false;
    for (auto *action : menu->actions())
        if (action->text().contains(QString::fromUtf8(u8"复制"))) chineseCopy = true;
    QVERIFY(chineseCopy);
}

static QPushButton *button(Widget &widget, const QString &text)
{
    for (auto *b : widget.findChildren<QPushButton *>()) if (b->text() == text) return b;
    return nullptr;
}

void WidgetTest::draftLifecycleAndVisibleFailures()
{
    QDir::setCurrent(QFileInfo(QDir(QString::fromLatin1(QT_TESTCASE_BUILDDIR)).absoluteFilePath(QString::fromLatin1(__FILE__))).dir().absoluteFilePath(".."));
    Widget widget;
    QCOMPARE(widget.findChildren<SerialTransport *>().size(), 1);
    auto *input = widget.findChild<QLineEdit *>("customerInput");
    auto *fields = widget.findChild<QTableWidget *>("fieldTable");
    auto *preview = widget.findChild<QTextEdit *>("previewOutput");
    auto *target = widget.findChild<QTextEdit *>("targetOutput");
    auto *reason = widget.findChild<QLabel *>("liveReason");
    QVERIFY(input && fields && preview && target && reason);
    auto *previewButton = button(widget, QString::fromUtf8(u8"预览但不发送"));
    auto *validateButton = button(widget, QString::fromUtf8(u8"校验配置"));
    auto *enableButton = button(widget, QString::fromUtf8(u8"启用配置"));
    auto *saveButton = button(widget, QString::fromUtf8(u8"保存到草稿"));
    auto *sendButton = button(widget, QString::fromUtf8(u8"真实发送到下游"));
    QVERIFY(previewButton && validateButton && enableButton && saveButton && sendButton);
    previewButton->click();
    QVERIFY(preview->toPlainText().contains(QString::fromUtf8(u8"2000")));
    QVERIFY(target->toPlainText().contains("AA AA 02 01 08 F7 00 11"));
    fields->item(2, 8)->setText("1000");
    validateButton->click(); enableButton->click();
    QVERIFY(reason->text().contains(QString::fromUtf8(u8"尚未显式启用")));
    previewButton->click(); QVERIFY(target->toPlainText().isEmpty());
    QVERIFY(preview->toPlainText().contains(QString::fromUtf8(u8"保存")));
    saveButton->click(); validateButton->click(); enableButton->click();
    auto *confirm = widget.findChild<QCheckBox *>("deviceConfirmed");
    QVERIFY(confirm->isEnabled()); confirm->setChecked(true);
    QVERIFY(reason->text().contains(QString::fromUtf8(u8"尚未连接")));
    QVERIFY(!sendButton->isEnabled());
    previewButton->click(); QVERIFY(target->toPlainText().isEmpty());
    QVERIFY(preview->toPlainText().contains(QString::fromUtf8(u8"最大值")));
    input->setText("/1A500R\\r"); previewButton->click();
    QVERIFY(target->toPlainText().contains("AA AA"));
    fields->item(2, 8)->setText("abc"); saveButton->click();
    // 保存失败仍为未保存表单，后续不能继续校验旧值并冒充新的成功版本。
    validateButton->click(); previewButton->click();
    QVERIFY(target->toPlainText().isEmpty()); QVERIFY(!confirm->isChecked());
    QVERIFY(!confirm->isEnabled());
}

void WidgetTest::advancedJsonAndDynamicControls()
{
    QDir::setCurrent(QFileInfo(QDir(QString::fromLatin1(QT_TESTCASE_BUILDDIR)).absoluteFilePath(QString::fromLatin1(__FILE__))).dir().absoluteFilePath(".."));
    Widget widget;
    auto *fields = widget.findChild<QTableWidget *>("fieldTable");
    auto *maps = widget.findChild<QTableWidget *>("mappingTable");
    auto *json = widget.findChild<QTextEdit *>("advancedJson");
    QVERIFY(fields && maps && json);
    QCOMPARE(fields->horizontalHeaderItem(2)->text(), QString::fromUtf8(u8"字段作用"));
    QVERIFY(fields->isColumnHidden(6));
    QCOMPARE(maps->columnCount(), 4);
    auto *source = widget.findChild<QComboBox *>("mappingSource");
    auto *fixed = widget.findChild<QLineEdit *>("mappingValue");
    auto *multiplier = widget.findChild<QLineEdit *>("mappingMultiplier");
    source->setCurrentIndex(source->findData("@constant"));
    QVERIFY(!fixed->isHidden()); QVERIFY(multiplier->isHidden());
    source->setCurrentIndex(source->findData(QString::fromUtf8(u8"位置")));
    fields->item(2, 1)->setText(QString::fromUtf8(u8"新位置"));
    QVERIFY(source->currentText().contains(QString::fromUtf8(u8"未找到")));
    source->setCurrentIndex(source->findData(QString::fromUtf8(u8"新位置")));
    button(widget, QString::fromUtf8(u8"保存到草稿"))->click();
    // 示例含多条运动映射：字段改名后，其他映射也必须显式修正，不能由界面默默替换失效引用。
    QJsonObject renamedRoot = QJsonDocument::fromJson(json->toPlainText().toUtf8()).object();
    QJsonArray renamedMappings = renamedRoot.value("mappings").toArray();
    for (int i = 0; i < renamedMappings.size(); ++i) {
        QJsonObject mapping = renamedMappings.at(i).toObject(); QJsonObject parameters = mapping.value("parameters").toObject();
        for (const QString &key : parameters.keys()) {
            QJsonObject expression = parameters.value(key).toObject();
            if (expression.value("field").toString() == QString::fromUtf8(u8"位置")) expression.insert("field", QString::fromUtf8(u8"新位置"));
            parameters.insert(key, expression);
        }
        mapping.insert("parameters", parameters);
        QJsonObject input = mapping.value("input").toObject(); QJsonArray omitted = input.value("omitFields").toArray();
        for (int j = 0; j < omitted.size(); ++j) if (omitted.at(j).toString() == QString::fromUtf8(u8"位置")) omitted[j] = QString::fromUtf8(u8"新位置");
        if (!omitted.isEmpty()) { input.insert("omitFields", omitted); mapping.insert("input", input); }
        renamedMappings[i] = mapping;
    }
    renamedRoot.insert("mappings", renamedMappings);
    const QString saved = QString::fromUtf8(QJsonDocument(renamedRoot).toJson());
    json->setPlainText(saved + "broken");
    button(widget, QString::fromUtf8(u8"保存到草稿"))->click();
    QVERIFY(json->toPlainText().endsWith("broken"));
    button(widget, QString::fromUtf8(u8"预览但不发送"))->click();
    QVERIFY(widget.findChild<QTextEdit *>("targetOutput")->toPlainText().isEmpty());
    json->setPlainText(saved);
    button(widget, QString::fromUtf8(u8"应用 JSON 到草稿"))->click();
    button(widget, QString::fromUtf8(u8"校验配置"))->click();
    button(widget, QString::fromUtf8(u8"预览但不发送"))->click();
    QVERIFY(widget.findChild<QTextEdit *>("targetOutput")->toPlainText().contains("AA AA"));
}

static QJsonObject draftRoot(Widget &widget)
{
    return QJsonDocument::fromJson(widget.findChild<QTextEdit *>("advancedJson")->toPlainText().toUtf8()).object();
}

static void applyRoot(Widget &widget, const QJsonObject &root)
{
    widget.findChild<QTextEdit *>("advancedJson")->setPlainText(QString::fromUtf8(QJsonDocument(root).toJson()));
    button(widget, QString::fromUtf8(u8"应用 JSON 到草稿"))->click();
}

void WidgetTest::simpleMappingEditorAndInvalidNumbers()
{
    QDir::setCurrent(QFileInfo(QDir(QString::fromLatin1(QT_TESTCASE_BUILDDIR)).absoluteFilePath(QString::fromLatin1(__FILE__))).dir().absoluteFilePath(".."));
    Widget widget;
    auto *maps = widget.findChild<QTableWidget *>("mappingTable");
    auto *source = widget.findChild<QComboBox *>("mappingSource");
    auto *multiplier = widget.findChild<QLineEdit *>("mappingMultiplier");
    auto *save = widget.findChild<QPushButton *>("saveMappings");
    auto *preview = widget.findChild<QTextEdit *>("previewOutput");
    auto *target = widget.findChild<QTextEdit *>("targetOutput");
    QVERIFY(maps && source && multiplier && save);
    QCOMPARE(maps->columnCount(), 4);
    QCOMPARE(maps->horizontalHeaderItem(2)->text(), QString::fromUtf8(u8"参数说明"));
    QCOMPARE(maps->editTriggers(), QAbstractItemView::NoEditTriggers);
    QVERIFY(source->findData(QString::fromUtf8(u8"命令码")) < 0);
    multiplier->setText("2.5"); save->click();
    QJsonObject expression = draftRoot(widget).value("mappings").toArray().at(0).toObject().value("parameters").toObject().value("positionUm").toObject();
    QCOMPARE(expression.value("kind").toString(), QString("linear"));
    QCOMPARE(expression.value("numerator").toInt(), 5); QCOMPARE(expression.value("denominator").toInt(), 2);
    button(widget, QString::fromUtf8(u8"预览但不发送"))->click(); QVERIFY(preview->toPlainText().contains("5000"));
    multiplier->setText("abc"); save->click();
    button(widget, QString::fromUtf8(u8"预览但不发送"))->click(); QVERIFY(target->toPlainText().isEmpty());
    QCOMPARE(draftRoot(widget).value("mappings").toArray().at(0).toObject().value("parameters").toObject().value("positionUm").toObject(), expression);
    multiplier->setText("1"); source->setCurrentIndex(source->findData("@constant"));
    widget.findChild<QLineEdit *>("mappingValue")->setText("4321"); save->click();
    button(widget, QString::fromUtf8(u8"预览但不发送"))->click(); QVERIFY(preview->toPlainText().contains("4321"));
    source->setCurrentIndex(source->findData(QString::fromUtf8(u8"位置"))); save->click();
    expression = draftRoot(widget).value("mappings").toArray().at(0).toObject().value("parameters").toObject().value("positionUm").toObject();
    QCOMPARE(expression.value("kind").toString(), QString("field"));
    QVERIFY(!expression.contains("value"));
}

void WidgetTest::advancedMappingIsPreserved()
{
    QDir::setCurrent(QFileInfo(QDir(QString::fromLatin1(QT_TESTCASE_BUILDDIR)).absoluteFilePath(QString::fromLatin1(__FILE__))).dir().absoluteFilePath(".."));
    Widget widget;
    QJsonObject root = draftRoot(widget); QJsonArray mappings = root.value("mappings").toArray();
    QJsonObject mapping = mappings.at(0).toObject();
    QJsonObject expression{{"kind", "linear"}, {"field", QString::fromUtf8(u8"位置")}, {"numerator", 1}, {"denominator", 3},
        {"offset", 17}, {"round", "floor"}, {"note", QString::fromUtf8(u8"保留高级关系备注")}};
    mapping.insert("parameters", QJsonObject{{"positionUm", expression}});
    mapping.insert("description", QString::fromUtf8(u8"保留来源信息"));
    mapping.insert("input", QJsonObject{{"footer", "R\\r"}, {"extension", QJsonObject{{"source", "manual"}}}});
    mappings[0] = mapping; root.insert("mappings", mappings); applyRoot(widget, root);
    QCOMPARE(widget.findChild<QLineEdit *>("mappingMultiplier")->text(), QString("1/3"));
    QVERIFY(!widget.findChild<QGroupBox *>("mappingMore")->isChecked());
    auto *response = widget.findChild<QComboBox *>("mappingResponse"); response->setCurrentIndex(response->findData("complete"));
    widget.findChild<QPushButton *>("saveMappings")->click();
    QJsonObject saved = draftRoot(widget).value("mappings").toArray().at(0).toObject();
    QCOMPARE(saved.value("parameters").toObject().value("positionUm").toObject(), expression);
    QCOMPARE(saved.value("input"), mapping.value("input"));
    QCOMPARE(saved.value("description"), mapping.value("description"));
    QCOMPARE(saved.value("responsePolicy").toString(), QString("complete"));
    // 改名会暴露失效引用，不能在保存时悄悄选中别的数值字段。
    widget.findChild<QTableWidget *>("fieldTable")->item(2, 1)->setText(QString::fromUtf8(u8"新位置"));
    QVERIFY(widget.findChild<QComboBox *>("mappingSource")->currentText().contains(QString::fromUtf8(u8"未找到")));
    widget.findChild<QPushButton *>("saveMappings")->click();
    button(widget, QString::fromUtf8(u8"校验配置"))->click();
    button(widget, QString::fromUtf8(u8"预览但不发送"))->click();
    QVERIFY(widget.findChild<QTextEdit *>("targetOutput")->toPlainText().isEmpty());
}

void WidgetTest::queryAndRelativeMappingControls()
{
    QDir::setCurrent(QFileInfo(QDir(QString::fromLatin1(QT_TESTCASE_BUILDDIR)).absoluteFilePath(QString::fromLatin1(__FILE__))).dir().absoluteFilePath(".."));
    Widget widget;
    auto *maps = widget.findChild<QTableWidget *>("mappingTable");
    auto *operation = widget.findChild<QComboBox *>("mappingOperation");
    auto *source = widget.findChild<QComboBox *>("mappingSource");
    auto *multiplier = widget.findChild<QLineEdit *>("mappingMultiplier");
    auto *response = widget.findChild<QComboBox *>("mappingResponse");
    auto *save = widget.findChild<QPushButton *>("saveMappings");
    int queryRow = -1, relativeRow = -1;
    for (int r = 0; r < maps->rowCount(); ++r) {
        const QString code = maps->item(r, 0)->text();
        if (code == "?0") queryRow = r;
        if (code == "P") relativeRow = r;
    }
    QVERIFY(queryRow >= 0 && relativeRow >= 0);
    maps->selectRow(queryRow);
    QCOMPARE(operation->currentData().toString(), QString("QUERY_MOTOR"));
    QVERIFY(source->isHidden()); QVERIFY(multiplier->isHidden()); QVERIFY(response->isHidden());
    QVERIFY(widget.findChild<QLabel *>("mappingParameterHint")->text().contains(QString::fromUtf8(u8"无参数")));
    QVERIFY(!widget.findChild<QCheckBox *>("mappingDefaultFooter")->isChecked());
    QCOMPARE(widget.findChild<QLineEdit *>("mappingFooter")->text(), QString("\\r"));
    widget.findChild<QLineEdit *>("customerInput")->setText("/1?0\\r");
    button(widget, QString::fromUtf8(u8"预览但不发送"))->click();
    QVERIFY(widget.findChild<QTextEdit *>("targetOutput")->toPlainText().contains("00 12"));
    maps->selectRow(relativeRow); QVERIFY(!source->isHidden()); QVERIFY(!multiplier->isHidden());
    QCOMPARE(operation->currentData().toString(), QString("RELATIVE_FORWARD"));
    QVERIFY(widget.findChild<QLabel *>("mappingParameterHint")->text().contains(QString::fromUtf8(u8"移动距离")));
    multiplier->setText("0.1"); save->click();
    QJsonObject expression = draftRoot(widget).value("mappings").toArray().at(relativeRow).toObject().value("parameters").toObject().value("distanceUm").toObject();
    QCOMPARE(expression.value("numerator").toInt(), 1); QCOMPARE(expression.value("denominator").toInt(), 10);
    QVERIFY(!draftRoot(widget).value("mappings").toArray().at(relativeRow).toObject().value("parameters").toObject().contains("positionUm"));
    // 新增查询模板时自动移除尾部数值参数，但公共地址仍保留；帧尾可在普通表单里直接修改。
    operation->setCurrentIndex(operation->findData("QUERY_MOTOR"));
    QVERIFY(source->isHidden()); QVERIFY(multiplier->isHidden());
    widget.findChild<QCheckBox *>("mappingDefaultFooter")->setChecked(false);
    widget.findChild<QLineEdit *>("mappingFooter")->setText("\\r"); save->click();
    QJsonObject queryMapping = draftRoot(widget).value("mappings").toArray().at(relativeRow).toObject();
    QVERIFY(queryMapping.value("parameters").toObject().isEmpty());
    const QJsonArray omitted = queryMapping.value("input").toObject().value("omitFields").toArray();
    QVERIFY(omitted.contains(QString::fromUtf8(u8"位置"))); QVERIFY(!omitted.contains(QString::fromUtf8(u8"地址")));
    operation->setCurrentIndex(operation->findData("QUERY_DEVICE_INFO")); save->click();
    QCOMPARE(draftRoot(widget).value("mappings").toArray().at(relativeRow).toObject().value("input"), queryMapping.value("input"));
    // 回告格式展示可单独核对，不需要打开真实串口；无效位置不能误显示成可执行的数值。
    auto *service = widget.findChild<TranslationService *>(); QVERIFY(service);
    UnifiedResult reply; reply.success = true; reply.requestId = 1;
    reply.data.insert("motorState", 3); reply.data.insert("positionUm", 0xffffff); reply.data.insert("boardType", 1);
    service->completed(reply);
    const QString shownReply = widget.findChild<QTextEdit *>("replyResult")->toPlainText();
    QVERIFY(shownReply.contains(QString::fromUtf8(u8"运行中")));
    QVERIFY(shownReply.contains(QString::fromUtf8(u8"未回零或未配置")));
    QVERIFY(shownReply.contains(QString::fromUtf8(u8"控制板类型")));
    QVERIFY(!shownReply.contains("16777215"));
}

void WidgetTest::renderChinesePages()
{
    QDir::setCurrent(QFileInfo(QDir(QString::fromLatin1(QT_TESTCASE_BUILDDIR)).absoluteFilePath(QString::fromLatin1(__FILE__))).dir().absoluteFilePath(".."));
    Widget widget;
    widget.show();
    button(widget, QString::fromUtf8(u8"预览但不发送"))->click();
    auto *tabs = widget.findChild<QTabWidget *>(); QVERIFY(tabs);
    const QStringList names = {QString::fromUtf8(u8"客户协议格式"), QString::fromUtf8(u8"命令映射"),
                              QString::fromUtf8(u8"报文调试"), QString::fromUtf8(u8"设备与连接"),
                              QString::fromUtf8(u8"高级配置")};
    QCOMPARE(tabs->count(), names.size());
    const QString renderDir = qEnvironmentVariable("TRANSLATION_RENDER_DIR");
    if (!renderDir.isEmpty()) QVERIFY(QDir().mkpath(renderDir));
    for (int i = 0; i < tabs->count(); ++i) {
        QCOMPARE(tabs->tabText(i), names.at(i));
        tabs->setCurrentIndex(i); QCoreApplication::processEvents();
        if (!renderDir.isEmpty())
            QVERIFY(widget.grab().save(QDir(renderDir).filePath(QString("page-%1.png").arg(i + 1))));
    }
}

QTEST_MAIN(WidgetTest)
#include "tst_widget.moc"
