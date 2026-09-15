#pragma once
#include <QJsonObject>

QJsonObject readSchema(const QJsonObject &connection, const QJsonObject &args);
QJsonObject compareSchemas(const QJsonObject &left, const QJsonObject &right);

namespace SchemaDetails {
QString definitionFingerprint(const QString &ddl, const QString &sqlMode = {});
bool hasUnsupportedSyntax(const QString &ddl, const QString &sqlMode = {});
}
