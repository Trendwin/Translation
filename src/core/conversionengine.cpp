#include "conversionengine.h"
#include "qusheng/qushengprotocol.h"
#include "qusheng/qushengcommandcatalog.h"
#include "qusheng/sdk/qushengsdk.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <climits>
#include <QRegularExpression>

namespace {
bool fail(TranslationError *error, const QString &code, const QString &message)
{
    if (error) *error = {code, message};
    return false;
}

QByteArray configuredBytes(const QJsonObject &external, const char *name)
{
    QByteArray bytes;
    // 这里只接收已校验配置，配置边界和界面输入共用一套严格转义规则。
    ProtocolConfig::decodeBytes(external.value(QLatin1String(name)).toString(),
                                external.value("encoding").toString(), &bytes, nullptr);
    return bytes;
}

QJsonObject mappingForInput(const ProtocolConfig &config, const QByteArray &input)
{
    const QJsonObject external = config.root.value("external").toObject();
    int cursor = configuredBytes(external, "header").size();
    for (const QJsonValue &value : external.value("fields").toArray()) {
        const QJsonObject field = value.toObject();
        if (field.value("lengthMode").toString() == "mappedCommand") {
            QJsonObject selected;
            int longest = 0;
            // 候选来自保存的配置。选择最长前缀让多字符命令不会被短命令吞掉；
            // 后续仍必须严格核对字段和帧尾，不能把含额外数据的查询当成功。
            for (const QJsonValue &candidate : config.root.value("mappings").toArray()) {
                const QJsonObject mapping = candidate.toObject();
                const QByteArray code = mapping.value("commandCode").toString().toLatin1();
                if (code.size() > longest && input.mid(cursor).startsWith(code)) {
                    selected = mapping; longest = code.size();
                }
            }
            return selected;
        }
        if (field.value("lengthMode").toString() != "fixed") break;
        cursor += field.value("length").toInt();
    }
    return QJsonObject();
}

quint64 readUnsigned(const QByteArray &bytes, bool littleEndian)
{
    quint64 result = 0;
    if (littleEndian) {
        for (int index = bytes.size() - 1; index >= 0; --index)
            result = (result << 8) | quint8(bytes.at(index));
    } else {
        for (char byte : bytes) result = (result << 8) | quint8(byte);
    }
    return result;
}

bool evaluate(const QJsonObject &expression, const QMap<QString, QVariant> &fields,
              QVariant *output, QString *trace, TranslationError *error)
{
    const QString kind = expression.value("kind").toString();
    if (kind == "constant") {
        *output = expression.value("value").toVariant();
        if (trace) *trace = QString::fromUtf8(u8"固定值 %1").arg(output->toString());
        return true;
    }
    const QString field = expression.value("field").toString();
    if (!fields.contains(field))
        return fail(error, "MISSING_FIELD", QString::fromUtf8(u8"参数关系引用了不存在的字段“%1”").arg(field));
    if (kind == "field") {
        *output = fields.value(field);
        if (trace) *trace = QString::fromUtf8(u8"直接引用 %1 = %2").arg(field, output->toString());
        return true;
    }
    if (kind != "linear")
        return fail(error, "EXPRESSION_UNSUPPORTED", QString::fromUtf8(u8"第一阶段不支持此参数关系"));

    bool ok = false;
    const qint64 input = fields.value(field).toLongLong(&ok);
    if (!ok) return fail(error, "CONVERSION_TYPE", QString::fromUtf8(u8"字段“%1”不是可换算的整数").arg(field));
    const qint64 numerator = qint64(expression.value("numerator").toDouble());
    const qint64 denominator = qint64(expression.value("denominator").toDouble());
    const qint64 offset = qint64(expression.value("offset").toDouble());
    // 分子可为负数或零。先用其绝对值检查乘法，再检查取负的最小整数边界。
    const qint64 magnitude = qAbs(numerator);
    if (denominator <= 0 || (magnitude > 0
            && (input > LLONG_MAX / magnitude || input < LLONG_MIN / magnitude))
            || (numerator < 0 && input * magnitude == LLONG_MIN))
        return fail(error, "CONVERSION_OVERFLOW", QString::fromUtf8(u8"线性换算发生整数溢出"));
    const qint64 scaled = input * numerator;
    const qint64 remainder = scaled % denominator;
    const QString rounding = expression.value("round").toString();
    if (rounding == "exact" && remainder != 0)
        return fail(error, "CONVERSION_FRACTION", QString::fromUtf8(u8"精确换算产生了小数，已拒绝下发"));
    qint64 converted = scaled / denominator;
    if (rounding == "floor" && remainder < 0) --converted;
    if (rounding == "ceil" && remainder > 0) ++converted;
    if (rounding == "nearest" && qAbs(remainder) * 2 >= denominator)
        converted += scaled >= 0 ? 1 : -1;
    if ((offset > 0 && converted > LLONG_MAX - offset)
            || (offset < 0 && converted < LLONG_MIN - offset))
        return fail(error, "CONVERSION_OVERFLOW", QString::fromUtf8(u8"线性换算偏移发生整数溢出"));
    converted += offset;
    *output = converted;
    if (trace) {
        const QMap<QString, QString> roundNames = {{"exact", QString::fromUtf8(u8"必须整除")},
            {"floor", QString::fromUtf8(u8"向下取整")}, {"ceil", QString::fromUtf8(u8"向上取整")},
            {"nearest", QString::fromUtf8(u8"四舍五入")}};
        *trace = QString::fromUtf8(u8"%1(%2) × %3 ÷ %4 + %5 = %6，舍入：%7")
                .arg(field).arg(input).arg(numerator).arg(denominator).arg(offset)
                .arg(converted, 0, 10).arg(roundNames.value(rounding));
    }
    return true;
}
}

