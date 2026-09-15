#pragma once
#include <QJsonObject>
#include <QString>

// SQL values stay text or explicit hex across the bridge.
QString canonicalDataValue(const QString &type, const QString &text, bool *ok);
QJsonObject runDataCommand(const QJsonObject &input);
QJsonObject readDataResult(const QString &path, const QString &operation, const QJsonObject &args, bool complete);
