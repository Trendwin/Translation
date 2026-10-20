#include <QtTest>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTemporaryDir>

#include "config/protocolconfig.h"
#include "core/conversionengine.h"
#include "core/requestmanager.h"
#include "demo/demointernalprotocol.h"
#include "qusheng/qushengprotocol.h"
#include "qusheng/qushengcommandcatalog.h"
#include "qusheng/sdk/qushengsdk.h"

class FakeTransport : public ITransport
{
public:
    explicit FakeTransport(QObject *parent = nullptr) : ITransport(parent) {}
    bool isConnected() const override { return connected; }
    void sendBytes(quint64 id, const QByteArray &data) override
    {
        sent.append(data);
        if (!stall) QTimer::singleShot(0, this, [this, id]() { emit sendFinished(id, true, QString()); });
    }
    void receive(const QByteArray &data) { emit bytesReceived(data); }
    void disconnectNow() { connected = false; emit connectionChanged(false, QStringLiteral("test disconnect")); }
    QList<QByteArray> sent;
    bool connected = true;
    bool stall = false;
};

class FrameworkTest : public QObject
{
    Q_OBJECT
private slots:
    void textTemplatesAndIndependentParsing();
    void textTemplateValidationAndFailures();
    void textTemplateMappingAndStreaming();
    void configuredDtStyleAndRoundTrip();
    void debugSequenceWraps();
    void debugRelativeSendsWithoutPreflight();
    void debugQueuedAddressesAreSnapshots();
    void configurationChangesNeedNoCppChange();
    void genericParserRejectsInvalidInputs();
    void hexEndianAndGenericStreamFraming();
    void finalQushengFrameMatchesSample();
    void runtimeHasExactlyOneDeviceTransport();
    void strictConfigurationAndByteInput();
    void parameterRelationsAndRounding();
    void signedFixedFieldsAndOverflow();
    void fixedBinaryFramesWithEmbeddedFooter();
    void ackTimeoutAndNoCompletionPolling();
    void cancelThenQueryRecoversBusyState();
    void demoFramingRegression();
    void codecReplyAndCrc();
    void capturedMotorReplyEndsWaiting();
    void sequenceExhaustionIsFailClosed();
    void managerAckIsNotCompletion();
    void managerStalledWriteAndRejection();
    void simpleCommandExamples_data();
    void simpleCommandExamples();
    void mappedInputFormatsAndRoundTrip();
    void mappedConfigurationRejectsInvalidFormats();
    void deviceVersionReply();
    void relativeMoveChecksPositionBeforeSending();
    void relativeMoveRejectsUnsafeStartAndTimeout();
    void completeCommandCatalogMatchesProtocol();
    void pendingCommandMappingsRoundTripAndRefuseExecution();
    void pendingParameterRelationshipsAreValidated();
    void sharedFooterAndLegacyResponse();
    void mappingSourcesRespectChangedFormat();
    void sdkSimpleControlsFromConfig();
    void sdkOrdinaryRequestLifecycle();
    void sdkQueryDoesNotUnlockOtherMotor();
    void fixedMotorActionTemplates();
    void stopAndResetLifecycle_data();
    void stopAndResetLifecycle();
    void stopInterruptsMotionAndCancelsQueue();
    void stopWaitsForWrite_data();
    void stopWaitsForWrite();
    void motorControlErrorsAllowNextSend();
    void consecutiveMovesNeedNoStatusQuery();
    void queuedMovesWaitForReplyAndCompleteWrite();
    void reportedStatusDoesNotGateSending_data();
    void reportedStatusDoesNotGateSending();
    void relativePreflightWaitsForWrite();
    void finalControlReplyWaitsForWrite();
    void cancellingQueuedStopKeepsOriginalActive();
    void fixedMotorActionRequestLifecycle_data();
    void fixedMotorActionRequestLifecycle();
};

static QString example(const QString &name)
{
    // qmake 在独立构建目录中把 __FILE__ 设为相对路径，必须相对构建目录解析，不能依赖启动目录。
    const QString source = QDir(QString::fromLatin1(QT_TESTCASE_BUILDDIR)).absoluteFilePath(QString::fromLatin1(__FILE__));
    return QFileInfo(source).dir().filePath("../examples/" + name);
}


void FrameworkTest::textTemplatesAndIndependentParsing()
{
    ProtocolConfig config; TranslationError error; ConversionPreview p;
    QVERIFY2(ProtocolConfig::load(example("text-template.json"), &config, &error), qPrintable(error.message));
    // The user's customer identifiers are test fixtures, never parser branches.
    auto external = config.root.value("external").toObject();
    auto format = external.value("textFormat").toObject();
    auto commands = format.value("commands").toArray();
    for (int i=0;i<3;++i) {
        auto c=commands.at(i).toObject(); c.insert("code",QStringList{"MFCV","XYZ_PX","DI_PDDA"}.at(i)); commands.replace(i,c);
    }
    format.insert("commands",commands); external.insert("textFormat",format);
    // Inactive legacy fields must not select command names or shift streaming boundaries.
    external.insert("fields",QJsonArray{QJsonObject{{"name","oldCommand"},{"role","command"},{"length",4000},{"lengthMode","fixed"}}});
    config.root.insert("external",external);
    config.root.insert("device",QJsonObject{{"destination","invalid business value"}});
    for (const int sequence : {1,12,255}) {
        const QByteArray input="@&5A&01&SCT&"+QByteArray::number(sequence)+"&0;MFCV=255;10\nEND&?&\r\n";
        QVERIFY2(ConversionEngine(config).parsePreview(input,&p,&error),qPrintable(error.message));
        QCOMPARE(p.fields.value("source").toString(),QString("5A"));
        QCOMPARE(p.fields.value("sequence").toInt(),sequence);
        QCOMPARE(p.fields.value("value").toInt(),255);
        QCOMPARE(p.fields.value("delay").toInt(),10);
        QCOMPARE(p.fields.value("checksum").toString(),QString("?"));
        QCOMPARE(p.notices.size(),3); QVERIFY(p.targetFrame.isEmpty());
    }
    QVERIFY(ConversionEngine(config).parsePreview("@&5A&01&SCT&1&0;XYZ_PX=234;10\nEND&?&\r\n",&p,&error));
    QCOMPARE(p.fields.value("position").toInt(),234);
    QVERIFY(ConversionEngine(config).parsePreview("@&5A&01&SCT&1&0;DI_PDDA=20,-200,30000;10\nEND&?&\r\n",&p,&error));
    QCOMPARE(p.fields.value("x").toInt(),20); QCOMPARE(p.fields.value("y").toInt(),-200); QCOMPARE(p.fields.value("z").toInt(),30000);
    QVERIFY(p.fieldOrder.indexOf("x")<p.fieldOrder.indexOf("y"));
    QVERIFY(ConversionEngine(config).parsePreview("@&5A&01&SCT&1&0;PING=;10\nEND&?&\r\n",&p,&error));
    QTemporaryDir dir; QVERIFY(config.save(dir.filePath("format.json"),&error));
    ProtocolConfig restored; QVERIFY(ProtocolConfig::load(dir.filePath("format.json"),&restored,&error)); QCOMPARE(restored.root,config.root);
}

void FrameworkTest::textTemplateValidationAndFailures()
{
    ProtocolConfig config; TranslationError e; ConversionPreview p;
    QVERIFY(ProtocolConfig::load(example("text-template.json"),&config,&e));
    const QByteArray prefix="@&5A&01&SCT&1&0;", suffix=";10\nEND&?&\r\n";
    for (const QByteArray bad : {QByteArray("VECTOR=1,2"),QByteArray("VECTOR=1,2,3,4"),QByteArray("VECTOR=1,2,abc"),QByteArray("VECTOR=1,2,3\n"),QByteArray("UNKNOWN=2"),QByteArray("SET=9223372036854775808"),QByteArray("SET=3\nSET=4")}) {
        QVERIFY(ConversionEngine(config).parsePreview(prefix+"SET=10"+suffix,&p,&e));
        QVERIFY2(!ConversionEngine(config).parsePreview(prefix+bad+suffix,&p,&e),bad.constData());
        QVERIFY(p.fields.isEmpty()); QVERIFY(p.targetFrame.isEmpty()); QVERIFY(e.isValid());
    }
    QVERIFY(!ConversionEngine(config).parsePreview("@&5A&01&UNKNOWN&1&0;SET=10"+suffix,&p,&e)); QCOMPARE(e.code,QString("FIELD_CONSTANT"));
    QVERIFY(!ConversionEngine(config).parsePreview(prefix+"SET=10;10\nWRONG&?&\r\n",&p,&e));
    QVERIFY(!ConversionEngine(config).parsePreview(prefix+"SET=10"+suffix+"extra",&p,&e));
    QVERIFY(!ConversionEngine(config).parsePreview(prefix+"SET=10"+suffix+prefix+"SET=20"+suffix,&p,&e));QCOMPARE(e.code,QString("FRAME_MULTIPLE"));
    const auto original=config.root.value("external").toObject();
    for (const QString bad : {QString("{command}{arguments}"),QString("{command}={command}"),QString("{command}={arguments};{undeclared}"),QString("{command}={arguments}\\q"),QString("{command}={arguments" )}) {
        auto ext=original; auto f=ext.value("textFormat").toObject();f.insert("template",bad);ext.insert("textFormat",f);config.root.insert("external",ext);QVERIFY2(!config.validateFormat(&e),qPrintable(bad));
    }
    auto ext=original;auto f=ext.value("textFormat").toObject();auto cmds=f.value("commands").toArray();auto c=cmds.at(0).toObject();c.insert("template",QString::fromUtf8(u8"{数值}"));c.insert("fields",QJsonArray{QJsonObject{{"name",QString::fromUtf8(u8"数值")},{"type","uint"},{"role","parameter"}}});cmds.replace(0,c);f.insert("commands",cmds);ext.insert("textFormat",f);config.root.insert("external",ext);
    QVERIFY2(ConversionEngine(config).parsePreview(prefix+"SET=10"+suffix,&p,&e),qPrintable(e.message));QCOMPARE(p.fields.value(QString::fromUtf8(u8"数值")).toInt(),10);
}

void FrameworkTest::textTemplateMappingAndStreaming()
{
    ProtocolConfig config;TranslationError e;ConversionPreview p;
    QVERIFY(ProtocolConfig::load(example("text-template-mapping.json"),&config,&e));
    auto ext=config.root.value("external").toObject();ext.insert("fields",QJsonArray{QJsonObject{{"name","oldCommand"},{"role","command"},{"length",4090},{"lengthMode","fixed"}}});config.root.insert("external",ext);
    QVERIFY2(ConversionEngine(config).convert("[12|MOVE:234]\r",false,&p,&e),qPrintable(e.message));
    QCOMPARE(p.command.parameters.value("positionUm").toInt(),234); QVERIFY(!p.targetFrame.isEmpty());
    ConversionEngine stream(config);QList<TranslationError> errors;
    QVERIFY(stream.feed("noise[12|MO",&errors).isEmpty());
    const auto frames=stream.feed("VE:234]\r[255|TRIPLE:20/30/40]\r",&errors);QCOMPARE(frames.size(),2);QVERIFY(errors.isEmpty());
    QVERIFY(stream.parsePreview(frames.last(),&p,&e));QCOMPARE(p.fields.value("z").toInt(),40);
    QVERIFY(!stream.convert(frames.last(),false,&p,&e));QVERIFY(p.targetFrame.isEmpty());
    auto format=ext.value("textFormat").toObject();auto fields=format.value("fields").toArray();auto field=fields.at(0).toObject();field.insert("role","execution");fields.replace(0,field);format.insert("fields",fields);ext.insert("textFormat",format);config.root.insert("external",ext);
    QVERIFY(ConversionEngine(config).parsePreview("[12|MOVE:234]\r",&p,&e));
    QVERIFY(!ConversionEngine(config).convert("[12|MOVE:234]\r",false,&p,&e));QCOMPARE(e.code,QString("EXECUTION_SEMANTICS"));QVERIFY(p.targetFrame.isEmpty());
    auto mappings=config.root.value("mappings").toArray();auto mapping=mappings.at(0).toObject();mapping.insert("parameters",QJsonObject{{"positionUm",QJsonObject{{"kind","field"},{"field","x"}}}});mappings.replace(0,mapping);config.root.insert("mappings",mappings);
    QVERIFY(!config.validate(&e));QCOMPARE(e.code,QString("CONFIG_EXPRESSION"));
}

