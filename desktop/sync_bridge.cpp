#include "foundation.h"
#include "credentials.h"
#include "schema.h"
#include <QClipboard>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTimer>
#include <QUuid>
#include <memory>

namespace {
QJsonObject fail(const QString &error, const QString &code = "sync") { return {{"ok", false}, {"error", error}, {"code", code}}; }
QString stamp() { return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs); }
QString connectionDigest(const QJsonObject &c) { return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(safeConnection(c)).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex()); }
bool writeJson(const QString &path, const QJsonObject &value) {
    QSaveFile file(path); const auto bytes = QJsonDocument(value).toJson();
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
QJsonObject endpointIdentity(const QJsonObject &c, const QJsonObject &snapshot) {
    return {{"connectionId", c["id"]}, {"connectionDigest", connectionDigest(c)}, {"name", c["name"]}, {"database", snapshot["database"]}, {"table", snapshot["table"]}};
}
}

bool Foundation::syncConnections(QJsonObject &payload, QString &error) const {
    for (auto side : {"left", "right"}) {
        auto c = find(state[side].toString());
        if (c.isEmpty()) { error = "请先选择两端连接"; return false; }
        c["password"] = c["remember"].toBool() ? Credentials::read(credentialKey(c["id"].toString()), error) : passwords.value(c["id"].toString());
        if (!error.isEmpty()) return false;
        if (c["timeout"].isNull() || c["timeout"].isUndefined()) c["timeout"] = state["settings"].toObject()["timeout"];
        c["database"] = ""; payload[side] = c;
    }
    return true;
}

void Foundation::runSyncProcess(const QJsonObject &payload, std::function<void(QJsonObject)> done, int timeout) {
    auto process = new QProcess(this); jobs["sync"] = process;
    auto timer = new QTimer(process); timer->setSingleShot(true);
    auto bytes = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    auto output = std::make_shared<QByteArray>();
    auto finished = std::make_shared<bool>(false);
    const auto complete = [this, process, timer, done, finished](QJsonObject result) {
        if (*finished) return;
        *finished = true; timer->stop(); jobs.remove("sync"); process->deleteLater();
        done(result); emit activityChanged();
    };
    connect(process, &QProcess::started, this, [process, bytes]() { process->write(bytes); process->closeWriteChannel(); });
    connect(process, &QProcess::readyReadStandardOutput, this, [process, output]() {
        output->append(process->readAllStandardOutput());
        if (output->size() > 32 * 1024 * 1024) { process->setProperty("oversized", true); process->kill(); }
    });
    connect(process, &QProcess::readyReadStandardError, this, [process]() { process->readAllStandardError(); });
    connect(timer, &QTimer::timeout, process, [process]() { process->setProperty("timedOut", true); process->kill(); });
    connect(process, &QProcess::errorOccurred, this, [complete](QProcess::ProcessError error) { if (error == QProcess::FailedToStart) complete(fail("无法启动同步进程", "process")); });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [process, output, complete](int code, QProcess::ExitStatus status) {
        output->append(process->readAllStandardOutput());
        auto result = QJsonDocument::fromJson(*output).object();
        if (process->property("timedOut").toBool()) result = fail("同步操作超时，服务器当前语句结果可能需要核实", "timeout");
        else if (status != QProcess::NormalExit || code != 0 || !result.contains("ok")) result = fail("同步进程异常结束，当前语句结果可能需要核实", "process");
        complete(result);
    });
    process->setProgram(QCoreApplication::applicationFilePath()); process->setArguments({"--schema"});
    process->start(); timer->start(timeout); emit activityChanged();
}

