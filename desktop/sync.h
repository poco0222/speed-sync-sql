#pragma once
#include <QJsonObject>
QJsonObject buildSyncPlan(const QJsonObject &comparison, const QJsonObject &args);
QJsonObject executeSyncStep(const QJsonObject &connection, const QJsonObject &target, const QJsonObject &step);