void FrameworkTest::completeCommandCatalogMatchesProtocol()
{
    // 从V0.1.7第3.2章独立列出命令码；不能仅验证目录自己宣称的数量。
    const QStringList expected = QStringLiteral("01 03 04 05 08 09 0B 0C 11 12 13 14 15 16 17 1A 21 22 23 24 25 44 45 46 54 63 64 65 71 72 73 74 81 82 83 84 91 92 A1 C1 C2 C3 D1 D2 D3 D4 D6 D7 D8 D9 E1 E2 E3 E4 E5 E6").split(' ');
    const QSet<QString> receiveOnly = {"0x17", "0x45", "0xD9", "0xE6"};
    const QJsonArray commands = QushengCommandCatalog::commands();
    QCOMPARE(commands.size(), 56);
    QCOMPARE(QushengCommandCatalog::options().size(), 59);
    QSet<QString> actual, operations;
    int rxCount = 0, executableOptions = 0;
    for (int i = 0; i < commands.size(); ++i) {
        const QJsonObject command = commands.at(i).toObject();
        const QString code = command.value("code").toString();
        const QString operation = command.value("operation").toString();
        QVERIFY(!actual.contains(code));
        QVERIFY(!operations.contains(operation));
        actual.insert(code); operations.insert(operation);
        QCOMPARE(command.value("section").toString(), QStringLiteral("3.2.%1").arg(i + 1));
        QVERIFY(!command.value("name").toString().isEmpty());
        const auto sdkDefinition = QushengSdk::definition(quint8(code.mid(2).toUInt(nullptr, 16)));
        QCOMPARE(sdkDefinition.code, quint8(code.mid(2).toUInt(nullptr, 16)));
        QVERIFY(!sdkDefinition.name.isEmpty()); // 所有协议码均登记接口，不能只用总数掩盖缺失。
        QVERIFY(!command.value("category").toString().isEmpty());
        QVERIFY(!command.value("description").toString().isEmpty());
        QVERIFY(command.value("parameters").isArray());
        const bool rx = QushengCommandCatalog::isReceiveOnly(operation);
        QCOMPARE(rx, receiveOnly.contains(code));
        if (rx) { ++rxCount; QVERIFY(!QushengCommandCatalog::isExecutable(operation)); }
        for (const QJsonValue &value : command.value("parameters").toArray()) {
            const QJsonObject parameter = value.toObject();
            QVERIFY(!parameter.value("key").toString().isEmpty());
            QVERIFY(!parameter.value("label").toString().isEmpty());
            QVERIFY(!parameter.value("description").toString().isEmpty());
            if (parameter.value("editable").toBool()) {
                QVERIFY(parameter.contains("min"));
                QVERIFY(parameter.contains("max"));
            }
        }
    }
    for (const QString &code : expected) QVERIFY(actual.contains("0x" + code));
    QCOMPARE(rxCount, 4);
    QCOMPARE(QushengCommandCatalog::find("QUSHENG_11").value("code").toString(), QStringLiteral("0x11"));
    QVERIFY(!QushengCommandCatalog::isExecutable("QUSHENG_11"));
    for (const QJsonValue &entry : QushengCommandCatalog::options())
        executableOptions += QushengCommandCatalog::isExecutable(entry.toObject().value("operation").toString()) ? 1 : 0;
    QCOMPARE(executableOptions, 33);
    QVERIFY(QushengCommandCatalog::find("NOT_IN_PROTOCOL").isEmpty());
    QVERIFY(!QushengCommandCatalog::isExecutable("NOT_IN_PROTOCOL"));
    QVERIFY(QushengCommandCatalog::find("QUSHENG_C1").value("note").toString().contains(QString::fromUtf8(u8"重叠")));
    QVERIFY(QushengCommandCatalog::find("QUSHENG_92").value("note").toString().contains("D4～D6"));
}

void FrameworkTest::pendingCommandMappingsRoundTripAndRefuseExecution()
{
    ProtocolConfig config;
    TranslationError error;
    QVERIFY2(ProtocolConfig::load(example("command-catalog-draft.json"), &config, &error), qPrintable(error.message));
    ConversionPreview result;
    QVERIFY2(ConversionEngine(config).convert("!MOVE2000\r", false, &result, &error), qPrintable(error.message));
    const QByteArray goodFrame = result.targetFrame;
    QVERIFY(!goodFrame.isEmpty());
    QVERIFY(ConversionEngine(config).convert("!FAN50\r", false, &result, &error));
    QCOMPARE(quint8(result.targetFrame.at(7)), quint8(0x81));
    QCOMPARE(result.targetFrame.mid(8, 2), QByteArray::fromHex("e032"));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(config.save(dir.filePath("draft.json"), &error));
    ProtocolConfig reloaded;
    QVERIFY(ProtocolConfig::load(dir.filePath("draft.json"), &reloaded, &error));
    QCOMPARE(reloaded.root, config.root);
    QVERIFY(ConversionEngine(reloaded).convert("!MOVE2000\r", false, &result, &error));
    QCOMPARE(result.targetFrame, goodFrame);
    QVERIFY(ConversionEngine(reloaded).convert("!FAN50\r", true, &result, &error));
    QCOMPARE(result.targetFrame.mid(8, 2), QByteArray::fromHex("e032"));

    // 任意目录状态/高级配置标志都不能授予执行权限，真实发送和预览共享同一编译白名单。
    QJsonArray mappings = config.root.value("mappings").toArray();
    QJsonObject pending = mappings.at(1).toObject();
    pending.insert("operation", "QUSHENG_13");
    pending.insert("parameters", QJsonObject());
    pending.insert("executable", true);
    pending.insert("enabled", true);
    pending.insert("catalog", QJsonObject{{"direction", "tx"}, {"executable", true}});
    mappings.replace(1, pending);
    config.root.insert("mappings", mappings);
    QVERIFY(config.validate(&error));
    QVERIFY(!QushengCommandCatalog::isExecutable(pending.value("operation").toString()));
    QVERIFY(!ConversionEngine(config).convert("!FAN50\r", true, &result, &error));
    QCOMPARE(error.code, QStringLiteral("COMMAND_NOT_IMPLEMENTED"));
    QVERIFY(result.targetFrame.isEmpty());
    QVERIFY(config.save(dir.filePath("metadata.json"), &error));
    QVERIFY(ProtocolConfig::load(dir.filePath("metadata.json"), &reloaded, &error));
    QCOMPARE(reloaded.root.value("mappings"), config.root.value("mappings"));

    // 同码0x11的通用模式与已验证绝对位置预设是不同执行状态，不能靠命令码借用发送许可。
    pending.insert("operation", "QUSHENG_11");
    pending.insert("parameters", QJsonObject{{"devId", QJsonObject{{"kind", "constant"}, {"value", 17}}},
                   {"mode", QJsonObject{{"kind", "constant"}, {"value", 1}}},
                   {"action", QJsonObject{{"kind", "constant"}, {"value", 2}}}});
    mappings.replace(1, pending); config.root.insert("mappings", mappings);
    QVERIFY2(config.validate(&error), qPrintable(error.message));
    QVERIFY(!ConversionEngine(config).convert("!FAN50\r", true, &result, &error));
    QCOMPARE(error.code, QStringLiteral("COMMAND_NOT_IMPLEMENTED"));
    QVERIFY(result.targetFrame.isEmpty());

    for (const QString &operation : {QStringLiteral("QUSHENG_17"), QStringLiteral("QUSHENG_45"),
                                    QStringLiteral("QUSHENG_D9"), QStringLiteral("QUSHENG_E6")}) {
        pending.insert("operation", operation);
        pending.insert("parameters", QJsonObject());
        mappings.replace(1, pending); config.root.insert("mappings", mappings);
        QVERIFY(!config.validate(&error));
        QCOMPARE(error.code, QStringLiteral("CONFIG_RECEIVE_ONLY"));
    }
    pending.insert("operation", "QUSHENG_FF");
    mappings.replace(1, pending); config.root.insert("mappings", mappings);
    QVERIFY(!config.validate(&error));
    QCOMPARE(error.code, QStringLiteral("CONFIG_MAPPING"));
}

void FrameworkTest::pendingParameterRelationshipsAreValidated()
{
    ProtocolConfig base;
    TranslationError error;
    QVERIFY(ProtocolConfig::load(example("command-catalog-draft.json"), &base, &error));
    auto withParameters = [&base](const QString &operation, const QJsonObject &parameters) {
        ProtocolConfig config = base;
        QJsonArray mappings = config.root.value("mappings").toArray();
        QJsonObject pending = mappings.at(1).toObject();
        pending.insert("operation", operation); pending.insert("parameters", parameters);
        mappings.replace(1, pending); config.root.insert("mappings", mappings);
        return config;
    };
    // 待实现条目可以暂时不填参数，方便逐步编辑；已填参数必须符合目录标量与来源定义。
    QVERIFY(withParameters("QUSHENG_81", QJsonObject()).validate(&error));
    for (int mode : {1, 2, 4})
        QVERIFY(withParameters("QUSHENG_11", QJsonObject{{"mode", QJsonObject{{"kind", "constant"}, {"value", mode}}}}).validate(&error));
    QVERIFY(!withParameters("QUSHENG_11", QJsonObject{{"mode", QJsonObject{{"kind", "constant"}, {"value", 3}}}}).validate(&error));
    QCOMPARE(error.code, QStringLiteral("CONFIG_EXPRESSION"));
    QVERIFY(withParameters("QUSHENG_A1", QJsonObject{{"pwm", QJsonObject{{"kind", "constant"}, {"value", -100}}}}).validate(&error));
    QVERIFY(withParameters("QUSHENG_71", QJsonObject{{"fileLength", QJsonObject{{"kind", "constant"}, {"value", 4294967295.0}}}}).validate(&error));
    QVERIFY(!withParameters("QUSHENG_A1", QJsonObject{{"pwm", QJsonObject{{"kind", "constant"}, {"value", -129}}}}).validate(&error));
    QCOMPARE(error.code, QStringLiteral("CONFIG_EXPRESSION"));
    QVERIFY(!withParameters("QUSHENG_71", QJsonObject{{"fileLength", QJsonObject{{"kind", "constant"}, {"value", 4294967296.0}}}}).validate(&error));
    QVERIFY(!withParameters("QUSHENG_81", QJsonObject{{"percent", QJsonObject{{"kind", "constant"}, {"value", 101}}}}).validate(&error));
    QVERIFY(!withParameters("QUSHENG_81", QJsonObject{{"unknownParameter", QJsonObject{{"kind", "constant"}, {"value", 10}}}}).validate(&error));
    QCOMPARE(error.code, QStringLiteral("CONFIG_PARAMETER"));
    QVERIFY(!withParameters("QUSHENG_91", QJsonObject{{"modeAndMask", QJsonObject{{"kind", "constant"}, {"value", 10}}}}).validate(&error));
    QCOMPARE(error.code, QStringLiteral("CONFIG_PARAMETER"));
    QVERIFY(!withParameters("QUSHENG_81", QJsonObject{{"percent", QJsonObject{{"kind", "field"}, {"field", QString::fromUtf8(u8"命令码")}}}}).validate(&error));
    QCOMPARE(error.code, QStringLiteral("CONFIG_EXPRESSION"));
    QVERIFY(withParameters("QUSHENG_81", QJsonObject{{"percent", QJsonObject{{"kind", "linear"}, {"field", QString::fromUtf8(u8"数值")}, {"numerator", 1}, {"denominator", 2}, {"offset", 0}, {"round", "exact"}}}}).validate(&error));
}

void FrameworkTest::configuredDtStyleAndRoundTrip()
{
    ProtocolConfig config;
    TranslationError error;
    QVERIFY2(ProtocolConfig::load(example("dt-preview.json"), &config, &error), qPrintable(error.message));
    ConversionEngine engine(config);
    ConversionPreview result;
    QVERIFY2(engine.convert("/1A2000R\r", false, &result, &error), qPrintable(error.message));
    QCOMPARE(result.fields.value(QString::fromUtf8(u8"地址")).toLongLong(), 1LL);
    QCOMPARE(result.fields.value(QString::fromUtf8(u8"命令码")).toString(), QStringLiteral("A"));
    QCOMPARE(result.fields.value(QString::fromUtf8(u8"位置")).toLongLong(), 2000LL);
    QCOMPARE(result.matchedCommandCode, QStringLiteral("A"));
    QCOMPARE(result.command.operation, QStringLiteral("ABSOLUTE_MOVE"));
    QCOMPARE(result.command.parameters.value("positionUm").toLongLong(), 20000LL);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString saved = directory.filePath("saved.json");
    QVERIFY(config.save(saved, &error));
    ProtocolConfig loaded;
    QVERIFY(ProtocolConfig::load(saved, &loaded, &error));
    QCOMPARE(loaded.version, config.version);
    ConversionPreview reloadedResult;
    QVERIFY(ConversionEngine(loaded).convert("/1A2000R\r", false, &reloadedResult, &error));
    QCOMPARE(reloadedResult.command.parameters, result.command.parameters);
    QCOMPARE(reloadedResult.targetFrame, result.targetFrame);
}

void FrameworkTest::configurationChangesNeedNoCppChange()
{
    ProtocolConfig original;
    TranslationError error;
    QVERIFY(ProtocolConfig::load(example("dt-preview.json"), &original, &error));
    QJsonObject root = original.root;
    QJsonObject external = root.value("external").toObject();
    external.insert("header", "#");
    external.insert("footer", "Z\\n");
    root.insert("external", external);
    QJsonArray mappings = root.value("mappings").toArray();
    QJsonObject mapping = mappings.first().toObject();
    mapping.insert("commandCode", "X");
    mappings.replace(0, mapping);
    root.insert("mappings", mappings);
    ProtocolConfig changed;
    QVERIFY2(ProtocolConfig::fromJson(QJsonDocument(root).toJson(), &changed, &error), qPrintable(error.message));
    ConversionPreview result;
    QVERIFY2(ConversionEngine(changed).convert("#1X2000Z\n", false, &result, &error), qPrintable(error.message));
    QCOMPARE(result.command.parameters.value("positionUm").toLongLong(), 20000LL);
    QCOMPARE(result.targetFrame, QByteArray::fromHex("aaaa020108f7001111410003e8004e2027f8"));
}

