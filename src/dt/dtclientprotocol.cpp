#include "dtclientprotocol.h"

#include <QRegularExpression>

bool DtClientProtocol::parseCommand(const QString &text, UnifiedCommand *command,
                                    TranslationError *error) const
{
    if (!command || !error) return false;
    static const QRegularExpression syntax(QStringLiteral("^/(\\d+)A([+-]?\\d+)(?:\\r?\\n)?$"));
    const QRegularExpressionMatch match = syntax.match(text);
    if (!match.hasMatch()) {
        *error = {"DT_BAD_COMMAND", QStringLiteral("DT 绝对定位指令格式应为 /<轴号>A<位置>，例如 /1A2000")};
        return false;
    }
    bool axisOk = false;
    bool positionOk = false;
    const uint axis = match.captured(1).toUInt(&axisOk);
    const qlonglong position = match.captured(2).toLongLong(&positionOk);
    if (!axisOk || axis == 0 || axis > 255 || !positionOk
            || position < -2147483648LL || position > 2147483647LL) {
        *error = {"DT_RANGE", QStringLiteral("轴号须为 1..255，位置须为 32 位有符号整数")};
        return false;
    }
    command->targetDevice = QString::number(axis);
    command->action = ActionType::Execute;
    command->operation = QStringLiteral("ABSOLUTE_MOVE");
    command->parameters.insert("axis", axis);
    command->parameters.insert("position", position);
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
                                       : TranslationError{"DT_DEVICE_ERROR", QStringLiteral("设备执行失败")};
        return false;
    }
    const int axis = result.data.value("axis").toInt();
    if (axis < 1 || axis > 255) {
        *error = {"DT_REPLY_AXIS", QStringLiteral("回告缺少有效轴号")};
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
