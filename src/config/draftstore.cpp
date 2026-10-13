#include "draftstore.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>

namespace {
bool fail(TranslationError *error, const char *code, const QString &message)
{
    if (error) *error = {QString::fromLatin1(code), message};
    return false;
}
}

DraftStore::DraftStore(const QString &filePath)
    : m_filePath(filePath.isEmpty() ? defaultFilePath() : filePath) {}

QString DraftStore::defaultFilePath()
{
    // 使用稳定的用户目录，重新编译、更换exe目录或当前工作目录不会丢失草稿。
    // main在创建窗口前设置Trendwin/Translation；显式路径供测试隔离使用。
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return directory.isEmpty() ? QString() : QDir(directory).filePath("draft.json");
}

bool DraftStore::exists() const { return QFileInfo::exists(m_filePath); }

bool DraftStore::load(QJsonObject *draft, TranslationError *error) const
{
    if (error) *error = TranslationError();
    if (!draft) return fail(error, "DRAFT_OUTPUT", QString::fromUtf8(u8"未提供草稿接收对象"));
    QFile file(m_filePath);
    if (!file.open(QIODevice::ReadOnly))
        return fail(error, "DRAFT_READ", QString::fromUtf8(u8"无法读取本地草稿：") + file.errorString());
    const QByteArray bytes = file.readAll();
    if (file.error() != QFileDevice::NoError)
        return fail(error, "DRAFT_READ", QString::fromUtf8(u8"读取本地草稿失败：") + file.errorString());
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (!document.isObject())
        return fail(error, "DRAFT_JSON", parseError.error == QJsonParseError::NoError
            ? QString::fromUtf8(u8"本地草稿必须为JSON对象")
            : QString::fromUtf8(u8"本地草稿JSON格式损坏（第%1字节）").arg(parseError.offset));
    // 不在读取时替换损坏文件；成功也只恢复编辑草稿，不自动授权真实发送。
    *draft = document.object();
    return true;
}

bool DraftStore::save(const QJsonObject &draft, TranslationError *error) const
{
    if (error) *error = TranslationError();
    if (m_filePath.isEmpty()) return fail(error, "DRAFT_PATH", QString::fromUtf8(u8"无法确定本地草稿保存目录"));
    if (!QDir().mkpath(QFileInfo(m_filePath).absolutePath()))
        return fail(error, "DRAFT_DIRECTORY", QString::fromUtf8(u8"无法创建本地草稿保存目录"));
    // 先写临时文件，全部写完后原子替换。磁盘错误不能截断上一份可恢复的草稿。
    QSaveFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly))
        return fail(error, "DRAFT_WRITE", QString::fromUtf8(u8"无法写入本地草稿：") + file.errorString());
    const QByteArray bytes = QJsonDocument(draft).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size())
        return fail(error, "DRAFT_WRITE", QString::fromUtf8(u8"本地草稿未完整写入：") + file.errorString());
    if (!file.commit())
        return fail(error, "DRAFT_COMMIT", QString::fromUtf8(u8"本地草稿保存失败：") + file.errorString());
    return true;
}
