import { useEffect, useRef, useState } from 'react';
import { Alert, Button, Modal, Select, Space, Table, Typography } from 'antd';
import { request } from './bridge';
import type { Comparison } from './schema';
import { changeSummary, isSyncRecord, rowKey, syncLabels, type Direction, type SyncPlan, type SyncRecord, type SyncStep } from './sync';

export function SyncOperations({plan}: {plan: SyncPlan}) {
 return <div><Typography.Text strong>{changeSummary(plan)}</Typography.Text>{plan.operations && <ul className="sync-operations">{plan.operations.map((operation,index)=><li key={index}><strong>{{add:'新增',modify:'修改',delete:'删除'}[operation.action]}</strong> · {operation.category} / {operation.name} · {operation.summary}</li>)}</ul>}</div>;
}
export function SyncSteps({steps}: {steps: SyncStep[]}) {
 return <Table<SyncStep> size="small" rowKey={(_, index) => String(index)} dataSource={steps} pagination={false} scroll={{x:600}} columns={[
  {title:'操作',dataIndex:'summary'}, {title:'对象',render:(_,step)=>`${step.category} / ${step.name}`},
  {title:'风险 / 结果',render:(_,step)=><><div>{step.risk}</div><strong>{syncLabels[step.status ?? ''] ?? ''}</strong>{step.error && <Typography.Paragraph type="danger">{step.error}</Typography.Paragraph>}</>}
 ]} expandable={{expandedRowRender:step=><><pre className="sync-sql" tabIndex={0}>{step.sql}</pre>{step.context && <pre className="sync-sql">{JSON.stringify(step.context,null,2)}</pre>}</>}} />;
}
export function SyncPanel({comparison,selected,disabled,onRunning,onComparison}: {comparison?:Comparison;selected:string[];disabled:boolean;onRunning:(running:boolean)=>void;onComparison:(comparison:Comparison)=>void}) {
 const [direction,setDirection]=useState<Direction>('left-to-right');
 const [plan,setPlan]=useState<SyncPlan>();
 const [record,setRecord]=useState<SyncRecord>();
 const [planning,setPlanning]=useState(false);
 const [running,setRunning]=useState(false);
 const [stopping,setStopping]=useState(false);
 const [error,setError]=useState('');
 const [modal,holder]=Modal.useModal();
 const generation=useRef(0);
 const submitted=useRef(false);
 const starting=useRef(false);
 const verified=useRef('');
 const callbacks=useRef({onRunning,onComparison}); callbacks.current={onRunning,onComparison};
 useEffect(()=>{
  generation.current++; setPlan(undefined); setPlanning(false); setError('');
  void request('invalidate-plan').catch(e=>setError(e.message));
 },[comparison,selected,direction]);
 useEffect(()=>{
  let disposed=false; let timer:ReturnType<typeof setTimeout>;
  const poll=async()=>{
   try {
    const result=await request('sync-status'); if(disposed)return;
    if(!result.ok)throw new Error(result.error ?? '执行状态读取失败');
    const latest=isSyncRecord(result.record)?result.record:undefined;
    if(latest)setRecord(latest);
    const active=starting.current || !!result.running; setRunning(active); callbacks.current.onRunning(active);
    if(!active){submitted.current=false;setStopping(false);if(result.comparison && latest && verified.current!==latest.id){verified.current=latest.id;callbacks.current.onComparison(result.comparison);}}
   }catch(e){if(!disposed)setError(e instanceof Error?e.message:'执行状态待核实；请勿重试执行');}
   if(!disposed)timer=setTimeout(()=>void poll(),1000);
  };
  void poll(); return()=>{disposed=true;clearTimeout(timer);};
 },[]);
 const build=async(alignAll:boolean)=>{
  if(planning||running)return;
  const current=++generation.current;setPlanning(true);setPlan(undefined);setError('');
  try{
   const result=await request('plan-sync',{direction,selected:comparison?.rows.filter(row=>selected.includes(rowKey(row))).map(({category,name})=>({category,name})) ?? [],alignAll});
   if(current!==generation.current)return;
   if(result.stale)throw new Error('比对或选区已变化，请重新预览');
   if(!result.ok || !result.plan)throw new Error([result.error ?? '无法生成计划',...(result.blockers ?? [])].join('\n'));
   setPlan(result.plan);
  }catch(e){if(current===generation.current)setError(e instanceof Error?e.message:'计划生成失败');}
  finally{if(current===generation.current)setPlanning(false);}
 };
 const execute=()=>{
  if(!plan||submitted.current||running)return;
  const confirmed=plan;const current=generation.current;
  modal.confirm({title:`确认执行到${direction==='left-to-right'?'右侧':'左侧'}？`,width:640,okText:`执行到${direction==='left-to-right'?'右侧':'左侧'}`,cancelText:'取消',okButtonProps:{danger:true},content:<Space orientation="vertical"><strong>{(direction==='left-to-right'?plan.right:plan.left).name} / {(direction==='left-to-right'?plan.right:plan.left).database} / {(direction==='left-to-right'?plan.right:plan.left).table}</strong><SyncOperations plan={plan}/><span>共 {plan.steps.length} 条语句。{plan.steps.filter(step=>step.risk).map(step=>step.risk).join('；')}</span><Alert type="warning" showIcon title="多条 DDL 不保证整体回滚；原始结构定义不能恢复丢失数据。" description="执行前检查两端漂移并保存原始结构。停止需等待当前语句结束。" /></Space>,onOk:async()=>{
   if(submitted.current||current!==generation.current)throw new Error('计划已失效，请重新预览');
   submitted.current=true;starting.current=true;setPlan(undefined);setRunning(true);onRunning(true);setError('');
   try{const result=await request('execute-sync',{planId:confirmed.id});if(!result.ok)throw new Error(result.error ?? '执行未启动');setPlan(undefined);}
   catch(e){setError(e instanceof Error?e.message:'启动结果待核实');throw e;}finally{starting.current=false;}
  }});
 };
 const exportPlan=async(operation:string)=>{
  try{const result=await request(operation,{planId:plan?.id});if(!result.ok)throw new Error(result.error ?? '复制或导出失败');}catch(e){setError(e instanceof Error?e.message:'操作失败');}
 };
 return <section className="sync-panel" aria-label="结构同步">{holder}
  <Space wrap><Typography.Text strong>结构同步</Typography.Text><Select aria-label="同步方向" value={direction} disabled={disabled||planning||running} onChange={setDirection} options={[{value:'left-to-right',label:'左侧读取 → 右侧写入'},{value:'right-to-left',label:'右侧读取 → 左侧写入'}]} /><span>已选 {selected.length} 项</span><Button disabled={disabled||!comparison||!selected.length||running} loading={planning} onClick={()=>void build(false)}>预览选中对象 SQL</Button><Button disabled={disabled||!comparison||planning||running} onClick={()=>void build(true)}>预览整表对齐（全部增改删）</Button></Space>
  {error && <Alert type="error" showIcon title="同步提示" description={<pre className="sync-sql">{error}</pre>} />}
  {planning && <Alert type="info" title="正在检查依赖并生成计划，不执行写入。" />}
  {plan && <><div className="definition-heading"><strong>只读 SQL 预览 · 写入 {(plan.direction==='left-to-right'?plan.right:plan.left).name} / {(plan.direction==='left-to-right'?plan.right:plan.left).database} / {(plan.direction==='left-to-right'?plan.right:plan.left).table} · {plan.steps.length} 步</strong><Space wrap><Button onClick={()=>document.getElementById('schema-differences')?.scrollIntoView({block:'start'})}>返回差异</Button><Button onClick={()=>void exportPlan('copy-sync')}>复制完整 SQL</Button><Button onClick={()=>void exportPlan('export-sync')}>导出 SQL</Button><Button type="primary" danger disabled={disabled||running||!plan.steps.length} onClick={execute}>执行到{direction==='left-to-right'?'右侧':'左侧'}</Button></Space></div><SyncOperations plan={plan}/><SyncSteps steps={plan.steps}/><pre className="sync-sql" tabIndex={0}>{plan.sql}</pre></>}
  {running && <Alert type="info" showIcon title={stopping?'等待当前语句结束后停止':'正在执行，连接、库表、方向与选区已锁定'} action={<Button disabled={stopping} onClick={()=>{setStopping(true);void request('stop-sync').then(result=>{if(!result.ok)throw new Error(result.error ?? '停止请求失败');}).catch(e=>{setStopping(false);setError(e.message);});}}>请求停止</Button>} />}
  {record && <><Typography.Text strong>最近执行：{syncLabels[record.status]} · {record.startedAt}</Typography.Text>{record.storageWarning && <Alert type="warning" showIcon title="记录保存异常" description={record.storageWarning}/>} {record.error && <Alert type="warning" title={record.error}/>}<SyncSteps steps={record.steps}/>{record.verification && <Alert type={record.verification.status==='same'?'success':'info'} title={`结构复核：${syncLabels[record.verification.status] ?? record.verification.status}`} description={record.verification.error || '部分选区同步后，未选对象可能仍有差异。'}/>}</>}
 </section>;
}
