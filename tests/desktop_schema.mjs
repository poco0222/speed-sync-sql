// Drives the real Qt WebEngine page; invoked only by the disposable MySQL fixture.
import assert from 'node:assert/strict';
import { writeFile, mkdir } from 'node:fs/promises';
import { dirname, join } from 'node:path';
import { execFileSync, spawn } from 'node:child_process';
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
let clipboardKeeper;
let clipboardRestored;
const trustedClick = async expression => {
  const point = await evaluate(`(() => {const e=${expression}; e.scrollIntoView({block:'center'}); const r=e.getBoundingClientRect();return {x:r.x+r.width/2,y:r.y+r.height/2};})()`);
  await command('Input.dispatchMouseEvent', { type: 'mousePressed', ...point, button: 'left', clickCount: 1 });
  await command('Input.dispatchMouseEvent', { type: 'mouseReleased', ...point, button: 'left', clickCount: 1 });
};
await mkdir(dirname(output), { recursive: true });
try {
  await waitFor(`document.querySelector('#left-table')?.value==='sample' && document.querySelector('#right-table')?.value==='different'`);
  assert((await evaluate('document.body.innerText')).includes('尚未比对'));
  checks.push('restored selection without automatic comparison');
  await button('读取 / 刷新库清单', 0);
  await waitFor(`!document.querySelector('.schema-results').innerText.includes('后台执行')`);
  await button('读取 / 刷新表清单', 0);
  await waitFor(`!document.querySelector('.schema-results').innerText.includes('后台执行')`);
  await button('开始比对');
  await waitFor(`document.querySelector('.schema-results').innerText.includes('存在结构差异')`);
  assert((await evaluate('document.body.innerText')).includes('amount'));
  checks.push('catalog actions and real five-category comparison');
  await setInput('input[aria-label="搜索结构对象"]', 'no_such_object_fixture');
  await waitFor(`document.body.innerText.includes('当前分类或筛选无匹配项')`);
  assert((await evaluate('document.body.innerText')).includes('存在结构差异'));
  await setInput('input[aria-label="搜索结构对象"]', '');
  checks.push('empty search retains overall difference status');
  const fullCount = await evaluate(`document.querySelectorAll('.ant-table-tbody > tr.ant-table-row').length`);
  const summaryBefore = await evaluate(`document.querySelector('.schema-results .ant-alert').innerText`);
  await trustedClick(`document.querySelector('.schema-filters input[type=checkbox]')`);
  await waitFor(`document.querySelectorAll('.ant-table-tbody > tr.ant-table-row').length < ${fullCount}`);
  assert(!(await evaluate(`[...document.querySelectorAll('.ant-table-tbody > tr.ant-table-row .ant-tag')].map(e=>e.innerText)`)).includes('相同'));
  assert.equal(await evaluate(`document.querySelector('.schema-results .ant-alert').innerText`), summaryBefore);
  await trustedClick(`document.querySelector('.schema-filters input[type=checkbox]')`);
  await waitFor(`document.querySelectorAll('.ant-table-tbody > tr.ant-table-row').length === ${fullCount}`);
  checks.push('differences-only hides equal rows and preserves full summary');
  for (const label of ['索引', '约束', '触发器', '表属性', '原始定义']) {
    assert(await evaluate(`(() => {const t=[...document.querySelectorAll('[role=tab]')].find(t=>t.innerText.startsWith(${JSON.stringify(label)})); if(!t)return false;t.click();return true;})()`));
    await pause(80);
  }
  await waitFor(`document.querySelectorAll('.definition pre').length===2`);
  assert((await evaluate(`[...document.querySelectorAll('.definition pre')].map(e=>e.innerText)`)).every(s => s.includes('CREATE TABLE')));
  checks.push('all category tabs and both complete table definitions');
  await evaluate(`[...document.querySelectorAll('[role=tab]')].find(t=>t.innerText.startsWith('字段')).click()`);
  const screenshot = async (name, width, height, scale = 1) => {
    await command('Emulation.setDeviceMetricsOverride', { width, height, deviceScaleFactor: scale, mobile: false });
    await evaluate('window.scrollTo(0,0)');
    await pause(200);
    const geometry = await evaluate(`(() => { const b=[...document.querySelectorAll('button')].find(e=>e.innerText.trim()==='刷新比对'); const r=b.getBoundingClientRect(); return {width:innerWidth,height:innerHeight,button:{x:r.x,y:r.y,right:r.right,bottom:r.bottom},bodyWidth:document.body.scrollWidth}; })()`);
    assert(geometry.button.x >= 0 && geometry.button.right <= width && geometry.button.bottom <= height, `${name}: primary action clipped`);
    const image = await command('Page.captureScreenshot', { format: 'png' });
    await writeFile(join(dirname(output), `${name}.png`), Buffer.from(image.data, 'base64'));
    checks.push({ name, geometry });
  };
  await screenshot('d2-light-1440', 1440, 900);
  await screenshot('d2-light-1280', 1280, 800);
  await screenshot('d2-light-1024', 1024, 800, 2);
  await button('设置');
  const selectOption = async (label, option) => {
    await waitFor(`!!document.querySelector('input[aria-label="${label}"]')`);
    await evaluate(`document.querySelector('input[aria-label="${label}"]').focus()`);
    await command('Input.dispatchKeyEvent', { type: 'keyDown', key: 'ArrowDown', code: 'ArrowDown', windowsVirtualKeyCode: 40 });
    await command('Input.dispatchKeyEvent', { type: 'keyUp', key: 'ArrowDown', code: 'ArrowDown', windowsVirtualKeyCode: 40 });
    const visibleOption = `[...document.querySelectorAll('.ant-select-item-option-content')].find(e=>e.innerText===${JSON.stringify(option)} && e.getClientRects().length)`;
    await waitFor(`!!(${visibleOption})`);
    await evaluate(`(${visibleOption}).click()`);
    await waitFor(`document.querySelector('input[aria-label="${label}"]').closest('.ant-select').textContent.includes(${JSON.stringify(option)})`);
  };
  await selectOption('主题', '深色'); await selectOption('密度', '紧凑'); await button('保存设置');
  await waitFor(`document.documentElement.dataset.theme==='dark' && !document.querySelector('.ant-modal-wrap:not([style*="display: none"])')`);
  await screenshot('d2-dark-compact-1024', 1024, 800, 2);
  await setInput('#right-table', 'renamed');
  await waitFor(`document.body.innerText.includes('尚未比对')`);
  assert(await evaluate(`[...document.querySelectorAll('button')].find(b=>b.innerText==='导出 JSON 摘要').disabled`));
  await button('开始比对');
  await waitFor(`document.querySelector('.schema-results').innerText.includes('结构相同')`);
  checks.push('pair change invalidates result and identical table compares equal');
  await command('Page.reload');
  await waitFor(`document.querySelector('#right-table')?.value==='renamed' && document.body.innerText.includes('尚未比对')`);
  checks.push('reload restores pair without restoring stale results');
  await setInput('#left-table', 'long_table'); await setInput('#right-table', 'long_table');
  await button('开始比对');
  await waitFor(`document.querySelector('.schema-results').innerText.includes('存在结构差异')`);
  await evaluate(`[...document.querySelectorAll('[role=tab]')].find(t=>t.innerText==='原始定义').click()`);
  await waitFor(`document.querySelectorAll('.definition pre').length===2`);
  assert.equal(process.platform, 'darwin', 'Clipboard audit currently uses macOS pasteboard tools');
  const keeperPath = '.local/test-tools/preserve-pasteboard';
  await mkdir(dirname(keeperPath), { recursive: true });
  execFileSync('/usr/bin/swiftc', ['tests/preserve_pasteboard.swift', '-module-cache-path', '.local/swift-module-cache', '-o', keeperPath]);
  clipboardKeeper = spawn(keeperPath, [], { stdio: ['pipe', 'pipe', 'pipe'] });
  clipboardRestored = new Promise((resolve, reject) => { clipboardKeeper.on('exit', code => code === 0 ? resolve() : reject(new Error('Clipboard restoration failed'))); clipboardKeeper.on('error', reject); });
  await new Promise((resolve, reject) => { clipboardKeeper.stdout.once('data', data => String(data).trim() === 'ready' ? resolve() : reject(new Error('Clipboard preservation failed'))); clipboardKeeper.once('error', reject); clipboardKeeper.once('exit', () => reject(new Error('Clipboard preservation exited before ready'))); });
  const copyDefinitions = async (expectedPhrase, label) => {
    const definitions = await evaluate(`[...document.querySelectorAll('.definition pre')].map(e=>e.textContent)`);
    assert.equal(definitions.length, 2);
    for (let i = 0; i < 2; i++) {
      assert(definitions[i].length > 1000 && definitions[i].includes(expectedPhrase), `${label}: long definition absent`);
      await trustedClick(`document.querySelectorAll('.definition button')[${i}]`);
      let copied;
      for (let j = 0; j < 30; j++) { copied = execFileSync('/usr/bin/pbpaste', { encoding: 'utf8' }); if (copied === definitions[i]) break; await pause(50); }
      assert(copied === definitions[i], `${label}: copied text differs from full displayed definition`);
    }
    checks.push({ name: label, lengths: definitions.map(s => s.length), clipboard: 'exact text matched on both sides' });
  };
  await copyDefinitions('CREATE TABLE', 'long table definitions and real full clipboard copy');
  await evaluate(`[...document.querySelectorAll('[role=tab]')].find(t=>t.innerText.startsWith('触发器')).click()`);
  await waitFor(`!!document.querySelector('.ant-table-row-expand-icon')`);
  await trustedClick(`document.querySelector('.ant-table-row-expand-icon')`);
  await waitFor(`document.querySelectorAll('.property-detail .definition pre').length===2`);
  await copyDefinitions('TRIGGER', 'expanded long trigger definitions and real full clipboard copy');
  await writeFile(output, JSON.stringify({ checks, result: 'passed', surface: 'real Qt WebEngine with isolated MySQL' }, null, 2) + '\n');
  console.log(`${checks.length} real desktop checks passed`);
} catch (error) {
  await writeFile(output, JSON.stringify({ result: 'failed', checks, error: 'Desktop assertions failed; see current Runtime command log' }, null, 2) + '\n');
  throw error;
} finally {
  try { if (clipboardKeeper) { clipboardKeeper.stdin.end(); await clipboardRestored; } }
  catch (error) { await writeFile(output, JSON.stringify({ result: 'failed', checks, error: 'Clipboard restoration failed' }) + '\n'); throw error; }
  finally { ws.close(); }
}