void FrameworkTest::genericParserRejectsInvalidInputs()
{
    ProtocolConfig config;
    TranslationError error;
    QVERIFY(ProtocolConfig::load(example("dt-preview.json"), &config, &error));
    ConversionEngine engine(config);
    ConversionPreview result;

    QVERIFY(!engine.convert("1A2000R\r", false, &result, &error));
    QCOMPARE(error.code, QStringLiteral("FRAME_HEADER"));
    QVERIFY(!engine.convert("/1A2000R", false, &result, &error));
    QCOMPARE(error.code, QStringLiteral("FRAME_FOOTER"));
    QVERIFY(!engine.convert("/A2000R\r", false, &result, &error));
    QCOMPARE(error.code, QStringLiteral("FIELD_NUMBER"));
    QVERIFY(!engine.convert("/1X2000R\r", false, &result, &error));
    QCOMPARE(error.code, QStringLiteral("COMMAND_UNSUPPORTED"));
    QVERIFY(!engine.convert("/1A20x0R\r", false, &result, &error));
    QCOMPARE(error.code, QStringLiteral("FIELD_NUMBER"));
    QVERIFY(!engine.convert("/1A6001R\r", false, &result, &error));
    QCOMPARE(error.code, QStringLiteral("FIELD_RANGE"));

    QJsonObject missingHeader = config.root;
    QJsonObject external = missingHeader.value("external").toObject();
    external.insert("header", "");
    missingHeader.insert("external", external);
    QVERIFY(!ProtocolConfig::fromJson(QJsonDocument(missingHeader).toJson(), nullptr, &error));
    QCOMPARE(error.code, QStringLiteral("CONFIG_HEADER"));

    QJsonObject fixedLength = config.root;
    external = fixedLength.value("external").toObject();
    QJsonArray fields = external.value("fields").toArray();
    QJsonObject position = fields.at(2).toObject();
    position.insert("lengthMode", "fixed");
    position.insert("length", 4);
    fields.replace(2, position);
    external.insert("fields", fields);
    fixedLength.insert("external", external);
    ProtocolConfig exact;
    QVERIFY(ProtocolConfig::fromJson(QJsonDocument(fixedLength).toJson(), &exact, &error));
    QVERIFY(!ConversionEngine(exact).convert("/1A200R\r", false, &result, &error));
    QCOMPARE(error.code, QStringLiteral("FIELD_LENGTH"));
}

void FrameworkTest::hexEndianAndGenericStreamFraming()
{
    ProtocolConfig binary;
    TranslationError error;
    QVERIFY2(ProtocolConfig::load(example("binary-preview.json"), &binary, &error), qPrintable(error.message));
    ConversionPreview result;
    QVERIFY(ConversionEngine(binary).convert(QByteArray::fromHex("aa550107d00d0a"), false, &result, &error));
    QCOMPARE(result.command.parameters.value("positionUm").toLongLong(), 20000LL);

    QJsonObject root = binary.root;
    QJsonObject external = root.value("external").toObject();
    external.insert("byteOrder", "little");
    root.insert("external", external);
    ProtocolConfig little;
    QVERIFY(ProtocolConfig::fromJson(QJsonDocument(root).toJson(), &little, &error));
    QVERIFY(ConversionEngine(little).convert(QByteArray::fromHex("aa5501d0070d0a"), false, &result, &error));
    QCOMPARE(result.command.parameters.value("positionUm").toLongLong(), 20000LL);

    ProtocolConfig ascii;
    QVERIFY(ProtocolConfig::load(example("dt-preview.json"), &ascii, &error));
    ConversionEngine stream(ascii);
    QList<TranslationError> errors;
    QCOMPARE(stream.feed("noise/1A20", &errors).size(), 0);
    const auto frames = stream.feed("00R\r/1A1000R\r", &errors);
    QCOMPARE(frames.size(), 2);
    QCOMPARE(frames.at(0), QByteArray("/1A2000R\r"));
    QCOMPARE(frames.at(1), QByteArray("/1A1000R\r"));
    stream.feed(QByteArray(4097, 'x'), &errors);
    QCOMPARE(errors.last().code, QStringLiteral("INPUT_OVERFLOW"));
}

void FrameworkTest::finalQushengFrameMatchesSample()
{
    ProtocolConfig first;
    ProtocolConfig second;
    TranslationError error;
    QVERIFY(ProtocolConfig::load(example("dt-preview.json"), &first, &error));
    QVERIFY(ProtocolConfig::load(example("ascii-preview.json"), &second, &error));
    ConversionPreview a;
    ConversionPreview b;
    QVERIFY(ConversionEngine(first).convert("/1A2000R\r", false, &a, &error));
    QVERIFY(ConversionEngine(second).convert("!M2000\r", false, &b, &error));
    const QByteArray independentlyListedSample = QByteArray::fromHex("aaaa020108f7001111410003e8004e2027f8");
    QCOMPARE(a.targetFrame, independentlyListedSample);
    QCOMPARE(b.targetFrame, independentlyListedSample);
}

void FrameworkTest::runtimeHasExactlyOneDeviceTransport()
{
    QFile source(example("../widget.cpp"));
    QVERIFY(source.open(QIODevice::ReadOnly));
    const QByteArray code = source.readAll().toLower();
    QCOMPARE(code.count("new serialtransport"), 1);
}

void FrameworkTest::demoFramingRegression()
{
    DemoInternalProtocol codec;
    const QByteArray first = DemoInternalProtocol::makeFrame(0x81, "PING|motor1|OK|PONG");
    const QByteArray second = DemoInternalProtocol::makeFrame(0x90, "REPORT");
    QList<TranslationError> errors;
    QCOMPARE(codec.feedReceivedData(first.left(5), &errors).size(), 0);
    const auto messages = codec.feedReceivedData(first.mid(5) + second, &errors);
    QCOMPARE(messages.size(), 2);
    QCOMPARE(messages.at(0).messageType, 0x81);
    QCOMPARE(messages.at(1).messageType, 0x90);
    codec.feedReceivedData(QByteArray(4097, 'x'), &errors);
    QCOMPARE(errors.last().code, QStringLiteral("RX_OVERFLOW"));
}

void FrameworkTest::codecReplyAndCrc()
{
    QushengProtocol codec;
    codec.setAddresses(2, 1);
    UnifiedCommand query;
    query.requestId = 7;
    query.operation = "QUERY_MOTOR";
    query.targetDevice = "17";
    QByteArray request;
    TranslationError error;
    QVERIFY(codec.encodeCommand(query, &request, &error));
    QCOMPARE(quint8(request.at(7)), quint8(0x12));
    const QByteArray reply = QushengProtocol::makeFrame(1, 2, 0, 0x12,
                                                        QByteArray::fromHex("0000000010004e20ffffff"));
    QList<TranslationError> errors;
    QCOMPARE(codec.feedReceivedData(reply.left(4), &errors).size(), 0);
    const auto messages = codec.feedReceivedData(reply.mid(4), &errors);
    QCOMPARE(messages.size(), 1);
    QVERIFY(codec.matchesReply(query, messages.first()));
    UnifiedResult result;
    QVERIFY(codec.decodeReply(query, messages.first(), &result, &error));
    QCOMPARE(result.data.value("motorState").toInt(), 1);
    QCOMPARE(result.data.value("positionUm").toUInt(), 20000u);
    QByteArray corrupted = reply;
    corrupted[corrupted.size() - 1] = char(quint8(corrupted.at(corrupted.size() - 1)) ^ 1);
    QCOMPARE(codec.feedReceivedData(corrupted, &errors).size(), 0);
    QCOMPARE(errors.last().code, QStringLiteral("QUSHENG_CRC"));
}

void FrameworkTest::capturedMotorReplyEndsWaiting()
{
    QushengProtocol codec;codec.setAddresses(2,1);
    UnifiedCommand query;query.requestId=7;query.operation="QUERY_MOTOR";query.targetDevice="17";
    TranslationError error;QByteArray request;
    // 截图序号为3；先推进编码器，避免改写实机记录的序号和CRC。
    for(int i=0;i<3;++i) QVERIFY(codec.encodeCommand(query,&request,&error));
    FakeTransport transport;RequestManager manager(&codec,&transport);manager.setTimeouts(1000,1000);
    QSignalSpy completed(&manager,&RequestManager::completed);
    QString lastProtocolError;
    connect(&manager,&RequestManager::protocolError,&manager,[&lastProtocolError](const TranslationError &error) {lastProtocolError=error.code;});
    manager.enqueue(query);QTRY_COMPARE(transport.sent.size(),1);QCOMPARE(quint8(transport.sent.first().at(6)),quint8(3));
    const QByteArray captured=QByteArray::fromHex("aaaa01020ef103120000000010000bb80000000000009f47");
    QCOMPARE(captured.size(),24);QCOMPARE(QushengProtocol::crc16(captured.left(22)),quint16(0x9f47));
    QByteArray badCrc=captured;badCrc[23]=char(0x46);transport.receive(badCrc);
    QCOMPARE(lastProtocolError,QStringLiteral("QUSHENG_CRC"));QCOMPARE(completed.count(),0);QVERIFY(manager.hasPendingRequests());
    transport.receive(captured.left(13));QCOMPARE(completed.count(),0);
    transport.receive(captured.mid(13));QTRY_COMPARE(completed.count(),1);
    const UnifiedResult result=qvariant_cast<UnifiedResult>(completed.first().at(0));QVERIFY(result.success);
    QCOMPARE(result.data.value("motorState").toInt(),1);QCOMPARE(result.data.value("positionUm").toUInt(),3000u);
    QCOMPARE(result.data.value("feedbackPositionUm").toUInt(),0u);
    QTRY_VERIFY(!manager.hasPendingRequests()); // 回告先到时，也须等待模拟完整写出信号释放请求。
    QCOMPARE(transport.sent.size(),1); // 不继续查询，也不等待超时释放本次请求。
}

void FrameworkTest::sequenceExhaustionIsFailClosed()
{
    QushengProtocol codec;
    codec.setAddresses(2, 1);
    UnifiedCommand query;
    query.operation = "QUERY_MOTOR";
    query.targetDevice = "17";
    QByteArray frame;
    TranslationError error;
    for (int index = 0; index < 256; ++index) {
        query.requestId = index + 1;
        QVERIFY(codec.encodeCommand(query, &frame, &error));
        QCOMPARE(quint8(frame.at(6)), quint8(index));
    }
    QVERIFY(!codec.encodeCommand(query, &frame, &error));
    QCOMPARE(error.code, QStringLiteral("QUSHENG_SEQUENCE_EXHAUSTED"));
    codec.resetSession();
    QVERIFY(codec.encodeCommand(query, &frame, &error));
    QCOMPARE(quint8(frame.at(6)), quint8(0));
}

void FrameworkTest::managerAckIsNotCompletion()
{
    QushengProtocol codec; codec.setAddresses(2, 1); FakeTransport transport;
    RequestManager manager(&codec, &transport);
    QSignalSpy results(&manager, &RequestManager::completed), states(&manager, &RequestManager::stateChanged);
    UnifiedCommand move; move.requestId=1; move.targetDevice="17"; move.operation="ABSOLUTE_MOVE";
    move.parameters={{"positionUm",2000},{"speedUmPerS",1000},{"absoluteAction",1}};
    manager.enqueue(move); QTRY_COMPARE(transport.sent.size(),1);
    transport.receive(QushengProtocol::makeReply(0,0));
    QTRY_VERIFY(!manager.hasPendingRequests());
    QCOMPARE(results.count(),1); QVERIFY(manager.deviceBusy());
    QCOMPARE(qvariant_cast<RequestState>(states.last().at(1)),RequestState::Acknowledged);
    // 接纳不是动作完成；主动/迟到报文不触发新查询，也不误解锁。
    transport.receive(QushengProtocol::makeFrame(1,2,99,0x17,QByteArray::fromHex("11100007d0ffffff")));
    QTest::qWait(350); QCOMPARE(transport.sent.size(),1); QVERIFY(manager.deviceBusy());
}

void FrameworkTest::managerStalledWriteAndRejection()
{
    QushengProtocol codec;
    codec.setAddresses(2, 1);
    FakeTransport transport;
    RequestManager manager(&codec, &transport);
    manager.setTimeouts(20, 50);
    int completed = 0;
    UnifiedResult last;
    QObject::connect(&manager, &RequestManager::completed, &manager,
                     [&](const UnifiedResult &result) { ++completed; last = result; });
    UnifiedCommand query;
    query.requestId = 1;
    query.operation = "QUERY_MOTOR";
    query.targetDevice = "17";
    transport.stall = true;
    manager.enqueue(query);
    QTRY_COMPARE(completed, 1);
    QCOMPARE(last.error.code, QStringLiteral("SEND_TIMEOUT"));
    transport.stall = false;
    query.requestId = 2;
    manager.enqueue(query);
    QTRY_COMPARE(transport.sent.size(), 2);
    transport.receive(QushengProtocol::makeFrame(1, 2, 1, 0x12,
                                                  QByteArray::fromHex("0400000010004e20ffffff")));
    QTRY_COMPARE(completed, 2);
    QCOMPARE(last.error.code, QStringLiteral("QUSHENG_REJECTED"));
    query.requestId = 3;
    manager.enqueue(query);
    QTRY_COMPARE(transport.sent.size(), 3);
    transport.disconnectNow();
    QTRY_COMPARE(completed, 3);
    QCOMPARE(last.error.code, QStringLiteral("DISCONNECTED"));
    transport.receive(QushengProtocol::makeFrame(1, 2, 2, 0x12,
                                                  QByteArray::fromHex("0000000010004e20ffffff")));
    QCOMPARE(completed, 3);
}

// 以下边界覆盖高级 JSON 与运行快照的直接入口，避免只依赖界面控件约束。
static QJsonObject withExpression(QJsonObject root, const QJsonObject &expression)
{
    QJsonArray mappings = root.value("mappings").toArray();
    QJsonObject mapping = mappings.first().toObject();
    mapping.insert("parameters", QJsonObject{{"positionUm", expression}});
    mappings.replace(0, mapping); root.insert("mappings", mappings);
    return root;
}