void Foundation::planSync(const QString &id, const QJsonObject &args) {
    if (!loadError.isEmpty()) { reply(id, fail(loadError, "storage")); return; }
    if (!jobs.isEmpty() || syncExecuting) { reply(id, fail("请等待当前任务结束", "busy")); return; }
    if (comparison.isEmpty()) { reply(id, fail("请先重新比对", "stale")); return; }
    syncPlan = {};
    QJsonObject payload{{"operation", "plan-sync"}, {"expected", comparison}, {"args", args}};
    QString error; if (!syncConnections(payload, error)) { reply(id, fail(error)); return; }
    const auto generation = planGeneration;
    runSyncProcess(payload, [this, id, generation](QJsonObject result) {
        if (generation != planGeneration) { reply(id, fail("比对或选区已改变，请重新预览", "stale")); return; }
        if (result["ok"].toBool()) {
            syncPlan = result["plan"].toObject(); syncPlan["id"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
            syncPlan["generation"] = QString::number(planGeneration);
            for (auto side : {"left", "right"}) syncPlan[QString(side) + "Identity"] = endpointIdentity(find(state[side].toString()), syncPlan[side].toObject());
            auto displayPlan = syncPlan;
            for (auto side : {"left", "right"}) { auto endpoint = displayPlan[side].toObject(); endpoint["name"] = find(state[side].toString())["name"]; displayPlan[side] = endpoint; }
            result["plan"] = displayPlan;
        }
        reply(id, result);
    });
}

void Foundation::recoverSyncRecords() {
    QDir folder(directory + "/sync-records");
    for (const auto &name : folder.entryList({"*.json"}, QDir::Files, QDir::Name)) {
        QFile file(folder.filePath(name));
        if (!file.open(QIODevice::ReadOnly)) continue;
        auto record = QJsonDocument::fromJson(file.readAll()).object();
        if (QUuid(record["id"].toString()).isNull() || !record["steps"].isArray()) continue;
        if (record["status"] == "running") {
            record["status"] = "unknown"; record["error"] = "上次执行未正常结束，请重新比对核实；不会自动重试";
            auto steps = record["steps"].toArray();
            for (qsizetype i = 0; i < steps.size(); ++i) { auto step = steps[i].toObject(); if (step["status"] == "running") step["status"] = "unknown"; steps[i] = step; }
            record["steps"] = steps;
            if (!writeJson(folder.filePath(name), record)) record["storageWarning"] = "恢复状态无法保存，原文件已保留";
        }
        records.append(record);
    }
}

bool Foundation::saveSyncRecord() {
    bool found = false;
    for (qsizetype i = 0; i < records.size(); ++i) if (records[i].toObject()["id"] == syncRecord["id"]) { records[i] = syncRecord; found = true; break; }
    if (!found) records.append(syncRecord);
    return QDir().mkpath(directory + "/sync-records") && writeJson(directory + "/sync-records/" + syncRecord["id"].toString() + ".json", syncRecord);
}

QJsonObject Foundation::syncOperation(const QString &operation, const QJsonObject &args) {
    if (operation == "sync-status") {
        QJsonObject result{{"ok", true}, {"running", syncExecuting}};
        if (!syncRecord.isEmpty()) result["record"] = syncRecord;
        if (!comparison.isEmpty()) result["comparison"] = comparison;
        return result;
    }
    if (operation == "sync-records") return {{"ok", true}, {"records", records}};
    if (operation == "sync-record" || operation == "export-sync-record" || operation == "restore-sync-record") {
        QJsonObject record;
        for (auto value : records) if (value.toObject()["id"] == args["id"]) { record = value.toObject(); break; }
        if (record.isEmpty()) return fail("执行记录不存在");
        if (operation == "sync-record") return {{"ok", true}, {"record", record}};
        if (operation == "export-sync-record") {
            auto filename = QFileDialog::getSaveFileName(nullptr, "导出执行记录", "schema-sync-record.json", "JSON (*.json)");
            if (filename.isEmpty()) return {{"ok", true}, {"cancelled", true}};
            if (!writeJson(filename, record)) return fail("记录导出失败", "storage");
            return {{"ok", true}};
        }
        if (syncExecuting || !jobs.isEmpty()) return fail("请等待当前任务结束", "busy");
        auto next = state; QJsonObject workspace;
        for (auto side : {"left", "right"}) {
            auto endpoint = record[side].toObject(); auto c = find(endpoint["connectionId"].toString());
            if (c.isEmpty() || connectionDigest(c) != endpoint["connectionDigest"].toString()) return fail("原连接已删除或修改，请重新选择连接和库表");
            next[side] = c["id"]; workspace[side] = QJsonObject{{"database", endpoint["database"]}, {"table", endpoint["table"]}};
        }
        workspace["width"] = 280;
        auto workspaces = next["workspaces"].toObject(); workspaces[next["left"].toString() + ":" + next["right"].toString()] = workspace; next["workspaces"] = workspaces;
        QString error; if (!persist(next, error)) return fail(error, "storage");
        invalidateSchema(); return {{"ok", true}, {"state", snapshot()}};
    }
    if (operation == "stop-sync") { syncStop = true; return {{"ok", true}}; }
    if (operation == "invalidate-plan") {
        if (syncExecuting) return fail("执行中不能更改计划", "busy");
        syncPlan = {}; ++planGeneration; return {{"ok", true}};
    }
    if (operation == "copy-sync" || operation == "export-sync") {
        if (syncPlan.isEmpty() || args["planId"] != syncPlan["id"]) return fail("计划已过期，请重新预览", "stale");
        const auto sql = syncPlan["sql"].toString();
        if (operation == "copy-sync") { QGuiApplication::clipboard()->setText(sql); return {{"ok", true}}; }
        const auto generation = planGeneration;
        auto filename = QFileDialog::getSaveFileName(nullptr, "导出同步 SQL", "schema-sync.sql", "SQL (*.sql)");
        if (filename.isEmpty()) return {{"ok", true}, {"cancelled", true}};
        if (generation != planGeneration) return fail("计划已过期", "stale");
        QSaveFile file(filename); const auto bytes = sql.toUtf8();
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) return fail("SQL 导出失败", "storage");
        return {{"ok", true}};
    }
    if (operation != "execute-sync") return fail("未知同步操作", "validation");
    if (syncExecuting || !jobs.isEmpty()) return fail("请等待当前任务结束", "busy");
    if (syncPlan.isEmpty() || args["planId"] != syncPlan["id"] || syncPlan["generation"].toString() != QString::number(planGeneration)) return fail("计划已过期，请重新预览", "stale");
    if (syncPlan["steps"].toArray().isEmpty()) return fail("计划没有可执行操作");
    syncPayload = {}; QString error;
    if (!syncConnections(syncPayload, error)) return fail(error);
    for (auto side : {"left", "right"}) if (syncPlan[QString(side) + "Identity"] != endpointIdentity(find(state[side].toString()), syncPlan[side].toObject())) { syncPayload = {}; return fail("连接已改变，请重新比对", "stale"); }
    syncRecord = {{"id", QUuid::createUuid().toString(QUuid::WithoutBraces)}, {"formatVersion", 1}, {"direction", syncPlan["direction"]}, {"mode", "schema-sync"}, {"startedAt", stamp()}, {"status", "running"}, {"stage", "checking"}, {"sql", syncPlan["sql"]}, {"left", syncPlan["leftIdentity"]}, {"right", syncPlan["rightIdentity"]}, {"snapshots", QJsonObject{{"left", syncPlan["left"]}, {"right", syncPlan["right"]}}}};
    syncRecord["operations"] = syncPlan["operations"]; syncRecord["counts"] = syncPlan["counts"];
    auto steps = syncPlan["steps"].toArray();
    for (qsizetype i = 0; i < steps.size(); ++i) { auto step = steps[i].toObject(); step["status"] = "pending"; steps[i] = step; }
    syncRecord["steps"] = steps;
    if (!saveSyncRecord()) { syncRecord["status"] = "blocked"; syncRecord["error"] = "原结构及初始记录保存失败，未执行数据库写入"; saveSyncRecord(); syncPayload = {}; return fail(syncRecord["error"].toString(), "storage"); }
    invalidateData();
    syncExecuting = true; syncStop = false; syncStep = 0;
    syncPayload["operation"] = "sync-check"; syncPayload["expected"] = syncRecord["snapshots"];
    syncPlan = {}; comparison = {}; ++schemaGeneration;
    runSyncProcess(syncPayload, [this](QJsonObject result) {
        if (!result["ok"].toBool()) { finishSync("blocked", result["error"].toString("写前核对失败，未执行 DDL")); return; }
        syncRecord["stage"] = "executing";
        advanceSync();
    });
    return {{"ok", true}, {"id", syncRecord["id"]}};
}

