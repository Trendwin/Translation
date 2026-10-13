#include <QtTest>
#include "qusheng/sdk/qushengsdk.h"
#include <limits>

class SdkRequestTest : public QObject {
    Q_OBJECT
private slots:
    void coverage();
    void protocolPayloads_data();
    void protocolPayloads();
    void defaultsAndPriority();
    void requiredBusinessValues();
    void deviceChannelsAreIndependent();
    void rejectedParameters_data();
    void rejectedParameters();
    void integerBoundaries();
    void deferredInterfaces();
    void resolvedSnapshotsAndStrictEncode();
};

void SdkRequestTest::coverage()
{
    const auto codes = QushengSdk::commandCodes();
    QCOMPARE(codes.size(), 56);
    const QList<quint8> expected = {0x01,0x03,0x04,0x05,0x08,0x09,0x0c,0x11,0x12,0x14,0x16,0x1a,0x21,0x22,0x23,0x24,0x25,0x44,0x46,0x54,0x63,0x64,0x65,0x81,0x82,0x83,0x84,0x91,0x92,0xa1};
    QList<quint8> supported;
    for (auto code : codes) {
        const auto command = QushengSdk::definition(code);
        QCOMPARE(command.code, code);
        QVERIFY(!command.name.isEmpty());
        QVERIFY(!command.name.contains(QChar(0xfffd)));
        if (QushengSdk::isSupported(code)) supported.append(code);
        QSet<QString> keys;
        for (const auto &parameter : command.parameters) {
            QVERIFY(!keys.contains(parameter.key)); keys.insert(parameter.key);
            QVERIFY(!parameter.label.isEmpty()); QVERIFY(parameter.minimum <= parameter.maximum);
        }
    }
    QCOMPARE(supported, expected);
    QVERIFY(!QushengSdk::isSupported(0xff));
    QVERIFY(QushengSdk::definition(0xff).name.isEmpty());
}

// 每条数据都是按协议表手工写出的预期字节，不使用SDK解码器生成期望值。
// 跨字节数据用不对称数值，便于发现高低字节颠倒、掩码移位和字段遗漏。
void SdkRequestTest::protocolPayloads_data()
{
    QTest::addColumn<int>("command");
    QTest::addColumn<QVariantMap>("parameters");
    QTest::addColumn<QByteArray>("expected");
    auto row = [](const char *name, int command, QVariantMap parameters, const char *hex) {
        QTest::newRow(name) << command << parameters << QByteArray::fromHex(hex);
    };
    row("01-enable", 0x01, {{"enableMask",65534}}, "FFFE");
    row("03-status", 0x03, {}, "");
    row("04-info", 0x04, {}, "0000");
    row("05-stop", 0x05, {{"devId",255}}, "FF");
    row("08-heartbeat", 0x08, {{"intervalMs",0x1234}}, "1234");
    row("09-error-reports", 0x09, {{"enabled",1}}, "01");
    row("0C-interface-query", 0x0c, {}, "");
    row("11-motor", 0x11, {{"positionUm",0x654321},{"speedUmPerS",0x123456}}, "1141123456654321");
    row("12-motor-query", 0x12, {{"devId",0x1c}}, "1C");
    row("14-hardware-query", 0x14, {{"devId",0x1a}}, "1A");
    row("16-motion-query", 0x16, {{"devId",0x19}}, "19");
    row("1A-change-speed", 0x1a, {{"devId",0x13},{"speed",0x123456}}, "13123456");
    row("21-valve-group", 0x21, {{"mode",2},{"valveMask",0x12345678},{"duration",0x01020304}}, "9234567801020304");
    row("22-valve", 0x22, {{"devId",0x4e},{"duration",QVariant::fromValue(4294967295LL)}}, "4EFFFFFFFF");
    row("23-valve-query", 0x23, {}, "0000");
    row("24-valve-timeouts", 0x24, {{"group1Timeout",0x01020304},{"group2Timeout",QVariant::fromValue(4294967295LL)},{"group3Timeout",0x10203040}}, "01020304FFFFFFFF10203040");
    row("25-valve-config-query", 0x25, {}, "");
    row("44-adc", 0x44, {{"devId",0x68}}, "68");
    row("46-adc-report", 0x46, {{"devId",0x67},{"intervalMs",0x123456}}, "67123456");
    row("54-dac", 0x54, {{"devId",0x78},{"voltageMv",0x0abcde}}, "780ABCDE");
    row("63-power-timing", 0x63, {{"peripheralDelay",0x01020304},{"power24Delay",0x10203040},{"power12Delay",0x01002000},{"power5Delay",QVariant::fromValue(4294967295LL)}}, "010203041020304001002000FFFFFFFF");
    row("64-power-query", 0x64, {{"devId",0xdf}}, "DF");
    row("65-power", 0x65, {{"devId",0xc7},{"enabled",1}}, "C701");
    row("81-fan", 0x81, {{"devId",0xe2},{"percent",100}}, "E264");
    row("82-fan-config-query", 0x82, {{"devId",0xe1}}, "E1");
    row("83-fan-speed-query", 0x83, {{"devId",0xe0}}, "E0");
    row("84-fan-config", 0x84, {{"devId",0xe1},{"speedMode",1},{"pulsesPerRevolution",2},{"filterWindow",3},{"minOutput",4},{"maxOutput",5},{"measurementInterval",0x06070809}}, "E1010203040506070809");
    row("91-digital-output", 0x91, {{"mode",2},{"outputMask",0x15}}, "95");
    row("92-digital-input", 0x92, {}, "");
    row("A1-dc-motor", 0xa1, {{"devId",0x2e},{"pwm",-128}}, "2E80");
}

