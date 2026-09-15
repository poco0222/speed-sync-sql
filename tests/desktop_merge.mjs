// Drives the real Qt WebEngine page; invoked only by the disposable MySQL fixture.
import assert from 'node:assert/strict';
import { writeFile, mkdir } from 'node:fs/promises';
import { dirname, join } from 'node:path';
const args = Object.fromEntries(process.argv.slice(2).reduce((pairs, arg, i, all) => i % 2 ? pairs : [...pairs, [arg, all[i + 1]]], []));
const output = args['--output'];
await mkdir(dirname(output), { recursive: true });
await writeFile(output, JSON.stringify({ result: 'running', checks: [] }) + '\n');
const pause = ms => new Promise(resolve => setTimeout(resolve, ms));
let target;
for (let i = 0; i < 100; i++) {
  try { target = (await (await fetch(`http://127.0.0.1:${args['--port']}/json/list`)).json()).find(p => p.url === 'qrc:/ui/index.html'); } catch {}
  if (target) break;
  await pause(100);
}
assert(target, 'Qt debug page not available');
const ws = new WebSocket(target.webSocketDebuggerUrl);
await new Promise((resolve, reject) => { ws.onopen = resolve; ws.onerror = reject; });
let sequence = 0;
const pending = new Map();
ws.onmessage = event => {
  const msg = JSON.parse(event.data);
  if (pending.has(msg.id)) { const p = pending.get(msg.id); pending.delete(msg.id); msg.error ? p.reject(new Error(JSON.stringify(msg.error))) : p.resolve(msg.result); }
};
const command = (method, params = {}) => new Promise((resolve, reject) => { const id = ++sequence; pending.set(id, { resolve, reject }); ws.send(JSON.stringify({ id, method, params })); });
const evaluate = async expression => {
  const r = await command('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true });
  if (r.exceptionDetails) throw new Error(JSON.stringify(r.exceptionDetails));
  return r.result.value;
};
const waitFor = async expression => {
  for (let i = 0; i < 150; i++) { if (await evaluate(expression)) return; await pause(100); }
  throw new Error(`Timed out: ${expression}; text: ${await evaluate('document.body.innerText')}`);
};
const button = async (text, index = 0) => {
  assert(await evaluate(`(() => { const b=[...document.querySelectorAll('button')].filter(b=>b.innerText.replace(/\\s/g,'')===${JSON.stringify(text.replace(/\s/g,''))})[${index}]; if(!b || b.disabled) return false; b.click(); return true; })()`), `Button unavailable: ${text}`);
};
const setInput = (selector, value) => evaluate(`(() => {const e=document.querySelector(${JSON.stringify(selector)}); if(!e) throw Error('Input missing'); Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,'value').set.call(e,${JSON.stringify(value)}); e.dispatchEvent(new Event('input',{bubbles:true})); e.dispatchEvent(new Event('change',{bubbles:true})); })()`);
const checks = [];
try {
  await waitFor(`document.querySelector('#left-table')?.value==='ui_sample' && document.querySelector('#right-table')?.value==='ui_sample'`);
  assert(await evaluate(`(()=>{const t=[...document.querySelectorAll('[role=tab]')].find(t=>t.innerText.includes('数据比对'));if(!t)return false;t.click();return true;})()`));
  await waitFor(`!!document.querySelector('.data-workbench')`);
  await button('读取键与字段');
  await waitFor(`[...document.querySelectorAll('button')].some(b=>b.innerText.replace(/\\s/g,'')==='开始数据比对'&&!b.disabled)`);
  await button('开始数据比对');
  await waitFor(`[...document.querySelectorAll('button')].some(b=>b.innerText.replace(/\\s/g,'')==='预览数据写入'&&!b.disabled)`);
  await button('预览数据写入');
  await waitFor(`document.querySelector('.data-workbench .sync-panel')?.innerText.includes('参数化 SQL 样例')`);
  const preview = await evaluate(`document.querySelector('.data-workbench .sync-panel').innerText`);
  assert(preview.includes('d5_right.ui_sample') && preview.includes('补齐') && preview.includes('INSERT') && preview.includes('?'), preview);
  assert(!preview.includes('UPDATE '), preview);
  assert(preview.includes('全表') && preview.includes('左侧读取')&&preview.includes('右侧读取') && preview.includes('256'), preview);
  checks.push('real UI default fill preview includes target scope timings bounded batches and parameterized SQL');
  for (const [width,height] of [[1440,900],[1024,800]]) {
    await command('Emulation.setDeviceMetricsOverride',{width,height,deviceScaleFactor:1,mobile:false});
    const geometry=await evaluate(`(()=>{const b=[...document.querySelectorAll('button')].find(b=>b.innerText.replace(/\\s/g,'')==='执行数据写入');b.scrollIntoView({block:'center'});const r=b.getBoundingClientRect();return {x:r.x,right:r.right,y:r.y,bottom:r.bottom};})()`);
    assert(geometry.x>=0&&geometry.right<=width&&geometry.y>=0&&geometry.bottom<=height);
    await pause(100);
    const capture=await command('Page.captureScreenshot',{format:'png'});
    await writeFile(join(dirname(output),`d5-preview-${width}.png`),Buffer.from(capture.data,'base64'));
    checks.push({name:`real UI merge action reachable ${width}`,geometry});
  }
  await button('执行数据写入');
  await waitFor(`!!document.querySelector('.ant-modal-confirm')`);
  const modal = await evaluate(`document.querySelector('.ant-modal-confirm').innerText`);
  assert(modal.includes('d5_right.ui_sample')&&modal.includes('先前提交不会整体回滚')&&modal.includes('确认执行'),modal);
  assert.equal(await evaluate(`document.querySelectorAll('.ant-modal-confirm').length`),1);
  await button('确认执行');
  await waitFor(`document.querySelector('.data-workbench .sync-panel')?.innerText.includes('成功 · 已确认提交 1 / 1')`);
  checks.push('single summary confirmation commits fill and reports execution separately from verification');
  await button('按当前上下文重新比对');
  await waitFor(`[...document.querySelectorAll('button')].some(b=>b.innerText.replace(/\\s/g,'')==='预览数据写入'&&!b.disabled)`);
  await button('预览数据写入');
  await waitFor(`document.querySelector('.data-workbench .sync-panel')?.innerText.includes('无可执行变更')`);
  assert((await evaluate(`document.querySelector('.data-workbench').innerText`)).includes('所选字段与范围存在数据差异'));
  checks.push('recompare proves fill has no remaining inserts while existing differences remain');
  await evaluate(`[...document.querySelectorAll('[role=menuitem]')].find(e=>e.innerText==='执行记录').click()`);
  await waitFor(`document.body.innerText.includes('查看记录')`);
  await button('查看记录');
  await waitFor(`document.querySelector('.ant-drawer-body')?.innerText.includes('已确认提交 1 / 1')`);
  const detail = await evaluate(`document.querySelector('.ant-drawer-body').innerText`);
  assert(detail.includes('数据补齐')&&detail.includes('已提交')&&!detail.includes('INSERT INTO'),detail);
  checks.push('real durable data record exposes batch results without replayable row SQL');
  if(args['--align']==='true') {
    const {checkAlignmentUi}=await import('./desktop_align.mjs');
    await checkAlignmentUi({command,evaluate,waitFor,button,pause,checks,output});
  }
  await command('Page.reload');
  await waitFor(`document.querySelector('#left-table')?.value==='ui_sample'`);
  await evaluate(`[...document.querySelectorAll('[role=menuitem]')].find(e=>e.innerText==='执行记录').click()`);
  await waitFor(`document.body.innerText.includes('查看记录')`);
  checks.push('record survives Qt page reload');

  await evaluate(`new Promise(resolve=>new QWebChannel(qt.webChannelTransport,channel=>{
    let seq=0;const pending=new Map();
    channel.objects.foundation.response.connect(text=>{const r=JSON.parse(text),done=pending.get(r.requestId);if(done){pending.delete(r.requestId);done(r);}});
    window.d5request=(operation,args={})=>new Promise(resolve=>{const requestId='d5-test-'+(++seq);pending.set(requestId,resolve);channel.objects.foundation.request(JSON.stringify({requestId,operation,args}));});resolve(true);
  }))`);
  const request=(operation,args={})=>evaluate(`window.d5request(${JSON.stringify(operation)},${JSON.stringify(args)})`);
  const scan=async table=>{
    const pair={left:{database:'d5_left',table},right:{database:'d5_right',table}};
    const prepared=await request('data-prepare',pair);assert(prepared.ok,JSON.stringify(prepared));
    const start=await request('data-start',{...pair,key:prepared.preparation.defaultKey,fields:prepared.preparation.defaultFields,filters:[]});
    assert(start.ok,JSON.stringify(start));
    for(let i=0;i<300;i++){
      const state=await request('data-status',{taskId:start.taskId});assert(state.ok,JSON.stringify(state));
      if(state.task.state!=='running'){assert.equal(state.task.state,'complete',JSON.stringify(state));return start.taskId;}
      await pause(30);
    }
    throw Error('Data scan timeout');
  };
  const complete=async()=>{
    for(let i=0;i<300;i++){
      const state=await request('merge-status');assert(state.ok,JSON.stringify(state));
      if(!state.running){assert(state.record,JSON.stringify(state));return state.record;}
      await pause(30);
    }
    throw Error('Merge timeout');
  };
  let taskId=await scan('ui_stop');
  let planned=await request('merge-plan',{taskId,direction:'left-to-right',mode:'fill'});
  assert(planned.ok&&planned.plan.total===800,JSON.stringify(planned));
  let page=await request('merge-page',{planId:planned.plan.id,offset:256,limit:20});
  assert(page.ok&&page.rows.length===20&&page.total===800,JSON.stringify(page));
  assert(!(await request('merge-execute',{planId:planned.plan.id,confirmed:false})).ok);
  assert((await request('merge-invalidate')).ok);
  assert(!(await request('merge-execute',{planId:planned.plan.id,confirmed:true})).ok);
  checks.push('real bridge requires confirmation and rejects invalidated plan');
  planned=await request('merge-plan',{taskId,direction:'left-to-right',mode:'fill'});
  assert(planned.ok,JSON.stringify(planned));
  const startStop=await evaluate(`(async()=>{
    const start=await window.d5request('merge-execute',{planId:${JSON.stringify(planned.plan.id)},confirmed:true});
    const duplicate=await window.d5request('merge-execute',{planId:${JSON.stringify(planned.plan.id)},confirmed:true});
    const stop=await window.d5request('merge-stop');return {start,duplicate,stop};
  })()`);
  assert(startStop.start.ok&&!startStop.duplicate.ok&&startStop.stop.ok,JSON.stringify(startStop));
  const stopped=await complete();
  assert.equal(stopped.status,'stopped',JSON.stringify(stopped));
  assert(stopped.committed<stopped.total&&stopped.committed%256===0,JSON.stringify(stopped));
  assert(!stopped.batches.some(b=>b.status==='running'),JSON.stringify(stopped));
  checks.push({name:'real merge stop at batch boundary and duplicate execute rejected',committed:stopped.committed,total:stopped.total});
  const records=await request('sync-records');assert(records.ok,JSON.stringify(records));
  assert(records.records.some(r=>r.id===stopped.id&&r.status==='stopped'),JSON.stringify(records));
  const saved=await request('sync-record',{id:stopped.id});assert(saved.ok&&saved.record.status==='stopped',JSON.stringify(saved));
  assert(!JSON.stringify(saved.record).includes('password'));
  checks.push('stopped record persisted with committed and unexecuted counts');
  if(args['--align']==='true') {
    const {checkAlignmentBridge}=await import('./desktop_align.mjs');
    await checkAlignmentBridge({request,scan,complete,checks,evaluate,waitFor,command});
  }
  await writeFile(output,JSON.stringify({result:'passed',surface:'real Qt WebEngine with isolated MySQL',checks},null,2)+'\n');
  console.log(`${checks.length} real D5 desktop checks passed`);
} catch(error) {
  await writeFile(output,JSON.stringify({result:'failed',checks,error:String(error)},null,2)+'\n');
  throw error;
} finally {ws.close();}
