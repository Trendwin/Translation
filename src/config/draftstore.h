#ifndef DRAFTSTORE_H
#define DRAFTSTORE_H

#include "model/translationtypes.h"
#include <QJsonObject>

// 仅负责草稿落盘，不负责协议校验、启用或连接设备。
// 草稿可能尚未编辑完成，不能调用只允许合法运行配置的ProtocolConfig::save来限制保存。
class DraftStore
{
public:
    explicit DraftStore(const QString &filePath = QString());
    static QString defaultFilePath();
    QString filePath() const { return m_filePath; }
    bool exists() const;
    bool load(QJsonObject *draft, TranslationError *error) const;
    bool save(const QJsonObject &draft, TranslationError *error) const;
private:
    QString m_filePath;
};

#endif
