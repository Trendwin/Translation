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
    void configuredDtStyleAndRoundTrip();
    void configurationChangesNeedNoCppChange();
    void genericParserRejectsInvalidInputs();
    void hexEndianAndGenericStreamFraming();
    void finalQushengFrameMatchesSample();
    void runtimeHasExactlyOneDeviceTransport();
    void strictConfigurationAndByteInput();
    void parameterRelationsAndRounding();
    void signedFixedFieldsAndOverflow();
    void fixedBinaryFramesWithEmbeddedFooter();
    void ackAndMotionTimeouts();
    void cancelThenQueryRecoversBusyState();
    void demoFramingRegression();
    void codecReplyAndCrc();
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
};

static QString example(const QString &name)
{
    // qmake 在独立构建目录中把 __FILE__ 设为相对路径，必须相对构建目录解析，不能依赖启动目录。
    const QString source = QDir(QString::fromLatin1(QT_TESTCASE_BUILDDIR)).absoluteFilePath(QString::fromLatin1(__FILE__));
    return QFileInfo(source).dir().filePath("../examples/" + name);
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
    QCOMPARE(executableOptions, 5);
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
    QVERIFY(!ConversionEngine(config).convert("!FAN50\r", false, &result, &error));
    QCOMPARE(error.code, QStringLiteral("COMMAND_NOT_IMPLEMENTED"));
    QVERIFY(result.targetFrame.isEmpty());
    QVERIFY(error.message.contains(QString::fromUtf8(u8"待实现")));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(config.save(dir.filePath("draft.json"), &error));
    ProtocolConfig reloaded;
    QVERIFY(ProtocolConfig::load(dir.filePath("draft.json"), &reloaded, &error));
    QCOMPARE(reloaded.root, config.root);
    QVERIFY(ConversionEngine(reloaded).convert("!MOVE2000\r", false, &result, &error));
    QCOMPARE(result.targetFrame, goodFrame);
    QVERIFY(!ConversionEngine(reloaded).convert("!FAN50\r", true, &result, &error));
    QCOMPARE(error.code, QStringLiteral("COMMAND_NOT_IMPLEMENTED"));
    QVERIFY(result.targetFrame.isEmpty());

    // 任意目录状态/高级配置标志都不能授予执行权限，真实发送和预览共享同一编译白名单。
    QJsonArray mappings = config.root.value("mappings").toArray();
    QJsonObject pending = mappings.at(1).toObject();
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
    QushengProtocol codec;
    codec.setAddresses(2, 1);
    FakeTransport transport;
    RequestManager manager(&codec, &transport);
    manager.setTimeouts(100, 100, 2000);
    int completed = 0;
    UnifiedResult last;
    QObject::connect(&manager, &RequestManager::completed, &manager,
                     [&](const UnifiedResult &result) { ++completed; last = result; });
    UnifiedCommand move;
    move.requestId = 1;
    move.targetDevice = "17";
    move.operation = "ABSOLUTE_MOVE";
    move.responsePolicy = "complete";
    move.parameters.insert("positionUm", 20000);
    move.parameters.insert("speedUmPerS", 1000);
    move.parameters.insert("absoluteAction", 1);
    manager.enqueue(move);
    QTRY_COMPARE(transport.sent.size(), 1);
    transport.receive(QushengProtocol::makeReply(0, 0));
    QTest::qWait(10);
    QCOMPARE(completed, 0);
    QVERIFY(manager.deviceBusy());
    transport.receive(QushengProtocol::makeFrame(1, 2, 99, 0x17,
                                                  QByteArray::fromHex("1110004e20ffffff")));
    QCOMPARE(completed, 0);
    QTRY_VERIFY(transport.sent.size() >= 2);
    transport.receive(QushengProtocol::makeFrame(1, 2, 1, 0x12,
                                                  QByteArray::fromHex("0000000030004e20ffffff")));
    QCOMPARE(completed, 0);
    QTRY_VERIFY(transport.sent.size() >= 3);
    transport.receive(QushengProtocol::makeFrame(1, 2, 2, 0x12,
                                                  QByteArray::fromHex("0000000010004e20ffffff")));
    QTRY_COMPARE(completed, 1);
    QVERIFY(last.success);
    QVERIFY(!manager.deviceBusy());
}

