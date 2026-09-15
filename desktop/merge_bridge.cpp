#include "foundation.h"
#include "data.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTimer>
#include <QUuid>

namespace {
QJsonObject fail(const QString &error,const QString &code="merge") { return {{"ok",false},{"error",error},{"code",code}}; }
QString stamp() { return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs); }
QJsonObject identity(const QJsonObject &connection,QJsonObject endpoint) {
    endpoint["connectionId"]=connection["id"]; endpoint["name"]=connection["name"];
    endpoint["connectionDigest"]=QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(safeConnection(connection)).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex()); return endpoint;
}
}
void Foundation::runMergeProcess(const QJsonObject &payload,std::function<void(QJsonObject)> done) {
    auto process=new QProcess(this); jobs["merge"]=process;
    auto timer=new QTimer(process); timer->setSingleShot(true); auto output=std::make_shared<QByteArray>();
    connect(process,&QProcess::started,this,[process,payload]{process->write(QJsonDocument(payload).toJson(QJsonDocument::Compact));process->closeWriteChannel();});
    connect(process,&QProcess::readyReadStandardError,this,[process]{process->readAllStandardError();});
    connect(process,&QProcess::readyReadStandardOutput,this,[process,output]{output->append(process->readAllStandardOutput());if(output->size()>2*1024*1024){process->setProperty("overflow",true);process->kill();}});
    connect(timer,&QTimer::timeout,this,[process]{process->setProperty("timedOut",true);process->kill();});
    auto finish=[this,process,timer,output,done](bool normal){
        if(process->property("handled").toBool())return;process->setProperty("handled",true);timer->stop();jobs.remove("merge");
        output->append(process->readAllStandardOutput());auto result=QJsonDocument::fromJson(*output).object();
        if(!normal||process->property("overflow").toBool()||process->property("timedOut").toBool()||!result.contains("ok")) result={{"ok",false},{"status","unknown"},{"error","数据进程异常或超时，当前批次结果待核实；不会自动重试"},{"code","process"}};
        process->deleteLater();done(result);emit activityChanged();
    };
    connect(process,&QProcess::errorOccurred,this,[finish](QProcess::ProcessError error){if(error==QProcess::FailedToStart)finish(false);});
    connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[finish](int code,QProcess::ExitStatus status){finish(code==0&&status==QProcess::NormalExit);});
    process->setProgram(QCoreApplication::applicationFilePath());process->setArguments({"--data"});process->start();timer->start(payload["operation"]=="merge-plan"?70000:120000);emit activityChanged();
}
void Foundation::planMerge(const QString &id,const QJsonObject &args) {
    if(!loadError.isEmpty()){reply(id,fail(loadError,"storage"));return;}
    if(syncExecuting||mergeExecuting||!jobs.isEmpty()){reply(id,fail("请等待当前任务结束","busy"));return;}
    if(!dataState["complete"].toBool()||dataState["state"]!="complete"||dataState["mode"]!="compare"||args["taskId"]!=dataState["id"]){reply(id,fail("需要当前完整且有可靠键的数据比对","stale"));return;}
    mergePlan={};const auto generation=++mergeGeneration;QJsonObject payload{{"operation","merge-plan"},{"path",dataPath},{"args",args}};QString error;
    if(!syncConnections(payload,error)){reply(id,fail(error,"credentials"));return;}
    runMergeProcess(payload,[this,id,generation](QJsonObject result){
        if(generation!=mergeGeneration){reply(id,fail("数据计划上下文已失效","stale"));return;}
        if(result["ok"].toBool()){
            mergePlan=result["plan"].toObject();
            for(auto side:{"left","right"})mergePlan[side]=identity(find(state[side].toString()),mergePlan[side].toObject());
            result["plan"]=mergePlan;
        }
        reply(id,result);
    });
}
bool Foundation::saveMergeRecord() {
    bool found=false;for(qsizetype i=0;i<records.size();++i)if(records[i].toObject()["id"]==mergeRecord["id"]){records[i]=mergeRecord;found=true;break;}
    if(!found)records.append(mergeRecord);
    if(!QDir().mkpath(directory+"/sync-records"))return false;
    QSaveFile file(directory+"/sync-records/"+mergeRecord["id"].toString()+".json");const auto bytes=QJsonDocument(mergeRecord).toJson();
    return file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size()&&file.commit();
}
QJsonObject Foundation::mergeOperation(const QString &operation,const QJsonObject &args) {
    if(operation=="merge-status")return {{"ok",true},{"running",mergeExecuting},{"record",mergeRecord}};
    if(operation=="merge-stop"){mergeStop=true;return {{"ok",true}};}
    if(operation=="merge-invalidate"){
        if(mergeExecuting)return fail("数据执行中不能更改计划","busy");
        mergePlan={};++mergeGeneration;return {{"ok",true}};
    }
    if(operation=="merge-page"){
        if(mergePlan.isEmpty()||args["planId"]!=mergePlan["id"])return fail("数据计划已过期","stale");
        return readMergePage(dataPath,args);
    }
    if(operation!="merge-execute")return fail("未知数据写入操作","validation");
    if(syncExecuting||mergeExecuting||!jobs.isEmpty())return fail("请等待当前任务结束","busy");
    if(args["confirmed"]!=true)return fail("请先确认本次数据写入摘要","validation");
    if(mergePlan.isEmpty()||args["planId"]!=mergePlan["id"]||mergePlan["taskId"]!=dataState["id"]||!dataState["complete"].toBool())return fail("数据计划已过期，请重新预览","stale");
    if(mergePlan["total"].toInt()<=0)return fail("当前计划没有可执行操作","validation");
    for(auto side:{"left","right"}){
        auto endpoint=mergePlan[side].toObject();endpoint.remove("connectionId");endpoint.remove("connectionDigest");endpoint.remove("name");
        if(identity(find(state[side].toString()),endpoint)!=mergePlan[side].toObject())return fail("连接已改变，请重新比对","stale");
    }
    mergePayload={{"operation","merge-batch"},{"path",dataPath}};QString error;
    if(!syncConnections(mergePayload,error)){mergePayload={};return fail(error,"credentials");}
    mergePayload["args"]=QJsonObject{{"planId",mergePlan["id"]},{"offset",0},{"limit",256}};
    mergeRecord={{"id",QUuid::createUuid().toString(QUuid::WithoutBraces)},{"formatVersion",1},{"mode",mergePlan["mode"]=="fill"?"data-fill":"data-merge"},{"direction",mergePlan["direction"]},{"left",mergePlan["left"]},{"right",mergePlan["right"]},{"counts",mergePlan["counts"]},{"total",mergePlan["total"]},{"committed",0},{"status","running"},{"stage","checking"},{"startedAt",stamp()},{"steps",QJsonArray{}},{"verification",QJsonObject{{"status","pending"}}}};
    QJsonArray batches;const auto total=mergePlan["total"].toInt();for(int offset=0;offset<total;offset+=256)batches.append(QJsonObject{{"index",offset/256+1},{"count",qMin(256,total-offset)},{"status","pending"}});mergeRecord["batches"]=batches;
    if(!saveMergeRecord()){
        mergeRecord["status"]="blocked";mergeRecord["stage"]="finished";mergeRecord["finishedAt"]=stamp();mergeRecord["error"]="无法保存初始执行记录，未写入数据库";
        mergeRecord["storageWarning"]="初始记录未保存到磁盘";saveMergeRecord();mergePayload={};return fail(mergeRecord["error"].toString(),"storage");
    }
    mergeExecuting=true;mergeStop=false;mergeOffset=0;mergePlan={};syncPlan={};++planGeneration;
    QTimer::singleShot(0,this,[this]{advanceMerge();});emit activityChanged();return {{"ok",true},{"id",mergeRecord["id"]}};
}
void Foundation::advanceMerge() {
    if(mergeStop){finishMerge("stopped","已在批次边界停止，已提交批次保留");return;}
    if(mergeOffset>=mergeRecord["total"].toInt()){finishMerge("passed");return;}
    auto batches=mergeRecord["batches"].toArray();auto batch=batches[mergeOffset/256].toObject();batch["status"]="running";batches[mergeOffset/256]=batch;mergeRecord["batches"]=batches;mergeRecord["stage"]="executing";
    if(!saveMergeRecord()){batch["status"]="pending";batches[mergeOffset/256]=batch;mergeRecord["batches"]=batches;finishMerge("blocked","批次开始前无法保存记录，未启动下一批");return;}
    auto args=mergePayload["args"].toObject();args["offset"]=mergeOffset;mergePayload["args"]=args;
    runMergeProcess(mergePayload,[this](QJsonObject result){
        auto batches=mergeRecord["batches"].toArray();auto batch=batches[mergeOffset/256].toObject();
        const bool passed=result["ok"].toBool()&&result["status"]=="passed"&&result["committed"].toInt()==batch["count"].toInt();
        const auto status=passed?QString("passed"):result["status"]=="failed"?QString("failed"):QString("unknown");batch["status"]=status;
        if(!passed)batch["error"]=result["error"].toString("当前批次结果待核实");batches[mergeOffset/256]=batch;mergeRecord["batches"]=batches;
        if(!passed){finishMerge(status,batch["error"].toString());return;}
        mergeOffset+=batch["count"].toInt();mergeRecord["committed"]=mergeOffset;
        if(!saveMergeRecord()){finishMerge("blocked","已提交批次，但最新记录保存失败；请核实后重新比对");return;}
        QTimer::singleShot(0,this,[this]{advanceMerge();});
    });
}
void Foundation::finishMerge(const QString &status,const QString &error) {
    mergeRecord["status"]=status;mergeRecord["stage"]="finished";mergeRecord["finishedAt"]=stamp();if(!error.isEmpty())mergeRecord["error"]=error;
    if(!saveMergeRecord())mergeRecord["storageWarning"]="最新结果无法保存，磁盘原记录保留；请重新比对核实";
    mergeExecuting=false;mergePayload={};emit activityChanged();
}
