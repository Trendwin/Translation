#include <QtTest>
#include "demo/demointernalprotocol.h"
#include "dt/dtclientprotocol.h"
#include "qusheng/qushengprotocol.h"

class FrameworkTest : public QObject
{
    Q_OBJECT
private slots:
    void splitAndStickyFrames();
    void unrelatedDoesNotMatch();
    void overflowIsReported();
    void dtACommandToQushengFrame();
    void splitQushengSuccessReplyToDtBytes();
    void stickyFramesAndNoiseAreResynchronised();
    void unrelatedSequenceDoesNotMatch();
    void badCrcIsRejected();
    void deviceErrorDoesNotBuildSuccessReply();
    void sequenceIncrementsAndWraps();
    void distanceRangeIsValidated();
};

void FrameworkTest::splitAndStickyFrames()
{
    DemoInternalProtocol codec;
    const QByteArray first = DemoInternalProtocol::makeFrame(0x81, "PING|motor1|OK|PONG");
    const QByteArray second = DemoInternalProtocol::makeFrame(0x90, "REPORT");
    QList<TranslationError> errors;
    QCOMPARE(codec.feedReceivedData(first.left(5), &errors).size(), 0);
    const QList<ProtocolMessage> messages = codec.feedReceivedData(first.mid(5) + second, &errors);
    QCOMPARE(errors.size(), 0);
    QCOMPARE(messages.size(), 2);
    QCOMPARE(messages.at(0).messageType, 0x81);
    QCOMPARE(messages.at(1).messageType, 0x90);
}

void FrameworkTest::unrelatedDoesNotMatch()
{
    DemoInternalProtocol codec;
    UnifiedCommand command; command.operation = "PING"; command.targetDevice = "motor1";
    ProtocolMessage report; report.messageType = 0x90; report.payload = "REPORT";
    ProtocolMessage other; other.messageType = 0x81; other.payload = "PING|motor2|OK|PONG";
    QVERIFY(!codec.matchesReply(command, report));
    QVERIFY(!codec.matchesReply(command, other));
}

void FrameworkTest::overflowIsReported()
{
    DemoInternalProtocol codec;
    QList<TranslationError> errors;
    codec.feedReceivedData(QByteArray(4097, 'x'), &errors);
    QCOMPARE(errors.size(), 1);
    QCOMPARE(errors.first().code, QString("RX_OVERFLOW"));
}

void FrameworkTest::dtACommandToQushengFrame()
{
    DtClientProtocol client;
    QushengProtocol protocol;
    UnifiedCommand command;
    TranslationError error;
    QVERIFY(client.parseCommand("/1A2000", &command, &error));
    QCOMPARE(command.parameters.value("axis").toUInt(), 1u);
    QCOMPARE(command.parameters.value("speed").toUInt(), 1000u);
    QCOMPARE(command.operation, QString("FORWARD_POSITION_MOVE"));
    QCOMPARE(command.parameters.value("distance").toLongLong(), 2000LL);
    QByteArray frame;
    QVERIFY(protocol.encodeCommand(command, &frame, &error));
    // Protocol acceptance vector, not assembled by the implementation under test.
    // Command 0x11 uses fixed-width, big-endian uint24 values:
    // speed 1000 = 00 03 E8, distance 2000 = 00 07 D0.
    QCOMPARE(frame, QByteArray::fromHex("aaaa020108f7001111210003e80007d0cca9"));
}

void FrameworkTest::splitQushengSuccessReplyToDtBytes()
{
    QushengProtocol protocol;
    DtClientProtocol client;
    UnifiedCommand command; command.requestId = 7; command.operation = "FORWARD_POSITION_MOVE";
    command.parameters.insert("axis", 1);
    command.parameters.insert("speed", 1000);
    command.parameters.insert("distance", 2000);
    QByteArray request;
    TranslationError error;
    QVERIFY(protocol.encodeCommand(command, &request, &error));
    // Independent successful reply vector: D0=ERR_CODE, D1..D3=basic status.
    const QByteArray wire = QByteArray::fromHex("aaaa010204fb00110000000010a4");
    QList<TranslationError> errors;
    QCOMPARE(protocol.feedReceivedData(wire.left(3), &errors).size(), 0);
    const QList<ProtocolMessage> messages = protocol.feedReceivedData(wire.mid(3), &errors);
    QCOMPARE(errors.size(), 0);
    QCOMPARE(messages.size(), 1);
    QVERIFY(protocol.matchesReply(command, messages.first()));
    UnifiedResult result;
    QVERIFY(protocol.decodeReply(command, messages.first(), &result, &error));
    QByteArray reply;
    QVERIFY(client.buildReplyBytes(result, &reply, &error));
    QCOMPARE(reply, QByteArray::fromHex("2f3140030d0a"));
}

