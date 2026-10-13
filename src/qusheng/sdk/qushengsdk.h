#ifndef QUSHENGSDK_H
#define QUSHENGSDK_H

#include "model/translationtypes.h"
#include <QList>
#include <QVariantMap>

// SDK仅依赖QtCore：不读取客户字段、配置JSON、串口或界面。业务层先完成字段映射，
// 再提交趋盛参数；同一请求保存已补齐的参数快照，不能在发送时重新读取可变默认值。
struct QushengParameterDefinition {
    // key是配置和调用方使用的稳定名称；label只用于中文显示，不能作为映射键。
    // type描述线上数据类型（如uint24），不是客户输入字段的编码方式。
    QString key, label, type;
    // 数值边界包含端点；统一用qint64表达，以容纳uint32及有符号控制值。
    qint64 minimum = 0, maximum = 0;
    // 无效QVariant表示不提供软件初始值，不能把它理解成0。
    // 软件初始值用于补齐调用参数，不代表厂家出厂设置。
    QVariant defaultValue;
    // 非空时允许从设备参数中取值，如步进速度；其他模块不会借用步进编号。
    QString deviceKey;
    // 必填标记供目录/表单提示使用；buildRequest最终检查补齐后是否仍缺值。
    bool required = false;
    // 非空时还须命中枚举/离散通道，单纯落在minimum/maximum之间并不足够。
    QList<qint64> allowedValues;
};

// Simple可编码；Deferred已登记但暂缓；LayoutPending等待正确字节布局；
// ReceiveOnly为设备上报。SDK能力与软件的运动发送许可分别检查。
enum class QushengSdkSupport { Simple, Deferred, LayoutPending, ReceiveOnly };

struct QushengCommandDefinition {
    quint8 code = 0;
    QString name;
    QushengSdkSupport support = QushengSdkSupport::Deferred;
    QList<QushengParameterDefinition> parameters;
    // 标识查询语义，供界面/统一命令分类；不表示运动已完成。
    bool query = false;
};

// 这是趋盛参数快照，客户字段表达式已在ConversionEngine中求值。
// payload仅含命令数据D0...Dn，完整帧还需makeFrame补入地址、序号及CRC。
struct QushengSdkRequest {
    quint8 command = 0;
    QVariantMap parameters;
    // 记录每个参数来自明确映射、预置、设备还是软件初始值，供预览核查。
    QMap<QString, QString> parameterSources;
    QByteArray payload;
};

struct QushengSdkReply {
    // success表示设备执行结果，不是解析函数的返回值；合法拒绝回告也能解析成功。
    bool success = false;
    quint32 errorCode = 0;
    QByteArray basicStatus;
    // 查询扩展字段或上报记录；线上载荷同时保留，方便比对协议及实机差异。
    QVariantMap values;
    QByteArray rawPayload;
};

class QushengSdk {
public:
    static QList<quint8> commandCodes();
    static QushengCommandDefinition definition(quint8 command);
    static bool isSupported(quint8 command);
    static bool isDeviceId(quint8 device);
    // 优先级为明确映射值、用户SDK预置、设备参数、接口默认。缺少必填业务数据拒绝编码。
    // 数值默认必须与协议固定字节分开：固定请求内容由encode写入，不能被参数覆盖。
    static bool buildRequest(quint8 command, const QVariantMap &supplied,
                             const QVariantMap &defaults, const QVariantMap &device,
                             QushengSdkRequest *request, TranslationError *error);
    // 仅编码已补齐的参数，不读取草稿或再次选择默认值；直接调用时也执行严格校验。
    static bool encode(quint8 command, const QVariantMap &parameters,
                       QByteArray *payload, TranslationError *error);
    // 返回true表示应答布局解析成功，设备是否接受由reply.success判断。
    // 调用方先验证整帧CRC和请求归属；这里处理payload，不分配序号、不写串口。
    static bool decodeReply(quint8 command, const QByteArray &payload,
                            QushengSdkReply *reply, TranslationError *error);
    // 上报没有通用应答头，必须走独立入口，避免将故障记录误读为执行结果。
    static bool decodeReport(quint8 command, const QByteArray &payload,
                             QushengSdkReply *reply, TranslationError *error);
    static quint16 crc16(const QByteArray &data);
    // 帧地址、线上序号由会话层传入；payload超过单字节长度上限时返回空帧。
    static QByteArray makeFrame(quint8 destination, quint8 source, quint8 sequence,
                                quint8 command, const QByteArray &payload);
};

#endif
