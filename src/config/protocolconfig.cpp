#include "protocolconfig.h"
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>
#include <QRegularExpression>

namespace {
bool fail(TranslationError *e, const QString &code, const QString &detail)
{ if (e) *e = {code, detail}; return false; }
bool integer(const QJsonValue &v, qint64 low, qint64 high)
{ const double d=v.toDouble(-1); return d>=low && d<=high && double(qint64(d))==d; }
bool validExpr(const QJsonObject &e)
{
    const QString kind=e.value("kind").toString();
    if (kind=="constant") return e.contains("value");
    if (kind=="field") return !e.value("name").toString().isEmpty();
    if (kind=="enum") return !e.value("field").toString().isEmpty() && e.value("table").isObject();
    if (kind=="linear") return !e.value("field").toString().isEmpty()
            && integer(e.value("numerator"),1,1000000000) && integer(e.value("denominator"),1,1000000000)
            && (!e.contains("offset") || integer(e.value("offset"),-1000000000,1000000000))
            && QStringList({"floor","ceil","nearest","exact"}).contains(e.value("round").toString());
    if (kind=="bits") {
        const QJsonArray parts=e.value("parts").toArray();
        if (parts.isEmpty()) return false;
        for (const QJsonValue &p:parts) if (!p.isObject() || !integer(p.toObject().value("shift"),0,31)
              || !integer(p.toObject().value("width"),1,32) || !validExpr(p.toObject().value("value").toObject())) return false;
        return true;
    }
    return false;
}
}

