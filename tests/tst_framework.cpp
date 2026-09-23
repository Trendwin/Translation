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
    void badCrcIsRejected();
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
    QCOMPARE(command.parameters.value("position").toLongLong(), 2000LL);
    QByteArray frame;
    QVERIFY(protocol.encodeCommand(command, &frame, &error));
    QCOMPARE(frame.left(6).toHex(), QByteArray("020111110001"));
    QCOMPARE(frame.mid(6, 4).toHex(), QByteArray("e8030000"));
    QCOMPARE(frame.mid(10, 4).toHex(), QByteArray("d0070000"));
    const quint16 wireCrc = quint8(frame.at(14)) | (quint16(quint8(frame.at(15))) << 8);
    QCOMPARE(wireCrc, QushengProtocol::crc16(frame.left(14)));
}

void FrameworkTest::splitQushengSuccessReplyToDtBytes()
{
    QushengProtocol protocol;
    DtClientProtocol client;
    UnifiedCommand command; command.requestId = 7; command.operation = "ABSOLUTE_MOVE";
    command.parameters.insert("axis", 1);
    const QByteArray wire = QushengProtocol::makeReply(0x01, 0x02, 0x11, 0x11, 0);
    QList<TranslationError> errors;
    QCOMPARE(protocol.feedReceivedData(wire.left(3), &errors).size(), 0);
    const QList<ProtocolMessage> messages = protocol.feedReceivedData(wire.mid(3), &errors);
    QCOMPARE(errors.size(), 0);
    QCOMPARE(messages.size(), 1);
    QVERIFY(protocol.matchesReply(command, messages.first()));
    UnifiedResult result;
    TranslationError error;
    QVERIFY(protocol.decodeReply(command, messages.first(), &result, &error));
    QByteArray reply;
    QVERIFY(client.buildReplyBytes(result, &reply, &error));
    QCOMPARE(reply, QByteArray::fromHex("2f3140030d0a"));
}

void FrameworkTest::badCrcIsRejected()
{
    QushengProtocol protocol;
    QByteArray wire = QushengProtocol::makeReply(1, 2, 0x11, 0x11, 0);
    wire[6] ^= 1;
    QList<TranslationError> errors;
    QCOMPARE(protocol.feedReceivedData(wire, &errors).size(), 0);
    QCOMPARE(errors.first().code, QString("QUSHENG_CRC"));
}

QTEST_APPLESS_MAIN(FrameworkTest)
#include "tst_framework.moc"
