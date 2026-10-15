#include <QtTest>
#include <QCheckBox>
#include <QAction>
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
#include <QDialog>
#include <QTimer>
#include <QListView>
#include <QStandardItemModel>
#include <QScrollArea>
#include <QSpinBox>
#include "qusheng/qushengcommandcatalog.h"
#include <QGroupBox>
#include <QTemporaryDir>
#include <QUuid>
#include <QFile>
#include <QFileDialog>
#include <QToolButton>
#include <QJsonArray>
#include <QJsonDocument>
#include "widget.h"
#include "transport/serialtransport.h"
#include "core/translationservice.h"

// 所有旧界面测试也使用独立草稿文件，避免测试覆盖用户数据或在用例间留下配置。
static QTemporaryDir &testDraftDirectory()
{ static QTemporaryDir directory; return directory; }
static QString freshDraftPath()
{ return testDraftDirectory().filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) + ".json"); }

class WidgetTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void debugSendNeedsNoWorkflowOrSavedDraft();
    void selectingRowsDoesNotEditMappings();
    void newMappingFormAddsOnlyValidRows();
    void deleteSelectedRowKeepsNewForm();
    void addMappingToEmptyTable();
    void draftLifecycleAndVisibleFailures();
    void advancedJsonAndDynamicControls();
    void simpleMappingEditorAndInvalidNumbers();
    void advancedMappingIsPreserved();
    void queryAndRelativeMappingControls();
    void completeCatalogAndSearchPreserveSelection();
    void catalogTextIsReadableAndSourceLiteralsAreExplicit();
    void pendingMappingEditorAndFieldRefresh();
    void switchingNewTargetDoesNotTouchRows();
    void simplifiedExpertInterface();
    void unifiedFormatAndVisibleErrors();
    void genericScalarRanges();
    void sdkGenericMappingsAndDefaults();
    void newQueriesOmitNumericFields();
    void sdkReportsAreVisibleWithoutCompletingRequests();
    void fixedMotorActionsAndLegacyCompatibility();
    void stopResetMappingsHaveNoDistanceControls();
    void resetAndStopOmitClientParameters();
    void oldResetDraftRestoresMissingOmissions();
    void explicitStopInputIsPreserved();
    void mappingButtonsStayTogether();
    void sharedSaveKeepsAllPages();
    void sharedSaveHandlesJsonAndConflict();
    void configMenuImportsAndExports();
    void savedDraftSurvivesRestart();
    void incompleteDraftSurvivesRestart();
    void draftWriteFailureIsVisibleAndRetryable();
    void damagedLocalDraftIsNotOverwritten();
    void closeSavesFormAndMapping();
    void closeCancelAndDiscardPreserveSavedDraft();
    void closeSavesAdvancedJson();
    void closeSaveFailuresAreRetryable();
    void closeConflictingEditorsRequiresChoice();
    void closeCleanDraftAndDebugInput();
    void closeCommitsActiveCellEditor();
    void closeEscapeCancels();
    void fieldNamesFollowAllMappings();
    void fieldRenameIgnoresUnsubmittedMapping();
    void fieldSwapPreservesMappingMetadata();
    void fieldDeletionTypesAndJsonBoundary();
    void renderChinesePages();
private:
    QTranslator m_chinese;
};

