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
