#include <QtTest>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDirIterator>
#include <QRegularExpression>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTabWidget>
#include <QTextEdit>
#include <QTranslator>
#include <QMenu>
#include <QListView>
#include <QStandardItemModel>
#include <QScrollArea>
#include <QSpinBox>
#include "qusheng/qushengcommandcatalog.h"
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
    void completeCatalogAndSearchPreserveSelection();
    void catalogTextIsReadableAndSourceLiteralsAreExplicit();
    void pendingMappingEditorAndFieldRefresh();
    void pendingAdvancedRelationAndTargetSwitch();
    void simplifiedExpertInterface();
    void unifiedFormatAndVisibleErrors();
    void genericScalarRanges();
    void sdkGenericMappingsAndDefaults();
    void sdkQueriesAndRequiredParameters();
    void sdkReportsAreVisibleWithoutCompletingRequests();
    void fixedMotorActionsAndLegacyCompatibility();
    void stopResetMappingsHaveNoDistanceControls();
    void fieldNamesFollowAllMappings();
    void fieldIdentitySwapAndDeferredSave();
    void fieldDeletionTypesAndJsonBoundary();
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

static QPushButton *button(Widget &widget, const char *objectName)
{
    return widget.findChild<QPushButton *>(QString::fromLatin1(objectName));
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
    auto *previewButton = button(widget, "previewButton");
    auto *validateButton = button(widget, "validateConfig");
    auto *enableButton = button(widget, "enableConfig");
    auto *saveButton = button(widget, "saveFields");
    auto *sendButton = button(widget, "sendButton");
    QVERIFY(previewButton && validateButton && enableButton && saveButton && sendButton);
    previewButton->click();
    QVERIFY(preview->toPlainText().contains(QString::fromUtf8(u8"2000")));
    QVERIFY(target->toPlainText().contains("AA AA 02 01 08 F7 00 11"));
    fields->item(2, 8)->setText("1000");
    validateButton->click(); enableButton->click();
    QVERIFY(reason->text().contains(QString::fromUtf8(u8"未启用")));
    previewButton->click(); QVERIFY(target->toPlainText().isEmpty());
    QVERIFY(preview->toPlainText().contains(QString::fromUtf8(u8"保存")));
    saveButton->click(); validateButton->click(); enableButton->click();
    auto *confirm = widget.findChild<QCheckBox *>("deviceConfirmed");
    QVERIFY(confirm->isEnabled()); confirm->setChecked(true);
    QVERIFY(reason->text().contains(QString::fromUtf8(u8"串口未连接")));
    QVERIFY(!sendButton->isEnabled());
    previewButton->click(); QVERIFY(target->toPlainText().isEmpty());
    QVERIFY(preview->toPlainText().contains(QString::fromUtf8(u8"最大值")));
    input->setText("/1A500R\\r"); previewButton->click();
    QVERIFY(target->toPlainText().contains("AA AA"));
    const QJsonObject beforeInvalidRange = QJsonDocument::fromJson(widget.findChild<QTextEdit *>("advancedJson")->toPlainText().toUtf8()).object();
    fields->item(2, 8)->setText("abc"); saveButton->click();
    QVERIFY(widget.findChild<QLabel *>("validationState")->text().contains(QString::fromUtf8(u8"第 3 行最大值不是数字")));
    QCOMPARE(QJsonDocument::fromJson(widget.findChild<QTextEdit *>("advancedJson")->toPlainText().toUtf8()).object(), beforeInvalidRange);
    // 保存失败仍为未保存表单，后续不能继续校验旧值并冒充新的成功版本。
    validateButton->click(); previewButton->click();
    QVERIFY(target->toPlainText().isEmpty()); QVERIFY(!confirm->isChecked());
    QVERIFY(!confirm->isEnabled());
    fields->item(2, 8)->setText("1000"); fields->item(2, 7)->setText("invalid"); saveButton->click();
    QVERIFY(widget.findChild<QLabel *>("validationState")->text().contains(QString::fromUtf8(u8"第 3 行最小值不是数字")));
    QCOMPARE(QJsonDocument::fromJson(widget.findChild<QTextEdit *>("advancedJson")->toPlainText().toUtf8()).object(), beforeInvalidRange);
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
    QCOMPARE(maps->columnCount(), 3);
    auto *source = widget.findChild<QComboBox *>("mappingSource");
    auto *fixed = widget.findChild<QLineEdit *>("mappingValue");
    auto *multiplier = widget.findChild<QLineEdit *>("mappingMultiplier");
    source->setCurrentIndex(source->findData("@constant"));
    QVERIFY(!fixed->isHidden()); QVERIFY(multiplier->isHidden());
    source->setCurrentIndex(source->findData(QString::fromUtf8(u8"位置")));
    fields->item(2, 1)->setText(QString::fromUtf8(u8"新位置"));
    QCOMPARE(source->currentData().toString(), QString::fromUtf8(u8"新位置"));
    button(widget, "saveFields")->click();
    const QString saved = json->toPlainText();
    json->setPlainText(saved + "broken");
    button(widget, "saveFields")->click();
    QVERIFY(json->toPlainText().endsWith("broken"));
    button(widget, "previewButton")->click();
    QVERIFY(widget.findChild<QTextEdit *>("targetOutput")->toPlainText().isEmpty());
    json->setPlainText(saved);
    button(widget, "applyJson")->click();
    button(widget, "validateConfig")->click();
    button(widget, "previewButton")->click();
    QVERIFY(widget.findChild<QTextEdit *>("targetOutput")->toPlainText().contains("AA AA"));
}

static QJsonObject draftRoot(Widget &widget)
{
    return QJsonDocument::fromJson(widget.findChild<QTextEdit *>("advancedJson")->toPlainText().toUtf8()).object();
}

static void applyRoot(Widget &widget, const QJsonObject &root)
{
    widget.findChild<QTextEdit *>("advancedJson")->setPlainText(QString::fromUtf8(QJsonDocument(root).toJson()));
    button(widget, "applyJson")->click();
}

static int parameterRow(QTableWidget *table, const QString &key)
{
    for (int row = 0; row < table->rowCount(); ++row)
        if (table->item(row, 0)->data(Qt::UserRole).toJsonObject().value("key").toString() == key) return row;
    return -1;
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
    QCOMPARE(maps->columnCount(), 3);
    QCOMPARE(maps->horizontalHeaderItem(2)->text(), QString::fromUtf8(u8"参数说明"));
    QCOMPARE(maps->editTriggers(), QAbstractItemView::NoEditTriggers);
    QVERIFY(source->findData(QString::fromUtf8(u8"命令码")) < 0);
    multiplier->setText("2.5"); save->click();
    QJsonObject expression = draftRoot(widget).value("mappings").toArray().at(0).toObject().value("parameters").toObject().value("positionUm").toObject();
    QCOMPARE(expression.value("kind").toString(), QString("linear"));
    QCOMPARE(expression.value("numerator").toInt(), 5); QCOMPARE(expression.value("denominator").toInt(), 2);
    button(widget, "previewButton")->click(); QVERIFY(preview->toPlainText().contains("5000"));
    multiplier->setText("abc"); save->click();
    button(widget, "previewButton")->click(); QVERIFY(target->toPlainText().isEmpty());
    QCOMPARE(draftRoot(widget).value("mappings").toArray().at(0).toObject().value("parameters").toObject().value("positionUm").toObject(), expression);
    multiplier->setText("1"); source->setCurrentIndex(source->findData("@constant"));
    widget.findChild<QLineEdit *>("mappingValue")->setText("4321"); save->click();
    button(widget, "previewButton")->click(); QVERIFY(preview->toPlainText().contains("4321"));
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
    mapping.insert("responsePolicy", "complete");
    mapping.insert("input", QJsonObject{{"footer", "R\\r"}, {"extension", QJsonObject{{"source", "manual"}}}});
    mappings[0] = mapping; root.insert("mappings", mappings); applyRoot(widget, root);
    QJsonObject cleanedInput = mapping.value("input").toObject(); cleanedInput.remove("footer");
    mapping.insert("input", cleanedInput);
    QCOMPARE(widget.findChild<QLineEdit *>("mappingMultiplier")->text(), QString("1/3"));
    QVERIFY(!widget.findChild<QGroupBox *>("mappingMore"));
    widget.findChild<QLineEdit *>("mappingCode")->setText("B");
    widget.findChild<QPushButton *>("saveMappings")->click();
    QJsonObject saved = draftRoot(widget).value("mappings").toArray().at(0).toObject();
    QCOMPARE(saved.value("parameters").toObject().value("positionUm").toObject(), expression);
    QCOMPARE(saved.value("input"), mapping.value("input"));
    QCOMPARE(saved.value("description"), mapping.value("description"));
    QVERIFY(!saved.contains("responsePolicy"));
    widget.findChild<QLineEdit *>("mappingMultiplier")->setText("2.5"); button(widget, "saveMappings")->click();
    const QJsonObject changed = draftRoot(widget).value("mappings").toArray().at(0).toObject();
    QJsonObject expected = expression; expected.insert("numerator", 5); expected.insert("denominator", 2);
    QCOMPARE(changed.value("parameters").toObject().value("positionUm").toObject(), expected);
    QCOMPARE(changed.value("input"), mapping.value("input"));
    QCOMPARE(changed.value("description"), mapping.value("description"));

    // 无身份的整份 JSON 字段改名不能猜测自动迁移；失效引用由校验提示。
    QJsonObject replaced = draftRoot(widget); QJsonObject external = replaced.value("external").toObject();
    QJsonArray fields = external.value("fields").toArray(); QJsonObject field = fields.at(2).toObject();
    field.insert("name", QString::fromUtf8(u8"新位置")); fields[2] = field; external.insert("fields", fields);
    replaced.insert("external", external); applyRoot(widget, replaced);
    QVERIFY(widget.findChild<QComboBox *>("mappingSource")->currentText().contains(QString::fromUtf8(u8"未找到")));
    widget.findChild<QPushButton *>("saveMappings")->click();
    button(widget, "validateConfig")->click();
    button(widget, "previewButton")->click();
    QVERIFY(widget.findChild<QTextEdit *>("targetOutput")->toPlainText().isEmpty());

    // 未校验草稿也能载入编辑。隐藏的非法高级值应由校验拒绝，不能经普通编辑被强制转换后变合法。
    const QList<QJsonObject> invalidAdvanced = {
        QJsonObject{{"offset", "bad"}, {"round", "floor"}},
        QJsonObject{{"offset", 2.5}, {"round", "floor"}},
        QJsonObject{{"offset", 0}, {"round", 7}}
    };
    for (const QJsonObject &advanced : invalidAdvanced) {
        Widget invalid;
        QJsonObject invalidRoot = draftRoot(invalid); QJsonArray rows = invalidRoot.value("mappings").toArray();
        QJsonObject row = rows.at(0).toObject(); QJsonObject relation = expression;
        relation.insert("offset", advanced.value("offset")); relation.insert("round", advanced.value("round"));
        row.insert("parameters", QJsonObject{{"positionUm", relation}}); rows[0] = row;
        invalidRoot.insert("mappings", rows); applyRoot(invalid, invalidRoot);
        invalid.findChild<QLineEdit *>("mappingCode")->setText("B"); button(invalid, "saveMappings")->click();
        QCOMPARE(draftRoot(invalid).value("mappings").toArray().at(0).toObject().value("parameters").toObject().value("positionUm").toObject(), relation);
        invalid.findChild<QLineEdit *>("mappingMultiplier")->setText("2"); button(invalid, "saveMappings")->click();
        const QJsonObject after = draftRoot(invalid).value("mappings").toArray().at(0).toObject().value("parameters").toObject().value("positionUm").toObject();
        QCOMPARE(after.value("offset"), advanced.value("offset")); QCOMPARE(after.value("round"), advanced.value("round"));
        QCOMPARE(after.value("numerator").toInt(), 2); QCOMPARE(after.value("denominator").toInt(), 1);
        QCOMPARE(after.value("note"), expression.value("note"));
        button(invalid, "validateConfig")->click();
        QVERIFY(invalid.findChild<QLabel *>("validationState")->text().contains(QString::fromUtf8(u8"校验失败")));
        QVERIFY(!invalid.findChild<QCheckBox *>("deviceConfirmed")->isEnabled());
    }
}