ConversionEngine::ConversionEngine(const ProtocolConfig &config) : m_config(config) {}

bool ConversionEngine::parse(const QByteArray &input, QMap<QString, QVariant> *fields,
                             TranslationError *error) const
{
    fields->clear();
    if (input.size() > 4096)
        return fail(error, "INPUT_OVERFLOW", QString::fromUtf8(u8"完整输入报文超过 4096 字节上限"));
    const QJsonObject matchedInput = mappingForInput(m_config, input);
    const QJsonObject external = m_config.inputFormat(matchedInput);
    const QByteArray header = configuredBytes(external, "header");
    const QByteArray footer = configuredBytes(external, "footer");
    if (!input.startsWith(header))
        return fail(error, "FRAME_HEADER", QString::fromUtf8(u8"输入报文缺少配置的帧头"));
    if (!input.endsWith(footer))
        return fail(error, "FRAME_FOOTER", QString::fromUtf8(u8"输入报文缺少配置的帧尾"));
    if (input.size() < header.size() + footer.size())
        return fail(error, "FRAME_LENGTH", QString::fromUtf8(u8"输入报文长度小于帧头与帧尾"));

    // 字段只在去掉帧头、帧尾后的有效载荷中按表格顺序推进，不允许 offset 穿透到普通配置。
    const QByteArray payload = input.mid(header.size(), input.size() - header.size() - footer.size());
    int cursor = 0;
    const bool asciiEncoding = external.value("encoding").toString() == "ascii";
    const bool littleEndian = external.value("byteOrder").toString("big") == "little";
    for (const QJsonValue &value : external.value("fields").toArray()) {
        const QJsonObject field = value.toObject();
        const QString name = field.value("name").toString();
        int length = field.value("lengthMode").toString() == "toFooter"
                ? payload.size() - cursor : field.value("length").toInt();
        if (field.value("lengthMode").toString() == "mappedCommand") {
            if (matchedInput.isEmpty())
                return fail(error, "COMMAND_UNSUPPORTED", QString::fromUtf8(u8"输入命令没有匹配的命令映射"));
            length = matchedInput.value("commandCode").toString().toLatin1().size();
        }
        if (length <= 0 || cursor + length > payload.size())
            return fail(error, "FIELD_LENGTH", QString::fromUtf8(u8"字段“%1”的实际长度不符合配置").arg(name));
        const QByteArray raw = payload.mid(cursor, length);
        cursor += length;

        QVariant parsed;
        const QString type = field.value("type").toString();
        if (type == "ascii") {
            for (char byte : raw)
                if (quint8(byte) > 127)
                    return fail(error, "FIELD_ASCII", QString::fromUtf8(u8"字段“%1”包含非 ASCII 字节").arg(name));
            parsed = QString::fromLatin1(raw);
        } else if (asciiEncoding) {
            // ASCII 编码下，整数也是十进制字符；这让字段表能描述多种客户文本协议。
            const QString text = QString::fromLatin1(raw);
            const QRegularExpression pattern(type == "uint" ? QStringLiteral("^[0-9]+$")
                                                              : QStringLiteral("^-?[0-9]+$"));
            bool ok = false;
            const qlonglong number = text.toLongLong(&ok, 10);
            const QRegularExpressionMatch match = pattern.match(text);
            if (!ok || !match.hasMatch() || match.capturedLength() != text.size())
                return fail(error, "FIELD_NUMBER", QString::fromUtf8(u8"字段“%1”不是有效十进制整数").arg(name));
            if (type == "uint" && number < 0)
                return fail(error, "FIELD_RANGE", QString::fromUtf8(u8"无符号字段“%1”不能为负数").arg(name));
            parsed = number;
        } else {
            // HEX 输入中的多字节整数才使用高级字节序。超过 8 字节会溢出统一整数模型。
            if (raw.size() > 8)
                return fail(error, "FIELD_NUMBER", QString::fromUtf8(u8"整数字段“%1”超过 8 字节").arg(name));
            const quint64 unsignedValue = readUnsigned(raw, littleEndian);
            if (type == "uint") {
                // 统一参数模型使用有符号 64 位整数；拒绝超出范围，避免随后换算时回绕。
                if (unsignedValue > quint64(LLONG_MAX))
                    return fail(error, "FIELD_NUMBER", QString::fromUtf8(u8"无符号字段“%1”超过统一整数范围").arg(name));
                parsed = qint64(unsignedValue);
            } else {
                const int bits = raw.size() * 8;
                qint64 signedValue = qint64(unsignedValue);
                if (bits < 64 && (unsignedValue & (quint64(1) << (bits - 1))))
                    signedValue = qint64(unsignedValue | (~quint64(0) << bits));
                parsed = signedValue;
            }
        }
        fields->insert(name, parsed);

        const bool constantMismatch = type == "ascii"
                ? parsed.toString() != field.value("fixedValue").toVariant().toString()
                : parsed.toLongLong() != field.value("fixedValue").toVariant().toLongLong();
        if (field.contains("fixedValue") && constantMismatch)
            return fail(error, "FIELD_CONSTANT", QString::fromUtf8(u8"固定字段“%1”的值不匹配").arg(name));
        if (field.contains("min") && parsed.toDouble() < field.value("min").toDouble())
            return fail(error, "FIELD_RANGE", QString::fromUtf8(u8"字段“%1”低于最小值").arg(name));
        if (field.contains("max") && parsed.toDouble() > field.value("max").toDouble())
            return fail(error, "FIELD_RANGE", QString::fromUtf8(u8"字段“%1”高于最大值").arg(name));
    }
    if (cursor != payload.size())
        return fail(error, "FIELD_LENGTH", QString::fromUtf8(u8"字段总长度与报文有效载荷不一致"));
    return true;
}

