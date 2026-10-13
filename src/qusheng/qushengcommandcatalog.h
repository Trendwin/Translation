#ifndef QUSHENGCOMMANDCATALOG_H
#define QUSHENGCOMMANDCATALOG_H

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

// 完整协议目录负责“用户能够找到什么”；执行器白名单负责“程序能够安全发送什么”。
// 两者不能合并：保存待实现映射只记录用户意图，不表示已具备组帧、应答和超时处理能力。
class QushengCommandCatalog
{
public:
    static QJsonArray commands();
    static QJsonArray options();
    static QJsonObject find(const QString &operation);
    static bool isExecutable(const QString &operation);
    static bool usesMotionEditor(const QString &operation);
    static bool isStopOrReset(const QString &operation);
    static int commandCode(const QString &operation);
    static bool isReceiveOnly(const QString &operation);
    static bool isAbsoluteMotion(const QString &operation);
    static int fixedAction(const QString &operation);
    static bool isLegacyOperation(const QString &operation);
    static QString name(const QString &operation);
};

#endif