void SdkRequestTest::protocolPayloads()
{
    QFETCH(int, command); QFETCH(QVariantMap, parameters); QFETCH(QByteArray, expected);
    QushengSdkRequest request; TranslationError error;
    QVERIFY2(QushengSdk::buildRequest(quint8(command), parameters, {}, {}, &request, &error), qPrintable(error.message));
    QCOMPARE(request.command, quint8(command)); QCOMPARE(request.payload, expected);
    QByteArray encoded;
    QVERIFY(QushengSdk::encode(quint8(command), request.parameters, &encoded, &error));
    QCOMPARE(encoded, expected);
}

void SdkRequestTest::defaultsAndPriority()
{
    QushengSdkRequest request; TranslationError error;
    const QVariantMap device = {{"devId",0x12},{"speedUmPerS",2500}};
    QVERIFY(QushengSdk::buildRequest(0x11, {{"positionUm",2000}}, {}, device, &request, &error));
    QCOMPARE(request.payload, QByteArray::fromHex("12410009C40007D0"));
    QCOMPARE(request.parameterSources.value("positionUm"), QString::fromUtf8(u8"明确映射"));
    QCOMPARE(request.parameterSources.value("speedUmPerS"), QString::fromUtf8(u8"设备参数"));
    QCOMPARE(request.parameterSources.value("mode"), QString::fromUtf8(u8"软件初始值"));
    const QVariantMap defaults = {{"positionUm",22},{"devId",0x13},{"speedUmPerS",3000}};
    QVERIFY(QushengSdk::buildRequest(0x11, {{"positionUm",10}}, defaults, device, &request, &error));
    QCOMPARE(request.payload, QByteArray::fromHex("1341000BB800000A"));
    QCOMPARE(request.parameterSources.value("devId"), QString::fromUtf8(u8"SDK预置"));
    QVERIFY(QushengSdk::buildRequest(0x11, {{"positionUm",10},{"devId",0x14},{"speedUmPerS",4000}}, defaults, device, &request, &error));
    QCOMPARE(request.payload, QByteArray::fromHex("1441000FA000000A"));
    QVERIFY(QushengSdk::buildRequest(0x11, {}, defaults, {}, &request, &error));
    QCOMPARE(request.parameters.value("positionUm").toLongLong(), 22LL);

    // 普通非业务关键参数均能用可编辑初始值补齐。初始值不是宣称厂家出厂值。
    const QMap<int,QByteArray> initial = {
        {0x03,{}},{0x04,QByteArray::fromHex("0000")},{0x05,QByteArray::fromHex("FF")},
        {0x08,QByteArray::fromHex("0000")},{0x09,QByteArray::fromHex("01")},{0x0c,{}},
        {0x12,QByteArray::fromHex("11")},{0x14,QByteArray::fromHex("11")},{0x16,QByteArray::fromHex("11")},
        {0x1a,QByteArray::fromHex("110003E8")},{0x21,QByteArray::fromHex("0000000000000000")},
        {0x22,QByteArray::fromHex("3100000000")},{0x23,QByteArray::fromHex("0000")},
        {0x24,QByteArray::fromHex("000000000000000000000000")},{0x25,{}},{0x44,QByteArray::fromHex("61")},
        {0x46,QByteArray::fromHex("61000000")},{0x54,QByteArray::fromHex("71000000")},
        {0x63,QByteArray::fromHex("00000000000000000000000000000000")},{0x64,QByteArray::fromHex("C5")},
        {0x65,QByteArray::fromHex("C500")},{0x81,QByteArray::fromHex("E000")},{0x82,QByteArray::fromHex("E0")},
        {0x83,QByteArray::fromHex("E0")},{0x84,QByteArray::fromHex("E000010100FF000003E8")},
        {0x91,QByteArray::fromHex("00")},{0x92,{}},{0xa1,QByteArray::fromHex("2100")}};
    QCOMPARE(initial.size(), 28);
    for (auto it = initial.constBegin(); it != initial.constEnd(); ++it) {
        QVERIFY2(QushengSdk::buildRequest(quint8(it.key()), {}, {}, {}, &request, &error), qPrintable(error.message));
        QCOMPARE(request.payload, it.value());
    }
}

