#include "sampleformat.h"
#include "protocolconfig.h"
#include "textformat.h"
#include <QJsonArray>
#include <QRegularExpression>

namespace
{
bool fail(TranslationError *error, const QString &code, const QString &message)
{
    if (error)
        *error = {code, message};
    return false;
}
struct Separators
{
    QByteArray region, field, assignment, parameter;
};
bool decodeSeparators(const QJsonObject &json, Separators *s, TranslationError *error)
{
    const QStringList keys{"region", "field", "assignment", "parameter"};
    QList<QByteArray> values;
    for (const QString &key : keys)
    {
        QByteArray value;
        if (!ProtocolConfig::decodeBytes(json.value(key).toString(), "ascii", &value, error))
            return false;
        if (value.size() > 8)
            return fail(error, "SAMPLE_SEPARATOR", QString::fromUtf8(u8"每个分隔符最多 8 字节"));
        for (char ch : value)
            if ((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z'))
                return fail(error, "SAMPLE_SEPARATOR",
                            QString::fromUtf8(u8"分隔符请使用标点、空白或转义字符，不能填写字母和数字"));
        for (const auto &other : values)
            if (!value.isEmpty() && !other.isEmpty() && (value.contains(other) || other.contains(value)))
                return fail(error, "SAMPLE_SEPARATOR",
                            QString::fromUtf8(u8"不同用途的分隔符不能相同或互相包含"));
        values.append(value);
    }
    s->region = values.at(0);
    s->field = values.at(1);
    s->assignment = values.at(2);
    s->parameter = values.at(3);
    if (s->assignment.isEmpty())
        return fail(error, "SAMPLE_SEPARATOR",
                    QString::fromUtf8(u8"请填写命令与参数之间的分隔符，例如 = 或 :"));
    return true;
}
QJsonObject definition(const QString &name, const QByteArray &bytes, const QString &role)
{
    const QString text = QString::fromLatin1(bytes);
    bool ok = false;
    text.toLongLong(&ok);
    const auto match = QRegularExpression("^-?[0-9]+$").match(text);
    const bool number = ok && match.hasMatch() && match.capturedLength() == text.size() &&
                        !(bytes.size() > 1 && bytes.startsWith('0'));
    return {{"name", name},
            {"type", number ? (bytes.startsWith('-') ? "int" : "uint") : "ascii"},
            {"role", role}};
}
void literal(QList<SamplePiece> *pieces, const QByteArray &bytes, const QString &scope, bool locked = true)
{
    if (!bytes.isEmpty())
        pieces->append({bytes, scope, true, locked, QJsonObject()});
}
void outerPieces(const QByteArray &input, const Separators &s, QList<SamplePiece> *pieces, int *number)
{
    int cursor = 0;
    bool afterLineBreak = false;
    while (cursor < input.size())
    {
        QByteArray separator;
        if (!s.region.isEmpty() && input.mid(cursor).startsWith(s.region))
            separator = s.region;
        else if (!s.field.isEmpty() && input.mid(cursor).startsWith(s.field))
            separator = s.field;
        if (!separator.isEmpty())
        {
            literal(pieces, separator, "outer");
            cursor += separator.size();
            afterLineBreak = false;
            continue;
        }
        if (quint8(input.at(cursor)) < 32 || quint8(input.at(cursor)) == 127)
        {
            const char ch = input.at(cursor++);
            literal(pieces, QByteArray(1, ch), "outer");
            afterLineBreak = ch == '\r' || ch == '\n';
            continue;
        }
        int end = cursor + 1;
        while (end < input.size() && quint8(input.at(end)) >= 32 && quint8(input.at(end)) != 127 &&
               (s.region.isEmpty() || !input.mid(end).startsWith(s.region)) &&
               (s.field.isEmpty() || !input.mid(end).startsWith(s.field)))
            ++end;
        const auto bytes = input.mid(cursor, end - cursor);
        if (afterLineBreak)
            literal(pieces, bytes, "outer", false);
        else
            pieces->append({bytes, "outer", false, false,
                            definition(QString::fromUtf8(u8"字段%1").arg(++*number), bytes, "metadata")});
        afterLineBreak = false;
        cursor = end;
    }
}
bool localPieces(const QByteArray &input, const QByteArray &separator, QList<SamplePiece> *pieces,
                 TranslationError *error)
{
    if (input.isEmpty())
    {
        pieces->append({QByteArray(), "local", true, true, QJsonObject()});
        return true;
    }
    int cursor = 0, index = 0;
    while (cursor <= input.size())
    {
        const int found = separator.isEmpty() ? -1 : input.indexOf(separator, cursor);
        const int end = found < 0 ? input.size() : found;
        const auto bytes = input.mid(cursor, end - cursor);
        if (bytes.isEmpty())
            return fail(error, "SAMPLE_PARAMETER",
                        QString::fromUtf8(u8"参数分隔后出现空值，请检查分隔符或使用零参数样例"));
        for (char ch : bytes)
            if (quint8(ch) < 32 || quint8(ch) > 126)
                return fail(error, "SAMPLE_PARAMETER",
                            QString::fromUtf8(u8"参数中含换行或控制字符，请调整分段位置"));
        pieces->append({bytes, "local", false, false,
                        definition(QString::fromUtf8(u8"参数%1").arg(++index), bytes, "parameter")});
        if (found < 0)
            break;
        literal(pieces, separator, "local");
        cursor = end + separator.size();
    }
    return true;
}
QJsonObject fieldByName(const QJsonArray &fields, const QString &name)
{
    for (const auto &v : fields)
        if (v.toObject().value("name").toString() == name)
            return v.toObject();
    return QJsonObject();
}
// Read the existing envelope without requiring a mapping or knowing the new command code.
bool existingPieces(const QString &text, const QByteArray &input, const QString &scope,
                    const QJsonArray &fields, QList<SamplePiece> *pieces, TranslationError *error)
{
    const auto marker = QRegularExpression(QStringLiteral("\\{([^{}]+)\\}"));
    auto matches = marker.globalMatch(text);
    int source = 0, cursor = 0;
    while (matches.hasNext())
    {
        const auto match = matches.next();
        QByteArray fixed;
        if (!ProtocolConfig::decodeBytes(text.mid(source, match.capturedStart() - source), "ascii", &fixed,
                                         error))
            return false;
        if (input.mid(cursor, fixed.size()) != fixed)
            return fail(error, "SAMPLE_STRUCTURE",
                        QString::fromUtf8(u8"样例与已保存格式的固定文字不一致；原配置未修改"));
        literal(pieces, fixed, scope);
        cursor += fixed.size();
        source = match.capturedEnd();
        const QString name = match.captured(1);
        QByteArray next;
        const int nextMarker = text.indexOf('{', source);
        if (!ProtocolConfig::decodeBytes(text.mid(source, nextMarker < 0 ? -1 : nextMarker - source), "ascii",
                                         &next, error))
            return false;
        const int end = next.isEmpty() ? input.size() : input.indexOf(next, cursor);
        if (end < 0)
            return fail(error, "SAMPLE_STRUCTURE",
                        QString::fromUtf8(u8"样例缺少已保存格式中的分隔文字；原配置未修改"));
        const auto bytes = input.mid(cursor, end - cursor);
        cursor = end;
        const QString kind = name == "command" && scope == "outer"     ? "command"
                             : name == "arguments" && scope == "outer" ? "arguments"
                                                                       : scope;
        pieces->append(
            {bytes, kind, false, kind == "command" || kind == "arguments", fieldByName(fields, name)});
    }
    QByteArray tail;
    if (!ProtocolConfig::decodeBytes(text.mid(source), "ascii", &tail, error))
        return false;
    if (input.mid(cursor) != tail)
        return fail(error, "SAMPLE_STRUCTURE",
                    QString::fromUtf8(u8"样例末尾或参数数量与已保存格式不一致；原配置未修改"));
    literal(pieces, tail, scope);
    return true;
}
} // namespace

QString SampleFormat::escaped(const QByteArray &bytes)
{
    QString result;
    for (char ch : bytes)
    {
        if (ch == '\r')
            result += "\\r";
        else if (ch == '\n')
            result += "\\n";
        else if (ch == '\t')
            result += "\\t";
        else if (ch == '\\')
            result += "\\\\";
        else if (ch == '{')
            result += "\\x7B";
        else if (ch == '}')
            result += "\\x7D";
        else if (quint8(ch) < 32 || quint8(ch) > 126)
            result += QStringLiteral("\\x%1").arg(quint8(ch), 2, 16, QLatin1Char('0'));
        else
            result += QChar::fromLatin1(ch);
    }
    return result;
}
QJsonObject SampleFormat::suggestSeparators(const QString &sample)
{
    QByteArray bytes;
    ProtocolConfig::decodeBytes(sample, "ascii", &bytes, nullptr);
    const QString assignment = bytes.contains('=') ? "=" : bytes.contains(':') ? ":" : "=";
    const QString region = bytes.contains(';') ? ";" : "";
    const QString field = bytes.contains('&') ? "&" : bytes.contains('|') ? "|" : "";
    const QString parameter = bytes.contains(',') ? "," : bytes.contains('/') ? "/" : ",";
    return {{"region", region}, {"field", field}, {"assignment", assignment}, {"parameter", parameter}};
}
bool SampleFormat::prepare(const QJsonObject &external, const QString &sample, const QJsonObject &separators,
                           SampleFormatDraft *draft, TranslationError *error)
{
    if (error)
        *error = TranslationError();
    SampleFormatDraft result;
    result.originalFormat = external.value("textFormat").toObject();
    if (!ProtocolConfig::decodeBytes(sample, "ascii", &result.input, error) ||
        !ProtocolConfig::decodeBytes(external.value("header").toString(), "ascii", &result.header, error) ||
        !ProtocolConfig::decodeBytes(external.value("footer").toString(), "ascii", &result.footer, error))
        return false;
    if (result.input.size() > 4096)
        return fail(error, "INPUT_OVERFLOW", QString::fromUtf8(u8"完整样例超过 4096 字节"));
    if (result.header.isEmpty() || result.footer.isEmpty())
        return fail(error, "SAMPLE_BOUNDARY", QString::fromUtf8(u8"请先在上方填写帧头和帧尾"));
    if (!result.input.startsWith(result.header) || !result.input.endsWith(result.footer) ||
        result.input.size() < result.header.size() + result.footer.size())
        return fail(error, "SAMPLE_BOUNDARY",
                    QString::fromUtf8(u8"样例与上方帧头“%1”或帧尾“%2”不匹配，请先调整这两项")
                        .arg(escaped(result.header), escaped(result.footer)));
    const auto payload = result.input.mid(result.header.size(),
                                          result.input.size() - result.header.size() - result.footer.size());
    if (payload.contains(result.footer))
        return fail(error, "FRAME_MULTIPLE",
                    QString::fromUtf8(u8"请一次粘贴一条完整样例，正文不能包含完整帧尾"));
    Separators s;
    if (!decodeSeparators(separators, &s, error))
        return false;
    const int assignment = payload.indexOf(s.assignment);
    if (assignment < 0 || payload.indexOf(s.assignment, assignment + s.assignment.size()) >= 0)
        return fail(
            error, "SAMPLE_COMMAND",
            QString::fromUtf8(
                u8"正文必须有且只有一处命令与参数分隔符；存在多处时请调整分隔符，不自动猜测命令区域"));
    int start = 0;
    for (const auto &separator : {s.region, s.field})
        if (!separator.isEmpty())
        {
            const int p = payload.lastIndexOf(separator, assignment - 1);
            if (p >= 0)
                start = qMax(start, p + separator.size());
        }
    for (int i = start; i < assignment; ++i)
        if (quint8(payload.at(i)) < 32)
            start = i + 1;
    const auto codeBytes = payload.mid(start, assignment - start);
    result.code = QString::fromLatin1(codeBytes);
    if (codeBytes.isEmpty())
        return fail(error, "SAMPLE_COMMAND", QString::fromUtf8(u8"命令分隔符之前没有命令码"));
    int end = payload.size();
    for (const auto &separator : {s.region, s.field})
        if (!separator.isEmpty())
        {
            const int p = payload.indexOf(separator, assignment + s.assignment.size());
            if (p >= 0)
                end = qMin(end, p);
        }
    for (int i = assignment + s.assignment.size(); i < end; ++i)
        if (quint8(payload.at(i)) < 32)
        {
            end = i;
            break;
        }
    const auto arguments =
        payload.mid(assignment + s.assignment.size(), end - assignment - s.assignment.size());
    QJsonObject existingCommand;
    for (const auto &v : result.originalFormat.value("commands").toArray())
        if (v.toObject().value("code").toString() == result.code)
            existingCommand = v.toObject();
    QList<SamplePiece> local;
    if (!existingCommand.isEmpty())
    {
        result.existingCommand = true;
        if (!existingPieces(existingCommand.value("template").toString(), arguments, "local",
                            existingCommand.value("fields").toArray(), &local, error))
            return false;
        if (local.isEmpty())
            local.append({QByteArray(), "local", true, true, QJsonObject()});
    }
    else if (!localPieces(arguments, s.parameter, &local, error))
        return false;
    literal(&result.pieces, result.header, "boundary");
    if (!result.originalFormat.value("template").toString().isEmpty())
    {
        QList<SamplePiece> outer;
        if (!existingPieces(result.originalFormat.value("template").toString(), payload, "outer",
                            result.originalFormat.value("fields").toArray(), &outer, error))
            return false;
        bool command = false, args = false;
        for (const auto &piece : outer)
        {
            if (piece.scope == "command")
            {
                command = piece.bytes == codeBytes;
                result.pieces.append(piece);
            }
            else if (piece.scope == "arguments")
            {
                args = piece.bytes == arguments;
                result.pieces += local;
            }
            else
                result.pieces.append(piece);
        }
        if (!command || !args)
            return fail(error, "SAMPLE_STRUCTURE",
                        QString::fromUtf8(u8"所选分隔符划出的命令区域与已保存格式不一致；原配置未修改"));
    }
    else
    {
        int number = 0;
        outerPieces(payload.left(start), s, &result.pieces, &number);
        result.pieces.append({codeBytes, "command", false, true, QJsonObject()});
        literal(&result.pieces, s.assignment, "outer");
        result.pieces += local;
        outerPieces(payload.mid(end), s, &result.pieces, &number);
    }
    literal(&result.pieces, result.footer, "boundary");
    if (draft)
        *draft = result;
    return true;
}
bool SampleFormat::apply(const QJsonObject &external, const SampleFormatDraft &draft,
                         const QJsonObject &separators, QJsonObject *format, TranslationError *error)
{
    if (error)
        *error = TranslationError();
    if (external.value("textFormat").toObject() != draft.originalFormat)
        return fail(error, "SAMPLE_STALE",
                    QString::fromUtf8(u8"当前格式已改变，请重新拆分样例，避免覆盖已有设置"));
    QByteArray header, footer;
    if (!ProtocolConfig::decodeBytes(external.value("header").toString(), "ascii", &header, error) ||
        !ProtocolConfig::decodeBytes(external.value("footer").toString(), "ascii", &footer, error))
        return false;
    if (header != draft.header || footer != draft.footer)
        return fail(error, "SAMPLE_STALE", QString::fromUtf8(u8"帧头或帧尾已改变，请重新拆分样例"));
    QJsonObject result = draft.originalFormat;
    QString outer, local;
    QJsonArray outerFields, localFields;
    bool insertedArguments = false;
    for (const auto &piece : draft.pieces)
    {
        if (piece.scope == "boundary")
            continue;
        if (piece.scope == "command")
        {
            outer += "{command}";
            continue;
        }
        if (piece.scope == "local" && !insertedArguments)
        {
            outer += "{arguments}";
            insertedArguments = true;
        }
        QString &target = piece.scope == "local" ? local : outer;
        if (piece.literal)
            target += escaped(piece.bytes);
        else
        {
            target += "{" + piece.field.value("name").toString() + "}";
            if (piece.scope == "local")
                localFields.append(piece.field);
            else
                outerFields.append(piece.field);
        }
    }
    result.insert("template", outer);
    result.insert("fields", outerFields);
    QJsonArray commands = result.value("commands").toArray();
    int existing = -1;
    for (int i = 0; i < commands.size(); ++i)
        if (commands.at(i).toObject().value("code").toString() == draft.code)
            existing = i;
    QJsonObject command = existing >= 0 ? commands.at(existing).toObject() : QJsonObject();
    command.insert("code", draft.code);
    command.insert("template", local);
    command.insert("fields", localFields);
    if (existing >= 0)
        commands.replace(existing, command);
    else
        commands.append(command);
    result.insert("commands", commands);
    auto check = external;
    check.insert("mode", "textTemplate");
    check.insert("encoding", "ascii");
    check.insert("textFormat", result);
    if (!TextFormat::validate(check, error))
        return false;
    QMap<QString, QVariant> fields;
    if (!TextFormat::parse(check,
                           draft.input.mid(header.size(), draft.input.size() - header.size() - footer.size()),
                           &fields, nullptr, nullptr, error))
        return false;
    auto editor = result.value("sampleEditor").toObject();
    auto samples = editor.value("samples").toObject();
    samples.insert(draft.code, escaped(draft.input));
    editor.insert("samples", samples);
    editor.insert("lastCommand", draft.code);
    editor.insert("separators", separators);
    result.insert("sampleEditor", editor);
    if (format)
        *format = result;
    return true;
}
