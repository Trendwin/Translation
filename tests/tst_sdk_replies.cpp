#include <QtTest>
#include "qusheng/sdk/qushengsdk.h"

class SdkRepliesTest : public QObject {
    Q_OBJECT
private slots:
    void sixMotorReports();
    void frameSample();
    void frameLimits();
    void replyLayouts_data();
    void replyLayouts();
    void shortErrors_data();
    void shortErrors();
    void deviceInfo();
    void motorAndValve();
    void motorReservedBytes();
    void hardwareAndSignedRuntime();
    void scalarQueries();
    void interfaceVariants();
    void statusRecords();
    void reports();
    void deferredAndReset();
};

void SdkRepliesTest::frameSample()
{
    // 来自已验证协议样例的完整字节；不是SDK自编码再自解码的循环证明。
    const QByteArray listed = QByteArray::fromHex("aaaa020108f7001111410003e8004e2027f8");
    QCOMPARE(QushengSdk::makeFrame(2, 1, 0, 0x11, QByteArray::fromHex("11410003e8004e20")), listed);
    QCOMPARE(QushengSdk::crc16(listed.left(listed.size() - 2)), quint16(0x27f8));
    QCOMPARE(QushengSdk::crc16(QByteArray("123456789")), quint16(0x29b1));
}

void SdkRepliesTest::sixMotorReports()
{
    QushengSdkReply reply; TranslationError error;
    for (int id : {0x11, 0x16, 0x17, 0x1c}) {
        QByteArray position = QByteArray::fromHex("11100004d2ffffff"); position[0] = char(id);
        QByteArray fault = QByteArray::fromHex("11010203"); fault[0] = char(id);
        const bool valid = id <= 0x16;
        QCOMPARE(QushengSdk::decodeReport(0x17, position, &reply, &error), valid);
        if (!valid) { QCOMPARE(error.code, QString("SDK_REPORT_DEVICE")); QVERIFY(reply.values.isEmpty()); }
        QCOMPARE(QushengSdk::decodeReport(0x09, fault, &reply, &error), valid);
        if (!valid) { QCOMPARE(error.code, QString("SDK_REPORT_DEVICE")); QVERIFY(reply.values.isEmpty()); }
    }
    // Other device families still have their own ranges.
    QVERIFY(QushengSdk::decodeReport(0x09, QByteArray::fromHex("e0010203"), &reply, &error));
}

void SdkRepliesTest::frameLimits()
{
    const QByteArray empty = QushengSdk::makeFrame(2, 1, 255, 0x03, QByteArray());
    QCOMPARE(empty.size(), 10); QCOMPARE(empty.mid(0, 8), QByteArray::fromHex("aaaa020100ffff03"));
    const QByteArray maximum = QushengSdk::makeFrame(2, 1, 7, 0xd1, QByteArray(255, char(0x80)));
    QCOMPARE(maximum.size(), 265); QCOMPARE(quint8(maximum.at(4)), quint8(255));
    QCOMPARE(quint8(maximum.at(5)), quint8(0));
    QVERIFY(QushengSdk::makeFrame(2, 1, 0, 0xd1, QByteArray(256, 'a')).isEmpty());
}

void SdkRepliesTest::replyLayouts_data()
{
    QTest::addColumn<int>("command"); QTest::addColumn<QByteArray>("payload");
    const struct { int code; int length; } layouts[] = {
        {0x01,4},{0x03,4},{0x04,28},{0x05,4},{0x08,4},{0x09,4},{0x0c,10},
        {0x11,4},{0x12,11},{0x14,36},{0x16,60},{0x1a,4},
        {0x21,4},{0x22,4},{0x23,8},{0x24,4},{0x25,16},
        {0x44,7},{0x46,4},{0x54,4},{0x63,4},{0x64,20},{0x65,4},
        {0x81,4},{0x82,13},{0x83,5},{0x84,4},{0x91,4},{0x92,7},{0xa1,4},{0xc1,4}
    };
    for (const auto &layout : layouts)
        QTest::newRow(qPrintable(QString::number(layout.code, 16))) << layout.code << QByteArray(layout.length, '\0');
    QTest::newRow("04-optional-board") << 0x04 << QByteArray(29, '\0');
    QTest::newRow("12-reserved-extension") << 0x12 << QByteArray(14, '\0');
    QTest::newRow("84-raw-extension") << 0x84 << QByteArray::fromHex("00010203010203040506");
}

