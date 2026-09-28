#include <QtTest>
#include <QFileInfo>
#include <QTemporaryDir>
#include "config/protocolconfig.h"
#include "core/conversionengine.h"
#include "core/requestmanager.h"
#include "dt/dtclientprotocol.h"
#include "demo/demointernalprotocol.h"
#include "qusheng/qushengprotocol.h"

class FakeTransport:public ITransport
{
public:
    explicit FakeTransport(QObject *parent=nullptr):ITransport(parent) {}
    bool isConnected() const override { return connected; }
    void sendBytes(quint64 id,const QByteArray &data) override {
        sent.append(data);
        if(!stall) QTimer::singleShot(0,this,[this,id]() { emit sendFinished(id,true,QString()); });
    }
    void receive(const QByteArray &data) { emit bytesReceived(data); }
    void disconnectNow() { connected=false; emit connectionChanged(false,"test disconnect"); }
    QList<QByteArray> sent;
    bool connected=true,stall=false;
};

class FrameworkTest:public QObject
{
    Q_OBJECT
private slots:
    void twoFormatsSameCommandAndRoundTrip();
    void demoFramingRegression();
    void binaryEndianCrcAndStream();
    void lengthFieldAndMappingExpressions();
    void invalidConfigAndBounds();
    void dtFormalSemantics();
    void codecReplyAndCrc();
    void sequenceExhaustionIsFailClosed();
    void managerAckIsNotCompletion();
    void managerStalledWriteAndRejection();
};

static QString example(const QString &name)
{ return QFileInfo(QString::fromLatin1(__FILE__)).dir().filePath("../examples/"+name); }

void FrameworkTest::demoFramingRegression()
{
    DemoInternalProtocol codec;
    const QByteArray first=DemoInternalProtocol::makeFrame(0x81,"PING|motor1|OK|PONG");
    const QByteArray second=DemoInternalProtocol::makeFrame(0x90,"REPORT");
    QList<TranslationError> errors;
    QCOMPARE(codec.feedReceivedData(first.left(5),&errors).size(),0);
    const auto messages=codec.feedReceivedData(first.mid(5)+second,&errors);
    QCOMPARE(messages.size(),2); QCOMPARE(messages.at(0).messageType,0x81);
    QCOMPARE(messages.at(1).messageType,0x90);
    ProtocolMessage unrelated; unrelated.messageType=0x90; unrelated.payload="REPORT";
    UnifiedCommand command; command.operation="PING"; command.targetDevice="motor1";
    QVERIFY(!codec.matchesReply(command,unrelated));
    codec.feedReceivedData(QByteArray(4097,'x'),&errors);
    QCOMPARE(errors.last().code,QString("RX_OVERFLOW"));
}

void FrameworkTest::twoFormatsSameCommandAndRoundTrip()
{
    ProtocolConfig ascii,binary; TranslationError error;
    QVERIFY2(ProtocolConfig::load(example("ascii-preview.json"),&ascii,&error),qPrintable(error.message));
    QVERIFY2(ProtocolConfig::load(example("binary-preview.json"),&binary,&error),qPrintable(error.message));
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const QString saved=dir.filePath("saved.json");
    QVERIFY(ascii.save(saved,&error)); ProtocolConfig loaded;
    QVERIFY(ProtocolConfig::load(saved,&loaded,&error)); QCOMPARE(loaded.version,ascii.version);
    ConversionEngine a(loaded),b(binary); ConversionPreview pa,pb;
    QVERIFY2(a.convert("!MOVE,2000\r",false,&pa,&error),qPrintable(error.message));
    QVERIFY2(b.convert(QByteArray::fromHex("aa5501d007e648"),false,&pb,&error),qPrintable(error.message));
    QCOMPARE(pa.command.operation,QString("ABSOLUTE_MOVE"));
    QCOMPARE(pa.command.parameters.value("positionUm").toLongLong(),20000LL);
    QCOMPARE(pa.command.parameters,pb.command.parameters);
    QCOMPARE(pa.targetFrame,pb.targetFrame);
    // Constructed CCITT-FALSE vector; real hardware CRC still needs independent confirmation.
    QCOMPARE(pa.targetFrame,QByteArray::fromHex("aaaa020108f7001111410003e8004e2027f8"));
    QVERIFY(!a.convert("!MOVE,2000\r",true,nullptr,&error));
    QCOMPARE(error.code,QString("DEVICE_UNCONFIRMED"));
}

