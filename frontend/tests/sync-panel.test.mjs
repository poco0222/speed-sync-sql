import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
import ts from 'typescript';
import * as sync from '../src/sync.ts';

test('one preview builds the selected scope, preserves polling when closed and blocks empty execution',async()=>{
 const slots=[],effects=[],requests=[],timers=[],scopes=[],busy=[];let cursor=0,tree,confirm,resolvePlan;
 const hooks={useState:initial=>{const i=cursor++;if(!(i in slots))slots[i]=initial;return[slots[i],v=>{slots[i]=v;}];},useRef:initial=>{const i=cursor++;return slots[i]??={current:initial};},useEffect:(fn,deps)=>{const i=cursor++,old=slots[i];if(!old||deps.some((d,j)=>d!==old.deps[j])){slots[i]={deps};effects.push(fn);}}};
 const endpoint={name:'target',database:'db',table:'t'},plan={id:'plan',direction:'left-to-right',left:endpoint,right:endpoint,steps:[],sql:'',counts:{add:0,modify:0,delete:0}};
 const antd=Object.fromEntries(['Alert','Button','Segmented','Space','Table'].map(name=>[name,name]));antd.Typography={Text:'Text',Paragraph:'Paragraph'};antd.Modal={useModal:()=>[{confirm:value=>{confirm=value;}},null]};
 const jsx=(type,props)=>({type,props}),module={exports:{}};
 const code=ts.transpileModule(fs.readFileSync(new URL('../src/SyncPanel.tsx',import.meta.url),'utf8'),{compilerOptions:{module:ts.ModuleKind.CommonJS,jsx:ts.JsxEmit.ReactJSX,target:ts.ScriptTarget.ES2022}}).outputText;
 vm.runInNewContext(code,{exports:module.exports,require:name=>({'react':hooks,'antd':antd,'./sync':sync,'./bridge':{request:async(operation,args)=>{requests.push({operation,args});return operation==='plan-sync'?new Promise(resolve=>{resolvePlan=()=>resolve({ok:true,plan});}):{ok:true,running:false};}},'react/jsx-runtime':{jsx,jsxs:jsx}}[name]),setTimeout:fn=>{timers.push(fn);return timers.length;},clearTimeout(){}});
 const props={target:'target / db / t',strategy:'fill',onStrategyChange:value=>{scopes.push(value);props.strategy=value;},children:jsx('div',{id:'preserved-results'}),comparison:{rows:[{category:'columns',name:'id'}]},selected:['columns:id'],disabled:false,onRunning:value=>busy.push(value),onComparison(){},action:jsx('Button',{children:'开始比对'})};
 const render=()=>{cursor=0;tree=module.exports.SyncPanel(props);while(effects.length)effects.shift()();};
 const nodes=type=>{const found=[];const walk=n=>{if(!n||typeof n!=='object')return;if(Array.isArray(n)){n.forEach(walk);return;}if(n.type===type)found.push(n);walk(n.props?.children);};walk(tree);return found;};
 const button=text=>nodes('Button').find(n=>n.props.children===text);
 const settle=async()=>{await new Promise(resolve=>setImmediate(resolve));render();};
 render();await settle();assert.equal(nodes('section').find(n=>n.props.className==='inline-preview').props.hidden,true);assert.ok(button('开始比对'));assert.ok(nodes('Text').some(n=>n.props.title==='target / db / t'));assert.equal(nodes('Segmented')[0].props.value,'fill');
 const preview=button('预览同步');preview.props.onClick();preview.props.onClick();assert.equal(requests.filter(r=>r.operation==='plan-sync').length,1);assert.equal(busy.at(-1),true);timers.shift()();await settle();assert.equal(busy.at(-1),true);resolvePlan();await settle();assert.equal(busy.at(-1),false);
 assert.equal(nodes('section').find(n=>n.props.className==='inline-preview').props.hidden,false);assert.equal(button('执行到目标').props.disabled,true);assert.ok(nodes('div').some(n=>n.props.id==='preserved-results'));assert.equal(nodes('Drawer').length,0);
 button('执行到目标').props.onClick();assert.equal(confirm,undefined);
 assert.equal(requests.find(r=>r.operation==='plan-sync').args.direction,'left-to-right');
 button('返回差异').props.onClick();render();assert.equal(nodes('section').find(n=>n.props.className==='inline-preview').props.hidden,true);
 const count=requests.filter(r=>r.operation==='sync-status').length;timers.shift()();await settle();assert.equal(requests.filter(r=>r.operation==='sync-status').length,count+1);
 nodes('Segmented')[0].props.onChange('align');assert.deepEqual(scopes,['align']);render();render();button('预览同步').props.onClick();resolvePlan();await settle();assert.equal(requests.filter(r=>r.operation==='plan-sync').at(-1).args.alignAll,true);
});
