#include "foundation.h"
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QUuid>
#include <cstdio>

class MergeBridgeTest : public QObject {
    Q_OBJECT
    int serial=0;
    QJsonObject send(Foundation &service,const QString &operation,QJsonObject args={}) {
        const auto id=QString::number(++serial); QSignalSpy spy(&service,&Foundation::response);
        service.request(QString::fromUtf8(QJsonDocument(QJsonObject{{"requestId",id},{"operation",operation},{"args",args}}).toJson()));
        if(spy.isEmpty())spy.wait(4000);
        for(const auto &entry:spy){auto r=QJsonDocument::fromJson(entry[0].toString().toUtf8()).object();if(r["requestId"]==id)return r;}
        return {};
    }
    void setup(Foundation &service,const QString &mode="normal") {
        for(auto side:{"left","right"}){
            QJsonObject c{{"name",mode},{"host","fixture"},{"port",3306},{"user","fixture"},{"password","test-secret"},{"database",""},{"remember",false},{"tls","preferred"},{"ca",""},{"timeout",1}};
            auto saved=service.execute("save",c);QVERIFY(saved["ok"].toBool());
            QVERIFY(service.execute("select",{{"side",side},{"id",saved["id"]}})["ok"].toBool());
        }
    }
    QJsonObject plan(Foundation &service,const QString &mode="fill") {
        const QJsonObject args{{"left",QJsonObject{{"database","a"},{"table","t"}}},{"right",QJsonObject{{"database","b"},{"table","t"}}},{"mode","compare"},{"key",QJsonArray{"id"}},{"fields",QJsonArray{"id","value"}},{"filters",QJsonArray{}}};
        auto started=send(service,"data-start",args);if(!started["ok"].toBool())return started;
        QElapsedTimer wait;wait.start();while(service.busy()&&wait.elapsed()<4000)QTest::qWait(20);
        if(service.busy())return {{"ok",false},{"error","fixture scan timeout"}};
        return send(service,"merge-plan",{{"taskId",started["taskId"]},{"direction","left-to-right"},{"mode",mode}});
    }
    QJsonObject record(Foundation &service){return service.execute("merge-status",{})["record"].toObject();}
    QJsonObject execute(Foundation &service,const QJsonObject &p){return service.execute("merge-execute",{{"planId",p["plan"].toObject()["id"]},{"confirmed",true}});}
private slots:
    void deleteConfirmation() {
        QTemporaryDir dir;Foundation s(dir.path());setup(s);auto p=plan(s,"align");QVERIFY(p["ok"].toBool());
        const auto id=p["plan"].toObject()["id"];
        QCOMPARE(execute(s,p)["code"].toString(),QString("validation"));QVERIFY(!s.busy());
        auto args=QJsonObject{{"planId",id},{"confirmed",true},{"deleteConfirmed",true}};
        QVERIFY(s.execute("merge-execute",args)["ok"].toBool());
        QVERIFY(!s.execute("merge-execute",args)["ok"].toBool());
        QTRY_VERIFY_WITH_TIMEOUT(!s.busy(),6000);
        QCOMPARE(record(s)["mode"].toString(),QString("data-align"));
        QCOMPARE(record(s)["counts"].toObject()["delete"].toInt(),600);
        QVERIFY(!s.execute("merge-execute",args)["ok"].toBool());
        p=plan(s,"align");QVERIFY(p["ok"].toBool());
        QCOMPARE(execute(s,p)["code"].toString(),QString("validation"));
        s.execute("merge-invalidate",{});
        args["planId"]=p["plan"].toObject()["id"];
        QCOMPARE(s.execute("merge-execute",args)["code"].toString(),QString("stale"));
    }
    void confirmationStaleAndDuplicate() {
        QTemporaryDir dir;Foundation s(dir.path());setup(s);auto p=plan(s);QVERIFY(p["ok"].toBool());
        const auto id=p["plan"].toObject()["id"];
        QCOMPARE(s.execute("merge-execute",{{"planId",id}})["code"].toString(),QString("validation"));
        QVERIFY(s.execute("merge-invalidate",{})["ok"].toBool());
        QCOMPARE(execute(s,p)["code"].toString(),QString("stale"));
        p=plan(s);QVERIFY(p["ok"].toBool());QVERIFY(execute(s,p)["ok"].toBool());
        QCOMPARE(execute(s,p)["code"].toString(),QString("busy"));
        QTRY_VERIFY_WITH_TIMEOUT(!s.busy(),6000);
        QCOMPARE(record(s)["status"].toString(),QString("passed"));QCOMPARE(record(s)["committed"].toInt(),600);
        QCOMPARE(execute(s,p)["code"].toString(),QString("stale"));
        Foundation recovered(dir.path());auto records=recovered.execute("sync-records",{})["records"].toArray();QCOMPARE(records.size(),1);
        const auto bytes=QJsonDocument(records).toJson();QVERIFY(!bytes.contains("test-secret"));QVERIFY(!bytes.contains("fixture-value"));QVERIFY(!bytes.contains("host"));
    }
    void nativeContextLocked() {
        QTemporaryDir dir;Foundation s(dir.path());setup(s);auto p=plan(s);QVERIFY(p["ok"].toBool());QVERIFY(execute(s,p)["ok"].toBool());
        for(const auto &operation:QStringList{"select","settings","cancel-schema","data-invalidate","merge-invalidate"})
            QCOMPARE(s.execute(operation,{})["code"].toString(),QString("busy"));
        QCOMPARE(send(s,"plan-sync",{{"direction","left-to-right"},{"alignAll",true}})["code"].toString(),QString("busy"));
        s.execute("merge-stop",{});QTRY_VERIFY_WITH_TIMEOUT(!s.busy(),5000);
    }
    void stopJobsWaitsForBatch() {
        QTemporaryDir dir;Foundation s(dir.path());setup(s);auto p=plan(s);QVERIFY(p["ok"].toBool());QVERIFY(execute(s,p)["ok"].toBool());
        QTRY_COMPARE_WITH_TIMEOUT(record(s)["batches"].toArray()[0].toObject()["status"].toString(),QString("running"),3000);
        s.stopJobs();QVERIFY(s.busy());QTRY_VERIFY_WITH_TIMEOUT(!s.busy(),5000);
        auto r=record(s);QCOMPARE(r["status"].toString(),QString("stopped"));QCOMPARE(r["committed"].toInt(),256);
        QCOMPARE(r["batches"].toArray()[0].toObject()["status"].toString(),QString("passed"));
        QCOMPARE(r["batches"].toArray()[1].toObject()["status"].toString(),QString("pending"));
    }
    void failedBatch_data() {
        QTest::addColumn<QString>("mode");QTest::addColumn<QString>("expected");
        QTest::newRow("rolled-back")<<"failure"<<"failed";
        QTest::newRow("commit-unknown")<<"unknown"<<"unknown";
        QTest::newRow("process-lost")<<"crash"<<"unknown";
    }
    void failedBatch() {
        QFETCH(QString,mode);QFETCH(QString,expected);QTemporaryDir dir;Foundation s(dir.path());setup(s,mode);auto p=plan(s);QVERIFY(p["ok"].toBool());QVERIFY(execute(s,p)["ok"].toBool());
        QTRY_VERIFY_WITH_TIMEOUT(!s.busy(),6000);auto r=record(s);QCOMPARE(r["status"].toString(),expected);QCOMPARE(r["committed"].toInt(),256);
        const auto batches=r["batches"].toArray();QCOMPARE(batches[0].toObject()["status"].toString(),QString("passed"));QCOMPARE(batches[1].toObject()["status"].toString(),expected);QCOMPARE(batches[2].toObject()["status"].toString(),QString("pending"));
    }
    void initialStorageFailure() {
        QTemporaryDir dir;Foundation s(dir.path());setup(s);auto p=plan(s);QVERIFY(p["ok"].toBool());
        QFile obstruction(dir.filePath("sync-records"));QVERIFY(obstruction.open(QIODevice::WriteOnly));obstruction.write("preserve");obstruction.close();
        QCOMPARE(execute(s,p)["code"].toString(),QString("storage"));QVERIFY(!s.busy());QCOMPARE(record(s)["committed"].toInt(),0);
        QCOMPARE(record(s)["batches"].toArray()[0].toObject()["status"].toString(),QString("pending"));
        const auto listed=s.execute("sync-records",{})["records"].toArray();QCOMPARE(listed.size(),1);
        QCOMPARE(listed[0].toObject()["status"].toString(),QString("blocked"));QVERIFY(!listed[0].toObject()["error"].toString().isEmpty());
    }
    void batchStorageFailure() {
        QTemporaryDir dir;Foundation s(dir.path());setup(s);auto p=plan(s);QVERIFY(p["ok"].toBool());QVERIFY(execute(s,p)["ok"].toBool());
        QTRY_COMPARE_WITH_TIMEOUT(record(s)["batches"].toArray()[0].toObject()["status"].toString(),QString("running"),3000);
        QVERIFY(QDir(dir.filePath("sync-records")).removeRecursively());QFile obstruction(dir.filePath("sync-records"));QVERIFY(obstruction.open(QIODevice::WriteOnly));obstruction.close();
        QTRY_VERIFY_WITH_TIMEOUT(!s.busy(),5000);auto r=record(s);QCOMPARE(r["status"].toString(),QString("blocked"));QCOMPARE(r["committed"].toInt(),256);
        QCOMPARE(r["batches"].toArray()[1].toObject()["status"].toString(),QString("pending"));QVERIFY(r.contains("storageWarning"));
    }
    void crashRecovery_data() {
        QTest::addColumn<QString>("recordMode");
        QTest::newRow("fill")<<"data-fill";QTest::newRow("align")<<"data-align";
    }
    void crashRecovery() {
        QFETCH(QString,recordMode);
        QTemporaryDir dir;QVERIFY(QDir().mkpath(dir.filePath("sync-records")));const auto id=QUuid::createUuid().toString(QUuid::WithoutBraces);
        QFile f(dir.filePath("sync-records/"+id+".json"));QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QJsonDocument(QJsonObject{{"id",id},{"mode",recordMode},{"status","running"},{"committed",256},{"batches",QJsonArray{QJsonObject{{"status","passed"}},QJsonObject{{"status","running"}},QJsonObject{{"status","pending"}}}}}).toJson());f.close();
        Foundation s(dir.path());auto rows=s.execute("sync-records",{})["records"].toArray();QCOMPARE(rows.size(),1);const auto r=rows[0].toObject();
        QCOMPARE(r["status"].toString(),QString("unknown"));QCOMPARE(r["committed"].toInt(),256);QCOMPARE(r["batches"].toArray()[1].toObject()["status"].toString(),QString("unknown"));QCOMPARE(r["batches"].toArray()[2].toObject()["status"].toString(),QString("pending"));QVERIFY(!s.busy());
        QVERIFY(f.open(QIODevice::ReadOnly));const auto persisted=QJsonDocument::fromJson(f.readAll()).object();f.close();
        QCOMPARE(persisted["status"].toString(),QString("unknown"));QCOMPARE(persisted["batches"].toArray()[1].toObject()["status"].toString(),QString("unknown"));
        Foundation again(dir.path());const auto reloaded=again.execute("sync-records",{})["records"].toArray();QCOMPARE(reloaded.size(),1);
        QCOMPARE(reloaded[0].toObject()["status"].toString(),QString("unknown"));QCOMPARE(reloaded[0].toObject()["batches"],persisted["batches"]);QCOMPARE(reloaded[0].toObject()["committed"].toInt(),256);
    }
};

