#ifndef CONVERSIONENGINE_H
#define CONVERSIONENGINE_H

#include "config/protocolconfig.h"
#include <QList>
#include <QStringList>

struct ConversionPreview
{
    QByteArray input;
    QMap<QString,QVariant> fields;
    QStringList fieldOrder;
    QString matchedCommandCode;
    QMap<QString,QString> parameterTrace;
    UnifiedCommand command;
    QByteArray targetFrame;
};

// 转换器只读取配置快照，不识别任何客户协议名称或命令字。
class ConversionEngine
{
public:
    explicit ConversionEngine(const ProtocolConfig &config);
    bool convert(const QByteArray &input, bool live, ConversionPreview *out, TranslationError *error) const;
    QList<QByteArray> feed(const QByteArray &chunk, QList<TranslationError> *errors);
    void clear() { m_buffer.clear(); }
private:
    bool parse(const QByteArray &input, QMap<QString,QVariant> *fields, TranslationError *error) const;
    bool map(const QMap<QString,QVariant> &fields, UnifiedCommand *command,
             ConversionPreview *preview, TranslationError *error) const;
    ProtocolConfig m_config;
    QByteArray m_buffer;
};

#endif
