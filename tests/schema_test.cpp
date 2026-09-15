#include "schema.h"
#include <QJsonArray>
#include <QTest>

class SchemaTest : public QObject {
    Q_OBJECT
    QJsonObject snapshot() {
        QJsonObject categories;
        for (auto name : {"columns", "indexes", "constraints", "triggers", "table"})
            categories[name] = QJsonObject{{"state", "complete"}, {"items", QJsonArray{}}};
        categories["columns"] = QJsonObject{{"state", "complete"}, {"items", QJsonArray{QJsonObject{{"name", "id"}, {"properties", QJsonObject{{"type", "bigint"}, {"default", QJsonValue::Null}}}}}}};
        return {{"ok", true}, {"existence", "present"}, {"database", "a"}, {"table", "t"}, {"lowerCaseTableNames", "0"}, {"categories", categories}};
    }
    QJsonObject withObjects(const QString &categoryName, const QJsonArray &items) {
        auto result = snapshot(), cats = result["categories"].toObject();
        cats[categoryName] = QJsonObject{{"state", "complete"}, {"items", items}}; result["categories"] = cats; return result;
    }
private slots:
    void propertyMatrix_data() {
        QTest::addColumn<QString>("categoryName");
        QTest::addColumn<QJsonObject>("before"); QTest::addColumn<QJsonObject>("after");
        QTest::addColumn<QString>("changedKey");
        const QJsonObject column{{"ordinal", "1"}, {"type", "decimal(30,10)"}, {"nullable", "YES"}, {"defaultValue", "12345678901234567890.1234567890"}, {"defaultKind", "literal"}, {"charset", "utf8mb4"}, {"collation", "utf8mb4_bin"}, {"comment", "amount"}, {"autoIncrement", false}, {"generationExpression", ""}, {"visibility", "visible"}};
        const QJsonObject columnChanges{{"ordinal", "2"}, {"type", "decimal(32,12)"}, {"nullable", "NO"}, {"defaultValue", "12345678901234567890.1234567891"}, {"defaultKind", "expression"}, {"charset", "latin1"}, {"collation", "utf8mb4_0900_ai_ci"}, {"comment", "amount  updated"}, {"autoIncrement", true}, {"generationExpression", "(`price` * 2)"}, {"visibility", "invisible"}};
        for (auto it = columnChanges.begin(); it != columnChanges.end(); ++it) {
            auto changed = column; changed[it.key()] = it.value();
            QTest::newRow(qPrintable("column-" + it.key())) << QString("columns") << column << changed << it.key();
        }
        auto sqlNull = column; sqlNull["defaultValue"] = QJsonValue::Null;
        QTest::newRow("column-default-sql-null") << QString("columns") << column << sqlNull << QString("defaultValue");
        auto literalNull = sqlNull; literalNull["defaultValue"] = "NULL";
        QTest::newRow("column-null-vs-literal-NULL") << QString("columns") << sqlNull << literalNull << QString("defaultValue");
        auto generated = column; generated["generationExpression"] = "concat('a  b', `code`)";
        auto generatedChanged = generated; generatedChanged["generationExpression"] = "concat('a b', `code`)";
        QTest::newRow("column-generation-preserves-literal-space") << QString("columns") << generated << generatedChanged << QString("generationExpression");
        const QJsonObject part{{"ordinal", "1"}, {"columnName", "code"}, {"method", "BTREE"}, {"nonUnique", "1"}, {"prefixLength", "8"}, {"expression", QJsonValue::Null}, {"direction", "A"}, {"visible", "YES"}, {"comment", "lookup"}};
        auto second = part; second["ordinal"] = "2"; second["columnName"] = "tenant_id"; second["prefixLength"] = QJsonValue::Null;
        const QJsonArray parts{part, second}; const QJsonObject index{{"parts", parts}};
        const QJsonObject partChanges{{"method", "HASH"}, {"nonUnique", "0"}, {"prefixLength", "9"}, {"expression", "(lower(`code`))"}, {"direction", "D"}, {"visible", "NO"}, {"comment", "lookup  updated"}};
        for (auto it = partChanges.begin(); it != partChanges.end(); ++it) {
            auto changedPart = part; changedPart[it.key()] = it.value();
            auto changedParts = parts; changedParts[0] = changedPart;
            QTest::newRow(qPrintable("index-" + it.key())) << QString("indexes") << index << QJsonObject{{"parts", changedParts}} << QString("parts");
        }
        for (const auto &method : {QString("FULLTEXT"), QString("RTREE")}) {
            auto changedPart = part; changedPart["method"] = method;
            QTest::newRow(qPrintable("index-method-" + method)) << QString("indexes") << index << QJsonObject{{"parts", QJsonArray{changedPart, second}}} << QString("parts");
        }
        auto movedFirst = part, movedSecond = second; movedFirst["ordinal"] = "2"; movedSecond["ordinal"] = "1";
        QTest::newRow("index-column-order") << QString("indexes") << index << QJsonObject{{"parts", QJsonArray{movedSecond, movedFirst}}} << QString("parts");
    }
    void propertyMatrix() {
        QFETCH(QString, categoryName); QFETCH(QJsonObject, before); QFETCH(QJsonObject, after); QFETCH(QString, changedKey);
        const QJsonObject leftItem{{"name", "target"}, {"properties", before}}, rightItem{{"name", "target"}, {"properties", after}}, unchanged{{"name", "unchanged"}, {"properties", before}};
        const auto result = compareSchemas(withObjects(categoryName, {leftItem, unchanged}), withObjects(categoryName, {rightItem, unchanged}));
        QVERIFY(result["complete"].toBool()); QCOMPARE(result["status"].toString(), QString("different"));
        int found = 0;
        for (auto v : result["rows"].toArray()) {
            const auto row = v.toObject(); if (row["category"] != categoryName) continue;
            if (row["name"] == "target") {
                ++found; QCOMPARE(row["status"].toString(), QString("different")); QCOMPARE(row["changed"].toArray(), QJsonArray{changedKey});
                QCOMPARE(row["left"].toObject(), leftItem); QCOMPARE(row["right"].toObject(), rightItem);
            } else if (row["name"] == "unchanged") {
                ++found; QCOMPARE(row["status"].toString(), QString("same")); QVERIFY(row["changed"].toArray().isEmpty());
                QCOMPARE(row["left"].toObject(), unchanged); QCOMPARE(row["right"].toObject(), unchanged);
            }
        }
        QCOMPARE(found, 2);
    }
    void renamedColumnIsTwoObjects() {
        const QJsonObject oldColumn{{"name", "old_name"}, {"properties", QJsonObject{{"type", "int"}, {"ordinal", "1"}}}};
        auto newColumn = oldColumn; newColumn["name"] = "new_name";
        const auto result = compareSchemas(withObjects("columns", {oldColumn}), withObjects("columns", {newColumn}));
        QCOMPARE(result["status"].toString(), QString("different")); int found = 0;
        for (auto v : result["rows"].toArray()) {
            auto row = v.toObject(); if (row["category"] != "columns") continue;
            ++found; QCOMPARE(row["changed"].toArray(), (QJsonArray{"ordinal", "type"}));
            if (row["name"] == "old_name") { QCOMPARE(row["status"].toString(), QString("left-only")); QCOMPARE(row["left"].toObject(), oldColumn); QVERIFY(row["right"].isNull()); }
            else { QCOMPARE(row["name"].toString(), QString("new_name")); QCOMPARE(row["status"].toString(), QString("right-only")); QVERIFY(row["left"].isNull()); QCOMPARE(row["right"].toObject(), newColumn); }
        }
        QCOMPARE(found, 2);
    }
    void exactPropertiesAndDdl() {
        auto l = snapshot(), r = l; r["ddl"] = "different formatting and AUTO_INCREMENT=123";
        QCOMPARE(compareSchemas(l, r)["status"].toString(), QString("same"));
        auto categories = r["categories"].toObject(); auto col = categories["columns"].toObject();
        auto items = col["items"].toArray(); auto item = items[0].toObject();
        item["properties"] = QJsonObject{{"type", "bigint"}, {"default", "NULL"}}; items[0] = item; col["items"] = items; categories["columns"] = col; r["categories"] = categories;
        auto result = compareSchemas(l, r); QCOMPARE(result["status"].toString(), QString("different"));
        QCOMPARE(result["rows"].toArray()[0].toObject()["changed"].toArray(), QJsonArray{"default"});
    }
    void failuresNeverEqual() {
        for (auto state : {"failed", "unsupported", "unread"}) {
            auto l = snapshot(); auto cats = l["categories"].toObject(); cats["triggers"] = QJsonObject{{"state", state}, {"error", "hidden"}}; l["categories"] = cats;
            QCOMPARE(compareSchemas(l, l)["status"].toString(), QString("incomplete"));
            QVERIFY(!compareSchemas(l, l)["complete"].toBool());
        }
        QCOMPARE(compareSchemas({}, {})["status"].toString(), QString("incomplete"));
    }
    void missingAndUnread() {
        auto missing = snapshot(); missing["existence"] = "missing"; missing["categories"] = QJsonObject{};
        QCOMPARE(compareSchemas(snapshot(), missing)["status"].toString(), QString("different"));
        QCOMPARE(compareSchemas(missing, missing)["status"].toString(), QString("incomplete"));
        auto unknown = missing; unknown["existence"] = "unknown";
        QCOMPARE(compareSchemas(snapshot(), unknown)["status"].toString(), QString("incomplete"));
    }
    void identifierRulesAndExpressions() {
        auto l = snapshot(), r = l; r["lowerCaseTableNames"] = "1";
        QCOMPARE(compareSchemas(l, r)["status"].toString(), QString("incomplete"));
        r = l; auto cats = r["categories"].toObject(); cats.remove("indexes"); r["categories"] = cats;
        QCOMPARE(compareSchemas(l, r)["status"].toString(), QString("incomplete"));
    }
    void selfReferencesAndExternalReferences() {
        auto l = snapshot(), r = snapshot(); r["database"] = "b"; r["table"] = "other";
        const auto constraint = [](const QString &database, const QString &table) {
            return QJsonObject{{"state", "complete"}, {"items", QJsonArray{QJsonObject{{"name", "fk"}, {"properties", QJsonObject{{"parts", QJsonArray{QJsonObject{{"referencedDatabase", database}, {"referencedTable", table}, {"referencedColumn", "id"}}}}}}}}}};
        };
        auto lc = l["categories"].toObject(), rc = r["categories"].toObject();
        lc["constraints"] = constraint("a", "t"); rc["constraints"] = constraint("b", "other"); l["categories"] = lc; r["categories"] = rc;
        QCOMPARE(compareSchemas(l, r)["status"].toString(), QString("same"));
        rc["constraints"] = constraint("outside", "t"); r["categories"] = rc;
        QCOMPARE(compareSchemas(l, r)["status"].toString(), QString("different"));
    }
    void expressionsAndPartialRows() {
        auto l = snapshot(), r = l;
        auto lc = l["categories"].toObject(), rc = r["categories"].toObject();
        const auto triggers = [](QString body) {
            return QJsonObject{{"state", "complete"}, {"items", QJsonArray{QJsonObject{{"name", "tr"}, {"properties", QJsonObject{{"body", body}}}}}}};
        };
        lc["triggers"] = triggers("SET NEW.v='a  b'"); rc["triggers"] = triggers("SET NEW.v='a b'"); l["categories"] = lc; r["categories"] = rc;
        QCOMPARE(compareSchemas(l, r)["status"].toString(), QString("different"));
        rc["triggers"] = QJsonObject{{"state", "failed"}, {"items", QJsonArray{}}}; r["categories"] = rc;
        auto result = compareSchemas(l, r); QCOMPARE(result["status"].toString(), QString("incomplete"));
        bool unread = false;
        for (auto v : result["rows"].toArray()) if (v.toObject()["name"] == "tr") { QCOMPARE(v.toObject()["status"].toString(), QString("unread")); unread = true; }
        QVERIFY(unread);
    }
    void preservesReadFailureAndCaseAmbiguity() {
        auto failed = compareSchemas(QJsonObject{{"ok", false}, {"error", "permission fixture"}}, snapshot());
        QCOMPARE(failed["status"].toString(), QString("incomplete"));
        QVERIFY(failed["rows"].toArray()[0].toObject()["reason"].toString().contains("permission fixture"));
        auto l = snapshot(), r = l; auto cats = r["categories"].toObject(); auto cat = cats["columns"].toObject();
        auto items = cat["items"].toArray(); auto item = items[0].toObject(); item["name"] = "ID"; items[0] = item; cat["items"] = items; cats["columns"] = cat; r["categories"] = cats;
        QCOMPARE(compareSchemas(l, r)["status"].toString(), QString("incomplete"));
    }
    void syntaxAwareDefinitionChecks() {
        using namespace SchemaDetails;
        const QString base = "CREATE TABLE `STORAGE` (`SRID` int AUTO_INCREMENT, v varchar(40) DEFAULT 'PARTITION BY', c varchar(40) COMMENT 'AUTO_INCREMENT=9') ENGINE=InnoDB AUTO_INCREMENT=9 COMMENT='SRID STORAGE'";
        auto grown = base; grown.replace("ENGINE=InnoDB AUTO_INCREMENT=9", "ENGINE=InnoDB AUTO_INCREMENT=123456");
        QCOMPARE(definitionFingerprint(base), definitionFingerprint(grown));
        auto emptyTable = base; emptyTable.replace(" AUTO_INCREMENT=9 COMMENT=", " COMMENT=");
        QCOMPARE(definitionFingerprint(emptyTable), definitionFingerprint(grown));
        QCOMPARE(definitionFingerprint("CREATE TABLE t (id int AUTO_INCREMENT) ENGINE=InnoDB"), definitionFingerprint("CREATE TABLE t (id int AUTO_INCREMENT) ENGINE=InnoDB AUTO_INCREMENT=2"));
        QVERIFY(!hasUnsupportedSyntax(base));
        auto changedComment = base; changedComment.replace("COMMENT='SRID STORAGE'", "COMMENT='SRID STORAGE changed'");
        QVERIFY(definitionFingerprint(base) != definitionFingerprint(changedComment));
        auto changedLiteral = base; changedLiteral.replace("COMMENT 'AUTO_INCREMENT=9'", "COMMENT 'AUTO_INCREMENT=99'");
        QVERIFY(definitionFingerprint(base) != definitionFingerprint(changedLiteral));
        QVERIFY(!hasUnsupportedSyntax(base + " /* STORAGE SRID PARTITION BY */ -- STORAGE\n# SRID\n"));
        QVERIFY(hasUnsupportedSyntax(base + " /*!50100 PARTITION BY HASH (`SRID`) PARTITIONS 2 */"));
        QVERIFY(hasUnsupportedSyntax("CREATE TABLE t (`point` POINT /*!80003 SRID 4326 */)"));
        QVERIFY(hasUnsupportedSyntax("CREATE TABLE t (a int STORAGE DISK)"));
        QVERIFY(!hasUnsupportedSyntax("CREATE TABLE t (a varchar(30) DEFAULT 'it\\'s STORAGE')"));
        QVERIFY(hasUnsupportedSyntax("CREATE TABLE t (a varchar(30) DEFAULT 'tail\\') STORAGE DISK", "NO_BACKSLASH_ESCAPES"));
        QVERIFY(!hasUnsupportedSyntax("CREATE TABLE `a``STORAGE` (a int)"));
        QVERIFY(!hasUnsupportedSyntax("CREATE TABLE t (a varchar(30) DEFAULT 'it''s SRID')"));
        QVERIFY(definitionFingerprint("CREATE TABLE t (a int AUTO_INCREMENT) AUTO_INCREMENT=9") != definitionFingerprint("CREATE TABLE t (a int) AUTO_INCREMENT=9"));
    }
    void invalidInput() {
        const auto result = readSchema({}, {{"action", "snapshot"}, {"database", "db"}, {"table", "target"}});
        QVERIFY(!result["ok"].toBool()); QCOMPARE(result["database"].toString(), QString("db")); QCOMPARE(result["table"].toString(), QString("target"));
        QVERIFY(!result["startedAt"].toString().isEmpty()); QVERIFY(!result["finishedAt"].toString().isEmpty());
    }
};
QTEST_GUILESS_MAIN(SchemaTest)
#include "schema_test.moc"
