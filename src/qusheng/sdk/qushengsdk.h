#ifndef QUSHENGSDK_H
#define QUSHENGSDK_H

#include "model/translationtypes.h"
#include <QList>
#include <QVariantMap>

// SDK仅依赖QtCore：不读取客户字段、配置JSON、串口或界面。业务层先完成字段映射，
// 再提交趋盛参数；同一请求保存已补齐的参数快照，不能在发送时重新读取可变默认值。
struct QushengParameterDefinition {
    QString key, label, type;
    qint64 minimum = 0, maximum = 0;
    QVariant defaultValue;
    QString deviceKey;
    bool required = false;
    QList<qint64> allowedValues;
};

enum class QushengSdkSupport { Simple, Deferred, LayoutPending, ReceiveOnly };

struct QushengCommandDefinition {
    quint8 code = 0;
    QString name;
    QushengSdkSupport support = QushengSdkSupport::Deferred;
    QList<QushengParameterDefinition> parameters;
    bool query = false;
};

struct QushengSdkRequest {
    quint8 command = 0;
    QVariantMap parameters;
    QMap<QString, QString> parameterSources;
    QByteArray payload;
};

struct QushengSdkReply {
    bool success = false;
    quint32 errorCode = 0;
    QByteArray basicStatus;
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
    static bool encode(quint8 command, const QVariantMap &parameters,
                       QByteArray *payload, TranslationError *error);
    static bool decodeReply(quint8 command, const QByteArray &payload,
                            QushengSdkReply *reply, TranslationError *error);
    static bool decodeReport(quint8 command, const QByteArray &payload,
                             QushengSdkReply *reply, TranslationError *error);
    static quint16 crc16(const QByteArray &data);
    static QByteArray makeFrame(quint8 destination, quint8 source, quint8 sequence,
                                quint8 command, const QByteArray &payload);
};

#endif