void WidgetTest::queryAndRelativeMappingControls()
{
    QDir::setCurrent(QFileInfo(QDir(QString::fromLatin1(QT_TESTCASE_BUILDDIR)).absoluteFilePath(QString::fromLatin1(__FILE__))).dir().absoluteFilePath(".."));
    Widget widget;
    auto *maps = widget.findChild<QTableWidget *>("mappingTable");
    auto *operation = widget.findChild<QComboBox *>("mappingOperation");
    auto *source = widget.findChild<QComboBox *>("mappingSource");
    auto *multiplier = widget.findChild<QLineEdit *>("mappingMultiplier");
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
    QVERIFY(source->isHidden()); QVERIFY(multiplier->isHidden());
    widget.findChild<QLineEdit *>("customerInput")->setText("/1?0R\\r");
    button(widget, "previewButton")->click();
    QVERIFY(widget.findChild<QTextEdit *>("targetOutput")->toPlainText().contains("00 12"));
    maps->selectRow(relativeRow); QVERIFY(!source->isHidden()); QVERIFY(!multiplier->isHidden());
    QCOMPARE(operation->currentData().toString(), QString("RELATIVE_FORWARD"));
    QVERIFY(maps->item(relativeRow, 1)->text().startsWith(QString::fromUtf8(u8"旧版")));
    multiplier->setText("0.1"); save->click();
    QJsonObject expression = draftRoot(widget).value("mappings").toArray().at(relativeRow).toObject().value("parameters").toObject().value("distanceUm").toObject();
    QCOMPARE(expression.value("numerator").toInt(), 1); QCOMPARE(expression.value("denominator").toInt(), 10);
    QVERIFY(!draftRoot(widget).value("mappings").toArray().at(relativeRow).toObject().value("parameters").toObject().contains("positionUm"));
    // 查询模板省略位置参数，但公共地址与协议格式帧尾仍统一读取。
    operation->setCurrentIndex(operation->findData("QUERY_MOTOR"));
    QVERIFY(source->isHidden()); QVERIFY(multiplier->isHidden());
    save->click();
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
    reply.data.insert("deviceBusy", true);
    service->replyReceived(reply);
    const QString shownReply = widget.findChild<QTextEdit *>("replyResult")->toPlainText();
    QVERIFY(shownReply.contains(QString::fromUtf8(u8"运行中")));
    QVERIFY(shownReply.contains(QString::fromUtf8(u8"未回零或未配置")));
    QVERIFY(shownReply.contains(QString::fromUtf8(u8"控制板类型")));
    QVERIFY(!shownReply.contains("16777215"));
    QVERIFY(shownReply.contains(QString::fromUtf8(u8"运动中或状态未知")));
    UnifiedResult completed; completed.success = true; completed.requestId = 1; completed.data.insert("positionUm", 100);
    service->completed(completed); QCOMPARE(widget.findChild<QTextEdit *>("replyResult")->toPlainText(), shownReply);
    UnifiedResult rejected; rejected.requestId = 1; rejected.success = false;
    rejected.error.code = "DEVICE_REJECTED"; rejected.error.message = QString::fromUtf8(u8"设备拒绝命令");
    service->replyReceived(rejected);
    QVERIFY(widget.findChild<QTextEdit *>("replyResult")->toPlainText().contains(QString::fromUtf8(u8"设备拒绝命令")));
}

void WidgetTest::completeCatalogAndSearchPreserveSelection()
{
    Widget widget;
    auto *operation = widget.findChild<QComboBox *>("mappingOperation"); auto *search = widget.findChild<QLineEdit *>("mappingSearch");
    QVERIFY(operation && search);
    QCOMPARE(operation->count(), 59); QCOMPARE(QushengCommandCatalog::commands().size(), 56);
    auto *model = qobject_cast<QStandardItemModel *>(operation->model()); QVERIFY(model);
    int receiveOnly = 0;
    for (int i = 0; i < operation->count(); ++i) {
        QVERIFY(operation->itemText(i).contains("0x"));
        if (QushengCommandCatalog::isReceiveOnly(operation->itemData(i).toString())) { ++receiveOnly; QVERIFY(!model->item(i)->isEnabled()); }
    }
    QCOMPARE(receiveOnly, 4);
    for (const QString &preset : {"QUSHENG_11", "ABSOLUTE_MOVE", "RELATIVE_FORWARD", "RELATIVE_BACKWARD"}) QCOMPARE(operation->findData(preset), -1);
    int motorActions = 0;
    for (int i = 0; i < operation->count(); ++i) if (operation->itemText(i).startsWith("0x11")) ++motorActions;
    QCOMPARE(motorActions, 4);
    for (const QString &preset : {"MOTOR_FORWARD", "MOTOR_STOP", "MOTOR_BACKWARD", "MOTOR_RESET"}) QVERIFY(operation->findData(preset) >= 0);
    const QString current = operation->currentData().toString(); const QJsonObject saved = draftRoot(widget);
    widget.findChild<QLineEdit *>("mappingMultiplier")->setText("2.5");
    search->setText(QString::fromUtf8(u8"不存在的指令"));
    QCOMPARE(operation->currentData().toString(), current);
    for (int i = 0; i < operation->count(); ++i) QVERIFY(qobject_cast<QListView *>(operation->view())->isRowHidden(i));
    QCOMPARE(widget.findChild<QLineEdit *>("mappingMultiplier")->text(), QString("2.5")); QCOMPARE(draftRoot(widget), saved);
    search->setText("0x81"); auto *view = qobject_cast<QListView *>(operation->view()); QVERIFY(view);
    QVERIFY(!view->isRowHidden(operation->findData("QUSHENG_81"))); QCOMPARE(operation->currentData().toString(), current);
    search->clear(); widget.findChild<QPushButton *>("saveMappings")->click();
    const QJsonObject expression = draftRoot(widget).value("mappings").toArray().at(0).toObject().value("parameters").toObject().value("positionUm").toObject();
    QCOMPARE(expression.value("numerator").toInt(), 5); QCOMPARE(expression.value("denominator").toInt(), 2);
}

