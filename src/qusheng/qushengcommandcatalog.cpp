#include "qushengcommandcatalog.h"
#include "sdk/qushengsdk.h"

#include <QFile>
#include <QJsonDocument>
#include <QStringList>

QJsonArray QushengCommandCatalog::commands()
{
    // 数据随程序资源发布，离线可用。原文矛盾留在 note 中供核对，不用目录猜测设备行为。
    static const QJsonArray catalog = []() {
        QFile file(QStringLiteral(":/protocol/qusheng-commands.json"));
        if (!file.open(QIODevice::ReadOnly)) return QJsonArray();
        QJsonArray entries = QJsonDocument::fromJson(file.readAll()).object().value("commands").toArray();
        for (int i = 0; i < entries.size(); ++i) {
            QJsonObject entry = entries.at(i).toObject();
            bool ok = false; const int code = entry.value("code").toString().mid(2).toInt(&ok, 16);
            if (!ok) continue;
            const QushengCommandDefinition definition = QushengSdk::definition(quint8(code));
            entry.insert("query", definition.query);
            entry.insert("sdkSupported", definition.support == QushengSdkSupport::Simple);
            entry.insert("layoutPending", definition.support == QushengSdkSupport::LayoutPending);
            if (definition.support == QushengSdkSupport::Simple) {
                if (code == 0xc1) entry.insert("name", definition.name);
                // 参数类型、默认值、能力均来自SDK；目录保留名称/章节，不再另存可执行白名单。
                QJsonArray parameters;
                for (const QushengParameterDefinition &spec : definition.parameters) {
                    QJsonObject parameter{{"key", spec.key}, {"label", spec.label}, {"type", spec.type},
                        {"min", double(spec.minimum)}, {"max", double(spec.maximum)}, {"editable", true},
                        {"required", spec.required}, {"optional", !spec.required}, {"deviceKey", spec.deviceKey},
                        {"description", QString::fromUtf8(u8"%1；数值范围%2～%3。未映射时由SDK补齐，必填业务值必须明确设置。")
                         .arg(spec.label).arg(spec.minimum).arg(spec.maximum)}};
                    if (!spec.deviceKey.isEmpty()) parameter.insert("defaultSource", "device." + spec.deviceKey);
                    if (spec.defaultValue.isValid()) parameter.insert("default", QJsonValue::fromVariant(spec.defaultValue));
                    if (code == 0x21 && spec.key == "mode")
                        parameter.insert("description", QString::fromUtf8(u8"0关闭选中、1开启选中，其余不变；2按位覆盖全部，未选阀关闭。"));
                    if (code == 0x21 && spec.key == "valveMask")
                        parameter.insert("description", QString::fromUtf8(u8"阀1为bit0，阀30为bit29；可在阀选择区勾选自动生成，也保留客户字段和原始数值来源。"));
                    if ((code == 0x21 || code == 0x22) && spec.key == "duration")
                        parameter.insert("description", QString::fromUtf8(u8"4字节大端开启时间。0使用超时配置，不表示关闭；关闭选中时保留配置值，不自动删改来源。"));
                    if (code == 0x22 && spec.key == "devId")
                        parameter.insert("description", QString::fromUtf8(u8"1～30号阀对应0x31～0x4E，0x22只开启；关闭使用0x21选择目标阀。"));
                    if (code == 0xc1 && (spec.key == "moduleType" || spec.key.endsWith("Speed")))
                        parameter.insert("editable", false);
                    QJsonArray allowed; for (qint64 value : spec.allowedValues) allowed.append(double(value));
                    if (!allowed.isEmpty()) parameter.insert("allowedValues", allowed);
                    parameters.append(parameter);
                }
                entry.insert("parameters", parameters);
                const QString oldNote = entry.value("note").toString();
                if (oldNote.isEmpty() || oldNote.contains(QString::fromUtf8(u8"尚未实现")))
                    entry.insert("note", QString::fromUtf8(u8"单条SDK接口；未映射参数使用预置、设备参数或软件初始值。"));
                if (code == 0xc1) {
                    entry.insert("description", QString::fromUtf8(u8"仅 XYZ 模块：设置虚拟模块编号与三轴位置，速度沿用设备页面默认运动速度；单位由下位机配置决定，不自动换算。"));
                    entry.insert("note", QString::fromUtf8(u8"只开放模块类型1；阀泵类型2及C2/C3尚未开放。物理电机绑定须在下位机预先配置；应答不表示机械动作完成。"));
                }
            }
            entries[i] = entry;
        }
        return entries;
    }();
    return catalog;
}

