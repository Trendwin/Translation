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
#include <QSplitter>
#include "qusheng/qushengcommandcatalog.h"
#include <QGroupBox>
#include <QTemporaryDir>
#include <QUuid>
#include <QFile>
#include <QFileDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include "widget.h"
#include "transport/serialtransport.h"
#include "core/translationservice.h"
#include "core/requestmanager.h"
#include "qusheng/qushengprotocol.h"

// 所有旧界面测试也使用独立草稿文件，避免测试覆盖用户数据或在用例间留下配置。
static QTemporaryDir &testDraftDirectory()
{ static QTemporaryDir directory; return directory; }
static QString freshDraftPath()
{ return testDraftDirectory().filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) + ".json"); }

class WidgetTest : public QObject
{
    Q_OBJECT
private slots:
    void valveStateParameterEditorLifecycle();
    void valveSelectionMappingLifecycle();
    void valveSourcesAndMetadataArePreserved();
    void formatDrivenParsingKeepsUnusedParameters();
    void explicitMappingStepOperationsAndResponsiveLayout();
    void noParameterResetGroupsAndExplicitTargets();
    void serialWriteErrorClosesBeforeNotification();
    void oneToManyMappingEditorLifecycle();
    void oneToManySharedInputAndPendingEdit();
    void directParameterSourcesAndMultipleFields();
    void sixMotorDraftRecoveryAndImport();
    void newMappingListGuidance();
    void integerListMappingLifecycle();
    void integerListFieldAdaptationAndDebugSend();
    void initTestCase();
    void sequentialDelimiterEditorLifecycle();
    void retiredDraftRecoveryRequiresExplicitSave();
    void retiredImportDoesNotOverwriteDraft();
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
    void perMappingMotorLifecycle();
    void xyzModuleMappingLifecycle();
    void stopResetMappingsHaveNoDistanceControls();
    void resetAndStopOmitClientParameters();
    void noParameterSourceKeepsFixedMotionAndCommonFields();
    void noParameterSdkSourceRejectsOmittedReferences();
    void oldResetDraftRestoresMissingOmissions();
    void explicitStopInputIsPreserved();
    void mappingButtonsStayTogether();
    void sharedSaveKeepsAllPages();
    void sharedSaveHandlesJsonAndConflict();
    void configButtonsImportAndExport();
    void savedConfigActivatesAndInvalidConfigDoesNot();
    void saveKeepsPendingReplyAndSequence();
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

void WidgetTest::serialWriteErrorClosesBeforeNotification()
{
    const QList<QSerialPort::SerialPortError> errors = {
        QSerialPort::WriteError, QSerialPort::ReadError, QSerialPort::ResourceError
    };
    for (QSerialPort::SerialPortError error : errors) {
        SerialTransport transport;
        // 只设置 QIODevice 的打开状态，不接触任何真实串口或设备。
        QVERIFY(transport.m_port.QIODevice::open(QIODevice::ReadWrite));
        QVERIFY(transport.isConnected());
        transport.m_pendingRequest = 41;
        transport.m_pendingBytes = 12;
        QSignalSpy finished(&transport, &ITransport::sendFinished);
        QSignalSpy connection(&transport, &ITransport::connectionChanged);
        bool checkedAtNotification = false;
        connect(&transport, &ITransport::sendFinished, &transport,
                [&](quint64 requestId, bool success, const QString &reason) {
            if (requestId != 41) return;
            checkedAtNotification = true;
            QVERIFY(!success);
            QVERIFY(!reason.isEmpty());
            QVERIFY(!transport.isConnected());
            QCOMPARE(transport.m_pendingRequest, quint64(0));
            QCOMPARE(transport.m_pendingBytes, qint64(0));
        });
        transport.m_port.errorOccurred(error);
        QVERIFY(checkedAtNotification);
        QCOMPARE(finished.count(), 1);
        QCOMPARE(connection.count(), 1);
        QCOMPARE(connection.first().at(0).toBool(), false);

        transport.m_port.bytesWritten(12);
        QCOMPARE(finished.count(), 1); // 晚到写完成不能再次完成原请求。
        transport.sendBytes(42, QByteArray("next"));
        QCOMPARE(finished.count(), 2);
        QCOMPARE(finished.last().at(0).toULongLong(), quint64(42));
        QCOMPARE(finished.last().at(1).toBool(), false);
        QVERIFY(finished.last().at(2).toString().contains(QString::fromUtf8(u8"未连接")));
    }

    SerialTransport idle;
    QVERIFY(idle.m_port.QIODevice::open(QIODevice::ReadWrite));
    QSignalSpy idleFinished(&idle, &ITransport::sendFinished);
    QSignalSpy idleConnection(&idle, &ITransport::connectionChanged);
    idle.m_port.errorOccurred(QSerialPort::ReadError);
    QVERIFY(idle.isConnected()); // 无写操作时保留原有非资源错误处理。
    QCOMPARE(idleConnection.count(), 0);
    idle.m_port.errorOccurred(QSerialPort::ResourceError);
    QVERIFY(!idle.isConnected());
    QCOMPARE(idleConnection.count(), 1);
    QCOMPARE(idleFinished.count(), 0);
}

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
    QVERIFY(!restored.findChild<QWidget *>("deviceConfirmed"));
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
    QVERIFY(!widget.findChild<QWidget *>("deviceConfirmed"));
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
    auto *saveButton = button(widget, "saveConfig");
    auto *sendButton = button(widget, "sendButton");
    QVERIFY(previewButton && saveButton && sendButton);
    previewButton->click();
    QVERIFY(preview->toPlainText().contains(QString::fromUtf8(u8"2000")));
    QVERIFY(target->toPlainText().contains("AA AA 02 01 08 F7 00 11"));
    fields->item(2, 8)->setText("1000");

    QVERIFY(reason->text().contains(QString::fromUtf8(u8"串口未连接")));
    previewButton->click(); QVERIFY(target->toPlainText().isEmpty());
    QVERIFY(preview->toPlainText().contains(QString::fromUtf8(u8"保存")));
    saveButton->click();

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
    previewButton->click();
    QVERIFY(target->toPlainText().isEmpty());

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
    QCOMPARE(operation->itemText(operation->findData("QUSHENG_21")), QString::fromUtf8(u8"0x21 | 电磁阀开关（单路/多路）"));
    QCOMPARE(operation->itemText(operation->findData("QUSHENG_22")), QString::fromUtf8(u8"0x22 | 电磁阀单路开启（仅开启）"));
    QCOMPARE(operation->itemText(operation->findData("QUSHENG_17")), QString::fromUtf8(u8"0x17 | 电机运动完成上报 (仅接收)"));
    QCOMPARE(operation->itemText(operation->findData("MOTOR_FORWARD")), QString::fromUtf8(u8"0x11 | 步进电机前进"));
    QCOMPARE(operation->itemText(operation->findData("MOTOR_STOP")), QString::fromUtf8(u8"0x11 | 步进电机停止"));
    QCOMPARE(operation->itemText(operation->findData("MOTOR_BACKWARD")), QString::fromUtf8(u8"0x11 | 步进电机后退"));
    QCOMPARE(operation->itemText(operation->findData("MOTOR_RESET")), QString::fromUtf8(u8"0x11 | 步进电机复位"));
    QCOMPARE(operation->itemText(operation->findData("QUERY_MOTOR")), QString::fromUtf8(u8"0x12 | 电机信息查询"));
    QCOMPARE(operation->itemText(operation->findData("QUERY_DEVICE_INFO")), QString::fromUtf8(u8"0x04 | 设备信息查询"));
    QCOMPARE(operation->itemData(operation->findData("QUSHENG_22"), Qt::ToolTipRole).toString(), QString::fromUtf8(u8"电磁阀单路开启（仅开启）"));
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
    auto *tabs = widget.findChild<QTabWidget *>("mainTabs"); QVERIFY(tabs); QCOMPARE(tabs->count(), 4);
    QCOMPARE(widget.windowTitle(),QString::fromUtf8(u8"配置驱动协议转换"));
    QVERIFY(!widget.findChild<QWidget *>("mappingOmissions"));
    QVERIFY(!widget.findChild<QWidget *>("advancedPage")->isVisible());
    QVERIFY(!widget.findChild<QLabel *>("draftVersion")->isVisible());
    QVERIFY(!widget.findChild<QLabel *>("activeVersion")->isVisible());
    for(int i=0;i<tabs->count();++i) QVERIFY(tabs->tabText(i)!=QString::fromUtf8(u8"高级配置"));
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
    QVERIFY(!widget.findChild<QWidget *>("deviceConfirmed"));
    tabs->setCurrentIndex(1); QCoreApplication::processEvents();
    auto *scroll = widget.findChild<QScrollArea *>("mappingEditorScroll"); QVERIFY(scroll);
    scroll->ensureWidgetVisible(widget.findChild<QLineEdit *>("mappingMultiplier")); QCoreApplication::processEvents();
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
    QVERIFY(invalid.findChild<QLabel *>("validationState")->text().contains(QString::fromUtf8(u8"配置有误")));
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
        QCOMPARE(widget.findChild<QComboBox *>("mappingSource")->currentData().toString(),QStringLiteral("@none"));
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
    QVERIFY(widget.findChild<QComboBox *>("mappingSource")->currentData().toString()!=QStringLiteral("@none"));
}

void WidgetTest::noParameterSourceKeepsFixedMotionAndCommonFields()
{
    const QString path=freshDraftPath();Widget widget(nullptr,path);QJsonObject root=draftRoot(widget);
    QJsonObject external=root.value("external").toObject();QJsonArray fields=external.value("fields").toArray();
    QJsonObject position=fields.at(2).toObject();position.insert("lengthMode","fixed");position.insert("length",2);fields[2]=position;
    fields.append(QJsonObject{{"name",QString::fromUtf8(u8"辅助值")},{"role","parameter"},{"type","uint"},{"lengthMode","toFooter"}});
    external.insert("fields",fields);root.insert("external",external);applyRoot(widget,root);
    auto *source=widget.findChild<QComboBox *>("mappingSource");source->setCurrentIndex(source->findData("@none"));
    widget.findChild<QLineEdit *>("mappingCode")->setText("N");const int count=mappingRows(widget).size();
    button(widget,"addMapping")->click();QCOMPARE(mappingRows(widget).size(),count); // 不隐式把目标位置填成零。
    widget.findChild<QTableWidget *>("fieldTable")->item(2,1)->setText(QString::fromUtf8(u8"数据位"));
    QCOMPARE(source->currentData().toString(),QStringLiteral("@none"));
    widget.findChild<QLineEdit *>("mappingValue")->setText("1234");button(widget,"addMapping")->click();
    QCOMPARE(mappingRows(widget).size(),count+1);const QJsonObject mapping=mappingRows(widget).last().toObject();
    const QJsonArray omitted=mapping.value("input").toObject().value("omitFields").toArray();
    QCOMPARE(omitted.size(),2);QVERIFY(omitted.contains(QString::fromUtf8(u8"数据位")));QVERIFY(omitted.contains(QString::fromUtf8(u8"辅助值")));
    QVERIFY(!omitted.contains(QString::fromUtf8(u8"地址")));
    QCOMPARE(mapping.value("parameters").toObject().value("positionUm").toObject().value("value").toInt(),1234);
    button(widget,"saveConfig")->click();Widget restored(nullptr,path);
    QCOMPARE(mappingRows(restored).last().toObject(),mapping);
    restored.findChild<QLineEdit *>("customerInput")->setText("/1NR\\r");button(restored,"previewButton")->click();
    QVERIFY(restored.findChild<QTextEdit *>("targetOutput")->toPlainText().contains("11 41 00 03 E8 00 04 D2"));
    restored.findChild<QLineEdit *>("customerInput")->setText("/NR\\r");button(restored,"previewButton")->click();
    QVERIFY(restored.findChild<QTextEdit *>("targetOutput")->toPlainText().isEmpty());
}