void SdkRepliesTest::replyLayouts()
{
    QFETCH(int, command); QFETCH(QByteArray, payload);
    QushengSdkReply reply; TranslationError error;
    QVERIFY2(QushengSdk::decodeReply(quint8(command), payload, &reply, &error), qPrintable(error.message));
    QVERIFY(reply.success); QVERIFY(!error.isValid()); QCOMPARE(reply.rawPayload, payload);
    QCOMPARE(reply.basicStatus.size(), command == 0x21 ? 2 : 3);
    // 每族检查短一字节和长一字节。04的28/29两种版本须分别接受，不能将版本兼容误当错误。
    for (int length : {payload.size() - 1, payload.size() + 1}) {
        if (command == 0x04 && (length == 28 || length == 29)) continue;
        QVERIFY(!QushengSdk::decodeReply(quint8(command), QByteArray(length, '\0'), &reply, &error));
        QCOMPARE(error.code, QStringLiteral("SDK_REPLY_LENGTH"));
        QVERIFY(!reply.success); QVERIFY(reply.values.isEmpty()); QCOMPARE(reply.rawPayload.size(), length);
    }
}

void SdkRepliesTest::shortErrors_data()
{
    QTest::addColumn<int>("command");
    const int commands[] = {0x01,0x03,0x04,0x05,0x08,0x09,0x0c,0x11,0x12,0x14,0x16,0x1a,
        0x21,0x22,0x23,0x24,0x25,0x44,0x46,0x54,0x63,0x64,0x65,0x81,0x82,0x83,0x84,0x91,0x92,0xa1};
    for (int command : commands) QTest::newRow(qPrintable(QString::number(command, 16))) << command;
}

void SdkRepliesTest::shortErrors()
{
    QFETCH(int, command);
    const QByteArray rejected = command == 0x21 ? QByteArray::fromHex("01234567") : QByteArray::fromHex("07abcdef");
    QushengSdkReply reply; TranslationError error;
    QVERIFY(QushengSdk::decodeReply(quint8(command), rejected, &reply, &error));
    QVERIFY(!reply.success); QCOMPARE(reply.errorCode, command == 0x21 ? quint32(0x123) : quint32(7));
    QCOMPARE(reply.basicStatus, command == 0x21 ? QByteArray::fromHex("4567") : QByteArray::fromHex("abcdef"));
    QVERIFY(reply.values.isEmpty()); QVERIFY(!error.isValid()); QCOMPARE(reply.rawPayload, rejected);
    QVERIFY(!QushengSdk::decodeReply(quint8(command), rejected.left(3), &reply, &error));
    QCOMPARE(error.code, QStringLiteral("SDK_REPLY_LENGTH"));
}

void SdkRepliesTest::deviceInfo()
{
    const QByteArray payload = QByteArray::fromHex("00112233010203040405000607080009000102030405060708090a0b03");
    QushengSdkReply reply; TranslationError error;
    QVERIFY(QushengSdk::decodeReply(0x04, payload, &reply, &error));
    QCOMPARE(reply.values.value("hardwareVersion").toString(), QStringLiteral("1.2.772"));
    QCOMPARE(reply.values.value("iapVersion").toString(), QStringLiteral("4.5.6"));
    QCOMPARE(reply.values.value("firmwareVersion").toString(), QStringLiteral("7.8.9"));
    QCOMPARE(reply.values.value("deviceIdentifier").toString(), QStringLiteral("00 01 02 03 04 05 06 07 08 09 0A 0B"));
    QCOMPARE(reply.values.value("boardType").toInt(), 3);
}

