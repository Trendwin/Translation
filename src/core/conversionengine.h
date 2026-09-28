#ifndef CONVERSIONENGINE_H
#define CONVERSIONENGINE_H

#include "config/protocolconfig.h"
#include <QList>

struct ConversionPreview
{
    QByteArray input;
    QMap<QString,QVariant> fields;
    UnifiedCommand command;
    QByteArray targetFrame;
};

// One instance per upstream connection. The configuration is a value snapshot.
class ConversionEngine
{
public:
    explicit ConversionEngine(const ProtocolConfig &config);
    bool convert(const QByteArray &input, bool live, ConversionPreview *out, TranslationError *error) const;
    QList<QByteArray> feed(const QByteArray &chunk, QList<TranslationError> *errors);
    void clear() { m_buffer.clear(); }
private:
    bool parse(const QByteArray &input, QMap<QString,QVariant> *fields, TranslationError *error) const;
    bool map(const QMap<QString,QVariant> &fields, UnifiedCommand *command, TranslationError *error) const;
    ProtocolConfig m_config;
    QByteArray m_buffer;
};

#endif