void WidgetTest::noParameterSdkSourceRejectsOmittedReferences()
{
    Widget widget(nullptr,freshDraftPath());auto *op=widget.findChild<QComboBox *>("mappingOperation");
    op->setCurrentIndex(op->findData("QUSHENG_81"));widget.findChild<QLineEdit *>("mappingCode")->setText("FAN");
    auto *parameters=widget.findChild<QTableWidget *>("mappingParameters");const int row=parameterRow(parameters,"percent");QVERIFY(row>=0);
    auto *source=qobject_cast<QComboBox *>(parameters->cellWidget(row,1));source->setCurrentIndex(source->findData(QString::fromUtf8(u8"位置")));
    auto *inputSource=widget.findChild<QComboBox *>("mappingSource");inputSource->setCurrentIndex(inputSource->findData("@none"));
    const int count=mappingRows(widget).size();button(widget,"addMapping")->click();QCOMPARE(mappingRows(widget).size(),count);
    QVERIFY(widget.findChild<QLabel *>("validationState")->text().contains(QString::fromUtf8(u8"被省略的字段")));
    source->setCurrentIndex(source->findData("@constant"));qobject_cast<QLineEdit *>(parameters->cellWidget(row,2))->setText("50");
    button(widget,"addMapping")->click();QCOMPARE(mappingRows(widget).size(),count+1);button(widget,"saveConfig")->click();
    widget.findChild<QLineEdit *>("customerInput")->setText("/1FANR\\r");button(widget,"previewButton")->click();
    QVERIFY(widget.findChild<QTextEdit *>("targetOutput")->toPlainText().contains("00 81"));
    QVERIFY(widget.findChild<QTextEdit *>("previewOutput")->toPlainText().contains("50"));
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
    const QString path=freshDraftPath();Widget widget(nullptr,path);QJsonObject root=draftRoot(widget);QJsonArray rows=root.value("mappings").toArray();
    const QJsonObject mapping{{"commandCode","S"},{"operation","MOTOR_STOP"},{"parameters",QJsonObject()},
        {"input",QJsonObject{{"omitFields",QJsonArray()},{"note","explicit"}}}};
    rows.append(mapping);root.insert("mappings",rows);applyRoot(widget,root);
    QCOMPARE(mappingRows(widget).last().toObject(),mapping);
    widget.findChild<QLineEdit *>("customerInput")->setText("/1S0R\\r");button(widget,"previewButton")->click();
    QVERIFY(widget.findChild<QTextEdit *>("targetOutput")->toPlainText().contains("11 42"));
    widget.findChild<QLineEdit *>("mappingCode")->setText("T");auto *op=widget.findChild<QComboBox *>("mappingOperation");
    op->setCurrentIndex(op->findData("MOTOR_STOP"));auto *source=widget.findChild<QComboBox *>("mappingSource");
    source->setCurrentIndex(source->findData("@fields"));button(widget,"addMapping")->click();button(widget,"saveConfig")->click();
    const QJsonObject added=mappingRows(widget).last().toObject();
    QVERIFY(added.value("input").toObject().contains("omitFields"));QVERIFY(added.value("input").toObject().value("omitFields").toArray().isEmpty());
    Widget restored(nullptr,path);QCOMPARE(mappingRows(restored).last().toObject(),added);
    restored.findChild<QLineEdit *>("customerInput")->setText("/1T0R\\r");button(restored,"previewButton")->click();
    QVERIFY(restored.findChild<QTextEdit *>("targetOutput")->toPlainText().contains("11 42"));
    restored.findChild<QLineEdit *>("customerInput")->setText("/1TR\\r");button(restored,"previewButton")->click();
    QVERIFY(restored.findChild<QTextEdit *>("targetOutput")->toPlainText().isEmpty());
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
    for(const char *oldName:{"saveFields","saveMappings","saveDevice","validateConfig","enableConfig"})
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

void WidgetTest::configButtonsImportAndExport()
{
    const QString path=freshDraftPath(),exportPath=freshDraftPath();Widget widget(nullptr,path);widget.show();
    auto *save=button(widget,"saveConfig");auto *import=button(widget,"importConfig");auto *exportButton=button(widget,"exportConfig");
    QVERIFY(save && import && exportButton);QCoreApplication::processEvents();
    QCOMPARE(save->size(),import->size());QCOMPARE(import->size(),exportButton->size());
    QCOMPARE(import->text(),QString::fromUtf8(u8"导入配置"));QCOMPARE(exportButton->text(),QString::fromUtf8(u8"导出配置"));
    QVERIFY(!widget.findChild<QWidget *>("configMenuButton"));
    for(const char *name:{"validateConfig","enableConfig"}) QVERIFY(!widget.findChild<QAction *>(name));
    auto fileAction=[&widget](const char *name,const QString &filePath) {
        bool seen=false;
        QTimer::singleShot(0,&widget,[&widget,&seen,filePath]() {
            auto *dialog=widget.findChild<QFileDialog *>();if(!dialog) return;
            seen=true;dialog->selectFile(filePath);QMetaObject::invokeMethod(dialog,"accept",Qt::QueuedConnection);
        });
        button(widget,name)->click();return seen;
    };
    addNewMapping(widget,"Z","MOTOR_RESET");button(widget,"saveConfig")->click();
    const QJsonObject exported=draftRoot(widget);
    const QString savedActive=widget.findChild<QLabel *>("activeVersion")->text();
    QVERIFY(fileAction("exportConfig",exportPath));QCOMPARE(QJsonDocument::fromJson(draftBytes(exportPath)).object(),exported);
    widget.findChild<QLineEdit *>("formatHeader")->setText("!");button(widget,"saveConfig")->click();
    QVERIFY(widget.findChild<QLabel *>("activeVersion")->text()!=savedActive);
    QVERIFY(fileAction("importConfig",exportPath));QCOMPARE(draftRoot(widget),exported);
    Widget restored(nullptr,path);QCOMPARE(draftRoot(restored),exported);
    QCOMPARE(widget.findChild<QLabel *>("activeVersion")->text(),savedActive);
    QCOMPARE(restored.findChild<QLabel *>("activeVersion")->text(),savedActive);
    const QString renderDir=qEnvironmentVariable("TRANSLATION_RENDER_DIR");
    if(!renderDir.isEmpty()) {
        QDir().mkpath(renderDir);widget.findChild<QTabWidget *>("mainTabs")->setCurrentIndex(1);
        QCoreApplication::processEvents();QVERIFY(widget.grab().save(QDir(renderDir).filePath("config-buttons.png")));
    }
}

void WidgetTest::savedConfigActivatesAndInvalidConfigDoesNot()
{
    const QString path=freshDraftPath();Widget widget(nullptr,path);
    QSignalSpy frames(widget.findChild<RequestManager *>(),&RequestManager::frameReady);
    auto *active=widget.findChild<QLabel *>("activeVersion");
    button(widget,"saveConfig")->click();
    QVERIFY(!active->text().contains(QString::fromUtf8(u8"无")));
    const QString before=active->text();
    widget.findChild<QSpinBox *>("defaultMotorSpeed")->setValue(2500);
    QCOMPARE(active->text(),before);button(widget,"saveConfig")->click();
    QVERIFY(active->text()!=before);
    QJsonObject valid=draftRoot(widget),invalid=valid;QJsonObject device=invalid.value("device").toObject();
    device.insert("destination",device.value("source"));invalid.insert("device",device);
    auto *json=widget.findChild<QTextEdit *>("advancedJson");
    json->setPlainText(QString::fromUtf8(QJsonDocument(invalid).toJson()));button(widget,"saveConfig")->click();
    QCOMPARE(QJsonDocument::fromJson(draftBytes(path)).object(),invalid);
    QCOMPARE(active->text(),QString::fromUtf8(u8"启用：无"));
    QVERIFY(widget.findChild<QLabel *>("validationState")->text().contains(QString::fromUtf8(u8"配置有误")));
    Widget restored(nullptr,path);QCOMPARE(restored.findChild<QLabel *>("activeVersion")->text(),active->text());
    QVERIFY(restored.findChild<QLabel *>("validationState")->text().contains(QString::fromUtf8(u8"配置有误")));
    // 修复后只需保存，无需手动执行任何校验或启用操作。
    json->setPlainText(QString::fromUtf8(QJsonDocument(valid).toJson()));button(widget,"saveConfig")->click();
    ProtocolConfig expected;TranslationError error;
    QVERIFY(ProtocolConfig::fromJson(QJsonDocument(valid).toJson(),&expected,&error));
    QCOMPARE(active->text(),QString::fromUtf8(u8"启用：%1").arg(expected.version.left(12)));
    QVERIFY(widget.findChild<QLabel *>("validationState")->text().isEmpty());
    QVERIFY(!widget.findChild<SerialTransport *>()->isConnected());QCOMPARE(frames.count(),0);
}

void WidgetTest::saveKeepsPendingReplyAndSequence()
{
    const QString path=freshDraftPath();Widget widget(nullptr,path);
    auto *protocol=widget.findChild<QushengProtocol *>();QVERIFY(protocol);
    UnifiedCommand command;command.operation="MOTOR_FORWARD";command.requestId=42;command.targetDevice="17";
    command.parameters={{"speedUmPerS",1000},{"positionUm",100}};
    QByteArray first;TranslationError error;QVERIFY(protocol->encodeCommand(command,&first,&error));
    QCOMPARE(quint8(first.at(2)),quint8(2));QCOMPARE(quint8(first.at(3)),quint8(1));
    const QByteArray reply=QushengProtocol::makeReply(quint8(first.at(6)),0);
    QList<TranslationError> errors;QVERIFY(protocol->feedReceivedData(reply.left(5),&errors).isEmpty());
    QJsonObject root=draftRoot(widget),device=root.value("device").toObject();
    device.insert("destination",9);device.insert("source",8);root.insert("device",device);
    widget.findChild<QTextEdit *>("advancedJson")->setPlainText(QString::fromUtf8(QJsonDocument(root).toJson()));
    button(widget,"saveConfig")->click();
    const QList<ProtocolMessage> messages=protocol->feedReceivedData(reply.mid(5),&errors);
    QVERIFY(errors.isEmpty());QCOMPARE(messages.size(),1);QVERIFY(protocol->matchesReply(command,messages.first()));
    QByteArray next;command.requestId=43;QVERIFY(protocol->encodeCommand(command,&next,&error));
    QCOMPARE(quint8(next.at(2)),quint8(9));QCOMPARE(quint8(next.at(3)),quint8(8));
    QCOMPARE(quint8(next.at(6)),quint8(quint8(first.at(6))+1));
    Widget restored(nullptr,path);QByteArray afterRestart;
    QVERIFY(restored.findChild<QushengProtocol *>()->encodeCommand(command,&afterRestart,&error));
    QCOMPARE(quint8(afterRestart.at(2)),quint8(9));QCOMPARE(quint8(afterRestart.at(3)),quint8(8));
}

void WidgetTest::stopResetMappingsHaveNoDistanceControls()
{
    Widget widget(nullptr,freshDraftPath());auto *op=widget.findChild<QComboBox *>("mappingOperation");
    for(const QString &action:{QString("MOTOR_STOP"),QString("MOTOR_RESET")}) {
        op->setCurrentIndex(op->findData(action));QVERIFY(!widget.findChild<QComboBox *>("mappingSource")->isHidden());
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



static QJsonObject listExample()
{
    const auto source = QFileInfo(QDir(QString::fromLatin1(QT_TESTCASE_BUILDDIR)).absoluteFilePath(QString::fromLatin1(__FILE__))).dir();
    QFile file(source.filePath("../examples/integer-list-parameters.json"));
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

static int parameterChoice(QComboBox *box, const QString &field, int itemIndex)
{
    for (int i = 0; i < box->count(); ++i)
        if (box->itemData(i).toString() == field && box->itemData(i, Qt::UserRole + 1).toBool()
                && box->itemData(i, Qt::UserRole + 2).toInt() == itemIndex) return i;
    return -1;
}

void WidgetTest::directParameterSourcesAndMultipleFields()
{
    Widget widget(nullptr, freshDraftPath()); auto root = listExample();
    auto external = root.value("external").toObject(); const auto original = external.value("fields").toArray();
    auto left = original[1].toObject(); left.insert("name", "left"); left.insert("lengthMode", "delimited"); left.insert("delimiter", ";");
    auto right = left; right.insert("name", "right");
    auto scalar = original[1].toObject(); scalar.insert("name", "scalar"); scalar.insert("type", "int");
    external.insert("fields", QJsonArray{original[0], left, right, scalar}); root.insert("external", external); root.insert("mappings", QJsonArray());
    applyRoot(widget, root);
    auto *operation = widget.findChild<QComboBox *>("mappingOperation");
    auto *source = widget.findChild<QComboBox *>("mappingSource");
    auto *counts = widget.findChild<QTableWidget *>("mappingParameterCounts"); QCOMPARE(counts->rowCount(), 2);
    const auto setCounts = [counts]() {
        qobject_cast<QSpinBox *>(counts->cellWidget(0, 1))->setValue(3);
        qobject_cast<QSpinBox *>(counts->cellWidget(1, 1))->setValue(3);
    };
    setCounts();
    const int leftSecond = parameterChoice(source, "left", 2), rightSecond = parameterChoice(source, "right", 2);
    QVERIFY(leftSecond >= 0); QVERIFY(rightSecond >= 0); QVERIFY(leftSecond != rightSecond);
    QVERIFY(source->itemText(leftSecond).contains("left")); QVERIFY(source->itemText(rightSecond).contains("right"));
    source->setCurrentIndex(leftSecond); QCOMPARE(source->currentData(Qt::UserRole + 2).toInt(), 2);
    source->setCurrentIndex(parameterChoice(source, "left", 3)); QCOMPARE(source->currentData(Qt::UserRole + 2).toInt(), 3);
    source->setCurrentIndex(parameterChoice(source, "right", 2));
    widget.findChild<QLineEdit *>("mappingCode")->setText("RIGHT"); button(widget, "addMapping")->click();
    auto relation = mappingRows(widget).last().toObject().value("parameters").toObject().value("positionUm").toObject();
    QCOMPARE(relation.value("field").toString(), QString("right")); QCOMPARE(relation.value("itemIndex").toInt(), 2);
    setCounts(); source->setCurrentIndex(parameterChoice(source, "left", 2));
    source->setCurrentIndex(source->findData("@constant")); widget.findChild<QLineEdit *>("mappingValue")->setText("0");
    widget.findChild<QLineEdit *>("mappingCode")->setText("FIXED"); button(widget, "addMapping")->click();
    relation = mappingRows(widget).last().toObject().value("parameters").toObject().value("positionUm").toObject();
    QCOMPARE(relation.value("kind").toString(), QString("constant")); QVERIFY(!relation.contains("itemIndex"));
    setCounts(); source->setCurrentIndex(parameterChoice(source, "left", 2)); source->setCurrentIndex(source->findData("scalar"));
    widget.findChild<QLineEdit *>("mappingCode")->setText("SCALAR"); button(widget, "addMapping")->click();
    relation = mappingRows(widget).last().toObject().value("parameters").toObject().value("positionUm").toObject();
    QCOMPARE(relation.value("field").toString(), QString("scalar")); QVERIFY(!relation.contains("itemIndex"));
    operation->setCurrentIndex(operation->findData("QUSHENG_C1"));
    setCounts();
    auto *params = widget.findChild<QTableWidget *>("mappingParameters"); QCOMPARE(params->columnCount(), 3);
    auto *x = qobject_cast<QComboBox *>(params->cellWidget(parameterRow(params, "xPosition"), 1));
    auto *y = qobject_cast<QComboBox *>(params->cellWidget(parameterRow(params, "yPosition"), 1));
    auto *z = qobject_cast<QComboBox *>(params->cellWidget(parameterRow(params, "zPosition"), 1));
    x->setCurrentIndex(parameterChoice(x, "left", 2)); y->setCurrentIndex(parameterChoice(y, "right", 2)); z->setCurrentIndex(z->findData("scalar"));
    const int module = parameterRow(params, "moduleIndex"); auto *moduleSource = qobject_cast<QComboBox *>(params->cellWidget(module, 1));
    moduleSource->setCurrentIndex(moduleSource->findData("@constant")); qobject_cast<QLineEdit *>(params->cellWidget(module, 2))->setText("35");
    auto *fields = widget.findChild<QTableWidget *>("fieldTable");
    fields->item(1, 1)->setText("alpha"); fields->item(2, 1)->setText("beta");
    QCOMPARE(x->currentData().toString(), QString("alpha")); QCOMPARE(y->currentData().toString(), QString("beta"));
    QCOMPARE(x->currentData(Qt::UserRole + 2).toInt(), 2); QCOMPARE(y->currentData(Qt::UserRole + 2).toInt(), 2);
    auto *type = qobject_cast<QComboBox *>(fields->cellWidget(1, 3)); type->setCurrentIndex(type->findData("int"));
    QVERIFY(x->currentText().contains(QString::fromUtf8(u8"类型已变化"))); QCOMPARE(y->currentData().toString(), QString("beta"));
    QCOMPARE(y->currentData(Qt::UserRole + 2).toInt(), 2);
    type->setCurrentIndex(type->findData("integerList")); QVERIFY(!x->currentText().contains(QString::fromUtf8(u8"类型已变化")));
    QCOMPARE(x->currentData(Qt::UserRole + 2).toInt(), 2);
    widget.findChild<QLineEdit *>("mappingCode")->setText("MULTI"); button(widget, "addMapping")->click();
    QCOMPARE(mappingRows(widget).size(), 4); button(widget, "saveConfig")->click();
    widget.findChild<QLineEdit *>("customerInput")->setText("!MULTI=10,20,30;4,5,6;7\\r"); button(widget, "previewButton")->click();
    const auto frame = QByteArray::fromHex(widget.findChild<QTextEdit *>("targetOutput")->toPlainText().toLatin1());
    QCOMPARE(frame.mid(8, 11), QByteArray::fromHex("0123000014000005000007"));
    // SDK scalar/fixed selections also drop the previous list index.
    operation->setCurrentIndex(operation->findData("QUSHENG_C1")); setCounts();
    x = qobject_cast<QComboBox *>(params->cellWidget(parameterRow(params, "xPosition"), 1));
    y = qobject_cast<QComboBox *>(params->cellWidget(parameterRow(params, "yPosition"), 1));
    z = qobject_cast<QComboBox *>(params->cellWidget(parameterRow(params, "zPosition"), 1));
    x->setCurrentIndex(parameterChoice(x, "alpha", 2)); y->setCurrentIndex(parameterChoice(y, "beta", 2));
    z->setCurrentIndex(z->findData("scalar"));
    moduleSource = qobject_cast<QComboBox *>(params->cellWidget(module, 1));
    moduleSource->setCurrentIndex(moduleSource->findData("@constant")); qobject_cast<QLineEdit *>(params->cellWidget(module, 2))->setText("35");
    x->setCurrentIndex(x->findData("@constant")); qobject_cast<QLineEdit *>(params->cellWidget(parameterRow(params, "xPosition"), 2))->setText("0");
    y->setCurrentIndex(y->findData("scalar")); widget.findChild<QLineEdit *>("mappingCode")->setText("PLAIN"); button(widget, "addMapping")->click();
    const auto plain = mappingRows(widget).last().toObject().value("parameters").toObject();
    QVERIFY(!plain.value("xPosition").toObject().contains("itemIndex")); QVERIFY(!plain.value("yPosition").toObject().contains("itemIndex"));
    setCounts(); source->setCurrentIndex(parameterChoice(source, "alpha", 3)); fields->selectRow(1); button(widget, "removeField")->click();
    QVERIFY(source->currentText().contains(QString::fromUtf8(u8"来源不可用"))); QCOMPARE(source->currentData(Qt::UserRole + 2).toInt(), 3);
}

void WidgetTest::sixMotorDraftRecoveryAndImport()
{
    const QString path = freshDraftPath(); auto original = listExample();
    auto device = original.value("device").toObject(); device.insert("devId", 0x1c); original.insert("device", device);
    QFile old(path); QVERIFY(old.open(QIODevice::WriteOnly)); old.write(QJsonDocument(original).toJson()); old.close();
    const auto bytes = draftBytes(path); Widget widget(nullptr, path);
    QCOMPARE(draftBytes(path), bytes); QCOMPARE(draftRoot(widget), original);
    auto *id = widget.findChild<QSpinBox *>("defaultMotorId"); QVERIFY(id); QCOMPARE(id->maximum(), 0x16);
    QVERIFY(id->text().contains(QString::fromUtf8(u8"需重选"))); QVERIFY(id->text().contains("0x1C"));
    QVERIFY(widget.findChild<QLabel *>("validationState")->text().contains(QString::fromUtf8(u8"重新选择")));
    auto *input = widget.findChild<QLineEdit *>("customerInput"); input->setText("!XYZ=20,30,40\\r");
    widget.findChild<QTableWidget *>("fieldTable")->item(1, 7)->setText("0"); // Force the unsaved-form path too.
    button(widget, "parseFormatButton")->click();
    QVERIFY(widget.findChild<QTextEdit *>("previewOutput")->toPlainText().contains("values = [20, 30, 40]"));
    button(widget, "saveConfig")->click(); QCOMPARE(draftBytes(path), bytes);
    for (const char *name : {"previewButton", "sendButton", "queryButton"}) {
        button(widget, name)->click();
        QVERIFY(widget.findChild<QTextEdit *>("targetOutput")->toPlainText().isEmpty());
        QVERIFY(widget.findChild<QTextEdit *>("previewOutput")->toPlainText().contains(QString::fromUtf8(u8"物理电机编号无效")));
        QCOMPARE(draftBytes(path), bytes);
    }
    id->setValue(0x16); button(widget, "saveConfig")->click();
    QCOMPARE(draftRoot(widget).value("device").toObject().value("devId").toInt(), 0x16);
    QVERIFY(!id->text().contains(QString::fromUtf8(u8"需重选"))); QCOMPARE(id->minimum(), 0x11);
    const auto saved = draftRoot(widget);
    const auto savedBytes = draftBytes(path);
    for (int kind = 0; kind < 3; ++kind) {
        auto invalid = listExample();
        if (kind == 0) invalid.insert("device", device);
        if (kind == 1) invalid.insert("sdkDefaults", QJsonObject{{"MOTOR_FORWARD", QJsonObject{{"devId", 0x17}}}});
        if (kind == 2) {
            auto maps = invalid.value("mappings").toArray(); auto mapping = maps[0].toObject(); auto params = mapping.value("parameters").toObject();
            params.insert("devId", QJsonObject{{"kind", "constant"}, {"value", 0x1c}}); mapping.insert("parameters", params); maps[0] = mapping; invalid.insert("mappings", maps);
        }
        const QString sourcePath = freshDraftPath(); QFile source(sourcePath); QVERIFY(source.open(QIODevice::WriteOnly)); source.write(QJsonDocument(invalid).toJson()); source.close();
        const auto sourceBytes = draftBytes(sourcePath); bool seen = false;
        QTimer::singleShot(0, &widget, [&widget, &seen, sourcePath]() {
            auto *dialog = widget.findChild<QFileDialog *>(); if (!dialog) return;
            seen = true; dialog->selectFile(sourcePath); QMetaObject::invokeMethod(dialog, "accept", Qt::QueuedConnection);
        });
        button(widget, "importConfig")->click(); QVERIFY(seen);
        QCOMPARE(draftRoot(widget), saved); QCOMPARE(draftBytes(path), savedBytes); QCOMPARE(draftBytes(sourcePath), sourceBytes);
        QVERIFY(widget.findChild<QLabel *>("validationState")->text().contains(QString::fromUtf8(u8"导入失败")));
    }
}

void WidgetTest::newMappingListGuidance()
{
    Widget widget(nullptr, freshDraftPath()); applyRoot(widget, listExample()); widget.resize(1100, 740); widget.show();
    widget.findChild<QTabWidget *>("mainTabs")->setCurrentIndex(1);
    auto *operation = widget.findChild<QComboBox *>("mappingOperation"); operation->setCurrentIndex(operation->findData("MOTOR_FORWARD"));
    auto *source = widget.findChild<QComboBox *>("mappingSource"); source->setCurrentIndex(0);
    QVERIFY(!widget.findChild<QSpinBox *>("mappingItemIndex"));
    auto *counts = widget.findChild<QTableWidget *>("mappingParameterCounts");
    qobject_cast<QSpinBox *>(counts->cellWidget(0, 1))->setValue(3);
    auto *guidance = widget.findChild<QLabel *>("mappingListGuidance"); QVERIFY(guidance);
    QVERIFY(guidance->text().contains(QString::fromUtf8(u8"values需3项")));
    QVERIFY(guidance->text().contains(QString::fromUtf8(u8"解析格式")));
    QVERIFY(guidance->text().contains(QString::fromUtf8(u8"第几个参数")));
    QCOMPARE(source->currentData().toString(), QString());
    for (int item = 1; item <= 3; ++item) QVERIFY(parameterChoice(source, "values", item) >= 0);
    QVERIFY(widget.findChild<QLabel *>("newMappingHint")->text().contains(QString::fromUtf8(u8"不会更新上方")));
    auto *motors = widget.findChild<QComboBox *>("mappingMotor"); QCOMPARE(motors->count(), 7);
    QCOMPARE(motors->findData(0x17), -1); QCOMPARE(motors->findData(0x1c), -1); QVERIFY(motors->findData(0x16) >= 0);
    const auto existing = mappingRows(widget);
    const QString render = qEnvironmentVariable("TRANSLATION_RENDER_DIR");
    if (!render.isEmpty()) {
        QDir().mkpath(render); QCoreApplication::processEvents();
        QVERIFY(widget.grab().save(QDir(render).filePath("list-three-no-source.png")));
    }
    source->setCurrentIndex(parameterChoice(source, "values", 2));
    qobject_cast<QSpinBox *>(counts->cellWidget(0, 1))->setValue(2048);
    QVERIFY(parameterChoice(source, "values", 2048) >= 0);
    QCOMPARE(source->currentData(Qt::UserRole + 2).toInt(), 2);
    qobject_cast<QSpinBox *>(counts->cellWidget(0, 1))->setValue(3);
    QVERIFY(guidance->text().contains(QString::fromUtf8(u8"目标位置取values第2项")));
    if (!render.isEmpty()) {
        QCoreApplication::processEvents(); QVERIFY(widget.grab().save(QDir(render).filePath("list-three-second-item.png")));
    }
    qobject_cast<QSpinBox *>(counts->cellWidget(0, 1))->setValue(1);
    QCOMPARE(source->currentData(Qt::UserRole + 2).toInt(), 2);
    QVERIFY(source->currentText().contains(QString::fromUtf8(u8"超出输入项数"))); QVERIFY(guidance->text().contains(QString::fromUtf8(u8"超出输入项数")));
    widget.findChild<QLineEdit *>("mappingCode")->setText("NEW"); button(widget, "addMapping")->click(); QCOMPARE(mappingRows(widget), existing);
    qobject_cast<QSpinBox *>(counts->cellWidget(0, 1))->setValue(3);
    button(widget, "saveConfig")->click(); QCOMPARE(mappingRows(widget), existing); // A new form never edits existing mappings.
    button(widget, "addMapping")->click(); QCOMPARE(mappingRows(widget).size(), existing.size() + 1);
    button(widget, "saveConfig")->click(); widget.findChild<QLineEdit *>("customerInput")->setText("!NEW=20,30,40\\r");
    button(widget, "previewButton")->click();
    QVERIFY(widget.findChild<QTextEdit *>("previewOutput")->toPlainText().contains(QString::fromUtf8(u8"第 2 项")));
    const auto frame = QByteArray::fromHex(widget.findChild<QTextEdit *>("targetOutput")->toPlainText().toLatin1());
    QCOMPARE(frame.mid(13, 3), QByteArray::fromHex("00001e"));
}

void WidgetTest::integerListMappingLifecycle()
{
    const QString path = freshDraftPath(), exported = freshDraftPath();
    Widget widget(nullptr, path); widget.resize(1100, 740); widget.show();
    auto root = listExample(); QVERIFY(!root.isEmpty()); applyRoot(widget, root);
    auto *fields = widget.findChild<QTableWidget *>("fieldTable"); QCOMPARE(fields->columnCount(), 9);
    QCOMPARE(qobject_cast<QComboBox *>(fields->cellWidget(1, 3))->currentData().toString(), QString("integerList"));
    auto *operation = widget.findChild<QComboBox *>("mappingOperation"); operation->setCurrentIndex(operation->findData("QUSHENG_C1"));
    auto *params = widget.findChild<QTableWidget *>("mappingParameters");
    QCOMPARE(params->columnCount(), 3);
    QVERIFY(!widget.findChild<QSpinBox *>("mappingItemIndex_xPosition"));
    auto *counts = widget.findChild<QTableWidget *>("mappingParameterCounts"); QCOMPARE(counts->rowCount(), 1);
    qobject_cast<QSpinBox *>(counts->cellWidget(0, 1))->setValue(3);
    for (const auto &entry : {qMakePair(QString("xPosition"), 1), qMakePair(QString("yPosition"), 2), qMakePair(QString("zPosition"), 3)}) {
        const int row = parameterRow(params, entry.first); QVERIFY(row >= 0);
        auto *source = qobject_cast<QComboBox *>(params->cellWidget(row, 1)); source->setCurrentIndex(parameterChoice(source, "values", entry.second));
        QVERIFY(source->currentText().contains(QString::number(entry.second)));
    }
    const int module = parameterRow(params, "moduleIndex");
    auto *moduleSource = qobject_cast<QComboBox *>(params->cellWidget(module, 1)); moduleSource->setCurrentIndex(moduleSource->findData("@constant"));
    qobject_cast<QLineEdit *>(params->cellWidget(module, 2))->setText("35");
    widget.findChild<QLineEdit *>("mappingCode")->setText("TEST");
    auto *zSource = qobject_cast<QComboBox *>(params->cellWidget(parameterRow(params, "zPosition"), 1));
    qobject_cast<QSpinBox *>(counts->cellWidget(0, 1))->setValue(2);
    QCOMPARE(zSource->currentData(Qt::UserRole + 2).toInt(), 3);
    QVERIFY(zSource->currentText().contains(QString::fromUtf8(u8"超出输入项数")));
    button(widget, "addMapping")->click(); QCOMPARE(mappingRows(widget).size(), 3);
    qobject_cast<QSpinBox *>(counts->cellWidget(0, 1))->setValue(3);
    QCOMPARE(zSource->currentText(), QString::fromUtf8(u8"第3个参数"));
    auto *tabs = widget.findChild<QTabWidget *>("mainTabs");
    const QString render = qEnvironmentVariable("TRANSLATION_RENDER_DIR");
    if (!render.isEmpty()) {
        QDir().mkpath(render); tabs->setCurrentIndex(1); QCoreApplication::processEvents();
        widget.findChild<QLabel *>("validationState")->clear();
        QVERIFY(widget.grab().save(QDir(render).filePath("list-mapping-xyz.png")));
        auto *xSource = qobject_cast<QComboBox *>(params->cellWidget(parameterRow(params, "xPosition"), 1));
        xSource->showPopup(); QCoreApplication::processEvents();
        QVERIFY(xSource->view()->window()->grab().save(QDir(render).filePath("direct-source-options.png")));
        xSource->hidePopup();
        tabs->setCurrentIndex(0); QCoreApplication::processEvents();
        QVERIFY(widget.grab().save(QDir(render).filePath("list-format.png")));
    }
    button(widget, "addMapping")->click(); QCOMPARE(mappingRows(widget).size(), 4);
    button(widget, "saveConfig")->click(); const auto saved = draftRoot(widget);
    for (int i = 0; i < 3; ++i) QCOMPARE(saved.value("mappings").toArray()[i], root.value("mappings").toArray()[i]);
    const auto mapping = saved.value("mappings").toArray().last().toObject();
    QCOMPARE(mapping.value("input").toObject().value("parameterCounts").toObject().value("values").toInt(), 3);
    QCOMPARE(mapping.value("parameters").toObject().value("zPosition").toObject().value("itemIndex").toInt(), 3);
    Widget restored(nullptr, path); QCOMPARE(draftRoot(restored), saved);
    auto fileAction = [&restored](const char *name, const QString &filePath) {
        bool seen = false;
        QTimer::singleShot(0, &restored, [&restored, &seen, filePath]() {
            auto *dialog = restored.findChild<QFileDialog *>(); if (!dialog) return;
            seen = true; dialog->selectFile(filePath); QMetaObject::invokeMethod(dialog, "accept", Qt::QueuedConnection);
        });
        button(restored, name)->click(); return seen;
    };
    QVERIFY(fileAction("exportConfig", exported)); QVERIFY(fileAction("importConfig", exported)); QCOMPARE(draftRoot(restored), saved);
    auto *input = widget.findChild<QLineEdit *>("customerInput"); input->setText("!TEST=20,200,3000\\r");
    button(widget, "previewButton")->click();
    QVERIFY(!widget.findChild<QTextEdit *>("targetOutput")->toPlainText().isEmpty());
    button(widget, "parseFormatButton")->click();
    const auto output = widget.findChild<QTextEdit *>("previewOutput")->toPlainText();
    QVERIFY(output.contains("values = [20, 200, 3000]")); QVERIFY(output.contains(QString::fromUtf8(u8"第 3 项 = 3000")));
    QVERIFY(widget.findChild<QTextEdit *>("targetOutput")->toPlainText().isEmpty());
    if (!render.isEmpty()) {
        tabs->setCurrentIndex(2); QCoreApplication::processEvents();
        QVERIFY(widget.grab().save(QDir(render).filePath("list-parse.png")));
    }
    // Arbitrary item index, including the old motion relation editor.
    operation->setCurrentIndex(operation->findData("MOTOR_FORWARD"));
    auto *source = widget.findChild<QComboBox *>("mappingSource"); source->setCurrentIndex(source->findData("values"));
    qobject_cast<QSpinBox *>(counts->cellWidget(0, 1))->setValue(5);
    source->setCurrentIndex(parameterChoice(source, "values", 5));
    widget.findChild<QLineEdit *>("mappingMultiplier")->setText("2");
    widget.findChild<QLineEdit *>("mappingCode")->setText("FIVE"); button(widget, "addMapping")->click();
    const auto five = mappingRows(widget).last().toObject();
    QCOMPARE(five.value("parameters").toObject().value("positionUm").toObject().value("itemIndex").toInt(), 5);
    QCOMPARE(five.value("parameters").toObject().value("positionUm").toObject().value("kind").toString(), QString("linear"));
}

void WidgetTest::integerListFieldAdaptationAndDebugSend()
{
    Widget widget(nullptr, freshDraftPath()); applyRoot(widget, listExample());
    auto *input = widget.findChild<QLineEdit *>("customerInput");
    auto *target = widget.findChild<QTextEdit *>("targetOutput"); auto *preview = widget.findChild<QTextEdit *>("previewOutput");
    input->setText("!XYZ=20,200,3000\\r"); button(widget, "previewButton")->click(); QVERIFY(!target->toPlainText().isEmpty());
    input->setText("!XYZ=20,200\\r"); button(widget, "sendButton")->click();
    QVERIFY(target->toPlainText().isEmpty()); QVERIFY2(preview->toPlainText().contains("PARAMETER_COUNT"), qPrintable(preview->toPlainText()));
    auto *operation = widget.findChild<QComboBox *>("mappingOperation"); operation->setCurrentIndex(operation->findData("MOTOR_FORWARD"));
    auto *source = widget.findChild<QComboBox *>("mappingSource"); source->setCurrentIndex(source->findData("values"));
    auto *counts = widget.findChild<QTableWidget *>("mappingParameterCounts"); qobject_cast<QSpinBox *>(counts->cellWidget(0, 1))->setValue(3);
    source->setCurrentIndex(parameterChoice(source, "values", 3));
    auto *fields = widget.findChild<QTableWidget *>("fieldTable"); fields->item(1, 1)->setText("renamed");
    QCOMPARE(source->currentData().toString(), QString("renamed")); QCOMPARE(source->currentData(Qt::UserRole + 2).toInt(), 3);
    button(widget, "saveConfig")->click(); const auto saved = draftRoot(widget);
    const auto xyz = saved.value("mappings").toArray()[1].toObject();
    QCOMPARE(xyz.value("input").toObject().value("parameterCounts").toObject().value("renamed").toInt(), 3);
    QCOMPARE(xyz.value("parameters").toObject().value("zPosition").toObject().value("field").toString(), QString("renamed"));
    QCOMPARE(xyz.value("parameters").toObject().value("zPosition").toObject().value("itemIndex").toInt(), 3);
    auto *type = qobject_cast<QComboBox *>(fields->cellWidget(1, 3)); type->setCurrentIndex(type->findData("int"));
    QVERIFY(source->currentText().contains(QString::fromUtf8(u8"类型已变化")));
    button(widget, "saveConfig")->click();
    QVERIFY(widget.findChild<QLabel *>("validationState")->text().contains(QString::fromUtf8(u8"失效"))
            || widget.findChild<QLabel *>("validationState")->text().contains(QString::fromUtf8(u8"有误")));
    QCOMPARE(draftRoot(widget).value("mappings").toArray()[1].toObject().value("parameters").toObject().value("zPosition").toObject().value("itemIndex").toInt(), 3);
    applyRoot(widget, saved); input->setText("!XYZ=1,2,3\\r"); button(widget, "previewButton")->click(); QVERIFY(!target->toPlainText().isEmpty());
}

void WidgetTest::xyzModuleMappingLifecycle()
{
    const QString path = freshDraftPath(), exported = freshDraftPath();
    Widget widget(nullptr, path);
    auto *operation = widget.findChild<QComboBox *>("mappingOperation");
    operation->setCurrentIndex(operation->findData("QUSHENG_C1"));
    QVERIFY(!operation->currentText().contains(QString::fromUtf8(u8"待实现")));
    auto *parameters = widget.findChild<QTableWidget *>("mappingParameters");
    QCOMPARE(parameters->rowCount(), 4); QVERIFY(widget.findChild<QComboBox *>("mappingMotor")->isHidden());
    const auto choose = [parameters](const char *key, const QString &source, const QString &value = QString()) {
        const int row = parameterRow(parameters, key); QVERIFY(row >= 0);
        auto *box = qobject_cast<QComboBox *>(parameters->cellWidget(row, 1));
        box->setCurrentIndex(box->findData(source));
        qobject_cast<QLineEdit *>(parameters->cellWidget(row, 2))->setText(value);
    };
    choose("moduleIndex", "@constant", "35");
    choose("xPosition", QString::fromUtf8(u8"位置"));
    choose("yPosition", "@constant", "0");
    choose("zPosition", QString::fromUtf8(u8"位置"));
    widget.findChild<QLineEdit *>("mappingCode")->setText("XYZ");
    const QString render = qEnvironmentVariable("TRANSLATION_RENDER_DIR");
    if (!render.isEmpty()) {
        widget.resize(1100, 740); widget.show(); widget.findChild<QTabWidget *>("mainTabs")->setCurrentIndex(1);
        QCoreApplication::processEvents(); QDir().mkpath(render);
        QVERIFY(widget.grab().save(QDir(render).filePath("mapping-xyz-fields.png")));
    }
    button(widget, "addMapping")->click(); button(widget, "saveConfig")->click();
    const QJsonObject saved = draftRoot(widget);
    const auto xyz = saved.value("mappings").toArray().last().toObject();
    QCOMPARE(xyz.value("operation").toString(), QString("QUSHENG_C1"));
    QCOMPARE(xyz.value("parameters").toObject().size(), 4);
    widget.findChild<QLineEdit *>("customerInput")->setText("/1XYZ2000R\\r");
    button(widget, "previewButton")->click();
    const QByteArray frame = QByteArray::fromHex(widget.findChild<QTextEdit *>("targetOutput")->toPlainText().toLatin1());
    QVERIFY(frame.size() >= 28);
    QCOMPARE(quint8(frame.at(7)), quint8(0xc1));
    QCOMPARE(frame.mid(8, 20), QByteArray::fromHex("01230007d00000000007d00003e80003e80003e8"));
    if (!render.isEmpty()) {
        widget.findChild<QTabWidget *>("mainTabs")->setCurrentIndex(2); QCoreApplication::processEvents();
        QVERIFY(widget.grab().save(QDir(render).filePath("mapping-xyz-preview.png")));
    }
    Widget restarted(nullptr, path); QCOMPARE(draftRoot(restarted), saved);
    const auto fileAction = [&restarted](const char *name, const QString &filePath) {
        bool seen = false;
        QTimer::singleShot(0, &restarted, [&restarted, &seen, filePath]() {
            auto *dialog = restarted.findChild<QFileDialog *>(); if (!dialog) return;
            seen = true; dialog->selectFile(filePath); QMetaObject::invokeMethod(dialog, "accept", Qt::QueuedConnection);
        });
        button(restarted, name)->click(); return seen;
    };
    QVERIFY(fileAction("exportConfig", exported));
    QVERIFY(fileAction("importConfig", exported)); QCOMPARE(draftRoot(restarted), saved);
    operation->setCurrentIndex(operation->findData("QUSHENG_C1"));
    choose("moduleIndex", "@constant", "256"); widget.findChild<QLineEdit *>("mappingCode")->setText("BAD");
    const int count = mappingRows(widget).size(); button(widget, "addMapping")->click();
    QCOMPARE(mappingRows(widget).size(), count);
    QVERIFY(widget.findChild<QLabel *>("validationState")->text().contains(QString::fromUtf8(u8"固定值")));
}

void WidgetTest::perMappingMotorLifecycle()
{
    const QString path = freshDraftPath();
    Widget widget(nullptr, path);
    const QJsonArray original = mappingRows(widget);
    auto *operation = widget.findChild<QComboBox *>("mappingOperation");
    auto *motor = widget.findChild<QComboBox *>("mappingMotor");
    QVERIFY(motor); QCOMPARE(motor->count(), 7); QCOMPARE(motor->currentData().toInt(), 0);
    const QStringList actions{"MOTOR_FORWARD", "MOTOR_STOP", "MOTOR_BACKWARD", "MOTOR_RESET"};
    for (int index = 0; index < actions.size(); ++index) {
        operation->setCurrentIndex(operation->findData(actions.at(index)));
        QVERIFY(!motor->isHidden());
        motor->setCurrentIndex(motor->findData(0x12 + index));
        addNewMapping(widget, "N" + QString::number(index), actions.at(index),
                      index == 0 || index == 2 ? "@constant" : "", index == 0 || index == 2 ? "2000" : "");
        const QJsonObject added = mappingRows(widget).last().toObject();
        QCOMPARE(added.value("parameters").toObject().value("devId").toObject(),
                 (QJsonObject{{"kind", "constant"}, {"value", 0x12 + index}}));
        QCOMPARE(motor->currentData().toInt(), 0); // New rows do not inherit the previous motor.
    }
    button(widget, "saveConfig")->click();
    const QJsonArray saved = mappingRows(widget);
    for (int index = 0; index < original.size(); ++index) QCOMPARE(saved.at(index), original.at(index));

    Widget restored(nullptr, path); QCOMPARE(mappingRows(restored), saved);
    const QString exported = freshDraftPath();
    const auto fileAction = [&restored](const char *name, const QString &filePath) {
        bool seen = false;
        QTimer::singleShot(0, &restored, [&restored, &seen, filePath]() {
            auto *dialog = restored.findChild<QFileDialog *>(); if (!dialog) return;
            seen = true; dialog->selectFile(filePath); QMetaObject::invokeMethod(dialog, "accept", Qt::QueuedConnection);
        });
        button(restored, name)->click(); return seen;
    };
    QVERIFY(fileAction("exportConfig", exported)); QVERIFY(fileAction("importConfig", exported));
    QCOMPARE(mappingRows(restored), saved);
    for (int index = 0; index < actions.size(); ++index) {
        restored.findChild<QLineEdit *>("customerInput")->setText(
            "/1N" + QString::number(index) + (index == 0 || index == 2 ? "2000R\\r" : "R\\r"));
        button(restored, "previewButton")->click();
        const QByteArray frame = QByteArray::fromHex(restored.findChild<QTextEdit *>("targetOutput")->toPlainText().toLatin1());
        QVERIFY2(frame.size() > 8, qPrintable(restored.findChild<QTextEdit *>("previewOutput")->toPlainText()));
        QCOMPARE(quint8(frame.at(8)), quint8(0x12 + index));
    }
    operation->setCurrentIndex(operation->findData("QUERY_MOTOR")); QVERIFY(motor->isHidden());
    operation->setCurrentIndex(operation->findData("MOTOR_FORWARD")); QCOMPARE(motor->currentData().toInt(), 0);
    const QString renderDir = qEnvironmentVariable("TRANSLATION_RENDER_DIR");
    if (!renderDir.isEmpty()) {
        widget.resize(1100, 740); widget.show();
        widget.findChild<QTabWidget *>("mainTabs")->setCurrentIndex(1);
        motor->setCurrentIndex(motor->findData(0x16));
        QCoreApplication::processEvents(); QDir().mkpath(renderDir);
        QVERIFY(widget.grab().save(QDir(renderDir).filePath("mapping-motor-selection.png")));
    }
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
    const QString active = widget.findChild<QLabel *>("activeVersion")->text();
    const QJsonObject oldDraft = draftRoot(widget);
    int queryRow = -1;
    for (int row = 0; row < maps->rowCount(); ++row) if (maps->item(row, 0)->text() == "?0") queryRow = row;
    QVERIFY(queryRow >= 0); maps->selectRow(queryRow);
    fields->item(2, 1)->setText(QString::fromUtf8(u8"数据参数"));
    QCOMPARE(draftRoot(widget), oldDraft); // 编辑表单尚未保存，不触碰草稿/已启用快照。
    QCOMPARE(widget.findChild<QLabel *>("activeVersion")->text(), active);
    QVERIFY(button(widget, "sendButton")->isEnabled());
    QVERIFY(widget.findChild<QComboBox *>("mappingSource")->findData(QString::fromUtf8(u8"数据参数"))>=0); button(widget, "saveConfig")->click();
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
    QVERIFY(!widget.findChild<QLabel *>("activeVersion")->text().contains(QString::fromUtf8(u8"无")));
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
    QVERIFY(widget.findChild<QLabel *>("validationState")->text().contains(QString::fromUtf8(u8"配置有误")));
    QCOMPARE(mappingRows(widget).first().toObject().value("parameters").toObject().value("positionUm").toObject().value("field").toString(),QString::fromUtf8(u8"位置"));
}



static QJsonObject sequentialExample()
{
    const auto source=QFileInfo(QDir(QString::fromLatin1(QT_TESTCASE_BUILDDIR)).absoluteFilePath(QString::fromLatin1(__FILE__))).dir();
    QFile file(source.filePath("../examples/delimited-single-parameter.json"));if(!file.open(QIODevice::ReadOnly))return QJsonObject();return QJsonDocument::fromJson(file.readAll()).object();
}
void WidgetTest::sequentialDelimiterEditorLifecycle()
{
    const QString path=freshDraftPath(),exportPath=freshDraftPath();Widget widget(nullptr,path);widget.show();auto root=sequentialExample();QVERIFY(!root.isEmpty());auto ext=root.value("external").toObject();ext.insert("extension","keep");auto rows=ext.value("fields").toArray();auto first=rows.at(0).toObject();first.insert("note","preserve");rows.replace(0,first);ext.insert("fields",rows);root.insert("external",ext);applyRoot(widget,root);
    QVERIFY(!widget.findChild<QComboBox *>("formatMode"));QVERIFY(!widget.findChild<QWidget *>("textFormatEditor"));QVERIFY(!widget.findChild<QLineEdit *>("formatSampleInput"));
    auto *table=widget.findChild<QTableWidget *>("fieldTable");QCOMPARE(table->columnCount(),9);QCOMPARE(table->rowCount(),10);QVERIFY(table->horizontalHeaderItem(5)->text().contains(QString::fromUtf8(u8"分隔符")));QCOMPARE(table->item(5,5)->text(),QString("="));
    auto *input=widget.findChild<QLineEdit *>("customerInput");input->setText("[AA|BB|CTL|255|0;SET=234;10\\nDONE|?|]\\r");button(widget,"parseFormatButton")->click();auto *output=widget.findChild<QTextEdit *>("previewOutput");QVERIFY2(output->toPlainText().contains(QString::fromUtf8(u8"数值 = 234")),qPrintable(output->toPlainText()));QVERIFY(output->toPlainText().contains(QString::fromUtf8(u8"未校验")));QVERIFY(widget.findChild<QTextEdit *>("targetOutput")->toPlainText().isEmpty());
    table->item(6,8)->setText("500");button(widget,"saveConfig")->click();const auto saved=draftRoot(widget);QCOMPARE(saved.value("external").toObject().value("extension").toString(),QString("keep"));QCOMPARE(saved.value("external").toObject().value("fields").toArray().at(0).toObject().value("note").toString(),QString("preserve"));
    Widget restored(nullptr,path);QCOMPARE(draftRoot(restored),saved);QCOMPARE(restored.findChild<QTableWidget *>("fieldTable")->item(7,5)->text(),QString("\\n"));
    // Both export and import accept a format-only sequential configuration.
    auto fileAction=[&widget](const char *name,const QString &filePath){bool seen=false;QTimer::singleShot(0,&widget,[&widget,&seen,filePath](){auto *dialog=widget.findChild<QFileDialog *>();if(!dialog)return;seen=true;dialog->selectFile(filePath);QMetaObject::invokeMethod(dialog,"accept",Qt::QueuedConnection);});button(widget,name)->click();return seen;};
    QVERIFY(fileAction("exportConfig",exportPath));QCOMPARE(QJsonDocument::fromJson(draftBytes(exportPath)).object(),saved);QVERIFY(fileAction("importConfig",exportPath));QCOMPARE(draftRoot(widget),saved);
    input->setText("[AA|BB|CTL|1|0;SET=1,2,3;10\\nDONE|?|]\\r");button(widget,"parseFormatButton")->click();QVERIFY(output->toPlainText().contains("FIELD_NUMBER"));QVERIFY(!output->toPlainText().contains(QString::fromUtf8(u8"数值 = 234")));
    input->setText("[AA|BB|CTL|1|0;SET=234;10\\nDONE|?|]\\r");button(widget,"parseFormatButton")->click();
    const QString render=qEnvironmentVariable("TRANSLATION_RENDER_DIR");if(!render.isEmpty()){QDir().mkpath(render);auto *tabs=widget.findChild<QTabWidget *>("mainTabs");tabs->setCurrentIndex(0);QCoreApplication::processEvents();QVERIFY(widget.grab().save(QDir(render).filePath("sequential-fields.png")));tabs->setCurrentIndex(2);QCoreApplication::processEvents();QVERIFY(widget.grab().save(QDir(render).filePath("sequential-parse.png")));}
}
void WidgetTest::retiredDraftRecoveryRequiresExplicitSave()
{
    const QString path=freshDraftPath();auto root=sequentialExample();auto external=root.value("external").toObject();external.insert("mode","textTemplate");const QJsonObject retired{{"template","{command}={arguments}"},{"extension","keep retired metadata"}};external.insert("textFormat",retired);root.insert("external",external);DraftStore store(path);TranslationError error;QVERIFY(store.save(root,&error));const auto bytes=draftBytes(path);
    Widget widget(nullptr,path);QCOMPARE(widget.findChild<QTableWidget *>("fieldTable")->rowCount(),10);QCOMPARE(draftBytes(path),bytes);widget.findChild<QLineEdit *>("customerInput")->setText("[AA|BB|CTL|1|0;SET=255;10\\nDONE|?|]\\r");
    for(const char *action:{"parseFormatButton","previewButton","sendButton"}){button(widget,action)->click();QVERIFY(widget.findChild<QTextEdit *>("previewOutput")->toPlainText().contains("UNSUPPORTED_FORMAT"));QVERIFY(widget.findChild<QTextEdit *>("targetOutput")->toPlainText().isEmpty());QCOMPARE(draftBytes(path),bytes);}
    button(widget,"saveConfig")->click();const auto recovered=QJsonDocument::fromJson(draftBytes(path)).object();QVERIFY(!recovered.value("external").toObject().contains("mode"));QVERIFY(!recovered.value("external").toObject().contains("textFormat"));QCOMPARE(recovered.value("retiredFormatBackup").toObject().value("textFormat").toObject(),retired);button(widget,"parseFormatButton")->click();QVERIFY(widget.findChild<QTextEdit *>("previewOutput")->toPlainText().contains(QString::fromUtf8(u8"解析成功")));
}
void WidgetTest::retiredImportDoesNotOverwriteDraft()
{
    const QString path=freshDraftPath(),importPath=freshDraftPath();Widget widget(nullptr,path);button(widget,"saveConfig")->click();const auto before=draftBytes(path);auto root=sequentialExample();auto external=root.value("external").toObject();external.insert("mode","textTemplate");external.insert("textFormat",QJsonObject{{"extension",42}});root.insert("external",external);DraftStore imported(importPath);TranslationError error;QVERIFY(imported.save(root,&error));const auto original=draftBytes(importPath);
    bool seen=false;QTimer::singleShot(0,&widget,[&widget,&seen,importPath](){auto *dialog=widget.findChild<QFileDialog *>();if(!dialog)return;seen=true;dialog->selectFile(importPath);QMetaObject::invokeMethod(dialog,"accept",Qt::QueuedConnection);});button(widget,"importConfig")->click();QVERIFY(seen);QCOMPARE(draftBytes(path),before);QCOMPARE(draftBytes(importPath),original);QVERIFY(widget.findChild<QLabel *>("validationState")->text().contains("UNSUPPORTED_FORMAT"));
}

void WidgetTest::renderChinesePages()
{
    QDir::setCurrent(QFileInfo(QDir(QString::fromLatin1(QT_TESTCASE_BUILDDIR)).absoluteFilePath(QString::fromLatin1(__FILE__))).dir().absoluteFilePath(".."));
    Widget widget(nullptr, freshDraftPath());
    widget.show();
    button(widget, "previewButton")->click();
    auto *tabs = widget.findChild<QTabWidget *>(); QVERIFY(tabs);
    const QStringList names = {QString::fromUtf8(u8"客户协议格式"), QString::fromUtf8(u8"命令映射"),
                              QString::fromUtf8(u8"报文调试"), QString::fromUtf8(u8"设备与连接")};
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
    operation->setCurrentIndex(operation->findData("QUSHENG_C2"));
    auto *mappingScroll = widget.findChild<QScrollArea *>("mappingEditorScroll"); QVERIFY(mappingScroll);
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
    ProtocolConfig restoredConfig;TranslationError restoredError;
    QVERIFY(ProtocolConfig::fromJson(QJsonDocument(saved).toJson(),&restoredConfig,&restoredError));
    QCOMPARE(second.findChild<QLabel *>("activeVersion")->text(),QString::fromUtf8(u8"启用：%1").arg(restoredConfig.version.left(12)));
    QVERIFY(!second.findChild<QWidget *>("deviceConfirmed"));
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
        applyRoot(first, incomplete); QVERIFY(first.findChild<QLabel *>("validationState")->text().contains(QString::fromUtf8(u8"配置有误")));
    }
    Widget second(nullptr, path); QCOMPARE(draftRoot(second), incomplete);
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
    const QString activeBefore=widget.findChild<QLabel *>("activeVersion")->text();
    widget.findChild<QTableWidget *>("fieldTable")->item(2, 1)->setText(QString::fromUtf8(u8"数据位"));
    button(widget, "saveConfig")->click();
    QCOMPARE(draftRoot(widget), original);
    QVERIFY(widget.findChild<QLabel *>("draftVersion")->text().contains(QString::fromUtf8(u8"未保存")));
    QVERIFY(widget.findChild<QLabel *>("validationState")->text().contains(QString::fromUtf8(u8"保存失败")));
    QCOMPARE(widget.findChild<QLabel *>("activeVersion")->text(),activeBefore);
    QVERIFY(blocker.open(QIODevice::ReadOnly)); QCOMPARE(blocker.readAll(), QByteArray("keep")); blocker.close();
    QVERIFY(QFile::remove(blockerPath)); button(widget, "saveConfig")->click();
    DraftStore store(path); QJsonObject disk; TranslationError error;
    QVERIFY(store.load(&disk, &error)); QCOMPARE(disk, draftRoot(widget));
    QVERIFY(widget.findChild<QLabel *>("activeVersion")->text()!=activeBefore);
    QCOMPARE(disk.value("external").toObject().value("fields").toArray().at(2).toObject().value("name").toString(), QString::fromUtf8(u8"数据位"));
}

void WidgetTest::damagedLocalDraftIsNotOverwritten()
{
    const QString path = freshDraftPath(); QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly)); file.write("{broken-json"); file.close();
    {
        Widget widget(nullptr, path);
        QVERIFY(widget.findChild<QLabel *>("validationState")->text().contains(QString::fromUtf8(u8"恢复失败")));
        QCOMPARE(widget.findChild<QLabel *>("activeVersion")->text(),QString::fromUtf8(u8"启用：无"));
        QVERIFY(button(widget, "sendButton")->isEnabled());
    }
    QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), QByteArray("{broken-json"));
}


