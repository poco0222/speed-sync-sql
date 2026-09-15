#include "schema.h"
#include "foundation.h"
#include <QDateTime>
#include <QJsonArray>
#include <QMap>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QUuid>

namespace SchemaDetails {
// Keep offsets intact so only proven syntax tokens can alter a fingerprint.
static QString syntaxMask(const QString &ddl, const QString &sqlMode) {
    QString mask = ddl;
    const auto modes = sqlMode.split(',');
    const bool escapes = !modes.contains("NO_BACKSLASH_ESCAPES");
    const auto blank = [&](qsizetype start, qsizetype end) {
        for (auto k = start; k < end; ++k) mask[k] = ' ';
    };
    for (qsizetype i = 0; i < ddl.size();) {
        const auto c = ddl[i];
        if (c == '\'' || c == '"' || c == '`') {
            const auto start = i++; const bool stringQuote = c == '\'' || (c == '"' && !modes.contains("ANSI_QUOTES"));
            while (i < ddl.size()) {
                if (stringQuote && escapes && ddl[i] == '\\') { i = qMin(i + 2, ddl.size()); continue; }
                if (ddl[i++] == c) {
                    if (i < ddl.size() && ddl[i] == c) { ++i; continue; }
                    break;
                }
            }
            blank(start, i);
        } else if (ddl.mid(i, 2) == "/*") {
            const auto end = ddl.indexOf("*/", i + 2);
            if (ddl.mid(i, 3) == "/*!") {
                const auto start = i; i += 3;
                while (i < ddl.size() && ddl[i].isDigit()) ++i;
                blank(start, i);
                if (end >= 0) blank(end, end + 2);
            } else { const auto start = i; i = end < 0 ? ddl.size() : end + 2; blank(start, i); }
        } else if (c == '#' || (ddl.mid(i, 2) == "--" && (i + 2 == ddl.size() || ddl[i + 2].isSpace()))) {
            const auto start = i; const auto end = ddl.indexOf('\n', i); i = end < 0 ? ddl.size() : end; blank(start, i);
        } else ++i;
    }
    return mask;
}
QString definitionFingerprint(const QString &ddl, const QString &sqlMode) {
    const auto mask = syntaxMask(ddl, sqlMode);
    const QRegularExpression counter("\\bAUTO_INCREMENT\\s*=\\s*([0-9]+)\\b", QRegularExpression::CaseInsensitiveOption);
    auto matches = counter.globalMatch(mask); QString result = ddl; qsizetype scanned = 0; int depth = 0; bool tableBodyEnded = false;
    QList<QPair<qsizetype, qsizetype>> spans;
    while (matches.hasNext()) {
        auto match = matches.next();
        for (; scanned < match.capturedStart(); ++scanned) {
            if (mask[scanned] == '(') ++depth;
            else if (mask[scanned] == ')' && depth > 0) { --depth; if (depth == 0) tableBodyEnded = true; }
        }
        if (tableBodyEnded && depth == 0) {
            auto start = match.capturedStart();
            while (start > 0 && ddl[start - 1].isSpace()) --start;
            spans.append({start, match.capturedEnd() - start});
        }
    }
    for (auto it = spans.crbegin(); it != spans.crend(); ++it) result.remove(it->first, it->second);
    return result;
}
bool hasUnsupportedSyntax(const QString &ddl, const QString &sqlMode) {
    return QRegularExpression("\\b(?:PARTITION\\s+BY|SRID|COLUMN_FORMAT|STORAGE|ENGINE_ATTRIBUTE|SECONDARY_ENGINE_ATTRIBUTE|WITH\\s+PARSER)\\b", QRegularExpression::CaseInsensitiveOption).match(syntaxMask(ddl, sqlMode)).hasMatch();
}
}