void WidgetTest::catalogTextIsReadableAndSourceLiteralsAreExplicit()
{
    Widget widget;
    auto *operation = widget.findChild<QComboBox *>("mappingOperation"); QVERIFY(operation);
    // 独立核对截图里的项，不能仅从同一张目录自动生成期望而遗漏格式乱码。
    QCOMPARE(operation->itemText(operation->findData("QUSHENG_22")), QString::fromUtf8(u8"0x22 | 电磁阀单路控制"));
    QCOMPARE(operation->itemText(operation->findData("QUSHENG_17")), QString::fromUtf8(u8"0x17 | 电机运动完成上报 (仅接收)"));
    QCOMPARE(operation->itemText(operation->findData("MOTOR_FORWARD")), QString::fromUtf8(u8"0x11 | 步进电机前进"));
    QCOMPARE(operation->itemText(operation->findData("MOTOR_STOP")), QString::fromUtf8(u8"0x11 | 步进电机停止"));
    QCOMPARE(operation->itemText(operation->findData("MOTOR_BACKWARD")), QString::fromUtf8(u8"0x11 | 步进电机后退"));
    QCOMPARE(operation->itemText(operation->findData("MOTOR_RESET")), QString::fromUtf8(u8"0x11 | 步进电机复位"));
    QCOMPARE(operation->itemText(operation->findData("QUERY_MOTOR")), QString::fromUtf8(u8"0x12 | 电机信息查询"));
    QCOMPARE(operation->itemText(operation->findData("QUERY_DEVICE_INFO")), QString::fromUtf8(u8"0x04 | 设备信息查询"));
    QCOMPARE(operation->itemData(operation->findData("QUSHENG_22"), Qt::ToolTipRole).toString(), QString::fromUtf8(u8"电磁阀单路控制"));
    QCOMPARE(operation->count(), 59);
    for (int i = 0; i < operation->count(); ++i) {
        const QString op = operation->itemData(i).toString();
        const QJsonObject entry = QushengCommandCatalog::find(op);
        const QString status = QushengCommandCatalog::isReceiveOnly(op) ? QString::fromUtf8(u8"仅接收")
            : QushengCommandCatalog::isExecutable(op) ? QString() : QString::fromUtf8(u8"待实现");
        const QString expected = entry.value("code").toString() + " | " + entry.value("name").toString() + (status.isEmpty() ? QString() : " (" + status + ")");
        QCOMPARE(operation->itemText(i), expected);
        QVERIFY(!expected.contains(QChar(0xfffd)));
        QVERIFY(!expected.contains(QString::fromUtf8(u8"锛")));
        QVERIFY(!expected.contains("%1") && !expected.contains("%2") && !expected.contains("%3"));
        QVERIFY(!operation->itemData(i, Qt::ToolTipRole).toString().isEmpty());
    }

    // 静态防线覆盖未触发的错误提示等路径；QStringLiteral 只留 ASCII，中文使用 fromUtf8(u8)。
    // 这与运行时比较互补，避免仅在默认 UTF-8 编译器上通过，用户本地 kit 却再次解错标点。
    const QDir sourceRoot(QFileInfo(QDir(QString::fromLatin1(QT_TESTCASE_BUILDDIR)).absoluteFilePath(QString::fromLatin1(__FILE__))).dir().absoluteFilePath(".."));
    QStringList files = {sourceRoot.filePath("widget.cpp"), sourceRoot.filePath("main.cpp")};
    QDirIterator it(sourceRoot.filePath("src"), {"*.cpp", "*.h"}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) files << it.next();
    const QRegularExpression literal(QStringLiteral("QStringLiteral\\s*\\(\\s*\"((?:[^\"\\\\]|\\\\.)*)\""));
    for (const QString &path : files) {
        QFile source(path); QVERIFY2(source.open(QIODevice::ReadOnly), qPrintable(path));
        auto matches = literal.globalMatch(QString::fromUtf8(source.readAll()));
        while (matches.hasNext()) {
            const QString value = matches.next().captured(1);
            for (const QChar ch : value) QVERIFY2(ch.unicode() < 128, qPrintable(path + ": " + value));
        }
    }
}

void WidgetTest::pendingMappingEditorAndFieldRefresh()
{
    Widget widget; auto *operation = widget.findChild<QComboBox *>("mappingOperation");
    operation->setCurrentIndex(operation->findData("QUSHENG_13"));
    auto *parameters = widget.findChild<QTableWidget *>("mappingParameters"); QVERIFY(parameters);
    QVERIFY(parameters->rowCount() > 2); QVERIFY(!parameters->isHidden());
    QVERIFY(widget.findChild<QComboBox *>("mappingSource")->isHidden());
    QVERIFY(operation->currentText().contains(QString::fromUtf8(u8"待实现")));
    auto *devSource = qobject_cast<QComboBox *>(parameters->cellWidget(0, 1)); auto *devValue = qobject_cast<QLineEdit *>(parameters->cellWidget(0, 2));
    auto *percentSource = qobject_cast<QComboBox *>(parameters->cellWidget(parameterRow(parameters, "microsteps"), 1));
    devSource->setCurrentIndex(devSource->findData("@constant")); devValue->setText("17");
    percentSource->setCurrentIndex(percentSource->findData(QString::fromUtf8(u8"位置")));
    // 字段改名保存选中编辑，再同步来源；常量与其他未保存参数仍保持。
    auto *fields = widget.findChild<QTableWidget *>("fieldTable"); fields->item(2, 1)->setText(QString::fromUtf8(u8"新位置"));
    devSource = qobject_cast<QComboBox *>(parameters->cellWidget(0, 1)); devValue = qobject_cast<QLineEdit *>(parameters->cellWidget(0, 2));
    percentSource = qobject_cast<QComboBox *>(parameters->cellWidget(parameterRow(parameters, "microsteps"), 1));
    QCOMPARE(devValue->text(), QString("17")); QCOMPARE(devSource->currentData().toString(), QString("@constant"));
    QCOMPARE(percentSource->currentData().toString(), QString::fromUtf8(u8"新位置"));
    QVERIFY(percentSource->findData(QString::fromUtf8(u8"新位置")) >= 0);
    fields->item(2, 1)->setText(QString::fromUtf8(u8"位置"));
    widget.findChild<QPushButton *>("saveMappings")->click();
    QJsonObject mapping = draftRoot(widget).value("mappings").toArray().at(0).toObject();
    QCOMPARE(mapping.value("operation").toString(), QString("QUSHENG_13"));
    QCOMPARE(mapping.value("parameters").toObject().value("devId").toObject().value("value").toInt(), 17);
    QCOMPARE(mapping.value("parameters").toObject().value("microsteps").toObject().value("field").toString(), QString::fromUtf8(u8"位置"));
    QVERIFY(!mapping.value("parameters").toObject().contains("positionUm"));
    button(widget, "validateConfig")->click(); button(widget, "enableConfig")->click();
    button(widget, "previewButton")->click();
    QVERIFY(widget.findChild<QTextEdit *>("targetOutput")->toPlainText().isEmpty());
    QVERIFY(widget.findChild<QTextEdit *>("previewOutput")->toPlainText().contains(QString::fromUtf8(u8"待实现")));
    QVERIFY(widget.findChild<QLabel *>("liveReason")->text().contains(QString::fromUtf8(u8"发送待实现")));
    const QJsonObject saved = draftRoot(widget); applyRoot(widget, saved);
    QCOMPARE(draftRoot(widget), saved); QCOMPARE(operation->currentData().toString(), QString("QUSHENG_13"));
    QCOMPARE(qobject_cast<QLineEdit *>(parameters->cellWidget(0, 2))->text(), QString("17"));
}

void WidgetTest::pendingAdvancedRelationAndTargetSwitch()
{
    Widget widget; QJsonObject root = draftRoot(widget); QJsonArray mappings = root.value("mappings").toArray();
    QJsonObject mapping = mappings.at(0).toObject();
    const QJsonObject expression{{"kind", "linear"}, {"field", QString::fromUtf8(u8"位置")}, {"numerator", 1}, {"denominator", 3}, {"offset", 4}, {"round", "floor"}, {"note", "keep"}};
    mapping.insert("operation", "QUSHENG_13"); mapping.insert("parameters", QJsonObject{{"microsteps", expression}}); mappings[0] = mapping; root.insert("mappings", mappings); applyRoot(widget, root);
    widget.findChild<QLineEdit *>("mappingCode")->setText("B"); widget.findChild<QPushButton *>("saveMappings")->click();
    QCOMPARE(draftRoot(widget).value("mappings").toArray().at(0).toObject().value("parameters").toObject().value("microsteps").toObject(), expression);
    auto *operation = widget.findChild<QComboBox *>("mappingOperation");
    operation->setCurrentIndex(operation->findData("QUSHENG_15")); widget.findChild<QPushButton *>("saveMappings")->click();
    QVERIFY(draftRoot(widget).value("mappings").toArray().at(0).toObject().value("parameters").toObject().isEmpty());
    operation->setCurrentIndex(operation->findData("MOTOR_FORWARD"));
    QCOMPARE(widget.findChild<QComboBox *>("mappingSource")->currentData().toString(), QString());
    widget.findChild<QPushButton *>("saveMappings")->click();
    // 新运动目标缺少来源时保存失败，旧草稿不会被悄悄改为可执行。
    QCOMPARE(draftRoot(widget).value("mappings").toArray().at(0).toObject().value("operation").toString(), QString("QUSHENG_15"));
    operation->setCurrentIndex(operation->findData("QUSHENG_13"));
    QCOMPARE(operation->currentData().toString(), QString("MOTOR_FORWARD"));
}

