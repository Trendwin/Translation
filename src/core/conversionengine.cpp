#include "conversionengine.h"
#include "qusheng/qushengprotocol.h"
#include <QJsonArray>
#include <QRegularExpression>
#include <QtMath>
#include <climits>

namespace {
bool fail(TranslationError *e,const QString &code,const QString &message)
{ if(e) *e={code,message}; return false; }
quint64 readNumber(const QByteArray &data,bool little)
{
    quint64 n=0;
    if(little) for(int i=data.size()-1;i>=0;--i) n=(n<<8)|quint8(data.at(i));
    else for(char b:data) n=(n<<8)|quint8(b);
    return n;
}
QByteArray hex(const QJsonValue &v) { return QByteArray::fromHex(v.toString().toLatin1()); }
bool eval(const QJsonObject &e,const QMap<QString,QVariant> &fields,QVariant *out,TranslationError *error)
{
    const QString kind=e.value("kind").toString();
    if(kind=="constant") { *out=e.value("value").toVariant(); return true; }
    const QString name=e.value(kind=="field" ? "name":"field").toString();
    if(kind!="bits" && !fields.contains(name)) return fail(error,"MISSING_FIELD","missing field: "+name);
    if(kind=="field") { *out=fields.value(name); return true; }
    if(kind=="enum") {
        const QJsonObject table=e.value("table").toObject();
        const QString key=fields.value(name).toString();
        if(!table.contains(key)) return fail(error,"ENUM_UNKNOWN","no enum mapping for "+key);
        *out=table.value(key).toVariant(); return true;
    }
    if(kind=="linear") {
        bool ok=false; const qlonglong input=fields.value(name).toLongLong(&ok);
        if(!ok) return fail(error,"CONVERSION_TYPE","linear input is not integer");
        const qint64 numerator=qint64(e.value("numerator").toDouble());
        const qint64 denominator=qint64(e.value("denominator").toDouble());
        const qint64 offset=qint64(e.value("offset").toDouble());
        if(input<0 || numerator<1 || denominator<1 || input>LLONG_MAX/numerator)
            return fail(error,"CONVERSION_OVERFLOW","linear conversion overflow");
        const qint64 scaled=input*numerator;
        const QString round=e.value("round").toString();
        if(round=="exact" && scaled%denominator) return fail(error,"CONVERSION_FRACTION","exact conversion has remainder");
        qint64 value=scaled/denominator;
        if(round=="ceil" && scaled%denominator) ++value;
        if(round=="nearest" && scaled%denominator>=(denominator+1)/2) ++value;
        if(offset<0 && value< -offset) return fail(error,"CONVERSION_RANGE","negative converted position");
        if(offset>0 && value>LLONG_MAX-offset) return fail(error,"CONVERSION_OVERFLOW","offset overflow");
        *out=value+offset; return true;
    }
    if(kind=="bits") {
        quint64 value=0, used=0;
        for(const QJsonValue &partValue:e.value("parts").toArray()) {
            const QJsonObject part=partValue.toObject(); QVariant piece;
            if(!eval(part.value("value").toObject(),fields,&piece,error)) return false;
            bool ok=false; const quint64 n=piece.toULongLong(&ok);
            const int shift=part.value("shift").toInt(), width=part.value("width").toInt();
            if(!ok || width<1 || width>32 || shift+width>32 || n >= (quint64(1)<<width))
                return fail(error,"BIT_RANGE","bitfield component out of range");
            const quint64 mask=((quint64(1)<<width)-1)<<shift;
            if(used&mask) return fail(error,"BIT_OVERLAP","bitfield components overlap");
            used|=mask; value|=n<<shift;
        }
        *out=qulonglong(value); return true;
    }
    return fail(error,"EXPRESSION_UNSUPPORTED","unsupported expression");
}
quint16 crc16(const QByteArray &data,const QJsonObject &cfg)
{
    quint16 crc=quint16(cfg.value("initial").toInt());
    const quint16 poly=quint16(cfg.value("polynomial").toInt());
    for(char b:data) { crc^=quint16(quint8(b))<<8; for(int i=0;i<8;++i) crc=(crc&0x8000)?quint16((crc<<1)^poly):quint16(crc<<1); }
    return crc^quint16(cfg.value("xorOut").toInt());
}
}