void FrameworkTest::managerStalledWriteAndRejection()
{
    QushengProtocol codec;
    codec.setAddresses(2, 1);
    FakeTransport transport;
    RequestManager manager(&codec, &transport);
    manager.setTimeouts(20, 50, 100);
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

void FrameworkTest::ackAndMotionTimeouts()
{
    QushengProtocol codec; codec.setAddresses(2, 1);
    FakeTransport transport;
    RequestManager manager(&codec, &transport); manager.setTimeouts(50, 30, 60);
    QSignalSpy states(&manager, &RequestManager::stateChanged);
    int completed = 0; UnifiedResult last;
    QObject::connect(&manager, &RequestManager::completed, &manager, [&](const UnifiedResult &r) { ++completed; last = r; });
    UnifiedCommand query; query.requestId = 1; query.operation = "QUERY_MOTOR"; query.targetDevice = "17";
    manager.enqueue(query);
    QTRY_COMPARE(completed, 1); QCOMPARE(last.error.code, QStringLiteral("ACK_TIMEOUT"));
    UnifiedCommand move; move.requestId = 2; move.operation = "ABSOLUTE_MOVE"; move.targetDevice = "17";
    move.responsePolicy = "ack";
    move.parameters = {{"positionUm", 20000}, {"speedUmPerS", 1000}, {"absoluteAction", 1}};
    manager.enqueue(move); QTRY_COMPARE(transport.sent.size(), 2);
    transport.receive(QushengProtocol::makeReply(1, 0));
    QTRY_COMPARE(completed, 2); QVERIFY(last.success);
    QVERIFY(manager.deviceBusy());
    TranslationError finalError;
    QObject::connect(&manager, &RequestManager::protocolError, &manager, [&](const TranslationError &e) { finalError = e; });
    QTRY_COMPARE(finalError.code, QStringLiteral("MOTION_TIMEOUT"));
    QCOMPARE(completed, 2); QVERIFY(manager.deviceBusy());
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
    QTest::newRow("absolute-ack") << QByteArray("/1A2000R\r") << QString("ABSOLUTE_MOVE") << QString("ack") << 0x11 << QByteArray::fromHex("11410003e80007d0");
    QTest::newRow("absolute-complete") << QByteArray("/1a2000R\r") << QString("ABSOLUTE_MOVE") << QString("complete") << 0x11 << QByteArray::fromHex("11410003e80007d0");
    QTest::newRow("relative-forward-ack") << QByteArray("/1P500R\r") << QString("RELATIVE_FORWARD") << QString("ack") << 0x11 << QByteArray::fromHex("11210003e80001f4");
    QTest::newRow("relative-forward-complete") << QByteArray("/1p500R\r") << QString("RELATIVE_FORWARD") << QString("complete") << 0x11 << QByteArray::fromHex("11210003e80001f4");
    QTest::newRow("relative-backward-ack") << QByteArray("/1D500R\r") << QString("RELATIVE_BACKWARD") << QString("ack") << 0x11 << QByteArray::fromHex("11230003e80001f4");
    QTest::newRow("relative-backward-complete") << QByteArray("/1d500R\r") << QString("RELATIVE_BACKWARD") << QString("complete") << 0x11 << QByteArray::fromHex("11230003e80001f4");
    QTest::newRow("position") << QByteArray("/1?\r") << QString("QUERY_MOTOR") << QString("ack") << 0x12 << QByteArray::fromHex("11");
    QTest::newRow("position-alias") << QByteArray("/1?0\r") << QString("QUERY_MOTOR") << QString("ack") << 0x12 << QByteArray::fromHex("11");
    QTest::newRow("pulse-position") << QByteArray("/1?4\r") << QString("QUERY_MOTOR") << QString("ack") << 0x12 << QByteArray::fromHex("11");
    QTest::newRow("version") << QByteArray("/1&\r") << QString("QUERY_DEVICE_INFO") << QString("ack") << 0x04 << QByteArray::fromHex("0000");
    QTest::newRow("version-alias") << QByteArray("/1?23\r") << QString("QUERY_DEVICE_INFO") << QString("ack") << 0x04 << QByteArray::fromHex("0000");
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
        const QString key = operation == "ABSOLUTE_MOVE" ? "positionUm" : "distanceUm";
        QCOMPARE(preview.command.parameters.value(key).toInt(), operation == "ABSOLUTE_MOVE" ? 2000 : 500);
    } else {
        QCOMPARE(preview.fieldOrder.size(), 2);
        QVERIFY(!preview.fields.contains(QString::fromUtf8(u8"位置")));
        QVERIFY(preview.command.parameters.isEmpty());
    }
}

