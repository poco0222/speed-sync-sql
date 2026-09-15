import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import { mkdir, writeFile } from 'node:fs/promises';
import { dirname, join } from 'node:path';
const args = Object.fromEntries(process.argv.slice(2).reduce((pairs,arg,i,all)=>i%2?pairs:[...pairs,[arg,all[i+1]]],[]));
const output=args['--output'];
await mkdir(dirname(output),{recursive:true});
const pause=ms=>new Promise(resolve=>setTimeout(resolve,ms));
let target;
for(let i=0;i<100;i++) {
  try { target=(await(await fetch(`http://127.0.0.1:${args['--port']}/json/list`)).json()).find(p=>p.url==='qrc:/ui/index.html'); } catch {}
  if(target) break;
  await pause(100);
}
assert(target,'Qt page unavailable');
const ws=new WebSocket(target.webSocketDebuggerUrl);
await new Promise((resolve,reject)=>{ws.onopen=resolve;ws.onerror=reject;});
let sequence=0;
const pending=new Map();
ws.onmessage=event=>{const msg=JSON.parse(event.data);if(pending.has(msg.id)){const p=pending.get(msg.id);pending.delete(msg.id);msg.error?p.reject(new Error(JSON.stringify(msg.error))):p.resolve(msg.result);}};
const command=(method,params={})=>new Promise((resolve,reject)=>{const id=++sequence;pending.set(id,{resolve,reject});ws.send(JSON.stringify({id,method,params}));});
const evaluate=async expression=>{const r=await command('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true,userGesture:true});if(r.exceptionDetails)throw Error(JSON.stringify(r.exceptionDetails));return r.result.value;};
const waitFor=async expression=>{for(let i=0;i<200;i++){if(await evaluate(expression))return;await pause(100);}throw Error(`Timed out: ${expression}; ${await evaluate('document.body.innerText')}`);};
const button=async text=>assert(await evaluate(`(()=>{const b=[...document.querySelectorAll('button')].find(b=>b.innerText.replace(/\\s/g,'')===${JSON.stringify(text.replace(/\s/g,''))});if(!b||b.disabled)return false;b.click();return true;})()`),`Button unavailable: ${text}`);
const checks=[];
const record=name=>checks.push(name);
try {
  await waitFor('!!window.qt && !!window.QWebChannel');
  await evaluate(`new Promise(resolve=>new QWebChannel(window.qt.webChannelTransport,channel=>{
    let seq=0;const pending=new Map();
    channel.objects.foundation.response.connect(text=>{const r=JSON.parse(text);const done=pending.get(r.requestId);if(done){pending.delete(r.requestId);done(r);}});
    window.d4request=(operation,args={})=>new Promise(resolve=>{const requestId='d4-test-'+(++seq);pending.set(requestId,resolve);channel.objects.foundation.request(JSON.stringify({requestId,operation,args}));});resolve(true);
  }))`);
  const request=(operation,args={})=>evaluate(`window.d4request(${JSON.stringify(operation)},${JSON.stringify(args)})`);
  const pair=table=>({left:{database:'d4_left',table},right:{database:'d4_right',table}});
  const prepare=async table=>{const r=await request('data-prepare',pair(table));assert(r.ok,JSON.stringify(r));return r.preparation;};
  const complete=async taskId=>{
    for(let i=0;i<300;i++) {
      const r=await request('data-status',{taskId});assert(r.ok,JSON.stringify(r));
      if(r.task.state!=='running'){assert.equal(r.task.state,'complete',JSON.stringify(r));assert(r.task.complete);return r.task;}
      await pause(30);
    }
    throw Error('Scan timeout');
  };
  const scan=async(table,options={})=>{
    const prep=await prepare(table);
    const r=await request('data-start',{...pair(table),key:prep.defaultKey,fields:prep.defaultFields,filters:[],...options});
    assert(r.ok,JSON.stringify(r));return {taskId:r.taskId,task:await complete(r.taskId),prep};
  };
  const prep=await prepare('sample');
  assert.deepEqual(prep.defaultKey,['id','sub']);
  assert(prep.defaultFields.includes('amount')&&prep.defaultFields.includes('doc'));
  record('composite primary key and compatible typed fields');
  const sample=await scan('sample');
  assert.deepEqual(sample.task.counts,{same:1,different:2,leftOnly:1,rightOnly:1});
  assert(sample.task.leftStartedAt&&sample.task.rightStartedAt&&sample.task.consistency);
  let page=await request('data-page',{taskId:sample.taskId,status:'all',offset:0,limit:2});
  assert(page.ok&&page.rows.length===2&&page.total===5,JSON.stringify(page));
  const located=await request('data-page',{taskId:sample.taskId,key:['9007199254740993','1'],offset:0,limit:10});
  assert(located.ok&&located.rows.length===1,JSON.stringify(located));
  assert.equal(located.rows[0].status,'same');
  const detail=await request('data-detail',{taskId:sample.taskId,rowId:located.rows[0].id});
  assert(detail.ok,JSON.stringify(detail));
  const field=name=>detail.fields.find(f=>f.name===name);
  assert.equal(field('id').left.text,'9007199254740993');
  assert.equal(field('amount').left.text,'12345678901234567890123.123456789012345');
  assert(field('stamp').left.text.includes('123456'));
  assert(field('doc').left.text.includes('9007199254740993'));
  assert.equal(field('note').left.text,'中文\0😀');
  assert.equal(field('raw_value').left.text,'00FF10');
  record('precise BIGINT DECIMAL JSON datetime and result paging');
  const changed=await request('data-page',{taskId:sample.taskId,key:['9007199254740994','1'],offset:0,limit:10});
  assert(changed.rows[0].changedFields.includes('note')&&changed.rows[0].changedFields.includes('stamp'));
  const nullDetail=await request('data-detail',{taskId:sample.taskId,rowId:changed.rows[0].id});
  const note=nullDetail.fields.find(f=>f.name==='note');assert(note.left.isNull&&!note.right.isNull&&note.right.text==='');
  record('NULL versus empty and microsecond differences');
  const longPage=await request('data-page',{taskId:sample.taskId,key:['9007199254740996','1'],offset:0,limit:10});
  assert(longPage.rows[0].changedFields.includes('note'));
  let full='',offset=0;
  for(let i=0;i<100;i++) {
    const chunk=await request('data-detail',{taskId:sample.taskId,rowId:longPage.rows[0].id,field:'note',side:'left',offset,limit:1024});
    assert(chunk.ok,JSON.stringify(chunk));full+=chunk.value.text;
    if(chunk.nextOffset===null||chunk.nextOffset===undefined)break;
    assert(chunk.nextOffset>offset);offset=chunk.nextOffset;
  }
  assert.equal(full,'长😀'.repeat(12000)+'左');
  record('long multibyte value complete retrieval and tail difference');
  const filtered=await scan('sample',{filters:[{field:'id',op:'=',value:'9007199254740993'}]});
  assert.equal(filtered.task.counts.same,1);assert.equal(filtered.task.leftScanned,1);
  const empty=await scan('sample',{filters:[{field:'id',op:'<',value:'0'}]});
  assert.equal(empty.task.leftScanned,0);assert.equal(empty.task.rightScanned,0);
  const bad=await request('data-start',{...pair('sample'),key:prep.defaultKey,fields:prep.defaultFields,filters:[{field:'id',op:'OR 1=1',value:'0'}]});
  if(bad.ok){let state;for(let i=0;i<100;i++){state=await request('data-status',{taskId:bad.taskId});if(state.task?.state!=='running')break;await pause(30);}assert.equal(state.task?.state,'failed','Invalid operator accepted');assert(!state.task.complete);}
  record('shared bound predicates empty range and rejected injection');
  const unique=await prepare('unique_key');assert.deepEqual(unique.defaultKey,['code']);
  const nullable=await prepare('nullable_key');assert(!nullable.canCompare&&nullable.defaultKey.length===0);
  const noKey=await prepare('no_key');assert(!noKey.canCompare);
  const browse=await scan('no_key',{mode:'browse',key:[]});
  const browsed=await request('data-page',{taskId:browse.taskId,side:'left',offset:0,limit:10});
  assert(browsed.ok&&browsed.rows.length===2,JSON.stringify(browsed));
  assert(browsed.rows.every(r=>!['same','different','left-only','right-only'].includes(r.status)));
  record('non-null unique key nullable rejection and unpaired browsing');
  const batched=await scan('batched');
  assert.deepEqual(batched.task.counts,{same:2492,different:7,leftOnly:1,rightOnly:1});
  assert(batched.task.batches>2,JSON.stringify(batched.task));
  record('2500 rows multiple batches and boundary differences');
  const types=await scan('types',{filters:[{field:'bits',op:'=',value:'0101'}]});
  assert.equal(types.task.counts.same,1);
  const typePage=await request('data-page',{taskId:types.taskId,offset:0,limit:10});
  const typeDetail=await request('data-detail',{taskId:types.taskId,rowId:typePage.rows[0].id});
  const typed=name=>typeDetail.fields.find(f=>f.name===name).left.text;
  assert.equal(typed('bits'),'0101');assert(typed('ts').includes('654321'));assert.equal(typed('tm'),'-12:34:56.123456');
  record('BIT exact HEX predicate and TIMESTAMP TIME precision');
  const adjacent=await scan('types',{filters:[{field:'id',op:'=',value:'2'}]});
  assert.equal(adjacent.task.counts.different,1);
  const adjacentPage=await request('data-page',{taskId:adjacent.taskId,offset:0,limit:10});
  assert(adjacentPage.rows[0].changedFields.includes('flt')&&adjacentPage.rows[0].changedFields.includes('dbl'));
  record('adjacent FLOAT and DOUBLE retain distinct exact comparison values');
  const textKey=await scan('text_key');
  assert.deepEqual(textKey.task.counts,{same:0,different:2,leftOnly:0,rightOnly:0});
  const textSubset=await scan('text_key',{fields:['value']});
  assert.deepEqual(textSubset.task.counts,{same:2,different:0,leftOnly:0,rightOnly:0});
  record('case and PAD SPACE key identity follow verified MySQL collation');
  const cancelPrep=await prepare('batched');
  const cancelStart=await request('data-start',{...pair('batched'),key:cancelPrep.defaultKey,fields:cancelPrep.defaultFields,filters:[]});
  assert(cancelStart.ok);
  const cancelled=await request('data-cancel',{taskId:cancelStart.taskId});assert(cancelled.ok,JSON.stringify(cancelled));
  let stopped;
  for(let i=0;i<100;i++){stopped=await request('data-status',{taskId:cancelStart.taskId});if(stopped.task?.state!=='running')break;await pause(30);}
  assert.equal(stopped.task?.state,'cancelled',JSON.stringify(stopped));assert(!stopped.task.complete);
  record('running scan cancellation stays incomplete');
  await request('data-invalidate',{});
  const stale=await request('data-page',{taskId:batched.taskId,offset:0,limit:10});assert(!stale.ok);
  const missing=await request('data-prepare',pair('missing_table'));assert(!missing.ok);
  record('invalidation rejects stale results and missing table is not empty');
  const state=(await request('snapshot')).state;
  const original=state.connections.find(c=>c.id===state.right);
  assert((await request('save',{...original,user:'d4_denied',password:''})).ok);
  const denied=await request('data-prepare',pair('sample'));assert(!denied.ok,'Denied metadata misreported as complete');
  assert((await request('save',{...original,password:''})).ok);
  record('permission failure is explicit and connection restoration works');
  await command('Page.reload');
  await waitFor(`document.querySelector('#left-table')?.value==='sample'`);
  assert(await evaluate(`(()=>{const t=[...document.querySelectorAll('[role=tab]')].find(t=>t.innerText.includes('数据比对'));if(!t)return false;t.click();return true;})()`));
  await waitFor(`!!document.querySelector('.data-workbench')`);
  assert(!(await evaluate(`document.querySelector('.data-workbench').innerText`)).includes('扫描完成'));
  await button('读取键与字段');
  await waitFor(`[...document.querySelectorAll('button')].some(b=>b.innerText.includes('开始数据比对')&&!b.disabled)`);
  await button('开始数据比对');
  await waitFor(`[...document.querySelectorAll('button')].some(b=>b.innerText.includes('查看左右原值'))`);
  assert(!await evaluate(`document.querySelector('.data-workbench .ant-alert-error')!==null`),'Completed scan retained a premature paging error');
  record('real UI explicitly prepares and scans shared table selection');
  for(const [width,height] of [[1440,900],[1024,800]]) {
    await command('Emulation.setDeviceMetricsOverride',{width,height,deviceScaleFactor:1,mobile:false});
    await pause(150);
    assert(await evaluate(`(()=>{const b=[...document.querySelectorAll('button')].find(b=>b.innerText.includes('开始数据比对'));if(!b)return false;const r=b.getBoundingClientRect();return r.left>=0&&r.right<=innerWidth&&r.top>=0&&r.bottom<=innerHeight;})()`));
    const screenshot=await command('Page.captureScreenshot',{format:'png'});
    await writeFile(join(dirname(output),`d4-${width}.png`),Buffer.from(screenshot.data,'base64'));
  }
  record('real UI 1440 and 1024 main action reachable');
  await evaluate(`(()=>{[...document.querySelectorAll('button')].find(b=>b.innerText.includes('查看左右原值')).click();})()`);
  await waitFor(`!!document.querySelector('.ant-drawer-body')`);
  await evaluate(`(()=>{const c=document.querySelector('.ant-drawer-body input[type=checkbox]');if(c?.checked)c.click();})()`);
  await waitFor(`document.querySelector('.ant-drawer-body')?.innerText.includes('amount')`);
  await command('Emulation.setFocusEmulationEnabled',{enabled:true});
  const expectedCopy=await evaluate(`(()=>{const row=[...document.querySelectorAll('.ant-drawer-body tr')].find(r=>r.querySelector('strong')?.innerText==='id');const text=row.querySelector('pre').innerText;row.querySelector('button').click();return text;})()`);
  await waitFor(`document.body.innerText.includes('已复制完整原文')`);
  assert.equal(execFileSync('/usr/bin/pbpaste',{encoding:'utf8'}),expectedCopy);
  record('real UI clipboard copy preserves exact BIGINT');
  record('real UI aligned field detail');
  await evaluate(`document.querySelector('.ant-drawer-close').click()`);
  await button('设置');
  const selectOption=async(label,text)=>{
    const rect=await evaluate(`(()=>{const el=document.querySelector('[aria-label="'+${JSON.stringify(label)}+'"]');const r=el.getBoundingClientRect();return {x:r.x+5,y:r.y+5};})()`);
    await command('Input.dispatchMouseEvent',{type:'mousePressed',...rect,button:'left',clickCount:1});
    await command('Input.dispatchMouseEvent',{type:'mouseReleased',...rect,button:'left',clickCount:1});
    await waitFor(`[...document.querySelectorAll('.ant-select-item-option-content')].some(e=>e.innerText===${JSON.stringify(text)})`);
    await evaluate(`[...document.querySelectorAll('.ant-select-item-option-content')].find(e=>e.innerText===${JSON.stringify(text)}).click()`);
  };
  await selectOption('主题','深色');await selectOption('密度','紧凑');await button('保存设置');
  await waitFor(`document.documentElement.dataset.theme==='dark'`);
  await waitFor(`[...document.querySelectorAll('.ant-modal-wrap')].every(e=>getComputedStyle(e).display==='none')`);
  await pause(600);
  const dark=await command('Page.captureScreenshot',{format:'png'});
  await writeFile(join(dirname(output),'d4-1024-dark.png'),Buffer.from(dark.data,'base64'));
  record('real UI dark and compact settings');

  await writeFile(output,JSON.stringify({result:'passed',checks},null,2)+'\n');
} catch(error) {
  await writeFile(output,JSON.stringify({result:'failed',checks,error:String(error)},null,2)+'\n');
  throw error;
} finally {ws.close();}
