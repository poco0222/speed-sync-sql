import type {ReactNode} from 'react';
import {useEffect,useRef,useState} from 'react';
import {Alert,Button,Checkbox,Collapse,Modal,Segmented,Select,Space,Table,Typography} from 'antd';
import {ArrowLeftOutlined,EyeOutlined,PlayCircleOutlined,ProfileOutlined,RedoOutlined,StopOutlined} from '@ant-design/icons';
import {request} from './bridge';
import {syncLabels} from './sync';
import {canPlanMerge,mergeCounts,isMergeRecord,mergeModeLabels,mergeSummary,type MergeBatch,type MergeMode,type MergePlan,type MergeRecord,type MergeResult,type MergeRow} from './merge';
import type {DataTask} from './data';
const call=async(operation:string,args:object={}):Promise<MergeResult>=>{const r=await request(operation,args) as unknown as MergeResult;if(!r.ok||r.stale)throw new Error([r.error??'数据计划已失效，请重新预览',...(r.blockers??[])].join('\n'));return r;};
export function MergeBatches({batches}:{batches:MergeBatch[]}) {
 return <Table<MergeBatch> size="small" rowKey="index" dataSource={batches} pagination={{pageSize:20,showSizeChanger:false}} columns={[{title:'批次',dataIndex:'index'},{title:'操作数',dataIndex:'count'},{title:'结果',render:(_,b)=><>{({running:'执行中',passed:'已提交',failed:'已回滚',unknown:'待核实',pending:'未执行'})[b.status]}{b.error&&<Typography.Paragraph type="danger">{b.error}</Typography.Paragraph>}</>}]}/>;
}
export function DataMergePanel({task,taskId,browse,disabled,onRunning,onRecompare,mode,onModeChange,target,scope,startAction,modeControl,children}:{modeControl?:ReactNode;children?:ReactNode;mode:MergeMode;onModeChange:(mode:MergeMode)=>void;target:string;scope:string;startAction:ReactNode;task?:DataTask;taskId:string;browse:boolean;disabled:boolean;onRunning:(active:boolean)=>void;onRecompare:()=>void}) {
 const [previewOpen,setPreviewOpen]=useState(false);
 const direction='left-to-right';
 const [plan,setPlan]=useState<MergePlan>(),[rows,setRows]=useState<MergeRow[]>([]),[page,setPage]=useState(1);
 const [record,setRecord]=useState<MergeRecord>(),[running,setRunning]=useState(false),[planning,setPlanning]=useState(false),[stopping,setStopping]=useState(false),[error,setError]=useState('');
 const [deleteConfirmed,setDeleteConfirmed]=useState(false),[rowAction,setRowAction]=useState('');
 const activityEpoch=useRef(0);
 const generation=useRef(0),starting=useRef(false),submitted=useRef(false),planningRef=useRef(false),runningRef=useRef(false),callback=useRef(onRunning),pendingModal=useRef<{destroy:()=>void}|undefined>(undefined);
 callback.current=onRunning;
 const [modal,holder]=Modal.useModal();
 useEffect(()=>{
  const g=++generation.current;setPreviewOpen(false);setPlan(undefined);setDeleteConfirmed(false);setRowAction('');setRows([]);setPage(1);setError('');setPlanning(false);planningRef.current=false;pendingModal.current?.destroy();
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
  void call('merge-page',{planId:plan.id,offset:(page-1)*30,limit:30,action:rowAction}).then(r=>{if(!disposed&&g===generation.current&&!submitted.current)setRows(r.rows??[]);}).catch(e=>{if(!disposed&&g===generation.current&&!submitted.current){setError(e.message);setPlan(undefined);}});
  return()=>{disposed=true;};
 },[plan,page,rowAction]);
 const preview=async()=>{
  if(disabled||runningRef.current||planningRef.current||!canPlanMerge(task,browse))return;
  const g=++generation.current;planningRef.current=true;setPlanning(true);callback.current(true);setPlan(undefined);setDeleteConfirmed(false);setRowAction('');setError('');
  try{const result=await call('merge-plan',{taskId,direction,mode});if(g!==generation.current)return;if(!result.plan)throw new Error('未收到数据计划');setPlan(result.plan);setPage(1);setPreviewOpen(true);}
  catch(e){if(g===generation.current)setError((e as Error).message);}
  finally{if(g===generation.current){planningRef.current=false;setPlanning(false);callback.current(runningRef.current);}}
 };
 const execute=()=>{
  if(!plan||!plan.total||disabled||runningRef.current||submitted.current||(plan.counts.delete>0&&!deleteConfirmed))return;
  const confirmed=plan,g=generation.current,target=plan.right;
  pendingModal.current=modal.confirm({title:`确认${mergeModeLabels[mode]}到目标？`,okText:'确认执行',cancelText:'取消',width:640,okButtonProps:{danger:true},content:<Space orientation="vertical"><strong>{target.name} / {target.database}.{target.table}</strong><span>{mergeSummary(plan)}</span><span>范围：{plan.filters.length?plan.filters.map(f=>`${f.field} ${f.op} ${f.value??''}`).join(' AND '):'全表'}</span>{plan.counts.delete>0&&<strong>已确认删除 {plan.counts.delete} 条整行，包含未参与比较的列；范围外记录保留。</strong>}<span>全部符合共享条件的差异；键 {plan.key.join('、')}；字段 {plan.fields.join('、')}</span><Alert type="warning" title="每批独立提交，先前提交不会整体回滚。" description="冲突或失败停止后续批次；停止需等待当前可控边界。结果待核实时不要重试，请先重新比对。"/></Space>,onOk:async()=>{
   if(g!==generation.current||submitted.current)throw new Error('计划已失效，请重新预览');
   activityEpoch.current++;generation.current++;submitted.current=true;starting.current=true;runningRef.current=true;setRunning(true);callback.current(true);setPlan(undefined);setDeleteConfirmed(false);setRowAction('');setError('');
   try{await call('merge-execute',{planId:confirmed.id,confirmed:true,deleteConfirmed});}
   catch(e){setError(`${(e as Error).message}；请查看执行状态，不要重复提交。`);throw e;}
   finally{starting.current=false;}
  }});
 };
 const locked=disabled||running||planning;
 const stop=()=>{setStopping(true);void call('merge-stop').catch(e=>{setError(e.message);setStopping(false);});};
 return <section className="sync-panel merge-panel">{holder}
 <div hidden={previewOpen}><Space wrap className="workbench-actions">{modeControl}<Segmented aria-label="数据写入模式" value={mode} disabled={locked} onChange={onModeChange} options={[{value:'fill',label:'仅补齐'},{value:'merge',label:'新增 + 更新'},{value:'align',label:'完全对齐（含删除）'}]}/>{startAction}<Button type="primary" icon={<EyeOutlined />} disabled={locked||!canPlanMerge(task,browse)} loading={planning} onClick={()=>void preview()}>预览同步</Button>{running&&<Button icon={<StopOutlined />} disabled={stopping} loading={stopping} onClick={stop}>请求边界停止</Button>}{!running&&record&&<><Typography.Text>{syncLabels[record.status]} · 已确认提交 {record.committed} / {record.total}</Typography.Text><Button icon={<RedoOutlined />} disabled={disabled||planning||!taskId} onClick={onRecompare}>按当前上下文重新比对</Button></>}{(running||record)&&<Button icon={<ProfileOutlined />} onClick={()=>setPreviewOpen(true)}>查看执行结果</Button>}</Space><div className="sync-estimate"><Typography.Text>目标：{target} · 范围：{scope}</Typography.Text><Typography.Paragraph type="secondary">{running?'执行中 · 返回差异不会停止任务':planning?'正在生成计划':task?.complete&&!browse?`本次预计：${mergeSummary({counts:mergeCounts(task,mode)})} · 保留 ${mergeCounts(task,mode).keep}（以预览为准）`:'完成可靠键比对后可预览'}。</Typography.Paragraph></div>{children}</div>
 {error&&<Alert type="error" showIcon title={error}/>}
 <div className="inline-preview" hidden={!previewOpen}><Button icon={<ArrowLeftOutlined />} onClick={()=>setPreviewOpen(false)}>返回差异</Button><Typography.Title level={4}>数据写入预览</Typography.Title>
 <Space orientation="vertical" size="middle" style={{width:'100%'}}>
 {!canPlanMerge(task,browse)&&<Typography.Paragraph type="secondary">完成可靠键比对后可预览。</Typography.Paragraph>}
 {error&&<Alert type="error" showIcon title={error}/>}
 {plan&&<Space orientation="vertical" style={{width:'100%'}}><strong>目标：{(plan.direction==='left-to-right'?plan.right:plan.left).name} / {(plan.direction==='left-to-right'?plan.right:plan.left).database}.{(plan.direction==='left-to-right'?plan.right:plan.left).table}</strong><strong>{plan.direction==='left-to-right'?'左 → 右':'右 → 左'} · {mergeModeLabels[plan.mode]} · {mergeSummary(plan)}</strong><span>匹配键：{plan.key.join('、')}；写入字段：{plan.fields.join('、')}；每批最多 {plan.batchSize} 项</span><span>共享 AND 范围：{plan.filters.length?plan.filters.map(f=>`${f.field} ${f.op}${f.op.includes('NULL')?'':` ${f.value}`}`).join(' AND '):'全表'}</span><Collapse items={[{key:'read',label:'读取时间与一致性',children:<Space orientation="vertical"><span>左侧读取：{plan.readTimes.leftStartedAt??task?.leftStartedAt??'未提供'} → {plan.readTimes.leftFinishedAt??task?.leftFinishedAt??'未提供'}</span><span>右侧读取：{plan.readTimes.rightStartedAt??task?.rightStartedAt??'未提供'} → {plan.readTimes.rightFinishedAt??task?.rightFinishedAt??'未提供'}</span><Typography.Text type="secondary">{plan.readTimes.consistency??task?.consistency}</Typography.Text></Space>}]} />{plan.warnings.map((warning,index)=><Alert key={index} type="warning" title={warning}/>)}<Select aria-label="操作预览分类" value={rowAction} onChange={value=>{setRowAction(value);setPage(1);}} options={[{value:'',label:'全部操作'},{value:'delete',label:`仅删除（${plan.counts.delete}）`}]}/><Table<MergeRow> rowKey="id" dataSource={rows} pagination={{current:page,pageSize:30,total:rowAction==='delete'?plan.counts.delete:plan.total,showSizeChanger:false,onChange:setPage}} scroll={{x:600,y:300}} columns={[{title:'完整键',render:(_,r)=><pre className="sync-sql">{JSON.stringify(r.key)}</pre>},{title:'操作',render:(_,r)=>r.action==='delete'?'删除整行':r.action==='insert'?'插入':'更新'},{title:'字段',render:(_,r)=>r.action==='delete'?'整行（含未比较列）':r.fields.join('、')}]} expandable={{expandedRowRender:r=><><pre className="sync-sql">{r.sql}</pre><pre className="sync-sql">{JSON.stringify(r.sample,null,2)}</pre></>}}/><details><summary>参数化 SQL 样例</summary><pre className="sync-sql">{plan.sql.join('\n')}</pre></details>{plan.counts.delete>0&&<Checkbox checked={deleteConfirmed} disabled={locked} onChange={e=>setDeleteConfirmed(e.target.checked)}>我确认删除上述{plan.filters.length?'共享筛选范围':'全表范围'}内 {plan.counts.delete} 条整行（含未参与比较的列）；范围外记录保留</Checkbox>}{!plan.total?<Alert type="info" title="无可执行变更；未执行写入。"/>:<Button type="primary" danger icon={<PlayCircleOutlined />} disabled={locked||(plan.counts.delete>0&&!deleteConfirmed)} onClick={execute}>执行数据写入</Button>}</Space>}
 {(running||record)&&<Space orientation="vertical" style={{width:'100%'}}><Typography.Title level={5}>最近数据执行</Typography.Title>{record&&<><Alert type={record.status==='passed'?'success':record.status==='failed'?'error':'info'} title={`${syncLabels[record.status]} · 已确认提交 ${record.committed} / ${record.total}`} description={<>{({checking:'校验结构与目标旧值',executing:'分批执行',finished:'执行结束'} as Record<string,string>)[record.stage??'']??'正在核实执行状态'}<div>{record.error}</div></>}/>{record.storageWarning&&<Alert type="warning" title={record.storageWarning}/>}<MergeBatches batches={record.batches}/></>}{running?<Button icon={<StopOutlined />} disabled={stopping} loading={stopping} onClick={stop}>请求边界停止</Button>:<Button icon={<RedoOutlined />} disabled={disabled||planning||!taskId} onClick={onRecompare}>按当前上下文重新比对</Button>}</Space>}
 </Space></div></section>;
}