void WidgetTest::initTestCase()
{
    QVERIFY(testDraftDirectory().isValid());
    QVERIFY(m_chinese.load(QStringLiteral(":/translations/qt_zh_CN.qm")));
    qApp->installTranslator(&m_chinese);
    qApp->setAttribute(Qt::AA_DontUseNativeDialogs);
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

static QAction *configAction(Widget &widget, const char *objectName)
{
    return widget.findChild<QAction *>(QString::fromLatin1(objectName));
}

static bool closeWithChoice(Widget &widget, const QString &choice, bool *seen,
                            bool *saveEnabled = nullptr)
{
    *seen = false;
    // 使用可见窗口模拟用户关闭。
    widget.show();
    QCoreApplication::processEvents();
    QTimer::singleShot(0, &widget, [&widget, choice, seen, saveEnabled]() {
        auto *prompt = widget.findChild<QDialog *>("unsavedClosePrompt");
        if (!prompt) return;
        *seen = true;
        for (auto *action : prompt->findChildren<QPushButton *>()) {
            if (action->text() == QString::fromUtf8(u8"保存") && saveEnabled)
                *saveEnabled = action->isEnabled();
        }
        for (auto *action : prompt->findChildren<QPushButton *>()) {
            if (action->text() == choice) { action->click(); return; }
        }
        prompt->reject();
    });
    return widget.close();
}

static QByteArray draftBytes(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return QByteArray();
    return file.readAll();
}

void WidgetTest::closeSavesFormAndMapping()
{
    const QString path = freshDraftPath();
    Widget widget(nullptr, path);
    widget.findChild<QTableWidget *>("fieldTable")->item(2, 1)->setText(QString::fromUtf8(u8"数据位"));
    widget.findChild<QLineEdit *>("mappingCode")->setText("NEW");
    auto *op=widget.findChild<QComboBox *>("mappingOperation");op->setCurrentIndex(op->findData("MOTOR_RESET"));button(widget,"addMapping")->click();
    bool seen;
    QVERIFY(closeWithChoice(widget, QString::fromUtf8(u8"保存"), &seen));
    QVERIFY(seen);
    Widget restored(nullptr, path);
    QCOMPARE(restored.findChild<QTableWidget *>("fieldTable")->item(2, 1)->text(), QString::fromUtf8(u8"数据位"));
    QCOMPARE(restored.findChild<QTableWidget *>("mappingTable")->item(restored.findChild<QTableWidget *>("mappingTable")->rowCount()-1,0)->text(),QStringLiteral("NEW"));
    QVERIFY(restored.findChild<QLineEdit *>("mappingCode")->text().isEmpty());
    QVERIFY(!restored.findChild<QCheckBox *>("deviceConfirmed")->isChecked());
}

void WidgetTest::closeCancelAndDiscardPreserveSavedDraft()
{
    const QString path = freshDraftPath();
    Widget widget(nullptr, path);
    button(widget, "saveConfig")->click();
    const QByteArray original = draftBytes(path);
    QVERIFY(!original.isEmpty());
    auto *fields = widget.findChild<QTableWidget *>("fieldTable");
    fields->item(2, 1)->setText(QString::fromUtf8(u8"未保存字段"));
    bool seen;
    QVERIFY(!closeWithChoice(widget, QString::fromUtf8(u8"取消"), &seen));
    QVERIFY(seen);
    QCOMPARE(fields->item(2, 1)->text(), QString::fromUtf8(u8"未保存字段"));
    QCOMPARE(draftBytes(path), original);
    QVERIFY(closeWithChoice(widget, QString::fromUtf8(u8"放弃"), &seen));
    QVERIFY(seen);
    QCOMPARE(draftBytes(path), original);
}

void WidgetTest::closeSavesAdvancedJson()
{
    const QString path = freshDraftPath();
    Widget widget(nullptr, path);
    auto *json = widget.findChild<QTextEdit *>("advancedJson");
    QJsonObject root = QJsonDocument::fromJson(json->toPlainText().toUtf8()).object();
    root.insert("name", QString::fromUtf8(u8"关闭保存的草稿"));
    json->setPlainText(QString::fromUtf8(QJsonDocument(root).toJson()));
    bool seen;
    QVERIFY(closeWithChoice(widget, QString::fromUtf8(u8"保存"), &seen));
    QVERIFY(seen);
    QCOMPARE(QJsonDocument::fromJson(draftBytes(path)).object(), root);
}

void WidgetTest::closeSaveFailuresAreRetryable()
{
    const QString path = freshDraftPath();
    Widget widget(nullptr, path);
    button(widget, "saveConfig")->click();
    const QByteArray original = draftBytes(path);
    auto *fields = widget.findChild<QTableWidget *>("fieldTable");
    fields->item(2, 8)->setText("abc");
    bool seen;
    QVERIFY(!closeWithChoice(widget, QString::fromUtf8(u8"保存"), &seen));
    QVERIFY(seen);
    QCOMPARE(draftBytes(path), original);
    fields->item(2, 8)->setText("6000");
    QVERIFY(closeWithChoice(widget, QString::fromUtf8(u8"保存"), &seen));

    const QString blocked = freshDraftPath();
    QVERIFY(QDir().mkdir(blocked));
    Widget jsonWidget(nullptr, blocked);
    auto *json = jsonWidget.findChild<QTextEdit *>("advancedJson");
    json->setPlainText("{");
    QVERIFY(!closeWithChoice(jsonWidget, QString::fromUtf8(u8"保存"), &seen));
    QVERIFY(seen);
    QVERIFY(jsonWidget.findChild<QLabel *>("validationState")->text().contains(QString::fromUtf8(u8"JSON 无效")));
    json->setPlainText("{\"schemaVersion\":1,\"name\":\"retry\"}");
    QVERIFY(!closeWithChoice(jsonWidget, QString::fromUtf8(u8"保存"), &seen));
    QVERIFY(seen);
    QVERIFY(jsonWidget.findChild<QLabel *>("validationState")->text().contains(QString::fromUtf8(u8"保存失败")));
    QVERIFY(QDir().rmdir(blocked));
    bool saveEnabled = false;
    QVERIFY(closeWithChoice(jsonWidget, QString::fromUtf8(u8"保存"), &seen, &saveEnabled));
    QVERIFY(seen && saveEnabled);
    QCOMPARE(QJsonDocument::fromJson(draftBytes(blocked)).object().value("name").toString(), QStringLiteral("retry"));
}

void WidgetTest::closeConflictingEditorsRequiresChoice()
{
    const QString path = freshDraftPath();
    Widget widget(nullptr, path);
    button(widget, "saveConfig")->click();
    const QByteArray original = draftBytes(path);
    auto *fields = widget.findChild<QTableWidget *>("fieldTable");
    fields->item(2, 1)->setText(QString::fromUtf8(u8"表单修改"));
    auto *json = widget.findChild<QTextEdit *>("advancedJson");
    const QString editedJson = json->toPlainText() + " ";
    json->setPlainText(editedJson);
    bool seen, saveEnabled = true;
    QVERIFY(!closeWithChoice(widget, QString::fromUtf8(u8"取消"), &seen, &saveEnabled));
    QVERIFY(seen && !saveEnabled);
    QCOMPARE(draftBytes(path), original);
    QCOMPARE(json->toPlainText(), editedJson);
    QCOMPARE(fields->item(2, 1)->text(), QString::fromUtf8(u8"表单修改"));
}

void WidgetTest::closeCleanDraftAndDebugInput()
{
    const QString path = freshDraftPath();
    Widget widget(nullptr, path);
    widget.findChild<QLineEdit *>("customerInput")->setText("/1A100R\\r");
    bool seen;
    QVERIFY(closeWithChoice(widget, QString::fromUtf8(u8"取消"), &seen));
    QVERIFY(!seen);
    QVERIFY(!QFile::exists(path));
    QCoreApplication::processEvents(); // 清理没有出现提示时的定时回调。
}

void WidgetTest::closeCommitsActiveCellEditor()
{
    const QString path = freshDraftPath();
    Widget widget(nullptr, path);
    widget.show();
    auto *fields = widget.findChild<QTableWidget *>("fieldTable");
    fields->setCurrentCell(2, 8);
    fields->editItem(fields->item(2, 8));
    QCoreApplication::processEvents();
    auto *editor = qobject_cast<QLineEdit *>(QApplication::focusWidget());
    QVERIFY(editor && fields->isAncestorOf(editor));
    editor->selectAll();
    QTest::keyClicks(editor, "5000");
    bool seen;
    QVERIFY(closeWithChoice(widget, QString::fromUtf8(u8"保存"), &seen));
    QVERIFY(seen);
    Widget restored(nullptr, path);
    QCOMPARE(restored.findChild<QTableWidget *>("fieldTable")->item(2, 8)->text(), QStringLiteral("5000"));
}

void WidgetTest::closeEscapeCancels()
{
    Widget widget(nullptr, freshDraftPath());
    widget.show();
    QCoreApplication::processEvents();
    widget.findChild<QTableWidget *>("fieldTable")->item(2,1)->setText("UNSAVED");
    bool seen = false;
    bool cancelIsDefault = false;
    QTimer::singleShot(0, &widget, [&widget, &seen, &cancelIsDefault]() {
        auto *prompt = widget.findChild<QDialog *>("unsavedClosePrompt");
        if (!prompt) return;
        seen = true;
        for (auto *action : prompt->findChildren<QPushButton *>())
            if (action->isDefault() && action->text() == QString::fromUtf8(u8"取消")) cancelIsDefault = true;
        const QString renderDirectory = qEnvironmentVariable("TRANSLATION_RENDER_DIR");
        if (!renderDirectory.isEmpty()) {
            QDir().mkpath(renderDirectory);
            prompt->grab().save(QDir(renderDirectory).filePath("unsaved-close.png"));
        }
        QTest::keyClick(prompt, Qt::Key_Escape);
    });
    QVERIFY(!widget.close());
    QVERIFY(seen);
    QVERIFY(cancelIsDefault);
    QCOMPARE(widget.findChild<QTableWidget *>("fieldTable")->item(2,1)->text(),QStringLiteral("UNSAVED"));
}

void WidgetTest::debugSendNeedsNoWorkflowOrSavedDraft()
{
    const QString path = freshDraftPath();
    Widget widget(nullptr,path);
    auto *service = widget.findChild<TranslationService *>();
    QSignalSpy results(service,&TranslationService::completed);
    auto *send = button(widget,"sendButton");
    QVERIFY(send->isEnabled()); QVERIFY(button(widget,"queryButton")->isEnabled());
    auto *json = widget.findChild<QTextEdit *>("advancedJson");
    const QString original = json->toPlainText();
    widget.findChild<QLineEdit *>("mappingCode")->setText("Z");
    widget.findChild<QTableWidget *>("fieldTable")->item(2,8)->setText("1000");
    auto *newSource=widget.findChild<QComboBox *>("mappingSource");newSource->setCurrentIndex(newSource->findData(QString::fromUtf8(u8"位置")));button(widget,"addMapping")->click();
    widget.findChild<QLineEdit *>("customerInput")->setText("/1Z7000R\\r");
    send->click(); QTRY_COMPARE(results.count(),1);
    QCOMPARE(qvariant_cast<UnifiedResult>(results.last().at(0)).error.code,QStringLiteral("DISCONNECTED"));
    QVERIFY(widget.findChild<QTextEdit *>("targetOutput")->toPlainText().contains("00 1B 58"));
    QVERIFY(widget.findChild<QTextEdit *>("previewOutput")->toPlainText().contains("Z"));
    QVERIFY(!QFile::exists(path)); QCOMPARE(json->toPlainText(),original);
    QVERIFY(send->isEnabled()); send->click(); QTRY_COMPARE(results.count(),2);
    QVERIFY(!widget.findChild<QCheckBox *>("deviceConfirmed")->isChecked());
    button(widget,"queryButton")->click(); QTRY_COMPARE(results.count(),3);
    // 未应用JSON也直接用于本次调试，不覆盖已保存草稿；坏JSON明确报错。
    json->setPlainText(original);
    widget.findChild<QLineEdit *>("customerInput")->setText("/1A2000R\\r");
    send->click(); QTRY_COMPARE(results.count(),4);
    QVERIFY(widget.findChild<QTextEdit *>("targetOutput")->toPlainText().contains("00 07 D0"));
    json->setPlainText("{"); send->click(); QCoreApplication::processEvents();
    QCOMPARE(results.count(),4);
    QVERIFY(widget.findChild<QTextEdit *>("previewOutput")->toPlainText().contains(QString::fromUtf8(u8"JSON")));
    QVERIFY(widget.findChild<QTextEdit *>("targetOutput")->toPlainText().isEmpty());
    QVERIFY(send->isEnabled()); QVERIFY(!QFile::exists(path));
}

void WidgetTest::draftLifecycleAndVisibleFailures()
{
    QDir::setCurrent(QFileInfo(QDir(QString::fromLatin1(QT_TESTCASE_BUILDDIR)).absoluteFilePath(QString::fromLatin1(__FILE__))).dir().absoluteFilePath(".."));
    Widget widget(nullptr, freshDraftPath());
    QCOMPARE(widget.findChildren<SerialTransport *>().size(), 1);
    auto *input = widget.findChild<QLineEdit *>("customerInput");
    auto *fields = widget.findChild<QTableWidget *>("fieldTable");
    auto *preview = widget.findChild<QTextEdit *>("previewOutput");
    auto *target = widget.findChild<QTextEdit *>("targetOutput");
    auto *reason = widget.findChild<QLabel *>("liveReason");
    QVERIFY(input && fields && preview && target && reason);
    auto *previewButton = button(widget, "previewButton");
    auto *validateButton = configAction(widget, "validateConfig");
    auto *enableButton = configAction(widget, "enableConfig");
    auto *saveButton = button(widget, "saveConfig");
    auto *sendButton = button(widget, "sendButton");
    QVERIFY(previewButton && validateButton && enableButton && saveButton && sendButton);
    previewButton->click();
    QVERIFY(preview->toPlainText().contains(QString::fromUtf8(u8"2000")));
    QVERIFY(target->toPlainText().contains("AA AA 02 01 08 F7 00 11"));
    fields->item(2, 8)->setText("1000");
    validateButton->trigger(); enableButton->trigger();
    QVERIFY(reason->text().contains(QString::fromUtf8(u8"串口未连接")));
    previewButton->click(); QVERIFY(target->toPlainText().isEmpty());
    QVERIFY(preview->toPlainText().contains(QString::fromUtf8(u8"保存")));
    saveButton->click(); validateButton->trigger(); enableButton->trigger();
    auto *confirm = widget.findChild<QCheckBox *>("deviceConfirmed");
    QVERIFY(confirm->isEnabled()); confirm->setChecked(true);
    QVERIFY(reason->text().contains(QString::fromUtf8(u8"串口未连接")));
    QVERIFY(sendButton->isEnabled());
    previewButton->click(); QVERIFY(target->toPlainText().isEmpty());
    QVERIFY(preview->toPlainText().contains(QString::fromUtf8(u8"最大值")));
    input->setText("/1A500R\\r"); previewButton->click();
    QVERIFY(target->toPlainText().contains("AA AA"));
    const QJsonObject beforeInvalidRange = QJsonDocument::fromJson(widget.findChild<QTextEdit *>("advancedJson")->toPlainText().toUtf8()).object();
    fields->item(2, 8)->setText("abc"); saveButton->click();
    QVERIFY(widget.findChild<QLabel *>("validationState")->text().contains(QString::fromUtf8(u8"第 3 行最大值不是数字")));
    QCOMPARE(QJsonDocument::fromJson(widget.findChild<QTextEdit *>("advancedJson")->toPlainText().toUtf8()).object(), beforeInvalidRange);
    // 保存失败仍为未保存表单，后续不能继续校验旧值并冒充新的成功版本。
    validateButton->trigger(); previewButton->click();
    QVERIFY(target->toPlainText().isEmpty()); QVERIFY(!confirm->isChecked());
    QVERIFY(!confirm->isEnabled());
    fields->item(2, 8)->setText("1000"); fields->item(2, 7)->setText("invalid"); saveButton->click();
    QVERIFY(widget.findChild<QLabel *>("validationState")->text().contains(QString::fromUtf8(u8"第 3 行最小值不是数字")));
    QCOMPARE(QJsonDocument::fromJson(widget.findChild<QTextEdit *>("advancedJson")->toPlainText().toUtf8()).object(), beforeInvalidRange);
}

void WidgetTest::advancedJsonAndDynamicControls()
{
    QDir::setCurrent(QFileInfo(QDir(QString::fromLatin1(QT_TESTCASE_BUILDDIR)).absoluteFilePath(QString::fromLatin1(__FILE__))).dir().absoluteFilePath(".."));
    Widget widget(nullptr, freshDraftPath());
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
    button(widget, "saveConfig")->click();
    const QString saved = json->toPlainText();
    json->setPlainText(saved + "broken");
    button(widget, "saveConfig")->click();
    QVERIFY(json->toPlainText().endsWith("broken"));
    button(widget, "previewButton")->click();
    QVERIFY(widget.findChild<QTextEdit *>("targetOutput")->toPlainText().isEmpty());
    json->setPlainText(saved);
    button(widget, "applyJson")->click();
    configAction(widget, "validateConfig")->trigger();
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

static QJsonArray mappingRows(Widget &widget)
{
    auto *table=widget.findChild<QTableWidget *>("mappingTable"); QJsonArray rows;
    for (int row=0;row<table->rowCount();++row)
        rows.append(table->item(row,0)->data(Qt::UserRole).toJsonObject());
    return rows;
}

static void editResetMapping(Widget &widget)
{
    widget.findChild<QLineEdit *>("mappingCode")->setText("Z");
    auto *operation=widget.findChild<QComboBox *>("mappingOperation");
    operation->setCurrentIndex(operation->findData("MOTOR_RESET"));
}

static void addNewMapping(Widget &widget,const QString &code,const QString &op,const QString &source=QString(),const QString &fixed=QString())
{
    widget.findChild<QLineEdit *>("mappingCode")->setText(code);
    auto *operation=widget.findChild<QComboBox *>("mappingOperation"); operation->setCurrentIndex(operation->findData(op));
    auto *sources=widget.findChild<QComboBox *>("mappingSource");
    if (!source.isEmpty()) sources->setCurrentIndex(sources->findData(source));
    if (!fixed.isEmpty()) widget.findChild<QLineEdit *>("mappingValue")->setText(fixed);
    button(widget,"addMapping")->click();
}

void WidgetTest::selectingRowsDoesNotEditMappings()
{
    const QString path=freshDraftPath(); Widget widget(nullptr,path);
    auto *table=widget.findChild<QTableWidget *>("mappingTable"); const QJsonArray original=mappingRows(widget);
    editResetMapping(widget);
    for(int row=0;row<table->rowCount();++row) {
        table->setCurrentCell(row,1);
        QCOMPARE(widget.findChild<QLineEdit *>("mappingCode")->text(),QStringLiteral("Z"));
        QCOMPARE(widget.findChild<QComboBox *>("mappingOperation")->currentData().toString(),QStringLiteral("MOTOR_RESET"));
        QCOMPARE(mappingRows(widget),original);
    }
    button(widget,"saveConfig")->click(); QCOMPARE(mappingRows(widget),original);
    QCOMPARE(draftRoot(widget).value("mappings").toArray(),original);
    const QString renderDir=qEnvironmentVariable("TRANSLATION_RENDER_DIR");
    if (!renderDir.isEmpty()) {
        widget.findChild<QTabWidget *>("mainTabs")->setCurrentIndex(1);widget.show();QCoreApplication::processEvents();QDir().mkpath(renderDir);
        QVERIFY(widget.grab().save(QDir(renderDir).filePath("mapping-add-delete-only.png")));
    }
    button(widget,"addMapping")->click(); const QJsonArray rows=mappingRows(widget);
    QCOMPARE(rows.size(),original.size()+1);
    for(int i=0;i<original.size();++i) QCOMPARE(rows.at(i),original.at(i));
    QCOMPARE(rows.last().toObject().value("commandCode").toString(),QStringLiteral("Z"));
    QVERIFY(widget.findChild<QLineEdit *>("mappingCode")->text().isEmpty());
    button(widget,"saveConfig")->click(); Widget restored(nullptr,path); QCOMPARE(mappingRows(restored),rows);
}

void WidgetTest::newMappingFormAddsOnlyValidRows()
{
    Widget widget(nullptr,freshDraftPath()); const QJsonArray original=mappingRows(widget);
    button(widget,"addMapping")->click(); QCOMPARE(mappingRows(widget),original); // 空表单不制造占位行。
    addNewMapping(widget,"Z","MOTOR_RESET");
    QCOMPARE(mappingRows(widget).size(),original.size()+1);
    QVERIFY(widget.findChild<QLineEdit *>("mappingCode")->text().isEmpty());
    addNewMapping(widget,"S","MOTOR_STOP");
    QCOMPARE(mappingRows(widget).size(),original.size()+2);
    QCOMPARE(mappingRows(widget).last().toObject().value("commandCode").toString(),QStringLiteral("S"));
}

void WidgetTest::deleteSelectedRowKeepsNewForm()
{
    Widget widget(nullptr,freshDraftPath()); const QJsonArray original=mappingRows(widget);
    addNewMapping(widget,"Z","MOTOR_RESET"); const QJsonArray added=mappingRows(widget);
    addNewMapping(widget,"Z","MOTOR_STOP"); QCOMPARE(mappingRows(widget),added);
    QVERIFY(widget.findChild<QLabel *>("validationState")->text().contains(QString::fromUtf8(u8"已存在")));
    auto *table=widget.findChild<QTableWidget *>("mappingTable"); table->setCurrentCell(0,0);
    widget.findChild<QLineEdit *>("mappingCode")->setText("S");
    const QString newCode=widget.findChild<QLineEdit *>("mappingCode")->text();
    button(widget,"removeMapping")->click(); QCOMPARE(mappingRows(widget).size(),added.size()-1);
    QCOMPARE(widget.findChild<QLineEdit *>("mappingCode")->text(),newCode);
    for(int i=0;i<original.size()-1;++i) QCOMPARE(mappingRows(widget).at(i),original.at(i+1));
    button(widget,"addMapping")->click(); QCOMPARE(mappingRows(widget).last().toObject().value("commandCode").toString(),QStringLiteral("S"));
}

void WidgetTest::addMappingToEmptyTable()
{
    Widget widget(nullptr,freshDraftPath()); QJsonObject root=draftRoot(widget); root.insert("mappings",QJsonArray()); applyRoot(widget,root);
    QVERIFY(widget.findChild<QGroupBox *>("mappingEditor")->isEnabled());
    QVERIFY(!button(widget,"removeMapping")->isEnabled());
    addNewMapping(widget,"Z","MOTOR_RESET"); QCOMPARE(mappingRows(widget).size(),1);
    QVERIFY(button(widget,"removeMapping")->isEnabled()); button(widget,"removeMapping")->click();
    QVERIFY(mappingRows(widget).isEmpty()); QVERIFY(!button(widget,"removeMapping")->isEnabled());
    addNewMapping(widget,"H","MOTOR_RESET"); QCOMPARE(mappingRows(widget).size(),1);
}

static int parameterRow(QTableWidget *table, const QString &key)
{
    for (int row = 0; row < table->rowCount(); ++row)
        if (table->item(row, 0)->data(Qt::UserRole).toJsonObject().value("key").toString() == key) return row;
    return -1;
}

void WidgetTest::simpleMappingEditorAndInvalidNumbers()
{
    Widget widget(nullptr,freshDraftPath()); const int count=mappingRows(widget).size();
    addNewMapping(widget,"F","MOTOR_FORWARD","@constant","abc"); QCOMPARE(mappingRows(widget).size(),count);
    QVERIFY(widget.findChild<QLabel *>("validationState")->text().contains(QString::fromUtf8(u8"整数")));
    widget.findChild<QLineEdit *>("mappingValue")->setText("2345"); button(widget,"addMapping")->click();
    QCOMPARE(mappingRows(widget).size(),count+1);
    QCOMPARE(mappingRows(widget).last().toObject().value("parameters").toObject().value("positionUm").toObject().value("value").toInt(),2345);
    widget.findChild<QLineEdit *>("mappingMultiplier")->setText("abc");
    addNewMapping(widget,"B","MOTOR_BACKWARD",QString::fromUtf8(u8"位置")); QCOMPARE(mappingRows(widget).size(),count+1);
    widget.findChild<QLineEdit *>("mappingMultiplier")->setText("2.5"); button(widget,"addMapping")->click();
    const QJsonObject relation=mappingRows(widget).last().toObject().value("parameters").toObject().value("positionUm").toObject();
    QCOMPARE(relation.value("numerator").toInt(),5); QCOMPARE(relation.value("denominator").toInt(),2);
}

void WidgetTest::advancedMappingIsPreserved()
{
    Widget widget(nullptr,freshDraftPath()); QJsonObject root=draftRoot(widget);
    QJsonArray mappings=root.value("mappings").toArray(); QJsonObject map=mappings.first().toObject();
    QJsonObject relation{{"kind","linear"},{"field",QString::fromUtf8(u8"位置")},{"numerator",1},{"denominator",3},{"offset",17},{"round","floor"},{"note","keep"}};
    map.insert("parameters",QJsonObject{{"positionUm",relation}}); map.insert("note","metadata"); mappings[0]=map;root.insert("mappings",mappings);applyRoot(widget,root);
    auto *table=widget.findChild<QTableWidget *>("mappingTable"); table->setCurrentCell(0,0);
    QVERIFY(widget.findChild<QLineEdit *>("mappingCode")->text().isEmpty());
    addNewMapping(widget,"Z","MOTOR_RESET");button(widget,"saveConfig")->click();
    QCOMPARE(draftRoot(widget).value("mappings").toArray().first().toObject(),map);
    QCOMPARE(draftRoot(widget).value("mappings").toArray().last().toObject().value("parameters").toObject(),QJsonObject());
}

void WidgetTest::queryAndRelativeMappingControls()
{
    Widget widget(nullptr,freshDraftPath()); const QJsonArray original=mappingRows(widget);
    auto *table=widget.findChild<QTableWidget *>("mappingTable");table->setCurrentCell(2,0);
    auto *operation=widget.findChild<QComboBox *>("mappingOperation");
    QCOMPARE(operation->findData("RELATIVE_FORWARD"),-1);
    QCOMPARE(operation->currentData().toString(),QStringLiteral("MOTOR_FORWARD"));
    addNewMapping(widget,"Q","QUERY_MOTOR");
    const QJsonObject query=mappingRows(widget).last().toObject();
    QVERIFY(query.value("input").toObject().value("omitFields").toArray().contains(QString::fromUtf8(u8"位置")));
    for(int i=0;i<original.size();++i) QCOMPARE(mappingRows(widget).at(i),original.at(i));
    button(widget,"saveConfig")->click();widget.findChild<QLineEdit *>("customerInput")->setText("/1QR\\r");button(widget,"previewButton")->click();
    QVERIFY(widget.findChild<QTextEdit *>("targetOutput")->toPlainText().contains("00 12"));
}

void WidgetTest::completeCatalogAndSearchPreserveSelection()
{
    Widget widget(nullptr, freshDraftPath());
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
    search->clear(); widget.findChild<QPushButton *>("saveConfig")->click();
    QCOMPARE(draftRoot(widget),saved);
}

void WidgetTest::catalogTextIsReadableAndSourceLiteralsAreExplicit()
{
    Widget widget(nullptr, freshDraftPath());
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
    Widget widget(nullptr,freshDraftPath()); auto *op=widget.findChild<QComboBox *>("mappingOperation");
    widget.findChild<QLineEdit *>("mappingCode")->setText("U");op->setCurrentIndex(op->findData("QUSHENG_13"));
    auto *params=widget.findChild<QTableWidget *>("mappingParameters");
    widget.findChild<QTableWidget *>("fieldTable")->item(2,1)->setText(QString::fromUtf8(u8"新位置"));
    for(int row=0;row<params->rowCount();++row) QVERIFY(qobject_cast<QComboBox *>(params->cellWidget(row,1))->findData(QString::fromUtf8(u8"新位置"))>=0);
    button(widget,"addMapping")->click();button(widget,"saveConfig")->click();
    QCOMPARE(mappingRows(widget).last().toObject().value("operation").toString(),QStringLiteral("QUSHENG_13"));
}

void WidgetTest::switchingNewTargetDoesNotTouchRows()
{
    Widget widget(nullptr,freshDraftPath()); const QJsonArray original=mappingRows(widget);
    auto *op=widget.findChild<QComboBox *>("mappingOperation");widget.findChild<QLineEdit *>("mappingCode")->setText("Z");
    op->setCurrentIndex(op->findData("QUSHENG_81"));op->setCurrentIndex(op->findData("MOTOR_RESET"));
    QCOMPARE(mappingRows(widget),original);button(widget,"saveConfig")->click();QCOMPARE(mappingRows(widget),original);
    button(widget,"addMapping")->click();QCOMPARE(mappingRows(widget).size(),original.size()+1);
}

void WidgetTest::simplifiedExpertInterface()
{
    Widget widget(nullptr, freshDraftPath()); widget.show(); QCoreApplication::processEvents();
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
    Widget widget(nullptr, freshDraftPath()); QVERIFY(!widget.findChild<QWidget *>("mappingResponse")); QVERIFY(!widget.findChild<QWidget *>("mappingFooter"));
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
    button(widget, "saveConfig")->click();
    for (const QString &text : {QStringLiteral("/1A2000!\\r"), QStringLiteral("/1?0!\\r")}) {
        input->setText(text); button(widget, "previewButton")->click(); QVERIFY(target->toPlainText().contains("AA AA"));
    }
    input->setText("/1?0R\\r"); button(widget, "previewButton")->click(); QVERIFY(target->toPlainText().isEmpty());
    widget.findChild<QComboBox *>("mappingOperation")->setCurrentIndex(widget.findChild<QComboBox *>("mappingOperation")->findData("MOTOR_STOP"));
    widget.findChild<QLineEdit *>("mappingCode")->setText("S"); button(widget,"addMapping")->click(); button(widget, "saveConfig")->click();
    const QJsonObject added = draftRoot(widget).value("mappings").toArray().last().toObject();
    QCOMPARE(added.value("commandCode").toString(), QString("S")); QVERIFY(!added.contains("responsePolicy"));
    QVERIFY(!added.value("input").toObject().contains("footer"));
    widget.findChild<QTextEdit *>("advancedJson")->setPlainText("invalid"); button(widget, "applyJson")->click();
    QVERIFY(widget.findChild<QLabel *>("validationState")->text().contains(QString::fromUtf8(u8"JSON 无效")));
    auto *transport = widget.findChild<SerialTransport *>(); QVERIFY(transport);
    transport->connectionChanged(false, QString::fromUtf8(u8"找不到串口设备；系统详情：测试详情"));
    QCOMPARE(widget.findChild<QLabel *>("validationState")->text(), QString::fromUtf8(u8"找不到串口设备"));
    // 迁移不修复未获授权的格式错误：坏类型仍保留，让配置校验明确拒绝。
    Widget invalid(nullptr, freshDraftPath()); QJsonObject invalidRoot = draftRoot(invalid);
    invalidRoot.insert("mappings", "invalid-array"); applyRoot(invalid, invalidRoot);
    QCOMPARE(draftRoot(invalid).value("mappings"), QJsonValue(QStringLiteral("invalid-array")));
    applyRoot(invalid, migrated);
    invalidRoot = draftRoot(invalid); QJsonArray invalidRows = invalidRoot.value("mappings").toArray();
    QJsonObject invalidMapping = invalidRows.at(0).toObject(); const QJsonArray invalidInput{"bad-type"};
    invalidMapping.insert("input", invalidInput); invalidRows[0] = invalidMapping;
    invalidRoot.insert("mappings", invalidRows); applyRoot(invalid, invalidRoot);
    QCOMPARE(draftRoot(invalid).value("mappings").toArray().at(0).toObject().value("input"), QJsonValue(invalidInput));
    invalid.findChild<QLineEdit *>("mappingCode")->setText("B"); button(invalid, "saveConfig")->click();
    QCOMPARE(draftRoot(invalid).value("mappings").toArray().at(0).toObject().value("input"), QJsonValue(invalidInput));
    configAction(invalid, "validateConfig")->trigger();
    QVERIFY(invalid.findChild<QLabel *>("validationState")->text().contains(QString::fromUtf8(u8"校验失败")));
}

void WidgetTest::genericScalarRanges()
{
    Widget widget(nullptr,freshDraftPath());const int count=mappingRows(widget).size();
    auto *op=widget.findChild<QComboBox *>("mappingOperation");op->setCurrentIndex(op->findData("QUSHENG_A1"));
    widget.findChild<QLineEdit *>("mappingCode")->setText("T");auto *params=widget.findChild<QTableWidget *>("mappingParameters");
    const int row=parameterRow(params,"pwm"); QVERIFY(row>=0);
    auto *source=qobject_cast<QComboBox *>(params->cellWidget(row,1));source->setCurrentIndex(source->findData("@constant"));
    auto *value=qobject_cast<QLineEdit *>(params->cellWidget(row,2));value->setText("-129");button(widget,"addMapping")->click();QCOMPARE(mappingRows(widget).size(),count);
    value->setText("-20");button(widget,"addMapping")->click();QCOMPARE(mappingRows(widget).size(),count+1);
    QCOMPARE(mappingRows(widget).last().toObject().value("parameters").toObject().value("pwm").toObject().value("value").toInt(),-20);
}

void WidgetTest::resetAndStopOmitClientParameters()
{
    Widget widget(nullptr,freshDraftPath());
    widget.findChild<QTableWidget *>("fieldTable")->item(2,1)->setText(QString::fromUtf8(u8"数据位"));
    auto *op=widget.findChild<QComboBox *>("mappingOperation");
    const QList<QPair<QString,QString>> actions{{"Z","MOTOR_RESET"},{"S","MOTOR_STOP"}};
    for(const auto &action:actions) {
        widget.findChild<QLineEdit *>("mappingCode")->setText(action.first);op->setCurrentIndex(op->findData(action.second));
        bool omitted=false;
        for(QCheckBox *check:widget.findChild<QGroupBox *>("mappingOmissions")->findChildren<QCheckBox *>())
            if(check->property("fieldName").toString()==QString::fromUtf8(u8"数据位")) omitted=check->isChecked();
        QVERIFY(omitted);
        const QString renderDir=qEnvironmentVariable("TRANSLATION_RENDER_DIR");
        if(action.first=="Z" && !renderDir.isEmpty()) {
            widget.findChild<QTabWidget *>("mainTabs")->setCurrentIndex(1);widget.show();QCoreApplication::processEvents();QDir().mkpath(renderDir);
            QVERIFY(widget.grab().save(QDir(renderDir).filePath("reset-omissions-and-buttons.png")));
        }
        button(widget,"addMapping")->click();
        const QJsonObject mapping=mappingRows(widget).last().toObject();
        QVERIFY(mapping.value("input").toObject().value("omitFields").toArray().contains(QString::fromUtf8(u8"数据位")));
        button(widget,"saveConfig")->click();
        widget.findChild<QLineEdit *>("customerInput")->setText("/1"+action.first+"R\\r");
        button(widget,"previewButton")->click();
        const QString payload=action.first=="Z"?"11 44 00 03 E8 00 00 00":"11 42 00 03 E8 00 00 00";
        QVERIFY(widget.findChild<QTextEdit *>("targetOutput")->toPlainText().contains(payload));
        button(widget,"sendButton")->click();
        QVERIFY(widget.findChild<QTextEdit *>("targetOutput")->toPlainText().contains(payload));
        QCoreApplication::processEvents();
    }
    // 回到有客户参数的前进模板，默认重新读取数据位。
    op->setCurrentIndex(op->findData("MOTOR_RESET"));op->setCurrentIndex(op->findData("MOTOR_FORWARD"));
    for(QCheckBox *check:widget.findChild<QGroupBox *>("mappingOmissions")->findChildren<QCheckBox *>())
        if(check->property("fieldRole").toString()=="parameter") QVERIFY(!check->isChecked());
}

void WidgetTest::oldResetDraftRestoresMissingOmissions()
{
    Widget seed(nullptr,freshDraftPath());QJsonObject root=draftRoot(seed);
    QJsonArray mappings=root.value("mappings").toArray();
    mappings.append(QJsonObject{{"commandCode","Z"},{"operation","MOTOR_RESET"},{"parameters",QJsonObject()},{"note","keep"}});
    root.insert("mappings",mappings);const QString path=freshDraftPath();DraftStore store(path);TranslationError error;
    QVERIFY(store.save(root,&error));const QByteArray original=draftBytes(path);
    Widget restored(nullptr,path);
    const QJsonObject migrated=mappingRows(restored).last().toObject();
    QVERIFY(migrated.value("input").toObject().value("omitFields").toArray().contains(QString::fromUtf8(u8"位置")));
    QCOMPARE(migrated.value("note").toString(),QStringLiteral("keep"));QCOMPARE(draftBytes(path),original);
    restored.findChild<QLineEdit *>("customerInput")->setText("/1ZR\\r");button(restored,"previewButton")->click();
    QVERIFY(restored.findChild<QTextEdit *>("targetOutput")->toPlainText().contains("11 44 00 03 E8 00 00 00"));
    button(restored,"saveConfig")->click();Widget again(nullptr,path);QCOMPARE(mappingRows(again),mappingRows(restored));
}

void WidgetTest::explicitStopInputIsPreserved()
{
    Widget widget(nullptr,freshDraftPath());QJsonObject root=draftRoot(widget);QJsonArray rows=root.value("mappings").toArray();
    const QJsonObject mapping{{"commandCode","S"},{"operation","MOTOR_STOP"},{"parameters",QJsonObject()},
        {"input",QJsonObject{{"omitFields",QJsonArray()},{"note","explicit"}}}};
    rows.append(mapping);root.insert("mappings",rows);applyRoot(widget,root);
    QCOMPARE(mappingRows(widget).last().toObject(),mapping);
    widget.findChild<QLineEdit *>("customerInput")->setText("/1S0R\\r");button(widget,"previewButton")->click();
    QVERIFY(widget.findChild<QTextEdit *>("targetOutput")->toPlainText().contains("11 42"));
}

void WidgetTest::mappingButtonsStayTogether()
{
    Widget widget(nullptr,freshDraftPath());widget.findChild<QTabWidget *>("mainTabs")->setCurrentIndex(1);
    widget.show();QCoreApplication::processEvents();
    auto *add=button(widget,"addMapping");auto *remove=button(widget,"removeMapping");auto *save=button(widget,"saveConfig");
    const QPoint a=add->mapTo(&widget,QPoint()),b=remove->mapTo(&widget,QPoint()),c=save->mapTo(&widget,QPoint());
    QCOMPARE(a.y(),b.y());QVERIFY(a.x()<b.x());QVERIFY(c.y()>b.y());
    QVERIFY(!widget.findChild<QGroupBox *>("mappingEditor")->isAncestorOf(add));
}

void WidgetTest::sharedSaveKeepsAllPages()
{
    const QString path=freshDraftPath();Widget widget(nullptr,path);widget.show();
    auto *tabs=widget.findChild<QTabWidget *>("mainTabs");
    auto *save=button(widget,"saveConfig");QVERIFY(save);
    QCOMPARE(save->text(),QString::fromUtf8(u8"保存配置"));
    for(const char *oldName:{"saveFields","saveMappings","saveDevice","importConfig","exportConfig","validateConfig","enableConfig"})
        QVERIFY(!widget.findChild<QPushButton *>(oldName));
    widget.findChild<QLineEdit *>("formatHeader")->setText("!");
    addNewMapping(widget,"Z","MOTOR_RESET");
    widget.findChild<QSpinBox *>("defaultMotorSpeed")->setValue(1234);
    // 切到调试页保存，仍须包含其他页面的修改，不提交未点击添加的新表单。
    widget.findChild<QLineEdit *>("mappingCode")->setText("NOT_ADDED");
    tabs->setCurrentIndex(2);QCoreApplication::processEvents();QVERIFY(save->isVisible());save->click();
    Widget restored(nullptr,path);
    QCOMPARE(restored.findChild<QLineEdit *>("formatHeader")->text(),QStringLiteral("!"));
    QCOMPARE(restored.findChild<QSpinBox *>("defaultMotorSpeed")->value(),1234);
    QCOMPARE(mappingRows(restored).last().toObject().value("commandCode").toString(),QStringLiteral("Z"));
    for(int i=0;i<tabs->count();++i) {tabs->setCurrentIndex(i);QCoreApplication::processEvents();QVERIFY(save->isVisible());}
    for(QPushButton *b:widget.findChildren<QPushButton *>()) QVERIFY(!b->text().contains(QString::fromUtf8(u8"草稿")));
    for(QLabel *label:widget.findChildren<QLabel *>()) QVERIFY(!label->text().contains(QString::fromUtf8(u8"草稿")));
}

void WidgetTest::sharedSaveHandlesJsonAndConflict()
{
    const QString path=freshDraftPath();Widget widget(nullptr,path);
    auto *json=widget.findChild<QTextEdit *>("advancedJson");
    QJsonObject root=draftRoot(widget);root.insert("name","JSON-SAVED");
    json->setPlainText(QString::fromUtf8(QJsonDocument(root).toJson()));button(widget,"saveConfig")->click();
    Widget restored(nullptr,path);QCOMPARE(draftRoot(restored).value("name").toString(),QStringLiteral("JSON-SAVED"));
    const QByteArray original=draftBytes(path);
    json->setPlainText("{");button(widget,"saveConfig")->click();
    QCOMPARE(draftBytes(path),original);QCOMPARE(json->toPlainText(),QStringLiteral("{"));
    QVERIFY(widget.findChild<QLabel *>("validationState")->text().contains(QString::fromUtf8(u8"JSON 无效")));
    widget.findChild<QLineEdit *>("formatHeader")->setText("!");
    json->setPlainText(QString::fromUtf8(QJsonDocument(root).toJson()));button(widget,"saveConfig")->click();
    QCOMPARE(draftBytes(path),original);QCOMPARE(widget.findChild<QLineEdit *>("formatHeader")->text(),QStringLiteral("!"));
    QVERIFY(widget.findChild<QLabel *>("validationState")->text().contains(QString::fromUtf8(u8"都有修改")));
}

void WidgetTest::configMenuImportsAndExports()
{
    const QString path=freshDraftPath(),exportPath=freshDraftPath();Widget widget(nullptr,path);widget.show();
    auto *menuButton=widget.findChild<QToolButton *>("configMenuButton");QVERIFY(menuButton && menuButton->menu());
    QCOMPARE(menuButton->text(),QString::fromUtf8(u8"配置"));
    int count=0;for(QAction *action:menuButton->menu()->actions()) if(!action->isSeparator()) ++count;
    QCOMPARE(count,4);
    for(const char *name:{"importConfig","exportConfig","validateConfig","enableConfig"})
        QVERIFY(menuButton->menu()->actions().contains(configAction(widget,name)));
    auto fileAction=[&widget](const char *name,const QString &filePath) {
        bool seen=false;
        QTimer::singleShot(0,&widget,[&widget,&seen,filePath]() {
            auto *dialog=widget.findChild<QFileDialog *>();if(!dialog) return;
            seen=true;dialog->selectFile(filePath);QMetaObject::invokeMethod(dialog,"accept",Qt::QueuedConnection);
        });
        configAction(widget,name)->trigger();return seen;
    };
    addNewMapping(widget,"Z","MOTOR_RESET");button(widget,"saveConfig")->click();
    const QJsonObject exported=draftRoot(widget);
    QVERIFY(fileAction("exportConfig",exportPath));QCOMPARE(QJsonDocument::fromJson(draftBytes(exportPath)).object(),exported);
    widget.findChild<QLineEdit *>("formatHeader")->setText("!");button(widget,"saveConfig")->click();
    QVERIFY(fileAction("importConfig",exportPath));QCOMPARE(draftRoot(widget),exported);
    Widget restored(nullptr,path);QCOMPARE(draftRoot(restored),exported);
    configAction(widget,"validateConfig")->trigger();
    QVERIFY(widget.findChild<QLabel *>("validationState")->text().contains(QString::fromUtf8(u8"通过")));
    configAction(widget,"enableConfig")->trigger();
    QVERIFY(!widget.findChild<QLabel *>("activeVersion")->text().contains(QString::fromUtf8(u8"无")));
    const QString renderDir=qEnvironmentVariable("TRANSLATION_RENDER_DIR");
    if(!renderDir.isEmpty()) {
        QDir().mkpath(renderDir);widget.findChild<QTabWidget *>("mainTabs")->setCurrentIndex(1);
        menuButton->menu()->popup(menuButton->mapToGlobal(QPoint(0,menuButton->height())));QCoreApplication::processEvents();
        QVERIFY(menuButton->menu()->grab().save(QDir(renderDir).filePath("config-menu.png")));menuButton->menu()->hide();
    }
}

void WidgetTest::stopResetMappingsHaveNoDistanceControls()
{
    Widget widget(nullptr,freshDraftPath());auto *op=widget.findChild<QComboBox *>("mappingOperation");
    for(const QString &action:{QString("MOTOR_STOP"),QString("MOTOR_RESET")}) {
        op->setCurrentIndex(op->findData(action));QVERIFY(widget.findChild<QComboBox *>("mappingSource")->isHidden());
        QVERIFY(widget.findChild<QLineEdit *>("mappingMultiplier")->isHidden());
        addNewMapping(widget,action=="MOTOR_STOP"?"S":"Z",action);
        QVERIFY(mappingRows(widget).last().toObject().value("parameters").toObject().isEmpty());
    }
}

void WidgetTest::sdkGenericMappingsAndDefaults()
{
    Widget widget(nullptr,freshDraftPath());auto *op=widget.findChild<QComboBox *>("mappingOperation");op->setCurrentIndex(op->findData("QUSHENG_81"));
    widget.findChild<QLineEdit *>("mappingCode")->setText("FAN");auto *params=widget.findChild<QTableWidget *>("mappingParameters");
    int row=parameterRow(params,"percent");QVERIFY(row>=0);auto *source=qobject_cast<QComboBox *>(params->cellWidget(row,1));
    source->setCurrentIndex(source->findData(QString::fromUtf8(u8"位置")));button(widget,"addMapping")->click();button(widget,"saveConfig")->click();
    widget.findChild<QLineEdit *>("customerInput")->setText("/1FAN50R\\r");button(widget,"previewButton")->click();
    QVERIFY(widget.findChild<QTextEdit *>("targetOutput")->toPlainText().contains("00 81"));
    QVERIFY(widget.findChild<QTextEdit *>("previewOutput")->toPlainText().contains("50"));
}

void WidgetTest::newQueriesOmitNumericFields()
{
    Widget widget(nullptr,freshDraftPath());
    addNewMapping(widget,"QM","QUERY_MOTOR");addNewMapping(widget,"QV","QUERY_DEVICE_INFO");button(widget,"saveConfig")->click();
    const QList<QPair<QString,QString>> cases{{"QM","00 12"},{"QV","00 04"}};
    for(const auto &item:cases){widget.findChild<QLineEdit *>("customerInput")->setText("/1"+item.first+"R\\r");button(widget,"previewButton")->click();
        QVERIFY(widget.findChild<QTextEdit *>("targetOutput")->toPlainText().contains(item.second));}
}


void WidgetTest::sdkReportsAreVisibleWithoutCompletingRequests()
{
    Widget widget(nullptr, freshDraftPath()); auto *service = widget.findChild<TranslationService *>(); QVERIFY(service);
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
    Widget widget(nullptr,freshDraftPath());const QJsonArray original=mappingRows(widget);
    const QStringList actions{"MOTOR_FORWARD","MOTOR_STOP","MOTOR_BACKWARD","MOTOR_RESET"};
    for(int i=0;i<actions.size();++i){
        addNewMapping(widget,"N"+QString::number(i),actions.at(i),i==0||i==2?"@constant":"",i==0||i==2?"2000":"");
        QCOMPARE(mappingRows(widget).last().toObject().value("operation").toString(),actions.at(i));}
    button(widget,"saveConfig")->click();for(int i=0;i<original.size();++i)QCOMPARE(mappingRows(widget).at(i),original.at(i));
    auto *table=widget.findChild<QTableWidget *>("mappingTable");table->setCurrentCell(2,0);
    QCOMPARE(widget.findChild<QComboBox *>("mappingOperation")->findData("RELATIVE_FORWARD"),-1);
    QVERIFY(widget.findChild<QLineEdit *>("mappingCode")->text().isEmpty());
}

void WidgetTest::fieldNamesFollowAllMappings()
{
    Widget widget(nullptr, freshDraftPath()); auto *fields = widget.findChild<QTableWidget *>("fieldTable");
    auto *maps = widget.findChild<QTableWidget *>("mappingTable");
    auto *target = widget.findChild<QTextEdit *>("targetOutput");
    button(widget, "previewButton")->click(); const QString originalFrame = target->toPlainText();
    configAction(widget, "validateConfig")->trigger(); configAction(widget, "enableConfig")->trigger();
    const QString active = widget.findChild<QLabel *>("activeVersion")->text();
    auto *confirm = widget.findChild<QCheckBox *>("deviceConfirmed"); confirm->setChecked(true);
    const QJsonObject oldDraft = draftRoot(widget);
    int queryRow = -1;
    for (int row = 0; row < maps->rowCount(); ++row) if (maps->item(row, 0)->text() == "?0") queryRow = row;
    QVERIFY(queryRow >= 0); maps->selectRow(queryRow);
    fields->item(2, 1)->setText(QString::fromUtf8(u8"数据参数"));
    QCOMPARE(draftRoot(widget), oldDraft); // 编辑表单尚未保存，不触碰草稿/已启用快照。
    QCOMPARE(widget.findChild<QLabel *>("activeVersion")->text(), active); QVERIFY(!confirm->isChecked());
    QVERIFY(button(widget, "sendButton")->isEnabled());
    auto *omissions = widget.findChild<QGroupBox *>("mappingOmissions"); bool checkedNewName = false;
    for (QCheckBox *check : omissions->findChildren<QCheckBox *>())
        if (check->property("fieldName").toString() == QString::fromUtf8(u8"数据参数")) checkedNewName = check->isChecked();
    QVERIFY(!checkedNewName); button(widget, "saveConfig")->click();
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
    configAction(widget, "validateConfig")->trigger(); QVERIFY(widget.findChild<QLabel *>("validationState")->text().contains(QString::fromUtf8(u8"通过")));
    widget.findChild<QLineEdit *>("customerInput")->setText("/1A2000R\\r"); button(widget, "previewButton")->click();
    QCOMPARE(target->toPlainText(), originalFrame);
    widget.findChild<QLineEdit *>("customerInput")->setText("/1?0R\\r"); button(widget, "previewButton")->click();
    QVERIFY(target->toPlainText().contains("00 12"));
    const QJsonObject saved = draftRoot(widget); applyRoot(widget, saved); button(widget, "saveConfig")->click(); QCOMPARE(draftRoot(widget), saved);
    const QString renderDir = qEnvironmentVariable("TRANSLATION_RENDER_DIR");
    if (!renderDir.isEmpty()) {
        QDir().mkpath(renderDir); widget.show(); widget.findChild<QTabWidget *>("mainTabs")->setCurrentIndex(1); QCoreApplication::processEvents();
        QVERIFY(widget.grab().save(QDir(renderDir).filePath("renamed-query-mapping.png")));
    }
}

void WidgetTest::fieldRenameIgnoresUnsubmittedMapping()
{
    Widget widget(nullptr,freshDraftPath());auto *fields=widget.findChild<QTableWidget *>("fieldTable");
    auto *source=widget.findChild<QComboBox *>("mappingSource");source->setCurrentIndex(source->findData(QString::fromUtf8(u8"位置")));
    widget.findChild<QLineEdit *>("mappingCode")->setText("N");widget.findChild<QLineEdit *>("mappingMultiplier")->setText("abc");
    fields->item(2,1)->setText(QString::fromUtf8(u8"数据"));button(widget,"saveConfig")->click();
    QCOMPARE(source->currentData().toString(),QString::fromUtf8(u8"数据"));
    QCOMPARE(widget.findChild<QLineEdit *>("mappingMultiplier")->text(),QStringLiteral("abc"));
    QCOMPARE(draftRoot(widget).value("mappings").toArray().first().toObject().value("parameters").toObject().value("positionUm").toObject().value("field").toString(),QString::fromUtf8(u8"数据"));
    fields->item(2,1)->setText("");const QJsonObject before=draftRoot(widget);button(widget,"saveConfig")->click();QCOMPARE(draftRoot(widget),before);
}

void WidgetTest::fieldSwapPreservesMappingMetadata()
{
    Widget widget(nullptr,freshDraftPath()); QJsonObject root=draftRoot(widget);
    QJsonObject external=root.value("external").toObject(); QJsonArray fields=external.value("fields").toArray();
    fields.append(QJsonObject{{"name",QString::fromUtf8(u8"速度")},{"role","parameter"},{"type","uint"},{"lengthMode","toFooter"}});
    external.insert("fields",fields); root.insert("external",external);
    QJsonArray maps=root.value("mappings").toArray(); QJsonObject mapping=maps.first().toObject();
    QJsonObject relation{{"kind","linear"},{"field",QString::fromUtf8(u8"位置")},{"numerator",1},{"denominator",3},{"offset",17},{"round","floor"},{"note","keep"}};
    mapping.insert("parameters",QJsonObject{{"positionUm",relation}});mapping.insert("note","metadata");maps[0]=mapping;root.insert("mappings",maps);applyRoot(widget,root);
    auto *table=widget.findChild<QTableWidget *>("fieldTable");
    auto *source=widget.findChild<QComboBox *>("mappingSource");source->setCurrentIndex(source->findData(QString::fromUtf8(u8"位置")));
    table->item(2,1)->setText(QString::fromUtf8(u8"速度"));
    QCOMPARE(mappingRows(widget).first().toObject(),mapping); // 重名暂态不猜测迁移。
    table->item(3,1)->setText(QString::fromUtf8(u8"位置"));
    relation.insert("field",QString::fromUtf8(u8"速度"));mapping.insert("parameters",QJsonObject{{"positionUm",relation}});
    QCOMPARE(mappingRows(widget).first().toObject(),mapping);
    QCOMPARE(source->currentData().toString(),QString::fromUtf8(u8"速度"));
}

void WidgetTest::fieldDeletionTypesAndJsonBoundary()
{
    Widget widget(nullptr,freshDraftPath());auto *fields=widget.findChild<QTableWidget *>("fieldTable");
    fields->setCurrentCell(2,0);button(widget,"removeField")->click();button(widget,"saveConfig")->click();
    configAction(widget, "validateConfig")->trigger();QVERIFY(widget.findChild<QLabel *>("validationState")->text().contains(QString::fromUtf8(u8"校验失败")));
    QCOMPARE(mappingRows(widget).first().toObject().value("parameters").toObject().value("positionUm").toObject().value("field").toString(),QString::fromUtf8(u8"位置"));
}


void WidgetTest::renderChinesePages()
{
    QDir::setCurrent(QFileInfo(QDir(QString::fromLatin1(QT_TESTCASE_BUILDDIR)).absoluteFilePath(QString::fromLatin1(__FILE__))).dir().absoluteFilePath(".."));
    Widget widget(nullptr, freshDraftPath());
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
    widget.findChild<QPushButton *>("saveConfig")->click(); QCoreApplication::processEvents();
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

void WidgetTest::savedDraftSurvivesRestart()
{
    const QString path = freshDraftPath(); QJsonObject saved; QByteArray previewFrame;
    {
        Widget first(nullptr, path);
        QVERIFY(first.findChild<QLineEdit *>("formatHeader"));
        first.findChild<QTableWidget *>("fieldTable")->item(2, 1)->setText(QString::fromUtf8(u8"数据位"));
        first.findChild<QLineEdit *>("formatHeader")->setText("!");
        first.findChild<QLineEdit *>("formatFooter")->setText("~");
        auto *newSource=first.findChild<QComboBox *>("mappingSource");newSource->setCurrentIndex(newSource->findData(QString::fromUtf8(u8"数据位")));
        addNewMapping(first,"Z","MOTOR_FORWARD",QString::fromUtf8(u8"数据位"));
        first.findChild<QSpinBox *>("defaultMotorSpeed")->setValue(2500);
        button(first, "saveConfig")->click(); saved = draftRoot(first);
        DraftStore store(path); TranslationError error; QJsonObject disk;
        QVERIFY2(store.load(&disk, &error), qPrintable(error.message)); QCOMPARE(disk, saved);
        QCOMPARE(saved.value("schemaVersion").toInt(), 1);
        ProtocolConfig config; QVERIFY(ProtocolConfig::fromJson(QJsonDocument(saved).toJson(), &config, &error));
        ConversionPreview converted; QVERIFY(ConversionEngine(config).convert("!1Z2000~", false, &converted, &error));
        previewFrame = converted.targetFrame;
        configAction(first, "validateConfig")->trigger(); configAction(first, "enableConfig")->trigger();
    }
    // 即使启动目录改变，也须优先读取同一用户草稿，而非重新加载示例。
    const QString originalDirectory = QDir::currentPath(); QVERIFY(QDir::setCurrent(testDraftDirectory().path()));
    Widget second(nullptr, path); QDir::setCurrent(originalDirectory);
    QVERIFY(second.findChild<QLineEdit *>("formatHeader"));
    QCOMPARE(draftRoot(second), saved);
    QCOMPARE(second.findChild<QTableWidget *>("fieldTable")->item(2, 1)->text(), QString::fromUtf8(u8"数据位"));
    QCOMPARE(second.findChild<QLineEdit *>("formatHeader")->text(), QString("!"));
    QCOMPARE(second.findChild<QSpinBox *>("defaultMotorSpeed")->value(), 2500);
    second.findChild<QLineEdit *>("customerInput")->setText("!1Z2000~"); button(second, "previewButton")->click();
    QVERIFY(second.findChild<QTextEdit *>("targetOutput")->toPlainText().contains(previewFrame.toHex(' ').toUpper()));
    QCOMPARE(second.findChild<QLabel *>("activeVersion")->text(), QString::fromUtf8(u8"启用：无"));
    QVERIFY(!second.findChild<QCheckBox *>("deviceConfirmed")->isChecked());
    QVERIFY(button(second, "sendButton")->isEnabled());
}

void WidgetTest::incompleteDraftSurvivesRestart()
{
    const QString path = freshDraftPath(); QJsonObject incomplete;
    {
        Widget first(nullptr, path); incomplete = draftRoot(first);
        QJsonArray mappings = incomplete.value("mappings").toArray(); QJsonObject mapping = mappings.first().toObject();
        mapping.insert("parameters", QJsonObject{{"positionUm", QJsonObject{{"kind", "field"}, {"field", "unfinished-field"}}}});
        mappings[0] = mapping; incomplete.insert("mappings", mappings);
        // 应用高级JSON和导入均通过同一落盘入口，不要求先成为可发送的有效配置。
        applyRoot(first, incomplete); configAction(first, "validateConfig")->trigger();
        QVERIFY(first.findChild<QLabel *>("validationState")->text().contains(QString::fromUtf8(u8"校验失败")));
    }
    Widget second(nullptr, path); QCOMPARE(draftRoot(second), incomplete);
    configAction(second, "validateConfig")->trigger();
    QVERIFY(second.findChild<QLabel *>("validationState")->text().contains(QString::fromUtf8(u8"unfinished-field")));
    QVERIFY(button(second, "sendButton")->isEnabled());
}

void WidgetTest::draftWriteFailureIsVisibleAndRetryable()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const QString blockerPath = directory.filePath("not-a-directory");
    QFile blocker(blockerPath); QVERIFY(blocker.open(QIODevice::WriteOnly)); blocker.write("keep"); blocker.close();
    const QString path = blockerPath + "/draft.json";
    Widget widget(nullptr, path); const QJsonObject original = draftRoot(widget);
    widget.findChild<QTableWidget *>("fieldTable")->item(2, 1)->setText(QString::fromUtf8(u8"数据位"));
    button(widget, "saveConfig")->click();
    QCOMPARE(draftRoot(widget), original);
    QVERIFY(widget.findChild<QLabel *>("draftVersion")->text().contains(QString::fromUtf8(u8"未保存")));
    QVERIFY(widget.findChild<QLabel *>("validationState")->text().contains(QString::fromUtf8(u8"保存失败")));
    QVERIFY(blocker.open(QIODevice::ReadOnly)); QCOMPARE(blocker.readAll(), QByteArray("keep")); blocker.close();
    QVERIFY(QFile::remove(blockerPath)); button(widget, "saveConfig")->click();
    DraftStore store(path); QJsonObject disk; TranslationError error;
    QVERIFY(store.load(&disk, &error)); QCOMPARE(disk, draftRoot(widget));
    QCOMPARE(disk.value("external").toObject().value("fields").toArray().at(2).toObject().value("name").toString(), QString::fromUtf8(u8"数据位"));
}

void WidgetTest::damagedLocalDraftIsNotOverwritten()
{
    const QString path = freshDraftPath(); QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly)); file.write("{broken-json"); file.close();
    {
        Widget widget(nullptr, path);
        QVERIFY(widget.findChild<QLabel *>("validationState")->text().contains(QString::fromUtf8(u8"恢复失败")));
        QVERIFY(button(widget, "sendButton")->isEnabled());
    }
    QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), QByteArray("{broken-json"));
}

QTEST_MAIN(WidgetTest)
#include "tst_widget.moc"
