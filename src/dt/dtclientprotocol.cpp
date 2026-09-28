#include "dtclientprotocol.h"
#include <QRegularExpression>

bool DtClientProtocol::parseCommand(const QString &text,UnifiedCommand *command,TranslationError *error) const
{
    if(!command || !error) return false;
    static const QRegularExpression move(QStringLiteral("^/([1-9])([Aa])([0-9]+)R\\r$"));
    static const QRegularExpression query(QStringLiteral("^/([1-9])Q\\r$"));
    const auto mm=move.match(text),qm=query.match(text);
    if(qm.hasMatch()) {
        command->targetDevice=qm.captured(1);
        command->action=ActionType::Query; command->operation="QUERY_MOTOR";
        command->responsePolicy="query"; return true;
    }
    if(!mm.hasMatch()) {
        *error={"DT_UNSUPPORTED",QString::fromUtf8(u8"仅支持单条 A/a...R 或 Q，且以 CR 结束；组合、广播与其他命令拒绝")};
        return false;
    }
    bool ok=false; const qlonglong position=mm.captured(3).toLongLong(&ok);
    if(!ok) { *error={"DT_RANGE",QString::fromUtf8(u8"位置数值溢出")}; return false; }
    command->targetDevice=mm.captured(1);
    command->action=ActionType::Execute; command->operation="ABSOLUTE_MOVE";
    command->parameters.insert("dtPosition",position);
    command->parameterUnits.insert("dtPosition","DT position unit (N mode dependent)");
    command->responsePolicy=mm.captured(2)=="A"?"ack":"complete";
    return true;
}

QByteArray DtClientProtocol::statusReply(bool busy,int errorCode)
{ return QByteArray("/0")+char(0x40|(busy?0x20:0)|(errorCode&0x0f))+QByteArray("\x03\r\n",3); }

bool DtClientProtocol::buildReplyBytes(const UnifiedResult &result,QByteArray *reply,TranslationError *error) const
{
    if(!reply || !error) return false;
    if(!result.success) {
        *error=result.error.isValid()?result.error:TranslationError{"DT_DEVICE_ERROR",QString::fromUtf8(u8"设备结果未确认")};
        return false;
    }
    *reply=statusReply(result.data.value("deviceBusy").toBool());
    return true;
}

bool DtClientProtocol::buildReply(const UnifiedResult &result,QString *reply,TranslationError *error) const
{
    QByteArray bytes;
    if(!reply || !buildReplyBytes(result,&bytes,error)) return false;
    *reply=QString::fromLatin1(bytes); return true;
}