void WidgetTest::valveStateParameterEditorLifecycle()
{
    const QString path = freshDraftPath(); Widget widget(nullptr, path);
    const auto directory = QFileInfo(QDir(QString::fromLatin1(QT_TESTCASE_BUILDDIR)).absoluteFilePath(QString::fromLatin1(__FILE__))).dir();
    QFile file(directory.filePath("../examples/valve-selection.json")); QVERIFY(file.open(QIODevice::ReadOnly));
    auto root = QJsonDocument::fromJson(file.readAll()).object();
    const auto existing = root.value("mappings").toArray(); applyRoot(widget, root);
    auto *operation = widget.findChild<QComboBox *>("mappingOperation");
    auto *mode = widget.findChild<QComboBox *>("valveMode");
    auto *state = widget.findChild<QComboBox *>("valveStateSource"); QVERIFY(state);
    auto *parameters = widget.findChild<QTableWidget *>("mappingParameters");
    auto *steps = widget.findChild<QTableWidget *>("mappingSteps");
    widget.findChild<QLineEdit *>("mappingCode")->setText("SWPAIR");
    operation->setCurrentIndex(operation->findData("QUSHENG_21"));
    mode->setCurrentIndex(mode->findData(-2)); QCOMPARE(mode->currentData().toInt(), -2);
    QCOMPARE(state->currentData().toString(), QString());
    button(widget, "saveMappingStep")->click(); QCOMPARE(steps->rowCount(), 0);
    state->setCurrentIndex(parameterChoice(state, "values", 1));
    widget.findChild<QCheckBox *>("valveCheck1")->click(); widget.findChild<QCheckBox *>("valveCheck30")->click();
    QCOMPARE(mode->currentData().toInt(), -2); QCOMPARE(state->currentData(Qt::UserRole + 2).toInt(), 1);
    auto *rawMode = qobject_cast<QComboBox *>(parameters->cellWidget(parameterRow(parameters, "mode"), 1));
    QCOMPARE(rawMode->currentData().toString(), QString("values")); QCOMPARE(rawMode->currentData(Qt::UserRole + 2).toInt(), 1);
    button(widget, "saveMappingStep")->click(); QCOMPARE(steps->rowCount(), 1);
    button(widget, "newMappingStep")->click(); mode->setCurrentIndex(mode->findData(-2));
    state->setCurrentIndex(parameterChoice(state, "values", 1)); widget.findChild<QCheckBox *>("valveCheck2")->click();
    button(widget, "saveMappingStep")->click(); QCOMPARE(steps->rowCount(), 2);
    auto *counts = widget.findChild<QTableWidget *>("mappingParameterCounts"); qobject_cast<QSpinBox *>(counts->cellWidget(0, 1))->setValue(2);
    state->setCurrentIndex(parameterChoice(state, "values", 2));
    steps->setCurrentCell(0, 0); button(widget, "addMapping")->click();
    QCOMPARE(steps->currentRow(), 1); QCOMPARE(mappingRows(widget).size(), existing.size());
    button(widget, "discardMappingStep")->click(); QCOMPARE(state->currentData(Qt::UserRole + 2).toInt(), 1);
    rawMode = qobject_cast<QComboBox *>(parameters->cellWidget(parameterRow(parameters, "mode"), 1));
    rawMode->setCurrentIndex(parameterChoice(rawMode, "values", 2));
    QCOMPARE(state->currentData(Qt::UserRole + 2).toInt(), 2); QCOMPARE(mode->currentData().toInt(), -2);
    button(widget, "saveMappingStep")->click(); button(widget, "addMapping")->click();
    QCOMPARE(mappingRows(widget).size(), existing.size() + 1); button(widget, "saveConfig")->click();
    const auto saved = mappingRows(widget).last().toObject(); const auto configured = saved.value("steps").toArray();
    QCOMPARE(configured.size(), 2);
    const auto first = configured.first().toObject().value("parameters").toObject();
    QCOMPARE(first.value("mode").toObject().value("allowedValues").toArray(), QJsonArray({0, 1}));
    QCOMPARE(first.value("valveMask").toObject().value("value").toInt(), int(0x20000001));
    QCOMPARE(configured.last().toObject().value("parameters").toObject().value("mode").toObject().value("itemIndex").toInt(), 2);
    for (int i = 0; i < existing.size(); ++i) QCOMPARE(mappingRows(widget).at(i), existing.at(i));
    Widget restored(nullptr, path); QCOMPARE(mappingRows(restored).last().toObject(), saved);
    auto *input = widget.findChild<QLineEdit *>("customerInput");
    auto *target = widget.findChild<QTextEdit *>("targetOutput"); auto *preview = widget.findChild<QTextEdit *>("previewOutput");
    input->setText("!SWPAIR=1,0\\r"); button(widget, "previewButton")->click();
    QVERIFY(target->toPlainText().contains("60 00 00 01 00 00 00 00")); QVERIFY(target->toPlainText().contains("00 00 00 02 00 00 00 00"));
    QVERIFY(preview->toPlainText().contains(QString::fromUtf8(u8"开启，其余不变"))); QVERIFY(preview->toPlainText().contains(QString::fromUtf8(u8"关闭，其余不变")));
    button(widget, "sendButton")->click(); QVERIFY(target->toPlainText().contains("60 00 00 01 00 00 00 00"));
    input->setText("!SWPAIR=1,2\\r"); button(widget, "previewButton")->click(); QVERIFY(target->toPlainText().isEmpty());
    button(widget, "sendButton")->click(); QVERIFY(target->toPlainText().isEmpty());
    // Explicitly choosing a fixed action removes the switch-only constraint, leaving cover-all available.
    widget.findChild<QLineEdit *>("mappingCode")->setText("FIXED2"); operation->setCurrentIndex(operation->findData("QUSHENG_21"));
    mode->setCurrentIndex(mode->findData(-2)); state->setCurrentIndex(parameterChoice(state, "values", 1));
    rawMode = qobject_cast<QComboBox *>(parameters->cellWidget(parameterRow(parameters, "mode"), 1));
    rawMode->setCurrentIndex(rawMode->findData("@constant"));
    qobject_cast<QLineEdit *>(parameters->cellWidget(parameterRow(parameters, "mode"), 2))->setText("2");
    QCOMPARE(mode->currentData().toInt(), 2); button(widget, "addMapping")->click();
    QVERIFY(!mappingRows(widget).last().toObject().value("parameters").toObject().value("mode").toObject().contains("allowedValues"));

    // A loaded fixed expression may also carry a constraint. Re-selecting its existing
    // fixed action changes only that constraint, so it must still require an explicit save.
    Widget guarded(nullptr, freshDraftPath()); applyRoot(guarded, root);
    auto *guardedOperation = guarded.findChild<QComboBox *>("mappingOperation");
    guarded.findChild<QLineEdit *>("mappingCode")->setText("GUARDED_FIXED");
    guardedOperation->setCurrentIndex(guardedOperation->findData("QUSHENG_21"));
    auto *guardedMode = guarded.findChild<QComboBox *>("valveMode"); guardedMode->setCurrentIndex(guardedMode->findData(1));
    auto *guardedParameters = guarded.findChild<QTableWidget *>("mappingParameters");
    const int guardedModeRow = parameterRow(guardedParameters, "mode");
    // Seed an imported advanced expression, then save and reload it through the real step editor.
    auto *guardedLabel = guardedParameters->item(guardedModeRow, 0);
    guardedLabel->setData(Qt::UserRole + 1, QJsonObject{{"kind", "constant"}, {"value", 1}, {"allowedValues", QJsonArray{0, 1}}});
    guardedLabel->setData(Qt::UserRole + 2, true);
    button(guarded, "saveMappingStep")->click(); button(guarded, "newMappingStep")->click();
    auto *guardedSteps = guarded.findChild<QTableWidget *>("mappingSteps"); guardedSteps->setCurrentCell(0, 0);
    QCOMPARE(guardedMode->currentData().toInt(), 1);
    guardedLabel = guardedParameters->item(parameterRow(guardedParameters, "mode"), 0);
    QVERIFY(guardedLabel->data(Qt::UserRole + 1).toJsonObject().contains("allowedValues"));
    QVERIFY(!guarded.findChild<QLabel *>("mappingStepState")->text().contains(QString::fromUtf8(u8"未保存修改")));
    QMetaObject::invokeMethod(guardedMode, "activated", Qt::DirectConnection, Q_ARG(int, guardedMode->findData(1)));
    QVERIFY(guarded.findChild<QLabel *>("mappingStepState")->text().contains(QString::fromUtf8(u8"未保存修改")));
    button(guarded, "newMappingStep")->click(); button(guarded, "addMapping")->click();
    QCOMPARE(guardedSteps->currentRow(), 0); QCOMPARE(mappingRows(guarded).size(), existing.size());
    button(guarded, "discardMappingStep")->click();
    guardedLabel = guardedParameters->item(parameterRow(guardedParameters, "mode"), 0);
    QVERIFY(guardedLabel->data(Qt::UserRole + 1).toJsonObject().contains("allowedValues"));
    QMetaObject::invokeMethod(guardedMode, "activated", Qt::DirectConnection, Q_ARG(int, guardedMode->findData(1)));
    button(guarded, "saveMappingStep")->click(); button(guarded, "addMapping")->click();
    QCOMPARE(mappingRows(guarded).size(), existing.size() + 1);
    QVERIFY(!mappingRows(guarded).last().toObject().value("steps").toArray().first().toObject().value("parameters").toObject().value("mode").toObject().contains("allowedValues"));
}