void SdkRequestTest::requiredBusinessValues()
{
    QushengSdkRequest request; TranslationError error;
    for (int command : {0x01,0x11}) {
        request.payload = "stale";
        QVERIFY(!QushengSdk::buildRequest(quint8(command), {}, {}, {}, &request, &error));
        QCOMPARE(error.code, QStringLiteral("SDK_PARAMETER_REQUIRED")); QVERIFY(request.payload.isEmpty());
        QVERIFY(error.message.contains(QString::fromUtf8(u8"需明确提供")));
    }
    QVERIFY(QushengSdk::buildRequest(0x01, {}, {{"enableMask",0x8000}}, {}, &request, &error));
    QCOMPARE(request.payload, QByteArray::fromHex("8000"));
    for (int action : {2,4}) {
        QVERIFY(QushengSdk::buildRequest(0x11, {{"action",action}}, {}, {}, &request, &error));
        QCOMPARE(request.payload, QByteArray::fromHex(action == 2 ? "11420003E8000000" : "11440003E8000000"));
    }
    QVERIFY(!QushengSdk::buildRequest(0x11, {{"action",3}}, {}, {}, &request, &error));
}

void SdkRequestTest::deviceChannelsAreIndependent()
{
    QushengSdkRequest request; TranslationError error;
    const QVariantMap motorDevice = {{"devId",0x1c},{"speedUmPerS",2500}};
    const QMap<int,int> channels = {{0x22,0x31},{0x44,0x61},{0x46,0x61},{0x54,0x71},{0x64,0xc5},{0x65,0xc5},{0x81,0xe0},{0x82,0xe0},{0x83,0xe0},{0x84,0xe0},{0xa1,0x21}};
    for (auto it = channels.constBegin(); it != channels.constEnd(); ++it) {
        QVERIFY(QushengSdk::buildRequest(quint8(it.key()), {}, {}, motorDevice, &request, &error));
        QCOMPARE(request.parameters.value("devId").toInt(), it.value());
        QVERIFY(!QushengSdk::buildRequest(quint8(it.key()), {{"devId",0x11}}, {}, {}, &request, &error));
        QVERIFY(request.payload.isEmpty());
    }
    // 1A只有协议明确的三个电机通道，设备页选到1C也不能悄悄退回11。
    QVERIFY(!QushengSdk::buildRequest(0x1a, {}, {}, motorDevice, &request, &error));
    QVERIFY(QushengSdk::buildRequest(0x1a, {}, {{"devId",0x12}}, motorDevice, &request, &error));
    QCOMPARE(request.payload, QByteArray::fromHex("120009C4"));
}