static QJsonArray legacyOptions()
{
    QFile file(QStringLiteral(":/protocol/qusheng-commands.json"));
    if (!file.open(QIODevice::ReadOnly)) return QJsonArray();
    QJsonObject absolute = QJsonDocument::fromJson(file.readAll()).object().value("legacyMotorControl").toObject();
    QJsonArray result;
    result.append(absolute);
    if (absolute.isEmpty()) return result;
    // 通用 0x11 条目覆盖所有模式/动作，仍是待实现状态；已验证预设使用自己确定的参数模型。
    // 不能将通用条目的 mode/action 参数偷偷交给绝对位置执行器，否则停止/复位可能被误发成运动。
    absolute.insert("operation", "ABSOLUTE_MOVE");
    absolute.insert("name", QString::fromUtf8(u8"绝对位置运动"));
    absolute.insert("description", QString::fromUtf8(u8"旧绝对位置预设；电机可按本条映射设置，未设置时沿用默认；速度和方向仍由设备默认提供，客户映射提供目标位置。"));
    absolute.insert("note", QString::fromUtf8(u8"仅用于兼容旧绝对映射；新建请选前进、停止、后退或复位，四动作均已开放发送。"));
    QJsonArray presetParameters;
    for (const QJsonValue &value : absolute.value("parameters").toArray()) {
        QJsonObject parameter = value.toObject();
        const QString key = parameter.value("key").toString();
        if (key == "mode" || key == "action") continue;
        if (key == "devId") parameter.insert("editable", true);
        else if (key == "speed") {
            parameter.insert("key", "speedUmPerS");
            parameter.insert("editable", false);
            parameter.insert("description", QString::fromUtf8(u8"D2～D4；由设备与连接页面确认的速度数值提供。"));
        } else if (key == "positionOrDistance") {
            parameter.insert("key", "positionUm");
            parameter.insert("label", QString::fromUtf8(u8"目标位置"));
            parameter.insert("description", QString::fromUtf8(u8"D5～D7；目标位置数值，由客户字段或固定值映射；本轮不推断客户单位。"));
            parameter.insert("max", 0xfffffe);
        }
        presetParameters.append(parameter);
    }
    presetParameters.append(QJsonObject{{"key", "modeAndAction"},
                           {"label", QString::fromUtf8(u8"模式与动作")}, {"type", "structure"},
                           {"description", QString::fromUtf8(u8"D1高四位固定模式4；低四位方向由设备确认项提供，只支持前进或后退。")},
                           {"editable", false}, {"optional", false}});
    absolute.insert("parameters", presetParameters);
    result.append(absolute);
    // 三个旧运动模板仅用于加载既有配置，不计入新建选择池；不能自动改变旧相对参数含义。
    for (const QString &operation : {QStringLiteral("RELATIVE_FORWARD"), QStringLiteral("RELATIVE_BACKWARD")}) {
        QJsonObject entry = absolute;
        entry.insert("operation", operation);
        entry.insert("name", operation == "RELATIVE_FORWARD"
                     ? QString::fromUtf8(u8"相对前进") : QString::fromUtf8(u8"相对后退"));
        entry.insert("description", QString::fromUtf8(u8"步进位置模式；真实发送前先查询当前位置，核对移动后的终点。方向由该预设确定。"));
        entry.insert("note", QString::fromUtf8(u8"已验证的相对位置模式预设；不是通用速度模式、停止或复位指令。"));
        QJsonArray parameters = entry.value("parameters").toArray();
        for (int i = 0; i < parameters.size(); ++i) {
            QJsonObject parameter = parameters.at(i).toObject();
            if (parameter.value("key").toString() == "positionUm") {
                parameter.insert("key", "distanceUm");
                parameter.insert("label", QString::fromUtf8(u8"移动距离"));
                parameter.insert("description", QString::fromUtf8(u8"D5～D7，移动距离数值；本轮直接按映射后的数值使用，终点还须处于已确认的行程范围内。"));
                parameters.replace(i, parameter);
            } else if (parameter.value("key").toString() == "modeAndAction") {
                parameter.insert("description", QString::fromUtf8(u8"D1高四位固定模式2；低四位由前进或后退预设确定。"));
                parameters.replace(i, parameter);
            }
        }
        entry.insert("parameters", parameters);
        result.append(entry);
    }
    return result;
}