void WidgetTest::simplifiedExpertInterface()
{
    Widget widget; widget.show(); QCoreApplication::processEvents();
    auto *tabs = widget.findChild<QTabWidget *>("mainTabs"); QVERIFY(tabs); QCOMPARE(tabs->count(), 5);
    for (const char *name : {"mappingMore", "mappingOffset", "mappingRounding", "mappingDefaultFooter",
                            "mappingProtocolMore", "mappingCommandInfo", "mappingParameterHint", "mappingSearchResult",
                            "mappingCategory", "browseCatalog", "commandCatalogDialog", "mappingResponse", "mappingFooter"}) QVERIFY(!widget.findChild<QWidget *>(name));
    for (QLabel *label : widget.findChildren<QLabel *>()) {
        QVERIFY(!label->text().contains(QString::fromUtf8(u8"编辑客户协议格式 →")));
        QVERIFY(!label->text().contains(QString::fromUtf8(u8"配置定义的单条运动")));
        QVERIFY(!label->text().contains(QString::fromUtf8(u8"运行日志（最多")));
    }
    for (QPushButton *b : widget.findChildren<QPushButton *>()) QVERIFY(!b->text().contains(QString::fromUtf8(u8"离线解析")));
    auto *log = widget.findChild<QTextEdit *>("runLog"); auto *outputs = widget.findChild<QTabWidget *>("deviceOutputs");
    QVERIFY(log && outputs); QCOMPARE(outputs->tabText(outputs->indexOf(log)), QString::fromUtf8(u8"日志"));
    QVERIFY(!log->isVisible()); QVERIFY(!widget.findChild<QLabel *>("liveReason")->isVisible());
    QCOMPARE(button(widget, "previewButton")->text(), QString::fromUtf8(u8"预览"));
    QCOMPARE(button(widget, "sendButton")->text(), QString::fromUtf8(u8"发送"));
    QCOMPARE(widget.findChild<QCheckBox *>("deviceConfirmed")->text(), QString::fromUtf8(u8"参数已确认"));
    tabs->setCurrentIndex(1); QCoreApplication::processEvents();
    auto *scroll = qobject_cast<QScrollArea *>(tabs->widget(1)); QVERIFY(scroll);
    QVERIFY(scroll->viewport()->rect().contains(widget.findChild<QLineEdit *>("mappingMultiplier")->mapTo(scroll->viewport(), QPoint(0, 0))));
}

void WidgetTest::unifiedFormatAndVisibleErrors()
{
    Widget widget; QVERIFY(!widget.findChild<QWidget *>("mappingResponse")); QVERIFY(!widget.findChild<QWidget *>("mappingFooter"));
    auto *maps = widget.findChild<QTableWidget *>("mappingTable"); QCOMPARE(maps->columnCount(), 3);
    // 所有旧行在载入时一起迁移，不能只处理当前选中行；仅删除退役项，不删省略字段和扩展。
    QJsonObject root = draftRoot(widget); QJsonArray rows = root.value("mappings").toArray();
    for (int i = 0; i < rows.size(); ++i) {
        QJsonObject row = rows.at(i).toObject(); row.insert("responsePolicy", i % 2 ? "complete" : "ack");
        QJsonObject input = row.value("input").toObject(); input.insert("footer", "!\\r");
        input.insert("note", "preserved"); row.insert("input", input); rows[i] = row;
    }
    root.insert("mappings", rows); applyRoot(widget, root);
    for (int i = 0; i < rows.size(); ++i) {
        QJsonObject expected = rows.at(i).toObject(); expected.remove("responsePolicy");
        QJsonObject input = expected.value("input").toObject(); input.remove("footer"); expected.insert("input", input);
        QCOMPARE(draftRoot(widget).value("mappings").toArray().at(i).toObject(), expected);
    }
    const QJsonObject migrated = draftRoot(widget); applyRoot(widget, migrated); QCOMPARE(draftRoot(widget), migrated);
    auto *input = widget.findChild<QLineEdit *>("customerInput"); auto *target = widget.findChild<QTextEdit *>("targetOutput");
    for (const QString &text : {QStringLiteral("/1A2000R\\r"), QStringLiteral("/1?0R\\r")}) {
        input->setText(text); button(widget, "previewButton")->click(); QVERIFY(target->toPlainText().contains("AA AA"));
        QVERIFY(!widget.findChild<QTextEdit *>("previewOutput")->toPlainText().contains(QString::fromUtf8(u8"响应方式")));
    }
    input->setText("/1?0\\r"); button(widget, "previewButton")->click(); QVERIFY(target->toPlainText().isEmpty());
    auto *footer = widget.findChild<QLineEdit *>("formatFooter"); QVERIFY(footer); footer->setText("!\\r");
    button(widget, "saveFields")->click();
    for (const QString &text : {QStringLiteral("/1A2000!\\r"), QStringLiteral("/1?0!\\r")}) {
        input->setText(text); button(widget, "previewButton")->click(); QVERIFY(target->toPlainText().contains("AA AA"));
    }
    input->setText("/1?0R\\r"); button(widget, "previewButton")->click(); QVERIFY(target->toPlainText().isEmpty());
    button(widget, "addMapping")->click();
    widget.findChild<QComboBox *>("mappingOperation")->setCurrentIndex(widget.findChild<QComboBox *>("mappingOperation")->findData("MOTOR_STOP"));
    widget.findChild<QLineEdit *>("mappingCode")->setText("S"); button(widget, "saveMappings")->click();
    const QJsonObject added = draftRoot(widget).value("mappings").toArray().last().toObject();
    QCOMPARE(added.value("commandCode").toString(), QString("S")); QVERIFY(!added.contains("responsePolicy"));
    QVERIFY(!added.value("input").toObject().contains("footer"));
    widget.findChild<QTextEdit *>("advancedJson")->setPlainText("invalid"); button(widget, "applyJson")->click();
    QVERIFY(widget.findChild<QLabel *>("validationState")->text().contains(QString::fromUtf8(u8"JSON 无效")));
    auto *transport = widget.findChild<SerialTransport *>(); QVERIFY(transport);
    transport->connectionChanged(false, QString::fromUtf8(u8"找不到串口设备；系统详情：测试详情"));
    QCOMPARE(widget.findChild<QLabel *>("validationState")->text(), QString::fromUtf8(u8"找不到串口设备"));
    // 迁移不修复未获授权的格式错误：坏类型仍保留，让配置校验明确拒绝。
    Widget invalid; QJsonObject invalidRoot = draftRoot(invalid);
    invalidRoot.insert("mappings", "invalid-array"); applyRoot(invalid, invalidRoot);
    QCOMPARE(draftRoot(invalid).value("mappings"), QJsonValue(QStringLiteral("invalid-array")));
    applyRoot(invalid, migrated);
    invalidRoot = draftRoot(invalid); QJsonArray invalidRows = invalidRoot.value("mappings").toArray();
    QJsonObject invalidMapping = invalidRows.at(0).toObject(); const QJsonArray invalidInput{"bad-type"};
    invalidMapping.insert("input", invalidInput); invalidRows[0] = invalidMapping;
    invalidRoot.insert("mappings", invalidRows); applyRoot(invalid, invalidRoot);
    QCOMPARE(draftRoot(invalid).value("mappings").toArray().at(0).toObject().value("input"), QJsonValue(invalidInput));
    invalid.findChild<QLineEdit *>("mappingCode")->setText("B"); button(invalid, "saveMappings")->click();
    QCOMPARE(draftRoot(invalid).value("mappings").toArray().at(0).toObject().value("input"), QJsonValue(invalidInput));
    button(invalid, "validateConfig")->click();
    QVERIFY(invalid.findChild<QLabel *>("validationState")->text().contains(QString::fromUtf8(u8"校验失败")));
}

void WidgetTest::genericScalarRanges()
{
    Widget widget; auto *operation = widget.findChild<QComboBox *>("mappingOperation"); auto *parameters = widget.findChild<QTableWidget *>("mappingParameters");
    operation->setCurrentIndex(operation->findData("QUSHENG_A1"));
    auto *source = qobject_cast<QComboBox *>(parameters->cellWidget(1, 1)); auto *value = qobject_cast<QLineEdit *>(parameters->cellWidget(1, 2));
    source->setCurrentIndex(source->findData("@constant")); value->setText("-20"); widget.findChild<QPushButton *>("saveMappings")->click();
    QCOMPARE(draftRoot(widget).value("mappings").toArray().at(0).toObject().value("parameters").toObject().value("pwm").toObject().value("value").toInt(), -20);
    value = qobject_cast<QLineEdit *>(parameters->cellWidget(1, 2));
    value->setText("128"); widget.findChild<QPushButton *>("saveMappings")->click();
    QCOMPARE(draftRoot(widget).value("mappings").toArray().at(0).toObject().value("parameters").toObject().value("pwm").toObject().value("value").toInt(), -20);
    value->setText("-20"); operation->setCurrentIndex(operation->findData("QUSHENG_63"));
    source = qobject_cast<QComboBox *>(parameters->cellWidget(0, 1)); value = qobject_cast<QLineEdit *>(parameters->cellWidget(0, 2));
    source->setCurrentIndex(source->findData("@constant")); value->setText("4294967295"); widget.findChild<QPushButton *>("saveMappings")->click();
    QCOMPARE(draftRoot(widget).value("mappings").toArray().at(0).toObject().value("parameters").toObject().value("peripheralDelay").toObject().value("value").toDouble(), 4294967295.0);
    operation->setCurrentIndex(operation->findData("MOTOR_STOP")); QCOMPARE(parameters->rowCount(), 0);
    widget.findChild<QPushButton *>("saveMappings")->click();
    QCOMPARE(draftRoot(widget).value("mappings").toArray().at(0).toObject().value("operation").toString(), QString("MOTOR_STOP"));
    QVERIFY(draftRoot(widget).value("mappings").toArray().at(0).toObject().value("parameters").toObject().isEmpty());
}

