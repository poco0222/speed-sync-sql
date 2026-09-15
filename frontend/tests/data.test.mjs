import test from 'node:test';
import assert from 'node:assert/strict';
import {dataConclusion,displayValue,exactKey,copyOriginalText,operatorsForType,filterPlaceholder,validFilter} from '../src/data.ts';
test('only complete reliable scan can conclude equal; empty sides and browse stay explicit',()=>{
 const t={state:'running',complete:false,leftScanned:0,rightScanned:0,counts:{same:0,different:0,leftOnly:0,rightOnly:0}};
 assert.equal(dataConclusion(t,false),'比对未完整');
 assert.match(dataConclusion({...t,state:'failed'},false),/未完整/);
 assert.match(dataConclusion({...t,state:'cancelled'},false),/未完整/);
 assert.match(dataConclusion({...t,state:'complete',complete:true},false),/两端范围均为空/);
 assert.match(dataConclusion({...t,state:'complete',complete:true,rightScanned:1},false),/左端范围为空/);
 assert.match(dataConclusion({...t,state:'complete',complete:true,leftScanned:1,rightScanned:1},false),/所选字段与范围相同/);
 assert.match(dataConclusion(t,true),/未匹配/);
});
test('precision and null/empty/binary presentation survive the UI',()=>{
 const v={isNull:false,text:'9007199254740993',length:16,encoding:'text',truncated:false};
 assert.equal(displayValue(v),'9007199254740993');
 assert.equal(displayValue({...v,isNull:true}),'NULL');
 assert.equal(displayValue({...v,text:''}),'（空字符串）');
 assert.equal(displayValue({...v,text:'00ff',encoding:'hex'}),'HEX: 00ff');
 assert.match(displayValue({...v,truncated:true}),/按需加载/);
 assert.deepEqual(exactKey({id:v.text,part:'',amount:'1.0000000000000001'},['id','part','amount']),[v.text,'','1.0000000000000001']);
});

test('type-aware filters restrict JSON and repair operators when fields change',()=>{
 assert.deepEqual(operatorsForType('json'),['IS NULL','IS NOT NULL']);
 assert.deepEqual(validFilter({field:'payload',op:'=',value:'123'},'JSON'),{field:'payload',op:'IS NULL',value:'123'});
 assert.equal(validFilter({field:'id',op:'>=',value:'9007199254740993'},'bigint unsigned').op,'>=');
 assert.match(filterPlaceholder('varbinary(32)'),/完整 HEX 字节/);
 assert.match(filterPlaceholder('datetime(6)'),/YYYY-MM-DD HH:mm:ss/);
 assert.equal(filterPlaceholder('date'),'YYYY-MM-DD');
 assert.equal(filterPlaceholder('time(6)'),'HH:mm:ss.ffffff');
});

test('copy event preserves full text including empty strings and always removes listener',()=>{
 const original=globalThis.document;
 let listener,removed=0,copied;
 globalThis.document={
  addEventListener(_event,fn){listener=fn;},
  removeEventListener(){removed++;},
  execCommand(){listener({preventDefault(){},stopPropagation(){},clipboardData:{clearData(){},setData(format,text){assert.equal(format,'text/plain');copied=text;}}});return true;}
 };
 try{
  const text='9007199254740993\n'+'x'.repeat(70000);
  copyOriginalText(text);assert.equal(copied,text);
  copyOriginalText('');assert.equal(copied,'');
  globalThis.document.execCommand=()=>false;
  assert.throws(()=>copyOriginalText('no'),/复制失败/);
  assert.equal(removed,3);
 }finally{globalThis.document=original;}
});