bool ConversionEngine::map(const QMap<QString, QVariant> &fields, UnifiedCommand *command,
                           ConversionPreview *preview, TranslationError *error) const
{
    QString commandField;
    for (const QJsonValue &value : m_config.root.value("external").toObject().value("fields").toArray()) {
        const QJsonObject field = value.toObject();
        if (field.value("role").toString() == "command") {
            commandField = field.value("name").toString();
            break;
        }
    }
    if (commandField.isEmpty() || !fields.contains(commandField))
        return fail(error, "MISSING_COMMAND", QString::fromUtf8(u8"没有解析出命令码字段"));
    const QString incomingCode = fields.value(commandField).toString();

    QJsonObject matched;
    for (const QJsonValue &value : m_config.root.value("mappings").toArray()) {
        const QJsonObject candidate = value.toObject();
        if (candidate.value("commandCode").toVariant().toString() != incomingCode) continue;
        if (!matched.isEmpty())
            return fail(error, "MAPPING_AMBIGUOUS", QString::fromUtf8(u8"同一客户命令码命中了多条映射"));
        matched = candidate;
    }
    if (matched.isEmpty())
        return fail(error, "COMMAND_UNSUPPORTED",
                    QString::fromUtf8(u8"客户命令码“%1”没有匹配的命令映射").arg(incomingCode));

    // 目录完整不等于执行完整。按命中的单条映射阻止待实现指令，避免把其参数误套成运动位置；
    // 同一配置中的已验证运动/查询仍可正常使用。拒绝点必须在组帧之前，不能生成可误发的HEX。
    const QString selectedOperation = matched.value("operation").toString();
    if (!QushengCommandCatalog::isExecutable(selectedOperation)) {
        const bool layoutPending = QushengCommandCatalog::find(selectedOperation).value("layoutPending").toBool();
        return fail(error, "COMMAND_NOT_IMPLEMENTED", layoutPending
            ? QString::fromUtf8(u8"“%1”的协议布局待确认，不生成目标帧").arg(QushengCommandCatalog::name(selectedOperation))
            : QString::fromUtf8(u8"“%1”已保存映射，软件发送待实现或待确认，不生成目标帧").arg(QushengCommandCatalog::name(selectedOperation)));
    }

    const QJsonObject device = m_config.root.value("device").toObject();
    command->configVersion = m_config.version;
    command->targetDevice = QString::number(device.value("devId").toInt());
    command->operation = matched.value("operation").toString();
    const bool query = QushengCommandCatalog::find(command->operation).value("query").toBool()
        || command->operation.startsWith("QUERY_");
    const bool relative = command->operation == "RELATIVE_FORWARD" || command->operation == "RELATIVE_BACKWARD";
    command->action = query ? ActionType::Query : ActionType::Execute;
    // 回告立即反馈；旧配置中的 complete 不再延迟显示，也不改变运动完成判断。
    command->responsePolicy = QStringLiteral("ack");
    command->parameters.clear();
    command->parameterUnits.clear();
    preview->matchedCommandCode = incomingCode;
    preview->parameterTrace.clear();

    const QJsonObject parameters = matched.value("parameters").toObject();
    for (auto it = parameters.begin(); it != parameters.end(); ++it) {
        QVariant output;
        QString trace;
        if (!evaluate(it.value().toObject(), fields, &output, &trace, error)) return false;
        command->parameters.insert(it.key(), output);
        preview->parameterTrace.insert(it.key(), trace);
    }

    if (!QushengCommandCatalog::usesMotionEditor(command->operation)) {
        const int code = QushengCommandCatalog::commandCode(command->operation);
        const QVariantMap defaults = m_config.root.value("sdkDefaults").toObject().value(command->operation).toObject().toVariantMap();
        QushengSdkRequest sdkRequest;
        if (!QushengSdk::buildRequest(quint8(code), command->parameters, defaults, device.toVariantMap(), &sdkRequest, error)) return false;
        command->parameters = sdkRequest.parameters;
        if (command->parameters.contains("devId")) command->targetDevice = command->parameters.value("devId").toString();
        for (auto it = sdkRequest.parameterSources.begin(); it != sdkRequest.parameterSources.end(); ++it) {
            QString label = it.key();
            for (const auto &spec : QushengSdk::definition(quint8(code)).parameters)
                if (spec.key == it.key()) { label = spec.label; break; }
            if (!preview->parameterTrace.contains(it.key())) preview->parameterTrace.insert(it.key(),
                QString::fromUtf8(u8"%1 = %2（%3）").arg(label, command->parameters.value(it.key()).toString(), it.value()));
        }
        return true;
    }
    // 客户输入是否带单位只由配置的表达式决定，本轮示例直接引用数值。
    // 参数名保留趋盛线上的单位约定；没有配置换算时不擅自乘以客户设备的步距。
    bool positionOk = false;
    const QString positionKey = relative ? QStringLiteral("distanceUm") : QStringLiteral("positionUm");
    const qlonglong positionUm = command->parameters.value(positionKey).toLongLong(&positionOk);
    if (!positionOk || (relative ? (positionUm < 0 || positionUm > device.value("maxPositionUm").toInt() - device.value("minPositionUm").toInt())
                                : (positionUm < device.value("minPositionUm").toInt() || positionUm > device.value("maxPositionUm").toInt())))
        return fail(error, "POSITION_LIMIT", QString::fromUtf8(u8"换算后的位置超出已配置设备行程"));
    if (relative) {
        // 相对动作的最终位置依赖实机当前值，预览只能检查距离；真实发送由请求管理器先查询再核对终点。
        command->parameters.insert("minPositionUm", device.value("minPositionUm").toInt());
        command->parameters.insert("maxPositionUm", device.value("maxPositionUm").toInt());
    }
    // 新建前进/后退模板各自固定动作1/3；设备默认方向只供旧ABS配置使用，不能覆盖新模板动作。
    const int fixedAction = QushengCommandCatalog::fixedAction(command->operation);
    const int action = fixedAction ? fixedAction : relative ? (command->operation == "RELATIVE_FORWARD" ? 1 : 3)
                                                        : device.value("absoluteAction").toInt();
    QushengSdkRequest motion;
    const QVariantMap supplied{{"positionUm", positionUm}, {"mode", relative ? 2 : 4}, {"action", action}};
    const QVariantMap defaults = m_config.root.value("sdkDefaults").toObject().value(command->operation).toObject().toVariantMap();
    if (!QushengSdk::buildRequest(0x11, supplied, defaults, device.toVariantMap(), &motion, error)) return false;
    command->targetDevice = motion.parameters.value("devId").toString();
    command->parameters.insert("speedUmPerS", motion.parameters.value("speedUmPerS"));
    command->parameters.insert("absoluteAction", action);
    preview->parameterTrace.insert("devId", QString::fromUtf8(u8"电机编号 = %1（%2）")
                                  .arg(command->targetDevice, motion.parameterSources.value("devId")));
    preview->parameterTrace.insert("speedUmPerS", QString::fromUtf8(u8"速度 = %1（%2）")
                                  .arg(command->parameters.value("speedUmPerS").toString(), motion.parameterSources.value("speedUmPerS")));
    command->parameterUnits.insert(positionKey, QStringLiteral("um"));
    command->parameterUnits.insert("speedUmPerS", QStringLiteral("um/s"));
    return true;
}

