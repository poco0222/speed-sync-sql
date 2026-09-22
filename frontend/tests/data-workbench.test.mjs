import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
import ts from 'typescript';
import * as data from '../src/data.ts';
import * as merge from '../src/merge.ts';

function harness(idType='bigint',noKey=false){
 const slots=[],effects=[],calls=[];let cursor=0,tree,dirty=false,detailReply;
 const preparation={fields:[{name:'id',type:idType,compatible:true},{name:'note',type:'varchar',compatible:true}],keys:[{name:'PRIMARY',fields:['id']}],defaultKey:['id'],defaultFields:['id','note'],canCompare:true};
 if(noKey){preparation.keys=[];preparation.defaultKey=[];preparation.canCompare=false;}
 const task={id:'scan',state:'complete',complete:true,counts:{same:10,different:1,leftOnly:0,rightOnly:0},leftScanned:11,rightScanned:11};
 const hooks={forwardRef:f=>f,useState:initial=>{const i=cursor++;if(!(i in slots))slots[i]=typeof initial==='function'?initial():initial;return[slots[i],value=>{slots[i]=typeof value==='function'?value(slots[i]):value;dirty=true;}];},useRef:initial=>{const i=cursor++;return slots[i]??=( {current:initial});},useEffect:(fn,deps)=>{const i=cursor++,old=slots[i];if(!old||!deps||deps.some((d,j)=>d!==old.deps[j])){slots[i]={deps,cleanup:old?.cleanup};effects.push(()=>{slots[i].cleanup?.();slots[i].cleanup=fn();});}},useImperativeHandle:(ref,fn)=>{ref.current=fn();}};
 const antd=Object.fromEntries(['Alert','Button','Checkbox','Drawer','Empty','Input','Select','Space','Table','Tag'].map(name=>[name,name]));antd.Typography={Title:'Title',Text:'Text',Paragraph:'Paragraph'};antd.message={useMessage:()=>[{success(){}},null]};
 const jsx=(type,props)=>({type,props});const module={exports:{}};
 const request=async(operation,args)=>{calls.push({operation,args});if(operation==='data-prepare')return{ok:true,preparation};if(operation==='data-start')return{ok:true,taskId:'scan'};if(operation==='data-status')return{ok:true,task};if(operation==='data-page')return{ok:true,total:1,rows:[{id:'one',key:['1'],status:'different',changedFields:['note']}]};if(operation==='data-detail')return await new Promise(resolve=>{detailReply=resolve;});return{ok:true};};
 const code=ts.transpileModule(fs.readFileSync(new URL('../src/DataWorkbench.tsx',import.meta.url),'utf8'),{compilerOptions:{module:ts.ModuleKind.CommonJS,jsx:ts.JsxEmit.ReactJSX,target:ts.ScriptTarget.ES2022}}).outputText;
 vm.runInNewContext(code,{exports:module.exports,require:name=>({'react':hooks,'antd':antd,'./data':data,'./merge':merge,'./bridge':{request},'./DataMergePanel':{DataMergePanel:'MergePanel'},'react/jsx-runtime':{jsx,jsxs:jsx,Fragment:'Fragment'}}[name]),setTimeout:()=>1,clearTimeout(){}});
 const ref={};let disabled=false;
 const render=()=>{cursor=0;dirty=false;tree=module.exports.DataWorkbench({workspace:{left:{database:'a',table:'t'},right:{database:'b',table:'t'}},disabled,selected:true,beforeRead:async()=>{},onRunning(){}},ref);while(effects.length)effects.shift()();};
 const flush=async()=>{for(let i=0;i<30;i++){await Promise.resolve();if(dirty)render();}};
 const nodes=(type)=>{const found=[];const walk=n=>{if(!n||typeof n!=='object')return;if(Array.isArray(n)){n.forEach(walk);return;}if(n.type===type)found.push(n);walk(n.props?.children);walk(n.props?.extra);walk(n.props?.startAction);};walk(tree);return found;};
 const button=label=>{const n=nodes('Button').find(n=>n.props.children===label);assert.ok(n,`missing ${label}`);assert.ok(!n.props.disabled,`${label} disabled`);return n.props.onClick();};
 render();return{calls,nodes,button,flush,ref,resolveDetail:fields=>detailReply({ok:true,fields}),lock:value=>{disabled=value;render();}};
}

test('draft cancel/no-op preserve result and plan; valid apply invalidates and late detail cannot refill',async()=>{
 const h=harness();h.button('开始数据比对');await h.flush();
 assert.equal(h.calls.find(c=>c.operation==='data-page').args.status,'differences');
 const initialInvalidations=h.calls.filter(c=>c.operation==='data-invalidate').length;
 h.button('编辑比对条件');await h.flush();h.nodes('Select').find(n=>n.props['aria-label']==='参与字段').props.onChange(['id']);await h.flush();h.button('取消');await h.flush();
 assert.equal(h.calls.filter(c=>c.operation==='data-invalidate').length,initialInvalidations);
 h.button('编辑比对条件');await h.flush();h.button('应用条件');await h.flush();
 assert.equal(h.calls.filter(c=>c.operation==='data-invalidate').length,initialInvalidations);
 const table=h.nodes('Table').find(n=>n.props.rowKey==='id');table.props.expandable.onExpand(true,table.props.dataSource[0]);await h.flush();
 h.button('编辑比对条件');await h.flush();h.nodes('Select').find(n=>n.props['aria-label']==='参与字段').props.onChange(['id']);await h.flush();h.button('应用条件');await h.flush();
 h.resolveDetail([{name:'note',changed:true}]);await h.flush();
 assert.equal(h.calls.filter(c=>c.operation==='data-invalidate').length,initialInvalidations+1);
 assert.equal(h.nodes('Table').some(n=>n.props.rowKey==='id'),false);
 assert.equal(h.nodes('MergePanel')[0].props.taskId,'');
 h.button('开始数据比对');await h.flush();assert.deepEqual(Array.from(h.calls.filter(c=>c.operation==='data-start').at(-1).args.fields),['id']);
});

