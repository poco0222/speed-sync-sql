#include "foundation.h"
#include <QCoreApplication>
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
    QCoreApplication app(argc, argv);
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