void WidgetTest::stopResetMappingsHaveNoDistanceControls()
{
    Widget widget;
    for (const QString &operation : {QString("MOTOR_STOP"), QString("MOTOR_RESET")}) {
        QJsonObject root = draftRoot(widget); QJsonArray mappings = root.value("mappings").toArray();
        QJsonObject mapping = mappings.first().toObject();
        mapping.insert("operation", operation); mapping.insert("commandCode", "Z");
        // 旧草稿曾保存距离关系，载入后应清理，不要求客户字段继续存在。
        mapping.insert("parameters", QJsonObject{{"positionUm", QJsonObject{{"kind", "field"}, {"field", "old-deleted-field"}}}});
        mapping.insert("input", QJsonObject{{"omitFields", QJsonArray{QString::fromUtf8(u8"位置")}}});
        mappings[0] = mapping; root.insert("mappings", mappings);
        root.insert("sdkDefaults", QJsonObject{{operation, QJsonObject{{"devId", 18}, {"speedUmPerS", 2000}}}});
        QJsonObject external = root.value("external").toObject(); external.insert("header", "!"); external.insert("footer", "~"); root.insert("external", external);
        applyRoot(widget, root);
        auto *selected = widget.findChild<QComboBox *>("mappingOperation");
        QCOMPARE(selected->currentData().toString(), operation);
        QVERIFY(!selected->currentText().contains(QString::fromUtf8(u8"待实现")));
        QCOMPARE(widget.findChild<QTableWidget *>("mappingParameters")->rowCount(), 0);
        QVERIFY(widget.findChild<QLineEdit *>("mappingMultiplier")->isHidden());
        QVERIFY(widget.findChild<QComboBox *>("mappingSource")->isHidden());
        QVERIFY(draftRoot(widget).value("mappings").toArray().first().toObject().value("parameters").toObject().isEmpty());
        widget.findChild<QLineEdit *>("customerInput")->setText("!1Z~");
        button(widget, "previewButton")->click();
        const QString target = widget.findChild<QTextEdit *>("targetOutput")->toPlainText();
        QVERIFY(target.contains(operation == "MOTOR_STOP" ? "12 42 00 07 D0 00 00 00" : "12 44 00 07 D0 00 00 00"));
        button(widget, "validateConfig")->click();
        QVERIFY(widget.findChild<QLabel *>("validationState")->text().contains(QString::fromUtf8(u8"通过")));
        QVERIFY(!widget.findChild<QLabel *>("liveReason")->text().contains(QString::fromUtf8(u8"待实现")));
    }
}

void WidgetTest::sdkGenericMappingsAndDefaults()
{
    const QStringList operations = {"QUSHENG_81", "QUSHENG_22", "QUSHENG_54", "QUSHENG_A1"};
    const QStringList keys = {"percent", "duration", "voltageMv", "pwm"};
    const QStringList inputs = {"50", "2000", "2000", "-20"};
    const QStringList frames = {"00 81 E0 32", "00 22 31 00 00 07 D0", "00 54 71 00 07 D0", "00 A1 21 EC"};
    for (int i = 0; i < operations.size(); ++i) {
        Widget widget; auto *operation = widget.findChild<QComboBox *>("mappingOperation");
        operation->setCurrentIndex(operation->findData(operations.at(i)));
        auto *parameters = widget.findChild<QTableWidget *>("mappingParameters");
        QVERIFY(!parameters->isHidden()); QVERIFY(widget.findChild<QComboBox *>("mappingSource")->isHidden());
        const int dataRow = parameterRow(parameters, keys.at(i)), deviceRow = parameterRow(parameters, "devId");
        QVERIFY(dataRow >= 0 && deviceRow >= 0);
        auto *source = qobject_cast<QComboBox *>(parameters->cellWidget(dataRow, 1));
        source->setCurrentIndex(source->findData(QString::fromUtf8(u8"位置")));
        QVERIFY(qobject_cast<QComboBox *>(parameters->cellWidget(deviceRow, 1))->itemText(0).contains(QString::fromUtf8(u8"默认")));
        if (i == 3) {
            auto *fields = widget.findChild<QTableWidget *>("fieldTable");
            auto *type = qobject_cast<QComboBox *>(fields->cellWidget(2, 3)); type->setCurrentIndex(type->findData("int"));
            fields->item(2, 7)->setText("-128"); fields->item(2, 8)->setText("127");
        }
        button(widget, "saveMappings")->click();
        const QJsonObject mapping = draftRoot(widget).value("mappings").toArray().at(0).toObject();
        QVERIFY(!mapping.value("parameters").toObject().contains("positionUm"));
        QVERIFY(!mapping.value("parameters").toObject().contains("devId")); // 默认值仍由 SDK 补齐。
        widget.findChild<QLineEdit *>("customerInput")->setText("/1A" + inputs.at(i) + "R\\r");
        button(widget, "previewButton")->click();
        QVERIFY2(widget.findChild<QTextEdit *>("targetOutput")->toPlainText().contains(frames.at(i)),
                 qPrintable(widget.findChild<QTextEdit *>("previewOutput")->toPlainText()));
        QVERIFY(widget.findChild<QTextEdit *>("previewOutput")->toPlainText().contains(QString::fromUtf8(u8"软件初始值")));
    }
    Widget widget; QJsonObject root = draftRoot(widget);
    root.insert("sdkDefaults", QJsonObject{{"QUSHENG_81", QJsonObject{{"devId", 226}, {"percent", 20}}}}); applyRoot(widget, root);
    auto *operation = widget.findChild<QComboBox *>("mappingOperation"); operation->setCurrentIndex(operation->findData("QUSHENG_81"));
    auto *parameters = widget.findChild<QTableWidget *>("mappingParameters"); int percentRow = parameterRow(parameters, "percent"), devRow = parameterRow(parameters, "devId");
    QCOMPARE(qobject_cast<QComboBox *>(parameters->cellWidget(percentRow, 1))->itemText(0), QString::fromUtf8(u8"默认值（20）"));
    QCOMPARE(qobject_cast<QComboBox *>(parameters->cellWidget(devRow, 1))->itemText(0), QString::fromUtf8(u8"默认值（226）"));
    auto *percent = qobject_cast<QComboBox *>(parameters->cellWidget(percentRow, 1)); percent->setCurrentIndex(percent->findData(QString::fromUtf8(u8"位置")));
    widget.findChild<QLineEdit *>("customerInput")->setText("/1A50R\\r"); button(widget, "saveMappings")->click(); button(widget, "previewButton")->click();
    QVERIFY(widget.findChild<QTextEdit *>("targetOutput")->toPlainText().contains("00 81 E2 32"));
    auto *fields = widget.findChild<QTableWidget *>("fieldTable"); fields->item(2, 1)->setText(QString::fromUtf8(u8"数据参数"));
    percent = qobject_cast<QComboBox *>(parameters->cellWidget(percentRow, 1)); QCOMPARE(percent->currentData().toString(), QString::fromUtf8(u8"数据参数"));
    auto *dev = qobject_cast<QComboBox *>(parameters->cellWidget(devRow, 1)); dev->setCurrentIndex(dev->findData("@constant"));
    qobject_cast<QLineEdit *>(parameters->cellWidget(devRow, 2))->setText("225");
    percent->setCurrentIndex(percent->findData("@constant")); qobject_cast<QLineEdit *>(parameters->cellWidget(percentRow, 2))->setText("75");
    button(widget, "saveMappings")->click(); button(widget, "previewButton")->click();
    QVERIFY(widget.findChild<QTextEdit *>("targetOutput")->toPlainText().contains("00 81 E1 4B"));
    const QJsonObject saved = draftRoot(widget); applyRoot(widget, saved); QCOMPARE(draftRoot(widget), saved);
    operation->setCurrentIndex(operation->findData("QUSHENG_22")); button(widget, "saveMappings")->click();
    QVERIFY(draftRoot(widget).value("mappings").toArray().at(0).toObject().value("parameters").toObject().isEmpty());
    const QString renderDir = qEnvironmentVariable("TRANSLATION_RENDER_DIR");
    if (!renderDir.isEmpty()) {
        widget.show(); widget.findChild<QTabWidget *>("mainTabs")->setCurrentIndex(1); QCoreApplication::processEvents();
        QVERIFY(QDir().mkpath(renderDir));
        QVERIFY(widget.grab().save(QDir(renderDir).filePath("sdk-default-parameters.png")));
    }
}

