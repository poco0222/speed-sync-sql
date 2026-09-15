#include "data.h"
#include "foundation.h"
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScopeGuard>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace {
struct Error { QString message, code; };
void require(bool yes, const QString &message, const QString &code = "data") { if (!yes) throw Error{message, code}; }
QJsonObject failure(const QString &message, const QString &code) { return {{"ok", false}, {"error", message}, {"code", code}}; }
QString stamp() { return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs); }
QString quote(QString s) { return "`" + s.replace('`', "``") + "`"; }
QString json(const QJsonArray &v) { return QString::fromUtf8(QJsonDocument(v).toJson(QJsonDocument::Compact)); }
QJsonArray array(const QString &v) { return QJsonDocument::fromJson(v.toUtf8()).array(); }
QStringList strings(const QJsonValue &value) {
    require(value.isArray(), "字段列表格式无效", "validation"); QStringList result;
    for (auto v : value.toArray()) { require(v.isString() && !result.contains(v.toString()), "字段无效或重复", "validation"); result << v.toString(); }
    return result;
}
QByteArray storedText(const QString &text) {
    const auto chars=text.toUcs4(); return QByteArray(reinterpret_cast<const char *>(chars.constData()),chars.size()*sizeof(char32_t));
}
QString loadedText(const QByteArray &bytes) {
    require(bytes.size()%sizeof(char32_t)==0,"临时原值编码损坏，请重新比对","storage"); QList<char32_t> chars(bytes.size()/sizeof(char32_t));
    if(!bytes.isEmpty()) std::memcpy(chars.data(),bytes.constData(),bytes.size()); return QString::fromUcs4(chars.constData(),chars.size());
}
bool binary(const QString &t) { return QStringList{"binary", "varbinary", "tinyblob", "blob", "mediumblob", "longblob", "bit"}.contains(t); }
bool numeric(const QString &t) { return QStringList{"tinyint", "smallint", "mediumint", "int", "bigint", "decimal", "float", "double"}.contains(t); }
bool temporal(const QString &t) { return QStringList{"date", "datetime", "timestamp", "time", "year"}.contains(t); }
bool supported(const QString &t) { return binary(t) || numeric(t) || temporal(t) || QStringList{"char", "varchar", "tinytext", "text", "mediumtext", "longtext", "enum", "set", "json"}.contains(t); }
QString decimal(QString s, bool *ok) {
    static const QRegularExpression re("^(-?)([0-9]+)(?:\\.([0-9]+))?(?:[eE]([+-]?[0-9]+))?$");
    const auto m = re.match(s); if (!m.hasMatch()) { *ok = false; return {}; }
    bool expOk = true; qlonglong exponent = m.captured(4).isEmpty() ? 0 : m.captured(4).toLongLong(&expOk);
    if (!expOk || exponent < -1000000 || exponent > 1000000) { *ok = false; return {}; }
    QString digits = m.captured(2) + m.captured(3); exponent -= m.captured(3).size();
    while (digits.size() > 1 && digits.startsWith('0')) digits.remove(0, 1);
    if (digits == "0") return "0";
    while (digits.endsWith('0')) { digits.chop(1); ++exponent; }
    return m.captured(1) + digits + "e" + QString::number(exponent);
}
// QJson numbers are doubles. Parse only the JSON grammar here and retain numeric tokens.
class ExactJson {
    QString input; qsizetype pos = 0;
    void ws() { while (pos < input.size() && QString(" \t\r\n").contains(input[pos])) ++pos; }
    QString string() {
        require(pos < input.size() && input[pos] == '"', "不支持的 JSON 字符串", "unsupported"); const auto start = pos++;
        while (pos < input.size()) { if (input[pos] == '\\') { pos += 2; continue; } if (input[pos++] == '"') {
            QJsonParseError error; auto doc = QJsonDocument::fromJson(("[" + input.mid(start, pos-start) + "]").toUtf8(), &error);
            require(error.error == QJsonParseError::NoError, "不支持的 JSON 字符串", "unsupported"); return doc.array()[0].toString();
        }}
        throw Error{"不完整的 JSON 字符串", "unsupported"};
    }
    QString value(int depth) {
        require(depth < 128, "JSON 嵌套过深", "unsupported"); ws(); require(pos < input.size(), "不完整的 JSON", "unsupported");
        const auto c = input[pos];
        if (c == '"') { auto s = string(); return json(QJsonArray{s}); }
        if (c == '{') {
            ++pos; ws(); QMap<QString, QString> fields;
            if (pos < input.size() && input[pos] == '}') { ++pos; return "{}"; }
            while (true) { ws(); auto key = string(); require(!fields.contains(key), "JSON 重复对象键无法可靠比较", "unsupported"); ws(); require(pos < input.size() && input[pos++] == ':', "JSON 对象格式无效", "unsupported"); fields[key] = value(depth+1); ws(); require(pos < input.size(), "不完整的 JSON", "unsupported"); auto sep = input[pos++]; if (sep == '}') break; require(sep == ',', "JSON 对象格式无效", "unsupported"); }
            QString out = "{"; for (auto it=fields.cbegin(); it!=fields.cend(); ++it) out += json(QJsonArray{it.key()}) + ":" + it.value() + ","; return out + "}";
        }
        if (c == '[') {
            ++pos; ws(); QString out="["; if (pos < input.size() && input[pos] == ']') { ++pos; return "[]"; }
            while (true) { out += value(depth+1) + ","; ws(); require(pos < input.size(), "不完整的 JSON", "unsupported"); auto sep=input[pos++]; if (sep==']') break; require(sep==',', "JSON 数组格式无效", "unsupported"); } return out+"]";
        }
        for (const auto &token : {QString("null"), QString("true"), QString("false")}) if (input.mid(pos, token.size()) == token) { pos += token.size(); return token; }
        const auto start=pos; while (pos<input.size() && QString("-+0123456789.eE").contains(input[pos])) ++pos;
        static const QRegularExpression grammar("^-?(?:0|[1-9][0-9]*)(?:\\.[0-9]+)?(?:[eE][+-]?[0-9]+)?$");
        const auto token=input.mid(start,pos-start); bool ok=grammar.match(token).hasMatch(); auto result=decimal(token,&ok); require(ok,"JSON 数值不支持","unsupported"); return "number:"+result;
    }
public:
    explicit ExactJson(QString text) : input(std::move(text)) {}
    QString parse() { auto result=value(0); ws(); require(pos==input.size(),"JSON 尾部无效","unsupported"); return result; }
};
void sql(QSqlQuery &q, const QString &statement) { require(q.exec(statement), QString("读取失败（驱动错误码 %1）").arg(q.lastError().nativeErrorCode()), "query"); }
void execute(QSqlQuery &q) { require(q.exec(), QString("查询或临时结果写入失败（驱动错误码 %1）").arg(q.lastError().nativeErrorCode()), "query"); }
struct Connection {
    QString name = "data-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QSqlDatabase db;
    Connection(const QJsonObject &c) {
        auto invalid=validateConnection(c); require(invalid.isEmpty(), invalid, "validation");
        db=QSqlDatabase::addDatabase("QMYSQL",name); db.setHostName(c["host"].toString()); db.setPort(c["port"].toInt()); db.setUserName(c["user"].toString()); db.setPassword(c["password"].toString());
        const auto mode=c["tls"].toString(); const auto ssl=mode=="verify"?"SSL_MODE_VERIFY_IDENTITY":mode=="required"?"SSL_MODE_REQUIRED":mode=="disabled"?"SSL_MODE_DISABLED":"SSL_MODE_PREFERRED";
        auto opts=QString("MYSQL_OPT_CONNECT_TIMEOUT=%1;MYSQL_OPT_READ_TIMEOUT=%1;MYSQL_OPT_WRITE_TIMEOUT=%1;MYSQL_OPT_SSL_MODE=%2").arg(c["timeout"].toInt(10)).arg(ssl); if(mode=="verify") opts+=";SSL_CA="+c["ca"].toString(); db.setConnectOptions(opts);
        if (!db.open()) { auto f=classifyDatabaseError(db.lastError().nativeErrorCode().toInt()); auto msg=f["error"].toString(); db=QSqlDatabase(); QSqlDatabase::removeDatabase(name); throw Error{msg,f["code"].toString()}; }
        try { QSqlQuery q(db); sql(q,"SHOW SESSION STATUS LIKE 'Ssl_cipher'"); require(q.next(),"无法核实 TLS 状态","tls"); require(!(mode=="verify" || mode=="required") || !q.value(1).toString().isEmpty(),"未建立要求的 TLS 连接","tls"); sql(q,"SET SESSION time_zone = '+00:00'"); }
        catch (...) { db.close(); db=QSqlDatabase(); QSqlDatabase::removeDatabase(name); throw; }
    }
    ~Connection() { db.close(); db=QSqlDatabase(); QSqlDatabase::removeDatabase(name); }
};
struct Column { QString name,type,definition,charset,collation,pad; bool nullable; };
struct Meta { QList<Column> columns; QList<QStringList> keys; QStringList keyNames; QString table,engine; };
Meta metadata(QSqlDatabase db,const QJsonObject &endpoint) {
    for(auto k:{"database","table"}) { auto v=endpoint[k]; require(v.isString() && !v.toString().isEmpty() && v.toString().size()<=64 && !v.toString().contains(QChar::Null),"请选择有效库表","validation"); }
    Meta m; m.table=quote(endpoint["database"].toString())+"."+quote(endpoint["table"].toString()); QSqlQuery q(db);
    q.prepare("SELECT ENGINE, TABLE_TYPE FROM information_schema.TABLES WHERE TABLE_SCHEMA=? AND TABLE_NAME=?"); q.addBindValue(endpoint["database"].toString()); q.addBindValue(endpoint["table"].toString()); execute(q);
    require(q.next(),"表不存在或无权读取，不能视为空表","missing-table"); m.engine=q.value(0).toString(); require(q.value(1).toString()=="BASE TABLE","仅支持基础表数据读取","unsupported");
    q.prepare("SELECT c.COLUMN_NAME,c.DATA_TYPE,c.COLUMN_TYPE,c.IS_NULLABLE,c.CHARACTER_SET_NAME,c.COLLATION_NAME,co.PAD_ATTRIBUTE FROM information_schema.COLUMNS c LEFT JOIN information_schema.COLLATIONS co ON co.COLLATION_NAME=c.COLLATION_NAME WHERE c.TABLE_SCHEMA=? AND c.TABLE_NAME=? ORDER BY c.ORDINAL_POSITION"); q.addBindValue(endpoint["database"].toString()); q.addBindValue(endpoint["table"].toString()); execute(q);
    while(q.next()) m.columns.append({q.value(0).toString(),q.value(1).toString(),q.value(2).toString(),q.value(4).toString(),q.value(5).toString(),q.value(6).toString(),q.value(3).toString()=="YES"}); require(!m.columns.isEmpty(),"无法读取字段元数据","metadata");
    q.prepare("SELECT INDEX_NAME,COLUMN_NAME,SUB_PART FROM information_schema.STATISTICS WHERE TABLE_SCHEMA=? AND TABLE_NAME=? AND NON_UNIQUE=0 ORDER BY (INDEX_NAME='PRIMARY') DESC,INDEX_NAME,SEQ_IN_INDEX"); q.addBindValue(endpoint["database"].toString()); q.addBindValue(endpoint["table"].toString()); execute(q);
    QString current; QStringList key; bool valid=true;
    auto finish=[&] { if(!current.isEmpty() && valid && !key.isEmpty()) { m.keys<<key; m.keyNames<<current; } };
    while(q.next()) { auto name=q.value(0).toString(); if(name!=current) { finish(); current=name; key.clear(); valid=true; } auto field=q.value(1).toString(); auto it=std::find_if(m.columns.cbegin(),m.columns.cend(),[&](const Column &c){return c.name==field;}); if(it==m.columns.cend() || it->nullable || !q.value(2).isNull() || !supported(it->type) || it->type=="json" || it->type=="float" || it->type=="double") valid=false; key<<field; } finish(); return m;
}
const Column *column(const Meta &m,const QString &name) { for(const auto &c:m.columns) if(c.name==name) return &c; return nullptr; }
bool compatible(const Column &a,const Column &b) { return supported(a.type) && a.definition==b.definition && a.charset==b.charset && a.collation==b.collation && a.pad==b.pad; }
QJsonObject prepare(const Meta &l,const Meta &r) {
    QJsonArray fields,defaults,keys; QSet<QString> seen;
    for(const auto &a:l.columns) { const auto b=column(r,a.name); const bool yes=b && compatible(a,*b); seen.insert(a.name); fields.append(QJsonObject{{"name",a.name},{"type",a.definition},{"compatible",yes},{"reason",yes?"":!b?"右端缺少同名字段":!supported(a.type)?"当前类型不支持精确读取":"类型或字符集/排序规则不兼容"}}); if(yes) defaults.append(a.name); }
    for(const auto &b:r.columns) if(!seen.contains(b.name)) fields.append(QJsonObject{{"name",b.name},{"type",b.definition},{"compatible",false},{"reason","左端缺少同名字段"}});
    for(int i=0;i<l.keys.size();++i) { const auto &k=l.keys[i]; bool rightUnique=false; auto sorted=k; sorted.sort(); for(auto candidate:r.keys) { candidate.sort(); if(candidate==sorted) rightUnique=true; } if(!rightUnique) continue; bool yes=true; for(const auto &name:k) { const auto a=column(l,name),b=column(r,name); if(!a || !b || !compatible(*a,*b)) yes=false; } if(yes) keys.append(QJsonObject{{"name",l.keyNames[i]},{"fields",QJsonArray::fromStringList(k)}}); }
    return {{"fields",fields},{"keys",keys},{"defaultFields",defaults},{"defaultKey",keys.isEmpty()?QJsonArray{}:keys[0].toObject()["fields"].toArray()},{"canCompare",!keys.isEmpty()},{"keyReason",keys.isEmpty()?"没有两端兼容的完整非空唯一键；可空、前缀、函数索引及不支持的键类型已排除，仅可分别浏览":""}};
}
QString filterSql(const Meta &m,const QJsonArray &filters,QVariantList &values) {
    require(filters.size()<=32,"筛选条件过多","validation"); QStringList clauses;
    const QMap<QString,QString> ops{{"eq","="},{"ne","<>"},{"gt",">"},{"gte",">="},{"lt","<"},{"lte","<="},{"is-null","IS NULL"},{"is-not-null","IS NOT NULL"}};
    for(auto item:filters) { require(item.isObject(),"筛选格式无效","validation"); auto f=item.toObject(); auto c=column(m,f["field"].toString()); auto op=f["op"].toString(); op=ops.value(op,op.toUpper());
        require(c && supported(c->type) && QStringList{"=","!=","<>",">",">=","<","<=","IS NULL","IS NOT NULL"}.contains(op),"筛选字段或操作符无效","validation");
        const auto id=quote(c->name); if(op=="IS NULL" || op=="IS NOT NULL") { require(!f.contains("value") || f["value"].isString(),"NULL 条件值格式无效","validation"); clauses<<id+" "+op; continue; }
        require(f["value"].isString() && f["value"].toString().size()<=4096,"筛选值必须为精确文本，最多 4096 字符","validation"); auto v=f["value"].toString(); QString bind="?";
        require(c->type!="json","JSON 仅支持 NULL 条件","validation");
        if(numeric(c->type)) {
            bool valid=true; decimal(v,&valid); require(valid,"数值筛选格式无效","validation");
            if(QStringList{"tinyint","smallint","mediumint","int","bigint"}.contains(c->type)) {
                const bool uns=c->definition.contains("unsigned"); static const QRegularExpression integer("^-?[0-9]+$"); require(integer.match(v).hasMatch(),"整数筛选值无效","validation"); bool inRange=false;
                const auto sizes=QMap<QString,int>{{"tinyint",8},{"smallint",16},{"mediumint",24},{"int",32},{"bigint",64}}; const int bits=sizes[c->type];
                if(uns) { auto n=v.toULongLong(&inRange); inRange=inRange && !v.startsWith('-') && (bits==64 || n< (quint64(1)<<bits)); }
                else { auto n=v.toLongLong(&inRange); inRange=inRange && (bits==64 || (n>=-(qint64(1)<<(bits-1)) && n<(qint64(1)<<(bits-1)))); }
                require(inRange,"整数筛选值超出字段范围","validation"); bind=uns?"CAST(? AS UNSIGNED)":"CAST(? AS SIGNED)";
            } else if(c->type=="decimal") {
                static const QRegularExpression def("decimal\\(([0-9]+),([0-9]+)\\)"); auto d=def.match(c->definition); require(d.hasMatch(),"DECIMAL 定义不支持","validation");
                static const QRegularExpression fixed("^-?([0-9]+)(?:\\.([0-9]+))?$"); auto n=fixed.match(v); require(n.hasMatch(),"DECIMAL 筛选应使用普通十进制文本","validation"); auto whole=n.captured(1); while(whole.size()>1 && whole.startsWith('0')) whole.remove(0,1);
                require((whole=="0"?0:whole.size())<=d.captured(1).toInt()-d.captured(2).toInt() && n.captured(2).size()<=d.captured(2).toInt() && !(c->definition.contains("unsigned") && v.startsWith('-')),"DECIMAL 筛选值超出精度或范围","validation"); bind="CAST(? AS DECIMAL("+d.captured(1)+","+d.captured(2)+"))";
            } else { bool finite=false; auto n=v.toDouble(&finite); require(finite && std::isfinite(n),"浮点筛选值超出范围","validation"); bind="CAST(? AS DOUBLE)"; }
        } else if(binary(c->type)) { static const QRegularExpression hex("^(?:[0-9a-fA-F]{2})*$"); require(hex.match(v).hasMatch(),"二进制筛选值须为完整 HEX 字节","validation"); bind="UNHEX(?)"; if(c->type=="bit") { bool fits=false; auto n=v.toULongLong(&fits,16); const int bits=c->definition.section('(',1).section(')',0,0).toInt(); require(fits && bits>=1 && bits<=64 && (bits==64 || n<(quint64(1)<<bits)),"BIT 筛选超出字段范围","validation"); bind="CAST(CONV(?,16,10) AS UNSIGNED)"; } }
        else if(temporal(c->type)) {
            bool valid=false;
            if(c->type=="date") valid=QRegularExpression("^[0-9]{4}-[0-9]{2}-[0-9]{2}$").match(v).hasMatch() && QDate::fromString(v,"yyyy-MM-dd").isValid();
            else if(c->type=="year") valid=QRegularExpression("^[0-9]{4}$").match(v).hasMatch() && (v=="0000" || (v.toInt()>=1901 && v.toInt()<=2155));
            else if(c->type=="time") { auto t=QRegularExpression("^-?([0-9]{1,3}):([0-9]{2}):([0-9]{2})(?:\\.([0-9]{1,6}))?$").match(v); valid=t.hasMatch() && t.captured(1).toInt()<=838 && t.captured(2).toInt()<60 && t.captured(3).toInt()<60; }
            else { auto t=QRegularExpression("^([0-9]{4}-[0-9]{2}-[0-9]{2}) ([0-9]{2}):([0-9]{2}):([0-9]{2})(?:\\.[0-9]{1,6})?$").match(v); valid=t.hasMatch() && QDate::fromString(t.captured(1),"yyyy-MM-dd").isValid() && t.captured(2).toInt()<24 && t.captured(3).toInt()<60 && t.captured(4).toInt()<60; }
            require(valid,"时间筛选值格式或范围无效","validation"); bind=c->type=="year"?"CAST(? AS UNSIGNED)":c->type=="date"?"CAST(? AS DATE)":c->type=="time"?"CAST(? AS TIME(6))":"CAST(? AS DATETIME(6))";
        }
        clauses<<id+" "+op+" "+bind; values<<v;
    }
    return clauses.isEmpty()?QString{}:" WHERE "+clauses.join(" AND ");
}
struct LocalDb {
    QString name="data-result-"+QUuid::createUuid().toString(QUuid::WithoutBraces); QSqlDatabase db;
    LocalDb(const QString &path,bool readOnly=false) { db=QSqlDatabase::addDatabase("QSQLITE",name); db.setDatabaseName(path); if(readOnly) db.setConnectOptions("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=1000"); else db.setConnectOptions("QSQLITE_BUSY_TIMEOUT=1000"); if(!db.open()) { db=QSqlDatabase(); QSqlDatabase::removeDatabase(name); throw Error{"无法打开临时结果","storage"}; } }
    ~LocalDb() { db.close(); db=QSqlDatabase(); QSqlDatabase::removeDatabase(name); }
};
void saveStatus(const QString &path,const QJsonObject &status) { QSaveFile f(path+"/status.json"); auto b=QJsonDocument(status).toJson(QJsonDocument::Compact); require(f.open(QIODevice::WriteOnly) && f.write(b)==b.size() && f.commit(),"无法保存临时扫描状态","storage"); }
QString valueExpression(const Column &c) {
    const auto id=quote(c.name);
    if(c.type=="bit") { const int bits=c.definition.section('(',1).section(')',0,0).toInt(); return "LPAD(HEX(CAST("+id+" AS UNSIGNED)),"+QString::number((bits+7)/8*2)+",'0')"; }
    // FLOAT's direct text conversion rounds to display precision; promotion keeps adjacent values distinct.
    const auto expression=c.type=="float"?"CAST("+id+" AS DOUBLE)":id;
    return binary(c.type)?"HEX("+id+")":"HEX(CONVERT("+expression+" USING utf8mb4))";
}
QString keyExpression(const Column &c) { if(!c.collation.isEmpty()) return "HEX(WEIGHT_STRING("+(c.pad=="PAD SPACE"?"RTRIM("+quote(c.name)+")":quote(c.name))+"))"; return valueExpression(c); }
QJsonObject scan(const QJsonObject &input,Connection &left,Connection &right,const Meta &lm,const Meta &rm,const QJsonObject &preparation) {
    const auto args=input["args"].toObject(); const auto path=input["path"].toString(); const bool browse=args["mode"]=="browse"; require(args["mode"].isUndefined() || args["mode"]=="compare" || browse,"读取模式无效","validation");
    auto selected=strings(args["fields"]); require(!selected.isEmpty(),"至少选择一个参与字段","validation"); auto key=strings(args["key"]); const auto compatibleFields=preparation["defaultFields"].toArray();
    for(const auto &f:selected) require(compatibleFields.contains(f),"参与字段不兼容或不存在","validation");
    if(!browse) { bool found=false; for(auto k:preparation["keys"].toArray()) if(strings(k.toObject()["fields"])==key) found=true; require(found,"所选键不是两端兼容的完整非空唯一键","validation"); }
    if(browse) key.clear();
    require(args["filters"].isArray(),"筛选列表格式无效","validation"); const auto filters=args["filters"].toArray();
    for(auto f:filters) require(f.isObject() && compatibleFields.contains(f.toObject()["field"]),"共享筛选字段不兼容","validation");
    QVariantList lv,rv; const auto lw=filterSql(lm,filters,lv),rw=filterSql(rm,filters,rv);
    LocalDb local(path+"/rows.sqlite"); QSqlQuery q(local.db); sql(q,"PRAGMA journal_mode=WAL"); sql(q,"CREATE TABLE rows(id INTEGER PRIMARY KEY, identity TEXT UNIQUE, key_text TEXT, status TEXT, changed TEXT)"); sql(q,"CREATE TABLE cells(row_id INTEGER, side TEXT, field TEXT, type TEXT, raw BLOB, canonical TEXT, is_null INTEGER, encoding TEXT, PRIMARY KEY(row_id,side,field))"); sql(q,"CREATE INDEX row_status ON rows(status,id)"); sql(q,"CREATE INDEX row_key ON rows(key_text)");
    QJsonObject status{{"id",input["taskId"]},{"state","running"},{"phase","opening-snapshots"},{"leftScanned",0},{"rightScanned",0},{"batches",0},{"complete",false},{"mode",browse?"browse":"compare"},{"counts",QJsonObject{{"same",0},{"different",0},{"leftOnly",0},{"rightOnly",0}}},{"consistency","两端独立只读 REPEATABLE READ 快照；TIMESTAMP 会话时区 UTC；不是全局同一瞬间"}};
    bool transactional=lm.engine.compare("InnoDB",Qt::CaseInsensitive)==0 && rm.engine.compare("InnoDB",Qt::CaseInsensitive)==0;
    if(!transactional) status["consistency"]="包含非 InnoDB 表：只读事务不能保证这些表的一致快照，扫描期间变化可能影响结果；TIMESTAMP 时区 UTC；不是全局同一瞬间";
    for(auto pair:{qMakePair(QString("left"),&left),qMakePair(QString("right"),&right)}) { QSqlQuery tx(pair.second->db); sql(tx,"SET SESSION TRANSACTION ISOLATION LEVEL REPEATABLE READ"); sql(tx,"START TRANSACTION WITH CONSISTENT SNAPSHOT, READ ONLY"); status[pair.first+"StartedAt"]=stamp(); }
    saveStatus(path,status);
    try {
        for(const auto &side:{QString("left"),QString("right")}) {
            auto &remote=side=="left"?left:right; const auto &meta=side=="left"?lm:rm; const auto &where=side=="left"?lw:rw; const auto &bindings=side=="left"?lv:rv;
            QStringList fetched=selected; for(const auto &f:key) if(!fetched.contains(f)) fetched<<f;
            QStringList expressions; for(const auto &f:fetched) expressions<<valueExpression(*column(meta,f)); for(const auto &f:key) expressions<<keyExpression(*column(meta,f));
            QStringList order; for(const auto &f:key) order<<quote(f);
            qint64 count=0; status["phase"]="scanning-"+side; saveStatus(path,status);
            while(true) {
                // OFFSET keeps each driver result bounded; very deep scans cost more server work.
                QSqlQuery read(remote.db); read.setForwardOnly(true); auto query="SELECT "+expressions.join(',')+" FROM "+meta.table+where+(order.isEmpty()?QString{}:" ORDER BY "+order.join(','))+" LIMIT 256 OFFSET "+QString::number(count);
                require(read.prepare(query),"无法准备只读批次查询","query"); for(const auto &v:bindings) read.addBindValue(v); execute(read);
                require(local.db.transaction(),"无法开始临时结果批次","storage"); int batch=0;
                while(read.next()) {
                    ++batch; ++count; QJsonArray display,identity;
                    for(int i=0;i<key.size();++i) { const int index=fetched.indexOf(key[i]); require(!read.value(index).isNull() && !read.value(fetched.size()+i).isNull(),"扫描发现 NULL 键，无法可靠匹配","invalid-key"); auto c=column(meta,key[i]); auto raw=binary(c->type)?read.value(index).toString():QString::fromUtf8(QByteArray::fromHex(read.value(index).toByteArray())); display.append(raw); auto ident=read.value(fetched.size()+i).toString(); if(c->collation.isEmpty() && numeric(c->type)) { bool ok=true; ident=canonicalDataValue(c->type,QString::fromUtf8(QByteArray::fromHex(ident.toLatin1())),&ok); require(ok,"键值无法精确读取","invalid-key"); } identity.append(ident); }
                    const auto identityText=browse?side+":"+QString::number(count):json(identity); q.prepare("SELECT id,status FROM rows WHERE identity=?"); q.addBindValue(identityText); execute(q); const bool exists=q.next(); qint64 rowId=exists?q.value(0).toLongLong():0;
                    require(!exists || (side=="right" && q.value(1).toString()=="pending-left"),"扫描发现重复键，无法可靠匹配","invalid-key");
                    if(!exists) { q.prepare("INSERT INTO rows(identity,key_text,status,changed) VALUES(?,?,?,?)"); q.addBindValue(identityText); q.addBindValue(json(display)); q.addBindValue(browse?"unmatched":side=="left"?"pending-left":"pending-right"); q.addBindValue("[]"); execute(q); rowId=q.lastInsertId().toLongLong(); }
                    QJsonArray changed;
                    for(const auto &f:selected) {
                        const int index=fetched.indexOf(f); const auto c=column(meta,f); const bool isNull=read.value(index).isNull(); const auto raw=isNull?QString{}:binary(c->type)?read.value(index).toString():QString::fromUtf8(QByteArray::fromHex(read.value(index).toByteArray())); bool ok=true; const auto canon=isNull?QString{}:canonicalDataValue(c->type,raw,&ok); require(ok,"字段 "+f+" 含无法可靠比较的值","unsupported");
                        if(exists) { q.prepare("SELECT canonical,is_null FROM cells WHERE row_id=? AND side='left' AND field=?"); q.addBindValue(rowId); q.addBindValue(f); execute(q); require(q.next(),"临时原值缺失，请重新比对","storage"); if(q.value(1).toBool()!=isNull || (!isNull && q.value(0).toString()!=canon)) changed.append(f); }
                        q.prepare("INSERT INTO cells(row_id,side,field,type,raw,canonical,is_null,encoding) VALUES(?,?,?,?,?,?,?,?)"); q.addBindValue(rowId); q.addBindValue(side); q.addBindValue(f); q.addBindValue(c->type); q.addBindValue(storedText(raw)); q.addBindValue(canon); q.addBindValue(isNull); q.addBindValue(binary(c->type)?"hex":"text"); execute(q);
                    }
                    if(exists) { q.prepare("UPDATE rows SET status=?,changed=? WHERE id=?"); q.addBindValue(changed.isEmpty()?"same":"different"); q.addBindValue(json(changed)); q.addBindValue(rowId); execute(q); }
                }
                require(!read.lastError().isValid(),"批次读取中断，结果不完整","query"); require(local.db.commit(),"临时结果提交失败","storage"); status[side+"Scanned"]=double(count); status["batches"]=status["batches"].toInt()+1; saveStatus(path,status); if(batch<256) break;
            }
            QSqlQuery tx(remote.db); sql(tx,"COMMIT"); status[side+"FinishedAt"]=stamp(); saveStatus(path,status);
        }
        if(!browse) { sql(q,"UPDATE rows SET status='left-only' WHERE status='pending-left'"); sql(q,"UPDATE rows SET status='right-only' WHERE status='pending-right'"); }
        QJsonObject counts{{"same",0},{"different",0},{"leftOnly",0},{"rightOnly",0}}; sql(q,"SELECT status,COUNT(*) FROM rows GROUP BY status"); const QMap<QString,QString> names{{"same","same"},{"different","different"},{"left-only","leftOnly"},{"right-only","rightOnly"}}; while(q.next()) if(names.contains(q.value(0).toString())) counts[names[q.value(0).toString()]]=double(q.value(1).toLongLong()); status["counts"]=counts; status["complete"]=true; status["state"]="complete"; status["phase"]="complete"; saveStatus(path,status); return {{"ok",true},{"task",status}};
    } catch(const Error &e) { local.db.rollback(); status["state"]="failed"; status["phase"]="failed"; status["error"]=e.message; status["code"]=e.code; for(auto side:{"left","right"}) if(!status.contains(QString(side)+"FinishedAt")) status[QString(side)+"FinishedAt"]=stamp(); saveStatus(path,status); return failure(e.message,e.code); }
}
}

