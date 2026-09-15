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
  await button('开始比对');
  await waitFor(`document.body.innerText.includes('存在结构差异')`);
  assert(await evaluate(`[...document.querySelectorAll('button')].find(b=>b.innerText==='预览选中对象 SQL').disabled`));
  checks.push('initial selection empty');
  await evaluate(`(() => {const row=[...document.querySelectorAll('.ant-table-row')].find(r=>r.innerText.includes('added'));if(!row)throw Error('Added row absent');row.querySelector('input[type=checkbox]').click();})()`);
  await waitFor(`document.querySelector('.sync-panel').innerText.includes('已选 1 项')`);
  await button('预览选中对象 SQL');
  await waitFor(`document.body.innerText.includes('只读 SQL 预览')`);
  const sql = await evaluate(`document.querySelector('.sync-panel > pre.sync-sql').textContent`);
  assert(sql.includes('ALTER TABLE') && sql.includes('added') && sql.includes('d3_right'));
  assert(await evaluate(`!document.querySelector('.sync-panel textarea') && !document.querySelector('.sync-panel [contenteditable=true]')`));
  checks.push('real selected object SQL preview');
  for (const [width, height] of [[1440,900],[1024,800]]) {
    await command('Emulation.setDeviceMetricsOverride', { width, height, deviceScaleFactor: 1, mobile: false });
    const geometry = await evaluate(`(() => {const b=[...document.querySelectorAll('button')].find(b=>b.innerText==='执行到右侧');b.scrollIntoView({block:'center'});const r=b.getBoundingClientRect();return {x:r.x,right:r.right,y:r.y,bottom:r.bottom};})()`);
    assert(geometry.x>=0 && geometry.right<=width && geometry.y>=0 && geometry.bottom<=height);
    await pause(100);
    const capture = await command('Page.captureScreenshot', { format:'png' });
    await writeFile(join(dirname(output),`d3-preview-${width}.png`),Buffer.from(capture.data,'base64'));
    checks.push({name:`preview action reachable ${width}`,geometry});
  }
  await button('设置');
  for (const [label, option] of [['主题','深色'],['密度','紧凑']]) {
    await evaluate(`document.querySelector('input[aria-label="${label}"]').focus()`);
    await command('Input.dispatchKeyEvent', {type:'keyDown',key:'ArrowDown',code:'ArrowDown',windowsVirtualKeyCode:40});
    await command('Input.dispatchKeyEvent', {type:'keyUp',key:'ArrowDown',code:'ArrowDown',windowsVirtualKeyCode:40});
    const choice = `[...document.querySelectorAll('.ant-select-item-option-content')].find(e=>e.innerText===${JSON.stringify(option)} && e.getClientRects().length)`;
    await waitFor(`!!(${choice})`);
    await evaluate(`(${choice}).click()`);
  }
  await button('保存设置');
  await waitFor(`document.documentElement.dataset.theme==='dark' && !document.querySelector('.ant-modal-wrap:not([style*="display: none"])')`);
  const darkGeometry = await evaluate(`(() => {const b=[...document.querySelectorAll('button')].find(b=>b.innerText==='执行到右侧');b.scrollIntoView({block:'center'});const r=b.getBoundingClientRect();return {x:r.x,right:r.right,y:r.y,bottom:r.bottom};})()`);
  assert(darkGeometry.x>=0 && darkGeometry.right<=1024 && darkGeometry.y>=0 && darkGeometry.bottom<=800);
  await pause(200);
  const darkCapture = await command('Page.captureScreenshot',{format:'png'});
  await writeFile(join(dirname(output),'d3-preview-dark-compact-1024.png'),Buffer.from(darkCapture.data,'base64'));
  checks.push({name:'dark compact preview action reachable 1024',geometry:darkGeometry});
  await button('执行到右侧');
  await waitFor(`document.querySelector('.ant-modal-confirm')?.innerText.includes('确认执行到右侧')`);
  assert((await evaluate(`document.querySelector('.ant-modal-confirm').innerText`)).includes('多条 DDL 不保证整体回滚'));
  await evaluate(`document.querySelector('.ant-modal-confirm .ant-btn-primary').click()`);
  await waitFor(`document.querySelector('.sync-panel')?.innerText.includes('结构复核：结构一致') || document.querySelector('.sync-panel')?.innerText.includes('结构复核：相同')`);
  checks.push('real summary confirmation execution and structure review');
  await evaluate(`[...document.querySelectorAll('[role=menuitem]')].find(e=>e.innerText==='执行记录').click()`);
  await waitFor(`document.body.innerText.includes('查看记录')`);
  await button('查看记录');
  await waitFor(`document.querySelector('.ant-drawer-body')?.innerText.includes('两端原始结构定义')`);
  const detail = await evaluate(`document.querySelector('.ant-drawer-body').innerText`);
  assert(detail.includes('added') && detail.includes('ALTER TABLE') && detail.includes('d3_right'));
  checks.push('durable execution detail includes SQL and original definitions');
  await command('Page.reload');
  await waitFor(`document.querySelector('#left-table')?.value==='ui_sample'`);
  await evaluate(`[...document.querySelectorAll('[role=menuitem]')].find(e=>e.innerText==='执行记录').click()`);
  await waitFor(`document.body.innerText.includes('查看记录')`);
  checks.push('record remains available after page reload');
  await writeFile(output,JSON.stringify({result:'passed',surface:'real Qt WebEngine with isolated MySQL',checks},null,2)+'\n');
  console.log(`${checks.length} real desktop sync checks passed`);
} catch (error) {
  await writeFile(output,JSON.stringify({result:'failed',checks,error:String(error)},null,2)+'\n');
  throw error;
} finally { ws.close(); }
