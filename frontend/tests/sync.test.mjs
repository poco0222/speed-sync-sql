import test from 'node:test';
import assert from 'node:assert/strict';
import { selectionReason, rowKey, selectVisible, filterRecords } from '../src/sync.ts';
const row = (category, name, status='different', properties={}) => ({category,name,status,left:{properties},right:null,changed:[]});
test('visible selection preserves other categories, excludes restricted items and clears only visible items', () => {
 const rows=[row('columns','id'),row('constraints','fk'),row('columns','generated','different',{generationExpression:'id + 1'})];
 assert.deepEqual(selectVisible(['indexes:ix'],rows,true),['indexes:ix','columns:id']);
 assert.deepEqual(selectVisible(['indexes:ix','columns:id'],rows,false),['indexes:ix']);
 assert.equal(rowKey(rows[0]),'columns:id');
});
test('unknown and unsupported syntax cannot be selected',()=>{
 for(const status of ['same','failed','unsupported','unread']) assert.ok(selectionReason(row('columns','x',status)));
 assert.ok(selectionReason(row('indexes','x','different',{expression:'lower(name)'})));
 assert.ok(selectionReason(row('indexes','x','different',{parts:[{expression:'lower(name)'}]})));
 assert.ok(selectionReason({...row('table','table'),changed:['engine']}));
 assert.equal(selectionReason(row('triggers','t')), '');
});
test('record filters combine date, status and connection without mutating records',()=>{
 const records=[{status:'passed',startedAt:'2026-09-15T10:00:00Z',left:{name:'dev',connectionId:'1'},right:{name:'test',connectionId:'2'}}];
 assert.equal(filterRecords(records,{status:'passed',connection:'DEV',from:'2026-09-15',to:'2026-09-15'}).length,1);
 assert.equal(filterRecords(records,{status:'failed',connection:'',from:'',to:''}).length,0);
 assert.equal(records.length,1);
});

test('empty status record is absent and unknown results never become success', async()=>{
 const { isSyncRecord, syncLabels, changeSummary } = await import('../src/sync.ts');
 assert.equal(isSyncRecord({}),false);
 assert.equal(isSyncRecord(null),false);
 assert.equal(isSyncRecord({id:'record-1',status:'unknown',steps:[],left:{},right:{},startedAt:'2026-09-15'}),true);
 assert.equal(syncLabels.unknown,'待核实');
 assert.equal(changeSummary({counts:{add:2,modify:1,delete:3}}),'新增 2 · 修改 1 · 删除 3');
 assert.equal(changeSummary({}), '增改删统计未提供');
});

test('same indexes and triggers can be explicitly included as dependencies without automatic selection',()=>{
 const index=row('indexes','ix','same'); const trigger=row('triggers','audit','same');
 assert.equal(selectionReason(index),''); assert.equal(selectionReason(trigger),'');
 assert.ok(selectionReason(row('columns','id','same')));
 assert.deepEqual(selectVisible([],[],true),[]);
 assert.deepEqual(selectVisible(['columns:id'],[index,trigger,row('columns','id','same')],true),['columns:id','indexes:ix','triggers:audit']);
 assert.deepEqual(selectVisible(['columns:id','indexes:ix','triggers:audit'],[index],false),['columns:id','triggers:audit']);
 assert.ok(selectionReason(row('indexes','fx','same',{parts:[{expression:'lower(name)'}]})));
});
