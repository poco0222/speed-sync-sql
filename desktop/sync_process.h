#pragma once
#include <QJsonObject>
QJsonObject runSyncCommand(const QJsonObject &input);
bool sameSyncSnapshot(QJsonObject expected, QJsonObject actual, bool guarded);