void SdkRepliesTest::motorAndValve()
{
    QushengSdkReply reply; TranslationError error;
    QVERIFY(QushengSdk::decodeReply(0x12, QByteArray::fromHex("00010203300004d2ffffff"), &reply, &error));
    QCOMPARE(reply.values.value("motorState").toInt(), 3);
    QCOMPARE(reply.values.value("positionUm").toUInt(), quint32(1234));
    QCOMPARE(reply.values.value("feedbackPositionUm").toUInt(), quint32(0xffffff));
    QVERIFY(!QushengSdk::decodeReply(0x12, QByteArray::fromHex("00010203500004d2ffffff"), &reply, &error));
    QCOMPARE(error.code, QStringLiteral("SDK_MOTOR_STATE")); QVERIFY(!reply.success);
    QVERIFY(QushengSdk::decodeReply(0x23, QByteArray::fromHex("0001020380000007"), &reply, &error));
    QCOMPARE(reply.values.value("valveMask").toUInt(), quint32(0x20000001));
    QVERIFY(QushengSdk::decodeReply(0x21, QByteArray::fromHex("0000aabb"), &reply, &error));
    QCOMPARE(reply.basicStatus, QByteArray::fromHex("aabb"));
}

void SdkRepliesTest::motorReservedBytes()
{
    const QByteArray fields=QByteArray::fromHex("00010203300004d2ffffff");
    QushengSdkReply base,reply;TranslationError error;
    QVERIFY(QushengSdk::decodeReply(0x12,fields,&base,&error));
    for(const QByteArray &reserved:{QByteArray(3,'\0'),QByteArray::fromHex("a1b2c3")}) {
        const QByteArray payload=fields+reserved;
        QVERIFY(QushengSdk::decodeReply(0x12,payload,&reply,&error));
        QVERIFY(reply.success);QVERIFY(!error.isValid());QCOMPARE(reply.values,base.values);
        QCOMPARE(reply.basicStatus,base.basicStatus);QCOMPARE(reply.rawPayload,payload);
    }
    // 用户实机截图：状态1、位置3000、负反馈0，末尾三个字节为保留位。
    const QByteArray captured=QByteArray::fromHex("0000000010000bb8000000000000");
    QVERIFY(QushengSdk::decodeReply(0x12,captured,&reply,&error));
    QCOMPARE(reply.values.value("motorState").toInt(),1);QCOMPARE(reply.values.value("positionUm").toUInt(),3000u);
    QCOMPARE(reply.values.value("feedbackPositionUm").toUInt(),0u);QCOMPARE(reply.values.size(),3);
    for(int length:{10,12,13,15}) {
        QVERIFY(!QushengSdk::decodeReply(0x12,QByteArray(length,'\0'),&reply,&error));
        QCOMPARE(error.code,QStringLiteral("SDK_REPLY_LENGTH"));QVERIFY(reply.values.isEmpty());
    }
    QByteArray invalid=captured;invalid[4]=char(0x50);
    QVERIFY(!QushengSdk::decodeReply(0x12,invalid,&reply,&error));QCOMPARE(error.code,QStringLiteral("SDK_MOTOR_STATE"));
    QByteArray rejected=captured;rejected[0]=char(7);
    QVERIFY(QushengSdk::decodeReply(0x12,rejected,&reply,&error));QVERIFY(!reply.success);
    QCOMPARE(reply.errorCode,quint32(7));QVERIFY(reply.values.isEmpty());
}

