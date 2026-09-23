import { useEffect, useRef, useState, type ReactNode } from 'react';
import { Alert, Button, Modal, Segmented, Space, Table, Typography } from 'antd';
import { ArrowLeftOutlined, CopyOutlined, ExportOutlined, EyeOutlined, PlayCircleOutlined, ProfileOutlined, RedoOutlined, StopOutlined } from '@ant-design/icons';
import { request } from './bridge';
import type { Comparison } from './schema';
import { changeSummary, isSyncRecord, rowKey, selectionReason, syncLabels, type SyncPlan, type SyncRecord, type SyncStep } from './sync';

export function SyncOperations({plan}: {plan: SyncPlan}) {
 return <div><Typography.Text strong>{changeSummary(plan)}</Typography.Text>{plan.operations && <ul className="sync-operations">{plan.operations.map((operation,index)=><li key={index}><strong>{{add:'新增',modify:'修改',delete:'删除'}[operation.action]}</strong> · {operation.category} / {operation.name} · {operation.summary}</li>)}</ul>}</div>;
}
export function SyncSteps({steps}: {steps: SyncStep[]}) {
 return <Table<SyncStep> size="small" rowKey={(_, index) => String(index)} dataSource={steps} pagination={false} scroll={{x:600}} columns={[
  {title:'操作',dataIndex:'summary'}, {title:'对象',render:(_,step)=>`${step.category} / ${step.name}`},
  {title:'风险 / 结果',render:(_,step)=><><div>{step.risk}</div><strong>{syncLabels[step.status ?? ''] ?? ''}</strong>{step.error && <Typography.Paragraph type="danger">{step.error}</Typography.Paragraph>}</>}
 ]} expandable={{expandedRowRender:step=><><pre className="sync-sql" tabIndex={0}>{step.sql}</pre>{step.context && <pre className="sync-sql">{JSON.stringify(step.context,null,2)}</pre>}</>}} />;
}
export function SyncPanel({comparison,selected,disabled,onRunning,onComparison,onRecompare,action,modeControl,strategy='fill',onStrategyChange,children,target}: {comparison?:Comparison;selected:string[];disabled:boolean;onRunning:(running:boolean)=>void;onComparison:(comparison:Comparison)=>void;onRecompare?:()=>void;action?:ReactNode;modeControl?:ReactNode;strategy?:string;onStrategyChange?:(strategy:string)=>void;children?:ReactNode;target?:string}) {
 const direction='left-to-right';
 const alignAll=strategy==='align';
 const [previewOpen,setPreviewOpen]=useState(false);
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
 const planningRef=useRef(false);
 const runningRef=useRef(false);
 const verified=useRef('');
 const callbacks=useRef({onRunning,onComparison}); callbacks.current={onRunning,onComparison};
 useEffect(()=>{
  generation.current++; setPlan(undefined); setPlanning(false); setPreviewOpen(false); setError('');
  void request('invalidate-plan').catch(e=>setError(e.message));
 },[comparison,selected,strategy]);
 useEffect(()=>{
  let disposed=false; let timer:ReturnType<typeof setTimeout>;
  const poll=async()=>{
   try {
    const result=await request('sync-status'); if(disposed)return;
    if(!result.ok)throw new Error(result.error ?? '执行状态读取失败');
    const latest=isSyncRecord(result.record)?result.record:undefined;
    if(latest)setRecord(latest);
    const active=starting.current || !!result.running; runningRef.current=active;setRunning(active); callbacks.current.onRunning(active||planningRef.current);
    if(!active){submitted.current=false;setStopping(false);if(result.comparison && latest && verified.current!==latest.id){verified.current=latest.id;callbacks.current.onComparison(result.comparison);}}
   }catch(e){if(!disposed)setError(e instanceof Error?e.message:'执行状态待核实；请勿重试执行');}
   if(!disposed)timer=setTimeout(()=>void poll(),1000);
  };
  void poll(); return()=>{disposed=true;clearTimeout(timer);};
 },[]);
 const build=async(alignAll:boolean)=>{
  if(disabled||planningRef.current||runningRef.current||running)return;
  const current=++generation.current;planningRef.current=true;callbacks.current.onRunning(true);setPlanning(true);setPlan(undefined);setError('');
  try{
   const result=await request('plan-sync',{direction,selected:comparison?.rows.filter(row=>selected.includes(rowKey(row))).map(({category,name})=>({category,name})) ?? [],alignAll});
   if(current!==generation.current)return;
   if(result.stale)throw new Error('比对或选区已变化，请重新预览');
   if(!result.ok || !result.plan)throw new Error([result.error ?? '无法生成计划',...(result.blockers ?? [])].join('\n'));
   setPlan(result.plan);setPreviewOpen(true);
  }catch(e){if(current===generation.current)setError(e instanceof Error?e.message:'计划生成失败');}
  finally{planningRef.current=false;callbacks.current.onRunning(runningRef.current);if(current===generation.current)setPlanning(false);}
 };
 const execute=()=>{
  if(!plan||!plan.steps.length||submitted.current||running)return;
  const confirmed=plan;const current=generation.current;
  modal.confirm({title:'确认执行到目标？',width:640,okText:'执行到目标',cancelText:'取消',okButtonProps:{danger:true},content:<Space orientation="vertical"><strong>{plan.right.name} / {plan.right.database} / {plan.right.table}</strong><SyncOperations plan={plan}/><span>共 {plan.steps.length} 条语句。{plan.steps.filter(step=>step.risk).map(step=>step.risk).join('；')}</span><Alert type="warning" showIcon title="多条 DDL 不保证整体回滚；原始结构定义不能恢复丢失数据。" description="执行前检查两端漂移并保存原始结构。停止需等待当前语句结束。" /></Space>,onOk:async()=>{
   if(submitted.current||current!==generation.current)throw new Error('计划已失效，请重新预览');
   submitted.current=true;starting.current=true;runningRef.current=true;setPlan(undefined);setRunning(true);onRunning(true);setError('');
   try{const result=await request('execute-sync',{planId:confirmed.id});if(!result.ok)throw new Error(result.error ?? '执行未启动');setPlan(undefined);}
   catch(e){setError(e instanceof Error?e.message:'启动结果待核实');throw e;}finally{starting.current=false;}
  }});
 };
 const exportPlan=async(operation:string)=>{
  try{const result=await request(operation,{planId:plan?.id});if(!result.ok)throw new Error(result.error ?? '复制或导出失败');}catch(e){setError(e instanceof Error?e.message:'操作失败');}
 };
 const scopeRows=comparison?.rows.filter(row=>row.status!=='same'&&!selectionReason(row))??[];
 const chosen=scopeRows.filter(row=>alignAll||selected.includes(rowKey(row)));
 const estimated=`新增 ${chosen.filter(row=>row.status==='left-only').length} · 修改 ${chosen.filter(row=>row.status==='different').length} · 删除 ${chosen.filter(row=>row.status==='right-only').length} · 保留 ${scopeRows.length-chosen.length} 项差异（预计，以预览为准）`;
 const runningStatus=running && <Alert type="info" showIcon title={stopping?'等待当前语句结束后停止':'正在执行，连接、库表、方向与选区已锁定'} action={<Button icon={<StopOutlined />} disabled={stopping} onClick={()=>{setStopping(true);void request('stop-sync').then(result=>{if(!result.ok)throw new Error(result.error ?? '停止请求失败');}).catch(e=>{setStopping(false);setError(e.message);});}}>请求停止</Button>} />;
 return <section className="sync-panel" aria-label="结构同步">{holder}
  <div hidden={previewOpen}><div className="workbench-actions"><Space wrap>{modeControl}<Segmented aria-label="结构同步方式" value={strategy} disabled={disabled||planning||running} onChange={value=>onStrategyChange?.(value)} options={[{value:'fill',label:'仅新增'},{value:'merge',label:'新增 + 更新'},{value:'align',label:'完全对齐（含删除）'}]} />{action}<Button type="primary" icon={<EyeOutlined />} disabled={disabled||!comparison||(!alignAll&&!selected.length)||running} loading={planning} onClick={()=>void build(alignAll)}>预览同步</Button>{(plan||record) && !previewOpen && <Button icon={<ProfileOutlined />} onClick={()=>setPreviewOpen(true)}>查看同步详情</Button>}</Space></div>
  <div className="sync-summary"><Typography.Text title={target}>目标：{target}</Typography.Text> · {alignAll?'范围：整张表，不受列表筛选影响':`已选 ${selected.length} 项，可在差异表调整`} {comparison&&<Typography.Paragraph>本次预计：{estimated}</Typography.Paragraph>}</div>
  </div>{runningStatus}
  {record&&!running && <Typography.Text strong>最近执行：{syncLabels[record.status]} · {record.startedAt}</Typography.Text>}
  {error && <Alert type="error" showIcon title="同步提示" description={<pre className="sync-sql">{error}</pre>} />}
  {planning && <Alert type="info" title="正在检查依赖并生成计划，不执行写入。" />}
  <div hidden={previewOpen}>{children}</div>
  <section className="inline-preview" aria-label="结构同步预览" hidden={!previewOpen}><Button icon={<ArrowLeftOutlined />} onClick={()=>setPreviewOpen(false)}>返回差异</Button>{record&&!running&&onRecompare && <Button icon={<RedoOutlined />} disabled={disabled||planning} onClick={()=>{setPreviewOpen(false);onRecompare();}}>重新比对</Button>}
  {runningStatus}
  {error && <Alert type="error" showIcon title={error}/>}
  {plan && <><div className="definition-heading"><strong>目标：{plan.right.name} / {plan.right.database} / {plan.right.table} · {plan.steps.length} 步</strong><Space wrap><Button type="primary" danger icon={<PlayCircleOutlined />} disabled={disabled||running||!plan.steps.length} onClick={execute}>执行到目标</Button></Space></div><Typography.Paragraph>来源：{plan.left.name} / {plan.left.database} / {plan.left.table}<br/>范围：{alignAll?'整表对齐（全部增改删）':`选中 ${selected.length} 个对象`}</Typography.Paragraph><SyncOperations plan={plan}/>{!plan.steps.length && <Alert type="info" showIcon title="没有可执行变更"/>}<SyncSteps steps={plan.steps}/><details><summary>SQL 与技术说明</summary><Typography.Paragraph>预览不写入。多条 DDL 不保证整体回滚；停止需等待当前语句结束。</Typography.Paragraph><Space wrap><Button icon={<CopyOutlined />} onClick={()=>void exportPlan('copy-sync')}>复制完整 SQL</Button><Button icon={<ExportOutlined />} onClick={()=>void exportPlan('export-sync')}>导出 SQL</Button></Space><pre className="sync-sql" tabIndex={0}>{plan.sql}</pre></details></>}
  {record && <><Typography.Text strong>最近执行：{syncLabels[record.status]} · {record.startedAt}</Typography.Text>{record.storageWarning && <Alert type="warning" showIcon title="记录保存异常" description={record.storageWarning}/>} {record.error && <Alert type="warning" title={record.error}/>}<SyncSteps steps={record.steps}/>{record.verification && <Alert type={record.verification.status==='same'?'success':'info'} title={`结构复核：${syncLabels[record.verification.status] ?? record.verification.status}`} description={record.verification.error || '部分选区同步后，未选对象可能仍有差异。'}/>}</>}
  </section>
 </section>;
}