void FrameworkTest::strictConfigurationAndByteInput()
{
    ProtocolConfig config;
    TranslationError error;
    QVERIFY(ProtocolConfig::load(example("dt-preview.json"), &config, &error));
    QByteArray bytes;
    QVERIFY(ProtocolConfig::decodeBytes("/1A2000R\\r", "ascii", &bytes, &error));
    QCOMPARE(bytes, QByteArray("/1A2000R\r"));
    QVERIFY(!ProtocolConfig::decodeBytes("bad\\z", "ascii", &bytes, &error));
    QVERIFY(!ProtocolConfig::decodeBytes(QString::fromUtf8(u8"中文"), "ascii", &bytes, &error));
    QVERIFY(!ProtocolConfig::decodeBytes("0AG0", "hex", &bytes, &error));
    QVERIFY(!ProtocolConfig::decodeBytes("\\xGG", "ascii", &bytes, &error));
    QVERIFY(ProtocolConfig::decodeBytes("AA 55 01", "hex", &bytes, &error));
    QCOMPARE(bytes, QByteArray::fromHex("aa5501"));
    const QList<QJsonObject> badExpressions = {
        {{"kind", "constant"}, {"value", "abc"}},
        {{"kind", "constant"}, {"value", 1.5}},
        {{"kind", "linear"}, {"field", QString::fromUtf8(u8"位置")}, {"numerator", 1}, {"denominator", 0}, {"round", "exact"}},
        {{"kind", "field"}, {"field", "missing"}},
        {{"kind", "script"}, {"value", 20000}}
    };
    for (const auto &expression : badExpressions)
        QVERIFY(!ProtocolConfig::fromJson(QJsonDocument(withExpression(config.root, expression)).toJson(), nullptr, &error));
    for (const QString &key : {QStringLiteral("schemaVersion"), QStringLiteral("device")}) {
        QJsonObject bad = config.root; bad.remove(key);
        QVERIFY(!ProtocolConfig::fromJson(QJsonDocument(bad).toJson(), nullptr, &error));
    }
    QJsonObject root = config.root;
    QJsonObject external = root.value("external").toObject();
    QJsonArray fields = external.value("fields").toArray();
    QJsonObject last = fields.last().toObject(); last.insert("min", "oops");
    fields.replace(2, last); external.insert("fields", fields); root.insert("external", external);
    QVERIFY(!ProtocolConfig::fromJson(QJsonDocument(root).toJson(), nullptr, &error));
    // 手工构造无效配置也必须失败，而不是进入不终止的空帧尾分帧循环。
    ProtocolConfig invalid;
    QVERIFY(ConversionEngine(invalid).feed("data", nullptr).isEmpty());
}

void FrameworkTest::parameterRelationsAndRounding()
{
    ProtocolConfig original;
    TranslationError error;
    QVERIFY(ProtocolConfig::load(example("dt-preview.json"), &original, &error));
    ConversionPreview result;
    const QString position = QString::fromUtf8(u8"位置");
    auto run = [&](QJsonObject expr, qint64 expected) {
        ProtocolConfig config;
        if (!ProtocolConfig::fromJson(QJsonDocument(withExpression(original.root, expr)).toJson(), &config, &error)) return false;
        if (!ConversionEngine(config).convert("/1A2000R\r", false, &result, &error)) return false;
        return result.command.parameters.value("positionUm").toLongLong() == expected;
    };
    QVERIFY(run({{"kind", "field"}, {"field", position}}, 2000));
    QVERIFY(run({{"kind", "constant"}, {"value", 20000}}, 20000));
    QVERIFY(run({{"kind", "linear"}, {"field", position}, {"numerator", 0}, {"denominator", 1}, {"offset", 123}, {"round", "exact"}}, 123));
    QVERIFY(run({{"kind", "linear"}, {"field", position}, {"numerator", -10}, {"denominator", 1}, {"offset", 40000}, {"round", "exact"}}, 20000));
    for (const QString &round : {QStringLiteral("floor"), QStringLiteral("ceil"), QStringLiteral("nearest")}) {
        QVERIFY(run({{"kind", "linear"}, {"field", position}, {"numerator", 1}, {"denominator", 3}, {"round", round}}, round == "floor" ? 666 : 667));
    }
    ProtocolConfig fractions;
    QVERIFY(ProtocolConfig::fromJson(QJsonDocument(withExpression(original.root,
        {{"kind", "linear"}, {"field", position}, {"numerator", 1}, {"denominator", 3}, {"round", "exact"}})).toJson(), &fractions, &error));
    QVERIFY(!ConversionEngine(fractions).convert("/1A2000R\r", false, &result, &error));
    QCOMPARE(error.code, QStringLiteral("CONVERSION_FRACTION"));
    QVERIFY(result.targetFrame.isEmpty());
}

void FrameworkTest::signedFixedFieldsAndOverflow()
{
    ProtocolConfig original;
    TranslationError error;
    QVERIFY(ProtocolConfig::load(example("dt-preview.json"), &original, &error));
    QJsonObject root = original.root;
    QJsonObject external = root.value("external").toObject();
    QJsonArray fields = external.value("fields").toArray();
    QJsonObject last = fields.last().toObject();
    last.insert("type", "int"); last.insert("min", -6000); fields.replace(2, last);
    QJsonObject fixed{{"name", "marker"}, {"role", "fixed"}, {"type", "ascii"}, {"lengthMode", "fixed"}, {"length", 1}, {"fixedValue", "!"}};
    fields.insert(2, fixed); external.insert("fields", fields); root.insert("external", external);
    root = withExpression(root, {{"kind", "linear"}, {"field", QString::fromUtf8(u8"位置")}, {"numerator", -10}, {"denominator", 1}, {"round", "exact"}});
    ProtocolConfig signedConfig;
    QVERIFY2(ProtocolConfig::fromJson(QJsonDocument(root).toJson(), &signedConfig, &error), qPrintable(error.message));
    ConversionPreview result;
    QVERIFY(ConversionEngine(signedConfig).convert("/1A!-2000R\r", false, &result, &error));
    QCOMPARE(result.command.parameters.value("positionUm").toLongLong(), 20000LL);
    QVERIFY(!ConversionEngine(signedConfig).convert("/1A?-2000R\r", false, &result, &error));
    QCOMPARE(error.code, QStringLiteral("FIELD_CONSTANT"));
    QVERIFY(result.targetFrame.isEmpty());
    // 无符号十进制禁止空白、符号、小数与超出 64 位的值。
    for (const QByteArray &value : {QByteArray("+2"), QByteArray(" 2"), QByteArray("-2"), QByteArray("2.0"), QByteArray("2\n"), QByteArray("2\r"), QByteArray("999999999999999999999")}) {
        QVERIFY(!ConversionEngine(original).convert("/1A" + value + "R\r", false, &result, &error));
        QVERIFY(result.targetFrame.isEmpty());
    }
    last.remove("min"); last.remove("max"); fields.replace(3, last);
    external.insert("fields", fields); root.insert("external", external);
    root = withExpression(root, {{"kind", "linear"}, {"field", QString::fromUtf8(u8"位置")}, {"numerator", 1000000000}, {"denominator", 1}, {"round", "exact"}});
    QVERIFY(ProtocolConfig::fromJson(QJsonDocument(root).toJson(), &signedConfig, &error));
    QVERIFY(!ConversionEngine(signedConfig).convert("/1A!9223372036854775807R\r", false, &result, &error));
    QCOMPARE(error.code, QStringLiteral("CONVERSION_OVERFLOW"));
}

void FrameworkTest::fixedBinaryFramesWithEmbeddedFooter()
{
    ProtocolConfig original;
    TranslationError error;
    QVERIFY(ProtocolConfig::load(example("binary-preview.json"), &original, &error));
    QJsonObject root = original.root;
    QJsonObject external = root.value("external").toObject(); external.insert("footer", "07"); root.insert("external", external);
    ProtocolConfig config;
    QVERIFY(ProtocolConfig::fromJson(QJsonDocument(root).toJson(), &config, &error));
    ConversionEngine stream(config);
    QList<TranslationError> errors;
    QCOMPARE(stream.feed(QByteArray::fromHex("aa550107"), &errors).size(), 0);
    auto frames = stream.feed(QByteArray::fromHex("d007aa550107d007"), &errors);
    QCOMPARE(frames.size(), 2);
    QVERIFY(errors.isEmpty());
    ConversionPreview result;
    QVERIFY(stream.convert(frames.first(), false, &result, &error));
    QCOMPARE(result.command.parameters.value("positionUm").toLongLong(), 20000LL);
}

void FrameworkTest::ackTimeoutAndNoCompletionPolling()
{
    QushengProtocol codec; codec.setAddresses(2,1); FakeTransport transport;
    RequestManager manager(&codec,&transport); manager.setTimeouts(50,30);
    QSignalSpy results(&manager,&RequestManager::completed), errors(&manager,&RequestManager::protocolError);
    UnifiedCommand query; query.requestId=1; query.operation="QUERY_MOTOR"; query.targetDevice="17";
    manager.enqueue(query); QTRY_COMPARE(results.count(),1);
    QCOMPARE(qvariant_cast<UnifiedResult>(results.last().at(0)).error.code,QStringLiteral("ACK_TIMEOUT"));
    UnifiedCommand move; move.requestId=2; move.operation="ABSOLUTE_MOVE"; move.targetDevice="17";
    move.parameters={{"positionUm",2000},{"speedUmPerS",1000},{"absoluteAction",1}};
    manager.enqueue(move); QTRY_COMPARE(transport.sent.size(),2);
    transport.receive(QushengProtocol::makeReply(1,0));
    QTRY_VERIFY(!manager.hasPendingRequests());
    QTest::qWait(350); QCOMPARE(transport.sent.size(),2); QCOMPARE(results.count(),2);
    QCOMPARE(errors.count(),0); QVERIFY(manager.deviceBusy());
}

void FrameworkTest::cancelThenQueryRecoversBusyState()
{
    QushengProtocol codec; codec.setAddresses(2, 1);
    FakeTransport transport;
    RequestManager manager(&codec, &transport);
    UnifiedCommand move; move.requestId = 1; move.targetDevice = "17"; move.operation = "ABSOLUTE_MOVE";
    move.responsePolicy = "complete";
    move.parameters = {{"positionUm", 20000}, {"speedUmPerS", 1000}, {"absoluteAction", 1}};
    UnifiedResult last;
    QObject::connect(&manager, &RequestManager::completed, &manager, [&](const UnifiedResult &r) { last = r; });
    manager.enqueue(move); QTRY_COMPARE(transport.sent.size(), 1);
    manager.cancel(1); QVERIFY(manager.deviceBusy()); QCOMPARE(last.error.code, QStringLiteral("LOCAL_WAIT_CANCELLED"));
    transport.receive(QushengProtocol::makeReply(0, 0));
    QCOMPARE(last.error.code, QStringLiteral("LOCAL_WAIT_CANCELLED"));
    UnifiedCommand query; query.requestId = 2; query.operation = "QUERY_MOTOR"; query.targetDevice = "17";
    manager.enqueue(query); QTRY_COMPARE(transport.sent.size(), 2);
    transport.receive(QushengProtocol::makeFrame(1, 2, 1, 0x12, QByteArray::fromHex("0000000010004e20ffffff")));
    QVERIFY(last.success); QVERIFY(!manager.deviceBusy());
}

void FrameworkTest::simpleCommandExamples_data()
{
    QTest::addColumn<QByteArray>("input");
    QTest::addColumn<QString>("operation");
    QTest::addColumn<QString>("policy");
    QTest::addColumn<int>("wireCommand");
    QTest::addColumn<QByteArray>("payload");
    QTest::newRow("absolute-ack") << QByteArray("/1A2000R\r") << QString("MOTOR_FORWARD") << QString("ack") << 0x11 << QByteArray::fromHex("11410003e80007d0");
    QTest::newRow("absolute-lowercase") << QByteArray("/1a2000R\r") << QString("MOTOR_FORWARD") << QString("ack") << 0x11 << QByteArray::fromHex("11410003e80007d0");
    QTest::newRow("relative-forward-ack") << QByteArray("/1P500R\r") << QString("RELATIVE_FORWARD") << QString("ack") << 0x11 << QByteArray::fromHex("11210003e80001f4");
    QTest::newRow("relative-forward-lowercase") << QByteArray("/1p500R\r") << QString("RELATIVE_FORWARD") << QString("ack") << 0x11 << QByteArray::fromHex("11210003e80001f4");
    QTest::newRow("relative-backward-ack") << QByteArray("/1D500R\r") << QString("RELATIVE_BACKWARD") << QString("ack") << 0x11 << QByteArray::fromHex("11230003e80001f4");
    QTest::newRow("relative-backward-lowercase") << QByteArray("/1d500R\r") << QString("RELATIVE_BACKWARD") << QString("ack") << 0x11 << QByteArray::fromHex("11230003e80001f4");
    QTest::newRow("position") << QByteArray("/1?R\r") << QString("QUERY_MOTOR") << QString("ack") << 0x12 << QByteArray::fromHex("11");
    QTest::newRow("position-alias") << QByteArray("/1?0R\r") << QString("QUERY_MOTOR") << QString("ack") << 0x12 << QByteArray::fromHex("11");
    QTest::newRow("pulse-position") << QByteArray("/1?4R\r") << QString("QUERY_MOTOR") << QString("ack") << 0x12 << QByteArray::fromHex("11");
    QTest::newRow("version") << QByteArray("/1&R\r") << QString("QUERY_DEVICE_INFO") << QString("ack") << 0x04 << QByteArray::fromHex("0000");
    QTest::newRow("version-alias") << QByteArray("/1?23R\r") << QString("QUERY_DEVICE_INFO") << QString("ack") << 0x04 << QByteArray::fromHex("0000");
}

