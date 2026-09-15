import test from 'node:test';
import assert from 'node:assert/strict';

test('schema bridge preserves native statuses and bounds compare/save-dialog timeouts', async () => {
  const timers = [];
  let receive;
  const sent = [];
  const native = { response: { connect(callback) { receive = callback; } }, request(json) { sent.push(JSON.parse(json)); } };
  globalThis.window = { qt: { webChannelTransport: {} }, QWebChannel: class { constructor(_transport, connected) { connected({ objects: { foundation: native } }); } } };
  const originalSetTimeout = globalThis.setTimeout;
  const originalClearTimeout = globalThis.clearTimeout;
  globalThis.setTimeout = (_callback, delay) => { timers.push(delay); return timers.length; };
  globalThis.clearTimeout = () => {};
  try {
    const { request } = await import('../src/bridge.ts');
    const pendingCompare = request('compare', { left: { database: 'a', table: 'sample' }, right: { database: 'b', table: 'sample' } });
    await Promise.resolve();
    assert.equal(sent[0].operation, 'compare');
    assert.equal(timers.at(-1), 180000);
    receive(JSON.stringify({ requestId: sent[0].requestId, ok: true, stale: true }));
    assert.equal((await pendingCompare).stale, true);
    const pendingExport = request('export-schema');
    await Promise.resolve();
    assert.equal(timers.at(-1), 600000);
    receive(JSON.stringify({ requestId: sent[1].requestId, ok: true, cancelled: true }));
    assert.equal((await pendingExport).cancelled, true);
    const pendingList = request('schema', { action: 'tables', side: 'left', database: 'a' });
    await Promise.resolve();
    assert.equal(timers.at(-1), 75000);
    receive(JSON.stringify({ requestId: sent[2].requestId, ok: false, error: 'permission denied' }));
    assert.equal((await pendingList).error, 'permission denied');
    for (const [operation,args,delay,response] of [
      ['plan-sync',{direction:'right-to-left',selected:[{category:'columns',name:'id'}],alignAll:false},180000,{ok:false,blockers:['incoming dependency']}],
      ['execute-sync',{planId:'native-plan'},75000,{ok:true,id:'record-1'}],
      ['sync-status',{},75000,{ok:true,running:true,record:{id:'record-1',status:'running'}}],
      ['export-sync',{planId:'native-plan'},600000,{ok:true,cancelled:true}],
      ['export-sync-record',{id:'record-1'},600000,{ok:false,error:'storage failure'}]
    ]) {
      const pending=request(operation,args); await Promise.resolve();
      const last=sent.at(-1); assert.equal(last.operation,operation); assert.deepEqual(last.args,args); assert.equal(timers.at(-1),delay);
      receive(JSON.stringify({requestId:last.requestId,...response}));
      assert.deepEqual(await pending,{requestId:last.requestId,...response});
    }
  } finally {
    globalThis.setTimeout = originalSetTimeout;
    globalThis.clearTimeout = originalClearTimeout;
    delete globalThis.window;
  }
});