QJsonArray QushengCommandCatalog::options()
{
    QJsonArray result;
    QJsonObject motor;
    for (const QJsonValue &value : commands()) {
        const QJsonObject entry = value.toObject();
        if (entry.value("operation").toString() == "QUSHENG_11") motor = entry;
        else result.append(entry);
    }
    // 四动作共享绝对模式；前进/后退映射目标，停止/复位固定零距离并使用独立完成条件。
    const QStringList operations = {"MOTOR_FORWARD", "MOTOR_STOP", "MOTOR_BACKWARD", "MOTOR_RESET"};
    const QStringList labels = {QString::fromUtf8(u8"步进电机前进"), QString::fromUtf8(u8"步进电机停止"),
                               QString::fromUtf8(u8"步进电机后退"), QString::fromUtf8(u8"步进电机复位")};
    for (int index = 0; index < operations.size(); ++index) {
        QJsonObject variant = motor;
        variant.insert("operation", operations.at(index));
        variant.insert("name", labels.at(index));
        QJsonArray parameters;
        for (const QJsonValue &value : motor.value("parameters").toArray()) {
            QJsonObject parameter = value.toObject();
            const QString key = parameter.value("key").toString();
            if (key == "speedUmPerS" || key == "mode" || key == "action") parameter.insert("editable", false);
            if (key == "positionUm") {
                parameter.insert("max", 0xfffffe);
                if (index == 1 || index == 3) {
                    parameter.insert("editable", false); parameter.insert("fixedValue", 0);
                    parameter.insert("required", false);
                }
            }
            if (key == "mode") parameter.insert("fixedValue", 4);
            if (key == "action") {
                parameter.insert("fixedValue", index + 1);
                parameter.insert("description", QString::fromUtf8(u8"D1低四位；由所选动作固定为%1，不能由客户参数覆盖。").arg(index + 1));
            }
            parameters.append(parameter);
        }
        variant.insert("parameters", parameters);
        variant.insert("description", index == 1 || index == 3
                       ? QString::fromUtf8(u8"固定绝对位置模式和零距离；电机可按本条映射设置，未设置时沿用默认；速度来自设备参数或SDK预置，无需客户距离参数。")
                       : QString::fromUtf8(u8"固定绝对位置模式；本条映射可选择电机并映射绝对目标位置，速度沿用默认。"));
        variant.insert("note", index == 0 || index == 2
                       ? QString::fromUtf8(u8"移动距离按绝对目标位置解释，不是当前位置加减距离；有效应答结束本地等待，机械动作完成未确认，不自动查询。")
                       : QString::fromUtf8(u8"距离固定为0，不要求客户距离字段；有效应答结束本地等待，机械动作完成未确认，需要时手动查询。"));
        result.append(variant);
    }
    return result;
}

QJsonObject QushengCommandCatalog::find(const QString &operation)
{
    for (const QJsonValue &value : options())
        if (value.toObject().value("operation").toString() == operation) return value.toObject();
    // 旧配置按原有单位、相对/绝对语义继续加载；不把旧相对距离静默改成绝对目标。
    if (isLegacyOperation(operation))
        for (const QJsonValue &value : legacyOptions())
            if (value.toObject().value("operation").toString() == operation) return value.toObject();
    return QJsonObject();
}

bool QushengCommandCatalog::isExecutable(const QString &operation)
{
    // 旧通用0x11不能覆盖固定模板模式/动作；四动作已各自接入发送与状态处理。
    if (operation == "QUSHENG_11") return false;
    const int code = commandCode(operation);
    return code >= 0 && QushengSdk::isSupported(quint8(code));
}

bool QushengCommandCatalog::usesMotionEditor(const QString &operation)
{
    return isAbsoluteMotion(operation) || operation == "RELATIVE_FORWARD" || operation == "RELATIVE_BACKWARD";
}

bool QushengCommandCatalog::isStopOrReset(const QString &operation)
{ return operation == "MOTOR_STOP" || operation == "MOTOR_RESET"; }

int QushengCommandCatalog::commandCode(const QString &operation)
{
    if (usesMotionEditor(operation) || operation == "MOTOR_STOP" || operation == "MOTOR_RESET" || operation == "QUSHENG_11") return 0x11;
    bool ok = false;
    const int code = find(operation).value("code").toString().mid(2).toInt(&ok, 16);
    return ok ? code : -1;
}

bool QushengCommandCatalog::isAbsoluteMotion(const QString &operation)
{ return operation == "ABSOLUTE_MOVE" || operation == "MOTOR_FORWARD" || operation == "MOTOR_BACKWARD"; }

int QushengCommandCatalog::fixedAction(const QString &operation)
{
    const QStringList operations = {"MOTOR_FORWARD", "MOTOR_STOP", "MOTOR_BACKWARD", "MOTOR_RESET"};
    return operations.indexOf(operation) + 1;
}

bool QushengCommandCatalog::isLegacyOperation(const QString &operation)
{ return QStringList({"ABSOLUTE_MOVE", "RELATIVE_FORWARD", "RELATIVE_BACKWARD", "QUSHENG_11"}).contains(operation); }

bool QushengCommandCatalog::isReceiveOnly(const QString &operation)
{
    // 主动上报具有独立报文布局，并不存在同命令码的主机请求，不能把它当查询下发。
    return find(operation).value("direction").toString() == "rx";
}

QString QushengCommandCatalog::name(const QString &operation)
{
    return find(operation).value("name").toString(operation);
}
