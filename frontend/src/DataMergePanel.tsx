import {useEffect,useRef,useState} from 'react';
import {Alert,Button,Modal,Select,Space,Table,Typography} from 'antd';
import {request} from './bridge';
import {syncLabels,type Direction} from './sync';
import {canPlanMerge,isMergeRecord,mergeModeLabels,mergeSummary,type MergeBatch,type MergeMode,type MergePlan,type MergeRecord,type MergeResult,type MergeRow} from './merge';
import type {DataTask} from './data';
const call=async(operation:string,args:object={}):Promise<MergeResult>=>{const r=await request(operation,args) as unknown as MergeResult;if(!r.ok||r.stale)throw new Error([r.error??'数据计划已失效，请重新预览',...(r.blockers??[])].join('\n'));return r;};
export function MergeBatches({batches}:{batches:MergeBatch[]}) {
 return <Table<MergeBatch> size="small" rowKey="index" dataSource={batches} pagination={{pageSize:20,showSizeChanger:false}} columns={[{title:'批次',dataIndex:'index'},{title:'操作数',dataIndex:'count'},{title:'结果',render:(_,b)=><>{({running:'执行中',passed:'已提交',failed:'已回滚',unknown:'待核实',pending:'未执行'})[b.status]}{b.error&&<Typography.Paragraph type="danger">{b.error}</Typography.Paragraph>}</>}]}/>;
}
export function DataMergePanel({task,taskId,browse,disabled,onRunning,onRecompare}:{task?:DataTask;taskId:string;browse:boolean;disabled:boolean;onRunning:(active:boolean)=>void;onRecompare:()=>void}) {
 const [direction,setDirection]=useState<Direction>('left-to-right'),[mode,setMode]=useState<MergeMode>('fill');
 const [plan,setPlan]=useState<MergePlan>(),[rows,setRows]=useState<MergeRow[]>([]),[page,setPage]=useState(1);
 const [record,setRecord]=useState<MergeRecord>(),[running,setRunning]=useState(false),[planning,setPlanning]=useState(false),[stopping,setStopping]=useState(false),[error,setError]=useState('');
 const activityEpoch=useRef(0);
 const generation=useRef(0),starting=useRef(false),submitted=useRef(false),planningRef=useRef(false),runningRef=useRef(false),callback=useRef(onRunning),pendingModal=useRef<{destroy:()=>void}|undefined>(undefined);
 callback.current=onRunning;
 const [modal,holder]=Modal.useModal();
 useEffect(()=>{
  const g=++generation.current;setPlan(undefined);setRows([]);setPage(1);setError('');setPlanning(false);planningRef.current=false;pendingModal.current?.destroy();
  void call('merge-invalidate').catch(e=>{if(g===generation.current)setError(e.message);});
 },[taskId,direction,mode]);
 useEffect(()=>{
  let disposed=false;let timer:ReturnType<typeof setTimeout>;
  const poll=async()=>{
   try{const epoch=activityEpoch.current,result=await call('merge-status');if(disposed)return;if(epoch!==activityEpoch.current){timer=setTimeout(()=>void poll(),100);return;}if(isMergeRecord(result.record))setRecord(result.record);
    const active=starting.current||!!result.running;runningRef.current=active;setRunning(active);callback.current(active||planningRef.current);
    if(!active){submitted.current=false;setStopping(false);}
   }catch(e){if(!disposed)setError((e as Error).message);}
   if(!disposed)timer=setTimeout(()=>void poll(),750);
  };void poll();return()=>{disposed=true;clearTimeout(timer);generation.current++;pendingModal.current?.destroy();callback.current(false);};
 },[]);
 useEffect(()=>{
  if(!plan)return;let disposed=false;const g=generation.current;setRows([]);
  void call('merge-page',{planId:plan.id,offset:(page-1)*30,limit:30}).then(r=>{if(!disposed&&g===generation.current&&!submitted.current)setRows(r.rows??[]);}).catch(e=>{if(!disposed&&g===generation.current&&!submitted.current){setError(e.message);setPlan(undefined);}});
  return()=>{disposed=true;};
 },[plan,page]);
 const preview=async()=>{
  if(disabled||runningRef.current||planningRef.current||!canPlanMerge(task,browse))return;
  const g=++generation.current;planningRef.current=true;setPlanning(true);callback.current(true);setPlan(undefined);setError('');
  try{const result=await call('merge-plan',{taskId,direction,mode});if(g!==generation.current)return;if(!result.plan)throw new Error('未收到数据计划');setPlan(result.plan);setPage(1);}
  catch(e){if(g===generation.current)setError((e as Error).message);}
  finally{if(g===generation.current){planningRef.current=false;setPlanning(false);callback.current(runningRef.current);}}
 };
 const execute=()=>{
  if(!plan||!plan.total||disabled||runningRef.current||submitted.current)return;
  const confirmed=plan,g=generation.current,target=direction==='left-to-right'?plan.right:plan.left;
  pendingModal.current=modal.confirm({title:`确认${mergeModeLabels[mode]}到${direction==='left-to-right'?'右侧':'左侧'}？`,okText:'确认执行',cancelText:'取消',width:640,okButtonProps:{danger:true},content:<Space orientation="vertical"><strong>{target.name} / {target.database}.{target.table}</strong><span>{mergeSummary(plan)}</span><span>全部符合共享条件的差异；键 {plan.key.join('、')}；字段 {plan.fields.join('、')}</span><Alert type="warning" title="每批独立提交，先前提交不会整体回滚。" description="冲突或失败停止后续批次；停止需等待当前可控边界。结果待核实时不要重试，请先重新比对。"/></Space>,onOk:async()=>{
   if(g!==generation.current||submitted.current)throw new Error('计划已失效，请重新预览');
   activityEpoch.current++;generation.current++;submitted.current=true;starting.current=true;runningRef.current=true;setRunning(true);callback.current(true);setPlan(undefined);setError('');
   try{await call('merge-execute',{planId:confirmed.id,confirmed:true});}
   catch(e){setError(`${(e as Error).message}；请查看执行状态，不要重复提交。`);throw e;}
   finally{starting.current=false;}
  }});
 };
 const locked=disabled||running||planning;
 return <section className="sync-panel">{holder}<Typography.Title level={4}>数据补齐与合并</Typography.Title>
 <Typography.Paragraph type="secondary">全部符合当前共享条件的差异；页面筛选、键定位与分页不缩小写入范围。参与字段由上方比对设置确定，匹配键不更新，目标独有记录保留。</Typography.Paragraph>
 <Space wrap><Select aria-label="数据写入方向" value={direction} disabled={locked} onChange={setDirection} options={[{value:'left-to-right',label:'左 → 右'},{value:'right-to-left',label:'右 → 左'}]}/><Select aria-label="数据写入模式" value={mode} disabled={locked} onChange={setMode} options={Object.entries(mergeModeLabels).map(([value,label])=>({value,label}))}/><Button disabled={locked||!canPlanMerge(task,browse)} loading={planning} onClick={()=>void preview()}>预览数据写入</Button></Space>
 {!canPlanMerge(task,browse)&&<Typography.Paragraph type="secondary">完成可靠键比对后可预览；无键浏览、停止或失败的扫描不能写入。</Typography.Paragraph>}
 {error&&<Alert type="error" showIcon title={error}/>}
 {plan&&<Space orientation="vertical" style={{width:'100%'}}><strong>目标：{(plan.direction==='left-to-right'?plan.right:plan.left).name} / {(plan.direction==='left-to-right'?plan.right:plan.left).database}.{(plan.direction==='left-to-right'?plan.right:plan.left).table}</strong><strong>{mergeModeLabels[plan.mode]} · {mergeSummary(plan)}</strong><span>匹配键：{plan.key.join('、')}；写入字段：{plan.fields.join('、')}；每批最多 {plan.batchSize} 项</span><span>共享 AND 范围：{plan.filters.length?plan.filters.map(f=>`${f.field} ${f.op}${f.op.includes('NULL')?'':` ${f.value}`}`).join(' AND '):'全表'}</span><span>左侧读取：{plan.readTimes.leftStartedAt??task?.leftStartedAt??'未提供'} → {plan.readTimes.leftFinishedAt??task?.leftFinishedAt??'未提供'}</span><span>右侧读取：{plan.readTimes.rightStartedAt??task?.rightStartedAt??'未提供'} → {plan.readTimes.rightFinishedAt??task?.rightFinishedAt??'未提供'}</span><Typography.Text type="secondary">{plan.readTimes.consistency??task?.consistency}</Typography.Text><Typography.Text type="secondary">源扫描快照为基准；两端不保证同一时刻，源随后变化不加入本次计划。</Typography.Text>{plan.warnings.map((warning,index)=><Alert key={index} type="warning" title={warning}/>)}<Table<MergeRow> rowKey="id" dataSource={rows} pagination={{current:page,pageSize:30,total:plan.total,showSizeChanger:false,onChange:setPage}} scroll={{x:600}} columns={[{title:'完整键',render:(_,r)=><pre>{JSON.stringify(r.key)}</pre>},{title:'操作',render:(_,r)=>r.action==='insert'?'插入':'更新'},{title:'字段',render:(_,r)=>r.fields.join('、')}]} expandable={{expandedRowRender:r=><><pre className="sync-sql">{r.sql}</pre><pre className="sync-sql">{JSON.stringify(r.sample,null,2)}</pre></>}}/><Typography.Text strong>参数化 SQL 样例</Typography.Text><pre className="sync-sql">{plan.sql.join('\n')}</pre>{!plan.total?<Alert type="info" title="无可执行变更；未执行写入。"/>:<Button type="primary" danger disabled={locked} onClick={execute}>执行数据写入</Button>}</Space>}
 {(running||record)&&<Space orientation="vertical" style={{width:'100%'}}><Typography.Title level={5}>最近数据执行</Typography.Title>{record&&<><Alert type={record.status==='passed'?'success':record.status==='failed'?'error':'info'} title={`${syncLabels[record.status]} · 已确认提交 ${record.committed} / ${record.total}`} description={<>{({checking:'校验结构与目标旧值',executing:'分批执行',finished:'执行结束'} as Record<string,string>)[record.stage??'']??'正在核实执行状态'}<div>{record.error}</div><div>执行结果不等于复核一致；补齐后可仍有同键字段差异。</div></>}/>{record.storageWarning&&<Alert type="warning" title={record.storageWarning}/>}<MergeBatches batches={record.batches}/></>}{running?<Button disabled={stopping} loading={stopping} onClick={()=>{setStopping(true);void call('merge-stop').catch(e=>{setError(e.message);setStopping(false);});}}>请求边界停止</Button>:<Button disabled={disabled||planning||!taskId} onClick={onRecompare}>按当前上下文重新比对</Button>}</Space>}
 </section>;
}