void Foundation::advanceSync() {
    if (syncStop) { finishSync("stopped", "已在语句边界停止，已成功步骤不会回滚"); return; }
    auto steps = syncRecord["steps"].toArray();
    if (syncStep >= steps.size()) { finishSync("passed"); return; }
    auto step = steps[syncStep].toObject(); step["status"] = "running"; step["startedAt"] = stamp(); steps[syncStep] = step; syncRecord["steps"] = steps;
    if (!saveSyncRecord()) { step["status"] = "pending"; steps[syncStep] = step; syncRecord["steps"] = steps; finishSync("failed", "步骤开始记录无法保存，已停止，当前语句未执行"); return; }
    const auto side = syncRecord["direction"] == "left-to-right" ? "right" : "left";
    auto payload = QJsonObject{{"operation", "sync-step"}, {"connection", syncPayload[side]}, {"target", syncRecord["snapshots"].toObject()[side]}, {"step", step}};
    const auto timeout = (syncPayload[side].toObject()["timeout"].toInt(10) + 5) * 1000;
    runSyncProcess(payload, [this](QJsonObject result) {
        auto steps = syncRecord["steps"].toArray(); auto step = steps[syncStep].toObject();
        const bool uncertain = result["uncertain"].toBool() || result["code"] == "timeout" || result["code"] == "process";
        step["status"] = result["ok"].toBool() ? "passed" : uncertain ? "unknown" : "failed";
        step["finishedAt"] = stamp();
        if (!result["ok"].toBool()) step["error"] = result["error"];
        steps[syncStep] = step; syncRecord["steps"] = steps;
        if (!saveSyncRecord()) { finishSync("unknown", "步骤结果无法持久化，停止后续操作；请核实数据库及记录"); return; }
        if (!result["ok"].toBool()) { finishSync(uncertain ? "unknown" : "failed", result["error"].toString()); return; }
        ++syncStep; advanceSync();
    }, timeout);
}

