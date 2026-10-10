#include "qushengcommandcatalog.h"

#include <QFile>
#include <QJsonDocument>
#include <QStringList>

QJsonArray QushengCommandCatalog::commands()
{
    // 数据随程序资源发布，离线可用。原文矛盾留在 note 中供核对，不用目录猜测设备行为。
    static const QJsonArray catalog = []() {
        QFile file(QStringLiteral(":/protocol/qusheng-commands.json"));
        if (!file.open(QIODevice::ReadOnly)) return QJsonArray();
        return QJsonDocument::fromJson(file.readAll()).object().value("commands").toArray();
    }();
    return catalog;
}

QJsonArray QushengCommandCatalog::options()
{
    QJsonArray result = commands();
    QJsonObject absolute;
    for (const QJsonValue &value : result)
        if (value.toObject().value("operation").toString() == "QUSHENG_11") {
            absolute = value.toObject(); break;
        }
    if (absolute.isEmpty()) return result;
    // 通用 0x11 条目覆盖所有模式/动作，仍是待实现状态；已验证预设使用自己确定的参数模型。
    // 不能将通用条目的 mode/action 参数偷偷交给绝对位置执行器，否则停止/复位可能被误发成运动。
    absolute.insert("operation", "ABSOLUTE_MOVE");
    absolute.insert("name", QString::fromUtf8(u8"绝对位置运动"));
    absolute.insert("description", QString::fromUtf8(u8"已验证的绝对位置预设；使用设备页面确认的电机编号、速度和方向，客户映射提供目标位置。"));
    absolute.insert("note", QString::fromUtf8(u8"只执行绝对位置模式；速度模式、停止、复位等请选择通用步进电机运动控制条目保存待实现映射。"));
    QJsonArray presetParameters;
    for (const QJsonValue &value : absolute.value("parameters").toArray()) {
        QJsonObject parameter = value.toObject();
        const QString key = parameter.value("key").toString();
        if (key == "mode" || key == "action") continue;
        if (key == "devId") parameter.insert("editable", false);
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
    // 三个预设同属一个 0x11 命令，不重复计算为新的协议命令；总计56目录条目和59选择项。
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

QJsonObject QushengCommandCatalog::find(const QString &operation)
{
    for (const QJsonValue &value : options())
        if (value.toObject().value("operation").toString() == operation) return value.toObject();
    return QJsonObject();
}

bool QushengCommandCatalog::isExecutable(const QString &operation)
{
    // 许可必须由代码决定。即使高级配置或目录 JSON 写入 enabled/executable，也不能开启发送。
    return QStringList({"ABSOLUTE_MOVE", "RELATIVE_FORWARD", "RELATIVE_BACKWARD",
                        "QUERY_MOTOR", "QUERY_DEVICE_INFO"}).contains(operation);
}

bool QushengCommandCatalog::isReceiveOnly(const QString &operation)
{
    // 主动上报具有独立报文布局，并不存在同命令码的主机请求，不能把它当查询下发。
    return find(operation).value("direction").toString() == "rx";
}

QString QushengCommandCatalog::name(const QString &operation)
{
    return find(operation).value("name").toString(operation);
}
