#include "textformat.h"
#include "protocolconfig.h"
#include <QRegularExpression>
#include <QSet>
#include <QtMath>

namespace
{
bool fail(TranslationError *e, const QString &code, const QString &message)
{
    if (e)
        *e = {code, message};
    return false;
}
struct Part
{
    QString name;
    QByteArray literal;
};
bool parts(const QString &text, QList<Part> *out, QStringList *names, TranslationError *e)
{
    if (text.size() > 4096)
        return fail(e, "TEMPLATE_SIZE", QString::fromUtf8(u8"模板超过 4096 字符"));
    int cursor = 0;
    while (cursor < text.size())
    {
        const int start = text.indexOf('{', cursor);
        const int endLiteral = start < 0 ? text.size() : start;
        const QString literalText = text.mid(cursor, endLiteral - cursor);
        if (literalText.contains('}'))
            return fail(e, "TEMPLATE_SYNTAX", QString::fromUtf8(u8"模板含未配对的花括号"));
        QByteArray literal;
        if (!ProtocolConfig::decodeBytes(literalText, "ascii", &literal, e))
            return false;
        if (!literal.isEmpty())
            out->append({QString(), literal});
        if (start < 0)
            break;
        const int end = text.indexOf('}', start + 1);
        const QString name = text.mid(start + 1, end - start - 1);
        const QRegularExpression identifier(QStringLiteral("^[\\p{L}_][\\p{L}\\p{N}_]*$"),
                                            QRegularExpression::UseUnicodePropertiesOption);
        const auto match = identifier.match(name);
        if (end < 0 || !match.hasMatch() || match.capturedLength() != name.size() || names->contains(name))
            return fail(e, "TEMPLATE_SYNTAX",
                        QString::fromUtf8(u8"占位字段须为唯一标识符，可用中文、字母、数字及下划线"));
        if (!out->isEmpty() && !out->last().name.isEmpty())
            return fail(e, "TEMPLATE_BOUNDARY", QString::fromUtf8(u8"相邻占位字段之间必须填写固定分隔文字"));
        names->append(name);
        out->append({name, QByteArray()});
        cursor = end + 1;
        if (names->size() > 64)
            return fail(e, "TEMPLATE_SIZE", QString::fromUtf8(u8"单个模板最多 64 个字段"));
    }
    return true;
}
bool definitions(const QJsonArray &defs, const QStringList &expected, QSet<QString> *all, TranslationError *e)
{
    QSet<QString> names;
    for (const auto &v : defs)
    {
        const auto f = v.toObject();
        const QString n = f.value("name").toString();
        if (!expected.contains(n) || names.contains(n) || all->contains(n))
            return fail(e, "TEMPLATE_FIELD",
                        QString::fromUtf8(u8"字段“%1”未在模板中、重复或与外层重名").arg(n));
        if (!QStringList({"ascii", "int", "uint"}).contains(f.value("type").toString()) ||
            !QStringList({"metadata", "parameter", "execution", "checksum"})
                 .contains(f.value("role").toString("parameter")))
            return fail(e, "TEMPLATE_FIELD", QString::fromUtf8(u8"字段“%1”的类型或用途无效").arg(n));
        if (f.contains("fixedValue"))
        {
            const QString value = f.value("fixedValue").toVariant().toString();
            bool ok = !value.isEmpty();
            for (const QChar ch : value)
                if (ch.unicode() < 32 || ch.unicode() > 126)
                    ok = false;
            if (f.value("type").toString() != "ascii")
            {
                bool numberOk = false;
                value.toLongLong(&numberOk);
                const QRegularExpression re(f.value("type").toString() == "uint" ? "^[0-9]+$" : "^-?[0-9]+$");
                const auto m = re.match(value);
                ok = ok && numberOk && m.hasMatch() && m.capturedLength() == value.size();
            }
            if (!ok)
                return fail(e, "TEMPLATE_FIXED", QString::fromUtf8(u8"字段“%1”的固定值与类型不符").arg(n));
        }
        for (const QString &key : {QStringLiteral("min"), QStringLiteral("max")})
            if (f.contains(key))
            {
                const double value = f.value(key).toDouble(qQNaN());
                if (f.value("type").toString() == "ascii" || !f.value(key).isDouble() || !qIsFinite(value) ||
                    qAbs(value) > 9007199254740991.0 || value != double(qint64(value)) ||
                    (f.value("type").toString() == "uint" && value < 0))
                    return fail(e, "TEMPLATE_RANGE",
                                QString::fromUtf8(u8"字段“%1”的范围不是有效整数").arg(n));
            }
        if (f.contains("min") && f.contains("max") && f.value("min").toDouble() > f.value("max").toDouble())
            return fail(e, "TEMPLATE_RANGE", QString::fromUtf8(u8"字段“%1”的最小值大于最大值").arg(n));
        names.insert(n);
    }
    if (names.size() != expected.size())
        return fail(e, "TEMPLATE_FIELD", QString::fromUtf8(u8"请为模板中的每个占位字段配置数据类型"));
    all->unite(names);
    return true;
}
bool capture(const QList<Part> &parts, const QByteArray &input, QMap<QString, QByteArray> *values,
             TranslationError *e)
{
    int cursor = 0;
    for (int i = 0; i < parts.size(); ++i)
    {
        const auto &p = parts.at(i);
        if (p.name.isEmpty())
        {
            if (input.mid(cursor, p.literal.size()) != p.literal)
                return fail(e, "TEMPLATE_LITERAL",
                            QString::fromUtf8(u8"报文固定文字不匹配（字节 %1）").arg(cursor));
            cursor += p.literal.size();
            continue;
        }
        const int end = i + 1 < parts.size() ? input.indexOf(parts.at(i + 1).literal, cursor) : input.size();
        if (end < 0)
            return fail(e, "TEMPLATE_DELIMITER", QString::fromUtf8(u8"字段“%1”之后缺少分隔文字").arg(p.name));
        values->insert(p.name, input.mid(cursor, end - cursor));
        cursor = end;
    }
    if (cursor != input.size())
        return fail(e, "TEMPLATE_EXTRA", QString::fromUtf8(u8"报文含模板之外的多余内容；不支持一包多条命令"));
    return true;
}
bool typed(const QJsonArray &defs, const QMap<QString, QByteArray> &raw, QMap<QString, QVariant> *out,
           QStringList *notices, TranslationError *e)
{
    for (const auto &v : defs)
    {
        const auto f = v.toObject();
        const QString name = f.value("name").toString(), type = f.value("type").toString();
        const QString value = QString::fromLatin1(raw.value(name));
        for (const QChar ch : value)
            if (ch.unicode() < 32 || ch.unicode() > 126)
                return fail(
                    e, "FIELD_ASCII",
                    QString::fromUtf8(u8"字段“%1”含控制字符；分隔与换行必须写入模板固定文字").arg(name));
        if (value.isEmpty())
            return fail(e, "FIELD_EMPTY", QString::fromUtf8(u8"字段“%1”不能为空").arg(name));
        if (type == "ascii")
            out->insert(name, value);
        else
        {
            bool ok = false;
            const qlonglong n = value.toLongLong(&ok);
            const QRegularExpression re(type == "uint" ? "^[0-9]+$" : "^-?[0-9]+$");
            if (!ok || !re.match(value).hasMatch() || re.match(value).capturedLength() != value.size())
                return fail(e, "FIELD_NUMBER",
                            QString::fromUtf8(u8"字段“%1”不是有效的 %2 十进制整数").arg(name, type));
            out->insert(name, n);
        }
        if (f.contains("fixedValue") &&
            (type == "ascii"
                 ? out->value(name).toString() != f.value("fixedValue").toVariant().toString()
                 : out->value(name).toLongLong() != f.value("fixedValue").toVariant().toLongLong()))
            return fail(e, "FIELD_CONSTANT", QString::fromUtf8(u8"固定字段“%1”的值不匹配").arg(name));
        if ((f.contains("min") && out->value(name).toDouble() < f.value("min").toDouble()) ||
            (f.contains("max") && out->value(name).toDouble() > f.value("max").toDouble()))
            return fail(e, "FIELD_RANGE", QString::fromUtf8(u8"字段“%1”超出配置范围").arg(name));
        if (f.value("role").toString() == "checksum" && notices)
            notices->append(QString::fromUtf8(u8"%1：仅提取校验值文本，未校验").arg(name));
        if (f.value("role").toString() == "execution" && notices)
            notices->append(
                QString::fromUtf8(u8"%1：执行控制（仅解析），本版本不执行该控制，禁止转换发送").arg(name));
    }
    return true;
}
} // namespace
QJsonArray TextFormat::fields(const QJsonObject &external, const QString &code)
{
    const auto format = external.value("textFormat").toObject();
    QJsonArray result = format.value("fields").toArray();
    for (const auto &v : format.value("commands").toArray())
    {
        const auto command = v.toObject();
        if (command.value("code").toString() == code)
            for (const auto &f : command.value("fields").toArray())
                result.append(f);
    }
    return result;
}
bool TextFormat::validate(const QJsonObject &external, TranslationError *e)
{
    if (external.value("encoding").toString() != "ascii")
        return fail(e, "TEMPLATE_ENCODING", QString::fromUtf8(u8"文本模板仅支持 ASCII 编码"));
    const auto f = external.value("textFormat").toObject();
    if (!external.value("textFormat").isObject() || !f.value("template").isString() ||
        !f.value("fields").isArray() || !f.value("commands").isArray())
        return fail(e, "TEMPLATE_SCHEMA",
                    QString::fromUtf8(u8"文本格式需要正文模板、外层字段列表和命令格式列表"));
    QList<Part> outer;
    QStringList names;
    if (!parts(f.value("template").toString(), &outer, &names, e))
        return false;
    if (!names.removeOne("command") || !names.removeOne("arguments"))
        return fail(e, "TEMPLATE_COMMAND",
                    QString::fromUtf8(u8"外层模板必须各包含一次 {command} 和 {arguments}"));
    QSet<QString> shared;
    if (!definitions(f.value("fields").toArray(), names, &shared, e))
        return false;
    shared.insert("command");
    shared.insert("arguments");
    QByteArray header, footer;
    if (!ProtocolConfig::decodeBytes(external.value("header").toString(), "ascii", &header, e) ||
        !ProtocolConfig::decodeBytes(external.value("footer").toString(), "ascii", &footer, e))
        return false;
    int outerMinimum = header.size() + footer.size();
    QByteArray argumentBoundary;
    for (int i = 0; i < outer.size(); ++i)
    {
        const auto &part = outer.at(i);
        if (part.name.isEmpty())
        {
            outerMinimum += part.literal.size();
            if (!footer.isEmpty() && part.literal.contains(footer))
                return fail(e, "TEMPLATE_BOUNDARY", QString::fromUtf8(u8"正文固定文字不能包含完整帧尾"));
        }
        else if (part.name == "arguments")
        {
            if (i + 1 < outer.size())
                argumentBoundary = outer.at(i + 1).literal;
        }
        else if (part.name != "command")
            ++outerMinimum;
    }
    QSet<QString> codes;
    const auto commands = f.value("commands").toArray();
    if (commands.isEmpty() || commands.size() > 256)
        return fail(e, "TEMPLATE_COMMAND", QString::fromUtf8(u8"请配置 1 至 256 条独立命令格式"));
    for (const auto &v : commands)
    {
        const auto c = v.toObject();
        if (!v.isObject() || !c.value("code").isString() || !c.value("template").isString() ||
            !c.value("fields").isArray())
            return fail(e, "TEMPLATE_SCHEMA",
                        QString::fromUtf8(u8"每条命令需要命令码、参数模板和参数字段列表"));
        const QString code = c.value("code").toString();
        if (code.isEmpty() || code.size() > 128 || codes.contains(code))
            return fail(e, "TEMPLATE_COMMAND", QString::fromUtf8(u8"命令码不能为空、重复或超过 128 字符"));
        for (const QChar ch : code)
            if (ch.unicode() < 33 || ch.unicode() > 126 || ch == '\\')
                return fail(e, "TEMPLATE_COMMAND", QString::fromUtf8(u8"命令码必须为可见 ASCII 字符"));
        for (int i = 0; i + 1 < outer.size(); ++i)
            if (outer.at(i).name == "command" && code.toLatin1().contains(outer.at(i + 1).literal))
                return fail(e, "TEMPLATE_COMMAND", QString::fromUtf8(u8"命令码含命令后的分隔文字"));
        codes.insert(code);
        QList<Part> p;
        QStringList local;
        QSet<QString> all = shared;
        if (!parts(c.value("template").toString(), &p, &local, e) ||
            !definitions(c.value("fields").toArray(), local, &all, e))
            return false;
        int minimum = outerMinimum + code.size();
        for (const auto &part : p)
        {
            minimum += part.name.isEmpty() ? part.literal.size() : 1;
            if (part.name.isEmpty() &&
                ((!argumentBoundary.isEmpty() && part.literal.contains(argumentBoundary)) ||
                 (!footer.isEmpty() && part.literal.contains(footer))))
                return fail(e, "TEMPLATE_BOUNDARY",
                            QString::fromUtf8(u8"命令“%1”的参数固定文字与外层边界冲突").arg(code));
        }
        if (minimum > 4096)
            return fail(e, "TEMPLATE_SIZE",
                        QString::fromUtf8(u8"命令“%1”的完整报文最短长度超过 4096 字节").arg(code));
    }
    return true;
}
bool TextFormat::parse(const QJsonObject &external, const QByteArray &payload, QMap<QString, QVariant> *out,
                       QStringList *order, QStringList *notices, TranslationError *e)
{
    for (char ch : payload)
        if (quint8(ch) > 127)
            return fail(e, "FIELD_ASCII", QString::fromUtf8(u8"文本报文含非 ASCII 字节"));
    const auto f = external.value("textFormat").toObject();
    QList<Part> p;
    QStringList names;
    QMap<QString, QByteArray> raw;
    if (!parts(f.value("template").toString(), &p, &names, e) || !capture(p, payload, &raw, e))
        return false;
    const QString code = QString::fromLatin1(raw.value("command"));
    QJsonObject command;
    for (const auto &v : f.value("commands").toArray())
        if (v.toObject().value("code").toString() == code)
            command = v.toObject();
    if (command.isEmpty())
        return fail(e, "COMMAND_UNSUPPORTED", QString::fromUtf8(u8"命令码“%1”没有独立命令格式").arg(code));
    out->insert("command", code);
    if (!typed(f.value("fields").toArray(), raw, out, notices, e))
        return false;
    QList<Part> local;
    QStringList localNames;
    QMap<QString, QByteArray> localRaw;
    if (!parts(command.value("template").toString(), &local, &localNames, e) ||
        !capture(local, raw.value("arguments"), &localRaw, e) ||
        !typed(command.value("fields").toArray(), localRaw, out, notices, e))
        return false;
    if (order)
        for (const QString &name : names)
        {
            if (name == "arguments")
                *order += localNames;
            else
                order->append(name);
        }
    return true;
}