void Foundation::finishSync(const QString &status, const QString &error) {
    syncRecord["status"] = status; syncRecord["finishedAt"] = stamp(); syncRecord["stage"] = "verifying";
    if (!error.isEmpty()) syncRecord["error"] = error;
    syncRecord["verification"] = QJsonObject{{"status", "pending"}};
    if (!saveSyncRecord()) syncRecord["storageWarning"] = "最新执行结果无法保存，磁盘原记录已保留；请核实";
    verifySync();
}

void Foundation::verifySync() {
    auto payload = syncPayload; payload["operation"] = "sync-read";
    payload["expected"] = syncRecord["snapshots"];
    runSyncProcess(payload, [this](QJsonObject result) {
        QJsonObject verification;
        if (result["ok"].toBool() && result["comparison"].isObject()) {
            comparison = result["comparison"].toObject();
            verification["status"] = comparison["status"];
            if (!comparison["complete"].toBool()) verification["error"] = "复核读取不完整，请重新比对核实";
        } else { verification["status"] = "unknown"; verification["error"] = result["error"].toString("无法完成复核"); }
        syncRecord["verification"] = verification; syncRecord["stage"] = "done";
        if (!saveSyncRecord()) syncRecord["storageWarning"] = "复核结果无法保存，磁盘原记录已保留";
        syncPayload = {}; syncExecuting = false; emit activityChanged();
    });
}