void FrameworkTest::binaryEndianCrcAndStream()
{
    ProtocolConfig config; TranslationError error;
    QVERIFY(ProtocolConfig::load(example("binary-preview.json"),&config,&error));
    ConversionEngine engine(config); QList<TranslationError> errors;
    const QByteArray wire=QByteArray::fromHex("aa5501d007e648");
    QCOMPARE(engine.feed(QByteArray::fromHex("9999aa55")+wire.mid(2,2),&errors).size(),0);
    const auto frames=engine.feed(wire.mid(4)+wire,&errors);
    QCOMPARE(frames.size(),2); QCOMPARE(frames.at(0),wire); QCOMPARE(frames.at(1),wire);
    QCOMPARE(errors.size(),0);
    ConversionPreview out;
    QByteArray bad=wire; bad[6]=char(quint8(bad.at(6))^1);
    QVERIFY(!engine.convert(bad,false,&out,&error)); QCOMPARE(error.code,QString("BINARY_CRC"));
    bad=wire; bad[3]=char(0x07); bad[4]=char(0xd0); // endian swap changes value; CRC no longer valid
    QVERIFY(!engine.convert(bad,false,&out,&error));
    const auto recovered=engine.feed(bad+wire,&errors);
    QCOMPARE(recovered.size(),1); QCOMPARE(recovered.first(),wire);
    engine.feed(QByteArray(4097,'x'),&errors);
    QCOMPARE(errors.last().code,QString("UPSTREAM_OVERFLOW"));
}

void FrameworkTest::lengthFieldAndMappingExpressions()
{
    TranslationError error; ProtocolConfig fixed;
    QVERIFY(ProtocolConfig::load(example("binary-preview.json"),&fixed,&error));
    QJsonObject root=fixed.root,external=root.value("external").toObject();
    external.remove("fixedLength"); external.insert("headerHex","aa");
    external.insert("lengthField",QJsonObject{{"offset",1},{"size",1},{"endian","little"},{"base",0}});
    root.insert("external",external);
    ProtocolConfig dynamic;
    QVERIFY2(ProtocolConfig::fromJson(QJsonDocument(root).toJson(),&dynamic,&error),qPrintable(error.message));
    ConversionEngine engine(dynamic); QList<TranslationError> errors;
    const QByteArray wire=QByteArray::fromHex("aa0701d0077e1b");
    QCOMPARE(engine.feed(wire.left(3),&errors).size(),0);
    QCOMPARE(engine.feed(wire.mid(3)+wire,&errors).size(),2);
    ConversionPreview converted; QVERIFY(engine.convert(wire,false,&converted,&error));
    QCOMPARE(converted.command.parameters.value("positionUm").toLongLong(),20000LL);

    ProtocolConfig ascii; QVERIFY(ProtocolConfig::load(example("ascii-preview.json"),&ascii,&error));
    root=ascii.root; external=root.value("external").toObject();
    external.insert("template","!MOVE,{position},{mode}\r");
    QJsonArray fields=external.value("fields").toArray();
    fields.append(QJsonObject{{"name","mode"},{"type","uint"},{"min",0},{"max",1}});
    external.insert("fields",fields); root.insert("external",external);
    QJsonArray maps=root.value("mappings").toArray(); QJsonObject map=maps.at(0).toObject();
    QJsonObject params=map.value("parameters").toObject();
    params.insert("positionUm",QJsonObject{{"kind","linear"},{"field","position"},{"numerator",3},{"denominator",2},{"round","nearest"}});
    params.insert("enumValue",QJsonObject{{"kind","enum"},{"field","mode"},{"table",QJsonObject{{"0",11},{"1",22}}}});
    params.insert("flags",QJsonObject{{"kind","bits"},{"parts",QJsonArray{
        QJsonObject{{"shift",0},{"width",1},{"value",QJsonObject{{"kind","constant"},{"value",1}}}},
        QJsonObject{{"shift",1},{"width",12},{"value",QJsonObject{{"kind","field"},{"name","position"}}}}
    }}});
    map.insert("parameters",params); maps.replace(0,map); root.insert("mappings",maps);
    ProtocolConfig mapped; QVERIFY2(ProtocolConfig::fromJson(QJsonDocument(root).toJson(),&mapped,&error),qPrintable(error.message));
    ConversionEngine expr(mapped);
    QVERIFY(expr.convert("!MOVE,2001,1\r",false,&converted,&error));
    QCOMPARE(converted.command.parameters.value("positionUm").toLongLong(),3002LL);
    QCOMPARE(converted.command.parameters.value("enumValue").toInt(),22);
    QCOMPARE(converted.command.parameters.value("flags").toInt(),4003);
}