void SdkRepliesTest::hardwareAndSignedRuntime()
{
    QushengSdkReply reply; TranslationError error;
    const QByteArray hardware = QByteArray::fromHex("0001020301ff0100ffff800089abcdef12345678ffffffff010203040000000180000000");
    QVERIFY2(QushengSdk::decodeReply(0x14, hardware, &reply, &error), qPrintable(error.message));
    QCOMPARE(reply.values.value("hardwareFlags").toUInt(), quint32(0x1ff));
    QCOMPARE(reply.values.value("microsteps").toUInt(), quint32(256));
    QCOMPARE(reply.values.value("staticCurrent").toUInt(), quint32(65535));
    QCOMPARE(reply.values.value("runningCurrent").toUInt(), quint32(32768));
    QCOMPARE(reply.values.value("lead").toUInt(), quint32(0x89abcdef));
    QCOMPARE(reply.values.value("encoderRatio").toUInt(), quint32(0xffffffff));
    QCOMPARE(reply.values.value("maxPosition").toUInt(), quint32(0x80000000));
    const QByteArray run = QByteArray::fromHex("0011223380000000ffffffff7fffffff00000001") + QByteArray(40, '\0');
    QVERIFY(QushengSdk::decodeReply(0x16, run, &reply, &error));
    QCOMPARE(reply.values.value("homingLeaveDistance").toLongLong(), Q_INT64_C(-2147483648));
    QCOMPARE(reply.values.value("homingSearchDistance").toLongLong(), Q_INT64_C(-1));
    QCOMPARE(reply.values.value("mechanicalZeroDistance").toLongLong(), Q_INT64_C(2147483647));
    QCOMPARE(reply.values.value("homingAccelTargetSpeed").toLongLong(), Q_INT64_C(1));
    QCOMPARE(reply.values.value("motionDeceleration1").toLongLong(), Q_INT64_C(0));
}

void SdkRepliesTest::scalarQueries()
{
    QushengSdkReply reply; TranslationError error;
    QVERIFY(QushengSdk::decodeReply(0x25, QByteArray::fromHex("0001020389abcdef00000001ffffffff"), &reply, &error));
    QCOMPARE(reply.values.value("group1Timeout").toUInt(), quint32(0x89abcdef));
    QCOMPARE(reply.values.value("group3Timeout").toUInt(), quint32(0xffffffff));
    QVERIFY(QushengSdk::decodeReply(0x44, QByteArray::fromHex("00010203800102"), &reply, &error));
    QCOMPARE(reply.values.value("voltageMv").toUInt(), quint32(0x800102));
    QVERIFY(QushengSdk::decodeReply(0x64, QByteArray::fromHex("000102030000000189abcdef80000000ffffffff"), &reply, &error));
    QCOMPARE(reply.values.value("peripheralDelay").toUInt(), quint32(1));
    QCOMPARE(reply.values.value("power24Delay").toUInt(), quint32(0x89abcdef));
    QCOMPARE(reply.values.value("power5Delay").toUInt(), quint32(0xffffffff));
    QVERIFY(QushengSdk::decodeReply(0x82, QByteArray::fromHex("00010203010203040589abcdef"), &reply, &error));
    QCOMPARE(reply.values.value("speedMode").toInt(), 1);
    QCOMPARE(reply.values.value("maxOutput").toInt(), 5);
    QCOMPARE(reply.values.value("measurementInterval").toUInt(), quint32(0x89abcdef));
    QVERIFY(QushengSdk::decodeReply(0x83, QByteArray::fromHex("0001020364"), &reply, &error));
    QCOMPARE(reply.values.value("percent").toUInt(), quint32(100));
    QVERIFY(QushengSdk::decodeReply(0x84, QByteArray::fromHex("00010203010203040506"), &reply, &error));
    QCOMPARE(reply.values.value("configurationRaw").toByteArray(), QByteArray::fromHex("010203040506"));
    QVERIFY(!reply.values.contains("measurementInterval"));
    QVERIFY(QushengSdk::decodeReply(0x92, QByteArray::fromHex("00010203800001"), &reply, &error));
    QCOMPARE(reply.values.value("inputMask").toUInt(), quint32(0x800001));
}