QString canonicalDataValue(const QString &type,const QString &text,bool *ok) {
    *ok=true; try { if(type=="json") return ExactJson(text).parse(); if(numeric(type)) return decimal(text,ok); if(binary(type)) return text.toUpper(); return text; } catch(const Error &) { *ok=false; return {}; }
}
QJsonObject runDataCommand(const QJsonObject &input) {
    try { Connection left(input["left"].toObject()),right(input["right"].toObject()); auto args=input["args"].toObject(); auto l=metadata(left.db,args["left"].toObject()),r=metadata(right.db,args["right"].toObject()); auto p=prepare(l,r); if(input["operation"]=="data-prepare") return {{"ok",true},{"preparation",p}}; require(input["operation"]=="data-start","无效数据操作","validation"); return scan(input,left,right,l,r,p); }
    catch(const Error &e) { return failure(e.message,e.code); }
}

QJsonObject readDataResult(const QString &path,const QString &operation,const QJsonObject &args,bool complete) {
    try {
        if(operation=="data-page" && !complete && !QFile::exists(path+"/rows.sqlite")) return {{"ok",true},{"rows",QJsonArray{}},{"total",0},{"offset",0},{"limit",50},{"complete",false}};
        require(QFile::exists(path+"/rows.sqlite"),"当前任务尚无可浏览结果","pending"); LocalDb local(path+"/rows.sqlite",true); QSqlQuery q(local.db);
        auto bounded=[&](const QString &name,int fallback,int low,int high) { const auto v=args[name]; if(v.isUndefined()) return fallback; require(v.isDouble() && v.toDouble()==v.toInt(-1) && v.toInt()>=low && v.toInt()<=high,"分页或分块参数无效","validation"); return v.toInt(); };
        if(operation=="data-page") {
            int offset=bounded("offset",0,0,2147483647),limit=bounded("limit",50,1,200); auto status=args["status"].toString("all");
            require(QStringList{"all","same","different","left-only","right-only","unmatched"}.contains(status),"状态筛选无效","validation");
            QStringList predicates; QVariantList binds;
            if(!complete) predicates<<"status IN ('same','different','unmatched')";
            if(status!="all") { predicates<<"status=?"; binds<<status; }
            if(args.contains("key")) { require(args["key"].isArray(),"定位键格式无效","validation"); const auto key=args["key"].toArray(); for(auto v:key) require(v.isString(),"定位键必须为精确文本","validation"); predicates<<"key_text=?"; binds<<json(key); }
            if(args.contains("side")) { const auto side=args["side"].toString(); require(side=="left" || side=="right","浏览端无效","validation"); predicates<<"EXISTS (SELECT 1 FROM cells WHERE row_id=rows.id AND side=?)"; binds<<side; }
            auto where=predicates.isEmpty()?QString{}:" WHERE "+predicates.join(" AND "); q.prepare("SELECT COUNT(*) FROM rows"+where); for(const auto &v:binds) q.addBindValue(v); execute(q); require(q.next(),"无法读取页计数","storage"); const auto total=q.value(0).toLongLong();
            q.prepare("SELECT id,key_text,status,changed FROM rows"+where+" ORDER BY id LIMIT ? OFFSET ?"); for(const auto &v:binds) q.addBindValue(v); q.addBindValue(limit); q.addBindValue(offset); execute(q); QJsonArray rows;
            while(q.next()) rows.append(QJsonObject{{"id",QString::number(q.value(0).toLongLong())},{"key",array(q.value(1).toString())},{"status",q.value(2).toString()},{"changedFields",array(q.value(3).toString())}});
            return {{"ok",true},{"rows",rows},{"total",double(total)},{"offset",offset},{"limit",limit},{"complete",complete}};
        }
        require(operation=="data-detail","无效数据结果操作","validation"); bool rowOk=false; auto rowId=args["rowId"].toString().toLongLong(&rowOk); require(rowOk && rowId>0,"记录标识无效","validation");
        q.prepare("SELECT changed,status FROM rows WHERE id=?"); q.addBindValue(rowId); execute(q); require(q.next(),"当前扫描原记录不存在，请重新比对","stale"); auto changed=array(q.value(0).toString()); const auto rowStatus=q.value(1).toString(); require(complete || rowStatus=="same" || rowStatus=="different" || rowStatus=="unmatched","记录尚未完成匹配","pending");
        auto value=[&](const QString &field,const QString &side,int offset,int limit)->QJsonValue { QSqlQuery cell(local.db); cell.prepare("SELECT is_null,substr(raw,?,?),length(raw)/4,encoding FROM cells WHERE row_id=? AND side=? AND field=?"); cell.addBindValue(qint64(offset)*4+1); cell.addBindValue(qint64(limit)*4); cell.addBindValue(rowId); cell.addBindValue(side); cell.addBindValue(field); execute(cell); if(!cell.next()) return QJsonValue(QJsonValue::Null); const auto length=cell.value(2).toLongLong(); require(offset<=length,"分块偏移超出原值范围","validation"); return QJsonObject{{"isNull",cell.value(0).toBool()},{"text",loadedText(cell.value(1).toByteArray())},{"length",double(length)},{"encoding",cell.value(3).toString()},{"truncated",offset+limit<length}}; };
        if(args.contains("field")) { const auto field=args["field"].toString(),side=args["side"].toString(); require(!field.isEmpty() && (side=="left" || side=="right"),"字段或读取端无效","validation"); int offset=bounded("offset",0,0,2147483646),limit=bounded("limit",65536,1,65536); auto v=value(field,side,offset,limit); require(!v.isNull(),"当前扫描没有该端字段原值","stale"); auto o=v.toObject(); return {{"ok",true},{"value",o},{"offset",offset},{"nextOffset",o["truncated"].toBool()?QJsonValue(offset+limit):QJsonValue(QJsonValue::Null)}}; }
        q.prepare("SELECT field,type FROM cells WHERE row_id=? GROUP BY field ORDER BY min(rowid)"); q.addBindValue(rowId); execute(q); QList<QPair<QString,QString>> names; while(q.next()) names.append({q.value(0).toString(),q.value(1).toString()}); QJsonArray fields;
        for(const auto &f:names) fields.append(QJsonObject{{"name",f.first},{"type",f.second},{"changed",changed.contains(f.first) || rowStatus=="left-only" || rowStatus=="right-only"},{"left",value(f.first,"left",0,256)},{"right",value(f.first,"right",0,256)}});
        return {{"ok",true},{"fields",fields}};
    } catch(const Error &e) { return failure(e.message,e.code); }
}