void FrameworkTest::invalidConfigAndBounds()
{
    ProtocolConfig config; TranslationError error;
    QVERIFY(ProtocolConfig::load(example("ascii-preview.json"),&config,&error));
    QJsonObject bad=config.root; bad.insert("schemaVersion",2);
    QVERIFY(!ProtocolConfig::fromJson(QJsonDocument(bad).toJson(),nullptr,&error));
    QCOMPARE(error.code,QString("CONFIG_SCHEMA"));
    ConversionEngine engine(config); ConversionPreview out;
    QVERIFY(!engine.convert("!MOVE,6001\r",false,&out,&error)); QCOMPARE(error.code,QString("FIELD_RANGE"));
    QVERIFY(!engine.convert("!MOVE,7000\r",false,&out,&error));
    QVERIFY(!engine.convert("!MOVE,2000\r!MOVE,1000\r",false,&out,&error));
    QJsonObject root=config.root;
    QJsonArray maps=root.value("mappings").toArray();
    QJsonObject more=maps.at(0).toObject(); more.insert("when",QJsonObject{{"position",2000}});
    maps.append(more); root.insert("mappings",maps);
    ProtocolConfig ambiguous; QVERIFY(ProtocolConfig::fromJson(QJsonDocument(root).toJson(),&ambiguous,&error));
    ConversionEngine ambiguousEngine(ambiguous);
    QVERIFY(!ambiguousEngine.convert("!MOVE,2000\r",false,&out,&error));
    QCOMPARE(error.code,QString("MAPPING_AMBIGUOUS"));
}

void FrameworkTest::dtFormalSemantics()
{
    ProtocolConfig config; TranslationError error;
    QVERIFY(ProtocolConfig::load(example("dt-preview.json"),&config,&error));
    ConversionEngine engine(config); ConversionPreview out;
    QVERIFY(engine.convert("/1A2000R\r",false,&out,&error));
    QCOMPARE(out.command.operation,QString("ABSOLUTE_MOVE"));
    QCOMPARE(out.command.responsePolicy,QString("ack"));
    QCOMPARE(out.targetFrame.at(9),char(0x41)); // absolute-position mode, not old relative 0x21
    QVERIFY(engine.convert("/1a2000R\r",false,&out,&error)); QCOMPARE(out.command.responsePolicy,QString("complete"));
    QVERIFY(engine.convert("/1Q\r",false,&out,&error)); QCOMPARE(out.command.operation,QString("QUERY_MOTOR"));
    for(const QByteArray bad:{QByteArray("/1A2000R"),QByteArray("/1A2000\r"),QByteArray("/1A1RP2R\r"),QByteArray("/0A1R\r"),QByteArray("/1P3R\r")}) {
        QVERIFY(!engine.convert(bad,false,&out,&error)); QCOMPARE(error.code,QString("DT_UNSUPPORTED"));
    }
    QCOMPARE(DtClientProtocol::statusReply(false),QByteArray::fromHex("2f3040030d0a"));
    QCOMPARE(DtClientProtocol::statusReply(true),QByteArray::fromHex("2f3060030d0a"));
}

void FrameworkTest::codecReplyAndCrc()
{
    QushengProtocol codec; codec.setAddresses(2,1);
    UnifiedCommand query; query.requestId=7; query.operation="QUERY_MOTOR"; query.targetDevice="17";
    QByteArray request; TranslationError error;
    QVERIFY(codec.encodeCommand(query,&request,&error)); QCOMPARE(quint8(request.at(7)),quint8(0x12));
    const QByteArray reply=QushengProtocol::makeFrame(1,2,0,0x12,QByteArray::fromHex("0000000010004e20ffffff"));
    QList<TranslationError> errors;
    QCOMPARE(codec.feedReceivedData(reply.left(4),&errors).size(),0);
    const auto messages=codec.feedReceivedData(reply.mid(4),&errors);
    QCOMPARE(messages.size(),1); QVERIFY(codec.matchesReply(query,messages.first()));
    UnifiedResult result; QVERIFY(codec.decodeReply(query,messages.first(),&result,&error));
    QCOMPARE(result.data.value("motorState").toInt(),1);
    QCOMPARE(result.data.value("positionUm").toUInt(),20000u);
    QByteArray corrupted=reply; corrupted[corrupted.size()-1]=char(quint8(corrupted.at(corrupted.size()-1))^1);
    QCOMPARE(codec.feedReceivedData(corrupted,&errors).size(),0);
    QCOMPARE(errors.last().code,QString("QUSHENG_CRC"));
}