void WidgetTest::sdkQueriesAndRequiredParameters()
{
    const QStringList queries = {"QUSHENG_03", "QUSHENG_44", "QUSHENG_83", "QUERY_MOTOR", "QUERY_DEVICE_INFO"};
    const QStringList frames = {"00 03", "00 44 61", "00 83 E0", "00 12 11", "00 04 00 00"};
    for (int i = 0; i < queries.size(); ++i) {
        Widget widget; QJsonObject root = draftRoot(widget); QJsonObject external = root.value("external").toObject();
        QJsonArray fields = external.value("fields").toArray(); fields.removeAt(2); external.insert("fields", fields); root.insert("external", external);
        QJsonObject mapping = root.value("mappings").toArray().at(0).toObject(); mapping.insert("operation", queries.at(i));
        mapping.insert("parameters", QJsonObject()); mapping.remove("input"); root.insert("mappings", QJsonArray{mapping}); applyRoot(widget, root);
        QVERIFY(QushengCommandCatalog::find(queries.at(i)).value("query").toBool());
        auto *parameters = widget.findChild<QTableWidget *>("mappingParameters");
        if (queries.at(i) == "QUERY_MOTOR") {
            const int row = parameterRow(parameters, "devId"); QVERIFY(row >= 0);
            QVERIFY(qobject_cast<QComboBox *>(parameters->cellWidget(row, 1))->itemText(0).contains(QString::fromUtf8(u8"设备默认")));
        }
        button(widget, "saveMappings")->click(); widget.findChild<QLineEdit *>("customerInput")->setText("/1AR\\r");
        button(widget, "previewButton")->click();
        QVERIFY2(widget.findChild<QTextEdit *>("targetOutput")->toPlainText().contains(frames.at(i)), qPrintable(widget.findChild<QTextEdit *>("previewOutput")->toPlainText()));
        QVERIFY(draftRoot(widget).value("mappings").toArray().at(0).toObject().value("parameters").toObject().isEmpty());
    }
    Widget widget; auto *operation = widget.findChild<QComboBox *>("mappingOperation"); operation->setCurrentIndex(operation->findData("QUSHENG_01"));
    auto *parameters = widget.findChild<QTableWidget *>("mappingParameters"); const int row = parameterRow(parameters, "enableMask"); QVERIFY(row >= 0);
    auto *source = qobject_cast<QComboBox *>(parameters->cellWidget(row, 1)); QCOMPARE(source->itemText(0), QString::fromUtf8(u8"需设置"));
    button(widget, "saveMappings")->click(); button(widget, "previewButton")->click();
    QVERIFY(widget.findChild<QTextEdit *>("targetOutput")->toPlainText().isEmpty());
    source = qobject_cast<QComboBox *>(parameters->cellWidget(row, 1)); source->setCurrentIndex(source->findData("@constant"));
    qobject_cast<QLineEdit *>(parameters->cellWidget(row, 2))->setText("65534"); button(widget, "saveMappings")->click(); button(widget, "previewButton")->click();
    QVERIFY(widget.findChild<QTextEdit *>("targetOutput")->toPlainText().contains("00 01 FF FE"));
}


void WidgetTest::sdkReportsAreVisibleWithoutCompletingRequests()
{
    Widget widget; auto *service = widget.findChild<TranslationService *>(); QVERIFY(service);
    auto *result = widget.findChild<QTextEdit *>("replyResult"); auto *state = widget.findChild<QLabel *>("requestState"); QVERIFY(state);
    service->requestStateChanged(9, RequestState::WaitingReply, QString::fromUtf8(u8"等待匹配回告"));
    const QString before = state->text(); QSignalSpy completed(service, &TranslationService::completed);
    const auto report = [](int code, const QByteArray &payload, const QByteArray &wire = QByteArray()) {
        ProtocolMessage message; message.messageType = code; message.payload = payload; message.wireData = wire; return message;
    };
    const QList<ProtocolMessage> reports = {
        report(0x08, QByteArray(), QByteArray::fromHex("AAAA020100FF0008")),
        report(0x09, QByteArray::fromHex("11000123")), report(0x17, QByteArray::fromHex("11100007D00007D1")),
        report(0x45, QByteArray::fromHex("61000064")), report(0xD9, QByteArray::fromHex("DEADBEEF"))
    };
    const QStringList labels = {QString::fromUtf8(u8"心跳 = 已收到"), QString::fromUtf8(u8"故障代码"),
                               QString::fromUtf8(u8"2000"), QString::fromUtf8(u8"电压"), QString::fromUtf8(u8"未解析")};
    for (int i = 0; i < reports.size(); ++i) {
        result->clear(); service->unsolicitedMessage(reports.at(i));
        QVERIFY2(result->toPlainText().contains(labels.at(i)), qPrintable(result->toPlainText()));
        QCOMPARE(state->text(), before); QCOMPARE(completed.count(), 0);
        QVERIFY(!result->toPlainText().contains(QString::fromUtf8(u8"设备回告成功")));
    }
    QVERIFY(result->toPlainText().contains("DE AD BE EF"));
}

void WidgetTest::fixedMotorActionsAndLegacyCompatibility()
{
    Widget widget; auto *operation = widget.findChild<QComboBox *>("mappingOperation");
    auto *source = widget.findChild<QComboBox *>("mappingSource"); auto *parameters = widget.findChild<QTableWidget *>("mappingParameters");
    auto *save = widget.findChild<QPushButton *>("saveMappings");
    auto *maps = widget.findChild<QTableWidget *>("mappingTable"); auto *speed = widget.findChild<QSpinBox *>("defaultMotorSpeed");
    auto *legacyAction = widget.findChild<QSpinBox *>("legacyAbsoluteAction"); QVERIFY(speed && legacyAction);
    QVERIFY(legacyAction->isHidden());
    QCOMPARE(operation->currentData().toString(), QString("MOTOR_FORWARD"));
    QVERIFY(parameters->isHidden());
    operation->setCurrentIndex(operation->findData("MOTOR_BACKWARD")); widget.findChild<QLineEdit *>("mappingMultiplier")->setText("0.1"); save->click();
    QJsonObject mapping = draftRoot(widget).value("mappings").toArray().at(0).toObject();
    QCOMPARE(mapping.value("operation").toString(), QString("MOTOR_BACKWARD"));
    const QJsonObject relation = mapping.value("parameters").toObject().value("positionUm").toObject();
    QCOMPARE(relation.value("numerator").toInt(), 1); QCOMPARE(relation.value("denominator").toInt(), 10);
    QCOMPARE(mapping.value("parameters").toObject().size(), 1);
    const QString renderDir = qEnvironmentVariable("TRANSLATION_RENDER_DIR");
    if (!renderDir.isEmpty()) QVERIFY(QDir().mkpath(renderDir));
    widget.show(); widget.findChild<QTabWidget *>()->setCurrentIndex(1); QCoreApplication::processEvents();
    if (!renderDir.isEmpty()) QVERIFY(widget.grab().save(QDir(renderDir).filePath("motor-backward.png")));
    for (const QString &pending : {QStringLiteral("MOTOR_STOP"), QStringLiteral("MOTOR_RESET")}) {
        operation->setCurrentIndex(operation->findData(pending)); QCOMPARE(parameters->rowCount(), 0); QVERIFY(source->isHidden());
        QVERIFY(!operation->currentText().contains(QString::fromUtf8(u8"待实现"))); save->click();
        mapping = draftRoot(widget).value("mappings").toArray().at(0).toObject(); QCOMPARE(mapping.value("operation").toString(), pending);
        QVERIFY(mapping.value("parameters").toObject().isEmpty()); // 未设置与固定零不是同一状态。
        applyRoot(widget, draftRoot(widget)); QCOMPARE(operation->currentData().toString(), pending);
        button(widget, "previewButton")->click(); QVERIFY(widget.findChild<QTextEdit *>("targetOutput")->toPlainText().contains("AA AA"));
        QCoreApplication::processEvents();
        if (!renderDir.isEmpty()) QVERIFY(widget.grab().save(QDir(renderDir).filePath(pending.toLower() + ".png")));
    }
    // 人工载入旧绝对映射，方向3仍留在设备配置，载入和保存不能偷偷变成新前进。
    QJsonObject root = draftRoot(widget); QJsonArray mappings = root.value("mappings").toArray(); mapping = mappings.at(0).toObject();
    mapping.insert("operation", "ABSOLUTE_MOVE"); mapping.insert("parameters", QJsonObject{{"positionUm", QJsonObject{{"kind", "field"}, {"field", QString::fromUtf8(u8"位置")}}}});
    mappings[0] = mapping; root.insert("mappings", mappings); QJsonObject device = root.value("device").toObject(); device.insert("absoluteAction", 3); root.insert("device", device); applyRoot(widget, root);
    QCOMPARE(operation->count(), 60); QCOMPARE(operation->currentData().toString(), QString("ABSOLUTE_MOVE"));
    QVERIFY(maps->item(0, 1)->text().startsWith(QString::fromUtf8(u8"旧版")));
    QVERIFY(operation->currentText().contains(QString::fromUtf8(u8"旧版"))); QVERIFY(!legacyAction->isHidden());
    QCOMPARE(legacyAction->value(), 3); widget.findChild<QLineEdit *>("mappingCode")->setText("X"); save->click();
    QCOMPARE(draftRoot(widget).value("mappings").toArray().at(0).toObject().value("operation").toString(), QString("ABSOLUTE_MOVE"));
    QCOMPARE(draftRoot(widget).value("device").toObject().value("absoluteAction").toInt(), 3);
    maps->selectRow(1); QCOMPARE(operation->count(), 59); QCOMPARE(operation->findData("ABSOLUTE_MOVE"), -1);
    maps->selectRow(0); operation->setCurrentIndex(operation->findData("MOTOR_FORWARD")); save->click();
    QCOMPARE(operation->count(), 59); QCOMPARE(operation->findData("ABSOLUTE_MOVE"), -1); QVERIFY(legacyAction->isHidden());
    button(widget, "addMapping")->click();
    QCOMPARE(operation->currentData().toString(), QString("MOTOR_FORWARD"));
    QCOMPARE(source->currentData().toString(), QString());
    operation->setCurrentIndex(operation->findData("MOTOR_STOP"));
    QCOMPARE(operation->currentData().toString(), QString("MOTOR_STOP"));
    widget.findChild<QLineEdit *>("mappingCode")->setText("S"); save->click();
    const QJsonObject added = draftRoot(widget).value("mappings").toArray().last().toObject();
    QCOMPARE(added.value("operation").toString(), QString("MOTOR_STOP"));
    QCOMPARE(added.value("commandCode").toString(), QString("S")); QVERIFY(added.value("parameters").toObject().isEmpty());
}