test('draft malformed numeric filter does not invalidate; activity disables settings',async()=>{
 const h=harness();h.button('编辑比对条件');await h.flush();h.button('添加 AND 条件');await h.flush();h.button('应用条件');await h.flush();
 assert.ok(h.nodes('Alert').some(n=>n.props.title==='整数筛选值格式无效'));
 assert.equal(h.calls.filter(c=>c.operation==='data-invalidate').length,0);
 h.lock(true);assert.equal(h.nodes('Button').find(n=>n.props.children==='应用条件').props.disabled,true);
});

test('closing details preserves status, exact-key location and page without invalidating write scope',async()=>{
 const h=harness();h.button('开始数据比对');await h.flush();
 h.nodes('Select').find(n=>n.props['aria-label']==='数据状态过滤').props.onChange('all');await h.flush();
 h.nodes('Input').find(n=>n.props['aria-label']==='定位键 id').props.onChange({target:{value:'9007199254740993'}});await h.flush();h.button('完整键定位');await h.flush();
 let table=h.nodes('Table').find(n=>n.props.rowKey==='id');table.props.pagination.onChange(2);await h.flush();table=h.nodes('Table').find(n=>n.props.rowKey==='id');
 table.props.expandable.onExpand(true,table.props.dataSource[0]);await h.flush();h.resolveDetail([]);await h.flush();table=h.nodes('Table').find(n=>n.props.rowKey==='id');table.props.expandable.onExpand(false,table.props.dataSource[0]);await h.flush();
 assert.equal(h.nodes('Select').find(n=>n.props['aria-label']==='数据状态过滤').props.value,'all');
 assert.equal(h.nodes('Table').find(n=>n.props.rowKey==='id').props.pagination.current,2);
 const page=h.calls.filter(c=>c.operation==='data-page').at(-1).args;
 assert.equal(page.offset,30);assert.deepEqual(Array.from(page.key),['9007199254740993']);
 assert.equal(h.calls.filter(c=>c.operation==='data-invalidate').length,0);
});

test('out-of-range drafts preserve scan rows and plan identity; valid boundary still applies',async()=>{
 for(const [type,invalid,valid] of [['tinyint','128','127'],['bigint unsigned','18446744073709551616','18446744073709551615'],['decimal(5,2) unsigned','1000.00','00999.99'],['bit(9)','0200','01ff']]){
  const h=harness(type);h.button('开始数据比对');await h.flush();
  await h.flush();
  const rows=h.nodes('Table').find(n=>n.props.rowKey==='id').props.dataSource;
  h.button('编辑比对条件');await h.flush();h.button('添加 AND 条件');await h.flush();
  const input=()=>h.nodes('Input').find(n=>n.props['aria-label']==='筛选值 1');
  input().props.onChange({target:{value:invalid}});await h.flush();h.button('应用条件');await h.flush();
  assert.ok(h.nodes('Alert').some(n=>/超出/.test(n.props.title)),type);
  assert.equal(h.nodes('MergePanel')[0].props.taskId,'scan',type);
  assert.equal(h.nodes('Table').find(n=>n.props.rowKey==='id').props.dataSource,rows,type);
  assert.equal(h.calls.filter(c=>c.operation==='data-invalidate').length,0,type);
  input().props.onChange({target:{value:valid}});await h.flush();h.button('应用条件');await h.flush();
  assert.equal(h.calls.filter(c=>c.operation==='data-invalidate').length,1,type);
  assert.equal(h.nodes('MergePanel')[0].props.taskId,'',type);
 }
});

test('one start prepares and scans once with reliable defaults',async()=>{
 const h=harness();h.button('开始数据比对');h.button('开始数据比对');await h.flush();await h.flush();
 assert.deepEqual(h.calls.filter(c=>['data-prepare','data-start'].includes(c.operation)).map(c=>c.operation),['data-prepare','data-start']);
 assert.deepEqual(Array.from(h.calls.find(c=>c.operation==='data-start').args.key),['id']);
});
test('merge projections preserve defaults and expose destructive align',()=>{
 const task={counts:{same:10,different:2,leftOnly:3,rightOnly:4}};
 assert.deepEqual(merge.mergeCounts(task,'fill'),{add:3,modify:0,delete:0,keep:16});
 assert.deepEqual(merge.mergeCounts(task,'merge'),{add:3,modify:2,delete:0,keep:14});
 assert.deepEqual(merge.mergeCounts(task,'align'),{add:3,modify:2,delete:4,keep:10});
 assert.equal(merge.mergeEffect('different','fill'),'保持目标原值');
 assert.equal(merge.mergeEffect('right-only','align'),'删除整行');
});

test('missing key blocks comparison until explicit independent browse',async()=>{
 const h=harness('bigint',true);h.button('开始数据比对');await h.flush();
 assert.equal(h.calls.filter(c=>c.operation==='data-start').length,0);
 assert.ok(h.nodes('Alert').some(n=>n.props.title.includes('无法比对差异')));
 h.button('开始独立浏览');await h.flush();
 assert.equal(h.calls.find(c=>c.operation==='data-start').args.mode,'browse');
 assert.equal(h.nodes('MergePanel')[0].props.browse,true);
});
