#include "foundation.h"
#include "sync_process.h"
#include <QApplication>
#include <QFile>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QUuid>
#include <cstdio>

class SyncBridgeTest : public QObject {
    Q_OBJECT
    int serial = 0;
    QJsonObject send(Foundation &service, const QString &operation, QJsonObject args = {}) {
        const auto id = QString::number(++serial); QSignalSpy spy(&service, &Foundation::response);
        service.request(QString::fromUtf8(QJsonDocument(QJsonObject{{"requestId", id}, {"operation", operation}, {"args", args}}).toJson()));
        if (spy.isEmpty()) spy.wait(3000);
        for (const auto &entry : spy) { auto r = QJsonDocument::fromJson(entry[0].toString().toUtf8()).object(); if (r["requestId"] == id) return r; }
        return {};
    }
    void setup(Foundation &service, const QString &mode = "normal") {
        for (auto side : {"left", "right"}) {
            QJsonObject c{{"name", mode}, {"host", "fixture"}, {"port", 3306}, {"user", "fixture"}, {"password", "test-secret"}, {"database", ""}, {"remember", false}, {"tls", "preferred"}, {"ca", ""}, {"timeout", 1}};
            auto saved = service.execute("save", c); QVERIFY(saved["ok"].toBool());
            QVERIFY(service.execute("select", {{"side", side}, {"id", saved["id"]}})["ok"].toBool());
        }
    }
    QJsonObject plan(Foundation &service) {
        QJsonObject args{{"left", QJsonObject{{"database", "a"}, {"table", "t"}}}, {"right", QJsonObject{{"database", "b"}, {"table", "t"}}}};
        auto compared = send(service, "compare", args);
        if (!compared["ok"].toBool()) return compared;
        return send(service, "plan-sync", {{"direction", "left-to-right"}, {"alignAll", true}});
    }
    QJsonObject record(Foundation &service) { return service.execute("sync-status", {})["record"].toObject(); }
private slots:
    void staleAndDuplicateProtection() {
        QTemporaryDir dir; Foundation s(dir.path()); setup(s); auto p = plan(s); QVERIFY(p["ok"].toBool());
        QJsonValue id = p["plan"].toObject()["id"];
        QVERIFY(s.execute("invalidate-plan", {})["ok"].toBool());
        QCOMPARE(s.execute("execute-sync", {{"planId", id}})["code"].toString(), QString("stale"));
        p = plan(s); QVERIFY(p["ok"].toBool()); id = p["plan"].toObject()["id"];
        QVERIFY(s.execute("execute-sync", {{"planId", id}})["ok"].toBool());
        QCOMPARE(s.execute("execute-sync", {{"planId", id}})["code"].toString(), QString("busy"));
        QCOMPARE(s.execute("select", {{"side", "left"}, {"id", ""}})["code"].toString(), QString("busy"));
        QCOMPARE(s.execute("cancel-schema", {})["code"].toString(), QString("busy"));
        QTRY_VERIFY_WITH_TIMEOUT(!s.busy(), 5000);
        QCOMPARE(record(s)["status"].toString(), QString("passed"));
        QCOMPARE(record(s)["verification"].toObject()["status"].toString(), QString("same"));
        QCOMPARE(s.execute("execute-sync", {{"planId", id}})["code"].toString(), QString("stale"));
        Foundation recovered(dir.path()); QCOMPARE(recovered.execute("sync-records", {})["records"].toArray().size(), 1);
        const auto bytes = QJsonDocument(recovered.execute("sync-records", {})).toJson(); QVERIFY(!bytes.contains("test-secret")); QVERIFY(!bytes.contains("host"));
    }
    void planInvalidationDoesNotCancelComparison() {
        QTemporaryDir dir; Foundation s(dir.path()); setup(s); QSignalSpy spy(&s, &Foundation::response);
        QJsonObject args{{"left", QJsonObject{{"database", "a"}, {"table", "t"}}}, {"right", QJsonObject{{"database", "b"}, {"table", "t"}}}};
        s.request(QString::fromUtf8(QJsonDocument(QJsonObject{{"requestId", "compare"}, {"operation", "compare"}, {"args", args}}).toJson()));
        QVERIFY(s.execute("invalidate-plan", {})["ok"].toBool());
        QTRY_VERIFY_WITH_TIMEOUT(!s.busy(), 3000);
        QVERIFY(!spy.isEmpty());
        auto result = QJsonDocument::fromJson(spy[0][0].toString().toUtf8()).object();
        QVERIFY(result["ok"].toBool()); QVERIFY(!result["stale"].toBool());
        QVERIFY(send(s, "plan-sync", {{"direction", "left-to-right"}, {"alignAll", true}})["ok"].toBool());
    }
    void stoppedAtBoundary() {
        QTemporaryDir dir; Foundation s(dir.path()); setup(s); auto p = plan(s); QVERIFY(p["ok"].toBool());
        QVERIFY(s.execute("execute-sync", {{"planId", p["plan"].toObject()["id"]}})["ok"].toBool());
        QTRY_COMPARE_WITH_TIMEOUT(record(s)["steps"].toArray()[0].toObject()["status"].toString(), QString("running"), 3000);
        s.stopJobs(); QTRY_VERIFY_WITH_TIMEOUT(!s.busy(), 5000);
        auto r = record(s); QCOMPARE(r["status"].toString(), QString("stopped"));
        QCOMPARE(r["steps"].toArray()[0].toObject()["status"].toString(), QString("passed"));
        QCOMPARE(r["steps"].toArray()[1].toObject()["status"].toString(), QString("pending"));
    }
    void failureAndUnknown_data() { QTest::addColumn<QString>("mode"); QTest::addColumn<QString>("expected"); QTest::newRow("server-reject") << "failure" << "failed"; QTest::newRow("process-lost") << "crash" << "unknown"; QTest::newRow("drift") << "drift" << "blocked"; }
    void failureAndUnknown() {
        QFETCH(QString, mode); QFETCH(QString, expected); QTemporaryDir dir; Foundation s(dir.path()); setup(s, mode); auto p = plan(s); QVERIFY(p["ok"].toBool());
        QVERIFY(s.execute("execute-sync", {{"planId", p["plan"].toObject()["id"]}})["ok"].toBool());
        QTRY_VERIFY_WITH_TIMEOUT(!s.busy(), 5000); auto r = record(s); QCOMPARE(r["status"].toString(), expected);
        auto steps = r["steps"].toArray(); QCOMPARE(steps[2].toObject()["status"].toString(), QString("pending"));
        QCOMPARE(steps[0].toObject()["status"].toString(), mode == "drift" ? QString("pending") : QString("passed"));
    }
    void initialStorageFailure() {
        QTemporaryDir dir; Foundation s(dir.path()); setup(s); auto p = plan(s); QVERIFY(p["ok"].toBool());
        QFile obstruction(dir.filePath("sync-records")); QVERIFY(obstruction.open(QIODevice::WriteOnly)); obstruction.write("preserve"); obstruction.close();
        QCOMPARE(s.execute("execute-sync", {{"planId", p["plan"].toObject()["id"]}})["code"].toString(), QString("storage"));
        QVERIFY(!s.busy()); QCOMPARE(record(s)["steps"].toArray()[0].toObject()["status"].toString(), QString("pending"));
    }
    void stepStorageFailure() {
        QTemporaryDir dir; Foundation s(dir.path()); setup(s); auto p = plan(s); QVERIFY(p["ok"].toBool());
        QVERIFY(s.execute("execute-sync", {{"planId", p["plan"].toObject()["id"]}})["ok"].toBool());
        QTRY_COMPARE_WITH_TIMEOUT(record(s)["steps"].toArray()[0].toObject()["status"].toString(), QString("running"), 3000);
        QVERIFY(QDir(dir.filePath("sync-records")).removeRecursively());
        QFile obstruction(dir.filePath("sync-records")); QVERIFY(obstruction.open(QIODevice::WriteOnly)); obstruction.close();
        QTRY_VERIFY_WITH_TIMEOUT(!s.busy(), 5000);
        QCOMPARE(record(s)["status"].toString(), QString("unknown")); QCOMPARE(record(s)["steps"].toArray()[1].toObject()["status"].toString(), QString("pending"));
    }
    void crashRecovery() {
        QTemporaryDir dir; QDir().mkpath(dir.filePath("sync-records")); auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QFile file(dir.filePath("sync-records/" + id + ".json")); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(QJsonObject{{"id", id}, {"status", "running"}, {"steps", QJsonArray{QJsonObject{{"status", "passed"}}, QJsonObject{{"status", "running"}}, QJsonObject{{"status", "pending"}}}}}).toJson()); file.close();
        Foundation s(dir.path()); auto r = s.execute("sync-records", {})["records"].toArray()[0].toObject();
        QCOMPARE(r["status"].toString(), QString("unknown")); QCOMPARE(r["steps"].toArray()[1].toObject()["status"].toString(), QString("unknown")); QVERIFY(!s.busy());
    }
    void snapshotNoiseAndIdentity() {
        QJsonObject a{{"ok", true}, {"existence", "present"}, {"ddl", "CREATE TABLE `t` (`id` int) ENGINE=InnoDB AUTO_INCREMENT=2"}, {"serverUuid", "a"}, {"startedAt", "old"}};
        auto b = a; b["startedAt"] = "new"; b["ddl"] = "CREATE TABLE `t` (`id` int) ENGINE=InnoDB AUTO_INCREMENT=12";
        QVERIFY(sameSyncSnapshot(a, b, true)); b["serverUuid"] = "b"; QVERIFY(!sameSyncSnapshot(a, b, true));
        b = a; b["ddl"] = "CREATE TABLE `t` (`id` bigint) ENGINE=InnoDB AUTO_INCREMENT=2"; QVERIFY(!sameSyncSnapshot(a, b, false));
    }
};
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    if (argc > 1 && QByteArray(argv[1]) == "--schema") {
        // This binary substitutes only process outcomes; SQL correctness uses real MySQL tests.
        QFile in; if (!in.open(stdin, QIODevice::ReadOnly)) return 1; auto p = QJsonDocument::fromJson(in.readAll()).object(); QThread::msleep(100);
        QJsonObject result{{"ok", true}}; auto operation = p["operation"].toString();
        QJsonObject left{{"database", "a"}, {"table", "t"}}, right{{"database", "b"}, {"table", "t"}};
        if (operation == "compare" || operation == "sync-read") result["comparison"] = QJsonObject{{"left", left}, {"right", right}, {"complete", true}, {"status", "same"}};
        if (operation == "plan-sync") {
            QJsonArray steps; for (int i=0; i<3; ++i) steps.append(QJsonObject{{"name", QString::number(i)}, {"sql", "FIXTURE ONLY"}, {"summary", "mock process step"}});
            result["plan"] = QJsonObject{{"direction", "left-to-right"}, {"left", left}, {"right", right}, {"steps", steps}, {"sql", "FIXTURE ONLY"}};
        }
        if (operation == "sync-check" && p["right"].toObject()["name"] == "drift") result = {{"ok", false}, {"error", "fixture drift"}};
        if (operation == "sync-step") {
            QThread::msleep(250); auto mode = p["connection"].toObject()["name"].toString();
            if (p["step"].toObject()["name"] == "1") { if (mode == "crash") return 2; if (mode == "failure") result = {{"ok", false}, {"error", "fixture rejected"}, {"code", "1064"}}; }
        }
        QFile out; if (!out.open(stdout, QIODevice::WriteOnly)) return 1; out.write(QJsonDocument(result).toJson(QJsonDocument::Compact)); return 0;
    }
    SyncBridgeTest t; return QTest::qExec(&t, argc, argv);
}
#include "sync_bridge_test.moc"