namespace {
const QStringList categoryNames{"columns", "indexes", "constraints", "triggers", "table"};
QJsonObject failure(const QString &error, const QString &code = "metadata") { return {{"ok", false}, {"error", error}, {"code", code}}; }
QString quoted(QString s) { return "`" + s.replace('`', "``") + "`"; }
bool identifier(const QJsonValue &v) { return v.isString() && !v.toString().isEmpty() && v.toString().size() <= 64 && !v.toString().contains(QChar::Null); }
QString now() { return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs); }
QJsonObject category(const QString &state, const QString &error = {}, QJsonArray items = {}) {
    QJsonObject result{{"state", state}, {"items", items}};
    if (!error.isEmpty()) result["error"] = error;
    return result;
}
QJsonValue value(const QVariant &v) { return v.isNull() ? QJsonValue(QJsonValue::Null) : QJsonValue(v.toString()); }
// Values stay strings: BIGINT, decimal defaults and expressions must not lose precision.
QJsonObject queryCategory(QSqlDatabase &db, const QString &sql, const QStringList &args, const QString &nameField) {
    QSqlQuery q(db); q.prepare(sql); for (const auto &arg : args) q.addBindValue(arg);
    if (!q.exec()) {
        auto code = q.lastError().nativeErrorCode().toInt();
        return category(code == 1054 || code == 1109 ? "unsupported" : "failed", QString("元数据读取失败或版本字段不支持（%1）").arg(code));
    }
    QJsonArray items; auto record = q.record();
    while (q.next()) {
        QJsonObject props;
        for (int i = 0; i < record.count(); ++i) if (record.fieldName(i) != nameField) props[record.fieldName(i)] = value(q.value(i));
        items.append(QJsonObject{{"name", q.value(nameField).toString()}, {"properties", props}});
    }
    return category("complete", {}, items);
}
// Only explicit whole-object grants prove that INFORMATION_SCHEMA did not hide rows.
// Role grants and wildcard scopes remain conservative, including partial revokes.
bool hasPrivilege(const QStringList &grants, const QString &privilege, const QString &database, const QString &table, int scopeDepth = 2) {
    QStringList scopes{"*.*"};
    if (scopeDepth >= 1) scopes.append(quoted(database) + ".*");
    if (scopeDepth >= 2) scopes.append(quoted(database) + "." + quoted(table));
    for (const auto &grant : grants) if (grant.startsWith("REVOKE ")) return false;
    for (const auto &grant : grants) {
        auto match = QRegularExpression("^GRANT (.+) ON (.+) TO ").match(grant);
        if (!match.hasMatch() || !scopes.contains(match.captured(2))) continue;
        const auto privileges = match.captured(1).split(", ");
        if (privileges.contains("ALL PRIVILEGES") || privileges.contains(privilege)) return true;
    }
    return false;
}
QJsonObject grouped(QJsonObject cat, const QString &partsKey) {
    if (cat["state"] != "complete") return cat;
    QMap<QString, QJsonArray> groups;
    for (auto v : cat["items"].toArray()) { auto item = v.toObject(); groups[item["name"].toString()].append(item["properties"]); }
    QJsonArray items;
    for (auto it = groups.begin(); it != groups.end(); ++it) items.append(QJsonObject{{"name", it.key()}, {"properties", QJsonObject{{partsKey, it.value()}}}});
    cat["items"] = items; return cat;
}
QJsonObject readConnected(QSqlDatabase &db, const QJsonObject &args) {
    QSqlQuery q(db);
    if (!q.exec("SELECT VERSION(), @@version_comment, @@lower_case_table_names, @@sql_mode") || !q.next()) return failure("无法读取服务器版本与标识符规则");
    auto version = q.value(0).toString(), vendor = q.value(1).toString(), caseMode = q.value(2).toString();
    const auto sqlMode = q.value(3).toString();
    if (version.section('.', 0, 0).toInt() < 8 || version.contains("MariaDB", Qt::CaseInsensitive) || !vendor.contains("MySQL", Qt::CaseInsensitive)) return failure("此服务端不在 MySQL 8.0+ 目标范围内", "unsupported");
    auto action = args["action"].toString(), database = args["database"].toString(), table = args["table"].toString();
    if (action == "databases" || action == "tables") {
        q.prepare(action == "databases" ? "SELECT SCHEMA_NAME, 'DATABASE' FROM INFORMATION_SCHEMA.SCHEMATA ORDER BY SCHEMA_NAME" : "SELECT TABLE_NAME, TABLE_TYPE FROM INFORMATION_SCHEMA.TABLES WHERE TABLE_SCHEMA=? ORDER BY TABLE_NAME");
        if (action == "tables") q.addBindValue(database);
        if (!q.exec()) return failure("无法读取可见对象清单");
        QJsonArray items; while (q.next()) items.append(QJsonObject{{"name", q.value(0).toString()}, {"type", q.value(1).toString()}});
        return {{"ok", true}, {"items", items}, {"version", version}, {"visibility", "visible-only"}};
    }
    QJsonObject result{{"ok", true}, {"existence", "unknown"}, {"database", database}, {"table", table}, {"version", version}, {"lowerCaseTableNames", caseMode}, {"startedAt", now()}};
    QStringList grants;
    if (q.exec("SHOW GRANTS FOR CURRENT_USER")) while (q.next()) grants.append(q.value(0).toString());
    const bool fullSelect = hasPrivilege(grants, "SELECT", database, table), fullTrigger = hasPrivilege(grants, "TRIGGER", database, table);
    if (args["sync"].toBool()) {
        result["sqlMode"] = sqlMode;
        if (!q.exec("SELECT @@server_uuid") || !q.next()) return failure("无法确认服务器身份");
        result["serverUuid"] = q.value(0).toString();
        q.prepare("SELECT DEFAULT_COLLATION_NAME FROM INFORMATION_SCHEMA.SCHEMATA WHERE SCHEMA_NAME=?"); q.addBindValue(database);
        if (!q.exec() || !q.next()) return failure("无法读取数据库排序上下文");
        result["databaseCollation"] = q.value(0).toString();
        const bool globalSelect = hasPrivilege(grants, "SELECT", {}, {}, 0);
        const bool databaseTrigger = hasPrivilege(grants, "TRIGGER", database, {}, 1);
        auto incoming = queryCategory(db, "SELECT CONSTRAINT_NAME AS name, TABLE_SCHEMA AS databaseName, TABLE_NAME AS tableName, COLUMN_NAME AS columnName, REFERENCED_COLUMN_NAME AS referencedColumn FROM INFORMATION_SCHEMA.KEY_COLUMN_USAGE WHERE REFERENCED_TABLE_SCHEMA=? AND REFERENCED_TABLE_NAME=? ORDER BY TABLE_SCHEMA,TABLE_NAME,CONSTRAINT_NAME,ORDINAL_POSITION", {database, table}, "name");
        auto names = queryCategory(db, "SELECT TRIGGER_NAME AS name, EVENT_OBJECT_TABLE AS tableName FROM INFORMATION_SCHEMA.TRIGGERS WHERE TRIGGER_SCHEMA=? ORDER BY TRIGGER_NAME", {database}, "name");
        result["syncGuard"] = QJsonObject{{"complete", globalSelect && databaseTrigger && incoming["state"] == "complete" && names["state"] == "complete"}, {"incoming", incoming["items"]}, {"triggerNames", names["items"]}};
    }
    const auto show = "SHOW CREATE TABLE " + quoted(database) + "." + quoted(table);
    if (!q.exec(show) || !q.next()) {
        auto code = q.lastError().nativeErrorCode().toInt();
        result["existence"] = (code == 1146 && fullSelect) ? "missing" : "unknown";
        result["error"] = result["existence"] == "missing" ? "表不存在" : "无法确认表存在性或无权读取定义";
        result["finishedAt"] = now(); return result;
    }
    result["existence"] = "present"; const auto ddl = q.value(1).toString(); result["ddl"] = ddl;
    QJsonObject cats;
    if (q.record().fieldName(1) != "Create Table") {
        for (const auto &name : categoryNames) cats[name] = category("unsupported", "视图不支持结构化比对");
        result["categories"] = cats; result["finishedAt"] = now(); return result;
    }
    const QStringList pair{database, table};
    if (!fullSelect) {
        for (const auto &name : categoryNames) cats[name] = category("failed", "无法证实整表元数据可见性；需要明确整表 SELECT 授权");
    } else {
        cats["columns"] = queryCategory(db,
            "SELECT COLUMN_NAME AS name, ORDINAL_POSITION AS ordinal, COLUMN_TYPE AS type, IS_NULLABLE AS nullable, COLUMN_DEFAULT AS defaultValue, CHARACTER_SET_NAME AS charset, COLLATION_NAME AS collation, COLUMN_COMMENT AS comment, EXTRA AS extra, GENERATION_EXPRESSION AS generationExpression FROM INFORMATION_SCHEMA.COLUMNS WHERE TABLE_SCHEMA=? AND TABLE_NAME=? ORDER BY ORDINAL_POSITION", pair, "name");
        auto columns = cats["columns"].toObject(); auto ci = columns["items"].toArray();
        if (columns["state"] == "complete" && ci.isEmpty()) columns = category("failed", "字段清单为空，无法证明完整读取");
        for (qsizetype i = 0; i < ci.size(); ++i) {
            auto item = ci[i].toObject(), props = item["properties"].toObject(); auto extra = props["extra"].toString();
            props["defaultKind"] = props["defaultValue"].isNull() ? "null" : extra.contains("DEFAULT_GENERATED") ? "expression" : "literal";
            props["autoIncrement"] = extra.contains("auto_increment"); props["visibility"] = extra.contains("INVISIBLE") ? "invisible" : "visible";
            item["properties"] = props; ci[i] = item;
        }
        columns["items"] = ci; cats["columns"] = columns;
        cats["indexes"] = grouped(queryCategory(db,
            "SELECT INDEX_NAME AS name, NON_UNIQUE AS nonUnique, INDEX_TYPE AS method, SEQ_IN_INDEX AS ordinal, COLUMN_NAME AS columnName, EXPRESSION AS expression, SUB_PART AS prefixLength, COLLATION AS direction, IS_VISIBLE AS visible, INDEX_COMMENT AS comment FROM INFORMATION_SCHEMA.STATISTICS WHERE TABLE_SCHEMA=? AND TABLE_NAME=? ORDER BY INDEX_NAME, SEQ_IN_INDEX", pair, "name"), "parts");
        cats["constraints"] = grouped(queryCategory(db,
            "SELECT t.CONSTRAINT_NAME AS name, t.CONSTRAINT_TYPE AS type, t.ENFORCED AS enforced, k.ORDINAL_POSITION AS ordinal, k.COLUMN_NAME AS columnName, k.REFERENCED_TABLE_SCHEMA AS referencedDatabase, k.REFERENCED_TABLE_NAME AS referencedTable, k.REFERENCED_COLUMN_NAME AS referencedColumn, r.UPDATE_RULE AS updateRule, r.DELETE_RULE AS deleteRule, c.CHECK_CLAUSE AS expression FROM INFORMATION_SCHEMA.TABLE_CONSTRAINTS t LEFT JOIN INFORMATION_SCHEMA.KEY_COLUMN_USAGE k ON k.CONSTRAINT_SCHEMA=t.CONSTRAINT_SCHEMA AND k.TABLE_NAME=t.TABLE_NAME AND k.CONSTRAINT_NAME=t.CONSTRAINT_NAME LEFT JOIN INFORMATION_SCHEMA.REFERENTIAL_CONSTRAINTS r ON r.CONSTRAINT_SCHEMA=t.CONSTRAINT_SCHEMA AND r.TABLE_NAME=t.TABLE_NAME AND r.CONSTRAINT_NAME=t.CONSTRAINT_NAME LEFT JOIN INFORMATION_SCHEMA.CHECK_CONSTRAINTS c ON c.CONSTRAINT_SCHEMA=t.CONSTRAINT_SCHEMA AND c.CONSTRAINT_NAME=t.CONSTRAINT_NAME WHERE t.TABLE_SCHEMA=? AND t.TABLE_NAME=? ORDER BY t.CONSTRAINT_NAME, k.ORDINAL_POSITION", pair, "name"), "parts");
        cats["table"] = queryCategory(db,
            "SELECT 'table' AS name, t.ENGINE AS engine, c.CHARACTER_SET_NAME AS charset, t.TABLE_COLLATION AS collation, t.TABLE_COMMENT AS comment, t.ROW_FORMAT AS rowFormat, t.CREATE_OPTIONS AS createOptions FROM INFORMATION_SCHEMA.TABLES t LEFT JOIN INFORMATION_SCHEMA.COLLATION_CHARACTER_SET_APPLICABILITY c ON c.COLLATION_NAME=t.TABLE_COLLATION WHERE t.TABLE_SCHEMA=? AND t.TABLE_NAME=?", pair, "name");
        auto tc = cats["table"].toObject(); auto ti = tc["items"].toArray();
        if (tc["state"] == "complete") {
            if (ti.size() != 1) tc = category("failed", "表属性不可见或读取期间已变化");
            else {
                auto tableItem = ti[0].toObject(); auto p = tableItem["properties"].toObject();
                // Only the default InnoDB shape is fully modeled; retain all other options verbatim.
                auto opts = p["createOptions"].toString(); opts.remove(QRegularExpression("(?:^| )row_format=\\w+", QRegularExpression::CaseInsensitiveOption));
                if (!opts.trimmed().isEmpty() || p["engine"] != "InnoDB" || SchemaDetails::hasUnsupportedSyntax(ddl, sqlMode)) {
                    tc["state"] = "unsupported"; tc["error"] = "分区、非 InnoDB 引擎或特殊表选项尚不支持语义比较";
                } else {
                    p.remove("createOptions"); tableItem["properties"] = p; ti[0] = tableItem; tc["items"] = ti;
                }
            }
        }
        cats["table"] = tc;
    }
    cats["triggers"] = fullTrigger ? queryCategory(db,
        "SELECT TRIGGER_NAME AS name, EVENT_MANIPULATION AS event, ACTION_TIMING AS timing, ACTION_ORDER AS ordinal, ACTION_STATEMENT AS body, DEFINER AS definer, SQL_MODE AS sqlMode, CHARACTER_SET_CLIENT AS characterSetClient, COLLATION_CONNECTION AS collationConnection, DATABASE_COLLATION AS databaseCollation FROM INFORMATION_SCHEMA.TRIGGERS WHERE EVENT_OBJECT_SCHEMA=? AND EVENT_OBJECT_TABLE=? ORDER BY TRIGGER_NAME", pair, "name") : category("failed", "无法证实触发器完整可见性；需要明确 TRIGGER 授权");
    auto triggers = cats["triggers"].toObject(); auto triggerItems = triggers["items"].toArray();
    for (qsizetype i = 0; i < triggerItems.size(); ++i) {
        auto item = triggerItems[i].toObject();
        if (item["properties"].toObject()["body"].isNull()) { triggers["state"] = "failed"; triggers["error"] = "触发器正文不可见"; }
        if (!q.exec("SHOW CREATE TRIGGER " + quoted(database) + "." + quoted(item["name"].toString())) || !q.next()) {
            triggers["state"] = "failed"; triggers["error"] = "触发器定义读取失败，请重新读取"; break;
        }
        item["ddl"] = q.value("SQL Original Statement").toString(); triggerItems[i] = item;
    }
    triggers["items"] = triggerItems; cats["triggers"] = triggers;
    if (!q.exec(show) || !q.next() || SchemaDetails::definitionFingerprint(q.value(1).toString(), sqlMode) != SchemaDetails::definitionFingerprint(ddl, sqlMode)) {
        for (const auto &name : categoryNames) { auto cat = cats[name].toObject(); cat["state"] = "failed"; cat["error"] = "读取期间定义变化或无法复核，请重新读取"; cats[name] = cat; }
    }
    result["categories"] = cats; result["finishedAt"] = now(); return result;
}
}

