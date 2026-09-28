#ifndef TRANSLATIONTYPES_H
#define TRANSLATIONTYPES_H

#include <QByteArray>
#include <QMap>
#include <QString>
#include <QVariant>

// 与任何具体协议无关的数据模型。requestId 只在本进程内跟踪请求，不等同于具体协议的线上序号。
enum class ActionType { Unknown, Query, Execute };
enum class RequestState { Queued, Sending, Written, WaitingReply, Acknowledged, InMotion, Succeeded, Failed, TimedOut, Cancelled };

struct TranslationError
{
    QString code;
    QString message;
    bool isValid() const { return !code.isEmpty(); }
};

struct UnifiedCommand
{
    quint64 requestId = 0;
    QString configVersion;
    QString targetDevice;
    ActionType action = ActionType::Unknown;
    QString operation;
    QMap<QString, QVariant> parameters;
    QMap<QString, QString> parameterUnits;
    QString responsePolicy;
};

struct UnifiedResult
{
    quint64 requestId = 0;
    bool success = false;
    QMap<QString, QVariant> data;
    TranslationError error;
};

// 编解码器解析出的协议消息。wireData 仅供诊断；是否匹配请求必须由协议实现判断。
struct ProtocolMessage
{
    int messageType = -1;
    QByteArray payload;
    QByteArray wireData;
};

Q_DECLARE_METATYPE(RequestState)
Q_DECLARE_METATYPE(UnifiedResult)
Q_DECLARE_METATYPE(ProtocolMessage)

#endif
