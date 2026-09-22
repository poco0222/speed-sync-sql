// Test-only native bridge. This file is loaded only by tests/workbench.html.
const endpoint=side=>({connectionId:side,name:`隔离测试${side==='left'?'源':'目标'}`,database:`fixture_${side}`,table:'orders'});
const connection=side=>({id:side,name:endpoint(side).name,host:'fixture.invalid',port:3306,user:'fixture',database:endpoint(side).database,remember:false,tls:'disabled',ca:'',timeout:10});
const state={connections:['left','right'].map(connection),left:'left',right:'right',settings:{theme:'light',density:'compact',timeout:10},driverAvailable:true,workspace:{left:endpoint('left'),right:endpoint('right')}};
const types={id:'bigint',status:'varchar(32)',amount:'decimal(20,2)',remark:'text',created_at:'date'};
const time='2026-09-22T10:00:00Z';
const longText='隔离测试长值·'.repeat(1000);
const dataset=Array.from({length:76},(_,i)=>({id:`row-${i+1}`,key:[`${1001+i}`],status:i<35?'different':i<40?'left-only':i<43?'right-only':'same',changedFields:i<35?['status','remark']:[],date:i<38?'2026-09-01':'2026-09-20'}));
let task,scanSettings,scanRows=[],plan,planRows=[],record,running=false,serial=0;
const calls=[];
window.__workbenchFixture={calls,label:'隔离测试数据，无真实数据库'};
const value=(text,truncate=false)=>({isNull:text===null,text:text??'',length:text?.length??0,encoding:'text',truncated:truncate});
const fieldsFor=row=>Object.entries(types).map(([name,type])=>{
 let left=name==='id'?row.key[0]:name==='status'?'pending':name==='amount'?'9007199254740993.01':name==='created_at'?row.date:null;
 let right=left;
 if(row.status==='different'){if(name==='status')right='processing';if(name==='remark')right=row.id==='row-1'?longText:'';}
 return {name,type,changed:row.changedFields.includes(name),left:row.status==='right-only'?null:value(left),right:row.status==='left-only'?null:value(right,name==='remark'&&right===longText)};
}).filter(field=>!scanSettings||scanSettings.fields.includes(field.name)||scanSettings.key.includes(field.name));
const fieldValue=(row,name)=>name==='id'?row.key[0]:name==='created_at'?row.date:name==='amount'?'9007199254740993.01':name==='status'?'pending':null;
function matches(row,f){
 const actual=fieldValue(row,f.field),expected=f.value;
 if(f.op==='IS NULL')return actual===null;if(f.op==='IS NOT NULL')return actual!==null;
 if(actual===null)return false;
 const cmp=types[f.field]==='bigint'?BigInt(actual)-BigInt(expected):actual===expected?0:actual>expected?1:-1;
 return ({'=':cmp==0,'!=':cmp!=0,'>':cmp>0,'>=':cmp>=0,'<':cmp<0,'<=':cmp<=0})[f.op]??false;
}
const currentTask=args=>{if(!task||args.taskId!==task.id)throw Error('隔离扫描已失效');};
const currentPlan=args=>{if(!plan||args.planId!==plan.id)throw Error('隔离计划已失效');};
const schemaRow=(name,left,right,status='different')=>({category:'columns',name,status,left:{name,properties:left,ddl:`\`${name}\` ${left.type}`},right:right?{name,properties:right,ddl:`\`${name}\` ${right.type}`} :null,changed:Object.keys(left).filter(k=>left[k]!==right?.[k])});
const comparison={complete:true,status:'different',left:{...endpoint('left'),version:'8.0 fixture',startedAt:time,finishedAt:time,ddl:'CREATE TABLE orders (id BIGINT PRIMARY KEY, status VARCHAR(32));'},right:{...endpoint('right'),version:'8.0 fixture',startedAt:time,finishedAt:time,ddl:'CREATE TABLE orders (id BIGINT PRIMARY KEY, status VARCHAR(16));'},rows:[schemaRow('status',{type:'varchar(32)',nullable:false,default:'pending',collation:'utf8mb4_bin'},{type:'varchar(16)',nullable:false,default:'pending',collation:'utf8mb4_bin'}),schemaRow('amount',{type:'decimal(20,2)',nullable:true,default:'0.00'},{type:'decimal(20,2)',nullable:true,default:null}),schemaRow('remark',{type:'text',nullable:true},null,'left-only'),schemaRow('id',{type:'bigint',nullable:false},{type:'bigint',nullable:false},'same')]};
async function dispatch(operation,args){
 switch(operation){
 case 'snapshot':return {state};
 case 'workspace':state.workspace=args;return {state};
 case 'settings':state.settings={...state.settings,...args};return {state};
 case 'schema':return {items:args.action==='databases'?[{name:endpoint(args.side).database}]:[{name:'orders',type:'BASE TABLE'}]};
 case 'compare':return {comparison};
 case 'cancel-schema':case 'invalidate-plan':return {};
 case 'sync-status':return {running:false};
 case 'sync-records':return {records:record?[record]:[]};
 case 'sync-record':if(args.id!==record?.id)throw Error('隔离记录不存在');return {record};
 case 'data-prepare':return {preparation:{fields:Object.entries(types).map(([name,type])=>({name,type,compatible:true})),keys:[{name:'PRIMARY',fields:['id']}],defaultFields:Object.keys(types),defaultKey:['id'],canCompare:true}};
 case 'data-start':{
  if(running)throw Error('隔离写入运行中');scanSettings=args;
  scanRows=dataset.filter(row=>args.filters.every(f=>matches(row,f))).map(row=>({...row,status:args.mode==='browse'?'unmatched':row.status}));
  task={id:`scan-${++serial}`,state:'complete',phase:'finished',leftScanned:scanRows.filter(r=>r.status!=='right-only').length,rightScanned:scanRows.filter(r=>r.status!=='left-only').length,batches:3,counts:{same:scanRows.filter(r=>r.status==='same').length,different:scanRows.filter(r=>r.status==='different').length,leftOnly:scanRows.filter(r=>r.status==='left-only').length,rightOnly:scanRows.filter(r=>r.status==='right-only').length},complete:true,leftStartedAt:time,leftFinishedAt:time,rightStartedAt:time,rightFinishedAt:time,consistency:'隔离模拟：两端读取不保证同一时刻'};return {taskId:task.id};
 }
 case 'data-status':currentTask(args);return {task};
 case 'data-page':{
  currentTask(args);const rows=scanRows.filter(row=>(args.status==='all'||(args.status==='differences'?['different','left-only','right-only'].includes(row.status):row.status===args.status))&&(!args.key||JSON.stringify(args.key)===JSON.stringify(row.key)));return {rows:rows.slice(args.offset,args.offset+args.limit),total:rows.length};
 }
 case 'data-detail':{
  currentTask(args);const row=scanRows.find(r=>r.id===args.rowId);if(!row)throw Error('隔离记录不存在');const fields=fieldsFor(row);
  if(args.field){const full=fields.find(f=>f.name===args.field)?.[args.side];if(!full)throw Error('隔离字段不存在');const text=full.text.slice(args.offset,args.offset+args.limit),nextOffset=args.offset+text.length;return {value:{...full,text,truncated:nextOffset<full.text.length},nextOffset};}
  return {fields:fields.map(f=>({...f,right:f.right?.truncated?{...f.right,text:f.right.text.slice(0,80)}:f.right}))};
 }
 case 'data-invalidate':if(task?.id===args.taskId){task=undefined;plan=undefined;}return {};
 case 'data-cancel':currentTask(args);task={...task,state:'cancelled',complete:false};return {};
 case 'merge-invalidate':plan=undefined;return {};
 case 'merge-plan':{
  currentTask(args);if(running)throw Error('隔离写入运行中');const sourceOnly=args.direction==='left-to-right'?'left-only':'right-only',targetOnly=sourceOnly==='left-only'?'right-only':'left-only';
  planRows=scanRows.filter(row=>row.status===sourceOnly||(args.mode!=='fill'&&row.status==='different')||(args.mode==='align'&&row.status===targetOnly)).map(row=>({id:row.id,key:row.key,action:row.status===sourceOnly?'insert':row.status===targetOnly?'delete':'update',fields:scanSettings.fields,sql:'/* 隔离示例参数化 SQL，绝不执行 */ UPDATE orders SET status = ? WHERE id = ?;',sample:{status:'pending',id:row.key[0]}}));
  plan={id:`plan-${++serial}`,taskId:task.id,direction:args.direction,mode:args.mode,left:endpoint('left'),right:endpoint('right'),key:scanSettings.key,fields:scanSettings.fields,filters:scanSettings.filters,counts:{add:planRows.filter(r=>r.action==='insert').length,modify:planRows.filter(r=>r.action==='update').length,delete:planRows.filter(r=>r.action==='delete').length},total:planRows.length,batchSize:30,readTimes:task,warnings:['隔离测试数据：没有 SQL 会被发送至数据库。每批独立提交，停止不回滚已提交批次。'],sql:['/* 隔离示例 */ UPDATE orders SET status = ? WHERE id = ?;']};return {plan};
 }
 case 'merge-page':{currentPlan(args);const rows=planRows.filter(r=>!args.action||r.action===args.action);return {rows:rows.slice(args.offset,args.offset+args.limit),total:rows.length};}
 case 'merge-execute':{
  currentPlan(args);if(running||!args.confirmed||(plan.counts.delete>0&&!args.deleteConfirmed))throw Error('隔离执行确认不完整或重复执行');running=true;record={...plan,id:`record-${++serial}`,mode:`data-${plan.mode}`,startedAt:time,status:'running',committed:0,batches:[{index:1,count:plan.total,status:'running'}],stage:'executing'};plan=undefined;return {running};
 }
 case 'merge-status':return {running,record};
 case 'merge-stop':if(record){running=false;record={...record,status:'stopped',finishedAt:time,stage:'finished',batches:record.batches.map(b=>({...b,status:'pending'}))};}return {running,record};
 default:throw Error(`隔离测试桥接未实现操作：${operation}`);
 }
}
window.qt={webChannelTransport:{fixture:true}};
window.QWebChannel=class {
 constructor(_transport,connect){let respond;connect({objects:{foundation:{response:{connect:callback=>{respond=callback;}},request:json=>{
  const {requestId,operation,args}=JSON.parse(json);calls.push({operation,args});
  Promise.resolve().then(()=>dispatch(operation,args)).then(result=>respond(JSON.stringify({requestId,ok:true,...result}))).catch(error=>respond(JSON.stringify({requestId,ok:false,error:error.message})));
 }}}});}
};