void FrameworkTest::simpleCommandExamples()
{
    QFETCH(QByteArray, input); QFETCH(QString, operation); QFETCH(QString, policy);
    QFETCH(int, wireCommand); QFETCH(QByteArray, payload);
    ProtocolConfig config; TranslationError error; ConversionPreview preview;
    QVERIFY2(ProtocolConfig::load(example("dt-simple-commands.json"), &config, &error), qPrintable(error.message));
    QVERIFY2(ConversionEngine(config).convert(input, false, &preview, &error), qPrintable(error.message));
    QCOMPARE(preview.command.operation, operation); QCOMPARE(preview.command.responsePolicy, policy);
    QCOMPARE(quint8(preview.targetFrame.at(7)), quint8(wireCommand));
    QCOMPARE(preview.targetFrame.mid(8, payload.size()), payload);
    if (wireCommand == 0x11) {
        const bool absolute = QushengCommandCatalog::isAbsoluteMotion(operation);
        const QString key = absolute ? "positionUm" : "distanceUm";
        QCOMPARE(preview.command.parameters.value(key).toInt(), absolute ? 2000 : 500);
    } else {
        QCOMPARE(preview.fieldOrder.size(), 2);
        QVERIFY(!preview.fields.contains(QString::fromUtf8(u8"位置")));
        if (wireCommand == 0x12) QCOMPARE(preview.command.parameters.value("devId").toInt(), 0x11);
        else QVERIFY(preview.command.parameters.isEmpty());
    }
}

void FrameworkTest::mappedInputFormatsAndRoundTrip()
{
    ProtocolConfig config; TranslationError error; ConversionPreview preview;
    QVERIFY(ProtocolConfig::load(example("dt-simple-commands.json"), &config, &error));
    ConversionEngine engine(config);
    for (const QByteArray &bad : {QByteArray("/1A2000\r"), QByteArray("/1?23\r"), QByteArray("/1?01R\r"), QByteArray("/1AR\r"),
                                 QByteArray("/1A2xR\r"), QByteArray("/1A6001R\r"), QByteArray("/1A500P20R\r"),
                                 QByteArray("/1#\r"), QByteArray("/1?20\r"), QByteArray("/1QR\r"), QByteArray("1A2000R\r")}) {
        QVERIFY2(!engine.convert(bad, false, &preview, &error), bad.constData()); QVERIFY(preview.targetFrame.isEmpty());
    }
    QList<TranslationError> errors;
    QVERIFY(engine.feed("noise/1?", &errors).isEmpty());
    const auto frames = engine.feed("23R\r/1A2000R\r/1?4R\r", &errors);
    QCOMPARE(frames.size(), 3); QVERIFY(errors.isEmpty()); QCOMPARE(frames.at(0), QByteArray("/1?23R\r"));
    QTemporaryDir directory; const QString saved = directory.filePath("commands.json");
    QVERIFY(config.save(saved, &error)); ProtocolConfig reloaded; QVERIFY(ProtocolConfig::load(saved, &reloaded, &error));
    QCOMPARE(config.version, reloaded.version);
    ConversionPreview second;
    QVERIFY(engine.convert("/1P500R\r", false, &preview, &error));
    QVERIFY(ConversionEngine(reloaded).convert("/1P500R\r", false, &second, &error));
    QCOMPARE(preview.targetFrame, second.targetFrame);
    // 将客户命令换成任意多字符值并更改共用帧尾，通用解析器不需要客户专用分支。
    QJsonObject root = config.root, external = root.value("external").toObject();
    external.insert("header", "!"); external.insert("footer", ";\\n"); root.insert("external", external);
    QJsonArray mappings = root.value("mappings").toArray();
    QJsonObject move = mappings.at(0).toObject(); move.insert("commandCode", "MOVE"); mappings.replace(0, move);
    QJsonObject query = mappings.at(10).toObject(); query.insert("commandCode", "VERSION");
    mappings.replace(10, query);
    root.insert("mappings", mappings); QVERIFY(ProtocolConfig::fromJson(QJsonDocument(root).toJson(), &config, &error));
    QVERIFY(ConversionEngine(config).convert("!1MOVE2000;\n", false, &preview, &error));
    QCOMPARE(preview.command.parameters.value("positionUm").toInt(), 2000);
    QVERIFY(ConversionEngine(config).convert("!1VERSION;\n", false, &preview, &error));
    QCOMPARE(preview.command.operation, QString("QUERY_DEVICE_INFO"));
}

void FrameworkTest::mappedConfigurationRejectsInvalidFormats()
{
    ProtocolConfig original; TranslationError error;
    QVERIFY(ProtocolConfig::load(example("dt-simple-commands.json"), &original, &error));
    const QList<QJsonObject> badInputs = {
        {{"omitFields", QJsonArray{QString::fromUtf8(u8"命令码")}}},
        {{"omitFields", QJsonArray{QString::fromUtf8(u8"地址")}}},
        {{"omitFields", QJsonArray{"missing"}}}, {{"omitFields", "bad"}},
        {{"footer", ""}}, {{"footer", "\\z"}},
        {{"omitFields", QJsonArray{QString::fromUtf8(u8"位置")}}}
    };
    for (const auto &bad : badInputs) {
        QJsonObject root = original.root; QJsonArray mappings = root.value("mappings").toArray();
        QJsonObject m = mappings.first().toObject(); m.insert("input", bad); mappings.replace(0, m); root.insert("mappings", mappings);
        QVERIFY(!ProtocolConfig::fromJson(QJsonDocument(root).toJson(), nullptr, &error));
    }
}

void FrameworkTest::deviceVersionReply()
{
    QushengProtocol codec; codec.setAddresses(2, 1); FakeTransport transport; RequestManager manager(&codec, &transport);
    UnifiedCommand query; query.requestId = 91; query.operation = "QUERY_DEVICE_INFO";
    UnifiedResult result; int completed = 0;
    connect(&manager, &RequestManager::completed, &manager, [&](const UnifiedResult &r) {result=r; ++completed;});
    manager.enqueue(query); QTRY_COMPARE(transport.sent.size(), 1);
    QCOMPARE(transport.sent.first().mid(8, 2), QByteArray::fromHex("0000"));
    const QByteArray payload = QByteArray::fromHex("000000000102012c020300040304000500112233445566778899aabb");
    QCOMPARE(payload.size(), 28);
    transport.receive(QushengProtocol::makeFrame(1, 2, 1, 0x04, payload)); QCOMPARE(completed, 0); // 错误序号不得完成。
    transport.receive(QushengProtocol::makeFrame(1, 2, 0, 0x04, payload.left(27))); QCOMPARE(completed, 0);
    transport.receive(QushengProtocol::makeFrame(1, 2, 0, 0x04, payload)); QTRY_COMPARE(completed, 1);
    QVERIFY(result.success); QCOMPARE(result.data.value("hardwareVersion").toString(), QString("1.2.300"));
    QCOMPARE(result.data.value("iapVersion").toString(), QString("2.3.4"));
    QCOMPARE(result.data.value("firmwareVersion").toString(), QString("3.4.5"));
    QVERIFY(result.data.value("deviceIdentifier").toString().startsWith("00 11 22"));
    QVERIFY(!manager.deviceBusy());
    query.requestId = 92; manager.enqueue(query); QTRY_COMPARE(transport.sent.size(), 2);
    transport.receive(QushengProtocol::makeFrame(1, 2, 1, 0x04, QByteArray::fromHex("04000000")));
    QTRY_COMPARE(completed, 2); QVERIFY(!result.success); QCOMPARE(result.error.code, QString("QUSHENG_REJECTED"));
}

static QByteArray motorReply(quint8 sequence, int state, int position)
{
    QByteArray data(11, '\0'); data[4] = char(state << 4);
    data[5]=char(position >> 16); data[6]=char(position >> 8); data[7]=char(position);
    return QushengProtocol::makeFrame(1, 2, sequence, 0x12, data);
}

void FrameworkTest::relativeMoveChecksPositionBeforeSending()
{
    ProtocolConfig config; TranslationError error; ConversionPreview preview;
    QVERIFY(ProtocolConfig::load(example("dt-simple-commands.json"), &config, &error));
    for (const QByteArray &input : {QByteArray("/1p500R\r"), QByteArray("/1d500R\r")}) {
        QVERIFY(ConversionEngine(config).convert(input, false, &preview, &error));
        QushengProtocol codec; codec.setAddresses(2, 1); FakeTransport transport; RequestManager manager(&codec, &transport);
        UnifiedResult result; int completed = 0;
        connect(&manager, &RequestManager::completed, &manager, [&](const UnifiedResult &r) {result=r; ++completed;});
        preview.command.requestId = 1; manager.enqueue(preview.command); QTRY_COMPARE(transport.sent.size(), 1);
        QCOMPARE(quint8(transport.sent.at(0).at(7)), quint8(0x12)); // 只有查询，尚无运动。
        transport.receive(motorReply(0, 1, 1000)); QTRY_COMPARE(transport.sent.size(), 2);
        QCOMPARE(quint8(transport.sent.at(1).at(7)), quint8(0x11));
        QCOMPARE(quint8(transport.sent.at(1).at(9)), quint8(input.at(2) == 'p' ? 0x21 : 0x23));
        transport.receive(QushengProtocol::makeReply(1, 0)); QCOMPARE(completed, 1); QVERIFY(manager.deviceBusy());
        QTRY_VERIFY(!manager.hasPendingRequests());
        QTest::qWait(250); QCOMPARE(transport.sent.size(),2);
        QVERIFY(result.success); QVERIFY(manager.deviceBusy()); // 仅保留一次前置查询与一次运动。
    }
}

void FrameworkTest::relativeMoveRejectsUnsafeStartAndTimeout()
{
    ProtocolConfig config; TranslationError error; ConversionPreview preview;
    QVERIFY(ProtocolConfig::load(example("dt-simple-commands.json"), &config, &error));
    QVERIFY(ConversionEngine(config).convert("/1D2000R\r", false, &preview, &error));
    for (int scenario = 0; scenario < 4; ++scenario) {
        QushengProtocol codec; codec.setAddresses(2, 1); FakeTransport transport;
        RequestManager manager(&codec, &transport); manager.setTimeouts(50, 30);
        UnifiedResult result; int completed = 0;
        connect(&manager, &RequestManager::completed, &manager, [&](const UnifiedResult &r) {result=r; ++completed;});
        preview.command.requestId = 1; manager.enqueue(preview.command); QTRY_COMPARE(transport.sent.size(), 1);
        if (scenario == 0) transport.receive(motorReply(0, 1, 1000)); // 终点小于下限。
        if (scenario == 1) transport.receive(motorReply(0, 3, 1000)); // 电机在运动。
        if (scenario == 2) transport.receive(motorReply(0, 0, 0xffffff)); // 未回零。
        QTRY_COMPARE(completed, 1); QVERIFY(!result.success); QCOMPARE(transport.sent.size(), 1);
        QCOMPARE(result.error.code, QString(scenario == 0 ? "POSITION_LIMIT" : scenario == 3 ? "POSITION_QUERY_TIMEOUT" : "POSITION_QUERY_STATE"));
        if (scenario != 0) QVERIFY(manager.deviceBusy());
    }
}

void FrameworkTest::sharedFooterAndLegacyResponse()
{
    ProtocolConfig config; TranslationError error; ConversionPreview preview;
    QVERIFY(ProtocolConfig::load(example("dt-simple-commands.json"), &config, &error));
    QJsonObject root = config.root; QJsonArray mappings = root.value("mappings").toArray();
    QJsonObject mapping = mappings.first().toObject();
    QVERIFY(!mapping.contains("responsePolicy"));
    mapping.insert("responsePolicy", "complete"); mappings[0] = mapping; root.insert("mappings", mappings);
    QVERIFY(ProtocolConfig::fromJson(QJsonDocument(root).toJson(), &config, &error));
    QVERIFY(ConversionEngine(config).convert("/1A2000R\r", false, &preview, &error));
    QCOMPARE(preview.command.responsePolicy, QString("ack"));
    // 即使旧覆盖与共用帧尾相同，也不再作为第二个编辑入口接纳。
    for (const QString &footer : {QStringLiteral("R\\r"), QStringLiteral("\\r")}) {
        QJsonObject old = mapping; old.insert("input", QJsonObject{{"footer", footer}});
        QCOMPARE(config.inputFormat(old).value("footer").toString(), QString("R\\r"));
        mappings[0] = old; root.insert("mappings", mappings);
        QVERIFY(!ProtocolConfig::fromJson(QJsonDocument(root).toJson(), nullptr, &error));
        QCOMPARE(error.code, QString("CONFIG_MAPPING_INPUT"));
    }
    QVERIFY(ProtocolConfig::load(example("dt-query-preview.json"), &config, &error));
    for (const QByteArray &input : {QByteArray("/1?\r"), QByteArray("/1?4\r"), QByteArray("/1?23\r")}) {
        QVERIFY(ConversionEngine(config).convert(input, false, &preview, &error));
        QVERIFY(!preview.targetFrame.isEmpty());
    }
    QVERIFY(!ConversionEngine(config).convert("/1?R\r", false, &preview, &error));
    QTemporaryDir directory; QVERIFY(config.save(directory.filePath("queries.json"), &error));
    ProtocolConfig restored; QVERIFY(ProtocolConfig::load(directory.filePath("queries.json"), &restored, &error));
    QCOMPARE(config.version, restored.version);
}