void FrameworkTest::stickyFramesAndNoiseAreResynchronised()
{
    QushengProtocol protocol;
    const QByteArray first = QByteArray::fromHex("aaaa010204fb00110000000010a4");
    const QByteArray second = QushengProtocol::makeReply(1, 0, QByteArray::fromHex("010203"));
    QList<TranslationError> errors;
    QCOMPARE(protocol.feedReceivedData(QByteArray::fromHex("9988") + first.left(1), &errors).size(), 0);
    QCOMPARE(protocol.feedReceivedData(first.mid(1, 4), &errors).size(), 0);
    const QList<ProtocolMessage> messages = protocol.feedReceivedData(first.mid(5) + second, &errors);
    QCOMPARE(errors.size(), 0);
    QCOMPARE(messages.size(), 2);
    QCOMPARE(messages.at(0).payload, QByteArray::fromHex("00000000"));
    QCOMPARE(messages.at(1).payload, QByteArray::fromHex("00010203"));
}

void FrameworkTest::unrelatedSequenceDoesNotMatch()
{
    DtClientProtocol client;
    QushengProtocol protocol;
    UnifiedCommand command;
    TranslationError error;
    QVERIFY(client.parseCommand("/1A2000", &command, &error));
    command.requestId = 42;
    QByteArray request;
    QVERIFY(protocol.encodeCommand(command, &request, &error));
    const QList<ProtocolMessage> messages = protocol.feedReceivedData(
                QushengProtocol::makeReply(1, 0), nullptr);
    QCOMPARE(messages.size(), 1);
    QVERIFY(!protocol.matchesReply(command, messages.first()));
}

void FrameworkTest::badCrcIsRejected()
{
    QushengProtocol protocol;
    QByteArray wire = QByteArray::fromHex("aaaa010204fb00110000000010a4");
    wire[13] ^= 1;
    QList<TranslationError> errors;
    QCOMPARE(protocol.feedReceivedData(wire, &errors).size(), 0);
    QCOMPARE(errors.first().code, QString("QUSHENG_CRC"));
}

void FrameworkTest::deviceErrorDoesNotBuildSuccessReply()
{
    DtClientProtocol client;
    QushengProtocol protocol;
    UnifiedCommand command;
    TranslationError error;
    QVERIFY(client.parseCommand("/1A2000", &command, &error));
    command.requestId = 8;
    QByteArray request;
    QVERIFY(protocol.encodeCommand(command, &request, &error));
    const QList<ProtocolMessage> messages = protocol.feedReceivedData(
                QushengProtocol::makeReply(0, 5), nullptr);
    QCOMPARE(messages.size(), 1);
    UnifiedResult result;
    QVERIFY(protocol.decodeReply(command, messages.first(), &result, &error));
    QVERIFY(!result.success);
    QByteArray reply;
    QVERIFY(!client.buildReplyBytes(result, &reply, &error));
    QVERIFY(reply.isEmpty());
}

void FrameworkTest::sequenceIncrementsAndWraps()
{
    DtClientProtocol client;
    QushengProtocol protocol;
    UnifiedCommand command;
    TranslationError error;
    QVERIFY(client.parseCommand("/1A1", &command, &error));
    QByteArray frame;
    for (int sequence = 0; sequence <= 256; ++sequence) {
        command.requestId = quint64(sequence + 1);
        QVERIFY(protocol.encodeCommand(command, &frame, &error));
        QCOMPARE(quint8(frame.at(6)), quint8(sequence));
    }
}

void FrameworkTest::distanceRangeIsValidated()
{
    DtClientProtocol client;
    UnifiedCommand command;
    TranslationError error;
    QVERIFY(!client.parseCommand("/1A-1", &command, &error));
    QCOMPARE(error.code, QString("DT_RANGE"));
    QVERIFY(!client.parseCommand("/1A16777215", &command, &error));
    QCOMPARE(error.code, QString("DT_RANGE"));
    QVERIFY(client.parseCommand("/1A16777214", &command, &error));
}

QTEST_APPLESS_MAIN(FrameworkTest)
#include "tst_framework.moc"
