#include "dtclientprotocol.h"

#include <QRegularExpression>

bool DtClientProtocol::parseCommand(const QString &text, UnifiedCommand *command,
                                    TranslationError *error) const
{
    if (!command || !error) return false;
    static const QRegularExpression syntax(QStringLiteral("^/(\\d+)A([+-]?\\d+)(?:\\r?\\n)?$"));
    const QRegularExpressionMatch match = syntax.match(text);
    if (!match.hasMatch()) {
        *error = {"DT_BAD_COMMAND", QString::fromUtf8(u8"DT 前进位置模式指令格式应为 /<轴号>A<距离>，例如 /1A2000")};
        return false;
    }
    bool axisOk = false;
    bool distanceOk = false;
    const uint axis = match.captured(1).toUInt(&axisOk);
    const qlonglong distance = match.captured(2).toLongLong(&distanceOk);
    if (!axisOk || axis != 1 || !distanceOk || distance < 0 || distance > 0xfffffeLL) {
        *error = {"DT_RANGE", QString::fromUtf8(u8"本阶段轴号须为 1，普通前进距离须为 0..0xFFFFFE")};
        return false;
    }
    command->targetDevice = QString::number(axis);
    command->action = ActionType::Execute;
    command->operation = QStringLiteral("FORWARD_POSITION_MOVE");
    command->parameters.insert("axis", axis);
    command->parameters.insert("distance", distance);
    command->parameters.insert("speed", 1000u);
    return true;
}

QByteArray DtClientProtocol::successReply(int axis)
{
    return QByteArray("/") + QByteArray::number(axis) + QByteArray("@\x03\r\n", 4);
}

bool DtClientProtocol::buildReplyBytes(const UnifiedResult &result, QByteArray *reply,
                                       TranslationError *error) const
{
    if (!reply || !error) return false;
    if (!result.success) {
        *error = result.error.isValid() ? result.error
                                       : TranslationError{"DT_DEVICE_ERROR", QString::fromUtf8(u8"设备执行失败")};
        return false;
    }
    const int axis = result.data.value("axis").toInt();
    if (axis < 1 || axis > 255) {
        *error = {"DT_REPLY_AXIS", QString::fromUtf8(u8"回告缺少有效轴号")};
        return false;
    }
    *reply = successReply(axis);
    return true;
}

bool DtClientProtocol::buildReply(const UnifiedResult &result, QString *reply,
                                  TranslationError *error) const
{
    QByteArray bytes;
    if (!reply || !buildReplyBytes(result, &bytes, error)) return false;
    *reply = QString::fromLatin1(bytes);
    return true;
}
