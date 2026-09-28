#ifndef PROTOCOLCONFIG_H
#define PROTOCOLCONFIG_H

#include "model/translationtypes.h"
#include <QJsonObject>

// A complete, immutable-by-value configuration snapshot. Drafts are never shared
// with requests; the caller copies an enabled snapshot when accepting input.
class ProtocolConfig
{
public:
    QJsonObject root;
    QString version;
    static bool fromJson(const QByteArray &json, ProtocolConfig *out, TranslationError *error);
    static bool load(const QString &path, ProtocolConfig *out, TranslationError *error);
    bool save(const QString &path, TranslationError *error) const;
    bool validate(TranslationError *error) const;
    bool liveReady(TranslationError *error) const;
};

#endif
