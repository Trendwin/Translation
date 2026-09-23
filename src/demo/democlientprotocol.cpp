#include "democlientprotocol.h"
#include <QStringList>

bool DemoClientProtocol::parseCommand(const QString &text, UnifiedCommand *command, TranslationError *error) const
{
    if (!command || !error) return false;
    const QStringList parts = text.trimmed().split(' ', QString::SkipEmptyParts);
    if (parts.size() != 2) {
        *error = {"DEMO_BAD_COMMAND", QString::fromUtf8(u8"演示指令格式：PING|FAIL|TIMEOUT 设备名")};
        return false;
    }
    const QString op = parts.at(0).toUpper();
    if (op != "PING" && op != "FAIL" && op != "TIMEOUT") {
        *error = {"DEMO_UNSUPPORTED", QString::fromUtf8(u8"演示适配器不支持该动作")};
        return false;
    }
    command->targetDevice = parts.at(1);
    command->action = ActionType::Execute;
    command->operation = op;
    return true;
}

bool DemoClientProtocol::buildReply(const UnifiedResult &result, QString *reply, TranslationError *error) const
{
    if (!reply || !error) return false;
    if (result.success)
        *reply = QString::fromUtf8(u8"演示回告 OK 请求=%1 数据=%2").arg(result.requestId).arg(result.data.value("value").toString());
    else
        *reply = QString::fromUtf8(u8"演示回告 ERROR 请求=%1 原因=%2").arg(result.requestId).arg(result.error.message);
    return true;
}
