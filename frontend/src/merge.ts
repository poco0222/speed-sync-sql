import type {TestResult} from './bridge';
import type {DataFilter,DataTask} from './data';
import type {Direction,SyncEndpoint,SyncRecord} from './sync';
export type MergeMode='fill'|'merge';
export type MergePlan={id:string;taskId:string;direction:Direction;mode:MergeMode;left:SyncEndpoint;right:SyncEndpoint;key:string[];fields:string[];filters:DataFilter[];counts:{add:number;modify:number;delete:0};total:number;batchSize:number;readTimes:Partial<Pick<DataTask,'leftStartedAt'|'leftFinishedAt'|'rightStartedAt'|'rightFinishedAt'|'consistency'>>;warnings:string[];sql:string[]};
export type MergeRow={id:string;key:string[];action:'insert'|'update';fields:string[];sql:string;sample:object};
export type MergeBatch={index:number;count:number;status:'passed'|'failed'|'unknown'|'pending'|'running';error?:string};
export type MergeRecord=SyncRecord & {mode:'data-fill'|'data-merge';committed:number;total:number;batches:MergeBatch[];stage?:string};
export type MergeResult=TestResult & {plan?:MergePlan;rows?:MergeRow[];total?:number;running?:boolean;record?:MergeRecord;id?:string;blockers?:string[]};
export const mergeModeLabels={fill:'补齐缺失记录',merge:'合并更新'};
export function canPlanMerge(task:DataTask|undefined,browse:boolean):boolean{return !!task?.complete&&task.state==='complete'&&!browse;}
export function isMergeRecord(value:unknown):value is MergeRecord {
 if(!value||typeof value!=='object')return false;
 const r=value as Partial<MergeRecord>;
 return !!r.id&&!!r.startedAt&&!!r.left&&!!r.right&&['data-fill','data-merge'].includes(r.mode??'')&&Array.isArray(r.batches)&&['running','passed','failed','stopped','unknown','blocked'].includes(r.status??'');
}
export function mergeSummary(plan:Pick<MergePlan,'counts'>):string{return `新增 ${plan.counts.add} · 更新 ${plan.counts.modify} · 删除 0`;}
