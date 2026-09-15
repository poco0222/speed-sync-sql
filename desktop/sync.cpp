#include "sync.h"
#include "schema.h"
#include "foundation.h"
#include <QJsonArray>
#include <QMap>
#include <QSet>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <algorithm>

namespace {
QString quote(QString s) { return "`" + s.replace('`', "``") + "`"; }
QString literal(QString s, const QString &mode = {}) { if(!mode.split(',').contains("NO_BACKSLASH_ESCAPES"))s.replace('\\', "\\\\"); return "'" + s.replace('\'', "''") + "'"; }
QJsonObject fail(QString reason) { return {{"ok", false}, {"error", reason}, {"code", "sync-plan"}, {"blockers", QJsonArray{reason}}}; }
QMap<QString,QJsonObject> items(const QJsonObject &s, const QString &cat) {
    QMap<QString,QJsonObject> out;
    for (auto v : s["categories"].toObject()[cat].toObject()["items"].toArray()) { auto o=v.toObject(); out[o["name"].toString()]=o; }
    return out;
}
// Split SHOW CREATE's table body only at top-level commas. Quoted defaults,
// expression parentheses and version comments remain byte-for-byte intact.
struct Definition { QMap<QString,QString> columns, indexes; QString body, suffix; bool valid=false; };
Definition parse(const QString &ddl, const QString &mode) {
    Definition out; int depth=0; qsizetype start=-1, begin=-1; QChar quoted; QStringList parts;
    const bool escapes=!mode.split(',').contains("NO_BACKSLASH_ESCAPES");
    for (qsizetype i=0;i<ddl.size();++i) {
        auto c=ddl[i];
        if (!quoted.isNull()) { if (c=='\\' && escapes && quoted!='`') { ++i; continue; } if(c==quoted) { if(i+1<ddl.size() && ddl[i+1]==quoted) ++i; else quoted=QChar(); } continue; }
        if(c=='\'' || c=='"' || c=='`') { quoted=c; continue; }
        if(ddl.mid(i,2)=="/*") { auto end=ddl.indexOf("*/",i+2); if(end<0)return out; i=end+1;continue; }
        if(c=='(') { if(depth++==0) start=begin=i+1; }
        else if(c==')') { if(--depth==0) { parts<<ddl.mid(start,i-start).trimmed(); out.body=ddl.mid(begin,i-begin); out.suffix=ddl.mid(i+1).trimmed(); break; } }
        else if(c==',' && depth==1) { parts<<ddl.mid(start,i-start).trimmed(); start=i+1; }
    }
    const QRegularExpression column("^`((?:``|[^`])+)`\\s+"), index("^(?:(?:UNIQUE|FULLTEXT|SPATIAL) )?KEY `((?:``|[^`])+)`\\s*\\(");
    if(begin<0 || depth!=0 || parts.isEmpty()) return out;
    for(const auto &part:parts) {
        auto c=column.match(part), k=index.match(part);
        if(c.hasMatch()) out.columns[c.captured(1).replace("``","`")]=part;
        else if(k.hasMatch()) out.indexes[k.captured(1).replace("``","`")]=part;
        else if(part.startsWith("PRIMARY KEY ")) out.indexes["PRIMARY"]=part;
        else if(!part.startsWith("CONSTRAINT ")) return out;
    }
    out.valid=true;return out;
}
bool special(const QJsonObject &s) {
    if(s["existence"]=="missing") return false;
    for(const auto &cat:QStringList{"columns","indexes","constraints","table","triggers"}) if(s["categories"].toObject()[cat].toObject()["state"]!="complete") return true;
    for(auto o:items(s,"columns")) if(!o["properties"].toObject()["generationExpression"].toString().isEmpty()) return true;
    for(auto o:items(s,"indexes")) for(auto p:o["properties"].toObject()["parts"].toArray()) if(!p.toObject()["expression"].toString().isEmpty()) return true;
    for(auto o:items(s,"constraints")) for(auto p:o["properties"].toObject()["parts"].toArray()) if(!QStringList{"PRIMARY KEY","UNIQUE"}.contains(p.toObject()["type"].toString())) return true;
    return SchemaDetails::hasUnsupportedSyntax(s["ddl"].toString(),s["sqlMode"].toString());
}
}

