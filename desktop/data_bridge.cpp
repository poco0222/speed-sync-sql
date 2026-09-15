#include "foundation.h"
#include "data.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QLockFile>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>

namespace {
QJsonObject fail(const QString &error,const QString &code="data") { return {{"ok",false},{"error",error},{"code",code}}; }
}
void Foundation::initializeData() {
    if(!QDir().mkpath(directory)) return;
    QDir parent(directory);
    for(const auto &name:parent.entryList({"data-tmp-*"},QDir::Dirs|QDir::NoDotAndDotDot)) {
        const auto path=parent.filePath(name); QLockFile old(path+"/owner.lock"); old.setStaleLockTime(0);
        if(old.tryLock(0)) QDir(path).removeRecursively();
    }
    dataTemp=std::make_unique<QTemporaryDir>(directory+"/data-tmp-XXXXXX");
    if(dataTemp->isValid()) { dataLock=std::make_unique<QLockFile>(dataTemp->path()+"/owner.lock"); dataLock->setStaleLockTime(0); if(!dataLock->tryLock(0)) { dataLock.reset(); dataTemp.reset(); } }
}
Foundation::~Foundation() {
    for(auto it=jobs.cbegin();it!=jobs.cend();++it) if(it.key().startsWith("data:")) { it.value()->disconnect(this); it.value()->kill(); it.value()->waitForFinished(5000); }
    dataLock.reset(); dataTemp.reset();
}
void Foundation::invalidateData() {
    ++dataGeneration; dataState={};
    const auto oldPath=dataPath; dataPath.clear(); bool running=false;
    for(auto it=jobs.cbegin();it!=jobs.cend();++it) if(it.key().startsWith("data:")) { it.value()->setProperty("cancelled",true); if(it.value()->property("dataPath").toString()==oldPath) running=true; it.value()->kill(); }
    if(!running && !oldPath.isEmpty()) QDir(oldPath).removeRecursively();
}
QJsonObject Foundation::dataOperation(const QString &operation,const QJsonObject &args) {
    if(operation=="data-invalidate") {
        if(args.contains("taskId") && args["taskId"]!=dataState["id"]) return fail("数据任务已失效","stale"); invalidateData(); return {{"ok",true}};
    }
    if(operation=="data-cancel") {
        if(args.contains("taskId") && args["taskId"]!=dataState["id"]) return fail("数据任务已失效","stale");
        for(auto it=jobs.cbegin();it!=jobs.cend();++it) if(it.key().startsWith("data:")) { it.value()->setProperty("cancelled",true); it.value()->kill(); }
        if(dataState["state"]=="running") { dataState["state"]="cancelled"; dataState["phase"]="cancelled"; dataState["complete"]=false; dataState["error"]="读取已取消，未完成整个范围"; }
        return {{"ok",true}};
    }
    if(dataState.isEmpty() || !args["taskId"].isString() || args["taskId"]!=dataState["id"]) return fail("数据任务已失效，请重新比对","stale");
    if(dataState["state"]=="running") {
        QFile f(dataPath+"/status.json"); if(f.open(QIODevice::ReadOnly)) { const auto status=QJsonDocument::fromJson(f.readAll()).object(); if(status["id"]==dataState["id"]) dataState=status; }
    }
    if(operation=="data-status") return {{"ok",true},{"task",dataState}};
    if(operation=="data-page" || operation=="data-detail") return readDataResult(dataPath,operation,args,dataState["complete"].toBool());
    return fail("未知数据操作","validation");
}
void Foundation::dataTask(const QString &id,const QString &operation,const QJsonObject &args) {
    if(!loadError.isEmpty()) { reply(id,fail(loadError,"storage")); return; }
    if(syncExecuting || jobs.contains("sync")) { reply(id,fail("结构同步进行中，请等待结束","busy")); return; }
    if(!dataTemp || !dataTemp->isValid()) { reply(id,fail("无法创建私有临时结果目录","storage")); return; }
    QJsonObject payload{{"operation",operation},{"args",args}}; QString error;
    if(!syncConnections(payload,error)) { reply(id,fail(error,"credentials")); return; }
    for(const auto &side:{"left","right"}) for(const auto &field:{"database","table"}) {
        const auto value=args[side].toObject()[field];
        if(!value.isString() || value.toString().isEmpty() || value.toString().size()>64 || value.toString().contains(QChar::Null)) { reply(id,fail("请选择有效库表","validation")); return; }
    }
    if(operation=="data-start" && (!args["key"].isArray() || !args["fields"].isArray() || !args["filters"].isArray())) { reply(id,fail("数据比对设置格式无效","validation")); return; }
    invalidateData(); const auto generation=dataGeneration; const auto taskId=QUuid::createUuid().toString(QUuid::WithoutBraces); const auto lane="data:"+taskId;
    const bool scanning=operation=="data-start"; const auto path=scanning?dataTemp->path()+"/"+taskId:QString{};
    if(scanning) {
        if(!QDir().mkdir(path)) { reply(id,fail("无法创建临时任务目录","storage")); return; }
        dataPath=path; dataState={{"id",taskId},{"state","running"},{"phase","preparing"},{"leftScanned",0},{"rightScanned",0},{"batches",0},{"counts",QJsonObject{{"same",0},{"different",0},{"leftOnly",0},{"rightOnly",0}}},{"complete",false},{"mode",args["mode"]=="browse"?"browse":"compare"}};
        payload["path"]=path; payload["taskId"]=taskId;
    }
    auto process=new QProcess(this); jobs[lane]=process; process->setProperty("dataPath",path);
    auto timer=new QTimer(process); timer->setSingleShot(true);
    auto output=std::make_shared<QByteArray>(); const auto input=QJsonDocument(payload).toJson(QJsonDocument::Compact);
    connect(process,&QProcess::started,this,[process,input]{process->write(input);process->closeWriteChannel();});
    connect(process,&QProcess::readyReadStandardOutput,this,[process,output] { output->append(process->readAllStandardOutput()); if(output->size()>2*1024*1024) { process->setProperty("overflow",true); process->kill(); } });
    connect(timer,&QTimer::timeout,process,[process]{process->setProperty("timedOut",true);process->kill();});
    auto finish=[this,process,timer,output,generation,lane,path,id,scanning](bool normal) {
        if(process->property("finishedHandled").toBool()) return; process->setProperty("finishedHandled",true); timer->stop(); jobs.remove(lane);
        output->append(process->readAllStandardOutput()); auto result=QJsonDocument::fromJson(*output).object();
        if(process->property("cancelled").toBool()) result=fail("读取已取消，未完成整个范围","cancelled");
        else if(process->property("timedOut").toBool()) result=fail("元数据读取超时","timeout");
        else if(!normal || process->property("overflow").toBool() || !result.contains("ok")) result=fail("数据读取进程异常结束","process");
        const bool stale=generation!=dataGeneration;
        if(stale) { result=fail("数据设置已变化，请重新读取","stale"); if(!path.isEmpty()) QDir(path).removeRecursively(); }
        else if(scanning) {
            QFile f(path+"/status.json"); if(f.open(QIODevice::ReadOnly)) { auto status=QJsonDocument::fromJson(f.readAll()).object(); if(status["id"]==dataState["id"]) dataState=status; }
            if(result["ok"].toBool()) dataState=result["task"].toObject();
            else { dataState["state"]=result["code"]=="cancelled"?"cancelled":"failed"; dataState["phase"]=dataState["state"]; dataState["complete"]=false; dataState["error"]=result["error"]; dataState["code"]=result["code"]; }
        }
        if(!scanning) reply(id,result); process->deleteLater(); emit activityChanged();
    };
    connect(process,&QProcess::errorOccurred,this,[finish](QProcess::ProcessError e){if(e==QProcess::FailedToStart) finish(false);});
    connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[finish](int code,QProcess::ExitStatus status){finish(code==0 && status==QProcess::NormalExit);});
    process->setProgram(QCoreApplication::applicationFilePath()); process->setArguments({"--data"}); process->start(); if(!scanning) timer->start(65000); emit activityChanged();
    if(scanning) reply(id,{{"ok",true},{"taskId",taskId}});
}
