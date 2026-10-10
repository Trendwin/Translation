#ifndef PROTOCOLCONFIG_H
#define PROTOCOLCONFIG_H

#include "model/translationtypes.h"
#include <QJsonObject>

// 完整配置按值保存。界面启用时生成不可变快照，请求不会引用仍在编辑的草稿。
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
    // 共用字段表保持顺序；无参数命令可显式省略指定字段、覆盖帧尾，不能按操作名称猜客户格式。
    QJsonObject inputFormat(const QJsonObject &mapping) const;
    // 界面和解析器共用严格的字节入口，避免同一转义在配置与输入框中被不同解释。
    static bool decodeBytes(const QString &text, const QString &encoding,
                            QByteArray *bytes, TranslationError *error);
};

#endif