void SdkRequestTest::rejectedParameters_data()
{
    QTest::addColumn<int>("command"); QTest::addColumn<QVariantMap>("parameters");
    auto row = [](const char *name, int command, QVariantMap parameters) { QTest::newRow(name) << command << parameters; };
    row("unknown-key",0x11,{{"positionUm",0},{"rawData",1}});
    row("fixed-request-immutable",0x04,{{"fixedData",0}});
    row("fraction",0x11,{{"positionUm",1.5}});
    row("string-fraction",0x11,{{"positionUm","1.5"}});
    row("trailing-newline",0x11,{{"positionUm","12\n"}});
    row("nonnumeric",0x11,{{"positionUm","abc"}});
    row("hex-string",0x11,{{"positionUm","0x20"}});
    row("bool",0x11,{{"positionUm",true}});
    row("null",0x11,{{"positionUm",QVariant()}});
    row("nan",0x11,{{"positionUm",std::numeric_limits<double>::quiet_NaN()}});
    row("infinity",0x11,{{"positionUm",std::numeric_limits<double>::infinity()}});
    row("unsigned-overflow",0x11,{{"positionUm",QVariant::fromValue(std::numeric_limits<qulonglong>::max())}});
    row("24-overflow",0x11,{{"positionUm",0x1000000}});
    row("negative-unsigned",0x11,{{"positionUm",-1}});
    row("32-overflow",0x22,{{"duration",QVariant::fromValue(4294967296LL)}});
    row("mode-enum",0x11,{{"positionUm",0},{"mode",3}});
    row("action-enum",0x11,{{"positionUm",0},{"action",5}});
    row("valve-mode",0x21,{{"mode",3}});
    row("valve-mask-width",0x21,{{"valveMask",0x40000000}});
    row("do-mode",0x91,{{"mode",3}});
    row("do-mask",0x91,{{"outputMask",64}});
    row("dc-gap-channel",0xa1,{{"devId",0x27}});
    row("dc-pwm-low",0xa1,{{"pwm",-129}});
    row("dc-pwm-high",0xa1,{{"pwm",128}});
    row("fan-percent",0x81,{{"percent",101}});
    row("fan-mode",0x84,{{"speedMode",2}});
    row("fan-pulses",0x84,{{"pulsesPerRevolution",0}});
    row("fan-threshold-order",0x84,{{"minOutput",10},{"maxOutput",9}});
    row("power-channel",0x65,{{"devId",0xc8}});
    row("enable-bool",0x65,{{"enabled",2}});
    row("urgent-invalid-channel",0x05,{{"devId",0x10}});
    row("enable-reserved-bit",0x01,{{"enableMask",1}});
}

void SdkRequestTest::rejectedParameters()
{
    QFETCH(int, command); QFETCH(QVariantMap, parameters);
    QushengSdkRequest request; request.payload = "stale"; request.parameters.insert("stale",1);
    TranslationError error;
    QVERIFY(!QushengSdk::buildRequest(quint8(command), parameters, {}, {}, &request, &error));
    QVERIFY(error.isValid()); QVERIFY(!error.message.isEmpty());
    QVERIFY(request.payload.isEmpty()); QVERIFY(request.parameters.isEmpty()); QVERIFY(request.parameterSources.isEmpty());
}