QJsonObject buildSyncPlan(const QJsonObject &comparison,const QJsonObject &args) {
    const auto direction=args["direction"].toString();
    if(direction!="left-to-right" && direction!="right-to-left") return fail("无效同步方向");
    auto left=comparison["left"].toObject(), right=comparison["right"].toObject();
    auto source=direction=="left-to-right"?left:right, target=direction=="left-to-right"?right:left;
    if(!source["ok"].toBool() || source["existence"]!="present") return fail("读取端不存在或读取失败；禁止删除整表");
    if(!target["ok"].toBool() || !QStringList{"present","missing"}.contains(target["existence"].toString())) return fail("写入端存在性未确认");
    if(source["serverUuid"].toString().isEmpty() || target["serverUuid"].toString().isEmpty() || source["lowerCaseTableNames"].isUndefined() || source["lowerCaseTableNames"]!=target["lowerCaseTableNames"]) return fail("服务器身份或标识符规则未确认");
    auto cs=source["lowerCaseTableNames"].toString()=="0"?Qt::CaseSensitive:Qt::CaseInsensitive;
    if(source["serverUuid"]==target["serverUuid"] && source["database"].toString().compare(target["database"].toString(),cs)==0 && source["table"].toString().compare(target["table"].toString(),cs)==0) return fail("禁止同一物理表自身同步");
    if(!source["syncGuard"].toObject()["complete"].toBool() || !target["syncGuard"].toObject()["complete"].toBool()) return fail("依赖可见性未证实；需要全局 SELECT 和库级 TRIGGER 元数据权限");
    const bool create=target["existence"]=="missing", all=args["alignAll"].toBool();
    if((create||all) && (special(source)||special(target))) return fail("完整对齐或建表包含仅展示的特殊对象，无法生成完整计划");
    QSet<QString> selected;
    for(auto v:args["selected"].toArray()) { auto o=v.toObject(); selected.insert(o["category"].toString()+"/"+o["name"].toString()); }
    if(all) for(auto v:comparison["rows"].toArray()) { auto r=v.toObject(); if(r["status"]!="same" && r["category"]!="constraints") selected.insert(r["category"].toString()+"/"+r["name"].toString()); }
    if(all) {
        bool changedColumns=false;
        for(const auto &key:selected)if(key.startsWith("columns/"))changedColumns=true;
        if(changedColumns)for(const auto &endpoint:{source,target})for(const auto &category:QStringList{"indexes","triggers"})for(auto o:items(endpoint,category))selected.insert(category+"/"+o["name"].toString());
    }
    for(const auto &key:selected) {
        auto category=key.section('/',0,0);
        for(const auto &endpoint:{source,target})if(endpoint["existence"]!="missing" && endpoint["categories"].toObject()[category].toObject()["state"]!="complete")return fail("选区元数据不完整："+key);
    }
    if(!all && selected.isEmpty()) return fail("请先选择同步对象");
    const auto from=parse(source["ddl"].toString(),source["sqlMode"].toString()), to=parse(target["ddl"].toString(),target["sqlMode"].toString());
    if(!from.valid || (!create && !to.valid)) return fail("无法可靠解析原始建表定义");
    const auto qualified=quote(target["database"].toString())+"."+quote(target["table"].toString());
    // Reject ambiguous cross-end names before resolving selection by exact key.
    for(auto v:comparison["rows"].toArray()) {auto r=v.toObject();if(r["status"]=="unsupported" && r["reason"].toString().contains("大小写"))return fail("对象名仅大小写不同，无法安全同步");}
    QJsonArray steps;
    const auto step=[&](QString sql,QString category,QString name,QString summary,QJsonObject context=QJsonObject{}) {
        if(context.isEmpty()) context=QJsonObject{{"sqlMode",source["sqlMode"]}};
        steps.append(QJsonObject{{"sql",sql},{"category",category},{"name",name},{"summary",summary},{"risk","DDL 可能重建或锁表；删除、类型/字符集转换、NOT NULL 和唯一性可能丢失数据或失败；不保证整体回滚"},{"context",context}});
    };
    QStringList alters; QSet<QString> columnChanges;
    for(const auto &key:selected) if(key.startsWith("columns/")) columnChanges.insert(key.mid(8));
    if(!columnChanges.isEmpty() && !create) {
        if(special(source)||special(target)||!target["syncGuard"].toObject()["incoming"].toArray().isEmpty()) return fail("字段变更的约束、生成列或入向外键依赖无法安全处理");
        for(auto trigger:items(target,"triggers")) if(!selected.contains("triggers/"+trigger["name"].toString())) return fail("字段变更涉及触发器；请明确纳入 triggers/"+trigger["name"].toString());
        for(auto index:items(target,"indexes")) for(auto p:index["properties"].toObject()["parts"].toArray()) if(columnChanges.contains(p.toObject()["columnName"].toString()) && !selected.contains("indexes/"+index["name"].toString())) return fail("字段变更涉及索引；请明确纳入 indexes/"+index["name"].toString());
    }
    if(create) {
        if(!all) return fail("目标缺表时请使用完整结构对齐");
        auto ddl=SchemaDetails::definitionFingerprint("CREATE TABLE "+qualified+" ("+from.body+") "+from.suffix,source["sqlMode"].toString());
        step(ddl,"table","table","创建目标表（不复制 AUTO_INCREMENT 当前计数）");
    } else {
        for(const auto &key:selected) {
            const auto cat=key.section('/',0,0), name=key.mid(cat.size()+1);
            if(cat=="triggers" || cat=="columns")continue;
            if(cat=="indexes") {
                if(!target["syncGuard"].toObject()["incoming"].toArray().isEmpty())return fail("索引存在入向外键依赖，仅展示");
                for(auto o:items(target,"constraints"))for(auto p:o["properties"].toObject()["parts"].toArray())if(p.toObject()["type"]=="FOREIGN KEY")return fail("索引存在外键依赖，仅展示");
                auto si=items(source,cat), ti=items(target,cat);
                if(!si.contains(name) && !ti.contains(name)) return fail("选区对象不存在："+key);
                for(auto o:{si.value(name),ti.value(name)}) for(auto v:o["properties"].toObject()["parts"].toArray()) {
                    auto p=v.toObject(); if(!p["expression"].toString().isEmpty()) return fail("函数索引仅展示："+name);
                    auto col=p["columnName"].toString(); if(!to.columns.contains(col) && !columnChanges.contains(col))return fail("索引依赖字段；请纳入 columns/"+col);
                }
                if(ti.contains(name)) alters<<(name=="PRIMARY"?"DROP PRIMARY KEY":"DROP INDEX "+quote(name));
                if(si.contains(name)) { if(!from.indexes.contains(name))return fail("索引定义无法定位："+name); alters<<"ADD "+from.indexes[name]; }
            } else if(cat=="table" && name=="table") {
                auto a=items(source,cat).value("table")["properties"].toObject(), b=items(target,cat).value("table")["properties"].toObject();
                if(a["engine"]!=b["engine"] || source["categories"].toObject()[cat].toObject()["state"]!="complete" || target["categories"].toObject()[cat].toObject()["state"]!="complete")return fail("引擎转换或特殊表属性仅展示");
                alters<<"DEFAULT CHARACTER SET "+quote(a["charset"].toString())+" COLLATE "+quote(a["collation"].toString());
                alters<<"COMMENT = "+literal(a["comment"].toString(),source["sqlMode"].toString());
                if(!QStringList{"Dynamic","Compact","Redundant","Compressed"}.contains(a["rowFormat"].toString()))return fail("未知行格式");
                alters<<"ROW_FORMAT = "+a["rowFormat"].toString().toUpper();
            } else return fail("选区不支持："+key);
        }
        QList<QJsonObject> columns=items(source,"columns").values();
        std::sort(columns.begin(),columns.end(),[](auto a,auto b){return a["properties"].toObject()["ordinal"].toString().toInt()<b["properties"].toObject()["ordinal"].toString().toInt();});
        QString previous;
        for(const auto &c:columns) {
            auto name=c["name"].toString();
            if(columnChanges.contains(name)) {
                if(!previous.isEmpty() && !to.columns.contains(previous) && !columnChanges.contains(previous))return fail("字段顺序依赖；请纳入 columns/"+previous);
                if(!from.columns.contains(name))return fail("字段定义无法定位："+name);
                auto fragment=from.columns[name]; auto props=c["properties"].toObject();
                if(!props["charset"].toString().isEmpty()) {
                    auto prefix=quote(name)+" "+props["type"].toString();
                    if(!fragment.startsWith(prefix,Qt::CaseInsensitive))return fail("字段类型定义无法定位："+name);
                    auto tail=fragment.mid(prefix.size());
                    tail.remove(QRegularExpression("^\\s+(?:CHARACTER SET \\w+\\s*)?(?:COLLATE \\w+\\s*)?",QRegularExpression::CaseInsensitiveOption));
                    fragment=prefix+" CHARACTER SET "+quote(props["charset"].toString())+" COLLATE "+quote(props["collation"].toString())+" "+tail;
                }
                alters<<(to.columns.contains(name)?"MODIFY COLUMN ":"ADD COLUMN ")+fragment+(previous.isEmpty()?" FIRST":" AFTER "+quote(previous));
            }
            previous=name;
        }
        for(const auto &name:columnChanges) if(!from.columns.contains(name)) { if(!to.columns.contains(name))return fail("选区字段不存在："+name); alters<<"DROP COLUMN "+quote(name); }
        if(!alters.isEmpty()) step("ALTER TABLE "+qualified+"\n  "+alters.join(",\n  "),"table","table",all?"完整对齐：应用字段、索引与表属性（含字段关联索引）":"应用选中字段、索引与表属性");
    }
    QJsonArray drops, adds;
    for(const auto &key:selected) if(key.startsWith("triggers/")) {
        auto name=key.mid(9); auto si=items(source,"triggers"),ti=items(target,"triggers");
        auto s=si.value(name),t=ti.value(name), p=s.value("properties").toObject();
        if(s.isEmpty() && t.isEmpty())return fail("触发器不存在："+name);
        for(const auto &endpoint:{source,target}) {
            if(endpoint["existence"]=="missing")continue;
            if(endpoint["categories"].toObject()["triggers"].toObject()["state"]!="complete")return fail("触发器上下文读取不完整");
            auto own=items(endpoint,"triggers").value(name)["properties"].toObject();
            if(own.isEmpty())own=p;
            if(own.isEmpty())continue;
            for(auto o:items(endpoint,"triggers")) { auto other=o["properties"].toObject(); if(o["name"]!=name && other["event"]==own["event"] && other["timing"]==own["timing"])return fail("同事件触发器顺序无法可靠重现"); }
        }
        if(!s.isEmpty()) {
            for(auto c:items(source,"columns"))if(!create && !to.columns.contains(c["name"].toString()) && !columnChanges.contains(c["name"].toString()))return fail("触发器依赖无法证实；请纳入 columns/"+c["name"].toString());
            if(p["databaseCollation"]!=target["databaseCollation"] || !p.contains("sqlMode") || p["characterSetClient"].toString().isEmpty() || p["collationConnection"].toString().isEmpty())return fail("无法保留触发器字符集/数据库排序上下文");
            for(auto n:target["syncGuard"].toObject()["triggerNames"].toArray()) {auto o=n.toObject();if(o["name"].toString().compare(name,Qt::CaseInsensitive)==0 && o["properties"].toObject()["tableName"]!=target["table"])return fail("目标库触发器名称冲突："+name);}
            // QMYSQL sends QString statements as UTF-8; another client encoding
            // would reinterpret the trigger body, even though SET itself succeeds.
            if(p["characterSetClient"]!="utf8mb4" && p["characterSetClient"]!="utf8mb3" && p["characterSetClient"]!="utf8")return fail("QMYSQL 无法可靠保留此触发器客户端编码，仅展示");
            const auto definer=p["definer"].toString(); auto at=definer.lastIndexOf('@');
            if(at<=0 || !QStringList{"INSERT","UPDATE","DELETE"}.contains(p["event"].toString()) || !QStringList{"BEFORE","AFTER"}.contains(p["timing"].toString()) || p["body"].toString().isEmpty())return fail("触发器定义无法可靠解释");
            auto account=quote(definer.left(at))+"@"+quote(definer.mid(at+1));
            const auto original=s.value("ddl").toString();
            if(!original.isEmpty()) {
                auto match=QRegularExpression("^CREATE\\s+DEFINER\\s*=\\s*(`(?:``|[^`])*`@`(?:``|[^`])*`)\\s+TRIGGER\\s",QRegularExpression::CaseInsensitiveOption).match(original);
                if(!match.hasMatch())return fail("无法可靠定位原 DEFINER");
                account=match.captured(1);
            } else if(definer.count('@')!=1)return fail("DEFINER 账户边界不明确");
            auto sql="CREATE DEFINER="+account+" TRIGGER "+quote(target["database"].toString())+"."+quote(name)+" "+p["timing"].toString()+" "+p["event"].toString()+" ON "+qualified+" FOR EACH ROW "+p["body"].toString();
            step(sql,"triggers",name,"创建触发器，保留原 DEFINER 和正文",{{"sqlMode",p["sqlMode"]},{"characterSetClient",p["characterSetClient"]},{"collationConnection",p["collationConnection"]}}); adds.append(steps.takeAt(steps.size()-1));
        }
        if(!t.isEmpty()) {step("DROP TRIGGER "+quote(target["database"].toString())+"."+quote(name),"triggers",name,"删除原触发器；后续创建失败不会回滚");drops.append(steps.takeAt(steps.size()-1));}
    }
    QJsonArray ordered=drops;for(auto s:steps)ordered.append(s);for(auto s:adds)ordered.append(s);steps=ordered;
    if(steps.isEmpty())return fail("没有可执行结构变更");
    QJsonArray operations; QJsonObject counts{{"add",0},{"modify",0},{"delete",0}}; QStringList structuralSummaries;
    const auto operation=[&](const QString &category,const QString &name,const QString &action,bool related=false) {
        const auto verb=action=="add"?QString("新增"):action=="delete"?QString("删除"):QString("修改");
        const auto label=category=="columns"?QString("字段"):category=="indexes"?QString("索引"):category=="triggers"?QString("触发器"):QString("表");
        const auto summary=verb+label+" "+name+(related?"（字段关联重建）":"");
        operations.append(QJsonObject{{"category",category},{"name",name},{"action",action},{"summary",summary}}); counts[action]=counts[action].toInt()+1;
        if(category!="triggers")structuralSummaries<<summary;
    };
    if(create)operation("table",target["table"].toString(),"add");
    auto keys=selected.values(); keys.sort();
    for(const auto &key:keys) {
        const auto category=key.section('/',0,0),name=key.mid(category.size()+1);
        if(create && category!="triggers")continue;
        const auto si=items(source,category),ti=items(target,category);
        const auto action=!ti.contains(name)?QString("add"):!si.contains(name)?QString("delete"):QString("modify");
        const bool related=all && !columnChanges.isEmpty() && (category=="indexes"||category=="triggers") && si.value(name)==ti.value(name);
        operation(category,name,action,related);
    }
    for(qsizetype i=0;i<steps.size();++i) {auto s=steps[i].toObject();if(s["category"]=="table") {s["summary"]=structuralSummaries.join("；");steps[i]=s;}}
    QString script="USE "+quote(target["database"].toString())+";\n";
    for(auto v:steps) {auto s=v.toObject(),c=s["context"].toObject(); script+="SET SESSION sql_mode = "+literal(c["sqlMode"].toString())+";\n"; if(c.contains("characterSetClient"))script+="SET character_set_client = "+literal(c["characterSetClient"].toString())+", collation_connection = "+literal(c["collationConnection"].toString())+";\n"; auto sql=s["sql"].toString();if(sql.startsWith("CREATE DEFINER")) {QString delimiter="$$";while(sql.contains(delimiter))delimiter+="$";script+="DELIMITER "+delimiter+"\n"+sql+delimiter+"\nDELIMITER ;\n";}else script+=sql+";\n";}
    return {{"ok",true},{"plan",QJsonObject{{"direction",direction},{"left",left},{"right",right},{"steps",steps},{"operations",operations},{"counts",counts},{"sql",script},{"alignAll",all},{"selected",args["selected"]}}}};
}

