#pragma once
#include <QString>
namespace Credentials {
QString read(const QString &key, QString &error, bool *missing = nullptr);
bool write(const QString &key, const QString &value, QString &error);
bool remove(const QString &key, QString &error);
}