void WidgetTest::valveSelectionMappingLifecycle()
{
    const QString path = freshDraftPath(); Widget widget(nullptr, path);
    const auto directory = QFileInfo(QDir(QString::fromLatin1(QT_TESTCASE_BUILDDIR)).absoluteFilePath(QString::fromLatin1(__FILE__))).dir();
    QFile file(directory.filePath("../examples/valve-selection.json")); QVERIFY(file.open(QIODevice::ReadOnly));
    applyRoot(widget, QJsonDocument::fromJson(file.readAll()).object());
    const int originalCount = mappingRows(widget).size();
    auto *operation = widget.findChild<QComboBox *>("mappingOperation");
    auto *parameters = widget.findChild<QTableWidget *>("mappingParameters");
    auto *steps = widget.findChild<QTableWidget *>("mappingSteps");
    auto *mode = widget.findChild<QComboBox *>("valveMode");
    auto *number = widget.findChild<QComboBox *>("valveNumber");
    auto check = [&widget](int number) { return widget.findChild<QCheckBox *>("valveCheck" + QString::number(number)); };
    widget.findChild<QLineEdit *>("mappingCode")->setText("VALVES");
    operation->setCurrentIndex(operation->findData("QUSHENG_21"));
    QVERIFY(operation->currentText().contains(QString::fromUtf8(u8"电磁阀")));
    QVERIFY(!qobject_cast<QStandardItemModel *>(mode->model())->item(0)->isEnabled());
    QVERIFY(widget.findChild<QLabel *>("valveActionHint")->text().contains(QString::fromUtf8(u8"不会操作任何阀")));
    for (int index = 1; index <= 30; ++index) QVERIFY(!check(index)->isChecked());
    QMetaObject::invokeMethod(mode, "activated", Qt::DirectConnection, Q_ARG(int, mode->findData(0)));
    check(1)->click(); button(widget, "saveMappingStep")->click(); QCOMPARE(steps->rowCount(), 1);
    button(widget, "saveMappingStep")->click(); QCOMPARE(steps->rowCount(), 1);
    button(widget, "newMappingStep")->click(); check(30)->click(); button(widget, "saveMappingStep")->click(); QCOMPARE(steps->rowCount(), 2);
    button(widget, "newMappingStep")->click(); mode->setCurrentIndex(mode->findData(1));
    for (int index : {1, 10, 11, 20, 21, 30}) check(index)->click();
    const int durationRow = parameterRow(parameters, "duration");
    auto *durationSource = qobject_cast<QComboBox *>(parameters->cellWidget(durationRow, 1));
    durationSource->setCurrentIndex(parameterChoice(durationSource, "values", 1));
    button(widget, "saveMappingStep")->click(); QCOMPARE(steps->rowCount(), 3);
    const QString render = qEnvironmentVariable("TRANSLATION_RENDER_DIR");
    widget.resize(1100, 740); widget.show(); widget.findChild<QTabWidget *>("mainTabs")->setCurrentIndex(1);
    auto *helper = widget.findChild<QGroupBox *>("valveSelectionEditor");
    auto *scroll = widget.findChild<QScrollArea *>("mappingEditorScroll"); QVERIFY(scroll);
    QCoreApplication::processEvents(); scroll->ensureWidgetVisible(helper); QCoreApplication::processEvents();
    if (!render.isEmpty()) {
        QDir().mkpath(render); QVERIFY(widget.grab().save(QDir(render).filePath("valve-selection-1100.png")));
        widget.resize(1400, 1000); QCoreApplication::processEvents(); scroll->ensureWidgetVisible(helper); QCoreApplication::processEvents();
        QVERIFY(widget.grab().save(QDir(render).filePath("valve-selection-large.png")));
        widget.resize(1100, 740);
    }
    button(widget, "newMappingStep")->click(); operation->setCurrentIndex(operation->findData("QUSHENG_22"));
    QCOMPARE(number->currentData().toInt(), 0x31); QVERIFY(number->currentText().contains("1")); QVERIFY(number->currentText().contains("0x31"));
    QCOMPARE(qobject_cast<QComboBox *>(parameters->cellWidget(parameterRow(parameters, "devId"), 1))->currentData().toString(), QString());
    QVERIFY(widget.findChild<QLabel *>("valveActionHint")->text().contains(QString::fromUtf8(u8"时间0不等于关闭")));
    QVERIFY(!qobject_cast<QStandardItemModel *>(number->model())->item(0)->isEnabled());
    number->setCurrentIndex(number->findData(0x4e)); button(widget, "saveMappingStep")->click(); QCOMPARE(steps->rowCount(), 4);
    button(widget, "newMappingStep")->click(); operation->setCurrentIndex(operation->findData("QUSHENG_21"));
    mode->setCurrentIndex(mode->findData(2)); button(widget, "clearValves")->click();
    QVERIFY(widget.findChild<QLabel *>("valveActionHint")->text().contains(QString::fromUtf8(u8"将关闭全部阀")));
    check(1)->click(); check(30)->click(); button(widget, "saveMappingStep")->click(); QCOMPARE(steps->rowCount(), 5);
    check(2)->click();
    button(widget, "newMappingStep")->click(); steps->setCurrentCell(0, 0); button(widget, "mappingStepUp")->click(); button(widget, "addMapping")->click();
    QCOMPARE(steps->rowCount(), 5); QCOMPARE(steps->currentRow(), 4); QCOMPARE(mappingRows(widget).size(), originalCount);
    QVERIFY(check(2)->isChecked()); button(widget, "discardMappingStep")->click(); QVERIFY(!check(2)->isChecked());
    QVERIFY(check(1)->isChecked()); QVERIFY(check(30)->isChecked());
    button(widget, "addMapping")->click(); QCOMPARE(mappingRows(widget).size(), originalCount + 1);
    button(widget, "saveConfig")->click(); const auto saved = mappingRows(widget).last().toObject();
    const auto configured = saved.value("steps").toArray(); QCOMPARE(configured.size(), 5);
    QCOMPARE(configured[0].toObject().value("parameters").toObject().value("valveMask").toObject().value("value").toInt(), 1);
    QCOMPARE(configured[1].toObject().value("parameters").toObject().value("valveMask").toObject().value("value").toInt(), int(0x20000000));
    QCOMPARE(configured[2].toObject().value("parameters").toObject().value("valveMask").toObject().value("value").toInt(), int(0x20180601));
    QCOMPARE(configured[2].toObject().value("parameters").toObject().value("duration").toObject().value("itemIndex").toInt(), 1);
    widget.findChild<QLineEdit *>("customerInput")->setText("!VALVES=3000\\r"); button(widget, "previewButton")->click();
    const QString targets = widget.findChild<QTextEdit *>("targetOutput")->toPlainText();
    const QString preview = widget.findChild<QTextEdit *>("previewOutput")->toPlainText();
    QVERIFY(!preview.contains(QString::fromUtf8(u8"目标17")));
    QVERIFY(preview.contains(QString::fromUtf8(u8"目标1号阀")));
    QVERIFY(preview.contains(QString::fromUtf8(u8"目标30号阀（0x4E）")));
    QVERIFY(preview.contains(QString::fromUtf8(u8"目标1、30号阀（未选阀关闭）")));
    for (const QString &payload : {QString("00 00 00 01 00 00 00 00"), QString("20 00 00 00 00 00 00 00"), QString("60 18 06 01 00 00 0B B8"), QString("4E 00 00 00 00"), QString("A0 00 00 01 00 00 00 00")}) QVERIFY2(targets.contains(payload), qPrintable(targets));
    if (!render.isEmpty()) {
        widget.findChild<QTabWidget *>("mainTabs")->setCurrentIndex(2); QCoreApplication::processEvents();
        QVERIFY(widget.grab().save(QDir(render).filePath("valve-preview-frames.png")));
    }
    Widget restored(nullptr, path); QCOMPARE(mappingRows(restored).last().toObject(), saved);
    widget.findChild<QLineEdit *>("customerInput")->setText("!DEFAULT22=0\\r"); button(widget, "previewButton")->click();
    QVERIFY(widget.findChild<QTextEdit *>("previewOutput")->toPlainText().contains(QString::fromUtf8(u8"目标=1号阀（0x31）")));
    auto root = draftRoot(widget); auto mappings = root.value("mappings").toArray();
    for (int modeValue : {0, 2}) mappings.append(QJsonObject{{"commandCode", "EMPTY" + QString::number(modeValue)}, {"operation", "QUSHENG_21"},
        {"input", QJsonObject{{"omitFields", QJsonArray{"values"}}}}, {"parameters", QJsonObject{{"mode", QJsonObject{{"kind", "constant"}, {"value", modeValue}}},
            {"valveMask", QJsonObject{{"kind", "constant"}, {"value", 0}}}}}});
    root.insert("mappings", mappings); applyRoot(widget, root);
    widget.findChild<QLineEdit *>("customerInput")->setText("!EMPTY0=0\\r"); button(widget, "previewButton")->click();
    QVERIFY(widget.findChild<QTextEdit *>("previewOutput")->toPlainText().contains(QString::fromUtf8(u8"目标=未选阀（无操作）")));
    widget.findChild<QLineEdit *>("customerInput")->setText("!EMPTY2=0\\r"); button(widget, "previewButton")->click();
    QVERIFY(widget.findChild<QTextEdit *>("previewOutput")->toPlainText().contains(QString::fromUtf8(u8"目标=全部30路阀关闭")));
}

