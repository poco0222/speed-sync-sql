#pragma once
#include <QJsonObject>
#include <QJsonArray>
#include <QHash>
#include <QObject>
#include <QProcess>

QJsonObject probeDatabase(const QJsonObject &connection);
QString validateConnection(const QJsonObject &connection);
QJsonObject safeConnection(const QJsonObject &connection);
QJsonObject classifyDatabaseError(int code);

class Foundation : public QObject {
    Q_OBJECT
public:
    explicit Foundation(QString directory, QObject *parent = nullptr);
    bool busy() const { return !jobs.isEmpty(); }
    void stopJobs();
    QJsonObject snapshot() const;
    QJsonObject execute(const QString &operation, const QJsonObject &args);
    QString credentialKey(const QString &id) const;
    Q_INVOKABLE void request(const QString &json);
signals:
    void response(const QString &json);
    void activityChanged();
private:
    QString directory, loadError;
    QJsonObject state;
    QHash<QString, QString> passwords;
    QHash<QString, QProcess *> jobs;
    QHash<QString, int> revisions;
    bool persist(const QJsonObject &next, QString &error);
    QJsonObject find(const QString &id) const;
    void reply(const QString &id, const QJsonObject &result);
    void test(const QString &id, QJsonObject args);
};