void WidgetTest::fieldNamesFollowAllMappings()
{
    Widget widget; auto *fields = widget.findChild<QTableWidget *>("fieldTable");
    auto *maps = widget.findChild<QTableWidget *>("mappingTable");
    auto *target = widget.findChild<QTextEdit *>("targetOutput");
    button(widget, "previewButton")->click(); const QString originalFrame = target->toPlainText();
    button(widget, "validateConfig")->click(); button(widget, "enableConfig")->click();
    const QString active = widget.findChild<QLabel *>("activeVersion")->text();
    auto *confirm = widget.findChild<QCheckBox *>("deviceConfirmed"); confirm->setChecked(true);
    const QJsonObject oldDraft = draftRoot(widget);
    int queryRow = -1;
    for (int row = 0; row < maps->rowCount(); ++row) if (maps->item(row, 0)->text() == "?0") queryRow = row;
    QVERIFY(queryRow >= 0); maps->selectRow(queryRow);
    fields->item(2, 1)->setText(QString::fromUtf8(u8"数据参数"));
    QCOMPARE(draftRoot(widget), oldDraft); // 编辑表单尚未保存，不触碰草稿/已启用快照。
    QCOMPARE(widget.findChild<QLabel *>("activeVersion")->text(), active); QVERIFY(!confirm->isChecked());
    QVERIFY(!button(widget, "sendButton")->isEnabled());
    auto *omissions = widget.findChild<QGroupBox *>("mappingOmissions"); bool checkedNewName = false;
    for (QCheckBox *check : omissions->findChildren<QCheckBox *>())
        if (check->property("fieldName").toString() == QString::fromUtf8(u8"数据参数")) checkedNewName = check->isChecked();
    QVERIFY(checkedNewName); button(widget, "saveFields")->click();
    int motionRefs = 0, queryOmissions = 0;
    for (const QJsonValue &value : draftRoot(widget).value("mappings").toArray()) {
        const QJsonObject mapping = value.toObject();
        for (const QJsonValue &relation : mapping.value("parameters").toObject())
            if (relation.toObject().value("kind").toString() == "field") {
                QCOMPARE(relation.toObject().value("field").toString(), QString::fromUtf8(u8"数据参数")); ++motionRefs;
            }
        if (mapping.value("input").toObject().value("omitFields").toArray().contains(QString::fromUtf8(u8"数据参数"))) ++queryOmissions;
    }
    QCOMPARE(motionRefs, 6); QCOMPARE(queryOmissions, 5);
    button(widget, "validateConfig")->click(); QVERIFY(widget.findChild<QLabel *>("validationState")->text().contains(QString::fromUtf8(u8"通过")));
    widget.findChild<QLineEdit *>("customerInput")->setText("/1A2000R\\r"); button(widget, "previewButton")->click();
    QCOMPARE(target->toPlainText(), originalFrame);
    widget.findChild<QLineEdit *>("customerInput")->setText("/1?0R\\r"); button(widget, "previewButton")->click();
    QVERIFY(target->toPlainText().contains("00 12"));
    const QJsonObject saved = draftRoot(widget); applyRoot(widget, saved); button(widget, "saveFields")->click(); QCOMPARE(draftRoot(widget), saved);
    const QString renderDir = qEnvironmentVariable("TRANSLATION_RENDER_DIR");
    if (!renderDir.isEmpty()) {
        QDir().mkpath(renderDir); widget.show(); widget.findChild<QTabWidget *>("mainTabs")->setCurrentIndex(1); QCoreApplication::processEvents();
        QVERIFY(widget.grab().save(QDir(renderDir).filePath("renamed-query-mapping.png")));
    }
}

void WidgetTest::fieldIdentitySwapAndDeferredSave()
{
    Widget widget; QJsonObject root = draftRoot(widget); QJsonObject external = root.value("external").toObject();
    QJsonArray fieldsJson = external.value("fields").toArray(); QJsonObject position = fieldsJson.at(2).toObject();
    position.insert("lengthMode", "fixed"); position.insert("length", 1); fieldsJson[2] = position;
    fieldsJson.append(QJsonObject{{"name", QString::fromUtf8(u8"速度")}, {"role", "parameter"}, {"type", "uint"}, {"lengthMode", "toFooter"}});
    external.insert("fields", fieldsJson); root.insert("external", external);
    QJsonArray mappings = root.value("mappings").toArray(); QJsonObject pending = mappings.at(1).toObject();
    const QJsonObject advanced{{"kind", "linear"}, {"field", QString::fromUtf8(u8"速度")}, {"numerator", 1}, {"denominator", 3}, {"offset", 17}, {"round", "floor"}, {"note", "keep"}};
    const QJsonObject constant{{"kind", "constant"}, {"value", 224}, {"note", QString::fromUtf8(u8"位置")}};
    pending.insert("operation", "QUSHENG_81"); pending.insert("parameters", QJsonObject{{"devId", constant}, {"percent", advanced}});
    pending.insert("input", QJsonObject{{"note", "input metadata"}}); mappings[1] = pending; root.insert("mappings", mappings); applyRoot(widget, root);
    auto *fields = widget.findChild<QTableWidget *>("fieldTable"); auto *maps = widget.findChild<QTableWidget *>("mappingTable");
    auto *multiplier = widget.findChild<QLineEdit *>("mappingMultiplier"); auto *source = widget.findChild<QComboBox *>("mappingSource");
    multiplier->setText("2.5"); fields->item(2, 1)->setText(QString::fromUtf8(u8"速度"));
    QCOMPARE(multiplier->text(), QString("2.5")); QCOMPARE(source->currentData().toString(), QString::fromUtf8(u8"位置"));
    QCOMPARE(maps->item(0, 0)->data(Qt::UserRole).toJsonObject().value("parameters").toObject().value("positionUm").toObject().value("field").toString(), QString::fromUtf8(u8"位置"));
    const QJsonObject duplicateDraft = draftRoot(widget); button(widget, "saveFields")->click();
    QCOMPARE(draftRoot(widget), duplicateDraft); QVERIFY(widget.findChild<QLabel *>("validationState")->text().contains(QString::fromUtf8(u8"重复")));
    QCOMPARE(multiplier->text(), QString("2.5"));
    fields->item(3, 1)->setText(QString::fromUtf8(u8"位置")); // 完成交换，两个旧名必须一次原子映射。
    QCOMPARE(source->currentData().toString(), QString::fromUtf8(u8"速度")); QCOMPARE(multiplier->text(), QString("2.5"));
    const QJsonObject switched = maps->item(1, 0)->data(Qt::UserRole).toJsonObject();
    QJsonObject renamedAdvanced = advanced; renamedAdvanced.insert("field", QString::fromUtf8(u8"位置"));
    QCOMPARE(switched.value("parameters").toObject().value("percent").toObject(), renamedAdvanced);
    QCOMPARE(switched.value("parameters").toObject().value("devId").toObject(), constant); QCOMPARE(switched.value("input"), pending.value("input"));
    button(widget, "saveFields")->click();
    const QJsonObject beforeFailure = draftRoot(widget); multiplier->setText("abc");
    fields->item(2, 1)->setText(QString::fromUtf8(u8"数据")); button(widget, "saveFields")->click();
    QCOMPARE(draftRoot(widget), beforeFailure); QCOMPARE(multiplier->text(), QString("abc"));
    QCOMPARE(maps->item(0, 0)->data(Qt::UserRole).toJsonObject().value("parameters").toObject().value("positionUm").toObject().value("field").toString(), QString::fromUtf8(u8"速度"));
    multiplier->setText("3"); button(widget, "saveFields")->click();
    QCOMPARE(source->currentData().toString(), QString::fromUtf8(u8"数据"));
    QCOMPARE(draftRoot(widget).value("mappings").toArray().at(0).toObject().value("parameters").toObject().value("positionUm").toObject().value("numerator").toInt(), 3);
    const QJsonObject emptyDraft = draftRoot(widget);
    fields->item(2, 1)->setText(""); button(widget, "saveFields")->click(); QVERIFY(widget.findChild<QLabel *>("validationState")->text().contains(QString::fromUtf8(u8"不能为空")));
    QCOMPARE(draftRoot(widget), emptyDraft);
    fields->item(2, 1)->setText(QString::fromUtf8(u8"数据二")); QCOMPARE(source->currentData().toString(), QString::fromUtf8(u8"数据二"));
    fields->item(2, 1)->setText(QString::fromUtf8(u8"数据")); QCOMPARE(source->currentData().toString(), QString::fromUtf8(u8"数据"));
    // 选中待实现多参数关系，同时保留另一项未保存常量；改名后两者都能继续编辑。
    maps->selectRow(1); auto *parameters = widget.findChild<QTableWidget *>("mappingParameters");
    auto *fixed = qobject_cast<QLineEdit *>(parameters->cellWidget(0, 2)); fixed->setText("225");
    fields->item(3, 1)->setText(QString::fromUtf8(u8"反馈"));
    QCOMPARE(qobject_cast<QLineEdit *>(parameters->cellWidget(0, 2))->text(), QString("225"));
    QCOMPARE(qobject_cast<QComboBox *>(parameters->cellWidget(1, 1))->currentData().toString(), QString::fromUtf8(u8"反馈"));
    QJsonObject expectedAdvanced = advanced; expectedAdvanced.insert("field", QString::fromUtf8(u8"反馈"));
    button(widget, "saveFields")->click();
    QCOMPARE(draftRoot(widget).value("mappings").toArray().at(1).toObject().value("parameters").toObject().value("percent").toObject(), expectedAdvanced);
    fixed = qobject_cast<QLineEdit *>(parameters->cellWidget(0, 2)); fixed->setText("226");
    const QJsonObject pendingDraft = draftRoot(widget);
    fields->item(3, 1)->setText(QString::fromUtf8(u8"数据")); // 临时重复，选中项的来源原属此字段。
    button(widget, "saveFields")->click(); QCOMPARE(draftRoot(widget), pendingDraft);
    QCOMPARE(qobject_cast<QLineEdit *>(parameters->cellWidget(0, 2))->text(), QString("226"));
    QCOMPARE(qobject_cast<QComboBox *>(parameters->cellWidget(1, 1))->currentData().toString(), QString::fromUtf8(u8"反馈"));
    fields->item(2, 1)->setText(QString::fromUtf8(u8"反馈")); // 完成交换，来源必须跟随原第三个字段到“数据”。
    QCOMPARE(qobject_cast<QComboBox *>(parameters->cellWidget(1, 1))->currentData().toString(), QString::fromUtf8(u8"数据"));
    QCOMPARE(qobject_cast<QLineEdit *>(parameters->cellWidget(0, 2))->text(), QString("226"));
    button(widget, "saveFields")->click(); expectedAdvanced.insert("field", QString::fromUtf8(u8"数据"));
    const QJsonObject pendingAfterSwap = draftRoot(widget).value("mappings").toArray().at(1).toObject();
    QCOMPARE(pendingAfterSwap.value("parameters").toObject().value("percent").toObject(), expectedAdvanced);
    QCOMPARE(pendingAfterSwap.value("parameters").toObject().value("devId").toObject().value("value").toInt(), 226);
}