bool ProtocolConfig::fromJson(const QByteArray &json, ProtocolConfig *out, TranslationError *error)
{
    QJsonParseError parse;
    const QJsonDocument doc=QJsonDocument::fromJson(json,&parse);
    if (!doc.isObject()) return fail(error,"CONFIG_JSON",parse.errorString());
    ProtocolConfig value; value.root=doc.object();
    if (!value.validate(error)) return false;
    value.version=QString::fromLatin1(QCryptographicHash::hash(doc.toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex());
    if (out) *out=value;
    return true;
}

bool ProtocolConfig::load(const QString &path, ProtocolConfig *out, TranslationError *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return fail(error,"CONFIG_READ",file.errorString());
    return fromJson(file.readAll(),out,error);
}

bool ProtocolConfig::save(const QString &path, TranslationError *error) const
{
    if (!validate(error)) return false;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return fail(error,"CONFIG_WRITE",file.errorString());
    if (file.write(QJsonDocument(root).toJson(QJsonDocument::Indented))<0 || !file.commit())
        return fail(error,"CONFIG_WRITE",file.errorString());
    return true;
}

bool ProtocolConfig::validate(TranslationError *error) const
{
    if (root.value("schemaVersion").toInt()!=1) return fail(error,"CONFIG_SCHEMA","schemaVersion must be 1");
    const QJsonObject frame=root.value("external").toObject();
    const QString format=frame.value("format").toString();
    if (!QStringList({"ascii","binary","dt"}).contains(format)) return fail(error,"CONFIG_FORMAT","external.format must be ascii, binary or dt");
    if (format=="ascii" && (frame.value("template").toString().isEmpty() || !frame.value("template").toString().contains('{')))
        return fail(error,"CONFIG_TEMPLATE","ASCII template needs a field placeholder");
    if (format=="ascii" && (frame.value("template").toString().startsWith('{')
             || frame.value("template").toString().endsWith('}')))
        return fail(error,"CONFIG_TEMPLATE","ASCII template needs fixed prefix and terminator");
    if (format!="binary" && !frame.value("crc").toObject().isEmpty())
        return fail(error,"CONFIG_CRC","CRC definition is only supported for binary frames");
    if (format=="binary") {
        const QByteArray head=QByteArray::fromHex(frame.value("headerHex").toString().toLatin1());
        if (head.isEmpty() || head.toHex()!=frame.value("headerHex").toString().toLatin1().toLower())
            return fail(error,"CONFIG_HEADER","binary headerHex is invalid");
        const QString footerText=frame.value("footerHex").toString();
        if (!footerText.isEmpty() && QByteArray::fromHex(footerText.toLatin1()).toHex()!=footerText.toLatin1().toLower())
            return fail(error,"CONFIG_FOOTER","binary footerHex is invalid");
        const int fixed=frame.value("fixedLength").toInt();
        const QJsonObject length=frame.value("lengthField").toObject();
        if ((fixed<1)==length.isEmpty()) return fail(error,"CONFIG_LENGTH","choose fixedLength or lengthField");
        if (fixed>4096 || (!length.isEmpty() && (!integer(length.value("offset"),0,4095)
              || !integer(length.value("size"),1,2) || !integer(length.value("base"),0,4096)
              || !QStringList({"little","big"}).contains(length.value("endian").toString()))))
            return fail(error,"CONFIG_LENGTH","invalid frame length definition");
        if (fixed>0 && fixed<int(head.size()+QByteArray::fromHex(frame.value("footerHex").toString().toLatin1()).size()))
            return fail(error,"CONFIG_LENGTH","fixed frame shorter than boundaries");
    }
    QSet<QString> names;
    for (const QJsonValue &v:frame.value("fields").toArray()) {
        const QJsonObject f=v.toObject(); const QString name=f.value("name").toString();
        if (name.isEmpty() || names.contains(name)) return fail(error,"CONFIG_FIELD","field name missing or duplicated");
        names.insert(name);
        if (!QStringList({"uint","int","ascii","constant"}).contains(f.value("type").toString()))
            return fail(error,"CONFIG_FIELD","unsupported field type: "+name);
        if (format=="ascii" && !QStringList({"uint","int"}).contains(f.value("type").toString()))
            return fail(error,"CONFIG_FIELD","ASCII placeholders support numeric fields only: "+name);
        if (format=="binary" && (!integer(f.value("offset"),0,4095) || !integer(f.value("length"),1,8)
             || !QStringList({"little","big"}).contains(f.value("endian").toString())))
            return fail(error,"CONFIG_FIELD","invalid binary field: "+name);
        if (format=="binary" && frame.value("fixedLength").toInt()>0
                && f.value("offset").toInt()+f.value("length").toInt()>frame.value("fixedLength").toInt())
            return fail(error,"CONFIG_FIELD","binary field exceeds fixed frame: "+name);
        if (f.contains("min") && f.contains("max") && f.value("min").toDouble()>f.value("max").toDouble())
            return fail(error,"CONFIG_RANGE","inverted field range: "+name);
    }
    if (format=="ascii") {
        const QString templ=frame.value("template").toString();
        QSet<QString> placeholders;
        const QRegularExpression re(QStringLiteral("\\{([^{}]+)\\}"));
        auto matches=re.globalMatch(templ);
        while(matches.hasNext()) placeholders.insert(matches.next().captured(1));
        if(placeholders!=names) return fail(error,"CONFIG_TEMPLATE","ASCII placeholders and field definitions differ");
    }
    const QJsonObject crc=frame.value("crc").toObject();
    if (!crc.isEmpty() && (!QStringList({"crc16-ccitt-false","crc16-msb"}).contains(crc.value("algorithm").toString())
          || !integer(crc.value("polynomial"),0,65535) || !integer(crc.value("initial"),0,65535)
          || !integer(crc.value("xorOut"),0,65535) || !integer(crc.value("offset"),0,4095)
          || !integer(crc.value("from"),0,4095) || !integer(crc.value("toExclusive"),1,4096)
          || !QStringList({"little","big"}).contains(crc.value("endian").toString())
          || !crc.contains("reflectIn") || !crc.contains("reflectOut")
          || crc.value("reflectIn").toBool() || crc.value("reflectOut").toBool()))
        return fail(error,"CONFIG_CRC","CRC requires algorithm, polynomial, initial, xorOut, range, offset, endian");
    if (!crc.isEmpty() && crc.value("algorithm").toString()=="crc16-ccitt-false"
            && (crc.value("polynomial").toInt()!=0x1021 || crc.value("initial").toInt()!=0xffff || crc.value("xorOut").toInt()!=0))
        return fail(error,"CONFIG_CRC","CCITT-FALSE parameters do not match its algorithm name");
    if (!crc.isEmpty() && frame.value("fixedLength").toInt()>0
            && (crc.value("offset").toInt()+2>frame.value("fixedLength").toInt()
                || crc.value("toExclusive").toInt()>frame.value("fixedLength").toInt()))
        return fail(error,"CONFIG_CRC","CRC range exceeds fixed frame");
    const QJsonArray maps=root.value("mappings").toArray();
    if (maps.isEmpty()) return fail(error,"CONFIG_MAPPING","at least one mapping is required");
    QSet<QByteArray> predicates;
    for (const QJsonValue &v:maps) {
        const QJsonObject m=v.toObject();
        if (!QStringList({"ABSOLUTE_MOVE","QUERY_MOTOR"}).contains(m.value("operation").toString())
                || !QStringList({"ack","complete","query"}).contains(m.value("responsePolicy").toString()))
            return fail(error,"CONFIG_MAPPING","unsupported operation or response policy");
        if ((m.value("operation").toString()=="QUERY_MOTOR")!=(m.value("responsePolicy").toString()=="query"))
            return fail(error,"CONFIG_MAPPING","query operation and query response policy must agree");
        const QByteArray predicate=QJsonDocument(m.value("when").toObject()).toJson(QJsonDocument::Compact);
        if (predicates.contains(predicate)) return fail(error,"CONFIG_MAPPING","duplicate mapping predicate");
        predicates.insert(predicate);
        const QJsonObject params=m.value("parameters").toObject();
        for (auto it=params.begin();it!=params.end();++it)
            if (!validExpr(it.value().toObject())) return fail(error,"CONFIG_EXPRESSION","invalid expression: "+it.key());
    }
    const QJsonObject dev=root.value("device").toObject();
    if (!integer(dev.value("destination"),1,255) || !integer(dev.value("source"),1,255)
            || dev.value("destination").toInt()==dev.value("source").toInt()
            || !integer(dev.value("devId"),0x11,0x1c) || !integer(dev.value("speedUmPerS"),1,0xfffffe)
            || !integer(dev.value("minPositionUm"),0,0xfffffe) || !integer(dev.value("maxPositionUm"),0,0xfffffe)
            || dev.value("minPositionUm").toInt()>dev.value("maxPositionUm").toInt()
            || (dev.value("absoluteAction").toInt()!=1 && dev.value("absoluteAction").toInt()!=3))
        return fail(error,"CONFIG_DEVICE","device addresses, speed, limits or absoluteAction invalid");
    return true;
}

bool ProtocolConfig::liveReady(TranslationError *error) const
{
    if (!validate(error)) return false;
    const QJsonObject dev=root.value("device").toObject();
    if (!dev.value("confirmed").toBool() || !dev.value("conversionConfirmed").toBool()
            || !dev.value("crcConfirmed").toBool())
        return fail(error,"DEVICE_UNCONFIRMED","device, unit conversion and CRC must be independently confirmed before live send");
    const QJsonObject responses=root.value("responses").toObject();
    if (root.value("external").toObject().value("format").toString()=="dt") {
        if (!root.value("dtErrorMapConfirmed").toBool() || !integer(root.value("defaultDtErrorCode"),1,15))
            return fail(error,"DT_ERROR_MAP_UNCONFIRMED","DT error code mapping and fallback require confirmation");
        const QJsonObject codes=root.value("dtErrorCodes").toObject();
        for(auto it=codes.begin();it!=codes.end();++it)
            if(!integer(it.value(),1,15)) return fail(error,"DT_ERROR_MAP_UNCONFIRMED","DT error code must be 1..15: "+it.key());
    } else if (responses.value("ack").toString().isEmpty() || responses.value("complete").toString().isEmpty()
               || responses.value("reject").toString().isEmpty())
        return fail(error,"RESPONSE_UNCONFIRMED","ack, complete and reject response templates are required");
    if (root.value("external").toObject().value("format").toString()=="binary")
        for (const QString &name:{QStringLiteral("ack"),QStringLiteral("complete"),QStringLiteral("reject")}) {
            const QByteArray candidate=responses.value(name).toString().toLatin1();
            if (QByteArray::fromHex(candidate).toHex()!=candidate.toLower())
                return fail(error,"RESPONSE_UNCONFIRMED","binary response is not valid hex: "+name);
        }
    return true;
}
