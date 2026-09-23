import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
import ts from 'typescript';

function application() {
  const slots = [], effects = [], requests = [], errors = [];
  let cursor = 0, tree, Component;
  const hooks = {
    useState(initial) { const i = cursor++; if (!(i in slots)) slots[i] = initial; return [slots[i], value => { slots[i] = typeof value === 'function' ? value(slots[i]) : value; }]; },
    useRef(initial) { const i = cursor++; return slots[i] ??= { current: initial }; },
    useEffect(fn, deps) { const i = cursor++, old = slots[i]; if (!old || deps.some((d, j) => d !== old[j])) { slots[i] = deps; effects.push(fn); } }
  };
  const antd = Object.fromEntries(['Alert', 'Button', 'Checkbox', 'Collapse', 'ConfigProvider', 'Drawer', 'Empty', 'InputNumber', 'Layout', 'Menu', 'Select', 'Space', 'Table', 'Tag'].map(name => [name, name]));
  const icons = new Proxy({}, { get: (_, name) => name });
  antd.Form = Object.assign(() => {}, { Item: 'Form.Item', useForm: () => [{}] });
  antd.Input = Object.assign(() => {}, { Password: 'Input.Password' });
  antd.Modal = Object.assign(() => {}, { useModal: () => [{ confirm() {} }, null] });
  antd.Typography = { Text: 'Text', Title: 'Title' };
  antd.theme = {};
  antd.message = { useMessage: () => [{ error: error => errors.push(error), success() {}, warning() {}, info() {} }, null] };
  const state = { connections: [], left: '', right: '', settings: { theme: 'light', density: 'standard', timeout: 10 }, driverAvailable: true };
  const runtime = { reply: async () => ({ ok: true, running: true }) };
  const jsx = (type, props) => ({ type, props });
  const dependencies = {
    react: hooks, antd, '@ant-design/icons': icons, 'antd/locale/zh_CN': {}, 'react/jsx-runtime': { jsx, jsxs: jsx },
    'react-dom/client': { createRoot: () => ({ render: element => { Component = element.type; } }) },
    './SchemaWorkbench': { SchemaWorkbench: 'SchemaWorkbench' }, './SyncRecords': { SyncRecords: 'SyncRecords' },
    './freshness': { Freshness: class { invalidate() { return 1; } current() { return true; } } }, './style.css': {},
    './bridge': { request: async (operation, args) => { requests.push({ operation, args }); return operation === 'snapshot' ? { ok: true, state } : runtime.reply(operation, args); } }
  };
  const code = ts.transpileModule(fs.readFileSync(new URL('../src/main.tsx', import.meta.url), 'utf8'), { compilerOptions: { module: ts.ModuleKind.CommonJS, jsx: ts.JsxEmit.ReactJSX, target: ts.ScriptTarget.ES2022 } }).outputText;
  vm.runInNewContext(code, { exports: {}, require: name => { assert.ok(name in dependencies, name); return dependencies[name]; }, document: { getElementById() {}, documentElement: { dataset: {} } }, matchMedia: () => ({ matches: false, addEventListener() {}, removeEventListener() {} }), setInterval() {}, clearInterval() {} });
  const render = () => { cursor = 0; tree = Component(); while (effects.length) effects.shift()(); };
  const nodes = (type, root = tree) => { const found = []; const walk = node => { if (!node || typeof node !== 'object') return; if (Array.isArray(node)) return node.forEach(walk); if (node.type === type) found.push(node); walk(node.props?.children); }; walk(root); return found; };
  const stop = () => nodes('Button', nodes('footer')[0]).find(node => node.props.children === '停止当前任务');
  const settle = async () => { await new Promise(resolve => setImmediate(resolve)); render(); };
  render();
  return { state, runtime, requests, errors, render, nodes, stop, settle };
}

for (const [callback, status, operation] of [['onRunning', 'sync-status', 'stop-sync'], ['onDataWriteRunning', 'merge-status', 'merge-stop']]) {
  test(`global stop survives navigation and routes ${operation} only once`, async () => {
    const app = application(); await app.settle();
    assert.equal(app.stop(), undefined);
    app.nodes('SchemaWorkbench')[0].props[callback](true); app.render();
    for (const key of ['connections', 'records']) {
      app.nodes('Menu')[0].props.onClick({ key }); app.render();
      assert.ok(app.stop(), `global stop remains on ${key}`);
      assert.deepEqual(Array.from(app.nodes('Menu')[0].props.selectedKeys), [key]);
    }
    let release;
    app.runtime.reply = name => name === status ? new Promise(resolve => { release = resolve; }) : { ok: true };
    const button = app.stop(); button.props.onClick(); button.props.onClick();
    assert.equal(app.requests.filter(request => request.operation === status).length, 1);
    release({ ok: true, running: true }); await app.settle();
    assert.equal(app.requests.filter(request => request.operation === operation).length, 1);
    app.nodes('SchemaWorkbench')[0].props[callback](false); app.render();
    assert.equal(app.stop(), undefined);
  });
}

test('global stop checks native running state and permits retry after failure', async () => {
  const app = application(); await app.settle();
  app.nodes('SchemaWorkbench')[0].props.onDataWriteRunning(true); app.render();
  app.runtime.reply = async () => ({ ok: true, running: false });
  app.stop().props.onClick(); await app.settle();
  assert.equal(app.requests.filter(request => request.operation === 'merge-stop').length, 0);
  app.runtime.reply = async name => name === 'merge-status' ? { ok: true, running: true } : { ok: false, error: 'stop failed' };
  app.stop().props.onClick(); await app.settle();
  assert.equal(app.requests.filter(request => request.operation === 'merge-stop').length, 1);
  assert.equal(app.errors.length, 1);
  app.runtime.reply = async () => ({ ok: true, running: true });
  app.stop().props.onClick(); await app.settle();
  assert.equal(app.requests.filter(request => request.operation === 'merge-stop').length, 2);
});


test('top connection tests use their own endpoint and remain independently busy', async () => {
  const app = application(); await app.settle();
  app.state.connections = [{ id: 'source', name: 'source' }, { id: 'target', name: 'target' }];
  app.state.left = 'source'; app.state.right = 'target'; app.render();
  const buttons = () => app.nodes('Button', app.nodes('SchemaWorkbench')[0].props.connectionControls).filter(node => node.props.children === '测试连接');
  const releases = [];
  app.runtime.reply = () => new Promise(resolve => releases.push(resolve));
  buttons()[0].props.onClick(); app.render();
  assert.equal(buttons()[0].props.loading, true);
  assert.ok(!buttons()[1].props.loading);
  buttons()[1].props.onClick(); app.render();
  assert.equal(buttons()[1].props.loading, true);
  assert.deepEqual(app.requests.filter(r => r.operation === 'test').map(r => [r.args.lane, r.args.connection.id]), [['left', 'source'], ['right', 'target']]);
  releases.forEach(resolve => resolve({ ok: true })); await app.settle();
  assert.equal(buttons()[0].props.loading, false);
  assert.equal(buttons()[1].props.loading, false);
  app.nodes('SchemaWorkbench')[0].props.onDataWriteRunning(true); app.render();
  assert.ok(buttons().every(button => button.props.disabled));
});
