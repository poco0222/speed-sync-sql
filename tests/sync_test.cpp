#include "sync.h"
#include "schema.h"
#include <QTest>
#include <QJsonArray>

class SyncTest:public QObject {
    Q_OBJECT
    static QJsonObject item(QString n,QJsonObject p) {return {{"name",n},{"properties",p}};}
    static QJsonObject snapshot(QString table="source",QString uuid="one") {
        QJsonObject cats;for(auto c:{"columns","indexes","constraints","triggers","table"})cats[c]=QJsonObject{{"state","complete"},{"items",QJsonArray{}}};
        cats["columns"]=QJsonObject{{"state","complete"},{"items",QJsonArray{item("id",{{"type","int"},{"ordinal","1"}}),item("title",{{"type","varchar(40)"},{"ordinal","2"},{"charset","utf8mb4"},{"collation","utf8mb4_0900_ai_ci"}})}}};
        cats["table"]=QJsonObject{{"state","complete"},{"items",QJsonArray{item("table",{{"engine","InnoDB"},{"charset","utf8mb4"},{"collation","utf8mb4_0900_ai_ci"},{"rowFormat","Dynamic"},{"comment",""}})}}};
        return {{"ok",true},{"existence","present"},{"database","db"},{"table",table},{"serverUuid",uuid},{"lowerCaseTableNames","0"},{"sqlMode",""},{"databaseCollation","utf8mb4_0900_ai_ci"},{"syncGuard",QJsonObject{{"complete",true},{"incoming",QJsonArray{}},{"triggerNames",QJsonArray{}}}},{"ddl","CREATE TABLE `source` (`id` int NOT NULL, `title` varchar(40) DEFAULT ('a,b)') COMMENT 'keep `source`') ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci"},{"categories",cats}};
    }
    static QJsonObject args(QString cat="columns",QString name="title") {return {{"direction","left-to-right"},{"selected",QJsonArray{QJsonObject{{"category",cat},{"name",name}}}}};}
private slots:
    void quotedDefinitions() {
        auto result=buildSyncPlan(compareSchemas(snapshot(),snapshot("tar`get","two")),args());QVERIFY2(result["ok"].toBool(),qPrintable(result["error"].toString()));
        auto plan=result["plan"].toObject();QCOMPARE(plan["counts"].toObject()["modify"].toInt(),1);QCOMPARE(plan["operations"].toArray().size(),1);QVERIFY(plan["steps"].toArray()[0].toObject()["summary"].toString().contains("修改字段 title"));
        auto sql=plan["sql"].toString();QVERIFY(sql.contains("ALTER TABLE `db`.`tar``get`"));QVERIFY(sql.contains("DEFAULT ('a,b)') COMMENT 'keep `source`'"));QVERIFY(sql.contains("CHARACTER SET `utf8mb4` COLLATE `utf8mb4_0900_ai_ci`"));
    }
    void guards() {
        QVERIFY(!buildSyncPlan(compareSchemas(snapshot(),snapshot()),args())["ok"].toBool());
        auto target=snapshot("target","two");target["syncGuard"]=QJsonObject{{"complete",false}};QVERIFY(!buildSyncPlan(compareSchemas(snapshot(),target),args())["ok"].toBool());
        target=snapshot("target","two");target["syncGuard"]=QJsonObject{{"complete",true},{"incoming",QJsonArray{QJsonObject{{"name","fk"}}}}};QVERIFY(!buildSyncPlan(compareSchemas(snapshot(),target),args())["ok"].toBool());
    }
    void creationAndDirection() {
        auto target=snapshot("target","two");target["existence"]="missing";target.remove("ddl");target.remove("categories");auto a=args();a["alignAll"]=true;
        auto result=buildSyncPlan(compareSchemas(snapshot(),target),a);QVERIFY(result["ok"].toBool());QVERIFY(result["plan"].toObject()["sql"].toString().contains("CREATE TABLE `db`.`target`"));
        a["direction"]="right-to-left";QVERIFY(!buildSyncPlan(compareSchemas(snapshot(),target),a)["ok"].toBool());
    }
    void completeMetadataAndDependencies() {
        auto source=snapshot(),target=snapshot("target","two");
        auto sc=source["categories"].toObject(),tc=target["categories"].toObject();
        auto index=item("title_idx",{{"parts",QJsonArray{QJsonObject{{"columnName","title"},{"method","BTREE"}}}}});
        auto category=QJsonObject{{"state","complete"},{"items",QJsonArray{index}}};sc["indexes"]=category;tc["indexes"]=category;
        auto columns=tc["columns"].toObject();auto ci=columns["items"].toArray();auto c=ci[1].toObject(),p=c["properties"].toObject();p["type"]="varchar(20)";c["properties"]=p;ci[1]=c;columns["items"]=ci;tc["columns"]=columns;
        source["categories"]=sc;target["categories"]=tc;
        source["ddl"]=source["ddl"].toString().replace(") ENGINE",", KEY `title_idx` (`title`)) ENGINE");target["ddl"]=source["ddl"].toString().replace("varchar(40)","varchar(20)");
        QVERIFY(!buildSyncPlan(compareSchemas(source,target),args())["ok"].toBool());
        auto a=args();a["alignAll"]=true;auto r=buildSyncPlan(compareSchemas(source,target),a);QVERIFY2(r["ok"].toBool(),qPrintable(r["error"].toString()));QVERIFY(r["plan"].toObject()["sql"].toString().contains("DROP INDEX `title_idx`"));
        tc["indexes"]=QJsonObject{{"state","failed"},{"items",QJsonArray{}}};target["categories"]=tc;
        QVERIFY(!buildSyncPlan(compareSchemas(source,target),args("indexes","title_idx"))["ok"].toBool());
    }
    void triggerContext() {
        auto source=snapshot(),target=snapshot("target","two");auto cats=source["categories"].toObject();
        cats["triggers"]=QJsonObject{{"state","complete"},{"items",QJsonArray{item("audit",{{"event","INSERT"},{"timing","BEFORE"},{"ordinal","1"},{"body","BEGIN SET NEW.id=1; SET @note='source; $$'; END"},{"definer","owner@localhost"},{"sqlMode","NO_BACKSLASH_ESCAPES"},{"characterSetClient","utf8mb4"},{"collationConnection","utf8mb4_0900_ai_ci"},{"databaseCollation","utf8mb4_0900_ai_ci"}})}}};source["categories"]=cats;
        auto result=buildSyncPlan(compareSchemas(source,target),args("triggers","audit"));QVERIFY2(result["ok"].toBool(),qPrintable(result["error"].toString()));
        auto plan=result["plan"].toObject();QVERIFY(plan["sql"].toString().contains("DELIMITER $$$"));auto step=plan["steps"].toArray()[0].toObject();QVERIFY(step["sql"].toString().contains("DEFINER=`owner`@`localhost`"));QVERIFY(step["sql"].toString().contains("ON `db`.`target`"));QVERIFY(step["sql"].toString().endsWith("BEGIN SET NEW.id=1; SET @note='source; $$'; END"));QCOMPARE(step["context"].toObject()["sqlMode"].toString(),"NO_BACKSLASH_ESCAPES");
        auto deleteTarget=source;deleteTarget["serverUuid"]="two";
        auto deletion=buildSyncPlan(compareSchemas(snapshot(),deleteTarget),args("triggers","audit"));
        QVERIFY2(deletion["ok"].toBool(),qPrintable(deletion["error"].toString()));auto deleteSteps=deletion["plan"].toObject()["steps"].toArray();QCOMPARE(deleteSteps.size(),1);QVERIFY(deleteSteps[0].toObject()["sql"].toString().startsWith("DROP TRIGGER "));
        target["databaseCollation"]="utf8mb4_bin";QVERIFY(!buildSyncPlan(compareSchemas(source,target),args("triggers","audit"))["ok"].toBool());
    }
};
QTEST_GUILESS_MAIN(SyncTest)
#include "sync_test.moc"