ConversionEngine::ConversionEngine(const ProtocolConfig &config):m_config(config) {}

bool ConversionEngine::parse(const QByteArray &input,QMap<QString,QVariant> *fields,TranslationError *error) const
{
    const QJsonObject external=m_config.root.value("external").toObject();
    const QString format=external.value("format").toString();
    fields->clear();
    if(format=="dt") {
        // Entire input is checked before any action is produced. R is a command,
        // CR is the terminator. Broadcast and multi-command buffers are rejected.
        const QRegularExpression re(QStringLiteral("^/([1-9])(A|a)([0-9]+)R\\r$|^/([1-9])Q\\r$"));
        const auto match=re.match(QString::fromLatin1(input));
        if(!match.hasMatch()) return fail(error,"DT_UNSUPPORTED","only one A/a ... R or Q frame ending CR is supported");
        const bool query=!match.captured(4).isEmpty();
        fields->insert("address",query?match.captured(4).toInt():match.captured(1).toInt());
        fields->insert("command",query?QStringLiteral("Q"):match.captured(2));
        if(!query) { bool ok=false; const qlonglong position=match.captured(3).toLongLong(&ok);
            if(!ok) return fail(error,"DT_RANGE","position overflows integer");
            fields->insert("position",position); }
        return true;
    }
    if(format=="ascii") {
        const QString templ=external.value("template").toString();
        QString pattern="^"; QStringList names;
        for(int i=0;i<templ.size();) {
            if(templ.at(i)=='{') {
                const int close=templ.indexOf('}',i+1);
                if(close<0) return fail(error,"CONFIG_TEMPLATE","unclosed placeholder");
                const QString name=templ.mid(i+1,close-i-1);
                if(name.isEmpty() || names.contains(name)) return fail(error,"CONFIG_TEMPLATE","duplicate or empty placeholder");
                names.append(name); pattern+=QStringLiteral("([+-]?[0-9]+)"); i=close+1;
            } else { pattern+=QRegularExpression::escape(templ.mid(i,1)); ++i; }
        }
        pattern+="$";
        const QRegularExpression re(pattern);
        const auto match=re.match(QString::fromLatin1(input));
        if(!match.hasMatch()) return fail(error,"ASCII_FRAME","input does not match complete ASCII template");
        for(int i=0;i<names.size();++i) { bool ok=false; const qlonglong n=match.captured(i+1).toLongLong(&ok);
            if(!ok) return fail(error,"ASCII_RANGE","numeric field overflows");
            fields->insert(names.at(i),n); }
    } else if(format=="binary") {
        const QByteArray header=hex(external.value("headerHex")),footer=hex(external.value("footerHex"));
        if(!input.startsWith(header) || (!footer.isEmpty() && !input.endsWith(footer)))
            return fail(error,"BINARY_BOUNDARY","header or footer mismatch");
        int size=external.value("fixedLength").toInt();
        if(size==0) { const QJsonObject lf=external.value("lengthField").toObject();
            const int at=lf.value("offset").toInt(), width=lf.value("size").toInt();
            if(at+width>input.size()) return fail(error,"BINARY_LENGTH","missing length field");
            size=int(readNumber(input.mid(at,width),lf.value("endian").toString()=="little"))+lf.value("base").toInt(); }
        if(size!=input.size() || size>4096) return fail(error,"BINARY_LENGTH","binary frame length mismatch");
        for(const QJsonValue &v:external.value("fields").toArray()) {
            const QJsonObject f=v.toObject(); const int at=f.value("offset").toInt(), width=f.value("length").toInt();
            if(at+width>input.size()) return fail(error,"BINARY_FIELD","field exceeds frame");
            const QByteArray raw=input.mid(at,width); QVariant value;
            if(f.value("type").toString()=="ascii") value=QString::fromLatin1(raw);
            else if(f.value("type").toString()=="constant") value=raw.toHex();
            else {
                quint64 n=readNumber(raw,f.value("endian").toString()=="little");
                if(f.value("type").toString()=="int" && width<8 && (n&(quint64(1)<<(width*8-1)))) n|=(~quint64(0)<<(width*8));
                value=f.value("type").toString()=="int" ? QVariant(qlonglong(n)):QVariant(qulonglong(n));
            }
            fields->insert(f.value("name").toString(),value);
        }
        const QJsonObject crc=external.value("crc").toObject();
        if(!crc.isEmpty()) {
            const int at=crc.value("offset").toInt(), from=crc.value("from").toInt(), to=crc.value("toExclusive").toInt();
            if(at+2>input.size() || from>=to || to>input.size() || !(at+2<=from || at>=to))
                return fail(error,"BINARY_CRC","CRC range or location invalid");
            const quint16 expected=crc16(input.mid(from,to-from),crc);
            if(readNumber(input.mid(at,2),crc.value("endian").toString()=="little")!=expected)
                return fail(error,"BINARY_CRC","CRC mismatch");
        }
    }
    for(const QJsonValue &v:external.value("fields").toArray()) {
        const QJsonObject f=v.toObject(); const QString name=f.value("name").toString();
        if(!fields->contains(name)) return fail(error,"MISSING_FIELD","missing field: "+name);
        const QVariant value=fields->value(name);
        if(f.value("type").toString()=="uint" && value.toLongLong()<0)
            return fail(error,"FIELD_RANGE","unsigned field is negative: "+name);
        if(f.contains("constant") && value.toString()!=f.value("constant").toVariant().toString())
            return fail(error,"FIELD_CONSTANT","field constant mismatch: "+name);
        if(f.contains("min") && value.toDouble()<f.value("min").toDouble()) return fail(error,"FIELD_RANGE","field below minimum: "+name);
        if(f.contains("max") && value.toDouble()>f.value("max").toDouble()) return fail(error,"FIELD_RANGE","field above maximum: "+name);
    }
    return true;
}