void FrameworkTest::mappedInputFormatsAndRoundTrip()
{
    ProtocolConfig config; TranslationError error; ConversionPreview preview;
    QVERIFY(ProtocolConfig::load(example("dt-simple-commands.json"), &config, &error));
    ConversionEngine engine(config);
    for (const QByteArray &bad : {QByteArray("/1A2000\r"), QByteArray("/1?23R\r"), QByteArray("/1?01\r"), QByteArray("/1AR\r"),
                                 QByteArray("/1A2xR\r"), QByteArray("/1A6001R\r"), QByteArray("/1A500P20R\r"),
                                 QByteArray("/1#\r"), QByteArray("/1?20\r"), QByteArray("/1QR\r"), QByteArray("1A2000R\r")}) {
        QVERIFY2(!engine.convert(bad, false, &preview, &error), bad.constData()); QVERIFY(preview.targetFrame.isEmpty());
    }
    QList<TranslationError> errors;
    QVERIFY(engine.feed("noise/1?", &errors).isEmpty());
    const auto frames = engine.feed("23\r/1A2000R\r/1?4\r", &errors);
    QCOMPARE(frames.size(), 3); QVERIFY(errors.isEmpty()); QCOMPARE(frames.at(0), QByteArray("/1?23\r"));
    QTemporaryDir directory; const QString saved = directory.filePath("commands.json");
    QVERIFY(config.save(saved, &error)); ProtocolConfig reloaded; QVERIFY(ProtocolConfig::load(saved, &reloaded, &error));
    QCOMPARE(config.version, reloaded.version);
    ConversionPreview second;
    QVERIFY(engine.convert("/1P500R\r", false, &preview, &error));
    QVERIFY(ConversionEngine(reloaded).convert("/1P500R\r", false, &second, &error));
    QCOMPARE(preview.targetFrame, second.targetFrame);
    // 将客户命令换成任意多字符值并更改全局/查询帧尾，通用解析器不需要客户专用分支。
    QJsonObject root = config.root, external = root.value("external").toObject();
    external.insert("header", "!"); external.insert("footer", ";\\n"); root.insert("external", external);
    QJsonArray mappings = root.value("mappings").toArray();
    QJsonObject move = mappings.at(0).toObject(); move.insert("commandCode", "MOVE"); mappings.replace(0, move);
    QJsonObject query = mappings.at(10).toObject(); query.insert("commandCode", "VERSION");
    QJsonObject format = query.value("input").toObject(); format.insert("footer", "~\\n"); query.insert("input", format); mappings.replace(10, query);
    root.insert("mappings", mappings); QVERIFY(ProtocolConfig::fromJson(QJsonDocument(root).toJson(), &config, &error));
    QVERIFY(ConversionEngine(config).convert("!1MOVE2000;\n", false, &preview, &error));
    QCOMPARE(preview.command.parameters.value("positionUm").toInt(), 2000);
    QVERIFY(ConversionEngine(config).convert("!1VERSION~\n", false, &preview, &error));
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
        transport.receive(QushengProtocol::makeReply(1, 0)); QCOMPARE(completed, 0); QVERIFY(manager.deviceBusy());
        QTRY_COMPARE(transport.sent.size(), 3); transport.receive(motorReply(2, 1, 1000));
        QCOMPARE(completed, 0); QVERIFY(manager.deviceBusy()); // 空闲但位置不符，不能冒充完成。
        QTRY_COMPARE(transport.sent.size(), 4); transport.receive(motorReply(3, 1, input.at(2) == 'p' ? 1500 : 500));
        QTRY_COMPARE(completed, 1); QVERIFY(result.success); QVERIFY(!manager.deviceBusy());
        QCOMPARE(transport.sent.size(), 4); // 只发一条运动，另外三条均为状态查询。
    }
}

void FrameworkTest::relativeMoveRejectsUnsafeStartAndTimeout()
{
    ProtocolConfig config; TranslationError error; ConversionPreview preview;
    QVERIFY(ProtocolConfig::load(example("dt-simple-commands.json"), &config, &error));
    QVERIFY(ConversionEngine(config).convert("/1D2000R\r", false, &preview, &error));
    for (int scenario = 0; scenario < 4; ++scenario) {
        QushengProtocol codec; codec.setAddresses(2, 1); FakeTransport transport;
        RequestManager manager(&codec, &transport); manager.setTimeouts(50, 30, 300);
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

QTEST_GUILESS_MAIN(FrameworkTest)
#include "tst_framework.moc"
