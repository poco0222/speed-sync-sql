import type {TestResult} from './bridge';
import type {DataFilter,DataTask} from './data';
import type {Direction,SyncEndpoint,SyncRecord} from './sync';
export type MergeMode='fill'|'merge'|'align';
export type MergePlan={id:string;taskId:string;direction:Direction;mode:MergeMode;left:SyncEndpoint;right:SyncEndpoint;key:string[];fields:string[];filters:DataFilter[];counts:{add:number;modify:number;delete:number};total:number;batchSize:number;readTimes:Partial<Pick<DataTask,'leftStartedAt'|'leftFinishedAt'|'rightStartedAt'|'rightFinishedAt'|'consistency'>>;warnings:string[];sql:string[]};
export type MergeRow={id:string;key:string[];action:'insert'|'update'|'delete';fields:string[];sql:string;sample:object};
export type MergeBatch={index:number;count:number;status:'passed'|'failed'|'unknown'|'pending'|'running';error?:string};
export type MergeRecord=SyncRecord & {mode:'data-fill'|'data-merge'|'data-align';committed:number;total:number;batches:MergeBatch[];stage?:string};
export type MergeResult=TestResult & {plan?:MergePlan;rows?:MergeRow[];total?:number;running?:boolean;record?:MergeRecord;id?:string;blockers?:string[]};
export const mergeModeLabels={fill:'补齐缺失记录',merge:'合并更新',align:'完全对齐'};
export function canPlanMerge(task:DataTask|undefined,browse:boolean):boolean{return !!task?.complete&&task.state==='complete'&&!browse;}
export function isMergeRecord(value:unknown):value is MergeRecord {
 if(!value||typeof value!=='object')return false;
 const r=value as Partial<MergeRecord>;
 return !!r.id&&!!r.startedAt&&!!r.left&&!!r.right&&['data-fill','data-merge','data-align'].includes(r.mode??'')&&Array.isArray(r.batches)&&['running','passed','failed','stopped','unknown','blocked'].includes(r.status??'');
}
export function mergeSummary(plan:Pick<MergePlan,'counts'>):string{return `新增 ${plan.counts.add} · 更新 ${plan.counts.modify} · 删除 ${plan.counts.delete}`;}
export const mergeRecordLabels={'data-fill':'数据补齐','data-merge':'数据合并','data-align':'数据完全对齐'};

export function mergeEffect(status:string,mode:MergeMode):string {
 if(status==='left-only')return '新增来源记录';
 if(status==='different')return mode==='fill'?'保持目标原值':'更新为来源值';
 if(status==='right-only')return mode==='align'?'删除整行':'保留目标记录';
 return status==='same'?'保持相同记录':'未匹配，不推断';
}
export function mergeCounts(task:DataTask,mode:MergeMode){
 const c=task.counts;return {add:c.leftOnly,modify:mode==='fill'?0:c.different,delete:mode==='align'?c.rightOnly:0,keep:c.same+(mode==='fill'?c.different:0)+(mode==='align'?0:c.rightOnly)};
}