void FrameworkTest::sequenceExhaustionIsFailClosed()
{
    QushengProtocol codec; codec.setAddresses(2,1);
    UnifiedCommand query; query.operation="QUERY_MOTOR"; query.targetDevice="17";
    QByteArray frame; TranslationError error;
    for(int i=0;i<256;++i) { query.requestId=i+1; QVERIFY(codec.encodeCommand(query,&frame,&error)); QCOMPARE(quint8(frame.at(6)),quint8(i)); }
    QVERIFY(!codec.encodeCommand(query,&frame,&error)); QCOMPARE(error.code,QString("QUSHENG_SEQUENCE_EXHAUSTED"));
    codec.resetSession(); QVERIFY(codec.encodeCommand(query,&frame,&error)); QCOMPARE(quint8(frame.at(6)),quint8(0));
}

void FrameworkTest::managerAckIsNotCompletion()
{
    QushengProtocol codec; codec.setAddresses(2,1); FakeTransport transport;
    RequestManager manager(&codec,&transport); manager.setTimeouts(100,100,2000);
    int completed=0; UnifiedResult last;
    QObject::connect(&manager,&RequestManager::completed,&manager,[&](const UnifiedResult &r) { ++completed; last=r; });
    UnifiedCommand move; move.requestId=1; move.targetDevice="17"; move.operation="ABSOLUTE_MOVE";
    move.responsePolicy="complete"; move.parameters.insert("positionUm",20000);
    move.parameters.insert("speedUmPerS",1000); move.parameters.insert("absoluteAction",1);
    manager.enqueue(move); QTRY_COMPARE(transport.sent.size(),1);
    transport.receive(QushengProtocol::makeReply(0,0));
    QTest::qWait(10); QCOMPARE(completed,0); QVERIFY(manager.deviceBusy());
    transport.receive(QushengProtocol::makeFrame(1,2,99,0x17,QByteArray::fromHex("1110004e20ffffff")));
    QCOMPARE(completed,0);
    QTRY_VERIFY(transport.sent.size()>=2); QCOMPARE(quint8(transport.sent.at(1).at(7)),quint8(0x12));
    transport.receive(QushengProtocol::makeFrame(1,2,1,0x12,QByteArray::fromHex("0000000030004e20ffffff")));
    QCOMPARE(completed,0);
    QTRY_VERIFY(transport.sent.size()>=3);
    transport.receive(QushengProtocol::makeFrame(1,2,2,0x12,QByteArray::fromHex("0000000010004e20ffffff")));
    QTRY_COMPARE(completed,1); QVERIFY(last.success); QVERIFY(!manager.deviceBusy());
}

void FrameworkTest::managerStalledWriteAndRejection()
{
    QushengProtocol codec; codec.setAddresses(2,1); FakeTransport transport;
    RequestManager manager(&codec,&transport); manager.setTimeouts(20,50,100);
    int completed=0; UnifiedResult last;
    QObject::connect(&manager,&RequestManager::completed,&manager,[&](const UnifiedResult &r) { ++completed; last=r; });
    UnifiedCommand query; query.requestId=1; query.operation="QUERY_MOTOR"; query.targetDevice="17";
    transport.stall=true; manager.enqueue(query);
    QTRY_COMPARE(completed,1); QCOMPARE(last.error.code,QString("SEND_TIMEOUT"));
    QCOMPARE(transport.sent.size(),1);
    transport.stall=false; query.requestId=2; manager.enqueue(query);
    QTRY_COMPARE(transport.sent.size(),2);
    transport.receive(QushengProtocol::makeFrame(1,2,1,0x12,QByteArray::fromHex("0400000010004e20ffffff")));
    QTRY_COMPARE(completed,2); QCOMPARE(last.error.code,QString("QUSHENG_REJECTED"));
    query.requestId=3; manager.enqueue(query); QTRY_COMPARE(transport.sent.size(),3);
    transport.disconnectNow(); QTRY_COMPARE(completed,3); QCOMPARE(last.error.code,QString("DISCONNECTED"));
    transport.receive(QushengProtocol::makeFrame(1,2,2,0x12,QByteArray::fromHex("0000000010004e20ffffff")));
    QCOMPARE(completed,3);
}

QTEST_GUILESS_MAIN(FrameworkTest)
#include "tst_framework.moc"