static QJsonObject readSchemaImpl(const QJsonObject &connection, const QJsonObject &args) {
    const auto invalid = validateConnection(connection);
    if (!invalid.isEmpty()) return failure(invalid, "validation");
    auto action = args["action"].toString();
    if (!QStringList{"databases", "tables", "snapshot"}.contains(action) || (action != "databases" && !identifier(args["database"])) || (action == "snapshot" && !identifier(args["table"]))) return failure("无效的结构读取参数", "validation");
    if (!QSqlDatabase::isDriverAvailable("QMYSQL")) return failure("QMYSQL 驱动缺失或无法加载", "driver");
    const auto name = "schema-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto cleanup = qScopeGuard([&] { QSqlDatabase::removeDatabase(name); });
    auto db = QSqlDatabase::addDatabase("QMYSQL", name);
    db.setHostName(connection["host"].toString()); db.setPort(connection["port"].toInt());
    db.setUserName(connection["user"].toString()); db.setPassword(connection["password"].toString());
    const auto mode = connection["tls"].toString();
    const auto ssl = mode == "verify" ? "SSL_MODE_VERIFY_IDENTITY" : mode == "required" ? "SSL_MODE_REQUIRED" : mode == "disabled" ? "SSL_MODE_DISABLED" : "SSL_MODE_PREFERRED";
    auto options = QString("MYSQL_OPT_CONNECT_TIMEOUT=%1;MYSQL_OPT_READ_TIMEOUT=%1;MYSQL_OPT_WRITE_TIMEOUT=%1;MYSQL_OPT_SSL_MODE=%2").arg(connection["timeout"].toInt(10)).arg(ssl);
    if (mode == "verify") options += ";SSL_CA=" + connection["ca"].toString();
    db.setConnectOptions(options);
    if (!db.open()) return classifyDatabaseError(db.lastError().nativeErrorCode().toInt());
    QSqlQuery q(db);
    if (!q.exec("SHOW SESSION STATUS LIKE 'Ssl_cipher'") || !q.next()) return failure("无法核实 TLS 会话状态", "tls");
    if ((mode == "required" || mode == "verify") && q.value(1).toString().isEmpty()) return failure("服务端未建立所要求的 TLS 连接", "tls");
    return readConnected(db, args);
}