bool ConversionEngine::map(const QMap<QString,QVariant> &fields,UnifiedCommand *command,TranslationError *error) const
{
    const QJsonArray mappings=m_config.root.value("mappings").toArray();
    int matches=0;
    for(const QJsonValue &v:mappings) {
        const QJsonObject when=v.toObject().value("when").toObject(); bool matched=true;
        for(auto it=when.begin();it!=when.end();++it)
            if(!fields.contains(it.key()) || fields.value(it.key()).toString()!=it.value().toVariant().toString()) matched=false;
        if(matched) ++matches;
    }
    if(matches==0) return fail(error,"COMMAND_UNSUPPORTED","no complete mapping matches the frame");
    if(matches>1) return fail(error,"MAPPING_AMBIGUOUS","more than one mapping matches the frame");
    for(const QJsonValue &v:mappings) {
        const QJsonObject m=v.toObject(),when=m.value("when").toObject(); bool matched=true;
        for(auto it=when.begin();it!=when.end();++it) if(!fields.contains(it.key()) || fields.value(it.key()).toString()!=it.value().toVariant().toString()) matched=false;
        if(!matched) continue;
        const QJsonObject dev=m_config.root.value("device").toObject();
        command->configVersion=m_config.version;
        command->targetDevice=QString::number(dev.value("devId").toInt());
        command->operation=m.value("operation").toString();
        command->action=command->operation=="QUERY_MOTOR"?ActionType::Query:ActionType::Execute;
        command->responsePolicy=m.value("responsePolicy").toString();
        command->parameters.clear(); command->parameterUnits.clear();
        const QJsonObject params=m.value("parameters").toObject();
        for(auto it=params.begin();it!=params.end();++it) {
            QVariant value; if(!eval(it.value().toObject(),fields,&value,error)) return false;
            command->parameters.insert(it.key(),value);
        }
        if(command->operation=="ABSOLUTE_MOVE") {
            bool ok=false; const qlonglong target=command->parameters.value("positionUm").toLongLong(&ok);
            if(!ok || target<dev.value("minPositionUm").toInt() || target>dev.value("maxPositionUm").toInt())
                return fail(error,"POSITION_LIMIT","target outside configured stroke limits");
            command->parameters.insert("speedUmPerS",dev.value("speedUmPerS").toInt());
            command->parameters.insert("absoluteAction",dev.value("absoluteAction").toInt());
            command->parameterUnits.insert("positionUm","um");
            command->parameterUnits.insert("speedUmPerS","um/s");
        }
        return true;
    }
    return fail(error,"COMMAND_UNSUPPORTED","no complete mapping matches the frame");
}