void SdkRepliesTest::interfaceVariants()
{
    QushengSdkReply reply; TranslationError error;
    QVERIFY(QushengSdk::decodeReply(0x0c, QByteArray::fromHex("00010203011d0001c200"), &reply, &error));
    QCOMPARE(reply.values.value("interfaceType").toInt(), 1); QCOMPARE(reply.values.value("flowControl").toInt(), 3);
    QCOMPARE(reply.values.value("parity").toInt(), 2); QCOMPARE(reply.values.value("stopBits").toInt(), 2);
    QCOMPARE(reply.values.value("baudrate").toUInt(), quint32(115200));
    QVERIFY(QushengSdk::decodeReply(0x0c, QByteArray::fromHex("000102030213"), &reply, &error));
    QCOMPARE(reply.values.value("canBaudrateIndex").toInt(), 19);
    const QByteArray network = QByteArray::fromHex("0001020303c0a80102ffffff00c0a8010108080808c0a8010301bb");
    QVERIFY(QushengSdk::decodeReply(0x0c, network, &reply, &error));
    QCOMPARE(reply.values.value("ip").toString(), QStringLiteral("192.168.1.2"));
    QCOMPARE(reply.values.value("serverIp").toString(), QStringLiteral("192.168.1.3"));
    QCOMPARE(reply.values.value("serverPort").toUInt(), quint32(443));
    QVERIFY(!QushengSdk::decodeReply(0x0c, QByteArray::fromHex("0000000004"), &reply, &error));
    QCOMPARE(error.code, QStringLiteral("SDK_REPLY_LENGTH"));
}

void SdkRepliesTest::statusRecords()
{
    QushengSdkReply reply; TranslationError error;
    QVERIFY(QushengSdk::decodeReply(0x03, QByteArray::fromHex("001122331100000561abcdef"), &reply, &error));
    const QVariantList records = reply.values.value("errors").toList(); QCOMPARE(records.size(), 2);
    QCOMPARE(records.at(0).toMap().value("devId").toInt(), 0x11);
    QCOMPARE(records.at(1).toMap().value("faultCode").toUInt(), quint32(0xabcdef));
    QCOMPARE(records.at(1).toMap().value("raw").toByteArray(), QByteArray::fromHex("61abcdef"));
}

void SdkRepliesTest::reports()
{
    QushengSdkReply reply; TranslationError error;
    QVERIFY(QushengSdk::decodeReport(0x08, QByteArray(), &reply, &error));
    QVERIFY(reply.success); QVERIFY(reply.basicStatus.isEmpty()); QCOMPARE(reply.values.value("heartbeat").toBool(), true);
    QVERIFY(!QushengSdk::decodeReport(0x08, QByteArray(4, '\0'), &reply, &error));
    QVERIFY(!QushengSdk::decodeReport(0x09, QByteArray::fromHex("00000000"), &reply, &error));
    QCOMPARE(error.code, QStringLiteral("SDK_REPORT_DEVICE"));
    QVERIFY(QushengSdk::decodeReport(0x09, QByteArray::fromHex("1101020361800000"), &reply, &error));
    QVERIFY(reply.basicStatus.isEmpty()); QCOMPARE(reply.errorCode, quint32(0));
    QCOMPARE(reply.values.value("errors").toList().at(0).toMap().value("faultCode").toUInt(), quint32(0x010203));
    QVERIFY(!QushengSdk::decodeReport(0x09, QByteArray(5, '\0'), &reply, &error));
    QVERIFY(!QushengSdk::decodeReport(0x09, QByteArray(), &reply, &error));
    QVERIFY(QushengSdk::decodeReport(0x17, QByteArray::fromHex("16100004d2ffffff"), &reply, &error));
    QCOMPARE(reply.values.value("devId").toInt(), 0x16); QCOMPARE(reply.values.value("motorState").toInt(), 1);
    QCOMPARE(reply.values.value("positionUm").toUInt(), quint32(1234));
    QCOMPARE(reply.values.value("feedbackPositionUm").toUInt(), quint32(0xffffff)); QVERIFY(reply.basicStatus.isEmpty());
    QVERIFY(!QushengSdk::decodeReport(0x17, QByteArray::fromHex("21100004d2ffffff"), &reply, &error));
    QCOMPARE(error.code, QStringLiteral("SDK_REPORT_DEVICE"));
    QVERIFY(!QushengSdk::decodeReport(0x17, QByteArray::fromHex("11500004d2ffffff"), &reply, &error));
    QCOMPARE(error.code, QStringLiteral("SDK_MOTOR_STATE"));
    QVERIFY(!QushengSdk::decodeReport(0x17, QByteArray(9, '\0'), &reply, &error));
    QVERIFY(QushengSdk::decodeReport(0x45, QByteArray::fromHex("68010203"), &reply, &error));
    QCOMPARE(reply.values.value("voltageMv").toUInt(), quint32(0x010203));
    QVERIFY(!QushengSdk::decodeReport(0x45, QByteArray::fromHex("69010203"), &reply, &error));
    QVERIFY(!QushengSdk::decodeReport(0x45, QByteArray(5, '\0'), &reply, &error));
    QVERIFY(!QushengSdk::decodeReport(0x11, QByteArray(4, '\0'), &reply, &error));
    QCOMPARE(error.code, QStringLiteral("SDK_NOT_REPORT"));
}