QJsonObject executeSyncStep(const QJsonObject &connection,const QJsonObject &target,const QJsonObject &step) {
    auto invalid=validateConnection(connection);if(!invalid.isEmpty())return fail(invalid);
    if(target["database"].toString().isEmpty() || step["sql"].toString().isEmpty())return fail("执行参数不完整");
    if(!QSqlDatabase::isDriverAvailable("QMYSQL"))return fail("QMYSQL 驱动缺失或无法加载");
    auto name="sync-"+QUuid::createUuid().toString(QUuid::WithoutBraces);auto cleanup=qScopeGuard([&]{QSqlDatabase::removeDatabase(name);});auto db=QSqlDatabase::addDatabase("QMYSQL",name);
    db.setHostName(connection["host"].toString());db.setPort(connection["port"].toInt());db.setUserName(connection["user"].toString());db.setPassword(connection["password"].toString());
    auto mode=connection["tls"].toString();auto ssl=mode=="verify"?"SSL_MODE_VERIFY_IDENTITY":mode=="required"?"SSL_MODE_REQUIRED":mode=="disabled"?"SSL_MODE_DISABLED":"SSL_MODE_PREFERRED";
    auto options=QString("MYSQL_OPT_CONNECT_TIMEOUT=%1;MYSQL_OPT_READ_TIMEOUT=%1;MYSQL_OPT_WRITE_TIMEOUT=%1;MYSQL_OPT_SSL_MODE=%2").arg(connection["timeout"].toInt(10)).arg(ssl);if(mode=="verify")options+=";SSL_CA="+connection["ca"].toString();db.setConnectOptions(options);
    if(!db.open())return classifyDatabaseError(db.lastError().nativeErrorCode().toInt());QSqlQuery q(db);
    if(!q.exec("SHOW SESSION STATUS LIKE 'Ssl_cipher'") || !q.next() || ((mode=="required"||mode=="verify") && q.value(1).toString().isEmpty()))return fail("无法核实所要求的 TLS 会话");
    if(!q.exec("SELECT @@server_uuid") || !q.next() || target["serverUuid"].toString().isEmpty() || q.value(0).toString()!=target["serverUuid"].toString())return fail("连接的服务器身份已变化");
    if(!q.exec("USE "+quote(target["database"].toString())))return fail("无法选择目标数据库");
    auto context=step["context"].toObject();for(const auto &key:QStringList{"sqlMode","characterSetClient","collationConnection"}) if(context.contains(key)) {
        auto variable=key=="sqlMode"?"sql_mode":key=="characterSetClient"?"character_set_client":"collation_connection";
        q.prepare(QString("SET SESSION %1 = ?").arg(variable));q.addBindValue(context[key].toString());if(!q.exec())return fail("无法设置执行上下文");
    }
    if(q.exec(step["sql"].toString()))return {{"ok",true}};
    auto code=q.lastError().nativeErrorCode().toInt();return {{"ok",false},{"error",QString("结构语句执行失败（%1）").arg(code)},{"code",QString::number(code)},{"uncertain",code==0||code==2006||code==2013||code==2055}};
}
