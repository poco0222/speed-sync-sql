#include "sync_process.h"
#include "schema.h"
#include "sync.h"

bool sameSyncSnapshot(QJsonObject expected, QJsonObject actual, bool guarded) {
    if (!expected["ok"].toBool() || !actual["ok"].toBool()) return false;
    for (auto snapshot : {&expected, &actual}) {
        snapshot->remove("startedAt"); snapshot->remove("finishedAt");
        if (snapshot->contains("ddl")) (*snapshot)["ddl"] = SchemaDetails::definitionFingerprint((*snapshot)["ddl"].toString(), (*snapshot)["sqlMode"].toString());
        if (!guarded) for (auto field : {"serverUuid", "sqlMode", "databaseCollation", "syncGuard"}) snapshot->remove(field);
    }
    return expected == actual;
}

QJsonObject runSyncCommand(const QJsonObject &input) {
    const auto operation = input["operation"].toString();
    if (operation == "sync-step") return executeSyncStep(input["connection"].toObject(), input["target"].toObject(), input["step"].toObject());
    auto expected = input["expected"].toObject();
    QJsonObject snapshots;
    for (auto side : {"left", "right"}) {
        const auto previous = expected[side].toObject();
        snapshots[side] = readSchema(input[side].toObject(), {{"action", "snapshot"}, {"database", previous["database"]}, {"table", previous["table"]}, {"sync", true}});
    }
    auto comparison = compareSchemas(snapshots["left"].toObject(), snapshots["right"].toObject());
    if (operation == "sync-read") return {{"ok", true}, {"comparison", comparison}};
    for (auto side : {"left", "right"}) if (!sameSyncSnapshot(expected[side].toObject(), snapshots[side].toObject(), operation == "sync-check")) return {{"ok", false}, {"code", "stale"}, {"error", "两端结构、身份或依赖已变化或无法完整核对，请重新比对；未执行 DDL"}};
    if (operation == "sync-check") return {{"ok", true}};
    if (operation == "plan-sync") return buildSyncPlan(comparison, input["args"].toObject());
    return {{"ok", false}, {"code", "validation"}, {"error", "未知同步操作"}};
}