void FrameworkTest::sdkSimpleControlsFromConfig()
{
    ProtocolConfig config; TranslationError error; ConversionPreview preview;
    QVERIFY2(ProtocolConfig::load(example("sdk-simple-controls.json"), &config, &error), qPrintable(error.message));
    const QList<QPair<QByteArray, QByteArray>> samples = {
        {"/1FAN50R\r", QByteArray::fromHex("e132")}, {"/1VAL1000R\r", QByteArray::fromHex("32000003e8")},
        {"/1DAC1000R\r", QByteArray::fromHex("710003e8")}, {"/1DC-30R\r", QByteArray::fromHex("21e2")},
        {"/1ADCR\r", QByteArray::fromHex("62")}, {"/1DIR\r", QByteArray()}, {"/1STATR\r", QByteArray()}
    };
    for (const auto &sample : samples) {
        QVERIFY2(ConversionEngine(config).convert(sample.first, false, &preview, &error), qPrintable(error.message));
        QCOMPARE(preview.targetFrame.mid(8, sample.second.size()), sample.second);
        QCOMPARE(quint8(preview.targetFrame.at(4)), quint8(sample.second.size()));
        QVERIFY(!preview.command.parameters.contains("positionUm"));
        if (preview.command.operation == "QUSHENG_81") {
            QCOMPARE(preview.command.parameters.value("devId").toInt(), 225);
            QVERIFY(preview.parameterTrace.value("devId").contains(QString::fromUtf8(u8"SDK预置")));
            QVERIFY(preview.parameterTrace.value("percent").contains(QString::fromUtf8(u8"直接引用")));
        }
    }
    // 覆盖预置只能在映射中明确发生；另一个客户字段仍由通用格式解析，不认命令字符。
    QJsonObject root = config.root; QJsonArray mappings = root.value("mappings").toArray();
    QJsonObject fan = mappings.first().toObject(); QJsonObject parameters = fan.value("parameters").toObject();
    parameters.insert("devId", QJsonObject{{"kind", "constant"}, {"value", 226}}); fan.insert("parameters", parameters);
    fan.insert("commandCode", "CHANGE"); mappings[0] = fan; root.insert("mappings", mappings);
    QVERIFY(ProtocolConfig::fromJson(QJsonDocument(root).toJson(), &config, &error));
    QVERIFY(ConversionEngine(config).convert("/1CHANGE25R\r", false, &preview, &error));
    QCOMPARE(preview.targetFrame.mid(8, 2), QByteArray::fromHex("e219"));
    for (const QByteArray &bad : {QByteArray("/1CHANGE101R\r"), QByteArray("/1DC-129R\r"), QByteArray("/1DAC16777215R\r")}) {
        QVERIFY(!ConversionEngine(config).convert(bad, false, &preview, &error)); QVERIFY(preview.targetFrame.isEmpty());
    }
    QTemporaryDir directory; QVERIFY(config.save(directory.filePath("sdk.json"), &error));
    ProtocolConfig restored; QVERIFY(ProtocolConfig::load(directory.filePath("sdk.json"), &restored, &error));
    QCOMPARE(restored.root, config.root); QCOMPARE(restored.version, config.version);
    QVERIFY(ConversionEngine(restored).convert("/1CHANGE25R\r", false, &preview, &error));
    QCOMPARE(preview.targetFrame.mid(8, 2), QByteArray::fromHex("e219"));
    root.insert("sdkDefaults", QJsonObject{{"QUSHENG_81", QJsonObject{{"percent", 101}}}});
    QVERIFY(!ProtocolConfig::fromJson(QJsonDocument(root).toJson(), nullptr, &error)); QCOMPARE(error.code, QString("CONFIG_SDK_DEFAULT"));
    for (const QJsonObject &bad : {QJsonObject{{"QUSHENG_01", QJsonObject{{"enableMask", 1}}}},
                                  QJsonObject{{"QUSHENG_84", QJsonObject{{"minOutput", 10}, {"maxOutput", 9}}}}}) {
        root.insert("sdkDefaults", bad);
        QVERIFY(!ProtocolConfig::fromJson(QJsonDocument(root).toJson(), nullptr, &error)); QCOMPARE(error.code, QString("CONFIG_SDK_DEFAULT"));
    }
    QVERIFY(ProtocolConfig::load(example("dt-simple-commands.json"), &config, &error));
    root = config.root;
    root.insert("sdkDefaults", QJsonObject{{"MOTOR_FORWARD", QJsonObject{{"devId", 18}, {"speedUmPerS", 2000}}}});
    QVERIFY(ProtocolConfig::fromJson(QJsonDocument(root).toJson(), &config, &error));
    QVERIFY(ConversionEngine(config).convert("/1A2000R\r", false, &preview, &error));
    QCOMPARE(preview.targetFrame.mid(8, 8), QByteArray::fromHex("12410007d00007d0"));
    root.insert("sdkDefaults", QJsonObject{{"MOTOR_FORWARD", QJsonObject{{"action", 2}}}});
    QVERIFY(!ProtocolConfig::fromJson(QJsonDocument(root).toJson(), nullptr, &error));
}

void FrameworkTest::sdkOrdinaryRequestLifecycle()
{
    QushengProtocol codec; codec.setAddresses(2, 1); FakeTransport transport;
    RequestManager manager(&codec, &transport); manager.setTimeouts(100, 100);
    int completed = 0; UnifiedResult last;
    connect(&manager, &RequestManager::completed, &manager, [&](const UnifiedResult &result) { ++completed; last = result; });
    UnifiedCommand fan; fan.requestId = 1; fan.operation = "QUSHENG_81"; fan.parameters = {{"percent", 50}};
    manager.enqueue(fan); QTRY_COMPARE(transport.sent.size(), 1);
    QCOMPARE(transport.sent.first().mid(8, 2), QByteArray::fromHex("e032"));
    // 错误序号、损坏长度不完成请求；有效单条应答不进入电机运动轮询。
    transport.receive(QushengProtocol::makeFrame(1, 2, 99, 0x81, QByteArray(4, '\0'))); QCOMPARE(completed, 0);
    transport.receive(QushengProtocol::makeFrame(1, 2, 0, 0x81, QByteArray(3, '\0'))); QCOMPARE(completed, 0);
    transport.receive(QushengProtocol::makeFrame(1, 2, 0, 0x81, QByteArray(4, '\0')));
    QTRY_COMPARE(completed, 1); QVERIFY(last.success); QVERIFY(!manager.deviceBusy()); QCOMPARE(transport.sent.size(), 1);
    UnifiedCommand adc; adc.requestId = 2; adc.operation = "QUSHENG_44";
    manager.enqueue(adc); QTRY_COMPARE(transport.sent.size(), 2);
    transport.receive(QushengProtocol::makeFrame(1, 2, 1, 0x44, QByteArray::fromHex("02000000")));
    QTRY_COMPARE(completed, 2); QVERIFY(!last.success); QCOMPARE(last.error.code, QString("QUSHENG_REJECTED"));
    QVERIFY(last.error.message.contains("02")); QCOMPARE(transport.sent.size(), 2);
    QSignalSpy reports(&manager, &RequestManager::unsolicitedMessage);
    UnifiedCommand reportSettings; reportSettings.requestId = 3; reportSettings.operation = "QUSHENG_09";
    manager.enqueue(reportSettings); QTRY_COMPARE(transport.sent.size(), 3);
    transport.receive(QushengProtocol::makeFrame(1, 2, 2, 0x09, QByteArray::fromHex("11000001")));
    QCOMPARE(completed, 2); QCOMPARE(reports.count(), 1); // 同序号主动故障不是设置失败。
    transport.receive(QushengProtocol::makeFrame(1, 2, 2, 0x09, QByteArray(4, '\0'))); QTRY_COMPARE(completed, 3);
    UnifiedCommand heartbeat; heartbeat.requestId = 4; heartbeat.operation = "QUSHENG_08";
    manager.enqueue(heartbeat); QTRY_COMPARE(transport.sent.size(), 4);
    transport.receive(QushengProtocol::makeFrame(1, 2, 3, 0x08, QByteArray()));
    QCOMPARE(completed, 3); QCOMPARE(reports.count(), 2);
    transport.receive(QushengProtocol::makeFrame(1, 2, 3, 0x08, QByteArray(4, '\0'))); QTRY_COMPARE(completed, 4);
}

void FrameworkTest::sdkQueryDoesNotUnlockOtherMotor()
{
    QushengProtocol codec; codec.setAddresses(2, 1); FakeTransport transport;
    RequestManager manager(&codec, &transport); int completed = 0;
    connect(&manager, &RequestManager::completed, &manager, [&](const UnifiedResult &) { ++completed; });
    UnifiedCommand move; move.requestId = 1; move.operation = "MOTOR_FORWARD"; move.targetDevice = "017";
    move.parameters = {{"positionUm", 2000}, {"speedUmPerS", 1000}};
    manager.enqueue(move); QTRY_COMPARE(transport.sent.size(), 1);
    manager.cancel(1); QVERIFY(manager.deviceBusy()); QTRY_COMPARE(completed, 1);
    UnifiedCommand query; query.requestId = 2; query.operation = "QUERY_MOTOR"; query.targetDevice = "17";
    query.parameters.insert("devId", 18); // 实际SDK请求对象优先于旧式targetDevice，不能误清17。
    manager.enqueue(query); QTRY_COMPARE(transport.sent.size(), 2);
    QCOMPARE(quint8(transport.sent.last().at(8)), quint8(18));
    transport.receive(QushengProtocol::makeFrame(1, 2, 1, 0x12, QByteArray::fromHex("00000000100007d0ffffff")));
    QTRY_COMPARE(completed, 2); QVERIFY(manager.deviceBusy());
    query.requestId = 3; query.parameters.insert("devId", 17);
    manager.enqueue(query); QTRY_COMPARE(transport.sent.size(), 3);
    transport.receive(QushengProtocol::makeFrame(1, 2, 2, 0x12, QByteArray::fromHex("00000000100007d0ffffff")));
    QTRY_COMPARE(completed, 3); QVERIFY(!manager.deviceBusy());
}

void FrameworkTest::mappingSourcesRespectChangedFormat()
{
    ProtocolConfig original; TranslationError error;
    QVERIFY(ProtocolConfig::load(example("dt-simple-commands.json"), &original, &error));
    // 只保留一个运动映射，使错误归因确实来自本次格式变化，而非其它查询布局。
    QJsonObject mapping = original.root.value("mappings").toArray().first().toObject();
    original.root.insert("mappings", QJsonArray{mapping});
    for (const QString &change : {QStringLiteral("delete"), QStringLiteral("rename"), QStringLiteral("ascii"), QStringLiteral("fixed"), QStringLiteral("omit")}) {
        ProtocolConfig config = original;
        QJsonObject external = config.root.value("external").toObject();
        QJsonArray fields = external.value("fields").toArray();
        QJsonObject position = fields.at(2).toObject();
        if (change == "delete") fields.removeAt(2);
        else if (change == "rename") { position.insert("name", QString::fromUtf8(u8"数据参数")); fields[2] = position; }
        else if (change == "ascii") {
            position.insert("type", "ascii"); position.remove("min"); position.remove("max"); fields[2] = position;
        } else if (change == "fixed") { position.insert("role", "fixed"); position.insert("fixedValue", "2000"); fields[2] = position; }
        else {
            QJsonObject omittedMapping = mapping;
            omittedMapping.insert("input", QJsonObject{{"omitFields", QJsonArray{QString::fromUtf8(u8"位置")}}});
            config.root.insert("mappings", QJsonArray{omittedMapping});
        }
        external.insert("fields", fields); config.root.insert("external", external);
        QVERIFY(!config.validate(&error)); QCOMPARE(error.code, QString("CONFIG_EXPRESSION"));
        QVERIFY(error.message.contains(QString::fromUtf8(u8"命令映射“A”")));
        QVERIFY(error.message.contains(QString::fromUtf8(u8"位置")));
        if (change == "ascii" || change == "fixed") QVERIFY(error.message.contains(QString::fromUtf8(u8"整数参数字段")));
        ConversionPreview preview;
        QVERIFY(!ConversionEngine(config).convert("/1A2000R\r", false, &preview, &error)); QVERIFY(preview.targetFrame.isEmpty());
    }
    // 删除原来源不会破坏固定数值映射；后者并不依赖任何客户数值字段。
    QJsonObject external = original.root.value("external").toObject();
    QJsonArray fields = external.value("fields").toArray(); fields.removeAt(2); external.insert("fields", fields);
    original.root.insert("external", external);
    mapping.insert("parameters", QJsonObject{{"positionUm", QJsonObject{{"kind", "constant"}, {"value", 2000}}}});
    original.root.insert("mappings", QJsonArray{mapping});
    QVERIFY2(original.validate(&error), qPrintable(error.message));
    ConversionPreview preview; QVERIFY(ConversionEngine(original).convert("/1AR\r", false, &preview, &error));
    QCOMPARE(preview.command.parameters.value("positionUm").toInt(), 2000);
}

