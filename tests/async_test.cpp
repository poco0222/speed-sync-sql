#include "foundation.h"
#include <QCoreApplication>
#include <QApplication>
#include <QFileDialog>
#include <QLineEdit>
#include <QClipboard>
#include <QFile>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTimer>
#include <cstdio>

class AsyncTest : public QObject {
    Q_OBJECT
    QJsonObject connection(const QString &name, bool stall = false) {
        return {{"name", name}, {"host", stall ? "stall" : "slow"}, {"port", 3306}, {"user", "fixture"}, {"password", ""}, {"database", ""}, {"remember", false}, {"tls", "preferred"}, {"timeout", 1}};
    }
    QJsonObject save(Foundation &service, const QString &side, bool stall = false) {
        auto c = connection(side, stall); auto r = service.execute("save", c);
        if (!r["ok"].toBool()) qFatal("Fixture save failed"); c["id"] = r["id"];
        if (!service.execute("select", {{"side", side}, {"id", r["id"]}})["ok"].toBool()) qFatal("Fixture select failed");
        return c;
    }
    void start(Foundation &service, const QString &request, const QString &side, const QJsonObject &c) {
        service.request(QString::fromUtf8(QJsonDocument(QJsonObject{{"requestId", request}, {"operation", "test"}, {"args", QJsonObject{{"lane", side}, {"connection", c}}}}).toJson()));
    }
    QJsonObject response(const QSignalSpy &spy, const QString &id) {
        for (const auto &entry : spy) {
            auto result = QJsonDocument::fromJson(entry[0].toString().toUtf8()).object();
            if (result["requestId"] == id) return result;
        }
        return {};
    }
private slots:
    void schemaCancellationAndLateResults() {
        QTemporaryDir dir; Foundation service(dir.path()); QSignalSpy spy(&service, &Foundation::response);
        auto c = save(service, "left"); save(service, "right");
        auto send = [&](const QString &id, const QString &operation, const QJsonObject &args) {
            service.request(QString::fromUtf8(QJsonDocument(QJsonObject{{"requestId", id}, {"operation", operation}, {"args", args}}).toJson()));
        };
        send("catalog", "schema", {{"side", "left"}, {"action", "databases"}});
        send("duplicate", "schema", {{"side", "left"}, {"action", "databases"}});
        QCOMPARE(response(spy, "duplicate")["code"].toString(), QString("busy"));
        QVERIFY(service.execute("cancel-schema", {})["ok"].toBool());
        send("replacement", "schema", {{"side", "left"}, {"action", "databases"}});
        QTRY_VERIFY_WITH_TIMEOUT(!service.busy(), 3000);
        QVERIFY(response(spy, "catalog")["stale"].toBool());
        QVERIFY(response(spy, "replacement")["ok"].toBool());
        QVERIFY(!response(spy, "replacement")["stale"].toBool());
        QJsonObject pair{{"left", QJsonObject{{"database", "a"}, {"table", "t"}}}, {"right", QJsonObject{{"database", "b"}, {"table", "t"}}}};
        send("compare", "compare", pair); c["name"] = "changed";
        QVERIFY(service.execute("save", c)["ok"].toBool());
        QTRY_VERIFY_WITH_TIMEOUT(!service.busy(), 3000);
        QVERIFY(response(spy, "compare")["stale"].toBool());
        QCOMPARE(service.execute("export-schema", {})["code"].toString(), QString("stale"));
    }
    void schemaExportCancelAndFailure() {
        QTemporaryDir dir; Foundation service(dir.path()); QSignalSpy spy(&service, &Foundation::response);
        save(service, "left"); save(service, "right");
        QJsonObject pair{{"left", QJsonObject{{"database", "a"}, {"table", "t"}}}, {"right", QJsonObject{{"database", "b"}, {"table", "t"}}}};
        service.request(QString::fromUtf8(QJsonDocument(QJsonObject{{"requestId", "compare"}, {"operation", "compare"}, {"args", pair}}).toJson()));
        QTRY_VERIFY_WITH_TIMEOUT(!service.busy(), 3000);
        QVERIFY(response(spy, "compare")["ok"].toBool());
        QVERIFY(service.execute("copy-schema", {{"side", "left"}})["ok"].toBool());
        QCOMPARE(QApplication::clipboard()->text(), QString("RAW DEFINITION"));
        QVERIFY(!service.execute("copy-schema", {{"side", "invalid"}})["ok"].toBool());
        auto choose = [](const QString &filename) {
            QTimer::singleShot(50, [filename]() {
                for (auto widget : QApplication::topLevelWidgets()) if (auto dialog = qobject_cast<QFileDialog *>(widget)) {
                    if (filename.isEmpty()) dialog->reject();
                    else { dialog->findChild<QLineEdit *>("fileNameEdit")->setText(filename); QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection); }
                }
            });
        };
        choose({}); QVERIFY(service.execute("export-schema", {})["cancelled"].toBool());
        const auto path = dir.filePath("report.json"); choose(path);
        QVERIFY(service.execute("export-schema", {})["ok"].toBool());
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); const auto bytes = file.readAll();
        QVERIFY(!bytes.contains("secret-fixture")); QVERIFY(!bytes.contains("host")); QVERIFY(!bytes.contains("RAW DEFINITION"));
        const auto report = QJsonDocument::fromJson(bytes).object();
        QCOMPARE(report["rows"].toArray().size(), 2); QCOMPARE(report["formatVersion"].toInt(), 1);
        QCOMPARE(report["left"].toObject()["name"].toString(), QString("left"));
        const auto blocked = dir.filePath("blocked.json");
        QTimer::singleShot(50, [blocked]() {
            for (auto widget : QApplication::topLevelWidgets()) if (auto dialog = qobject_cast<QFileDialog *>(widget)) {
                QObject::connect(dialog, &QFileDialog::accepted, [blocked]() { QDir().mkdir(blocked); });
                dialog->findChild<QLineEdit *>("fileNameEdit")->setText(blocked); QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
            }
        });
        QCOMPARE(service.execute("export-schema", {})["code"].toString(), QString("storage"));
        service.execute("cancel-schema", {});
        QCOMPARE(service.execute("copy-schema", {{"side", "left"}})["code"].toString(), QString("stale"));
    }
    void independentLanesAndHeartbeat() {
        QTemporaryDir dir; Foundation service(dir.path()); QSignalSpy spy(&service, &Foundation::response);
        auto left = save(service, "left"), right = save(service, "right");
        int ticks = 0; QTimer timer; connect(&timer, &QTimer::timeout, [&] { ++ticks; }); timer.start(10);
        start(service, "left1", "left", left); start(service, "left2", "left", left); start(service, "right1", "right", right);
        QVERIFY(service.busy()); QVERIFY(!response(spy, "left2")["ok"].toBool());
        QTRY_VERIFY_WITH_TIMEOUT(!service.busy(), 3000);
        QVERIFY(response(spy, "left1")["ok"].toBool()); QVERIFY(response(spy, "right1")["ok"].toBool());
        QVERIFY(ticks >= 10);
    }
    void editingAndDeletionInvalidateProcesses() {
        QTemporaryDir dir; Foundation service(dir.path()); QSignalSpy spy(&service, &Foundation::response);
        auto c = save(service, "left"); start(service, "edit", "left", c);
        c["name"] = "changed while pending"; QVERIFY(service.execute("save", c)["ok"].toBool());
        QTRY_VERIFY_WITH_TIMEOUT(!service.busy(), 3000);
        QVERIFY(response(spy, "edit")["stale"].toBool());
        QVERIFY(!service.snapshot()["connections"].toArray()[0].toObject().contains("lastTest"));
        start(service, "delete", "left", c);
        QVERIFY(service.execute("delete", {{"id", c["id"]}})["ok"].toBool());
        QTRY_VERIFY_WITH_TIMEOUT(!service.busy(), 3000);
        QVERIFY(response(spy, "delete")["stale"].toBool());
        QVERIFY(service.snapshot()["connections"].toArray().isEmpty());
        QCOMPARE(service.snapshot()["left"].toString(), QString());
    }
    void stopReapsRunningProcess() {
        QTemporaryDir dir; Foundation service(dir.path()); QSignalSpy spy(&service, &Foundation::response);
        auto c = save(service, "left", true); start(service, "stop", "left", c);
        QTest::qWait(150); QVERIFY(service.busy()); service.stopJobs();
        QTRY_VERIFY_WITH_TIMEOUT(!service.busy(), 3000);
        QVERIFY(!response(spy, "stop")["ok"].toBool());
    }
    void timeoutKeepsEventLoopResponsive() {
        QTemporaryDir dir; Foundation service(dir.path()); QSignalSpy spy(&service, &Foundation::response);
        auto c = save(service, "right", true);
        int ticks = 0; QTimer timer; connect(&timer, &QTimer::timeout, [&] { ++ticks; }); timer.start(20);
        start(service, "timeout", "right", c);
        QTRY_VERIFY_WITH_TIMEOUT(!service.busy(), 8500);
        QCOMPARE(response(spy, "timeout")["code"].toString(), QString("timeout"));
        QVERIFY(ticks > 100);
    }
};
int main(int argc, char **argv) {
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    if (argc > 1 && QByteArray(argv[1]) == "--schema") {
        QFile input; if (!input.open(stdin, QIODevice::ReadOnly)) return 1;
        const auto payload = QJsonDocument::fromJson(input.readAll()).object();
        QThread::msleep(300);
        QJsonObject result{{"ok", true}};
        result["items"] = QJsonArray{QJsonObject{{"name", "fixture"}, {"type", "database"}}};
        if (payload["operation"] == "compare") {
            const QJsonObject endpoint{{"database", "fixture"}, {"table", "t"}, {"version", "8.0-fixture"}, {"ddl", "RAW DEFINITION"}, {"host", "private-host"}, {"password", "secret-fixture"}};
            const QJsonArray rows{QJsonObject{{"name", "a"}, {"status", "different"}}, QJsonObject{{"name", "b"}, {"status", "same"}}};
            result["comparison"] = QJsonObject{{"complete", true}, {"status", "different"}, {"left", endpoint}, {"right", endpoint}, {"rows", rows}};
        }
        QFile output; if (!output.open(stdout, QIODevice::WriteOnly)) return 1;
        output.write(QJsonDocument(result).toJson(QJsonDocument::Compact)); return 0;
    }
    if (argc > 1 && QByteArray(argv[1]) == "--probe") {
        // Only this test executable replaces the SQL probe with a controllable delayed child.
        QFile input; if (!input.open(stdin, QIODevice::ReadOnly)) return 1;
        const auto c = QJsonDocument::fromJson(input.readAll()).object();
        QThread::msleep(c["host"] == "stall" ? 120000 : 500);
        QFile output; if (!output.open(stdout, QIODevice::WriteOnly)) return 1;
        output.write("{\"ok\":true,\"version\":\"8.0-test-fixture\",\"encrypted\":true}");
        return 0;
    }
    AsyncTest test; return QTest::qExec(&test, argc, argv);
}
#include "async_test.moc"
