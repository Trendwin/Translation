#ifndef CONVERSIONENGINE_H
#define CONVERSIONENGINE_H

#include "config/protocolconfig.h"
#include <QList>
#include <QStringList>

struct ConversionStepPreview
{
    UnifiedCommand command;
    QMap<QString,QString> parameterTrace;
    QByteArray targetFrame;
};

struct ConversionPreview
{
    QByteArray input;
    QMap<QString,QVariant> fields;
    QStringList fieldOrder;
    QStringList notices;
    QString matchedCommandCode;
    QMap<QString,QString> parameterTrace;
    UnifiedCommand command;
    QByteArray targetFrame;
    QList<ConversionStepPreview> steps;
};

// 转换器只读取配置快照，不识别任何客户协议名称或命令字。
class ConversionEngine
{
public:
    explicit ConversionEngine(const ProtocolConfig &config);
    bool parsePreview(const QByteArray &input, ConversionPreview *out, TranslationError *error) const;
    bool convert(const QByteArray &input, bool live, ConversionPreview *out, TranslationError *error) const;
    bool convertBatch(const QByteArray &input, bool live, ConversionPreview *out, TranslationError *error) const;
    QList<QByteArray> feed(const QByteArray &chunk, QList<TranslationError> *errors);
    void clear() { m_buffer.clear(); }
private:
    bool parse(const QByteArray &input, QMap<QString,QVariant> *fields, TranslationError *error,
               QStringList *order = nullptr, QStringList *notices = nullptr) const;
    bool map(const QMap<QString,QVariant> &fields, UnifiedCommand *command,
             ConversionPreview *preview, TranslationError *error, const QJsonObject &matched) const;
    ProtocolConfig m_config;
    QByteArray m_buffer;
};

#endif