void FrameworkTest::fixedMotorActionTemplates()
{
    const QStringList expected = {"MOTOR_FORWARD", "MOTOR_STOP", "MOTOR_BACKWARD", "MOTOR_RESET"};
    QStringList found;
    for (const QJsonValue &value : QushengCommandCatalog::options()) {
        const QJsonObject entry = value.toObject();
        if (entry.value("code").toString() != "0x11") continue;
        const QString operation = entry.value("operation").toString();
        found.append(operation);
        int editable = 0;
        for (const QJsonValue &parameter : entry.value("parameters").toArray()) {
            const QJsonObject spec = parameter.toObject();
            if (spec.value("editable").toBool()) {
                ++editable; QCOMPARE(spec.value("key").toString(), QString("positionUm"));
                QCOMPARE(spec.value("min").toInt(), 0); QCOMPARE(spec.value("max").toInt(), 0xfffffe);
            }
            if (spec.value("key").toString() == "mode") QCOMPARE(spec.value("fixedValue").toInt(), 4);
            if (spec.value("key").toString() == "action") QCOMPARE(spec.value("fixedValue").toInt(), expected.indexOf(operation) + 1);
            if (spec.value("key").toString() == "speedUmPerS") QCOMPARE(spec.value("defaultSource").toString(), QString("device.speedUmPerS"));
        }
        QCOMPARE(editable, QushengCommandCatalog::isStopOrReset(operation) ? 0 : 1);
    }
    QCOMPARE(found, expected);
    for (const QString &operation : {QString("ABSOLUTE_MOVE"), QString("RELATIVE_FORWARD"), QString("RELATIVE_BACKWARD"), QString("QUSHENG_11")}) {
        QVERIFY(QushengCommandCatalog::isLegacyOperation(operation));
        QVERIFY(!QushengCommandCatalog::find(operation).isEmpty());
        QVERIFY(!found.contains(operation));
    }

    ProtocolConfig base; TranslationError error;
    QVERIFY(ProtocolConfig::load(example("dt-preview.json"), &base, &error));
    QTemporaryDir directory; QVERIFY(directory.isValid());
    for (const QString &operation : expected) {
        ProtocolConfig config = base;
        QJsonObject mapping = config.root.value("mappings").toArray().at(0).toObject();
        mapping.insert("operation", operation); mapping.insert("responsePolicy", "ack");
        config.root.insert("mappings", QJsonArray{mapping});
        QJsonObject device = config.root.value("device").toObject();
        device.insert("speedUmPerS", 1234); device.remove("absoluteAction"); config.root.insert("device", device);
        QVERIFY2(config.validate(&error), qPrintable(error.message));
        ConversionPreview preview;
        QVERIFY(ConversionEngine(config).convert("/1A2000R\r", false, &preview, &error));
        const bool movement = operation == "MOTOR_FORWARD" || operation == "MOTOR_BACKWARD";
        if (movement) {
            QCOMPARE(preview.command.parameters.value("positionUm").toInt(), 20000);
            QCOMPARE(preview.command.parameters.value("speedUmPerS").toInt(), 1234);
            QCOMPARE(quint8(preview.targetFrame.at(9)), quint8(0x40 | QushengCommandCatalog::fixedAction(operation)));
            QCOMPARE(preview.targetFrame.mid(10, 6), QByteArray::fromHex("0004d2004e20"));
            const QByteArray before = preview.targetFrame;
            // 设备中的旧方向与直接构造的统一命令都不能改写新模板固定动作。
            device.insert("absoluteAction", operation == "MOTOR_FORWARD" ? 3 : 1); config.root.insert("device", device);
            QVERIFY(ConversionEngine(config).convert("/1A2000R\r", true, &preview, &error)); QCOMPARE(preview.targetFrame, before);
            UnifiedCommand forged = preview.command; forged.parameters.insert("absoluteAction", 2);
            QushengProtocol codec; codec.setAddresses(2, 1); QByteArray frame;
            QVERIFY(codec.encodeCommand(forged, &frame, &error)); QCOMPARE(frame, before);
            device.insert("maxPositionUm", 19999); config.root.insert("device", device);
            QVERIFY(!ConversionEngine(config).convert("/1A2000R\r", false, &preview, &error));
            QCOMPARE(error.code, QString("POSITION_LIMIT")); QVERIFY(preview.targetFrame.isEmpty());
            device.insert("maxPositionUm", 50000); config.root.insert("device", device);
        } else {
            QCOMPARE(preview.targetFrame.mid(8, 8), QByteArray::fromHex(operation == "MOTOR_STOP" ? "11420004d2000000" : "11440004d2000000"));
            QCOMPARE(preview.command.parameters.value("positionUm").toInt(), 0);
            QVERIFY(preview.parameterTrace.value("positionUm").contains(QString::fromUtf8(u8"模板固定")));
            UnifiedCommand forged = preview.command;
            forged.parameters.insert("positionUm", 0xffffff); forged.parameters.insert("mode", 1); forged.parameters.insert("action", 1);
            QushengProtocol codec; codec.setAddresses(2, 1); QByteArray frame;
            QVERIFY(codec.encodeCommand(forged, &frame, &error)); QCOMPARE(frame, preview.targetFrame);
            QJsonObject preset{{"devId", 18}, {"speedUmPerS", 2000}};
            config.root.insert("sdkDefaults", QJsonObject{{operation, preset}});
            QVERIFY(ConversionEngine(config).convert("/1A2000R\r", false, &preview, &error));
            QCOMPARE(preview.targetFrame.mid(8, 8), QByteArray::fromHex(operation == "MOTOR_STOP" ? "12420007d0000000" : "12440007d0000000"));
            preset.insert("action", 1); config.root.insert("sdkDefaults", QJsonObject{{operation, preset}});
            QVERIFY(!config.validate(&error)); config.root.remove("sdkDefaults");
        }
        QVERIFY(config.save(directory.filePath("actions.json"), &error));
        ProtocolConfig loaded; QVERIFY(ProtocolConfig::load(directory.filePath("actions.json"), &loaded, &error));
        QCOMPARE(loaded.root, config.root);
        QVERIFY(ConversionEngine(loaded).convert("/1A2000R\r", false, &preview, &error));
        QJsonObject parameters = mapping.value("parameters").toObject();
        parameters.insert("mode", QJsonObject{{"kind", "constant"}, {"value", 1}});
        mapping.insert("parameters", parameters); config.root.insert("mappings", QJsonArray{mapping});
        QVERIFY(!config.validate(&error)); // 高级JSON也不能为新动作添加工作模式参数。
    }
    QJsonObject device = base.root.value("device").toObject(); device.remove("absoluteAction"); base.root.insert("device", device);
    QVERIFY(!base.validate(&error)); // 旧ABS的方向缺失仍需明确拒绝，不能静默猜测。

    ProtocolConfig sample; QVERIFY(ProtocolConfig::load(example("motor-actions.json"), &sample, &error));
    ConversionPreview result;
    QVERIFY(ConversionEngine(sample).convert("!F2000\r", false, &result, &error));
    QCOMPARE(result.targetFrame.mid(8, 8), QByteArray::fromHex("11410003e80007d0"));
    QVERIFY(ConversionEngine(sample).convert("!B500\r", false, &result, &error));
    QCOMPARE(result.targetFrame.mid(8, 8), QByteArray::fromHex("11430003e80001f4"));
    for (const QByteArray &input : {QByteArray("!S\r"), QByteArray("!H\r")}) {
        QVERIFY(ConversionEngine(sample).convert(input, true, &result, &error));
        QCOMPARE(result.targetFrame.mid(8, 8), QByteArray::fromHex(input.at(1) == 'S' ? "11420003e8000000" : "11440003e8000000"));
    }
}

void FrameworkTest::fixedMotorActionRequestLifecycle_data()
{
    QTest::addColumn<QString>("operation"); QTest::addColumn<QString>("policy");
    QTest::newRow("forward-ack") << QString("MOTOR_FORWARD") << QString("ack");
    QTest::newRow("forward-complete") << QString("MOTOR_FORWARD") << QString("complete");
    QTest::newRow("backward-ack") << QString("MOTOR_BACKWARD") << QString("ack");
    QTest::newRow("backward-complete") << QString("MOTOR_BACKWARD") << QString("complete");
}

void FrameworkTest::fixedMotorActionRequestLifecycle()
{
    QFETCH(QString, operation); QFETCH(QString, policy);
    QushengProtocol codec; codec.setAddresses(2,1); FakeTransport transport;
    RequestManager manager(&codec,&transport);
    QSignalSpy results(&manager,&RequestManager::completed), states(&manager,&RequestManager::stateChanged);
    UnifiedCommand move; move.requestId=1; move.targetDevice="17"; move.operation=operation;
    move.responsePolicy=policy; move.parameters={{"positionUm",2000},{"speedUmPerS",1000}};
    manager.enqueue(move); QTRY_COMPARE(transport.sent.size(),1);
    QCOMPARE(quint8(transport.sent.first().at(7)),quint8(0x11));
    transport.receive(QushengProtocol::makeReply(0,0)); QTRY_VERIFY(!manager.hasPendingRequests());
    QCOMPARE(results.count(),1); QVERIFY(manager.deviceBusy());
    QCOMPARE(qvariant_cast<RequestState>(states.last().at(1)),RequestState::Acknowledged);
    QTest::qWait(300); QCOMPARE(transport.sent.size(),1);
    // 手动查询仍可使用，且不会继续触发下一次查询。
    UnifiedCommand query; query.requestId=2; query.targetDevice="17"; query.operation="QUERY_MOTOR";
    manager.enqueue(query); QTRY_COMPARE(transport.sent.size(),2);
    transport.receive(motorReply(1,1,2000)); QTRY_VERIFY(!manager.hasPendingRequests());
    QVERIFY(!manager.deviceBusy()); QCOMPARE(results.count(),2);
    QTest::qWait(250); QCOMPARE(transport.sent.size(),2);
}

static UnifiedCommand motorRequest(quint64 id, const QString &operation)
{
    UnifiedCommand command; command.requestId = id; command.operation = operation; command.targetDevice = "17";
    command.parameters = {{"positionUm", 2000}, {"speedUmPerS", 1000}, {"absoluteAction", 1}};
    return command;
}

void FrameworkTest::stopAndResetLifecycle_data()
{
    QTest::addColumn<QString>("operation"); QTest::addColumn<int>("state"); QTest::addColumn<quint32>("position");
    QTest::newRow("stop-idle-at-arbitrary-position") << QString("MOTOR_STOP") << 1 << quint32(777);
    QTest::newRow("reset-logical-zero-offset") << QString("MOTOR_RESET") << 1 << quint32(777);
    QTest::newRow("stop-unhomed") << QString("MOTOR_STOP") << 0 << quint32(0xffffff);
    QTest::newRow("stop-idle-unknown-position") << QString("MOTOR_STOP") << 1 << quint32(0xffffff);
}

void FrameworkTest::stopAndResetLifecycle()
{
    QFETCH(QString, operation); QFETCH(int,state); QFETCH(quint32,position);
    QushengProtocol codec; codec.setAddresses(2,1); FakeTransport transport; RequestManager manager(&codec,&transport);
    QSignalSpy states(&manager,&RequestManager::stateChanged);
    manager.enqueue(motorRequest(1,operation)); QTRY_COMPARE(transport.sent.size(),1);
    transport.receive(QushengProtocol::makeReply(0,0)); QTRY_VERIFY(!manager.hasPendingRequests());
    QCOMPARE(qvariant_cast<RequestState>(states.last().at(1)),RequestState::Acknowledged);
    QVERIFY(manager.deviceBusy()); QTest::qWait(250); QCOMPARE(transport.sent.size(),1);
    UnifiedCommand query; query.requestId=2; query.operation="QUERY_MOTOR"; query.targetDevice="17";
    manager.enqueue(query); QTRY_COMPARE(transport.sent.size(),2);
    transport.receive(motorReply(1,state,position)); QTRY_VERIFY(!manager.hasPendingRequests());
    QCOMPARE(manager.deviceBusy(),state==0 || position==0xffffff);
    QCOMPARE(qvariant_cast<RequestState>(states.last().at(1)),state==0 || position==0xffffff ? RequestState::Failed : RequestState::Succeeded);
}

void FrameworkTest::stopInterruptsMotionAndCancelsQueue()
{
    QushengProtocol codec; codec.setAddresses(2, 1); FakeTransport transport; RequestManager manager(&codec, &transport);
    manager.setTimeouts(1000, 800);
    QSignalSpy states(&manager, &RequestManager::stateChanged), unmatched(&manager, &RequestManager::unsolicitedMessage);
    manager.enqueue(motorRequest(1, "MOTOR_FORWARD")); QTRY_COMPARE(transport.sent.size(), 1);
    manager.enqueue(motorRequest(2, "MOTOR_BACKWARD"));
    UnifiedCommand stop = motorRequest(3, "MOTOR_STOP"), other = stop; other.targetDevice = "18";
    QVERIFY(!manager.canInterruptWithStop(other)); QVERIFY(manager.canInterruptWithStop(stop));
    manager.enqueue(stop); QTRY_COMPARE(transport.sent.size(), 2);
    bool interrupted = false, queuedCancelled = false;
    for (const auto &signal : states) if (qvariant_cast<RequestState>(signal.at(1)) == RequestState::Cancelled) {
        interrupted |= signal.at(0).toULongLong() == 1; queuedCancelled |= signal.at(0).toULongLong() == 2;
    }
    QVERIFY(interrupted && queuedCancelled); QCOMPARE(quint8(transport.sent.last().at(9)), quint8(0x42));
    transport.receive(QushengProtocol::makeReply(0, 0)); QCOMPARE(unmatched.count(), 1);
    transport.receive(QushengProtocol::makeReply(1, 0));
    QTRY_VERIFY(!manager.hasPendingRequests());
    QCOMPARE(qvariant_cast<RequestState>(states.last().at(1)),RequestState::Acknowledged);
    QVERIFY(manager.deviceBusy()); QTest::qWait(300); QCOMPARE(transport.sent.size(),2);
}