bool ConversionEngine::convert(const QByteArray &input, bool live, ConversionPreview *out,
                               TranslationError *error) const
{
    Q_UNUSED(live);
    // 失败必须清空旧结果。调用方不能误把上一条合法报文的目标帧当成本次转换结果。
    if (out) *out = ConversionPreview();
    if (error) *error = TranslationError();
    if (!m_config.validate(error)) return false;
    ConversionPreview result;
    result.input = input;
    if (!parse(input, &result.fields, error)
            || !map(result.fields, &result.command, &result, error))
        return false;
    for (const QJsonValue &field : m_config.root.value("external").toObject().value("fields").toArray()) {
        const QString name = field.toObject().value("name").toString();
        if (result.fields.contains(name)) result.fieldOrder << name;
    }

    const QJsonObject device = m_config.root.value("device").toObject();
    QushengProtocol codec;
    codec.setAddresses(quint8(device.value("destination").toInt()),
                       quint8(device.value("source").toInt()));
    if (!codec.encodeCommand(result.command, &result.targetFrame, error)) return false;
    if (out) *out = result;
    return true;
}

QList<QByteArray> ConversionEngine::feed(const QByteArray &chunk, QList<TranslationError> *errors)
{
    QList<QByteArray> frames;
    TranslationError configError;
    if (!m_config.validate(&configError)) {
        m_buffer.clear();
        if (errors) errors->append(configError);
        return frames;
    }
    m_buffer.append(chunk);
    const QJsonObject external = m_config.root.value("external").toObject();
    const QByteArray header = configuredBytes(external, "header");
    if (m_buffer.size() > 4096) {
        m_buffer.clear();
        if (errors) errors->append({"INPUT_OVERFLOW", QString::fromUtf8(u8"输入缓存超过 4096 字节，已清空")});
        return frames;
    }

    // 通用分帧仅依赖配置边界：保留可能被拆开的帧头，丢弃帧头前噪声，并逐个取出粘连帧。
    for (;;) {
        const int start = m_buffer.indexOf(header);
        if (start < 0) {
            m_buffer = m_buffer.right(qMax(0, header.size() - 1));
            break;
        }
        if (start > 0) m_buffer.remove(0, start);
        const QJsonObject selected = mappingForInput(m_config, m_buffer);
        const QJsonObject format = m_config.inputFormat(selected);
        const QByteArray footer = configuredBytes(format, "footer");
        int fixedPayloadLength = 0;
        bool hasTailField = false;
        for (const QJsonValue &value : format.value("fields").toArray()) {
            const QJsonObject field = value.toObject();
            const QString mode = field.value("lengthMode").toString();
            if (mode == "toFooter") hasTailField = true;
            else if (mode == "mappedCommand") {
                fixedPayloadLength += selected.value("commandCode").toString().size();
                if (selected.isEmpty()) hasTailField = true;
            } else fixedPayloadLength += field.value("length").toInt();
        }
        // 全部固定长度时按配置长度定位帧尾，HEX 参数中的同值字节不能被当成结束符。
        // 读取到帧尾则从固定字段之后搜索；此模式要求尾字段不含未转义的完整帧尾。
        const int end = hasTailField ? m_buffer.indexOf(footer, header.size() + fixedPayloadLength)
                                    : header.size() + fixedPayloadLength;
        if (end < 0 || m_buffer.size() < end + footer.size()) break;
        const int frameSize = end + footer.size();
        const QByteArray candidate = m_buffer.left(frameSize);
        m_buffer.remove(0, frameSize);
        QMap<QString, QVariant> fields;
        TranslationError parseError;
        if (parse(candidate, &fields, &parseError)) frames.append(candidate);
        else if (errors) errors->append(parseError);
    }
    return frames;
}
