import test from 'node:test';
import assert from 'node:assert/strict';
import {canPlanMerge,isMergeRecord,mergeSummary} from '../src/merge.ts';
test('only complete keyed scan can make a write plan',()=>{
 const task={id:'scan',state:'complete',complete:true};
 assert.equal(canPlanMerge(task,false),true);
 for(const state of ['running','failed','cancelled'])assert.equal(canPlanMerge({...task,state},false),false);
 assert.equal(canPlanMerge({...task,complete:false},false),false);
 assert.equal(canPlanMerge(task,true),false);
 assert.equal(canPlanMerge(undefined,false),false);
});
test('data records need batches and never require DDL steps',()=>{
 const record={id:'r',mode:'data-fill',startedAt:'now',status:'passed',left:{},right:{},batches:[],committed:2,total:2};
 assert.equal(isMergeRecord(record),true);
 assert.equal(isMergeRecord({...record,mode:'schema'}),false);
 assert.equal(isMergeRecord({...record,batches:undefined}),false);
 assert.equal(isMergeRecord(null),false);
 assert.equal(mergeSummary({counts:{add:2,modify:3,delete:0}}),'新增 2 · 更新 3 · 删除 0');
});

test('alignment record and destructive count remain visible',()=>{
 const record={id:'r',mode:'data-align',startedAt:'now',status:'unknown',left:{},right:{},batches:[],committed:256,total:600};
 assert.equal(isMergeRecord(record),true);
 assert.equal(mergeSummary({counts:{add:1,modify:2,delete:600}}),'新增 1 · 更新 2 · 删除 600');
});

// Drive real component hooks without a browser or a live database bridge.
import {readFileSync} from 'node:fs';
import {runInNewContext} from 'node:vm';
import ts from 'typescript';
import * as mergeRules from '../src/merge.ts';
import {syncLabels} from '../src/sync.ts';
test('closing preview preserves polling, write lock and the visible stop action',async()=>{
 const slots=[],effects=[],calls=[],locks=[],timers=[];let cursor=0,active=true;
 const hooks={
  useState(initial){const i=cursor++;if(!(i in slots))slots[i]=initial;return [slots[i],value=>{slots[i]=value;}];},
  useRef(initial){const i=cursor++;return slots[i]??(slots[i]={current:initial});},
  useEffect(fn,deps){const i=cursor++,prior=slots[i];if(!prior||deps.some((v,j)=>v!==prior.deps[j])){prior?.cleanup?.();effects.push(()=>{slots[i]={deps,cleanup:fn()};});}}
 };
 const jsx=(type,props)=>({type,props}),exports={};
 const components=Object.fromEntries(['Alert','Button','Checkbox','Collapse','Drawer','Select','Space','Table'].map(name=>[name,name]));
 components.Typography={Title:'Title',Text:'Text',Paragraph:'Paragraph'};
 components.Modal={useModal:()=>[{},null]};
 const request=async(op,args)=>{calls.push({op,args});return {ok:true,running:active};};
 const code=ts.transpileModule(readFileSync(new URL('../src/DataMergePanel.tsx',import.meta.url),'utf8'),{compilerOptions:{jsx:ts.JsxEmit.ReactJSX,module:ts.ModuleKind.CommonJS,target:ts.ScriptTarget.ES2022}}).outputText;
 runInNewContext(code,{exports,require:name=>({'react':hooks,'react/jsx-runtime':{jsx,jsxs:jsx},antd:components,'./bridge':{request},'./sync':{syncLabels},'./merge':mergeRules})[name],setTimeout:fn=>{timers.push(fn);return timers.length;},clearTimeout:()=>{}});
 const render=()=>{cursor=0;const tree=exports.DataMergePanel({task:{complete:true,state:'complete'},taskId:'scan',browse:false,disabled:false,onRunning:value=>locks.push(value),onRecompare:()=>{}});while(effects.length)effects.shift()();return tree;};
 const settle=()=>new Promise(resolve=>setImmediate(resolve));
 const nodes=(tree,type)=>{const result=[];function walk(node){if(!node||typeof node!=='object')return;if(Array.isArray(node)){node.forEach(walk);return;}if(node.type===type)result.push(node);walk(node.props?.children);}walk(tree);return result;};
 render();await settle();let tree=render();
 const drawer=()=>nodes(tree,'Drawer')[0];
 assert.equal(locks.at(-1),true);assert.equal(drawer().props.open,false);
 nodes(tree,'Button').find(n=>n.props.children==='打开写入预览').props.onClick();tree=render();assert.equal(drawer().props.open,true);
 drawer().props.onClose();tree=render();assert.equal(drawer().props.open,false);assert.equal(locks.at(-1),true);
 nodes(tree,'Button').find(n=>n.props.children==='打开写入预览').props.onClick();tree=render();assert.equal(drawer().props.open,true);
 assert.equal(calls.filter(c=>c.op==='merge-invalidate').length,1);
 assert.equal(calls.filter(c=>['merge-plan','merge-execute'].includes(c.op)).length,0);
 drawer().props.onClose();tree=render();
 const toolbar=nodes(tree,'Space').find(n=>n.props.className==='merge-toolbar');
 nodes(toolbar,'Button').find(n=>n.props.children==='请求边界停止').props.onClick();await settle();assert.equal(calls.filter(c=>c.op==='merge-stop').length,1);
 active=false;await timers.shift()();await settle();render();assert.equal(locks.at(-1),false);
});