void FrameworkTest::stopWaitsForWrite_data()
{
    QTest::addColumn<bool>("earlyReply");
    QTest::newRow("write-pending") << false;
    QTest::newRow("reply-before-bytes-written") << true;
}

void FrameworkTest::stopWaitsForWrite()
{
    QFETCH(bool, earlyReply);
    QushengProtocol codec; codec.setAddresses(2, 1); FakeTransport transport; transport.stall = true;
    RequestManager manager(&codec, &transport); manager.setTimeouts(1000, 800);
    QSignalSpy states(&manager, &RequestManager::stateChanged);
    manager.enqueue(motorRequest(1, "MOTOR_FORWARD")); QTRY_COMPARE(transport.sent.size(), 1);
    if (earlyReply) transport.receive(QushengProtocol::makeReply(0, 0));
    manager.enqueue(motorRequest(2, "MOTOR_STOP")); QTest::qWait(30); QCOMPARE(transport.sent.size(), 1);
    transport.sendFinished(1, true, QString()); QTRY_COMPARE(transport.sent.size(), 2);
    QCOMPARE(quint8(transport.sent.last().at(9)), quint8(0x42));
    transport.sendFinished(1, true, QString()); // 旧写出通知不得释放新的停止写操作。
    transport.sendFinished(2, true, QString());
    transport.receive(QushengProtocol::makeReply(0, 0));
    transport.receive(QushengProtocol::makeReply(1, 0));
    QTRY_VERIFY(!manager.hasPendingRequests());
    QCOMPARE(qvariant_cast<RequestState>(states.last().at(1)),RequestState::Acknowledged);
    QVERIFY(manager.deviceBusy()); QTest::qWait(250); QCOMPARE(transport.sent.size(),2);
}

void FrameworkTest::motorControlErrorsAllowNextSend()
{
    for (const QString &operation : {QString("MOTOR_STOP"),QString("MOTOR_RESET")}) {
        for (int scenario=0;scenario<3;++scenario) {
            QushengProtocol codec; codec.setAddresses(2,1); FakeTransport transport; RequestManager manager(&codec,&transport);
            manager.setTimeouts(100,50); QSignalSpy states(&manager,&RequestManager::stateChanged);
            manager.enqueue(motorRequest(1,"MOTOR_FORWARD")); QTRY_COMPARE(transport.sent.size(),1); manager.cancel(1);
            manager.enqueue(motorRequest(2,operation)); QTRY_COMPARE(transport.sent.size(),2);
            if(scenario==0) transport.receive(QushengProtocol::makeReply(1,4));
            if(scenario==2) transport.disconnectNow();
            QTRY_VERIFY(!manager.hasPendingRequests()); QVERIFY(manager.deviceBusy());
            QCOMPARE(qvariant_cast<RequestState>(states.last().at(1)),scenario==1 ? RequestState::TimedOut : RequestState::Failed);
            transport.connected=true;
            manager.enqueue(motorRequest(3,"MOTOR_FORWARD"));
            QTRY_COMPARE(transport.sent.size(),3);
            QCOMPARE(quint8(transport.sent.last().at(7)),quint8(0x11));
            transport.receive(QushengProtocol::makeReply(2,0));
            QTRY_VERIFY(!manager.hasPendingRequests());
            QCOMPARE(qvariant_cast<RequestState>(states.last().at(1)),RequestState::Acknowledged);
            QTest::qWait(250); QCOMPARE(transport.sent.size(),3);
        }
    }
}

void FrameworkTest::consecutiveMovesNeedNoStatusQuery()
{
    QushengProtocol codec; codec.setAddresses(2,1); FakeTransport transport;
    RequestManager manager(&codec,&transport);
    QSignalSpy results(&manager,&RequestManager::completed);
    // 用户连续发同一条命令，再改发后退，无需插入查询；旧忙状态仍可显示。
    const QStringList actions{"MOTOR_FORWARD","MOTOR_FORWARD","MOTOR_BACKWARD"};
    for (int i=0;i<actions.size();++i) {
        manager.enqueue(motorRequest(i+1,actions.at(i)));
        QTRY_COMPARE(transport.sent.size(),i+1);
        QCOMPARE(quint8(transport.sent.last().at(7)),quint8(0x11));
        QCOMPARE(quint8(transport.sent.last().at(6)),quint8(i));
        transport.receive(QushengProtocol::makeReply(quint8(i),0));
        QTRY_VERIFY(!manager.hasPendingRequests());
        QVERIFY(manager.deviceBusy());
        QCOMPARE(results.count(),i+1);
        QVERIFY(qvariant_cast<UnifiedResult>(results.last().at(0)).success);
    }
    QTest::qWait(300); QCOMPARE(transport.sent.size(),3);
}

void FrameworkTest::queuedMovesWaitForReplyAndCompleteWrite()
{
    QushengProtocol codec; codec.setAddresses(2,1); FakeTransport transport; transport.stall=true;
    RequestManager manager(&codec,&transport); manager.setTimeouts(1000,800);
    manager.enqueue(motorRequest(1,"MOTOR_FORWARD"));
    manager.enqueue(motorRequest(2,"MOTOR_FORWARD"));
    manager.enqueue(motorRequest(3,"MOTOR_BACKWARD"));
    QTRY_COMPARE(transport.sent.size(),1);
    transport.receive(QushengProtocol::makeReply(0,0)); // 应答先到，仍须写完第一帧。
    QTest::qWait(30); QCOMPARE(transport.sent.size(),1);
    transport.sendFinished(1,true,QString()); QTRY_COMPARE(transport.sent.size(),2);
    transport.sendFinished(2,true,QString()); // 第二帧已写完，仍等本次应答。
    QTest::qWait(30); QCOMPARE(transport.sent.size(),2);
    transport.receive(QushengProtocol::makeReply(1,0)); QTRY_COMPARE(transport.sent.size(),3);
    transport.sendFinished(3,true,QString()); transport.receive(QushengProtocol::makeReply(2,0));
    QTRY_VERIFY(!manager.hasPendingRequests());
    for (const QByteArray &frame : transport.sent) QCOMPARE(quint8(frame.at(7)),quint8(0x11));
    QTest::qWait(250); QCOMPARE(transport.sent.size(),3);
}

void FrameworkTest::reportedStatusDoesNotGateSending_data()
{
    QTest::addColumn<int>("motorState"); QTest::addColumn<int>("position");
    QTest::newRow("unhomed") << 0 << 0xffffff;
    QTest::newRow("idle") << 1 << 1000;
    QTest::newRow("resetting") << 2 << 0xffffff;
    QTest::newRow("moving") << 3 << 1000;
    QTest::newRow("motor-error") << 4 << 1000;
    QTest::newRow("unknown-position") << 1 << 0xffffff;
}

void FrameworkTest::reportedStatusDoesNotGateSending()
{
    QFETCH(int,motorState); QFETCH(int,position);
    QushengProtocol codec; codec.setAddresses(2,1); FakeTransport transport;
    RequestManager manager(&codec,&transport);
    UnifiedCommand query; query.requestId=1; query.operation="QUERY_MOTOR"; query.targetDevice="17";
    manager.enqueue(query); QTRY_COMPARE(transport.sent.size(),1);
    transport.receive(motorReply(0,motorState,position));
    QTRY_VERIFY(!manager.hasPendingRequests());
    manager.enqueue(motorRequest(2,"MOTOR_FORWARD"));
    QTRY_COMPARE(transport.sent.size(),2);
    QCOMPARE(quint8(transport.sent.last().at(7)),quint8(0x11));
    transport.receive(QushengProtocol::makeReply(1,0));
    QTRY_VERIFY(!manager.hasPendingRequests());
    QTest::qWait(250); QCOMPARE(transport.sent.size(),2);
}

void FrameworkTest::debugSequenceWraps()
{
    QushengProtocol codec;
    UnifiedCommand query; query.operation="QUERY_MOTOR"; query.targetDevice="17";
    query.packetDebug=true; query.destinationAddress=2; query.sourceAddress=1;
    QByteArray frame; TranslationError error;
    for (int i=0;i<600;++i) {
        query.requestId=i+1;
        QVERIFY2(codec.encodeCommand(query,&frame,&error),qPrintable(error.message));
        QCOMPARE(quint8(frame.at(6)),quint8(i));
    }
}

void FrameworkTest::debugRelativeSendsWithoutPreflight()
{
    QushengProtocol codec; codec.setAddresses(2,1); FakeTransport transport; RequestManager manager(&codec,&transport);
    UnifiedCommand move=motorRequest(1,"RELATIVE_FORWARD"); move.packetDebug=true;
    move.parameters.insert("distanceUm",7000);
    manager.enqueue(move); QTRY_COMPARE(transport.sent.size(),1);
    QCOMPARE(quint8(transport.sent.first().at(7)),quint8(0x11));
    QCOMPARE(transport.sent.first().mid(8,8),QByteArray::fromHex("11210003e8001b58"));
    transport.receive(QushengProtocol::makeReply(0,0)); QTRY_VERIFY(!manager.hasPendingRequests());
    QTest::qWait(300); QCOMPARE(transport.sent.size(),1);
}

void FrameworkTest::debugQueuedAddressesAreSnapshots()
{
    QushengProtocol codec; FakeTransport transport; RequestManager manager(&codec,&transport);
    UnifiedCommand first=motorRequest(1,"MOTOR_FORWARD"); first.packetDebug=true;
    first.destinationAddress=2; first.sourceAddress=1;
    UnifiedCommand second=first; second.requestId=2; second.destinationAddress=3; second.sourceAddress=5;
    manager.enqueue(first); manager.enqueue(second); QTRY_COMPARE(transport.sent.size(),1);
    codec.setAddresses(9,8); // 编辑/启用另一地址不会修改本次回告匹配或已入队的地址。
    transport.receive(QushengProtocol::makeReply(0,0)); QTRY_COMPARE(transport.sent.size(),2);
    QCOMPARE(quint8(transport.sent.last().at(2)),quint8(3));
    QCOMPARE(quint8(transport.sent.last().at(3)),quint8(5));
    transport.receive(QushengProtocol::makeFrame(5,3,1,0x11,QByteArray(4,'\0')));
    QTRY_VERIFY(!manager.hasPendingRequests());
}

void FrameworkTest::relativePreflightWaitsForWrite()
{
    QushengProtocol codec; codec.setAddresses(2, 1); FakeTransport transport; transport.stall = true;
    RequestManager manager(&codec, &transport); manager.setTimeouts(1000, 800);
    UnifiedCommand relative = motorRequest(1, "RELATIVE_FORWARD");
    relative.parameters.insert("distanceUm", 500); relative.parameters.insert("minPositionUm", 0); relative.parameters.insert("maxPositionUm", 6000);
    manager.enqueue(relative); QTRY_COMPARE(transport.sent.size(), 1);
    transport.receive(motorReply(0, 1, 1000)); QTest::qWait(30); QCOMPARE(transport.sent.size(), 1);
    transport.sendFinished(1, true, QString()); QTRY_COMPARE(transport.sent.size(), 2);
    QCOMPARE(transport.sent.last().mid(8, 8), QByteArray::fromHex("11210003e80001f4"));
}

void FrameworkTest::finalControlReplyWaitsForWrite()
{
    QushengProtocol codec; codec.setAddresses(2,1); FakeTransport transport; transport.stall=true;
    RequestManager manager(&codec,&transport); manager.setTimeouts(1000,800);
    QSignalSpy results(&manager,&RequestManager::completed);
    manager.enqueue(motorRequest(1,"MOTOR_STOP")); QTRY_COMPARE(transport.sent.size(),1);
    transport.receive(QushengProtocol::makeReply(0,0)); QCOMPARE(results.count(),1);
    UnifiedCommand query; query.requestId=2; query.operation="QUERY_MOTOR"; query.targetDevice="17";
    manager.enqueue(query); QTest::qWait(250); QCOMPARE(transport.sent.size(),1);
    QVERIFY(manager.hasPendingRequests()); QVERIFY(manager.deviceBusy());
    transport.sendFinished(1,true,QString()); QTRY_COMPARE(transport.sent.size(),2);
    transport.sendFinished(2,true,QString()); transport.receive(motorReply(1,1,123));
    QTRY_VERIFY(!manager.hasPendingRequests()); QVERIFY(!manager.deviceBusy());
    QTest::qWait(250); QCOMPARE(transport.sent.size(),2);
}

void FrameworkTest::cancellingQueuedStopKeepsOriginalActive()
{
    QushengProtocol codec; codec.setAddresses(2, 1); FakeTransport transport; transport.stall = true;
    RequestManager manager(&codec, &transport); manager.setTimeouts(1000, 800);
    manager.enqueue(motorRequest(1, "MOTOR_FORWARD")); QTRY_COMPARE(transport.sent.size(), 1);
    const auto stop = motorRequest(2, "MOTOR_STOP"); manager.enqueue(stop); manager.cancel(2);
    QVERIFY(manager.hasPendingRequests()); QVERIFY(manager.canInterruptWithStop(stop));
    transport.sendFinished(1, true, QString()); QTest::qWait(30); QCOMPARE(transport.sent.size(), 1);
    manager.cancel(1); QVERIFY(!manager.hasPendingRequests()); QVERIFY(manager.deviceBusy());
}

QTEST_GUILESS_MAIN(FrameworkTest)
#include "tst_framework.moc"
