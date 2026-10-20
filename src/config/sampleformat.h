#ifndef SAMPLEFORMAT_H
#define SAMPLEFORMAT_H
#include "model/translationtypes.h"
#include <QJsonObject>
#include <QList>

struct SamplePiece
{
    QByteArray bytes;
    QString scope; // outer, local, command, boundary
    bool literal = false;
    bool locked = false;
    QJsonObject field;
};

struct SampleFormatDraft
{
    QList<SamplePiece> pieces;
    QJsonObject originalFormat;
    QString code;
    QByteArray input;
    QByteArray header;
    QByteArray footer;
    bool existingCommand = false;
};

// Explicit sample operations: preparing or failing never modifies the current format.
class SampleFormat
{
  public:
    static QString escaped(const QByteArray &bytes);
    static QJsonObject suggestSeparators(const QString &sample);
    static bool prepare(const QJsonObject &external, const QString &sample, const QJsonObject &separators,
                        SampleFormatDraft *draft, TranslationError *error);
    static bool apply(const QJsonObject &external, const SampleFormatDraft &draft,
                      const QJsonObject &separators, QJsonObject *format, TranslationError *error);
};
#endif
