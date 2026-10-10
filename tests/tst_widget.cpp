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
#include "widget.h"
#include "transport/serialtransport.h"

class WidgetTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void draftLifecycleAndVisibleFailures();
    void advancedJsonAndDynamicControls();
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
    QDir::setCurrent(QFileInfo(QString::fromLatin1(__FILE__)).dir().absoluteFilePath(".."));
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
    QVERIFY(preview->toPlainText().contains(QString::fromUtf8(u8"20000")));
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
    QDir::setCurrent(QFileInfo(QString::fromLatin1(__FILE__)).dir().absoluteFilePath(".."));
    Widget widget;
    auto *fields = widget.findChild<QTableWidget *>("fieldTable");
    auto *maps = widget.findChild<QTableWidget *>("mappingTable");
    auto *json = widget.findChild<QTextEdit *>("advancedJson");
    QVERIFY(fields && maps && json);
    QCOMPARE(fields->horizontalHeaderItem(2)->text(), QString::fromUtf8(u8"字段作用"));
    QVERIFY(fields->isColumnHidden(6));
    QVERIFY(maps->isColumnHidden(6));
    auto *kind = qobject_cast<QComboBox *>(maps->cellWidget(0, 4));
    kind->setCurrentIndex(kind->findData("constant"));
    QVERIFY(!maps->isColumnHidden(6)); QVERIFY(maps->isColumnHidden(5)); QVERIFY(maps->isColumnHidden(7));
    kind->setCurrentIndex(kind->findData("linear"));
    fields->item(2, 1)->setText(QString::fromUtf8(u8"新位置"));
    auto *source = qobject_cast<QComboBox *>(maps->cellWidget(0, 5));
    QVERIFY(source->currentText().contains(QString::fromUtf8(u8"未找到字段")));
    source->setCurrentIndex(source->findData(QString::fromUtf8(u8"新位置")));
    button(widget, QString::fromUtf8(u8"保存到草稿"))->click();
    const QString saved = json->toPlainText();
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

void WidgetTest::renderChinesePages()
{
    QDir::setCurrent(QFileInfo(QString::fromLatin1(__FILE__)).dir().absoluteFilePath(".."));
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