QJsonObject readSchema(const QJsonObject &connection, const QJsonObject &args) {
    const auto startedAt = now(); auto result = readSchemaImpl(connection, args);
    if (args["action"] == "snapshot") {
        result["database"] = args["database"].toString(); result["table"] = args["table"].toString();
        result["startedAt"] = startedAt; result["finishedAt"] = now();
    }
    return result;
}

QJsonObject compareSchemas(const QJsonObject &left, const QJsonObject &right) {
    QJsonArray rows; bool complete = true, different = false;
    const bool lm = left["ok"].toBool() && left["existence"] == "missing", rm = right["ok"].toBool() && right["existence"] == "missing";
    const bool lp = left["ok"].toBool() && left["existence"] == "present", rp = right["ok"].toBool() && right["existence"] == "present";
    const auto addReason = [&](const QString &categoryName, const QString &state, const QString &reason) {
        rows.append(QJsonObject{{"category", categoryName}, {"name", ""}, {"status", state}, {"left", QJsonValue::Null}, {"right", QJsonValue::Null}, {"changed", QJsonArray{}}, {"reason", reason}}); complete = false;
    };
    if ((!lp && !lm) || (!rp && !rm) || (lm && rm)) {
        const bool failed = left.contains("error") || right.contains("error");
        addReason("table", failed ? "failed" : "unread", lm && rm ? "两端均无表，配对无效" : QString("左：%1；右：%2").arg(left["error"].toString(lp ? "已读取" : "存在性无法确认"), right["error"].toString(rp ? "已读取" : "存在性无法确认")));
    }
    if (lp && rp && (left["lowerCaseTableNames"].isUndefined() || right["lowerCaseTableNames"].isUndefined() || left["lowerCaseTableNames"] != right["lowerCaseTableNames"])) addReason("table", "unsupported", "两端标识符规则未知或不一致");
    for (const auto &categoryName : categoryNames) {
        auto lc = left["categories"].toObject()[categoryName].toObject(), rc = right["categories"].toObject()[categoryName].toObject();
        auto ls = lm ? QString("complete") : lc["state"].toString("unread"), rs = rm ? QString("complete") : rc["state"].toString("unread");
        if (ls != "complete" || rs != "complete") {
            auto state = ls == "failed" || rs == "failed" ? "failed" : ls == "unsupported" || rs == "unsupported" ? "unsupported" : "unread";
            addReason(categoryName, state, QString("左：%1；右：%2").arg(lc["error"].toString(ls), rc["error"].toString(rs)));
        }
        QMap<QString, QJsonObject> li, ri;
        for (auto v : lc["items"].toArray()) { auto o = v.toObject(); li[o["name"].toString()] = o; }
        for (auto v : rc["items"].toArray()) { auto o = v.toObject(); ri[o["name"].toString()] = o; }
        for (auto it = li.begin(); it != li.end(); ++it) {
            for (auto jt = ri.begin(); jt != ri.end(); ++jt) {
                if (it.key() != jt.key() && it.key().compare(jt.key(), Qt::CaseInsensitive) == 0)
                    addReason(categoryName, "unsupported", "对象名仅大小写不同，当前不推断跨端标识符等价");
            }
        }
        auto names = li.keys(); for (auto it = ri.begin(); it != ri.end(); ++it) if (!names.contains(it.key())) names.append(it.key());
        names.sort();
        for (const auto &name : names) {
            const auto l = li.value(name), r = ri.value(name);
            auto lprops = l["properties"].toObject(), rprops = r["properties"].toObject();
            // Normalize only explicit self references; never rewrite expression or trigger text.
            const auto selfReferences = [](QJsonObject props, const QJsonObject &snapshot) {
                auto parts = props["parts"].toArray();
                for (qsizetype i = 0; i < parts.size(); ++i) {
                    auto part = parts[i].toObject();
                    if (part["referencedDatabase"] == snapshot["database"] && part["referencedTable"] == snapshot["table"]) { part["referencedDatabase"] = "<self>"; part["referencedTable"] = "<self>"; }
                    parts[i] = part;
                }
                if (props.contains("parts")) props["parts"] = parts;
                return props;
            };
            if (categoryName == "constraints") { lprops = selfReferences(lprops, left); rprops = selfReferences(rprops, right); }
            auto keys = lprops.keys(); for (const auto &key : rprops.keys()) if (!keys.contains(key)) keys.append(key); keys.sort();
            QJsonArray changed; for (const auto &key : keys) if (lprops[key] != rprops[key]) changed.append(key);
            QString state;
            if (l.isEmpty()) state = ls == "complete" ? "right-only" : "unread";
            else if (r.isEmpty()) state = rs == "complete" ? "left-only" : "unread";
            else state = changed.isEmpty() ? "same" : "different";
            if (state == "different" || state == "left-only" || state == "right-only") different = true;
            rows.append(QJsonObject{{"category", categoryName}, {"name", name}, {"status", state}, {"left", l.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(l)}, {"right", r.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(r)}, {"changed", changed}});
        }
    }
    if (lm != rm && (lp || rp)) different = true;
    return {{"complete", complete}, {"status", !complete ? "incomplete" : different ? "different" : "same"}, {"rows", rows}, {"left", left}, {"right", right}};
}
