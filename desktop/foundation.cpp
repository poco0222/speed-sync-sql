#include "foundation.h"
#include "credentials.h"
#include "schema.h"
#include <QCoreApplication>
#include <QClipboard>
#include <QGuiApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTimer>
#include <QUuid>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QLockFile>

namespace {
QJsonObject failure(const QString &message, const QString &code = "application") {
    return {{"ok", false}, {"error", message}, {"code", code}};
}
QJsonObject success(QJsonObject value = {}) { value["ok"] = true; return value; }
const QStringList fields{"id", "name", "host", "port", "user", "database", "remember", "tls", "ca", "timeout"};
bool integer(const QJsonValue &v, int low, int high) {
    return v.isDouble() && v.toDouble() == v.toInt(-1) && v.toInt() >= low && v.toInt() <= high;
}
}
QJsonObject safeConnection(const QJsonObject &c) {
    QJsonObject result;
    for (const auto &key : fields) if (c.contains(key)) result[key] = c[key];
    return result;
}
QString validateConnection(const QJsonObject &c) {
    for (const auto &key : {"name", "host", "user"}) {
        if (!c[key].isString() || c[key].toString().trimmed().isEmpty() || c[key].toString().size() > 255 || c[key].toString().contains(QChar::Null))
            return "名称、主机和用户名必填，最多 255 个字符";
    }
    if (!integer(c["port"], 1, 65535)) return "端口必须为 1–65535 的整数";
    if (!c["timeout"].isNull() && !c["timeout"].isUndefined() && !integer(c["timeout"], 1, 60)) return "连接超时必须为 1–60 秒的整数";
    if (!QStringList{"preferred", "required", "verify", "disabled"}.contains(c["tls"].toString())) return "无效的 TLS 模式";
    if (c["tls"] == "verify" && c["ca"].toString().isEmpty()) return "验证服务器身份需要 CA 文件路径";
    if (c["ca"].toString().contains(';') || c["ca"].toString().contains(QChar::Null)) return "CA 路径不能包含分号或空字符";
    if (c["database"].toString().size() > 255 || c["database"].toString().contains(QChar::Null)) return "默认数据库无效";
    if (c.contains("password") && (!c["password"].isString() || c["password"].toString().contains(QChar::Null) || c["password"].toString().toUtf8().size() > 2048)) return "密码格式无效或超过 2048 字节";
    return {};
}
QJsonObject classifyDatabaseError(int code) {
    switch (code) {
    case 1045: case 1698: return failure("认证失败，请检查用户名和密码", "authentication");
    case 1044: case 1049: return failure("默认数据库不存在或无权访问", "database");
    case 2002: case 2003: case 2005: case 2013: return failure("无法连接服务器，请检查地址、网络和连接超时", "network");
    case 2026: return failure("TLS 连接失败，请检查加密模式、证书与主机名", "tls");
    default: return failure(QString("连接失败（驱动错误码 %1）").arg(code), "connection");
    }
}
QJsonObject probeDatabase(const QJsonObject &c) {
    auto invalid = validateConnection(c);
    if (!invalid.isEmpty()) return failure(invalid, "validation");
    if (!QSqlDatabase::isDriverAvailable("QMYSQL")) return failure("QMYSQL 驱动缺失或无法加载，请检查插件及客户端库", "driver");
    QJsonObject result;
    {
        auto db = QSqlDatabase::addDatabase("QMYSQL", "probe");
        if (!db.isValid()) result = failure("QMYSQL 驱动无法加载，请检查插件及客户端库", "driver");
        else {
            db.setHostName(c["host"].toString()); db.setPort(c["port"].toInt());
            db.setUserName(c["user"].toString()); db.setPassword(c["password"].toString());
            db.setDatabaseName(c["database"].toString());
            QString mode = c["tls"].toString();
            QString ssl = mode == "verify" ? "SSL_MODE_VERIFY_IDENTITY" : mode == "required" ? "SSL_MODE_REQUIRED" : mode == "disabled" ? "SSL_MODE_DISABLED" : "SSL_MODE_PREFERRED";
            int timeout = c["timeout"].toInt(10);
            QString options = QString("MYSQL_OPT_CONNECT_TIMEOUT=%1;MYSQL_OPT_READ_TIMEOUT=%1;MYSQL_OPT_WRITE_TIMEOUT=%1;MYSQL_OPT_SSL_MODE=%2").arg(timeout).arg(ssl);
            if (mode == "verify") options += ";SSL_CA=" + c["ca"].toString();
            db.setConnectOptions(options);
            if (!db.open()) result = classifyDatabaseError(db.lastError().nativeErrorCode().toInt());
            else {
                QSqlQuery q(db);
                if (!q.exec("SELECT VERSION(), DATABASE(), @@version_comment") || !q.next()) result = failure("连接已建立，但无法读取服务器信息", "metadata");
                else {
                    auto version = q.value(0).toString(); auto vendor = q.value(2).toString();
                    auto major = version.section('.', 0, 0).toInt();
                    if (major < 8 || version.contains("MariaDB", Qt::CaseInsensitive) || !vendor.contains("MySQL", Qt::CaseInsensitive))
                        result = failure("此服务端不在 MySQL 8.0+ 目标范围内", "unsupported");
                    else result = success({{"version", version}, {"database", q.value(1).toString()}});
                }
                if (result["ok"].toBool()) {
                    if (!q.exec("SHOW SESSION STATUS LIKE 'Ssl_cipher'") || !q.next()) result = failure("无法核实 TLS 会话状态", "tls");
                    else {
                        bool encrypted = !q.value(1).toString().isEmpty();
                        if ((mode == "required" || mode == "verify") && !encrypted) result = failure("服务端未建立所要求的 TLS 连接", "tls");
                        else result["encrypted"] = encrypted;
                    }
                }
                db.close();
            }
        }
    }
    QSqlDatabase::removeDatabase("probe");
    return result;
}
Foundation::Foundation(QString path, QObject *parent) : QObject(parent), directory(std::move(path)) {
    state = {{"connections", QJsonArray{}}, {"left", ""}, {"right", ""}, {"settings", QJsonObject{{"theme", "system"}, {"density", "standard"}, {"timeout", 10}}}};
    initializeData();
    recoverSyncRecords();
    QFile f(directory + "/connections.json");
    if (!f.exists()) return;
    if (!f.open(QIODevice::ReadOnly)) { loadError = "无法读取连接配置，请检查文件权限"; return; }
    QJsonParseError parse;
    auto document = QJsonDocument::fromJson(f.readAll(), &parse);
    auto loaded = document.object();
    if (parse.error != QJsonParseError::NoError || !loaded["connections"].isArray() || !loaded["settings"].isObject()) { loadError = "连接配置损坏，请保留原文件并修复后重启"; return; }
    auto settings = loaded["settings"].toObject();
    if (!QStringList{"system", "light", "dark"}.contains(settings["theme"].toString()) || !QStringList{"standard", "compact"}.contains(settings["density"].toString()) || !integer(settings["timeout"], 1, 60)) { loadError = "设置配置损坏，请保留原文件并修复后重启"; return; }
    QSet<QString> ids;
    for (auto value : loaded["connections"].toArray()) {
        auto c = value.toObject(); auto id = c["id"].toString();
        if (!validateConnection(c).isEmpty() || QUuid(id).isNull() || ids.contains(id) || c.contains("password")) { loadError = "连接配置字段损坏，请保留原文件并修复后重启"; return; }
        ids.insert(id);
    }
    for (auto side : {"left", "right"}) if (!loaded[side].isString() || (!loaded[side].toString().isEmpty() && !ids.contains(loaded[side].toString()))) { loadError = "连接选择配置损坏，请修复后重启"; return; }
    state = loaded;
}
QString Foundation::credentialKey(const QString &id) const {
    return QString::fromLatin1(QCryptographicHash::hash(QDir(directory).absolutePath().toUtf8(), QCryptographicHash::Sha256).toHex().left(16)) + "/" + id;
}
QJsonObject Foundation::find(const QString &id) const {
    for (auto value : state["connections"].toArray()) if (value.toObject()["id"] == id) return value.toObject();
    return {};
}
QJsonObject Foundation::snapshot() const {
    auto result = state;
    result["workspace"] = state["workspaces"].toObject()[workspaceKey()].toObject();
    result["loadError"] = loadError;
    result["qtVersion"] = qVersion();
    result["driverAvailable"] = QSqlDatabase::isDriverAvailable("QMYSQL");
    return result;
}
bool Foundation::persist(const QJsonObject &next, QString &error) {
    if (!loadError.isEmpty()) { error = loadError; return false; }
    if (!QDir().mkpath(directory)) { error = "无法创建配置目录"; return false; }
    QSaveFile file(directory + "/connections.json");
    auto bytes = QJsonDocument(next).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) { error = "保存配置失败，请检查目录权限和剩余空间"; return false; }
    state = next;
    return true;
}
QJsonObject Foundation::execute(const QString &operation, const QJsonObject &args) {
    if (operation == "snapshot") return success({{"state", snapshot()}});
    if (!loadError.isEmpty()) return failure(loadError, "storage");
    if (operation.startsWith("data-")) return dataOperation(operation, args);
    if (operation.contains("sync") || operation == "invalidate-plan") return syncOperation(operation, args);
    if (syncExecuting && QStringList{"save", "delete", "select", "workspace", "settings", "cancel-schema"}.contains(operation)) return failure("结构执行中，请等待结束后修改上下文", "busy");
    auto next = state;
    QString error;
    if (operation == "save") {
        auto invalid = validateConnection(args);
        if (!invalid.isEmpty()) return failure(invalid, "validation");
        QString id = args["id"].toString();
        auto old = find(id);
        if (!id.isEmpty() && old.isEmpty()) return failure("连接已删除，请重新新建");
        if (id.isEmpty()) id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        auto c = safeConnection(args); c["id"] = id;
        c["name"] = c["name"].toString().trimmed(); c["host"] = c["host"].toString().trimmed();
        auto key = credentialKey(id);
        QString previous;
        bool hadCredential = old["remember"].toBool();
        if (hadCredential) {
            bool missing = false;
            previous = Credentials::read(key, error, &missing);
            if (!error.isEmpty() && !missing) return failure(error, "credentials");
            if (missing) {
                if (c["remember"].toBool() && !args.contains("password")) return failure(error, "credentials");
                hadCredential = false; error.clear();
            }
        }
        QString password = args.contains("password") ? args["password"].toString() : old["remember"].toBool() ? previous : passwords.value(id);
        bool remember = c["remember"].toBool(); c["remember"] = remember;
        bool credentialChanged = remember || old["remember"].toBool();
        if (credentialChanged && !(remember ? Credentials::write(key, password, error) : Credentials::remove(key, error))) return failure(error, "credentials");
        QJsonArray connections;
        for (auto item : state["connections"].toArray()) if (item.toObject()["id"] != id) connections.append(item);
        connections.append(c); next["connections"] = connections;
        if (!persist(next, error)) {
            if (credentialChanged) {
                QString rollbackError;
                bool restored = hadCredential ? Credentials::write(key, previous, rollbackError) : Credentials::remove(key, rollbackError);
                if (!restored) error += "；凭据恢复失败，请重新输入密码后保存";
            }
            return failure(error, "storage");
        }
        passwords[id] = password; ++revisions[id]; invalidateSchema();
        return success({{"state", snapshot()}, {"id", id}});
    }
    if (operation == "delete") {
        auto id = args["id"].toString(); auto old = find(id);
        if (old.isEmpty()) return failure("连接不存在");
        QString previous;
        bool hadCredential = old["remember"].toBool();
        if (hadCredential) {
            bool missing = false;
            previous = Credentials::read(credentialKey(id), error, &missing);
            if (!error.isEmpty() && !missing) return failure(error, "credentials");
            if (missing) { hadCredential = false; error.clear(); }
            if (!Credentials::remove(credentialKey(id), error)) return failure(error, "credentials");
        }
        QJsonArray connections;
        for (auto item : state["connections"].toArray()) if (item.toObject()["id"] != id) connections.append(item);
        next["connections"] = connections;
        for (auto side : {"left", "right"}) if (next[side] == id) next[side] = "";
        if (!persist(next, error)) {
            if (hadCredential) { QString restore; if (!Credentials::write(credentialKey(id), previous, restore)) error += "；凭据恢复失败"; }
            return failure(error, "storage");
        }
        passwords.remove(id); ++revisions[id]; invalidateSchema();
    } else if (operation == "select") {
        auto side = args["side"].toString(); auto id = args["id"].toString();
        if ((side != "left" && side != "right") || (!id.isEmpty() && find(id).isEmpty())) return failure("连接选择无效", "validation");
        if (jobs.contains(side)) return failure("该端连接测试尚未结束");
        next[side] = id;
        if (!persist(next, error)) return failure(error, "storage");
        invalidateSchema();
    } else if (operation == "cancel-schema") {
        invalidateSchema();
        return success({{"cancelled", true}});
    } else if (operation == "workspace") {
        QJsonObject workspace;
        for (auto side : {"left", "right"}) {
            auto value = args[side].toObject();
            for (auto field : {"database", "table"}) {
                if (!value[field].isString() || value[field].toString().size() > 64 || value[field].toString().contains(QChar::Null)) return failure("库表选择无效", "validation");
            }
            workspace[side] = QJsonObject{{"database", value["database"]}, {"table", value["table"]}};
        }
        if (!integer(args["width"], 0, 600)) return failure("面板尺寸无效", "validation");
        workspace["width"] = args["width"];
        auto workspaces = state["workspaces"].toObject();
        const auto previous = workspaces[workspaceKey()].toObject();
        workspaces[workspaceKey()] = workspace; next["workspaces"] = workspaces;
        if (!persist(next, error)) return failure(error, "storage");
        if (previous["left"] != workspace["left"] || previous["right"] != workspace["right"]) invalidateSchema();
    } else if (operation == "copy-schema") {
        if (comparison.isEmpty()) return failure("比对结果已失效，请重新比对", "stale");
        const auto side = args["side"].toString();
        if (side != "left" && side != "right") return failure("连接端无效", "validation");
        auto endpoint = comparison[side].toObject();
        auto definition = endpoint["ddl"].toString();
        if (args.contains("category")) {
            definition.clear();
            const auto items = endpoint["categories"].toObject()[args["category"].toString()].toObject()["items"].toArray();
            for (auto value : items) if (value.toObject()["name"] == args["name"]) { definition = value.toObject()["ddl"].toString(); break; }
        }
        if (definition.isEmpty()) return failure("未读取到完整定义", "metadata");
        QGuiApplication::clipboard()->setText(definition);
        return success();
    } else if (operation == "export-schema") {
        if (comparison.isEmpty()) return failure("比对结果已失效，请重新比对", "stale");
        const auto generation = schemaGeneration;
        auto report = comparison;
        for (auto side : {"left", "right"}) {
            auto original = report[side].toObject();
            QJsonObject endpoint;
            for (auto field : {"database", "table", "version", "startedAt", "finishedAt", "existence"}) endpoint[field] = original[field];
            endpoint["name"] = find(state[side].toString())["name"];
            report[side] = endpoint;
        }
        // Raw definitions are available in the viewer; reports contain structured differences only.
        auto rows = report["rows"].toArray();
        for (qsizetype i = 0; i < rows.size(); ++i) {
            auto row = rows[i].toObject(); row.remove("ddl");
            for (auto side : {"left", "right"}) if (row[side].isObject()) {
                auto value = row[side].toObject(); value.remove("ddl"); row[side] = value;
            }
            rows[i] = row;
        }
        report["rows"] = rows; report["formatVersion"] = 1;
        report["generatedAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        auto filename = QFileDialog::getSaveFileName(nullptr, "保存结构差异摘要", QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/schema-comparison.json", "JSON (*.json)");
        if (filename.isEmpty()) return success({{"cancelled", true}});
        if (generation != schemaGeneration) return failure("比对结果已失效，请重新比对", "stale");
        QSaveFile file(filename); const auto bytes = QJsonDocument(report).toJson();
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) return failure("差异摘要保存失败，请检查目标目录", "storage");
        return success({{"cancelled", false}});
    } else if (operation == "settings") {
        if (!QStringList{"system", "light", "dark"}.contains(args["theme"].toString()) || !QStringList{"standard", "compact"}.contains(args["density"].toString()) || !integer(args["timeout"], 1, 60)) return failure("设置值无效", "validation");
        next["settings"] = QJsonObject{{"theme", args["theme"]}, {"density", args["density"]}, {"timeout", args["timeout"]}};
        if (!persist(next, error)) return failure(error, "storage");
        invalidateData();
    } else if (operation == "export") {
        auto filename = QFileDialog::getSaveFileName(nullptr, "保存连接测试摘要", QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/connection-diagnostics.json", "JSON (*.json)");
        if (filename.isEmpty()) return success({{"cancelled", true}});
        QJsonArray diagnostics;
        for (auto item : state["connections"].toArray()) {
            auto c = item.toObject();
            diagnostics.append(QJsonObject{{"name", c["name"]}, {"lastTest", c["lastTest"]}});
        }
        auto bytes = QJsonDocument(QJsonObject{{"application", "Speed Sync SQL"}, {"qtVersion", qVersion()}, {"connections", diagnostics}}).toJson();
        QSaveFile file(filename);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) return failure("诊断文件保存失败，请检查目标目录", "storage");
        return success({{"cancelled", false}});
    } else return failure("未知操作", "validation");
    return success({{"state", snapshot()}});
}
void Foundation::reply(const QString &id, const QJsonObject &result) {
    auto envelope = result; envelope["requestId"] = id;
    emit response(QString::fromUtf8(QJsonDocument(envelope).toJson(QJsonDocument::Compact)));
}
void Foundation::request(const QString &json) {
    if (json.toUtf8().size() > 32768) return;
    QJsonParseError error;
    auto document = QJsonDocument::fromJson(json.toUtf8(), &error);
    auto o = document.object(); auto id = o["requestId"].toString();
    if (id.isEmpty() || id.size() > 100) return;
    if (error.error != QJsonParseError::NoError || !o["args"].isObject()) { reply(id, failure("请求格式无效", "validation")); return; }
    if (o["operation"] == "data-prepare" || o["operation"] == "data-start") dataTask(id, o["operation"].toString(), o["args"].toObject());
    else if (o["operation"] == "plan-sync") planSync(id, o["args"].toObject());
    else if (o["operation"] == "test") test(id, o["args"].toObject());
    else if (o["operation"] == "schema" || o["operation"] == "compare") schemaTask(id, o["operation"].toString(), o["args"].toObject());
    else reply(id, execute(o["operation"].toString(), o["args"].toObject()));
}
void Foundation::test(const QString &id, QJsonObject args) {
    if (!loadError.isEmpty()) { reply(id, failure(loadError, "storage")); return; }
    auto lane = args["lane"].toString();
    if (!QStringList{"left", "right", "editor"}.contains(lane) || jobs.contains(lane)) { reply(id, failure("该端已有连接测试，请等待结束")); return; }
    auto c = args["connection"].toObject();
    auto connectionId = c["id"].toString();
    auto old = find(connectionId);
    if (!connectionId.isEmpty() && old.isEmpty()) { reply(id, failure("连接已删除")); return; }
    if (lane != "editor") {
        if (connectionId.isEmpty() || state[lane] != connectionId) { reply(id, failure("连接选择已变化")); return; }
        c = old;
    }
    if (c["timeout"].isNull() || c["timeout"].isUndefined()) c["timeout"] = state["settings"].toObject()["timeout"];
    auto invalid = validateConnection(c);
    if (!invalid.isEmpty()) { reply(id, failure(invalid, "validation")); return; }
    if (!c.contains("password")) {
        QString error;
        c["password"] = old["remember"].toBool() ? Credentials::read(credentialKey(connectionId), error) : passwords.value(connectionId);
        if (!error.isEmpty()) { reply(id, failure(error, "credentials")); return; }
    }
    auto process = new QProcess(this); jobs[lane] = process;
    auto timer = new QTimer(process); timer->setSingleShot(true);
    const auto revision = revisions.value(connectionId);
    const auto started = QDateTime::currentMSecsSinceEpoch();
    auto input = QJsonDocument(c).toJson(QJsonDocument::Compact);
    connect(process, &QProcess::started, this, [process, input]() { process->write(input); process->closeWriteChannel(); });
    connect(timer, &QTimer::timeout, process, [process]() { process->setProperty("timedOut", true); process->kill(); });
    connect(process, &QProcess::errorOccurred, this, [this, process, id, lane](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) { jobs.remove(lane); reply(id, failure("无法启动连接测试进程", "process")); process->deleteLater(); emit activityChanged(); }
    });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this, process, timer, lane, id, connectionId, revision, started](int exit, QProcess::ExitStatus status) {
        timer->stop(); jobs.remove(lane);
        auto result = QJsonDocument::fromJson(process->readAllStandardOutput()).object();
        if (process->property("timedOut").toBool()) result = failure("连接测试超过时间上限，已结束；请检查网络与超时设置", "timeout");
        else if (status != QProcess::NormalExit || exit != 0 || !result.contains("ok")) result = failure("连接测试进程异常结束", "process");
        result["elapsedMs"] = QDateTime::currentMSecsSinceEpoch() - started;
        result["testedAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        bool stale = !connectionId.isEmpty() && (revisions.value(connectionId) != revision || find(connectionId).isEmpty());
        result["stale"] = stale;
        if (!stale && !connectionId.isEmpty() && lane != "editor") {
            QJsonArray connections;
            for (auto item : state["connections"].toArray()) {
                auto c = item.toObject(); if (c["id"] == connectionId) c["lastTest"] = result;
                connections.append(c);
            }
            auto next = state; next["connections"] = connections; QString error;
            if (!persist(next, error)) result["storageWarning"] = error;
        }
        result["state"] = snapshot(); reply(id, result);
        process->deleteLater(); emit activityChanged();
    });
    process->setProgram(QCoreApplication::applicationFilePath()); process->setArguments({"--probe"});
    process->start(); timer->start((c["timeout"].toInt(10) + 5) * 1000); emit activityChanged();
}
QString Foundation::workspaceKey() const {
    return state["left"].toString() + ":" + state["right"].toString();
}
void Foundation::invalidateSchema() {
    invalidateData();
    ++schemaGeneration; ++planGeneration; comparison = {}; syncPlan = {};
    for (auto it = jobs.cbegin(); it != jobs.cend(); ++it) if (it.key().startsWith("schema:")) {
        it.value()->setProperty("cancelled", true); it.value()->kill();
    }
}
void Foundation::schemaTask(const QString &id, const QString &operation, const QJsonObject &args) {
    if (!loadError.isEmpty()) { reply(id, failure(loadError, "storage")); return; }
    if (syncExecuting || jobs.contains("sync")) { reply(id, failure("同步任务进行中", "busy")); return; }
    const bool comparing = operation == "compare";
    const auto side = args["side"].toString();
    if (!comparing && (side != "left" && side != "right")) { reply(id, failure("连接端无效", "validation")); return; }
    if (!comparing && !QStringList{"databases", "tables"}.contains(args["action"].toString())) { reply(id, failure("读取操作无效", "validation")); return; }
    for (auto it = jobs.cbegin(); it != jobs.cend(); ++it) if (it.key().startsWith("schema:") && !it.value()->property("cancelled").toBool()) {
        if (comparing || it.value()->property("comparing").toBool() || it.value()->property("side").toString() == side) {
            reply(id, failure("该端已有读取任务，请等待或取消", "busy")); return;
        }
    }
    QJsonObject payload{{"operation", operation}, {"args", args}};
    for (const auto &endpoint : comparing ? QStringList{"left", "right"} : QStringList{side}) {
        auto c = find(state[endpoint].toString());
        if (c.isEmpty()) { reply(id, failure("请先选择连接", "validation")); return; }
        if (comparing || args["action"] == "tables") {
            auto selected = comparing ? args[endpoint].toObject() : args;
            for (const auto &field : comparing ? QStringList{"database", "table"} : QStringList{"database"}) {
                auto value = selected[field];
                if (!value.isString() || value.toString().isEmpty() || value.toString().size() > 64 || value.toString().contains(QChar::Null)) { reply(id, failure("请选择有效库表", "validation")); return; }
            }
        }
        QString error;
        c["password"] = c["remember"].toBool() ? Credentials::read(credentialKey(c["id"].toString()), error) : passwords.value(c["id"].toString());
        if (!error.isEmpty()) { reply(id, failure(error, "credentials")); return; }
        if (c["timeout"].isNull() || c["timeout"].isUndefined()) c["timeout"] = state["settings"].toObject()["timeout"];
        // Catalog browsing must work even when the saved default database was removed.
        c["database"] = ""; payload[endpoint] = c;
    }
    if (comparing) { ++schemaGeneration; ++planGeneration; comparison = {}; syncPlan = {}; }
    const auto generation = schemaGeneration;
    const auto lane = "schema:" + id;
    auto process = new QProcess(this); jobs[lane] = process;
    process->setProperty("comparing", comparing); process->setProperty("side", side);
    auto timer = new QTimer(process); timer->setSingleShot(true);
    const auto input = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    connect(process, &QProcess::started, this, [process, input]() { process->write(input); process->closeWriteChannel(); });
    connect(timer, &QTimer::timeout, process, [process]() { process->setProperty("timedOut", true); process->kill(); });
    connect(process, &QProcess::errorOccurred, this, [this, process, id, lane](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) { jobs.remove(lane); reply(id, failure("无法启动元数据读取进程", "process")); process->deleteLater(); emit activityChanged(); }
    });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this, process, timer, generation, comparing, id, lane](int exit, QProcess::ExitStatus status) {
        timer->stop(); jobs.remove(lane);
        auto result = QJsonDocument::fromJson(process->readAllStandardOutput()).object();
        if (process->property("cancelled").toBool()) result = {{"ok", false}, {"cancelled", true}, {"code", "cancelled"}, {"error", "读取已取消"}};
        else if (process->property("timedOut").toBool()) result = failure("元数据读取超时，请重试或缩小范围", "timeout");
        else if (status != QProcess::NormalExit || exit != 0 || !result.contains("ok")) result = failure("元数据读取进程异常结束", "process");
        const bool stale = generation != schemaGeneration;
        result["stale"] = stale;
        if (!stale && comparing && result["ok"].toBool()) comparison = result["comparison"].toObject();
        reply(id, result); process->deleteLater(); emit activityChanged();
    });
    process->setProgram(QCoreApplication::applicationFilePath()); process->setArguments({"--schema"});
    process->start(); timer->start(comparing ? 120000 : 65000); emit activityChanged();
}
void Foundation::stopJobs() {
    syncStop = true;
    if (!syncExecuting) invalidateSchema();
    for (auto it = jobs.cbegin(); it != jobs.cend(); ++it) if (it.key() != "sync" || !syncExecuting) it.value()->kill();
}
