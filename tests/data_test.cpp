#include "data.h"
#include "foundation.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

static QByteArray stored(const QString &s) { auto u=s.toUcs4(); return QByteArray(reinterpret_cast<const char *>(u.constData()),u.size()*4); }

class DataTest : public QObject {
    Q_OBJECT
private slots:
    void exactNumbers() {
        bool ok;
        QCOMPARE(canonicalDataValue("bigint","18446744073709551615",&ok),QString("18446744073709551615e0")); QVERIFY(ok);
        auto a=canonicalDataValue("decimal","1234567890123456789012345.123456789012345",&ok); QVERIFY(ok);
        auto b=canonicalDataValue("decimal","1234567890123456789012345.123456789012346",&ok); QVERIFY(ok); QVERIFY(a!=b);
        QCOMPARE(canonicalDataValue("decimal","-0.0000",&ok),QString("0"));
        QCOMPARE(canonicalDataValue("double","1.20e2",&ok),canonicalDataValue("double","120",&ok));
        auto nearFloatA=canonicalDataValue("float","1.0000001192092896",&ok); QVERIFY(ok);
        auto nearFloatB=canonicalDataValue("float","1.000000238418579",&ok); QVERIFY(ok); QVERIFY(nearFloatA!=nearFloatB);
        auto nearDoubleA=canonicalDataValue("double","1.234567890123456",&ok); QVERIFY(ok);
        auto nearDoubleB=canonicalDataValue("double","1.234567890123457",&ok); QVERIFY(ok); QVERIFY(nearDoubleA!=nearDoubleB);
        canonicalDataValue("double","nan",&ok); QVERIFY(!ok);
    }
    void exactJson() {
        bool ok;
        auto a=canonicalDataValue("json",R"({"b":[true,null,"中文"],"a":9007199254740993})",&ok); QVERIFY(ok);
        auto b=canonicalDataValue("json",R"({"a":9007199254740993.0,"b":[true,null,"\u4e2d\u6587"]})",&ok); QVERIFY(ok); QCOMPARE(a,b);
        b=canonicalDataValue("json",R"({"a":9007199254740992,"b":[true,null,"中文"]})",&ok); QVERIFY(ok); QVERIFY(a!=b);
        auto text=canonicalDataValue("json",R"({"a":"9007199254740993"})",&ok); QVERIFY(ok); QVERIFY(text!=a);
        canonicalDataValue("json",R"({"a":1,"a":2})",&ok); QVERIFY(!ok);
        canonicalDataValue("json","[01]",&ok); QVERIFY(!ok);
        canonicalDataValue("json","[1,]",&ok); QVERIFY(!ok);
        canonicalDataValue("json","1 trailing",&ok); QVERIFY(!ok);
    }
    void textBinaryTemporal() {
        bool ok;
        QCOMPARE(canonicalDataValue("varchar"," 中文 ",&ok),QString(" 中文 ")); QVERIFY(ok);
        QCOMPARE(canonicalDataValue("blob","00ff80",&ok),QString("00FF80"));
        QVERIFY(canonicalDataValue("datetime","2026-09-15 12:34:56.123456",&ok)!=canonicalDataValue("datetime","2026-09-15 12:34:56.123457",&ok));
    }
    void savedResultPagesAndChunks() {
        QTemporaryDir dir; const auto name=QString("test-results");
        {
            auto db=QSqlDatabase::addDatabase("QSQLITE",name); db.setDatabaseName(dir.path()+"/rows.sqlite"); QVERIFY(db.open()); QSqlQuery q(db);
            QVERIFY(q.exec("CREATE TABLE rows(id INTEGER PRIMARY KEY, identity TEXT, key_text TEXT, status TEXT, changed TEXT)"));
            QVERIFY(q.exec("CREATE TABLE cells(row_id INTEGER, side TEXT, field TEXT, type TEXT, raw TEXT, canonical TEXT, is_null INTEGER, encoding TEXT)"));
            QVERIFY(q.exec("INSERT INTO rows VALUES(1,'key','[\"9007199254740993\",\"9007199254740993\"]','different','[\"text\"]')"));
            QVERIFY(q.exec("INSERT INTO rows VALUES(2,'missing','[\"2\"]','pending-left','[]')"));
            for(auto side:{"left","right"}) {
                q.prepare("INSERT INTO cells VALUES(1,?,'text','text',?,'',0,'text')"); q.addBindValue(side); q.addBindValue(stored(QString::fromUtf8("中文😀").repeated(1000)+QChar::Null+(QString(side)=="left"?"L":"R"))); QVERIFY(q.exec());
                q.prepare("INSERT INTO cells VALUES(1,?,'nullable','varchar','','',?,'text')"); q.addBindValue(side); q.addBindValue(QString(side)=="left"?1:0); QVERIFY(q.exec());
            }
            q.prepare("INSERT INTO cells VALUES(2,'left','text','text',?,'',0,'text')"); q.addBindValue(stored("left")); QVERIFY(q.exec());
        }
        QSqlDatabase::removeDatabase(name);
        auto page=readDataResult(dir.path(),"data-page",{{"limit",1}},false); QVERIFY(page["ok"].toBool()); QCOMPARE(page["total"].toInt(),1); QCOMPARE(page["rows"].toArray()[0].toObject()["key"].toArray()[0].toString(),QString("9007199254740993"));
        auto repeatedKey=readDataResult(dir.path(),"data-page",{{"key",QJsonArray{"9007199254740993","9007199254740993"}}},false); QVERIFY(repeatedKey["ok"].toBool()); QCOMPARE(repeatedKey["total"].toInt(),1);
        auto details=readDataResult(dir.path(),"data-detail",{{"rowId","1"}},false); QVERIFY(details["ok"].toBool()); auto fields=details["fields"].toArray(); QVERIFY(fields[0].toObject()["left"].toObject()["truncated"].toBool()); QVERIFY(fields[1].toObject()["left"].toObject()["isNull"].toBool()); QVERIFY(!fields[1].toObject()["right"].toObject()["isNull"].toBool());
        QString raw; int offset=0; while(true) { auto chunk=readDataResult(dir.path(),"data-detail",{{"rowId","1"},{"field","text"},{"side","left"},{"offset",offset},{"limit",257}},true); QVERIFY(chunk["ok"].toBool()); raw+=chunk["value"].toObject()["text"].toString(); if(!chunk["value"].toObject()["truncated"].toBool()) { QVERIFY(chunk["nextOffset"].isNull()); break; } offset=chunk["nextOffset"].toInt(); }
        QCOMPARE(raw,QString::fromUtf8("中文😀").repeated(1000)+QChar::Null+"L");
        auto missing=readDataResult(dir.path(),"data-detail",{{"rowId","2"}},true); QVERIFY(missing["fields"].toArray()[0].toObject()["right"].isNull());
        auto search=readDataResult(dir.path(),"data-page",{{"key",QJsonArray{"absent"}}},true); QCOMPARE(search["total"].toInt(),0); QVERIFY(search["complete"].toBool());
        QVERIFY(!readDataResult(dir.path(),"data-page",{{"limit",201}},true)["ok"].toBool());
        QVERIFY(!readDataResult(dir.path(),"data-page",{{"status","';DROP TABLE rows"}},true)["ok"].toBool());
        QVERIFY(!readDataResult(dir.path(),"data-detail",{{"rowId","1"},{"field","text"},{"side","left"},{"offset",999999}},true)["ok"].toBool());
    }
    void staleLifecycle() {
        QTemporaryDir dir; QString temp;
        { Foundation service(dir.path()); auto folders=QDir(dir.path()).entryList({"data-tmp-*"},QDir::Dirs|QDir::NoDotAndDotDot); QCOMPARE(folders.size(),1); temp=dir.path()+"/"+folders[0];
          QVERIFY(service.execute("data-invalidate",{})["ok"].toBool()); QCOMPARE(service.execute("data-status",{{"taskId","old"}})["code"].toString(),QString("stale"));
          QVERIFY(!service.execute("data-page",{})["ok"].toBool()); }
        QVERIFY(!QDir(temp).exists());
    }
};
QTEST_GUILESS_MAIN(DataTest)
#include "data_test.moc"