void WidgetTest::fieldDeletionTypesAndJsonBoundary()
{
    for (const QString &change : {QStringLiteral("delete"), QStringLiteral("ascii"), QStringLiteral("fixed")}) {
        Widget widget; auto *fields = widget.findChild<QTableWidget *>("fieldTable");
        if (change == "delete") { fields->selectRow(2); button(widget, "removeField")->click(); }
        else { const int column = change == "ascii" ? 3 : 2; auto *box = qobject_cast<QComboBox *>(fields->cellWidget(2, column)); box->setCurrentIndex(box->findData(change)); }
        QVERIFY(widget.findChild<QComboBox *>("mappingSource")->currentText().contains(QString::fromUtf8(u8"未找到")));
        QVERIFY(widget.findChild<QTableWidget *>("mappingTable")->item(0, 2)->text().contains(QString::fromUtf8(u8"待修正")));
        button(widget, "saveFields")->click(); button(widget, "validateConfig")->click();
        QVERIFY(widget.findChild<QLabel *>("validationState")->text().contains(QString::fromUtf8(u8"失败")));
        button(widget, "previewButton")->click(); QVERIFY(widget.findChild<QTextEdit *>("targetOutput")->toPlainText().isEmpty());
        QVERIFY(!button(widget, "sendButton")->isEnabled());
    }
    Widget widget; auto *fields = widget.findChild<QTableWidget *>("fieldTable"); button(widget, "addField")->click();
    auto *type = qobject_cast<QComboBox *>(fields->cellWidget(3, 3)); type->setCurrentIndex(type->findData("uint"));
    fields->item(3, 5)->setText("1"); fields->item(3, 1)->setText(QString::fromUtf8(u8"辅助"));
    QVERIFY(widget.findChild<QComboBox *>("mappingSource")->findData(QString::fromUtf8(u8"辅助")) >= 0);
    QCOMPARE(widget.findChild<QComboBox *>("mappingSource")->currentData().toString(), QString::fromUtf8(u8"位置"));
    fields->selectRow(3); button(widget, "fieldUp")->click(); button(widget, "saveFields")->click();
    QCOMPARE(draftRoot(widget).value("external").toObject().value("fields").toArray().at(2).toObject().value("name").toString(), QString::fromUtf8(u8"辅助"));
    QCOMPARE(draftRoot(widget).value("mappings").toArray().at(0).toObject().value("parameters").toObject().value("positionUm").toObject().value("field").toString(), QString::fromUtf8(u8"位置"));
    widget.findChild<QLineEdit *>("customerInput")->setText("/1A92000R\\r"); button(widget, "previewButton")->click();
    QVERIFY(widget.findChild<QTextEdit *>("previewOutput")->toPlainText().contains("2000")); QVERIFY(widget.findChild<QTextEdit *>("targetOutput")->toPlainText().contains("AA AA"));
    fields->selectRow(2); button(widget, "removeField")->click(); button(widget, "saveFields")->click();
    widget.findChild<QLineEdit *>("customerInput")->setText("/1A2000R\\r"); button(widget, "previewButton")->click(); QVERIFY(widget.findChild<QTextEdit *>("targetOutput")->toPlainText().contains("AA AA"));
    QJsonObject root = draftRoot(widget); QJsonObject external = root.value("external").toObject();
    QJsonArray names = external.value("fields").toArray(); QJsonObject field = names.at(2).toObject(); field.insert("name", "replacement"); names[2] = field;
    external.insert("fields", names); root.insert("external", external); applyRoot(widget, root);
    QCOMPARE(widget.findChild<QComboBox *>("mappingSource")->currentData().toString(), QString::fromUtf8(u8"位置"));
    button(widget, "validateConfig")->click(); QVERIFY(widget.findChild<QLabel *>("validationState")->text().contains(QString::fromUtf8(u8"位置")));
}


void WidgetTest::renderChinesePages()
{
    QDir::setCurrent(QFileInfo(QDir(QString::fromLatin1(QT_TESTCASE_BUILDDIR)).absoluteFilePath(QString::fromLatin1(__FILE__))).dir().absoluteFilePath(".."));
    Widget widget;
    widget.show();
    button(widget, "previewButton")->click();
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
        if (i == 1 && !renderDir.isEmpty()) QVERIFY(widget.grab().save(QDir(renderDir).filePath("motor-forward.png")));
    }
    tabs->setCurrentIndex(1);
    auto *operation = widget.findChild<QComboBox *>("mappingOperation");
    operation->setCurrentIndex(operation->findData("QUSHENG_81"));
    auto *parameters = widget.findChild<QTableWidget *>("mappingParameters");
    auto *devSource = qobject_cast<QComboBox *>(parameters->cellWidget(0, 1));
    devSource->setCurrentIndex(devSource->findData("@constant")); qobject_cast<QLineEdit *>(parameters->cellWidget(0, 2))->setText("224");
    auto *percentSource = qobject_cast<QComboBox *>(parameters->cellWidget(1, 1));
    percentSource->setCurrentIndex(percentSource->findData(QString::fromUtf8(u8"位置")));
    widget.findChild<QPushButton *>("saveMappings")->click(); QCoreApplication::processEvents();
    if (!renderDir.isEmpty()) QVERIFY(widget.grab().save(QDir(renderDir).filePath("mapping-sdk-81.png")));
    operation->setCurrentIndex(operation->findData("QUSHENG_C1"));
    auto *mappingScroll = qobject_cast<QScrollArea *>(tabs->widget(1)); QVERIFY(mappingScroll);
    operation->setCurrentIndex(operation->findData("QUSHENG_22"));
    mappingScroll->ensureWidgetVisible(operation); QCoreApplication::processEvents();
    operation->showPopup(); QCoreApplication::processEvents();
    operation->view()->scrollTo(operation->model()->index(operation->findData("QUSHENG_17"), 0), QAbstractItemView::PositionAtTop);
    QCoreApplication::processEvents();
    if (!renderDir.isEmpty()) QVERIFY(operation->view()->window()->grab().save(QDir(renderDir).filePath("command-dropdown.png")));
    operation->hidePopup();

}

QTEST_MAIN(WidgetTest)
#include "tst_widget.moc"