void WidgetTest::valveSourcesAndMetadataArePreserved()
{
    Widget widget(nullptr, freshDraftPath()); applyRoot(widget, listExample());
    auto root = draftRoot(widget); root.insert("sdkDefaults", QJsonObject{{"QUSHENG_21", QJsonObject{{"mode", 1}, {"valveMask", double(0x20000001)}, {"duration", 77}}},
        {"QUSHENG_22", QJsonObject{{"devId", 78}, {"duration", 88}}}});
    auto mappings = root.value("mappings").toArray();
    const QJsonObject relation{{"kind", "linear"}, {"field", "values"}, {"itemIndex", 2}, {"numerator", 2}, {"denominator", 1}, {"offset", 3}, {"round", "floor"}, {"extension", "keep relation"}};
    const QJsonObject legacy{{"commandCode", "RAW"}, {"operation", "QUSHENG_21"}, {"input", QJsonObject{{"parameterCounts", QJsonObject{{"values", 3}}}, {"extension", "keep input"}}},
        {"parameters", QJsonObject{{"mode", QJsonObject{{"kind", "constant"}, {"value", 1}}}, {"valveMask", relation}, {"duration", relation}}}, {"extension", "keep mapping"}};
    mappings.append(legacy); root.insert("mappings", mappings); applyRoot(widget, root);
    auto *operation = widget.findChild<QComboBox *>("mappingOperation"); operation->setCurrentIndex(operation->findData("QUSHENG_21"));
    auto *parameters = widget.findChild<QTableWidget *>("mappingParameters");
    auto *mode = widget.findChild<QComboBox *>("valveMode"); QCOMPARE(mode->currentData().toInt(), 1);
    QVERIFY(widget.findChild<QCheckBox *>("valveCheck1")->isChecked()); QVERIFY(widget.findChild<QCheckBox *>("valveCheck30")->isChecked());
    for (int row = 0; row < parameters->rowCount(); ++row)
        QCOMPARE(qobject_cast<QComboBox *>(parameters->cellWidget(row, 1))->currentData().toString(), QString());
    widget.findChild<QLineEdit *>("mappingCode")->setText("DEFAULTS");
    auto *inputSource = widget.findChild<QComboBox *>("mappingSource"); inputSource->setCurrentIndex(inputSource->findData("@none"));
    button(widget, "saveMappingStep")->click();
    button(widget, "newMappingStep")->click(); operation->setCurrentIndex(operation->findData("QUSHENG_22"));
    QCOMPARE(widget.findChild<QComboBox *>("valveNumber")->currentData().toInt(), 78);
    button(widget, "saveMappingStep")->click(); button(widget, "addMapping")->click();
    const auto defaults = mappingRows(widget).last().toObject().value("steps").toArray();
    QCOMPARE(defaults.size(), 2); QVERIFY(defaults[0].toObject().value("parameters").toObject().isEmpty()); QVERIFY(defaults[1].toObject().value("parameters").toObject().isEmpty());
    operation->setCurrentIndex(operation->findData("QUSHENG_21")); widget.findChild<QLineEdit *>("mappingCode")->setText("DYNAMIC");
    auto *counts = widget.findChild<QTableWidget *>("mappingParameterCounts"); qobject_cast<QSpinBox *>(counts->cellWidget(0, 1))->setValue(3);
    for (const auto &pair : {qMakePair(QString("mode"), 1), qMakePair(QString("valveMask"), 2), qMakePair(QString("duration"), 3)}) {
        auto *source = qobject_cast<QComboBox *>(parameters->cellWidget(parameterRow(parameters, pair.first), 1));
        source->setCurrentIndex(parameterChoice(source, "values", pair.second));
    }
    QCOMPARE(mode->currentData().toInt(), -1); QVERIFY(widget.findChild<QLabel *>("valveSelectionSummary")->text().contains(QString::fromUtf8(u8"客户来源")));
    mode->setCurrentIndex(mode->findData(0)); widget.findChild<QCheckBox *>("valveCheck3")->click();
    const int durationRow = parameterRow(parameters, "duration");
    QCOMPARE(qobject_cast<QComboBox *>(parameters->cellWidget(durationRow, 1))->currentData(Qt::UserRole + 2).toInt(), 3);
    auto *rawMask = qobject_cast<QLineEdit *>(parameters->cellWidget(parameterRow(parameters, "valveMask"), 2));
    rawMask->setText("1073741824"); QVERIFY(widget.findChild<QLabel *>("valveSelectionSummary")->text().contains(QString::fromUtf8(u8"无效")));
    rawMask->setText("2"); QVERIFY(widget.findChild<QCheckBox *>("valveCheck2")->isChecked()); QVERIFY(!widget.findChild<QCheckBox *>("valveCheck3")->isChecked());
    button(widget, "selectAllValves")->click();
    for (int number = 1; number <= 30; ++number) QVERIFY(widget.findChild<QCheckBox *>("valveCheck" + QString::number(number))->isChecked());
    button(widget, "clearValves")->click(); QVERIFY(widget.findChild<QLabel *>("valveActionHint")->text().contains(QString::fromUtf8(u8"不会操作任何阀")));
    button(widget, "addMapping")->click(); button(widget, "saveConfig")->click();
    QCOMPARE(mappingRows(widget).last().toObject().value("parameters").toObject().value("duration").toObject().value("itemIndex").toInt(), 3);
    QCOMPARE(mappingRows(widget).at(mappings.size() - 1).toObject(), legacy);
    const QString exported = freshDraftPath();
    for (const char *action : {"exportConfig", "importConfig"}) {
        bool opened = false;
        QTimer::singleShot(0, &widget, [&widget, &opened, exported]() {
            auto *dialog = widget.findChild<QFileDialog *>(); if (!dialog) return;
            opened = true; dialog->selectFile(exported); QMetaObject::invokeMethod(dialog, "accept", Qt::QueuedConnection);
        });
        button(widget, action)->click(); QVERIFY(opened); QCOMPARE(mappingRows(widget).at(mappings.size() - 1).toObject(), legacy);
    }
}

