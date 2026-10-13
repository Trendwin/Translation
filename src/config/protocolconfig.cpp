#include "protocolconfig.h"
#include "qusheng/qushengcommandcatalog.h"
#include "qusheng/sdk/qushengsdk.h"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>
#include <QtMath>
#include <QRegularExpression>

namespace {
bool fail(TranslationError *error, const QString &code, const QString &message)
{
    if (error) *error = {code, message};
    return false;
}

bool integer(const QJsonValue &value, qint64 minimum, qint64 maximum)
{
    const double number = value.toDouble(qQNaN());
    return value.isDouble() && qIsFinite(number) && number >= minimum && number <= maximum
            && number == double(qint64(number));
}

bool validHex(const QString &text)
{
    QByteArray compact = text.toLatin1();
    compact.replace(" ", "");
    return !compact.isEmpty() && compact.size() % 2 == 0
            && QByteArray::fromHex(compact).toHex() == compact.toLower();
}

bool validExpression(const QJsonObject &expression, const QSet<QString> &fieldNames,
                     qint64 constantMin = 0, qint64 constantMax = 0xfffffe)
{
    const QString kind = expression.value("kind").toString();
    if (kind == "constant") return integer(expression.value("value"), constantMin, constantMax);
    if (kind == "field") return fieldNames.contains(expression.value("field").toString());
    if (kind != "linear" || !fieldNames.contains(expression.value("field").toString())) return false;
    return integer(expression.value("numerator"), -1000000000, 1000000000)
            && integer(expression.value("denominator"), 1, 1000000000)
            && (!expression.contains("offset") || integer(expression.value("offset"), -1000000000, 1000000000))
            && QStringList({"exact", "floor", "ceil", "nearest"}).contains(expression.value("round").toString());
}
}

bool ProtocolConfig::decodeBytes(const QString &text, const QString &encoding,
                                QByteArray *bytes, TranslationError *error)
{
    bytes->clear();
    if (encoding == "hex") {
        if (!validHex(text))
            return fail(error, "INPUT_HEX", QString::fromUtf8(u8"HEX 必须是偶数个十六进制字符，可用空格分隔字节"));
        QByteArray compact = text.toLatin1();
        compact.replace(" ", "");
        *bytes = QByteArray::fromHex(compact);
        return true;
    }
    if (encoding != "ascii")
        return fail(error, "INPUT_ENCODING", QString::fromUtf8(u8"输入编码未配置为 ASCII 或 HEX"));
    // 转义错误不能悄悄丢弃反斜杠或把中文替换成问号，否则预览与客户实际字节会不一致。
    for (int i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (ch.unicode() > 127)
            return fail(error, "INPUT_ASCII", QString::fromUtf8(u8"ASCII 输入只能包含 ASCII 字符"));
        if (ch != '\\') { bytes->append(ch.toLatin1()); continue; }
        if (++i == text.size())
            return fail(error, "INPUT_ESCAPE", QString::fromUtf8(u8"反斜杠后缺少转义字符"));
        const QChar next = text.at(i);
        if (next == 'r') bytes->append('\r');
        else if (next == 'n') bytes->append('\n');
        else if (next == 't') bytes->append('\t');
        else if (next == '\\') bytes->append('\\');
        else if (next == 'x' && i + 2 < text.size()) {
            const QString digits = text.mid(i + 1, 2);
            if (!validHex(digits))
                return fail(error, "INPUT_ESCAPE", QString::fromUtf8(u8"\\x 后必须有两个十六进制字符"));
            const int value = digits.toInt(nullptr, 16);
            if (value > 127)
                return fail(error, "INPUT_ASCII", QString::fromUtf8(u8"超过 ASCII 范围的字节请使用 HEX 编码"));
            bytes->append(char(value)); i += 2;
        } else return fail(error, "INPUT_ESCAPE", QString::fromUtf8(u8"不支持的转义；可用 \\r、\\n、\\t、\\xNN 或双反斜杠"));
    }
    return true;
}

