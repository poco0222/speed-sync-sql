#include "foundation.h"
#include "credentials.h"
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QSignalSpy>
#include <QJsonDocument>
#include <QScopeGuard>
class FoundationTest : public QObject {
    Q_OBJECT
    QJsonObject connection() { return {{"name", "本地测试"}, {"host", "127.0.0.1"}, {"port", 3306}, {"user", "test"}, {"password", "test-secret-NEVER-PERSIST"}, {"database", ""}, {"tls", "preferred"}, {"ca", ""}, {"remember", false}, {"timeout", QJsonValue::Null}}; }
private slots:
    void swapEndpointsKeepsCurrentMappingAndIsAtomic() {
        QTemporaryDir dir; Foundation service(dir.path());
        QCOMPARE(service.execute("swap-endpoints", {})["code"].toString(), QString("validation"));
        const QJsonValue a = service.execute("save", connection())["id"], b = service.execute("save", connection())["id"];
        QVERIFY(service.execute("select", {{"side", "left"}, {"id", b}})["ok"].toBool());
        QVERIFY(service.execute("select", {{"side", "right"}, {"id", a}})["ok"].toBool());
        QJsonObject original{{"left", QJsonObject{{"database", "source"}, {"table", "orders"}}}, {"right", QJsonObject{{"database", "target"}, {"table", "archive"}}}, {"width", 240}};
        auto historic = original; historic["left"] = QJsonObject{{"database", "obsolete"}, {"table", "old"}};
        QVERIFY(service.execute("workspace", historic)["ok"].toBool());
        QVERIFY(service.execute("select", {{"side", "left"}, {"id", a}})["ok"].toBool());
        QVERIFY(service.execute("select", {{"side", "right"}, {"id", b}})["ok"].toBool());
        QVERIFY(service.execute("workspace", original)["ok"].toBool());
        auto swapped = original; swapped["left"] = original["right"]; swapped["right"] = original["left"];
        const auto result = service.execute("swap-endpoints", {});
        QVERIFY(result["ok"].toBool());
        QCOMPARE(result["state"].toObject()["left"], b); QCOMPARE(result["state"].toObject()["right"], a);
        QCOMPARE(result["state"].toObject()["workspace"].toObject(), swapped);
        Foundation restarted(dir.path()); QCOMPARE(restarted.snapshot()["workspace"].toObject(), swapped);
        QVERIFY(service.execute("swap-endpoints", {})["ok"].toBool());
        QCOMPARE(service.snapshot()["workspace"].toObject(), original);
        const auto before = service.snapshot();
        QVERIFY(QFile::remove(dir.filePath("connections.json")));
        QVERIFY(QDir().mkdir(dir.filePath("connections.json")));
        QCOMPARE(service.execute("swap-endpoints", {})["code"].toString(), QString("storage"));
        QCOMPARE(service.snapshot(), before);
    }
    void recentWorkspaceIsLocalAndScoped() {
        QTemporaryDir dir; Foundation service(dir.path());
        QJsonValue a = service.execute("save", connection())["id"];
        QJsonValue b = service.execute("save", connection())["id"];
        QVERIFY(service.execute("select", {{"side", "left"}, {"id", a}})["ok"].toBool());
        QVERIFY(service.execute("select", {{"side", "right"}, {"id", b}})["ok"].toBool());
        QJsonObject selection{{"left", QJsonObject{{"database", "left_db"}, {"table", "t"}}}, {"right", QJsonObject{{"database", "right_db"}, {"table", "other"}}}, {"width", 240}};
        QVERIFY(service.execute("workspace", selection)["ok"].toBool());
        QVERIFY(!service.busy());
        Foundation restarted(dir.path()); QCOMPARE(restarted.snapshot()["workspace"].toObject(), selection);
        QCOMPARE(restarted.execute("export-schema", {})["code"].toString(), QString("stale"));
        QVERIFY(service.execute("select", {{"side", "right"}, {"id", a}})["ok"].toBool());
        QVERIFY(service.snapshot()["workspace"].toObject().isEmpty());
        selection["width"] = 10000; QVERIFY(!service.execute("workspace", selection)["ok"].toBool());
        selection["width"] = 240; selection["left"] = QJsonObject{{"database", QString(65, 'a')}, {"table", "t"}};
        QVERIFY(!service.execute("workspace", selection)["ok"].toBool());
    }
    void configLifecycle() {
        QTemporaryDir dir;
        Foundation service(dir.path());
        auto saved = service.execute("save", connection()); QVERIFY(saved["ok"].toBool());
        auto id = saved["id"].toString(); QVERIFY(!id.isEmpty());
        QVERIFY(service.execute("select", {{"side", "left"}, {"id", id}})["ok"].toBool());
        QVERIFY(service.execute("select", {{"side", "right"}, {"id", id}})["ok"].toBool());
        QFile file(dir.filePath("connections.json")); QVERIFY(file.open(QIODevice::ReadOnly));
        auto bytes = file.readAll(); QVERIFY(!bytes.contains("test-secret")); QVERIFY(!bytes.contains("password"));
        file.close();
        Foundation restarted(dir.path()); QCOMPARE(restarted.snapshot()["left"].toString(), id);
        auto edit = connection(); edit["id"] = id; edit["name"] = "重命名"; edit.remove("password");
        QVERIFY(service.execute("save", edit)["ok"].toBool());
        auto clone = connection(); clone.remove("id"); auto copied = service.execute("save", clone); QVERIFY(copied["id"] != id);
        QVERIFY(service.execute("delete", {{"id", id}})["ok"].toBool());
        QCOMPARE(service.snapshot()["left"].toString(), QString()); QCOMPARE(service.snapshot()["right"].toString(), QString());
        QVERIFY(!service.execute("save", edit)["ok"].toBool());
    }
    void invalidInputAndCorruption() {
        QTemporaryDir dir; Foundation service(dir.path()); auto c = connection();
        for (double port : {0.0, 65536.0, 1.5}) { c["port"] = port; QVERIFY(!service.execute("save", c)["ok"].toBool()); }
        c = connection(); c["ca"] = "ca.pem;MYSQL_OPT_SSL_MODE=DISABLED"; QVERIFY(!validateConnection(c).isEmpty());
        c = connection(); c["timeout"] = 0; QVERIFY(!validateConnection(c).isEmpty());
        QVERIFY(!service.execute("settings", {{"theme", "invalid"}})["ok"].toBool());
        QFile file(dir.filePath("connections.json")); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("broken"); file.close();
        Foundation damaged(dir.path()); QVERIFY(!damaged.snapshot()["loadError"].toString().isEmpty());
        QVERIFY(!damaged.execute("save", connection())["ok"].toBool());
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), QByteArray("broken"));
    }
    void storageFailure() {
        QTemporaryDir dir; QFile file(dir.filePath("not-a-directory")); QVERIFY(file.open(QIODevice::WriteOnly)); file.close();
        Foundation service(file.fileName()); QVERIFY(!service.execute("save", connection())["ok"].toBool());
    }
    void errorClassification() {
        QCOMPARE(classifyDatabaseError(1045)["code"].toString(), QString("authentication"));
        QCOMPARE(classifyDatabaseError(1049)["code"].toString(), QString("database"));
        QCOMPARE(classifyDatabaseError(2003)["code"].toString(), QString("network"));
        QCOMPARE(classifyDatabaseError(2026)["code"].toString(), QString("tls"));
        QCOMPARE(classifyDatabaseError(9999)["code"].toString(), QString("connection"));
    }
    void bridgeBoundary() {
        QTemporaryDir dir; Foundation service(dir.path()); QSignalSpy spy(&service, &Foundation::response);
        service.request("{\"requestId\":\"r1\",\"operation\":\"unknown\",\"args\":{}}");
        QCOMPARE(spy.size(), 1); auto reply = QJsonDocument::fromJson(spy.takeFirst().at(0).toString().toUtf8()).object();
        QVERIFY(!reply["ok"].toBool()); QCOMPARE(reply["requestId"].toString(), QString("r1"));
    }
    void platformCredentials() {
        if (!qEnvironmentVariableIsSet("SPEED_SYNC_TEST_CREDENTIALS")) QSKIP("Set SPEED_SYNC_TEST_CREDENTIALS=1 for isolated system-credential lifecycle check");
        QTemporaryDir dir; Foundation service(dir.path()); auto c = connection(); c["remember"] = true;
        auto saved = service.execute("save", c); QVERIFY2(saved["ok"].toBool(), qPrintable(saved["error"].toString()));
        auto id = saved["id"].toString(); QString error;
        const auto cleanup = qScopeGuard([&] { QString ignored; Credentials::remove(service.credentialKey(id), ignored); });
        auto value = Credentials::read(service.credentialKey(id), error); QVERIFY(error.isEmpty()); QVERIFY(value == c["password"].toString());
        c["id"] = id; c["remember"] = false; c.remove("password");
        QVERIFY(service.execute("save", c)["ok"].toBool());
        Credentials::read(service.credentialKey(id), error); QVERIFY(!error.isEmpty());
    }
    void credentialRollbackAndMissing() {
        if (!qEnvironmentVariableIsSet("SPEED_SYNC_TEST_CREDENTIALS")) QSKIP("System credentials opt-in");
        QTemporaryDir dir; Foundation service(dir.path()); auto c = connection(); c["remember"] = true;
        auto saved = service.execute("save", c); QVERIFY(saved["ok"].toBool());
        auto id = saved["id"].toString(); auto key = service.credentialKey(id);
        const auto cleanup = qScopeGuard([&] { QString ignored; Credentials::remove(key, ignored); });
        const QString filename = dir.filePath("connections.json");
        QVERIFY(QFile::remove(filename)); QVERIFY(QDir().mkdir(filename));
        auto edit = c; edit["id"] = id; edit["password"] = "replacement-fixture-secret";
        QVERIFY(!service.execute("save", edit)["ok"].toBool());
        QString error; auto restored = Credentials::read(key, error);
        QVERIFY(error.isEmpty()); QVERIFY(restored == c["password"].toString());
        QVERIFY(QDir().rmdir(filename));
        QVERIFY(Credentials::remove(key, error));
        bool missing = false; Credentials::read(key, error, &missing); QVERIFY(missing);
        QVERIFY(service.execute("delete", {{"id", id}})["ok"].toBool());
        saved = service.execute("save", c); QVERIFY(saved["ok"].toBool());
        id = saved["id"].toString(); key = service.credentialKey(id); error.clear();
        QVERIFY(Credentials::remove(key, error));
        edit = c; edit["id"] = id; edit["remember"] = false; edit.remove("password");
        QVERIFY(service.execute("save", edit)["ok"].toBool());
    }
};
QTEST_GUILESS_MAIN(FoundationTest)
#include "foundation_test.moc"