void WidgetTest::formatDrivenParsingKeepsUnusedParameters()
{
    const QString path = freshDraftPath(); Widget widget(nullptr, path);
    const auto directory = QFileInfo(QDir(QString::fromLatin1(QT_TESTCASE_BUILDDIR)).absoluteFilePath(QString::fromLatin1(__FILE__))).dir();
    QFile example(directory.filePath("../examples/format-driven-unused-parameter.json")); QVERIFY(example.open(QIODevice::ReadOnly));
    auto root = QJsonDocument::fromJson(example.readAll()).object();
    auto external = root.value("external").toObject(); external.insert("header", "@&"); external.insert("footer", "\\r\\n");
    auto fields = external.value("fields").toArray();
    for (int row = 0; row < fields.size(); ++row) {
        auto field = fields.at(row).toObject();
        if (field.value("delimiter").toString() == "|") field.insert("delimiter", "&");
        if (row < 3) field.insert("fixedValue", QStringList{"5A", "01", "SCT"}.at(row));
        if (row == 8) { field.insert("fixedValue", "END&"); field.insert("length", 4); }
        field.insert("extension", QString("field-%1").arg(row)); fields[row] = field;
    }
    external.insert("fields", fields); root.insert("external", external);
    auto mapping = root.value("mappings").toArray().first().toObject(); mapping.insert("commandCode", "RST");
    auto mappingInput = mapping.value("input").toObject(); mappingInput.insert("extension", "preserve unused input"); mapping.insert("input", mappingInput);
    root.insert("mappings", QJsonArray{mapping}); applyRoot(widget, root);
    const auto savedMappings = mappingRows(widget);
    auto *input = widget.findChild<QLineEdit *>("customerInput");
    const QString packet = "@&5A&01&SCT&1&0;RST=0;10\\nEND&?&\\r\\n";
    input->setText(packet); button(widget, "parseFormatButton")->click();
    auto *output = widget.findChild<QTextEdit *>("previewOutput");
    QVERIFY2(output->toPlainText().contains(QString::fromUtf8(u8"数值 = [0]")), qPrintable(output->toPlainText()));
    QVERIFY(output->toPlainText().contains(QString::fromUtf8(u8"延时 = 10")));
    QVERIFY(widget.findChild<QTextEdit *>("targetOutput")->toPlainText().isEmpty());
    QCOMPARE(mappingRows(widget), savedMappings);
    const QString render = qEnvironmentVariable("TRANSLATION_RENDER_DIR");
    if (!render.isEmpty()) {
        widget.resize(1100, 740); widget.show(); widget.findChild<QTabWidget *>("mainTabs")->setCurrentIndex(2);
        QDir().mkpath(render); QCoreApplication::processEvents();
        QVERIFY(widget.grab().save(QDir(render).filePath("format-driven-rst-parse.png")));
    }
    button(widget, "previewButton")->click(); QVERIFY(output->toPlainText().contains("EXECUTION_SEMANTICS"));
    QVERIFY(widget.findChild<QTextEdit *>("targetOutput")->toPlainText().isEmpty());
    auto *maps = widget.findChild<QTableWidget *>("mappingTable"); maps->setCurrentCell(0, 0); button(widget, "removeMapping")->click();
    button(widget, "parseFormatButton")->click(); QVERIFY(output->toPlainText().contains(QString::fromUtf8(u8"数值 = [0]")));
    QVERIFY(output->toPlainText().contains(QString::fromUtf8(u8"延时 = 10")));
    applyRoot(widget, root); button(widget, "saveConfig")->click();
    Widget restored(nullptr, path); QCOMPARE(mappingRows(restored), savedMappings);
    QCOMPARE(draftRoot(restored).value("external").toObject().value("fields").toArray().at(6).toObject().value("extension").toString(), QString("field-6"));
    restored.findChild<QLineEdit *>("customerInput")->setText(packet); button(restored, "parseFormatButton")->click();
    QVERIFY(restored.findChild<QTextEdit *>("previewOutput")->toPlainText().contains(QString::fromUtf8(u8"延时 = 10")));
    const QString exported = freshDraftPath();
    for (const char *action : {"exportConfig", "importConfig"}) {
        bool opened = false;
        QTimer::singleShot(0, &restored, [&restored, &opened, exported]() {
            auto *dialog = restored.findChild<QFileDialog *>(); if (!dialog) return;
            opened = true; dialog->selectFile(exported); QMetaObject::invokeMethod(dialog, "accept", Qt::QueuedConnection);
        });
        button(restored, action)->click(); QVERIFY(opened);
        QCOMPARE(mappingRows(restored), savedMappings);
    }

    auto *operation = widget.findChild<QComboBox *>("mappingOperation"); operation->setCurrentIndex(operation->findData("MOTOR_RESET"));
    auto *source = widget.findChild<QComboBox *>("mappingSource");
    QVERIFY(source->itemText(source->findData("@none")).contains(QString::fromUtf8(u8"字段仍解析")));
    widget.findChild<QLineEdit *>("mappingCode")->setText("OTHER"); button(widget, "saveMappingStep")->click();
    auto *mode = widget.findChild<QComboBox *>("groupInputMode"); QCOMPARE(mode->currentData().toString(), QString("none"));
    QVERIFY(mode->currentText().contains(QString::fromUtf8(u8"不使用客户参数")));
    QCOMPARE(widget.findChild<QTableWidget *>("mappingParameterCounts")->rowCount(), 0);
    QVERIFY(widget.findChild<QLabel *>("mappingInputHint")->text().contains(QString::fromUtf8(u8"始终读取全部字段")));
    const auto visibleFields = widget.findChild<QTableWidget *>("fieldTable");
    auto *commandMode = qobject_cast<QComboBox *>(visibleFields->cellWidget(5, 4));
    commandMode->setCurrentIndex(commandMode->findData("mappedCommand"));
    QVERIFY(mode->currentText().contains(QString::fromUtf8(u8"短报文")));
    QVERIFY(widget.findChild<QLabel *>("mappingInputHint")->text().contains(QString::fromUtf8(u8"省略")));

    // 只在测试副本将执行字段声明为普通元数据，验证RST三步均使用SDK固定零位置。
    for (int row : {4, 7}) { auto field = fields.at(row).toObject(); field.insert("role", "metadata"); fields[row] = field; }
    external.insert("fields", fields); root.insert("external", external); applyRoot(widget, root); button(widget, "saveConfig")->click();
    input->setText(packet); button(widget, "previewButton")->click();
    const auto targets = widget.findChild<QTextEdit *>("targetOutput")->toPlainText();
    for (const QString &motor : {QString("11"), QString("12"), QString("13")}) QVERIFY(targets.contains(motor + " 44 00 03 E8 00 00 00"));
    QVERIFY(output->toPlainText().contains(QString::fromUtf8(u8"数值 = [0]")));
    input->setText(QString(packet).replace("RST=0;", "RST=bad;")); button(widget, "parseFormatButton")->click();
    QVERIFY(output->toPlainText().contains("FIELD_LIST_NUMBER")); QVERIFY(widget.findChild<QTextEdit *>("targetOutput")->toPlainText().isEmpty());
}

