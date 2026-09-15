#pragma once
#include <QJsonObject>
#include <QJsonArray>
#include <QHash>
#include <QObject>
#include <QProcess>
#include <functional>
#include <memory>
class QTemporaryDir;
class QLockFile;

QJsonObject probeDatabase(const QJsonObject &connection);
QString validateConnection(const QJsonObject &connection);
QJsonObject safeConnection(const QJsonObject &connection);
QJsonObject classifyDatabaseError(int code);

class Foundation : public QObject {
    Q_OBJECT
public:
    explicit Foundation(QString directory, QObject *parent = nullptr);
    ~Foundation() override;
    bool busy() const { return mergeExecuting || !jobs.isEmpty(); }
    void stopJobs();
    QJsonObject snapshot() const;
    QJsonObject execute(const QString &operation, const QJsonObject &args);
    QString credentialKey(const QString &id) const;
    Q_INVOKABLE void request(const QString &json);
signals:
    void response(const QString &json);
    void activityChanged();
private:
    std::unique_ptr<QTemporaryDir> dataTemp;
    std::unique_ptr<QLockFile> dataLock;
    QString dataPath;
    QJsonObject dataState;
    quint64 dataGeneration = 0;
    void initializeData();
    void invalidateData();
    void dataTask(const QString &id, const QString &operation, const QJsonObject &args);
    QJsonObject dataOperation(const QString &operation, const QJsonObject &args);
    QJsonObject mergePlan, mergeRecord, mergePayload;
    bool mergeExecuting = false, mergeStop = false;
    int mergeOffset = 0;
    quint64 mergeGeneration = 0;
    void planMerge(const QString &id, const QJsonObject &args);
    QJsonObject mergeOperation(const QString &operation, const QJsonObject &args);
    void runMergeProcess(const QJsonObject &payload, std::function<void(QJsonObject)> done);
    void advanceMerge();
    void finishMerge(const QString &status, const QString &error = {});
    bool saveMergeRecord();
    QString directory, loadError;
    QJsonObject state;
    QHash<QString, QString> passwords;
    QHash<QString, QProcess *> jobs;
    QHash<QString, int> revisions;
    quint64 schemaGeneration = 0, planGeneration = 0;
    QJsonObject comparison;
    QJsonObject syncPlan, syncRecord, syncPayload;
    QJsonArray records;
    bool syncExecuting = false, syncStop = false;
    int syncStep = 0;
    QJsonObject syncOperation(const QString &operation, const QJsonObject &args);
    void planSync(const QString &id, const QJsonObject &args);
    void runSyncProcess(const QJsonObject &payload, std::function<void(QJsonObject)> done, int timeout = 120000);
    bool syncConnections(QJsonObject &payload, QString &error) const;
    bool saveSyncRecord();
    void recoverSyncRecords();
    void advanceSync();
    void finishSync(const QString &status, const QString &error = {});
    void verifySync();
    QString workspaceKey() const;
    void invalidateSchema();
    void schemaTask(const QString &id, const QString &operation, const QJsonObject &args);
    bool persist(const QJsonObject &next, QString &error);
    QJsonObject find(const QString &id) const;
    void reply(const QString &id, const QJsonObject &result);
    void test(const QString &id, QJsonObject args);
};