bool ProtocolConfig::fromJson(const QByteArray &json, ProtocolConfig *out, TranslationError *error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (!document.isObject())
        return fail(error, "CONFIG_JSON", QString::fromUtf8(u8"JSON 无效：%1").arg(parseError.errorString()));

    ProtocolConfig value;
    value.root = document.object();
    if (!value.validate(error)) return false;

    // 版本由规范化后的完整 JSON 计算。请求接收时复制该值，后续编辑草稿不会改变在途请求。
    value.version = QString::fromLatin1(QCryptographicHash::hash(
        document.toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
    if (out) *out = value;
    return true;
}

bool ProtocolConfig::load(const QString &path, ProtocolConfig *out, TranslationError *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return fail(error, "CONFIG_READ", file.errorString());
    return fromJson(file.readAll(), out, error);
}

bool ProtocolConfig::save(const QString &path, TranslationError *error) const
{
    if (!validate(error)) return false;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return fail(error, "CONFIG_WRITE", file.errorString());
    const QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Indented);
    // 原子提交前必须写满，磁盘错误不能把半份配置当作成功的用户版本。
    if (file.write(json) != json.size() || !file.commit())
        return fail(error, "CONFIG_WRITE", file.errorString());
    return true;
}

bool ProtocolConfig::validate(TranslationError *error) const
{
    if (!integer(root.value("schemaVersion"), 1, 1))
        return fail(error, "CONFIG_SCHEMA", QString::fromUtf8(u8"schemaVersion 必须为 1"));

    const QJsonObject external = root.value("external").toObject();
    // 同一 schemaVersion 的旧实验配置也不能静默混用：明确提示重建顺序字段与映射。
    if (external.contains("format") || root.contains("upstream") || root.contains("live"))
        return fail(error, "CONFIG_LEGACY", QString::fromUtf8(u8"旧实验配置已停用，请按中文页面重新配置顺序字段、命令映射和设备参数"));
    const QString encoding = external.value("encoding").toString();
    if (!QStringList({"ascii", "hex"}).contains(encoding))
        return fail(error, "CONFIG_ENCODING", QString::fromUtf8(u8"编码方式只能是 ASCII 或 HEX"));
    const QString header = external.value("header").toString();
    const QString footer = external.value("footer").toString();
    if (header.isEmpty()) return fail(error, "CONFIG_HEADER", QString::fromUtf8(u8"帧头不能为空"));
    if (footer.isEmpty()) return fail(error, "CONFIG_FOOTER", QString::fromUtf8(u8"帧尾不能为空"));
    if (encoding == "hex" && (!validHex(header) || !validHex(footer)))
        return fail(error, "CONFIG_BOUNDARY", QString::fromUtf8(u8"HEX 帧头和帧尾必须是偶数个十六进制字符"));
    QByteArray headerBytes, footerBytes;
    if (!decodeBytes(header, encoding, &headerBytes, error)
            || !decodeBytes(footer, encoding, &footerBytes, error)) return false;
    if (headerBytes.size() + footerBytes.size() > 4096)
        return fail(error, "CONFIG_BOUNDARY", QString::fromUtf8(u8"帧头与帧尾超过 4096 字节上限"));
    if (!QStringList({"big", "little"}).contains(external.value("byteOrder").toString("big")))
        return fail(error, "CONFIG_ENDIAN", QString::fromUtf8(u8"高级字节序只能是大端或小端"));

    const QJsonArray fields = external.value("fields").toArray();
    if (fields.isEmpty()) return fail(error, "CONFIG_FIELD", QString::fromUtf8(u8"至少需要一个字段"));
    QSet<QString> names;
    int commandFields = 0;
    bool metTailField = false;
    int fixedBytes = headerBytes.size() + footerBytes.size();
    for (int index = 0; index < fields.size(); ++index) {
        const QJsonObject field = fields.at(index).toObject();
        if (field.contains("offset") || field.contains("endian"))
            return fail(error, "CONFIG_LEGACY", QString::fromUtf8(u8"字段不再使用 offset/endian，请按顺序排列并在高级配置中设置 byteOrder"));
        const QString name = field.value("name").toString().trimmed();
        if (name.isEmpty() || name != field.value("name").toString() || names.contains(name))
            return fail(error, "CONFIG_FIELD", QString::fromUtf8(u8"字段名称不能为空或重复（第 %1 行）").arg(index + 1));
        names.insert(name);
        const QString role = field.value("role").toString();
        if (!QStringList({"command", "parameter", "fixed"}).contains(role))
            return fail(error, "CONFIG_FIELD_ROLE", QString::fromUtf8(u8"字段“%1”的作用无效").arg(name));
        if (role == "command") ++commandFields;
        const QString type = field.value("type").toString();
        if (!QStringList({"ascii", "uint", "int"}).contains(type))
            return fail(error, "CONFIG_FIELD_TYPE", QString::fromUtf8(u8"字段“%1”的数据类型无效").arg(name));
        const QString lengthMode = field.value("lengthMode").toString();
        if (!QStringList({"fixed", "toFooter", "mappedCommand"}).contains(lengthMode))
            return fail(error, "CONFIG_FIELD_LENGTH", QString::fromUtf8(u8"字段“%1”的长度方式无效").arg(name));
        if (lengthMode == "mappedCommand") {
            if (encoding != "ascii" || type != "ascii" || role != "command")
                return fail(error, "CONFIG_FIELD_LENGTH", QString::fromUtf8(u8"按命令码识别仅适用于 ASCII 命令码字段"));
            for (int before = 0; before < index; ++before)
                if (fields.at(before).toObject().value("lengthMode").toString() != "fixed")
                    return fail(error, "CONFIG_FIELD_ORDER", QString::fromUtf8(u8"可变命令码之前只能放固定长度字段"));
        }
        if (metTailField)
            return fail(error, "CONFIG_FIELD_ORDER", QString::fromUtf8(u8"读取到帧尾的字段必须排在最后"));
        if (lengthMode == "fixed" && !integer(field.value("length"), 1, 4096))
            return fail(error, "CONFIG_FIELD_LENGTH", QString::fromUtf8(u8"字段“%1”的固定长度必须大于 0").arg(name));
        if (lengthMode == "toFooter") metTailField = true;
        else fixedBytes += lengthMode == "mappedCommand" ? 1 : field.value("length").toInt();
        if (encoding == "hex" && type != "ascii"
                && (lengthMode != "fixed" || field.value("length").toInt() > 8))
            return fail(error, "CONFIG_FIELD_LENGTH", QString::fromUtf8(u8"HEX 整数字段必须固定为 1 至 8 字节"));
        if (role == "fixed" && !field.contains("fixedValue"))
            return fail(error, "CONFIG_FIELD_CONSTANT", QString::fromUtf8(u8"固定字段“%1”必须填写固定值").arg(name));
        if (role == "fixed") {
            const QString text = field.value("fixedValue").toVariant().toString();
            bool ok = false;
            if (type == "ascii") {
                ok = field.value("fixedValue").isString() && !text.isEmpty();
                for (QChar ch : text) if (ch.unicode() > 127) ok = false;
                if (lengthMode == "fixed" && text.size() != field.value("length").toInt()) ok = false;
            } else {
                text.toLongLong(&ok);
                const QRegularExpression pattern(type == "uint" ? "^[0-9]+$" : "^-?[0-9]+$");
                const QRegularExpressionMatch match = pattern.match(text);
                ok = ok && match.hasMatch() && match.capturedLength() == text.size();
            }
            if (!ok) return fail(error, "CONFIG_FIELD_CONSTANT", QString::fromUtf8(u8"固定字段“%1”的值与类型或长度不符").arg(name));
        }
        if (role != "fixed" && field.contains("fixedValue"))
            return fail(error, "CONFIG_FIELD_CONSTANT", QString::fromUtf8(u8"只有固定字段可以设置固定值（%1）").arg(name));
        if (type == "ascii" && (field.contains("min") || field.contains("max")))
            return fail(error, "CONFIG_RANGE", QString::fromUtf8(u8"ASCII 字段不使用数值上下限（%1）").arg(name));
        // JSON 数字使用双精度，边界限于精确整数范围；不能接受字符串、空值或小数后再隐式转零。
        const qint64 jsonMax = 9007199254740991LL;
        for (const QString &key : {QStringLiteral("min"), QStringLiteral("max")})
            if (field.contains(key) && !integer(field.value(key), type == "uint" ? 0 : -jsonMax, jsonMax))
                return fail(error, "CONFIG_RANGE", QString::fromUtf8(u8"字段“%1”的范围必须为可精确保存的整数").arg(name));
        if (field.contains("min") && field.contains("max")
                && field.value("min").toDouble() > field.value("max").toDouble())
            return fail(error, "CONFIG_RANGE", QString::fromUtf8(u8"字段“%1”的最小值大于最大值").arg(name));
    }
    if (fixedBytes + (metTailField ? 1 : 0) > 4096)
        return fail(error, "CONFIG_FIELD_LENGTH", QString::fromUtf8(u8"配置的完整帧超过 4096 字节上限"));
    if (commandFields != 1)
        return fail(error, "CONFIG_COMMAND_FIELD", QString::fromUtf8(u8"字段表必须且只能有一个“命令码”字段"));

    const QJsonArray mappings = root.value("mappings").toArray();
    if (mappings.isEmpty()) return fail(error, "CONFIG_MAPPING", QString::fromUtf8(u8"至少需要一条命令映射"));
    QSet<QString> commandCodes;
    // SDK预置按操作保存。校验的是被声明的默认参数，不要求提前填写其它业务必填项。
    if (root.contains("sdkDefaults") && !root.value("sdkDefaults").isObject())
        return fail(error, "CONFIG_SDK_DEFAULT", QString::fromUtf8(u8"SDK默认参数必须为对象"));
    const QJsonObject sdkDefaults = root.value("sdkDefaults").toObject();
    for (auto it = sdkDefaults.begin(); it != sdkDefaults.end(); ++it) {
        const int sdkCode = QushengCommandCatalog::commandCode(it.key());
        if (sdkCode < 0 || !it.value().isObject())
            return fail(error, "CONFIG_SDK_DEFAULT", QString::fromUtf8(u8"SDK预置的指令或参数对象无效：%1").arg(it.key()));
        const auto definition = QushengSdk::definition(quint8(sdkCode));
        const QJsonObject preset = it.value().toObject();
        for (auto p = preset.begin(); p != preset.end(); ++p) {
            if (QushengCommandCatalog::usesMotionEditor(it.key()) && p.key() != "devId" && p.key() != "speedUmPerS")
                return fail(error, "CONFIG_SDK_DEFAULT", QString::fromUtf8(u8"运动模板仅预置电机编号和速度；模式、动作固定，位置请使用参数映射"));
            if (QushengCommandCatalog::usesMotionEditor(it.key()) && p.key() == "speedUmPerS" && !integer(p.value(), 1, 0xfffffe))
                return fail(error, "CONFIG_SDK_DEFAULT", QString::fromUtf8(u8"运动模板的SDK预置速度必须为1～16777214"));
            bool found = false;
            for (const auto &spec : definition.parameters) if (spec.key == p.key()) {
                found = integer(p.value(), spec.minimum, spec.maximum)
                    && (spec.allowedValues.isEmpty() || spec.allowedValues.contains(qint64(p.value().toDouble())));
                break;
            }
            if (!found) return fail(error, "CONFIG_SDK_DEFAULT", QString::fromUtf8(u8"SDK预置“%1”的参数“%2”无效").arg(it.key(), p.key()));
        }
        if (definition.support == QushengSdkSupport::Simple) {
            QushengSdkRequest checked; TranslationError sdkError;
            // 预置应在启用前检查保留位、通道及组合关系；必填业务值可由具体映射以后提供。
            if (!QushengSdk::buildRequest(quint8(sdkCode), QVariantMap(), preset.toVariantMap(),
                    root.value("device").toObject().toVariantMap(), &checked, &sdkError)
                    && sdkError.code != "SDK_PARAMETER_REQUIRED")
                return fail(error, "CONFIG_SDK_DEFAULT", QString::fromUtf8(u8"SDK预置“%1”无效：%2").arg(it.key(), sdkError.message));
        }
    }
    bool requiresLegacyDirection = false;
    for (const QJsonValue &value : mappings) {
        const QJsonObject mapping = value.toObject();
        const QString code = mapping.value("commandCode").toVariant().toString();
        if (code.isEmpty() || commandCodes.contains(code))
            return fail(error, "CONFIG_MAPPING", QString::fromUtf8(u8"客户命令码不能为空或重复"));
        commandCodes.insert(code);
        const QString operation = mapping.value("operation").toString();
        requiresLegacyDirection |= operation == "ABSOLUTE_MOVE";
        const bool relative = operation == "RELATIVE_FORWARD" || operation == "RELATIVE_BACKWARD";
        const QJsonObject catalogEntry = QushengCommandCatalog::find(operation);
        if (catalogEntry.isEmpty())
            return fail(error, "CONFIG_MAPPING", QString::fromUtf8(u8"目标指令不在趋盛协议目录中"));
        if (QushengCommandCatalog::isReceiveOnly(operation))
            return fail(error, "CONFIG_RECEIVE_ONLY", QString::fromUtf8(u8"“%1”是设备主动上报指令，只接收，不能创建下发映射").arg(catalogEntry.value("name").toString()));
        // 旧配置允许保留合法的响应字段以便加载；新版本不再用它控制显示时机。
        // 不把错误类型或未知值当作合法草稿，运行层统一在收到回告时反馈。
        if (mapping.contains("responsePolicy") && !QStringList({"ack", "complete"}).contains(mapping.value("responsePolicy").toString()))
            return fail(error, "CONFIG_MAPPING", QString::fromUtf8(u8"旧响应字段无效；请删除 responsePolicy"));
        if (!mapping.value("parameters").isObject())
            return fail(error, "CONFIG_MAPPING", QString::fromUtf8(u8"参数关系必须为对象；查询使用空对象"));
        const QJsonObject parameters = mapping.value("parameters").toObject();
        if (QushengCommandCatalog::usesMotionEditor(operation)
                && (parameters.size() != 1 || !parameters.contains(relative ? "distanceUm" : "positionUm")))
            return fail(error, "CONFIG_MAPPING", QString::fromUtf8(u8"运动只填写位置或距离；查询无参数且收到回告后返回"));
        // 每条映射只允许显式省略既有非命令字段，不能引入脚本或隐含的客户协议规则。
        if (mapping.contains("input") && !mapping.value("input").isObject())
            return fail(error, "CONFIG_MAPPING_INPUT", QString::fromUtf8(u8"命令输入设置必须为对象"));
        const QJsonObject input = mapping.value("input").toObject();
        QSet<QString> available = names, omitted;
        if (input.contains("omitFields") && !input.value("omitFields").isArray())
            return fail(error, "CONFIG_MAPPING_INPUT", QString::fromUtf8(u8"省略字段必须是字段名称列表"));
        for (const QJsonValue &omittedValue : input.value("omitFields").toArray()) {
            const QString omittedName = omittedValue.toString();
            if (!omittedValue.isString() || !available.contains(omittedName) || omitted.contains(omittedName))
                return fail(error, "CONFIG_MAPPING_INPUT", QString::fromUtf8(u8"命令映射“%1”的省略字段不存在或重复：%2").arg(code, omittedName));
            bool beforeCommand = true;
            for (const QJsonValue &f : fields) {
                const QJsonObject declaredField = f.toObject();
                if (declaredField.value("role").toString() == "command") {
                    if (declaredField.value("name").toString() == omittedName)
                        return fail(error, "CONFIG_MAPPING_INPUT", QString::fromUtf8(u8"不能省略命令码字段"));
                    beforeCommand = false;
                }
                // 定位命令必须有稳定前缀，首版不做可选地址或多套前缀布局，避免不同命令错位匹配。
                if (beforeCommand && declaredField.value("name").toString() == omittedName)
                    return fail(error, "CONFIG_MAPPING_INPUT", QString::fromUtf8(u8"命令码之前的字段不能按映射省略"));
            }
            omitted.insert(omittedName); available.remove(omittedName);
        }
        if (!input.isEmpty()) {
            bool mapped = false;
            for (const QJsonValue &f : fields) mapped |= f.toObject().value("lengthMode").toString() == "mappedCommand";
            if (!mapped) return fail(error, "CONFIG_MAPPING_INPUT", QString::fromUtf8(u8"省略字段需要将命令码长度方式设为“按命令码识别”"));
        }
        if (code.size() > 128) return fail(error, "CONFIG_MAPPING", QString::fromUtf8(u8"客户命令码超过 128 字符"));
        if (encoding == "ascii") {
            for (QChar ch : code)
                if (ch.unicode() < 0x21 || ch.unicode() > 0x7e || ch == '\\')
                    return fail(error, "CONFIG_MAPPING", QString::fromUtf8(u8"ASCII 客户命令码需为可见字符，不能含空格、控制符或转义"));
        }
        // 帧尾属于整份客户协议格式，不能让不可见的旧映射覆盖分帧边界。
        if (input.contains("footer"))
            return fail(error, "CONFIG_MAPPING_INPUT", QString::fromUtf8(u8"帧尾请在客户协议格式中设置；命令映射不再支持独立帧尾"));
        const QJsonObject effective = inputFormat(mapping);
        int minimumLength = headerBytes.size() + footerBytes.size();
        for (const QJsonValue &f : effective.value("fields").toArray()) {
            const QJsonObject effectiveField = f.toObject();
            const QString mode = effectiveField.value("lengthMode").toString();
            minimumLength += mode == "mappedCommand" ? code.size() : mode == "fixed" ? effectiveField.value("length").toInt() : 1;
        }
        if (minimumLength > 4096)
            return fail(error, "CONFIG_MAPPING_INPUT", QString::fromUtf8(u8"此命令的完整报文超过 4096 字节"));
        QSet<QString> numericParameters;
        for (const QJsonValue &fieldValue : fields) {
            const QJsonObject field = fieldValue.toObject();
            if (available.contains(field.value("name").toString())
                    && field.value("role").toString() == "parameter"
                    && field.value("type").toString() != "ascii")
                numericParameters.insert(field.value("name").toString());
        }
        // 待实现指令可以先保存部分参数关系；这只验证编辑意图，不能冒充完整发送配置。
        // 标量按自身表示范围检查，例如有符号PWM和32位固件长度，不能套用运动的24位位置范围。
        // 位结构、嵌套命令和可变字节串仍只读，首版不通过数值表达式伪造复杂协议布局。
        for (auto it = parameters.begin(); it != parameters.end(); ++it) {
            qint64 minimum = 0, maximum = 0xfffffe;
            QString parameterName = relative ? QString::fromUtf8(u8"移动距离") : QString::fromUtf8(u8"目标位置");
            if (!QushengCommandCatalog::usesMotionEditor(operation)) {
                QJsonObject parameterSpec;
                for (const QJsonValue &parameter : catalogEntry.value("parameters").toArray())
                    if (parameter.toObject().value("key").toString() == it.key()) {
                        parameterSpec = parameter.toObject(); break;
                    }
                if (parameterSpec.isEmpty() || !parameterSpec.value("editable").toBool())
                    return fail(error, "CONFIG_PARAMETER", QString::fromUtf8(u8"“%1”没有可编辑的数值参数“%2”").arg(catalogEntry.value("name").toString(), it.key()));
                minimum = qint64(parameterSpec.value("min").toDouble());
                maximum = qint64(parameterSpec.value("max").toDouble());
                parameterName = parameterSpec.value("label").toString();
                // 枚举取值不是连续范围：例如模式只允许1、2、4，不能把范围内的3保存为合法模式。
                // 客户字段引用在编辑时还没有具体值，只验证来源；发送未实现前绝不会据此组帧。
                const QJsonObject expression = it.value().toObject();
                if (expression.value("kind").toString() == "constant" && parameterSpec.contains("allowedValues")
                        && !parameterSpec.value("allowedValues").toArray().contains(expression.value("value")))
                    return fail(error, "CONFIG_EXPRESSION", QString::fromUtf8(u8"参数“%1”的固定值不在协议允许的枚举中").arg(parameterSpec.value("label").toString()));
            }
            const QJsonObject expression = it.value().toObject();
            const QString kind = expression.value("kind").toString();
            if (kind == "field" || kind == "linear") {
                const QString source = expression.value("field").toString();
                // 界面可追踪同一行字段的改名，但删除、类型变化或整体JSON替换不能猜替代字段。
                // 在配置层统一核对引用与当前字段定义，防止不可见失效引用进入真实组帧。
                if (!names.contains(source))
                    return fail(error, "CONFIG_EXPRESSION", QString::fromUtf8(u8"命令映射“%1”的参数“%2”找不到客户字段“%3”；请重新选择来源").arg(code, parameterName, source));
                if (!available.contains(source))
                    return fail(error, "CONFIG_EXPRESSION", QString::fromUtf8(u8"命令映射“%1”的参数“%2”引用了已省略的客户字段“%3”").arg(code, parameterName, source));
                if (!numericParameters.contains(source))
                    return fail(error, "CONFIG_EXPRESSION", QString::fromUtf8(u8"命令映射“%1”的参数“%2”来源“%3”必须是整数参数字段").arg(code, parameterName, source));
            }
            if (!it.value().isObject() || !validExpression(expression, numericParameters, minimum, maximum))
                return fail(error, "CONFIG_EXPRESSION", QString::fromUtf8(u8"命令映射“%1”的参数“%2”关系无效；请检查固定值范围或换算设置").arg(code, parameterName));
        }
        const int sdkCode = QushengCommandCatalog::commandCode(operation);
        if (sdkCode >= 0 && QushengSdk::isSupported(quint8(sdkCode)) && !QushengCommandCatalog::usesMotionEditor(operation)
                && operation != "MOTOR_STOP" && operation != "MOTOR_RESET" && operation != "QUSHENG_11") {
            const QVariantMap preset = sdkDefaults.value(operation).toObject().toVariantMap();
            for (const auto &spec : QushengSdk::definition(quint8(sdkCode)).parameters)
                if (spec.required && !parameters.contains(spec.key) && !preset.contains(spec.key))
                    return fail(error, "CONFIG_SDK_PARAMETER", QString::fromUtf8(u8"命令映射“%1”的SDK参数“%2”需明确提供").arg(code, spec.label));
            QVariantMap constants; bool dynamic = false;
            for (auto p = parameters.begin(); p != parameters.end(); ++p) {
                const QJsonObject expression = p.value().toObject();
                if (expression.value("kind").toString() == "constant") constants.insert(p.key(), expression.value("value").toVariant());
                else dynamic = true;
            }
            if (!dynamic) {
                QushengSdkRequest checked; TranslationError sdkError;
                if (!QushengSdk::buildRequest(quint8(sdkCode), constants, preset, root.value("device").toObject().toVariantMap(), &checked, &sdkError))
                    return fail(error, "CONFIG_SDK_PARAMETER", QString::fromUtf8(u8"命令映射“%1”的SDK参数无效：%2").arg(code, sdkError.message));
            }
        }
    }

    // 这些值直接参与真实设备组帧，所以即使仅预览也必须是合法数值；是否已经人工核对由界面单独确认。
    const QJsonObject device = root.value("device").toObject();
    if (requiresLegacyDirection && !integer(device.value("absoluteAction"), 1, 1)
            && !integer(device.value("absoluteAction"), 3, 3))
        return fail(error, "CONFIG_DEVICE", QString::fromUtf8(u8"旧版绝对映射需要保留方向值1或3；可在设备页面核对旧版方向"));
    if (!integer(device.value("destination"), 1, 255)
            || !integer(device.value("source"), 1, 255)
            || device.value("destination").toInt() == device.value("source").toInt()
            || !integer(device.value("devId"), 0x11, 0x1c)
            || !integer(device.value("speedUmPerS"), 1, 0xfffffe)
            || !integer(device.value("minPositionUm"), 0, 0xfffffe)
            || !integer(device.value("maxPositionUm"), 0, 0xfffffe)
            || device.value("minPositionUm").toInt() > device.value("maxPositionUm").toInt())
        return fail(error, "CONFIG_DEVICE", QString::fromUtf8(u8"下游地址、电机编号、默认速度或行程参数不完整"));
    return true;
}

bool ProtocolConfig::liveReady(TranslationError *error) const
{
    // 配置层只检查设备参数完整性。人工确认、显式启用和串口连接由界面在发送瞬间共同检查。
    return validate(error);
}

QJsonObject ProtocolConfig::inputFormat(const QJsonObject &mapping) const
{
    QJsonObject format = root.value("external").toObject();
    const QJsonObject input = mapping.value("input").toObject();
    // 只按映射省略字段；帧头、帧尾、编码始终来自共用格式。
    const QJsonArray omitted = input.value("omitFields").toArray();
    QJsonArray selected;
    for (const QJsonValue &field : format.value("fields").toArray())
        if (!omitted.contains(field.toObject().value("name"))) selected.append(field);
    format.insert("fields", selected);
    return format;
}