void WidgetTest::explicitMappingStepOperationsAndResponsiveLayout()
{
    const QString path = freshDraftPath();
    Widget widget(nullptr, path);
    const auto directory = QFileInfo(QDir(QString::fromLatin1(QT_TESTCASE_BUILDDIR)).absoluteFilePath(QString::fromLatin1(__FILE__))).dir();
    QFile example(directory.filePath("../examples/no-parameter-reset-group.json")); QVERIFY(example.open(QIODevice::ReadOnly));
    auto root = QJsonDocument::fromJson(example.readAll()).object();
    const auto sample = root.value("mappings").toArray().first().toObject();
    QJsonArray savedMappings;
    for (int index = 0; index < 8; ++index) {
        auto mapping = sample; mapping.insert("commandCode", QString("R%1").arg(index)); savedMappings.append(mapping);
    }
    root.insert("mappings", savedMappings); applyRoot(widget, root);
    widget.resize(1100, 740); widget.show();
    widget.findChild<QTabWidget *>("mainTabs")->setCurrentIndex(1);
    auto *operation = widget.findChild<QComboBox *>("mappingOperation");
    auto *motor = widget.findChild<QComboBox *>("mappingMotor");
    auto *steps = widget.findChild<QTableWidget *>("mappingSteps");
    auto *saveStep = button(widget, "saveMappingStep");
    auto *state = widget.findChild<QLabel *>("mappingStepState");
    widget.findChild<QLineEdit *>("mappingCode")->setText("EIGHT");
    operation->setCurrentIndex(operation->findData("MOTOR_RESET")); motor->setCurrentIndex(motor->findData(0x11));
    QCOMPARE(saveStep->text(), QString::fromUtf8(u8"添加到清单"));
    saveStep->click(); QCOMPARE(steps->rowCount(), 1);
    QCOMPARE(saveStep->text(), QString::fromUtf8(u8"保存本条修改"));
    saveStep->click(); QCOMPARE(steps->rowCount(), 1); // 再保存只更新，不追加。
    button(widget, "newMappingStep")->click(); button(widget, "newMappingStep")->click();
    QCOMPARE(steps->rowCount(), 1); QCOMPARE(steps->currentRow(), -1);
    motor->setCurrentIndex(motor->findData(0x12));
    for (const char *action : {"newMappingStep", "mappingStepUp", "mappingStepDown", "addMapping"}) {
        button(widget, action)->click(); QCOMPARE(steps->rowCount(), 1); QCOMPARE(mappingRows(widget).size(), 8);
        QCOMPARE(motor->currentData().toInt(), 0x12);
        QVERIFY(widget.findChild<QLabel *>("validationState")->text().contains(QString::fromUtf8(u8"尚未保存")));
    }
    steps->setCurrentCell(0, 0); QCOMPARE(steps->currentRow(), -1); QCOMPARE(motor->currentData().toInt(), 0x12);
    saveStep->click(); QCOMPARE(steps->rowCount(), 2);
    steps->setCurrentCell(0, 0); motor->setCurrentIndex(motor->findData(0x13));
    QVERIFY(state->text().contains(QString::fromUtf8(u8"未保存")));
    const auto firstRow = steps->item(0, 2)->text();
    steps->setCurrentCell(1, 0); QCOMPARE(steps->currentRow(), 0);
    for (const char *action : {"newMappingStep", "mappingStepUp", "mappingStepDown", "addMapping"}) button(widget, action)->click();
    QCOMPARE(steps->rowCount(), 2); QCOMPARE(steps->currentRow(), 0);
    QCOMPARE(steps->item(0, 2)->text(), firstRow); QCOMPARE(motor->currentData().toInt(), 0x13);
    button(widget, "discardMappingStep")->click(); QCOMPARE(motor->currentData().toInt(), 0x11);
    motor->setCurrentIndex(motor->findData(0x13)); saveStep->click(); QCOMPARE(steps->rowCount(), 2);
    button(widget, "mappingStepDown")->click(); QCOMPARE(steps->rowCount(), 2); QCOMPARE(steps->currentRow(), 1);
    button(widget, "mappingStepUp")->click(); QCOMPARE(steps->rowCount(), 2); QCOMPARE(steps->currentRow(), 0);

    // 完全相同的指令仍可明确添加两次，不按值自动去重。
    for (int index = 0; index < 2; ++index) { button(widget, "newMappingStep")->click(); saveStep->click(); }
    QCOMPARE(steps->rowCount(), 4); QCOMPARE(steps->item(2, 2)->text(), steps->item(3, 2)->text());
    while (steps->rowCount() < 8) {
        button(widget, "newMappingStep")->click(); motor->setCurrentIndex(motor->findData(0x11 + steps->rowCount() % 6)); saveStep->click();
    }
    QCoreApplication::processEvents();
    QVERIFY(widget.findChild<QTableWidget *>("mappingTable")->height() >= 100);
    QVERIFY(steps->height() > 110);
    const int normalHeight = steps->height();
    const QString render = qEnvironmentVariable("TRANSLATION_RENDER_DIR");
    if (!render.isEmpty()) {
        QDir().mkpath(render);
        QVERIFY(widget.grab().save(QDir(render).filePath("explicit-reset-eight.png")));
    }
    widget.resize(1400, 1000); QCoreApplication::processEvents();
    QVERIFY(steps->height() > normalHeight);
    if (!render.isEmpty()) QVERIFY(widget.grab().save(QDir(render).filePath("explicit-reset-large.png")));
    widget.resize(1100, 740); QCoreApplication::processEvents();
    auto *splitter = widget.findChild<QSplitter *>("mappingSplitter"); QVERIFY(splitter);
    const int upperHeight = splitter->sizes().first(); splitter->setSizes({upperHeight + 40, splitter->sizes().last() - 40});
    QCoreApplication::processEvents(); QVERIFY(splitter->sizes().first() > upperHeight);

    // 干净的新草稿不属于清单；最终映射条数必须等于眼前的8条。
    button(widget, "newMappingStep")->click(); button(widget, "newMappingStep")->click();
    button(widget, "addMapping")->click(); QCOMPARE(mappingRows(widget).size(), 9);
    QCOMPARE(mappingRows(widget).last().toObject().value("steps").toArray().size(), 8);
    button(widget, "saveConfig")->click(); const auto saved = mappingRows(widget);
    button(widget, "saveConfig")->click(); QCOMPARE(mappingRows(widget), saved);
    Widget restored(nullptr, path); QCOMPARE(mappingRows(restored), saved);

    widget.findChild<QLineEdit *>("mappingCode")->setText("XYZGROUP");
    operation->setCurrentIndex(operation->findData("MOTOR_RESET")); saveStep->click();
    button(widget, "newMappingStep")->click(); operation->setCurrentIndex(operation->findData("QUSHENG_C1"));
    auto *parameters = widget.findChild<QTableWidget *>("mappingParameters");
    for (const QString &key : {QString("moduleIndex"), QString("xPosition"), QString("yPosition"), QString("zPosition")}) {
        const int row = parameterRow(parameters, key); QVERIFY(row >= 0);
        auto *source = qobject_cast<QComboBox *>(parameters->cellWidget(row, 1)); source->setCurrentIndex(source->findData("@constant"));
        qobject_cast<QLineEdit *>(parameters->cellWidget(row, 2))->setText(key == "moduleIndex" ? "35" : "1234");
    }
    saveStep->click(); QCOMPARE(steps->rowCount(), 2);
    auto *scroll = widget.findChild<QScrollArea *>("mappingEditorScroll"); QVERIFY(scroll);
    QCoreApplication::processEvents(); scroll->ensureWidgetVisible(parameters);
    QCoreApplication::processEvents();
    QVERIFY(scroll->viewport()->rect().contains(QRect(parameters->mapTo(scroll->viewport(), QPoint()), parameters->size())));
    if (!render.isEmpty()) QVERIFY(widget.grab().save(QDir(render).filePath("explicit-xyz-parameters.png")));
}

