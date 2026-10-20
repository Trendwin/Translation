#ifndef TEXTFORMAT_H
#define TEXTFORMAT_H
#include "model/translationtypes.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

// Text templates deliberately use first literal boundaries, never regex backtracking.
class TextFormat
{
  public:
    static bool enabled(const QJsonObject &external)
    {
        return external.value("mode").toString() == "textTemplate";
    }
    static bool validate(const QJsonObject &external, TranslationError *error);
    static bool parse(const QJsonObject &external, const QByteArray &payload, QMap<QString, QVariant> *fields,
                      QStringList *order, QStringList *notices, TranslationError *error);
    static QJsonArray fields(const QJsonObject &external, const QString &code);
};
#endif
