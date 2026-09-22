#include <QtTest>
#include "demo/demointernalprotocol.h"

class FrameworkTest : public QObject
{
    Q_OBJECT
private slots:
    void splitAndStickyFrames();
    void unrelatedDoesNotMatch();
    void overflowIsReported();
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

QTEST_APPLESS_MAIN(FrameworkTest)
#include "tst_framework.moc"