void WidgetTest::noParameterResetGroupsAndExplicitTargets()
{
    const QString path = freshDraftPath();
    Widget widget(nullptr, path);
    const auto sourceDirectory = QFileInfo(QDir(QString::fromLatin1(QT_TESTCASE_BUILDDIR)).absoluteFilePath(QString::fromLatin1(__FILE__))).dir();
    QFile example(sourceDirectory.filePath("../examples/no-parameter-reset-group.json")); QVERIFY(example.open(QIODevice::ReadOnly));
    const auto exampleRoot = QJsonDocument::fromJson(example.readAll()).object();
    applyRoot(widget, exampleRoot);
    auto *operation = widget.findChild<QComboBox *>("mappingOperation");
    auto *motor = widget.findChild<QComboBox *>("mappingMotor");
    auto *source = widget.findChild<QComboBox *>("mappingSource");
    auto *value = widget.findChild<QLineEdit *>("mappingValue");
    auto *steps = widget.findChild<QTableWidget *>("mappingSteps");
    auto *mode = widget.findChild<QComboBox *>("groupInputMode");
    widget.findChild<QLineEdit *>("mappingCode")->setText("PAIR");
    operation->setCurrentIndex(operation->findData("MOTOR_RESET"));
    motor->setCurrentIndex(motor->findData(0x11));
    button(widget, "saveMappingStep")->click(); QCOMPARE(steps->rowCount(), 1);
    QCOMPARE(mode->currentData().toString(), QString("none"));
    button(widget, "newMappingStep")->click();
    QCOMPARE(operation->currentData().toString(), QString("MOTOR_RESET"));
    QCOMPARE(motor->currentData().toInt(), 0x11);
    QVERIFY(value->isHidden()); QVERIFY(value->text().isEmpty());
    motor->setCurrentIndex(motor->findData(0x12));
    button(widget, "discardMappingStep")->click();
    QCOMPARE(operation->currentData().toString(), QString("MOTOR_RESET"));
    QCOMPARE(motor->currentData().toInt(), 0x12);
    button(widget, "saveMappingStep")->click(); QCOMPARE(steps->rowCount(), 2);

    const QString render = qEnvironmentVariable("TRANSLATION_RENDER_DIR");
    widget.resize(1100, 740); widget.show();
    widget.findChild<QTabWidget *>("mainTabs")->setCurrentIndex(1);
    if (!render.isEmpty()) {
        QDir().mkpath(render); QCoreApplication::processEvents();
        QVERIFY(widget.grab().save(QDir(render).filePath("no-parameter-reset-group.png")));
    }
    button(widget, "addMapping")->click(); button(widget, "saveConfig")->click();
    const auto pair = mappingRows(widget).last().toObject();
    QCOMPARE(pair.value("steps").toArray().size(), 2);
    for (int index = 0; index < 2; ++index) {
        const auto step = pair.value("steps").toArray().at(index).toObject();
        QCOMPARE(step.value("operation").toString(), QString("MOTOR_RESET"));
        const auto parameters = step.value("parameters").toObject();
        QVERIFY(!parameters.contains("positionUm"));
        QCOMPARE(parameters.value("devId").toObject().value("value").toInt(), 0x11 + index);
    }
    Widget restored(nullptr, path); QCOMPARE(mappingRows(restored).last().toObject(), pair);
    auto *input = restored.findChild<QLineEdit *>("customerInput");
    for (const QString &code : {QString("PAIR"), QString("RST")}) {
        input->setText("!" + code + "\\r"); button(restored, "previewButton")->click();
        const auto output = restored.findChild<QTextEdit *>("targetOutput")->toPlainText();
        QVERIFY2(output.contains("11 44 00 03 E8 00 00 00"), qPrintable(output));
        QVERIFY(output.contains("12 44 00 03 E8 00 00 00"));
        QVERIFY(output.contains(QString::fromUtf8(u8"第2条预览帧")));
    }
    input->setText("!FIXED\\r"); button(restored, "previewButton")->click();
    QVERIFY(restored.findChild<QTextEdit *>("targetOutput")->toPlainText().contains("12 41 00 03 E8 00 04 D2"));

    widget.findChild<QLineEdit *>("mappingCode")->setText("MIXED");
    operation->setCurrentIndex(operation->findData("MOTOR_RESET"));
    button(widget, "saveMappingStep")->click();
    button(widget, "newMappingStep")->click();
    operation->setCurrentIndex(operation->findData("MOTOR_FORWARD"));
    QCOMPARE(source->findData("@none"), -1);
    QCOMPARE(source->currentData().toString(), QString("@constant"));
    QCOMPARE(source->count(), 2); // 未选择/固定值，不提供已省略的客户字段。
    QCOMPARE(mode->currentData().toString(), QString("none"));
    QVERIFY(!value->isHidden()); QVERIFY(value->text().isEmpty());
    const int mappingCount = mappingRows(widget).size();
    button(widget, "saveMappingStep")->click(); QCOMPARE(steps->rowCount(), 1);
    QVERIFY(widget.findChild<QLabel *>("validationState")->text().contains(QString::fromUtf8(u8"目标位置")));
    button(widget, "addMapping")->click(); QCOMPARE(mappingRows(widget).size(), mappingCount);
    QCOMPARE(operation->currentData().toString(), QString("MOTOR_FORWARD"));
    QCOMPARE(steps->rowCount(), 1); QVERIFY(value->text().isEmpty());
    value->setText("1234");
    if (!render.isEmpty()) {
        widget.findChild<QLabel *>("validationState")->clear(); QCoreApplication::processEvents();
        QVERIFY(widget.grab().save(QDir(render).filePath("no-parameter-fixed-position.png")));
    }
    button(widget, "saveMappingStep")->click();
    button(widget, "addMapping")->click(); QCOMPARE(mappingRows(widget).size(), mappingCount + 1);
    const auto mixed = mappingRows(widget).last().toObject();
    QCOMPARE(mixed.value("steps").toArray().size(), 2);
    QCOMPARE(mixed.value("input").toObject().value("omitFields").toArray(), pair.value("input").toObject().value("omitFields").toArray());
    QCOMPARE(mixed.value("steps").toArray().last().toObject().value("parameters").toObject().value("positionUm").toObject().value("value").toInt(), 1234);

    // 单条入口保留原行为；组输入切换不得自动替换已有字段来源。
    widget.findChild<QLineEdit *>("mappingCode")->setText("INPUT");
    QVERIFY(source->itemText(source->findData("@none")).contains(QString::fromUtf8(u8"使用固定位置")));
    source->setCurrentIndex(source->findData("@none")); value->setText("25");
    button(widget, "saveMappingStep")->click(); QCOMPARE(steps->rowCount(), 1);
    QCOMPARE(source->findData("@none"), -1);
    mode->setCurrentIndex(mode->findData("parameters"));
    source->setCurrentIndex(source->findData(QString::fromUtf8(u8"距离")));
    button(widget, "saveMappingStep")->click();
    mode->setCurrentIndex(mode->findData("none"));
    QCOMPARE(source->currentData().toString(), QString::fromUtf8(u8"距离"));
    QVERIFY(source->currentText().contains(QString::fromUtf8(u8"不可用")));
    button(widget, "addMapping")->click(); QCOMPARE(mappingRows(widget).size(), mappingCount + 1);
    QCOMPARE(source->currentData().toString(), QString::fromUtf8(u8"距离"));
    mode->setCurrentIndex(mode->findData("parameters"));
    QVERIFY(!source->currentText().contains(QString::fromUtf8(u8"不可用")));
    QCOMPARE(source->currentData().toString(), QString::fromUtf8(u8"距离"));
    button(widget, "addMapping")->click(); QCOMPARE(mappingRows(widget).size(), mappingCount + 2);

    widget.findChild<QLineEdit *>("mappingCode")->setText("REPEAT");
    operation->setCurrentIndex(operation->findData("MOTOR_RESET"));
    motor->setCurrentIndex(motor->findData(0x13));
    button(widget, "saveMappingStep")->click();
    button(widget, "newMappingStep")->click(); button(widget, "saveMappingStep")->click();
    button(widget, "addMapping")->click();
    const auto repeated = mappingRows(widget).last().toObject().value("steps").toArray();
    QCOMPARE(repeated.size(), 2); QCOMPARE(repeated.first(), repeated.last());

    widget.findChild<QLineEdit *>("mappingCode")->setText("DISCARD");
    operation->setCurrentIndex(operation->findData("MOTOR_RESET"));
    button(widget, "saveMappingStep")->click();
    button(widget, "newMappingStep")->click(); button(widget, "discardMappingStep")->click();
    button(widget, "addMapping")->click();
    QVERIFY(!mappingRows(widget).last().toObject().contains("steps"));
    QCOMPARE(mappingRows(widget).last().toObject().value("operation").toString(), QString("MOTOR_RESET"));
}

void WidgetTest::oneToManyMappingEditorLifecycle()
{
    const QString path = freshDraftPath(), exported = freshDraftPath();
    Widget widget(nullptr, path); applyRoot(widget, listExample()); widget.resize(1100, 740); widget.show();
    widget.findChild<QTabWidget *>("mainTabs")->setCurrentIndex(1);
    auto *code = widget.findChild<QLineEdit *>("mappingCode"); code->setText("GROUP");
    auto *operation = widget.findChild<QComboBox *>("mappingOperation");
    auto *source = widget.findChild<QComboBox *>("mappingSource");
    auto *counts = widget.findChild<QTableWidget *>("mappingParameterCounts"); qobject_cast<QSpinBox *>(counts->cellWidget(0, 1))->setValue(3);
    source->setCurrentIndex(parameterChoice(source, "values", 1));
    button(widget, "saveMappingStep")->click(); auto *steps = widget.findChild<QTableWidget *>("mappingSteps"); QCOMPARE(steps->rowCount(), 1);
    button(widget, "newMappingStep")->click(); operation->setCurrentIndex(operation->findData("QUSHENG_C1"));
    auto *parameters = widget.findChild<QTableWidget *>("mappingParameters");
    for (const auto &entry : {qMakePair(QString("xPosition"), 1), qMakePair(QString("yPosition"), 2), qMakePair(QString("zPosition"), 3)}) {
        auto *box = qobject_cast<QComboBox *>(parameters->cellWidget(parameterRow(parameters, entry.first), 1));
        box->setCurrentIndex(parameterChoice(box, "values", entry.second));
    }
    int module = parameterRow(parameters, "moduleIndex"); auto *moduleSource = qobject_cast<QComboBox *>(parameters->cellWidget(module, 1));
    moduleSource->setCurrentIndex(moduleSource->findData("@constant")); qobject_cast<QLineEdit *>(parameters->cellWidget(module, 2))->setText("35");
    button(widget, "saveMappingStep")->click(); QCOMPARE(steps->rowCount(), 2);
    button(widget, "newMappingStep")->click(); operation->setCurrentIndex(operation->findData("MOTOR_FORWARD")); source->setCurrentIndex(parameterChoice(source, "values", 1));
    button(widget, "saveMappingStep")->click(); QCOMPARE(steps->rowCount(), 3);
    button(widget, "mappingStepUp")->click(); QCOMPARE(steps->currentRow(), 1); QVERIFY(steps->item(1, 1)->text().contains(QString::fromUtf8(u8"前进")));
    button(widget, "mappingStepDown")->click(); QCOMPARE(steps->currentRow(), 2);
    steps->setCurrentCell(1, 0); QCOMPARE(operation->currentData().toString(), QString("QUSHENG_C1"));
    module = parameterRow(parameters, "moduleIndex"); QCOMPARE(qobject_cast<QLineEdit *>(parameters->cellWidget(module, 2))->text(), QString("35"));
    const QString render = qEnvironmentVariable("TRANSLATION_RENDER_DIR");
    if (!render.isEmpty()) {
        QDir().mkpath(render); QCoreApplication::processEvents();
        QVERIFY(widget.grab().save(QDir(render).filePath("one-to-many-steps.png")));
    }
    qobject_cast<QLineEdit *>(parameters->cellWidget(module, 2))->setText("bad"); steps->setCurrentCell(0, 0);
    QCOMPARE(steps->currentRow(), 1); QCOMPARE(qobject_cast<QLineEdit *>(parameters->cellWidget(module, 2))->text(), QString("bad"));
    button(widget, "discardMappingStep")->click(); QCOMPARE(qobject_cast<QLineEdit *>(parameters->cellWidget(module, 2))->text(), QString("35"));
    steps->setCurrentCell(2, 0); button(widget, "deleteMappingStep")->click(); QCOMPARE(steps->rowCount(), 2);
    button(widget, "newMappingStep")->click(); operation->setCurrentIndex(operation->findData("MOTOR_FORWARD")); source->setCurrentIndex(parameterChoice(source, "values", 1)); button(widget, "saveMappingStep")->click(); QCOMPARE(steps->rowCount(), 3);
    steps->setCurrentCell(0, 0); source->setCurrentIndex(parameterChoice(source, "values", 2));
    widget.findChild<QComboBox *>("mappingMotor")->setCurrentIndex(widget.findChild<QComboBox *>("mappingMotor")->findData(0x12));
    // Only explicit step save changes the list; final add uses the visible list.
    button(widget, "saveMappingStep")->click();
    button(widget, "addMapping")->click(); QCOMPARE(mappingRows(widget).size(), 4);
    button(widget, "saveConfig")->click(); const auto saved = draftRoot(widget); const auto mapping = saved.value("mappings").toArray().last().toObject();
    const auto configured = mapping.value("steps").toArray(); QCOMPARE(configured.size(), 3);
    QCOMPARE(configured[0].toObject().value("parameters").toObject().value("positionUm").toObject().value("itemIndex").toInt(), 2);
    QCOMPARE(configured[0].toObject().value("parameters").toObject().value("devId").toObject().value("value").toInt(), 18);
    QCOMPARE(configured[1].toObject().value("parameters").toObject().value("moduleIndex").toObject().value("value").toInt(), 35);
    QCOMPARE(mapping.value("input").toObject().value("parameterCounts").toObject().value("values").toInt(), 3);
    QVERIFY(!mapping.contains("operation")); QVERIFY(!mapping.contains("parameters"));
    Widget restored(nullptr, path); QCOMPARE(draftRoot(restored), saved);
    auto fileAction = [&restored](const char *name, const QString &filePath) {
        bool seen = false;
        QTimer::singleShot(0, &restored, [&restored, &seen, filePath]() {
            auto *dialog = restored.findChild<QFileDialog *>(); if (!dialog) return;
            seen = true; dialog->selectFile(filePath); QMetaObject::invokeMethod(dialog, "accept", Qt::QueuedConnection);
        });
        button(restored, name)->click(); return seen;
    };
    QVERIFY(fileAction("exportConfig", exported)); QVERIFY(fileAction("importConfig", exported)); QCOMPARE(draftRoot(restored), saved);
    widget.findChild<QLineEdit *>("customerInput")->setText("!GROUP=20,30,40\\r"); button(widget, "previewButton")->click();
    const QString output = widget.findChild<QTextEdit *>("targetOutput")->toPlainText();
    QVERIFY(output.contains(QString::fromUtf8(u8"第1条预览帧"))); QVERIFY(output.contains(QString::fromUtf8(u8"第3条预览帧")));
    QVERIFY(widget.findChild<QTextEdit *>("previewOutput")->toPlainText().contains(QString::fromUtf8(u8"共3条")));
    if (!render.isEmpty()) {
        widget.findChild<QTabWidget *>("mainTabs")->setCurrentIndex(2); QCoreApplication::processEvents();
        QVERIFY(widget.grab().save(QDir(render).filePath("one-to-many-preview.png")));
    }
    auto *fields = widget.findChild<QTableWidget *>("fieldTable"); fields->item(1, 1)->setText("renamed"); button(widget, "saveConfig")->click();
    const auto renamed = draftRoot(widget).value("mappings").toArray().last().toObject();
    for (const auto &step : renamed.value("steps").toArray())
        for (const auto &expression : step.toObject().value("parameters").toObject())
            if (expression.toObject().contains("field")) QCOMPARE(expression.toObject().value("field").toString(), QString("renamed"));
    auto *type = qobject_cast<QComboBox *>(fields->cellWidget(1, 3)); type->setCurrentIndex(type->findData("int")); button(widget, "saveConfig")->click();
    QVERIFY(widget.findChild<QLabel *>("validationState")->text().contains(QString::fromUtf8(u8"有误")));
    button(widget, "previewButton")->click(); QVERIFY(widget.findChild<QTextEdit *>("targetOutput")->toPlainText().isEmpty());
}

void WidgetTest::oneToManySharedInputAndPendingEdit()
{
    Widget widget(nullptr, freshDraftPath()); applyRoot(widget, listExample());
    auto *operation = widget.findChild<QComboBox *>("mappingOperation"); auto *source = widget.findChild<QComboBox *>("mappingSource");
    widget.findChild<QLineEdit *>("mappingCode")->setText("SHARED"); operation->setCurrentIndex(operation->findData("QUERY_MOTOR"));
    button(widget, "saveMappingStep")->click(); auto *steps = widget.findChild<QTableWidget *>("mappingSteps"); QCOMPARE(steps->rowCount(), 1);
    auto *mode = widget.findChild<QComboBox *>("groupInputMode"); QCOMPARE(mode->currentData().toString(), QString("none"));
    mode->setCurrentIndex(mode->findData("parameters"));
    auto *counts = widget.findChild<QTableWidget *>("mappingParameterCounts"); QCOMPARE(counts->rowCount(), 1); qobject_cast<QSpinBox *>(counts->cellWidget(0, 1))->setValue(3);
    button(widget, "newMappingStep")->click(); operation->setCurrentIndex(operation->findData("MOTOR_FORWARD")); source->setCurrentIndex(parameterChoice(source, "values", 2)); button(widget, "saveMappingStep")->click(); QCOMPARE(steps->rowCount(), 2);
    button(widget, "newMappingStep")->click(); operation->setCurrentIndex(operation->findData("MOTOR_STOP"));
    QCOMPARE(mode->currentData().toString(), QString("parameters")); QCOMPARE(counts->rowCount(), 1);
    QCOMPARE(qobject_cast<QSpinBox *>(counts->cellWidget(0, 1))->value(), 3);
    button(widget, "saveMappingStep")->click(); QCOMPARE(steps->rowCount(), 3);
    button(widget, "addMapping")->click(); QCOMPARE(mappingRows(widget).size(), 4);
    const auto mapping = mappingRows(widget).last().toObject(); QVERIFY(mapping.value("input").toObject().value("omitFields").toArray().isEmpty());
    QCOMPARE(mapping.value("input").toObject().value("parameterCounts").toObject().value("values").toInt(), 3);
    button(widget, "saveConfig")->click(); widget.findChild<QLineEdit *>("customerInput")->setText("!SHARED=20,30,40\\r"); button(widget, "previewButton")->click();
    QVERIFY2(widget.findChild<QTextEdit *>("targetOutput")->toPlainText().contains(QString::fromUtf8(u8"第3条")), qPrintable(widget.findChild<QTextEdit *>("previewOutput")->toPlainText()));
    auto *service = widget.findChild<TranslationService *>(); QVERIFY(service);
    QSignalSpy completed(service, &TranslationService::groupCompleted);
    button(widget, "sendButton")->click(); QTRY_COMPARE(completed.size(), 1);
    QVERIFY(widget.findChild<QLabel *>("requestState")->text().contains(QString::fromUtf8(u8"组")));
    QVERIFY(widget.findChild<QTextEdit *>("replyResult")->toPlainText().contains(QString::fromUtf8(u8"剩余2条未发送")));
    widget.findChild<QLineEdit *>("mappingCode")->setText("PENDING"); source->setCurrentIndex(source->findData("@constant"));
    widget.findChild<QLineEdit *>("mappingValue")->setText("50"); button(widget, "saveMappingStep")->click();
    button(widget, "newMappingStep")->click(); source->setCurrentIndex(source->findData("@constant")); widget.findChild<QLineEdit *>("mappingValue")->setText("bad");
    button(widget, "addMapping")->click(); QCOMPARE(mappingRows(widget).size(), 4); // Invalid pending step cannot disappear.
    QCOMPARE(widget.findChild<QLineEdit *>("mappingValue")->text(), QString("bad"));
    button(widget, "discardMappingStep")->click(); QCOMPARE(steps->rowCount(), 1);
}

QTEST_MAIN(WidgetTest)
#include "tst_widget.moc"