void SdkRequestTest::integerBoundaries()
{
    QushengSdkRequest request; TranslationError error;
    QVERIFY(QushengSdk::buildRequest(0x11, {{"positionUm",0xffffff},{"speedUmPerS",0xffffff},{"mode",2},{"action",3}}, {}, {}, &request, &error));
    QCOMPARE(request.payload,QByteArray::fromHex("1123FFFFFFFFFFFF"));
    QVERIFY(QushengSdk::buildRequest(0x21, {{"mode",2},{"valveMask",0x3fffffff},{"duration",4294967295.0}}, {}, {}, &request, &error));
    QCOMPARE(request.payload,QByteArray::fromHex("BFFFFFFFFFFFFFFF"));
    QVERIFY(QushengSdk::buildRequest(0xa1, {{"pwm",127}}, {}, {}, &request, &error));
    QCOMPARE(request.payload,QByteArray::fromHex("217F"));
    QVERIFY(QushengSdk::buildRequest(0x11, {{"positionUm","123"}}, {}, {}, &request, &error));
    QCOMPARE(request.parameters.value("positionUm").type(), QVariant::LongLong);
    QCOMPARE(request.payload,QByteArray::fromHex("11410003E800007B"));
}

void SdkRequestTest::deferredInterfaces()
{
    QushengSdkRequest request; TranslationError error;
    for (auto code : QushengSdk::commandCodes()) {
        if (QushengSdk::isSupported(code)) continue;
        request.payload = "stale";
        QVERIFY(!QushengSdk::buildRequest(code, {}, {}, {}, &request, &error));
        QVERIFY(request.payload.isEmpty());
        QByteArray bytes = "stale";
        QVERIFY(!QushengSdk::encode(code, {}, &bytes, &error)); QVERIFY(bytes.isEmpty());
        switch (QushengSdk::definition(code).support) {
        case QushengSdkSupport::ReceiveOnly: QCOMPARE(error.code,QStringLiteral("SDK_RECEIVE_ONLY")); break;
        case QushengSdkSupport::LayoutPending: QCOMPARE(error.code,QStringLiteral("SDK_LAYOUT_PENDING")); QVERIFY(error.message.contains(QString::fromUtf8(u8"布局待确认"))); break;
        case QushengSdkSupport::Deferred: QCOMPARE(error.code,QStringLiteral("SDK_DEFERRED")); break;
        default: QFAIL("Unexpected support state");
        }
    }
    QVERIFY(!QushengSdk::buildRequest(0xff, {}, {}, {}, &request, &error)); QCOMPARE(error.code,QStringLiteral("SDK_UNKNOWN_COMMAND"));
    QCOMPARE(QushengSdk::definition(0x72).parameters.size(),2);
    QCOMPARE(QushengSdk::definition(0xc1).parameters.size(),4);
}

void SdkRequestTest::resolvedSnapshotsAndStrictEncode()
{
    QushengSdkRequest request; TranslationError error;
    QVariantMap device = {{"devId",0x11},{"speedUmPerS",1000}};
    QVERIFY(QushengSdk::buildRequest(0x11, {{"positionUm",100}}, {}, device, &request, &error));
    device.insert("speedUmPerS",9999);
    QByteArray bytes;
    QVERIFY(QushengSdk::encode(0x11,request.parameters,&bytes,&error)); QCOMPARE(bytes,request.payload);
    bytes = "stale";
    QVERIFY(!QushengSdk::encode(0x11,{{"positionUm",100}},&bytes,&error)); QVERIFY(bytes.isEmpty());
    QVERIFY(!QushengSdk::buildRequest(0x44, {}, {{"unknown",1}}, {}, &request,&error)); QCOMPARE(error.code,QStringLiteral("SDK_UNKNOWN_PARAMETER"));
    QVERIFY(!QushengSdk::buildRequest(0x44, {}, {{"devId",0x11}}, {}, &request,&error));
    QVERIFY(!QushengSdk::buildRequest(0x03, {}, {}, {}, nullptr, &error)); QCOMPARE(error.code,QStringLiteral("SDK_OUTPUT_REQUIRED"));
    QVERIFY(!QushengSdk::encode(0x03, {}, nullptr, &error));
}

QTEST_APPLESS_MAIN(SdkRequestTest)
#include "tst_sdk_requests.moc"