void SdkRepliesTest::deferredAndReset()
{
    QushengSdkReply reply; TranslationError error;
    QVERIFY(QushengSdk::decodeReply(0x11, QByteArray(4, '\0'), &reply, &error));
    const QByteArray raw = QByteArray::fromHex("abcdef");
    QVERIFY(!QushengSdk::decodeReply(0x72, raw, &reply, &error)); QCOMPARE(error.code, QStringLiteral("SDK_LAYOUT_PENDING"));
    QCOMPARE(reply.rawPayload, raw); QVERIFY(!reply.success); QVERIFY(reply.values.isEmpty()); QVERIFY(reply.basicStatus.isEmpty());
    QVERIFY(!QushengSdk::decodeReply(0xc1, raw, &reply, &error)); QCOMPARE(error.code, QStringLiteral("SDK_REPLY_LENGTH"));
    QVERIFY(!QushengSdk::decodeReply(0xd1, raw, &reply, &error)); QCOMPARE(error.code, QStringLiteral("SDK_DEFERRED"));
    QVERIFY(!QushengSdk::decodeReply(0x17, raw, &reply, &error)); QCOMPARE(error.code, QStringLiteral("SDK_RECEIVE_ONLY"));
    QVERIFY(!QushengSdk::decodeReply(0x02, raw, &reply, &error)); QCOMPARE(error.code, QStringLiteral("SDK_UNKNOWN_COMMAND"));
    QVERIFY(!QushengSdk::decodeReport(0xd9, raw, &reply, &error)); QCOMPARE(error.code, QStringLiteral("SDK_DEFERRED"));
    QVERIFY(!QushengSdk::decodeReport(0xe6, raw, &reply, &error)); QCOMPARE(reply.rawPayload, raw);
    QVERIFY(!QushengSdk::decodeReply(0x11, raw, nullptr, &error)); QCOMPARE(error.code, QStringLiteral("SDK_OUTPUT"));
    QVERIFY(!QushengSdk::decodeReply(0x03, QByteArray(256, '\0'), &reply, &error));
    QCOMPARE(error.code, QStringLiteral("SDK_PAYLOAD_LENGTH")); QCOMPARE(reply.rawPayload.size(), 256);
    QVERIFY(!QushengSdk::decodeReport(0x09, QByteArray(256, '\0'), &reply, &error));
    QCOMPARE(error.code, QStringLiteral("SDK_PAYLOAD_LENGTH")); QVERIFY(!reply.success);
}

QTEST_APPLESS_MAIN(SdkRepliesTest)
#include "tst_sdk_replies.moc"