bool ConversionEngine::convert(const QByteArray &input,bool live,ConversionPreview *out,TranslationError *error) const
{
    if(live && !m_config.liveReady(error)) return false;
    ConversionPreview result; result.input=input;
    if(!parse(input,&result.fields,error) || !map(result.fields,&result.command,error)) return false;
    const QJsonObject dev=m_config.root.value("device").toObject();
    QushengProtocol codec; codec.setAddresses(quint8(dev.value("destination").toInt()),quint8(dev.value("source").toInt()));
    if(!codec.encodeCommand(result.command,&result.targetFrame,error)) return false;
    if(out) *out=result;
    return true;
}

QList<QByteArray> ConversionEngine::feed(const QByteArray &chunk,QList<TranslationError> *errors)
{
    QList<QByteArray> frames;
    m_buffer.append(chunk);
    const QJsonObject ext=m_config.root.value("external").toObject();
    const QString format=ext.value("format").toString();
    if(m_buffer.size()>4096) { m_buffer.clear(); if(errors) errors->append(TranslationError{"UPSTREAM_OVERFLOW","upstream receive cache exceeded 4096 bytes"}); return frames; }
    const QByteArray header=format=="binary"?hex(ext.value("headerHex")):
          format=="dt"?QByteArray("/"):ext.value("template").toString().left(ext.value("template").toString().indexOf('{')).toLatin1();
    const QByteArray suffix=format=="dt"?QByteArray("\r"):
          format=="ascii"?ext.value("template").toString().mid(ext.value("template").toString().lastIndexOf('}')+1).toLatin1():QByteArray();
    for(;;) {
        const int start=m_buffer.indexOf(header);
        if(start<0) { m_buffer=m_buffer.right(qMax(0,header.size()-1)); break; }
        if(start>0) m_buffer.remove(0,start);
        int size=0;
        if(format=="binary") {
            size=ext.value("fixedLength").toInt();
            if(!size) { const QJsonObject lf=ext.value("lengthField").toObject(); const int at=lf.value("offset").toInt(),width=lf.value("size").toInt();
                if(m_buffer.size()<at+width) break;
                size=int(readNumber(m_buffer.mid(at,width),lf.value("endian").toString()=="little"))+lf.value("base").toInt(); }
            if(size< header.size() || size>4096) { m_buffer.remove(0,1); if(errors) errors->append(TranslationError{"UPSTREAM_LENGTH","invalid upstream frame length"}); continue; }
            if(m_buffer.size()<size) break;
        } else {
            if(suffix.isEmpty()) { if(errors) errors->append(TranslationError{"UPSTREAM_SUFFIX","missing ASCII terminator"}); m_buffer.clear(); break; }
            const int end=m_buffer.indexOf(suffix,header.size()); if(end<0) break;
            size=end+suffix.size();
        }
        const QByteArray candidate=m_buffer.left(size);
        QMap<QString,QVariant> fields; TranslationError e;
        if(parse(candidate,&fields,&e)) { m_buffer.remove(0,size); frames.append(candidate); }
        else {
            const bool lostBoundary=format=="binary" && (e.code=="BINARY_CRC" || e.code=="BINARY_BOUNDARY" || e.code=="BINARY_LENGTH");
            m_buffer.remove(0,lostBoundary?1:size);
            if(errors) errors->append(e);
        }
    }
    return frames;
}