int main(int argc,char **argv) {
    QApplication app(argc,argv);
    if(argc>1&&QByteArray(argv[1])=="--data") {
        // Only process outcomes are fixtures; real MySQL tests verify SQL behavior.
        QFile in;if(!in.open(stdin,QIODevice::ReadOnly))return 1;const auto p=QJsonDocument::fromJson(in.readAll()).object();
        const auto operation=p["operation"].toString();const auto args=p["args"].toObject();QJsonObject result{{"ok",true}};
        if(operation=="data-start") {
            QJsonObject task{{"id",p["taskId"]},{"state","complete"},{"mode","compare"},{"phase","complete"},{"complete",true},{"counts",QJsonObject{{"leftOnly",600}}}};
            QFile status(p["path"].toString()+"/status.json");if(!status.open(QIODevice::WriteOnly))return 1;status.write(QJsonDocument(task).toJson());status.close();result["task"]=task;
        } else if(operation=="merge-plan") {
            result["plan"]=QJsonObject{{"id",QUuid::createUuid().toString(QUuid::WithoutBraces)},{"taskId",args["taskId"]},{"direction","left-to-right"},{"mode","fill"},{"left",QJsonObject{{"database","a"},{"table","t"}}},{"right",QJsonObject{{"database","b"},{"table","t"}}},{"key",QJsonArray{"id"}},{"fields",QJsonArray{"value"}},{"filters",QJsonArray{}},{"counts",QJsonObject{{"add",600},{"modify",0},{"delete",0}}},{"total",600},{"batchSize",256},{"sql",QJsonArray{"FIXTURE ONLY"}}};
            if(args["mode"]=="align") {
                auto plan=result["plan"].toObject();plan["mode"]="align";
                plan["counts"]=QJsonObject{{"add",0},{"modify",0},{"delete",600}};result["plan"]=plan;
            }
        } else if(operation=="merge-batch") {
            QThread::msleep(400);const auto offset=args["offset"].toInt();const auto mode=p["right"].toObject()["name"].toString();
            result={{"ok",true},{"status","passed"},{"committed",qMin(256,600-offset)}};
            if(offset==256){if(mode=="crash")return 2;if(mode=="failure"||mode=="unknown")result={{"ok",false},{"status",mode=="failure"?"failed":"unknown"},{"committed",0},{"code","fixture"},{"error","fixture rejected"}};}
        } else return 2;
        QFile out;if(!out.open(stdout,QIODevice::WriteOnly))return 1;out.write(QJsonDocument(result).toJson(QJsonDocument::Compact));return 0;
    }
    MergeBridgeTest test;return QTest::qExec(&test,argc,argv);
}
#include "merge_bridge_test.moc"
